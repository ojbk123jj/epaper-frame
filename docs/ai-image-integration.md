# ESP32 云端生图接入

已实现 ESP32 直接调用魔搭 ModelScope 的 API-Inference，默认模型 `Qwen/Qwen-Image-2.1-Turbo`。
提示词由网页通过 BLE 提交，设备自行联网生成并直接刷屏；不再上电自动生成。
串口 `ai` 命令保留作为调试入口。生成成功后网页可通过 BLE 回读 30,000 字节四色屏幕帧。

实现按 ESP-IDF 组件划分：`ble_image` 接收提示词并上报 AI 状态/回读屏幕帧，
`frame_service` 协调 AI/BLE 共享任务流程，`ai_service` 保留串口入口，
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
| `APP_AI_MODEL` | `Qwen/Qwen-Image-2.1-Turbo` | 生图模型；Turbo 为 8 步蒸馏版本 |
| `APP_AI_SIZE` | `768x576` | 请求尺寸 |
| `APP_AI_TIMEOUT_MS` | `600000` | 云端生成轮询上限，默认 10 分钟 |
| `APP_AI_PROMPT` | 四色猫咪插画 | 串口 `ai` 无参数时的默认提示词 |
| `APP_AI_FAST_REFRESH` | `0` | 默认全刷，设为 `1` 改用快刷 |

凭据随固件存储，忽略配置文件只防止 Git 提交，不是固件加密机制。
更换模型时需要确认其异步 API 协议、尺寸和图片格式兼容本实现。

## 服务与额度

截至 2026-11-10，官方规则说明该服务面向开发者免费体验，需要绑定已实名认证的阿里云账号，
使用 Access Token 鉴权。当前使用魔粒兑换调用，模型可用性、速率及免费额度以登录后的平台为准，
不能把旧的“每日免费 2000 次”作为生图保证。该服务不承诺生产 SLA。

- [API 推理介绍与异步生图示例](https://modelscope.cn/docs/model-service/API-Inference/intro)
- [使用限制](https://modelscope.cn/docs/model-service/API-Inference/limits)
- [Qwen-Image-2.1-Turbo 模型页](https://www.modelscope.cn/models/Qwen/Qwen-Image-2.1-Turbo)
- [Access Token 管理](https://modelscope.cn/my/myaccesstoken)

## 执行流程

1. 网页通过 `op=4` AI BEGIN + Data 分块 + `op=5` AI COMMIT 上传提示词；固件校验长度和 CRC 后，
   `frame_service_submit_ai_id()` 检查配置和忙状态，接收任务。串口显示 `queued`。
   串口 `ai` 命令走同一路径，由固件分配任务号。
2. `connecting`：连接 Wi-Fi，最多 30 秒；首次通过 `ntp.aliyun.com` 校时，最多 20 秒。
   HTTPS 使用 CA 证书包验证服务端证书，时间无效时不跳过验证。
3. `generating`：请求 `POST https://api-inference.modelscope.cn/v1/images/generations`，
   JSON 包含 `model`、`prompt`、`size`；设置 `Authorization: Bearer <token>` 和
   `X-ModelScope-Async-Mode: true`。
4. 取得 `task_id` 后每 5 秒查询 `GET /v1/tasks/{task_id}`，设置
   `X-ModelScope-Task-Type: image_generation`。总生图等待上限 10 分钟，成功状态为 `SUCCEED`。
   串口会打印 `task_id`，以及每轮 `task_status`（PENDING/RUNNING/SUCCEED/FAILED）和已用时间，便于判断是排队还是生成慢。
5. `downloading`：下载 `output_images[0]`。只接受 HTTPS，最多跟随 3 次绝对 HTTPS 重定向。
   下载请求不携带 ModelScope Token；响应累计最多 2MiB。
6. 下载后停止 Wi-Fi，刷屏前停止并反初始化 BLE 控制器，完成后重建服务并恢复广播。`converting`：PNG/JPEG 解码到 PSRAM，透明背景合成白色，
   居中裁剪为 4:3，最近邻缩放到 400×300，通过 Floyd–Steinberg 抖动量化为黑白黄红。
7. `displaying`：提交 30,000 字节 2bpp 数据给 `epaper_display_show()`。
   保留固定 8 秒恢复保护及完整 BUSY 周期检查，默认全刷。
8. 驱动返回成功才进入 `succeeded` 并增加 `displayed`，同时保留本次屏幕帧供网页回读；
   任一步出错进入 `failed`，记录 `last_result`，释放图片缓冲区并停止 Wi-Fi，等待下一次网页或串口请求。
9. 网页在 AI 生成期间每约 1 秒读取 AI Status，得到排队、联网、生成、下载、转换、刷屏阶段和已用时间。
   刷屏会短暂断开 BLE，恢复后读取终态。成功后通过 `op=6` 和 Preview Data 特征分块读回 30,000 字节屏幕帧。

单次 HTTP 操作超时最多 30 秒，图片下载每跳总时限 60 秒。
图片只支持 PNG 和 JPEG（含渐进 JPEG），单边最多 2048、总像素最多 1,048,576。
过大的图片会拒绝，避免耗尽 8MB PSRAM。默认尺寸在限制内。

## 网页与串口测试

网页流程见 [网页换图说明](ble-web.md)。串口调试仍可输入：

```text
ai status
ai "黑白红黄四色插画，一座海边灯塔，大块色彩，无文字，横向4:3"
ai
```

上电不再自动生成；先用网页提交一次，确认页面依次显示
`connecting → generating → downloading → converting → displaying`，
最后显示成功并可读回预览。也可用不同提示词连续生成两张，检查 `ai status` 的 `displayed`
逐次增加，并目视确认屏幕图案变化。生成期间再次提交会被拒绝；等待当前任务结束再试。

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
| `failed at generating` | 生图任务失败、响应协议不兼容或超过 10 分钟；不会自动重新提交。查看 `task_id` 和 `task_status` 判断是排队还是生成慢 |
| `failed at downloading` | 图像地址、HTTPS 证书、网络中断或超过 2MiB |
| `failed at converting` | 图片格式、分辨率限制或 PSRAM 不足 |
| `failed at displaying` | 查看面板驱动的 SPI/BUSY 错误及实际屏幕状态 |

请求提交后遇到超时，云端可能已接受该任务；为避免重复消耗额度，代码不自动重发提交请求。
当前任务 ID 和屏幕帧预览不跨重启保存；重启后需要从网页重新生成。生成中关闭页面不会中断相框，
但重新打开页面后如果相框已重启，就无法再确认旧任务。

离线回归涵盖驱动保护、显示接口、实际 PNG/JPEG 解码、图像转换、提示词接收、协议解析、HTTP 边界与鉴权隔离。
编译通过和离线测试不能代替填入有效凭据后的云端、网络和实屏联调。
