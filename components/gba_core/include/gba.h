/**
 * GBA Emulator Core - Main Header
 *
 * Game Boy Advance emulation for ESP32-P4
 */

#ifndef GBA_H
#define GBA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// GBA Display dimensions
#define GBA_SCREEN_WIDTH  240
#define GBA_SCREEN_HEIGHT 160

// GBA CPU frequency (16.78 MHz)
#define GBA_CPU_FREQ 16777216

// GBA button masks
#define GBA_BUTTON_A      (1 << 0)
#define GBA_BUTTON_B      (1 << 1)
#define GBA_BUTTON_SELECT (1 << 2)
#define GBA_BUTTON_START  (1 << 3)
#define GBA_BUTTON_RIGHT  (1 << 4)
#define GBA_BUTTON_LEFT   (1 << 5)
#define GBA_BUTTON_UP     (1 << 6)
#define GBA_BUTTON_DOWN   (1 << 7)
#define GBA_BUTTON_R      (1 << 8)
#define GBA_BUTTON_L      (1 << 9)

// Opaque GBA instance type
typedef struct gba gba_t;

// Frame callback function type
typedef void (*gba_frame_callback_t)(const uint16_t *framebuffer);

/**
 * Create a new GBA instance
 *
 * @return Pointer to GBA instance, or NULL on failure
 */
gba_t *gba_create(void);

/**
 * Destroy a GBA instance and free all resources
 *
 * @param gba GBA instance
 */
void gba_destroy(gba_t *gba);

/**
 * Load a ROM into the GBA
 *
 * @param gba GBA instance
 * @param rom_data ROM data buffer
 * @param rom_size Size of ROM in bytes
 * @return ESP_OK on success
 */
esp_err_t gba_load_rom(gba_t *gba, const uint8_t *rom_data, size_t rom_size);

/**
 * Load a BIOS file (optional - uses HLE BIOS if not loaded)
 *
 * @param gba GBA instance
 * @param bios_data BIOS data buffer
 * @param bios_size Size of BIOS (should be 16KB)
 * @return ESP_OK on success
 */
esp_err_t gba_load_bios(gba_t *gba, const uint8_t *bios_data, size_t bios_size);

/**
 * Reset the GBA
 *
 * @param gba GBA instance
 */
void gba_reset(gba_t *gba);

/**
 * Run the GBA for one frame (~280896 cycles)
 *
 * @param gba GBA instance
 */
void gba_run_frame(gba_t *gba);

/**
 * Run the GBA for a specific number of cycles
 *
 * @param gba GBA instance
 * @param cycles Number of cycles to run
 * @return Actual number of cycles executed
 */
uint32_t gba_run_cycles(gba_t *gba, uint32_t cycles);

/**
 * Set the button state
 *
 * @param gba GBA instance
 * @param buttons Button mask (GBA_BUTTON_* flags)
 */
void gba_set_buttons(gba_t *gba, uint16_t buttons);

/**
 * Set the frame complete callback
 *
 * @param gba GBA instance
 * @param callback Function to call when frame is complete
 */
void gba_set_frame_callback(gba_t *gba, gba_frame_callback_t callback);

/**
 * Get audio samples from the APU
 *
 * @param gba GBA instance
 * @param buffer Output buffer for stereo samples (interleaved L/R)
 * @param samples Number of stereo sample pairs to generate
 */
void gba_audio_get_samples(gba_t *gba, int16_t *buffer, size_t samples);

/**
 * Get the current framebuffer
 *
 * @param gba GBA instance
 * @return Pointer to 240x160 RGB565 framebuffer
 */
const uint16_t *gba_get_framebuffer(gba_t *gba);

/**
 * Save state to buffer
 *
 * @param gba GBA instance
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of bytes written, or required size if buffer is NULL
 */
size_t gba_save_state(gba_t *gba, void *buffer, size_t size);

/**
 * Load state from buffer
 *
 * @param gba GBA instance
 * @param buffer State data
 * @param size Size of state data
 * @return ESP_OK on success
 */
esp_err_t gba_load_state(gba_t *gba, const void *buffer, size_t size);

/**
 * Get game title from ROM header
 *
 * @param gba GBA instance
 * @param title Output buffer (at least 13 bytes)
 */
void gba_get_game_title(gba_t *gba, char *title);

/**
 * Get game code from ROM header
 *
 * @param gba GBA instance
 * @param code Output buffer (at least 5 bytes)
 */
void gba_get_game_code(gba_t *gba, char *code);

#ifdef __cplusplus
}
#endif

#endif // GBA_H
