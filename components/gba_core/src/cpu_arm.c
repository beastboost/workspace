/**
 * GBA Emulator Core - ARM Instruction Set Implementation
 */

#include "gba_internal.h"

// Forward declarations
extern uint32_t cpu_barrel_shift(gba_t *gba, uint32_t value, uint8_t shift_type,
                                  uint8_t shift_amount, bool *carry_out, bool reg_shift);
extern uint32_t cpu_alu_add(gba_t *gba, uint32_t a, uint32_t b, bool set_flags);
extern uint32_t cpu_alu_adc(gba_t *gba, uint32_t a, uint32_t b, bool set_flags);
extern uint32_t cpu_alu_sub(gba_t *gba, uint32_t a, uint32_t b, bool set_flags);
extern uint32_t cpu_alu_sbc(gba_t *gba, uint32_t a, uint32_t b, bool set_flags);
extern void cpu_switch_mode(gba_t *gba, uint8_t new_mode);

// Get operand 2 for data processing instructions
static uint32_t get_operand2(gba_t *gba, uint32_t opcode, bool *carry)
{
    *carry = GET_FLAG_C();

    if (opcode & (1 << 25)) {
        // Immediate value
        uint32_t imm = opcode & 0xFF;
        uint8_t rotate = ((opcode >> 8) & 0xF) * 2;
        if (rotate) {
            *carry = (imm >> (rotate - 1)) & 1;
            imm = (imm >> rotate) | (imm << (32 - rotate));
        }
        return imm;
    } else {
        // Register with shift
        uint8_t rm = opcode & 0xF;
        uint32_t value = gba->cpu.r[rm];
        if (rm == 15) value += 4;  // PC + 8 for register shift

        uint8_t shift_type = (opcode >> 5) & 0x3;
        uint8_t shift_amount;
        bool reg_shift = false;

        if (opcode & (1 << 4)) {
            // Shift by register
            reg_shift = true;
            uint8_t rs = (opcode >> 8) & 0xF;
            shift_amount = gba->cpu.r[rs] & 0xFF;
        } else {
            // Shift by immediate
            shift_amount = (opcode >> 7) & 0x1F;
        }

        return cpu_barrel_shift(gba, value, shift_type, shift_amount, carry, reg_shift);
    }
}

