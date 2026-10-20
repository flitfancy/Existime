# 本地 AI 接口

统一服务地址：`http://127.0.0.1:7865`。这些是同一端口下的不同路径，不需为每项功能另开端口。
仅供本机调用，沿用回环来源、Host/Origin 与 Fetch Metadata 限制。网页仍经 2671 同源代理控制。

## 播放可视化

`GET /status` 的 `playback_spectrum` 返回 `active`、`step_ms=50` 和最多 16 组、每组 16 个 0–100 的频段强度。
复用现有 16 kHz 播放参考，显示 50–7600 Hz；按板端消耗帧数估算播放位置，并计入音量。
它是数字播放参考的可视化，不是扬声器实际声压测量。HTTP 工作线程计算 FFT，不占用传输或识别线程。
网页沿用状态轮询，按 50 ms 显示已有片段；暂停、停止、连接中断或 800 ms 片段过期时回落，禁止随机补帧。

## 从板子到 AI

### 完整指令文字（推荐作为对话入口）

`GET /v1/voice/events` → `text/event-stream`（SSE）。

首次连接从当前时刻开始；只在明确提供 `Last-Event-ID` 请求头或 `?after=事件ID` 时续接。
最近 256 条事件保留在内存，不跨音频服务重启保存。事件 ID 为服务代次加序号。
连接首先收到 `stream.ready`，每 15 秒发送保活注释。

| 事件 | 含义 | AI 应如何使用 |
|---|---|---|
| `capture.started` | 新的手动或唤醒会话开始 | 更新界面/会话关联 |
| `transcript.final` | 一段 STT 最终文字；长句可能有多个分段 | 可显示字幕；不要与完整指令同时重复请求 AI |
| `voice.turn.completed` | 单条指令收取结束，全部分段识别完毕 | 用 `data.text` 发起一次 AI 请求 |
| `voice.turn.ended` | 超时、停止、未识别或错误 | 检查 `data.phase/reason`；空文字不发起 AI 请求 |
| `stream.reset` | 服务重启、续接游标过期或异常 | 显式重建订阅状态；不会悄悄重放过期指令 |

每条 JSON 包含 `id`、`type`、`time`、`data`。完整指令的 `data` 中有 `session`、`text`、
`phase`、`reason`；分段文字有 `session`、`chunk`、`source`、`playback_overlap` 与 `mode`。
消费者应以事件 ID/会话 ID 去重，并在成功处理后保存游标；SSE 不替代业务侧的幂等处理。

人工停止时，停止前已排队的音频仍会完成识别。有文字的结束事件可能 reason=manual/disconnect/limit；
对话应用可只接受 reason=utterance，避免把断线或取消中的半句话当指令。

```python
from voice_client import events

for event in events():
    if event['type'] == 'voice.turn.completed':
        turn = event['data']
        if turn['reason'] == 'utterance':
            print(turn['session'], turn['text'])  # 交给自己的 AI；不要执行为 shell 指令
```

事件等待原始音频队列结束标记和最后一个 STT 任务完成后才发出，一次会话只发一次终态事件。
它提供的是当前电脑端 AEC/VAD/SenseVoice 的结果。

### 原始连续音频

`GET /v1/audio/input` → `application/octet-stream`。

- 24,000 Hz，PCM signed 16-bit little-endian，双声道交错：左 U6、右 U7。
- 无 WAV 文件头；每 10 ms 双麦数据对应 960 字节。HTTP 读取边界不保证等于音频包边界。
- 响应头 `X-Audio-Format: s16le`、`X-Audio-Sample-Rate: 24000`、`X-Audio-Channels: 2`。
- **这是原始收音，不是 AEC 后音频。** 当前未提供处理后 PCM 的独立外部流。
- 原始 HTTP 流不携带每包序号；输入/订阅缺口在 `/status` 计数，不可假定永久无缺包。

## 从 AI 到板子

`POST /v1/audio/output?rate=48000`，body 为 PCM16 单声道音频，Content-Type 为 `application/octet-stream`。
支持 32,000 或 48,000 Hz，可使用 HTTP 分块上传；不能直接提交文本、WAV 文件头或 MP3。
文字可先经下面的本地 TTS 接口转换成规定的 PCM，也可由自己的 TTS 生成。服务在板端播放完成后返回 JSON，包含 frames、rate、bytes。
扬声器正在播放音乐/TTS 时返回 409，由 AI 应用决定排队、取消还是提示忙；不会抢占音乐。

