#include "image_convert.h"

#include <stdlib.h>
#include <string.h>
#include "epd_fb_pack.h"

static const int palette[4][3] = {{0, 0, 0}, {255, 255, 255}, {255, 255, 0}, {255, 0, 0}};

esp_err_t image_rgb_to_frame(const uint8_t *rgb, size_t length, int width, int height,
                             bool dither, uint8_t *frame, size_t frame_length)
{
    if (!rgb || !frame || width <= 0 || height <= 0 || frame_length != EPD_FB_SIZE ||
        (size_t)width > SIZE_MAX / (size_t)height / 3 ||
        length < (size_t)width * (size_t)height * 3) return ESP_ERR_INVALID_ARG;

    int crop_w = width, crop_h = height;
    if ((int64_t)width * EPD_HEIGHT > (int64_t)height * EPD_WIDTH) {
        crop_w = (int)((int64_t)height * EPD_WIDTH / EPD_HEIGHT);
    } else {
        crop_h = (int)((int64_t)width * EPD_HEIGHT / EPD_WIDTH);
    }
    if (crop_w < 1) crop_w = 1;
    if (crop_h < 1) crop_h = 1;
    int offset_x = (width - crop_w) / 2, offset_y = (height - crop_h) / 2;
    const size_t row_size = (EPD_WIDTH + 2) * 3;
    int32_t *errors = dither ? calloc(row_size * 2, sizeof(int32_t)) : NULL;
    if (dither && !errors) return ESP_ERR_NO_MEM;
    int32_t *current = errors, *next = errors ? errors + row_size : NULL;
    memset(frame, 0, frame_length);
    for (int y = 0; y < EPD_HEIGHT; ++y) {
        int sy = offset_y + (int)(((int64_t)y * 2 + 1) * crop_h / (EPD_HEIGHT * 2));
        for (int x = 0; x < EPD_WIDTH; ++x) {
            int sx = offset_x + (int)(((int64_t)x * 2 + 1) * crop_w / (EPD_WIDTH * 2));
            const uint8_t *pixel = rgb + ((size_t)sy * width + sx) * 3;
            int scaled[3], sample[3];
            for (int c = 0; c < 3; ++c) {
                int v = pixel[c] * 16 + (dither ? current[(x + 1) * 3 + c] : 0);
                scaled[c] = v < 0 ? 0 : v > 4080 ? 4080 : v;
                sample[c] = (scaled[c] + 8) / 16;
            }
            int best = 0, best_distance = INT32_MAX;
            for (int i = 0; i < 4; ++i) {
                int r = sample[0] - palette[i][0], g = sample[1] - palette[i][1];
                int b = sample[2] - palette[i][2];
                int distance = 3 * r * r + 6 * g * g + b * b;
                if (distance < best_distance) { best = i; best_distance = distance; }
            }
            epd_pack_set(frame, x, y, (epd_color_t)best);
            if (dither) for (int c = 0; c < 3; ++c) {
                int e = scaled[c] - palette[best][c] * 16;
                current[(x + 2) * 3 + c] += e * 7 / 16;
                next[x * 3 + c] += e * 3 / 16;
                next[(x + 1) * 3 + c] += e * 5 / 16;
                next[(x + 2) * 3 + c] += e / 16;
            }
        }
        if (dither) {
            int32_t *tmp = current; current = next; next = tmp;
            memset(next, 0, row_size * sizeof(int32_t));
        }
    }
    free(errors);
    return ESP_OK;
}
