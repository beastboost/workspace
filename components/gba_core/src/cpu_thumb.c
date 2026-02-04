/**
 * GBA Emulator Core - THUMB Instruction Set Implementation
 */

#include "gba_internal.h"

// Forward declarations
extern uint32_t cpu_alu_add(gba_t *gba, uint32_t a, uint32_t b, bool set_flags);
extern uint32_t cpu_alu_sub(gba_t *gba, uint32_t a, uint32_t b, bool set_flags);
extern uint32_t cpu_alu_adc(gba_t *gba, uint32_t a, uint32_t b, bool set_flags);
extern uint32_t cpu_alu_sbc(gba_t *gba, uint32_t a, uint32_t b, bool set_flags);
extern void cpu_switch_mode(gba_t *gba, uint8_t new_mode);

// Format 1: Move shifted register
static uint32_t thumb_shift(gba_t *gba, uint16_t opcode)
{
    uint8_t op = (opcode >> 11) & 0x3;
    uint8_t offset = (opcode >> 6) & 0x1F;
    uint8_t rs = (opcode >> 3) & 0x7;
    uint8_t rd = opcode & 0x7;

    uint32_t value = gba->cpu.r[rs];
    bool carry = GET_FLAG_C();

    switch (op) {
        case 0: // LSL
            if (offset > 0) {
                carry = (value >> (32 - offset)) & 1;
                value <<= offset;
            }
            break;
        case 1: // LSR
            if (offset == 0) offset = 32;
            carry = (value >> (offset - 1)) & 1;
            value = (offset < 32) ? (value >> offset) : 0;
            break;
        case 2: // ASR
            if (offset == 0) offset = 32;
            carry = ((int32_t)value >> (offset - 1)) & 1;
            value = (int32_t)value >> ((offset < 32) ? offset : 31);
            break;
    }

    gba->cpu.r[rd] = value;
    SET_FLAG_N(value & 0x80000000);
    SET_FLAG_Z(value == 0);
    SET_FLAG_C(carry);

    return 1;
}

// Format 2: Add/Subtract
static uint32_t thumb_add_sub(gba_t *gba, uint16_t opcode)
{
    uint8_t rd = opcode & 0x7;
    uint8_t rs = (opcode >> 3) & 0x7;
    uint8_t rn_imm = (opcode >> 6) & 0x7;
    bool is_imm = (opcode >> 10) & 1;
    bool is_sub = (opcode >> 9) & 1;

    uint32_t op1 = gba->cpu.r[rs];
    uint32_t op2 = is_imm ? rn_imm : gba->cpu.r[rn_imm];

    if (is_sub) {
        gba->cpu.r[rd] = cpu_alu_sub(gba, op1, op2, true);
    } else {
        gba->cpu.r[rd] = cpu_alu_add(gba, op1, op2, true);
    }

    return 1;
}

// Format 3: Move/Compare/Add/Subtract immediate
static uint32_t thumb_imm_ops(gba_t *gba, uint16_t opcode)
{
    uint8_t op = (opcode >> 11) & 0x3;
    uint8_t rd = (opcode >> 8) & 0x7;
    uint8_t imm = opcode & 0xFF;

    switch (op) {
        case 0: // MOV
            gba->cpu.r[rd] = imm;
            SET_FLAG_N(0);
            SET_FLAG_Z(imm == 0);
            break;
        case 1: // CMP
            cpu_alu_sub(gba, gba->cpu.r[rd], imm, true);
            break;
        case 2: // ADD
            gba->cpu.r[rd] = cpu_alu_add(gba, gba->cpu.r[rd], imm, true);
            break;
        case 3: // SUB
            gba->cpu.r[rd] = cpu_alu_sub(gba, gba->cpu.r[rd], imm, true);
            break;
    }

    return 1;
}

