cmake_minimum_required(VERSION 3.12)

# plan2040: one source tree, one firmware per config (configs/<name>.cmake).
# The config is read first, before the SDK, so a config can set PICO_BOARD
# (a Pico W build will need that).
set(PLAN2040_CONFIG cpu_rp2040 CACHE STRING "configs/<name>.cmake to build")
set(PLAN2040_CONFIG_FILE ${CMAKE_CURRENT_SOURCE_DIR}/configs/${PLAN2040_CONFIG}.cmake)
if(NOT EXISTS ${PLAN2040_CONFIG_FILE})
    message(FATAL_ERROR "no such config: ${PLAN2040_CONFIG_FILE}")
endif()
include(${PLAN2040_CONFIG_FILE})
set(PLAN2040_BOARD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/boards/${PLAN2040_BOARD})
if(NOT EXISTS ${PLAN2040_BOARD_DIR}/board.h)
    message(FATAL_ERROR "config ${PLAN2040_CONFIG}: no board ${PLAN2040_BOARD_DIR}/board.h")
endif()
message(STATUS "plan2040: config ${PLAN2040_CONFIG}, board ${PLAN2040_BOARD}, app ${PLAN2040_APP}")

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(plan2040 C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function
        -Wno-stringop-truncation # shell.cpp's strncpy(dst, src, N-1) + explicit '\0' is the
                                 # intended truncate-and-terminate idiom, not a bug. picoos
                                 # never saw this: its add_compile_options came after its
                                 # targets, so -Wall wasn't actually applied there.
)
# The config's sizes, for every file (a header's default applies otherwise).
add_compile_definitions(${PLAN2040_SIZES})

# Every file includes from the repo root ("kernel/fs.h", "drivers/...")
# and gets the board's own "board.h".
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${PLAN2040_BOARD_DIR})
# An app (PLAN2040_APP, optional): apps/<app>/{dev,prog,scripts}/ next
# to the generic ones, its headers found from apps/<app>/.
if(PLAN2040_APP)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/apps/${PLAN2040_APP})
        message(FATAL_ERROR "config ${PLAN2040_CONFIG}: no app apps/${PLAN2040_APP}")
    endif()
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/apps/${PLAN2040_APP})
endif()

# board_drivers: the board and its chips, shared by bringup and plan2040
# so both talk to the hardware the same way.
set(drv_src ${PLAN2040_BOARD_DIR}/board.cpp)
foreach(n ${PLAN2040_DRIVERS})
    # drivers/<name>.cpp, or a driver with files of its own (a copied-in
    # library with its LICENSE): drivers/<name>/<name>.cpp
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/drivers/${n}.cpp)
        list(APPEND drv_src drivers/${n}.cpp)
    elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/drivers/${n}/${n}.cpp)
        list(APPEND drv_src drivers/${n}/${n}.cpp)
    else()
        message(FATAL_ERROR "config ${PLAN2040_CONFIG}: no driver drivers/${n}.cpp or drivers/${n}/${n}.cpp")
    endif()
endforeach()
add_library(board_drivers ${drv_src})
target_link_libraries(board_drivers pico_stdlib hardware_i2c hardware_gpio)

# plan2040_kernel: the same on every board: scheduler, fs, programs
# table, klog, the shell and its jobs.
add_library(plan2040_kernel
    kernel/task.cpp
    kernel/klog.cpp
    kernel/fs.cpp
    kernel/prog.cpp
    kernel/shell.cpp
    kernel/jobs.cpp
)
target_compile_definitions(plan2040_kernel PRIVATE PLAN2040_BOARD="${PLAN2040_BOARD}")
target_link_libraries(plan2040_kernel pico_stdlib hardware_watchdog)

# bringup: the board's standalone test (no plan2040 kernel). One for
# every board, boards/bringup.cpp, testing what board.h describes; a
# board can bring its own as boards/<board>/bringup.cpp.
if(EXISTS ${PLAN2040_BOARD_DIR}/bringup.cpp)
    set(bringup_src ${PLAN2040_BOARD_DIR}/bringup.cpp)
else()
    set(bringup_src boards/bringup.cpp)
endif()
add_executable(bringup ${bringup_src})
target_compile_definitions(bringup PRIVATE PLAN2040_BOARD="${PLAN2040_BOARD}")
target_link_libraries(bringup board_drivers pico_stdlib hardware_i2c)
pico_enable_stdio_usb(bringup 1)
pico_enable_stdio_uart(bringup 0)
pico_add_extra_outputs(bringup)