// Data Processing instructions
static uint32_t arm_data_processing(gba_t *gba, uint32_t opcode)
{
    uint8_t op = (opcode >> 21) & 0xF;
    bool set_flags = (opcode >> 20) & 1;
    uint8_t rn = (opcode >> 16) & 0xF;
    uint8_t rd = (opcode >> 12) & 0xF;

    uint32_t op1 = gba->cpu.r[rn];
    if (rn == 15) op1 += 4;  // PC + 8

    bool carry;
    uint32_t op2 = get_operand2(gba, opcode, &carry);

    uint32_t result;
    bool write_result = true;

    switch (op) {
        case 0x0: // AND
            result = op1 & op2;
            if (set_flags) {
                SET_FLAG_N(result & 0x80000000);
                SET_FLAG_Z(result == 0);
                SET_FLAG_C(carry);
            }
            break;

        case 0x1: // EOR
            result = op1 ^ op2;
            if (set_flags) {
                SET_FLAG_N(result & 0x80000000);
                SET_FLAG_Z(result == 0);
                SET_FLAG_C(carry);
            }
            break;

        case 0x2: // SUB
            result = cpu_alu_sub(gba, op1, op2, set_flags);
            break;

        case 0x3: // RSB
            result = cpu_alu_sub(gba, op2, op1, set_flags);
            break;

        case 0x4: // ADD
            result = cpu_alu_add(gba, op1, op2, set_flags);
            break;

        case 0x5: // ADC
            result = cpu_alu_adc(gba, op1, op2, set_flags);
            break;

        case 0x6: // SBC
            result = cpu_alu_sbc(gba, op1, op2, set_flags);
            break;

        case 0x7: // RSC
            result = cpu_alu_sbc(gba, op2, op1, set_flags);
            break;

        case 0x8: // TST
            result = op1 & op2;
            SET_FLAG_N(result & 0x80000000);
            SET_FLAG_Z(result == 0);
            SET_FLAG_C(carry);
            write_result = false;
            break;

        case 0x9: // TEQ
            result = op1 ^ op2;
            SET_FLAG_N(result & 0x80000000);
            SET_FLAG_Z(result == 0);
            SET_FLAG_C(carry);
            write_result = false;
            break;

        case 0xA: // CMP
            cpu_alu_sub(gba, op1, op2, true);
            write_result = false;
            break;

        case 0xB: // CMN
            cpu_alu_add(gba, op1, op2, true);
            write_result = false;
            break;

        case 0xC: // ORR
            result = op1 | op2;
            if (set_flags) {
                SET_FLAG_N(result & 0x80000000);
                SET_FLAG_Z(result == 0);
                SET_FLAG_C(carry);
            }
            break;

        case 0xD: // MOV
            result = op2;
            if (set_flags) {
                SET_FLAG_N(result & 0x80000000);
                SET_FLAG_Z(result == 0);
                SET_FLAG_C(carry);
            }
            break;

        case 0xE: // BIC
            result = op1 & ~op2;
            if (set_flags) {
                SET_FLAG_N(result & 0x80000000);
                SET_FLAG_Z(result == 0);
                SET_FLAG_C(carry);
            }
            break;

        case 0xF: // MVN
            result = ~op2;
            if (set_flags) {
                SET_FLAG_N(result & 0x80000000);
                SET_FLAG_Z(result == 0);
                SET_FLAG_C(carry);
            }
            break;

        default:
            result = 0;
            break;
    }

    if (write_result) {
        gba->cpu.r[rd] = result;
        if (rd == 15) {
            if (set_flags) {
                // Restore CPSR from SPSR
                uint8_t mode = gba->cpu.cpsr & 0x1F;
                if (mode != CPU_MODE_USER && mode != CPU_MODE_SYS) {
                    uint32_t *spsr;
                    switch (mode) {
                        case CPU_MODE_FIQ: spsr = &gba->cpu.spsr[0]; break;
                        case CPU_MODE_SVC: spsr = &gba->cpu.spsr[1]; break;
                        case CPU_MODE_ABT: spsr = &gba->cpu.spsr[2]; break;
                        case CPU_MODE_IRQ: spsr = &gba->cpu.spsr[3]; break;
                        case CPU_MODE_UND: spsr = &gba->cpu.spsr[4]; break;
                        default: spsr = NULL; break;
                    }
                    if (spsr) {
                        cpu_switch_mode(gba, *spsr & 0x1F);
                        gba->cpu.cpsr = *spsr;
                    }
                }
            }
            gba->cpu.pipeline_invalid = true;
            return 3;
        }
    }

    return 1;
}

// Multiply instructions
static uint32_t arm_multiply(gba_t *gba, uint32_t opcode)
{
    uint8_t rd = (opcode >> 16) & 0xF;
    uint8_t rn = (opcode >> 12) & 0xF;
    uint8_t rs = (opcode >> 8) & 0xF;
    uint8_t rm = opcode & 0xF;
    bool set_flags = (opcode >> 20) & 1;
    bool accumulate = (opcode >> 21) & 1;

    uint32_t result = gba->cpu.r[rm] * gba->cpu.r[rs];
    if (accumulate) {
        result += gba->cpu.r[rn];
    }

    gba->cpu.r[rd] = result;

    if (set_flags) {
        SET_FLAG_N(result & 0x80000000);
        SET_FLAG_Z(result == 0);
    }

    // Multiply takes variable cycles based on operand
    return 2 + (accumulate ? 1 : 0);
}

