/**
 * GBA Emulator Web Server
 *
 * Provides web-based interface for:
 * - ROM upload and management
 * - Video streaming via WebSocket
 * - Input control via WebSocket
 * - Settings and status
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Video streaming configuration
#define WEBSERVER_STREAM_FPS     30
#define WEBSERVER_JPEG_QUALITY   80

// Callback types
typedef void (*webserver_input_callback_t)(uint16_t buttons);
typedef void (*webserver_rom_callback_t)(const char *rom_name);
typedef void (*webserver_command_callback_t)(const char *command);

// Web server configuration
typedef struct {
    uint16_t port;                          // HTTP port (default 80)
    webserver_input_callback_t input_cb;    // Input state callback
    webserver_rom_callback_t rom_cb;        // ROM selection callback
    webserver_command_callback_t cmd_cb;    // Command callback (pause, reset, etc.)
} webserver_config_t;

/**
 * Initialize the web server
 *
 * @param config Server configuration
 * @return ESP_OK on success
 */
esp_err_t webserver_init(const webserver_config_t *config);

/**
 * Start the web server
 *
 * @return ESP_OK on success
 */
esp_err_t webserver_start(void);

/**
 * Stop the web server
 */
void webserver_stop(void);

/**
 * Send a video frame to connected clients
 *
 * @param framebuffer RGB565 framebuffer (240x160)
 */
void webserver_send_frame(const uint16_t *framebuffer);

/**
 * Update emulator status
 *
 * @param fps Current FPS
 * @param rom_name Current ROM name (or NULL if none)
 * @param paused True if emulator is paused
 */
void webserver_update_status(float fps, const char *rom_name, bool paused);

/**
 * Get number of connected clients
 *
 * @return Number of WebSocket clients
 */
int webserver_get_client_count(void);

/**
 * Check if any clients are connected
 *
 * @return true if at least one client is connected
 */
bool webserver_has_clients(void);

#ifdef __cplusplus
}
#endif

#endif // WEBSERVER_H
