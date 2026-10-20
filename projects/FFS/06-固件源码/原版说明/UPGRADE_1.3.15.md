# FIREFLY-SENSE 1.3.15 · D8 心率明暗节奏

日期：2026-10-10（Asia/Shanghai）。目标：ESP32-S3 N16R8，CSV schema 5。

D8 改为心率指示灯，使用 U5（0x21）P6，低电平点亮。完整周期为
`180000 / BPM` 毫秒，亮、暗各占一半；72 bpm 时亮 1.25 秒、暗 1.25 秒。
这是按 BPM 计算的均匀明暗节奏，不是对每次实际搏动的相位同步或亮度渐变。
无有效心率、断线、接触不良、样本超过 15 秒未更新或夜间熄灯时，D8 熄灭。

程序读取已有心率缓存，在主循环中按时间切换输出，不使用等待延时。D8 不再
表示采集模式。`GET /heart-rate` 的 `indicator` 给出周期、亮暗时长、夜间状态、
期望输出和 U5 实际输出寄存器回读；寄存器回读不能替代实物观察。

1.3.14 的 120 毫秒短闪已由本版等长明暗节奏替代。升级镜像不携带真实 Wi-Fi
默认凭证，沿用板内配置、现有 TFT 页面、六路采集和手机 BLE 协议。

在工程目录内升级：

```powershell
py -3 scripts\upload_ota.py 192.168.1.33 artifacts\firefly-sense-1.3.15-ota.bin
```

需要回退到原显示与模式灯版本时，使用同一入口上传包内的 1.3.13 应用：

```powershell
py -3 scripts\upload_ota.py 192.168.1.33 artifacts\firefly-sense-1.3.13-ota.bin
```

回退是手动操作，不会自动发生。OTA 只更新备用应用分区。
测试与本次安装记录分别在 `artifacts/test-results-1.3.15.json` 和
`artifacts/ota-1.3.15-20261010/`；打包文件带逐文件校验和及回退镜像。
