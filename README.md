# PedalAudio

Pedal de efectos de guitarra sobre una Raspberry Pi Pico 2 W. Un PCM1808 digitaliza la señal, la CPU le aplica un efecto muestra a muestra y un PCM5102 la devuelve al amplificador. Ambos módulos van precedidos y seguidos de una etapa de buffer analógica. El bus I2S lo manejan dos programas PIO, así que la CPU solo se ocupa del audio.

## Cómo funciona

```
                 MCLK (PWM, 12 MHz)
          ┌────────────────────────────────┐
          ▼                                │
guitarra → PCM1808 ──DOUT──► PIO RX ──┐        │
          ▲                       ▼        │
          │ BCK/LRCK            CPU (efecto)
          │                       │
          └──── PIO TX ◄──────────┘
                  │
                  └──DIN──► PCM5102 → salida
```

La Pico es maestra del bus:

- Un PWM divide el reloj del sistema y genera el MCLK del ADC.
- El programa `i2s_tx` saca los datos al DAC y genera BCK y LRCK con side-set.
- El programa `i2s_rx` no genera relojes. Corre con el mismo divisor que el TX y arranca en el mismo ciclo, así que lee el pin de datos en el momento correcto sin mirar BCK.

| Reloj | Valor | Origen |
|---|---|---|
| Sistema | 120 MHz | PLL desde el cristal de 12 MHz |
| MCLK | 12 MHz | 120 MHz / 10 (PWM) |
| Frecuencia de muestreo | 46 875 Hz | MCLK / 256 |
| BCK | 3 MHz | 64 bits por trama |
| Reloj PIO | 12 MHz | 4 ciclos por bit |

Todas las divisiones son exactas. Los valores están definidos en `src/main.cpp` y hay `static_assert` que avisan al compilar si alguna deja de serlo.

Cada trama estéreo dura unos 21 µs. La guitarra entra por el canal derecho y sale por el derecho; el izquierdo sale en silencio.

## Conexiones

| Pin | Señal | Dirección |
|---|---|---|
| GPIO 18 | Datos hacia el DAC | salida |
| GPIO 19 | MCLK hacia el ADC | salida |
| GPIO 20 | BCK | salida |
| GPIO 21 | LRCK | salida |
| GPIO 22 | Datos desde el ADC | entrada |

BCK y LRCK deben ser pines consecutivos y van conectados a los dos módulos a la vez.

## Hardware

- ADC: módulo PCM1808, configurado como esclavo en formato I2S. Recibe el MCLK de 12 MHz (256 × fs) desde el GPIO 19, y BCK y LRCK desde la Pico.
- DAC: módulo PCM5102. No recibe MCLK: genera su reloj interno a partir de BCK, para lo cual su pin SCK debe ir a GND (en muchos módulos es un puente de soldadura).
- Buffers analógicos de entrada y salida: no intervienen en la parte digital. Como `OctaveFuzz` produce componente DC, la salida debe pasar por un capacitor de acople si el buffer no lo tiene ya.

## Efectos

El efecto activo se elige en `src/main.cpp`:

```cpp
constexpr Effect kActiveEffect = Effect::OctaveFuzz;
```

| Efecto | Qué hace |
|---|---|
| `Bypass` | Pasa la señal sin tocarla. Sirve para comprobar la cadena de audio. |
| `OctaveFuzz` | Rectificador de onda completa. Sube la frecuencia una octava en señales simples y suena áspero con acordes. |
| `LowPass` | Filtro de un polo, corte cercano a 370 Hz con `alpha = 0.05`. |
| `Delay` | Eco de 250 ms con realimentación a la mitad. |

Los efectos están en `src/effects.h`, sin dependencias del SDK.

## Compilar

Con la extensión de Raspberry Pi Pico para VS Code basta abrir la carpeta y usar Compile. También se puede compilar a mano con el SDK instalado (versión 2.x):

```sh
export PICO_SDK_PATH=/ruta/al/pico-sdk
mkdir build && cd build
cmake .. -DPICO_BOARD=pico2_w
make -j4
```

Para cargarlo, conecta la placa con BOOTSEL presionado y copia `build/PedalAudio.uf2` a la unidad que aparece. Con USB conectado, `printf` sale por el puerto serie virtual.

## Pruebas

Los efectos se prueban en la PC, sin placa:

```sh
g++ -std=c++17 -Wall -Wextra -I src tests/test_effects.cpp -o test_effects && ./test_effects
```

## Estructura

```
src/
  main.cpp        inicialización de relojes y PIO, bucle de audio
  effects.h       efectos
  pio/
    i2s_tx.pio    transmisor I2S y generador de BCK/LRCK
    i2s_rx.pio    receptor I2S
tests/
  test_effects.cpp
CMakeLists.txt
pico_sdk_import.cmake   copia del SDK, sin modificar
```