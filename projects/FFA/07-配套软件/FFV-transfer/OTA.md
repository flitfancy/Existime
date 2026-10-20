# FIREFLY VOICE 局域网固件升级

更新：2026-09-12。网页入口位于“侧耳倾听”右上角的“固件升级”。

## 使用

1. 板子保持供电，电脑音频服务运行，等待 Wi-Fi 已连接。
2. 停止播放和语音识别，点击“固件升级”→“选择固件”。
3. 选择 `firefly-voice-版本.bin`，点击“开始升级”。
4. 页面依次显示接收、写入、重启、验证，直到“升级完成”。

只接受当前硬件的 ESP32-S3 **应用 bin**，不接受 merged.bin、bootloader.bin 或分区表。
无需 USB 数据线；首次安装 OTA 接收固件及修改分区表仍需 USB。
升级期间禁止音频控制，后台唤醒暂不发起对话；成功重连后恢复原有持续收音。
不要断电。传输失败可重新选择完整文件重试，本版不做断点续传。

## 构建与文件

- 版本：`firmware-wifi-transport/firmware_version.h`。
- 编译：运行 `build-wifi-transport.ps1`，沿用 Arduino ESP32 3.3.10 和当前自定义分区。
- 打包：在音频服务的 Python 环境中运行 `package_firmware.py`。
- 本机固件包：`media-work/ota/<version>/firefly-voice-<version>.bin` 与 `manifest.json`。
- 板端：`ota_transport.h`、`firmware-wifi-transport.ino`。
- 电脑端：`ota_service.py`，由 `DeviceSession` 独占连接并执行升级。
- 网站：`/api/audio/firmware` → 7865 `/device/firmware`，状态放在 `/status.ota`。

本项目当前固件沿用私有 `wifi_credentials.local.h`，生成的 bin 含本机 Wi-Fi/配对配置，
因此固件包只保存到被忽略的 `media-work`，不进入网站公开资源、提交或下载接口。

## 可靠性和边界

- 沿用已认证 TCP 7866，未新开端口；网站升级接口在本机与公网都要求管理员登录。
- 沿用 Host/Origin/Fetch Metadata 防护。固定回环代理目标，不接受任意板子地址或下载 URL。
- 单个固件最多 `0x640000` 字节；有界接收，上传停滞 30 秒退出。
- 两个槽位 app0/app1 各 6.25 MiB。只写未运行的槽，完整验证后才更改启动项。
- SHA-256 校验上传文件与板端接收字节，ESP-IDF 再校验可启动镜像；芯片与项目标识也检查。
- 按 4096 字节逐块写入，每块回读确认已写位置。中断、顺序错误或校验失败不切换启动槽。
- 新固件首次启动处于待验证状态。覆盖 Arduino 的提前确认钩子，等待麦克风、I2S、PSRAM、
  播放任务初始化，以及电脑观测到至少 5 秒连接后的新麦克风数据与最近心跳，才发确认。
- 90 秒没有确认时板子请求回滚；待验证期间崩溃或重新上电由启动器回滚。
- 确认的是启动、驱动与数据链健康，不等于完成长时间音质测试；确认后不会因任意业务故障自动回退。
- 使用家庭可信局域网内的现有配对认证，SHA-256 不是发布者数字签名，也未启用 Secure Boot。
  不应将板端端口映射到公网。

## 验证入口

```powershell
python -m unittest test_ota_service test_protocol_v2 test_wifi_transport test_pause_protocol test_music_upload test_voice_turn
```

`diagnostics/verify_ota.py` 的实板检查需要先停止音频服务，避免多个进程争用设备连接：

- `probe`：读取版本、分区、镜像摘要与健康状态。
- `interrupt --image <bin>`：只写入一块便断开，确认仍运行原固件。
- `rollback --image <bin>`：完整更新但故意不确认，观察 90 秒自动回滚。

只有该诊断工具会故意跳过确认。日常使用网页与常驻音频服务自动完成确认。
如网页等待恢复超时，先检查供电、Wi-Fi 与音频服务；仍无法恢复可用 USB 重新烧录。

## 本轮实测结果

- USB 基础固件 0.4.0 安装并校验成功，分区表与原板回读一致。
- 4096 字节后断开升级：仍运行 app0/0.4.0，未切换启动槽。
- 完整更新至 app1/0.4.1 后故意不确认：90 秒窗口结束后自动回到 app0/0.4.0。
- 正式 7865 HTTP → DeviceSession → Wi-Fi 链路再次更新至 0.4.1：经历 writing、rebooting、
  verifying、complete；运行镜像摘要与包内摘要一致，pending_verify=false。
- 升级期间音频控制返回 409。网站升级接口未登录返回 401，跨站请求返回 403。
- 升级后 1 秒低电平测试音播放完成，播放期间双麦包计数继续增加；该检查时未观察到缺包。
- 20 项相关 Python 单测通过，网站 JS/样式全套检查及后端模块/冒烟检查通过。

实板记录在本机仓库外 `S:/FlitFancy/logs/ffv-ota-rollback-test.log`、
`ffv-ota-service-result.json`、`ffv-ota-audio-check.json`。这是短时验收，不替代长期稳定性测试。

OTA API 依据：[Espressif ESP32-S3 OTA 文档](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/ota.html)。
