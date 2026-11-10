#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai_http.h"
#include "app_config.h"
#include "modelscope_client.h"
#include "cJSON.h"

static int submit_calls, poll_calls, download_calls;
static esp_err_t submit_error, poll_error;
static const char *submit_body;
static const char *poll_body;
static int64_t now_us;
static bool complete_after_pending;

static void reset(void)
{
    submit_calls = poll_calls = download_calls = 0;
    submit_error = poll_error = ESP_OK;
    submit_body = "{\"task_id\":\"task-123\"}";
    poll_body = "{\"task_status\":\"SUCCEED\",\"output_images\":[\"https://images.example/result.png\"]}";
    now_us = 0;
    complete_after_pending = false;
}

int64_t esp_timer_get_time(void) { return now_us; }
void vTaskDelay(unsigned ticks) { now_us += (int64_t)ticks * 1000; }

esp_err_t ai_http_api(const char *url, const char *json, const char *task_type,
                      uint32_t timeout_ms, char **body, size_t *length)
{
    assert(timeout_ms && timeout_ms <= 30000);
    const char *reply;
    *body = NULL; *length = 0;
    if (json) {
        ++submit_calls;
        assert(!strcmp(url, "https://api-inference.modelscope.cn/v1/images/generations"));
        assert(!task_type);
        cJSON *request = cJSON_Parse(json); assert(request);
        assert(!strcmp(cJSON_GetObjectItem(request, "model")->valuestring, APP_AI_MODEL));
        assert(!strcmp(cJSON_GetObjectItem(request, "size")->valuestring, APP_AI_SIZE));
        assert(!strcmp(cJSON_GetObjectItem(request, "prompt")->valuestring, "test cat"));
        cJSON_Delete(request);
        if (submit_error != ESP_OK) return submit_error;
        reply = submit_body;
    } else {
        ++poll_calls;
        assert(!strcmp(url, "https://api-inference.modelscope.cn/v1/tasks/task-123"));
        assert(task_type && !strcmp(task_type, "image_generation"));
        if (poll_error != ESP_OK) return poll_error;
        reply = complete_after_pending && poll_calls <= 2 ?
            (poll_calls == 1 ? "{\"task_status\":\"PENDING\"}" : "{\"task_status\":\"RUNNING\"}") : poll_body;
    }
    *body = strdup(reply); assert(*body);
    *length = strlen(reply);
    return ESP_OK;
}

esp_err_t ai_http_download(const char *url, uint8_t **image, size_t *length)
{
    if (!url) return ESP_ERR_INVALID_ARG;
    ++download_calls;
    assert(!strcmp(url, "https://images.example/result.png"));
    *image = malloc(3); assert(*image); memcpy(*image, "png", 3); *length = 3;
    return ESP_OK;
}

int main(void)
{
    char *url;
    reset(); complete_after_pending = true;
    assert(modelscope_generate_image("test cat", &url) == ESP_OK);
    assert(url && !strcmp(url, "https://images.example/result.png"));
    assert(submit_calls == 1 && poll_calls == 3 && download_calls == 0);
    uint8_t *image; size_t length;
    assert(modelscope_download_image(url, &image, &length) == ESP_OK);
    assert(download_calls == 1 && length == 3 && !memcmp(image, "png", 3));
    free(image); free(url);

    reset(); submit_error = ESP_FAIL;
    assert(modelscope_generate_image("test cat", &url) == ESP_FAIL && !url);
    assert(submit_calls == 1 && poll_calls == 0); /* no automatic resubmission */
    reset(); submit_body = "{}";
    assert(modelscope_generate_image("test cat", &url) == ESP_ERR_INVALID_RESPONSE && !url);
    assert(submit_calls == 1 && poll_calls == 0);
    reset(); poll_error = ESP_FAIL;
    assert(modelscope_generate_image("test cat", &url) == ESP_FAIL && !url);
    assert(submit_calls == 1 && poll_calls == 1);
    reset(); poll_body = "{\"task_status\":\"FAILED\"}";
    assert(modelscope_generate_image("test cat", &url) == ESP_FAIL && !url);
    reset(); poll_body = "{\"task_status\":\"SUCCEED\",\"output_images\":[]}";
    assert(modelscope_generate_image("test cat", &url) == ESP_ERR_INVALID_RESPONSE && !url);
    reset(); poll_body = "{\"task_status\":\"RUNNING\"}";
    assert(modelscope_generate_image("test cat", &url) == ESP_ERR_TIMEOUT && !url);
    assert(submit_calls == 1 && poll_calls > 1 && now_us >= (int64_t)APP_AI_TIMEOUT_MS * 1000);
    reset();
    assert(modelscope_generate_image(NULL, &url) == ESP_ERR_INVALID_ARG && !url);
    assert(modelscope_generate_image("", &url) == ESP_ERR_INVALID_ARG && !url);
    assert(modelscope_generate_image("test cat", NULL) == ESP_ERR_INVALID_ARG);
    assert(submit_calls == 0 && poll_calls == 0);
    image = (uint8_t *)1; length = 123;
    assert(modelscope_download_image(NULL, &image, &length) == ESP_ERR_INVALID_ARG);
    assert(!image && !length);
    puts("ModelScope client orchestration tests passed");
}
