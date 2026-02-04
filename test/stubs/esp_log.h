/**
 * ESP-IDF Logging Stub for Host Testing
 */

#ifndef _ESP_LOG_H_
#define _ESP_LOG_H_

#include <stdio.h>

typedef enum {
    ESP_LOG_NONE,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE
} esp_log_level_t;

#define ESP_LOGE(tag, format, ...) printf("[ERROR] %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) printf("[WARN] %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) printf("[INFO] %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) /* Debug logs disabled in tests */
#define ESP_LOGV(tag, format, ...) /* Verbose logs disabled in tests */

#define esp_log_level_set(tag, level) (void)(tag); (void)(level)

#endif /* _ESP_LOG_H_ */
