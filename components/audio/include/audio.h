/**
 * Audio Driver for ESP32-P4 GBA Emulator
 *
 * Uses I2S output for audio playback
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Audio callback function type
typedef void (*audio_callback_t)(int16_t *buffer, size_t samples);

// Audio configuration structure
typedef struct {
    uint32_t sample_rate;       // Sample rate in Hz (default 32768)
    audio_callback_t callback;  // Callback to fill audio buffer
} audio_config_t;

/**
 * Initialize the audio system
 *
 * @param config Audio configuration
 * @return ESP_OK on success
 */
esp_err_t audio_init(const audio_config_t *config);

/**
 * Start audio playback
 */
void audio_start(void);

/**
 * Stop audio playback
 */
void audio_stop(void);

/**
 * Set audio volume
 *
 * @param volume Volume level 0-100
 */
void audio_set_volume(uint8_t volume);

/**
 * Check if audio is currently playing
 *
 * @return true if playing
 */
bool audio_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_H