// Format 4: ALU operations
static uint32_t thumb_alu(gba_t *gba, uint16_t opcode)
{
    uint8_t op = (opcode >> 6) & 0xF;
    uint8_t rs = (opcode >> 3) & 0x7;
    uint8_t rd = opcode & 0x7;

    uint32_t a = gba->cpu.r[rd];
    uint32_t b = gba->cpu.r[rs];
    uint32_t result;
    uint32_t cycles = 1;

    switch (op) {
        case 0x0: // AND
            result = a & b;
            SET_FLAG_N(result & 0x80000000);
            SET_FLAG_Z(result == 0);
            gba->cpu.r[rd] = result;
            break;

        case 0x1: // EOR
            result = a ^ b;
            SET_FLAG_N(result & 0x80000000);
            SET_FLAG_Z(result == 0);
            gba->cpu.r[rd] = result;
            break;

        case 0x2: // LSL
            b &= 0xFF;
            if (b > 0) {
                if (b < 32) {
                    SET_FLAG_C((a >> (32 - b)) & 1);
                    result = a << b;
                } else if (b == 32) {
                    SET_FLAG_C(a & 1);
                    result = 0;
                } else {
                    SET_FLAG_C(0);
                    result = 0;
                }
            } else {
                result = a;
            }
            SET_FLAG_N(result & 0x80000000);
            SET_FLAG_Z(result == 0);
            gba->cpu.r[rd] = result;
            cycles = 2;
            break;

        case 0x3: // LSR
            b &= 0xFF;
            if (b > 0) {
                if (b < 32) {
                    SET_FLAG_C((a >> (b - 1)) & 1);
                    result = a >> b;
                } else if (b == 32) {
                    SET_FLAG_C((a >> 31) & 1);
                    result = 0;
                } else {
                    SET_FLAG_C(0);
                    result = 0;
                }
            } else {
                result = a;
            }
            SET_FLAG_N(result & 0x80000000);
            SET_FLAG_Z(result == 0);
            gba->cpu.r[rd] = result;
            cycles = 2;
            break;

        case 0x4: // ASR
            b &= 0xFF;
            if (b > 0) {
                if (b < 32) {
                    SET_FLAG_C(((int32_t)a >> (b - 1)) & 1);
                    result = (int32_t)a >> b;
                } else {
                    SET_FLAG_C((a >> 31) & 1);
                    result = (int32_t)a >> 31;
                }
            } else {
                result = a;
            }
            SET_FLAG_N(result & 0x80000000);
            SET_FLAG_Z(result == 0);
            gba->cpu.r[rd] = result;
            cycles = 2;
            break;

        case 0x5: // ADC
            gba->cpu.r[rd] = cpu_alu_adc(gba, a, b, true);
            break;

        case 0x6: // SBC
            gba->cpu.r[rd] = cpu_alu_sbc(gba, a, b, true);
            break;

        case 0x7: // ROR
            b &= 0xFF;
            if (b > 0) {
                b &= 0x1F;
                if (b == 0) {
                    SET_FLAG_C((a >> 31) & 1);
                    result = a;
                } else {
                    SET_FLAG_C((a >> (b - 1)) & 1);
                    result = (a >> b) | (a << (32 - b));
                }
            } else {
                result = a;
            }
            SET_FLAG_N(result & 0x80000000);
            SET_FLAG_Z(result == 0);
            gba->cpu.r[rd] = result;
            cycles = 2;
            break;

        case 0x8: // TST
            result = a & b;
            SET_FLAG_N(result & 0x80000000);
            SET_FLAG_Z(result == 0);
            break;

        case 0x9: // NEG
            gba->cpu.r[rd] = cpu_alu_sub(gba, 0, b, true);
            break;

        case 0xA: // CMP
            cpu_alu_sub(gba, a, b, true);
            break;

        case 0xB: // CMN
            cpu_alu_add(gba, a, b, true);
            break;

        case 0xC: // ORR
            result = a | b;
            SET_FLAG_N(result & 0x80000000);
            SET_FLAG_Z(result == 0);
            gba->cpu.r[rd] = result;
            break;

        case 0xD: // MUL
            result = a * b;
            SET_FLAG_N(result & 0x80000000);
            SET_FLAG_Z(result == 0);
            gba->cpu.r[rd] = result;
            cycles = 3;
            break;

        case 0xE: // BIC
            result = a & ~b;
            SET_FLAG_N(result & 0x80000000);
            SET_FLAG_Z(result == 0);
            gba->cpu.r[rd] = result;
            break;

        case 0xF: // MVN
            result = ~b;
            SET_FLAG_N(result & 0x80000000);
            SET_FLAG_Z(result == 0);
            gba->cpu.r[rd] = result;
            break;
    }

    return cycles;
}

