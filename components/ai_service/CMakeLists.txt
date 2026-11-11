idf_component_register(
    SRCS "src/ai_service.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES frame_service
)
