#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Network and a valid system clock must be prepared by the caller.
 * Submit once and poll until success, failure or the configured deadline.
 * On success, caller owns *image_url and must free it. On failure it is NULL. */
esp_err_t modelscope_generate_image(const char *prompt, char **image_url);

/* Download the generated image without API credentials. Caller frees *image.
 * On failure, outputs are NULL and zero. */
esp_err_t modelscope_download_image(const char *image_url, uint8_t **image, size_t *length);
