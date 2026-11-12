#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modelscope_protocol.h"
#include "cJSON.h"

static esp_err_t task(const char *json, modelscope_task_state_t *state, char **url)
{
    return modelscope_parse_task(json, strlen(json), state, url);
}

int main(void)
{
    const char *prompt = "猫，红色\"帽子\"\nback\\slash";
    char *json = modelscope_request_json("Qwen/Qwen-Image-2.1-Turbo", prompt, "768x576");
    assert(json);
    cJSON *root = cJSON_Parse(json); assert(root);
    assert(!strcmp(cJSON_GetObjectItem(root, "prompt")->valuestring, prompt));
    assert(!strcmp(cJSON_GetObjectItem(root, "model")->valuestring, "Qwen/Qwen-Image-2.1-Turbo"));
    cJSON_Delete(root); free(json);
    char id[MODELSCOPE_TASK_ID_SIZE];
    const char *submit = "{\"task_id\":\"abc-123_test\"} \n";
    assert(modelscope_parse_submit(submit, strlen(submit), id) == ESP_OK);
    assert(!strcmp(id, "abc-123_test"));
    const char *invalid[] = {"{}", "{\"task_id\":1}", "{\"task_id\":\"../evil\"}",
        "{\"task_id\":\"\"}", "{\"task_id\":\"ok\"}garbage", "[]", "{broken"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        assert(modelscope_parse_submit(invalid[i], strlen(invalid[i]), id) == ESP_ERR_INVALID_RESPONSE);
        assert(!id[0]);
    }
    submit = "{\"task_id\":\"abc\",\"task_status\":\"FAILED\"}";
    assert(modelscope_parse_submit(submit, strlen(submit), id) == ESP_FAIL);
    modelscope_task_state_t state;
    char *url;
    assert(task("{\"task_status\":\"RUNNING\"}", &state, &url) == ESP_OK && state == MODELSCOPE_PENDING && !url);
    assert(task("{\"task_status\":\"FAILED\"}", &state, &url) == ESP_OK && state == MODELSCOPE_FAILED && !url);
    assert(task("{\"task_status\":\"SUCCEED\",\"output_images\":[\"https://cdn.example/a.png?x=1\"]}", &state, &url) == ESP_OK);
    assert(state == MODELSCOPE_SUCCEEDED && !strcmp(url, "https://cdn.example/a.png?x=1")); free(url);
    const char *bad_tasks[] = {
        "{\"task_status\":\"UNKNOWN\"}", "{\"task_status\":\"SUCCEED\",\"output_images\":[]}",
        "{\"task_status\":\"SUCCEED\",\"output_images\":[123]}",
        "{\"task_status\":\"SUCCEED\",\"output_images\":[\"http://cdn.example/a.png\"]}",
        "{\"task_status\":\"SUCCEED\",\"output_images\":[\"https:///a.png\"]}",
        "{\"task_status\":\"SUCCEED\",\"output_images\":[\"https://user:pass@cdn.example/a.png\"]}",
        "{\"task_status\":\"SUCCEED\",\"output_images\":[\"https://cdn.example/a\\n.png\"]}",
    };
    for (size_t i = 0; i < sizeof(bad_tasks)/sizeof(bad_tasks[0]); ++i) {
        assert(task(bad_tasks[i], &state, &url) == ESP_ERR_INVALID_RESPONSE && !url);
    }
    char nested[2050];
    memset(nested, '[', 1000); memset(nested+1000, ']', 1000); nested[2000]=0;
    assert(task(nested, &state, &url) == ESP_ERR_INVALID_RESPONSE);
    puts("ModelScope protocol tests passed");
    return 0;
}
