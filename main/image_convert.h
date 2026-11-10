#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Center-crop RGB888 to 4:3, resize to panel size, quantize and optionally dither. */
esp_err_t image_rgb_to_frame(const uint8_t *rgb, size_t length, int width, int height,
                             bool dither, uint8_t *frame, size_t frame_length);
