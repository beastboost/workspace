/**
 * GBA Emulator Core - Main Implementation
 */

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "gba.h"
#include "gba_internal.h"

static const char *TAG = "GBA_CORE";

gba_t *gba_create(void)
{
    // Allocate GBA structure in internal RAM for fast access
    gba_t *gba = heap_caps_calloc(1, sizeof(gba_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (gba == NULL) {
        ESP_LOGE(TAG, "Failed to allocate GBA structure");
        return NULL;
    }

    // Initialize memory subsystem
    memory_init(gba);

    // Initialize CPU
    cpu_init(gba);

    // Initialize PPU
    ppu_init(gba);

    // Initialize APU
    apu_init(gba);

    // Initialize DMA
    dma_init(gba);

    // Initialize timers
    timer_init(gba);

    // Initialize BIOS (HLE)
    bios_init(gba);

    // Initial key state (all buttons released)
    gba->keyinput = 0x03FF;

    ESP_LOGI(TAG, "GBA instance created");
    return gba;
}

void gba_destroy(gba_t *gba)
{
    if (gba == NULL) return;

    memory_free(gba);
    free(gba);

    ESP_LOGI(TAG, "GBA instance destroyed");
}

esp_err_t gba_load_rom(gba_t *gba, const uint8_t *rom_data, size_t rom_size)
{
    if (gba == NULL || rom_data == NULL || rom_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (rom_size > ROM_MAX_SIZE) {
        ESP_LOGE(TAG, "ROM too large: %zu bytes (max %d)", rom_size, ROM_MAX_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    // Allocate ROM in PSRAM if available, otherwise internal
    if (gba->mem.rom != NULL) {
        free(gba->mem.rom);
    }

    gba->mem.rom = heap_caps_malloc(rom_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (gba->mem.rom == NULL) {
        // Try internal memory
        gba->mem.rom = heap_caps_malloc(rom_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (gba->mem.rom == NULL) {
            ESP_LOGE(TAG, "Failed to allocate ROM memory");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGW(TAG, "ROM allocated in internal memory");
    } else {
        ESP_LOGI(TAG, "ROM allocated in PSRAM");
    }

    memcpy(gba->mem.rom, rom_data, rom_size);
    gba->mem.rom_size = rom_size;

    // Extract game title (offset 0xA0, 12 bytes)
    memcpy(gba->game_title, &rom_data[0xA0], 12);
    gba->game_title[12] = '\0';

    // Extract game code (offset 0xAC, 4 bytes)
    memcpy(gba->game_code, &rom_data[0xAC], 4);
    gba->game_code[4] = '\0';

    ESP_LOGI(TAG, "ROM loaded: %s (%s), size: %zu bytes",
             gba->game_title, gba->game_code, rom_size);

    // Reset after loading ROM
    gba_reset(gba);

    return ESP_OK;
}

esp_err_t gba_load_bios(gba_t *gba, const uint8_t *bios_data, size_t bios_size)
{
    if (gba == NULL || bios_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (bios_size != BIOS_SIZE) {
        ESP_LOGE(TAG, "Invalid BIOS size: %zu (expected %d)", bios_size, BIOS_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(gba->mem.bios, bios_data, BIOS_SIZE);
    gba->mem.bios_loaded = true;

    ESP_LOGI(TAG, "BIOS loaded");
    return ESP_OK;
}

void gba_reset(gba_t *gba)
{
    if (gba == NULL) return;

    cpu_reset(gba);
    ppu_reset(gba);
    apu_reset(gba);
    dma_reset(gba);
    timer_reset(gba);
    memory_reset(gba);

    // Clear interrupt registers
    gba->ie = 0;
    gba->if_ = 0;
    gba->ime = 0;
    gba->keyinput = 0x03FF;

    ESP_LOGI(TAG, "GBA reset complete");
}

void gba_run_frame(gba_t *gba)
{
    if (gba == NULL) return;

    uint32_t frame_cycles = 0;

    while (frame_cycles < FRAME_CYCLES) {
        uint32_t cycles;

        // Check for DMA first
        if (dma_active(gba)) {
            cycles = dma_run(gba);
        } else if (gba->cpu.halted) {
            // CPU halted - just advance time to next event
            cycles = 1;
        } else {
            // Execute CPU instruction
            cycles = cpu_execute(gba);
        }

        // Step PPU
        ppu_step(gba, cycles);

        // Step APU
        apu_step(gba, cycles);

        // Step timers
        timer_step(gba, cycles);

        frame_cycles += cycles;
    }
}

uint32_t gba_run_cycles(gba_t *gba, uint32_t target_cycles)
{
    if (gba == NULL) return 0;

    uint32_t total_cycles = 0;

    while (total_cycles < target_cycles) {
        uint32_t cycles;

        if (dma_active(gba)) {
            cycles = dma_run(gba);
        } else if (gba->cpu.halted) {
            cycles = 1;
        } else {
            cycles = cpu_execute(gba);
        }

        ppu_step(gba, cycles);
        apu_step(gba, cycles);
        timer_step(gba, cycles);

        total_cycles += cycles;
    }

    return total_cycles;
}

void gba_set_buttons(gba_t *gba, uint16_t buttons)
{
    if (gba == NULL) return;

    // KEYINPUT is active low - 0 means pressed
    gba->keyinput = (~buttons) & 0x03FF;
}

void gba_set_frame_callback(gba_t *gba, gba_frame_callback_t callback)
{
    if (gba == NULL) return;
    gba->frame_callback = callback;
}

void gba_audio_get_samples(gba_t *gba, int16_t *buffer, size_t samples)
{
    if (gba == NULL || buffer == NULL) return;

    // TODO: Implement proper audio resampling
    // For now, just output silence or last APU state
    for (size_t i = 0; i < samples; i++) {
        buffer[i * 2] = gba->apu.left_output;
        buffer[i * 2 + 1] = gba->apu.right_output;
    }
}

const uint16_t *gba_get_framebuffer(gba_t *gba)
{
    if (gba == NULL) return NULL;
    return gba->ppu.framebuffer;
}

size_t gba_save_state(gba_t *gba, void *buffer, size_t size)
{
    // Calculate required size
    size_t required = sizeof(cpu_state_t) + sizeof(ppu_state_t) +
                      sizeof(apu_state_t) + EWRAM_SIZE + IWRAM_SIZE +
                      PALETTE_SIZE + VRAM_SIZE + OAM_SIZE + IO_SIZE;

    if (buffer == NULL) {
        return required;
    }

    if (size < required) {
        return 0;
    }

    uint8_t *ptr = buffer;

    // Save CPU state
    memcpy(ptr, &gba->cpu, sizeof(cpu_state_t));
    ptr += sizeof(cpu_state_t);

    // Save PPU state
    memcpy(ptr, &gba->ppu, sizeof(ppu_state_t));
    ptr += sizeof(ppu_state_t);

    // Save APU state
    memcpy(ptr, &gba->apu, sizeof(apu_state_t));
    ptr += sizeof(apu_state_t);

    // Save memory regions
    memcpy(ptr, gba->mem.ewram, EWRAM_SIZE);
    ptr += EWRAM_SIZE;

    memcpy(ptr, gba->mem.iwram, IWRAM_SIZE);
    ptr += IWRAM_SIZE;

    memcpy(ptr, gba->mem.palette, PALETTE_SIZE);
    ptr += PALETTE_SIZE;

    memcpy(ptr, gba->mem.vram, VRAM_SIZE);
    ptr += VRAM_SIZE;

    memcpy(ptr, gba->mem.oam, OAM_SIZE);
    ptr += OAM_SIZE;

    memcpy(ptr, gba->mem.io, IO_SIZE);

    return required;
}

esp_err_t gba_load_state(gba_t *gba, const void *buffer, size_t size)
{
    size_t required = gba_save_state(gba, NULL, 0);
    if (size < required) {
        return ESP_ERR_INVALID_SIZE;
    }

    const uint8_t *ptr = buffer;

    // Load CPU state
    memcpy(&gba->cpu, ptr, sizeof(cpu_state_t));
    ptr += sizeof(cpu_state_t);

    // Load PPU state
    memcpy(&gba->ppu, ptr, sizeof(ppu_state_t));
    ptr += sizeof(ppu_state_t);

    // Load APU state
    memcpy(&gba->apu, ptr, sizeof(apu_state_t));
    ptr += sizeof(apu_state_t);

    // Load memory regions
    memcpy(gba->mem.ewram, ptr, EWRAM_SIZE);
    ptr += EWRAM_SIZE;

    memcpy(gba->mem.iwram, ptr, IWRAM_SIZE);
    ptr += IWRAM_SIZE;

    memcpy(gba->mem.palette, ptr, PALETTE_SIZE);
    ptr += PALETTE_SIZE;

    memcpy(gba->mem.vram, ptr, VRAM_SIZE);
    ptr += VRAM_SIZE;

    memcpy(gba->mem.oam, ptr, OAM_SIZE);
    ptr += OAM_SIZE;

    memcpy(gba->mem.io, ptr, IO_SIZE);

    return ESP_OK;
}

void gba_get_game_title(gba_t *gba, char *title)
{
    if (gba == NULL || title == NULL) return;
    strcpy(title, gba->game_title);
}

void gba_get_game_code(gba_t *gba, char *code)
{
    if (gba == NULL || code == NULL) return;
    strcpy(code, gba->game_code);
}
