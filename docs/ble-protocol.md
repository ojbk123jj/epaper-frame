# BLE 图片与 AI 协议 v1

目标：ESP32-S3 + GDEM042F86，400×300，2bpp，30,000 字节；00 黑、01 白、10 黄、11 红；逐行高位像素优先。多字节字段均为小端，无文本/Base64 包装。IEEE CRC32（多项式 0xEDB88320，初值与最终异或 0xFFFFFFFF）；`123456789` 校验值 0xCBF43926。

广播名称 `EPaperFrame`，主广播包含服务 UUID，扫描响应包含名称。最多一个连接。原型不做绑定/配对授权，附近客户端可连接；生产化需添加物理授权窗口与设备绑定。

| 特征 | UUID | 操作 |
|---|---|---|
| 服务 | `f0e10000-9c7d-4f2b-a6c8-61d3e5b74001` | Primary |
| Info | `f0e10001-9c7d-4f2b-a6c8-61d3e5b74001` | Read |
| Control | `f0e10002-9c7d-4f2b-a6c8-61d3e5b74001` | Write with response |
| Data | `f0e10003-9c7d-4f2b-a6c8-61d3e5b74001` | Write with response |
| Status | `f0e10004-9c7d-4f2b-a6c8-61d3e5b74001` | Read / Notify |
| AI Status | `f0e10005-9c7d-4f2b-a6c8-61d3e5b74001` | Read |
| Preview Data | `f0e10006-9c7d-4f2b-a6c8-61d3e5b74001` | Read |

AI 提示词和 AI 状态是向后兼容扩展：旧网页不会读取 05/06 特征，也不使用 Info 的 flags，原有图片换图流程不变。

## Info（16 字节）

| 偏移 | 类型 | 含义 |
|---|---|---|
| 0 | u8 | 协议版本 1 |
| 1 | u8 | 颜色数 4 |
| 2 | u16 | 宽 400 |
| 4 | u16 | 高 300 |
| 6 | u32 | 帧长 30000 |
| 10 | u16 | Data 最大写入长度，包含 4 字节偏移；min(ATT_MTU−3,244) |
| 12 | u8 | 支持模式位图：bit0 全刷，bit1 快刷 |
| 13 | u8 | 能力 flags：bit0 支持 AI 生图，bit1 已配置 Wi-Fi/Token，bit2 支持屏幕帧回读 |
| 14 | u16 | AI 提示词最大 UTF-8 字节数，当前 1024 |

设备建议 MTU 247，实际由连接协商决定；客户端必须读取 Info，不假设 244。ATT MTU 23 时每个 Data 写入最多包含 16 个图像字节。图片与提示词均采用逐包带响应写入保证顺序。

## Control

图片 BEGIN：15 字节，`op:u8=1, version:u8=1, job_id:u32, length:u32=30000, crc32:u32, mode:u8`。job_id 必须非零；mode 0 全刷、1 快刷。成功后独占共享图片任务槽；即使 Wi-Fi/Token 未配置也可接收。忙时拒绝。

图片 COMMIT：6 字节，`op:u8=2, version:u8=1, job_id:u32`。必须为当前会话并已收齐 30,000 字节、CRC32 一致；成功后复制帧并排队。COMMIT 只表示接纳任务，不能当作显示成功。客户端不得因断连或超时自动重发 COMMIT。

AI BEGIN：15 字节，`op:u8=4, version:u8=1, job_id:u32, prompt_len:u32=1..1024, crc32:u32, reserved:u8=0`。提示词随后通过 Data 特征分块上传，偏移和 CRC 规则与图片相同。AI BEGIN 不占用共享任务槽；忙或未配置时拒绝。

AI COMMIT：6 字节，`op:u8=5, version:u8=1, job_id:u32`。必须为当前 AI 会话、已收齐提示词且 CRC32 一致。成功后使用该 job_id 提交 AI 任务；客户端不得因断连或超时自动重发 COMMIT。

预览准备：6 字节，`op:u8=6, version:u8=1, job_id:u32`。把最近一次成功显示的 AI 屏幕帧复制到回读缓冲区并重置偏移。job_id 必须匹配最近一次成功的 AI 任务。没有可预览画面时返回 ATT 错误。