// Multiply Long instructions
static uint32_t arm_multiply_long(gba_t *gba, uint32_t opcode)
{
    uint8_t rdhi = (opcode >> 16) & 0xF;
    uint8_t rdlo = (opcode >> 12) & 0xF;
    uint8_t rs = (opcode >> 8) & 0xF;
    uint8_t rm = opcode & 0xF;
    bool set_flags = (opcode >> 20) & 1;
    bool accumulate = (opcode >> 21) & 1;
    bool is_signed = (opcode >> 22) & 1;

    int64_t result;
    if (is_signed) {
        result = (int64_t)(int32_t)gba->cpu.r[rm] * (int64_t)(int32_t)gba->cpu.r[rs];
    } else {
        result = (uint64_t)gba->cpu.r[rm] * (uint64_t)gba->cpu.r[rs];
    }

    if (accumulate) {
        result += ((uint64_t)gba->cpu.r[rdhi] << 32) | gba->cpu.r[rdlo];
    }

    gba->cpu.r[rdlo] = (uint32_t)result;
    gba->cpu.r[rdhi] = (uint32_t)(result >> 32);

    if (set_flags) {
        SET_FLAG_N(result & 0x8000000000000000ULL);
        SET_FLAG_Z(result == 0);
    }

    return 3 + (accumulate ? 1 : 0);
}

// Single Data Transfer (LDR/STR)
static uint32_t arm_single_transfer(gba_t *gba, uint32_t opcode)
{
    uint8_t rn = (opcode >> 16) & 0xF;
    uint8_t rd = (opcode >> 12) & 0xF;
    bool is_load = (opcode >> 20) & 1;
    bool writeback = (opcode >> 21) & 1;
    bool is_byte = (opcode >> 22) & 1;
    bool add = (opcode >> 23) & 1;
    bool pre_index = (opcode >> 24) & 1;

    uint32_t base = gba->cpu.r[rn];
    if (rn == 15) base += 4;

    uint32_t offset;
    if (opcode & (1 << 25)) {
        // Register offset with shift
        uint8_t rm = opcode & 0xF;
        uint32_t value = gba->cpu.r[rm];
        uint8_t shift_type = (opcode >> 5) & 0x3;
        uint8_t shift_amount = (opcode >> 7) & 0x1F;
        bool carry;
        offset = cpu_barrel_shift(gba, value, shift_type, shift_amount, &carry, false);
    } else {
        // Immediate offset
        offset = opcode & 0xFFF;
    }

    uint32_t addr = add ? base + offset : base - offset;
    uint32_t final_addr = pre_index ? addr : base;

    if (is_load) {
        if (is_byte) {
            gba->cpu.r[rd] = memory_read8(gba, final_addr);
        } else {
            gba->cpu.r[rd] = memory_read32(gba, final_addr & ~3);
            // Rotate misaligned reads
            uint8_t rotate = (final_addr & 3) * 8;
            if (rotate) {
                gba->cpu.r[rd] = (gba->cpu.r[rd] >> rotate) | (gba->cpu.r[rd] << (32 - rotate));
            }
        }
        if (rd == 15) {
            gba->cpu.pipeline_invalid = true;
        }
    } else {
        uint32_t value = gba->cpu.r[rd];
        if (rd == 15) value += 4;

        if (is_byte) {
            memory_write8(gba, final_addr, value & 0xFF);
        } else {
            memory_write32(gba, final_addr & ~3, value);
        }
    }

    // Writeback
    if (!pre_index || writeback) {
        gba->cpu.r[rn] = addr;
    }

    return is_load ? 3 : 2;
}

