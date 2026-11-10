#define _POSIX_C_SOURCE 200809L
#define APP_CONFIG_NO_LOCAL
#define APP_MODELSCOPE_TOKEN "test-token"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../components/modelscope_client/src/ai_http.c"

typedef struct {
    int status;
    int64_t advertised;
    const char *body, *location;
    bool incomplete, read_error;
    size_t repeated_length;
} reply_t;
static reply_t replies[5];
static int requests, cleanups, authorizations, async_headers, task_headers;
static size_t written;
static int64_t fake_time;
struct fake_client { esp_http_client_config_t config; reply_t reply; size_t offset; };

static void reset(void)
{
    memset(replies, 0, sizeof(replies));
    requests = cleanups = authorizations = async_headers = task_headers = 0;
    written = 0; fake_time = 0;
    for (int i = 0; i < 5; ++i) { replies[i].status = 200; replies[i].body = "ok"; }
}
int64_t esp_timer_get_time(void) { return fake_time += 1000; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    assert(requests < 5 && config->disable_auto_redirect && config->crt_bundle_attach);
    struct fake_client *client = calloc(1, sizeof(*client));
    assert(client);
    client->config = *config; client->reply = replies[requests++];
    return client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value)
{
    if (!strcmp(key, "Authorization")) {
        assert(!strncmp(c->config.url, "https://api-inference.modelscope.cn/v1/", 37));
        assert(!strcmp(value, "Bearer test-token")); ++authorizations;
    }
    if (!strcmp(key, "X-ModelScope-Async-Mode")) { assert(!strcmp(value, "true")); ++async_headers; }
    if (!strcmp(key, "X-ModelScope-Task-Type")) { assert(!strcmp(value, "image_generation")); ++task_headers; }
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int n) { (void)c; (void)n; return ESP_OK; }
int esp_http_client_write(esp_http_client_handle_t c, const char *data, int n)
{
    (void)c; (void)data; int sent = n > 2 ? 2 : n; written += (size_t)sent; return sent;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c)
{
    if (c->reply.location) {
        esp_http_client_event_t event = { .event_id = HTTP_EVENT_ON_HEADER,
            .user_data = c->config.user_data, .header_key = "Location", .header_value = (char *)c->reply.location };
        c->config.event_handler(&event);
    }
    return c->reply.advertised;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c) { return c->reply.status; }
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t c, int n) { (void)c; assert(n > 0); return ESP_OK; }
int esp_http_client_read(esp_http_client_handle_t c, char *data, int n)
{
    if (c->reply.read_error) return -1;
    size_t length = c->reply.repeated_length ? c->reply.repeated_length : strlen(c->reply.body);
    size_t count = length - c->offset;
    if (count > (size_t)n) count = (size_t)n;
    if (c->reply.repeated_length) memset(data, 'x', count);
    else memcpy(data, c->reply.body + c->offset, count);
    c->offset += count;
    return (int)count;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) { return !c->reply.incomplete; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { ++cleanups; free(c); return ESP_OK; }

int main(void)
{
    const char *api = "https://api-inference.modelscope.cn/v1/images/generations";
    char *body; uint8_t *image; size_t length;
    reset();
    assert(ai_http_api(api, "{\"prompt\":\"test\"}", NULL, 30000, &body, &length) == ESP_OK);
    assert(length == 2 && !strcmp(body, "ok") && authorizations == 1 && async_headers == 1);
    assert(written == strlen("{\"prompt\":\"test\"}") && cleanups == 1); free(body);
    reset();
    assert(ai_http_api(api, NULL, "image_generation", 30000, &body, &length) == ESP_OK);
    assert(task_headers == 1 && async_headers == 0); free(body);
    reset();
    assert(ai_http_api("https://other.example/v1/", NULL, NULL, 30000, &body, &length) == ESP_ERR_INVALID_ARG);
    assert(requests == 0 && !body && length == 0);
    reset(); replies[0].status = 302; replies[0].location = "https://other.example/image";
    assert(ai_http_api(api, "{}", NULL, 30000, &body, &length) == ESP_ERR_INVALID_RESPONSE);
    assert(requests == 1 && cleanups == 1 && !body);
    reset(); replies[0].status = 302; replies[0].location = "https://cdn.example/image";
    assert(ai_http_download("https://images.example/image", &image, &length) == ESP_OK);
    assert(requests == 2 && cleanups == 2 && authorizations == 0 && length == 2); free(image);
    reset(); replies[0].status = 302; replies[0].location = "http://cdn.example/image";
    assert(ai_http_download("https://images.example/image", &image, &length) == ESP_ERR_INVALID_ARG);
    assert(requests == 1 && cleanups == 1 && !image && !length);
    reset(); replies[0].advertised = APP_AI_MAX_IMAGE_BYTES + 1;
    assert(ai_http_download("https://images.example/image", &image, &length) == ESP_ERR_INVALID_SIZE);
    assert(!image && !length && cleanups == 1);
    reset(); replies[0].repeated_length = APP_AI_MAX_IMAGE_BYTES + 1;
    assert(ai_http_download("https://images.example/image", &image, &length) == ESP_ERR_INVALID_SIZE);
    assert(!image && !length && cleanups == 1);
    reset(); replies[0].incomplete = true;
    assert(ai_http_download("https://images.example/image", &image, &length) == ESP_ERR_INVALID_RESPONSE);
    assert(!image && !length && cleanups == 1);
    reset(); replies[0].read_error = true;
    assert(ai_http_download("https://images.example/image", &image, &length) == ESP_FAIL);
    assert(!image && !length && cleanups == 1);
    reset(); replies[0].status = 429;
    assert(ai_http_api(api, "{}", NULL, 30000, &body, &length) == ESP_ERR_INVALID_RESPONSE);
    assert(requests == 1 && cleanups == 1 && !body);
    reset();
    assert(ai_http_api(api, NULL, NULL, 1, &body, &length) == ESP_ERR_TIMEOUT);
    assert(cleanups == 1 && !body);
    puts("AI HTTP bounds, authentication and redirect tests passed");
}
