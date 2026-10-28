idf_component_register(
    SRCS "main.c" "epd_worker.c" "epd_console.c" "epd_patterns.c" "epd_text.c"
    INCLUDE_DIRS "." "assets"
    REQUIRES epd_gdem042f86 console
)
