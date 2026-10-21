# 43灯全亮测试固件

适用于map v1/v2；两版LED网络、驱动地址和直驱引脚一致，已通过原生接线快照与生成映射核对。

上电后43颗灯持续全亮，循环写入和回读五颗TCA9534。没有按键、指南针、电量或充电逻辑，不访问外部EEPROM。`main.c`、`startup.S`和`linker.ld`与v1已确认的测试程序一致。

在`06-固件源码`目录执行`python scripts/build_firmware.py all --mode all-on`重编译。产物在`releases/v1/all-led-on.bin`和`releases/v2/all-led-on.bin`，两份二进制相同。v1实物记录见`v1-physical-acceptance.json`；这份记录不作为v2实板验收。
