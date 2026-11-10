#include <assert.h>
#include <stdio.h>

#include "../../components/epaper_display/src/epaper_display.c"

struct display_test_mutex { bool locked; };
struct epd_dev { uint8_t frame[EPD_FB_SIZE]; };

static struct epd_dev panel;
static uint8_t input[EPD_FB_SIZE], other[EPD_FB_SIZE];
static bool fail_mutex, check_concurrent;
static esp_err_t new_result = ESP_OK, refresh_result = ESP_OK;
static int new_calls, refresh_calls, deleted_mutexes;
static epd_mode_t last_mode;

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return fail_mutex ? NULL : calloc(1, sizeof(struct display_test_mutex));
}

int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned ticks)
{
    assert(mutex != NULL && ticks == 0);
    if (mutex->locked) return pdFALSE;
    mutex->locked = true;
    return pdTRUE;
}

int xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex != NULL && mutex->locked);
    mutex->locked = false;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t mutex)
{
    assert(mutex != NULL && !mutex->locked);
    ++deleted_mutexes;
    free(mutex);
}

void epd_default_config(epd_config_t *cfg) { memset(cfg, 0, sizeof(*cfg)); }

esp_err_t epd_new(const epd_config_t *cfg, epd_handle_t *out)
{
    assert(cfg != NULL && out != NULL);
    ++new_calls;
    *out = new_result == ESP_OK ? &panel : NULL;
    return new_result;
}

uint8_t *epd_fb(epd_handle_t h)
{
    assert(h == &panel);
    return panel.frame;
}

esp_err_t epd_refresh(epd_handle_t h, epd_mode_t mode, uint32_t *elapsed_ms)
{
    assert(h == &panel && s_lock->locked);
    assert(memcmp(panel.frame, input, EPD_FB_SIZE) == 0);
    ++refresh_calls;
    last_mode = mode;
    if (check_concurrent) {
        assert(epaper_display_show(other, sizeof(other), EPD_MODE_FAST) == ESP_ERR_INVALID_STATE);
        assert(memcmp(panel.frame, input, EPD_FB_SIZE) == 0);
    }
    *elapsed_ms = 123;
    return refresh_result;
}

int main(void)
{
    memset(input, 0x55, sizeof(input));
    memset(other, 0xaa, sizeof(other));
    assert(epaper_display_show(input, sizeof(input), EPD_MODE_FULL) == ESP_ERR_INVALID_STATE);

    fail_mutex = true;
    assert(epaper_display_init() == ESP_ERR_NO_MEM);
    assert(new_calls == 0);
    fail_mutex = false;
    new_result = ESP_FAIL;
    assert(epaper_display_init() == ESP_FAIL);
    assert(deleted_mutexes == 1);
    new_result = ESP_OK;
    assert(epaper_display_init() == ESP_OK);
    assert(epaper_display_init() == ESP_ERR_INVALID_STATE);
    assert(new_calls == 2);

    assert(epaper_display_show(NULL, sizeof(input), EPD_MODE_FULL) == ESP_ERR_INVALID_ARG);
    assert(epaper_display_show(input, sizeof(input) - 1, EPD_MODE_FULL) == ESP_ERR_INVALID_ARG);
    assert(epaper_display_show(input, sizeof(input) + 1, EPD_MODE_FULL) == ESP_ERR_INVALID_ARG);
    assert(epaper_display_show(input, sizeof(input), (epd_mode_t)99) == ESP_ERR_INVALID_ARG);
    assert(refresh_calls == 0 && !s_lock->locked);

    check_concurrent = true;
    assert(epaper_display_show(input, sizeof(input), EPD_MODE_FULL) == ESP_OK);
    assert(last_mode == EPD_MODE_FULL && refresh_calls == 1 && !s_lock->locked);
    input[0] = 0xff;
    assert(panel.frame[0] == 0x55); /* Display owns its copy of the submitted frame. */

    check_concurrent = false;
    refresh_result = ESP_ERR_TIMEOUT;
    assert(epaper_display_show(input, sizeof(input), EPD_MODE_FAST) == ESP_ERR_TIMEOUT);
    assert(last_mode == EPD_MODE_FAST && !s_lock->locked);
    refresh_result = ESP_OK;
    assert(epaper_display_show(input, sizeof(input), EPD_MODE_FAST) == ESP_OK);
    assert(refresh_calls == 3 && !s_lock->locked);

    vSemaphoreDelete(s_lock);
    puts("Display service tests passed");
    return 0;
}
