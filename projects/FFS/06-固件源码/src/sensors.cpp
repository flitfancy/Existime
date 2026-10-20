#include "sensors.h"

#include "config.h"

#include <Adafruit_AS7341.h>
#include <Adafruit_LTR390.h>
#include <NOxGasIndexAlgorithm.h>
#include <SensirionI2CSgp41.h>
#include <SensirionI2cScd4x.h>
#include <VOCGasIndexAlgorithm.h>
#include <Wire.h>

// ---- Shared compensation / algorithm state ----
static float s_sht41TempC = 25.0f;
static float s_sht41Rh = 50.0f;
static VOCGasIndexAlgorithm s_vocAlgo(1.0f);
static SensirionGasIndexAlgorithm s_noxAlgo(
    SensirionGasIndexAlgorithm::ALGORITHM_TYPE_NOX, 1.0f);
static uint8_t s_sgp41ConditioningSamples = 0;
static SensirionI2cScd4x s_scd40Sensor;
static bool s_scd40Measuring = false;
static uint8_t s_scd40ConsecutiveErrors = 0;
static Adafruit_LTR390 s_ltr390Sensor;
enum class Ltr390Phase : uint8_t { Uninitialized, WaitAls, WaitUvs };
static Ltr390Phase s_ltr390Phase = Ltr390Phase::Uninitialized;
static uint32_t s_ltr390Als = 0;

void resetSensorRuntime(uint8_t channel) {
  if (channel == 4) {
    s_sgp41ConditioningSamples = 0;
    s_vocAlgo.reset();
    s_noxAlgo.reset();
  } else if (channel == 5) {
    s_scd40Measuring = false;
    s_scd40ConsecutiveErrors = 0;
  } else if (channel == 3) {
    s_ltr390Phase = Ltr390Phase::Uninitialized;
    s_ltr390Als = 0;
  }
}

void resetSensorRuntime() {
  for (uint8_t channel = 0; channel < 6; ++channel) {
    resetSensorRuntime(channel);
  }
}

// ---- AS7341 auto-exposure state ----
static const float kAs7341Gains[] = {0.5f, 1, 2, 4, 8, 16, 32,
                                     64,  128, 256, 512};
static const as7341_gain_t kAs7341GainCodes[] = {
    AS7341_GAIN_0_5X, AS7341_GAIN_1X,  AS7341_GAIN_2X,  AS7341_GAIN_4X,
    AS7341_GAIN_8X,   AS7341_GAIN_16X, AS7341_GAIN_32X, AS7341_GAIN_64X,
    AS7341_GAIN_128X, AS7341_GAIN_256X, AS7341_GAIN_512X};
static uint8_t s_as7341Atime = 29;
static uint16_t s_as7341Astep = 599;
static uint8_t s_as7341GainIdx = 7;  // 64x default.

void setSht41Compensation(float tempC, float rh) {
  s_sht41TempC = tempC;
  s_sht41Rh = rh;
}

void getSht41Compensation(float& tempC, float& rh) {
  tempC = s_sht41TempC;
  rh = s_sht41Rh;
}

// ---- I2C helpers ----
uint8_t i2cProbeAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission();
}

bool i2cWriteRegister(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  const uint8_t status = Wire.endTransmission();
  if (status != 0) {
    Serial.printf(
        "[i2c] write failed address=0x%02X register=0x%02X value=0x%02X "
        "status=%u\n",
        address, reg, value, status);
    return false;
  }
  return true;
}

bool i2cReadRegister(uint8_t address, uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(address, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

bool i2cReadRegisters(uint8_t address, uint8_t reg, uint8_t* buf,
                      size_t len) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const uint8_t got = Wire.requestFrom(address, static_cast<uint8_t>(len));
  if (got != len) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    buf[i] = Wire.read();
  }
  return true;
}

bool muxWrite(uint8_t value) {
  Wire.beginTransmission(kMuxAddress);
  Wire.write(value);
  const uint8_t status = Wire.endTransmission();
  if (status != 0) {
    Serial.printf("[mux] write failed value=0x%02X status=%u\n", value,
                  status);
    return false;
  }
  return true;
}

