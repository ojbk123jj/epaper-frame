# EPaperFrame Bring-up 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 ESP32-S3 主控板 + GDEM042F86 四色墨水屏上跑通首个可用驱动，并通过图案与实测耗时验证时序、色彩映射与休眠。

**Architecture:** 一个自研 ESP-IDF 组件 `epd_gdem042f86`（SPI/GPIO + 面板协议，无全局状态），一个 app 层 worker task 串行执行刷新（避免 20s 阻塞 console），console 只入队命令。纯逻辑（像素打包、图案生成、文字）与 ESP-IDF 解耦，可在 PC 上用 gcc 单测。

**Tech Stack:** ESP-IDF v5.5.5（`/home/miaojianyu/esp/ESP_IDF/esp_idf_5.5/esp-idf`）、esp_driver_spi、esp_driver_gpio、esp_console、FreeRTOS、gcc（host 单测）。

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `CMakeLists.txt` | 顶层工程定义，`EXTRA_COMPONENT_DIRS components` |
| `sdkconfig.defaults` | esp32s3 / 16MB flash / Octal PSRAM / UART0 console |
| `main/CMakeLists.txt` | app 组件，注册 srcs + 依赖 |
| `main/main.c` | 入口：启动 worker → 自动序列 → console |
| `main/epd_worker.c/.h` | 刷新任务、命令队列（深度 1，后到覆盖）、bench、作者序列 |
| `main/epd_console.c/.h` | `esp_console` REPL 与 `epd` 命令解析 |
| `main/epd_patterns.c/.h` | 6 种测试图案渲染（纯函数，仅依赖帧缓冲） |
| `main/epd_text.c/.h` | 8x8 字模绘制（用于图案标注） |
| `main/font8.h` | 95 字符 ASCII 字模（源自 GoodDisplay 例程） |
| `main/assets/gimage_1.h` | 官方 400x300 四色样图（30000 字节） |
| `components/epd_gdem042f86/` | 驱动组件：`EPD_WIDTH/HEIGHT`、像素打包内联函数、面板协议 |
| `tests/host/` | PC 端 gcc 单测：打包、图案几何 |
| `README.md` | 构建、烧录、命令、验收结果记录 |

约定：
- 颜色：`00`=黑 `01`=白 `10`=黄 `11`=红，**行内 4 像素/字节，最左像素占 bit7..6**。
- 帧缓冲 30000 字节，行跨度 `EPD_FB_STRIDE = 100`。
- 所有面板访问串行化在 worker task 中，驱动本身不做互斥。

---

### Task 1: 工程骨架能编译

**Files:**
- Create: `CMakeLists.txt`
- Create: `sdkconfig.defaults`
- Create: `main/CMakeLists.txt`
- Create: `main/main.c`

- [ ] **Step 1: 写顶层 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")
project(epaper_frame)
```

- [ ] **Step 2: 写 sdkconfig.defaults**

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200
CONFIG_FREERTOS_HZ=1000
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
```

- [ ] **Step 3: 写 main/CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES epd_gdem042f86 esp_console
)
```

- [ ] **Step 4: 写临时 main/main.c**

```c
#include "esp_log.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_LOGI(TAG, "epaper-frame bring-up: skeleton");
}
```

- [ ] **Step 5: 编译**

Run:
```bash
. /home/miaojianyu/esp/ESP_IDF/esp_idf_5.5/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```
Expected: `Project build complete.`，无 error。

- [ ] **Step 6: 提交**

```bash
git add CMakeLists.txt sdkconfig.defaults main
git commit -m "build: 初始化 ESP-IDF 工程骨架"
```

---

### Task 2: 像素打包纯逻辑（TDD）

**Files:**
- Create: `components/epd_gdem042f86/include/epd_panel.h`
- Create: `components/epd_gdem042f86/include/epd_fb_pack.h`
- Create: `tests/host/test_fb_pack.c`
- Create: `tests/host/run.sh`

- [ ] **Step 1: 先写失败的测试**

`tests/host/test_fb_pack.c`：

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "epd_panel.h"
#include "epd_fb_pack.h"

static uint8_t fb[EPD_FB_SIZE];

static void test_frame_size(void)
{
    assert(EPD_WIDTH == 400);
    assert(EPD_HEIGHT == 300);
    assert(EPD_FB_STRIDE == 100);
    assert(EPD_FB_SIZE == 30000);
}

static void test_color_pattern(void)
{
    assert(epd_color_pattern(EPD_COLOR_BLACK) == 0x00);
    assert(epd_color_pattern(EPD_COLOR_WHITE) == 0x55);
    assert(epd_color_pattern(EPD_COLOR_YELLOW) == 0xAA);
    assert(epd_color_pattern(EPD_COLOR_RED) == 0xFF);
}

static void test_leftmost_pixel_is_msb(void)
{
    memset(fb, 0, sizeof fb);
    epd_pack_set(fb, 0, 0, EPD_COLOR_RED);
    assert(fb[0] == 0xC0);
    epd_pack_set(fb, 1, 0, EPD_COLOR_WHITE);
    assert(fb[0] == 0xD0);
    epd_pack_set(fb, 3, 0, EPD_COLOR_YELLOW);
    assert(fb[0] == 0xD2);
}

static void test_byte_order_black_white_yellow_red(void)
{
    memset(fb, 0, sizeof fb);
    epd_pack_set(fb, 0, 0, EPD_COLOR_BLACK);
    epd_pack_set(fb, 1, 0, EPD_COLOR_WHITE);
    epd_pack_set(fb, 2, 0, EPD_COLOR_YELLOW);
    epd_pack_set(fb, 3, 0, EPD_COLOR_RED);
    assert(fb[0] == 0x1B); /* 00 01 10 11 */
}

static void test_row_stride(void)
{
    memset(fb, 0, sizeof fb);
    epd_pack_set(fb, 0, 1, EPD_COLOR_RED);
    assert(fb[EPD_FB_STRIDE] == 0xC0);
    assert(fb[EPD_FB_STRIDE - 1] == 0x00);
}

static void test_fill_and_get(void)
{
    epd_pack_fill(fb, EPD_COLOR_YELLOW);
    assert(fb[0] == 0xAA);
    assert(fb[EPD_FB_SIZE - 1] == 0xAA);
    for (int y = 0; y < EPD_HEIGHT; y += 37) {
        for (int x = 0; x < EPD_WIDTH; x += 29) {
            assert(epd_pack_get(fb, x, y) == EPD_COLOR_YELLOW);
        }
    }
    epd_pack_set(fb, 399, 299, EPD_COLOR_BLACK);
    assert(epd_pack_get(fb, 399, 299) == EPD_COLOR_BLACK);
    assert(fb[EPD_FB_SIZE - 1] == 0xA8); /* 0xAA & ~0x03 */
}

static void test_out_of_range_is_ignored(void)
{
    epd_pack_fill(fb, EPD_COLOR_WHITE);
    epd_pack_set(fb, -1, 0, EPD_COLOR_BLACK);
    epd_pack_set(fb, 0, -1, EPD_COLOR_BLACK);
    epd_pack_set(fb, EPD_WIDTH, 0, EPD_COLOR_BLACK);
    epd_pack_set(fb, 0, EPD_HEIGHT, EPD_COLOR_BLACK);
    assert(fb[0] == 0x55);
    assert(fb[EPD_FB_SIZE - 1] == 0x55);
}

int main(void)
{
    test_frame_size();
    test_color_pattern();
    test_leftmost_pixel_is_msb();
    test_byte_order_black_white_yellow_red();
    test_row_stride();
    test_fill_and_get();
    test_out_of_range_is_ignored();
    printf("test_fb_pack: all tests passed\n");
    return 0;
}
```

