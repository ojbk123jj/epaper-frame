#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "epaper_display.h"

typedef enum {
    FRAME_IDLE, FRAME_QUEUED, FRAME_CONNECTING, FRAME_GENERATING, FRAME_DOWNLOADING,
    FRAME_CONVERTING, FRAME_DISPLAYING, FRAME_SUCCEEDED, FRAME_FAILED, FRAME_RECEIVING
} frame_stage_t;
typedef enum { FRAME_SOURCE_NONE, FRAME_SOURCE_AI, FRAME_SOURCE_BLE } frame_source_t;
typedef struct {
    bool configured, busy;
    frame_stage_t stage;
    frame_source_t source;
    uint32_t job_id, displayed_count;
    esp_err_t last_error;
} frame_status_t;
typedef esp_err_t (*frame_radio_guard_t)(bool quiet);
esp_err_t frame_service_start(void);
esp_err_t frame_service_submit_ai(const char *prompt);
/* Reserve before receiving any frame bytes. Commit copies; caller retains buffer. */
esp_err_t frame_service_reserve(uint32_t job_id);
esp_err_t frame_service_commit(uint32_t job_id, const uint8_t *frame, size_t length, epd_mode_t mode);
esp_err_t frame_service_cancel(uint32_t job_id, esp_err_t reason);
void frame_service_status(frame_status_t *status);
void frame_service_set_radio_guard(frame_radio_guard_t guard);
const char *frame_stage_name(frame_stage_t stage);