// Halfword/Signed Transfer
static uint32_t arm_halfword_transfer(gba_t *gba, uint32_t opcode)
{
    uint8_t rn = (opcode >> 16) & 0xF;
    uint8_t rd = (opcode >> 12) & 0xF;
    bool is_load = (opcode >> 20) & 1;
    bool writeback = (opcode >> 21) & 1;
    bool is_imm = (opcode >> 22) & 1;
    bool add = (opcode >> 23) & 1;
    bool pre_index = (opcode >> 24) & 1;
    uint8_t sh = (opcode >> 5) & 0x3;

    uint32_t base = gba->cpu.r[rn];
    if (rn == 15) base += 4;

    uint32_t offset;
    if (is_imm) {
        offset = ((opcode >> 4) & 0xF0) | (opcode & 0xF);
    } else {
        offset = gba->cpu.r[opcode & 0xF];
    }

    uint32_t addr = add ? base + offset : base - offset;
    uint32_t final_addr = pre_index ? addr : base;

    if (is_load) {
        switch (sh) {
            case 1: // LDRH
                gba->cpu.r[rd] = memory_read16(gba, final_addr & ~1);
                break;
            case 2: // LDRSB
                gba->cpu.r[rd] = (int32_t)(int8_t)memory_read8(gba, final_addr);
                break;
            case 3: // LDRSH
                gba->cpu.r[rd] = (int32_t)(int16_t)memory_read16(gba, final_addr & ~1);
                break;
        }
        if (rd == 15) {
            gba->cpu.pipeline_invalid = true;
        }
    } else {
        // STRH
        memory_write16(gba, final_addr & ~1, gba->cpu.r[rd] & 0xFFFF);
    }

    if (!pre_index || writeback) {
        gba->cpu.r[rn] = addr;
    }

    return is_load ? 3 : 2;
}

// Block Data Transfer (LDM/STM)
static uint32_t arm_block_transfer(gba_t *gba, uint32_t opcode)
{
    uint8_t rn = (opcode >> 16) & 0xF;
    uint16_t rlist = opcode & 0xFFFF;
    bool is_load = (opcode >> 20) & 1;
    bool writeback = (opcode >> 21) & 1;
    bool s_bit = (opcode >> 22) & 1;
    bool add = (opcode >> 23) & 1;
    bool pre_index = (opcode >> 24) & 1;

    uint32_t base = gba->cpu.r[rn];
    int count = __builtin_popcount(rlist);

    // Calculate start address
    uint32_t addr;
    if (add) {
        addr = pre_index ? base + 4 : base;
    } else {
        addr = pre_index ? base - count * 4 : base - count * 4 + 4;
    }

    // Process registers
    for (int i = 0; i < 16; i++) {
        if (!(rlist & (1 << i))) continue;

        if (is_load) {
            gba->cpu.r[i] = memory_read32(gba, addr);
            if (i == 15) {
                gba->cpu.pipeline_invalid = true;
                if (s_bit) {
                    // Restore CPSR from SPSR
                    uint8_t mode = gba->cpu.cpsr & 0x1F;
                    uint32_t *spsr = NULL;
                    switch (mode) {
                        case CPU_MODE_FIQ: spsr = &gba->cpu.spsr[0]; break;
                        case CPU_MODE_SVC: spsr = &gba->cpu.spsr[1]; break;
                        case CPU_MODE_ABT: spsr = &gba->cpu.spsr[2]; break;
                        case CPU_MODE_IRQ: spsr = &gba->cpu.spsr[3]; break;
                        case CPU_MODE_UND: spsr = &gba->cpu.spsr[4]; break;
                    }
                    if (spsr) {
                        cpu_switch_mode(gba, *spsr & 0x1F);
                        gba->cpu.cpsr = *spsr;
                    }
                }
            }
        } else {
            uint32_t value = gba->cpu.r[i];
            if (i == 15) value += 4;
            memory_write32(gba, addr, value);
        }

        addr += 4;
    }

    // Writeback
    if (writeback) {
        if (add) {
            gba->cpu.r[rn] = base + count * 4;
        } else {
            gba->cpu.r[rn] = base - count * 4;
        }
    }

    return count + (is_load ? 2 : 1);
}

// Branch instructions
static uint32_t arm_branch(gba_t *gba, uint32_t opcode)
{
    bool link = (opcode >> 24) & 1;
    int32_t offset = (int32_t)(opcode << 8) >> 6;  // Sign extend and *4

    if (link) {
        gba->cpu.r[14] = gba->cpu.r[15] - 4;
    }

    gba->cpu.r[15] += offset;
    gba->cpu.pipeline_invalid = true;

    return 3;
}

