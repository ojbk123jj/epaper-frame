idf_component_register(
    SRCS "src/ai_service.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES app_config epaper_display image_processing network_service
                  modelscope_client freertos heap log
)
