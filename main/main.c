/**
 * GBA Emulator for ESP32-P4
 *
 * Main application entry point with web interface support
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_psram.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"

// WiFi support via ESP32-C6 coprocessor (ESP-Hosted)
#include "esp_wifi.h"

#include "gba.h"
#include "display.h"
#include "input.h"
#include "audio.h"
#include "rom_loader.h"
#include "webserver.h"

static const char *TAG = "GBA_EMU";

// WiFi credentials - configure via menuconfig or hardcode
#ifndef CONFIG_WIFI_SSID
#define CONFIG_WIFI_SSID "YourWiFiSSID"
#endif
#ifndef CONFIG_WIFI_PASSWORD
#define CONFIG_WIFI_PASSWORD "YourWiFiPassword"
#endif

// WiFi event group
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Emulator state
static gba_t *gba = NULL;
static volatile bool emulator_running = false;
static volatile bool emulator_paused = false;
static SemaphoreHandle_t frame_sync_sem = NULL;
static SemaphoreHandle_t gba_mutex = NULL;

// Web input state (from WebSocket)
static volatile uint16_t web_buttons = 0;

// Current ROM info
static char current_rom_name[64] = {0};
static char current_rom_path[128] = {0};

// Performance tracking
static uint32_t frame_count = 0;
static int64_t last_fps_time = 0;
static float current_fps = 0.0f;

// Frame timing
#define GBA_FPS 59.7275f
#define FRAME_TIME_US (uint32_t)(1000000.0f / GBA_FPS)

/**
 * WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    static int retry_count = 0;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_count < 10) {
            esp_wifi_connect();
            retry_count++;
            ESP_LOGI(TAG, "Retrying WiFi connection...");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * Initialize WiFi
 */
static esp_err_t init_wifi(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization complete, connecting to %s...", CONFIG_WIFI_SSID);

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Failed to connect to WiFi");
        return ESP_FAIL;
    }
}

/**
 * Web input callback - called from WebSocket handler
 */
static void web_input_callback(uint16_t buttons)
{
    web_buttons = buttons;
}

/**
 * Web ROM selection callback
 */
static void web_rom_callback(const char *rom_name)
{
    ESP_LOGI(TAG, "Web request to load ROM: %s", rom_name);

    // Build full path
    char rom_path[128];
    snprintf(rom_path, sizeof(rom_path), "/sdcard/gba/%s", rom_name);

    // Load ROM in a separate task to avoid blocking WebSocket
    strncpy(current_rom_path, rom_path, sizeof(current_rom_path) - 1);
    strncpy(current_rom_name, rom_name, sizeof(current_rom_name) - 1);

    // Signal main task to reload ROM
    // For now, we'll just restart emulation
    emulator_running = false;
    vTaskDelay(pdMS_TO_TICKS(100));

    // Load new ROM
    uint8_t *rom_data = NULL;
    size_t rom_size = 0;
    esp_err_t ret = rom_loader_load(rom_path, &rom_data, &rom_size);

    if (ret == ESP_OK && rom_data != NULL) {
        xSemaphoreTake(gba_mutex, portMAX_DELAY);

        gba_reset(gba);
        ret = gba_load_rom(gba, rom_data, rom_size);
        free(rom_data);

        xSemaphoreGive(gba_mutex);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "ROM loaded successfully: %s", rom_name);
            emulator_running = true;
            emulator_paused = false;
        }
    }
}

/**
 * Web command callback (pause, reset, etc.)
 */
static void web_command_callback(const char *command)
{
    ESP_LOGI(TAG, "Web command: %s", command);

    if (strcmp(command, "pause") == 0) {
        emulator_paused = true;
    } else if (strcmp(command, "resume") == 0) {
        emulator_paused = false;
    } else if (strcmp(command, "reset") == 0) {
        xSemaphoreTake(gba_mutex, portMAX_DELAY);
        gba_reset(gba);
        xSemaphoreGive(gba_mutex);
    } else if (strcmp(command, "mute") == 0) {
        audio_set_volume(0);
    } else if (strcmp(command, "unmute") == 0) {
        audio_set_volume(100);
    }
}

/**
 * Audio callback - called from audio driver when buffer needs filling
 */
static void audio_callback(int16_t *buffer, size_t samples)
{
    if (gba && emulator_running && !emulator_paused) {
        gba_audio_get_samples(gba, buffer, samples);
    } else {
        memset(buffer, 0, samples * 2 * sizeof(int16_t));
    }
}

/**
 * Input polling - combines physical buttons and web input
 */