ABORT：6 字节，`op:u8=3, version:u8=1, job_id:u32`。仅取消同编号、尚未提交的图片或提示词接收会话；已进入显示队列的任务不能取消。

## Data

`offset:u32 + payload`，总长 5..Info.maxWrite。图片和 AI 提示词共用该特征；只接收连续偏移，禁止重复、跳过或越界；错误包不修改已收数据。30 秒没有成功写入或连接断开，会清理未提交会话并释放任务槽。图片 COMMIT 校验失败时会话仍存在，客户端应断开或 ABORT；AI 提示词 COMMIT 后会话结束。

## Status（20 字节，原有图片状态）

| 偏移 | 类型 | 含义 |
|---|---|---|
| 0 | u8 | 版本 1 |
| 1 | u8 | 状态：0 idle、1 receiving、2 queued、3 displaying、4 succeeded、5 failed、6 AI busy |
| 2 | u16 | 错误：0 成功、1 包格式、2 忙、3 长度、4 CRC、5 偏移、6 接收超时、7 状态/执行失败 |
| 4 | u32 | BLE job_id；非 BLE 来源为 0 |
| 8 | u32 | 已收字节；queued/displaying/succeeded 为 30000 |
| 12 | u32 | 总长 30000 |
| 16 | u32 | 本次开机成功显示次数（含 AI 来源） |

控制命令与接收超时会触发 Status 更新通知；客户端仍以主动 Read 为准。状态保存在 RAM，重启丢失；后续新任务会覆盖旧结果。BLE 执行错误详情在串口日志中。

## AI Status（16 字节）

客户端在 AI 生成期间每约 1 秒读取一次。该特征只读，不依赖通知。

| 偏移 | 类型 | 含义 |
|---|---|---|
| 0 | u8 | 版本 1 |
| 1 | u8 | 0 idle、1 queued、2 connecting、3 generating、4 downloading、5 converting、6 displaying、7 succeeded、8 failed |
| 2 | u16 | 最近错误 `esp_err_t` 低 16 位；运行中为 0 |
| 4 | u32 | 当前/最近 AI job_id；source 不是 AI 时为 0 |
| 8 | u16 | 当前/最近任务已用秒数 |
| 10 | u8 | 失败阶段（复用固件 `frame_stage_t`）：2 联网、3 生成、4 下载、5 转换、6 刷屏 |
| 11 | u8 | flags：bit0 保留有可回读的 AI 屏幕帧 |
| 12 | u32 | 最近一次成功 AI 任务 job_id；无预览为 0 |

`state=7` 且 `error=0` 才表示刷屏成功；`state=8` 或非零 error 表示失败。AI 任务在页面关闭或蓝牙断开后仍会继续，重新连接后可读取当前阶段或最终结果。

## Preview Data（Read）

先用 Control `op=6` 准备回读，然后反复读取该特征。每个值的前 4 字节是当前偏移，后跟最多 240 字节屏幕帧数据；读取成功会推进设备端偏移。客户端校验偏移连续，累计到 30,000 字节即可用与图片相同的 2bpp 格式解析。偏移不一致时重新发送 `op=6` 重置一次。读取过程中请保持连接；该特征返回的是相框实际显示的 4 色帧，不是云端原图。

## 显示与重连

图片 COMMIT 或 AI COMMIT 接纳后，工作任务短暂等待 ATT 响应发送，停止 Wi-Fi，停止 NimBLE 主机并等待主机任务离开事件循环，然后反初始化 NimBLE 和蓝牙控制器。只有确认控制器回到 IDLE，才调用显示驱动；这也覆盖连接事件尚未送达应用层的竞态。保留驱动 8 秒恢复保护和 BUSY 超时。刷新成功/失败后重新初始化 NimBLE/GATT 并恢复广播；终态在恢复广播前写入。停止或重启失败则记录失败，无法确认安全状态时要求重启设备。

网页持有用户已授权的同一 BluetoothDevice 对象，尝试重连并轮询 Status/AI Status。图片换图最多等待约 90 秒；AI 生图最多等待约 15 分钟（设备端云端生成轮询上限 10 分钟，另加下载、转换和刷屏）。只接受同一 job_id 的终态，不能把另一台相框或后续任务的成功当作本次成功。AI 成功后可再回读屏幕帧。驱动成功仍需首次联调时目视检查真实画面。
