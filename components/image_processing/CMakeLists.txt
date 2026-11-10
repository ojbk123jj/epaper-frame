idf_component_register(
    SRCS "src/image_decode.c" "src/image_convert.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../third_party/stb"
    REQUIRES esp_common
    PRIV_REQUIRES epd_gdem042f86 heap
)
