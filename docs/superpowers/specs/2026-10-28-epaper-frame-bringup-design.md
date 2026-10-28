# EPaperFrame 固件 Bring-up 设计

- 日期：2026-10-28
- 阶段：第一阶段（硬件验证，方案 A）
- 硬件：EPaperFrame 主控板（ESP32-S3-WROOM-1-N16R8）+ 屏幕板（GDEM042F86 4.2" 四色墨水屏）
- 工具链：ESP-IDF v5.5.5（`/home/miaojianyu/esp/ESP_IDF/esp_idf_5.5/esp-idf`）

## 1. 目标

在真实硬件上跑通 GDEM042F86，验证 SPI 时序、像素打包、刷新耗时与休眠，为后续
（图像解码、WiFi 拉图、深睡眠相框）建立可靠的驱动基础。

非目标（本期不做）：LVGL、局部刷新、图片解码/抖动、WiFi、OTA、多面板适配。

## 2. 硬件事实（来自原理图与规格书）

| 项 | 值 |
|---|---|
| 面板 | GDEM042F86，4.2"，400×300，黑/白/红/黄（BWRY）|
| 驱动 IC | SSD2683ZA，波形存于 OTP（固件不下 LUT）|
| 色深 | 2 bit/像素，帧缓冲 400×300/4 = **30,000 字节**，行内 4 像素/字节，高位在前 |
| 颜色编码 | `00`=黑 `01`=白 `10`=黄 `11`=红 |
| 接口 | 4-wire SPI（CS/DC/SCK/MOSI）+ RST + BUSY，Mode 0，MSB first |
| 电源 | VCI/VDDIO 2.3–3.6V（typ 3.0V）；板载 3.3V |
| 电流/耗时 | 典型 4mA；全刷 20s，快刷 12s；深睡 <1µA |
| 升压 | 屏内 IC 通过 GDR(pin2)/RESE(pin3) 驱动屏幕板上的 Q4+L2+D4/D7/D8 |

GPIO 映射（已用 EDA 网表核对）：

| 信号 | GPIO | 备注 |
|---|---|---|
| EPD_SCK | IO12 | |
| EPD_MOSI | IO11 | |
| EPD_CS | IO10 | |
| EPD_DC | IO13 | |
| EPD_RST | IO14 | |
| EPD_BUSY | IO21 | 高电平 = 空闲 |
| U0TXD/U0RXD | IO43/IO44 | → CH340K，console |
| BOOT | IO0 | 按键（本期不绑定功能）|

## 3. 架构

```
console task ──enqueue{pattern,mode}──► epd worker task ──► epd_refresh()
   (esp_console)                          (串行执行)          │
                                                             └─ 日志: pattern/mode/elapsed_ms
```

- **一次刷新 = 一个完整事务**：硬件复位 → init（full 或 fast）→ `0x10` 写 30KB →
  `0x12` 刷新 → 等 BUSY → `0x02` power off → `0x07 0xA5` deep sleep。
  依据：规格书 10.1/10.2 与官方例程，IC 每次全屏刷新后必须重新初始化。
- **驱动持有帧缓冲**，默认内部 RAM（配置可切换 PSRAM）。
- **刷新在独立 worker task 执行**，console 只入队，避免 20s 阻塞与看门狗问题。
- 队列深度 1（覆盖语义）：新命令丢弃尚未开始的旧命令。

## 4. 目录结构

```
epaper-frame/
├── CMakeLists.txt
├── sdkconfig.defaults
├── README.md
├── Docs/                          # 规格书、SSD2683 手册、官方例程（已下载）
├── components/epd_gdem042f86/
│   ├── CMakeLists.txt
│   ├── Kconfig
│   ├── include/epd_gdem042f86.h
│   └── src/epd_gdem042f86.c
├── main/
│   ├── CMakeLists.txt
│   ├── main.c
│   ├── epd_console.c / .h
│   ├── epd_patterns.c / .h
│   ├── font8x8.h
│   └── assets/gimage_1.h
└── docs/superpowers/specs/...
```

## 5. 驱动 API

