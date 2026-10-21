# Existime

把自己做的硬件，慢慢整理在这里。

这里会放原理图、PCB、固件和制作资料。实物展示和制作记录，也会放到[云萤 · FlitFancy](https://flitfancy.com/resources.html)。

目前整理了 FFS 环境感知板、FFA 双麦语音交互板和 map 地图灯，工程和软件会随着使用继续完善。

## 项目与资料

硬件项目放在 [`projects/`](projects/)，每个项目一个目录。原理图、PCB、BOM、固件、外壳模型和使用说明随项目整理，具体版本、验证进度和许可证在各项目中说明。

FFS、FFA的首批资料以 [开源预览版 v0.1.0](https://github.com/flitfancy/Existime/releases/tag/v0.1.0) 提供下载；这两个项目的预编译固件后续另行发布。map的硬件变体与已构建固件见其项目下载入口。

## 开源项目

### FFS · FIREFLY-SENSE 环境感知板

![FFS 环境感知板实物正面，白色 PCB。](projects/FFS/08-实物照片/ffs-front.jpg)

六通道环境感知板，配合外部主控采集、显示和传输身边的环境数据。

[项目介绍与照片](projects/FFS/README.md) · [完整资料目录](projects/FFS/README.txt)

### FFA · FIREFLY VOICE 双麦语音交互板

![FFA 语音交互板实装，绿色 PCB，搭配外壳与扬声器。](projects/FFA/08-实物照片/ffa-assembled.jpg)

ESP32-S3 双麦采音与播放终端，配合电脑端服务完成语音识别和交互。

[项目介绍与照片](projects/FFA/README.md) · [完整资料目录](projects/FFA/README.txt)

### map · 省份记录与指南针地图灯

![map charge充电版实物，部分省份灯和中心灯点亮。](projects/map/08-实物照片/map-charge-lit.jpg)

34颗省份灯记录、八方向指南针和电量显示。提供charge板载充电版与battery外部电池版，每版都有完整丝印和无背面丝印两种选择，以及全亮测试/正常使用两类固件。charge已有实物功能验证，battery首版供打样测试。

[项目介绍与下载](projects/map/README.md) · [工作逻辑](projects/map/04-装配资料/使用说明/operation-logic.md)

FFS、FFA首批资料于2026-10-20整理；map资料于2026-10-21整理。各项目的 `MANIFEST.json` 记录文件校验和已有检查范围，版本与验证进度以项目说明为准。

## 授权

FFS、FFA 的原创硬件设计采用 **CERN-OHL-S-2.0**，具体范围和第三方例外见各项目的 `HARDWARE-NOTICE.txt`。

map 的原创硬件设计采用 **CERN-OHL-P-2.0**，软件与原创说明采用 **MIT**；范围及第三方材料说明见[map许可范围](projects/map/LICENSING.md)。

原创固件与配套软件采用 **MIT License**；第三方保留原许可证和声明。软件条款见 [MIT 许可证](LICENSE-SOFTWARE.txt)，完整范围见 [授权说明](LICENSING.txt)。
