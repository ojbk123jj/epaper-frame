#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    AI_IDLE, AI_QUEUED, AI_CONNECTING, AI_GENERATING, AI_DOWNLOADING,
    AI_CONVERTING, AI_DISPLAYING, AI_SUCCEEDED, AI_FAILED,
} ai_stage_t;

typedef struct {
    bool configured;
    bool busy;
    ai_stage_t stage;
    esp_err_t last_error;
    uint32_t displayed_count;
} ai_status_t;

esp_err_t ai_service_start(void);
esp_err_t ai_service_submit(const char *prompt);
void ai_service_status(ai_status_t *status);
const char *ai_stage_name(ai_stage_t stage);
