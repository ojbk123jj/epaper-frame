#include "epd_patterns.h"

#include <string.h>

#include "assets/gimage_1.h"
#include "epd_fb_pack.h"
#include "epd_text.h"

static const char *const k_names[EPD_PATTERN_COUNT] = {
    "solid", "colors", "checker", "bars", "grid", "photo",
};

const char *epd_pattern_name(epd_pattern_id_t id)
{
    if ((unsigned)id >= (unsigned)EPD_PATTERN_COUNT) {
        return "unknown";
    }
    return k_names[id];
}

bool epd_pattern_lookup(const char *name, epd_pattern_id_t *out)
{
    for (int i = 0; i < EPD_PATTERN_COUNT; i++) {
        if (strcmp(name, k_names[i]) == 0) {
            *out = (epd_pattern_id_t)i;
            return true;
        }
    }
    return false;
}

static void fill_rect(uint8_t *fb, int x0, int y0, int w, int h, epd_color_t c)
{
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            epd_pack_set(fb, x, y, c);
        }
    }
}

static void draw_colors(uint8_t *fb)
{
    fill_rect(fb, 0, 0, EPD_WIDTH / 2, EPD_HEIGHT / 2, EPD_COLOR_BLACK);
    fill_rect(fb, EPD_WIDTH / 2, 0, EPD_WIDTH / 2, EPD_HEIGHT / 2, EPD_COLOR_WHITE);
    fill_rect(fb, 0, EPD_HEIGHT / 2, EPD_WIDTH / 2, EPD_HEIGHT / 2, EPD_COLOR_YELLOW);
    fill_rect(fb, EPD_WIDTH / 2, EPD_HEIGHT / 2, EPD_WIDTH / 2, EPD_HEIGHT / 2, EPD_COLOR_RED);

    /* 中心十字，用于确认坐标原点与方向 */
    for (int x = 0; x < EPD_WIDTH; x++) {
        epd_pack_set(fb, x, EPD_HEIGHT / 2, EPD_COLOR_BLACK);
    }
    for (int y = 0; y < EPD_HEIGHT; y++) {
        epd_pack_set(fb, EPD_WIDTH / 2, y, EPD_COLOR_BLACK);
    }

    epd_text_draw(fb, 20, 20, "BLACK", EPD_COLOR_RED, EPD_COLOR_BLACK, 3);
    epd_text_draw(fb, 240, 20, "WHITE", EPD_COLOR_BLACK, EPD_COLOR_WHITE, 3);
    epd_text_draw(fb, 20, 260, "YELLOW", EPD_COLOR_BLACK, EPD_COLOR_YELLOW, 3);
    epd_text_draw(fb, 250, 260, "RED", EPD_COLOR_WHITE, EPD_COLOR_RED, 3);
}

static void draw_checker(uint8_t *fb)
{
    static const int k_cell[3] = {1, 2, 4};
    for (int band = 0; band < 3; band++) {
        int cell = k_cell[band];
        for (int y = band * 100; y < (band + 1) * 100; y++) {
            for (int x = 0; x < EPD_WIDTH; x++) {
                bool odd = (((x / cell) + (y / cell)) & 0x1) != 0;
                epd_pack_set(fb, x, y, odd ? EPD_COLOR_BLACK : EPD_COLOR_WHITE);
            }
        }
    }
    for (int x = 0; x < EPD_WIDTH; x++) {
        epd_pack_set(fb, x, 0, EPD_COLOR_RED);
        epd_pack_set(fb, x, EPD_HEIGHT - 1, EPD_COLOR_RED);
    }
}

static void draw_bars(uint8_t *fb)
{
    static const int k_width[4] = {1, 2, 4, 8};
    static const epd_color_t k_colors[4] = {
        EPD_COLOR_BLACK, EPD_COLOR_WHITE, EPD_COLOR_YELLOW, EPD_COLOR_RED,
    };
    for (int group = 0; group < 4; group++) {
        int w = k_width[group];
        int base = group * 100;
        for (int x = base; x < base + 100; x++) {
            epd_color_t c = k_colors[((x - base) / w) % 4];
            for (int y = 0; y < EPD_HEIGHT; y++) {
                epd_pack_set(fb, x, y, c);
            }
        }
    }
    for (int x = 0; x < EPD_WIDTH; x++) {
        epd_pack_set(fb, x, 0, EPD_COLOR_RED);
        epd_pack_set(fb, x, EPD_HEIGHT - 1, EPD_COLOR_RED);
    }
}

static void draw_grid(uint8_t *fb)
{
    epd_pack_fill(fb, EPD_COLOR_WHITE);
    for (int x = 0; x < EPD_WIDTH; x += 20) {
        for (int y = 0; y < EPD_HEIGHT; y++) {
            epd_pack_set(fb, x, y, EPD_COLOR_BLACK);
        }
    }
    for (int y = 0; y < EPD_HEIGHT; y += 20) {
        for (int x = 0; x < EPD_WIDTH; x++) {
            epd_pack_set(fb, x, y, EPD_COLOR_BLACK);
        }
    }
    /* 10px 红色外框 */
    for (int i = 0; i < 10; i++) {
        for (int x = 0; x < EPD_WIDTH; x++) {
            epd_pack_set(fb, x, i, EPD_COLOR_RED);
            epd_pack_set(fb, x, EPD_HEIGHT - 1 - i, EPD_COLOR_RED);
        }
        for (int y = 0; y < EPD_HEIGHT; y++) {
            epd_pack_set(fb, i, y, EPD_COLOR_RED);
            epd_pack_set(fb, EPD_WIDTH - 1 - i, y, EPD_COLOR_RED);
        }
    }
    /* 四角标记：右上为空心，便于判断方向 */
    fill_rect(fb, 20, 20, 20, 20, EPD_COLOR_BLACK);
    fill_rect(fb, EPD_WIDTH - 40, 20, 20, 20, EPD_COLOR_BLACK);
    fill_rect(fb, EPD_WIDTH - 34, 26, 8, 8, EPD_COLOR_WHITE);
    epd_text_draw(fb, 60, 44, "400x300 BWRY", EPD_COLOR_BLACK, EPD_COLOR_WHITE, 2);
}

void epd_pattern_render(uint8_t *fb, epd_pattern_id_t id, epd_color_t solid)
{
    switch (id) {
    case EPD_PATTERN_SOLID:
        epd_pack_fill(fb, solid);
        break;
    case EPD_PATTERN_COLORS:
        draw_colors(fb);
        break;
    case EPD_PATTERN_CHECKER:
        draw_checker(fb);
        break;
    case EPD_PATTERN_BARS:
        draw_bars(fb);
        break;
    case EPD_PATTERN_GRID:
        draw_grid(fb);
        break;
    case EPD_PATTERN_PHOTO:
        memcpy(fb, gimage_1, EPD_FB_SIZE);
        break;
    default:
        epd_pack_fill(fb, EPD_COLOR_WHITE);
        break;
    }
}
