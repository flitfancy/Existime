# FFT current hardware pin map

| 信号 | 原GPIO | 新GPIO | 新模组脚 |
| --- | ---: | ---: | ---: |
| LCD_SCK | 39 | 16 | 9 |
| LCD_MOSI | 40 | 15 | 8 |
| LCD_RESET_N | 41 | 7 | 7 |
| LCD_DC | 42 | 17 | 10 |
| LCD_CS_N | 2 | 18 | 11 |
| BL_PWM | 10 | 5 | 5 |
| WAKE_KEY_N | 9 | 8 | 12 |
| I2C_SDA | 4 | 40 | 33 |
| I2C_SCL | 5 | 39 | 32 |
| HOST_FFS_INT_N | 6 | 48 | 25 |
| HOST_FFS_RESET_N | 7 | 47 | 24 |
| FFS_OC_N | 15 | 2 | 38 |
| FFS_LINK_EN | 18 | 38 | 31 |
| FFS_PWR_EN | 14 | 1 | 39 |

Source: design/SCHEMATIC_CONTRACT.v1.json and native SCH/PCB readback. Header: tactile-bench/src/fft_pins.h. Existing tactile bench was built successfully; no device flashed.
