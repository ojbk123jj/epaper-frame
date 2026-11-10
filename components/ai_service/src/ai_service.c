#include "ai_service.h"

#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "modelscope_client.h"
#include "network_service.h"
#include "epaper_display.h"
#include "image_convert.h"
#include "image_decode.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "ai";
typedef struct { char prompt[APP_AI_MAX_PROMPT + 1]; } ai_job_t;
static QueueHandle_t s_jobs;
static SemaphoreHandle_t s_lock;
static ai_status_t s_status;

const char *ai_stage_name(ai_stage_t stage)
{
    static const char *const names[] = {
        "idle", "queued", "connecting", "generating", "downloading",
        "converting", "displaying", "succeeded", "failed",
    };
    return (unsigned)stage < sizeof(names) / sizeof(names[0]) ? names[stage] : "unknown";
}

static bool configured(void)
{
    size_t ssid = strlen(APP_WIFI_SSID), password = strlen(APP_WIFI_PASSWORD);
    size_t token = strlen(APP_MODELSCOPE_TOKEN);
    return ssid > 0 && ssid <= 32 && password <= 63 && (!password || password >= 8) &&
           token > 0 && token <= 512 && !strpbrk(APP_MODELSCOPE_TOKEN, "\r\n") &&
           strlen(APP_AI_MODEL) > 0 && strlen(APP_AI_MODEL) <= 128;
}

static void stage(ai_stage_t value)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.stage = value;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "state: %s", ai_stage_name(value));
}

static esp_err_t generate_and_display(const char *prompt)
{
    char *image_url = NULL;
    uint8_t *compressed = NULL, *rgb = NULL, *frame = NULL;
    size_t length = 0;
    int width = 0, height = 0;
    stage(AI_CONNECTING);
    esp_err_t err = network_service_prepare();
    if (err != ESP_OK) goto done;

    stage(AI_GENERATING);
    err = modelscope_generate_image(prompt, &image_url);
    if (err != ESP_OK) goto done;

    stage(AI_DOWNLOADING);
    err = modelscope_download_image(image_url, &compressed, &length);
    if (err != ESP_OK) goto done;
    /* No radio transmissions while the panel booster/waveform is active. */
    err = network_service_suspend();
    if (err != ESP_OK) goto done;
    stage(AI_CONVERTING);
    err = image_decode_rgb(compressed, length, &rgb, &width, &height);
    free(compressed); compressed = NULL;
    if (err != ESP_OK) goto done;
    frame = heap_caps_malloc(EPD_FB_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!frame) { err = ESP_ERR_NO_MEM; goto done; }
    err = image_rgb_to_frame(rgb, (size_t)width * height * 3, width, height, true, frame, EPD_FB_SIZE);
    free(rgb); rgb = NULL;
    if (err != ESP_OK) goto done;
    stage(AI_DISPLAYING);
    err = epaper_display_show(frame, EPD_FB_SIZE, APP_AI_FAST_REFRESH ? EPD_MODE_FAST : EPD_MODE_FULL);
done:
    network_service_suspend();
    free(image_url);
    free(compressed); free(rgb); free(frame);
    return err;
}

static void worker(void *arg)
{
    (void)arg;
    ai_job_t job;
    for (;;) {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        esp_err_t err = generate_and_display(job.prompt);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        ai_stage_t failed_stage = s_status.stage;
        s_status.stage = err == ESP_OK ? AI_SUCCEEDED : AI_FAILED;
        s_status.last_error = err;
        if (err == ESP_OK) ++s_status.displayed_count;
        s_status.busy = false;
        xSemaphoreGive(s_lock);
        if (err == ESP_OK) ESP_LOGI(TAG, "image generation and display completed");
        else ESP_LOGE(TAG, "request failed at %s: %s", ai_stage_name(failed_stage), esp_err_to_name(err));
    }
}

esp_err_t ai_service_start(void)
{
    if (s_lock || s_jobs) return ESP_ERR_INVALID_STATE;
    s_lock = xSemaphoreCreateMutex();
    s_jobs = xQueueCreate(1, sizeof(ai_job_t));
    if (!s_lock || !s_jobs) goto fail;
    s_status.configured = configured();
    if (xTaskCreate(worker, "ai_image", 24576, NULL, 5, NULL) != pdPASS) goto fail;
    if (!s_status.configured) ESP_LOGW(TAG, "configure Wi-Fi and ModelScope token in config/local_config.h");
    return ESP_OK;
fail:
    if (s_jobs) vQueueDelete(s_jobs);
    if (s_lock) vSemaphoreDelete(s_lock);
    s_jobs = NULL; s_lock = NULL;
    return ESP_ERR_NO_MEM;
}

esp_err_t ai_service_submit(const char *prompt)
{
    if (!prompt || !*prompt || strlen(prompt) > APP_AI_MAX_PROMPT) return ESP_ERR_INVALID_ARG;
    if (!s_lock || !s_jobs) return ESP_ERR_INVALID_STATE;
    ai_job_t job;
    memset(&job, 0, sizeof(job));
    memcpy(job.prompt, prompt, strlen(prompt));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_status.configured || s_status.busy) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_status.busy = true;
    ai_stage_t previous = s_status.stage;
    s_status.stage = AI_QUEUED;
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        s_status.busy = false; s_status.stage = previous;
        xSemaphoreGive(s_lock);
        return ESP_FAIL;
    }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

void ai_service_status(ai_status_t *status)
{
    if (!status) return;
    if (!s_lock) { memset(status, 0, sizeof(*status)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status;
    xSemaphoreGive(s_lock);
}
