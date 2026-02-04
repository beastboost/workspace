idf_component_register(
    SRCS
        "src/gba.c"
        "src/cpu.c"
        "src/cpu_arm.c"
        "src/cpu_thumb.c"
        "src/memory.c"
        "src/ppu.c"
        "src/apu.c"
        "src/dma.c"
        "src/timer.c"
        "src/bios.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer
)

# Enable optimizations for the emulator core
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -ffast-math
    -funroll-loops
)
