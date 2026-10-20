# FIREFLY-SENSE 固件项目

ESP32-S3（N16R8）六通道环境感知板固件。此目录于 2026-08-20 从
FlitFancy 站点仓库的 git 历史快照（firefly-fw-1.3.0-src.zip）重建。

## 目录

- `src/` — 采集器、传感器、Wi-Fi、网页/OTA、手表心率、手机 BLE 数据服务及 SPI TFT 显示
- `platformio.ini` — 两个构建环境：
  - `esp32s3_n16r8_usb`：整板版（GPIO4/5，默认）
  - `esp32s3_bench`：bench 板（GPIO1 SDA / GPIO2 SCL / GPIO42 INT）

## 版本

- **1.3.15 D8 心率节奏**（2026-10-10）：已 OTA 安装到 app0，D8 改为心率频率的 1/3，亮暗各占一半；断线、接触不良、过期及夜间熄灯时关闭。计时、输出位、回绕和 I²C 重试回归通过；实板 U5 输出位及网页七路新鲜数据通过，用户确认亮暗节奏均匀。见 `UPGRADE_1.3.15.md`。

- **1.3.12 中文界面与面板校准**（2026-10-07）：已OTA安装到app1，温度大数字、湿度/二氧化碳双卡片和中文状态。默认BLACK零偏移/RGB并修复重复初始化残留偏移，实屏中文/边缘/色序待确认。X3供电保留1.3.11修复，见 `UPGRADE_1.3.12.md`。

- **1.3.11 X3供电修复**（2026-10-06）：已OTA安装到app0，开机先开启屏幕VCC所接的X3标称3.3V，再初始化TFT；停止采集、传感器重试和单路扫描保留屏幕供电。用户确认温度画面出现，网站六路连续数据正常，见 `UPGRADE_1.3.11.md`。

- **1.3.10 低速 GPIO 直写诊断**（2026-10-06）：已 OTA 安装到 app1。TFTRAW 以代码不超过 100kHz 的时钟发送固定色块/白框/黑带，复用未改动 GREEN 初始化表；受控复测仍仅背光，正检查供电/复位及物理通信，见 `UPGRADE_1.3.10.md`。

- **1.3.9 软件 SPI 对照诊断**（2026-10-06）：已 OTA 安装到 app0，参考旧备份的 ST7735 软件 SPI 初始化；新增 TFTBUS SOFT/HARD、TFTTEST HOLD 和 TFTDATA。LEDA 固定 3V3，GPIO1 不再驱动；色条目视验收待反馈，见 `UPGRADE_1.3.9.md`。

- **1.3.8 TFT 诊断版**（2026-10-06）：已 OTA 安装到 app1，针对 1.3.7 白屏报告，将初始化命令降为 1MHz，加入受原 LAN/nonce 检查保护的诊断入口。SPI 路由和复位/片选/背光回读正常，实际屏幕结果待确认；见 `UPGRADE_1.3.8.md`。

- **1.3.7 彩屏准备版**（2026-10-06）：新增独立 SPI TFT，按用户确认的 GPIO39/40/41/42/2/1 接线显示 SHT41 温度/湿度、SCD40 CO₂、Wi-Fi 与 PC 链路状态。屏幕驱动暂按 ST7735 系列、160×128 横屏；保留原采集、心率、手机 BLE、OTA、schema 5 和 EEPROM 格式。升级及现场验证见 `UPGRADE_1.3.7.md`，实板显示尚待验证。

- **1.3.6 准备版**（2026-10-04）：新增 App 0.2.0 的手机 BLE 协议 1，保留手表 HRS 与原 Wi-Fi/CSV。编译、主机测试和软件互通通过，尚未刷入实板；升级步骤见 `UPGRADE_1.3.6.md`。

- **1.3.0**（2026-08-17）：flicker + RSSI 自愈、schema 4、独立采集调度
- **1.3.1**：自动关灯——CH3 LTR390 环境光低于阈值时熄灭
  装饰性 LED（六路通道活动灯 + 模式灯），心跳灯保持原语义。
- **1.3.2**（2026-09-09）：连续无新样本时自动重启故障通道，
  包含一直 Pending、没有 I²C 错误的停滞。CSV 仍为 schema 4。
