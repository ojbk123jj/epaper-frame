#include "esp_log.h"

#include "epaper_display.h"
#include "ai_console.h"
#include "ai_service.h"
#include "ble_image.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_ERROR_CHECK(epaper_display_init());
    ESP_ERROR_CHECK(ai_service_start());
    ESP_ERROR_CHECK(ble_image_start());
    ESP_ERROR_CHECK(ai_console_start());
    ESP_LOGI(TAG, "EPaperFrame ready");
}
