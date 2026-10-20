# FIREFLY VOICE 音频架构

更新：2026-09-13

OTA 固件升级通过现有 TCP 7866 认证会话执行，网页沿用 2671 的管理员入口，未新增端口。
`ota_service.py` 负责上传验证、写入进度和启动确认，板端 `ota_transport.h` 负责双槽写入与回滚。
使用和测试边界见 [OTA.md](OTA.md)。

## 运行链路

```text
浏览器控制台
  └─ HTTP 127.0.0.1:2671  FlitFancy 本机控制与状态
       └─ HTTP 127.0.0.1:7865  FFV-transfer 常驻音频服务
            ├─ TCP firefly-voice.local:7866
            │    认证 / 心跳 / 控制 / 按键 / 扬声器 PCM
            ├─ UDP 本机:7867
            │    24 kHz PCM16 双麦实时流（序号 + CRC32）
            └─ USB COM5
                 烧录与诊断
```

FlitFancy 的 2671 端口是统一控制面，负责页面同源 API、权限和状态展示。连续 PCM 不再
经过 2671：本地 AI 直接使用 FFV 的 7865 流式接口，避免网页后端复制每个音频块。
FFV 是唯一设备拥有者，持续维护认证、心跳、重连、录音、STT 和扬声器队列。板子自行
保存并使用 Wi-Fi 参数，网页不参与联网。

## 端口与协议

| 端口 | 所在设备 | 作用 | 暴露范围 |
|---|---|---|---|
| 2671/TCP | 电脑 | FlitFancy 页面、控制和状态代理 | 本机/既有受控隧道 |
| 7865/TCP | 电脑 | FFV 本地 AI 输入输出与媒体 API | 仅 `127.0.0.1` |
| 7866/TCP | ESP32 | 配对认证、控制、TTS/音乐下行 | 家庭局域网 |
| 7867/UDP | 电脑 | 双麦实时 PCM 上行 | 电脑当前局域网地址 |

TCP 7866 需要 32 字节本机配对密钥。UDP 只在认证会话发出，FFV 只接受当前板子 IP，
并校验每包长度、序号和 CRC。7865 还校验回环来源、Host、Origin 与 Fetch Metadata。
这些边界适用于受信任家庭网络，不应把 7866/7867 转发到公网。

## 数据与控制

### 本机媒体解码

`media_decode.py` 负责 NCM 解包、音轨识别、解码与 PCM 校验。普通编码沿用本机
FFmpeg；`av3a` 音轨自动选择 `third_party/av3a/ffmpeg.exe` 原生后端。仍先完成
整首解码，再提交现有扬声器调度器，输出格式及 Wi-Fi 协议不变。

原生库会向 stdout 打印调试文字，因此使用每次任务独立的临时 PCM 文件，不把
stdout 当音频，也不使用该构建的 ffprobe JSON。稳定版 ffprobe 按音轨 tag 识别
AV3A。停止/超时先回收解码进程，再清理临时输出；成功后读取 PCM 并立即清理。
状态元数据保留原始编码和 `decoded_backend`。本机依赖安装说明见
`third_party/av3a/README.md`，验证见 `test_media_decode.py`、`test_media_cancel.py`。

### 麦克风电平历史

`microphone_history.py` 在 `update_live_input` 已校验的双麦流中累计每秒电平统计，
不依赖网页或识别开关。网络线程只更新内存数值，独立线程每 5 秒提交 SQLite；
`data/microphone-history.sqlite3` 同时保存每秒原始统计与分钟汇总，没有按日期删除。
平均电平在能量域按样本数加权，最高值为音频包的最大 RMS 电平，单位 dBFS。
统计保留增益、实际接收音频时长与首尾时间；这不是经过校准的房间声压级 dB SPL。

`GET /v1/audio/history?span=5m|1h|24h|7d|30d|all&end=<Unix秒>` 返回最多约 480 个
绘图点，聚合查询不删除每秒记录。网站沿用 `/api/audio/history` 认证代理，图表
位于现有电平区域背面，点击实时电平进入历史，点击曲线或返回按钮回到正面；
断连和增益变化处断开。电脑休眠/关机、板子断电和未收到音频
的时段不补造数据；板端离线存储和补传尚未实现。

正常退出会提交余下统计；进程被强杀或电脑突然断电时可能缺少最后约 5 秒的内存
记录。写入失败保留待提交统计并在页面提示异常。数据库及 WAL 是运行数据，不能
提交到 Git；需要迁移/备份时使用 SQLite backup API 或停服务后复制数据库。

