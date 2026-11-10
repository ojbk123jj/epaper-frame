idf_component_register(
    SRCS "src/ai_console.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES ai_service app_config console
)
