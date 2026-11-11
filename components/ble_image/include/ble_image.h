#pragma once
#include "esp_err.h"
/* Call after frame_service_start and before submitting any job. */
esp_err_t ble_image_start(void);
