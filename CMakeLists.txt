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

project(ka_keyer C CXX ASM)

pico_sdk_init()

add_executable(ka_keyer
    main.c
    keyer.c
    winkeyer.c
    display.c
    encoder.c
    flash_settings.c
)

target_include_directories(ka_keyer PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(ka_keyer
    pico_stdlib
    hardware_gpio
    hardware_i2c
    hardware_pwm
    hardware_flash
    hardware_sync
)

pico_enable_stdio_usb(ka_keyer 1)
pico_enable_stdio_uart(ka_keyer 0)

pico_add_extra_outputs(ka_keyer)
pico_set_program_name(ka_keyer "ka_keyer")
pico_set_program_version(ka_keyer "0.1")