// Format 5: Hi register operations / Branch exchange
static uint32_t thumb_hi_reg_bx(gba_t *gba, uint16_t opcode)
{
    uint8_t op = (opcode >> 8) & 0x3;
    uint8_t rs = ((opcode >> 3) & 0x7) | ((opcode >> 3) & 0x8);
    uint8_t rd = (opcode & 0x7) | ((opcode >> 4) & 0x8);

    uint32_t a = gba->cpu.r[rd];
    uint32_t b = gba->cpu.r[rs];
    if (rs == 15) b += 2;
    if (rd == 15) a += 2;

    switch (op) {
        case 0: // ADD
            gba->cpu.r[rd] = a + b;
            if (rd == 15) {
                gba->cpu.r[15] &= ~1;
                gba->cpu.pipeline_invalid = true;
            }
            break;

        case 1: // CMP
            cpu_alu_sub(gba, a, b, true);
            break;

        case 2: // MOV
            gba->cpu.r[rd] = b;
            if (rd == 15) {
                gba->cpu.r[15] &= ~1;
                gba->cpu.pipeline_invalid = true;
            }
            break;

        case 3: // BX
            if (b & 1) {
                // Stay in THUMB
                gba->cpu.r[15] = b & ~1;
            } else {
                // Switch to ARM
                gba->cpu.cpsr &= ~CPSR_T;
                gba->cpu.r[15] = b & ~3;
            }
            gba->cpu.pipeline_invalid = true;
            break;
    }

    return (op == 3 || (op != 1 && rd == 15)) ? 3 : 1;
}

// Format 6: PC-relative load
static uint32_t thumb_pc_load(gba_t *gba, uint16_t opcode)
{
    uint8_t rd = (opcode >> 8) & 0x7;
    uint16_t offset = (opcode & 0xFF) << 2;

    uint32_t addr = (gba->cpu.r[15] & ~2) + offset;
    gba->cpu.r[rd] = memory_read32(gba, addr);

    return 3;
}

// Format 7: Load/Store with register offset
static uint32_t thumb_reg_offset(gba_t *gba, uint16_t opcode)
{
    uint8_t ro = (opcode >> 6) & 0x7;
    uint8_t rb = (opcode >> 3) & 0x7;
    uint8_t rd = opcode & 0x7;
    bool is_load = (opcode >> 11) & 1;
    bool is_byte = (opcode >> 10) & 1;

    uint32_t addr = gba->cpu.r[rb] + gba->cpu.r[ro];

    if (is_load) {
        if (is_byte) {
            gba->cpu.r[rd] = memory_read8(gba, addr);
        } else {
            gba->cpu.r[rd] = memory_read32(gba, addr & ~3);
            uint8_t rotate = (addr & 3) * 8;
            if (rotate) {
                gba->cpu.r[rd] = (gba->cpu.r[rd] >> rotate) | (gba->cpu.r[rd] << (32 - rotate));
            }
        }
        return 3;
    } else {
        if (is_byte) {
            memory_write8(gba, addr, gba->cpu.r[rd] & 0xFF);
        } else {
            memory_write32(gba, addr & ~3, gba->cpu.r[rd]);
        }
        return 2;
    }
}

// Format 8: Load/Store sign-extended byte/halfword
static uint32_t thumb_sign_extend(gba_t *gba, uint16_t opcode)
{
    uint8_t ro = (opcode >> 6) & 0x7;
    uint8_t rb = (opcode >> 3) & 0x7;
    uint8_t rd = opcode & 0x7;
    uint8_t op = (opcode >> 10) & 0x3;

    uint32_t addr = gba->cpu.r[rb] + gba->cpu.r[ro];

    switch (op) {
        case 0: // STRH
            memory_write16(gba, addr & ~1, gba->cpu.r[rd] & 0xFFFF);
            return 2;
        case 1: // LDSB
            gba->cpu.r[rd] = (int32_t)(int8_t)memory_read8(gba, addr);
            return 3;
        case 2: // LDRH
            gba->cpu.r[rd] = memory_read16(gba, addr & ~1);
            return 3;
        case 3: // LDSH
            gba->cpu.r[rd] = (int32_t)(int16_t)memory_read16(gba, addr & ~1);
            return 3;
    }

    return 1;
}

