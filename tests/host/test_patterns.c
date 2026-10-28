#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "epd_fb_pack.h"
#include "epd_patterns.h"
#include "epd_text.h"

static uint8_t fb[EPD_FB_SIZE];

static int count_color(epd_color_t c)
{
    int n = 0;
    for (int y = 0; y < EPD_HEIGHT; y++) {
        for (int x = 0; x < EPD_WIDTH; x++) {
            if (epd_pack_get(fb, x, y) == c) {
                n++;
            }
        }
    }
    return n;
}

static void test_lookup(void)
{
    epd_pattern_id_t id = EPD_PATTERN_COUNT;
    assert(epd_pattern_lookup("grid", &id) && id == EPD_PATTERN_GRID);
    assert(epd_pattern_lookup("photo", &id) && id == EPD_PATTERN_PHOTO);
    assert(!epd_pattern_lookup("nope", &id));
    assert(strcmp(epd_pattern_name(EPD_PATTERN_CHECKER), "checker") == 0);
}

static void test_solid(void)
{
    epd_pattern_render(fb, EPD_PATTERN_SOLID, EPD_COLOR_YELLOW);
    assert(count_color(EPD_COLOR_YELLOW) == EPD_WIDTH * EPD_HEIGHT);
}

static void test_colors_quadrants(void)
{
    epd_pattern_render(fb, EPD_PATTERN_COLORS, EPD_COLOR_WHITE);
    assert(epd_pack_get(fb, 10, 10) == EPD_COLOR_BLACK);
    assert(epd_pack_get(fb, 390, 10) == EPD_COLOR_WHITE);
    assert(epd_pack_get(fb, 10, 290) == EPD_COLOR_YELLOW);
    assert(epd_pack_get(fb, 390, 290) == EPD_COLOR_RED);
}

static void test_checker_groups(void)
{
    epd_pattern_render(fb, EPD_PATTERN_CHECKER, EPD_COLOR_WHITE);
    /* 1px 区：相邻像素必须交替（y=10 避开红色边框） */
    assert(epd_pack_get(fb, 0, 10) != epd_pack_get(fb, 1, 10));
    assert(epd_pack_get(fb, 1, 10) != epd_pack_get(fb, 2, 10));
    /* 上下边框为红色 */
    assert(epd_pack_get(fb, 0, 0) == EPD_COLOR_RED);
    assert(epd_pack_get(fb, 0, EPD_HEIGHT - 1) == EPD_COLOR_RED);
    /* 4px 区（y>=200）：同组 4 个像素相同，下一组相反 */
    epd_color_t a = epd_pack_get(fb, 0, 250);
    for (int x = 0; x < 4; x++) {
        assert(epd_pack_get(fb, x, 250) == a);
    }
    assert(epd_pack_get(fb, 4, 250) != a);
}

static void test_grid_border(void)
{
    epd_pattern_render(fb, EPD_PATTERN_GRID, EPD_COLOR_WHITE);
    assert(epd_pack_get(fb, 0, 150) == EPD_COLOR_RED); /* 左边框 */
    assert(epd_pack_get(fb, EPD_WIDTH - 1, 150) == EPD_COLOR_RED);
    assert(epd_pack_get(fb, 200, 0) == EPD_COLOR_RED); /* 上边框 */
    assert(epd_pack_get(fb, 200, EPD_HEIGHT - 1) == EPD_COLOR_RED);
    assert(epd_pack_get(fb, 50, 50) == EPD_COLOR_WHITE);  /* 网格交点留白 */
    assert(epd_pack_get(fb, 100, 50) == EPD_COLOR_BLACK); /* 竖网格线 */
    assert(epd_pack_get(fb, 50, 100) == EPD_COLOR_BLACK); /* 横网格线 */
}

static void test_bars_groups(void)
{
    epd_pattern_render(fb, EPD_PATTERN_BARS, EPD_COLOR_WHITE);
    /* 1px 组内相邻像素必须是不同颜色 */
    assert(epd_pack_get(fb, 0, 150) != epd_pack_get(fb, 1, 150));
    /* 8px 组内同一格 8 个像素同色（格边界对齐 300 + 8k） */
    epd_color_t c = epd_pack_get(fb, 316, 150);
    for (int x = 316; x < 324; x++) {
        assert(epd_pack_get(fb, x, 150) == c);
    }
    assert(epd_pack_get(fb, 324, 150) != c);
}

static void test_text_draw(void)
{
    epd_pack_fill(fb, EPD_COLOR_WHITE);
    epd_text_draw(fb, 10, 10, "AB", EPD_COLOR_BLACK, EPD_COLOR_WHITE, 3);
    int dark = 0;
    for (int y = 10; y < 34; y++) {
        for (int x = 10; x < 46; x++) {
            if (epd_pack_get(fb, x, y) == EPD_COLOR_BLACK) {
                dark++;
            }
        }
    }
    assert(dark > 10);
    assert(epd_text_width("AB", 3) == 36);
}

static void test_photo(void)
{
    epd_pattern_render(fb, EPD_PATTERN_PHOTO, EPD_COLOR_WHITE);
    /* 样图不是纯白，必须出现非白像素 */
    assert(count_color(EPD_COLOR_WHITE) < EPD_WIDTH * EPD_HEIGHT);
}

int main(void)
{
    test_lookup();
    test_solid();
    test_colors_quadrants();
    test_checker_groups();
    test_grid_border();
    test_bars_groups();
    test_text_draw();
    test_photo();
    printf("test_patterns: all tests passed\n");
    return 0;
}