```c
#define EPD_WIDTH   400
#define EPD_HEIGHT  300
#define EPD_FB_SIZE 30000

typedef enum { EPD_COLOR_BLACK=0, EPD_COLOR_WHITE=1,
               EPD_COLOR_YELLOW=2, EPD_COLOR_RED=3 } epd_color_t;
typedef enum { EPD_MODE_FULL=0, EPD_MODE_FAST=1 } epd_mode_t;

typedef struct {
    int  pin_sck, pin_mosi, pin_cs, pin_dc, pin_rst, pin_busy;
    int  spi_host;
    int  clock_hz;
    bool fb_in_psram;
} epd_config_t;

esp_err_t epd_new(const epd_config_t *cfg, epd_handle_t *out);
esp_err_t epd_del(epd_handle_t h);
uint8_t  *epd_fb(epd_handle_t h);
void      epd_fb_fill(epd_handle_t h, epd_color_t c);
void      epd_fb_set_px(epd_handle_t h, int x, int y, epd_color_t c);
esp_err_t epd_refresh(epd_handle_t h, epd_mode_t mode, uint32_t *elapsed_ms);
esp_err_t epd_sleep(epd_handle_t h);
```

- 全部接口无全局状态，可多实例。
- BUSY 等待必须带超时：full 40s / fast 25s，超时返回 `ESP_ERR_TIMEOUT`。
- `epd_refresh` 内部处理 reset + init，调用者无需关心初始化状态。

## 6. 测试图案

| 名称 | 内容 | 验证点 |
|---|---|---|
| `solid <c>` | 全屏纯色 | 均匀性、鬼影 |
| `colors` | 四象限色块 + 中心十字 | 颜色映射 |
| `checker` | 1/2/4px 三级棋盘 | 字节内位序（错则呈 4px 竖条）|
| `bars` | 1/4/8px 四色竖条 + 边框 | 列对齐 |
| `grid` | 20px 网格 + 10px 外框 + 四角标记 | 几何与朝向 |
| `photo` | 官方 gImage_1 样图 | 全链路 |
| `bench` | 同图案 full + fast | 耗时达标 |

图案生成函数只操作帧缓冲，不依赖驱动内部实现，便于后续被图像管线复用。

## 7. Console 命令

```
epd help
epd info
epd solid <black|white|yellow|red> [full|fast]
epd colors|checker|bars|grid|photo [full|fast]
epd bench [colors|checker|grid|photo]
epd clk <hz>
epd sleep
```

## 8. 上电自动序列

`EPD_AUTORUN`（Kconfig，默认开）：`solid white`(full) → `colors`(full) → `photo`(fast)，
约 52s，然后进入 console 提示符。自动序列与命令走同一 worker 队列。

## 9. 构建配置

`sdkconfig.defaults`：

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200
CONFIG_FREERTOS_HZ=1000
CONFIG_COMPILER_WARN_WRITE_STRINGS=y
```

分区表：默认 single-app；16MB flash 暂不划分图片分区。

## 10. 验收标准

1. `idf.py build` 零 error、零 warning。
2. 串口日志给出实测耗时：full 20s ±4s，fast 12s ±3s。
3. `colors` 四色正确；`checker` 不出现 4px 竖条纹。
4. `grid` 线距相等、外框闭合、方向为 400 宽 × 300 高。
5. `photo` 与官方样张一致。
6. 每次刷新后面板进 deep sleep，图像保持，BUSY 释放。
7. `epd clk 20000000` 正常（作为时序余量参考）。

## 11. 风险与对策

| 风险 | 对策 |
|---|---|
| 位序/字节序搞错 → 画面呈 4px 竖条 | `checker` 图案专门覆盖，出错一眼可辨 |
| 20s 刷新阻塞导致 TWDT 复位 | 刷新放独立 task 且该 task 不订阅 TWDT；console task 不做长操作 |
| BUSY 极性判断错 → 立即返回、画面花 | 先读一次 BUSY 电平打日志，再实现等待 |
| 屏幕板 3V3 由磁吸供电接触不良 | 日志包含刷新前后命令返回值；失败时报错而非静默 |
| 首次烧录无串口设备 | 需要用户插入 USB；构建阶段可先完成并通过静态检查 |
