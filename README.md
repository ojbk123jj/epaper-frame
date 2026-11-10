# EPaperFrame

ESP32-S3-WROOM-1-N16R8 + GDEM042F86 四色墨水屏（400×300，SSD2683ZA）。

## AI 生图与烧录

已接入 ESP32 直接调用魔搭 ModelScope 的 `Qwen/Qwen-Image`：联网、校时、异步生图、
下载 PNG/JPEG、裁剪缩放、四色抖动转换、刷新屏幕。默认启动生成一张，也支持串口提示词。

先在 [ModelScope](https://modelscope.cn/my/myaccesstoken) 获取 Access Token，按平台要求绑定
已实名认证的阿里云账号，并确认模型可调用且账户有可用魔粒。免费体验额度以账户显示为准。

编辑 `config/local_config.h`，填写以下三项（不存在时从 `config/local_config.example.h` 复制，
首次 CMake 配置也会自动创建）：

```c
#define APP_WIFI_SSID "你的2.4GHz Wi-Fi名称"
#define APP_WIFI_PASSWORD "你的Wi-Fi密码"
#define APP_MODELSCOPE_TOKEN "你的ModelScope Access Token"
```

Token 直接填原值，不加 `Bearer ` 前缀。该文件已加入 Git 忽略；凭据会编入本地固件，
请勿公开配置文件和含凭据的固件。无需把 Token 发到聊天里。

```bash
. /home/miaojianyu/esp/ESP_IDF/esp_idf_5.5/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

将串口路径改为实际设备。ESP-IDF v5.5.5，16MB Flash，8MB Octal PSRAM，
CH340K / UART0 / 115200。没有有效配置时跳过启动生图，保留串口控制台。

使用 `partitions.csv` 自定义分区表，应用分区为 3MiB，NVS 和 PHY 分区位置保持原值。
更新分区表后请使用上面的完整 `idf.py flash` 命令，同时烧录分区表和应用。

启动后使用 `APP_AI_PROMPT` 自动生成。首次等待联网、校时、生图及约 20 秒全刷，
生图轮询最多等待 5 分钟。串口 `frame>` 可输入：

```text
ai status
ai "黑白红黄四色插画，一座海边灯塔，简洁构图，大块色彩，无文字，横向4:3"
ai
```

`ai status` 查看进度；带提示词的 `ai` 生成新图片；单独 `ai` 使用配置中的默认提示词。
提示词最多 1024 个 UTF-8 字节，含空格时建议用双引号包裹。一次只执行一个任务，忙时拒绝新请求。
`state=succeeded` 且 `displayed` 增加表示驱动已完成本次刷新；仍需目视核对实际屏幕。

默认请求 `768x576`、全刷、开启四色抖动。在本地配置中可修改默认提示词，或设置
`APP_AI_GENERATE_ON_BOOT 0` 禁用启动生成，避免每次重启再次消耗云端额度。
详细流程、限制与故障说明见 [AI 接入说明](docs/ai-image-integration.md)。

## 显示接口

启动时调用 `epaper_display_init()`。业务任务将已转换的图像交给
`epaper_display_show(frame, length, mode)`，函数返回本次显示的真实结果。

- 输入：400×300、2bpp、共 30,000 字节，逐行排列，每字节最左像素占 bit7..6。
- 颜色：`00` 黑、`01` 白、`10` 黄、`11` 红。
- 支持 `EPD_MODE_FULL` 和 `EPD_MODE_FAST`。
- 图像先复制到显示层内部缓冲区；刷新期间并发提交返回 `ESP_ERR_INVALID_STATE`。
- 调用阻塞到刷新和关电完成，应从业务任务调用，不能从中断或网络事件回调直接调用。
- PNG/JPEG 解码、缩放和四色量化由上层完成，此接口只接收面板格式数据。

显示操作由独立 AI 业务任务调用，串口输入在生图和刷屏期间保持可用。

## 刷新保护

恢复间隔固定为 **8 秒**，由驱动自动补足关电或故障后的剩余等待时间。
已经过的待机时间计入该间隔；每次释放复位前仍保证至少 1 秒低电平复位，并等待 120ms 启动。

每次显示执行复位、初始化、上电、传图、刷新、关电/休眠，然后保持 RST 低电平。
上电、刷新、关电均检查完整 BUSY 周期并设有超时；失败立即返回错误并保持复位。
保持复位不等同于物理断电，也不保证深睡电流。

## 硬件连接

| 信号 | GPIO |
|---|---|
| SCK | 12 |
| MOSI | 11 |
| CS | 10 |
| DC | 13 |
| RST | 14 |
| BUSY | 21（高为空闲）|

引脚和 SPI 频率可通过 `idf.py menuconfig` → `EPaperFrame` 配置。

## 离线回归

```bash
./tests/host/run.sh
```

覆盖像素打包、连续显示与固定 8 秒恢复保护、BUSY/SPI 故障、关电顺序，以及业务接口的帧长度验证、
缓冲区复制、并发拒绝和错误返回；同时覆盖 PNG/JPEG 解码、透明背景、裁剪/四色转换、
ModelScope 请求和响应解析、HTTP 下载上限、凭据隔离与重定向，以及云端客户端的提交、轮询、
失败退出和总超时。这些模拟测试不替代云端和实屏验证。

## 代码结构

`main/` 只保留 `main.c` 和 `CMakeLists.txt`，负责初始化、组装组件和触发启动生成。
功能实现按 ESP-IDF 组件组织，每个组件的 `include/` 提供公开接口，`src/` 存放实现。

| 组件 | 职责 | 依赖的业务组件 |
|---|---|---|
| `app_config` | 默认配置与本地配置入口、生成缺失的本地配置模板 | 无 |
| `ai_console` | 串口提示词输入、任务提交和状态展示 | `ai_service`、`app_config` |
| `ai_service` | 单任务队列、状态管理、协调生图到显示 | 云端客户端、联网、图像处理、显示、配置 |
| `modelscope_client` | HTTPS 鉴权、请求构造、提交、轮询和图片下载 | `app_config` |
| `network_service` | Wi-Fi 连接、SNTP 校时、停止 Wi-Fi | `app_config` |
| `image_processing` | PNG/JPEG 解码、裁剪缩放、四色抖动及面板格式打包 | `epd_gdem042f86` 的尺寸和像素打包定义 |
| `epaper_display` | 业务帧校验、互斥、缓冲区复制和显示结果 | `epd_gdem042f86` |
| `epd_gdem042f86` | SPI/GPIO、面板时序、BUSY 检查和固定 8 秒恢复保护 | 无 |

`modelscope_client/private_include/` 内的 HTTP 和协议解析头文件只供组件内部使用，
业务层只调用 `modelscope_client.h`，不直接操作任务 ID、HTTP 头或 JSON 响应。
联网和图像处理组件不调用显示接口，由 `ai_service` 负责协调资源释放和执行顺序。
每个组件在自身 `CMakeLists.txt` 声明依赖，内部依赖使用 `PRIV_REQUIRES`。

Wi-Fi 和 Token 继续在 `config/local_config.h` 配置；移动组件不会覆盖已有配置。
`tests/host/` 存放离线回归，`Docs/` 保留面板/IC 规格书与厂家参考例程。

图像解码器使用 vendored stb_image，仅启用 PNG/JPEG；版本和许可见 `third_party/stb/README.md`。
