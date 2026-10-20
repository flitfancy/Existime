#include "tft_gpio_probe.h"

#include <Adafruit_ST7735.h>
#include <Arduino.h>
#include "config.h"

namespace {
struct BoardIO {
  void write(TftProbeLine line, bool high) {
    int pin = kTftCsPin;
    switch (line) {
      case TftProbeLine::Clock: pin = kTftSckPin; break;
      case TftProbeLine::Data: pin = kTftMosiPin; break;
      case TftProbeLine::Reset: pin = kTftResetPin; break;
      case TftProbeLine::Dc: pin = kTftDcPin; break;
      case TftProbeLine::Cs: break;
    }
    digitalWrite(pin, high ? HIGH : LOW);
  }
  void delayUs(uint32_t us) { delayMicroseconds(us); }
  void delayMs(uint32_t ms) { delay(ms); }
  void serviceRow() { delay(1); }
};
}  // namespace

void tftRunGpioProbe() {
  for (const int pin : {kTftCsPin, kTftDcPin, kTftSckPin, kTftMosiPin, kTftResetPin}) {
    pinMode(pin, OUTPUT);
  }
  BoardIO io;
  TftGpioProbe<BoardIO> probe(io);
  probe.reset();
  for (uint8_t part = 0; part < 3; ++part) {
    probe.replay(Adafruit_ST7735::diagnosticGreenTable(part));
  }
  probe.pattern();
}
