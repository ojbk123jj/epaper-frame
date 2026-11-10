#pragma once

#include <stddef.h>
#include "esp_err.h"

#define MODELSCOPE_TASK_ID_SIZE 129
#define MODELSCOPE_MAX_URL 4096

typedef enum {
    MODELSCOPE_PENDING,
    MODELSCOPE_SUCCEEDED,
    MODELSCOPE_FAILED,
} modelscope_task_state_t;

/* Returned JSON and successful image_url are heap-owned by the caller. */
char *modelscope_request_json(const char *model, const char *prompt, const char *size);
esp_err_t modelscope_parse_submit(const char *body, size_t length,
                                 char task_id[MODELSCOPE_TASK_ID_SIZE]);
esp_err_t modelscope_parse_task(const char *body, size_t length,
                               modelscope_task_state_t *state, char **image_url);
