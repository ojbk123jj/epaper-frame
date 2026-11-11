idf_component_register(SRCS "src/frame_service.c" INCLUDE_DIRS "include"
    REQUIRES esp_common epaper_display
    PRIV_REQUIRES app_config image_processing network_service modelscope_client freertos heap log)
