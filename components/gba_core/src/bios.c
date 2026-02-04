/**
 * GBA Emulator Core - HLE BIOS Implementation
 *
 * High-Level Emulation of GBA BIOS functions for when no BIOS file is loaded
 */

#include <string.h>
#include <math.h>
#include "gba_internal.h"

void bios_init(gba_t *gba)
{
    // If no BIOS is loaded, fill with open bus value
    if (!gba->mem.bios_loaded) {
        memset(gba->mem.bios, 0, BIOS_SIZE);
    }
}

// SWI 0x00: SoftReset
static void swi_soft_reset(gba_t *gba)
{
    // Clear work RAM
    memset(gba->mem.iwram, 0, 0x7E00);

    // Reset CPU state
    gba->cpu.r[13] = 0x03007F00;
    gba->cpu.r13_irq = 0x03007FA0;
    gba->cpu.r13_svc = 0x03007FE0;

    // Check reset flag at 0x03007FFA
    uint8_t flag = gba->mem.iwram[0x7FFA];
    if (flag == 0) {
        // Return to ROM
        gba->cpu.r[15] = 0x08000000;
    } else {
        // Return to RAM
        gba->cpu.r[15] = 0x02000000;
    }

    gba->cpu.cpsr = CPU_MODE_SYS;
    gba->cpu.pipeline_invalid = true;
}

// SWI 0x01: RegisterRamReset
static void swi_register_ram_reset(gba_t *gba)
{
    uint32_t flags = gba->cpu.r[0];

    if (flags & (1 << 0)) memset(gba->mem.ewram, 0, EWRAM_SIZE);
    if (flags & (1 << 1)) memset(gba->mem.iwram, 0, 0x7E00);
    if (flags & (1 << 2)) memset(gba->mem.palette, 0, PALETTE_SIZE);
    if (flags & (1 << 3)) memset(gba->mem.vram, 0, VRAM_SIZE);
    if (flags & (1 << 4)) memset(gba->mem.oam, 0, OAM_SIZE);
    if (flags & (1 << 5)) {
        // Reset SIO registers
    }
    if (flags & (1 << 6)) {
        // Reset sound registers
        memset(&gba->apu, 0, sizeof(apu_state_t));
        gba->apu.soundbias = 0x200;
    }
    if (flags & (1 << 7)) {
        // Reset other IO registers
    }
}

// SWI 0x02: Halt
static void swi_halt(gba_t *gba)
{
    gba->cpu.halted = true;
}

// SWI 0x03: Stop
static void swi_stop(gba_t *gba)
{
    // Stop mode - halts CPU until keypad interrupt
    gba->cpu.halted = true;
}

// SWI 0x04: IntrWait
static void swi_intr_wait(gba_t *gba)
{
    // r0 = discard old flags (1) or not (0)
    // r1 = interrupt flags to wait for
    gba->cpu.halted = true;
}

// SWI 0x05: VBlankIntrWait
static void swi_vblank_intr_wait(gba_t *gba)
{
    // Wait for VBlank interrupt
    gba->cpu.halted = true;
}

// SWI 0x06: Div
static void swi_div(gba_t *gba)
{
    int32_t num = (int32_t)gba->cpu.r[0];
    int32_t denom = (int32_t)gba->cpu.r[1];

    if (denom == 0) {
        // Division by zero
        gba->cpu.r[0] = (num < 0) ? -1 : 1;
        gba->cpu.r[1] = num;
        gba->cpu.r[3] = (num < 0) ? (uint32_t)-num : (uint32_t)num;
    } else {
        gba->cpu.r[0] = num / denom;
        gba->cpu.r[1] = num % denom;
        int32_t result = num / denom;
        gba->cpu.r[3] = (result < 0) ? (uint32_t)-result : (uint32_t)result;
    }
}

// SWI 0x07: DivArm
static void swi_div_arm(gba_t *gba)
{
    // Same as Div but with swapped arguments
    int32_t num = (int32_t)gba->cpu.r[1];
    int32_t denom = (int32_t)gba->cpu.r[0];

    if (denom == 0) {
        gba->cpu.r[0] = (num < 0) ? -1 : 1;
        gba->cpu.r[1] = num;
        gba->cpu.r[3] = (num < 0) ? (uint32_t)-num : (uint32_t)num;
    } else {
        gba->cpu.r[0] = num / denom;
        gba->cpu.r[1] = num % denom;
        int32_t result = num / denom;
        gba->cpu.r[3] = (result < 0) ? (uint32_t)-result : (uint32_t)result;
    }
}

