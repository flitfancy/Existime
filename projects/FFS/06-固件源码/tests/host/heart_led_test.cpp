#include "Arduino.h"
#include "Wire.h"
#include "leds.h"
#include "heart_rate_measurement.h"
#include <cstdlib>
#include <iostream>

uint32_t testNow = 0;
FakeWire Wire;
static void require(bool ok, const char* why) {
  if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
bool i2cReadRegister(uint8_t address, uint8_t reg, uint8_t& value) {
  require(address == 0x21 && reg == 1, "indicator queried an unrelated I2C register");
  value = Wire.output;
  return true;
}
static bool d8() { return (Wire.output & 0x40) == 0; }
static void update(bool valid, uint16_t bpm) {
#if HAS_HEART_LED
  ledUpdateHeartRate(valid, bpm);
#else
  // Original main-loop call in its default reporting mode.
  (void)valid; (void)bpm;
  ledUpdateMode(CollectorMode::Normal);
#endif
}
static void reset(uint32_t now = 1000) {
  Wire.failWrite = false;
  testNow = now;
  ledSetDarkMode(false);
  ledAllOff();
  require(ledInit() && Wire.config == 0, "LED initialization failed");
}
int main() {
  reset();
  update(true, 72);
  require(d8() && Wire.output == 0xBF, "D8 does not indicate fresh heart rate in normal mode");
  testNow = 2249; update(true, 72); require(d8(), "bright half ended before 1250 ms");
  testNow = 2250; update(true, 72); require(!d8(), "bright half stayed on after 1250 ms");
  for (testNow = 2300; testNow < 3500; testNow += 100) {
    const auto before = testNow;
    update(true, 72);
    require(!d8() && testNow == before, "updates reset pulse phase or block the sampling loop");
  }
  testNow = 3500; update(true, 72); require(d8(), "72 bpm did not produce a 2500 ms period");
  ledSetChannel(0, true);
  require(d8() && (Wire.output & 0x80) == 0, "channel activity overwrote the heart indicator");
  ledSetChannel(0, false); require(d8(), "channel clear overwrote D8");
  testNow = 4750; update(true, 72); require(!d8(), "second bright half did not end");

  reset(); update(true, 72);
  testNow = 1400; update(true, 90); require(d8(), "BPM update ended the bright half early");
  testNow = 2000; update(true, 90); require(!d8(), "BPM update reset the phase instead of balancing the two halves");
  testNow = 2999; update(true, 90); require(!d8(), "90 bpm pulse arrived early");
  testNow = 3000; update(true, 90); require(d8(), "90 bpm did not update the period to 2000 ms");
  update(false, 90); require(!d8(), "disconnect left D8 lit");
  update(true, 0); require(!d8(), "zero BPM was accepted");
  update(true, 72); require(d8(), "reconnect did not start a new pulse");
  ledSetDarkMode(true); require(!d8(), "night mode did not extinguish D8 immediately");
  testNow += 5000; update(true, 72); require(!d8(), "night mode leaked a heart pulse");
  ledSetDarkMode(false); update(true, 72); require(d8(), "day mode did not restore the indicator");
  ledAllOff(); require(!d8(), "all-off left D8 on");

  reset();
  HeartRateSample sample;
  const uint8_t packet[] = {0, 72};
  sample.ingest(packet, sizeof(packet), testNow);
  update(sample.valid(true, testNow), sample.bpm); require(d8(), "valid measurement did not light D8");
  testNow += kHeartRateFreshMs + 1;
  update(sample.valid(true, testNow), sample.bpm); require(!d8(), "stale measurement kept D8 active");
  const uint8_t noContact[] = {4, 72};
  sample.ingest(noContact, sizeof(noContact), testNow);
  update(sample.valid(true, testNow), sample.bpm); require(!d8(), "poor contact kept D8 active");

  reset(UINT32_MAX - 1000);
  const auto start = testNow;
  update(true, 72); require(d8(), "first pulse missing before rollover");
  testNow = start + 1249; update(true, 72); require(d8(), "bright half was shortened near rollover");
  testNow = start + 1250; update(true, 72); require(!d8(), "bright half did not end near rollover");
  testNow = start + 2499; update(true, 72); require(!d8(), "rollover caused an early pulse");
  testNow = start + 2500; update(true, 72); require(d8(), "rollover prevented the next pulse");

  reset(); Wire.failWrite = true; update(true, 72); require(!d8(), "failed write changed fake hardware");
  Wire.failWrite = false; testNow += 10; update(true, 72); require(d8(), "failed pulse write was not retried");
  std::cout << "PASS: actual D8 output, balanced 1/3 BPM cycle, freshness, night mode, channel isolation, I2C retry and timer rollover\n";
}