- **1.3.3**（2026-10-02）：增加局域网网页应用 OTA，保留 schema 4 和六路传感器逻辑。
- **1.3.4**（2026-10-02）：CO₂=0 不再作为有效新样本；仅 CH5 自动恢复的断电时间从 1 秒延长到 5 秒。CSV 仍为 schema 4。
- **1.3.5**（2026-10-02 开发，2026-10-03 完成心率实板验收）：BLE 心率接收、自动重连和 schema 5；正式固件已安装在 app1，真实心率及广播关闭/重开后的恢复已验证。仅剩 PC 监听器重载与旧混合 CSV 表头修复，详见 `CURRENT_HANDOFF.md`。

### SPI TFT 显示（1.3.7 起，当前 1.3.12）

屏幕使用单独的SPI数据路径，屏幕脚3～7分别接GPIO39、40、41、42、2；1脚GND共地，2脚VCC实际接FFS X3第1脚的受控3.3V。1.3.11会在初始化前开启并持续保留X3供电，X3的I²C引脚不用于此屏。8脚LEDA固定接ESP32 3V3，GPIO1断开。

显示每秒检查已采集缓存，CH0温湿度、CH5 CO₂；温度主卡片和下方湿度/二氧化碳两格使用一个160×54（17280字节）RGB565缓冲，只重画变化区域。中文标签和比例数字采用U8g2字体，缺测/过期/恢复/停止时显示`--`，不增加传感器读取或调整采样周期。顶部显示Wi-Fi，底部显示采集状态及TCP联机/离线。

1.3.12依据实物照片默认采用ST7735 BLACK的零偏移/RGB配置、rotation1；初始化1MHz、像素8MHz。重复切换初始化配置前清除旧行列偏移。实际效果以目视确认为准；诊断命令仅报告配置，不提供LCD像素回读。串口/TCP命令：

- `TFTTEST`：色条测试 5 秒；`TFTTEST HOLD` 保持色条，`TFTDATA` 返回数据页，采集继续。
- `TFTRAW`：独立低速 GPIO 写入固定色块，写完保持；TFTDATA 重新初始化普通软件 SPI 后返回数据页。
- `TFTBUS SOFT` / `HARD`：在当前信号脚切换软件/硬件 SPI 并重新初始化。
- `TFTROT 1` / `TFTROT 3`：两个横屏方向。
- `TFTTYPE GREEN` / `RED` / `BLACK` / `B`：切换 ST7735 初始化配置。
- `TFTINVERT 0` / `1`：显示反相；`TFTBL 0` / `1`：固定 3V3 背光供电时只提示不可控制。

这些诊断选择只保存在 RAM，重启回到默认；`TFTTYPE` 会执行屏幕复位/初始化延时，切换时采集可能短暂停顿。bench 构建禁用显示 GPIO，原 GPIO1/2/42 测试映射保持可用。中文、时间、天气和心率屏显尚未加入；当前标签为 TEMP/HUM/CO2。

1.3.8 使用工程内的 ST7735 1.11.0 小范围派生驱动：初始化频率为 1MHz，正常像素为 8MHz。`GET /display` 可查配置与 GPIO，带原 OTA 启动 nonce 的 `POST /display/command` 可发 TFT 命令。额外支持 `TFTHZ 1000000/8000000` 切换像素速率；初始化已通过调用链回归，实际屏幕出图仍待验证。

### 手表心率（1.3.5）

`heart_rate.cpp` 使用锁定版本 NimBLE-Arduino 2.5.0，在独立 FreeRTOS 任务中扫描、
连接和订阅，避免连接及 GATT 等待阻塞原有传感器、TCP 和 HTTP 主循环。
扫描实测目标为 `Xiaomi C4A9`，随机地址 `ed:fc:17:92:9b:91`，广播标准 HRS `0x180D`；
只选择该名称或已实测地址且提供 HRS 的设备，订阅 `0x2A37`。
地址发生变化时仍可按准确名称识别；更换手表需调整目标配置后重编译。

逻辑 CH6 每 2 秒输出状态，末尾追加 `heart_rate_bpm,hr_connected,hr_contact,hr_state`。
前 36 个字段位置保持兼容，CH0–CH5 的新增字段为 NA。心率按标准 flags 支持 8/16 位、
接触状态和可选 Energy/RR 字段长度检查。断线、接触不良或超过 15 秒没有新有效心率时
输出 `ok=0`、心率 NA；连接后 30 秒无有效心率则重新扫描连接。
`GET /heart-rate` 提供当前状态、通知计数、连接尝试和扫描诊断，仅供可信局域网访问。

