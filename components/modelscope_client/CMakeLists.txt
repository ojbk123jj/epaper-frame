idf_component_register(
    SRCS "src/modelscope_client.c" "src/ai_http.c" "src/modelscope_protocol.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES esp_common
    PRIV_REQUIRES app_config esp_http_client mbedtls esp_timer heap json freertos log
)
