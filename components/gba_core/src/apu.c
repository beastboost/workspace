/**
 * GBA Emulator Core - APU (Audio Processing Unit) Implementation
 */

#include <string.h>
#include "gba_internal.h"

// Sample rate constants
#define GBA_SAMPLE_RATE 32768
#define CPU_CYCLES_PER_SAMPLE (GBA_CPU_FREQ / GBA_SAMPLE_RATE)

void apu_init(gba_t *gba)
{
    memset(&gba->apu, 0, sizeof(apu_state_t));
    gba->apu.soundbias = 0x200;
}

void apu_reset(gba_t *gba)
{
    memset(&gba->apu, 0, sizeof(apu_state_t));
    gba->apu.soundbias = 0x200;
}

// Write to FIFO
void apu_write_fifo(gba_t *gba, int channel, int32_t sample)
{
    fifo_t *fifo = (channel == 0) ? &gba->apu.fifo_a : &gba->apu.fifo_b;

    if (fifo->count < 32) {
        fifo->data[fifo->write_pos] = sample;
        fifo->write_pos = (fifo->write_pos + 1) & 31;
        fifo->count++;
    }
}

// Read from FIFO
static int8_t fifo_read(fifo_t *fifo)
{
    if (fifo->count == 0) {
        return 0;
    }

    int8_t sample = fifo->data[fifo->read_pos];
    fifo->read_pos = (fifo->read_pos + 1) & 31;
    fifo->count--;

    return sample;
}

// Square channel duty cycles
static const uint8_t duty_table[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},  // 12.5%
    {1, 0, 0, 0, 0, 0, 0, 1},  // 25%
    {1, 0, 0, 0, 0, 1, 1, 1},  // 50%
    {0, 1, 1, 1, 1, 1, 1, 0},  // 75%
};

// Update square channel 1 (with sweep)
static void update_channel1(gba_t *gba)
{
    apu_channel_t *ch = &gba->apu.ch1;

    if (!ch->enabled) {
        ch->output = 0;
        return;
    }

    // Update timer
    if (ch->timer > 0) {
        ch->timer--;
    }

    if (ch->timer == 0) {
        ch->timer = (2048 - ch->frequency) * 4;

        // Advance duty position
        ch->length = (ch->length + 1) & 7;
    }

    // Get duty output
    uint8_t duty = (gba->mem.io[0x62] >> 6) & 3;
    ch->output = duty_table[duty][ch->length] ? ch->volume : 0;
}

// Update square channel 2
static void update_channel2(gba_t *gba)
{
    apu_channel_t *ch = &gba->apu.ch2;

    if (!ch->enabled) {
        ch->output = 0;
        return;
    }

    if (ch->timer > 0) {
        ch->timer--;
    }

    if (ch->timer == 0) {
        ch->timer = (2048 - ch->frequency) * 4;
        ch->length = (ch->length + 1) & 7;
    }

    uint8_t duty = (gba->mem.io[0x68] >> 6) & 3;
    ch->output = duty_table[duty][ch->length] ? ch->volume : 0;
}

// Update wave channel
static void update_channel3(gba_t *gba)
{
    apu_channel_t *ch = &gba->apu.ch3;

    if (!ch->enabled) {
        ch->output = 0;
        return;
    }

    if (ch->timer > 0) {
        ch->timer--;
    }

    if (ch->timer == 0) {
        ch->timer = (2048 - ch->frequency) * 2;
        ch->length = (ch->length + 1) & 63;

        // Read wave sample
        uint8_t sample = gba->apu.wave_ram[ch->length >> 1];
        if (ch->length & 1) {
            sample &= 0x0F;
        } else {
            sample >>= 4;
        }

        // Apply volume
        uint8_t volume_shift = (gba->mem.io[0x72] >> 5) & 3;
        if (volume_shift == 0) {
            ch->output = 0;
        } else {
            ch->output = sample >> (volume_shift - 1);
        }
    }
}

// Update noise channel
static void update_channel4(gba_t *gba)
{
    apu_channel_t *ch = &gba->apu.ch4;

    if (!ch->enabled) {
        ch->output = 0;
        return;
    }

    if (ch->timer > 0) {
        ch->timer--;
    }

    if (ch->timer == 0) {
        // Calculate period
        uint8_t r = gba->mem.io[0x7C] & 7;
        uint8_t s = (gba->mem.io[0x7C] >> 4) & 0xF;
        ch->timer = r ? (r << (s + 1)) : (1 << s);

        // LFSR
        bool width7 = gba->mem.io[0x7C] & 8;
        uint16_t lfsr = ch->frequency;
        uint16_t bit = ((lfsr >> 1) ^ lfsr) & 1;

        if (width7) {
            lfsr = (lfsr >> 1) | (bit << 6);
            lfsr &= 0x7F;
        } else {
            lfsr = (lfsr >> 1) | (bit << 14);
            lfsr &= 0x7FFF;
        }

        ch->frequency = lfsr;
        ch->output = (lfsr & 1) ? 0 : ch->volume;
    }
}

