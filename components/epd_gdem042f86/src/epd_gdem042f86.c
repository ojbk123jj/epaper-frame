#include "epd_gdem042f86.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "epd_fb_pack.h"

static const char *TAG = "epd";

#define EPD_SPI_CHUNK 2048
#define EPD_BUSY_TIMEOUT_POWER_MS 5000
#define EPD_BUSY_TIMEOUT_FULL_MS  40000
#define EPD_BUSY_TIMEOUT_FAST_MS  25000
#define EPD_RESET_HOLD_MS 1000
#define EPD_RESET_STARTUP_MS 120
#define EPD_SLEEP_SETTLE_MS 20

typedef struct epd_dev {
    epd_config_t cfg;
    spi_device_handle_t spi;
    uint8_t *fb;
    bool owns_bus;
    bool parked;
    bool recovery_pending;
    int64_t recovery_started_us;
} epd_dev_t;

/* 供 SPI 发送参数用，.bss 位于内部 RAM，可被 DMA 访问 */
static uint8_t s_param[8] __attribute__((aligned(4)));

void epd_default_config(epd_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->pin_sck = CONFIG_EPD_PIN_SCK;
    cfg->pin_mosi = CONFIG_EPD_PIN_MOSI;
    cfg->pin_cs = CONFIG_EPD_PIN_CS;
    cfg->pin_dc = CONFIG_EPD_PIN_DC;
    cfg->pin_rst = CONFIG_EPD_PIN_RST;
    cfg->pin_busy = CONFIG_EPD_PIN_BUSY;
    cfg->spi_host = SPI2_HOST;
    cfg->clock_hz = CONFIG_EPD_SPI_CLOCK_HZ;
#ifdef CONFIG_EPD_FB_IN_PSRAM
    cfg->fb_in_psram = true;
#else
    cfg->fb_in_psram = false;
#endif
}

static esp_err_t epd_write_bytes(epd_handle_t h, const uint8_t *data, size_t len)
{
    ESP_RETURN_ON_FALSE(h->spi != NULL, ESP_ERR_INVALID_STATE, TAG, "SPI device unavailable");
    gpio_set_level(h->cfg.pin_dc, 1);
    gpio_set_level(h->cfg.pin_cs, 0);

    esp_err_t err = ESP_OK;
    while (len > 0) {
        size_t chunk = len > EPD_SPI_CHUNK ? EPD_SPI_CHUNK : len;
        spi_transaction_t t = {
            .length = chunk * 8,
            .tx_buffer = data,
        };
        err = spi_device_polling_transmit(h->spi, &t);
        if (err != ESP_OK) {
            break;
        }
        data += chunk;
        len -= chunk;
    }

    gpio_set_level(h->cfg.pin_cs, 1);
    return err;
}

