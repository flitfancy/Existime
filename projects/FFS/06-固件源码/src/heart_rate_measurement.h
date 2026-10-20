#pragma once
#include <stddef.h>
#include <stdint.h>

constexpr uint32_t kHeartRateFreshMs = 15000;

// Bluetooth HRS 1.0, Heart Rate Measurement (0x2A37).
struct HeartRateSample {
  uint16_t bpm = 0;
  int8_t contact = -1;  // -1: not supported; 0: poor/no contact; 1: contact
  bool hasSample = false;
  uint32_t sampledAt = 0;
  uint32_t notifiedAt = 0;
  uint32_t sequence = 0;
  uint32_t notifications = 0;
  uint32_t invalidPackets = 0;

  bool ingest(const uint8_t* data, size_t length, uint32_t now) {
    ++notifications;
    notifiedAt = now;
    if (!data || length < 2) { ++invalidPackets; return false; }
    const uint8_t flags = data[0];
    size_t offset = (flags & 1) ? 3 : 2;
    if (length < offset) { ++invalidPackets; return false; }
    const uint16_t value = (flags & 1) ? uint16_t(data[1] | uint16_t(data[2]) << 8) : data[1];
    if (flags & 8) offset += 2;  // Energy Expended
    if (length < offset || ((flags & 16) && (length - offset < 2 || (length - offset) % 2))) {
      ++invalidPackets;
      return false;
    }
    contact = (flags & 4) ? ((flags & 2) ? 1 : 0) : -1;
    if (value == 0 || contact == 0) return false;
    bpm = value;
    sampledAt = now;
    hasSample = true;
    ++sequence;
    return true;
  }

  bool valid(bool connected, uint32_t now) const {
    return connected && hasSample && contact != 0 && uint32_t(now - sampledAt) <= kHeartRateFreshMs;
  }
};