bool muxRead(uint8_t& value) {
  if (Wire.requestFrom(kMuxAddress, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

bool powerAccessorySetup() {
  // Set the output latch before configuring pins: reset defaults are 0xFF.
  if (!muxDisconnect() ||
      !i2cWriteRegister(kPowerExpanderAddress, kTca9534OutputRegister,
                        kAccessoryPowerMask) ||
      !i2cWriteRegister(kPowerExpanderAddress, kTca9534PolarityRegister, 0x00) ||
      !i2cWriteRegister(kPowerExpanderAddress, kTca9534ConfigRegister,
                        kPowerConfigSafe)) return false;
  delay(100);  // Let the switched X3 supply settle before the TFT reset/init.
  uint8_t verified = 0;
  return i2cReadRegister(kPowerExpanderAddress, kTca9534OutputRegister, verified) &&
         verified == kAccessoryPowerMask;
}

bool powerAllOff() {
  const bool muxOff = muxWrite(0x00);
  const bool powerOff = i2cWriteRegister(kPowerExpanderAddress,
                                         kTca9534OutputRegister,
                                         kAccessoryPowerMask);
  return muxOff && powerOff;
}

bool powerAllOn() {
  if (!muxWrite(0x00) ||
      !i2cWriteRegister(kPowerExpanderAddress, kTca9534OutputRegister,
                        kAccessoryPowerMask)) {
    return false;
  }
  // One deliberate power cycle at collector start clears continuous-mode
  // state left by the previously flashed firmware. Normal sampling leaves the
  // rails on; only a timed-out channel is power-cycled by recovery. X3 remains
  // powered through the sensor cycle, collector stop and single-branch sweep.
  delay(100);
  const bool configured =
      i2cWriteRegister(kPowerExpanderAddress, kTca9534PolarityRegister, 0x00) &&
      i2cWriteRegister(kPowerExpanderAddress, kTca9534ConfigRegister,
                       kPowerConfigSafe) &&
      i2cWriteRegister(kPowerExpanderAddress, kTca9534OutputRegister,
                       kStreamAllPowerMask | kAccessoryPowerMask);
  delay(100);
  uint8_t verified = 0;
  return configured &&
         i2cReadRegister(kPowerExpanderAddress, kTca9534OutputRegister, verified) &&
         verified == (kStreamAllPowerMask | kAccessoryPowerMask);
}

bool powerBranchOn(uint8_t channel) {
  if (channel > 5) {
    return false;
  }
  return muxWrite(0x00) &&
         i2cWriteRegister(kPowerExpanderAddress, kTca9534OutputRegister,
                          kAccessoryPowerMask) &&
         i2cWriteRegister(kPowerExpanderAddress, kTca9534PolarityRegister,
                          0x00) &&
         i2cWriteRegister(kPowerExpanderAddress, kTca9534ConfigRegister,
                          kPowerConfigSafe) &&
         i2cWriteRegister(kPowerExpanderAddress, kTca9534OutputRegister,
                          static_cast<uint8_t>((1u << channel) | kAccessoryPowerMask));
}

bool setSensorPower(uint8_t channel, bool on) {
  if (channel > 5 || !muxDisconnect()) {
    return false;
  }
  uint8_t outputs = 0;
  if (!i2cReadRegister(kPowerExpanderAddress, kTca9534OutputRegister, outputs)) {
    return false;
  }
  const uint8_t bit = static_cast<uint8_t>(1u << channel);
  const uint8_t next = on ? static_cast<uint8_t>(outputs | bit)
                          : static_cast<uint8_t>(outputs & ~bit);
  if (!i2cWriteRegister(kPowerExpanderAddress, kTca9534OutputRegister, next)) {
    return false;
  }
  uint8_t verified = 0;
  return i2cReadRegister(kPowerExpanderAddress, kTca9534OutputRegister, verified) &&
         verified == next;
}

bool muxConnectChannel(uint8_t channel) {
  if (channel > 5) {
    return false;
  }
  const uint8_t mask = static_cast<uint8_t>(1u << channel);
  return muxWrite(mask);
}

bool muxDisconnect() { return muxWrite(0x00); }

// ---- Branch open/close ----
static bool openBranch(uint8_t channel, bool powerAlreadyOn) {
  if (digitalRead(kSdaPin) != HIGH || digitalRead(kSclPin) != HIGH) {
    return false;
  }
  if (!powerAlreadyOn) {
    if (!powerBranchOn(channel)) {
      return false;
    }
    const uint16_t settleMs =
        channel == 4 ? 50 : (channel == 5 ? 30 : 10);
    delay(settleMs);
  }
  if (!muxConnectChannel(channel)) {
    return false;
  }
  delay(3);
  return i2cProbeAddress(kChannels[channel].sensorAddress) == 0;
}

static bool closeBranch(bool powerAlreadyOn) {
  const bool muxOff = muxDisconnect();
  if (powerAlreadyOn) {
    return muxOff;
  }
  const bool powerOff = i2cWriteRegister(kPowerExpanderAddress,
                                         kTca9534OutputRegister,
                                         kAccessoryPowerMask);
  return muxOff && powerOff;
}

// ---- SHT41 ----
static uint8_t sensirionCrc8(const uint8_t* data, size_t length) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) != 0 ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                              : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

static bool readSht41(uint16_t& rawTemp, uint16_t& rawHum) {
  uint8_t data[6] = {};
  Wire.beginTransmission(kSht41Address);
  Wire.write(0xFD);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  delay(10);
  if (Wire.requestFrom(kSht41Address, static_cast<uint8_t>(6)) != 6) {
    return false;
  }
  for (uint8_t i = 0; i < 6; ++i) {
    data[i] = Wire.read();
  }
  if (sensirionCrc8(&data[0], 2) != data[2] ||
      sensirionCrc8(&data[3], 2) != data[5]) {
    return false;
  }
  rawTemp = (static_cast<uint16_t>(data[0]) << 8) | data[1];
  rawHum = (static_cast<uint16_t>(data[3]) << 8) | data[4];
  return true;
}

// ---- BMP580 ----
static bool bmp580ReadIdentity(uint8_t& chipId, uint8_t& revision,
                               uint8_t& status) {
  return i2cReadRegister(kBmp580Address, kBmp5ChipIdRegister, chipId) &&
         i2cReadRegister(kBmp580Address, kBmp5RevisionIdRegister, revision) &&
         i2cReadRegister(kBmp580Address, kBmp5StatusRegister, status);
}

static bool bmp580Initialize() {
  uint8_t chipId = 0;
  uint8_t revision = 0;
  uint8_t status = 0;
  if (!bmp580ReadIdentity(chipId, revision, status)) {
    return false;
  }
  if (chipId != kBmp5ChipIdPrimary ||
      (status & kBmp5NvmReadyMask) == 0 ||
      (status & (kBmp5NvmErrorMask | kBmp5NvmCommandErrorMask)) != 0) {
    return false;
  }
  if (!i2cWriteRegister(kBmp580Address, kBmp5CommandRegister,
                        kBmp5SoftResetCommand)) {
    return false;
  }
  delay(3);
  uint8_t intStatus = 0;
  if (!i2cReadRegister(kBmp580Address, kBmp5IntStatusRegister, intStatus) ||
      (intStatus & kBmp5PorCompleteMask) == 0) {
    return false;
  }
  uint8_t odr = 0;
  if (!i2cReadRegister(kBmp580Address, kBmp5OdrConfigRegister, odr)) {
    return false;
  }
  const uint8_t standby = static_cast<uint8_t>(
      (odr & ~(kBmp5DeepStandbyDisableMask | kBmp5PowerModeMask)) |
      kBmp5DeepStandbyDisableMask | kBmp5PowerModeStandby);
  if (!i2cWriteRegister(kBmp580Address, kBmp5OdrConfigRegister, standby)) {
    return false;
  }
  delay(3);
  if (!i2cWriteRegister(kBmp580Address, kBmp5OsrConfigRegister,
                        kBmp5PressureEnableMask)) {
    return false;
  }
  uint8_t intSource = 0;
  if (!i2cReadRegister(kBmp580Address, kBmp5IntSourceRegister, intSource) ||
      !i2cWriteRegister(kBmp580Address, kBmp5IntSourceRegister,
                        static_cast<uint8_t>(intSource |
                                             kBmp5DataReadyMask))) {
    return false;
  }
  return true;
}

static bool bmp580ForcedRead(float& temperatureC, float& pressurePa) {
  uint8_t odr = 0;
  if (!i2cReadRegister(kBmp580Address, kBmp5OdrConfigRegister, odr)) {
    return false;
  }
  const uint8_t forced = static_cast<uint8_t>(
      (odr & ~(kBmp5DeepStandbyDisableMask | kBmp5PowerModeMask)) |
      kBmp5DeepStandbyDisableMask | kBmp5PowerModeForced);
  if (!i2cWriteRegister(kBmp580Address, kBmp5OdrConfigRegister, forced)) {
    return false;
  }
  uint8_t intStatus = 0;
  bool ready = false;
  for (uint8_t poll = 0; poll < 50; ++poll) {
    delay(2);
    if (!i2cReadRegister(kBmp580Address, kBmp5IntStatusRegister, intStatus)) {
      return false;
    }
    if ((intStatus & kBmp5DataReadyMask) != 0) {
      ready = true;
      break;
    }
  }
  if (!ready) {
    return false;
  }
  uint8_t data[6] = {};
  if (!i2cReadRegisters(kBmp580Address, kBmp5TemperatureDataRegister, data,
                        sizeof(data))) {
    return false;
  }
  int32_t rawT = static_cast<int32_t>(data[0]) |
                 (static_cast<int32_t>(data[1]) << 8) |
                 (static_cast<int32_t>(data[2]) << 16);
  if ((rawT & 0x00800000L) != 0) {
    rawT |= static_cast<int32_t>(0xFF000000UL);
  }
  const uint32_t rawP = static_cast<uint32_t>(data[3]) |
                        (static_cast<uint32_t>(data[4]) << 8) |
                        (static_cast<uint32_t>(data[5]) << 16);
  temperatureC = static_cast<float>(rawT) / 65536.0f;
  pressurePa = static_cast<float>(rawP) / 64.0f;
  return pressurePa >= 30000.0f && pressurePa <= 125000.0f;
}

// ---- LTR390 ----
// ALS and UVS are two phases of one continuous device. Keep the driver and
// mode state alive so the scheduler never waits in a 600 ms polling loop.
static SensorReadStatus readLtr390(SensorValues& out) {
  if (s_ltr390Phase == Ltr390Phase::Uninitialized) {
    if (!s_ltr390Sensor.begin(&Wire)) {
      return SensorReadStatus::Error;
    }
    s_ltr390Sensor.setGain(LTR390_GAIN_3);
    s_ltr390Sensor.setResolution(LTR390_RESOLUTION_18BIT);
    s_ltr390Sensor.setMode(LTR390_MODE_ALS);
    s_ltr390Phase = Ltr390Phase::WaitAls;
    return SensorReadStatus::Pending;
  }
  if (!s_ltr390Sensor.newDataAvailable()) {
    return SensorReadStatus::Pending;
  }
  if (s_ltr390Phase == Ltr390Phase::WaitAls) {
    s_ltr390Als = s_ltr390Sensor.readALS();
    s_ltr390Sensor.setMode(LTR390_MODE_UVS);
    s_ltr390Phase = Ltr390Phase::WaitUvs;
    return SensorReadStatus::Pending;
  }
  out.alsRaw = s_ltr390Als;
  out.uvRaw = s_ltr390Sensor.readUVS();
  s_ltr390Sensor.setMode(LTR390_MODE_ALS);
  s_ltr390Phase = Ltr390Phase::WaitAls;
  return SensorReadStatus::Fresh;
}

// ---- SGP41 ----
static uint16_t encodeSgp41Rh(float rh) {
  float v = rh * 655.35f;
  if (v < 0.0f) {
    v = 0.0f;
  }
  if (v > 65535.0f) {
    v = 65535.0f;
  }
  return static_cast<uint16_t>(v);
}

static uint16_t encodeSgp41Temp(float tempC) {
  float v = (tempC + 45.0f) * 65535.0f / 175.0f;
  if (v < 0.0f) {
    v = 0.0f;
  }
  if (v > 65535.0f) {
    v = 65535.0f;
  }
  return static_cast<uint16_t>(v);
}

static SensorReadStatus readSgp41(SensorValues& out) {
  const uint16_t compRh = encodeSgp41Rh(s_sht41Rh);
  const uint16_t compT = encodeSgp41Temp(s_sht41TempC);
  SensirionI2CSgp41 sensor;
  sensor.begin(Wire);
  if (s_sgp41ConditioningSamples < 10) {
    uint16_t conditioningVoc = 0;
    const uint16_t error =
        sensor.executeConditioning(compRh, compT, conditioningVoc);
    if (error != 0) {
      return SensorReadStatus::Error;
    }
    ++s_sgp41ConditioningSamples;
    out.srawVoc = conditioningVoc;
    // Conditioning is progressing normally, but VOC/NOx indices are not yet
    // valid. Keep the last public snapshot until sample 11.
    return SensorReadStatus::Pending;
  }
  uint16_t error =
      sensor.measureRawSignals(compRh, compT, out.srawVoc, out.srawNox);
  if (error != 0) {
    return SensorReadStatus::Error;
  }
  out.vocIndex = s_vocAlgo.process(out.srawVoc);
  out.noxIndex = s_noxAlgo.process(out.srawNox);
  return SensorReadStatus::Fresh;
}

// ---- SCD40 ----
static SensorReadStatus readScd40(SensorValues& out) {
  if (!s_scd40Measuring) {
    s_scd40Sensor.begin(Wire, SCD41_I2C_ADDR_62);
    int16_t error = s_scd40Sensor.startPeriodicMeasurement();
    if (error != 0) {
      // Recover a sensor left in periodic mode by a reset during collection.
      s_scd40Sensor.stopPeriodicMeasurement();
      delay(500);
      error = s_scd40Sensor.startPeriodicMeasurement();
    }
    if (error != 0) {
      return SensorReadStatus::Error;
    }
    s_scd40Measuring = true;
    s_scd40ConsecutiveErrors = 0;
    return SensorReadStatus::Pending;  // first sample needs about five seconds.
  }

  bool ready = false;
  const int16_t error = s_scd40Sensor.getDataReadyStatus(ready);
  if (error != 0) {
    if (++s_scd40ConsecutiveErrors >= 3) {
      // Re-enter the guarded stop/start recovery on the next poll. A single
      // transient bus error does not tear down a healthy periodic session.
      s_scd40Measuring = false;
      s_scd40ConsecutiveErrors = 0;
    }
    return SensorReadStatus::Error;
  }
  if (!ready) {
    // Do not block the other five channels while the next sample matures.
    return SensorReadStatus::Pending;
  }
  if (s_scd40Sensor.readMeasurement(out.co2Ppm, out.tempC, out.rh) != 0) {
    if (++s_scd40ConsecutiveErrors >= 3) {
      s_scd40Measuring = false;
      s_scd40ConsecutiveErrors = 0;
    }
    return SensorReadStatus::Error;
  }
  s_scd40ConsecutiveErrors = 0;
  // A successful I2C transfer can still contain an invalid CO2 sample.
  // Do not refresh the collector's last-valid deadline on zero CO2.
  if (out.co2Ppm == 0) {
    return SensorReadStatus::Error;
  }
  return SensorReadStatus::Fresh;
}

// ---- Public readSensor ----
SensorReadStatus readSensorStatus(uint8_t channel, SensorValues& out,
                                  bool powerAlreadyOn) {
  out = SensorValues();
  if (channel > 5) {
    return SensorReadStatus::Error;
  }
  const bool opened = openBranch(channel, powerAlreadyOn);
  bool ok = false;
  if (opened) {
    switch (channel) {
      case 0: {
        uint16_t rawT = 0;
        uint16_t rawH = 0;
        if (readSht41(rawT, rawH)) {
          out.tempC = -45.0f + 175.0f * static_cast<float>(rawT) / 65535.0f;
          out.rh = -6.0f + 125.0f * static_cast<float>(rawH) / 65535.0f;
          ok = true;
        }
        break;
      }
      case 1: {
        if (bmp580Initialize()) {
          ok = bmp580ForcedRead(out.tempC, out.pressurePa);
        }
        break;
      }
      case 2: {
        Adafruit_AS7341 sensor;
        if (sensor.begin(kAs7341Address, &Wire)) {
          sensor.setATIME(s_as7341Atime);
          sensor.setASTEP(s_as7341Astep);
          sensor.setGain(kAs7341GainCodes[s_as7341GainIdx]);
          // Record the settings that produced this sample before adapting the
          // state for the next sweep.
          out.as7341Atime = s_as7341Atime;
          out.as7341Astep = s_as7341Astep;
          out.as7341Gain = kAs7341Gains[s_as7341GainIdx];
          ok = sensor.readAllChannels(out.spectral);
          sensor.disableAll();
          // FLICKER：检测光源频闪（100/120Hz 等）。detectFlickerHz 会重写
          // SMUX 配置且内部约 500ms 延时；readAllChannels 每次开头都会
          // 重建光谱 SMUX，因此放在光谱读取之后即可安全交替。
          // 每 6 次 CH2 采样测一次（约 30 秒），其余轮次沿用最近结果。
          static uint16_t s_flickerHz = 0;
          static uint8_t s_flickerCountdown = 0;
          if (s_flickerCountdown == 0) {
            s_flickerHz = sensor.detectFlickerHz();
            s_flickerCountdown = 6;
          } else {
            --s_flickerCountdown;
          }
          out.flickerHz = s_flickerHz;
          if (ok) {
            const uint16_t clearRaw = out.spectral[10];
            bool saturated = clearRaw >= 60000;
            for (uint8_t i = 0; i < 12; ++i) {
              if (out.spectral[i] == 0xFFFF) {
                saturated = true;
              }
            }
            if (saturated) {
              ok = false;  // unreliable this round; exposure drops next round.
              if (s_as7341GainIdx > 0) {
                --s_as7341GainIdx;
              } else if (s_as7341Atime > 1) {
                s_as7341Atime >>= 1;
              }
            } else if (clearRaw >= 50000) {
              // Too bright: reduce exposure one step (gain first, then ATIME).
              if (s_as7341GainIdx > 0) {
                --s_as7341GainIdx;
              } else if (s_as7341Atime > 1) {
                s_as7341Atime >>= 1;
              }
            } else if (clearRaw < 1500) {
              // Too dark: increase exposure (gain first, then ATIME).
              if (s_as7341GainIdx < 10) {
                ++s_as7341GainIdx;
              } else if (s_as7341Atime < 250) {
                s_as7341Atime = static_cast<uint8_t>(
                    s_as7341Atime * 2 > 255 ? 255 : s_as7341Atime * 2);
              }
            }
          }
        }
        break;
      }
      case 3: {
        const SensorReadStatus status = readLtr390(out);
        const bool closed = closeBranch(powerAlreadyOn);
        out.ok = status == SensorReadStatus::Fresh && closed;
        if (!closed) {
          return SensorReadStatus::Error;
        }
        return status;
      }
      case 4: {
        const SensorReadStatus status = readSgp41(out);
        const bool closed = closeBranch(powerAlreadyOn);
        out.ok = status == SensorReadStatus::Fresh && closed;
        if (!closed) {
          return SensorReadStatus::Error;
        }
        return status;
      }
      case 5:
        {
          const SensorReadStatus status = readScd40(out);
          const bool closed = closeBranch(powerAlreadyOn);
          out.ok = status == SensorReadStatus::Fresh && closed;
          if (!closed) {
            return SensorReadStatus::Error;
          }
          return status;
        }
        break;
      default:
        break;
    }
  }
  const bool closed = closeBranch(powerAlreadyOn);
  out.ok = ok && closed;
  return out.ok ? SensorReadStatus::Fresh : SensorReadStatus::Error;
}

bool readSensor(uint8_t channel, SensorValues& out, bool powerAlreadyOn) {
  return readSensorStatus(channel, out, powerAlreadyOn) ==
         SensorReadStatus::Fresh;
}

// ---- RUNALL: one full sweep with verbose output ----
void runAllSensorsOnce() {
  Serial.println();
  Serial.println("============================================================");
  Serial.println(" FIREFLY-SENSE RUNALL (six sensors, one channel at a time)");
  Serial.println("============================================================");
  for (uint8_t ch = 0; ch < 6; ++ch) {
    SensorValues v;
    const bool ok = readSensor(ch, v, false);
    Serial.printf("[runall] CH%u %s ok=%d", ch, kChannels[ch].name, ok ? 1 : 0);
    if (ok && ch == 0) {
      Serial.printf(" T=%.2fC RH=%.2f%%", v.tempC, v.rh);
    }
    if (ok && ch == 1) {
      Serial.printf(" T=%.2fC P=%.2fhPa", v.tempC, v.pressurePa / 100.0f);
    }
    if (ok && ch == 4) {
      Serial.printf(" VOC=%ld NOx=%ld SRAW=%u/%u", v.vocIndex, v.noxIndex,
                    v.srawVoc, v.srawNox);
    }
    if (ok && ch == 5) {
      Serial.printf(" CO2=%u T=%.2fC RH=%.2f%%", v.co2Ppm, v.tempC, v.rh);
    }
    Serial.println();
  }
  Serial.println("[FINAL] RUNALL_COMPLETE ch0-5 executed");
}