// Branch and Exchange (BX)
static uint32_t arm_branch_exchange(gba_t *gba, uint32_t opcode)
{
    uint8_t rm = opcode & 0xF;
    uint32_t addr = gba->cpu.r[rm];

    if (addr & 1) {
        // Switch to THUMB mode
        gba->cpu.cpsr |= CPSR_T;
        gba->cpu.r[15] = addr & ~1;
    } else {
        // Stay in ARM mode
        gba->cpu.cpsr &= ~CPSR_T;
        gba->cpu.r[15] = addr & ~3;
    }

    gba->cpu.pipeline_invalid = true;
    return 3;
}

// Software Interrupt
static uint32_t arm_swi(gba_t *gba, uint32_t opcode)
{
    uint8_t swi_num = (opcode >> 16) & 0xFF;

    if (gba->mem.bios_loaded) {
        // Save state
        uint8_t old_mode = gba->cpu.cpsr & 0x1F;
        gba->cpu.spsr[1] = gba->cpu.cpsr;  // Save to SVC SPSR

        // Switch to SVC mode
        cpu_switch_mode(gba, CPU_MODE_SVC);
        gba->cpu.cpsr |= CPSR_I;
        gba->cpu.cpsr &= ~CPSR_T;

        gba->cpu.r[14] = gba->cpu.r[15] - 4;
        gba->cpu.r[15] = 0x08;  // SWI vector
        gba->cpu.pipeline_invalid = true;
    } else {
        // HLE BIOS call
        bios_call(gba, swi_num);
    }

    return 3;
}

// MRS - Move PSR to Register
static uint32_t arm_mrs(gba_t *gba, uint32_t opcode)
{
    uint8_t rd = (opcode >> 12) & 0xF;
    bool use_spsr = (opcode >> 22) & 1;

    if (use_spsr) {
        uint8_t mode = gba->cpu.cpsr & 0x1F;
        uint32_t *spsr = NULL;
        switch (mode) {
            case CPU_MODE_FIQ: spsr = &gba->cpu.spsr[0]; break;
            case CPU_MODE_SVC: spsr = &gba->cpu.spsr[1]; break;
            case CPU_MODE_ABT: spsr = &gba->cpu.spsr[2]; break;
            case CPU_MODE_IRQ: spsr = &gba->cpu.spsr[3]; break;
            case CPU_MODE_UND: spsr = &gba->cpu.spsr[4]; break;
        }
        gba->cpu.r[rd] = spsr ? *spsr : gba->cpu.cpsr;
    } else {
        gba->cpu.r[rd] = gba->cpu.cpsr;
    }

    return 1;
}

// MSR - Move Register to PSR
static uint32_t arm_msr(gba_t *gba, uint32_t opcode)
{
    bool use_spsr = (opcode >> 22) & 1;
    uint32_t mask = 0;

    if (opcode & (1 << 16)) mask |= 0x000000FF;  // Control
    if (opcode & (1 << 17)) mask |= 0x0000FF00;  // Extension
    if (opcode & (1 << 18)) mask |= 0x00FF0000;  // Status
    if (opcode & (1 << 19)) mask |= 0xFF000000;  // Flags

    uint32_t value;
    if (opcode & (1 << 25)) {
        // Immediate
        uint32_t imm = opcode & 0xFF;
        uint8_t rotate = ((opcode >> 8) & 0xF) * 2;
        value = (imm >> rotate) | (imm << (32 - rotate));
    } else {
        value = gba->cpu.r[opcode & 0xF];
    }

    // User mode can only modify flags
    uint8_t mode = gba->cpu.cpsr & 0x1F;
    if (mode == CPU_MODE_USER) {
        mask &= 0xFF000000;
    }

    if (use_spsr) {
        uint32_t *spsr = NULL;
        switch (mode) {
            case CPU_MODE_FIQ: spsr = &gba->cpu.spsr[0]; break;
            case CPU_MODE_SVC: spsr = &gba->cpu.spsr[1]; break;
            case CPU_MODE_ABT: spsr = &gba->cpu.spsr[2]; break;
            case CPU_MODE_IRQ: spsr = &gba->cpu.spsr[3]; break;
            case CPU_MODE_UND: spsr = &gba->cpu.spsr[4]; break;
        }
        if (spsr) {
            *spsr = (*spsr & ~mask) | (value & mask);
        }
    } else {
        uint8_t new_mode = (mask & 0x1F) ? (value & 0x1F) : mode;
        if (new_mode != mode && (mask & 0x1F)) {
            cpu_switch_mode(gba, new_mode);
        }
        gba->cpu.cpsr = (gba->cpu.cpsr & ~mask) | (value & mask);
    }

    return 1;
}

