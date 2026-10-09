// Efectos de audio para el pedal. Todo es header-only y no depende del SDK,
// así que se puede probar en una PC con cualquier compilador de C++17.
//
// Las muestras son int32 con el audio justificado a la izquierda: el ADC
// entrega 24 bits en los 24 bits altos de la palabra de 32.

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace effects {

// Suma sin desbordar: si el resultado no cabe en 32 bits se queda en el
// límite en vez de dar la vuelta (que suena como un chasquido fuerte).
inline int32_t saturating_add(int32_t a, int32_t b) {
    constexpr int64_t kMax = std::numeric_limits<int32_t>::max();
    constexpr int64_t kMin = std::numeric_limits<int32_t>::min();
    const int64_t sum = static_cast<int64_t>(a) + b;
    if (sum > kMax) return static_cast<int32_t>(kMax);
    if (sum < kMin) return static_cast<int32_t>(kMin);
    return static_cast<int32_t>(sum);
}

// Fuzz con rectificador de onda completa: las mitades negativas de la onda
// se vuelven positivas. En una senoidal pura eso duplica la frecuencia (una
// octava arriba). Con una guitarra el resultado es más áspero que una octava
// limpia. La salida tiene componente DC, así que conviene acoplarla con un
// capacitor en la etapa analógica de salida.
inline int32_t octave_fuzz(int32_t x) {
    if (x == std::numeric_limits<int32_t>::min()) {
        return std::numeric_limits<int32_t>::max();  // -INT32_MIN desborda
    }
    return x < 0 ? -x : x;
}

// Filtro pasa-bajos de un polo: y[n] = y[n-1] + alpha * (x[n] - y[n-1]).
// Con alpha pequeño la frecuencia de corte es aproximadamente
// alpha * fs / (2 * pi). Con alpha = 0.05 y fs = 46875 Hz queda cerca de 370 Hz.
// Usa aritmética entera (alpha en Q15) para no depender de la FPU.
class OnePoleLowPass {
public:
    explicit OnePoleLowPass(float alpha)
        : alpha_q15_(static_cast<int32_t>(alpha * 32768.0f)) {}

    int32_t process(int32_t x) {
        const int64_t diff = static_cast<int64_t>(x) - state_;
        state_ += static_cast<int32_t>((diff * alpha_q15_) >> 15);
        return state_;
    }

private:
    int32_t alpha_q15_;
    int32_t state_ = 0;
};

// Eco con realimentación. Un buffer circular guarda la salida y la devuelve
// N muestras después, a la mitad de volumen. Como lo que se guarda es la
// mezcla ya con eco, las repeticiones se van apagando a razón de 1/2 cada vez.
//
// El buffer vive dentro del objeto: declara la instancia como `static` o
// global, no en la pila (a 46875 Hz, un cuarto de segundo ocupa unos 46 KB).
template <size_t N>
class Delay {
public:
    int32_t process(int32_t x) {
        const int32_t echo = buffer_[pos_];
        const int32_t out = saturating_add(x, echo / 2);
        buffer_[pos_] = out;
        if (++pos_ >= N) pos_ = 0;
        return out;
    }

private:
    int32_t buffer_[N] = {};
    size_t pos_ = 0;
};

}  // namespace effects
