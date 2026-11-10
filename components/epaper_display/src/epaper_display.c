#include "epaper_display.h"

#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "display";
static epd_handle_t s_epd;
static SemaphoreHandle_t s_lock;

esp_err_t epaper_display_init(void)
{
    ESP_RETURN_ON_FALSE(s_epd == NULL && s_lock == NULL, ESP_ERR_INVALID_STATE,
                        TAG, "display already initialized");
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "display lock allocation failed");

    epd_config_t cfg;
    epd_default_config(&cfg);
    esp_err_t err = epd_new(&cfg, &s_epd);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return err;
    }
    return ESP_OK;
}

esp_err_t epaper_display_show(const uint8_t *frame, size_t length, epd_mode_t mode)
{
    ESP_RETURN_ON_FALSE(frame != NULL && length == EPD_FB_SIZE &&
                        (mode == EPD_MODE_FULL || mode == EPD_MODE_FAST),
                        ESP_ERR_INVALID_ARG, TAG, "invalid image frame");
    ESP_RETURN_ON_FALSE(s_epd != NULL && s_lock != NULL, ESP_ERR_INVALID_STATE,
                        TAG, "display not initialized");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_lock, 0) == pdTRUE, ESP_ERR_INVALID_STATE,
                        TAG, "display already in progress");

    memcpy(epd_fb(s_epd), frame, length);
    uint32_t elapsed_ms = 0;
    esp_err_t err = epd_refresh(s_epd, mode, &elapsed_ms);
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "image displayed: %s, %" PRIu32 " ms",
                 mode == EPD_MODE_FAST ? "fast" : "full", elapsed_ms);
    } else {
        ESP_LOGE(TAG, "image display failed: %s", esp_err_to_name(err));
    }
    return err;
}
