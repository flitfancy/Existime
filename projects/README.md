# 硬件项目

每个项目单独一个目录，以项目名称命名。

当前已整理：

- [`Flitime/`](Flitime/README.md)：电子沙漏主板硬件预发布 v0.3.0；工程、BOM、制造资料和坐标已整理，固件、软件、照片暂空。
- [`FFS/`](FFS/README.md)：感知板工程、制造资料、1.3.15 固件、Windows 接收端、Android 采集端及核对记录。
- [`FFA/`](FFA/README.md)：语音板工程、制造资料、0.4.1 固件、FFV-transfer 音频服务及核对记录。
- [`map/`](map/README.md)：地图灯charge充电版/battery电池版，各有完整丝印/无背面丝印工程与制造文件，以及全亮测试/正常使用固件。charge与battery均已由作者确认实板验证成功。

FFS、FFA 对应正式版 v0.1.0；map 当前正式版 v0.2.1。作者已于2026-10-22确认三个项目验证成功，详细资料和原始检查记录见各项目说明。

四个项目使用同一套资料目录，具体内容按项目说明：

```text
project-name/
├── 01-完整工程/     原生原理图、PCB、网表和预览
├── 02-BOM/          Excel、参数表及原生物料表
├── 03-PCB制造/      Gerber、钻孔及文件检查
├── 04-装配资料/     贴片坐标、接线和使用资料
├── 05-核对记录/     来源、对账、测试及验证范围
├── 06-固件源码/     源码、构建、烧录及相应说明
├── 07-配套软件/     配套程序或工具范围说明
├── 08-实物照片/     实物图片和使用范围
├── README.md        项目介绍及下载入口
├── README.txt       完整目录及使用说明
├── LICENSE-HARDWARE.txt
├── LICENSE-SOFTWARE.txt
├── HARDWARE-NOTICE.txt
├── SOFTWARE-NOTICE.txt
└── MANIFEST.json    文件校验与已有检查范围
```

map的charge/battery和丝印选择放在上述目录的下一级；没有独立电脑端服务。硬件与固件版本、已验证功能及待验证内容以各项目说明为准。
