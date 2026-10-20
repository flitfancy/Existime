#pragma once
#include <Arduino.h>
void heartRateSetup();
void heartRateTick();
String heartRateJson();

struct HeartRateDisplayValues {
  bool connected = false;
  bool valid = false;
  bool hasSample = false;
  uint16_t bpm = 0;
  int8_t contact = -1;
  uint32_t ageMs = 0;
};
// Read the existing protected cache; never starts a scan or GATT operation.
HeartRateDisplayValues heartRateDisplayValues();
