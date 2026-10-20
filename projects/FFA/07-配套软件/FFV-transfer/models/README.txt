外部模型与工具

SenseVoice：把兼容的 SenseVoiceSmall 模型放在此处的 SenseVoiceSmall 目录，或设置 FFV_ASR_MODEL。
当前加载器至少需要 model.pt、config.yaml、am.mvn；完整模型还应保留其配置引用的分词等文件。

唤醒：当前模型为 sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20。
wakeword-download.json 保存原下载来源、压缩包大小和 SHA-256，未复制模型权重。
目录至少需要 tokens.txt，以及源码 wakeword.py 中列出的 encoder、decoder、joiner ONNX 文件。
可用 FFV_WAKE_MODEL 指向自行下载和校验过的目录。

FFmpeg、FFprobe 默认从 PATH 查找，也可通过 FFV_FFMPEG、FFV_FFPROBE 指定。
模型、运行环境、外部可执行程序和私人参考录音不属于此源码副本。
