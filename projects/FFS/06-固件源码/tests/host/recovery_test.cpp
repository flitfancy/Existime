// Compile the production collector and SCD40 read path; fake only hardware/time.
#include "Arduino.h"
#include "sensors.h"
#include "config.h"
#include <cstdlib>
#include <iostream>
#include <vector>
uint32_t testNow = 0;
static uint8_t powerMask = 0x3f;
static unsigned offCount[6] = {}, resetCount[6] = {};
static bool stalled = false, healOnPower = true, failPowerOn = false;
static bool zeroCo2 = false;
static bool readErrors = false;
static uint8_t faultChannel = 5;
static uint32_t changedAt[6] = {}, lastOutput[6] = {}, bootAt = 0;
static uint32_t powerOnAt[6] = {};
static uint32_t powerOffAt[6] = {}, powerOffDuration[6] = {};
static bool checkRecoveryTiming = true;
static std::vector<std::string> events;
static void require(bool ok, const char* message) {
  if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool muxDisconnect() { return true; }
bool i2cReadRegister(uint8_t, uint8_t, uint8_t& value) { value = powerMask; return true; }
bool i2cWriteRegister(uint8_t, uint8_t, uint8_t value) {
  const uint8_t changed = powerMask ^ value;
  require((changed & (changed - 1)) == 0, "power write changed multiple channels");
  if (failPowerOn && (value & ~powerMask)) return false;
  for (uint8_t ch = 0; ch < 6; ++ch) if (changed & (1 << ch)) {
    changedAt[ch] = millis();
    if (!(value & (1 << ch))) { ++offCount[ch]; powerOffAt[ch] = millis(); }
    else {
      powerOnAt[ch] = millis();
      if (offCount[ch] && checkRecoveryTiming) {
        powerOffDuration[ch] = uint32_t(millis() - powerOffAt[ch]);
        const uint32_t requiredMs = ch == 5 ? kScd40RecoveryPowerOffMs : kRecoveryPowerOffMs;
        require(powerOffDuration[ch] >= requiredMs, "recovery restored power before configured off duration");
      }
      if (ch == faultChannel && healOnPower) { stalled = false; zeroCo2 = false; }
    }
  }
  powerMask = value;
  return true;
}
// Generated from the actual function, without editing or duplicating its logic.
#include "power_under_test.inc"
static bool s_scd40Measuring = false;
static uint8_t s_scd40ConsecutiveErrors = 0;
static int Wire = 0;
constexpr int SCD41_I2C_ADDR_62 = 0x62;
struct FakeScd40 {
  uint32_t last = 0;
  void begin(int&, int) {}
  int16_t startPeriodicMeasurement() { last = millis(); return 0; }
  int16_t stopPeriodicMeasurement() { return 0; }
  int16_t getDataReadyStatus(bool& ready) {
    if (stalled && faultChannel == 5 && readErrors) return 1;
    ready = !(stalled && faultChannel == 5) && millis() - last >= 5000; return 0;
  }
  int16_t readMeasurement(uint16_t& co2, float& temp, float& rh) {
    last = millis(); co2 = zeroCo2 ? 0 : 600; temp = 25; rh = 50; return 0;
  }
} s_scd40Sensor;
#include "scd40_under_test.inc"
void resetSensorRuntime(uint8_t ch) {
  ++resetCount[ch];
  if (ch == 5) { s_scd40Measuring = false; s_scd40ConsecutiveErrors = 0; }
}
void resetSensorRuntime() { s_scd40Measuring = false; s_scd40ConsecutiveErrors = 0; }
bool powerAllOn() { powerMask = 0x3f; bootAt = millis(); return true; }
bool powerAllOff() { powerMask = 0; return true; }
void setSht41Compensation(float, float) {}
SensorReadStatus readSensorStatus(uint8_t ch, SensorValues& v, bool) {
  require(powerMask & (1 << ch), "attempted sampling an unpowered channel");
  if (ch == 5) {
    if (offCount[ch]) require(millis() - powerOnAt[ch] >= 1000, "SCD40 power-up wait too short");
    auto result = readScd40(v); v.ok = result == SensorReadStatus::Fresh; return result;
  }
  if (stalled && ch == faultChannel) return readErrors ? SensorReadStatus::Error : SensorReadStatus::Pending;
  // Ordinary startup/alternating phases must not trigger false recovery.
  if (ch == 4 && millis() - bootAt < 11000) return SensorReadStatus::Pending;
  if (ch == 3 && millis() - lastOutput[ch] < 2000) return SensorReadStatus::Pending;
  lastOutput[ch] = millis(); v.ok = true; return SensorReadStatus::Fresh;
}
void ledAllOff() {}
void ledActivityStart(uint8_t) {}
void ledActivityFinish(uint8_t) {}
bool ledActivityBusy() { return false; }
void wifiSendLine(const String& s) { if (s.text.find("[recovery]") == 0) events.push_back(s.text); }
int wifiRssiDbm() { return -40; }
void webPanelAddRow(const String&) {}
#include "collector.cpp"
static void runFor(uint32_t duration) {
  uint32_t start = testNow;
  while (uint32_t(testNow - start) < duration) { collectorTick(); testNow += 2; }
}
int main(int argc, char** argv) {
  const std::string scenario = argc > 1 ? argv[1] : "stuck";
  if (scenario == "display_snapshot") {
    SensorValues value;
    value.ok = true;
    value.tempC = 99;
    require(!collectorLatestSensor(0, value) && !value.ok && value.tempC == 0,
            "display exposed a missing/stopped sample");
    collectorSetup();
    require(!collectorLatestSensor(0, value), "display fabricated a boot sample");
    collectorSetMode(CollectorMode::Slow);
    runFor(15000);
    require(s_reportCycle == 0, "display test unexpectedly required a CSV report");
    s_latest[0].tempC = 25.25f;
    s_latest[0].rh = 47.5f;
    require(collectorLatestSensor(0, value) && value.ok && value.tempC == 25.25f && value.rh == 47.5f,
            "display snapshot did not preserve the latest sensor value");
    require(collectorLatestSensor(5, value) && value.co2Ppm == 600,
            "display did not see fresh CO2 before a report request");
    require(!collectorLatestSensor(6, value) && !value.ok && value.co2Ppm == 0,
            "display accepted a logical heart-rate channel as a physical sensor");
    testNow = s_lastFreshAt[0] + staleLimitMs(0) + 1;
    require(!collectorLatestSensor(0, value) && !value.ok,
            "display exposed an expired temperature");
    s_lastFreshAt[0] = testNow;
    s_recovery[0].phase = RecoveryPhase::PowerOff;
    require(!collectorLatestSensor(0, value), "display exposed a recovering channel");
    s_recovery[0].phase = RecoveryPhase::Running;
    s_lastFreshAt[0] = UINT32_MAX - 100;
    testNow = 100;
    require(collectorLatestSensor(0, value), "display lost a fresh value across uptime wrap");
    collectorStop();
    require(!collectorLatestSensor(0, value) && !value.ok,
            "display retained a valid value after collection stopped");
    std::cout << "PASS display_snapshot\n";
    return 0;
  }
  if (scenario == "power_bits") {
    checkRecoveryTiming = false;  // direct register tests, without scheduler delays.
    for (unsigned mask = 0; mask < 256; ++mask) for (uint8_t ch = 0; ch < 6; ++ch) {
      powerMask = mask;
      require(setSensorPower(ch, false) && powerMask == (mask & ~(1 << ch)), "power-off clobbered another output");
      require(setSensorPower(ch, true) && powerMask == (mask | (1 << ch)), "power-on clobbered another output");
    }
    uint8_t saved = powerMask;
    require(!setSensorPower(6, false) && powerMask == saved, "invalid channel changed outputs");
    std::cout << "PASS: power_bits\n"; return 0;
  }
  if (scenario == "wrap") testNow = UINT32_MAX - 25000;
  collectorSetup();
  runFor(15000);
  require(s_sampleSeq[5] > 0, "healthy startup produced no CO2 sample");
  if (scenario == "healthy") {
    collectorSetMode(CollectorMode::Fast); runFor(60000);
    collectorSetMode(CollectorMode::Slow); runFor(120000);
    for (int ch = 0; ch < 6; ++ch) require(offCount[ch] == 0, "healthy or warm-up channel reset");
  } else if (scenario == "zero_co2") {
    // A successful read can carry CO2=0 with live temperature/humidity, exactly
    // like the captured CH5 records. This is not an I2C error or not-ready flag.
    const unsigned before = s_sampleSeq[5];
    zeroCo2 = true;
    runFor(50000);
    require(offCount[5] == 1, "CO2=0 with live temperature was treated as Fresh; no recovery");
    require(powerOffDuration[5] >= 5000 && powerOffDuration[5] <= 5002, "CH5 did not stay off for five seconds");
    require(!zeroCo2 && s_sampleSeq[5] > before, "valid CO2 did not resume after recovery");
    for (int ch = 0; ch < 5; ++ch) require(offCount[ch] == 0, "healthy sibling was restarted");
  } else if (scenario == "zero_burst") {
    const unsigned before = s_sampleSeq[5];
    const uint32_t lastValid = s_lastFreshAt[5];
    zeroCo2 = true;
    runFor(6000);
    require(s_sampleSeq[5] == before && s_lastFreshAt[5] == lastValid,
            "zero CO2 advanced valid sample metadata");
    require(s_errorStreak[5] > 0, "zero CO2 was not classified as invalid");
    zeroCo2 = false;
    runFor(10000);
    require(offCount[5] == 0 && s_sampleSeq[5] > before && s_latest[5].co2Ppm == 600,
            "short zero burst unnecessarily power-cycled sensor or failed to clear");
  } else if (scenario == "off_sampling" || scenario == "stop_during_off") {
    stalled = true;
    const uint32_t start = millis();
    while (!offCount[5] && uint32_t(millis() - start) < 30000) runFor(2);
    require(offCount[5] == 1 && !(powerMask & (1 << 5)), "CH5 never entered power-off phase");
    if (scenario == "stop_during_off") {
      collectorStop(); runFor(120000);
      require(powerMask == 0 && resetCount[5] == 0, "manual stop was undone during power-off");
    } else {
      const unsigned gasBefore = s_sampleSeq[4];
      runFor(4000);
      require(!(powerMask & (1 << 5)), "CH5 restored power before five seconds");
      require(s_sampleSeq[4] >= gasBefore + 3, "other sampling blocked during CH5 power-off");
      for (int ch = 0; ch < 5; ++ch) require(offCount[ch] == 0, "healthy sibling powered off");
      runFor(15000);
      require(powerOffDuration[5] >= 5000 && s_latest[5].co2Ppm == 600 && s_hasFresh[5],
              "CH5 did not resume after five-second power-off");
    }
  } else if (scenario == "stop") {
    collectorStop(); stalled = true; runFor(120000);
    require(powerMask == 0, "manual stop was undone by recovery");
  } else {
    if (scenario.size() == 3 && scenario.substr(0, 2) == "ch") faultChannel = scenario[2] - '0';
    if (scenario == "long_uptime") {
      // Model 26 days of healthy acquisition with recent sampling deadlines,
      // while the initial (unused) retry deadline remains in the distant past.
      uint32_t elapsed = uint32_t(26UL * 24 * 60 * 60 * 1000);
      testNow += elapsed; bootAt += elapsed; s_scd40Sensor.last += elapsed;
      s_nextReportAt += elapsed;
      for (uint8_t ch = 0; ch < 6; ++ch) {
        s_nextSampleAt[ch] += elapsed; s_lastFreshAt[ch] += elapsed;
        lastOutput[ch] += elapsed; s_recovery[ch].freshDeadline += elapsed;
      }
    }
    unsigned before = s_sampleSeq[faultChannel];
    stalled = true;
    if (scenario == "read_errors") readErrors = true;
    if (scenario == "persistent") healOnPower = false;
    if (scenario == "restore_failure") failPowerOn = true;
    runFor(faultChannel == 1 ? 140000 : 50000);
    require(offCount[faultChannel] == 1, "stuck not-ready channel was not power-cycled once");
    for (int ch = 0; ch < 6; ++ch) if (ch != faultChannel) {
      require(offCount[ch] == 0 && resetCount[ch] == 0, "healthy channel was restarted");
      require(s_sampleSeq[ch] >= 2, "other channels stopped collecting");
    }
    if (scenario == "persistent") {
      require(!s_hasFresh[5], "stale pre-reset cache treated as valid");
      runFor(40000); require(offCount[5] == 2, "retry cooldown not respected");
    } else {
      if (scenario == "restore_failure") { failPowerOn = false; runFor(12000); }
      require(s_sampleSeq[faultChannel] > before, "channel did not recover after branch power cycle");
      require(powerMask == 0x3f, "branch power not restored");
      require(!events.empty(), "no recovery diagnostics emitted");
    }
  }
  std::cout << "PASS: " << scenario << '\n';
}
