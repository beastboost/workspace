/**
 * GBA Emulator Core - Timer Implementation
 */

#include <string.h>
#include "gba_internal.h"

// Prescaler values (CPU cycles per timer tick)
static const uint16_t prescaler_values[] = {1, 64, 256, 1024};

void timer_init(gba_t *gba)
{
    memset(gba->timer, 0, sizeof(gba->timer));
}

void timer_reset(gba_t *gba)
{
    memset(gba->timer, 0, sizeof(gba->timer));
}

// Called when a timer overflows
static void timer_overflow(gba_t *gba, int timer_id)
{
    timer_state_t *timer = &gba->timer[timer_id];

    // Reload counter
    timer->counter = timer->reload;

    // Raise IRQ if enabled
    if (timer->control & (1 << 6)) {
        cpu_raise_irq(gba, IRQ_TIMER0 << timer_id);
    }

    // Sound FIFO triggers
    if (timer_id == 0 || timer_id == 1) {
        uint16_t soundcnt_h = gba->apu.soundcnt_h;

        // Check if this timer is connected to FIFO A
        if (((soundcnt_h >> 10) & 1) == timer_id) {
            // Read sample from FIFO A
            if (gba->apu.fifo_a.count > 0) {
                gba->apu.fifo_a.read_pos = (gba->apu.fifo_a.read_pos + 1) & 31;
                gba->apu.fifo_a.count--;
            }
            // Request more data if FIFO is low
            if (gba->apu.fifo_a.count <= 16) {
                dma_trigger(gba, 3);  // Special timing
            }
        }

        // Check if this timer is connected to FIFO B
        if (((soundcnt_h >> 14) & 1) == timer_id) {
            if (gba->apu.fifo_b.count > 0) {
                gba->apu.fifo_b.read_pos = (gba->apu.fifo_b.read_pos + 1) & 31;
                gba->apu.fifo_b.count--;
            }
            if (gba->apu.fifo_b.count <= 16) {
                dma_trigger(gba, 3);
            }
        }
    }

    // Cascade to next timer if it's in count-up mode
    if (timer_id < 3) {
        timer_state_t *next = &gba->timer[timer_id + 1];
        if ((next->control & 0x84) == 0x84) {  // Enabled and count-up
            next->counter++;
            if (next->counter == 0) {
                timer_overflow(gba, timer_id + 1);
            }
        }
    }
}

void timer_step(gba_t *gba, uint32_t cycles)
{
    for (int i = 0; i < 4; i++) {
        timer_state_t *timer = &gba->timer[i];

        // Check if timer is enabled
        if (!(timer->control & (1 << 7))) continue;

        // Check if timer is in count-up mode (cascaded)
        if ((timer->control & (1 << 2)) && i > 0) continue;

        // Get prescaler
        uint16_t prescaler = prescaler_values[timer->control & 3];

        // Add cycles to prescaler counter
        timer->prescaler_count += cycles;

        // Process ticks
        while (timer->prescaler_count >= prescaler) {
            timer->prescaler_count -= prescaler;
            timer->counter++;

            // Check for overflow
            if (timer->counter == 0) {
                timer_overflow(gba, i);
            }
        }
    }
}
