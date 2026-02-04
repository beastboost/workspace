/**
 * ROM Loader for ESP32-P4 GBA Emulator
 *
 * Handles SD card mounting and ROM file loading
 */

#ifndef ROM_LOADER_H
#define ROM_LOADER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Maximum ROMs to list
#define ROM_LIST_MAX 100
#define ROM_NAME_MAX 64
#define ROM_PATH_MAX 256

// ROM entry structure
typedef struct {
    char name[ROM_NAME_MAX];
    char path[ROM_PATH_MAX];
    size_t size;
} rom_entry_t;

// ROM list structure
typedef struct {
    rom_entry_t entries[ROM_LIST_MAX];
    int count;
} rom_list_t;

/**
 * Initialize the ROM loader (mount SD card)
 *
 * @return ESP_OK on success
 */
esp_err_t rom_loader_init(void);

/**
 * Deinitialize the ROM loader (unmount SD card)
 */
void rom_loader_deinit(void);

/**
 * Check if SD card is mounted
 *
 * @return true if mounted
 */
bool rom_loader_is_mounted(void);

/**
 * List ROM files in a directory
 *
 * @param path Directory path (e.g., "/sdcard/gba")
 * @param list Output list structure
 * @return ESP_OK on success
 */
esp_err_t rom_loader_list(const char *path, rom_list_t *list);

/**
 * Free ROM list resources
 *
 * @param list List to free
 */
void rom_loader_free_list(rom_list_t *list);

/**
 * Load a ROM file into memory
 *
 * @param path Path to ROM file
 * @param data Output pointer to allocated ROM data
 * @param size Output size of ROM data
 * @return ESP_OK on success
 */
esp_err_t rom_loader_load(const char *path, uint8_t **data, size_t *size);

/**
 * Load a save file (SRAM/Flash)
 *
 * @param rom_path Path to ROM file (save file name derived from this)
 * @param data Output pointer to save data
 * @param size Output size of save data
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no save exists
 */
esp_err_t rom_loader_load_save(const char *rom_path, uint8_t **data, size_t *size);

/**
 * Save a save file (SRAM/Flash)
 *
 * @param rom_path Path to ROM file (save file name derived from this)
 * @param data Save data to write
 * @param size Size of save data
 * @return ESP_OK on success
 */
esp_err_t rom_loader_save_save(const char *rom_path, const uint8_t *data, size_t size);

/**
 * Get free space on SD card
 *
 * @param free_bytes Output free bytes
 * @param total_bytes Output total bytes
 * @return ESP_OK on success
 */
esp_err_t rom_loader_get_space(uint64_t *free_bytes, uint64_t *total_bytes);

#ifdef __cplusplus
}
#endif

#endif // ROM_LOADER_H
