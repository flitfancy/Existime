#include "collector.h"

#include "leds.h"
#include "sensors.h"
#include "web_panel.h"
#include "wifi_link.h"

#include <LittleFS.h>

// Forward-declared from wifi_link.cpp.
void wifiSendLine(const String& line);

static bool s_running = false;
static CollectorMode s_mode = CollectorMode::Normal;
static SensorValues s_latest[6];
static bool s_hasFresh[6] = {};
static uint32_t s_lastFreshAt[6] = {};
static uint32_t s_sampleSeq[6] = {};
static uint32_t s_nextSampleAt[6] = {};
static uint16_t s_errorStreak[6] = {};
static uint8_t s_scanCursor = 0;
static uint32_t s_reportCycle = 0;
static uint32_t s_nextReportAt = 0;

enum class RecoveryPhase : uint8_t { Running, PowerOff, Settling };
struct ChannelRecovery {
  RecoveryPhase phase = RecoveryPhase::Running;
  uint32_t freshDeadline = 0;
  uint32_t phaseDeadline = 0;
  uint32_t nextAttemptAt = 0;
  uint32_t attempts = 0;
  uint32_t restoreErrors = 0;
  bool awaitingFresh = false;
  bool coolingDown = false;
};
static ChannelRecovery s_recovery[6];

static bool timeReached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

static uint32_t noDataWindowMs(uint8_t channel) {
  return kSensorResultPeriodMs[channel] * kRecoveryMissedPeriods +
         kRecoverySchedulingSlackMs;
}

static void recoveryLog(uint8_t channel, const char* event) {
  String line("[recovery] CH");
  line += channel;
  line += ' ';
  line += event;
  line += " attempt=";
  line += s_recovery[channel].attempts;
  line += " uptime_ms=";
  line += millis();
  line += " seq=";
  line += s_sampleSeq[channel];
  line += " errors=";
  line += s_errorStreak[channel];
  Serial.println(line);
  wifiSendLine(line);
}

static void startRecovery(uint8_t channel, SensorReadStatus status) {
  ChannelRecovery& state = s_recovery[channel];
  ++state.attempts;
  state.awaitingFresh = true;
  recoveryLog(channel, status == SensorReadStatus::Pending ?
                          "timeout-not-ready" : "timeout-read-error");
  const bool off = setSensorPower(channel, false);
  recoveryLog(channel, off ? "power-off" : "power-off-unconfirmed");
  // Even an unconfirmed write may have switched the rail: always attempt to
  // restore it. Never publish a pre-reset cache as a recovered measurement.
  s_hasFresh[channel] = false;
  state.phase = RecoveryPhase::PowerOff;
  state.phaseDeadline = millis() +
      (channel == 5 ? kScd40RecoveryPowerOffMs : kRecoveryPowerOffMs);
  state.nextAttemptAt = millis() + kRecoveryCooldownMs;
  state.coolingDown = true;
  state.restoreErrors = 0;
}

static void advanceRecovery(uint8_t channel, uint32_t now) {
  ChannelRecovery& state = s_recovery[channel];
  if (state.coolingDown && timeReached(now, state.nextAttemptAt)) {
    state.coolingDown = false;
  }
  if (state.phase == RecoveryPhase::Running ||
      !timeReached(now, state.phaseDeadline)) return;
  if (state.phase == RecoveryPhase::PowerOff) {
    if (!setSensorPower(channel, true)) {
      if (++state.restoreErrors == 1 || state.restoreErrors % 30 == 0) {
        recoveryLog(channel, "power-on-unconfirmed");
      }
      state.phaseDeadline = millis() + 1000;
      return;
    }
    resetSensorRuntime(channel);
    state.phase = RecoveryPhase::Settling;
    state.phaseDeadline = millis() + kSensorPowerUpMs[channel];
    recoveryLog(channel, "power-on");
  } else {
    state.phase = RecoveryPhase::Running;
    state.freshDeadline = now + kSensorWarmupMs[channel] + noDataWindowMs(channel);
    s_nextSampleAt[channel] = now;
  }
}

static uint32_t reportPeriodMs() {
  switch (s_mode) {
    case CollectorMode::Slow:
      return kModeSlowReportMs;
    case CollectorMode::Fast:
      return kModeFastReportMs;
    case CollectorMode::Normal:
    default:
      return kModeNormalReportMs;
  }
}

