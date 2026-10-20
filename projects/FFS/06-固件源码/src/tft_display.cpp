#include "tft_display.h"

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFi.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <soc/gpio_struct.h>

#include "collector.h"
#include "heart_rate.h"
#include "config.h"
#include "sensors.h"
#include "tft_gpio_probe.h"
#include "tft_ui.h"
#include "wifi_link.h"

namespace {
constexpr int kWidth = 160;
constexpr int kHeight = 128;

SPIClass s_spi(FSPI);
Adafruit_ST7735 s_hardwareTft(&s_spi, kTftCsPin, kTftDcPin, kTftResetPin);
Adafruit_ST7735 s_softwareTft(kTftCsPin, kTftDcPin, kTftMosiPin, kTftSckPin, kTftResetPin);
Adafruit_ST7735* s_tft = &s_hardwareTft;
bool s_softwareSpi = false;
bool s_gpioProbe = false;
uint32_t s_probeElapsedMs = 0;
// One small reusable strip avoids a full-frame allocation and visible clearing.
GFXcanvas16 s_strip(kWidth, kTftUiTemperatureHeight);
bool s_initialized = false;
bool s_backlight = true;
bool s_inverted = false;
uint8_t s_profile = INITR_BLACKTAB;
bool s_profileB = false;
uint8_t s_rotation = kTftRotation;
uint32_t s_nextRefresh = 0;
bool s_testActive = false;
bool s_testHeld = false;
uint32_t s_testUntil = 0;
uint32_t s_pixelFrequencyHz = kTftSpiFrequencyHz;
TftCarousel s_carousel;
TftPageValues s_previousValues;
bool s_primaryKnown = false, s_secondaryKnown = false;
bool s_watchConnected = false;
char s_previousFooter[80] = {};

bool reached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

const char* profileName() {
  if (s_profileB) return "B";
  if (s_profile == INITR_REDTAB) return "RED";
  if (s_profile == INITR_BLACKTAB) return "BLACK";
  return "GREEN";
}

void invalidate() {
  s_primaryKnown = s_secondaryKnown = false;
  s_previousFooter[0] = '\0';
  s_nextRefresh = millis();
}

void drawValues(const TftPageValues& values);
void drawFooter(const TftPageValues& values);

void drawFrame() {
  invalidate();
  s_carousel.restart(millis());
  s_watchConnected = heartRateDisplayValues().connected;
  const auto values = tftPageValues(s_carousel.page());
  s_tft->fillScreen(kTftUiBackground);
  drawValues(values);
  drawFooter(values);
}

void initializePanel() {
  s_gpioProbe = false;
  // Only run at startup or after an explicit TFTTYPE diagnostic command.
  // ST7735 init includes its required reset/sleep-out delays.
  s_tft->setInitSPISpeed(kTftInitFrequencyHz);
  if (s_profileB) s_tft->initB();
  else s_tft->initR(s_profile);
  s_tft->setSPISpeed(s_pixelFrequencyHz);
  s_tft->setRotation(s_rotation);
  s_tft->invertDisplay(s_inverted);
  s_testActive = false;
  s_testHeld = false;
  drawFrame();
  if (kTftBacklightPin >= 0) digitalWrite(kTftBacklightPin, s_backlight ? HIGH : LOW);
}

bool sameMetric(const TftMetric& a, const TftMetric& b) {
  return a.valid == b.valid && a.textValue == b.textValue &&
         std::strcmp(a.text, b.text) == 0 && std::strcmp(a.unit, b.unit) == 0;
}

void drawValues(const TftPageValues& values) {
  const bool spectrumChanged = values.page == TftPage::Spectrum &&
      (values.spectrumValid != s_previousValues.spectrumValid ||
       std::memcmp(values.spectrum, s_previousValues.spectrum, sizeof(values.spectrum)) != 0);
  if (!s_primaryKnown || !sameMetric(values.metrics[0], s_previousValues.metrics[0]) || spectrumChanged) {
    tftUiPagePrimary(s_strip, values);
    s_tft->drawRGBBitmap(0, kTftUiTemperatureY, s_strip.getBuffer(), kWidth,
                         kTftUiTemperatureHeight);
    s_primaryKnown = true;
  }
  if (!s_secondaryKnown || !sameMetric(values.metrics[1], s_previousValues.metrics[1]) ||
      !sameMetric(values.metrics[2], s_previousValues.metrics[2])) {
    tftUiPageSecondary(s_strip, values);
    s_tft->drawRGBBitmap(0, kTftUiSecondaryY, s_strip.getBuffer(), kWidth,
                         kTftUiSecondaryHeight);
    s_secondaryKnown = true;
  }
  s_previousValues = values;
}

void drawFooter(const TftPageValues& values) {
  const bool wifi = WiFi.status() == WL_CONNECTED;
  const bool pc = wifiLinkConfirmed();
  const bool running = collectorRunning();
  const uint8_t count = s_watchConnected || s_carousel.page() == TftPage::Heart ? 6 : 5;
  char footer[80];
  std::snprintf(footer, sizeof(footer), "%u%u%u%u%u:%u:%u:%s", wifi, pc, running,
                values.healthy, values.watchConnected, unsigned(values.page), count,
                values.footerNote ? values.footerNote : "");
  if (std::strcmp(footer, s_previousFooter) == 0) return;
  tftUiHeader(*s_tft, wifi, tftPageTitle(values.page));
  tftUiFooter(*s_tft, running, values.healthy, pc, values.footerNote,
              uint8_t(values.page) + 1, count, values.page == TftPage::Heart, values.watchConnected);
  std::strcpy(s_previousFooter, footer);
}

void printStatus() {
  // No MISO/readback wire: this reports configuration, not a detected panel.
  Serial.printf("[tft] init_sent=%u driver=ST7735 profile=%s bus=%s size=%dx%d "
                "rotation=%u invert=%u backlight=%u spi_hz=%lu init_spi_hz=%lu "
                "SCK=%d MOSI=%d RES=%d DC=%d CS=%d BL=%d hardware_check=PENDING\n",
                s_initialized, profileName(), s_gpioProbe ? "GPIO-SLOW" : s_softwareSpi ? "SOFT" : "HARD",
                s_gpioProbe ? kWidth : s_tft->width(), s_gpioProbe ? kHeight : s_tft->height(),
                s_rotation, s_inverted, s_backlight,
                static_cast<unsigned long>(s_softwareSpi ? 0 : s_pixelFrequencyHz),
                static_cast<unsigned long>(s_softwareSpi ? 0 : kTftInitFrequencyHz), kTftSckPin,
                kTftMosiPin, kTftResetPin, kTftDcPin, kTftCsPin,
                kTftBacklightPin);
}

void showTest(bool held = false) {
  const uint16_t colors[3] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE};
  const char* names[3] = {"RED", "GREEN", "BLUE"};
  s_tft->fillScreen(ST77XX_BLACK);
  s_tft->setTextWrap(false);
  s_tft->setTextSize(1);
  for (uint8_t i = 0; i < 3; ++i) {
    const int x = 2 + i * 52;
    s_tft->fillRect(x, 18, 50, 80, colors[i]);
    s_tft->setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    s_tft->setCursor(x + 5, 103);
    s_tft->print(names[i]);
  }
  s_tft->drawRect(0, 0, kWidth, kHeight, ST77XX_WHITE);
  s_tft->setCursor(7, 5);
  s_tft->print("160x128 COLOR TEST");
  s_tft->setCursor(7, 117);
  s_tft->print(held ? "TFTDATA to return" : "Returns in 5 seconds");
  s_testActive = true;
  s_testHeld = held;
  s_testUntil = millis() + 5000;
}
}  // namespace

