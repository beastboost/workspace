/**
 * GBA Emulator Core - ARM7TDMI CPU Implementation
 */

#include <string.h>
#include "esp_log.h"
#include "gba_internal.h"

static const char *TAG = "GBA_CPU";

// Mode index lookup
static const uint8_t mode_index[] = {
    [CPU_MODE_USER & 0xF] = 0,
    [CPU_MODE_FIQ & 0xF] = 1,
    [CPU_MODE_IRQ & 0xF] = 2,
    [CPU_MODE_SVC & 0xF] = 3,
    [CPU_MODE_ABT & 0xF] = 4,
    [CPU_MODE_UND & 0xF] = 5,
    [CPU_MODE_SYS & 0xF] = 0,
};

void cpu_init(gba_t *gba)
{
    memset(&gba->cpu, 0, sizeof(cpu_state_t));
}

void cpu_reset(gba_t *gba)
{
    memset(&gba->cpu, 0, sizeof(cpu_state_t));

    // Set initial mode to Supervisor
    gba->cpu.cpsr = CPU_MODE_SVC | CPSR_I | CPSR_F;

    // Set stack pointers for different modes
    gba->cpu.r13_svc = 0x03007FE0;
    gba->cpu.r13_irq = 0x03007FA0;
    gba->cpu.r[13] = 0x03007F00;  // User/System SP

    // Start execution at ROM entry point (skip BIOS if not loaded)
    if (gba->mem.bios_loaded) {
        gba->cpu.r[15] = 0x00000000;
    } else {
        gba->cpu.r[15] = 0x08000000;
        gba->cpu.cpsr = CPU_MODE_SYS;  // System mode for direct ROM boot
    }

    gba->cpu.pipeline_invalid = true;
    gba->cpu.halted = false;

    ESP_LOGI(TAG, "CPU reset, PC=0x%08lX", (unsigned long)gba->cpu.r[15]);
}

// Save current registers to banked registers based on mode
static void save_banked_regs(gba_t *gba, uint8_t mode)
{
    switch (mode & 0x1F) {
        case CPU_MODE_FIQ:
            gba->cpu.r8_fiq = gba->cpu.r[8];
            gba->cpu.r9_fiq = gba->cpu.r[9];
            gba->cpu.r10_fiq = gba->cpu.r[10];
            gba->cpu.r11_fiq = gba->cpu.r[11];
            gba->cpu.r12_fiq = gba->cpu.r[12];
            gba->cpu.r13_fiq = gba->cpu.r[13];
            gba->cpu.r14_fiq = gba->cpu.r[14];
            break;
        case CPU_MODE_SVC:
            gba->cpu.r13_svc = gba->cpu.r[13];
            gba->cpu.r14_svc = gba->cpu.r[14];
            break;
        case CPU_MODE_ABT:
            gba->cpu.r13_abt = gba->cpu.r[13];
            gba->cpu.r14_abt = gba->cpu.r[14];
            break;
        case CPU_MODE_IRQ:
            gba->cpu.r13_irq = gba->cpu.r[13];
            gba->cpu.r14_irq = gba->cpu.r[14];
            break;
        case CPU_MODE_UND:
            gba->cpu.r13_und = gba->cpu.r[13];
            gba->cpu.r14_und = gba->cpu.r[14];
            break;
    }
}

// Restore registers from banked registers based on mode
static void restore_banked_regs(gba_t *gba, uint8_t mode)
{
    switch (mode & 0x1F) {
        case CPU_MODE_FIQ:
            gba->cpu.r[8] = gba->cpu.r8_fiq;
            gba->cpu.r[9] = gba->cpu.r9_fiq;
            gba->cpu.r[10] = gba->cpu.r10_fiq;
            gba->cpu.r[11] = gba->cpu.r11_fiq;
            gba->cpu.r[12] = gba->cpu.r12_fiq;
            gba->cpu.r[13] = gba->cpu.r13_fiq;
            gba->cpu.r[14] = gba->cpu.r14_fiq;
            break;
        case CPU_MODE_SVC:
            gba->cpu.r[13] = gba->cpu.r13_svc;
            gba->cpu.r[14] = gba->cpu.r14_svc;
            break;
        case CPU_MODE_ABT:
            gba->cpu.r[13] = gba->cpu.r13_abt;
            gba->cpu.r[14] = gba->cpu.r14_abt;
            break;
        case CPU_MODE_IRQ:
            gba->cpu.r[13] = gba->cpu.r13_irq;
            gba->cpu.r[14] = gba->cpu.r14_irq;
            break;
        case CPU_MODE_UND:
            gba->cpu.r[13] = gba->cpu.r13_und;
            gba->cpu.r[14] = gba->cpu.r14_und;
            break;
    }
}

// Switch CPU mode
void cpu_switch_mode(gba_t *gba, uint8_t new_mode)
{
    uint8_t old_mode = gba->cpu.cpsr & 0x1F;
    if (old_mode == new_mode) return;

    // Save current banked registers
    save_banked_regs(gba, old_mode);

    // Update mode
    gba->cpu.cpsr = (gba->cpu.cpsr & ~0x1F) | new_mode;

    // Restore new mode's banked registers
    restore_banked_regs(gba, new_mode);
}

