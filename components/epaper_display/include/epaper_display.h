#pragma once

#include <stddef.h>
#include <stdint.h>

#include "epd_gdem042f86.h"

/* 在启动阶段调用一次，随后业务任务可以提交图像。 */
esp_err_t epaper_display_init(void);

/* 输入为400x300、2bpp的面板数据，长度必须是EPD_FB_SIZE（30000字节）。
 * 颜色为00黑/01白/10黄/11红，逐行排列，每字节最左像素占bit7..6。
 * 函数先复制图像，再阻塞到刷新及关电完成，返回真实结果。
 * 必须从任务上下文调用；并发提交返回ESP_ERR_INVALID_STATE。
 * 图像缩放、PNG/JPEG解码和颜色量化由调用方在提交前完成。 */
esp_err_t epaper_display_show(const uint8_t *frame, size_t length, epd_mode_t mode);
