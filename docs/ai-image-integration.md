# ESP32 云端生图接入

已实现 ESP32 直接调用魔搭 ModelScope 的 API-Inference，默认模型 `Qwen/Qwen-Image`。
启动生成一张，之后通过 UART 串口输入提示词。无需手机或中转服务器。

实现按 ESP-IDF 组件划分：`ai_console` 接收提示词，`ai_service` 保留串口入口，`frame_service` 协调 AI/BLE 共享任务流程，
`modelscope_client` 封装云端请求与轮询，`network_service` 管理联网和校时，
`image_processing` 处理图像，`epaper_display` 提交显示，`epd_gdem042f86` 驱动面板。
`main` 只负责初始化和启动任务，组件接口与依赖见 [代码结构](../README.md#代码结构)。

## 本地配置

在 `config/local_config.h` 填写 Wi-Fi 名称、密码和 Access Token，重新编译并烧录。
该文件缺失时，CMake 会从 `config/local_config.example.h` 创建空凭据模板；已有文件不会覆盖。
Token 不加 `Bearer ` 前缀，不在串口打印。Wi-Fi 使用 2.4GHz，密码需至少 8 字节，或留空连接开放网络。
默认 Wi-Fi 安全门槛为 WPA2。

| 配置 | 默认值 | 用途 |
|---|---|---|
| `APP_WIFI_SSID` | 空 | Wi-Fi 名称 |
| `APP_WIFI_PASSWORD` | 空 | Wi-Fi 密码 |
| `APP_MODELSCOPE_TOKEN` | 空 | ModelScope Access Token |
| `APP_AI_MODEL` | `Qwen/Qwen-Image` | 生图模型 |
| `APP_AI_SIZE` | `768x576` | 请求尺寸 |
| `APP_AI_PROMPT` | 四色猫咪插画 | 启动及 `ai` 默认提示词 |
| `APP_AI_GENERATE_ON_BOOT` | `1` | 每次启动自动生成，设为 `0` 可禁用 |
| `APP_AI_FAST_REFRESH` | `0` | 默认全刷，设为 `1` 改用快刷 |

凭据随固件存储，忽略配置文件只防止 Git 提交，不是固件加密机制。
更换模型时需要确认其异步 API 协议、尺寸和图片格式兼容本实现。

## 服务与额度

截至 2026-11-10，官方规则说明该服务面向开发者免费体验，需要绑定已实名认证的阿里云账号，
使用 Access Token 鉴权。当前使用魔粒兑换调用，模型可用性、速率及免费额度以登录后的平台为准，
不能把旧的“每日免费 2000 次”作为生图保证。该服务不承诺生产 SLA。

- [API 推理介绍与异步生图示例](https://modelscope.cn/docs/model-service/API-Inference/intro)
- [使用限制](https://modelscope.cn/docs/model-service/API-Inference/limits)
- [Qwen-Image 模型页](https://www.modelscope.cn/models/Qwen/Qwen-Image)
- [Access Token 管理](https://modelscope.cn/my/myaccesstoken)

## 执行流程

1. `ai_service_submit()` 检查配置和忙状态，接收一个任务。串口显示 `queued`。
2. `connecting`：连接 Wi-Fi，最多 30 秒；首次通过 `ntp.aliyun.com` 校时，最多 20 秒。
   HTTPS 使用 CA 证书包验证服务端证书，时间无效时不跳过验证。
3. `generating`：请求 `POST https://api-inference.modelscope.cn/v1/images/generations`，
   JSON 包含 `model`、`prompt`、`size`；设置 `Authorization: Bearer <token>` 和
   `X-ModelScope-Async-Mode: true`。
4. 取得 `task_id` 后每 5 秒查询 `GET /v1/tasks/{task_id}`，设置
   `X-ModelScope-Task-Type: image_generation`。总生图等待上限 5 分钟，成功状态为 `SUCCEED`。
5. `downloading`：下载 `output_images[0]`。只接受 HTTPS，最多跟随 3 次绝对 HTTPS 重定向。
   下载请求不携带 ModelScope Token；响应累计最多 2MiB。
6. 下载后停止 Wi-Fi，刷屏前停止并反初始化 BLE 控制器，完成后重建服务并恢复广播。`converting`：PNG/JPEG 解码到 PSRAM，透明背景合成白色，
   居中裁剪为 4:3，最近邻缩放到 400×300，通过 Floyd–Steinberg 抖动量化为黑白黄红。
7. `displaying`：提交 30,000 字节 2bpp 数据给 `epaper_display_show()`。
   保留固定 8 秒恢复保护及完整 BUSY 周期检查，默认全刷。
8. 驱动返回成功才进入 `succeeded` 并增加 `displayed`；任一步出错进入 `failed`，
   记录 `last_result`，释放图片缓冲区并停止 Wi-Fi，等待下一次串口请求。

单次 HTTP 操作超时最多 30 秒，图片下载每跳总时限 60 秒。
图片只支持 PNG 和 JPEG（含渐进 JPEG），单边最多 2048、总像素最多 1,048,576。
过大的图片会拒绝，避免耗尽 8MB PSRAM。默认尺寸在限制内。

## 串口测试

```text
ai status
ai "黑白红黄四色插画，一座海边灯塔，大块色彩，无文字，横向4:3"
ai
```

先观察启动自动生成，再用不同提示词连续生成两张。
每次应经历 `connecting → generating → downloading → converting → displaying`，
最后出现 `image generation and display completed`。检查 `ai status` 的 `displayed` 逐次增加，
并目视确认屏幕图案变化。生成期间再次提交会被拒绝；等待当前任务结束再试。

`configured=yes` 只表示本地配置字段符合格式，并不表示 Token 或账户已经过云端验证。
`last_result` 是最近一次已完成任务的结果，当前任务进度看 `state` 和 `busy`。

## 故障定位

| 日志/现象 | 检查内容 |
|---|---|
| `configured=no` | 本地配置是否填写、是否重新编译烧录，SSID/密码/Token 长度是否合法 |
| `Wi-Fi connection failed` | 2.4GHz Wi-Fi 名称、密码、信号及安全模式 |
| `time synchronization failed` | 网络是否允许访问 NTP，是否能访问外网 |
| HTTP 401/403 | Token、账号绑定、模型调用权限 |
| HTTP 429 | 魔粒余额、模型配额及调用频率 |
| `failed at generating` | 生图任务失败、响应协议不兼容或超过 5 分钟；不会自动重新提交 |
| `failed at downloading` | 图像地址、HTTPS 证书、网络中断或超过 2MiB |
| `failed at converting` | 图片格式、分辨率限制或 PSRAM 不足 |
| `failed at displaying` | 查看面板驱动的 SPI/BUSY 错误及实际屏幕状态 |

请求提交后遇到超时，云端可能已接受该任务；为避免重复消耗额度，代码不自动重发提交请求。
当前任务 ID 不跨重启保存。重启会按启动配置重新生成，调试时可关闭启动生成。

离线回归涵盖驱动保护、显示接口、实际 PNG/JPEG 解码、图像转换、协议解析、HTTP 边界与鉴权隔离。
编译通过和离线测试不能代替填入有效凭据后的云端、网络和实屏联调。