// Single Data Swap (SWP)
static uint32_t arm_swap(gba_t *gba, uint32_t opcode)
{
    uint8_t rn = (opcode >> 16) & 0xF;
    uint8_t rd = (opcode >> 12) & 0xF;
    uint8_t rm = opcode & 0xF;
    bool is_byte = (opcode >> 22) & 1;

    uint32_t addr = gba->cpu.r[rn];

    if (is_byte) {
        uint8_t tmp = memory_read8(gba, addr);
        memory_write8(gba, addr, gba->cpu.r[rm] & 0xFF);
        gba->cpu.r[rd] = tmp;
    } else {
        uint32_t tmp = memory_read32(gba, addr & ~3);
        memory_write32(gba, addr & ~3, gba->cpu.r[rm]);
        gba->cpu.r[rd] = tmp;
    }

    return 4;
}

// Main ARM instruction decoder
uint32_t cpu_execute_arm(gba_t *gba, uint32_t opcode)
{
    // Decode instruction format
    uint8_t bits27_20 = (opcode >> 20) & 0xFF;
    uint8_t bits7_4 = (opcode >> 4) & 0xF;

    // Branch and Exchange
    if ((opcode & 0x0FFFFFF0) == 0x012FFF10) {
        return arm_branch_exchange(gba, opcode);
    }

    // Software Interrupt
    if ((opcode & 0x0F000000) == 0x0F000000) {
        return arm_swi(gba, opcode);
    }

    // Branch
    if ((opcode & 0x0E000000) == 0x0A000000) {
        return arm_branch(gba, opcode);
    }

    // Block Data Transfer
    if ((opcode & 0x0E000000) == 0x08000000) {
        return arm_block_transfer(gba, opcode);
    }

    // Single Data Transfer
    if ((opcode & 0x0C000000) == 0x04000000) {
        return arm_single_transfer(gba, opcode);
    }

    // Multiply Long
    if ((opcode & 0x0F8000F0) == 0x00800090) {
        return arm_multiply_long(gba, opcode);
    }

    // Multiply
    if ((opcode & 0x0FC000F0) == 0x00000090) {
        return arm_multiply(gba, opcode);
    }

    // Single Data Swap
    if ((opcode & 0x0FB00FF0) == 0x01000090) {
        return arm_swap(gba, opcode);
    }

    // Halfword/Signed Transfer
    if ((opcode & 0x0E000090) == 0x00000090 && (bits7_4 & 0x9) == 0x9) {
        return arm_halfword_transfer(gba, opcode);
    }

    // MRS
    if ((opcode & 0x0FBF0FFF) == 0x010F0000) {
        return arm_mrs(gba, opcode);
    }

    // MSR
    if ((opcode & 0x0DB0F000) == 0x0120F000) {
        return arm_msr(gba, opcode);
    }

    // Data Processing
    if ((opcode & 0x0C000000) == 0x00000000) {
        return arm_data_processing(gba, opcode);
    }

    // Undefined instruction
    return 1;
}
