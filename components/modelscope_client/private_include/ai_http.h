#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t ai_http_api(const char *url, const char *json, const char *task_type,
                      uint32_t timeout_ms, char **response, size_t *length);
esp_err_t ai_http_download(const char *url, uint8_t **image, size_t *length);
