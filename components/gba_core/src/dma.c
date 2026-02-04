/**
 * GBA Emulator Core - DMA (Direct Memory Access) Implementation
 */

#include <string.h>
#include "gba_internal.h"

void dma_init(gba_t *gba)
{
    memset(gba->dma, 0, sizeof(gba->dma));
}

void dma_reset(gba_t *gba)
{
    memset(gba->dma, 0, sizeof(gba->dma));
}

// DMA timing modes
#define DMA_TIMING_IMMEDIATE 0
#define DMA_TIMING_VBLANK    1
#define DMA_TIMING_HBLANK    2
#define DMA_TIMING_SPECIAL   3

// Check and trigger DMA based on timing
void dma_trigger(gba_t *gba, int timing)
{
    for (int i = 0; i < 4; i++) {
        uint16_t ctrl = gba->dma[i].control;

        // Check if DMA is enabled
        if (!(ctrl & (1 << 15))) continue;

        // Check timing
        int dma_timing = (ctrl >> 12) & 3;

        if (timing == 0 && dma_timing == DMA_TIMING_IMMEDIATE) {
            gba->dma[i].active = true;
        } else if (timing == 1 && dma_timing == DMA_TIMING_VBLANK) {
            gba->dma[i].active = true;
        } else if (timing == 2 && dma_timing == DMA_TIMING_HBLANK) {
            gba->dma[i].active = true;
        } else if (timing == 3 && dma_timing == DMA_TIMING_SPECIAL) {
            // Special timing depends on channel:
            // DMA1/2: Sound FIFO
            // DMA3: Video capture
            if (i == 1 || i == 2) {
                gba->dma[i].active = true;
            }
        }
    }
}

// Check if any DMA is active
bool dma_active(gba_t *gba)
{
    for (int i = 0; i < 4; i++) {
        if (gba->dma[i].active) return true;
    }
    return false;
}

// Run active DMA channels
uint32_t dma_run(gba_t *gba)
{
    uint32_t cycles = 0;

    for (int i = 0; i < 4; i++) {
        if (!gba->dma[i].active) continue;

        dma_channel_t *dma = &gba->dma[i];
        uint16_t ctrl = dma->control;

        // Source/dest address control
        int src_ctrl = (ctrl >> 7) & 3;
        int dst_ctrl = (ctrl >> 5) & 3;
        bool is_32bit = ctrl & (1 << 10);
        int transfer_size = is_32bit ? 4 : 2;

        // Get count
        uint32_t count = dma->count;
        if (count == 0) {
            count = (i == 3) ? 0x10000 : 0x4000;
        }

        // Special handling for sound FIFO DMA
        int timing = (ctrl >> 12) & 3;
        if (timing == DMA_TIMING_SPECIAL && (i == 1 || i == 2)) {
            // Sound FIFO transfer - always 4 words
            count = 4;
            is_32bit = true;
            transfer_size = 4;
            dst_ctrl = 2;  // Fixed destination
        }

        // Perform transfers
        uint32_t src = dma->src;
        uint32_t dst = dma->dst;

        for (uint32_t j = 0; j < count; j++) {
            if (is_32bit) {
                uint32_t value = memory_read32(gba, src);
                memory_write32(gba, dst, value);
            } else {
                uint16_t value = memory_read16(gba, src);
                memory_write16(gba, dst, value);
            }

            // Update source address
            switch (src_ctrl) {
                case 0: src += transfer_size; break;  // Increment
                case 1: src -= transfer_size; break;  // Decrement
                case 2: break;                        // Fixed
                case 3: src += transfer_size; break;  // Increment/Reload
            }

            // Update destination address
            switch (dst_ctrl) {
                case 0: dst += transfer_size; break;  // Increment
                case 1: dst -= transfer_size; break;  // Decrement
                case 2: break;                        // Fixed
                case 3: dst += transfer_size; break;  // Increment/Reload
            }

            cycles += 2;  // Approximate cycle count
        }

        // Update DMA state
        dma->src = src;
        dma->dst = dst;

        // Reload destination if needed
        if (dst_ctrl == 3) {
            // Read from IO registers
            uint32_t base_addr = 0x040000B0 + i * 12;
            dma->dst = memory_read32(gba, base_addr + 4) & ((i == 3) ? 0x0FFFFFFF : 0x07FFFFFF);
        }

        // Check repeat
        bool repeat = ctrl & (1 << 9);
        if (repeat && timing != DMA_TIMING_IMMEDIATE) {
            // Reload count
            uint32_t base_addr = 0x040000B8 + i * 12;
            dma->count = memory_read16(gba, base_addr) & ((i == 3) ? 0xFFFF : 0x3FFF);
            dma->active = false;  // Wait for next trigger
        } else {
            // Disable DMA
            dma->control &= ~(1 << 15);
            dma->active = false;
        }

        // IRQ on completion
        if (ctrl & (1 << 14)) {
            cpu_raise_irq(gba, IRQ_DMA0 << i);
        }

        // Only run one DMA at a time (priority)
        break;
    }

    return cycles > 0 ? cycles : 1;
}
