#include "Arduino.h"
#include "config.h"
#include <cstdlib>
#include <iostream>

uint32_t testNow = 0;
static uint8_t outputs = 0, config = 0xFF;
static bool failWrite = false, wrongReadback = false;
static void require(bool ok, const char* why) {
  if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
bool muxWrite(uint8_t value) { require(value == 0, "power function connected an I2C branch"); return true; }
bool muxDisconnect() { return muxWrite(0); }
bool i2cWriteRegister(uint8_t address, uint8_t reg, uint8_t value) {
  require(address == kPowerExpanderAddress, "unexpected device");
  if (failWrite) return false;
  if (reg == kTca9534OutputRegister) outputs = value;
  if (reg == kTca9534ConfigRegister) config = value;
  return true;
}
bool i2cReadRegister(uint8_t address, uint8_t reg, uint8_t& value) {
  require(address == kPowerExpanderAddress && reg == kTca9534OutputRegister, "unexpected register");
  value = wrongReadback ? uint8_t(outputs ^ 0x40) : outputs;
  return true;
}
#include "power_rails_under_test.inc"

int main() {
  const uint8_t accessory = kAccessoryPowerMask;
  require(powerAllOn(), "all-power setup failed");
  require(outputs == uint8_t(0x3F | accessory), "all-power setup leaves X3 off");
  require(config == 0x80, "P6 was not configured as output");
  require(setSensorPower(5, false), "CH5 off failed");
  require(outputs == uint8_t(0x1F | accessory), "CH5 recovery cut accessory or another sensor");
  require(setSensorPower(5, true) && outputs == uint8_t(0x3F | accessory), "CH5 restoration clobbered X3");
  require(powerAllOff() && outputs == accessory, "collector stop cut accessory power");
  for (uint8_t channel = 0; channel < 6; ++channel) {
    require(powerBranchOn(channel) && outputs == uint8_t((1 << channel) | accessory), "single-branch sweep cut accessory");
    require(closeBranch(false) && outputs == accessory, "closing a sweep branch cut accessory");
  }
  require(!setSensorPower(6, true) && outputs == accessory, "logical CH6 became a physical sensor");
#if HAS_ACCESSORY_SETUP
  outputs = 0xFF; config = 0xFF;
  require(powerAccessorySetup() && outputs == accessory && config == 0x80, "accessory boot setup failed");
  wrongReadback = true;
  require(!powerAccessorySetup(), "accessory setup accepted wrong output readback");
  require(!powerAllOn(), "collector setup accepted wrong output readback");
  wrongReadback = false;
#endif
  failWrite = true;
  require(!powerAllOn(), "all-power setup accepted failed write");
  std::cout << "PASS: accessory=" << unsigned(accessory)
            << ", startup, stop, CH5 recovery and six-channel sweep preserve intended rails\n";
}
