# Existime

把自己做的硬件，慢慢整理在这里。

这里会放原理图、PCB、固件和制作资料。实物展示和制作记录，也会放到[云萤 · FlitFancy](https://flitfancy.com/resources.html)。

目前整理了 FFS 环境感知板和 FFA 双麦语音交互板，工程和软件会随着使用继续完善。

## 项目与资料

硬件项目放在 [`projects/`](projects/)，每个项目一个目录。原理图、PCB、BOM、固件、外壳模型和使用说明随项目整理，具体版本、验证进度和许可证在各项目中说明。

首批资料以 [FFS 与 FFA 开源预览版 v0.1.0](https://github.com/flitfancy/Existime/releases/tag/v0.1.0) 提供下载。硬件工程和源码包含在资料包中，预编译固件后续另行发布。

## 开源项目

### FFS · FIREFLY-SENSE 环境感知板

![FFS 环境感知板实物正面，白色 PCB。](projects/FFS/08-实物照片/ffs-front.jpg)

六通道环境感知板，配合外部主控采集、显示和传输身边的环境数据。

[项目介绍与照片](projects/FFS/README.md) · [完整资料目录](projects/FFS/README.txt)

### FFA · FIREFLY VOICE 双麦语音交互板

![FFA 语音交互板实装，绿色 PCB，搭配外壳与扬声器。](projects/FFA/08-实物照片/ffa-assembled.jpg)

ESP32-S3 双麦采音与播放终端，配合电脑端服务完成语音识别和交互。

[项目介绍与照片](projects/FFA/README.md) · [完整资料目录](projects/FFA/README.txt)

这两份资料于 2026-10-20 从当前 EDA 工程整理。每个项目的 `MANIFEST.json` 记录来源、文件校验和已有检查范围；当前作为持续完善的个人硬件项目开放。

## 授权

FFS、FFA 的原创硬件设计采用 **CERN-OHL-S-2.0**，具体范围和第三方例外见各项目的 `HARDWARE-NOTICE.txt`。

原创固件与配套软件采用 **MIT License**；第三方保留原许可证和声明。软件条款见 [MIT 许可证](LICENSE-SOFTWARE.txt)，完整范围见 [授权说明](LICENSING.txt)。
