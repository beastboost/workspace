/**
 * Input Handler for ESP32-P4 GBA Emulator
 *
 * Supports GPIO buttons, I2C gamepads, and USB HID controllers
 */

#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Input state structure
typedef struct {
    bool a;
    bool b;
    bool select;
    bool start;
    bool right;
    bool left;
    bool up;
    bool down;
    bool r;
    bool l;
} input_state_t;

/**
 * Initialize the input system
 *
 * @return ESP_OK on success
 */
esp_err_t input_init(void);

/**
 * Poll the current input state
 *
 * @param state Output structure for button states
 */
void input_poll(input_state_t *state);

/**
 * Check if any button is pressed
 *
 * @return true if any button is pressed
 */
bool input_any_pressed(void);

/**
 * Set button repeat rate
 *
 * @param initial_delay_ms Initial delay before repeat starts
 * @param repeat_rate_ms Delay between repeats
 */
void input_set_repeat(uint32_t initial_delay_ms, uint32_t repeat_rate_ms);

#ifdef __cplusplus
}
#endif

#endif // INPUT_H
