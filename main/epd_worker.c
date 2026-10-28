#include "epd_worker.h"

#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "epd_worker";

static epd_handle_t s_epd;
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_done;

static const char *mode_name(epd_mode_t m)
{
    return m == EPD_MODE_FAST ? "fast" : "full";
}

static esp_err_t run_job(const epd_job_t *job)
{
    epd_pattern_render(epd_fb(s_epd), job->pattern, job->solid);

    if (job->bench) {
        uint32_t full_ms = 0;
        uint32_t fast_ms = 0;
        esp_err_t err = epd_refresh(s_epd, EPD_MODE_FULL, &full_ms);
        if (err == ESP_OK) {
            err = epd_refresh(s_epd, EPD_MODE_FAST, &fast_ms);
        }
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "bench %s: full=%" PRIu32 " ms, fast=%" PRIu32 " ms",
                     epd_pattern_name(job->pattern), full_ms, fast_ms);
        }
        return err;
    }

    uint32_t ms = 0;
    esp_err_t err = epd_refresh(s_epd, job->mode, &ms);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s %s: %" PRIu32 " ms", epd_pattern_name(job->pattern),
                 mode_name(job->mode), ms);
    } else {
        ESP_LOGE(TAG, "%s %s failed: %s", epd_pattern_name(job->pattern),
                 mode_name(job->mode), esp_err_to_name(err));
    }
    return err;
}

static void epd_worker_task(void *arg)
{
    (void)arg;
    epd_job_t job;
    for (;;) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) == pdTRUE) {
            (void)run_job(&job);
            xSemaphoreGive(s_done);
        }
    }
}

esp_err_t epd_worker_start(void)
{
    epd_config_t cfg;
    epd_default_config(&cfg);
    ESP_RETURN_ON_ERROR(epd_new(&cfg, &s_epd), TAG, "epd_new");

    s_queue = xQueueCreate(1, sizeof(epd_job_t));
    s_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_queue != NULL && s_done != NULL, ESP_ERR_NO_MEM, TAG,
                        "queue/semaphore alloc");

    BaseType_t ok = xTaskCreate(epd_worker_task, "epd_worker", 6144, NULL, 5, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create");
    return ESP_OK;
}

epd_handle_t epd_worker_handle(void)
{
    return s_epd;
}

esp_err_t epd_worker_submit(const epd_job_t *job)
{
    ESP_RETURN_ON_FALSE(job != NULL, ESP_ERR_INVALID_ARG, TAG, "null job");
    return xQueueOverwrite(s_queue, job) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t epd_worker_submit_wait(const epd_job_t *job, uint32_t timeout_ms)
{
    ESP_RETURN_ON_ERROR(epd_worker_submit(job), TAG, "submit");
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "wait timeout after %" PRIu32 " ms", timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void epd_worker_autorun(void)
{
    const epd_job_t jobs[] = {
        {.pattern = EPD_PATTERN_SOLID, .mode = EPD_MODE_FULL, .solid = EPD_COLOR_WHITE},
        {.pattern = EPD_PATTERN_COLORS, .mode = EPD_MODE_FULL, .solid = EPD_COLOR_WHITE},
        {.pattern = EPD_PATTERN_PHOTO, .mode = EPD_MODE_FAST, .solid = EPD_COLOR_WHITE},
    };
    const size_t count = sizeof(jobs) / sizeof(jobs[0]);

    ESP_LOGI(TAG, "autorun: %u steps", (unsigned)count);
    for (size_t i = 0; i < count; i++) {
        esp_err_t err = epd_worker_submit_wait(&jobs[i], 60000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "autorun step %u failed: %s", (unsigned)i, esp_err_to_name(err));
        }
    }
    ESP_LOGI(TAG, "autorun done");
}
