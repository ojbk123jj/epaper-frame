#include "modelscope_protocol.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "modelscope_task";

static cJSON *parse_object(const char *body, size_t length)
{
    if (body == NULL || length == 0 || length > 32768) return NULL;
    /* Bound parser recursion before passing a remote response to cJSON. */
    int depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        char ch = body[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
        } else if (ch == '"') quoted = true;
        else if (ch == '{' || ch == '[') { if (++depth > 32) return NULL; }
        else if (ch == '}' || ch == ']') { if (--depth < 0) return NULL; }
    }
    if (quoted || depth) return NULL;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(body, length, &end, false);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }
    while (end < body + length && isspace((unsigned char)*end)) ++end;
    if (end != body + length) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

char *modelscope_request_json(const char *model, const char *prompt, const char *size)
{
    if (!model || !*model || !prompt || !*prompt || !size || !*size) return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    char *json = NULL;
    if (cJSON_AddStringToObject(root, "model", model) &&
        cJSON_AddStringToObject(root, "prompt", prompt) &&
        cJSON_AddStringToObject(root, "size", size)) {
        json = cJSON_PrintUnformatted(root);
    }
    cJSON_Delete(root);
    return json;
}

esp_err_t modelscope_parse_submit(const char *body, size_t length,
                                 char task_id[MODELSCOPE_TASK_ID_SIZE])
{
    if (!task_id) return ESP_ERR_INVALID_ARG;
    task_id[0] = '\0';
    cJSON *root = parse_object(body, length);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "task_id");
    const cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "task_status");
    if (cJSON_IsString(status) && !strcmp(status->valuestring, "FAILED")) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    if (cJSON_IsString(id) && id->valuestring) {
        size_t n = strlen(id->valuestring);
        bool valid = n > 0 && n < MODELSCOPE_TASK_ID_SIZE;
        for (size_t i = 0; valid && i < n; ++i) {
            unsigned char ch = (unsigned char)id->valuestring[i];
            valid = isalnum(ch) || ch == '-' || ch == '_';
        }
        if (valid) {
            memcpy(task_id, id->valuestring, n + 1);
            err = ESP_OK;
        }
    }
    cJSON_Delete(root);
    return err;
}

esp_err_t modelscope_parse_task(const char *body, size_t length,
                               modelscope_task_state_t *state, char **image_url)
{
    if (!state || !image_url) return ESP_ERR_INVALID_ARG;
    *image_url = NULL;
    *state = MODELSCOPE_PENDING;
    cJSON *root = parse_object(body, length);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    const cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "task_status");
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    if (!cJSON_IsString(status) || !status->valuestring) goto done;
    ESP_LOGI(TAG, "task_status=%s", status->valuestring);
    if (!strcmp(status->valuestring, "PENDING") || !strcmp(status->valuestring, "RUNNING")) {
        err = ESP_OK;
    } else if (!strcmp(status->valuestring, "FAILED") || !strcmp(status->valuestring, "CANCELED") ||
               !strcmp(status->valuestring, "CANCELLED")) {
        *state = MODELSCOPE_FAILED;
        err = ESP_OK;
    } else if (!strcmp(status->valuestring, "SUCCEED")) {
        const cJSON *images = cJSON_GetObjectItemCaseSensitive(root, "output_images");
        const cJSON *url = cJSON_GetArrayItem(images, 0);
        if (!cJSON_IsArray(images) || !cJSON_IsString(url) || !url->valuestring) goto done;
        size_t n = strlen(url->valuestring);
        if (n <= 8 || n > MODELSCOPE_MAX_URL || strncmp(url->valuestring, "https://", 8)) goto done;
        const char *authority_end = strpbrk(url->valuestring + 8, "/?#");
        size_t authority_length = authority_end ? (size_t)(authority_end - url->valuestring - 8) : n - 8;
        if (!authority_length || memchr(url->valuestring + 8, '@', authority_length)) goto done;
        for (size_t i = 0; i < n; ++i) {
            if ((unsigned char)url->valuestring[i] <= ' ') goto done;
        }
        *image_url = malloc(n + 1);
        if (!*image_url) { err = ESP_ERR_NO_MEM; goto done; }
        memcpy(*image_url, url->valuestring, n + 1);
        *state = MODELSCOPE_SUCCEEDED;
        err = ESP_OK;
    }
done:
    cJSON_Delete(root);
    return err;
}
