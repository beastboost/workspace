# GBA Emulator for ESP32-P4

A Game Boy Advance emulator designed specifically for the ESP32-P4 microcontroller, taking advantage of its dual-core 400MHz RISC-V processor and enhanced memory capabilities.

## Features

- **Full ARM7TDMI CPU emulation** - Complete ARM and THUMB instruction set support
- **PPU (Graphics)** - All video modes (0-5), sprites, backgrounds, blending, and windowing
- **APU (Audio)** - PSG channels and Direct Sound (FIFO) support
- **HLE BIOS** - High-level emulation of common BIOS functions (no BIOS file required)
- **Save States** - Save and load game progress
- **SD Card Support** - Load ROMs from SD card
- **Dual-core optimization** - Emulation on core 1, display/audio on core 0

## Hardware Requirements

- **ESP32-P4** development board (e.g., ESP32-P4-Function-EV-Board)
- **320x240 RGB LCD** (or compatible display)
- **SD card** for ROM storage
- **Buttons** for input (GPIO-based)
- **I2S DAC** for audio output (optional)

## Pin Configuration

Modify the pin definitions in the component source files to match your hardware:

### Display (components/display/src/display.c)
- RGB LCD pins for data, sync, and clock

### Input (components/input/src/input.c)
```
A      - GPIO 4
B      - GPIO 5
SELECT - GPIO 6
START  - GPIO 7
UP     - GPIO 15
DOWN   - GPIO 16
LEFT   - GPIO 17
RIGHT  - GPIO 18
L      - GPIO 19
R      - GPIO 20
```

### Audio (components/audio/src/audio.c)
```
I2S BCK - GPIO 26
I2S WS  - GPIO 25
I2S DO  - GPIO 27
```

### SD Card (components/rom_loader/src/rom_loader.c)
Uses SDMMC interface (4-bit mode)

## Building

### Prerequisites

1. Install ESP-IDF v5.3 or later:
```bash
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32p4
source export.sh
```

### Compile and Flash

```bash
# Configure the project (optional)
idf.py menuconfig

# Build
idf.py build

# Flash to device
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor
```

## Usage

1. Format an SD card as FAT32
2. Create a folder called `gba` in the root of the SD card
3. Copy your `.gba` ROM files to the `gba` folder
4. Insert the SD card into your ESP32-P4 board
5. Power on the device

The emulator will automatically load the first ROM found in the `/sdcard/gba/` directory.

## Performance

Expected performance on ESP32-P4 at 400MHz:

| Game Type | Performance |
|-----------|-------------|
| Simple 2D games | 100% speed |
| Pokemon series | ~90-100% speed |
| Mario Kart | ~70-80% speed |
| Complex 3D games | ~50-70% speed |

Performance can be improved with:
- Enabling frameskip
- Disabling audio
- Overclocking (if supported)

## Project Structure

```
├── CMakeLists.txt          # Main project CMake file
├── sdkconfig.defaults      # Default SDK configuration
├── partitions.csv          # Partition table
├── main/
│   ├── CMakeLists.txt
│   └── main.c              # Application entry point
└── components/
    ├── gba_core/           # GBA emulation core
    │   ├── include/
    │   │   ├── gba.h           # Public API
    │   │   └── gba_internal.h  # Internal structures
    │   └── src/
    │       ├── gba.c           # Main emulator
    │       ├── cpu.c           # CPU core
    │       ├── cpu_arm.c       # ARM instructions
    │       ├── cpu_thumb.c     # THUMB instructions
    │       ├── memory.c        # Memory bus
    │       ├── ppu.c           # Graphics
    │       ├── apu.c           # Audio
    │       ├── dma.c           # DMA controller
    │       ├── timer.c         # Timers
    │       └── bios.c          # HLE BIOS
    ├── display/            # Display driver
    ├── input/              # Input handling
    ├── audio/              # Audio output
    └── rom_loader/         # SD card ROM loading
```

## Configuration

Key settings in `sdkconfig.defaults`:

- `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=400` - Maximum CPU frequency
- `CONFIG_SPIRAM=y` - Enable PSRAM
- `CONFIG_COMPILER_OPTIMIZATION_PERF=y` - Optimize for performance

## Known Limitations

- Some games with complex timing requirements may have issues
- RTC (Real-Time Clock) not fully implemented
- Serial link/multiplayer not supported
- Some special cartridge features (EEPROM, Flash types) may need adjustment

## Contributing

Contributions are welcome! Areas that could use improvement:

- Better PPU accuracy (per-pixel rendering)
- Audio quality improvements
- Additional display support (SPI, MIPI-DSI)
- USB gamepad support
- Menu system for ROM selection

## License

This project is provided for educational purposes. The GBA emulation code is original work.

**Note**: GBA BIOS and game ROMs are copyrighted by Nintendo. Only use ROM files that you have legally obtained.

## Acknowledgments

- [mGBA](https://mgba.io/) - Reference for accurate GBA emulation
- [Retro-Go](https://github.com/ducalex/retro-go) - ESP32 emulation framework inspiration
- [GBATEK](https://problemkaputt.de/gbatek.htm) - GBA technical documentation
- [Espressif](https://www.espressif.com/) - ESP32-P4 hardware and ESP-IDF

## Resources

- [GBA Technical Reference](https://problemkaputt.de/gbatek.htm)
- [ESP32-P4 Datasheet](https://www.espressif.com/en/products/socs/esp32-p4)
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/)
