idf_component_register(
    SRCS "src/rom_loader.c"
    INCLUDE_DIRS "include"
    REQUIRES fatfs sdmmc driver vfs
)