- [ ] **Step 2: 写测试运行脚本**

`tests/host/run.sh`：

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."

INC_COMP=components/epd_gdem042f86/include
INC_MAIN=main

run() {
    local name="$1"; shift
    echo "== $name"
    gcc -std=c11 -Wall -Wextra -Werror -O1 \
        -I "$INC_COMP" -I "$INC_MAIN" \
        -o "/tmp/$name" "$@" && "/tmp/$name"
}

run test_fb_pack tests/host/test_fb_pack.c
run test_patterns tests/host/test_patterns.c main/epd_patterns.c main/epd_text.c
```

先只跑第一个（`test_patterns` 相关文件还不存在）：

Run: `chmod +x tests/host/run.sh && gcc -std=c11 -Wall -Wextra -Werror -O1 -I components/epd_gdem042f86/include -I main -o /tmp/test_fb_pack tests/host/test_fb_pack.c`
Expected: FAIL，`epd_panel.h: No such file or directory`。

- [ ] **Step 3: 实现 epd_panel.h**

`components/epd_gdem042f86/include/epd_panel.h`：

```c
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
```

- [ ] **Step 4: 实现 epd_fb_pack.h**

`components/epd_gdem042f86/include/epd_fb_pack.h`：

```c
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "epd_panel.h"

/* 单色时整字节的值：0x00 / 0x55 / 0xAA / 0xFF */
static inline uint8_t epd_color_pattern(epd_color_t c)
{
    return (uint8_t)(((unsigned)c & 0x3u) * 0x55u);
}

/* 最左像素占 bit7..6 */
static inline void epd_pack_set(uint8_t *fb, int x, int y, epd_color_t c)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
        return;
    }
    size_t idx = (size_t)y * EPD_FB_STRIDE + (size_t)(x >> 2);
    unsigned shift = 6u - 2u * (unsigned)(x & 3);
    fb[idx] = (uint8_t)((fb[idx] & ~(0x3u << shift)) | (((unsigned)c & 0x3u) << shift));
}

static inline epd_color_t epd_pack_get(const uint8_t *fb, int x, int y)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
        return EPD_COLOR_WHITE;
    }
    size_t idx = (size_t)y * EPD_FB_STRIDE + (size_t)(x >> 2);
    unsigned shift = 6u - 2u * (unsigned)(x & 3);
    return (epd_color_t)((fb[idx] >> shift) & 0x3u);
}

static inline void epd_pack_fill(uint8_t *fb, epd_color_t c)
{
    uint8_t v = epd_color_pattern(c);
    for (size_t i = 0; i < EPD_FB_SIZE; i++) {
        fb[i] = v;
    }
}
```

- [ ] **Step 5: 跑测试**

Run: `gcc -std=c11 -Wall -Wextra -Werror -O1 -I components/epd_gdem042f86/include -I main -o /tmp/test_fb_pack tests/host/test_fb_pack.c && /tmp/test_fb_pack`
Expected: `test_fb_pack: all tests passed`

- [ ] **Step 6: 提交**

```bash
git add components/epd_gdem042f86/include tests/host
git commit -m "feat(epd): 加入 2bit 像素打包逻辑与 host 单测"
```

---

### Task 3: 测试图案与文字（TDD）

**Files:**
- Create: `main/font8.h`（由官方例程转换生成）
- Create: `main/assets/gimage_1.h`（由官方例程转换生成）
- Create: `main/epd_text.h`, `main/epd_text.c`
- Create: `main/epd_patterns.h`, `main/epd_patterns.c`
- Create: `tests/host/test_patterns.c`

- [ ] **Step 1: 生成字模与样图头文件**

Run:
```bash
mkdir -p main/assets
{
  printf '#pragma once\n#include <stdint.h>\n\n'
  printf '/* GoodDisplay GDEM042F86 例程自带 8x8 (5 像素宽) ASCII 字模，0x20~0x7E */\n'
  sed -n '/const uint8_t Font8_Table/,/^};/p' \
      Docs/demos/STM32_GDEM042F86/HARDWARE/Fonts/font8.c \
    | sed 's/const uint8_t Font8_Table\[\]/static const uint8_t epd_font8_table[95 * 8]/'
} > main/font8.h

