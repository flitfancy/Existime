map 核对记录

当前整体检查：final-review.json。
文件、物料、坐标及固件校验：packaging-check.json；全项目文件哈希见项目根目录MANIFEST.json。
charge-silkscreen-check.json与battery-silkscreen-check.json：原生丝印对象差异及制造文件对照。
charge/battery-host-tests.json和charge/battery-normal-build.json：主机回归及Flash/RAM、启动向量检查。
validation-charge.md与package-verification.json：2026-10-20以前的charge实板和初始整理记录，保留历史范围，不作为battery实板验收。

2026-10-21整体复查
四个待发布EPRO2逐份重新原生导入独立本地工程，均为8页原理图，DRC均为0。
charge两份各148个器件、450个引脚网络；battery两份各135个器件、386个引脚网络，位置、朝向、层与BOM标记均核对。
同版工程仅移除背面丝印IMAGE；Gerber仅省略GBO，其余14个成员逐字节一致，解包成员与ZIP一致。
两版的外部预览与工程内预览一致，无背面版旧缩略图缓存已经刷新。
BOM、毫米坐标和UTF-8坐标对账通过；LED及阻容感等按参数填写，芯片及机械件保留型号。
目录已统一为FFS、FFA使用的01–08结构，103份原有源代码、硬件归档、制造包、表格及镜像在移动中保持字节一致。
固定工具链在含空格的独立目录重建四个BIN，与待发布镜像一致；主机断言charge为218381项，battery为220951项。
两张宣传图保持用户原始JPEG字节，明确为charge实物。
FFS既有190份及FFA既有168份清单文件均未改动。

仍待验证
battery实板打样、1.8V带载启动、3.3V纹波、关机输入电流、ADC及亮灯负载下指南针干扰。
无背面变体没有新制实物；未做板厂DFM或完整PCBA放行。本轮未烧录设备。
