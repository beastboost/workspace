# GBA Emulator for ESP32-P4
# Requires ESP-IDF v5.3 or later

cmake_minimum_required(VERSION 3.16)

# Set the target to ESP32-P4
set(IDF_TARGET "esp32p4")

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(gba_emulator_p4)
