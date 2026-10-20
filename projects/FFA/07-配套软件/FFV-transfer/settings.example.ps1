# Copy to settings.local.ps1 and enter paths on your own computer.
$env:FFV_BOARD_HOST = 'firefly-voice.local'
$env:FFV_ASR_MODEL = Join-Path $PSScriptRoot 'models\SenseVoiceSmall'
$env:FFV_WAKE_MODEL = Join-Path $PSScriptRoot 'models\sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20'
$env:FFV_PAIRING_KEY_FILE = Join-Path $PSScriptRoot 'media-work\wifi-pairing.key'
# FFmpeg and FFprobe default to PATH. Set absolute paths if necessary.
# $env:FFV_FFMPEG = 'C:\Tools\ffmpeg\bin\ffmpeg.exe'
# $env:FFV_FFPROBE = 'C:\Tools\ffmpeg\bin\ffprobe.exe'
# Optional model startup self-test, using your own WAV file:
# $env:FFV_ASR_SELF_TEST_WAV = 'C:\Audio\self-test.wav'