void apu_step(gba_t *gba, uint32_t cycles)
{
    gba->apu.sample_timer += cycles;

    if (gba->apu.sample_timer < CPU_CYCLES_PER_SAMPLE) {
        return;
    }

    gba->apu.sample_timer -= CPU_CYCLES_PER_SAMPLE;

    // Check if sound is enabled
    if (!(gba->apu.soundcnt_x & 0x80)) {
        gba->apu.left_output = 0;
        gba->apu.right_output = 0;
        return;
    }

    // Update PSG channels
    update_channel1(gba);
    update_channel2(gba);
    update_channel3(gba);
    update_channel4(gba);

    // Mix PSG channels
    int32_t psg_left = 0;
    int32_t psg_right = 0;

    uint16_t soundcnt_l = gba->apu.soundcnt_l;
    uint16_t soundcnt_h = gba->apu.soundcnt_h;

    // Channel outputs to left/right
    if (soundcnt_l & (1 << 8)) psg_right += gba->apu.ch1.output;
    if (soundcnt_l & (1 << 9)) psg_right += gba->apu.ch2.output;
    if (soundcnt_l & (1 << 10)) psg_right += gba->apu.ch3.output;
    if (soundcnt_l & (1 << 11)) psg_right += gba->apu.ch4.output;
    if (soundcnt_l & (1 << 12)) psg_left += gba->apu.ch1.output;
    if (soundcnt_l & (1 << 13)) psg_left += gba->apu.ch2.output;
    if (soundcnt_l & (1 << 14)) psg_left += gba->apu.ch3.output;
    if (soundcnt_l & (1 << 15)) psg_left += gba->apu.ch4.output;

    // Apply PSG volume
    int psg_vol_right = soundcnt_l & 7;
    int psg_vol_left = (soundcnt_l >> 4) & 7;
    psg_right = (psg_right * psg_vol_right) >> 3;
    psg_left = (psg_left * psg_vol_left) >> 3;

    // PSG master volume (SOUNDCNT_H bits 0-1)
    int psg_ratio = soundcnt_h & 3;
    if (psg_ratio < 3) {
        psg_right >>= (2 - psg_ratio);
        psg_left >>= (2 - psg_ratio);
    }

    // DMA Sound mixing
    int32_t dma_left = 0;
    int32_t dma_right = 0;

    // FIFO A
    int8_t fifo_a_sample = gba->apu.fifo_a.count > 0 ?
        gba->apu.fifo_a.data[gba->apu.fifo_a.read_pos] : 0;
    int fifo_a_vol = (soundcnt_h & (1 << 2)) ? 4 : 2;
    if (soundcnt_h & (1 << 8)) dma_right += fifo_a_sample * fifo_a_vol;
    if (soundcnt_h & (1 << 9)) dma_left += fifo_a_sample * fifo_a_vol;

    // FIFO B
    int8_t fifo_b_sample = gba->apu.fifo_b.count > 0 ?
        gba->apu.fifo_b.data[gba->apu.fifo_b.read_pos] : 0;
    int fifo_b_vol = (soundcnt_h & (1 << 3)) ? 4 : 2;
    if (soundcnt_h & (1 << 12)) dma_right += fifo_b_sample * fifo_b_vol;
    if (soundcnt_h & (1 << 13)) dma_left += fifo_b_sample * fifo_b_vol;

    // Final mix
    int32_t left = psg_left + dma_left;
    int32_t right = psg_right + dma_right;

    // Apply bias
    int bias = (gba->apu.soundbias >> 1) & 0x1FF;
    left += bias;
    right += bias;

    // Clamp to 10-bit range
    if (left < 0) left = 0;
    if (left > 0x3FF) left = 0x3FF;
    if (right < 0) right = 0;
    if (right > 0x3FF) right = 0x3FF;

    // Convert to 16-bit signed
    gba->apu.left_output = (left - 0x200) << 6;
    gba->apu.right_output = (right - 0x200) << 6;
}
