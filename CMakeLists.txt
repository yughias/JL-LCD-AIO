cmake_minimum_required(VERSION 3.16)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(jl_lcd VERSION 0.0.2 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_APP "Build the Raylib AIO Monitor Application (lcd_aio)" ON)
option(BUILD_DRIVER_EXAMPLES "Build standalone C driver examples" ON)

# Synchronize if user passed -DBUILD_EXAMPLES=ON/OFF
if(DEFINED BUILD_EXAMPLES AND NOT DEFINED BUILD_DRIVER_EXAMPLES)
    set(BUILD_DRIVER_EXAMPLES ${BUILD_EXAMPLES})
endif()

# Output all executables directly into the root build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Build standalone driver examples
if(BUILD_DRIVER_EXAMPLES)
    set(DRIVER_EXAMPLES
        examples/aspect_ratio.c
        examples/benchmark.c
        examples/color_wave.c
        examples/dvd_logo.c
        examples/gradient.c
        examples/sine_wave.c
    )

    foreach(EXAMPLE_SRC ${DRIVER_EXAMPLES})
        get_filename_component(EXAMPLE_NAME ${EXAMPLE_SRC} NAME_WE)
        add_executable(${EXAMPLE_NAME} ${EXAMPLE_SRC})
        target_include_directories(${EXAMPLE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(${EXAMPLE_NAME} PRIVATE ole32 oleaut32 setupapi)
    endforeach()
endif()

# Build the main AIO application
if(BUILD_APP)
    add_subdirectory(lcd_aio)
endif()
