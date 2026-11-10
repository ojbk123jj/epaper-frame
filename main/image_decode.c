#include "image_decode.h"

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define STBI_MALLOC(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_REALLOC(p,n) heap_caps_realloc((p), (n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_FREE(p) free(p)
#endif
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 2048
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

esp_err_t image_decode_rgb(const uint8_t *data, size_t length,
                           uint8_t **rgb, int *width, int *height)
{
    if (!rgb || !width || !height) return ESP_ERR_INVALID_ARG;
    *rgb = NULL; *width = *height = 0;
    if (!data || length < 3 || length > 2U * 1024U * 1024U || length > INT_MAX) return ESP_ERR_INVALID_SIZE;
    bool png = length >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0;
    bool jpeg = data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff;
    if (!png && !jpeg) return ESP_ERR_NOT_SUPPORTED;
    int w, h, channels;
    if (!stbi_info_from_memory(data, (int)length, &w, &h, &channels)) return ESP_ERR_INVALID_RESPONSE;
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048 || (int64_t)w * h > 1024 * 1024) return ESP_ERR_INVALID_SIZE;
    bool alpha = channels == 2 || channels == 4;
    uint8_t *pixels = stbi_load_from_memory(data, (int)length, &w, &h, &channels, alpha ? 4 : 3);
    if (!pixels) {
        const char *reason = stbi_failure_reason();
        return reason && !strcmp(reason, "outofmem") ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_RESPONSE;
    }
    if (alpha) {
        /* Composite transparency onto the panel's white background in-place. */
        for (size_t i = 0; i < (size_t)w * h; ++i) {
            unsigned a = pixels[i * 4 + 3];
            for (int c = 0; c < 3; ++c) {
                pixels[i * 3 + c] = (uint8_t)((pixels[i * 4 + c] * a + 255 * (255 - a) + 127) / 255);
            }
        }
    }
    *rgb = pixels; *width = w; *height = h;
    return ESP_OK;
}
