# 本地角色 TTS

2026-09-13：已接入 GPT-SoVITS v2Pro。对外接口仍为 FFV 的
`POST http://127.0.0.1:7865/v1/audio/speech`，没有新增监听端口。

## 当前声音

- 项目环境：`S:\Fireflys\GPT-SoVITS\GPT-SoVITS-v2pro-20250604-nvidia50`。
- GPT：`GPT_weights_v2Pro/FFV_v2_0913-e60.ckpt`。
- SoVITS：`SoVITS_weights_v2Pro/FFV_v2_0913_e30_s510.pth`。
- 用户指定参考音频：`result_data/slicer_opt_v2/流萤voice_raw_1.wav_0000658560_0000816320.wav`，4.93 秒。
- 参考文本：`一起走走吧，美好的时光总是过得那么快。`
- 中文/中英混合，语速 1.0，CUDA 半精度，固定 seed 42。

实际本机路径、权重及推理参数保存在 `data/tts.local.json`，不提交 Git。
后续换参考音频时，需要同时修改 `ref_audio_path` 与完全对应的 `prompt_text`。
修改配置后下一次合成会重新加载；在 GPT-SoVITS WebUI 选择其他模型不会自动改变 FFV 的固定配置。

训练权重完整并已验证能推理，不等于这些轮次一定是最佳音色；听感和长文本稳定性仍可继续调整。

## 调用关系

```text
AstrBot 回复文字（板子、网页或 QQ 语音入口）
  → FFV /v1/audio/speech
    → character_voice.py 管理一个驻留子进程
      → gpt_sovits_worker.py 使用 GPT-SoVITS 自带 Python / CUDA
        → 生成 WAV → FFmpeg 转为 48 kHz / PCM16 / 单声道
  → 板子：原 /v1/audio/output 播放接口
  → QQ：原 AstrBot 插件附加 WAV 语音回复
```

AstrBot 插件 0.2.2 在调用 TTS 前过滤中文 `（…）` 和英文 `(…)` 旁白，支持嵌套与跨行；
未闭合的左括号之后也不朗读。引号内台词保留，网页、QQ 文字和记忆保存原文。
过滤后没有可朗读内容时正常结束，不请求 TTS、不生成空语音。插件升级后需重载或重启 AstrBot。
通用 TTS 接口本身不删改调用者的文字。此接口仅合成声音，
不会自行发送 QQ 消息或播放扬声器；输出仍由原入口控制。

首次合成自动加载模型，当前实测冷启动约 30–60 秒；模型驻留后短句约 1–2 秒，
具体取决于文字长度。原训练/推理 WebUI 可以关闭，FFV 不依赖其网页；同时打开时
可能各占一份模型显存。当前实现是整段合成后播放，尚未改为边合成边播放。

## 生命周期与失败处理

- 2026-09-14：SenseVoice 默认改用 CPU、4 个推理线程，为大模型和 TTS 留出显存。
  `FFV_ASR_DEVICE=cuda:0` 可显式切回 GPU；`/status.asr_device` 显示实际设备。
  三段 3–5 秒实录的 CPU 纯推理约 0.18–0.21 秒，正式 HTTP 接口约 0.26–0.30 秒；
  对照结果位于本机 `media-work/asr-cpu-benchmark.json`，仅代表这些短句。
  TTS 驻留后强制卸载并重新加载 Ollama，以约 5237 token 的真实上下文离线回放三次：
  首次 15.332 秒，缓存命中的两次为 6.051 / 6.612 秒，全部正常结束；
  同时请求识别耗时 0.309–0.332 秒且文字一致。没有发送 QQ 消息或改写对话历史。
  结果在 `media-work/cpu-coexistence-results.json`，回放脚本为
  `diagnostics/validate_cpu_coexistence.py`。这验证了重载和共存，尚不代表长时间压力测试。
- 使用 Ollama 为默认模型的两个本机 AstrBot 配置组将 `provider_settings.request_max_retries`
  设为 1（总尝试次数），单独的云端配置组保持原值。该设置按配置组生效，组内临时切换模型也会沿用。
  插件通过启动完成和请求前钩子，将仅指向回环地址 11434 的 SDK 客户端 `max_retries` 设为 0，
  卸载插件时恢复原值；没有修改 AstrBot 核心源码。云端 SDK 客户端不受影响。
  原有约 120 秒 HTTP 等待上限保留；这不是覆盖工具调用、队列、合成的整轮硬截止时间。

- 私有 stdin 管道提交任务；结果和音频通过每个任务的私有文件返回，模型日志与音频分离。
- 每次完整耗时限制为模型加载/推理最多 150 秒、格式转换最多 20 秒；已有单请求限制与 429 保留。
- 失败返回 502 并保留文字回复，不悄悄切换回 Windows 声音。
- `GET /status` 的 `tts` 提供引擎、阶段、最近耗时与错误；详细日志在 `data/tts-worker.log`。
- 成功或失败都清理任务输出；超时先回收子进程。正常退出释放模型；父服务被关闭后
  子进程的 stdin 收到 EOF，会在当前推理完成后退出。
- 临时推理 YAML 在 `media-work/tts-worker-*`，不会覆盖用户的 WebUI 推理配置。
- 本机配置、日志、模型和参考音频不随网站发布。没有本机配置时保留旧 Windows 临时声音；
  也可明确设置 `engine` 为 `windows-temporary`。启用 GPT-SoVITS 时缺文件会明确报错。

## 验证

`python -m unittest test_character_voice test_speech_services test_output_pipeline test_dialogue_service`

本机试听与实测结果：`media-work/tts-integration/`，属于私有运行产物。
