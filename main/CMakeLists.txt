idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES gba_core display input audio rom_loader
             esp_psram esp_lcd driver nvs_flash fatfs sdmmc
)