static uint16_t poll_input(void)
{
    input_state_t state;
    input_poll(&state);

    uint16_t buttons = web_buttons;  // Start with web input

    // OR with physical buttons
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
    // Update local display
    display_update(framebuffer);

    // Send to web clients
    webserver_send_frame(framebuffer);

    frame_count++;

    // Calculate FPS every second
    int64_t now = esp_timer_get_time();
    if (now - last_fps_time >= 1000000) {
        current_fps = (float)frame_count * 1000000.0f / (float)(now - last_fps_time);
        frame_count = 0;
        last_fps_time = now;

        // Update web server status
        webserver_update_status(current_fps, current_rom_name, emulator_paused);

        ESP_LOGI(TAG, "FPS: %.1f, Clients: %d", current_fps, webserver_get_client_count());
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

    while (1) {
        if (!emulator_running || emulator_paused) {
            vTaskDelay(pdMS_TO_TICKS(16));
            continue;
        }

        frame_start = esp_timer_get_time();

        xSemaphoreTake(gba_mutex, portMAX_DELAY);

        // Update input
        uint16_t buttons = poll_input();
        gba_set_buttons(gba, buttons);

        // Run one frame
        gba_run_frame(gba);

        xSemaphoreGive(gba_mutex);

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
 * Load first available ROM or wait for web upload
 */
static bool load_initial_rom(void)
{
    rom_list_t rom_list;
    esp_err_t ret = rom_loader_list("/sdcard/gba", &rom_list);

    if (ret != ESP_OK || rom_list.count == 0) {
        ESP_LOGI(TAG, "No ROMs found, waiting for web upload...");
        return false;
    }

    ESP_LOGI(TAG, "Found %d ROM(s), loading first one", rom_list.count);

    // Load first ROM
    strncpy(current_rom_name, rom_list.entries[0].name, sizeof(current_rom_name) - 1);
    strncpy(current_rom_path, rom_list.entries[0].path, sizeof(current_rom_path) - 1);

    uint8_t *rom_data = NULL;
    size_t rom_size = 0;
    ret = rom_loader_load(current_rom_path, &rom_data, &rom_size);

    rom_loader_free_list(&rom_list);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load ROM");
        return false;
    }

    ret = gba_load_rom(gba, rom_data, rom_size);
    free(rom_data);

    return ret == ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "  GBA Emulator for ESP32-P4 (Guition)");
    ESP_LOGI(TAG, "  Web Interface Enabled");
    ESP_LOGI(TAG, "==========================================");

    // Initialize NVS
    ESP_ERROR_CHECK(init_nvs());

    // Initialize PSRAM
    init_psram();

    // Create mutex for GBA access
    gba_mutex = xSemaphoreCreateMutex();

    // Initialize display
    ESP_LOGI(TAG, "Initializing display...");
    ESP_ERROR_CHECK(display_init());
    display_clear(0x0000);
    display_show_message("GBA Emulator\nInitializing...");

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

    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    display_show_message("Connecting to WiFi...");

    if (init_wifi() == ESP_OK) {
        // Initialize and start web server
        ESP_LOGI(TAG, "Starting web server...");

        webserver_config_t ws_config = {
            .port = 80,
            .input_cb = web_input_callback,
            .rom_cb = web_rom_callback,
            .cmd_cb = web_command_callback,
        };
        ESP_ERROR_CHECK(webserver_init(&ws_config));
        ESP_ERROR_CHECK(webserver_start());

        // Get IP address for display
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info);

        char ip_msg[64];
        snprintf(ip_msg, sizeof(ip_msg), "Web UI:\nhttp://" IPSTR, IP2STR(&ip_info.ip));
        display_show_message(ip_msg);
        ESP_LOGI(TAG, "Web server started at http://" IPSTR, IP2STR(&ip_info.ip));
    } else {
        display_show_message("WiFi failed\nUsing local mode");
    }

    // Create GBA instance
    ESP_LOGI(TAG, "Creating GBA instance...");
    gba = gba_create();
    if (gba == NULL) {
        ESP_LOGE(TAG, "Failed to create GBA instance");
        display_show_message("Error: Out of memory!");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Set callbacks
    gba_set_frame_callback(gba, frame_complete_callback);

    // Try to load initial ROM
    if (load_initial_rom()) {
        emulator_running = true;
        ESP_LOGI(TAG, "ROM loaded: %s", current_rom_name);
    } else {
        display_show_message("No ROMs found\nUpload via web UI");
    }

    // Create frame sync semaphore
    frame_sync_sem = xSemaphoreCreateBinary();

    // Start audio
    audio_start();

    // Start emulation task on core 1
    ESP_LOGI(TAG, "Starting emulation...");
    last_fps_time = esp_timer_get_time();

    xTaskCreatePinnedToCore(
        emulation_task,
        "emu_task",
        8192,
        NULL,
        configMAX_PRIORITIES - 1,
        NULL,
        1  // Core 1
    );

    // Main loop on core 0 handles display sync and menu
    while (1) {
        // Wait for frame to complete
        if (xSemaphoreTake(frame_sync_sem, pdMS_TO_TICKS(100)) == pdTRUE) {
            display_vsync();
        }

        // Check for special button combos (menu)
        input_state_t state;
        input_poll(&state);

        if (state.start && state.select && state.l && state.r) {
            ESP_LOGI(TAG, "Menu requested");
            // TODO: Show on-device menu
        }
    }
}