输出任务在接收请求体前原子接纳；板子离线返回 503。成功响应新增 `task_id`，原响应字段保留。
断线、停止、上传异常会结束对应任务，重连不自动播放旧 TTS。主动停止的 TTS 请求不再报告完整播放成功。
分块上传连续 30 秒无数据返回超时；整段播放完成等待最多 660 秒，超时会取消该任务。

```python
from voice_client import play_pcm
result = play_pcm(pcm16_mono_bytes, rate=48000)
```

## 会话控制与状态

- `POST /voice/start`：主动收取单条指令，等同唤醒后的交互，8 秒等候、最长 45 秒。
- `POST /start?seconds=600`：手动连续识别，1～600 秒。
- `POST /stop`：结束识别会话。
- `GET /status`：当前状态、`texts`、`voice_turn`、`wakeword`、`aec`、`vad`。可诊断/显示，不是持久消息队列。
- `POST /stop-playback`、`/pause-playback`、`/resume-playback`：播放控制。

`/status` 新增 `output_task_id` 与 `output_phase`；它们是当前或最近输出的状态，不是持久任务历史。
阶段包含 uploading / decoding / queued / playing / stopping / complete / cancelled / failed。
停止上传、解码和待播任务会释放输出；已开始发送时保持 stopping，等板端 STOP 确认后才释放。
如果 4 秒未收到停止确认，设备会话断开并重连，防止上一任务的数据进入下一任务。
麦克风识别与输出停止独立，`/stop-playback` 不等同于 `/stop`。

## QQ 文件识别与可替换的语音合成

- `POST /v1/audio/transcriptions`：兼容 OpenAI 风格的 multipart `file` 上传，返回 `{"text":"..."}`。
  最大请求 16 MiB、音频 120 秒；经 FFmpeg 转为 16 kHz 单声道，进入与板子共用的 SenseVoice 队列。
  文件识别不会伪装成板子指令，也不改变板子的录音会话。模型未就绪为 503，队列忙为 429。
- `POST /v1/audio/speech`：JSON `{"input":"回复文字","response_format":"pcm"}`。
  `pcm` 返回 48 kHz PCM16 mono；`wav` 返回带头的同规格 WAV。长度 1–1500 字；单次合成，忙时 429。
  当前本机配置为训练后的 GPT-SoVITS v2Pro 角色音色，响应头 `X-Voice-Engine: gpt-sovits`。
  首次合成按需加载模型，之后驻留复用；失败返回 502，不自动改用其他声音。
  `GET /status` 的 `tts` 字段提供阶段、引擎和最近耗时。配置与模型说明见 [TTS.md](TTS.md)。
- `GET /v1/voice/bridge`：AstrBot 心跳与最近一条回复状态；`connected` 根据最近 30 秒心跳计算。
- `POST /v1/voice/bridge`：AstrBot 插件写入 `thinking/text/playing/complete/error` 状态；SSE 同时发出
  `assistant.state`、`assistant.reply`。其中 `complete` 代表板端播放接口已完成，不代表人工确认听感。

用户已选择 AstrBot，插件源码与部署说明见 [integrations/README.md](integrations/README.md)。
全部新增路由仍在本机 7865，未创建新的 STT/TTS 监听端口，未接网站云端 AI。

## 网页文字对话（插件 0.2.0）

- 网页发送到网站同源 `POST /api/dialogue/messages`，由 2671 固定代理到
  `POST /v1/dialogue/messages`。JSON 为 `text`（1–2000 字）和 `request_id`（UUID）。
  返回 202 与 `session`；相同编号和文字重复提交不会重复进入 AstrBot，不自动向其他 AI 回退。
- `GET /v1/dialogue` 与 `/status.conversation` 提供最近 40 条展示快照、同步版本、
  桥接在线状态和最近请求状态。历史由 AstrBot 读取后同步，刷新网页会重新获取，不写浏览器存储。
- 插件使用 `POST /v1/dialogue/snapshot` 同步用户/助手消息，使用 `/v1/dialogue/result`
  报告网页请求完成。没有开放读写任意数据库、指定任意 QQ 会话或覆盖人设的参数。
- 网页回复只显示文字；不会向 QQ 发送，也不会自动播放板子。QQ 与板子流程保持原出口。
- 最多 2 个待处理网页请求，队列忙返回 429；桥接未启动/旧版本返回 503，240 秒等待超时报错，
  不自动重发。缓存最多保留 64 个请求用于当前服务生命周期内去重；不是崩溃恢复消息事务。

网站私有页面采用现有管理员边界：本机回环可直接使用，经隧道的 API 读写需要管理员令牌。
访客 AI 的 `chat_enabled` 保持原值；私有 AstrBot 对话使用独立 `dialogue_enabled`（默认开启）。
