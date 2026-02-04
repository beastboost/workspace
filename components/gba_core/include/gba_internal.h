/**
 * GBA Emulator Core - Internal Header
 *
 * Internal structures and functions
 */

#ifndef GBA_INTERNAL_H
#define GBA_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include "gba.h"

#ifdef __cplusplus
extern "C" {
#endif

// Memory region sizes
#define BIOS_SIZE       0x4000      // 16 KB
#define EWRAM_SIZE      0x40000     // 256 KB
#define IWRAM_SIZE      0x8000      // 32 KB
#define IO_SIZE         0x400       // 1 KB
#define PALETTE_SIZE    0x400       // 1 KB
#define VRAM_SIZE       0x18000     // 96 KB
#define OAM_SIZE        0x400       // 1 KB
#define ROM_MAX_SIZE    0x2000000   // 32 MB
#define SRAM_SIZE       0x10000     // 64 KB (max)
#define FLASH_SIZE      0x20000     // 128 KB (max)

// Memory map addresses
#define ADDR_BIOS       0x00000000
#define ADDR_EWRAM      0x02000000
#define ADDR_IWRAM      0x03000000
#define ADDR_IO         0x04000000
#define ADDR_PALETTE    0x05000000
#define ADDR_VRAM       0x06000000
#define ADDR_OAM        0x07000000
#define ADDR_ROM        0x08000000
#define ADDR_ROM_M1     0x0A000000
#define ADDR_ROM_M2     0x0C000000
#define ADDR_SRAM       0x0E000000

// IO Register addresses
#define REG_DISPCNT     0x04000000
#define REG_DISPSTAT    0x04000004
#define REG_VCOUNT      0x04000006
#define REG_BG0CNT      0x04000008
#define REG_BG1CNT      0x0400000A
#define REG_BG2CNT      0x0400000C
#define REG_BG3CNT      0x0400000E
#define REG_BG0HOFS     0x04000010
#define REG_BG0VOFS     0x04000012
#define REG_BG1HOFS     0x04000014
#define REG_BG1VOFS     0x04000016
#define REG_BG2HOFS     0x04000018
#define REG_BG2VOFS     0x0400001A
#define REG_BG3HOFS     0x0400001C
#define REG_BG3VOFS     0x0400001E
#define REG_BG2PA       0x04000020
#define REG_BG2PB       0x04000022
#define REG_BG2PC       0x04000024
#define REG_BG2PD       0x04000026
#define REG_BG2X        0x04000028
#define REG_BG2Y        0x0400002C
#define REG_BG3PA       0x04000030
#define REG_BG3PB       0x04000032
#define REG_BG3PC       0x04000034
#define REG_BG3PD       0x04000036
#define REG_BG3X        0x04000038
#define REG_BG3Y        0x0400003C
#define REG_WIN0H       0x04000040
#define REG_WIN1H       0x04000042
#define REG_WIN0V       0x04000044
#define REG_WIN1V       0x04000046
#define REG_WININ       0x04000048
#define REG_WINOUT      0x0400004A
#define REG_MOSAIC      0x0400004C
#define REG_BLDCNT      0x04000050
#define REG_BLDALPHA    0x04000052
#define REG_BLDY        0x04000054
#define REG_SOUND1CNT_L 0x04000060
#define REG_SOUND1CNT_H 0x04000062
#define REG_SOUND1CNT_X 0x04000064
#define REG_SOUND2CNT_L 0x04000068
#define REG_SOUND2CNT_H 0x0400006C
#define REG_SOUND3CNT_L 0x04000070
#define REG_SOUND3CNT_H 0x04000072
#define REG_SOUND3CNT_X 0x04000074
#define REG_SOUND4CNT_L 0x04000078
#define REG_SOUND4CNT_H 0x0400007C
#define REG_SOUNDCNT_L  0x04000080
#define REG_SOUNDCNT_H  0x04000082
#define REG_SOUNDCNT_X  0x04000084
#define REG_SOUNDBIAS   0x04000088
#define REG_WAVE_RAM    0x04000090
#define REG_FIFO_A      0x040000A0
#define REG_FIFO_B      0x040000A4
#define REG_DMA0SAD     0x040000B0
#define REG_DMA0DAD     0x040000B4
#define REG_DMA0CNT     0x040000B8
#define REG_DMA1SAD     0x040000BC
#define REG_DMA1DAD     0x040000C0
#define REG_DMA1CNT     0x040000C4
#define REG_DMA2SAD     0x040000C8
#define REG_DMA2DAD     0x040000CC
#define REG_DMA2CNT     0x040000D0
#define REG_DMA3SAD     0x040000D4
#define REG_DMA3DAD     0x040000D8
#define REG_DMA3CNT     0x040000DC
#define REG_TM0CNT_L    0x04000100
#define REG_TM0CNT_H    0x04000102
#define REG_TM1CNT_L    0x04000104
#define REG_TM1CNT_H    0x04000106
#define REG_TM2CNT_L    0x04000108
#define REG_TM2CNT_H    0x0400010A
#define REG_TM3CNT_L    0x0400010C
#define REG_TM3CNT_H    0x0400010E
#define REG_KEYINPUT    0x04000130
#define REG_KEYCNT      0x04000132
#define REG_IE          0x04000200
#define REG_IF          0x04000202
#define REG_WAITCNT     0x04000204
#define REG_IME         0x04000208
#define REG_HALTCNT     0x04000301

// CPU modes
#define CPU_MODE_USER   0x10
#define CPU_MODE_FIQ    0x11
#define CPU_MODE_IRQ    0x12
#define CPU_MODE_SVC    0x13
#define CPU_MODE_ABT    0x17
#define CPU_MODE_UND    0x1B
#define CPU_MODE_SYS    0x1F

// CPSR flags
#define CPSR_N          (1 << 31)
#define CPSR_Z          (1 << 30)
#define CPSR_C          (1 << 29)
#define CPSR_V          (1 << 28)
#define CPSR_I          (1 << 7)
#define CPSR_F          (1 << 6)
#define CPSR_T          (1 << 5)

// Interrupt flags
#define IRQ_VBLANK      (1 << 0)
#define IRQ_HBLANK      (1 << 1)
#define IRQ_VCOUNT      (1 << 2)
#define IRQ_TIMER0      (1 << 3)
#define IRQ_TIMER1      (1 << 4)
#define IRQ_TIMER2      (1 << 5)
#define IRQ_TIMER3      (1 << 6)
#define IRQ_SERIAL      (1 << 7)
#define IRQ_DMA0        (1 << 8)
#define IRQ_DMA1        (1 << 9)
#define IRQ_DMA2        (1 << 10)
#define IRQ_DMA3        (1 << 11)
#define IRQ_KEYPAD      (1 << 12)
#define IRQ_GAMEPAK     (1 << 13)

// PPU timing constants
#define CYCLES_PER_PIXEL    4
#define HDRAW_CYCLES        960     // 240 pixels * 4 cycles
#define HBLANK_CYCLES       272     // 68 pixels * 4 cycles
#define SCANLINE_CYCLES     1232    // 308 pixels * 4 cycles
#define VDRAW_LINES         160
#define VBLANK_LINES        68
#define TOTAL_LINES         228
#define FRAME_CYCLES        280896  // 228 lines * 1232 cycles

// CPU state structure
typedef struct {
    uint32_t r[16];             // General purpose registers
    uint32_t cpsr;              // Current program status register
    uint32_t spsr[6];           // Saved PSRs (FIQ, SVC, ABT, IRQ, UND)

    // Banked registers
    uint32_t r8_fiq, r9_fiq, r10_fiq, r11_fiq, r12_fiq;
    uint32_t r13_fiq, r14_fiq;
    uint32_t r13_svc, r14_svc;
    uint32_t r13_abt, r14_abt;
    uint32_t r13_irq, r14_irq;
    uint32_t r13_und, r14_und;

    // Pipeline
    uint32_t pipeline[2];
    bool pipeline_invalid;

    // Halt state
    bool halted;

    // Cycle counter
    uint32_t cycles;
} cpu_state_t;

// DMA channel state
typedef struct {
    uint32_t src;
    uint32_t dst;
    uint32_t count;
    uint16_t control;
    bool active;
} dma_channel_t;

// Timer state
typedef struct {
    uint16_t counter;
    uint16_t reload;
    uint16_t control;
    uint32_t prescaler_count;
} timer_state_t;

// APU channel state
typedef struct {
    bool enabled;
    uint32_t frequency;
    uint32_t timer;
    uint32_t length;
    uint32_t envelope;
    uint32_t volume;
    int32_t output;
} apu_channel_t;

// FIFO buffer for Direct Sound
typedef struct {
    int8_t data[32];
    uint8_t read_pos;
    uint8_t write_pos;
    uint8_t count;
} fifo_t;

// APU state
typedef struct {
    apu_channel_t ch1;          // Square 1 with sweep
    apu_channel_t ch2;          // Square 2
    apu_channel_t ch3;          // Wave
    apu_channel_t ch4;          // Noise
    fifo_t fifo_a;              // DMA Sound A
    fifo_t fifo_b;              // DMA Sound B
    uint8_t wave_ram[32];       // Wave pattern RAM
    uint16_t soundcnt_l;
    uint16_t soundcnt_h;
    uint16_t soundcnt_x;
    uint16_t soundbias;
    uint32_t sample_timer;
    int16_t left_output;
    int16_t right_output;
} apu_state_t;

// PPU state
typedef struct {
    uint16_t dispcnt;
    uint16_t dispstat;
    uint16_t vcount;
    uint16_t bgcnt[4];
    int16_t bghofs[4];
    int16_t bgvofs[4];
    int16_t bgpa[2];
    int16_t bgpb[2];
    int16_t bgpc[2];
    int16_t bgpd[2];
    int32_t bgx[2];
    int32_t bgy[2];
    int32_t bgx_ref[2];
    int32_t bgy_ref[2];
    uint16_t win0h, win1h;
    uint16_t win0v, win1v;
    uint16_t winin, winout;
    uint16_t mosaic;
    uint16_t bldcnt;
    uint16_t bldalpha;
    uint16_t bldy;
    uint32_t cycle;
    uint16_t framebuffer[GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT];
} ppu_state_t;

// Memory state
typedef struct {
    uint8_t *bios;
    uint8_t *ewram;
    uint8_t *iwram;
    uint8_t *palette;
    uint8_t *vram;
    uint8_t *oam;
    uint8_t *rom;
    uint8_t *sram;
    size_t rom_size;
    size_t sram_size;
    bool bios_loaded;
    uint16_t waitcnt;
    uint8_t io[IO_SIZE];
} memory_state_t;

// Main GBA state structure
struct gba {
    cpu_state_t cpu;
    ppu_state_t ppu;
    apu_state_t apu;
    memory_state_t mem;
    dma_channel_t dma[4];
    timer_state_t timer[4];

    uint16_t keyinput;
    uint16_t ie;
    uint16_t if_;
    uint16_t ime;

    gba_frame_callback_t frame_callback;

    // ROM info
    char game_title[13];
    char game_code[5];
};

// CPU functions
void cpu_init(gba_t *gba);
void cpu_reset(gba_t *gba);
uint32_t cpu_execute(gba_t *gba);
void cpu_raise_irq(gba_t *gba, uint16_t irq);

// ARM instruction execution
uint32_t cpu_execute_arm(gba_t *gba, uint32_t opcode);

// THUMB instruction execution
uint32_t cpu_execute_thumb(gba_t *gba, uint16_t opcode);

// Memory functions
void memory_init(gba_t *gba);
void memory_free(gba_t *gba);
void memory_reset(gba_t *gba);

uint8_t memory_read8(gba_t *gba, uint32_t addr);
uint16_t memory_read16(gba_t *gba, uint32_t addr);
uint32_t memory_read32(gba_t *gba, uint32_t addr);

void memory_write8(gba_t *gba, uint32_t addr, uint8_t value);
void memory_write16(gba_t *gba, uint32_t addr, uint16_t value);
void memory_write32(gba_t *gba, uint32_t addr, uint32_t value);

// PPU functions
void ppu_init(gba_t *gba);
void ppu_reset(gba_t *gba);
void ppu_step(gba_t *gba, uint32_t cycles);

// APU functions
void apu_init(gba_t *gba);
void apu_reset(gba_t *gba);
void apu_step(gba_t *gba, uint32_t cycles);
void apu_write_fifo(gba_t *gba, int channel, int32_t sample);

// DMA functions
void dma_init(gba_t *gba);
void dma_reset(gba_t *gba);
void dma_trigger(gba_t *gba, int timing);
bool dma_active(gba_t *gba);
uint32_t dma_run(gba_t *gba);

// Timer functions
void timer_init(gba_t *gba);
void timer_reset(gba_t *gba);
void timer_step(gba_t *gba, uint32_t cycles);

// BIOS functions
void bios_init(gba_t *gba);
uint32_t bios_call(gba_t *gba, uint8_t swi_num);

// Helper macros
#define REG_PC gba->cpu.r[15]
#define REG_LR gba->cpu.r[14]
#define REG_SP gba->cpu.r[13]

#define CPU_IN_THUMB_MODE() (gba->cpu.cpsr & CPSR_T)
#define CPU_IRQ_ENABLED() (!(gba->cpu.cpsr & CPSR_I))

#define SET_FLAG_N(v) do { if (v) gba->cpu.cpsr |= CPSR_N; else gba->cpu.cpsr &= ~CPSR_N; } while(0)
#define SET_FLAG_Z(v) do { if (v) gba->cpu.cpsr |= CPSR_Z; else gba->cpu.cpsr &= ~CPSR_Z; } while(0)
#define SET_FLAG_C(v) do { if (v) gba->cpu.cpsr |= CPSR_C; else gba->cpu.cpsr &= ~CPSR_C; } while(0)
#define SET_FLAG_V(v) do { if (v) gba->cpu.cpsr |= CPSR_V; else gba->cpu.cpsr &= ~CPSR_V; } while(0)

#define GET_FLAG_N() ((gba->cpu.cpsr >> 31) & 1)
#define GET_FLAG_Z() ((gba->cpu.cpsr >> 30) & 1)
#define GET_FLAG_C() ((gba->cpu.cpsr >> 29) & 1)
#define GET_FLAG_V() ((gba->cpu.cpsr >> 28) & 1)

#ifdef __cplusplus
}
#endif

#endif // GBA_INTERNAL_H
