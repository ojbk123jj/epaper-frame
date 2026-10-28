idf_component_register(
    SRCS "src/epd_gdem042f86.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_spi esp_driver_gpio esp_common esp_timer heap
    PRIV_REQUIRES freertos
)
