# AstrBot / QQ / Firefly Voice

用户选择：QQ 与板子沿用现有人设、记忆插件和同一段私聊上下文；角色音色还在训练。
插件已升级为 0.2.0，增加网页文字入口与最近 40 条对话快照。QQ/NapCat 未启动时，
只要 AstrBot 和本地音频服务运行，网页与板子仍可聊天；历史继续使用原会话标识，未迁移或复制数据库。

```text
QQ 私聊语音 → AstrBot 原有 Record/STT 预处理 ─┐
                                           ├→ 同一 QQ 会话 → AstrBot / 既有 LLM / 记忆插件
板子唤醒指令 → AEC / VAD / STT → SSE ────────┘                   ├→ QQ 来源：文字 + WAV 语音
                                                              └→ 板子来源：临时 TTS → PCM → Wi-Fi 扬声器
```

源码：本目录 `astrbot_plugin_firefly_voice/`。部署：
`C:/Users/Yunwe/.astrbot/data/plugins/astrbot_plugin_firefly_voice/`。
AstrBot 内置 Python：`S:/Fireflys/Astrbot/backend/python/python.exe`。

`install_astrbot_voice.py` 会检查唯一 QQ 主人及现有选中私聊对话，复制插件，并且只修改
已有 `sensevoice` provider 的 `api_base`（7890 → 7865）。原始配置备份保留在 AstrBot data
目录，不把带凭据的配置复制到这个可分享源码目录。人设、记忆、主动聊天设置均未改写。

更新插件后需在 AstrBot 重载插件或重启 AstrBot；FFV 修改后需重启音频服务。
QQ 实际语音收发需要 QQ/NapCat 在线。`GET http://127.0.0.1:7865/v1/voice/bridge`
的 `connected=true` 只证明插件心跳在线，不证明 QQ 已登录。

当前使用 Windows 自带 `Microsoft Huihui Desktop` 离线声音，接口实现为
`speech_services.py` 与 `temporary_voice.ps1`。它是验证传输与回复路由的临时声音。
训练完成后替换 `/v1/audio/speech` 的后端即可；这次没有启动或下载 VoxCPM 模型。

行为边界：

- QQ 文字保持原文字回复；此 QQ 主人的私聊语音回复为文字加语音，合成失败保留文字。
- 网页只回网页文字，使用 `WebMessageEvent`，与板子事件共享会话但不调用语音播放。
- 板子通过“小萤小萤”或 `/voice/start` 收取单条指令；手动连续识别不会自动提交每句话给 AI。
- 用同一个会话 UMO 共享上下文和配置；本地 `send()` 只调用 7865，不向 QQ 广播回复。
- 板子不自动获得 QQ 管理命令权限；语音识别不是身份认证。
- 当前扬声器只能有一个播放源；音乐占用时 AI 回复保留为文字，状态显示忙。
- 1500 字以上回复只保留文字；对话中断、持久消息事务、流式 TTS、音色训练尚未实现。
- SSE 为有界短期事件；插件重启不补播旧指令，短线续接跳过超过 30 秒的指令。

验证：`test_astrbot_voice.py` 使用 AstrBot 实际类测试共享 UMO、本地发送路由、QQ 语音筛选、
识别后原始语音标记和 TTS 失败的文字回退。不会启动 QQ，也不会向联系人发测试消息。
音频 HTTP 实测见 `../diagnostics/astrbot-voice/interfaces-result.json`。
真人 QQ/板子闭环已通过，用户确认共享上下文正确；证据与边界见
`../diagnostics/astrbot-voice/RESULT.md`。不把此次短测试写成长时间稳定性验收。

## 记忆位置

- AstrBot 持久数据库：`C:/Users/Yunwe/.astrbot/data/data_v4.db`，包含当前选中对话和历史。
  原默认人设与配置继续由 AstrBot 管理，不把人设内容复制进网页 JS。
- 现有 `astrbot_plugin_firefly_memory` 的聊天日志目录：`S:/Fireflys/Memory/diary/raw/`，
  同时沿用现有视觉上下文注入。日志保存与可检索的长期记忆是不同能力，本轮没有新增自动提炼/检索。
- FFV 的 `dialogue_service.py` 只维护有界展示缓存和短期请求状态；网站不把浏览器缓存当作记忆库。
