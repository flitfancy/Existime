#pragma once

#include <Arduino.h>

// ---- Pins ----
#ifdef BENCH_SDA
constexpr int kSdaPin = BENCH_SDA;
#else
constexpr int kSdaPin = 4;
#endif
#ifdef BENCH_SCL
constexpr int kSclPin = BENCH_SCL;
#else
constexpr int kSclPin = 5;
#endif
#ifdef BENCH_INT
constexpr int kIntPin = BENCH_INT;
#else
constexpr int kIntPin = 6;
#endif
constexpr int kResetPin = 7;

constexpr uint32_t kI2cFrequencyHz = 100000;
constexpr uint16_t kI2cTimeoutMs = 50;

// ---- Firmware identity ----
// Sent independently from the CSV schema so hosts can identify the flashed
// binary without breaking existing 28-field data parsers.
constexpr char kFirmwareVersion[] = "1.3.15";  // D8 uses equal bright/dark halves at one third of the BPM frequency.
constexpr uint8_t kSensorSchemaVersion = 5;
constexpr char kSchedulerName[] = "independent-v1";

// ---- External 1.77-inch SPI TFT ----
// Screen pins 3..7 -> GPIO39,40,41,42,2. Pin8 LEDA now uses 3V3 directly.
// The screen's SDA is SPI MOSI. It does not use the FFS CH6 I2C branch.
// Keep the bench environment usable without attaching an unverified display.
#ifdef BENCH_SDA
constexpr bool kTftEnabled = false;
#else
constexpr bool kTftEnabled = true;
#endif
constexpr int kTftSckPin = 39;
constexpr int kTftMosiPin = 40;
constexpr int kTftResetPin = 41;
constexpr int kTftDcPin = 42;
constexpr int kTftCsPin = 2;
constexpr int kTftBacklightPin = -1;  // GPIO1 disconnected; backlight fixed to 3V3.
constexpr uint32_t kTftSpiFrequencyHz = 8000000;
constexpr uint32_t kTftInitFrequencyHz = 1000000;
constexpr uint32_t kTftRefreshMs = 1000;
constexpr uint8_t kTftRotation = 1;  // Landscape 160 x 128.

// ---- I2C addresses ----
constexpr uint8_t kPowerExpanderAddress = 0x20;
constexpr uint8_t kLedExpanderAddress = 0x21;
constexpr uint8_t kEepromAddress = 0x50;
constexpr uint8_t kMuxAddress = 0x70;
constexpr uint8_t kSht41Address = 0x44;
constexpr uint8_t kBmp580Address = 0x46;
constexpr uint8_t kAs7341Address = 0x39;
constexpr uint8_t kLtr390Address = 0x53;
constexpr uint8_t kSgp41Address = 0x59;
constexpr uint8_t kScd40Address = 0x62;
constexpr uint8_t kNoExpectedAddress = 0x00;

// ---- TCA9534 registers ----
constexpr uint8_t kTca9534InputRegister = 0x00;
constexpr uint8_t kTca9534OutputRegister = 0x01;
constexpr uint8_t kTca9534PolarityRegister = 0x02;
constexpr uint8_t kTca9534ConfigRegister = 0x03;

// ---- Power / MUX ----
constexpr uint8_t kPowerOutputsOff = 0x00;
constexpr uint8_t kPowerConfigSafe = 0x80;
constexpr uint8_t kStreamAllPowerMask = 0x3F;  // Fixed sensors CH0..CH5.
// U4 P6 -> U13 -> X3 pin1. Independent accessory power, not BLE/logical CH6.
constexpr uint8_t kAccessoryPowerMask = kTftEnabled ? 0x40 : 0x00;

// ---- LED mapping (U5 bits) ----
// Channel LEDs: CH0->D9(bit7), CH1->D2(bit0), CH2->D3(bit1),
// CH3->D5(bit3), CH4->D6(bit4), CH5->D7(bit5).
constexpr uint8_t kChannelLedBits[6] = {7, 0, 1, 3, 4, 5};
constexpr uint8_t kHeartbeatLedBit = 2;   // D4
constexpr uint8_t kHeartRateLedBit = 6;   // D8, U5 P6, active low
constexpr uint8_t kLedOutputsOff = 0xFF;
constexpr uint8_t kLedConfigOutputs = 0x00;

// Heartbeat: no link -> blink 1 s on / 2 s off; linked -> off.
constexpr unsigned long kHeartbeatOnMs = 1000;
constexpr unsigned long kHeartbeatPeriodMs = 3000;
constexpr unsigned long kHeartbeatConfirmTimeoutMs = 15000;

// D8: equal on/off halves every three estimated beats; not synchronised to measured beats.
constexpr uint8_t kHeartRateLedBeatDivisor = 3;
constexpr unsigned long kChannelLedMinPulseMs = 80;

// ---- Auto dark (CH3 LTR390 ambient light) ----
// 环境光低于阈值时自动熄灭六路通道活动灯和 D8 心率灯；
// 心跳灯（D4，链路状态）保持原语义不受影响。ALS 原始计数在默认
// 3x 增益下约为 0.2 lux/count：阈值 120 ≈ 24 lux（暗室），
// 回差 60 ≈ 12 lux，防止临界光照下反复闪烁。
constexpr bool kAutoDarkEnabled = true;
constexpr uint32_t kAutoDarkAlsThreshold = 120;       // 低于此值进入熄灯
constexpr uint32_t kAutoDarkAlsHysteresis = 60;       // 高于 阈值+回差 才恢复

// ---- Channels ----
struct Stage5Branch {
  uint8_t channel;
  uint8_t sensorAddress;
  const char* name;
  const char* probe;
  uint16_t supplyLimitMa;
};