验证：`python -m unittest test_microphone_history.py`；网站运行
`node tests/console-audio-history.test.mjs`。

### AstrBot 共享对话入口

`integrations/astrbot_plugin_firefly_voice` 作为 AstrBot 插件订阅 7865 的 SSE，只有自然完成的
`voice.turn.completed / reason=utterance` 进入正常事件队列。沿用已有 QQ 私聊的 UMO，因此
使用现有选中对话、人设、模型覆盖、记忆插件与 AstrBot 的同会话锁。物理平台仍标记为
`firefly_voice`，自定义事件的 `send()` 仅调用本机 TTS 和板子播放，不调用 QQ 发送接口。
默认只绑定现有唯一数字 QQ 管理员与唯一启用 QQ 平台；多个候选时拒绝猜测。

QQ 原有 SenseVoice provider 的地址改为 `http://127.0.0.1:7865/v1`。文件识别与板子 STT
共享一份模型、一个有界队列，不再需要旧 7890 服务。QQ 回复插件只匹配此用户的私聊语音，
保留文字并附加语音；合成失败时保留文字。普通文字与其他聊天不改变回复策略。
当前合成为 GPT-SoVITS v2Pro 本机角色声音。`character_voice.py` 按需启动并复用独立
Python/CUDA 子进程，`gpt_sovits_worker.py` 加载固定权重和用户选择的参考音频；
私有管道及任务文件传递合成请求与 WAV，不增加端口。统一返回 48 kHz PCM16 mono，
继续沿用现有板子/QQ 回复路径。模型驻留、配置和生命周期见 `TTS.md`；流式合成尚未接入。

文字事件保留最近 256 条，游标带服务 epoch；失去历史时显式 reset。插件重启从当前事件开始，
短暂 HTTP 重连可续接，超过 30 秒的指令不自动执行。当前以避免自动重复对话为优先，不能承诺
进程崩溃中的指令恰好执行一次。手动连续识别暂时只输出文字，不自动调用 AI。
扬声器忙时本轮 AI 回复保留为文字，不抢占音乐；两路音频混音、打断与恢复另行实现。

AstrBot、QQ/NapCat 和 Ollama 沿用已有端口与启动方式；本次语音桥接没有新开 HTTP 端口。
FFV 音频服务仍可在 AstrBot 退出时单独播放、录音和识别。

播放文件先由电脑本机解包/解码为 48 kHz PCM16 mono，再通过 FFVP 20 字节帧头发送，
每帧负载最多 8192 字节。TCP 负责可靠传输，不再逐块 ACK 或超时重发。协议定义见
`PROTOCOL_V2.md`，编解码见 `protocol_v2.py`，连接封装见 `ffv_v2_client.py`。

板端只有网络任务可以访问 WiFiClient，包括 `connected()`；它内部也会调用 socket
窥读。其他任务通过有界队列发送状态。PSRAM 在启动时一次分配 1152000 字节，音乐
使用最长 12 秒环形缓冲，TTS 使用 2 秒逻辑容量。播放任务消费 PCM，麦克风、灯光、
按键在主循环运行。主机依据每 250 ms 的消费进度限制音乐提前发送至 6 秒、TTS 至
600 ms，给暂停/停止留出接收空间。文件启动预缓存 3 秒，TTS 为 160 ms；实际延迟
还包含主机、网络与 I2S 启动时间。TTS 使用 LIVE 标志、零总长度和显式 PLAY_END。

双麦始终由板端以 10 ms UDP 包发送。FFV 同时把原始双声道流交给本地 AI；网页识别
改用电脑端 AEC 后的 16 kHz 音频，经 VAD 按语音停顿分句后送入 SenseVoice。
原始 24 kHz 与处理后 16 kHz WAV 分别保存，页面回听默认使用处理后版本。UDP 丢包不会阻塞
TCP 控制；序号、CRC、DMA 溢出和网页订阅丢弃分别计数。

## 输出调度（2026-09-12）

音乐、原始 PCM、TTS 和唤醒提示音通过 `OutputScheduler` 统一申请一个扬声器任务。
任务在接收请求体前即占用输出位置，具有独立 ID、停止事件和完成事件；网页字段是调度器
发布的状态，不再用全局 `playing` 的先检查后赋值作为互斥手段。