// Get SPSR for current mode
static uint32_t *get_spsr(gba_t *gba)
{
    uint8_t mode = gba->cpu.cpsr & 0x1F;
    switch (mode) {
        case CPU_MODE_FIQ: return &gba->cpu.spsr[0];
        case CPU_MODE_SVC: return &gba->cpu.spsr[1];
        case CPU_MODE_ABT: return &gba->cpu.spsr[2];
        case CPU_MODE_IRQ: return &gba->cpu.spsr[3];
        case CPU_MODE_UND: return &gba->cpu.spsr[4];
        default: return NULL;  // User/System mode has no SPSR
    }
}

// Check if condition is met
static bool check_condition(gba_t *gba, uint8_t cond)
{
    bool n = GET_FLAG_N();
    bool z = GET_FLAG_Z();
    bool c = GET_FLAG_C();
    bool v = GET_FLAG_V();

    switch (cond) {
        case 0x0: return z;                    // EQ - Equal
        case 0x1: return !z;                   // NE - Not equal
        case 0x2: return c;                    // CS/HS - Carry set
        case 0x3: return !c;                   // CC/LO - Carry clear
        case 0x4: return n;                    // MI - Minus
        case 0x5: return !n;                   // PL - Plus
        case 0x6: return v;                    // VS - Overflow
        case 0x7: return !v;                   // VC - No overflow
        case 0x8: return c && !z;              // HI - Unsigned higher
        case 0x9: return !c || z;              // LS - Unsigned lower/same
        case 0xA: return n == v;               // GE - Signed >=
        case 0xB: return n != v;               // LT - Signed <
        case 0xC: return !z && (n == v);       // GT - Signed >
        case 0xD: return z || (n != v);        // LE - Signed <=
        case 0xE: return true;                 // AL - Always
        case 0xF: return true;                 // NV - Never (but execute anyway on ARM7TDMI)
        default: return false;
    }
}

// Raise an interrupt
void cpu_raise_irq(gba_t *gba, uint16_t irq)
{
    gba->if_ |= irq;

    // Wake from halt if interrupt is enabled
    if ((gba->ie & irq) && gba->ime) {
        gba->cpu.halted = false;
    }
}

// Handle IRQ
static void cpu_handle_irq(gba_t *gba)
{
    if (!gba->ime) return;
    if (gba->cpu.cpsr & CPSR_I) return;  // IRQs disabled
    if (!(gba->ie & gba->if_)) return;   // No pending enabled IRQ

    // Save current mode
    uint8_t old_mode = gba->cpu.cpsr & 0x1F;
    save_banked_regs(gba, old_mode);

    // Switch to IRQ mode
    gba->cpu.cpsr = (gba->cpu.cpsr & ~0x1F) | CPU_MODE_IRQ;
    restore_banked_regs(gba, CPU_MODE_IRQ);

    // Save return address and CPSR
    uint32_t *spsr = get_spsr(gba);
    *spsr = (gba->cpu.cpsr & ~0x1F) | old_mode;
    gba->cpu.r[14] = gba->cpu.r[15] - (CPU_IN_THUMB_MODE() ? 2 : 4) + 4;

    // Disable IRQs and clear thumb mode
    gba->cpu.cpsr |= CPSR_I;
    gba->cpu.cpsr &= ~CPSR_T;

    // Jump to IRQ vector
    gba->cpu.r[15] = 0x18;
    gba->cpu.pipeline_invalid = true;
}

// Fill instruction pipeline
static void cpu_fill_pipeline(gba_t *gba)
{
    if (CPU_IN_THUMB_MODE()) {
        gba->cpu.pipeline[0] = memory_read16(gba, gba->cpu.r[15]);
        gba->cpu.r[15] += 2;
        gba->cpu.pipeline[1] = memory_read16(gba, gba->cpu.r[15]);
        gba->cpu.r[15] += 2;
    } else {
        gba->cpu.pipeline[0] = memory_read32(gba, gba->cpu.r[15]);
        gba->cpu.r[15] += 4;
        gba->cpu.pipeline[1] = memory_read32(gba, gba->cpu.r[15]);
        gba->cpu.r[15] += 4;
    }
    gba->cpu.pipeline_invalid = false;
}

