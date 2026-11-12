# Android 网页 BLE 换图

这是自托管静态网站与 ESP32-S3 接收固件的原型。手机访问服务器上的 HTTPS 页面，照片在手机本地裁剪、缩放、四色转换，再通过 BLE 发送 30KB 面板帧；也可以在网页输入 AI 提示词，由相框通过 Wi-Fi 调用 ModelScope 生成并直接刷屏。服务器不接收照片或提示词，也不需要 Node 后端或数据库。

## 编译与烧录

使用 ESP-IDF v5.5.5 及 ESP32-S3 工具链。先在你的实际 SDK 路径执行 `source <ESP-IDF>/export.sh`，在项目根目录执行：

```bash
idf.py -B build-ble -D SDKCONFIG=sdkconfig.ble build
idf.py -B build-ble -D SDKCONFIG=sdkconfig.ble -p /dev/ttyUSB0 flash monitor
```

将串口路径替换为实际设备。单独的 `sdkconfig.ble` 初次从 `sdkconfig.defaults` 生成，启用 NimBLE；不覆盖原有 `sdkconfig`。若之前已生成不含 BLE 的 `sdkconfig.ble`，通过相同参数运行 `menuconfig`，启用 Bluetooth → NimBLE，并将最大连接数设为 1、首选 ATT MTU 设为 247。勿在此配置中选择 Bluedroid。

主程序先初始化显示与共享工作任务，再启动 BLE 服务。串口应看到 `BLE image receiver ready: EPaperFrame`。即使本地 Wi-Fi/ModelScope Token 为空也可通过 BLE 换图；AI 生图页签会提示未配置。固件不再上电自动生图，提示词只由网页或串口调试命令触发，避免重启消耗云端额度。

沿用已有硬件引脚、3MiB app 分区和显示驱动保护。构建产物可能含本地云端凭据，不要放到静态网站目录或公开分发。

## 本地预览与测试

```bash
npm --prefix web test
npm --prefix web run dev
```

电脑打开 `http://127.0.0.1:8080` 可预览与导出。本地预览默认只监听回环地址。手机访问电脑的普通 HTTP 局域网地址不满足 Web Bluetooth 安全上下文要求；手机 BLE 联调请使用服务器上有有效证书的 HTTPS 地址。

使用 Chrome for Android。页面会检查安全上下文与 Web Bluetooth 可用性；不支持时仍可选图、预览和导出。不要通过微信内嵌浏览器或嵌入 iframe 打开。连接必须由用户点击按钮授权。

## 服务器部署

```bash
npm --prefix web run build
```

将 `web/dist/` 内的全部文件复制到服务器静态目录，例如 `/srv/epaper-frame/`。只需要部署 `index.html`、`styles.css` 与 `src/`；无需安装 npm 依赖。配置示例为 `web/deploy/nginx.conf`，替换域名、静态目录和证书路径后，先在服务器运行 `nginx -t`，再 reload Nginx。

域名应指向服务器，443 端口可访问，证书需被 Android 信任。示例将 HTTP 重定向 HTTPS，并配置蓝牙 Permissions-Policy、同源资源 CSP 和不缓存原型文件。若在子目录部署，保留相对文件层级即可。

当前域名地址为 **https://lumicat.top:8443/**，原 **https://103.117.138.156:8443/** 保留可用。服务器为 Ubuntu 24.04，原有服务继续占用 443，相框网站使用 8443；`http://lumicat.top` 自动跳转到域名 HTTPS 地址。80 同时保留 ACME 验证入口。实际配置备份在 `web/deploy/nginx-domain.conf` 与 `web/deploy/nginx-ip.conf`。

网站根目录 `/srv/epaper-frame/site`，Nginx 配置 `/etc/nginx/conf.d/epaper-frame.conf`（IP）与 `/etc/nginx/conf.d/epaper-frame-domain.conf`（域名）。使用独立环境 `/opt/epaper-certbot` 中的 Certbot 5.4.0，公网 IP 证书名称 `epaper-frame-ip`，域名证书名称 `epaper-frame-domain`。`epaper-certbot-renew.timer` 每天检查两次所有已管理证书的续期，成功后通过 `/usr/local/sbin/epaper-nginx-reload` 验证并重载 Nginx。仅新增了 UFW 的 80/tcp、8443/tcp 规则，保留原有规则。