`playback_routes.py` 负责 HTTP 上传和解码提交；`output_scheduler.py` 负责原子接纳、
取消及终态；`output_session.py` 由 `DeviceSession` 驱动，独占当前任务的板端发送与进度处理。
传输、采样率、音量算法和数字参考保持原协议。音乐与 TTS 仍互斥，未加入混音或压低背景音乐。

- 输出阶段：uploading / decoding / queued / playing / stopping，终态为 complete / cancelled / failed。
- 上传、解码或未启动任务可以立即取消；旧任务的进度、失败和解码结果不会修改下一任务。
- 已发往板端的任务停止后，必须等 STOP 确认才能接受下一任务；4 秒无确认则重建连接。
- 断线结束当前和待启动输出，解除 HTTP 等待；恢复连接后不会自动重放旧歌曲或 TTS。
- 离线请求返回 503，输出忙返回 409。停止输出不停止麦克风识别，也不清空其启动命令。
- 提示音在输出忙时跳过；尚未启动的提示音可让位于音乐/TTS，已关闭语音会话的提示音不播放。
- OTA 接纳与输出接纳共享一把短锁，升级预留期间不能进入新输出；实际传输不持有这把锁。
- TTS 队列有字节/块数上限，停止和断线可唤醒被缓冲阻塞的写入者；EOF 不需要再占一个队列位置。
- 解包、探测和 FFmpeg 子进程观察任务取消事件，退出后清理本任务临时目录。

这轮只收拢输出调度及其与断线/OTA 的边界。识别工作线程、采集会话的完整生命周期，
以及模型/工具路径配置仍在后续整理范围；不把本轮视为整套服务已全部重构。

## 当前文件结构

### 电脑端 VAD（2026-09-09）

`app.py` 将已校验的 24 kHz 双麦包同时交给 `vad_observer.py`。独立 CPU 工作线程
执行持续去直流、24→16 kHz FIR 重采样和 Silero ONNX 双通道推理。两路状态独立，
页面选择左/右麦时使用对应概率，双麦取两路最大值。连续 96 ms 确认开始，512 ms
低概率确认结束；这些是检测状态阈值，不是对话结束判定或唤醒词。

输入使用最多 32 包的非阻塞音频队列，积压时丢弃新音频包并计数；结束录音控制消息
仍可入队，不会因音频队列满而丢失。断流/序号缺口后重置滤波器和模型状态。
原始输入独立保留；处理后 WAV 连续保存，STT 只识别被 VAD 标记含语音的片段。
`/status.vad` 提供状态、两路概率、计算时间、队列丢弃计数；FlitFancy 沿用同源状态
代理，在在线检测时每 600 ms 刷新。未新增端口或权限入口。

VAD 当前使用 AEC 后的音频。关闭 AEC 时使用未做回声消除的重采样音频；停止语音识别
不会停止连续收音/VAD。早期参考采集脚本 `diagnostics/probe_vad_reference.py` 仅用于
对照实验，其提交时间不是声学延迟，不能取代当前播放参考时间轴。

```text
FFV-transfer/
├─ app.py                       常驻设备会话、服务组装、录音/STT
├─ output_scheduler.py          输出任务接纳、独立取消、状态和有界 TTS 缓冲
├─ output_session.py            板端 PCM 发送、消费进度、暂停/停止确认
├─ playback_routes.py           播放 HTTP 接口、上传与解码任务
├─ vad_observer.py              CPU 双麦 AEC/VAD 与处理后录音工作线程
├─ aec_processor.py             WebRTC 10 ms 双通道独立 AEC 适配器
├─ aec_runtime.py               有界播放参考、消费进度与因果对齐
├─ recognition_segments.py      按语音停顿分句与长句保护
├─ wakeword.py                  独立关键词工作线程、开关状态、唤醒事件
├─ voice_turn.py                单条指令状态机、超时与轻提示音
├─ wakeword-keywords.txt        小萤小萤的模型 token
├─ requirements-wakeword.txt    sherpa-onnx 增量依赖
├─ test_aec_runtime.py          与正式链路相同的 AEC/时间轴测试
├─ test_recognition_pipeline.py 处理后音频进入 STT 与双版本录音测试
├─ requirements-vad.txt         VAD 增量依赖固定版本
├─ test_vad_observer.py         重采样/状态/通道隔离与本机性能验证
├─ ffv_v2_client.py             FFVP 认证、TCP 组帧与 UDP 输入
├─ protocol_v2.py               二进制协议和强类型负载
├─ PROTOCOL_V2.md               协议、事件与流量控制约定
├─ media_decode.py              NCM/FFmpeg/FFprobe 解包与 PCM 解码
├─ lyrics.py                    LRC 查找、编码与时间轴
├─ firmware-wifi-transport/     当前 ESP32-S3 固件与私有配置模板
├─ build-wifi-transport.ps1     固件构建入口
├─ test_wifi_transport.py       分片认证/心跳、UDP 输入测试
├─ test_pause_protocol.py       发送上限、暂停/恢复与停止顺序测试
├─ diagnostics/                实板复现、服务/整曲验证与回滚资料
├─ test_lyrics.py               LRC 测试
├─ third_party/pyNCMDUMP/       固定版本 NCM 解包依赖
├─ recordings/                  运行时 WAV 与识别结果
└─ media-work/                  临时媒体与本机配对密钥

S:\FlitFancy\site/
├─ docs/console.html            “侧耳倾听”页面结构
├─ docs/assets/console-audio.js 状态、录音、音量、播放与重启交互
├─ backend/flitfancy_audio.py   固定回环目标的控制代理
├─ backend/flitfancy_http.py    /api/audio/* 同源与权限边界
└─ scripts/start_flitfancy.*    服务启动、健康检查与监听 PID 记录
```