// Format 9: Load/Store with immediate offset
static uint32_t thumb_imm_offset(gba_t *gba, uint16_t opcode)
{
    uint8_t rb = (opcode >> 3) & 0x7;
    uint8_t rd = opcode & 0x7;
    bool is_load = (opcode >> 11) & 1;
    bool is_byte = (opcode >> 12) & 1;
    uint8_t offset = (opcode >> 6) & 0x1F;

    uint32_t addr;
    if (is_byte) {
        addr = gba->cpu.r[rb] + offset;
    } else {
        addr = gba->cpu.r[rb] + (offset << 2);
    }

    if (is_load) {
        if (is_byte) {
            gba->cpu.r[rd] = memory_read8(gba, addr);
        } else {
            gba->cpu.r[rd] = memory_read32(gba, addr & ~3);
            uint8_t rotate = (addr & 3) * 8;
            if (rotate) {
                gba->cpu.r[rd] = (gba->cpu.r[rd] >> rotate) | (gba->cpu.r[rd] << (32 - rotate));
            }
        }
        return 3;
    } else {
        if (is_byte) {
            memory_write8(gba, addr, gba->cpu.r[rd] & 0xFF);
        } else {
            memory_write32(gba, addr & ~3, gba->cpu.r[rd]);
        }
        return 2;
    }
}

// Format 10: Load/Store halfword
static uint32_t thumb_halfword(gba_t *gba, uint16_t opcode)
{
    uint8_t rb = (opcode >> 3) & 0x7;
    uint8_t rd = opcode & 0x7;
    bool is_load = (opcode >> 11) & 1;
    uint8_t offset = ((opcode >> 6) & 0x1F) << 1;

    uint32_t addr = gba->cpu.r[rb] + offset;

    if (is_load) {
        gba->cpu.r[rd] = memory_read16(gba, addr & ~1);
        return 3;
    } else {
        memory_write16(gba, addr & ~1, gba->cpu.r[rd] & 0xFFFF);
        return 2;
    }
}

// Format 11: SP-relative Load/Store
static uint32_t thumb_sp_relative(gba_t *gba, uint16_t opcode)
{
    uint8_t rd = (opcode >> 8) & 0x7;
    uint16_t offset = (opcode & 0xFF) << 2;
    bool is_load = (opcode >> 11) & 1;

    uint32_t addr = gba->cpu.r[13] + offset;

    if (is_load) {
        gba->cpu.r[rd] = memory_read32(gba, addr);
        return 3;
    } else {
        memory_write32(gba, addr, gba->cpu.r[rd]);
        return 2;
    }
}

// Format 12: Load address
static uint32_t thumb_load_addr(gba_t *gba, uint16_t opcode)
{
    uint8_t rd = (opcode >> 8) & 0x7;
    uint16_t offset = (opcode & 0xFF) << 2;
    bool use_sp = (opcode >> 11) & 1;

    if (use_sp) {
        gba->cpu.r[rd] = gba->cpu.r[13] + offset;
    } else {
        gba->cpu.r[rd] = (gba->cpu.r[15] & ~2) + offset;
    }

    return 1;
}

// Format 13: Add offset to SP
static uint32_t thumb_sp_offset(gba_t *gba, uint16_t opcode)
{
    uint16_t offset = (opcode & 0x7F) << 2;
    bool negative = (opcode >> 7) & 1;

    if (negative) {
        gba->cpu.r[13] -= offset;
    } else {
        gba->cpu.r[13] += offset;
    }

    return 1;
}

