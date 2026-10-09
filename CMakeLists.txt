# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(mon_keyer C CXX ASM)

pico_sdk_init()

add_executable(mon_keyer
    main.c
    keyer.c
    winkeyer.c
    display.c
    encoder.c
    flash_settings.c
    usb_device.c
    usb_audio.c
    usb_audio_hw.c
    usb_descriptors.c
)

target_include_directories(mon_keyer PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(mon_keyer
    pico_stdlib
    hardware_gpio
    hardware_i2c
    hardware_pwm
    hardware_flash
    hardware_sync
    pico_multicore
    pico_flash
    pico_unique_id
    tinyusb_device
)

# USB is handled by our own TinyUSB composite device (CDC + audio + MIDI, see
# usb_device.c / usb_descriptors.c), not by the SDK's stdio over USB.
pico_enable_stdio_usb(mon_keyer 0)
pico_enable_stdio_uart(mon_keyer 0)

pico_add_extra_outputs(mon_keyer)
pico_set_program_name(mon_keyer "mon-keyer")
pico_set_program_version(mon_keyer "0.1")
