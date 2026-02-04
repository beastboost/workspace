/**
 * GBA Emulator for ESP32-P4
 *
 * Main application entry point
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_psram.h"
#include "nvs_flash.h"

#include "gba.h"
#include "display.h"
#include "input.h"
#include "audio.h"
#include "rom_loader.h"

static const char *TAG = "GBA_EMU";

// Emulator state
static gba_t *gba = NULL;
static volatile bool emulator_running = false;
static SemaphoreHandle_t frame_sync_sem = NULL;

// Performance tracking
static uint32_t frame_count = 0;
static int64_t last_fps_time = 0;
static float current_fps = 0.0f;

// Frame timing
#define GBA_FPS 59.7275f
#define FRAME_TIME_US (uint32_t)(1000000.0f / GBA_FPS)

/**
 * Audio callback - called from audio driver when buffer needs filling
 */
static void audio_callback(int16_t *buffer, size_t samples)
{
    if (gba && emulator_running) {
        gba_audio_get_samples(gba, buffer, samples);
    } else {
        memset(buffer, 0, samples * 2 * sizeof(int16_t));
    }
}

/**
 * Input polling - called each frame
 */
static uint16_t poll_input(void)
{
    input_state_t state;
    input_poll(&state);

    uint16_t buttons = 0;
    if (state.a) buttons |= GBA_BUTTON_A;
    if (state.b) buttons |= GBA_BUTTON_B;
    if (state.select) buttons |= GBA_BUTTON_SELECT;
    if (state.start) buttons |= GBA_BUTTON_START;
    if (state.right) buttons |= GBA_BUTTON_RIGHT;
    if (state.left) buttons |= GBA_BUTTON_LEFT;
    if (state.up) buttons |= GBA_BUTTON_UP;
    if (state.down) buttons |= GBA_BUTTON_DOWN;
    if (state.r) buttons |= GBA_BUTTON_R;
    if (state.l) buttons |= GBA_BUTTON_L;

    return buttons;
}

/**
 * Frame complete callback - called when PPU finishes a frame
 */
static void frame_complete_callback(const uint16_t *framebuffer)
{
    display_update(framebuffer);

    frame_count++;

    // Calculate FPS every second
    int64_t now = esp_timer_get_time();
    if (now - last_fps_time >= 1000000) {
        current_fps = (float)frame_count * 1000000.0f / (float)(now - last_fps_time);
        frame_count = 0;
        last_fps_time = now;
        ESP_LOGI(TAG, "FPS: %.1f", current_fps);
    }

    if (frame_sync_sem) {
        xSemaphoreGive(frame_sync_sem);
    }
}

/**
 * Main emulation loop - runs on CPU core 1
 */
static void emulation_task(void *arg)
{
    ESP_LOGI(TAG, "Emulation task started on core %d", xPortGetCoreID());

    int64_t frame_start;
    int64_t frame_end;
    int64_t frame_time;

    while (emulator_running) {
        frame_start = esp_timer_get_time();

        // Update input
        uint16_t buttons = poll_input();
        gba_set_buttons(gba, buttons);

        // Run one frame
        gba_run_frame(gba);

        // Frame timing
        frame_end = esp_timer_get_time();
        frame_time = frame_end - frame_start;

        // Wait for next frame if we're running too fast
        if (frame_time < FRAME_TIME_US) {
            int64_t delay_us = FRAME_TIME_US - frame_time;
            if (delay_us > 1000) {
                vTaskDelay(pdMS_TO_TICKS(delay_us / 1000));
            }
        }
    }

    ESP_LOGI(TAG, "Emulation task stopped");
    vTaskDelete(NULL);
}

/**
 * Initialize NVS
 */
static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

/**
 * Initialize PSRAM
 */
static esp_err_t init_psram(void)
{
    esp_err_t ret = esp_psram_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PSRAM init failed, continuing without external memory");
        return ret;
    }

    size_t psram_size = esp_psram_get_size();
    ESP_LOGI(TAG, "PSRAM initialized, size: %zu bytes", psram_size);

    return ESP_OK;
}