// Format 14: Push/Pop registers
static uint32_t thumb_push_pop(gba_t *gba, uint16_t opcode)
{
    uint8_t rlist = opcode & 0xFF;
    bool is_pop = (opcode >> 11) & 1;
    bool pc_lr = (opcode >> 8) & 1;

    int count = __builtin_popcount(rlist) + (pc_lr ? 1 : 0);
    uint32_t addr = gba->cpu.r[13];

    if (is_pop) {
        // POP
        for (int i = 0; i < 8; i++) {
            if (rlist & (1 << i)) {
                gba->cpu.r[i] = memory_read32(gba, addr);
                addr += 4;
            }
        }
        if (pc_lr) {
            gba->cpu.r[15] = memory_read32(gba, addr) & ~1;
            addr += 4;
            gba->cpu.pipeline_invalid = true;
        }
        gba->cpu.r[13] = addr;
        return count + 2;
    } else {
        // PUSH
        addr -= count * 4;
        gba->cpu.r[13] = addr;

        for (int i = 0; i < 8; i++) {
            if (rlist & (1 << i)) {
                memory_write32(gba, addr, gba->cpu.r[i]);
                addr += 4;
            }
        }
        if (pc_lr) {
            memory_write32(gba, addr, gba->cpu.r[14]);
        }
        return count + 1;
    }
}

// Format 15: Multiple Load/Store
static uint32_t thumb_ldm_stm(gba_t *gba, uint16_t opcode)
{
    uint8_t rb = (opcode >> 8) & 0x7;
    uint8_t rlist = opcode & 0xFF;
    bool is_load = (opcode >> 11) & 1;

    int count = __builtin_popcount(rlist);
    uint32_t addr = gba->cpu.r[rb];

    if (is_load) {
        for (int i = 0; i < 8; i++) {
            if (rlist & (1 << i)) {
                gba->cpu.r[i] = memory_read32(gba, addr);
                addr += 4;
            }
        }
    } else {
        for (int i = 0; i < 8; i++) {
            if (rlist & (1 << i)) {
                memory_write32(gba, addr, gba->cpu.r[i]);
                addr += 4;
            }
        }
    }

    // Writeback (unless rb is in rlist for LDMIA)
    if (!(is_load && (rlist & (1 << rb)))) {
        gba->cpu.r[rb] = addr;
    }

    return count + (is_load ? 2 : 1);
}

// Format 16: Conditional branch
static uint32_t thumb_cond_branch(gba_t *gba, uint16_t opcode)
{
    uint8_t cond = (opcode >> 8) & 0xF;
    int8_t offset = opcode & 0xFF;

    bool n = GET_FLAG_N();
    bool z = GET_FLAG_Z();
    bool c = GET_FLAG_C();
    bool v = GET_FLAG_V();

    bool take = false;
    switch (cond) {
        case 0x0: take = z; break;           // BEQ
        case 0x1: take = !z; break;          // BNE
        case 0x2: take = c; break;           // BCS
        case 0x3: take = !c; break;          // BCC
        case 0x4: take = n; break;           // BMI
        case 0x5: take = !n; break;          // BPL
        case 0x6: take = v; break;           // BVS
        case 0x7: take = !v; break;          // BVC
        case 0x8: take = c && !z; break;     // BHI
        case 0x9: take = !c || z; break;     // BLS
        case 0xA: take = n == v; break;      // BGE
        case 0xB: take = n != v; break;      // BLT
        case 0xC: take = !z && (n == v); break; // BGT
        case 0xD: take = z || (n != v); break;  // BLE
    }

    if (take) {
        gba->cpu.r[15] += (int32_t)offset * 2;
        gba->cpu.pipeline_invalid = true;
        return 3;
    }

    return 1;
}

// Format 17: Software Interrupt
static uint32_t thumb_swi(gba_t *gba, uint16_t opcode)
{
    uint8_t swi_num = opcode & 0xFF;

    if (gba->mem.bios_loaded) {
        // Save state
        gba->cpu.spsr[1] = gba->cpu.cpsr;

        // Switch to SVC mode
        cpu_switch_mode(gba, CPU_MODE_SVC);
        gba->cpu.cpsr |= CPSR_I;
        gba->cpu.cpsr &= ~CPSR_T;

        gba->cpu.r[14] = gba->cpu.r[15] - 2;
        gba->cpu.r[15] = 0x08;
        gba->cpu.pipeline_invalid = true;
    } else {
        // HLE BIOS call
        bios_call(gba, swi_num);
    }

    return 3;
}

