# FFT 压力台架测试源码

这里只提供`tactile-bench`，不是完整FFT应用。程序通过I²C读取ADS1115（0x48），输出压力差分值/激励估计及归一化比值；可使用`zero`、`cal <已知牛顿值>`、`clear`和`info`命令。

程序不初始化LCD、不操作FFS支路，不提供时钟/天气/完整交互界面。标定数据保存在ESP32 NVS；力值只有零点与有效载荷标定后才有意义，需结合真实传感器及机械结构验证。

## 构建

安装PlatformIO Core，在本目录执行：

```sh
pio run --project-dir tactile-bench --environment fft_s3_tactile
```

平台固定为`espressif32@7.0.1`，目标为ESP32-S3，16MB Flash、QIO/OPI内存配置见`platformio.ini`。原始四份构建/源码文件在隔离目录成功编译，未烧录设备；依赖安装使用本机已有缓存，不视为新环境完整复现。

GPIO对照见[PIN_MAP.md](PIN_MAP.md)，统一常量在`tactile-bench/src/fft_pins.h`。依赖的Arduino、Wire、Preferences由上游环境提供，保留原许可证，不打包工具链或缓存。本项目原创代码采用MIT。
