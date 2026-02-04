/**
 * Display Driver for ESP32-P4 GBA Emulator
 *
 * Supports various LCD interfaces (RGB, SPI, MIPI-DSI)
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// GBA display dimensions
#define GBA_WIDTH  240
#define GBA_HEIGHT 160

// Display configuration (adjust based on your hardware)
#define DISPLAY_WIDTH  320     // Physical LCD width
#define DISPLAY_HEIGHT 240     // Physical LCD height

// Scale factor (will be calculated based on display size)
#define SCALE_X ((DISPLAY_WIDTH) / GBA_WIDTH)
#define SCALE_Y ((DISPLAY_HEIGHT) / GBA_HEIGHT)

/**
 * Initialize the display
 *
 * @return ESP_OK on success
 */
esp_err_t display_init(void);

/**
 * Clear the display with a solid color
 *
 * @param color RGB565 color value
 */
void display_clear(uint16_t color);

/**
 * Update the display with a new framebuffer
 *
 * @param framebuffer Pointer to 240x160 RGB565 framebuffer
 */
void display_update(const uint16_t *framebuffer);

/**
 * Wait for vertical sync
 */
void display_vsync(void);

/**
 * Show a text message on screen
 *
 * @param message Null-terminated string
 */
void display_show_message(const char *message);

/**
 * Set display brightness (if supported)
 *
 * @param brightness 0-100 percent
 */
void display_set_brightness(uint8_t brightness);

/**
 * Get display info
 *
 * @param width Output for display width
 * @param height Output for display height
 */
void display_get_size(uint16_t *width, uint16_t *height);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_H
