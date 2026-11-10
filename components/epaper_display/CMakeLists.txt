idf_component_register(
    SRCS "src/epaper_display.c"
    INCLUDE_DIRS "include"
    REQUIRES epd_gdem042f86
    PRIV_REQUIRES freertos esp_common log
)