static esp_err_t epd_write_cmd(epd_handle_t h, uint8_t cmd,
                               const uint8_t *params, size_t nparams)
{
    ESP_RETURN_ON_FALSE(h->spi != NULL, ESP_ERR_INVALID_STATE, TAG, "SPI device unavailable");
    ESP_RETURN_ON_FALSE(nparams <= sizeof(s_param) && (nparams == 0 || params != NULL),
                        ESP_ERR_INVALID_ARG, TAG, "invalid command parameters");
    gpio_set_level(h->cfg.pin_dc, 0);
    gpio_set_level(h->cfg.pin_cs, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    esp_err_t err = spi_device_polling_transmit(h->spi, &t);
    gpio_set_level(h->cfg.pin_cs, 1);
    ESP_RETURN_ON_ERROR(err, TAG, "cmd 0x%02x failed", cmd);

    if (nparams == 0) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(nparams <= sizeof(s_param), ESP_ERR_INVALID_SIZE, TAG,
                        "too many params: %u", (unsigned)nparams);
    memcpy(s_param, params, nparams);
    return epd_write_bytes(h, s_param, nparams);
}

static esp_err_t epd_wait_busy(epd_handle_t h, uint32_t timeout_ms)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (gpio_get_level(h->cfg.pin_busy) == 0) {
        if (esp_timer_get_time() > deadline) {
            ESP_LOGE(TAG, "busy timeout after %" PRIu32 " ms", timeout_ms);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

/*
 * 命令发出后，面板要过一小段时间才会把 BUSY 拉低。
 * 如果像 epd_wait_busy() 那样"立刻读一次高电平就返回"，会直接跳过整个刷新。
 * 这里先等它变忙（最多 1500ms），再等它恢复空闲。
 */
static esp_err_t epd_wait_busy_cycle(epd_handle_t h, uint32_t timeout_ms)
{
    int64_t start = esp_timer_get_time();

    while (gpio_get_level(h->cfg.pin_busy) == 1) {
        if (esp_timer_get_time() - start > 1500 * 1000) {
            ESP_LOGE(TAG, "BUSY did not assert; check panel connection and command acceptance");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    int64_t deadline = start + (int64_t)timeout_ms * 1000;
    while (gpio_get_level(h->cfg.pin_busy) == 0) {
        if (esp_timer_get_time() > deadline) {
            ESP_LOGE(TAG, "busy timeout after %" PRIu32 " ms", timeout_ms);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

/* Commands that start an operation must complete a full BUSY cycle. */
static esp_err_t epd_run_busy_cmd(epd_handle_t h, uint8_t cmd,
                                const uint8_t *params, size_t nparams,
                                uint32_t timeout_ms)
{
    esp_err_t err = epd_write_cmd(h, cmd, params, nparams);
    if (err == ESP_OK) err = epd_wait_busy_cycle(h, timeout_ms);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "command 0x%02x failed: %s, BUSY=%d", cmd,
                 esp_err_to_name(err), gpio_get_level(h->cfg.pin_busy));
    }
    return err;
}

static esp_err_t epd_hw_reset(epd_handle_t h)
{
    /* Keep reset asserted until both the pulse and post-shutdown recovery
     * interval have elapsed. Already elapsed idle time counts toward recovery.
     * The fixed interval preserves the recovery margin validated on this board. */
    h->parked = false;
    gpio_set_level(h->cfg.pin_cs, 1);
    gpio_set_level(h->cfg.pin_rst, 0);
    int64_t now = esp_timer_get_time();
    int64_t release_at = now + (int64_t)EPD_RESET_HOLD_MS * 1000;
    if (h->recovery_pending) {
        int64_t recovery_end = h->recovery_started_us + (int64_t)EPD_RECOVERY_INTERVAL_MS * 1000;
        if (recovery_end > release_at) release_at = recovery_end;
    }
    uint32_t wait_ms = (uint32_t)((release_at - now + 999) / 1000);
    do {
        /* Round up a tick and recheck the deadline; vTaskDelay is tick-based. */
        vTaskDelay(pdMS_TO_TICKS(wait_ms) + 1);
        int64_t remaining_us = release_at - esp_timer_get_time();
        wait_ms = remaining_us > 0 ? (uint32_t)((remaining_us + 999) / 1000) : 0;
    } while (wait_ms > 0);
    h->recovery_pending = false;
    gpio_set_level(h->cfg.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(EPD_RESET_STARTUP_MS));
    return ESP_OK;
}

/* Reset is the available recovery mechanism; this board has no power switch.
 * Do not send commands to a controller whose BUSY signal is stuck low. */
static void epd_abort(epd_handle_t h)
{
    h->parked = false;
    gpio_set_level(h->cfg.pin_cs, 1);
    gpio_set_level(h->cfg.pin_rst, 0);
    h->recovery_started_us = esp_timer_get_time();
    h->recovery_pending = true;
    ESP_LOGE(TAG, "panel held in reset after failure; disconnect power if rails persist");
}

static esp_err_t epd_panel_init(epd_handle_t h, epd_mode_t mode)
{
    static const uint8_t btst[4] = {0x0F, 0x8B, 0x9C, 0x96};
    static const uint8_t psr[2] = {0x2F, 0x69};
    static const uint8_t pwr[2] = {0x07, 0xF0};
    static const uint8_t cdi[1] = {0x37};
    static const uint8_t gate[2] = {0x64, 0x53};
    static const uint8_t gsst[4] = {0x00, 0x00, 0x00, 0x00};
    static const uint8_t pll[1] = {0x08};
    static const uint8_t e9[1] = {0x01};
    const uint8_t tres[4] = {
        (uint8_t)(EPD_WIDTH / 256), (uint8_t)(EPD_WIDTH % 256),
        (uint8_t)(EPD_HEIGHT / 256), (uint8_t)(EPD_HEIGHT % 256),
    };

    ESP_RETURN_ON_ERROR(epd_hw_reset(h), TAG, "reset failed");
    ESP_RETURN_ON_ERROR(epd_wait_busy(h, EPD_BUSY_TIMEOUT_POWER_MS), TAG, "standby busy");

    ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0x06, btst, sizeof(btst)), TAG, "BTST");
    ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0x00, psr, sizeof(psr)), TAG, "PSR");
    ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0x01, pwr, sizeof(pwr)), TAG, "PWR");
    ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0x50, cdi, sizeof(cdi)), TAG, "CDI");
    ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0x61, tres, sizeof(tres)), TAG, "TRES");
    ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0x62, gate, sizeof(gate)), TAG, "GATE");
    ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0x65, gsst, sizeof(gsst)), TAG, "GSST");
    ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0x30, pll, sizeof(pll)), TAG, "PLL");
    ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0xE9, e9, sizeof(e9)), TAG, "E9");

    ESP_RETURN_ON_ERROR(epd_run_busy_cmd(h, 0x04, NULL, 0, EPD_BUSY_TIMEOUT_POWER_MS),
                        TAG, "power on");

    if (mode == EPD_MODE_FAST) {
        static const uint8_t v1 = 0x01;
        static const uint8_t f6 = 0x15;
        static const uint8_t v0 = 0x00;
        static const uint8_t e0 = 0x02;
        static const uint8_t e6 = 0x5A;
        ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0xEF, &v1, 1), TAG, "EF on");
        ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0xF6, &f6, 1), TAG, "F6");
        ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0xEF, &v0, 1), TAG, "EF off");
        ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0xE0, &e0, 1), TAG, "E0");
        ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0xE6, &e6, 1), TAG, "E6");
        ESP_RETURN_ON_ERROR(epd_run_busy_cmd(h, 0xA5, NULL, 0, EPD_BUSY_TIMEOUT_POWER_MS),
                            TAG, "A5");
    }

    return ESP_OK;
}

