/**
 * Input Handler Implementation
 *
 * Configure GPIO pins for your button layout
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "input.h"

static const char *TAG = "INPUT";

// GPIO Pin configuration - adjust for your hardware
// Set to -1 to disable a button
#define PIN_BUTTON_A      GPIO_NUM_4
#define PIN_BUTTON_B      GPIO_NUM_5
#define PIN_BUTTON_SELECT GPIO_NUM_6
#define PIN_BUTTON_START  GPIO_NUM_7
#define PIN_BUTTON_UP     GPIO_NUM_15
#define PIN_BUTTON_DOWN   GPIO_NUM_16
#define PIN_BUTTON_LEFT   GPIO_NUM_17
#define PIN_BUTTON_RIGHT  GPIO_NUM_18
#define PIN_BUTTON_L      GPIO_NUM_19
#define PIN_BUTTON_R      GPIO_NUM_20

// Button is active low by default
#define BUTTON_ACTIVE_LEVEL 0

// Debounce time in milliseconds
#define DEBOUNCE_TIME_MS 10

// Input state
static input_state_t current_state;
static uint32_t last_poll_time = 0;

// Initialize a single button GPIO
static void init_button_gpio(gpio_num_t pin)
{
    if (pin < 0) return;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

// Read a button state
static bool read_button(gpio_num_t pin)
{
    if (pin < 0) return false;
    return gpio_get_level(pin) == BUTTON_ACTIVE_LEVEL;
}

esp_err_t input_init(void)
{
    ESP_LOGI(TAG, "Initializing input system");

    memset(&current_state, 0, sizeof(current_state));

    // Initialize button GPIOs
    init_button_gpio(PIN_BUTTON_A);
    init_button_gpio(PIN_BUTTON_B);
    init_button_gpio(PIN_BUTTON_SELECT);
    init_button_gpio(PIN_BUTTON_START);
    init_button_gpio(PIN_BUTTON_UP);
    init_button_gpio(PIN_BUTTON_DOWN);
    init_button_gpio(PIN_BUTTON_LEFT);
    init_button_gpio(PIN_BUTTON_RIGHT);
    init_button_gpio(PIN_BUTTON_L);
    init_button_gpio(PIN_BUTTON_R);

    ESP_LOGI(TAG, "Input system initialized");
    return ESP_OK;
}

void input_poll(input_state_t *state)
{
    if (state == NULL) return;

    // Simple debounce - only update if enough time has passed
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - last_poll_time < DEBOUNCE_TIME_MS) {
        *state = current_state;
        return;
    }
    last_poll_time = now;

    // Read all buttons
    current_state.a = read_button(PIN_BUTTON_A);
    current_state.b = read_button(PIN_BUTTON_B);
    current_state.select = read_button(PIN_BUTTON_SELECT);
    current_state.start = read_button(PIN_BUTTON_START);
    current_state.up = read_button(PIN_BUTTON_UP);
    current_state.down = read_button(PIN_BUTTON_DOWN);
    current_state.left = read_button(PIN_BUTTON_LEFT);
    current_state.right = read_button(PIN_BUTTON_RIGHT);
    current_state.l = read_button(PIN_BUTTON_L);
    current_state.r = read_button(PIN_BUTTON_R);

    // Prevent opposite directions from being pressed simultaneously
    if (current_state.up && current_state.down) {
        current_state.up = false;
        current_state.down = false;
    }
    if (current_state.left && current_state.right) {
        current_state.left = false;
        current_state.right = false;
    }

    *state = current_state;
}

bool input_any_pressed(void)
{
    input_state_t state;
    input_poll(&state);

    return state.a || state.b || state.select || state.start ||
           state.up || state.down || state.left || state.right ||
           state.l || state.r;
}

void input_set_repeat(uint32_t initial_delay_ms, uint32_t repeat_rate_ms)
{
    // TODO: Implement key repeat functionality
    (void)initial_delay_ms;
    (void)repeat_rate_ms;
}
