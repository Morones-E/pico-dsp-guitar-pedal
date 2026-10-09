cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == NO EDITAR: necesario para la extensión de Raspberry Pi Pico en VS Code ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ==========================================================================

set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Tiene que ir antes de project()
include(pico_sdk_import.cmake)

project(PedalAudio C CXX ASM)

pico_sdk_init()

add_executable(PedalAudio src/main.cpp)

pico_generate_pio_header(PedalAudio ${CMAKE_CURRENT_LIST_DIR}/src/pio/i2s_rx.pio)
pico_generate_pio_header(PedalAudio ${CMAKE_CURRENT_LIST_DIR}/src/pio/i2s_tx.pio)

pico_set_program_name(PedalAudio "PedalAudio")
pico_set_program_version(PedalAudio "0.1")

target_include_directories(PedalAudio PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

target_compile_options(PedalAudio PRIVATE -Wall -Wextra)

target_link_libraries(PedalAudio
    pico_stdlib
    hardware_clocks
    hardware_pio
    hardware_pwm
)

# Salida de printf por USB (1) o por UART (0)
pico_enable_stdio_usb(PedalAudio 1)
pico_enable_stdio_uart(PedalAudio 0)

pico_add_extra_outputs(PedalAudio)
