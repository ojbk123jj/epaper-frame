#pragma once

#include <stdint.h>

#define EPD_WIDTH       400
#define EPD_HEIGHT      300
#define EPD_FB_STRIDE   (EPD_WIDTH / 4)              /* 100 字节/行 */
#define EPD_FB_SIZE     (EPD_FB_STRIDE * EPD_HEIGHT) /* 30000 字节 */

/* 2bit 颜色编码，与 SSD2683 的 4 色数据一致 */
typedef enum {
    EPD_COLOR_BLACK  = 0x0,
    EPD_COLOR_WHITE  = 0x1,
    EPD_COLOR_YELLOW = 0x2,
    EPD_COLOR_RED    = 0x3,
} epd_color_t;