esp_err_t epd_new(const epd_config_t *cfg, epd_handle_t *out)
{
    ESP_RETURN_ON_FALSE(cfg != NULL && out != NULL, ESP_ERR_INVALID_ARG, TAG, "bad args");

    *out = NULL;
    const int pins[] = {cfg->pin_sck, cfg->pin_mosi, cfg->pin_cs, cfg->pin_dc, cfg->pin_rst, cfg->pin_busy};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(pins[i]) && (i == 5 || GPIO_IS_VALID_OUTPUT_GPIO(pins[i])),
                            ESP_ERR_INVALID_ARG, TAG, "invalid GPIO");
        for (size_t j = 0; j < i; ++j) {
            ESP_RETURN_ON_FALSE(pins[i] != pins[j], ESP_ERR_INVALID_ARG, TAG, "duplicate GPIO");
        }
    }
    ESP_RETURN_ON_FALSE(cfg->clock_hz >= 100000 && cfg->clock_hz <= 10000000,
                        ESP_ERR_INVALID_ARG, TAG, "SPI clock must be 100000..10000000 Hz");

    epd_handle_t h = calloc(1, sizeof(*h));
    ESP_RETURN_ON_FALSE(h != NULL, ESP_ERR_NO_MEM, TAG, "no mem for handle");
    h->cfg = *cfg;

    uint32_t caps = MALLOC_CAP_8BIT | (cfg->fb_in_psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_DMA);
    h->fb = heap_caps_malloc(EPD_FB_SIZE, caps);
    if (h->fb == NULL) {
        ESP_LOGE(TAG, "framebuffer alloc failed (%d bytes, %s)",
                 EPD_FB_SIZE, cfg->fb_in_psram ? "PSRAM" : "internal");
        free(h);
        return ESP_ERR_NO_MEM;
    }
    epd_pack_fill(h->fb, EPD_COLOR_WHITE);

    gpio_config_t io_out = {
        .pin_bit_mask = (1ULL << cfg->pin_cs) | (1ULL << cfg->pin_dc) | (1ULL << cfg->pin_rst),
        .mode = GPIO_MODE_INPUT_OUTPUT, /* 保持输入通路以检查复位状态 */
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_out);
    if (err != ESP_OK) {
        free(h->fb);
        free(h);
        return err;
    }

    gpio_config_t io_in = {
        .pin_bit_mask = (1ULL << cfg->pin_busy),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&io_in);
    if (err != ESP_OK) {
        epd_abort(h);
        free(h->fb);
        free(h);
        return err;
    }

    gpio_set_level(cfg->pin_cs, 1);
    gpio_set_level(cfg->pin_dc, 1);
    gpio_set_level(cfg->pin_rst, 0); /* Remain in reset until an explicit operation. */

    spi_bus_config_t bus = {
        .mosi_io_num = cfg->pin_mosi,
        .miso_io_num = -1,
        .sclk_io_num = cfg->pin_sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_SPI_CHUNK,
    };
    err = spi_bus_initialize(cfg->spi_host, &bus, SPI_DMA_CH_AUTO);
    h->owns_bus = err == ESP_OK;
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi bus init failed: %s", esp_err_to_name(err));
        free(h->fb);
        free(h);
        return err;
    }

    spi_device_interface_config_t dev = {
        .clock_speed_hz = cfg->clock_hz,
        .mode = 0,
        .spics_io_num = -1, /* CS 手动控制 */
        .queue_size = 1,
    };
    err = spi_bus_add_device(cfg->spi_host, &dev, &h->spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi add device failed: %s", esp_err_to_name(err));
        if (h->owns_bus) spi_bus_free(cfg->spi_host);
        free(h->fb);
        free(h);
        return err;
    }

    *out = h;
    return ESP_OK;
}

