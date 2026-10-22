map 固件源码、构建与烧录

当前目录可独立复制；LICENSE.txt为MIT条款，硬件映射范围见随附HARDWARE-NOTICE.txt与LICENSE-HARDWARE.txt。
firmware/charge：已实板验收的charge正常应用v5。
firmware/battery：battery正常应用v6，默认两节碱性AA/AAA，作者已于2026-10-22确认实板功能验证成功。
firmware/all-on：共用43灯全亮测试源码，不访问外部EEPROM。
releases/charge与battery：各有map-app.bin和all-led-on.bin，以及校验清单。

构建
需要Python 3.10或更新版本及ziglang==0.13.0。在本目录执行：
python -m pip install -r requirements-build.txt
python scripts/build_firmware.py all
该命令先检查两版硬件接线映射，再执行主机测试并构建四个下载文件。
仅构建battery正常版：python scripts/build_firmware.py all --hardware battery --mode normal
charge重建必须与既有实测镜像一致；两版全亮也需与charge实测全亮镜像一致，否则脚本终止。

首次烧录
需要支持STM32C0的OpenOCD与CMSIS-DAP下载器，使用自己的探针序列号：
python scripts/flash_firmware.py --hardware charge --mode all-on --serial YOUR_PROBE_SERIAL
python scripts/flash_firmware.py --hardware charge --mode normal --serial YOUR_PROBE_SERIAL
battery板改成--hardware battery。可用--openocd与--scripts指定工具及脚本目录。
脚本核对下载文件哈希、STM32C011身份与16KiB Flash，烧录后读回校验并运行；不清空外部EEPROM。
丝印选择不影响固件。charge/battery正常固件不能混选。

使用与验证
完整按键、校准、省份保存、指南针和电量逻辑见项目04-装配资料/使用说明。
charge/battery主机测试分别通过218381/220951项断言；这不代替battery实际电源、ADC或指南针测试。
