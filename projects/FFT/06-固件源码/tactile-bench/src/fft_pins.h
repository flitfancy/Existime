#pragma once

// FFT hardware pin remap2026-10-22. Generated from verified mapping-result.json.
// Module: ESP32-S3-WROOM-1-N16R8. External connector pinouts unchanged.
namespace fft_pins {
constexpr int kLcdSck = 16;  // LCD_SCK, module pad9
constexpr int kLcdMosi = 15;  // LCD_MOSI, module pad8
constexpr int kLcdReset = 7;  // LCD_RESET_N, module pad7
constexpr int kLcdDc = 17;  // LCD_DC, module pad10
constexpr int kLcdCs = 18;  // LCD_CS_N, module pad11
constexpr int kBacklightPwm = 5;  // BL_PWM, module pad5
constexpr int kWakeKey = 8;  // WAKE_KEY_N, module pad12
constexpr int kI2cSda = 40;  // I2C_SDA, module pad33
constexpr int kI2cScl = 39;  // I2C_SCL, module pad32
constexpr int kFfsInterrupt = 48;  // HOST_FFS_INT_N, module pad25
constexpr int kFfsReset = 47;  // HOST_FFS_RESET_N, module pad24
constexpr int kFfsOvercurrent = 2;  // FFS_OC_N, module pad38
constexpr int kFfsLinkEnable = 38;  // FFS_LINK_EN, module pad31
constexpr int kFfsPowerEnable = 1;  // FFS_PWR_EN, module pad39
constexpr int kUsbDm = 19;
constexpr int kUsbDp = 20;
constexpr int kBoot = 0;
constexpr int kUartTx = 43;
constexpr int kUartRx = 44;
static_assert(kWakeKey >= 0 && kWakeKey <= 21, "Wake input requires an RTC-capableGPIO");
}  // namespace fft_pins