void collectorSetup() {
  s_running = false;
  s_mode = CollectorMode::Normal;
  resetSensorRuntime();
  if (!powerAllOn()) {
    Serial.println("[collector] start failed: all-power setup did not verify.");
    return;
  }
  const uint32_t now = millis();
  for (uint8_t channel = 0; channel < 6; ++channel) {
    s_latest[channel] = SensorValues();
    s_hasFresh[channel] = false;
    s_lastFreshAt[channel] = 0;
    s_sampleSeq[channel] = 0;
    s_nextSampleAt[channel] = now + kSensorPowerUpMs[channel] +
                              static_cast<uint32_t>(channel) * 30;
    s_errorStreak[channel] = 0;
    s_recovery[channel] = ChannelRecovery();
    s_recovery[channel].freshDeadline = s_nextSampleAt[channel] +
        kSensorWarmupMs[channel] + noDataWindowMs(channel);
    s_recovery[channel].nextAttemptAt = now;
  }
  s_scanCursor = 0;
  s_reportCycle = 0;
  // SGP41 needs ten 1 Hz conditioning commands; wait until sample 11 before
  // publishing the first coherent six-channel snapshot.
  s_nextReportAt = now + 12000;
  s_running = true;
  Serial.printf(
      "[collector] auto-start mode=%u normal report=10s, independent sampler, "
      "all-power/mux-one, 1 MB bounded log\n",
      static_cast<unsigned>(s_mode));
}

bool collectorRunning() { return s_running; }

void collectorStop() {
  if (s_running) {
    s_running = false;
    ledAllOff();
    powerAllOff();
    resetSensorRuntime();
    Serial.println("[collector] stopped by user.");
  }
}

void collectorSetMode(CollectorMode mode) {
  s_mode = mode;
  s_nextReportAt = millis() + reportPeriodMs();
  Serial.printf("[collector] mode=%u report_period_ms=%lu; sensor cadences unchanged\n",
                static_cast<unsigned>(mode),
                static_cast<unsigned long>(reportPeriodMs()));
}

CollectorMode collectorGetMode() { return s_mode; }

// ---- 1 MB bounded CSV log ----
bool ringLogLine(const String& line) {
  static bool flashReady = false;
  if (!flashReady) {
    flashReady = LittleFS.begin(true);
  }
  if (!flashReady) {
    return false;
  }
  const bool exists = LittleFS.exists(kStreamCsvPath);
  File f = LittleFS.open(kStreamCsvPath, exists ? "a" : "w");
  if (!f) {
    return false;
  }
  if (!exists) {
    f.print(kStreamCsvHeader);
    f.print('\n');
  }
  const size_t lineLen = line.length() + 1;
  if (f.size() + lineLen > kRingMaxBytes) {
    // Variable-length CSV records cannot safely be overwritten in place: a
    // shorter new row leaves bytes from the old row behind. Start a fresh,
    // always-valid bounded log instead.
    f.close();
    f = LittleFS.open(kStreamCsvPath, "w");
    if (!f || f.println(kStreamCsvHeader) == 0) {
      return false;
    }
  }
  const bool ok = f.println(line) > 0;
  f.close();
  return ok;
}

// ---- CSV row emission ----
static void emitCsvRow(uint32_t cycle, uint8_t channel,
                       const SensorValues& v, bool hasFresh,
                       uint32_t sampleAgeMs, uint32_t sampleSeq,
                       uint16_t errorStreak) {
  String line;
  line.reserve(320);
  line += "CSV,";
  line += static_cast<unsigned long>(millis());
  line += ',';
  line += cycle;
  line += ',';
  line += channel;
  line += ',';
  line += kChannels[channel].name;
  line += ',';
  line += v.ok ? "1" : "0";
  line += ',';
  if (v.ok) {
    line += String(v.tempC, 2);
    line += ',';
    line += String(v.rh, 2);
    line += ',';
    line += v.alsRaw;
    line += ',';
    line += v.uvRaw;
    line += ',';
    line += v.spectral[0];
    line += ',';
    line += v.spectral[1];
    line += ',';
    line += v.spectral[2];
    line += ',';
    line += v.spectral[3];
    line += ',';
    line += v.spectral[6];
    line += ',';
    line += v.spectral[7];
    line += ',';
    line += v.spectral[8];
    line += ',';
    line += v.spectral[9];
    line += ',';
    line += v.spectral[10];
    line += ',';
    line += v.spectral[11];
    line += ',';
    line += v.vocIndex;
    line += ',';
    line += v.noxIndex;
    line += ',';
    line += v.srawVoc;
    line += ',';
    line += v.srawNox;
    line += ',';
    line += v.co2Ppm;
    line += ',';
    line += String(v.pressurePa, 2);
    line += ',';
    line += v.as7341Atime;
    line += ',';
    line += v.as7341Astep;
    line += ',';
    line += String(v.as7341Gain, 1);
  } else {
    line +=
        "NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,"
        "NA,NA,NA";
  }
  line += ',';
  if (hasFresh) {
    line += sampleAgeMs;
  } else {
    line += "NA";
  }
  line += ',';
  line += sampleSeq;
  line += ',';
  line += errorStreak;
  line += ',';
  line += kFirmwareVersion;
  line += ',';
  line += static_cast<unsigned>(kSensorSchemaVersion);
  line += ',';
  line += kSchedulerName;
  line += ',';
  line += v.flickerHz;
  line += ',';
  line += wifiRssiDbm();
  line += ",NA,NA,NA,NA";  // schema 5: heart-rate fields belong to logical CH6
  Serial.println(line);
  wifiSendLine(line);
  ringLogLine(line);
  webPanelAddRow(line);
}

