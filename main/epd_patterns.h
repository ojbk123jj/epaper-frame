#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "epd_panel.h"

typedef enum {
    EPD_PATTERN_SOLID = 0,
    EPD_PATTERN_COLORS,
    EPD_PATTERN_CHECKER,
    EPD_PATTERN_BARS,
    EPD_PATTERN_GRID,
    EPD_PATTERN_PHOTO,
    EPD_PATTERN_COUNT
} epd_pattern_id_t;

const char *epd_pattern_name(epd_pattern_id_t id);
bool epd_pattern_lookup(const char *name, epd_pattern_id_t *out);

/* solid 参数仅在 id == EPD_PATTERN_SOLID 时生效 */
void epd_pattern_render(uint8_t *fb, epd_pattern_id_t id, epd_color_t solid);