域名接入需要三步：DNS A 记录指向服务器、Nginx 的 `server_name` 匹配域名、签发并加载该域名的 TLS 证书。DNS 不映射端口，因此 HTTPS 使用 8443 时地址仍需包含 `:8443`；省略端口的 `https://lumicat.top` 会连接到当前另一个服务占用的 443。

IP 证书支持依据：[Let’s Encrypt 官方说明](https://letsencrypt.org/2026/03/11/shorter-certs-certbot.html)。服务端维护可用 `systemctl status epaper-certbot-renew.timer` 与 `journalctl -u epaper-certbot-renew.service`；更新网站时重新打包并同步 `web/dist/` 内容即可。仓库不保存登录密码或服务器私钥。

## 操作流程

1. 从相册选择 JPG/PNG/WebP（最大 20MB、3200 万像素），或先用示例画面。
2. 调整放大、左右/上下位置及明暗细节开关，切换原图构图与四色预览。
3. 点击连接，在系统弹窗选择 EPaperFrame。
4. 点击发送。进度条反映已成功写入的数据，不表示屏幕已经更新。
5. 收图完成后，相框暂时断开 BLE 刷屏。保持页面在前台、手机靠近相框，等待自动重连。
6. 只有同一任务的驱动刷新成功才显示“显示成功”。可点击“查询状态”检查最近任务；断连或超时后优先查询，避免重复刷屏。

AI 生图：

1. 连接后切到“AI 生图”，输入或选择示例提示词；提示词最多 1024 个 UTF-8 字节。
2. 点击“生成并显示到相框”，页面先把提示词通过 BLE 分块上传，再提交任务。
3. 相框自行联网、校时并调用 ModelScope。页面显示排队、连接网络、云端生成、下载图片、四色转换和刷屏阶段；ModelScope 不提供百分比，因此另外显示已用时间。
4. 刷屏期间 BLE 会短暂断开，页面自动重连并读取最终结果。生成中关闭页面或离开蓝牙范围不会中断相框生图，重新连接后可继续查询。
5. 成功后页面会读回 30,000 字节四色屏幕帧作为预览，可点击“保存 AI 屏幕帧”。预览读取需要保持蓝牙连接，通常需要几秒。
6. AI 生成会消耗 ModelScope 额度；失败时页面显示失败阶段和错误编号，详情看串口日志。

“保存屏幕帧”导出 30,000 字节原始文件，不是普通照片格式；可用于跨端协议验证。全刷为网页默认模式，快刷协议预留。

## 验证与人工联调清单

主机回归：`./tests/host/run.sh`，必要时先 `export IDF_PATH=<实际SDK>`。覆盖接收状态机、共享任务、原有显示/图像/云端客户端。

网页自动测试使用 Node 内置测试框架，包含 CRC/颜色打包/透明背景/裁剪边界、连续分块、忙状态、提交断连恢复、不同任务结果和超时，也覆盖 AI 提示词分块、AI 状态轮询、断线重连和屏幕帧预览回读。可直接运行 `node web/tests/codec.test.js`、`node web/tests/ble.test.js` 查看逐项结果。

实机仍须逐项确认：

- Android 可发现、连接，取消授权后可重试。
- 发送黑白黄红色块，目视确认颜色、像素顺序和方向。
- 连续两次更换不同图片，核对显示计数、恢复间隔及真实画面。
- 接收中断连/关页面，再连接后可重新发送，未提交图片没有刷出。
- AI 生图期间图片发送被拒绝；BLE 接收/刷屏期间 AI 提交被拒绝；串口 AI 请求同样互斥。
- 上电不再自动生图；网页提交后依次看到联网、云端生成、下载、转换、刷屏阶段。
- AI 生成期间关闭页面或离开蓝牙范围，相框继续生成；重新连接后可读取当前阶段或最终结果。
- AI 成功后页面能读回屏幕帧并显示预览，保存文件为 30,000 字节；预览偏移不一致时能重试。
- 刷屏时 BLE 暂时断开，完成后可重连读取正确结果；BUSY/SPI 故障返回失败。
- Android 前台长时间传输、默认 MTU 与协商 MTU、Nginx HTTPS/CSP 在实际服务器上的表现。

当前为近距离单设备原型，没有账号、图片持久化、远程控制或设备绑定。后续 App 继续使用 `docs/ble-protocol.md`；只需替换浏览器蓝牙适配层，并移植/复用图像转换。
