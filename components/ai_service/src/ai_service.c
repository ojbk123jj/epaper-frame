#include "ai_service.h"
#include "frame_service.h"
#include <string.h>

esp_err_t ai_service_start(void) { return frame_service_start(); }
esp_err_t ai_service_submit(const char *prompt) { return frame_service_submit_ai(prompt); }
const char *ai_stage_name(ai_stage_t stage) { return frame_stage_name((frame_stage_t)stage); }
void ai_service_status(ai_status_t *status)
{
    if (!status) return;
    frame_status_t frame;
    frame_service_status(&frame);
    memset(status, 0, sizeof(*status));
    status->configured = frame.configured;
    status->busy = frame.busy;
    status->stage = frame.stage == FRAME_RECEIVING ? AI_QUEUED : (ai_stage_t)frame.stage;
    status->last_error = frame.last_error;
    status->displayed_count = frame.displayed_count;
}
