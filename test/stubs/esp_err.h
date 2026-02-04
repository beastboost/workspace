/**
 * ESP-IDF Error Handling Stub for Host Testing
 */

#ifndef _ESP_ERR_H_
#define _ESP_ERR_H_

#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK          0
#define ESP_FAIL        -1
#define ESP_ERR_NO_MEM  0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105

#define ESP_ERROR_CHECK(x) do { (void)(x); } while(0)

#endif /* _ESP_ERR_H_ */
