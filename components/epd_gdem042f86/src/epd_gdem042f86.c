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

typedef struct epd_dev {
    epd_config_t cfg;
    spi_device_handle_t spi;
    uint8_t *fb;
} epd_dev_t;

/* 供 SPI 发送参数用，.bss 位于内部 RAM，可被 DMA 访问 */
static uint8_t s_param[8] __attribute__((aligned(4)));

void epd_default_config(epd_config_t *cfg)
{
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

static esp_err_t epd_hw_reset(epd_handle_t h)
{
    gpio_set_level(h->cfg.pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(h->cfg.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
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

    ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0x04, NULL, 0), TAG, "power on");
    ESP_RETURN_ON_ERROR(epd_wait_busy(h, EPD_BUSY_TIMEOUT_POWER_MS), TAG, "power on busy");

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
        ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0xA5, NULL, 0), TAG, "A5");
        ESP_RETURN_ON_ERROR(epd_wait_busy(h, EPD_BUSY_TIMEOUT_POWER_MS), TAG, "A5 busy");
    }

    return ESP_OK;
}

esp_err_t epd_new(const epd_config_t *cfg, epd_handle_t *out)
{
    ESP_RETURN_ON_FALSE(cfg != NULL && out != NULL, ESP_ERR_INVALID_ARG, TAG, "bad args");

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
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_out), TAG, "gpio out config");

    gpio_config_t io_in = {
        .pin_bit_mask = (1ULL << cfg->pin_busy),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_in), TAG, "gpio in config");

    gpio_set_level(cfg->pin_cs, 1);
    gpio_set_level(cfg->pin_dc, 1);
    gpio_set_level(cfg->pin_rst, 1);

    spi_bus_config_t bus = {
        .mosi_io_num = cfg->pin_mosi,
        .miso_io_num = -1,
        .sclk_io_num = cfg->pin_sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_SPI_CHUNK,
    };
    esp_err_t err = spi_bus_initialize(cfg->spi_host, &bus, SPI_DMA_CH_AUTO);
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
        spi_bus_free(cfg->spi_host);
        free(h->fb);
        free(h);
        return err;
    }

    ESP_LOGI(TAG, "init ok: %dx%d, fb=%p (%s), spi=%d Hz, busy=%d",
             EPD_WIDTH, EPD_HEIGHT, (void *)h->fb,
             cfg->fb_in_psram ? "psram" : "internal", cfg->clock_hz,
             gpio_get_level(cfg->pin_busy));

    *out = h;
    return ESP_OK;
}

esp_err_t epd_del(epd_handle_t h)
{
    ESP_RETURN_ON_FALSE(h != NULL, ESP_ERR_INVALID_ARG, TAG, "null handle");
    spi_bus_remove_device(h->spi);
    spi_bus_free(h->cfg.spi_host);
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

int epd_busy_level(epd_handle_t h)
{
    return h != NULL ? gpio_get_level(h->cfg.pin_busy) : -1;
}

esp_err_t epd_sleep(epd_handle_t h)
{
    ESP_RETURN_ON_FALSE(h != NULL, ESP_ERR_INVALID_ARG, TAG, "null handle");
    const uint8_t off = 0x00;
    ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0x02, &off, 1), TAG, "power off");
    ESP_RETURN_ON_ERROR(epd_wait_busy(h, EPD_BUSY_TIMEOUT_POWER_MS), TAG, "power off busy");
    const uint8_t magic = 0xA5;
    return epd_write_cmd(h, 0x07, &magic, 1);
}

esp_err_t epd_refresh(epd_handle_t h, epd_mode_t mode, uint32_t *elapsed_ms)
{
    ESP_RETURN_ON_FALSE(h != NULL, ESP_ERR_INVALID_ARG, TAG, "null handle");

    int64_t t0 = esp_timer_get_time();

    ESP_RETURN_ON_ERROR(epd_panel_init(h, mode), TAG, "panel init");
    ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0x10, NULL, 0), TAG, "DTM");
    ESP_RETURN_ON_ERROR(epd_write_bytes(h, h->fb, EPD_FB_SIZE), TAG, "write fb");

    const uint8_t data = 0x00;
    ESP_RETURN_ON_ERROR(epd_write_cmd(h, 0x12, &data, 1), TAG, "DRF");

    uint32_t timeout = (mode == EPD_MODE_FAST) ? EPD_BUSY_TIMEOUT_FAST_MS
                                               : EPD_BUSY_TIMEOUT_FULL_MS;
    ESP_RETURN_ON_ERROR(epd_wait_busy(h, timeout), TAG, "refresh busy");
    ESP_RETURN_ON_ERROR(epd_sleep(h), TAG, "sleep");

    uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    if (elapsed_ms != NULL) {
        *elapsed_ms = ms;
    }
    ESP_LOGI(TAG, "refresh done: mode=%s %" PRIu32 " ms",
             mode == EPD_MODE_FAST ? "fast" : "full", ms);
    return ESP_OK;
}

esp_err_t epd_set_clock(epd_handle_t h, int clock_hz)
{
    ESP_RETURN_ON_FALSE(h != NULL && clock_hz > 0, ESP_ERR_INVALID_ARG, TAG, "bad args");
    spi_bus_remove_device(h->spi);
    spi_device_interface_config_t dev = {
        .clock_speed_hz = clock_hz,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(h->cfg.spi_host, &dev, &h->spi), TAG, "re-add device");
    h->cfg.clock_hz = clock_hz;
    return ESP_OK;
}

int epd_clock_hz(epd_handle_t h)
{
    return h != NULL ? h->cfg.clock_hz : 0;
}
