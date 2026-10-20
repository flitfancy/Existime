#pragma once

#include <cstdint>

enum class TftPage : uint8_t { Environment, Pressure, Light, Gas, Spectrum, Heart };
constexpr uint32_t kTftPageDurationMs = 8000;

class TftCarousel {
 public:
  TftPage page() const { return page_; }
  bool automatic() const { return automatic_; }
  void restart(uint32_t now) { deadline_ = now + kTftPageDurationMs; }
  void setAutomatic(bool enabled, uint32_t now) { automatic_ = enabled; restart(now); }
  bool select(TftPage page, uint32_t now) {
    if (uint8_t(page) > uint8_t(TftPage::Heart)) return false;
    page_ = page; automatic_ = false; restart(now); return true;
  }
  bool tick(uint32_t now, bool watchConnected) {
    if (!automatic_) return false;
    if (page_ == TftPage::Heart && !watchConnected) {
      page_ = TftPage::Environment; restart(now); return true;
    }
    if (int32_t(now - deadline_) < 0) return false;
    const uint8_t count = watchConnected ? 6 : 5;
    page_ = TftPage((uint8_t(page_) + 1) % count);
    restart(now);
    return true;
  }
 private:
  TftPage page_ = TftPage::Environment;
  bool automatic_ = true;
  uint32_t deadline_ = kTftPageDurationMs;
};

struct TftMetric {
  const char* label = "";
  const char* unit = "";
  char text[16] = "--";
  bool valid = false;
  bool textValue = false;
};

struct TftPageValues {
  TftPage page = TftPage::Environment;
  TftMetric metrics[3];
  uint16_t spectrum[8] = {};
  uint16_t spectrumPeak = 0;
  bool spectrumValid = false;
  bool healthy = false;
  bool watchConnected = false;
  const char* footerNote = nullptr;
};

const char* tftPageTitle(TftPage page);
const char* tftPageKey(TftPage page);
TftPageValues tftPageValues(TftPage page);