// Format 18: Unconditional branch
static uint32_t thumb_branch(gba_t *gba, uint16_t opcode)
{
    int16_t offset = (opcode & 0x7FF) << 1;
    if (offset & 0x800) offset |= 0xF000;  // Sign extend

    gba->cpu.r[15] += offset;
    gba->cpu.pipeline_invalid = true;

    return 3;
}

// Format 19: Long branch with link
static uint32_t thumb_long_branch(gba_t *gba, uint16_t opcode)
{
    bool is_second = (opcode >> 11) & 1;
    uint16_t offset = opcode & 0x7FF;

    if (!is_second) {
        // First instruction: LR = PC + (offset << 12)
        int32_t off = ((int32_t)offset << 21) >> 9;  // Sign extend
        gba->cpu.r[14] = gba->cpu.r[15] + off;
        return 1;
    } else {
        // Second instruction: PC = LR + (offset << 1), LR = old_PC | 1
        uint32_t temp = gba->cpu.r[15] - 2;
        gba->cpu.r[15] = gba->cpu.r[14] + (offset << 1);
        gba->cpu.r[14] = temp | 1;
        gba->cpu.pipeline_invalid = true;
        return 3;
    }
}

// Main THUMB instruction decoder
uint32_t cpu_execute_thumb(gba_t *gba, uint16_t opcode)
{
    uint8_t bits15_8 = opcode >> 8;

    // Format 19: Long branch with link
    if ((opcode & 0xF000) == 0xF000) {
        return thumb_long_branch(gba, opcode);
    }

    // Format 18: Unconditional branch
    if ((opcode & 0xF800) == 0xE000) {
        return thumb_branch(gba, opcode);
    }

    // Format 17: SWI
    if ((opcode & 0xFF00) == 0xDF00) {
        return thumb_swi(gba, opcode);
    }

    // Format 16: Conditional branch
    if ((opcode & 0xF000) == 0xD000) {
        return thumb_cond_branch(gba, opcode);
    }

    // Format 15: Multiple load/store
    if ((opcode & 0xF000) == 0xC000) {
        return thumb_ldm_stm(gba, opcode);
    }

    // Format 14: Push/Pop
    if ((opcode & 0xF600) == 0xB400) {
        return thumb_push_pop(gba, opcode);
    }

    // Format 13: Add offset to SP
    if ((opcode & 0xFF00) == 0xB000) {
        return thumb_sp_offset(gba, opcode);
    }

    // Format 12: Load address
    if ((opcode & 0xF000) == 0xA000) {
        return thumb_load_addr(gba, opcode);
    }

    // Format 11: SP-relative load/store
    if ((opcode & 0xF000) == 0x9000) {
        return thumb_sp_relative(gba, opcode);
    }

    // Format 10: Load/store halfword
    if ((opcode & 0xF000) == 0x8000) {
        return thumb_halfword(gba, opcode);
    }

    // Format 9: Load/store with immediate offset
    if ((opcode & 0xE000) == 0x6000) {
        return thumb_imm_offset(gba, opcode);
    }

    // Format 8: Load/store sign-extended
    if ((opcode & 0xF200) == 0x5200) {
        return thumb_sign_extend(gba, opcode);
    }

    // Format 7: Load/store with register offset
    if ((opcode & 0xF200) == 0x5000) {
        return thumb_reg_offset(gba, opcode);
    }

    // Format 6: PC-relative load
    if ((opcode & 0xF800) == 0x4800) {
        return thumb_pc_load(gba, opcode);
    }

    // Format 5: Hi register operations / BX
    if ((opcode & 0xFC00) == 0x4400) {
        return thumb_hi_reg_bx(gba, opcode);
    }

    // Format 4: ALU operations
    if ((opcode & 0xFC00) == 0x4000) {
        return thumb_alu(gba, opcode);
    }

    // Format 3: Move/Compare/Add/Sub immediate
    if ((opcode & 0xE000) == 0x2000) {
        return thumb_imm_ops(gba, opcode);
    }

    // Format 2: Add/Subtract
    if ((opcode & 0xF800) == 0x1800) {
        return thumb_add_sub(gba, opcode);
    }

    // Format 1: Move shifted register
    if ((opcode & 0xE000) == 0x0000) {
        return thumb_shift(gba, opcode);
    }

    // Unknown instruction
    return 1;
}
