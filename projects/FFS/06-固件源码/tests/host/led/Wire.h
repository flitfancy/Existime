#pragma once
#include <cstdint>
#include <vector>
struct FakeWire {
  uint8_t address = 0, output = 0xFF, config = 0xFF;
  bool failWrite = false;
  std::vector<uint8_t> bytes;
  void beginTransmission(uint8_t target) { address = target; bytes.clear(); }
  void write(uint8_t value) { bytes.push_back(value); }
  uint8_t endTransmission() {
    if (failWrite || address != 0x21 || bytes.size() != 2) return 4;
    if (bytes[0] == 1) output = bytes[1];
    if (bytes[0] == 3) config = bytes[1];
    return 0;
  }
};
extern FakeWire Wire;