原固件的 `WiFi.setSleep(false)` 与当前 ESP-IDF 4.4 的 BLE 共存发生初始化崩溃，
本版使用 `WiFi.setSleep(true)`。实板崩溃栈与修正后的正常采集均已留证。
依据：[Espressif 共存问题](https://github.com/espressif/esp-idf/issues/9595)、
[Bluetooth HRS 1.0](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/HRS_v1.0/out/en/index-en.html)。

主机验证：`py -3 scripts/test_heart_rate.py --zig S:\FlitFancy\tools\ffs-host-test\ziglang\zig.exe`。
网站 v1.28.3 已发布并加载：公开存在页和 Worker 最新快照包含 CH6；心率历史仍需管理员登录，不上传云端历史。
本机后端已重载，`/api/status` 返回 `capabilities.heart_rate=1`。正式 1.3.5 的真实
BLE → CH6/TCP → PC CSV → SQLite 链路已通过；广播关闭/重开后观察到
74 bpm → 缺测 → 86 bpm，六路采样序号继续增长且 uptime 连续。
证据位于 `S:\FlitFancy\security-work\ffs-heart-rate-20261002\formal-live-check.json`
及同目录 `reconnect-live-check.json`。早期 probe、1.3.3/1.3.4 的检查结果作为历史
记录保留在 `CURRENT_HANDOFF.md`，不代表当前安装版本。

### CH5 最小恢复改动（1.3.4）

本版沿用 1.3.2 的单路恢复状态机，只改用户确认的两项行为：

- SCD40 的 I²C 读取成功但 CO₂=0 时返回 Error；不增加有效采样序号，也不刷新最后有效采样时间。
  正常的未就绪轮询仍为 Pending。短暂零值随后恢复正常时，不会额外断电。
- CH5 使用 `kScd40RecoveryPowerOffMs=5000`；其他通道仍使用原有 1000 ms。
  关电等待通过既有状态机推进，其他通道继续采集。

CH5 无有效新样本的约 17 秒触发条件、上电后 1 秒等待、60 秒重试间隔和原有日志沿用。
恢复后收到有效非零 CO₂ 才更新成功样本。控制寄存器回读仍不能证明电源轨实际降到零，
本版不声称已验证所有长期卡死均能自动恢复。

主机测试直接编译生产采集器、SCD40 读取和单路电源操作，18 个场景通过；新增覆盖
持续零值、短暂零值、CH5 五秒断电时其他采样继续、以及断电期间手动停止。
原零值场景在 1.3.3 失败，1.3.4 通过。应用镜像按前述隔离方法去掉默认网络凭证，
实际 OTA 安装和回读结果见 `CURRENT_HANDOFF.md`。

### Wi-Fi 固件升级（1.3.3）

打开板子的 `http://<板子IP>/update`，选择整板应用 `.bin` 并点击“开始升级”。
板子把固件写到备用应用分区，验证 ESP32-S3 芯片、FIREFLY-SENSE 整板标识和
完整镜像后才切换启动分区，成功响应后延迟 1.5 秒重启。网页显示上传进度并等待
设备重新连接。升级期间采集会短暂停顿；重启后恢复原有采集和 TCP 上报。

首次从不带 OTA 的固件升级时需要 USB 烧录应用一次。当前默认 16 MB 分区表含
两个 0x640000 字节的应用槽，OTA 不修改分区表、外部 EEPROM 或 LittleFS 数据。
上传中断、长度错误、校验失败不会切换当前应用。当前没有启用新应用启动失败后的
自动回滚；保留 USB 恢复方式。1.3.5 心率固件保持相同整板标识，已使用 OTA 安装。

这是可信局域网中的升级入口：请求限制在设备所在子网，检查 Host、Origin 和每次
启动生成的请求 nonce，阻止跨站网页直接发起升级。nonce 是跨站请求保护，
不代表独立账号鉴权。板端接口不直接对公网发布。
“存在 → 现实感知 → 固件升级”自 FlitFancy 1.25.0 加入，当前网站资源版本为 1.28.3，通过本机后端转发，
另有网站管理员鉴权；在网页上选择应用固件即可升级，无需另开板载页面。

应用镜像必须含 `FIREFLY-SENSE-N16R8-OTA1` 自定义描述符，位于镜像偏移 288，
长度 32 字节；链接参数保留该描述符。Bench 版本使用不同标识，整板会拒绝。
不要上传其他设备固件、bootloader、分区表或合并镜像。

凭证不进入升级产物。推荐隔离编译：

```powershell
py -3 scripts/build_ota.py --pio 'C:\Users\Yunwe\AppData\Roaming\Python\Python314\Scripts\pio.exe' --safe-esptool
```

脚本在 `.ota-build/` 中将 Wi-Fi 默认凭证置空，构建并核对镜像标识后复制到
`artifacts/firefly-sense-<版本>-ota.bin`。上传前应确认设备已有有效 EEPROM 网络配置。
`--safe-esptool` 仅绕过本机打包工具 `_contrib` 的读取错误，仍使用已安装的
PlatformIO esptool 4.11.0，不修改全局工具包。

命令行上传和主机测试：

```powershell
py -3 scripts/upload_ota.py 192.168.1.33 artifacts/firefly-sense-1.3.5-ota.bin
py -3 scripts/test_ota.py --zig 'S:\FlitFancy\tools\ffs-host-test\ziglang\zig.exe'
```

`GET /device` 提供版本、运行时间、EEPROM 有效状态、启动分区和 OTA 容量；
`POST /device/firmware` 接收 `application/octet-stream` 原始应用镜像。
测试直接运行生产 `OtaSession`，覆盖分块边界、错误芯片/整板标识、上传截断、
大小限制、写入/校验失败和失败后的重试。板上安装及 OTA 实测结果见 `CURRENT_HANDOFF.md`。

### 单路无数据恢复（1.3.2）

故障证据：2026-09-09 的网页本地接口中，CH5 序号停在 4253，
`sample_age_ms` 持续增加至四小时左右，`error_streak=0`；其余五路正常。
旧代码仅在 SCD40 通信错误时重启测量，持续“未就绪”没有超时出口。
这能解释为什么无法自行恢复，尚不能确定最初停止出数的硬件原因。

- 从最后一次 Fresh 起，超过 **3 个实际结果周期 + 2 秒调度余量**，
  且下一次采样仍为 Pending/Error 时，开始恢复；MODE 上报频率不影响判断。
  六路结果周期为 10、30、5、2、1、5 秒。SCD40 是约 17 秒后下一次轮询触发，
  不是把其 1 秒 ready 轮询误当成结果周期。
- 先断开 I²C mux，再读改写 U4 输出寄存器的目标位并回读确认；
  不调用会关闭其他通道的旧 `powerBranchOn()`。
- 目标通道断电 1 秒，上电后等待稳定（CH5 保守留 1 秒），重置该通道软件状态。
  使用非阻塞状态机，其他通道继续采集，网络和网页继续服务。
- 给 SGP41 额外 12 秒调理窗口；给多阶段 LTR390/SCD40 启动留余量。
  重启前缓存失效，只有重新获得 Fresh 后才报告恢复，不以数值是否变化判断。
- 同一路两次断电尝试至少相隔 60 秒；上电未确认时每秒重试恢复供电。
  手动 `X` 停止优先，不会被自动恢复重新上电。
- 串口和 TCP 输出 `[recovery]` 事件：超时原因、断电/上电确认、恢复新样本，
  带运行时间、尝试次数、采样序号、错误计数。它们不是 CSV 数据行。
  离线时没有持久化事件日志，也没有修改原 CSV 格式。
- 寄存器回读只能确认控制位；实际电压下降、供电质量与传感器恢复效果仍需实板验证。

主机回归测试（不连接或复位板子）：

```powershell
py -3 scripts/test_recovery.py --zig 'S:\FlitFancy\tools\ffs-host-test\ziglang\zig.exe'
```

测试直接编译生产 collector、SCD40 读取函数和单路电源操作函数，模拟硬件与时钟。
覆盖正常预热/模式切换、持续未就绪、通信错误、供电恢复失败、重试间隔、
计时回绕/长期运行、手动停止、六路故障隔离和全部输出位组合。
原版在持续未就绪场景下失败，1.3.2 通过。整板编译使用空 WiFi 凭证的隔离副本；
于 2026-09-09 经用户授权通过 COM7 烧录，写入哈希校验通过。
烧录前确认外部 EEPROM 配置有效，空凭证构建沿用板内网络配置。
串口连续三批数据及本地网页接口均确认为 1.3.2，六路有效且序号增长，
CO₂ 约 735 ppm。观察期间没有恢复事件；故障状态下的单路恢复仍待实板验证。
未制作公开发布包。

### 自动关灯参数（src/config.h，编译期常量）

| 常量 | 值 | 含义 |
|---|---|---|
| `kAutoDarkEnabled` | true | 功能总开关 |
| `kAutoDarkAlsThreshold` | 120 | ALS 原始计数（默认 3x 增益 ≈ 0.2 lux/count）≈ 24 lux，低于则熄灯 |
| `kAutoDarkAlsHysteresis` | 60 | ≈ 12 lux 回差，高于 阈值+回差 才恢复，防止临界闪烁 |

行为细节：
- 光照数据用 CH3 最新鲜样本（15 s 内有效）；传感器无数据时保持现状。
- 手动 LED 测试（LEDRUN/LEDALL）优先：退出熄灯并解除武装，房间再次变亮后
  才重新武装——暗室里跑灯测试不会被立刻按灭。
- 心跳灯 D4 不受影响（未连接时仍按 1s/3s 闪烁提示链路状态）。

## 构建与烧录

本机 PlatformIO 已就绪（2026-08-21 重建）：CLI 在
`C:\Users\Yunwe\AppData\Roaming\Python\Python314\Scripts\pio.exe`，
工具链与框架已装到 `C:\Users\Yunwe\.platformio\packages`。

1. 编译/烧录（VS Code 或命令行均可）：
   ```
   pio run -e esp32s3_n16r8_usb                          # 编译整板版
   pio run -e esp32s3_n16r8_usb -t upload --upload-port COM7   # 烧录
   ```
2. 烧录会复位板子，数据中断约 30 秒属预期；板端固件带自愈，无需手动干预。

> 注意：esp32s3 用不到的包（riscv32 工具链、GDB、mkspiffs 等）当时因
> 网络限制以空壳占位（有 .piopm/package.json 但无内容）。**本工程只构建
> esp32s3 + arduino，永远用不到它们**；若将来要构建其他 MCU（C3/C6）或
> 用 espidf 框架，先删掉 `packages` 下对应空壳再让 pio 重新下载。

## listen_wifi.ps1（PC 端监听器）

**正本在站点仓库 `S:\FlitFancy\site\scripts\vendor\listen_wifi.ps1`**
（bat 优先搜索路径，git 跟踪），本仓库不再存放副本。契约由
`S:\FlitFancy\site\tests\sensor-listener-reconnect.test.ps1`
钉死，重写版已通过该测试与端到端冒烟（HELLO/CSV 落盘 + POST /api/ingest）。

- 参数：`-Port`、`-DataRoot`、`-NoFlitFancy`、`-Quiet`、
  `-TimeoutSeconds`（HELLO 前的探测空闲超时）、`-StartStream`（对板
  补发 MODE 2）
- PING→PONG 双向心跳；TCP RST/FIN 只释放当前连接，进程不退出；
- CSV 写 `sessions/wifi-<stamp>[-序号].csv`（UTF-8 BOM，逐行 AutoFlush）与
  `live/firefly_live.csv`（无 BOM，AutoFlush），并转发本机后端
  `/api/ingest`（X-Firefly-Board 头）；
- 启动时把已结束的会话移入 `archive/sessions/` 长期归档，不删除 14 天前的原始 CSV。
- 2026-10-03 已修正表头轮换：收到不同表头时关闭并保留旧会话，以原子新建和递增
  后缀创建新文件，并重置 live 缓存；同表头重连不轮换。存储和 TCP 重连测试通过。
  生产监听器仍待重启加载，随后才能修复已关闭的旧混合文件表头。当前活动文件为
  `S:\FlitFancy\site\data\sensors\sessions\wifi-20261001-213313.csv`，仍是 37 列
  表头、41 列数据（含 pc_time）；在线固定位置解析正常，尚未改写该文件。
- 文件为 UTF-8 BOM：Windows PowerShell 5.1 必须（无 BOM 时中文注释会
  破坏解析）。运行由 `S:\FlitFancy\site\scripts\watch_sensor_listener.ps1`
  守护，`启动云萤.bat` 的 listener 动作负责幂等启动。

### 数据保存位置

`CURRENT_HANDOFF.md` 和简短交接文件是状态与路径说明；实际数据分开保存：

- `S:\FlitFancy\site\data\sensors\sessions\` 与 `archive\sessions\` 保存原始会话 CSV 和长期归档。
- `S:\FlitFancy\site\data\sensors\live\firefly_live.csv` 是可覆盖的实时缓存，不能作为独立历史备份。
- `S:\FlitFancy\site\backend\data\flitfancy.db` 是 SQLite 查询副本，感知明细保留最近 14 天。
- `S:\FlitFancy\security-work\ffs-heart-rate-20261002\` 保存本次验收记录、测试证据及 `baseline/` 恢复副本。

CSV 长期归档与运行 SQLite 位于 S 盘；完整工作区快照保存在另一物理磁盘 B:\FlitFancy\workspace\snapshots\，包含源码/Git 历史、原始 CSV、数据库和私有配置。恢复说明见网站 BACKUP.md。
