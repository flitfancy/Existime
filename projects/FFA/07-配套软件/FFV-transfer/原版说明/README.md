# FIREFLY VOICE · 侧耳倾听

本机 AI 音频设备服务：ESP32-S3 通过 Wi-Fi 持续上传双麦，接收音乐/TTS；电脑负责解码、AEC、VAD、唤醒和 SenseVoice。
当前固件入口仅为 `firmware-wifi-transport/`。更早的 USB 固件和工具保留作历史诊断。

## 使用

使用 FlitFancy 控制台 `http://127.0.0.1:2671/console.html`，点击“音频”可幂等启动。
独立诊断也可运行 `start-stt.bat`，打开 `http://127.0.0.1:7865/`；已有服务时无需重复启动。
当前 Python 环境为 `S:/Fireflys/Miniconda3/venv/venv_voxcpm/python.exe`。
更新电脑端代码后，可手动运行 `restart-audio.bat` 加载新版。它只重启已核对身份的 7865
音频进程；播放、识别或 OTA 进行中会拒绝重启，不操作网站后台或板子固件。

- 默认双麦；可选择左麦 U6、右麦 U7 或双麦，1/2/4/8 倍板端增益。
- “小萤小萤”默认开启本地唤醒。空闲时响短提示音，音乐播放时显示网页提示；随后听取一条指令，停顿后自动提交。
- 8 秒没有开始说话自动退出，单次指令最长 45 秒；网页或 USER 双击手动开始识别仍为最长 10 分钟连续模式。
- 唤醒开关与手动识别相互独立，关闭唤醒仍可手动开始识别。停止后 3 秒再进入待唤醒。
- 开启识别后以语音停顿分句，同时保存 AEC 后 WAV 和原始 WAV；已取消固定 3 秒切片。
- 拖入 NCM、FLAC、MP3、WAV、AAC、M4A、OGG、Opus，由本机解码为 48 kHz / PCM16 / 单声道。
- 支持播放音量、暂停/继续、停止及同名 LRC 逐行歌词。进度拖动未实现。
- 播放与识别可以同时运行；已验证低音量音乐下基本准确，高音量回声仍可能干扰。
- AstrBot 语音桥接使用同一个 QQ 私聊会话：QQ 语音先识别，回复文字加语音；板子唤醒指令的回复只播到板子。
  当前是离线临时音色，角色音色后续替换。部署与验证边界见 [integrations/README.md](integrations/README.md)。

## 模型与设置

SenseVoice：`S:/Fireflys/SenseVoice/model`。
关键词模型：`S:/Fireflys/WakeWord/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20`；可用 `FFV_WAKE_MODEL` 覆盖。
增量依赖固定在 `requirements-wakeword.txt`；使用 sherpa-onnx 1.13.7，CPU 单线程、双麦独立流。
`wakeword-keywords.txt` 保存中文拼音 token；模型下载来源及校验值在模型目录的 `download-manifest.json`。
此版本唤醒在电脑运行，服务退出后不提供板端独立唤醒。

`media-work/voice-settings.json` 只保存唤醒开关；AEC 调试开关重启后恢复开启。
运行录音、配对密钥、私有 Wi-Fi 头文件和构建产物不进入可分享源码。

## 文件导航

| 文件/目录 | 职责 |
|---|---|
| `app.py` | 常驻连接、服务组装、识别会话与通用 HTTP API |
| `output_scheduler.py` | 音乐/TTS/提示音统一接纳、独立取消、输出状态及有界缓冲 |
| `output_session.py` | PCM 板端发送、进度、暂停/停止确认与 AEC 数字参考 |
| `playback_routes.py` | 播放接口、上传、解码任务与 HTTP 取消收尾 |
| `ota_service.py` / `OTA.md` | 电脑端固件升级、启动确认和升级说明 |
| `ffv_v2_client.py` / `protocol_v2.py` | FFVP 认证、心跳、PCM 数据与事件 |
| `vad_observer.py` | 独立音频处理线程、重采样、AEC/VAD、保存处理后录音 |
| `aec_runtime.py` / `aec_processor.py` | 播放数字参考、时钟对齐、双麦 WebRTC AEC |
| `recognition_segments.py` | 按停顿分句、句首保留、长句保护 |
| `voice_turn.py` | 单条指令会话、等待超时、结束状态与提示音 |
| `voice_events.py` / `voice_client.py` | 有界文字事件日志、SSE 订阅与 Python 调用示例 |
| `speech_services.py` / `temporary_voice.ps1` | 共用模型的文件识别、可替换 TTS HTTP 接口与离线临时声音 |
| `dialogue_service.py` | 网页文字入口、幂等请求与 AstrBot 最近对话展示缓存 |
| `integrations/astrbot_plugin_firefly_voice/` | AstrBot 正常流水线入口、共享会话与两种回复出口 |
| `wakeword.py` / `wakeword-keywords.txt` | 有界关键词工作线程、“小萤小萤”与会话事件 |
| `media_decode.py` / `lyrics.py` | 本机音频解码及 LRC 解析 |
| `firmware-wifi-transport/` | 当前板端 PDM、I2S、Wi-Fi、按键与灯光 |
| `recordings/` / `media-work/` | 本机运行数据 |
| `diagnostics/` / `test_*.py` | 回归、实板证据与离线检查 |
| `firmware-ring-playback/` / `firmware-stream-playback/` / `firmware-idf5/` | 历史固件，不是当前运行入口 |

调用格式见 [API.md](API.md)，端口和边界见 [ARCHITECTURE.md](ARCHITECTURE.md)，协议见 [PROTOCOL_V2.md](PROTOCOL_V2.md)。
当前结论见 [CURRENT_HANDOFF.md](../CURRENT_HANDOFF.md)。原文历史在 `../history/2026-09-09-pre-wakeword-docs/`。

## 输出调度回归

```powershell
python -m unittest test_output_scheduler test_output_pipeline test_music_upload test_media_cancel test_voice_turn test_ota_service
python test_pause_protocol.py
```

这些检查使用隔离状态、模拟板端或临时解码子进程，不连接真实板子。
需要实板复核时，在服务空闲且新版已加载后手动运行 `python diagnostics/verify_output_scheduler.py`：
它通过现有 HTTP 接口发送静音 WAV/PCM，验证文件解码、暂停/继续/停止、TTS 完成和持续双麦上行。
结果保存在 `media-work/output-scheduler-verification.json`；不代表真人听感或高音量双讲验收。
