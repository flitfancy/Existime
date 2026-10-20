#include "leds.h"

#include "sensors.h"

#include <Wire.h>

// Local shadow of the U5 output latch: every write goes through this cache,
// so we never need to read back the expander just to toggle one LED.
static uint8_t s_ledOutput = kLedOutputsOff;
static bool s_channelActivity[6] = {};
static bool s_channelReadDone[6] = {};
static unsigned long s_channelActivityStartedAt[6] = {};
static bool s_darkMode = false;
static bool s_heartRateLedOn = false;
static bool s_heartRateActive = false;
static uint32_t s_heartRatePhaseStart = 0;
static uint32_t s_heartRatePeriodMs = 0;

static bool writeLedOutput() {
  Wire.beginTransmission(kLedExpanderAddress);
  Wire.write(kTca9534OutputRegister);
  Wire.write(s_ledOutput);
  return Wire.endTransmission() == 0;
}

static bool setLedBit(uint8_t bit, bool on) {
  if (on) {
    s_ledOutput = static_cast<uint8_t>(s_ledOutput & ~(1u << bit));
  } else {
    s_ledOutput = static_cast<uint8_t>(s_ledOutput | (1u << bit));
  }
  return writeLedOutput();
}

// 熄灯模式关闭通道活动灯和 D8，D4 链路状态灯位不动。
static void forceDecorativeOff() {
  for (uint8_t channel = 0; channel < 6; ++channel) {
    s_ledOutput |= (1u << kChannelLedBits[channel]);
  }
  s_ledOutput |= (1u << kHeartRateLedBit);
  if (writeLedOutput()) s_heartRateLedOn = false;
  s_heartRateActive = false;
  s_heartRatePeriodMs = 0;
  for (uint8_t channel = 0; channel < 6; ++channel) {
    s_channelActivity[channel] = false;
    s_channelReadDone[channel] = false;
  }
}

bool ledInit() {
  s_ledOutput = kLedOutputsOff;
  s_heartRateLedOn = false;
  s_heartRateActive = false;
  s_heartRatePeriodMs = 0;
  // TCA9534: one register per I2C transaction.
  Wire.beginTransmission(kLedExpanderAddress);
  Wire.write(kTca9534OutputRegister);
  Wire.write(kLedOutputsOff);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  Wire.beginTransmission(kLedExpanderAddress);
  Wire.write(kTca9534PolarityRegister);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  Wire.beginTransmission(kLedExpanderAddress);
  Wire.write(kTca9534ConfigRegister);
  Wire.write(kLedConfigOutputs);
  return Wire.endTransmission() == 0;
}

void ledSetChannel(uint8_t channel, bool on) {
  if (channel < 6 && !(s_darkMode && on)) {
    setLedBit(kChannelLedBits[channel], on);
  }
}

void ledActivityStart(uint8_t channel) {
  if (channel >= 6) {
    return;
  }
  s_channelActivity[channel] = true;
  s_channelReadDone[channel] = false;
  s_channelActivityStartedAt[channel] = millis();
  ledSetChannel(channel, true);
}

void ledActivityFinish(uint8_t channel) {
  if (channel < 6 && s_channelActivity[channel]) {
    s_channelReadDone[channel] = true;
  }
}

void ledUpdateActivity() {
  const uint32_t now = millis();
  for (uint8_t channel = 0; channel < 6; ++channel) {
    if (s_channelActivity[channel] && s_channelReadDone[channel] &&
        now - s_channelActivityStartedAt[channel] >= kChannelLedMinPulseMs) {
      ledSetChannel(channel, false);
      s_channelActivity[channel] = false;
      s_channelReadDone[channel] = false;
    }
  }
}

bool ledActivityBusy() {
  for (uint8_t channel = 0; channel < 6; ++channel) {
    if (s_channelActivity[channel]) {
      return true;
    }
  }
  return false;
}

// ---- Heartbeat (D4) ----
static bool s_heartbeatOn = false;
static unsigned long s_heartbeatPhaseStart = 0;

void ledUpdateHeartbeat(bool linkConfirmed) {
  const uint32_t now = millis();
  if (linkConfirmed) {
    if (s_heartbeatOn) {
      s_heartbeatOn = false;
      setLedBit(kHeartbeatLedBit, false);
    }
    s_heartbeatPhaseStart = now;
    return;
  }
  // Not linked: 1 s on, then 2 s off (3 s period).
  const uint32_t phase = now - s_heartbeatPhaseStart;
  const bool wantOn = phase < kHeartbeatOnMs;
  if (wantOn != s_heartbeatOn) {
    s_heartbeatOn = wantOn;
    setLedBit(kHeartbeatLedBit, wantOn);
  }
  if (phase >= kHeartbeatPeriodMs) {
    s_heartbeatPhaseStart = now;
  }
}

// ---- Heart rate (D8) ----
void ledUpdateHeartRate(bool valid, uint16_t bpm) {
  const uint32_t now = millis();
  if (!valid || bpm == 0 || s_darkMode) {
    s_heartRateActive = false;
    s_heartRatePeriodMs = 0;
    if (s_heartRateLedOn && setLedBit(kHeartRateLedBit, false)) s_heartRateLedOn = false;
    return;
  }
  s_heartRatePeriodMs = 60000UL * kHeartRateLedBeatDivisor / bpm;
  if (!s_heartRateActive) {
    s_heartRatePhaseStart = now;
    s_heartRateActive = true;
  }
  const uint32_t elapsed = now - s_heartRatePhaseStart;
  if (elapsed >= s_heartRatePeriodMs) {
    s_heartRatePhaseStart = now - elapsed % s_heartRatePeriodMs;
  }
  const uint32_t width = s_heartRatePeriodMs / 2;
  const bool wantOn = uint32_t(now - s_heartRatePhaseStart) < width;
  if (wantOn != s_heartRateLedOn && setLedBit(kHeartRateLedBit, wantOn)) s_heartRateLedOn = wantOn;
}

String ledHeartRateJson() {
  uint8_t output = 0;
  const bool read = i2cReadRegister(kLedExpanderAddress, kTca9534OutputRegister, output);
  String out = "{\"led\":\"D8\",\"beat_divisor\":";
  out += kHeartRateLedBeatDivisor;
  out += ",\"period_ms\":"; out += s_heartRatePeriodMs;
  out += ",\"pulse_ms\":"; out += s_heartRatePeriodMs / 2;
  out += ",\"off_ms\":"; out += s_heartRatePeriodMs - s_heartRatePeriodMs / 2;
  out += ",\"active\":"; out += s_heartRateActive ? "true" : "false";
  out += ",\"dark_mode\":"; out += s_darkMode ? "true" : "false";
  out += ",\"commanded_on\":"; out += s_heartRateLedOn ? "true" : "false";
  out += ",\"output_on\":";
  out += read ? ((output & (1u << kHeartRateLedBit)) == 0 ? "true" : "false") : "null";
  out += '}';
  return out;
}

void ledSetDarkMode(bool dark) {
  if (dark == s_darkMode) {
    return;
  }
  s_darkMode = dark;
  if (dark) {
    forceDecorativeOff();
  }
}

void ledAllOff() {
  s_ledOutput = kLedOutputsOff;
  if (writeLedOutput()) s_heartRateLedOn = false;
  s_heartbeatOn = false;
  s_heartRateActive = false;
  s_heartRatePeriodMs = 0;
  for (uint8_t channel = 0; channel < 6; ++channel) {
    s_channelActivity[channel] = false;
    s_channelReadDone[channel] = false;
  }
}