{
  printf '#pragma once\n#include <stdint.h>\n\n'
  printf '/* GoodDisplay 官方 400x300 四色样图（BWRY，2bit/像素，30000 字节） */\n'
  sed 's/^const unsigned char gImage_1\[30000\]/static const uint8_t gimage_1[30000]/' \
      Docs/demos/ESP32_GDEM042F86/Ap_29demo.h
} > main/assets/gimage_1.h
```

验证：
Run: `grep -c '0x' main/font8.h main/assets/gimage_1.h`
Expected: `main/font8.h:760`，`main/assets/gimage_1.h:1875`（数值只需非 0）。

- [ ] **Step 2: 先写失败的图案测试**

`tests/host/test_patterns.c`：

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "epd_fb_pack.h"
#include "epd_patterns.h"
#include "epd_text.h"

static uint8_t fb[EPD_FB_SIZE];

static int count_color(epd_color_t c)
{
    int n = 0;
    for (int y = 0; y < EPD_HEIGHT; y++) {
        for (int x = 0; x < EPD_WIDTH; x++) {
            if (epd_pack_get(fb, x, y) == c) {
                n++;
            }
        }
    }
    return n;
}

static void test_lookup(void)
{
    epd_pattern_id_t id = EPD_PATTERN_COUNT;
    assert(epd_pattern_lookup("grid", &id) && id == EPD_PATTERN_GRID);
    assert(epd_pattern_lookup("photo", &id) && id == EPD_PATTERN_PHOTO);
    assert(!epd_pattern_lookup("nope", &id));
    assert(strcmp(epd_pattern_name(EPD_PATTERN_CHECKER), "checker") == 0);
}

static void test_solid(void)
{
    epd_pattern_render(fb, EPD_PATTERN_SOLID, EPD_COLOR_YELLOW);
    assert(count_color(EPD_COLOR_YELLOW) == EPD_WIDTH * EPD_HEIGHT);
}

static void test_colors_quadrants(void)
{
    epd_pattern_render(fb, EPD_PATTERN_COLORS, EPD_COLOR_WHITE);
    assert(epd_pack_get(fb, 10, 10) == EPD_COLOR_BLACK);
    assert(epd_pack_get(fb, 390, 10) == EPD_COLOR_WHITE);
    assert(epd_pack_get(fb, 10, 290) == EPD_COLOR_YELLOW);
    assert(epd_pack_get(fb, 390, 290) == EPD_COLOR_RED);
}

static void test_checker_groups(void)
{
    epd_pattern_render(fb, EPD_PATTERN_CHECKER, EPD_COLOR_WHITE);
    /* 1px 区：相邻像素必须交替 */
    assert(epd_pack_get(fb, 0, 0) != epd_pack_get(fb, 1, 0));
    assert(epd_pack_get(fb, 1, 0) != epd_pack_get(fb, 2, 0));
    /* 4px 区（y>=200）：同组 4 个像素相同，下一组相反 */
    epd_color_t a = epd_pack_get(fb, 0, 250);
    for (int x = 0; x < 4; x++) {
        assert(epd_pack_get(fb, x, 250) == a);
    }
    assert(epd_pack_get(fb, 4, 250) != a);
}

static void test_grid_border(void)
{
    epd_pattern_render(fb, EPD_PATTERN_GRID, EPD_COLOR_WHITE);
    assert(epd_pack_get(fb, 0, 150) == EPD_COLOR_RED);      /* 左边框 */
    assert(epd_pack_get(fb, EPD_WIDTH - 1, 150) == EPD_COLOR_RED);
    assert(epd_pack_get(fb, 200, 0) == EPD_COLOR_RED);      /* 上边框 */
    assert(epd_pack_get(fb, 200, EPD_HEIGHT - 1) == EPD_COLOR_RED);
    assert(epd_pack_get(fb, 50, 50) == EPD_COLOR_WHITE);    /* 网格交点留白 */
    assert(epd_pack_get(fb, 100, 50) == EPD_COLOR_BLACK);   /* 竖网格线 */
    assert(epd_pack_get(fb, 50, 100) == EPD_COLOR_BLACK);   /* 横网格线 */
}

static void test_bars_groups(void)
{
    epd_pattern_render(fb, EPD_PATTERN_BARS, EPD_COLOR_WHITE);
    /* 1px 组内相邻像素必须是不同颜色 */
    assert(epd_pack_get(fb, 0, 150) != epd_pack_get(fb, 1, 150));
    /* 8px 组内前 8 个像素同色 */
    epd_color_t c = epd_pack_get(fb, 320, 150);
    for (int x = 320; x < 328; x++) {
        assert(epd_pack_get(fb, x, 150) == c);
    }
}

static void test_text_draw(void)
{
    epd_pack_fill(fb, EPD_COLOR_WHITE);
    epd_text_draw(fb, 10, 10, "AB", EPD_COLOR_BLACK, EPD_COLOR_WHITE, 3);
    int dark = 0;
    for (int y = 10; y < 34; y++) {
        for (int x = 10; x < 46; x++) {
            if (epd_pack_get(fb, x, y) == EPD_COLOR_BLACK) {
                dark++;
            }
        }
    }
    assert(dark > 10);
    assert(epd_text_width("AB", 3) == 36);
}

static void test_photo(void)
{
    epd_pattern_render(fb, EPD_PATTERN_PHOTO, EPD_COLOR_WHITE);
    /* 样图不是纯白，必须出现非白像素 */
    assert(count_color(EPD_COLOR_WHITE) < EPD_WIDTH * EPD_HEIGHT);
}

int main(void)
{
    test_lookup();
    test_solid();
    test_colors_quadrants();
    test_checker_groups();
    test_grid_border();
    test_bars_groups();
    test_text_draw();
    test_photo();
    printf("test_patterns: all tests passed\n");
    return 0;
}
```