/**
 * ROM selection menu (placeholder - can be expanded)
 */
static char* select_rom(void)
{
    // List available ROMs
    rom_list_t rom_list;
    esp_err_t ret = rom_loader_list("/sdcard/gba", &rom_list);

    if (ret != ESP_OK || rom_list.count == 0) {
        ESP_LOGE(TAG, "No ROMs found on SD card");
        return NULL;
    }

    ESP_LOGI(TAG, "Found %d ROM(s):", rom_list.count);
    for (int i = 0; i < rom_list.count && i < 10; i++) {
        ESP_LOGI(TAG, "  %d: %s", i + 1, rom_list.entries[i].name);
    }

    // For now, just load the first ROM
    // TODO: Add proper menu UI
    char *rom_path = strdup(rom_list.entries[0].path);
    rom_loader_free_list(&rom_list);

    return rom_path;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "  GBA Emulator for ESP32-P4");
    ESP_LOGI(TAG, "=================================");

    // Initialize NVS
    ESP_ERROR_CHECK(init_nvs());

    // Initialize PSRAM
    init_psram();

    // Initialize display
    ESP_LOGI(TAG, "Initializing display...");
    ESP_ERROR_CHECK(display_init());
    display_clear(0x0000);

    // Initialize input
    ESP_LOGI(TAG, "Initializing input...");
    ESP_ERROR_CHECK(input_init());

    // Initialize audio
    ESP_LOGI(TAG, "Initializing audio...");
    audio_config_t audio_cfg = {
        .sample_rate = 32768,
        .callback = audio_callback,
    };
    ESP_ERROR_CHECK(audio_init(&audio_cfg));

    // Initialize ROM loader (mount SD card)
    ESP_LOGI(TAG, "Initializing ROM loader...");
    ESP_ERROR_CHECK(rom_loader_init());

    // Select and load ROM
    char *rom_path = select_rom();
    if (rom_path == NULL) {
        ESP_LOGE(TAG, "No ROM selected, halting");
        display_show_message("No ROMs found!\nPlace .gba files in\n/sdcard/gba/");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "Loading ROM: %s", rom_path);

    // Load ROM data
    uint8_t *rom_data = NULL;
    size_t rom_size = 0;
    esp_err_t ret = rom_loader_load(rom_path, &rom_data, &rom_size);
    free(rom_path);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load ROM");
        display_show_message("Failed to load ROM!");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "ROM loaded, size: %zu bytes", rom_size);

    // Create GBA instance
    ESP_LOGI(TAG, "Creating GBA instance...");
    gba = gba_create();
    if (gba == NULL) {
        ESP_LOGE(TAG, "Failed to create GBA instance");
        free(rom_data);
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Set callbacks
    gba_set_frame_callback(gba, frame_complete_callback);

    // Load ROM into GBA
    ret = gba_load_rom(gba, rom_data, rom_size);
    free(rom_data);  // GBA core makes its own copy

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load ROM into GBA");
        gba_destroy(gba);
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Create frame sync semaphore
    frame_sync_sem = xSemaphoreCreateBinary();

    // Start audio
    audio_start();

    // Start emulation
    ESP_LOGI(TAG, "Starting emulation...");
    emulator_running = true;
    last_fps_time = esp_timer_get_time();

    // Create emulation task on core 1
    xTaskCreatePinnedToCore(
        emulation_task,
        "emu_task",
        8192,
        NULL,
        configMAX_PRIORITIES - 1,
        NULL,
        1  // Core 1
    );

    // Main loop on core 0 handles display updates
    while (1) {
        // Wait for frame to complete
        if (xSemaphoreTake(frame_sync_sem, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Frame sync received
            display_vsync();
        }

        // Check for special button combos (e.g., menu)
        input_state_t state;
        input_poll(&state);

        if (state.start && state.select && state.l && state.r) {
            // Open menu (TODO)
            ESP_LOGI(TAG, "Menu requested");
        }
    }
}