## 音频处理与唤醒

### 当前 AEC 链路（2026-09-09）

```text
OutputSession 发送 PCM ─→ 20 秒有界参考环 ─┐
板端 PLAY_PROGRESS ─────→ 消费位置/状态 ─┤
                                       ↓
UDP 双麦（序号/CRC）→ 24→16 kHz → 两路独立 WebRTC AEC → Silero VAD → STT
          └─ 原始 24 kHz WAV / 原始 AI 订阅             └─ 处理后 16 kHz WAV
```

`RenderReference` 对已发送的 32/48 kHz PCM 持续 FIR 重采样，保存最多 20 秒参考。
板端实际消费进度缩小搜索范围；音频线程每 500 ms 使用已收到的 800 ms 双麦窗口做
相关对齐，两路须共同支持同一位置。初始位置须两次确认；已锁定时 10 ms 内漂移保留
滤波器，较大偏移需连续三次确认再重建。开始阶段先用消费位置估计工作，页面显示“正在对齐”，不能保证开头无回声。

暂停/继续、换曲、音量/麦克风增益变化、重连和采集序号缺口会使旧对齐失效。
参考过期/超出环形缓存时报告等待参考；AEC 异常显式报告并回退原始收音，不静音用户。
队列丢块保留原始 WAV、处理后 WAV 插入对应静音以维持时间长度，缺口前有效片段先提交。

`/status.aec` 提供开关与运行状态。`POST /aec/0|1` 经已有网站同源控制代理的 `aec`
动作调用，严格布尔校验，沿用原有认证边界；默认开启，不跨服务重启保存调试关闭状态。
网站提供 AEC 开关、状态提示和原始录音对比入口。没有修改固件、端口或裸 PCM API。
**`/v1/audio/input` 仍是原始 24 kHz 双声道**；本轮正式接入的是网页 VAD/STT，外部 AI
若需要处理后连续流，应后续增加明确格式的独立接口。

验证见 `../validation/aec-integrated-2026-09-09/RESULT.md`。长时间漂移、极低信噪比
和大音量失真仍需更长实测；短测中曾有独立的 UDP 缺包，不能宣称已经永久消除。

### 弱电平与识别分句

`CaptureScale` 在每路 native AEC 内部使用最大 16 倍有界增益，随峰值降低以保留余量，
输出再逆向还原；不改变原始录音、播放参考或板端采集增益。两路分别计算，不强行匹配电平。

`UtteranceBuffer` 使用处理后 16 kHz PCM 与对应 VAD 概率，约 100 ms 确认开始，
800 ms 停顿提交；保留最多 2 秒弱句首，最长 15 秒总音频强制分段并保留 300 ms 连续余量。
停止/缺包先提交有效片段，缺口补零保持 WAV 时长，禁止跨缺口拼接语句。
这里的分句阈值与网页 96/512 ms 的语音活动展示阈值是不同职责。

### “小萤小萤”唤醒

```text
AEC 后双麦 16 kHz
  ├─ Silero → 语音活动展示 → 识别开启时：按停顿分句 → SenseVoice
  └─ 非阻塞有界队列 → sherpa-onnx 双麦独立关键词流
                       └─ 带代次的 wake 事件 → 网络会话线程 → MicCapture.start
```