- [ ] **Step 3: 跑测试确认失败**

Run: `gcc -std=c11 -Wall -Wextra -Werror -O1 -I components/epd_gdem042f86/include -I main -o /tmp/test_patterns tests/host/test_patterns.c main/epd_patterns.c main/epd_text.c`
Expected: FAIL，`epd_patterns.h: No such file or directory`。

- [ ] **Step 4: 实现 epd_text**

`main/epd_text.h`：

```c
#pragma once

#include <stdbool.h>

#include "epd_panel.h"

/* 8x8 字模，5 像素宽 + 1 像素字距；scale>=1 */
void epd_text_draw(uint8_t *fb, int x, int y, const char *s,
                   epd_color_t fg, epd_color_t bg, int scale);

int epd_text_width(const char *s, int scale);
```

`main/epd_text.c`：

```c
#include "epd_text.h"

#include "epd_fb_pack.h"
#include "font8.h"

#define GLYPH_W 5
#define GLYPH_H 8

void epd_text_draw(uint8_t *fb, int x, int y, const char *s,
                   epd_color_t fg, epd_color_t bg, int scale)
{
    if (scale < 1) {
        scale = 1;
    }
    for (const char *p = s; *p != '\0'; p++) {
        unsigned ch = (unsigned char)*p;
        if (ch < 0x20 || ch > 0x7E) {
            ch = (unsigned)'?';
        }
        const uint8_t *glyph = &epd_font8_table[(ch - 0x20) * GLYPH_H];
        for (int row = 0; row < GLYPH_H; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < GLYPH_W + 1; col++) {
                bool on = (col < GLYPH_W) && ((bits >> (7 - col)) & 0x1);
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        epd_pack_set(fb, x + col * scale + sx,
                                     y + row * scale + sy,
                                     on ? fg : bg);
                    }
                }
            }
        }
        x += (GLYPH_W + 1) * scale;
    }
}

int epd_text_width(const char *s, int scale)
{
    if (scale < 1) {
        scale = 1;
    }
    int n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n * (GLYPH_W + 1) * scale;
}
```

- [ ] **Step 5: 实现 epd_patterns**

`main/epd_patterns.h`：

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "epd_panel.h"

typedef enum {
    EPD_PATTERN_SOLID = 0,
    EPD_PATTERN_COLORS,
    EPD_PATTERN_CHECKER,
    EPD_PATTERN_BARS,
    EPD_PATTERN_GRID,
    EPD_PATTERN_PHOTO,
    EPD_PATTERN_COUNT
} epd_pattern_id_t;

const char *epd_pattern_name(epd_pattern_id_t id);
bool epd_pattern_lookup(const char *name, epd_pattern_id_t *out);

/* solid 参数仅在 id == EPD_PATTERN_SOLID 时生效 */
void epd_pattern_render(uint8_t *fb, epd_pattern_id_t id, epd_color_t solid);
```

`main/epd_patterns.c`：

```c
#include "epd_patterns.h"

#include <string.h>

#include "assets/gimage_1.h"
#include "epd_fb_pack.h"
#include "epd_text.h"

static const char *const k_names[EPD_PATTERN_COUNT] = {
    "solid", "colors", "checker", "bars", "grid", "photo",
};

const char *epd_pattern_name(epd_pattern_id_t id)
{
    if ((unsigned)id >= (unsigned)EPD_PATTERN_COUNT) {
        return "unknown";
    }
    return k_names[id];
}

bool epd_pattern_lookup(const char *name, epd_pattern_id_t *out)
{
    for (int i = 0; i < EPD_PATTERN_COUNT; i++) {
        if (strcmp(name, k_names[i]) == 0) {
            *out = (epd_pattern_id_t)i;
            return true;
        }
    }
    return false;
}

static void fill_rect(uint8_t *fb, int x0, int y0, int w, int h, epd_color_t c)
{
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            epd_pack_set(fb, x, y, c);
        }
    }
}

static void draw_colors(uint8_t *fb)
{
    fill_rect(fb, 0, 0, EPD_WIDTH / 2, EPD_HEIGHT / 2, EPD_COLOR_BLACK);
    fill_rect(fb, EPD_WIDTH / 2, 0, EPD_WIDTH / 2, EPD_HEIGHT / 2, EPD_COLOR_WHITE);
    fill_rect(fb, 0, EPD_HEIGHT / 2, EPD_WIDTH / 2, EPD_HEIGHT / 2, EPD_COLOR_YELLOW);
    fill_rect(fb, EPD_WIDTH / 2, EPD_HEIGHT / 2, EPD_WIDTH / 2, EPD_HEIGHT / 2, EPD_COLOR_RED);

    /* 中心十字，用于确认坐标原点与方向 */
    for (int x = 0; x < EPD_WIDTH; x++) {
        epd_pack_set(fb, x, EPD_HEIGHT / 2, EPD_COLOR_BLACK);
    }
    for (int y = 0; y < EPD_HEIGHT; y++) {
        epd_pack_set(fb, EPD_WIDTH / 2, y, EPD_COLOR_BLACK);
    }

    epd_text_draw(fb, 20, 20, "BLACK", EPD_COLOR_RED, EPD_COLOR_BLACK, 3);
    epd_text_draw(fb, 240, 20, "WHITE", EPD_COLOR_BLACK, EPD_COLOR_WHITE, 3);
    epd_text_draw(fb, 20, 260, "YELLOW", EPD_COLOR_BLACK, EPD_COLOR_YELLOW, 3);
    epd_text_draw(fb, 250, 260, "RED", EPD_COLOR_WHITE, EPD_COLOR_RED, 3);
}

