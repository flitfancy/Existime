FIREFLY VOICE / FFV-transfer · 源码整理版

当前音频服务源码已按独立目录整理。原业务代码、协议、API、OTA、第三方说明和现有测试均保留。
打包适配仅涉及本机路径、配置入口、构建输出位置，以及把原先必需的私人 ASR 自测 WAV 改为可选配置。
原版说明目录保留原 README，供查阅历史功能和实测范围。

安装与启动
1. 使用 Windows Python 3.11 创建自己的 .venv。
2. 安装 requirements.txt；版本来自本次现有可用环境，不包含整个环境或模型权重。
3. 按 models/README.txt 放置 ASR/唤醒模型，安装 FFmpeg/FFprobe。
4. 把 settings.example.ps1 复制为被忽略的 settings.local.ps1，填写自己的模型、工具及配对文件路径。
5. 配对文件为独立的 32 字节文件，需与自己的板子配置对应；本副本没有携带原配对文件。
6. 运行 run.ps1 -Check 查看依赖与文件是否齐全。此检查不启动设备连接。
7. 配置完成后运行 run.ps1，在浏览器打开 http://127.0.0.1:7865/。

run.ps1 优先使用本目录 .venv/Scripts/python.exe，也可通过 -Python 指定解释器。
默认板子发现名称为 firefly-voice.local，可用 FFV_BOARD_HOST 覆盖。
FFV_ASR_MODEL、FFV_WAKE_MODEL、FFV_PAIRING_KEY_FILE、FFV_FFMPEG、FFV_FFPROBE、FFV_LYRICS_ROOT 控制本机路径。
FFV_ASR_SELF_TEST_WAV 可选；未指定时不会读取原电脑上的私人参考录音。
requirements.txt 中 Torch 版本移除了原机器的 CUDA 后缀，服务默认 CPU；新建完整环境安装尚未在本次重新验证。

可选功能
character_voice.py 默认使用 Windows 临时 TTS；data/tts.example.json 为安全示例，私人 tts.local.json 没有复制。
integrations 中保留 AstrBot 插件和安装工具；只在需要时按该目录说明操作，不要把自己的 AstrBot data 复制进此仓库。
集成测试需要自己安装 AstrBot，或通过 ASTRBOT_APP_ROOT 指定源码目录；本次没有改动现有 AstrBot 服务。
pyNCMDUMP 的源码和许可证已保留。AV3A 原生解码器是可选外部依赖，按 third_party/av3a/README.md 自行配置。
THIRD_PARTY.md、各许可证及模型下载来源继续保留。

开发与验证
test_*.py 为当前主机测试；其中一些是独立 main() 脚本，一些使用 unittest。
test_wifi_transport.py 使用临时本机套接字验证认证、分片与 UDP，不连接真实设备。
test_vad_observer.py 的录音基准需显式设置 FFV_ASR_SELF_TEST_WAV，并自行安装其基准依赖；录音本身没有打包。
package_firmware.py 从相邻 06-固件源码/build-wifi-transport 读取用户自己的编译结果，只打包到被忽略的 media-work。
本次没有启动正式音频服务、加载原模型、播放声音或更新板子。

授权
FlitFancy 原创软件部分采用 MIT，见 LICENSE.txt 和 SOFTWARE-NOTICE.txt；上游来源及第三方许可证继续有效。
