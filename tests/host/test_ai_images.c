#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image_convert.h"
#include "image_decode.h"
#include "epd_fb_pack.h"

static uint8_t frame[EPD_FB_SIZE];

static uint8_t *read_fixture(const char *name, size_t *length)
{
    char path[256];
    snprintf(path, sizeof(path), "tests/host/fixtures/%s", name);
    FILE *file = fopen(path, "rb"); assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file); assert(size > 0);
    rewind(file);
    uint8_t *bytes = malloc((size_t)size); assert(bytes);
    assert(fread(bytes, 1, (size_t)size, file) == (size_t)size);
    fclose(file); *length = (size_t)size;
    return bytes;
}

static void test_decode(void)
{
    size_t length;
    uint8_t *bytes = read_fixture("palette.png", &length), *rgb;
    int width, height;
    assert(image_decode_rgb(bytes, length, &rgb, &width, &height) == ESP_OK);
    assert(width == 4 && height == 4);
    static const uint8_t colors[] = {0,0,0, 255,255,255, 255,255,0, 255,0,0};
    assert(memcmp(rgb, colors, sizeof(colors)) == 0);
    free(rgb); free(bytes);
    const char *jpegs[] = {"white.jpg", "white-progressive.jpg"};
    for (int i = 0; i < 2; ++i) {
        bytes = read_fixture(jpegs[i], &length);
        assert(image_decode_rgb(bytes, length, &rgb, &width, &height) == ESP_OK);
        assert(width == 16 && height == 16);
        for (int p = 0; p < width * height * 3; ++p) assert(rgb[p] >= 237 && rgb[p] <= 243);
        free(rgb); free(bytes);
    }
    bytes = read_fixture("alpha.png", &length);
    assert(image_decode_rgb(bytes, length, &rgb, &width, &height) == ESP_OK);
    assert(width == 2 && height == 1);
    assert(rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255);
    assert(rgb[3] == 127 && rgb[4] == 127 && rgb[5] == 127);
    free(rgb); free(bytes);
    bytes = read_fixture("oversized.png", &length);
    assert(image_decode_rgb(bytes, length, &rgb, &width, &height) == ESP_ERR_INVALID_SIZE);
    assert(!rgb && width == 0 && height == 0);
    free(bytes);
    const uint8_t bad[] = {0xff, 0xd8, 0xff, 0, 1};
    assert(image_decode_rgb(bad, sizeof(bad), &rgb, &width, &height) == ESP_ERR_INVALID_RESPONSE);
    assert(!rgb);
    assert(image_decode_rgb((const uint8_t *)"not-an-image", 12, &rgb, &width, &height) == ESP_ERR_NOT_SUPPORTED);
}

static void test_convert(void)
{
    const uint8_t colors[4][3] = {{0,0,0}, {255,255,255}, {255,255,0}, {255,0,0}};
    for (int i = 0; i < 4; ++i) {
        assert(image_rgb_to_frame(colors[i], 3, 1, 1, true, frame, sizeof(frame)) == ESP_OK);
        for (size_t j = 0; j < sizeof(frame); ++j) assert(frame[j] == i * 0x55);
    }
    size_t size = 800U * 300U * 3U;
    uint8_t *rgb = malloc(size); assert(rgb);
    for (int y = 0; y < 300; ++y) for (int x = 0; x < 800; ++x) {
        const uint8_t *c = colors[x < 400 ? EPD_COLOR_BLACK : EPD_COLOR_RED];
        memcpy(rgb + (y * 800 + x) * 3, c, 3);
    }
    assert(image_rgb_to_frame(rgb, size, 800, 300, false, frame, sizeof(frame)) == ESP_OK);
    assert(epd_pack_get(frame, 0, 150) == EPD_COLOR_BLACK);
    assert(epd_pack_get(frame, 199, 150) == EPD_COLOR_BLACK);
    assert(epd_pack_get(frame, 200, 150) == EPD_COLOR_RED);
    assert(epd_pack_get(frame, 399, 150) == EPD_COLOR_RED);
    free(rgb);
    const uint8_t gray[] = {128,128,128};
    assert(image_rgb_to_frame(gray, 3, 1, 1, true, frame, sizeof(frame)) == ESP_OK);
    int black = 0, white = 0;
    for (int y = 0; y < EPD_HEIGHT; ++y) for (int x = 0; x < EPD_WIDTH; ++x) {
        epd_color_t c = epd_pack_get(frame, x, y);
        assert(c == EPD_COLOR_BLACK || c == EPD_COLOR_WHITE);
        black += c == EPD_COLOR_BLACK; white += c == EPD_COLOR_WHITE;
    }
    assert(black > 40000 && white > 40000);
    memset(frame, 0xa5, sizeof(frame));
    assert(image_rgb_to_frame(gray, 2, 1, 1, true, frame, sizeof(frame)) == ESP_ERR_INVALID_ARG);
    assert(frame[0] == 0xa5);
    assert(image_rgb_to_frame(gray, 3, 0, 1, true, frame, sizeof(frame)) == ESP_ERR_INVALID_ARG);
    assert(image_rgb_to_frame(gray, 3, 1, 1, true, frame, sizeof(frame)-1) == ESP_ERR_INVALID_ARG);
}

int main(void)
{
    test_decode(); test_convert();
    puts("AI image decode/conversion tests passed");
    return 0;
}
