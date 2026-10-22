# Flitime · 电子沙漏主板

一块基于 ESP32-S3 的电子沙漏主板，用彩屏显示沙粒运动与计时，配合 BMI270、LSM303AGR、温湿度、气压和 RTC 提供运动与环境信息。

当前作为**硬件预发布版 v0.3.0**公开：43×77 mm，四角 R2 mm，双层板；38 个器件、176 个焊盘。原工程仍沿用 SplitTime 的工作名称，原生文件内保留原始名称与工程身份。

## 下载

[Flitime 硬件预发布 v0.3.0](https://github.com/flitfancy/Existime/releases/tag/v0.3.0) 提供[完整项目ZIP](https://github.com/flitfancy/Existime/releases/download/v0.3.0/Existime-Flitime-v0.3.0.zip)及SHA-256校验文件。ZIP按01–08目录收录现有资料，缺少的内容暂空；完整源资料固定在[同名标签](https://github.com/flitfancy/Existime/tree/v0.3.0/projects/Flitime)。

## 项目资料

| 目录 | 当前内容 |
| --- | --- |
| [01-完整工程](01-完整工程/) | 可编辑原生工程、1页原理图及PCB文档源码；工程预览暂空 |
| [02-BOM](02-BOM/) | 原生BOM和UTF-8 CSV，16组36件；暂未提供Excel |
| [03-PCB制造](03-PCB制造/) | Gerber ZIP、钻孔、飞针资料及逐文件解包 |
| [04-装配资料](04-装配资料/) | 原生及UTF-8毫米装配坐标、接线说明 |
| [05-核对记录](05-核对记录/) | 工程身份、物料/坐标对账及整理检查 |
| [06-固件源码](06-固件源码/) | 暂空 |
| [07-配套软件](07-配套软件/) | 暂空 |
| [08-实物照片](08-实物照片/) | 暂空 |

原生CSV为UTF-16、Tab分隔，另附UTF-8逗号分隔版本。JIN和JDBG不加入BOM，但保留在38行坐标中；JDBG按裸焊线孔使用。坐标保留EDA制造原点，未另加贴片厂旋转补偿。

## 接线与使用范围

主板从JIN接入**外部稳压3.3V与保护后GND**，不能接USB 5V或原始电芯。充电、保护和稳压由外部T3lite或合适电源承担，外置USB烧录小板也不包含在本主板工程中。接口定义见[接线说明](04-装配资料/接线说明.md)。

当前未收录可运行固件或整机使用教程。屏幕驱动、实际供电、机械装配、指南针校准及整机实物测试尚待完成。

## 已有检查

原工程读取时原生DRC为0；原生归档与制造ZIP的CRC通过，BOM和38行坐标与当前器件核对通过。Gerber飞针资料中的器件姿态、焊盘位置与连接关系已按EDA原点和网络别名对账。详见[整理核对](05-核对记录/packaging-check.json)及[文件清单](MANIFEST.json)。

本次保留原始工程与制造文件字节，发布为硬件预发布版。完整原生归档在目标机器重新导入、板厂DFM与实物验收尚未由本次整理完成。

## 授权

原创硬件采用[CERN-OHL-S-2.0](LICENSE-HARDWARE.txt)，原创固件与配套软件采用[MIT](LICENSE-SOFTWARE.txt)。第三方材料保留原许可证及权利声明；范围见[硬件说明](HARDWARE-NOTICE.txt)和[软件说明](SOFTWARE-NOTICE.txt)。
