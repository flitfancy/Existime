# map v0.2.1 下载说明

发布页：[map · charge / battery 正式发布](https://github.com/flitfancy/Existime/releases/tag/v0.2.1)。源代码和硬件工程固定在[同名标签](https://github.com/flitfancy/Existime/tree/v0.2.1/projects/map)。

| 选择 | 下载 | 内容 |
| --- | --- | --- |
| charge · 完整丝印 | [Existime-map-charge-full-silkscreen-v0.2.1.zip](https://github.com/flitfancy/Existime/releases/download/v0.2.1/Existime-map-charge-full-silkscreen-v0.2.1.zip) | 对应原生工程、Gerber、BOM、坐标、正常/全亮固件及说明 |
| charge · 无背面丝印 | [Existime-map-charge-no-back-silkscreen-v0.2.1.zip](https://github.com/flitfancy/Existime/releases/download/v0.2.1/Existime-map-charge-no-back-silkscreen-v0.2.1.zip) | 对应原生工程、Gerber、BOM、坐标、正常/全亮固件及说明 |
| battery · 完整丝印 | [Existime-map-battery-full-silkscreen-v0.2.1.zip](https://github.com/flitfancy/Existime/releases/download/v0.2.1/Existime-map-battery-full-silkscreen-v0.2.1.zip) | 对应原生工程、Gerber、BOM、坐标、正常/全亮固件及说明 |
| battery · 无背面丝印 | [Existime-map-battery-no-back-silkscreen-v0.2.1.zip](https://github.com/flitfancy/Existime/releases/download/v0.2.1/Existime-map-battery-no-back-silkscreen-v0.2.1.zip) | 对应原生工程、Gerber、BOM、坐标、正常/全亮固件及说明 |
| 完整项目 | [Existime-map-complete-v0.2.1.zip](https://github.com/flitfancy/Existime/releases/download/v0.2.1/Existime-map-complete-v0.2.1.zip) | 四种硬件、全部源码和镜像、物料表、照片、许可与核对资料 |
| 固件与源码 | [Existime-map-firmware-v0.2.1.zip](https://github.com/flitfancy/Existime/releases/download/v0.2.1/Existime-map-firmware-v0.2.1.zip) | charge/battery两版源码、四个BIN、构建/烧录脚本和使用说明 |
| 物料表 | [map-BOM.xlsx](https://github.com/flitfancy/Existime/releases/download/v0.2.1/map-BOM.xlsx) | charge充电版、battery电池版两张表 |
| 文件校验 | [SHA256SUMS.txt](https://github.com/flitfancy/Existime/releases/download/v0.2.1/SHA256SUMS.txt) | 上述7个附件的SHA-256 |

charge为板载充电/锂电保护版；battery为外部1.8–5V输入、3.3V输出电池版，板上无充电和电池保护。完整丝印与无背面丝印电路相同，同一系列共用BOM与固件；无背面版保留正面丝印。

每个硬件版本包只带该版本的工程、制造文件和相应系列的两个预编译BIN，以及所需烧录脚本。Excel物料表仍含两张表，按所选系列使用；对应CSV与装配坐标单独保存在系列目录。完整固件源码在完整项目包及固件包中。

每个ZIP都有MANIFEST.json，记录包内文件及SHA-256；硬件包中不随包提供的资料链接指向同一发布标签的源文件。下载后可用SHA256SUMS.txt核对附件，再按包内README选择文件。

全亮版用于检查43颗LED，正常版用于省份记录、校准、指南针及电量显示。烧录地址均为0x08000000，需要支持STM32C0的OpenOCD与CMSIS-DAP下载器；硬件版本包README给出匹配系列的命令。

现有四份原生工程重新导入DRC均为0；charge正常固件与既有实测镜像一致，battery主机回归通过。项目作者于2026-10-22确认map的实板与功能验证成功，现转正式发布；原始检查记录保留各自检查日期及范围。软件MIT、原创硬件CERN-OHL-P-2.0，第三方及照片范围按随附声明。
