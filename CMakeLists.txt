cmake_minimum_required(VERSION 3.20.0)

set(BOARD ganymed_sk/sy120_gbm)
set(EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/deps/open62541/arch)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(opcua-server VERSION 1.0.0 LANGUAGES C CXX)

set_property(TARGET app PROPERTY CXX_STANDARD 17)
set_property(TARGET app PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET app PROPERTY CXX_EXTENSIONS OFF)
target_compile_options(app PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    -ffunction-sections
    -fdata-sections
    -flto
)
target_link_options(app PRIVATE -Wl,--gc-sections -flto)

target_sources(app PRIVATE app/main.cpp)
target_link_libraries(app PUBLIC open62541)