static void draw_checker(uint8_t *fb)
{
    static const int k_cell[3] = {1, 2, 4};
    for (int band = 0; band < 3; band++) {
        int cell = k_cell[band];
        for (int y = band * 100; y < (band + 1) * 100; y++) {
            for (int x = 0; x < EPD_WIDTH; x++) {
                bool odd = (((x / cell) + (y / cell)) & 0x1) != 0;
                epd_pack_set(fb, x, y, odd ? EPD_COLOR_BLACK : EPD_COLOR_WHITE);
            }
        }
    }
    for (int x = 0; x < EPD_WIDTH; x++) {
        epd_pack_set(fb, x, 0, EPD_COLOR_RED);
        epd_pack_set(fb, x, EPD_HEIGHT - 1, EPD_COLOR_RED);
    }
}

static void draw_bars(uint8_t *fb)
{
    static const int k_width[4] = {1, 2, 4, 8};
    static const epd_color_t k_colors[4] = {
        EPD_COLOR_BLACK, EPD_COLOR_WHITE, EPD_COLOR_YELLOW, EPD_COLOR_RED,
    };
    for (int group = 0; group < 4; group++) {
        int w = k_width[group];
        for (int x = group * 100; x < (group + 1) * 100; x++) {
            epd_color_t c = k_colors[((x - group * 100) / w) % 4];
            for (int y = 0; y < EPD_HEIGHT; y++) {
                epd_pack_set(fb, x, y, c);
            }
        }
    }
    for (int x = 0; x < EPD_WIDTH; x++) {
        epd_pack_set(fb, x, 0, EPD_COLOR_RED);
        epd_pack_set(fb, x, EPD_HEIGHT - 1, EPD_COLOR_RED);
    }
}

static void draw_grid(uint8_t *fb)
{
    epd_pack_fill(fb, EPD_COLOR_WHITE);
    for (int x = 0; x < EPD_WIDTH; x += 20) {
        for (int y = 0; y < EPD_HEIGHT; y++) {
            epd_pack_set(fb, x, y, EPD_COLOR_BLACK);
        }
    }
    for (int y = 0; y < EPD_HEIGHT; y += 20) {
        for (int x = 0; x < EPD_WIDTH; x++) {
            epd_pack_set(fb, x, y, EPD_COLOR_BLACK);
        }
    }
    /* 10px 红色外框 */
    for (int i = 0; i < 10; i++) {
        for (int x = 0; x < EPD_WIDTH; x++) {
            epd_pack_set(fb, x, i, EPD_COLOR_RED);
            epd_pack_set(fb, x, EPD_HEIGHT - 1 - i, EPD_COLOR_RED);
        }
        for (int y = 0; y < EPD_HEIGHT; y++) {
            epd_pack_set(fb, i, y, EPD_COLOR_RED);
            epd_pack_set(fb, EPD_WIDTH - 1 - i, y, EPD_COLOR_RED);
        }
    }
    /* 四角标记：左上实心、右上空心，便于判断方向 */
    fill_rect(fb, 20, 20, 20, 20, EPD_COLOR_BLACK);
    fill_rect(fb, EPD_WIDTH - 40, 20, 20, 20, EPD_COLOR_BLACK);
    fill_rect(fb, EPD_WIDTH - 34, 26, 8, 8, EPD_COLOR_WHITE);
    epd_text_draw(fb, 60, 44, "400x300 BWRY", EPD_COLOR_BLACK, EPD_COLOR_WHITE, 2);
}

void epd_pattern_render(uint8_t *fb, epd_pattern_id_t id, epd_color_t solid)
{
    switch (id) {
    case EPD_PATTERN_SOLID:
        epd_pack_fill(fb, solid);
        break;
    case EPD_PATTERN_COLORS:
        draw_colors(fb);
        break;
    case EPD_PATTERN_CHECKER:
        draw_checker(fb);
        break;
    case EPD_PATTERN_BARS:
        draw_bars(fb);
        break;
    case EPD_PATTERN_GRID:
        draw_grid(fb);
        break;
    case EPD_PATTERN_PHOTO:
        memcpy(fb, gimage_1, EPD_FB_SIZE);
        break;
    default:
        epd_pack_fill(fb, EPD_COLOR_WHITE);
        break;
    }
}
```

- [ ] **Step 6: 跑测试**

Run: `./tests/host/run.sh`（先补上 `test_patterns` 那一行所需的 `main/epd_patterns.c main/epd_text.c` 已存在）
Expected:
```
test_fb_pack: all tests passed
== test_patterns
test_patterns: all tests passed
```

- [ ] **Step 7: 提交**

```bash
git add main/font8.h main/assets main/epd_text.c main/epd_text.h \
        main/epd_patterns.c main/epd_patterns.h tests/host/test_patterns.c
git commit -m "feat(app): 测试图案与 8x8 文字渲染，附 host 单测"
```

---

### Task 4: 驱动组件（SPI + 面板协议）

**Files:**
- Create: `components/epd_gdem042f86/CMakeLists.txt`
- Create: `components/epd_gdem042f86/Kconfig`
- Create: `components/epd_gdem042f86/include/epd_gdem042f86.h`
- Create: `components/epd_gdem042f86/src/epd_gdem042f86.c`
- Modify: `main/CMakeLists.txt`（加入 `REQUIRES epd_gdem042f86`，Task 1 已写）

- [ ] **Step 1: 写组件 CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "src/epd_gdem042f86.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_spi esp_driver_gpio esp_common
)
```

- [ ] **Step 2: 写 Kconfig**