# Where a name from the config lives: the app's own directory first
# (apps/<app>/<kind>/), then the generic one (<kind>/). An app's file of
# the same name shadows the generic one, like a Plan 9 bind.
function(plan2040_find kind name ext out)
    set(app ${CMAKE_CURRENT_SOURCE_DIR}/apps/${PLAN2040_APP}/${kind}/${name}${ext})
    set(gen ${CMAKE_CURRENT_SOURCE_DIR}/${kind}/${name}${ext})
    if(PLAN2040_APP AND EXISTS ${app})
        if(EXISTS ${gen})
            message(STATUS "plan2040: ${kind}/${name}${ext} from apps/${PLAN2040_APP} (shadows the generic one)")
        endif()
        set(${out} ${app} PARENT_SCOPE)
    elseif(EXISTS ${gen})
        set(${out} ${gen} PARENT_SCOPE)
    else()
        message(FATAL_ERROR "config ${PLAN2040_CONFIG}: no ${kind}/${name}${ext}"
                            " (looked in apps/${PLAN2040_APP}/${kind}/ and ${kind}/)")
    endif()
endfunction()

# conf.cpp: generated from the config's lists, like Plan 9's mkdevc.
# Written only when it changes, so a re-run of cmake doesn't rebuild it.
set(conf "// conf.cpp: generated by CMakeLists.txt from configs/${PLAN2040_CONFIG}.cmake. Don't edit.\n\n")
set(conf_calls "")
set(os_src kernel/main.cpp)
foreach(n ${PLAN2040_DEVS})
    plan2040_find(dev ${n} .cpp f)
    list(APPEND os_src ${f})
    string(APPEND conf "void dev_${n}_register(void);\n")
    string(APPEND conf_calls "    dev_${n}_register();\n")
endforeach()
foreach(n ${PLAN2040_PROGS})
    plan2040_find(prog ${n} .cpp f)
    list(APPEND os_src ${f})
    string(APPEND conf "void prog_${n}_register(void);\n")
    string(APPEND conf_calls "    prog_${n}_register();\n")
endforeach()
foreach(n ${PLAN2040_FS})
    list(APPEND os_src fs/${n}.cpp)
endforeach()
# An app's other sources (not devices or programs: a screen, a table),
# relative to apps/<app>/.
foreach(f ${PLAN2040_APP_SRC})
    list(APPEND os_src apps/${PLAN2040_APP}/${f})
endforeach()
string(APPEND conf "\nvoid conf_register(void)\n{\n${conf_calls}}\n")

# The built-in scripts: plain files, scripts/<name> or apps/<app>/scripts/<name>,
# embedded as C strings. Comment lines and empty lines are left out (the
# script buffer is 1 KB, kernel/shell.cpp SCRIPT_TEXT_MAX); `boot` is a must.
if(NOT "boot" IN_LIST PLAN2040_SCRIPTS)
    message(FATAL_ERROR "config ${PLAN2040_CONFIG}: PLAN2040_SCRIPTS must have a boot script")
endif()
string(APPEND conf "\nstruct conf_script { const char *name; const char *text; };\n")
string(APPEND conf "extern const conf_script conf_scripts[];\nextern const int conf_nscripts;\n\n")
string(APPEND conf "const conf_script conf_scripts[] = {\n")
list(LENGTH PLAN2040_SCRIPTS nscripts)
foreach(n ${PLAN2040_SCRIPTS})
    plan2040_find(scripts ${n} "" f)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${f})
    file(READ ${f} txt)
    string(REGEX REPLACE "(^|\n)[ \t]*#[^\n]*" "\\1" txt "${txt}")   # comment lines
    string(REGEX REPLACE "\n[ \t]*\n" "\n" txt "${txt}")              # empty lines
    string(REGEX REPLACE "\n[ \t]*\n" "\n" txt "${txt}")
    string(REGEX REPLACE "^[ \t]*\n" "" txt "${txt}")
    if(NOT txt MATCHES "\n$")
        string(APPEND txt "\n")
    endif()
    string(LENGTH "${txt}" len)
    if(len GREATER 1023)
        message(FATAL_ERROR "script ${f}: ${len} bytes without comments, at most 1023 fit")
    endif()
    string(REPLACE "\\" "\\\\" txt "${txt}")
    string(REPLACE "\"" "\\\"" txt "${txt}")
    string(REPLACE "\n" "\\n\"\n        \"" txt "${txt}")
    string(APPEND conf "    { \"${n}\",\n        \"${txt}\" },\n")
endforeach()
string(APPEND conf "};\nconst int conf_nscripts = ${nscripts};\n")

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/conf.cpp.new "${conf}")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/conf.cpp.new ${CMAKE_CURRENT_BINARY_DIR}/conf.cpp COPYONLY)

# plan2040: the OS, as the config describes it.
add_executable(plan2040 ${os_src} ${CMAKE_CURRENT_BINARY_DIR}/conf.cpp)
target_link_libraries(plan2040 plan2040_kernel board_drivers pico_stdlib
    hardware_watchdog ${PLAN2040_LIBS})
# UART stdio off: its default pins are GPIO0/1, and GPIO1 is the LED.
pico_enable_stdio_usb(plan2040 1)
pico_enable_stdio_uart(plan2040 0)
pico_add_extra_outputs(plan2040)
