#pragma once

#include "esp_err.h"

/* Connect configured Wi-Fi and synchronize the clock for TLS if needed.
 * Call from the owning business task; concurrent calls are not supported. */
esp_err_t network_service_prepare(void);

/* Stop Wi-Fi, preserving initialization so the next task can reconnect. */
esp_err_t network_service_suspend(void);
