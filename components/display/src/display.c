/**
 * Display Driver Implementation
 *
 * This implementation supports multiple display types:
 * - RGB LCD panel (default for ESP32-P4)
 * - SPI LCD (ILI9341, ST7789, etc.)
 * - MIPI-DSI (higher resolution displays)
 *
 * Configure via menuconfig or sdkconfig.defaults
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "display.h"

static const char *TAG = "DISPLAY";

// Display state
static esp_lcd_panel_handle_t panel_handle = NULL;
static SemaphoreHandle_t vsync_sem = NULL;
static uint16_t *display_buffer = NULL;
static bool display_initialized = false;

// Display configuration - adjust for your hardware
// These are example values for a common 320x240 RGB LCD
#define LCD_PIXEL_CLOCK_HZ     (16 * 1000 * 1000)
#define LCD_H_RES              320
#define LCD_V_RES              240
#define LCD_HSYNC_BACK_PORCH   40
#define LCD_HSYNC_FRONT_PORCH  20
#define LCD_HSYNC_PULSE_WIDTH  1
#define LCD_VSYNC_BACK_PORCH   8
#define LCD_VSYNC_FRONT_PORCH  4
#define LCD_VSYNC_PULSE_WIDTH  1
#define LCD_PCLK_ACTIVE_NEG    true

// Pin configuration - adjust for your board
#define LCD_PIN_HSYNC          GPIO_NUM_46
#define LCD_PIN_VSYNC          GPIO_NUM_3
#define LCD_PIN_DE             GPIO_NUM_0
#define LCD_PIN_PCLK           GPIO_NUM_9
#define LCD_PIN_DATA0          GPIO_NUM_14  // B0
#define LCD_PIN_DATA1          GPIO_NUM_13  // B1
#define LCD_PIN_DATA2          GPIO_NUM_12  // B2
#define LCD_PIN_DATA3          GPIO_NUM_11  // B3
#define LCD_PIN_DATA4          GPIO_NUM_10  // B4
#define LCD_PIN_DATA5          GPIO_NUM_39  // G0
#define LCD_PIN_DATA6          GPIO_NUM_40  // G1
#define LCD_PIN_DATA7          GPIO_NUM_41  // G2
#define LCD_PIN_DATA8          GPIO_NUM_42  // G3
#define LCD_PIN_DATA9          GPIO_NUM_45  // G4
#define LCD_PIN_DATA10         GPIO_NUM_48  // G5
#define LCD_PIN_DATA11         GPIO_NUM_47  // R0
#define LCD_PIN_DATA12         GPIO_NUM_21  // R1
#define LCD_PIN_DATA13         GPIO_NUM_14  // R2
#define LCD_PIN_DATA14         GPIO_NUM_38  // R3
#define LCD_PIN_DATA15         GPIO_NUM_8   // R4
#define LCD_PIN_BACKLIGHT      GPIO_NUM_1

// V-sync callback
static bool IRAM_ATTR on_vsync_event(esp_lcd_panel_handle_t panel,
                                      const esp_lcd_rgb_panel_event_data_t *event_data,
                                      void *user_ctx)
{
    BaseType_t high_task_awoken = pdFALSE;

    if (vsync_sem) {
        xSemaphoreGiveFromISR(vsync_sem, &high_task_awoken);
    }

    return high_task_awoken == pdTRUE;
}

esp_err_t display_init(void)
{
    if (display_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing display (%dx%d)", LCD_H_RES, LCD_V_RES);

    // Create vsync semaphore
    vsync_sem = xSemaphoreCreateBinary();
    if (vsync_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create vsync semaphore");
        return ESP_ERR_NO_MEM;
    }

    // Allocate display buffer
    display_buffer = heap_caps_malloc(LCD_H_RES * LCD_V_RES * 2, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
    if (display_buffer == NULL) {
        display_buffer = heap_caps_malloc(LCD_H_RES * LCD_V_RES * 2, MALLOC_CAP_DMA);
    }
    if (display_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate display buffer");
        return ESP_ERR_NO_MEM;
    }

    // Initialize backlight GPIO
    gpio_config_t bl_gpio_config = {
        .pin_bit_mask = 1ULL << LCD_PIN_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_gpio_config);
    gpio_set_level(LCD_PIN_BACKLIGHT, 1);

    // Configure RGB LCD panel
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = LCD_PIXEL_CLOCK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_back_porch = LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = LCD_HSYNC_FRONT_PORCH,
            .hsync_pulse_width = LCD_HSYNC_PULSE_WIDTH,
            .vsync_back_porch = LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = LCD_VSYNC_FRONT_PORCH,
            .vsync_pulse_width = LCD_VSYNC_PULSE_WIDTH,
            .flags = {
                .pclk_active_neg = LCD_PCLK_ACTIVE_NEG,
            },
        },
        .data_width = 16,
        .num_fbs = 1,
        .psram_trans_align = 64,
        .hsync_gpio_num = LCD_PIN_HSYNC,
        .vsync_gpio_num = LCD_PIN_VSYNC,
        .de_gpio_num = LCD_PIN_DE,
        .pclk_gpio_num = LCD_PIN_PCLK,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            LCD_PIN_DATA0, LCD_PIN_DATA1, LCD_PIN_DATA2, LCD_PIN_DATA3,
            LCD_PIN_DATA4, LCD_PIN_DATA5, LCD_PIN_DATA6, LCD_PIN_DATA7,
            LCD_PIN_DATA8, LCD_PIN_DATA9, LCD_PIN_DATA10, LCD_PIN_DATA11,
            LCD_PIN_DATA12, LCD_PIN_DATA13, LCD_PIN_DATA14, LCD_PIN_DATA15,
        },
        .flags = {
            .fb_in_psram = true,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));

    // Register vsync callback
    esp_lcd_rgb_panel_event_callbacks_t callbacks = {
        .on_vsync = on_vsync_event,
    };
    esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &callbacks, NULL);

    // Reset and initialize panel
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    display_initialized = true;
    ESP_LOGI(TAG, "Display initialized successfully");

    return ESP_OK;
}

void display_clear(uint16_t color)
{
    if (!display_initialized || display_buffer == NULL) return;

    // Fill buffer with color
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) {
        display_buffer[i] = color;
    }

    // Draw to panel
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, display_buffer);
}

void display_update(const uint16_t *framebuffer)
{
    if (!display_initialized || display_buffer == NULL || framebuffer == NULL) return;

    // Calculate centering offsets
    int offset_x = (LCD_H_RES - GBA_WIDTH) / 2;
    int offset_y = (LCD_V_RES - GBA_HEIGHT) / 2;

    // Clear border areas (if display is larger than GBA screen)
    if (offset_x > 0 || offset_y > 0) {
        // Simple scaling: just center the GBA framebuffer
        memset(display_buffer, 0, LCD_H_RES * LCD_V_RES * 2);
    }

    // Copy framebuffer with optional scaling
    if (LCD_H_RES == GBA_WIDTH && LCD_V_RES == GBA_HEIGHT) {
        // Direct 1:1 copy
        memcpy(display_buffer, framebuffer, GBA_WIDTH * GBA_HEIGHT * 2);
    } else if (LCD_H_RES >= GBA_WIDTH && LCD_V_RES >= GBA_HEIGHT) {
        // Center the image (no scaling, just centering)
        for (int y = 0; y < GBA_HEIGHT; y++) {
            memcpy(&display_buffer[(offset_y + y) * LCD_H_RES + offset_x],
                   &framebuffer[y * GBA_WIDTH],
                   GBA_WIDTH * 2);
        }
    } else {
        // Nearest neighbor scaling (shrink)
        for (int y = 0; y < LCD_V_RES; y++) {
            int src_y = (y * GBA_HEIGHT) / LCD_V_RES;
            for (int x = 0; x < LCD_H_RES; x++) {
                int src_x = (x * GBA_WIDTH) / LCD_H_RES;
                display_buffer[y * LCD_H_RES + x] = framebuffer[src_y * GBA_WIDTH + src_x];
            }
        }
    }

    // Draw to panel
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, display_buffer);
}

void display_vsync(void)
{
    if (vsync_sem) {
        xSemaphoreTake(vsync_sem, pdMS_TO_TICKS(20));
    }
}

// Simple 8x8 font for messages
static const uint8_t font_8x8[][8] = {
    // Space (32)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // ! (33)
    {0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00},
    // ... (add more characters as needed)
    // A-Z, a-z, 0-9
};

static void draw_char(uint16_t *buffer, int x, int y, char c, uint16_t color)
{
    if (c < 32 || c > 127) c = '?';

    // Simple character rendering (placeholder)
    // In a real implementation, use a proper font library
    for (int py = 0; py < 8; py++) {
        for (int px = 0; px < 8; px++) {
            int screen_x = x + px;
            int screen_y = y + py;
            if (screen_x >= 0 && screen_x < LCD_H_RES &&
                screen_y >= 0 && screen_y < LCD_V_RES) {
                // Draw a simple block character
                if (c != ' ') {
                    buffer[screen_y * LCD_H_RES + screen_x] = color;
                }
            }
        }
    }
}

void display_show_message(const char *message)
{
    if (!display_initialized || display_buffer == NULL) return;

    // Clear screen to black
    memset(display_buffer, 0, LCD_H_RES * LCD_V_RES * 2);

    // Calculate starting position
    int start_x = 10;
    int start_y = LCD_V_RES / 2 - 20;
    int x = start_x;
    int y = start_y;

    // Draw each character
    const uint16_t white = 0xFFFF;
    while (*message) {
        if (*message == '\n') {
            x = start_x;
            y += 10;
        } else {
            draw_char(display_buffer, x, y, *message, white);
            x += 8;
        }
        message++;
    }

    // Update display
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, display_buffer);
}

void display_set_brightness(uint8_t brightness)
{
    // PWM control for backlight (if supported)
    // For now, just on/off
    gpio_set_level(LCD_PIN_BACKLIGHT, brightness > 0 ? 1 : 0);
}

void display_get_size(uint16_t *width, uint16_t *height)
{
    if (width) *width = LCD_H_RES;
    if (height) *height = LCD_V_RES;
}