void tftDisplaySetup() {
  if (!kTftEnabled) return;
  if (kTftBacklightPin >= 0) {
    pinMode(kTftBacklightPin, OUTPUT);
    digitalWrite(kTftBacklightPin, LOW);
  }
  if (!s_strip.getBuffer()) {
    Serial.println("[tft] strip allocation failed; display disabled.");
    return;
  }
  // Explicit pins keep native USB GPIO19/20 and octal PSRAM GPIO35..37 free.
  s_spi.begin(kTftSckPin, -1, kTftMosiPin, kTftCsPin);
  initializePanel();
  s_initialized = true;
  printStatus();
}

void tftDisplayTick() {
  if (!s_initialized) return;
  const uint32_t now = millis();
  if (s_testActive) {
    if (s_testHeld) return;
    if (!reached(now, s_testUntil)) return;
    s_testActive = false;
    drawFrame();
  }
  if (!reached(now, s_nextRefresh)) return;
  s_nextRefresh = now + kTftRefreshMs;

  s_watchConnected = heartRateDisplayValues().connected;
  if (s_carousel.tick(now, s_watchConnected)) {
    drawFrame();
    return;
  }
  const auto values = tftPageValues(s_carousel.page());
  drawValues(values);
  drawFooter(values);
}

bool tftDisplayCommand(const String& command) {
  if (!command.startsWith("TFT")) return false;
  if (!s_initialized) {
    Serial.println("[tft] unavailable on this build or allocation failed.");
    return true;
  }
  // Returning to the library write path requires restoring its panel state.
  if (s_gpioProbe && command != "TFTRAW" && command != "TFTSTATUS") initializePanel();
  if (command == "TFTSTATUS") printStatus();
  else if (command == "TFTAUTO 0" || command == "TFTAUTO 1") {
    s_carousel.setAutomatic(command == "TFTAUTO 1", millis());
    drawFrame();
  }
  else if (command.startsWith("TFTPAGE ")) {
    const char* names[] = {"ENV", "PRESSURE", "LIGHT", "GAS", "SPECTRUM", "HEART"};
    const String selected = command.substring(8);
    for (uint8_t i = 0; i < 6; ++i) {
      if (selected == names[i]) {
        s_carousel.select(TftPage(i), millis());
        drawFrame();
        break;
      }
    }
  }
  else if (command == "TFTRAW") {
    s_spi.end();
    s_softwareSpi = true;
    s_tft = &s_softwareTft;
    s_profile = INITR_GREENTAB;
    s_profileB = false;
    s_rotation = 3;
    s_inverted = true;
    const uint32_t started = millis();
    tftRunGpioProbe();
    s_probeElapsedMs = millis() - started;
    s_gpioProbe = true;
    s_testActive = true;
    s_testHeld = true;
    printStatus();
  }
  else if (command == "TFTBUS SOFT" || command == "TFTBUS HARD") {
    s_softwareSpi = command == "TFTBUS SOFT";
    // One owner per SPI host/pin pair. Detach the peripheral before bit-banging.
    s_spi.end();
    if (s_softwareSpi) s_tft = &s_softwareTft;
    else {
      s_spi.begin(kTftSckPin, -1, kTftMosiPin, kTftCsPin);
      s_tft = &s_hardwareTft;
    }
    initializePanel();
    printStatus();
  }
  else if (command == "TFTHZ 1000000" || command == "TFTHZ 8000000") {
    s_pixelFrequencyHz = command == "TFTHZ 1000000" ? 1000000 : 8000000;
    s_tft->setSPISpeed(s_pixelFrequencyHz);
    drawFrame();
    printStatus();
  }
  else if (command == "TFTTEST" || command == "TFTTEST HOLD") {
    showTest(command == "TFTTEST HOLD");
  }
  else if (command == "TFTDATA") {
    s_testActive = false;
    s_testHeld = false;
    drawFrame();
  }
  else if (command == "TFTBL 0" || command == "TFTBL 1") {
    if (kTftBacklightPin < 0) {
      Serial.println("[tft] LEDA has fixed 3V3 supply; GPIO backlight control unavailable.");
      return true;
    }
    s_backlight = command == "TFTBL 1";
    digitalWrite(kTftBacklightPin, s_backlight ? HIGH : LOW);
    printStatus();
  } else if (command == "TFTROT 1" || command == "TFTROT 3") {
    s_rotation = command == "TFTROT 1" ? 1 : 3;
    s_tft->setRotation(s_rotation);
    s_testActive = false;
    drawFrame();
    printStatus();
  } else if (command == "TFTINVERT 0" || command == "TFTINVERT 1") {
    s_inverted = command == "TFTINVERT 1";
    s_tft->invertDisplay(s_inverted);
    printStatus();
  } else if (command == "TFTTYPE GREEN" || command == "TFTTYPE RED" ||
             command == "TFTTYPE BLACK" || command == "TFTTYPE B") {
    s_profileB = command == "TFTTYPE B";
    s_profile = command == "TFTTYPE RED" ? INITR_REDTAB :
                command == "TFTTYPE BLACK" ? INITR_BLACKTAB : INITR_GREENTAB;
    initializePanel();
    printStatus();
  } else {
    Serial.println("[tft] use TFTPAGE ENV/PRESSURE/LIGHT/GAS/SPECTRUM/HEART, TFTAUTO 0/1, TFTSTATUS, TFTRAW, TFTTEST [HOLD], TFTDATA, TFTBUS SOFT/HARD, TFTHZ 1000000/8000000, TFTBL 0/1, TFTROT 1/3, "
                   "TFTINVERT 0/1, TFTTYPE GREEN/RED/BLACK/B");
  }
  return true;
}

