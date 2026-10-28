#pragma once

#include <stddef.h>
#include <stdint.h>

#include "epd_panel.h"

/* 单色时整字节的值：0x00 / 0x55 / 0xAA / 0xFF */
static inline uint8_t epd_color_pattern(epd_color_t c)
{
    return (uint8_t)(((unsigned)c & 0x3u) * 0x55u);
}

/* 最左像素占 bit7..6 */
static inline void epd_pack_set(uint8_t *fb, int x, int y, epd_color_t c)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
        return;
    }
    size_t idx = (size_t)y * EPD_FB_STRIDE + (size_t)(x >> 2);
    unsigned shift = 6u - 2u * (unsigned)(x & 3);
    fb[idx] = (uint8_t)((fb[idx] & ~(0x3u << shift)) | (((unsigned)c & 0x3u) << shift));
}

static inline epd_color_t epd_pack_get(const uint8_t *fb, int x, int y)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
        return EPD_COLOR_WHITE;
    }
    size_t idx = (size_t)y * EPD_FB_STRIDE + (size_t)(x >> 2);
    unsigned shift = 6u - 2u * (unsigned)(x & 3);
    return (epd_color_t)((fb[idx] >> shift) & 0x3u);
}

static inline void epd_pack_fill(uint8_t *fb, epd_color_t c)
{
    uint8_t v = epd_color_pattern(c);
    for (size_t i = 0; i < EPD_FB_SIZE; i++) {
        fb[i] = v;
    }
}
