# GBA Emulator Test Suite
# Can run on host (native) or on device

cmake_minimum_required(VERSION 3.16)

# Check if building for host or ESP-IDF
if(DEFINED ENV{IDF_PATH})
    # ESP-IDF build
    set(IDF_TARGET "esp32p4")
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(gba_tests)
else()
    # Host build for fast iteration
    project(gba_tests_host C)

    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g -DTEST_HOST")

    # Include paths
    include_directories(
        ${CMAKE_SOURCE_DIR}/../components/gba_core/include
        ${CMAKE_SOURCE_DIR}/../components/gba_core/src
    )

    # Stub out ESP-IDF types for host build
    add_definitions(-DESP_OK=0 -DESP_FAIL=-1)

    # Source files - include GBA core for testing
    set(GBA_CORE_SOURCES
        ../components/gba_core/src/gba.c
        ../components/gba_core/src/cpu.c
        ../components/gba_core/src/cpu_arm.c
        ../components/gba_core/src/cpu_thumb.c
        ../components/gba_core/src/memory.c
        ../components/gba_core/src/ppu.c
        ../components/gba_core/src/apu.c
        ../components/gba_core/src/dma.c
        ../components/gba_core/src/timer.c
        ../components/gba_core/src/bios.c
    )

    # Test executable
    add_executable(test_cpu
        unit/test_cpu.c
        ${GBA_CORE_SOURCES}
    )

    # Math library
    target_link_libraries(test_cpu m)

    # Add test target
    enable_testing()
    add_test(NAME cpu_tests COMMAND test_cpu)

    # Custom target to run all tests
    add_custom_target(run_tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS test_cpu
    )
endif()