// Execute one CPU instruction
uint32_t cpu_execute(gba_t *gba)
{
    // Check for pending interrupts
    cpu_handle_irq(gba);

    // Refill pipeline if needed
    if (gba->cpu.pipeline_invalid) {
        cpu_fill_pipeline(gba);
        return 3;  // Pipeline refill takes cycles
    }

    uint32_t cycles;

    if (CPU_IN_THUMB_MODE()) {
        // Fetch next instruction
        uint16_t opcode = gba->cpu.pipeline[0];
        gba->cpu.pipeline[0] = gba->cpu.pipeline[1];
        gba->cpu.pipeline[1] = memory_read16(gba, gba->cpu.r[15]);
        gba->cpu.r[15] += 2;

        // Execute THUMB instruction
        cycles = cpu_execute_thumb(gba, opcode);
    } else {
        // Fetch next instruction
        uint32_t opcode = gba->cpu.pipeline[0];
        gba->cpu.pipeline[0] = gba->cpu.pipeline[1];
        gba->cpu.pipeline[1] = memory_read32(gba, gba->cpu.r[15]);
        gba->cpu.r[15] += 4;

        // Check condition
        uint8_t cond = (opcode >> 28) & 0xF;
        if (check_condition(gba, cond)) {
            // Execute ARM instruction
            cycles = cpu_execute_arm(gba, opcode);
        } else {
            cycles = 1;  // Condition not met, skip
        }
    }

    gba->cpu.cycles += cycles;
    return cycles;
}

// Barrel shifter operations
uint32_t cpu_barrel_shift(gba_t *gba, uint32_t value, uint8_t shift_type,
                          uint8_t shift_amount, bool *carry_out, bool reg_shift)
{
    if (shift_amount == 0 && !reg_shift) {
        // Special case for immediate shift of 0
        switch (shift_type) {
            case 0: // LSL #0 - no change
                *carry_out = GET_FLAG_C();
                return value;
            case 1: // LSR #0 -> LSR #32
                *carry_out = (value >> 31) & 1;
                return 0;
            case 2: // ASR #0 -> ASR #32
                *carry_out = (value >> 31) & 1;
                return (int32_t)value >> 31;
            case 3: // ROR #0 -> RRX
                *carry_out = value & 1;
                return (GET_FLAG_C() << 31) | (value >> 1);
        }
    }

    switch (shift_type) {
        case 0: // LSL
            if (shift_amount >= 32) {
                *carry_out = (shift_amount == 32) ? (value & 1) : 0;
                return 0;
            }
            *carry_out = shift_amount ? ((value >> (32 - shift_amount)) & 1) : GET_FLAG_C();
            return value << shift_amount;

        case 1: // LSR
            if (shift_amount >= 32) {
                *carry_out = (shift_amount == 32) ? ((value >> 31) & 1) : 0;
                return 0;
            }
            *carry_out = shift_amount ? ((value >> (shift_amount - 1)) & 1) : GET_FLAG_C();
            return value >> shift_amount;

        case 2: // ASR
            if (shift_amount >= 32) {
                *carry_out = (value >> 31) & 1;
                return (int32_t)value >> 31;
            }
            *carry_out = shift_amount ? (((int32_t)value >> (shift_amount - 1)) & 1) : GET_FLAG_C();
            return (int32_t)value >> shift_amount;

        case 3: // ROR
            shift_amount &= 31;
            if (shift_amount == 0) {
                *carry_out = (value >> 31) & 1;
                return value;
            }
            *carry_out = (value >> (shift_amount - 1)) & 1;
            return (value >> shift_amount) | (value << (32 - shift_amount));
    }

    return value;
}

// ALU operations with flag updates
uint32_t cpu_alu_add(gba_t *gba, uint32_t a, uint32_t b, bool set_flags)
{
    uint32_t result = a + b;

    if (set_flags) {
        SET_FLAG_N(result & 0x80000000);
        SET_FLAG_Z(result == 0);
        SET_FLAG_C(result < a);
        SET_FLAG_V(((a ^ result) & (b ^ result)) >> 31);
    }

    return result;
}

uint32_t cpu_alu_adc(gba_t *gba, uint32_t a, uint32_t b, bool set_flags)
{
    uint32_t c = GET_FLAG_C();
    uint64_t result64 = (uint64_t)a + (uint64_t)b + c;
    uint32_t result = (uint32_t)result64;

    if (set_flags) {
        SET_FLAG_N(result & 0x80000000);
        SET_FLAG_Z(result == 0);
        SET_FLAG_C(result64 > 0xFFFFFFFF);
        SET_FLAG_V(((a ^ result) & (b ^ result)) >> 31);
    }

    return result;
}

uint32_t cpu_alu_sub(gba_t *gba, uint32_t a, uint32_t b, bool set_flags)
{
    uint32_t result = a - b;

    if (set_flags) {
        SET_FLAG_N(result & 0x80000000);
        SET_FLAG_Z(result == 0);
        SET_FLAG_C(a >= b);
        SET_FLAG_V(((a ^ b) & (a ^ result)) >> 31);
    }

    return result;
}

uint32_t cpu_alu_sbc(gba_t *gba, uint32_t a, uint32_t b, bool set_flags)
{
    uint32_t c = GET_FLAG_C() ? 0 : 1;
    uint32_t result = a - b - c;

    if (set_flags) {
        SET_FLAG_N(result & 0x80000000);
        SET_FLAG_Z(result == 0);
        SET_FLAG_C(a >= (uint64_t)b + c);
        SET_FLAG_V(((a ^ b) & (a ^ result)) >> 31);
    }

    return result;
}