默认启用关键词检测。唤醒进入 `VoiceTurn` 单条指令会话，等待 8 秒无人说话自动退出，
一句结束后停止收取，最多 45 秒；手动网页/USER 双击仍为 10 分钟连续识别。进入识别后暂停
关键词推理，结束后有 3 秒间隔。当前实现收取指令与识别结果，LLM/TTS 回复尚未接入。
不保存唤醒前录音，用户应在提示音后再说指令。

工作线程独立于 AEC/VAD 与设备网络线程，队列最多 32 个 10 ms 包。积压超过 500 ms、
序号缺口、音频重连、切麦、开关变化时使模型上下文失效。开关/停止变更递增代次，
网络线程只领取当前代次的待触发事件，拒绝推理期间关闭或停止产生的迟到事件。
任何模型异常仅使唤醒显示错误，不阻断手动识别、播放或原始 AI 音频流。

模型是 `sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20`，采用 chunk16、int8 encoder/joiner、
float decoder、CPU 单线程。两个声道独立流，选单麦时只处理该路。只在关键词输入上施加
固定 8 倍、限幅到 [-1,1] 的增益，不影响录音/AEC/STT。
关键词文件为 `x iǎo y íng x iǎo y íng @小萤小萤`；依赖和模型各自有版本/校验记录。

`GET /status` 中 `wakeword` 返回 enabled、status、keyword、detections、source、last_detected、
queue_drops、CPU 占用及 error；CPU 百分比按一个核心计。
`POST /wakeword/0|1` 经网站 `POST /api/audio/control` 的 `wakeword` 布尔动作调用，
保持现有回环/同源与权限限制，不开放任意目标地址。开关原子保存于 `media-work/voice-settings.json`。
没有新增端口，也没有把模型或录音上传到公网。

### 本轮验证与限制

`python -m unittest test_wakeword` 验证持久开关、推理中关闭、队列溢出、过期事件、
单次领取及正式 MicCapture 会话启动。`python diagnostics/verify_wakeword.py` 验证实际模型：
正常/弱合成关键词、左右路分别触发；普通合成句及真人非关键词录音不触发。
真人唤醒、2 m 距离、口音、音乐中的漏唤醒/误唤醒率须实测；模型离线正例不替代这些验收。
AEC 的低音量双讲已获用户确认，高音量失真和回声仍是开放项。

音乐仍在电脑内存中解码整首 PCM；更长内容可另做有界 FFmpeg 流式生产。
板端 VAD/WakeNet、测向、波束形成以及对外 AEC 后 PCM 流尚未实现；OTA 已实现，见顶部说明。
旧实验和架构原文在 `../history/2026-09-09-pre-wakeword-docs/`，不作为当前实现说明。

## 单条指令交互

`voice_turn.py` 定义 cue → waiting-speech → listening → processing → complete/timeout/error。
缺少清楚的文字结果为 unrecognized，手动/断线结束有独立原因。识别任务携带所属会话对象，
异步结果不会推进另一次会话；旧会话文本不再混进新会话的网页列表。录音工作线程在手动停止后
仍处理 finish 控制消息之前已经排队的音频，保留尾句。

单条指令沿用 UtteranceBuffer 的约 800 ms 停顿结束；15 秒长句分段继续收取，45 秒为会话保护上限。
首段还没有开始说话时等待 8 秒，提示音用时不消耗这 8 秒。识别任务队列失败会使状态显示错误，
不无限停在 processing。原始与处理后 WAV 继续保存。

空闲提示为 300 ms、峰值约 -24 dBFS 的双音提示，沿用用户播放音量。使用现有 LIVE PCM
与 FFVP 输出会话，发送的确切 PCM 同时进入 AEC 数字参考。提示期间的音频不进入指令分句。
音乐/TTS/暂停中的输出拥有优先权，提示不排在音乐后面，也不暂停、混音或重启现有音轨；
此时只有网页状态反馈。没有新增模型或固件任务。

`POST /voice/start` 为本地 AI 或诊断主动开始同样的单条指令流程；不伪造关键词检测计数。
要求板子在线、模型 ready、没有现存识别会话，沿用 7865 回环与 Origin/Host 防护。
`POST /stop` 仍可结束；`GET /status.voice_turn` 返回 phase、wait_remaining、pending、text、reason 等字段。
网页手动识别按钮仍调用原 `/start` 接口；不改变现有同源控制代理的动作集合。
