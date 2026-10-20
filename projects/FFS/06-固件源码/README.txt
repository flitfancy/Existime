FFS 固件源码 · 1.3.15（源码整理版）

来源是当前 FIREFLY-SENSE-ds 工程。ESP32-S3 N16R8、PlatformIO；默认环境 esp32s3_n16r8_usb。
src、lib、scripts、tests、platformio.ini 和字体声明已保留。
原版说明目录保存原 README、1.3.15 升级说明和原版本清单，供追溯；其中历史机器地址不作为本副本的启动步骤。

构建
先在自己的 Python 环境安装 PlatformIO，再在本目录运行：
  powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
Windows 用户目录或工具权限导致 esptool 的 _contrib/intelhex 读取失败时，可使用已有的绕行入口：
  powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -SafeEsptool
脚本使用英文临时工作目录构建，避免旧版 ESP32 链接器不能处理中文工程路径。
输出为 build-output/firefly-sense-1.3.15-ota.bin。该目录被 Git 忽略；本次构建验证产物放在 Existime/local 中。

配置
原私人 wifi_config.h 未复制；当前位置只保留从空白示例生成的头文件。
未提供本地头文件时，源码回退到 wifi_config.example.h，因此不带凭据也能编译。
网络、TCP 主机等运行配置优先来自板上的 EEPROM。新板的首次配置仍沿用原固件机制，尚未改造成通用首次配网工具。
config_eeprom.h 中保留原 TCP 默认主机参数，换电脑时需核对自己的实际配置。
此副本没有写入或更新任何板子。

工具与验证
scripts/build_ota.py：隔离构建、空网络默认值、镜像标识和摘要检查。
scripts/upload_ota.py：向用户指定的板子上传应用镜像。
scripts/test_heart_rate.py、test_heart_led.py、test_phone_ble.py 等：主机侧协议与状态机测试，需要自行提供 Zig C++ 编译器。
lib/Firefly_ST7735/NOTICE.md 与 FONT_NOTICES.txt 保留第三方来源和声明。
硬件与新板完整复现状态继续看项目根目录及 MANIFEST.json；编译通过不等于实板验收。

授权
FlitFancy 原创软件部分采用 MIT，见 LICENSE.txt 和 SOFTWARE-NOTICE.txt；第三方保留原许可证及声明。