constexpr Stage5Branch kCh0Branch = {0, kSht41Address, "CH0 SHT41", "C17", 80};
constexpr Stage5Branch kCh1Branch = {1, kBmp580Address, "CH1 BMP580", "C21", 80};
constexpr Stage5Branch kCh2Branch = {2, kAs7341Address, "CH2 AS7341", "C25", 80};
constexpr Stage5Branch kCh3Branch = {3, kLtr390Address, "CH3 LTR390", "C29", 80};
constexpr Stage5Branch kCh4Branch = {4, kSgp41Address, "CH4 SGP41", "C32", 80};
constexpr Stage5Branch kCh5Branch = {5, kScd40Address, "CH5 SCD40", "C37", 300};

constexpr Stage5Branch kChannels[6] = {kCh0Branch, kCh1Branch, kCh2Branch,
                                        kCh3Branch, kCh4Branch, kCh5Branch};

// ---- CSV / storage ----
constexpr char kStreamCsvPath[] = "/firefly_stream.csv";
constexpr uint32_t kRingMaxBytes = 1024 * 1024;  // 1 MB bounded log.
constexpr char kStreamCsvHeader[] =
    "CSV,uptime_ms,cycle,channel,sensor,ok,temp_c,rh_pct,als_raw,uv_raw,"
    "f1_415,f2_445,f3_480,f4_515,f5_555,f6_590,f7_630,f8_680,clear_raw,"
    "nir_raw,voc_index,nox_index,sraw_voc,sraw_nox,co2_ppm,pressure_pa,"
    "as7341_atime,as7341_astep,as7341_gainx,sample_age_ms,sample_seq,"
    "error_streak,firmware_version,schema_version,scheduler,flicker_hz,rssi_dbm,"
    "heart_rate_bpm,hr_connected,hr_contact,hr_state";

// ---- Independent acquisition / reporting ----
// Acquisition keeps the physical sensor requirements fixed. MODE changes only
// how often a six-channel snapshot is emitted to serial/Wi-Fi/Flash.
constexpr uint32_t kSensorSamplePeriodMs[6] = {
    10000,  // CH0 SHT41: temperature/RH and SGP41 compensation.
    30000,  // CH1 BMP580: slowly changing pressure.
    5000,   // CH2 AS7341: light spectrum.
    1000,   // CH3 LTR390: alternate non-blocking ALS / UV phases.
    1000,   // CH4 SGP41: required cadence for the gas-index algorithms.
    1000    // CH5 SCD40: non-blocking ready poll; samples mature every ~5 s.
};

// Watch fresh results, not 1 Hz ready polls or MODE-dependent report cycles.
// LTR390 needs an ALS/UV pair; SCD40 produces one result every ~5 seconds.
constexpr uint32_t kSensorResultPeriodMs[6] = {10000, 30000, 5000, 2000, 1000, 5000};
constexpr uint32_t kRecoveryMissedPeriods = 3;
constexpr uint32_t kRecoverySchedulingSlackMs = 2000;
constexpr uint32_t kRecoveryPowerOffMs = 1000;
constexpr uint32_t kScd40RecoveryPowerOffMs = 5000;
// Conservative rail settling, with extra margin for the CO2 module.
constexpr uint32_t kSensorPowerUpMs[6] = {10, 10, 10, 10, 50, 1000};
// SGP41 performs ten conditioning commands before its first valid result.
constexpr uint32_t kSensorWarmupMs[6] = {0, 0, 0, 1000, 12000, 1000};
constexpr uint32_t kRecoveryCooldownMs = 60000;

enum class CollectorMode : uint8_t {
  Slow = 1,    // emit one six-channel snapshot every 60 s
  Normal = 2,  // emit one six-channel snapshot every 10 s (power-on default)
  Fast = 3     // emit one six-channel snapshot every 2 s
};

constexpr uint32_t kModeSlowReportMs = 60000;
constexpr uint32_t kModeNormalReportMs = 10000;
constexpr uint32_t kModeFastReportMs = 2000;

// ---- BMP580 registers (Bosch SensorAPI v1.5.0) ----
constexpr uint8_t kBmp5ChipIdRegister = 0x01;
constexpr uint8_t kBmp5RevisionIdRegister = 0x02;
constexpr uint8_t kBmp5IntSourceRegister = 0x15;
constexpr uint8_t kBmp5TemperatureDataRegister = 0x1D;
constexpr uint8_t kBmp5IntStatusRegister = 0x27;
constexpr uint8_t kBmp5StatusRegister = 0x28;
constexpr uint8_t kBmp5OsrConfigRegister = 0x36;
constexpr uint8_t kBmp5OdrConfigRegister = 0x37;
constexpr uint8_t kBmp5CommandRegister = 0x7E;
constexpr uint8_t kBmp5ChipIdPrimary = 0x50;
constexpr uint8_t kBmp5SoftResetCommand = 0xB6;
constexpr uint8_t kBmp5NvmReadyMask = 0x02;
constexpr uint8_t kBmp5NvmErrorMask = 0x04;
constexpr uint8_t kBmp5NvmCommandErrorMask = 0x08;
constexpr uint8_t kBmp5DataReadyMask = 0x01;
constexpr uint8_t kBmp5PorCompleteMask = 0x10;
constexpr uint8_t kBmp5PressureEnableMask = 0x40;
constexpr uint8_t kBmp5DeepStandbyDisableMask = 0x80;
constexpr uint8_t kBmp5PowerModeMask = 0x03;
constexpr uint8_t kBmp5PowerModeStandby = 0x00;
constexpr uint8_t kBmp5PowerModeForced = 0x02;
