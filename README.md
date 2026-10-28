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

引脚与 SPI 频率可在 `idf.py menuconfig` → `EPaperFrame` 中修改；`EPD_AUTORUN` 控制上电
自动测试序列（默认开）。

## 命令

```
epd info
epd solid <black|white|yellow|red> [full|fast]
epd colors|checker|bars|grid|photo [full|fast]
epd bench [colors|checker|grid|photo]
epd clk <hz>
epd sleep
```

- `full`：全刷，约 20s；`fast`：快刷，约 12s。
- `bench`：同一图案先全刷再快刷，串口打印两次耗时。
- `clk`：运行时改 SPI 频率，用于探测时序余量（默认 10 MHz）。

## 主机端单测

```bash
./tests/host/run.sh
```

覆盖像素打包位序（最左像素占 bit7..6）、整字节色值、图案几何与文字绘制。

## 代码结构

```
components/epd_gdem042f86/   面板驱动（SPI/GPIO + SSD2683 协议）
main/epd_worker.c            刷新任务与命令队列（串行执行，避免阻塞 console）
main/epd_console.c           esp_console 命令
main/epd_patterns.c          测试图案
main/epd_text.c              8x8 字模绘制
```

## 资料

`Docs/datasheets/`（面板与 IC 规格书）、`Docs/demos/`（官方 ESP32 / STM32 例程）。
设计文档：`docs/superpowers/specs/2026-10-28-epaper-frame-bringup-design.md`。
实施计划：`docs/superpowers/plans/2026-10-28-epaper-frame-bringup.md`。