```
menu "EPaperFrame"

config EPD_PIN_SCK
    int "EPD SPI SCLK GPIO"
    default 12

config EPD_PIN_MOSI
    int "EPD SPI MOSI GPIO"
    default 11

config EPD_PIN_CS
    int "EPD chip select GPIO"
    default 10

config EPD_PIN_DC
    int "EPD data/command GPIO"
    default 13

config EPD_PIN_RST
    int "EPD reset GPIO"
    default 14

config EPD_PIN_BUSY
    int "EPD busy GPIO"
    default 21

config EPD_SPI_CLOCK_HZ
    int "EPD SPI clock (Hz)"
    default 10000000

config EPD_FB_IN_PSRAM
    bool "Allocate frame buffer in PSRAM"
    default n

config EPD_AUTORUN
    bool "Run test sequence on boot"
    default y

endmenu
```

- [ ] **Step 3: 写公开头文件**

`components/epd_gdem042f86/include/epd_gdem042f86.h`：

```c
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

/* 使用 Kconfig 默认引脚与频率（见 epd_default_config） */
void epd_default_config(epd_config_t *cfg);

esp_err_t epd_new(const epd_config_t *cfg, epd_handle_t *out);
esp_err_t epd_del(epd_handle_t h);

uint8_t *epd_fb(epd_handle_t h);

void epd_fb_fill(epd_handle_t h, epd_color_t c);
void epd_fb_set_px(epd_handle_t h, int x, int y, epd_color_t c);

/* 完整体验一次：复位 -> 初始化 -> 写帧 -> 刷新 -> 休眠 */
esp_err_t epd_refresh(epd_handle_t h, epd_mode_t mode, uint32_t *elapsed_ms);
esp_err_t epd_sleep(epd_handle_t h);

int epd_busy_level(epd_handle_t h);
esp_err_t epd_set_clock(epd_handle_t h, int clock_hz);
int epd_clock_hz(epd_handle_t h);
```

- [ ] **Step 4: 写驱动实现**

`components/epd_gdem042f86/src/epd_gdem042f86.c`：

```c
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
    const uint8_t tres[4] = {
        (uint8_t)(EPD_WIDTH / 256), (uint8_t)(EPD_WIDTH % 256),
        (uint8_t)(EPD_HEIGHT / 256), (uint8_t)(EPD_HEIGHT % 256),
    };
    static const uint8_t gate[2] = {0x64, 0x53};
    static const uint8_t gsst[4] = {0x00, 0x00, 0x00, 0x00};
    static const uint8_t pll[1] = {0x08};
    static const uint8_t e9[1] = {0x01};

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
             EPD_WIDTH, EPD_HEIGHT, h->fb,
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
```

- [ ] **Step 5: 编译**

Run:
```bash
idf.py build
```
Expected: `Project build complete.`，且日志中无 `warning:`

- [ ] **Step 6: 提交**

```bash
git add components/epd_gdem042f86
git commit -m "feat(epd): SSD2683 SPI 驱动：初始化、写帧、刷新、休眠"
```

---

### Task 5: Worker + Console + 自动序列

**Files:**
- Create: `main/epd_worker.h`, `main/epd_worker.c`
- Create: `main/epd_console.h`, `main/epd_console.c`
- Modify: `main/main.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: 写 worker 头文件**

`main/epd_worker.h`：

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "epd_gdem042f86.h"
#include "epd_patterns.h"

typedef struct {
    epd_pattern_id_t pattern;
    epd_mode_t mode;
    epd_color_t solid; /* 仅 EPD_PATTERN_SOLID 使用 */
    bool bench;        /* true: 同图案先 full 再 fast */
} epd_job_t;

esp_err_t epd_worker_start(void);
epd_handle_t epd_worker_handle(void);

/* 最新命令覆盖尚未开始的旧命令 */
esp_err_t epd_worker_submit(const epd_job_t *job);

/* 提交并等待完成（自动序列使用） */
esp_err_t epd_worker_submit_wait(const epd_job_t *job, uint32_t timeout_ms);

void epd_worker_autorun(void);
```

- [ ] **Step 2: 写 worker 实现**

`main/epd_worker.c`：

```c
#include "epd_worker.h"

#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "epd_worker";

static epd_handle_t s_epd;
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_done;

static const char *mode_name(epd_mode_t m)
{
    return m == EPD_MODE_FAST ? "fast" : "full";
}

static esp_err_t run_job(const epd_job_t *job)
{
    epd_pattern_render(epd_fb(s_epd), job->pattern, job->solid);

    if (job->bench) {
        uint32_t full_ms = 0;
        uint32_t fast_ms = 0;
        esp_err_t err = epd_refresh(s_epd, EPD_MODE_FULL, &full_ms);
        if (err == ESP_OK) {
            err = epd_refresh(s_epd, EPD_MODE_FAST, &fast_ms);
        }
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "bench %s: full=%" PRIu32 " ms, fast=%" PRIu32 " ms",
                     epd_pattern_name(job->pattern), full_ms, fast_ms);
        }
        return err;
    }

    uint32_t ms = 0;
    esp_err_t err = epd_refresh(s_epd, job->mode, &ms);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s %s: %" PRIu32 " ms", epd_pattern_name(job->pattern),
                 mode_name(job->mode), ms);
    } else {
        ESP_LOGE(TAG, "%s %s failed: %s", epd_pattern_name(job->pattern),
                 mode_name(job->mode), esp_err_to_name(err));
    }
    return err;
}

static void epd_worker_task(void *arg)
{
    (void)arg;
    epd_job_t job;
    for (;;) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) == pdTRUE) {
            (void)run_job(&job);
            xSemaphoreGive(s_done);
        }
    }
}

esp_err_t epd_worker_start(void)
{
    epd_config_t cfg;
    epd_default_config(&cfg);
    ESP_RETURN_ON_ERROR(epd_new(&cfg, &s_epd), TAG, "epd_new");

    s_queue = xQueueCreate(1, sizeof(epd_job_t));
    s_done = xSemaphoreCreateBinary();
    if (s_queue == NULL || s_done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(epd_worker_task, "epd_worker", 6144, NULL, 5, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create");
    return ESP_OK;
}

epd_handle_t epd_worker_handle(void)
{
    return s_epd;
}

esp_err_t epd_worker_submit(const epd_job_t *job)
{
    ESP_RETURN_ON_FALSE(job != NULL, ESP_ERR_INVALID_ARG, TAG, "null job");
    return xQueueOverwrite(s_queue, job) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t epd_worker_submit_wait(const epd_job_t *job, uint32_t timeout_ms)
{
    ESP_RETURN_ON_ERROR(epd_worker_submit(job), TAG, "submit");
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "wait timeout after %" PRIu32 " ms", timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void epd_worker_autorun(void)
{
    const epd_job_t jobs[] = {
        {.pattern = EPD_PATTERN_SOLID, .mode = EPD_MODE_FULL, .solid = EPD_COLOR_WHITE},
        {.pattern = EPD_PATTERN_COLORS, .mode = EPD_MODE_FULL, .solid = EPD_COLOR_WHITE},
        {.pattern = EPD_PATTERN_PHOTO, .mode = EPD_MODE_FAST, .solid = EPD_COLOR_WHITE},
    };
    ESP_LOGI(TAG, "autorun: %d steps", (int)(sizeof(jobs) / sizeof(jobs[0])));
    for (size_t i = 0; i < sizeof(jobs) / sizeof(jobs[0]); i++) {
        esp_err_t err = epd_worker_submit_wait(&jobs[i], 60000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "autorun step %u failed: %s", (unsigned)i, esp_err_to_name(err));
        }
    }
    ESP_LOGI(TAG, "autorun done");
}
```

