idf_component_register(
    SRCS "src/network_service.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES app_config esp_wifi esp_event esp_netif nvs_flash freertos log
)
