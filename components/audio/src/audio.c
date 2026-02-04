/**
 * Audio Driver Implementation
 *
 * Uses I2S for audio output
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "audio.h"

static const char *TAG = "AUDIO";

// I2S configuration - adjust for your hardware
#define I2S_NUM         I2S_NUM_0
#define I2S_BCK_PIN     GPIO_NUM_26
#define I2S_WS_PIN      GPIO_NUM_25
#define I2S_DO_PIN      GPIO_NUM_27
#define I2S_DI_PIN      -1  // Not used

// Audio buffer configuration
#define AUDIO_BUFFER_SIZE 1024
#define AUDIO_BUFFER_COUNT 4

// Audio state
static i2s_chan_handle_t tx_handle = NULL;
static audio_callback_t audio_callback = NULL;
static TaskHandle_t audio_task_handle = NULL;
static volatile bool audio_running = false;
static uint8_t volume = 100;
static int16_t audio_buffer[AUDIO_BUFFER_SIZE * 2];  // Stereo

// Audio task - continuously feeds samples to I2S
static void audio_task(void *arg)
{
    size_t bytes_written;

    ESP_LOGI(TAG, "Audio task started on core %d", xPortGetCoreID());

    while (audio_running) {
        // Get samples from callback
        if (audio_callback) {
            audio_callback(audio_buffer, AUDIO_BUFFER_SIZE);

            // Apply volume
            if (volume < 100) {
                for (int i = 0; i < AUDIO_BUFFER_SIZE * 2; i++) {
                    audio_buffer[i] = (audio_buffer[i] * volume) / 100;
                }
            }
        } else {
            // Output silence
            memset(audio_buffer, 0, sizeof(audio_buffer));
        }

        // Write to I2S
        i2s_channel_write(tx_handle, audio_buffer, sizeof(audio_buffer),
                          &bytes_written, portMAX_DELAY);
    }

    ESP_LOGI(TAG, "Audio task stopped");
    vTaskDelete(NULL);
}

esp_err_t audio_init(const audio_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing audio (sample rate: %lu Hz)", (unsigned long)config->sample_rate);

    audio_callback = config->callback;

    // Configure I2S channel
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = AUDIO_BUFFER_COUNT;
    chan_cfg.dma_frame_num = AUDIO_BUFFER_SIZE;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    // Configure I2S standard mode
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config->sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_DO_PIN,
            .din = I2S_DI_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));

    ESP_LOGI(TAG, "Audio initialized");
    return ESP_OK;
}

void audio_start(void)
{
    if (audio_running) return;

    ESP_LOGI(TAG, "Starting audio playback");

    // Enable I2S channel
    i2s_channel_enable(tx_handle);

    // Start audio task on core 0 (emulation runs on core 1)
    audio_running = true;
    xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, NULL,
                            configMAX_PRIORITIES - 2, &audio_task_handle, 0);
}

void audio_stop(void)
{
    if (!audio_running) return;

    ESP_LOGI(TAG, "Stopping audio playback");

    audio_running = false;

    // Wait for task to finish
    if (audio_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(50));
        audio_task_handle = NULL;
    }

    // Disable I2S channel
    i2s_channel_disable(tx_handle);
}

void audio_set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    volume = vol;
}

bool audio_is_playing(void)
{
    return audio_running;
}