- [ ] **Step 3: 写 console**

`main/epd_console.h`：

```c
#pragma once

#include "esp_err.h"

esp_err_t epd_console_start(void);
```

`main/epd_console.c`：

```c
#include "epd_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"

#include "epd_gdem042f86.h"
#include "epd_patterns.h"
#include "epd_worker.h"

static const char *TAG = "console";

static void print_usage(void)
{
    printf("usage:\n");
    printf("  epd info\n");
    printf("  epd solid <black|white|yellow|red> [full|fast]\n");
    printf("  epd colors|checker|bars|grid|photo [full|fast]\n");
    printf("  epd bench [colors|checker|grid|photo]\n");
    printf("  epd clk <hz>\n");
    printf("  epd sleep\n");
}

static bool parse_mode(const char *s, epd_mode_t *out)
{
    if (s == NULL || strcmp(s, "full") == 0) {
        *out = EPD_MODE_FULL;
        return true;
    }
    if (strcmp(s, "fast") == 0) {
        *out = EPD_MODE_FAST;
        return true;
    }
    return false;
}

static bool parse_color(const char *s, epd_color_t *out)
{
    static const struct {
        const char *name;
        epd_color_t color;
    } k_map[] = {
        {"black", EPD_COLOR_BLACK},
        {"white", EPD_COLOR_WHITE},
        {"yellow", EPD_COLOR_YELLOW},
        {"red", EPD_COLOR_RED},
    };
    for (size_t i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++) {
        if (strcmp(s, k_map[i].name) == 0) {
            *out = k_map[i].color;
            return true;
        }
    }
    return false;
}

static int do_info(void)
{
    epd_handle_t h = epd_worker_handle();
    printf("panel      : GDEM042F86 %dx%d, 2bpp, fb=%d bytes\n",
           EPD_WIDTH, EPD_HEIGHT, EPD_FB_SIZE);
    printf("spi clock  : %d Hz\n", epd_clock_hz(h));
    printf("busy level : %d (1 = idle)\n", epd_busy_level(h));
    printf("framebuffer: %p\n", (void *)epd_fb(h));
    return 0;
}

static int cmd_epd(int argc, char **argv)
{
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *sub = argv[1];
    epd_job_t job = {
        .pattern = EPD_PATTERN_SOLID,
        .mode = EPD_MODE_FULL,
        .solid = EPD_COLOR_WHITE,
        .bench = false,
    };

    if (strcmp(sub, "info") == 0) {
        return do_info();
    }
    if (strcmp(sub, "sleep") == 0) {
        esp_err_t err = epd_sleep(epd_worker_handle());
        printf("sleep: %s\n", esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "clk") == 0) {
        if (argc < 3) {
            printf("current clock: %d Hz\n", epd_clock_hz(epd_worker_handle()));
            return 0;
        }
        esp_err_t err = epd_set_clock(epd_worker_handle(), atoi(argv[2]));
        printf("set clock %d: %s\n", atoi(argv[2]), esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "solid") == 0) {
        if (argc < 3 || !parse_color(argv[2], &job.solid)) {
            printf("invalid color\n");
            return 1;
        }
        job.pattern = EPD_PATTERN_SOLID;
        if (argc >= 4 && !parse_mode(argv[3], &job.mode)) {
            printf("invalid mode (full|fast)\n");
            return 1;
        }
        ESP_LOGI(TAG, "enqueue solid %s %s", argv[2], job.mode == EPD_MODE_FAST ? "fast" : "full");
        return epd_worker_submit(&job) == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "bench") == 0) {
        epd_pattern_id_t id = EPD_PATTERN_COLORS;
        if (argc >= 3 && !epd_pattern_lookup(argv[2], &id)) {
            printf("unknown pattern\n");
            return 1;
        }
        job.pattern = id;
        job.bench = true;
        return epd_worker_submit(&job) == ESP_OK ? 0 : 1;
    }

    epd_pattern_id_t id;
    if (!epd_pattern_lookup(sub, &id)) {
        printf("unknown subcommand: %s\n", sub);
        print_usage();
        return 1;
    }
    job.pattern = id;
    if (argc >= 3 && !parse_mode(argv[2], &job.mode)) {
        printf("invalid mode (full|fast)\n");
        return 1;
    }
    ESP_LOGI(TAG, "enqueue %s %s", sub, job.mode == EPD_MODE_FAST ? "fast" : "full");
    return epd_worker_submit(&job) == ESP_OK ? 0 : 1;
}

esp_err_t epd_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "epd>";
    repl_cfg.max_cmdline_length = 128;

    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl), TAG, "repl init");

    esp_console_cmd_t cmd = {
        .command = "epd",
        .help = "EPD test commands (run 'epd' for usage)",
        .hint = NULL,
        .func = &cmd_epd,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&cmd), TAG, "cmd register");
    esp_console_register_help_command();

    return esp_console_start_repl(repl);
}
```

