#include "esp_log.h"

#include "epaper_display.h"
#include "ai_console.h"
#include "ai_service.h"
#include "app_config.h"
#include "ble_image.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_ERROR_CHECK(epaper_display_init());
    ESP_ERROR_CHECK(ai_service_start());
    ESP_ERROR_CHECK(ble_image_start());
    ESP_ERROR_CHECK(ai_console_start());
    ESP_LOGI(TAG, "EPaperFrame ready");
    if (APP_AI_GENERATE_ON_BOOT) {
        ai_status_t status;
        ai_service_status(&status);
        if (status.configured) {
            esp_err_t err = ai_service_submit(APP_AI_PROMPT);
            if (err != ESP_OK) ESP_LOGE(TAG, "startup request not queued: %s", esp_err_to_name(err));
        }
    }
}