String tftDisplayJson() {
  uint8_t powerOutputs = 0;
  const bool powerReadback = i2cReadRegister(kPowerExpanderAddress,
                                            kTca9534OutputRegister, powerOutputs);
  const auto values = tftPageValues(s_carousel.page());
  const bool watch = heartRateDisplayValues().connected;
  char body[1600];
  std::snprintf(body, sizeof(body),
      "{\"init_sent\":%s,\"driver_assumption\":\"ST7735\",\"profile\":\"%s\",\"transport\":\"%s\","
      "\"backlight_source\":\"3V3-fixed\","
      "\"ui_language\":\"zh\",\"ui_layout\":\"sensor-carousel\","
      "\"page\":\"%s\",\"page_title\":\"%s\",\"page_index\":%u,\"page_count\":%u,"
      "\"auto_cycle\":%s,\"page_duration_ms\":8000,\"heart_in_cycle\":%s,"
      "\"page_values\":[\"%s\",\"%s\",\"%s\"],"
      "\"vcc_source\":\"FFS-X3-switched-3V3\",\"x3_power_readback_ok\":%s,"
      "\"x3_enable_bit\":%s,\"power_expander_outputs\":%u,"
      "\"clock_upper_bound_hz\":%lu,\"probe_elapsed_ms\":%lu,"
      "\"rotation\":%u,\"inverted\":%s,\"backlight\":%s,\"test_active\":%s,"
      "\"init_spi_hz\":%lu,\"pixel_spi_hz\":%lu,\"width\":%d,\"height\":%d,"
      "\"gpio\":{\"sck\":%d,\"mosi\":%d,\"reset\":%d,\"dc\":%d,\"cs\":%d,\"bl\":%d},"
      "\"output_signal\":{\"sck\":%u,\"mosi\":%u},"
      "\"levels\":{\"reset\":%d,\"dc\":%d,\"cs\":%d,\"bl\":%d},"
      "\"hardware_readback\":false}",
      s_initialized ? "true" : "false", profileName(),
      s_gpioProbe ? "gpio-slow" : s_softwareSpi ? "software" : "hardware",
      tftPageKey(values.page), tftPageTitle(values.page), unsigned(values.page) + 1,
      watch || values.page == TftPage::Heart ? 6u : 5u,
      s_carousel.automatic() ? "true" : "false", watch ? "true" : "false",
      values.metrics[0].text, values.metrics[1].text, values.metrics[2].text,
      powerReadback ? "true" : "false",
      powerReadback && (powerOutputs & 0x40) ? "true" : "false",
      unsigned(powerOutputs),
      static_cast<unsigned long>(s_gpioProbe ? 100000 : 0),
      static_cast<unsigned long>(s_probeElapsedMs), s_rotation,
      s_inverted ? "true" : "false", s_backlight ? "true" : "false",
      s_testActive ? "true" : "false",
      static_cast<unsigned long>(s_softwareSpi ? 0 : kTftInitFrequencyHz),
      static_cast<unsigned long>(s_softwareSpi ? 0 : s_pixelFrequencyHz),
      s_gpioProbe ? kWidth : s_tft->width(), s_gpioProbe ? kHeight : s_tft->height(),
      kTftSckPin, kTftMosiPin, kTftResetPin, kTftDcPin, kTftCsPin, kTftBacklightPin,
      unsigned(GPIO.func_out_sel_cfg[kTftSckPin].func_sel),
      unsigned(GPIO.func_out_sel_cfg[kTftMosiPin].func_sel),
      digitalRead(kTftResetPin), digitalRead(kTftDcPin), digitalRead(kTftCsPin),
      kTftBacklightPin >= 0 ? digitalRead(kTftBacklightPin) : -1);
  return String(body);
}
