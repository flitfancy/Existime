FFA / FIREFLY VOICE 固件源码 · 0.4.1

当前入口只有 firmware-wifi-transport，保留双麦采集、Wi-Fi、播放、配对认证及 OTA 源码。
旧 USB 诊断固件和旧编译输出没有复制进来。

构建
使用 Arduino CLI，安装 esp32:esp32 3.3.10 后运行：
  powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
需要指定已有 CLI 或配置目录时，使用 -ArduinoCli 和 -ConfigFile。
Windows 上可用 -BuildPath 指向英文工作目录，例如 C:\Build\Existime-FFA。
构建参数为 ESP32-S3、16MB Flash、DIO、OPI PSRAM、自定义分区、原生 USB CDC。

配置
私人 wifi_credentials.local.h 没有复制。源码在缺少该文件时回退到空白 wifi_credentials.example.h，供源码编译检查。
网络和 32 字节配对密钥仍按原固件配置机制提供；电脑端需要对应的独立配对密钥文件。
该空白示例不是可直接联网运行的完整设备配置。通用首次配网流程和不带私人配置的公开固件包仍需另行准备。
原 0.4.1 私人二进制没有复制，也没有执行烧录或 OTA。

授权
FlitFancy 原创软件部分采用 MIT，见 LICENSE.txt 和 SOFTWARE-NOTICE.txt；第三方保留原许可证及声明。