// SWI 0x08: Sqrt
static void swi_sqrt(gba_t *gba)
{
    uint32_t val = gba->cpu.r[0];

    // Integer square root
    uint32_t result = 0;
    uint32_t bit = 1 << 30;

    while (bit > val) {
        bit >>= 2;
    }

    while (bit != 0) {
        if (val >= result + bit) {
            val -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }

    gba->cpu.r[0] = result;
}

// SWI 0x09: ArcTan
static void swi_arctan(gba_t *gba)
{
    int16_t tan = (int16_t)gba->cpu.r[0];

    // Approximate arctangent
    // Result in range -0x4000 to 0x4000 (representing -pi/2 to pi/2)
    float x = tan / 16384.0f;
    float result = atanf(x) * (16384.0f / 1.5707963f);

    gba->cpu.r[0] = (int16_t)result;
}

// SWI 0x0A: ArcTan2
static void swi_arctan2(gba_t *gba)
{
    int16_t x = (int16_t)gba->cpu.r[0];
    int16_t y = (int16_t)gba->cpu.r[1];

    // Full circle arctangent
    // Result in range 0x0000 to 0xFFFF (representing 0 to 2pi)
    float fx = x / 16384.0f;
    float fy = y / 16384.0f;
    float result = atan2f(fy, fx);
    if (result < 0) result += 6.2831853f;
    result = result * (32768.0f / 3.1415926f);

    gba->cpu.r[0] = (uint16_t)result;
}

// SWI 0x0B: CpuSet
static void swi_cpu_set(gba_t *gba)
{
    uint32_t src = gba->cpu.r[0];
    uint32_t dst = gba->cpu.r[1];
    uint32_t cnt = gba->cpu.r[2];

    bool is_32bit = cnt & (1 << 26);
    bool is_fill = cnt & (1 << 24);
    uint32_t count = cnt & 0x1FFFFF;

    if (is_32bit) {
        uint32_t value = memory_read32(gba, src);
        for (uint32_t i = 0; i < count; i++) {
            memory_write32(gba, dst, value);
            dst += 4;
            if (!is_fill) {
                src += 4;
                value = memory_read32(gba, src);
            }
        }
    } else {
        uint16_t value = memory_read16(gba, src);
        for (uint32_t i = 0; i < count; i++) {
            memory_write16(gba, dst, value);
            dst += 2;
            if (!is_fill) {
                src += 2;
                value = memory_read16(gba, src);
            }
        }
    }
}

// SWI 0x0C: CpuFastSet
static void swi_cpu_fast_set(gba_t *gba)
{
    uint32_t src = gba->cpu.r[0] & ~3;
    uint32_t dst = gba->cpu.r[1] & ~3;
    uint32_t cnt = gba->cpu.r[2];

    bool is_fill = cnt & (1 << 24);
    uint32_t count = (cnt & 0x1FFFFF) & ~7;  // Must be multiple of 8 words

    uint32_t value = memory_read32(gba, src);
    for (uint32_t i = 0; i < count; i++) {
        memory_write32(gba, dst, value);
        dst += 4;
        if (!is_fill) {
            src += 4;
            value = memory_read32(gba, src);
        }
    }
}

// SWI 0x0E: BgAffineSet
static void swi_bg_affine_set(gba_t *gba)
{
    uint32_t src = gba->cpu.r[0];
    uint32_t dst = gba->cpu.r[1];
    uint32_t count = gba->cpu.r[2];

    for (uint32_t i = 0; i < count; i++) {
        // Read source data
        int32_t center_x = (int32_t)memory_read32(gba, src);
        int32_t center_y = (int32_t)memory_read32(gba, src + 4);
        int16_t disp_x = (int16_t)memory_read16(gba, src + 8);
        int16_t disp_y = (int16_t)memory_read16(gba, src + 10);
        int16_t scale_x = (int16_t)memory_read16(gba, src + 12);
        int16_t scale_y = (int16_t)memory_read16(gba, src + 14);
        uint16_t angle = memory_read16(gba, src + 16);
        src += 20;

        // Calculate sine and cosine
        float theta = angle * (3.1415926f / 32768.0f);
        float cos_a = cosf(theta);
        float sin_a = sinf(theta);

        // Calculate affine parameters
        int16_t pa = (int16_t)(cos_a * (256.0f / scale_x) * 256.0f);
        int16_t pb = (int16_t)(sin_a * (256.0f / scale_x) * 256.0f);
        int16_t pc = (int16_t)(-sin_a * (256.0f / scale_y) * 256.0f);
        int16_t pd = (int16_t)(cos_a * (256.0f / scale_y) * 256.0f);

        int32_t dx = center_x - (pa * disp_x + pb * disp_y);
        int32_t dy = center_y - (pc * disp_x + pd * disp_y);

        // Write destination data
        memory_write16(gba, dst, pa);
        memory_write16(gba, dst + 2, pb);
        memory_write16(gba, dst + 4, pc);
        memory_write16(gba, dst + 6, pd);
        memory_write32(gba, dst + 8, dx);
        memory_write32(gba, dst + 12, dy);
        dst += 16;
    }
}

// SWI 0x0F: ObjAffineSet
static void swi_obj_affine_set(gba_t *gba)
{
    uint32_t src = gba->cpu.r[0];
    uint32_t dst = gba->cpu.r[1];
    uint32_t count = gba->cpu.r[2];
    uint32_t stride = gba->cpu.r[3];

    for (uint32_t i = 0; i < count; i++) {
        int16_t scale_x = (int16_t)memory_read16(gba, src);
        int16_t scale_y = (int16_t)memory_read16(gba, src + 2);
        uint16_t angle = memory_read16(gba, src + 4);
        src += 8;

        float theta = angle * (3.1415926f / 32768.0f);
        float cos_a = cosf(theta);
        float sin_a = sinf(theta);

        int16_t pa = (int16_t)(cos_a * (256.0f / scale_x) * 256.0f);
        int16_t pb = (int16_t)(sin_a * (256.0f / scale_x) * 256.0f);
        int16_t pc = (int16_t)(-sin_a * (256.0f / scale_y) * 256.0f);
        int16_t pd = (int16_t)(cos_a * (256.0f / scale_y) * 256.0f);

        memory_write16(gba, dst, pa);
        memory_write16(gba, dst + stride, pb);
        memory_write16(gba, dst + stride * 2, pc);
        memory_write16(gba, dst + stride * 3, pd);
        dst += stride * 4;
    }
}

// SWI 0x11: LZ77UnCompWram
static void swi_lz77_decomp_wram(gba_t *gba)
{
    uint32_t src = gba->cpu.r[0];
    uint32_t dst = gba->cpu.r[1];

    // Read header
    uint32_t header = memory_read32(gba, src);
    uint32_t size = header >> 8;
    src += 4;

    uint32_t written = 0;
    while (written < size) {
        uint8_t flags = memory_read8(gba, src++);

        for (int i = 0; i < 8 && written < size; i++) {
            if (flags & 0x80) {
                // Compressed block
                uint8_t byte1 = memory_read8(gba, src++);
                uint8_t byte2 = memory_read8(gba, src++);
                uint32_t length = ((byte1 >> 4) & 0xF) + 3;
                uint32_t offset = ((byte1 & 0xF) << 8) | byte2;
                offset++;

                for (uint32_t j = 0; j < length && written < size; j++) {
                    uint8_t value = memory_read8(gba, dst - offset);
                    memory_write8(gba, dst++, value);
                    written++;
                }
            } else {
                // Uncompressed byte
                memory_write8(gba, dst++, memory_read8(gba, src++));
                written++;
            }
            flags <<= 1;
        }
    }
}

// SWI 0x12: LZ77UnCompVram
static void swi_lz77_decomp_vram(gba_t *gba)
{
    // Same as WRAM version but writes in 16-bit units
    swi_lz77_decomp_wram(gba);
}

// SWI 0x14: RLUnCompWram
static void swi_rl_decomp_wram(gba_t *gba)
{
    uint32_t src = gba->cpu.r[0];
    uint32_t dst = gba->cpu.r[1];

    uint32_t header = memory_read32(gba, src);
    uint32_t size = header >> 8;
    src += 4;

    uint32_t written = 0;
    while (written < size) {
        uint8_t flag = memory_read8(gba, src++);

        if (flag & 0x80) {
            // Compressed run
            uint8_t length = (flag & 0x7F) + 3;
            uint8_t value = memory_read8(gba, src++);
            for (int i = 0; i < length && written < size; i++) {
                memory_write8(gba, dst++, value);
                written++;
            }
        } else {
            // Uncompressed run
            uint8_t length = (flag & 0x7F) + 1;
            for (int i = 0; i < length && written < size; i++) {
                memory_write8(gba, dst++, memory_read8(gba, src++));
                written++;
            }
        }
    }
}

// Main BIOS call dispatcher
uint32_t bios_call(gba_t *gba, uint8_t swi_num)
{
    switch (swi_num) {
        case 0x00: swi_soft_reset(gba); break;
        case 0x01: swi_register_ram_reset(gba); break;
        case 0x02: swi_halt(gba); break;
        case 0x03: swi_stop(gba); break;
        case 0x04: swi_intr_wait(gba); break;
        case 0x05: swi_vblank_intr_wait(gba); break;
        case 0x06: swi_div(gba); break;
        case 0x07: swi_div_arm(gba); break;
        case 0x08: swi_sqrt(gba); break;
        case 0x09: swi_arctan(gba); break;
        case 0x0A: swi_arctan2(gba); break;
        case 0x0B: swi_cpu_set(gba); break;
        case 0x0C: swi_cpu_fast_set(gba); break;
        case 0x0E: swi_bg_affine_set(gba); break;
        case 0x0F: swi_obj_affine_set(gba); break;
        case 0x11: swi_lz77_decomp_wram(gba); break;
        case 0x12: swi_lz77_decomp_vram(gba); break;
        case 0x14: swi_rl_decomp_wram(gba); break;
        default:
            // Unknown SWI - do nothing
            break;
    }

    return 0;
}
