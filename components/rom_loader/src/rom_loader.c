/**
 * ROM Loader Implementation
 *
 * Uses SDMMC driver for SD card access
 */

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "rom_loader.h"

static const char *TAG = "ROM_LOADER";

// SD card mount point
#define MOUNT_POINT "/sdcard"

// SD card pin configuration - adjust for your hardware
// ESP32-P4 typically uses SDMMC interface
#define SDMMC_CLK_PIN   GPIO_NUM_43
#define SDMMC_CMD_PIN   GPIO_NUM_44
#define SDMMC_D0_PIN    GPIO_NUM_39
#define SDMMC_D1_PIN    GPIO_NUM_40
#define SDMMC_D2_PIN    GPIO_NUM_41
#define SDMMC_D3_PIN    GPIO_NUM_42

// State
static sdmmc_card_t *card = NULL;
static bool mounted = false;

esp_err_t rom_loader_init(void)
{
    if (mounted) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SD card");

    // Configure mount options
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = true,
    };

    // Configure SDMMC host
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    // Configure slot
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;  // 4-bit mode for faster transfers

    // Use custom GPIO pins if needed
    #ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    slot_config.clk = SDMMC_CLK_PIN;
    slot_config.cmd = SDMMC_CMD_PIN;
    slot_config.d0 = SDMMC_D0_PIN;
    slot_config.d1 = SDMMC_D1_PIN;
    slot_config.d2 = SDMMC_D2_PIN;
    slot_config.d3 = SDMMC_D3_PIN;
    #endif

    // Mount filesystem
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    mounted = true;

    // Print card info
    ESP_LOGI(TAG, "SD card mounted successfully");
    sdmmc_card_print_info(stdout, card);

    // Create GBA directory if it doesn't exist
    struct stat st;
    if (stat(MOUNT_POINT "/gba", &st) != 0) {
        mkdir(MOUNT_POINT "/gba", 0775);
        ESP_LOGI(TAG, "Created /gba directory");
    }

    return ESP_OK;
}

void rom_loader_deinit(void)
{
    if (!mounted) return;

    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    card = NULL;
    mounted = false;

    ESP_LOGI(TAG, "SD card unmounted");
}

bool rom_loader_is_mounted(void)
{
    return mounted;
}

// Check if file has GBA extension
static bool is_gba_file(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (ext == NULL) return false;

    return (strcasecmp(ext, ".gba") == 0 ||
            strcasecmp(ext, ".agb") == 0 ||
            strcasecmp(ext, ".bin") == 0);
}

esp_err_t rom_loader_list(const char *path, rom_list_t *list)
{
    if (!mounted || list == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(list, 0, sizeof(rom_list_t));

    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && list->count < ROM_LIST_MAX) {
        // Skip hidden files and directories
        if (entry->d_name[0] == '.') continue;

        // Skip non-GBA files
        if (!is_gba_file(entry->d_name)) continue;

        // Build full path
        char full_path[ROM_PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        // Get file size
        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        // Skip directories
        if (S_ISDIR(st.st_mode)) continue;

        // Add to list
        rom_entry_t *rom = &list->entries[list->count];
        strncpy(rom->name, entry->d_name, ROM_NAME_MAX - 1);
        strncpy(rom->path, full_path, ROM_PATH_MAX - 1);
        rom->size = st.st_size;

        list->count++;
    }

    closedir(dir);

    ESP_LOGI(TAG, "Found %d ROM(s) in %s", list->count, path);
    return ESP_OK;
}

void rom_loader_free_list(rom_list_t *list)
{
    // Nothing to free for now since we use static arrays
    if (list) {
        list->count = 0;
    }
}

esp_err_t rom_loader_load(const char *path, uint8_t **data, size_t *size)
{
    if (!mounted || path == NULL || data == NULL || size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Loading ROM: %s", path);

    // Get file size
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    *size = st.st_size;
    ESP_LOGI(TAG, "ROM size: %zu bytes", *size);

    // Allocate memory (prefer PSRAM for large files)
    *data = heap_caps_malloc(*size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (*data == NULL) {
        // Try internal memory
        *data = heap_caps_malloc(*size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (*data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes for ROM", *size);
        return ESP_ERR_NO_MEM;
    }

    // Open and read file
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        free(*data);
        *data = NULL;
        return ESP_FAIL;
    }

    size_t read_size = fread(*data, 1, *size, f);
    fclose(f);

    if (read_size != *size) {
        ESP_LOGE(TAG, "Failed to read complete file (read %zu of %zu bytes)",
                 read_size, *size);
        free(*data);
        *data = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ROM loaded successfully");
    return ESP_OK;
}

// Get save file path from ROM path
static void get_save_path(const char *rom_path, char *save_path, size_t save_path_size)
{
    strncpy(save_path, rom_path, save_path_size - 1);

    // Replace extension with .sav
    char *ext = strrchr(save_path, '.');
    if (ext != NULL) {
        strcpy(ext, ".sav");
    } else {
        strncat(save_path, ".sav", save_path_size - strlen(save_path) - 1);
    }
}

esp_err_t rom_loader_load_save(const char *rom_path, uint8_t **data, size_t *size)
{
    if (!mounted || rom_path == NULL || data == NULL || size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char save_path[ROM_PATH_MAX];
    get_save_path(rom_path, save_path, sizeof(save_path));

    struct stat st;
    if (stat(save_path, &st) != 0) {
        return ESP_ERR_NOT_FOUND;
    }

    *size = st.st_size;
    *data = heap_caps_malloc(*size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (*data == NULL) {
        *data = malloc(*size);
    }
    if (*data == NULL) {
        return ESP_ERR_NO_MEM;
    }

    FILE *f = fopen(save_path, "rb");
    if (f == NULL) {
        free(*data);
        *data = NULL;
        return ESP_FAIL;
    }

    size_t read_size = fread(*data, 1, *size, f);
    fclose(f);

    if (read_size != *size) {
        free(*data);
        *data = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Loaded save file: %s (%zu bytes)", save_path, *size);
    return ESP_OK;
}

esp_err_t rom_loader_save_save(const char *rom_path, const uint8_t *data, size_t size)
{
    if (!mounted || rom_path == NULL || data == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char save_path[ROM_PATH_MAX];
    get_save_path(rom_path, save_path, sizeof(save_path));

    FILE *f = fopen(save_path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to create save file: %s", save_path);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) {
        ESP_LOGE(TAG, "Failed to write complete save file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved to: %s (%zu bytes)", save_path, size);
    return ESP_OK;
}

esp_err_t rom_loader_get_space(uint64_t *free_bytes, uint64_t *total_bytes)
{
    if (!mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    FATFS *fs;
    DWORD free_clusters;
    FRESULT res = f_getfree("0:", &free_clusters, &fs);

    if (res != FR_OK) {
        return ESP_FAIL;
    }

    uint64_t sector_size = fs->csize * 512;
    if (free_bytes) {
        *free_bytes = (uint64_t)free_clusters * sector_size;
    }
    if (total_bytes) {
        *total_bytes = (uint64_t)(fs->n_fatent - 2) * sector_size;
    }

    return ESP_OK;
}
