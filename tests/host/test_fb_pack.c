#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "epd_panel.h"
#include "epd_fb_pack.h"

static uint8_t fb[EPD_FB_SIZE];

static void test_frame_size(void)
{
    assert(EPD_WIDTH == 400);
    assert(EPD_HEIGHT == 300);
    assert(EPD_FB_STRIDE == 100);
    assert(EPD_FB_SIZE == 30000);
}

static void test_color_pattern(void)
{
    assert(epd_color_pattern(EPD_COLOR_BLACK) == 0x00);
    assert(epd_color_pattern(EPD_COLOR_WHITE) == 0x55);
    assert(epd_color_pattern(EPD_COLOR_YELLOW) == 0xAA);
    assert(epd_color_pattern(EPD_COLOR_RED) == 0xFF);
}

static void test_leftmost_pixel_is_msb(void)
{
    memset(fb, 0, sizeof fb);
    epd_pack_set(fb, 0, 0, EPD_COLOR_RED);
    assert(fb[0] == 0xC0);
    epd_pack_set(fb, 1, 0, EPD_COLOR_WHITE);
    assert(fb[0] == 0xD0);
    epd_pack_set(fb, 3, 0, EPD_COLOR_YELLOW);
    assert(fb[0] == 0xD2);
}

static void test_byte_order_black_white_yellow_red(void)
{
    memset(fb, 0, sizeof fb);
    epd_pack_set(fb, 0, 0, EPD_COLOR_BLACK);
    epd_pack_set(fb, 1, 0, EPD_COLOR_WHITE);
    epd_pack_set(fb, 2, 0, EPD_COLOR_YELLOW);
    epd_pack_set(fb, 3, 0, EPD_COLOR_RED);
    assert(fb[0] == 0x1B); /* 00 01 10 11 */
}

static void test_row_stride(void)
{
    memset(fb, 0, sizeof fb);
    epd_pack_set(fb, 0, 1, EPD_COLOR_RED);
    assert(fb[EPD_FB_STRIDE] == 0xC0);
    assert(fb[EPD_FB_STRIDE - 1] == 0x00);
}

static void test_fill_and_get(void)
{
    epd_pack_fill(fb, EPD_COLOR_YELLOW);
    assert(fb[0] == 0xAA);
    assert(fb[EPD_FB_SIZE - 1] == 0xAA);
    for (int y = 0; y < EPD_HEIGHT; y += 37) {
        for (int x = 0; x < EPD_WIDTH; x += 29) {
            assert(epd_pack_get(fb, x, y) == EPD_COLOR_YELLOW);
        }
    }
    epd_pack_set(fb, 399, 299, EPD_COLOR_BLACK);
    assert(epd_pack_get(fb, 399, 299) == EPD_COLOR_BLACK);
    assert(fb[EPD_FB_SIZE - 1] == 0xA8); /* 0xAA & ~0x03 */
}

static void test_out_of_range_is_ignored(void)
{
    epd_pack_fill(fb, EPD_COLOR_WHITE);
    epd_pack_set(fb, -1, 0, EPD_COLOR_BLACK);
    epd_pack_set(fb, 0, -1, EPD_COLOR_BLACK);
    epd_pack_set(fb, EPD_WIDTH, 0, EPD_COLOR_BLACK);
    epd_pack_set(fb, 0, EPD_HEIGHT, EPD_COLOR_BLACK);
    assert(fb[0] == 0x55);
    assert(fb[EPD_FB_SIZE - 1] == 0x55);
}

int main(void)
{
    test_frame_size();
    test_color_pattern();
    test_leftmost_pixel_is_msb();
    test_byte_order_black_white_yellow_red();
    test_row_stride();
    test_fill_and_get();
    test_out_of_range_is_ignored();
    printf("test_fb_pack: all tests passed\n");
    return 0;
}