- [ ] **Step 4: 改写 main/main.c**

```c
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
```

- [ ] **Step 5: 更新 main/CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "main.c" "epd_worker.c" "epd_console.c" "epd_patterns.c" "epd_text.c"
    INCLUDE_DIRS "." "assets"
    REQUIRES epd_gdem042f86 esp_console
)
```

- [ ] **Step 6: 编译**

Run: `idf.py build`
Expected: `Project build complete.`，无 warning。

- [ ] **Step 7: 提交**

```bash
git add main
git commit -m "feat(app): 刷新 worker、esp_console 命令与上电自动序列"
```

---

### Task 6: 上板实测与文档

**Files:**
- Create: `README.md`
- Modify: `docs/superpowers/specs/2026-10-28-epaper-frame-bringup-design.md`（追加实测结果章节）

- [ ] **Step 1: 确认串口设备**

Run: `ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null`
Expected: 出现一个设备（例如 `/dev/ttyUSB0`）。若为空，请插入主控板 USB 后重试。

- [ ] **Step 2: 烧录并观察自动序列**

Run:
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```
Expected:
```
I (xxx) epd: init ok: 400x300, fb=0x... (internal), spi=10000000 Hz, busy=1
I (xxx) epd_worker: autorun: 3 steps
I (xxx) epd: refresh done: mode=full xxxxx ms
I (xxx) epd_worker: solid full: xxxxx ms
...
I (xxx) app: console ready, type 'epd' for commands
```
屏幕依次显示：全白 → 四象限色块 → 官方样图。

- [ ] **Step 3: 逐项验收并记录耗时**

在 console 依次执行并记录：

```
epd colors full
epd checker full
epd bars full
epd grid full
epd photo full
epd bench grid
```

判定标准（来自设计文档第 10 节）：

| # | 检查项 | 通过条件 |
|---|---|---|
| 1 | 构建 | 零 error / 零 warning |
| 2 | 耗时 | full 20s ±4s；fast 12s ±3s |
| 3 | 颜色 | `colors` 四象限颜色正确 |
| 4 | 位序 | `checker` 无 4px 竖条纹 |
| 5 | 几何 | `grid` 线距相等、外框闭合、横向 400x300 |
| 6 | 样图 | `photo` 与官方样张一致 |
| 7 | 休眠 | 每次刷新后屏幕保持、busy=1 |
| 8 | 时序余量 | `epd clk 20000000` 后再刷一次仍正常 |

- [ ] **Step 4: 写 README.md**

`README.md`：

```markdown
# EPaperFrame Firmware

ESP32-S3（ESP32-S3-WROOM-1-N16R8）+ 4.2" 四色墨水屏 GDEM042F86（400x300，SSD2683ZA）。

## 环境

- ESP-IDF v5.5.5：`/home/miaojianyu/esp/ESP_IDF/esp_idf_5.5/esp-idf`
- 目标：esp32s3，16MB flash，8MB Octal PSRAM

## 构建与烧录

```bash
. /home/miaojianyu/esp/ESP_IDF/esp_idf_5.5/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## 硬件连接（已由原理图确认）

| EPD 信号 | GPIO |
|---|---|
| SCK | 12 |
| MOSI | 11 |
| CS | 10 |
| DC | 13 |
| RST | 14 |
| BUSY | 21（高=空闲）|

控制台：USB 转 CH340K，UART0，115200。

## 命令

```
epd info
epd solid <black|white|yellow|red> [full|fast]
epd colors|checker|bars|grid|photo [full|fast]
epd bench [colors|checker|grid|photo]
epd clk <hz>
epd sleep
```

## 主机端单测

```bash
./tests/host/run.sh
```

## 资料

`Docs/datasheets/`（面板与 IC 规格书）、`Docs/demos/`（官方 ESP32 / STM32 例程）。
设计文档：`docs/superpowers/specs/2026-10-28-epaper-frame-bringup-design.md`。
```

- [ ] **Step 5: 把实测结果写回设计文档**

在 `docs/superpowers/specs/2026-10-28-epaper-frame-bringup-design.md` 末尾追加一节：

```markdown
## 12. 实测结果（YYYY-MM-DD）

| 项 | 结果 |
|---|---|
| full 刷新耗时 | ... ms |
| fast 刷新耗时 | ... ms |
| colors 颜色 | 通过 / 失败 |
| checker 位序 | 通过 / 失败 |
| grid 几何 | 通过 / 失败 |
| photo 样图 | 通过 / 失败 |
| SPI 20MHz | 通过 / 失败 |

备注：...
```

（把 `YYYY-MM-DD` 与 `...` 换成当次实测数据，不要留占位符。）

- [ ] **Step 6: 提交**

```bash
git add README.md docs/superpowers/specs
git commit -m "docs: bring-up 实测结果与使用说明"
```

---

## 自检

- 规格覆盖：驱动事务（Task 4）、图案（Task 3）、console/worker/自动序列（Task 5）、验收（Task 6）、构建配置（Task 1）均有对应任务。
- 占位符：仅 Task 6 的结果表格需要在实测后填写，属于数据记录而非未完成设计。
- 类型一致性：`epd_pattern_id_t` / `epd_color_t` / `epd_mode_t` / `epd_job_t` 在 Task 2/3/4/5 中定义一次并复用；`epd_refresh(h, mode, elapsed_ms)` 签名在头文件、实现、调用处一致。
