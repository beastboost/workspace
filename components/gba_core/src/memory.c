/**
 * GBA Emulator Core - Memory Bus Implementation
 */

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "gba_internal.h"

static const char *TAG = "GBA_MEM";

void memory_init(gba_t *gba)
{
    // Allocate BIOS in internal RAM
    gba->mem.bios = heap_caps_calloc(1, BIOS_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    // Allocate work RAM - try PSRAM first
    gba->mem.ewram = heap_caps_calloc(1, EWRAM_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (gba->mem.ewram == NULL) {
        gba->mem.ewram = heap_caps_calloc(1, EWRAM_SIZE, MALLOC_CAP_DEFAULT);
    }

    // Internal work RAM - keep in internal memory for speed
    gba->mem.iwram = heap_caps_calloc(1, IWRAM_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    // Palette RAM
    gba->mem.palette = heap_caps_calloc(1, PALETTE_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    // VRAM - try PSRAM
    gba->mem.vram = heap_caps_calloc(1, VRAM_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (gba->mem.vram == NULL) {
        gba->mem.vram = heap_caps_calloc(1, VRAM_SIZE, MALLOC_CAP_DEFAULT);
    }

    // OAM
    gba->mem.oam = heap_caps_calloc(1, OAM_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    // SRAM
    gba->mem.sram = heap_caps_calloc(1, SRAM_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (gba->mem.sram == NULL) {
        gba->mem.sram = heap_caps_calloc(1, SRAM_SIZE, MALLOC_CAP_DEFAULT);
    }
    gba->mem.sram_size = SRAM_SIZE;

    gba->mem.bios_loaded = false;
    gba->mem.rom = NULL;
    gba->mem.rom_size = 0;

    ESP_LOGI(TAG, "Memory initialized");
}

void memory_free(gba_t *gba)
{
    if (gba->mem.bios) free(gba->mem.bios);
    if (gba->mem.ewram) free(gba->mem.ewram);
    if (gba->mem.iwram) free(gba->mem.iwram);
    if (gba->mem.palette) free(gba->mem.palette);
    if (gba->mem.vram) free(gba->mem.vram);
    if (gba->mem.oam) free(gba->mem.oam);
    if (gba->mem.rom) free(gba->mem.rom);
    if (gba->mem.sram) free(gba->mem.sram);

    memset(&gba->mem, 0, sizeof(memory_state_t));
}

void memory_reset(gba_t *gba)
{
    // Clear work RAM
    memset(gba->mem.ewram, 0, EWRAM_SIZE);
    memset(gba->mem.iwram, 0, IWRAM_SIZE);
    memset(gba->mem.io, 0, IO_SIZE);
    memset(gba->mem.palette, 0, PALETTE_SIZE);
    memset(gba->mem.vram, 0, VRAM_SIZE);
    memset(gba->mem.oam, 0, OAM_SIZE);

    gba->mem.waitcnt = 0;
}

// IO Register read handler
static uint8_t io_read8(gba_t *gba, uint32_t addr)
{
    addr &= 0x3FF;

    switch (addr) {
        // Display registers
        case 0x000: return gba->ppu.dispcnt & 0xFF;
        case 0x001: return gba->ppu.dispcnt >> 8;
        case 0x004: return gba->ppu.dispstat & 0xFF;
        case 0x005: return gba->ppu.dispstat >> 8;
        case 0x006: return gba->ppu.vcount & 0xFF;
        case 0x007: return gba->ppu.vcount >> 8;

        // Key input
        case 0x130: return gba->keyinput & 0xFF;
        case 0x131: return gba->keyinput >> 8;

        // Interrupt registers
        case 0x200: return gba->ie & 0xFF;
        case 0x201: return gba->ie >> 8;
        case 0x202: return gba->if_ & 0xFF;
        case 0x203: return gba->if_ >> 8;
        case 0x208: return gba->ime & 0xFF;
        case 0x209: return gba->ime >> 8;

        default:
            return gba->mem.io[addr];
    }
}

static uint16_t io_read16(gba_t *gba, uint32_t addr)
{
    return io_read8(gba, addr) | (io_read8(gba, addr + 1) << 8);
}

static uint32_t io_read32(gba_t *gba, uint32_t addr)
{
    return io_read16(gba, addr) | (io_read16(gba, addr + 2) << 16);
}

// IO Register write handler
static void io_write8(gba_t *gba, uint32_t addr, uint8_t value)
{
    addr &= 0x3FF;

    switch (addr) {
        // Display control
        case 0x000:
            gba->ppu.dispcnt = (gba->ppu.dispcnt & 0xFF00) | value;
            break;
        case 0x001:
            gba->ppu.dispcnt = (gba->ppu.dispcnt & 0x00FF) | (value << 8);
            break;

        // Display status
        case 0x004:
            gba->ppu.dispstat = (gba->ppu.dispstat & 0xFF07) | (value & 0xF8);
            break;
        case 0x005:
            gba->ppu.dispstat = (gba->ppu.dispstat & 0x00FF) | (value << 8);
            break;

        // Interrupt enable
        case 0x200:
            gba->ie = (gba->ie & 0xFF00) | value;
            break;
        case 0x201:
            gba->ie = (gba->ie & 0x00FF) | (value << 8);
            break;

        // Interrupt flags (write 1 to acknowledge)
        case 0x202:
            gba->if_ &= ~value;
            break;
        case 0x203:
            gba->if_ &= ~(value << 8);
            break;

        // Interrupt master enable
        case 0x208:
            gba->ime = value & 1;
            break;

        // Halt control
        case 0x301:
            if (value & 0x80) {
                // STOP mode - not implemented
            } else {
                // HALT mode
                gba->cpu.halted = true;
            }
            break;

        default:
            gba->mem.io[addr] = value;
            break;
    }
}

static void io_write16(gba_t *gba, uint32_t addr, uint16_t value)
{
    addr &= 0x3FE;

    // Handle specific 16-bit writes
    switch (addr) {
        case 0x000: gba->ppu.dispcnt = value; return;
        case 0x004:
            gba->ppu.dispstat = (gba->ppu.dispstat & 0x07) | (value & 0xFFF8);
            return;
        case 0x008: gba->ppu.bgcnt[0] = value; return;
        case 0x00A: gba->ppu.bgcnt[1] = value; return;
        case 0x00C: gba->ppu.bgcnt[2] = value; return;
        case 0x00E: gba->ppu.bgcnt[3] = value; return;
        case 0x010: gba->ppu.bghofs[0] = value & 0x1FF; return;
        case 0x012: gba->ppu.bgvofs[0] = value & 0x1FF; return;
        case 0x014: gba->ppu.bghofs[1] = value & 0x1FF; return;
        case 0x016: gba->ppu.bgvofs[1] = value & 0x1FF; return;
        case 0x018: gba->ppu.bghofs[2] = value & 0x1FF; return;
        case 0x01A: gba->ppu.bgvofs[2] = value & 0x1FF; return;
        case 0x01C: gba->ppu.bghofs[3] = value & 0x1FF; return;
        case 0x01E: gba->ppu.bgvofs[3] = value & 0x1FF; return;

        // BG2 affine
        case 0x020: gba->ppu.bgpa[0] = value; return;
        case 0x022: gba->ppu.bgpb[0] = value; return;
        case 0x024: gba->ppu.bgpc[0] = value; return;
        case 0x026: gba->ppu.bgpd[0] = value; return;

        // BG3 affine
        case 0x030: gba->ppu.bgpa[1] = value; return;
        case 0x032: gba->ppu.bgpb[1] = value; return;
        case 0x034: gba->ppu.bgpc[1] = value; return;
        case 0x036: gba->ppu.bgpd[1] = value; return;

        // Window
        case 0x040: gba->ppu.win0h = value; return;
        case 0x042: gba->ppu.win1h = value; return;
        case 0x044: gba->ppu.win0v = value; return;
        case 0x046: gba->ppu.win1v = value; return;
        case 0x048: gba->ppu.winin = value; return;
        case 0x04A: gba->ppu.winout = value; return;
        case 0x04C: gba->ppu.mosaic = value; return;

        // Blending
        case 0x050: gba->ppu.bldcnt = value; return;
        case 0x052: gba->ppu.bldalpha = value; return;
        case 0x054: gba->ppu.bldy = value & 0x1F; return;

        // Interrupts
        case 0x200: gba->ie = value; return;
        case 0x202: gba->if_ &= ~value; return;
        case 0x208: gba->ime = value & 1; return;

        // Timers
        case 0x100: gba->timer[0].reload = value; return;
        case 0x102:
            gba->timer[0].control = value;
            if (value & 0x80) gba->timer[0].counter = gba->timer[0].reload;
            return;
        case 0x104: gba->timer[1].reload = value; return;
        case 0x106:
            gba->timer[1].control = value;
            if (value & 0x80) gba->timer[1].counter = gba->timer[1].reload;
            return;
        case 0x108: gba->timer[2].reload = value; return;
        case 0x10A:
            gba->timer[2].control = value;
            if (value & 0x80) gba->timer[2].counter = gba->timer[2].reload;
            return;
        case 0x10C: gba->timer[3].reload = value; return;
        case 0x10E:
            gba->timer[3].control = value;
            if (value & 0x80) gba->timer[3].counter = gba->timer[3].reload;
            return;

        // DMA
        case 0x0BA: gba->dma[0].control = value; dma_trigger(gba, 0); return;
        case 0x0C6: gba->dma[1].control = value; dma_trigger(gba, 0); return;
        case 0x0D2: gba->dma[2].control = value; dma_trigger(gba, 0); return;
        case 0x0DE: gba->dma[3].control = value; dma_trigger(gba, 0); return;
    }

    io_write8(gba, addr, value & 0xFF);
    io_write8(gba, addr + 1, value >> 8);
}

static void io_write32(gba_t *gba, uint32_t addr, uint32_t value)
{
    addr &= 0x3FC;

    // Handle BG reference points (28-bit values)
    switch (addr) {
        case 0x028: // BG2X
            gba->ppu.bgx[0] = (int32_t)(value << 4) >> 4;
            gba->ppu.bgx_ref[0] = gba->ppu.bgx[0];
            return;
        case 0x02C: // BG2Y
            gba->ppu.bgy[0] = (int32_t)(value << 4) >> 4;
            gba->ppu.bgy_ref[0] = gba->ppu.bgy[0];
            return;
        case 0x038: // BG3X
            gba->ppu.bgx[1] = (int32_t)(value << 4) >> 4;
            gba->ppu.bgx_ref[1] = gba->ppu.bgx[1];
            return;
        case 0x03C: // BG3Y
            gba->ppu.bgy[1] = (int32_t)(value << 4) >> 4;
            gba->ppu.bgy_ref[1] = gba->ppu.bgy[1];
            return;

        // DMA source/dest
        case 0x0B0: gba->dma[0].src = value & 0x07FFFFFF; return;
        case 0x0B4: gba->dma[0].dst = value & 0x07FFFFFF; return;
        case 0x0BC: gba->dma[1].src = value & 0x0FFFFFFF; return;
        case 0x0C0: gba->dma[1].dst = value & 0x07FFFFFF; return;
        case 0x0C8: gba->dma[2].src = value & 0x0FFFFFFF; return;
        case 0x0CC: gba->dma[2].dst = value & 0x07FFFFFF; return;
        case 0x0D4: gba->dma[3].src = value & 0x0FFFFFFF; return;
        case 0x0D8: gba->dma[3].dst = value & 0x0FFFFFFF; return;
    }

    io_write16(gba, addr, value & 0xFFFF);
    io_write16(gba, addr + 2, value >> 16);
}

// Main memory read functions
uint8_t memory_read8(gba_t *gba, uint32_t addr)
{
    switch ((addr >> 24) & 0xFF) {
        case 0x00:  // BIOS
            if (addr < BIOS_SIZE) {
                return gba->mem.bios[addr];
            }
            break;

        case 0x02:  // EWRAM (256 KB, mirrored)
            return gba->mem.ewram[addr & (EWRAM_SIZE - 1)];

        case 0x03:  // IWRAM (32 KB, mirrored)
            return gba->mem.iwram[addr & (IWRAM_SIZE - 1)];

        case 0x04:  // IO Registers
            return io_read8(gba, addr);

        case 0x05:  // Palette RAM (1 KB, mirrored)
            return gba->mem.palette[addr & (PALETTE_SIZE - 1)];

        case 0x06:  // VRAM (96 KB, mirrored)
            addr &= 0x1FFFF;
            if (addr >= VRAM_SIZE) addr -= 0x8000;
            return gba->mem.vram[addr];

        case 0x07:  // OAM (1 KB, mirrored)
            return gba->mem.oam[addr & (OAM_SIZE - 1)];

        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C:
        case 0x0D:  // ROM
            addr &= 0x01FFFFFF;
            if (addr < gba->mem.rom_size) {
                return gba->mem.rom[addr];
            }
            return (addr >> 1) & 0xFF;  // Open bus

        case 0x0E:
        case 0x0F:  // SRAM
            return gba->mem.sram[addr & (SRAM_SIZE - 1)];
    }

    return 0;
}

uint16_t memory_read16(gba_t *gba, uint32_t addr)
{
    addr &= ~1;  // Align

    switch ((addr >> 24) & 0xFF) {
        case 0x00:
            if (addr < BIOS_SIZE) {
                return *(uint16_t*)&gba->mem.bios[addr];
            }
            break;

        case 0x02:
            return *(uint16_t*)&gba->mem.ewram[addr & (EWRAM_SIZE - 1)];

        case 0x03:
            return *(uint16_t*)&gba->mem.iwram[addr & (IWRAM_SIZE - 1)];

        case 0x04:
            return io_read16(gba, addr);

        case 0x05:
            return *(uint16_t*)&gba->mem.palette[addr & (PALETTE_SIZE - 1)];

        case 0x06: {
            uint32_t vram_addr = addr & 0x1FFFF;
            if (vram_addr >= VRAM_SIZE) vram_addr -= 0x8000;
            return *(uint16_t*)&gba->mem.vram[vram_addr];
        }

        case 0x07:
            return *(uint16_t*)&gba->mem.oam[addr & (OAM_SIZE - 1)];

        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C:
        case 0x0D: {
            uint32_t rom_addr = addr & 0x01FFFFFF;
            if (rom_addr < gba->mem.rom_size) {
                return *(uint16_t*)&gba->mem.rom[rom_addr];
            }
            return (rom_addr >> 1) & 0xFFFF;
        }

        case 0x0E:
        case 0x0F:
            return gba->mem.sram[addr & (SRAM_SIZE - 1)] * 0x0101;
    }

    return 0;
}

uint32_t memory_read32(gba_t *gba, uint32_t addr)
{
    addr &= ~3;  // Align

    switch ((addr >> 24) & 0xFF) {
        case 0x00:
            if (addr < BIOS_SIZE) {
                return *(uint32_t*)&gba->mem.bios[addr];
            }
            break;

        case 0x02:
            return *(uint32_t*)&gba->mem.ewram[addr & (EWRAM_SIZE - 1)];

        case 0x03:
            return *(uint32_t*)&gba->mem.iwram[addr & (IWRAM_SIZE - 1)];

        case 0x04:
            return io_read32(gba, addr);

        case 0x05:
            return *(uint32_t*)&gba->mem.palette[addr & (PALETTE_SIZE - 1)];

        case 0x06: {
            uint32_t vram_addr = addr & 0x1FFFF;
            if (vram_addr >= VRAM_SIZE) vram_addr -= 0x8000;
            return *(uint32_t*)&gba->mem.vram[vram_addr];
        }

        case 0x07:
            return *(uint32_t*)&gba->mem.oam[addr & (OAM_SIZE - 1)];

        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C:
        case 0x0D: {
            uint32_t rom_addr = addr & 0x01FFFFFF;
            if (rom_addr < gba->mem.rom_size) {
                return *(uint32_t*)&gba->mem.rom[rom_addr];
            }
            return ((rom_addr >> 1) & 0xFFFF) | (((rom_addr >> 1) + 1) << 16);
        }

        case 0x0E:
        case 0x0F:
            return gba->mem.sram[addr & (SRAM_SIZE - 1)] * 0x01010101;
    }

    return 0;
}

// Main memory write functions
void memory_write8(gba_t *gba, uint32_t addr, uint8_t value)
{
    switch ((addr >> 24) & 0xFF) {
        case 0x02:
            gba->mem.ewram[addr & (EWRAM_SIZE - 1)] = value;
            break;

        case 0x03:
            gba->mem.iwram[addr & (IWRAM_SIZE - 1)] = value;
            break;

        case 0x04:
            io_write8(gba, addr, value);
            break;

        case 0x05:
            // 8-bit palette writes write to both bytes
            addr &= (PALETTE_SIZE - 1) & ~1;
            gba->mem.palette[addr] = value;
            gba->mem.palette[addr + 1] = value;
            break;

        case 0x06: {
            // 8-bit VRAM writes write to both bytes (only in certain modes)
            uint32_t vram_addr = addr & 0x1FFFF;
            if (vram_addr >= VRAM_SIZE) vram_addr -= 0x8000;
            vram_addr &= ~1;
            gba->mem.vram[vram_addr] = value;
            gba->mem.vram[vram_addr + 1] = value;
            break;
        }

        case 0x0E:
        case 0x0F:
            gba->mem.sram[addr & (SRAM_SIZE - 1)] = value;
            break;
    }
}

void memory_write16(gba_t *gba, uint32_t addr, uint16_t value)
{
    addr &= ~1;

    switch ((addr >> 24) & 0xFF) {
        case 0x02:
            *(uint16_t*)&gba->mem.ewram[addr & (EWRAM_SIZE - 1)] = value;
            break;

        case 0x03:
            *(uint16_t*)&gba->mem.iwram[addr & (IWRAM_SIZE - 1)] = value;
            break;

        case 0x04:
            io_write16(gba, addr, value);
            break;

        case 0x05:
            *(uint16_t*)&gba->mem.palette[addr & (PALETTE_SIZE - 1)] = value;
            break;

        case 0x06: {
            uint32_t vram_addr = addr & 0x1FFFF;
            if (vram_addr >= VRAM_SIZE) vram_addr -= 0x8000;
            *(uint16_t*)&gba->mem.vram[vram_addr] = value;
            break;
        }

        case 0x07:
            *(uint16_t*)&gba->mem.oam[addr & (OAM_SIZE - 1)] = value;
            break;

        case 0x0E:
        case 0x0F:
            gba->mem.sram[addr & (SRAM_SIZE - 1)] = value & 0xFF;
            break;
    }
}

void memory_write32(gba_t *gba, uint32_t addr, uint32_t value)
{
    addr &= ~3;

    switch ((addr >> 24) & 0xFF) {
        case 0x02:
            *(uint32_t*)&gba->mem.ewram[addr & (EWRAM_SIZE - 1)] = value;
            break;

        case 0x03:
            *(uint32_t*)&gba->mem.iwram[addr & (IWRAM_SIZE - 1)] = value;
            break;

        case 0x04:
            io_write32(gba, addr, value);
            break;

        case 0x05:
            *(uint32_t*)&gba->mem.palette[addr & (PALETTE_SIZE - 1)] = value;
            break;

        case 0x06: {
            uint32_t vram_addr = addr & 0x1FFFF;
            if (vram_addr >= VRAM_SIZE) vram_addr -= 0x8000;
            *(uint32_t*)&gba->mem.vram[vram_addr] = value;
            break;
        }

        case 0x07:
            *(uint32_t*)&gba->mem.oam[addr & (OAM_SIZE - 1)] = value;
            break;

        case 0x0E:
        case 0x0F:
            gba->mem.sram[addr & (SRAM_SIZE - 1)] = value & 0xFF;
            break;
    }
}
