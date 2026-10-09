// pico-dsp-guitar-pedal: pedal de efectos de guitarra sobre Raspberry Pi Pico 2 W.
//
// Flujo de la señal:
//   PCM1808--(PIO RX)--> CPU (efecto) --(PIO TX)--> PCM5102
//
// La Pico es el maestro del bus: genera el MCLK del ADC con un PWM, y el PIO
// de transmisión genera BCK y LRCK. El PIO de recepción solo lee, usando esos
// mismos relojes (ver src/pio/).

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

#include "effects.h"
#include "i2s_rx.pio.h"
#include "i2s_tx.pio.h"

// ---------------------------------------------------------------------------
// Pines
// ---------------------------------------------------------------------------
constexpr uint kPinDacData = 18;  // Datos hacia el DAC (salida del PIO TX)
constexpr uint kPinMclk    = 19;  // MCLK hacia el ADC (PWM)
constexpr uint kPinBck     = 20;  // Bit clock
constexpr uint kPinLrck    = 21;  // Word clock, tiene que ser kPinBck + 1
constexpr uint kPinAdcData = 22;  // Datos desde el ADC (entrada del PIO RX)

static_assert(kPinLrck == kPinBck + 1,
              "El PIO TX maneja BCK y LRCK con side-set: deben ser pines consecutivos");

// ---------------------------------------------------------------------------
// Relojes
// ---------------------------------------------------------------------------
// Con el cristal de 12 MHz de la Pico, 120 MHz sale de una relación entera
// del PLL y todos los demás relojes se derivan de él por división exacta.
constexpr uint32_t kSysClockKhz   = 120000;
constexpr uint32_t kMclkHz        = 12'000'000;
constexpr uint32_t kMclkPerSample = 256;                        // MCLK = 256 * fs
constexpr uint32_t kSampleRateHz  = kMclkHz / kMclkPerSample;   // 46875 Hz

constexpr uint32_t kBitsPerFrame     = 64;  // 32 bits por canal, 2 canales
constexpr uint32_t kPioCyclesPerBit  = 4;   // lo fija el diseño de los .pio

static_assert((kSysClockKhz * 1000) % kMclkHz == 0,
              "El reloj del sistema debe ser múltiplo exacto del MCLK");
static_assert(kMclkHz % kMclkPerSample == 0,
              "El MCLK debe ser múltiplo exacto de la frecuencia de muestreo");

// ---------------------------------------------------------------------------
// Efecto activo: cámbialo aquí y recompila.
// ---------------------------------------------------------------------------
enum class Effect { Bypass, OctaveFuzz, LowPass, Delay };

constexpr Effect kActiveEffect = Effect::OctaveFuzz;

constexpr uint32_t kDelaySamples = kSampleRateHz / 4;  // 250 ms

static const char *effect_name(Effect e) {
    switch (e) {
        case Effect::Bypass:     return "bypass";
        case Effect::OctaveFuzz: return "octave fuzz";
        case Effect::LowPass:    return "low-pass";
        case Effect::Delay:      return "delay";
    }
    return "?";
}

// El estado de los efectos va en memoria estática. El buffer del delay pesa
// ~46 KB y en la pila no cabe.
static effects::OnePoleLowPass g_lowpass(0.05f);
static effects::Delay<kDelaySamples> g_delay;

static inline int32_t apply_effect(int32_t x) {
    switch (kActiveEffect) {
        case Effect::OctaveFuzz: return effects::octave_fuzz(x);
        case Effect::LowPass:    return g_lowpass.process(x);
        case Effect::Delay:      return g_delay.process(x);
        case Effect::Bypass:     break;
    }
    return x;
}

// ---------------------------------------------------------------------------
// Inicialización
// ---------------------------------------------------------------------------

// Genera el MCLK dividiendo el reloj del sistema con un PWM al 50 %.
static void start_master_clock(uint pin) {
    const uint32_t period = (kSysClockKhz * 1000) / kMclkHz;  // 10 ciclos

    gpio_set_function(pin, GPIO_FUNC_PWM);
    const uint slice = pwm_gpio_to_slice_num(pin);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 1.0f);
    pwm_config_set_wrap(&cfg, period - 1);
    pwm_init(slice, &cfg, true);
    pwm_set_gpio_level(pin, period / 2);
}

int main() {
    // stdio se inicializa después de cambiar el reloj para que el USB y la
    // UART arranquen ya con la configuración final.
    set_sys_clock_khz(kSysClockKhz, true);
    stdio_init_all();

    start_master_clock(kPinMclk);

    // Los dos state machines comparten el mismo divisor. Con 120 MHz da
    // exactamente 10.0, es decir, 12 MHz de reloj PIO.
    const float pio_clkdiv = static_cast<float>(clock_get_hz(clk_sys)) /
                             static_cast<float>(kSampleRateHz * kBitsPerFrame * kPioCyclesPerBit);

    constexpr uint kSmRx = 0;
    constexpr uint kSmTx = 1;
    PIO pio = pio0;

    const uint offset_rx = pio_add_program(pio, &i2s_rx_program);
    const uint offset_tx = pio_add_program(pio, &i2s_tx_program);

    i2s_rx_program_init(pio, kSmRx, offset_rx, kPinAdcData, pio_clkdiv);
    i2s_tx_program_init(pio, kSmTx, offset_tx, kPinDacData, kPinBck, pio_clkdiv);

    // El TX es quien genera BCK y LRCK. Si arranca con la FIFO vacía se
    // detiene en el primer `out` y el RX, que sí corre solo, se desfasa del
    // reloj. Se llena la FIFO con silencio para que ambos arranquen en el
    // mismo ciclo y se queden alineados.
    constexpr int kTxFifoDepth = 8;  // FIFO unida: 8 palabras = 4 tramas estéreo
    pio_sm_clear_fifos(pio, kSmRx);
    pio_sm_clear_fifos(pio, kSmTx);
    for (int i = 0; i < kTxFifoDepth; ++i) {
        pio_sm_put(pio, kSmTx, 0);
    }
    pio_enable_sm_mask_in_sync(pio, (1u << kSmRx) | (1u << kSmTx));

    printf("PedalAudio: fs = %u Hz, efecto = %s\n",
           static_cast<unsigned>(kSampleRateHz), effect_name(kActiveEffect));

    // -----------------------------------------------------------------------
    // Bucle de audio. Cada vuelta procesa una trama estéreo (21 µs a 46875 Hz).
    // La guitarra entra por el canal derecho y sale por el derecho.
    // -----------------------------------------------------------------------
    while (true) {
        pio_sm_get_blocking(pio, kSmRx);  // canal izquierdo: se descarta
        const int32_t input = static_cast<int32_t>(pio_sm_get_blocking(pio, kSmRx));

        const int32_t output = apply_effect(input);

        pio_sm_put_blocking(pio, kSmTx, 0);                              // izquierdo en silencio
        pio_sm_put_blocking(pio, kSmTx, static_cast<uint32_t>(output));  // derecho con efecto
    }
}
