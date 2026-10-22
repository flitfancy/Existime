Flitime 当前硬件资料（硬件预发布版 v0.3.0）

整理日期：2026-10-22，Asia/Shanghai。
目录与FFS、FFA、map使用同一套01–08结构。项目介绍、接线和验证范围见README.md。

01-完整工程/Flitime-20261022.epro2为完整可编辑原生导出，包含原理图、PCB和项目器件库。
01-完整工程/当前文档源码保留1页原理图与PCB的原始文档记录。
02-BOM为16组36件；原生CSV为UTF-16制表符，UTF8.csv为UTF-8逗号分隔；Excel暂缺。
03-PCB制造包含原始Gerber ZIP及14个逐文件解包成员。
04-装配资料包含38行毫米坐标；JIN和JDBG原生不加入BOM，坐标仍保留。
05-核对记录和根MANIFEST.json记录已有检查与文件哈希。
工程预览、06-固件源码、07-配套软件、08-实物照片当前留空，.gitkeep仅用于保留目录。

主板输入为外部稳压3.3V及保护后GND，不能接5V或原始电芯。外部电源与USB烧录小板不包含在本主板资料中。
硬件CERN-OHL-S-2.0，原创软件MIT，第三方保留原许可；版本与下载：https://github.com/flitfancy/Existime/releases/tag/v0.3.0。
