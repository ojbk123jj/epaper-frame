#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "epd_panel.h"

typedef enum {
    EPD_MODE_FULL = 0, /* 规格书 20s 全刷，画质优先 */
    EPD_MODE_FAST = 1, /* 规格书 12s 快刷 */
} epd_mode_t;

typedef struct {
    int pin_sck;
    int pin_mosi;
    int pin_cs;
    int pin_dc;
    int pin_rst;
    int pin_busy;
    int spi_host;   /* 例如 SPI2_HOST */
    int clock_hz;
    bool fb_in_psram;
} epd_config_t;

typedef struct epd_dev *epd_handle_t;

/* 使用 Kconfig 默认引脚与频率 */
void epd_default_config(epd_config_t *cfg);

esp_err_t epd_new(const epd_config_t *cfg, epd_handle_t *out);
esp_err_t epd_del(epd_handle_t h);

uint8_t *epd_fb(epd_handle_t h);

void epd_fb_fill(epd_handle_t h, epd_color_t c);
void epd_fb_set_px(epd_handle_t h, int x, int y, epd_color_t c);

/* 一次完整事务：复位 -> 初始化 -> 写帧 -> 刷新 -> 休眠 */
esp_err_t epd_refresh(epd_handle_t h, epd_mode_t mode, uint32_t *elapsed_ms);
esp_err_t epd_sleep(epd_handle_t h);

int epd_busy_level(epd_handle_t h);
esp_err_t epd_set_clock(epd_handle_t h, int clock_hz);
int epd_clock_hz(epd_handle_t h);
