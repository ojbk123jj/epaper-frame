#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Decode bounded PNG/JPEG input into heap-owned RGB888. Caller frees *rgb. */
esp_err_t image_decode_rgb(const uint8_t *data, size_t length,
                           uint8_t **rgb, int *width, int *height);
