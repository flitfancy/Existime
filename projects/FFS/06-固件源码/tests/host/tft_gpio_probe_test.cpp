#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
#include "tft_gpio_probe.h"
#include "tft_probe_tables.inc"

struct Byte { uint8_t value; bool data; };
struct FakeIO {
  bool clock = false, data = false, dc = true, cs = true;
  uint64_t us = 0, lastFall = 0, lastRise = 0, lastData = 0;
  unsigned bits = 0, rows = 0;
  uint8_t value = 0;
  std::vector<std::vector<Byte>> transactions;
  std::vector<Byte> current;
  std::vector<uint32_t> pauses;
  void write(TftProbeLine line, bool high) {
    if (line == TftProbeLine::Data) {
      assert(!clock);
      data = high;
      lastData = us;
    } else if (line == TftProbeLine::Clock) {
      if (high) {
        assert(!clock && !cs && us - lastFall >= 5 && us - lastData >= 5);
        lastRise = us;
        value = uint8_t((value << 1) | data);
        if (++bits == 8) { current.push_back({value, dc}); bits = 0; value = 0; }
      } else if (clock) {
        assert(us - lastRise >= 5);
        lastFall = us;
      }
      clock = high;
    } else if (line == TftProbeLine::Dc) {
      assert(bits == 0 && !clock);
      dc = high;
    } else if (line == TftProbeLine::Cs) {
      assert(bits == 0 && !clock);
      if (high && !cs) { transactions.push_back(current); current.clear(); }
      cs = high;
    }
  }
  void delayUs(uint32_t n) { us += n; }
  void delayMs(uint32_t n) { pauses.push_back(n); us += n * 1000ULL; }
  void serviceRow() { ++rows; }
};

uint16_t pixel(const std::vector<Byte>& row, unsigned x) {
  assert(row[2*x].data && row[2*x+1].data);
  return uint16_t(row[2*x].value << 8) | row[2*x+1].value;
}

int main() {
  FakeIO io;
  TftGpioProbe<FakeIO> probe(io);
  probe.reset();
  const uint8_t sample[] = {0xA5, 0x3C};
  probe.command(0x2A, sample, 2);
  const auto& sent = io.transactions.back();
  assert(sent.size() == 3 && sent[0].value == 0x2A && !sent[0].data);
  assert(sent[1].value == 0xA5 && sent[1].data && sent[2].value == 0x3C && sent[2].data);
  for (const auto* table : {Rcmd1, Rcmd2green, Rcmd3}) probe.replay(table);
  bool sleepOut = false, rgb565 = false, displayOn = false;
  for (const auto& t : io.transactions) {
    if (t[0].value == 0x11 && !t[0].data) sleepOut = true;
    if (t[0].value == 0x3A && !t[0].data && t.size() == 2 && t[1].value == 5) rgb565 = true;
    if (t[0].value == 0x29 && !t[0].data) displayOn = true;
  }
  assert(sleepOut && rgb565 && displayOn);
  bool waited500 = false;
  for (auto pause : io.pauses) waited500 |= pause == 500;
  assert(waited500);
  const auto start = io.transactions.size();
  probe.pattern();
  assert(io.rows == 128 && io.transactions.size() == start + 2 + 128*4);
  assert(io.transactions[start][0].value == 0x36 && io.transactions[start][1].value == 0x68);
  assert(io.transactions[start+1][0].value == 0x21);
  for (unsigned y = 0; y < 128; ++y) {
    const auto base = start + 2 + y*4;
    const auto& columns = io.transactions[base];
    const auto& rows = io.transactions[base+1];
    assert(columns[0].value == 0x2A && columns[2].value == 1 && columns[4].value == 160);
    assert(rows[0].value == 0x2B && rows[2].value == y+2 && rows[4].value == y+2);
    assert(io.transactions[base+2][0].value == 0x2C);
    const auto& pixels = io.transactions[base+3];
    assert(pixels.size() == 320 && pixel(pixels,0) == 0xFFFF && pixel(pixels,159) == 0xFFFF);
    if (y == 0 || y == 127) assert(pixel(pixels,80) == 0xFFFF);
    else if (y < 16) assert(pixel(pixels,80) == 0);
    else assert(pixel(pixels,20) == 0xF800 && pixel(pixels,80) == 0x07E0 && pixel(pixels,130) == 0x001F);
  }
  assert(io.cs && !io.clock && io.bits == 0);
  std::cout << "PASS: MSB-first bytes, CS/DC framing, >=5us half cycles, real GREEN tables, RGB565 windows and 160x128 pattern\n";
}
