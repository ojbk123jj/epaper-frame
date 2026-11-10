#include "ai_http.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app_config.h"
#include "modelscope_protocol.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "ai_http";

typedef struct {
    uint8_t *data;
    size_t length;
    int status;
    char *location;
    esp_err_t header_error;
} response_t;

static void response_free(response_t *response)
{
    free(response->data);
    free(response->location);
    memset(response, 0, sizeof(*response));
}

static esp_err_t headers(esp_http_client_event_t *event)
{
    response_t *response = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key &&
        !strcasecmp(event->header_key, "Location") && event->header_value) {
        size_t n = strlen(event->header_value);
        free(response->location); response->location = NULL;
        if (n > MODELSCOPE_MAX_URL) response->header_error = ESP_ERR_INVALID_SIZE;
        else {
            response->location = strdup(event->header_value);
            if (!response->location) response->header_error = ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static esp_err_t fetch(const char *url, const char *json, const char *task_type,
                       const char *authorization, size_t limit, uint32_t timeout_ms,
                       response_t *response)
{
    memset(response, 0, sizeof(*response));
    if (!url || strncmp(url, "https://", 8) || !timeout_ms) return ESP_ERR_INVALID_ARG;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    esp_http_client_config_t config = {
        .url = url,
        .method = json ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = (int)(timeout_ms < 30000 ? timeout_ms : 30000),
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
        .event_handler = headers,
        .user_data = response,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .user_agent = "EPaperFrame/1.0",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_err_t err = ESP_OK;
    if (authorization) err = esp_http_client_set_header(client, "Authorization", authorization);
    if (err == ESP_OK && json) err = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (err == ESP_OK && json) err = esp_http_client_set_header(client, "X-ModelScope-Async-Mode", "true");
    if (err == ESP_OK && task_type) err = esp_http_client_set_header(client, "X-ModelScope-Task-Type", task_type);
    size_t json_length = json ? strlen(json) : 0;
    if (err == ESP_OK) err = esp_http_client_open(client, (int)json_length);
    if (err != ESP_OK) goto done;
    for (size_t sent = 0; sent < json_length;) {
        int n = esp_http_client_write(client, json + sent, (int)(json_length - sent));
        if (n <= 0) { err = ESP_FAIL; goto done; }
        sent += (size_t)n;
    }
    int64_t content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) { err = ESP_FAIL; goto done; }
    response->status = esp_http_client_get_status_code(client);
    if (response->header_error != ESP_OK) { err = response->header_error; goto done; }
    if ((uint64_t)content_length > limit) { err = ESP_ERR_INVALID_SIZE; goto done; }
    size_t capacity = 0;
    char chunk[2048];
    for (;;) {
        int64_t remaining = deadline - esp_timer_get_time();
        if (remaining <= 0) { err = ESP_ERR_TIMEOUT; break; }
        int remaining_ms = (int)((remaining + 999) / 1000);
        esp_http_client_set_timeout_ms(client, remaining_ms < 30000 ? remaining_ms : 30000);
        int n = esp_http_client_read(client, chunk, sizeof(chunk));
        if (n < 0) { err = ESP_FAIL; break; }
        if (n == 0) {
            if (!esp_http_client_is_complete_data_received(client)) err = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        if ((size_t)n > limit - response->length) { err = ESP_ERR_INVALID_SIZE; break; }
        size_t needed = response->length + (size_t)n + 1;
        if (needed > capacity) {
            size_t next = capacity ? capacity * 2 : 4096;
            if (next < needed) next = needed;
            if (next > limit + 1) next = limit + 1;
            uint8_t *buffer = heap_caps_realloc(response->data, next, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buffer) { err = ESP_ERR_NO_MEM; break; }
            response->data = buffer; capacity = next;
        }
        memcpy(response->data + response->length, chunk, (size_t)n);
        response->length += (size_t)n;
        response->data[response->length] = 0;
    }
done:
    esp_http_client_cleanup(client);
    if (err != ESP_OK) response_free(response);
    return err;
}

static esp_err_t check_status(int status)
{
    if (status >= 200 && status < 300) return ESP_OK;
    if (status == 401 || status == 403) ESP_LOGE(TAG, "HTTP %d: check token, account binding and model permission", status);
    else if (status == 429) ESP_LOGE(TAG, "HTTP 429: model quota or rate limit reached");
    else ESP_LOGE(TAG, "HTTP status %d", status);
    return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t ai_http_api(const char *url, const char *json, const char *task_type,
                      uint32_t timeout_ms, char **response, size_t *length)
{
    if (!response || !length) return ESP_ERR_INVALID_ARG;
    *response = NULL; *length = 0;
    const char *prefix = "https://api-inference.modelscope.cn/v1/";
    if (!url || strncmp(url, prefix, strlen(prefix))) return ESP_ERR_INVALID_ARG;
    size_t n = strlen(APP_MODELSCOPE_TOKEN);
    if (!n || n > 512 || strpbrk(APP_MODELSCOPE_TOKEN, "\r\n")) return ESP_ERR_INVALID_ARG;
    char authorization[520];
    memcpy(authorization, "Bearer ", 7);
    memcpy(authorization + 7, APP_MODELSCOPE_TOKEN, n + 1);
    response_t result;
    esp_err_t err = fetch(url, json, task_type, authorization, 32768, timeout_ms, &result);
    memset(authorization, 0, sizeof(authorization));
    if (err == ESP_OK) err = check_status(result.status);
    if (err == ESP_OK) {
        *response = (char *)result.data; *length = result.length; result.data = NULL;
    }
    response_free(&result);
    return err;
}

esp_err_t ai_http_download(const char *url, uint8_t **image, size_t *length)
{
    if (!image || !length || !url) return ESP_ERR_INVALID_ARG;
    *image = NULL; *length = 0;
    char *current = strdup(url);
    if (!current) return ESP_ERR_NO_MEM;
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    for (int redirect = 0; redirect <= 3; ++redirect) {
        response_t result;
        /* Image hosts never receive the API authorization header. */
        err = fetch(current, NULL, NULL, NULL, APP_AI_MAX_IMAGE_BYTES, 60000, &result);
        if (err != ESP_OK) { response_free(&result); break; }
        if (result.status >= 300 && result.status < 400 && result.location && redirect < 3) {
            free(current); current = result.location; result.location = NULL;
            response_free(&result);
            continue;
        }
        err = check_status(result.status);
        if (err == ESP_OK) {
            if (!result.data || !result.length) err = ESP_ERR_INVALID_RESPONSE;
            else { *image = result.data; *length = result.length; result.data = NULL; }
        }
        response_free(&result);
        break;
    }
    free(current);
    return err;
}
