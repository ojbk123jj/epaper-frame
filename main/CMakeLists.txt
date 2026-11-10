if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../config/local_config.h")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/../config/local_config.example.h"
                   "${CMAKE_CURRENT_LIST_DIR}/../config/local_config.h" COPYONLY)
endif()

idf_component_register(
    SRCS "main.c" "epaper_display.c" "ai_service.c" "ai_console.c" "ai_network.c"
         "ai_http.c" "modelscope_protocol.c" "image_convert.c" "image_decode.c"
    INCLUDE_DIRS "." "../config" "../third_party/stb"
    REQUIRES epd_gdem042f86 freertos console esp_wifi esp_event esp_netif
             nvs_flash esp_http_client mbedtls esp_timer json
)
