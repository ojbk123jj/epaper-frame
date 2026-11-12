idf_component_register(SRCS "src/ble_image.c" "src/radio_lifecycle.c" "src/ble_policy.c"
    INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "private_include"
    REQUIRES esp_common
    PRIV_REQUIRES app_config bt frame_service frame_transfer freertos nvs_flash esp_timer heap log)