static uint32_t staleLimitMs(uint8_t channel) {
  const uint32_t scaled = kSensorSamplePeriodMs[channel] * 4UL;
  return scaled < 15000UL ? 15000UL : scaled;
}

bool collectorLatestSensor(uint8_t channel, SensorValues& out) {
  out = SensorValues();
  if (channel >= 6 || !s_running || !s_hasFresh[channel] ||
      s_recovery[channel].phase != RecoveryPhase::Running ||
      millis() - s_lastFreshAt[channel] > staleLimitMs(channel)) {
    return false;
  }
  out = s_latest[channel];
  out.ok = true;
  return true;
}

bool collectorLatestAls(uint32_t& alsRaw) {
  const uint8_t channel = 3;  // CH3 LTR390
  alsRaw = s_latest[channel].alsRaw;
  if (!s_hasFresh[channel]) {
    return false;
  }
  return millis() - s_lastFreshAt[channel] <= staleLimitMs(channel);
}

static void sampleOne(uint8_t channel, uint32_t now) {
  ledActivityStart(channel);
  SensorValues value;
  const SensorReadStatus status = readSensorStatus(channel, value, true);
  ledActivityFinish(channel);

  if (status == SensorReadStatus::Fresh) {
    s_latest[channel] = value;
    s_hasFresh[channel] = true;
    s_lastFreshAt[channel] = millis();
    ++s_sampleSeq[channel];
    s_errorStreak[channel] = 0;
    s_recovery[channel].freshDeadline = millis() + noDataWindowMs(channel);
    if (s_recovery[channel].awaitingFresh) {
      recoveryLog(channel, "fresh-data-restored");
      s_recovery[channel].awaitingFresh = false;
    }
    if (channel == 0) {
      setSht41Compensation(value.tempC, value.rh);
    }
  } else if (status == SensorReadStatus::Error) {
    if (s_errorStreak[channel] < 0xFFFF) {
      ++s_errorStreak[channel];
    }
    if (s_errorStreak[channel] == 1 || s_errorStreak[channel] % 10 == 0) {
      Serial.printf("[sensor] CH%u error_streak=%u\n", channel,
                    static_cast<unsigned>(s_errorStreak[channel]));
    }
  }

  s_nextSampleAt[channel] = now + kSensorSamplePeriodMs[channel];
  // Pending does not count as an I2C error, but it cannot extend the deadline
  // forever. Both a silent not-ready stall and repeated errors reach recovery.
  const uint32_t finishedAt = millis();
  if (status != SensorReadStatus::Fresh &&
      timeReached(finishedAt, s_recovery[channel].freshDeadline) &&
      !s_recovery[channel].coolingDown) {
    startRecovery(channel, status);
  }
}

static void emitSnapshot(uint32_t now) {
  for (uint8_t channel = 0; channel < 6; ++channel) {
    SensorValues value;
    if (s_hasFresh[channel] &&
        now - s_lastFreshAt[channel] <= staleLimitMs(channel)) {
      value = s_latest[channel];
      value.ok = true;
    } else {
      value.ok = false;
    }
    const bool hasFresh = s_hasFresh[channel];
    const uint32_t age = hasFresh ? now - s_lastFreshAt[channel] : 0;
    emitCsvRow(s_reportCycle, channel, value, hasFresh, age,
               s_sampleSeq[channel], s_errorStreak[channel]);
  }
  ++s_reportCycle;
  s_nextReportAt = millis() + reportPeriodMs();
}

void collectorEmitNow() {
  if (s_running) {
    emitSnapshot(millis());
  }
}

// ---- Independent acquisition / snapshot reporting state machine ----
void collectorTick() {
  if (!s_running) {
    return;
  }
  uint32_t now = millis();
  for (uint8_t channel = 0; channel < 6; ++channel) {
    advanceRecovery(channel, millis());
  }
  now = millis();
  if (ledActivityBusy()) {
    return;
  }
  // One physical transaction per loop keeps Wi-Fi, heartbeat and web serving
  // responsive. Round-robin selection prevents CH4/CH5's 1 Hz jobs from
  // starving the slower channels when several deadlines coincide.
  for (uint8_t checked = 0; checked < 6; ++checked) {
    const uint8_t channel = static_cast<uint8_t>((s_scanCursor + checked) % 6);
    if (s_recovery[channel].phase == RecoveryPhase::Running &&
        timeReached(now, s_nextSampleAt[channel])) {
      s_scanCursor = static_cast<uint8_t>((channel + 1) % 6);
      sampleOne(channel, now);
      return;
    }
  }
  if (timeReached(now, s_nextReportAt)) {
    emitSnapshot(now);
  }
}
