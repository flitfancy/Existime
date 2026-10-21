# 43灯全亮测试固件

适用于map charge/battery；两版LED网络、驱动地址和直驱引脚一致，已通过原生接线快照与生成映射核对。

上电后43颗灯持续全亮，循环写入和回读五颗TCA9534。没有按键、指南针、电量或充电逻辑，不访问外部EEPROM。`main.c`、`startup.S`和`linker.ld`与charge已确认的测试程序一致。

在`06-固件源码`目录执行`python scripts/build_firmware.py all --mode all-on`重编译。产物在`releases/charge/all-led-on.bin`和`releases/battery/all-led-on.bin`，两份二进制相同。charge实物记录见`charge-physical-acceptance.json`；这份记录不作为battery实板验收。
