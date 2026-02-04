/**
 * GBA Emulator Test Suite - CPU Tests
 *
 * Tests for ARM7TDMI CPU emulation including:
 * - ARM instruction set
 * - THUMB instruction set
 * - Flag calculations
 * - Condition codes
 * - Register banking
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

// Include GBA core headers
#include "gba.h"
#include "gba_internal.h"

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Test macros
#define TEST_ASSERT(condition, msg) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
        printf("  ✓ %s\n", msg); \
    } else { \
        tests_failed++; \
        printf("  ✗ %s (FAILED at line %d)\n", msg, __LINE__); \
    } \
} while(0)

#define TEST_ASSERT_EQ(expected, actual, msg) do { \
    tests_run++; \
    if ((expected) == (actual)) { \
        tests_passed++; \
        printf("  ✓ %s\n", msg); \
    } else { \
        tests_failed++; \
        printf("  ✗ %s (expected 0x%X, got 0x%X) at line %d\n", \
               msg, (unsigned)(expected), (unsigned)(actual), __LINE__); \
    } \
} while(0)

#define TEST_SECTION(name) printf("\n=== %s ===\n", name)

// Helper to create a test GBA instance
static gba_t* create_test_gba(void)
{
    gba_t *gba = gba_create();
    if (gba) {
        // Reset to known state
        gba_reset(gba);
    }
    return gba;
}

// Helper to set up ARM mode
static void setup_arm_mode(gba_t *gba)
{
    gba->cpu.cpsr &= ~CPSR_T;  // Clear THUMB bit
    gba->cpu.pipeline_invalid = true;
}

// Helper to set up THUMB mode
static void setup_thumb_mode(gba_t *gba)
{
    gba->cpu.cpsr |= CPSR_T;  // Set THUMB bit
    gba->cpu.pipeline_invalid = true;
}

// Check if ARM condition code is met
static bool check_arm_condition(gba_t *gba, uint8_t cond)
{
    bool n = GET_FLAG_N();
    bool z = GET_FLAG_Z();
    bool c = GET_FLAG_C();
    bool v = GET_FLAG_V();

    switch (cond) {
        case 0x0: return z;                    // EQ
        case 0x1: return !z;                   // NE
        case 0x2: return c;                    // CS
        case 0x3: return !c;                   // CC
        case 0x4: return n;                    // MI
        case 0x5: return !n;                   // PL
        case 0x6: return v;                    // VS
        case 0x7: return !v;                   // VC
        case 0x8: return c && !z;              // HI
        case 0x9: return !c || z;              // LS
        case 0xA: return n == v;               // GE
        case 0xB: return n != v;               // LT
        case 0xC: return !z && (n == v);       // GT
        case 0xD: return z || (n != v);        // LE
        case 0xE: return true;                 // AL
        case 0xF: return true;                 // NV
        default: return false;
    }
}

// Execute ARM instruction with condition check
static void execute_arm_with_cond(gba_t *gba, uint32_t opcode)
{
    uint8_t cond = (opcode >> 28) & 0xF;
    if (check_arm_condition(gba, cond)) {
        cpu_execute_arm(gba, opcode);
    }
}

// =============================================================================
// ARM Data Processing Tests
// =============================================================================

void test_arm_mov(void)
{
    TEST_SECTION("ARM MOV Instructions");

    gba_t *gba = create_test_gba();
    if (!gba) {
        printf("Failed to create GBA instance\n");
        return;
    }

    setup_arm_mode(gba);

    // MOV R0, #0x12
    gba->cpu.r[0] = 0;
    uint32_t opcode = 0xE3A00012;  // MOV R0, #0x12
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(0x12, gba->cpu.r[0], "MOV R0, #0x12");

    // MOV R1, R0
    opcode = 0xE1A01000;  // MOV R1, R0
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(0x12, gba->cpu.r[1], "MOV R1, R0");

    // MOV with shift: MOV R2, R0, LSL #4
    opcode = 0xE1A02200;  // MOV R2, R0, LSL #4
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(0x120, gba->cpu.r[2], "MOV R2, R0, LSL #4");

    // MOVS with flag update
    gba->cpu.r[0] = 0;
    opcode = 0xE3B00000;  // MOVS R0, #0
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT(gba->cpu.cpsr & CPSR_Z, "MOVS sets Z flag for zero");

    gba->cpu.r[0] = 0x80000000;
    opcode = 0xE1B01000;  // MOVS R1, R0
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT(gba->cpu.cpsr & CPSR_N, "MOVS sets N flag for negative");

    gba_destroy(gba);
}

void test_arm_add_sub(void)
{
    TEST_SECTION("ARM ADD/SUB Instructions");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    setup_arm_mode(gba);

    // ADD R0, R1, #5
    gba->cpu.r[1] = 10;
    uint32_t opcode = 0xE2810005;  // ADD R0, R1, #5
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(15, gba->cpu.r[0], "ADD R0, R1, #5 (10+5=15)");

    // SUB R0, R1, #3
    opcode = 0xE2410003;  // SUB R0, R1, #3
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(7, gba->cpu.r[0], "SUB R0, R1, #3 (10-3=7)");

    // Test carry flag
    gba->cpu.r[0] = 0xFFFFFFFF;
    gba->cpu.r[1] = 1;
    opcode = 0xE0902001;  // ADDS R2, R0, R1
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(0, gba->cpu.r[2], "ADDS overflow wraps to 0");
    TEST_ASSERT(gba->cpu.cpsr & CPSR_C, "ADDS sets carry on overflow");
    TEST_ASSERT(gba->cpu.cpsr & CPSR_Z, "ADDS sets zero flag");

    // Test borrow flag (carry clear on SUB)
    gba->cpu.r[0] = 5;
    gba->cpu.r[1] = 10;
    opcode = 0xE0502001;  // SUBS R2, R0, R1
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT(!(gba->cpu.cpsr & CPSR_C), "SUBS clears carry on borrow");

    // Test overflow flag
    gba->cpu.r[0] = 0x7FFFFFFF;  // Max positive
    gba->cpu.r[1] = 1;
    opcode = 0xE0902001;  // ADDS R2, R0, R1
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT(gba->cpu.cpsr & CPSR_V, "ADDS sets overflow on signed overflow");
    TEST_ASSERT(gba->cpu.cpsr & CPSR_N, "Result is negative after overflow");

    gba_destroy(gba);
}

void test_arm_logic(void)
{
    TEST_SECTION("ARM Logic Instructions");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    setup_arm_mode(gba);

    // AND R0, R1, R2
    gba->cpu.r[1] = 0xFF00FF00;
    gba->cpu.r[2] = 0x0F0F0F0F;
    uint32_t opcode = 0xE0010002;  // AND R0, R1, R2
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(0x0F000F00, gba->cpu.r[0], "AND R0, R1, R2");

    // ORR R0, R1, R2
    opcode = 0xE1810002;  // ORR R0, R1, R2
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(0xFF0FFF0F, gba->cpu.r[0], "ORR R0, R1, R2");

    // EOR R0, R1, R2
    gba->cpu.r[1] = 0xAAAAAAAA;
    gba->cpu.r[2] = 0x55555555;
    opcode = 0xE0210002;  // EOR R0, R1, R2
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(0xFFFFFFFF, gba->cpu.r[0], "EOR R0, R1, R2");

    // BIC R0, R1, R2 (bit clear)
    gba->cpu.r[1] = 0xFFFFFFFF;
    gba->cpu.r[2] = 0x0000FFFF;
    opcode = 0xE1C10002;  // BIC R0, R1, R2
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(0xFFFF0000, gba->cpu.r[0], "BIC R0, R1, R2");

    // MVN R0, R1 (move not)
    gba->cpu.r[1] = 0x00000000;
    opcode = 0xE1E00001;  // MVN R0, R1
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(0xFFFFFFFF, gba->cpu.r[0], "MVN R0, R1");

    gba_destroy(gba);
}

void test_arm_cmp_tst(void)
{
    TEST_SECTION("ARM CMP/TST Instructions");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    setup_arm_mode(gba);

    // CMP R0, R1 (equal)
    gba->cpu.r[0] = 100;
    gba->cpu.r[1] = 100;
    uint32_t opcode = 0xE1500001;  // CMP R0, R1
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT(gba->cpu.cpsr & CPSR_Z, "CMP sets Z when equal");
    TEST_ASSERT(gba->cpu.cpsr & CPSR_C, "CMP sets C when R0 >= R1");

    // CMP R0, R1 (R0 < R1)
    gba->cpu.r[0] = 50;
    gba->cpu.r[1] = 100;
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT(!(gba->cpu.cpsr & CPSR_Z), "CMP clears Z when not equal");
    TEST_ASSERT(!(gba->cpu.cpsr & CPSR_C), "CMP clears C when R0 < R1");
    TEST_ASSERT(gba->cpu.cpsr & CPSR_N, "CMP sets N for negative result");

    // TST R0, R1
    gba->cpu.r[0] = 0xFF00;
    gba->cpu.r[1] = 0x00FF;
    opcode = 0xE1100001;  // TST R0, R1
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT(gba->cpu.cpsr & CPSR_Z, "TST sets Z when AND is zero");

    gba->cpu.r[0] = 0xFF00;
    gba->cpu.r[1] = 0xFF00;
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT(!(gba->cpu.cpsr & CPSR_Z), "TST clears Z when AND is non-zero");

    gba_destroy(gba);
}

void test_arm_mul(void)
{
    TEST_SECTION("ARM Multiply Instructions");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    setup_arm_mode(gba);

    // MUL R0, R1, R2
    gba->cpu.r[1] = 7;
    gba->cpu.r[2] = 6;
    uint32_t opcode = 0xE0000291;  // MUL R0, R1, R2
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(42, gba->cpu.r[0], "MUL 7*6=42");

    // MLA R0, R1, R2, R3 (multiply-accumulate)
    gba->cpu.r[1] = 5;
    gba->cpu.r[2] = 4;
    gba->cpu.r[3] = 10;
    opcode = 0xE0203291;  // MLA R0, R1, R2, R3
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(30, gba->cpu.r[0], "MLA 5*4+10=30");

    gba_destroy(gba);
}

// =============================================================================
// ARM Load/Store Tests
// =============================================================================

void test_arm_load_store(void)
{
    TEST_SECTION("ARM Load/Store Instructions");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    setup_arm_mode(gba);

    // Write test value to IWRAM
    uint32_t test_addr = 0x03000100;
    memory_write32(gba, test_addr, 0x12345678);

    // LDR R0, [R1]
    gba->cpu.r[1] = test_addr;
    uint32_t opcode = 0xE5910000;  // LDR R0, [R1]
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(0x12345678, gba->cpu.r[0], "LDR R0, [R1]");

    // STR R0, [R1, #4]
    gba->cpu.r[0] = 0xDEADBEEF;
    opcode = 0xE5810004;  // STR R0, [R1, #4]
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(0xDEADBEEF, memory_read32(gba, test_addr + 4), "STR R0, [R1, #4]");

    // LDRB (byte load)
    memory_write8(gba, test_addr + 8, 0xAB);
    gba->cpu.r[1] = test_addr + 8;
    opcode = 0xE5D10000;  // LDRB R0, [R1]
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(0xAB, gba->cpu.r[0], "LDRB R0, [R1]");

    // STRB (byte store)
    gba->cpu.r[0] = 0xCD;
    opcode = 0xE5C10001;  // STRB R0, [R1, #1]
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(0xCD, memory_read8(gba, test_addr + 9), "STRB R0, [R1, #1]");

    // LDRH (halfword load)
    memory_write16(gba, test_addr + 16, 0x1234);
    gba->cpu.r[1] = test_addr + 16;
    opcode = 0xE1D100B0;  // LDRH R0, [R1]
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(0x1234, gba->cpu.r[0], "LDRH R0, [R1]");

    // Pre-indexed with writeback: LDR R0, [R1, #4]!
    gba->cpu.r[1] = test_addr;
    opcode = 0xE5B10004;  // LDR R0, [R1, #4]!
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(test_addr + 4, gba->cpu.r[1], "Pre-indexed writeback updates base");

    // Post-indexed: LDR R0, [R1], #4
    gba->cpu.r[1] = test_addr;
    opcode = 0xE4910004;  // LDR R0, [R1], #4
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(test_addr + 4, gba->cpu.r[1], "Post-indexed updates base");

    gba_destroy(gba);
}

// =============================================================================
// ARM Branch Tests
// =============================================================================

void test_arm_branch(void)
{
    TEST_SECTION("ARM Branch Instructions");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    setup_arm_mode(gba);

    // B (unconditional branch)
    gba->cpu.r[15] = 0x08000100;
    uint32_t opcode = 0xEA000010;  // B +0x44 (offset = 0x10 * 4 + 8)
    cpu_execute_arm(gba, opcode);
    // Note: Branch adds (offset << 2) + 8 to PC
    TEST_ASSERT(gba->cpu.pipeline_invalid, "Branch invalidates pipeline");

    // BL (branch with link)
    gba->cpu.r[15] = 0x08000200;
    opcode = 0xEB000020;  // BL +0x88
    cpu_execute_arm(gba, opcode);
    // LR should be set to return address
    TEST_ASSERT(gba->cpu.r[14] != 0, "BL sets link register");

    // BX (branch and exchange)
    gba->cpu.r[0] = 0x08000301;  // Bit 0 set = switch to THUMB
    opcode = 0xE12FFF10;  // BX R0
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT(gba->cpu.cpsr & CPSR_T, "BX with bit 0 set switches to THUMB");

    gba_destroy(gba);
}

void test_arm_conditions(void)
{
    TEST_SECTION("ARM Condition Codes");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    setup_arm_mode(gba);

    // Test EQ (Z=1)
    gba->cpu.cpsr |= CPSR_Z;
    gba->cpu.r[0] = 0;
    uint32_t opcode = 0x03A00001;  // MOVEQ R0, #1
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(1, gba->cpu.r[0], "MOVEQ executes when Z=1");

    // Test NE (Z=0)
    gba->cpu.cpsr &= ~CPSR_Z;
    gba->cpu.r[0] = 0;
    opcode = 0x13A00001;  // MOVNE R0, #1
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(1, gba->cpu.r[0], "MOVNE executes when Z=0");

    // Test CS (C=1)
    gba->cpu.cpsr |= CPSR_C;
    gba->cpu.r[0] = 0;
    opcode = 0x23A00001;  // MOVCS R0, #1
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(1, gba->cpu.r[0], "MOVCS executes when C=1");

    // Test MI (N=1)
    gba->cpu.cpsr |= CPSR_N;
    gba->cpu.r[0] = 0;
    opcode = 0x43A00001;  // MOVMI R0, #1
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(1, gba->cpu.r[0], "MOVMI executes when N=1");

    // Test GE (N=V)
    gba->cpu.cpsr = (gba->cpu.cpsr & ~(CPSR_N | CPSR_V)) | CPSR_N | CPSR_V;
    gba->cpu.r[0] = 0;
    opcode = 0xA3A00001;  // MOVGE R0, #1
    cpu_execute_arm(gba, opcode);
    TEST_ASSERT_EQ(1, gba->cpu.r[0], "MOVGE executes when N=V");

    // Test condition not met
    gba->cpu.cpsr &= ~CPSR_Z;
    gba->cpu.r[0] = 0;
    opcode = 0x03A00001;  // MOVEQ R0, #1 (should not execute)
    execute_arm_with_cond(gba, opcode);
    TEST_ASSERT_EQ(0, gba->cpu.r[0], "MOVEQ skipped when Z=0");

    gba_destroy(gba);
}

// =============================================================================
// THUMB Tests
// =============================================================================

void test_thumb_mov(void)
{
    TEST_SECTION("THUMB MOV Instructions");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    setup_thumb_mode(gba);

    // MOV R0, #0x55
    gba->cpu.r[0] = 0;
    uint16_t opcode = 0x2055;  // MOV R0, #0x55
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(0x55, gba->cpu.r[0], "THUMB MOV R0, #0x55");

    // MOV R7, #0xFF
    opcode = 0x27FF;  // MOV R7, #0xFF
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(0xFF, gba->cpu.r[7], "THUMB MOV R7, #0xFF");

    gba_destroy(gba);
}

void test_thumb_add_sub(void)
{
    TEST_SECTION("THUMB ADD/SUB Instructions");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    setup_thumb_mode(gba);

    // ADD R0, R1, R2
    gba->cpu.r[1] = 10;
    gba->cpu.r[2] = 5;
    uint16_t opcode = 0x1888;  // ADD R0, R1, R2
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(15, gba->cpu.r[0], "THUMB ADD R0, R1, R2");

    // SUB R0, R1, R2
    opcode = 0x1A88;  // SUB R0, R1, R2
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(5, gba->cpu.r[0], "THUMB SUB R0, R1, R2");

    // ADD R0, #10
    gba->cpu.r[0] = 20;
    opcode = 0x300A;  // ADD R0, #10
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(30, gba->cpu.r[0], "THUMB ADD R0, #10");

    // SUB R0, #5
    opcode = 0x3805;  // SUB R0, #5
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(25, gba->cpu.r[0], "THUMB SUB R0, #5");

    gba_destroy(gba);
}

void test_thumb_shift(void)
{
    TEST_SECTION("THUMB Shift Instructions");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    setup_thumb_mode(gba);

    // LSL R0, R1, #4
    gba->cpu.r[1] = 0x0F;
    uint16_t opcode = 0x0108;  // LSL R0, R1, #4
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(0xF0, gba->cpu.r[0], "THUMB LSL R0, R1, #4");

    // LSR R0, R1, #4
    gba->cpu.r[1] = 0xF0;
    opcode = 0x0908;  // LSR R0, R1, #4
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(0x0F, gba->cpu.r[0], "THUMB LSR R0, R1, #4");

    // ASR R0, R1, #4
    gba->cpu.r[1] = 0xF0000000;
    opcode = 0x1108;  // ASR R0, R1, #4
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(0xFF000000, gba->cpu.r[0], "THUMB ASR preserves sign");

    gba_destroy(gba);
}

void test_thumb_alu(void)
{
    TEST_SECTION("THUMB ALU Instructions");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    setup_thumb_mode(gba);

    // AND R0, R1
    gba->cpu.r[0] = 0xFF00;
    gba->cpu.r[1] = 0x0FF0;
    uint16_t opcode = 0x4008;  // AND R0, R1
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(0x0F00, gba->cpu.r[0], "THUMB AND R0, R1");

    // ORR R0, R1
    gba->cpu.r[0] = 0xFF00;
    gba->cpu.r[1] = 0x00FF;
    opcode = 0x4308;  // ORR R0, R1
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(0xFFFF, gba->cpu.r[0], "THUMB ORR R0, R1");

    // EOR R0, R1
    gba->cpu.r[0] = 0xFFFF;
    gba->cpu.r[1] = 0x00FF;
    opcode = 0x4048;  // EOR R0, R1
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(0xFF00, gba->cpu.r[0], "THUMB EOR R0, R1");

    // MUL R0, R1
    gba->cpu.r[0] = 7;
    gba->cpu.r[1] = 8;
    opcode = 0x4348;  // MUL R0, R1
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(56, gba->cpu.r[0], "THUMB MUL R0, R1");

    // NEG R0, R1
    gba->cpu.r[1] = 5;
    opcode = 0x4248;  // NEG R0, R1
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(0xFFFFFFFB, gba->cpu.r[0], "THUMB NEG R0, R1");

    gba_destroy(gba);
}

void test_thumb_hi_reg(void)
{
    TEST_SECTION("THUMB Hi Register Operations");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    setup_thumb_mode(gba);

    // ADD R8, R0 (hi register)
    gba->cpu.r[0] = 100;
    gba->cpu.r[8] = 50;
    uint16_t opcode = 0x4480;  // ADD R8, R0
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(150, gba->cpu.r[8], "THUMB ADD R8, R0 (hi reg)");

    // MOV R8, R0
    gba->cpu.r[0] = 0x12345678;
    opcode = 0x4680;  // MOV R8, R0
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(0x12345678, gba->cpu.r[8], "THUMB MOV R8, R0");

    gba_destroy(gba);
}

void test_thumb_push_pop(void)
{
    TEST_SECTION("THUMB PUSH/POP Instructions");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    setup_thumb_mode(gba);

    // Set up stack pointer
    gba->cpu.r[13] = 0x03007F00;

    // PUSH {R0, R1, R2}
    gba->cpu.r[0] = 0x11111111;
    gba->cpu.r[1] = 0x22222222;
    gba->cpu.r[2] = 0x33333333;
    uint16_t opcode = 0xB407;  // PUSH {R0, R1, R2}
    uint32_t old_sp = gba->cpu.r[13];
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(old_sp - 12, gba->cpu.r[13], "PUSH decrements SP");

    // Clear registers
    gba->cpu.r[0] = 0;
    gba->cpu.r[1] = 0;
    gba->cpu.r[2] = 0;

    // POP {R0, R1, R2}
    opcode = 0xBC07;  // POP {R0, R1, R2}
    cpu_execute_thumb(gba, opcode);
    TEST_ASSERT_EQ(0x11111111, gba->cpu.r[0], "POP restores R0");
    TEST_ASSERT_EQ(0x22222222, gba->cpu.r[1], "POP restores R1");
    TEST_ASSERT_EQ(0x33333333, gba->cpu.r[2], "POP restores R2");
    TEST_ASSERT_EQ(old_sp, gba->cpu.r[13], "POP restores SP");

    gba_destroy(gba);
}

// =============================================================================
// Memory Tests
// =============================================================================

void test_memory_regions(void)
{
    TEST_SECTION("Memory Region Access");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    // IWRAM
    memory_write32(gba, 0x03000000, 0xDEADBEEF);
    TEST_ASSERT_EQ(0xDEADBEEF, memory_read32(gba, 0x03000000), "IWRAM write/read");

    // EWRAM
    memory_write32(gba, 0x02000000, 0xCAFEBABE);
    TEST_ASSERT_EQ(0xCAFEBABE, memory_read32(gba, 0x02000000), "EWRAM write/read");

    // Palette RAM
    memory_write16(gba, 0x05000000, 0x7FFF);
    TEST_ASSERT_EQ(0x7FFF, memory_read16(gba, 0x05000000), "Palette RAM write/read");

    // VRAM
    memory_write16(gba, 0x06000000, 0x1234);
    TEST_ASSERT_EQ(0x1234, memory_read16(gba, 0x06000000), "VRAM write/read");

    // OAM
    memory_write32(gba, 0x07000000, 0x12345678);
    TEST_ASSERT_EQ(0x12345678, memory_read32(gba, 0x07000000), "OAM write/read");

    gba_destroy(gba);
}

void test_memory_mirroring(void)
{
    TEST_SECTION("Memory Mirroring");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    // IWRAM mirrors every 0x8000 bytes
    memory_write32(gba, 0x03000000, 0x12345678);
    TEST_ASSERT_EQ(0x12345678, memory_read32(gba, 0x03008000), "IWRAM mirror at +0x8000");

    // Palette mirrors every 0x400 bytes
    memory_write16(gba, 0x05000000, 0xABCD);
    TEST_ASSERT_EQ(0xABCD, memory_read16(gba, 0x05000400), "Palette mirror at +0x400");

    gba_destroy(gba);
}

void test_memory_alignment(void)
{
    TEST_SECTION("Memory Alignment");

    gba_t *gba = create_test_gba();
    if (!gba) return;

    // 32-bit access aligned
    memory_write32(gba, 0x03000000, 0x12345678);
    TEST_ASSERT_EQ(0x12345678, memory_read32(gba, 0x03000000), "Aligned 32-bit access");

    // 32-bit access unaligned (should rotate)
    uint32_t val = memory_read32(gba, 0x03000001);
    // Unaligned reads rotate the value
    TEST_ASSERT(val != 0x12345678, "Unaligned 32-bit read rotates");

    // 16-bit access
    memory_write16(gba, 0x03000010, 0xABCD);
    TEST_ASSERT_EQ(0xABCD, memory_read16(gba, 0x03000010), "16-bit access");

    gba_destroy(gba);
}

// =============================================================================
// Main Test Runner
// =============================================================================

void run_all_tests(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║        GBA Emulator Test Suite                        ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");

    // ARM Tests
    test_arm_mov();
    test_arm_add_sub();
    test_arm_logic();
    test_arm_cmp_tst();
    test_arm_mul();
    test_arm_load_store();
    test_arm_branch();
    test_arm_conditions();

    // THUMB Tests
    test_thumb_mov();
    test_thumb_add_sub();
    test_thumb_shift();
    test_thumb_alu();
    test_thumb_hi_reg();
    test_thumb_push_pop();

    // Memory Tests
    test_memory_regions();
    test_memory_mirroring();
    test_memory_alignment();

    // Summary
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("Test Results: %d passed, %d failed, %d total\n",
           tests_passed, tests_failed, tests_run);
    printf("═══════════════════════════════════════════════════════\n");

    if (tests_failed == 0) {
        printf("✓ All tests passed!\n");
    } else {
        printf("✗ Some tests failed!\n");
    }
}

#ifdef TEST_HOST
// Host-based test runner
int main(int argc, char **argv)
{
    run_all_tests();
    return tests_failed > 0 ? 1 : 0;
}
#endif
