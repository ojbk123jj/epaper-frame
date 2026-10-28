#include "esp_log.h"

#include "epd_console.h"
#include "epd_worker.h"
#include "sdkconfig.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_LOGI(TAG, "EPaperFrame bring-up start");

    ESP_ERROR_CHECK(epd_worker_start());

#ifdef CONFIG_EPD_AUTORUN
    epd_worker_autorun();
#endif

    ESP_ERROR_CHECK(epd_console_start());
    ESP_LOGI(TAG, "console ready, type 'epd' for commands");
}
