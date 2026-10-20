#pragma once

#include <cstdint>

// Independent write path; uses the existing GREEN init tables.
enum class TftProbeLine { Clock, Data, Reset, Dc, Cs };

template <typename IO>
class TftGpioProbe {
 public:
  explicit TftGpioProbe(IO& io) : io_(io) {}

  void reset() {
    io_.write(TftProbeLine::Cs, true);
    io_.write(TftProbeLine::Dc, true);
    io_.write(TftProbeLine::Clock, false);
    io_.write(TftProbeLine::Data, false);
    io_.write(TftProbeLine::Reset, true);
    io_.delayMs(100);
    io_.write(TftProbeLine::Reset, false);
    io_.delayMs(100);
    io_.write(TftProbeLine::Reset, true);
    io_.delayMs(100);
  }

  void writeByte(uint8_t value) {
    for (uint8_t bit = 0; bit < 8; ++bit) {
      io_.write(TftProbeLine::Data, (value & 0x80) != 0);
      io_.delayUs(5);  // MOSI setup + SCK low time.
      io_.write(TftProbeLine::Clock, true);
      io_.delayUs(5);  // SCK high time; interrupts may only slow it down.
      io_.write(TftProbeLine::Clock, false);
      value <<= 1;
    }
  }

  void command(uint8_t value, const uint8_t* data = nullptr, uint8_t count = 0) {
    io_.write(TftProbeLine::Cs, false);
    io_.write(TftProbeLine::Dc, false);
    io_.delayUs(5);
    writeByte(value);
    io_.write(TftProbeLine::Dc, true);
    io_.delayUs(5);
    for (uint8_t i = 0; i < count; ++i) writeByte(data[i]);
    io_.write(TftProbeLine::Cs, true);
    io_.delayUs(5);
  }

  void replay(const uint8_t* table) {
    uint8_t commands = *table++;
    while (commands--) {
      const uint8_t value = *table++;
      const uint8_t flags = *table++;
      const uint8_t count = flags & 0x7F;
      command(value, table, count);
      table += count;
      if (flags & 0x80) {
        const uint16_t pause = *table++;
        io_.delayMs(pause == 255 ? 500 : pause);
      }
    }
  }

  void pattern() {
    // GREEN rotation3: MADCTL MX|MV|BGR, xstart=1, ystart=2.
    const uint8_t rotation = 0x68;
    command(0x36, &rotation, 1);
    command(0x21);  // Same inversion as the old working example.
    const uint8_t x[4] = {0, 1, 0, 160};
    for (uint16_t row = 0; row < 128; ++row) {
      const uint8_t y[4] = {0, uint8_t(row + 2), 0, uint8_t(row + 2)};
      command(0x2A, x, 4);
      command(0x2B, y, 4);
      command(0x2C);
      io_.write(TftProbeLine::Cs, false);
      io_.write(TftProbeLine::Dc, true);
      io_.delayUs(5);
      for (uint16_t col = 0; col < 160; ++col) {
        const bool border = row == 0 || row == 127 || col == 0 || col == 159;
        const uint16_t color = border ? 0xFFFF :
            row < 16 ? 0x0000 : col < 54 ? 0xF800 : col < 107 ? 0x07E0 : 0x001F;
        writeByte(uint8_t(color >> 8));
        writeByte(uint8_t(color));
      }
      io_.write(TftProbeLine::Cs, true);
      io_.serviceRow();
    }
  }

 private:
  IO& io_;
};

void tftRunGpioProbe();