esp_err_t epd_del(epd_handle_t h)
{
    ESP_RETURN_ON_FALSE(h != NULL, ESP_ERR_INVALID_ARG, TAG, "null handle");
    gpio_set_level(h->cfg.pin_cs, 1);
    gpio_set_level(h->cfg.pin_rst, 0);
    if (h->spi != NULL) spi_bus_remove_device(h->spi);
    if (h->owns_bus) spi_bus_free(h->cfg.spi_host);
    free(h->fb);
    free(h);
    return ESP_OK;
}

uint8_t *epd_fb(epd_handle_t h)
{
    return h != NULL ? h->fb : NULL;
}

void epd_fb_fill(epd_handle_t h, epd_color_t c)
{
    if (h != NULL) {
        epd_pack_fill(h->fb, c);
    }
}

void epd_fb_set_px(epd_handle_t h, int x, int y, epd_color_t c)
{
    if (h != NULL) {
        epd_pack_set(h->fb, x, y, c);
    }
}

esp_err_t epd_sleep(epd_handle_t h)
{
    ESP_RETURN_ON_FALSE(h != NULL, ESP_ERR_INVALID_ARG, TAG, "null handle");
    /* Repeated sleep requests must not send SPI commands into held reset.
     * Only a completed shutdown may set parked; an abort must not look asleep. */
    if (h->parked) return ESP_OK;
    ESP_RETURN_ON_FALSE(gpio_get_level(h->cfg.pin_rst) == 1,
                        ESP_ERR_INVALID_STATE, TAG, "panel held in reset; initialize first");
    esp_err_t err = epd_wait_busy(h, EPD_BUSY_TIMEOUT_POWER_MS);
    const uint8_t off = 0x00;
    if (err == ESP_OK) err = epd_run_busy_cmd(h, 0x02, &off, 1, EPD_BUSY_TIMEOUT_POWER_MS);
    const uint8_t magic = 0xA5;
    if (err == ESP_OK) err = epd_write_cmd(h, 0x07, &magic, 1);
    if (err != ESP_OK) {
        epd_abort(h);
        return err;
    }
    /* Let the sleep check byte take effect before asserting reset. Holding RST
     * does not guarantee deep-sleep current or physically remove panel power. */
    vTaskDelay(pdMS_TO_TICKS(EPD_SLEEP_SETTLE_MS));
    gpio_set_level(h->cfg.pin_cs, 1);
    gpio_set_level(h->cfg.pin_rst, 0);
    h->recovery_started_us = esp_timer_get_time();
    h->recovery_pending = true;
    h->parked = true;
    return err;
}

esp_err_t epd_refresh(epd_handle_t h, epd_mode_t mode, uint32_t *elapsed_ms)
{
    ESP_RETURN_ON_FALSE(h != NULL && (mode == EPD_MODE_FULL || mode == EPD_MODE_FAST),
                        ESP_ERR_INVALID_ARG, TAG, "invalid refresh arguments");
    if (elapsed_ms != NULL) *elapsed_ms = 0;
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = epd_panel_init(h, mode);
    if (err == ESP_OK) err = epd_write_cmd(h, 0x10, NULL, 0);
    if (err == ESP_OK) err = epd_write_bytes(h, h->fb, EPD_FB_SIZE);
    const uint8_t data = 0x00;
    uint32_t timeout = mode == EPD_MODE_FAST ? EPD_BUSY_TIMEOUT_FAST_MS : EPD_BUSY_TIMEOUT_FULL_MS;
    if (err == ESP_OK) err = epd_run_busy_cmd(h, 0x12, &data, 1, timeout);
    if (err != ESP_OK) {
        epd_abort(h);
        return err;
    }
    /* Complete power-off before deep sleep, matching the panel reference flow.
     * epd_sleep() holds the panel in reset if shutdown fails. */
    err = epd_sleep(h);
    if (err != ESP_OK) return err;
    uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    if (elapsed_ms != NULL) *elapsed_ms = ms;
    return ESP_OK;
}
