#include "modelscope_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai_http.h"
#include "app_config.h"
#include "modelscope_protocol.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "modelscope";

esp_err_t modelscope_generate_image(const char *prompt, char **image_url)
{
    if (!image_url) return ESP_ERR_INVALID_ARG;
    *image_url = NULL;
    if (!prompt || !*prompt || strlen(prompt) > APP_AI_MAX_PROMPT) return ESP_ERR_INVALID_ARG;
    char *body = NULL;
    char *request = modelscope_request_json(APP_AI_MODEL, prompt, APP_AI_SIZE);
    if (!request) return ESP_ERR_NO_MEM;
    size_t length = 0;
    /* Do not retry submission: a failed response may still create a paid task. */
    esp_err_t err = ai_http_api("https://api-inference.modelscope.cn/v1/images/generations",
                               request, NULL, 30000, &body, &length);
    free(request);
    if (err != ESP_OK) goto done;
    char task_id[MODELSCOPE_TASK_ID_SIZE];
    err = modelscope_parse_submit(body, length, task_id);
    free(body); body = NULL;
    if (err != ESP_OK) goto done;
    ESP_LOGI(TAG, "task_id=%s", task_id);
    char task_url[256];
    snprintf(task_url, sizeof(task_url), "https://api-inference.modelscope.cn/v1/tasks/%s", task_id);
    const int64_t started_us = esp_timer_get_time();
    const int64_t deadline = started_us + (int64_t)APP_AI_TIMEOUT_MS * 1000;
    for (;;) {
        int64_t remaining = deadline - esp_timer_get_time();
        if (remaining <= 0) { err = ESP_ERR_TIMEOUT; break; }
        uint32_t wait_ms = remaining > 5000000 ? 5000 : (uint32_t)((remaining + 999) / 1000);
        vTaskDelay(pdMS_TO_TICKS(wait_ms) + 1);
        remaining = deadline - esp_timer_get_time();
        if (remaining <= 0) { err = ESP_ERR_TIMEOUT; break; }
        uint32_t timeout_ms = remaining > 30000000 ? 30000 : (uint32_t)((remaining + 999) / 1000);
        err = ai_http_api(task_url, NULL, "image_generation", timeout_ms, &body, &length);
        if (err != ESP_OK) break;
        modelscope_task_state_t state;
        err = modelscope_parse_task(body, length, &state, image_url);
        free(body); body = NULL;
        if (err != ESP_OK) break;
        ESP_LOGI(TAG, "task %s state=%d after %llds", task_id, (int)state,
                 (long long)((esp_timer_get_time() - started_us) / 1000000));
        if (state == MODELSCOPE_FAILED) { err = ESP_FAIL; break; }
        if (state == MODELSCOPE_SUCCEEDED) break;
    }
    if (err == ESP_ERR_TIMEOUT)
        ESP_LOGE(TAG, "task %s timed out after %u ms", task_id, (unsigned)APP_AI_TIMEOUT_MS);
done:
    free(body);
    return err;
}

esp_err_t modelscope_download_image(const char *image_url, uint8_t **image, size_t *length)
{
    if (!image || !length) return ESP_ERR_INVALID_ARG;
    *image = NULL; *length = 0;
    return ai_http_download(image_url, image, length);
}
