#pragma once

#include <stdbool.h>

#include "epd_panel.h"

/* 8x8 字模，5 像素宽 + 1 像素字距；scale >= 1 */
void epd_text_draw(uint8_t *fb, int x, int y, const char *s,
                   epd_color_t fg, epd_color_t bg, int scale);

int epd_text_width(const char *s, int scale);
