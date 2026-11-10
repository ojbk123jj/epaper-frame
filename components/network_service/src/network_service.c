#include "network_service.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "app_config.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#define CONNECTED BIT0
#define FAILED BIT1

static EventGroupHandle_t s_events;
static bool s_initialized, s_started, s_sntp, s_netif_initialized;
static atomic_bool s_connect_enabled;
static atomic_uint s_retries;
static esp_event_handler_instance_t s_wifi_handler, s_ip_handler;

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (atomic_load(&s_connect_enabled)) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, CONNECTED);
        if (atomic_load(&s_connect_enabled)) {
            if (atomic_fetch_add(&s_retries, 1) < 5) esp_wifi_connect();
            else xEventGroupSetBits(s_events, FAILED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&s_retries, 0);
        xEventGroupClearBits(s_events, FAILED);
        xEventGroupSetBits(s_events, CONNECTED);
    }
}

static esp_err_t initialize(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err; /* Preserve existing NVS contents on failure. */
    if (!s_netif_initialized) {
        err = esp_netif_init();
        if (err != ESP_OK) return err;
        s_netif_initialized = true;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_events = xEventGroupCreate();
    if (!s_events) return ESP_ERR_NO_MEM;
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (!netif) { vEventGroupDelete(s_events); s_events = NULL; return ESP_ERR_NO_MEM; }
    bool wifi_ready = false;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) goto fail;
    wifi_ready = true;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               network_event, NULL, &s_wifi_handler);
    if (err != ESP_OK) goto fail;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               network_event, NULL, &s_ip_handler);
    if (err != ESP_OK) goto fail;
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, APP_WIFI_SSID, strlen(APP_WIFI_SSID));
    memcpy(wifi.sta.password, APP_WIFI_PASSWORD, strlen(APP_WIFI_PASSWORD));
    wifi.sta.pmf_cfg.capable = true;
    wifi.sta.threshold.authmode = strlen(APP_WIFI_PASSWORD) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    if (err != ESP_OK) goto fail;
    s_initialized = true;
    return ESP_OK;
fail:
    if (s_ip_handler) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
    if (s_wifi_handler) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
    s_ip_handler = s_wifi_handler = NULL;
    if (wifi_ready) esp_wifi_deinit();
    esp_netif_destroy_default_wifi(netif);
    vEventGroupDelete(s_events); s_events = NULL;
    return err;
}

esp_err_t network_service_prepare(void)
{
    if (!strlen(APP_WIFI_SSID) || strlen(APP_WIFI_SSID) > 32 || strlen(APP_WIFI_PASSWORD) > 63) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = s_initialized ? ESP_OK : initialize();
    if (err != ESP_OK) return err;
    if (!s_started) {
        atomic_store(&s_retries, 0);
        atomic_store(&s_connect_enabled, true);
        xEventGroupClearBits(s_events, CONNECTED | FAILED);
        err = esp_wifi_start();
        if (err != ESP_OK) { atomic_store(&s_connect_enabled, false); return err; }
        s_started = true;
    }
    EventBits_t bits = xEventGroupWaitBits(s_events, CONNECTED | FAILED, pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    if (!(bits & CONNECTED)) {
        ESP_LOGE("network_service", "Wi-Fi connection failed; check SSID, password and 2.4GHz network");
        return bits & FAILED ? ESP_FAIL : ESP_ERR_TIMEOUT;
    }
    time_t now = time(NULL);
    if (now < 1735689600) {
        if (!s_sntp) {
            esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
            err = esp_netif_sntp_init(&config);
            if (err != ESP_OK) return err;
            s_sntp = true;
        }
        err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(20000));
        if (err != ESP_OK || time(NULL) < 1735689600) {
            ESP_LOGE("network_service", "time synchronization failed; HTTPS requires a valid clock");
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

esp_err_t network_service_suspend(void)
{
    atomic_store(&s_connect_enabled, false);
    if (!s_started) return ESP_OK;
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK) s_started = false;
    return err;
}
