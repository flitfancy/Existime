#include "tft_ui.h"

#include <U8g2_for_Adafruit_GFX.h>
#include <cstring>
#include <cstdio>

namespace {
constexpr uint16_t kCard = 0x10C4;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kMuted = 0x7C53;
constexpr uint16_t kTemperature = 0xFD6C;
constexpr uint16_t kHumidity = 0x66D9;
constexpr uint16_t kCo2 = 0xA6CD;
constexpr uint16_t kConnected = 0x5EA7;
constexpr uint16_t kHeart = 0xFB0F;

bool unicode(const char* value) {
  while (*value) if (uint8_t(*value++) >= 128) return true;
  return false;
}
void font(U8G2_FOR_ADAFRUIT_GFX& text, const uint8_t* value) {
  text.setFont(value);
  text.setFontMode(1);
}

void label(U8G2_FOR_ADAFRUIT_GFX& text, int x, int baseline,
           const char* value, uint16_t color) {
  text.setFont(u8g2_font_wqy12_t_gb2312);
  text.setFontMode(1);
  text.setForegroundColor(color);
  text.drawUTF8(x, baseline, value);
}

void smallValue(U8G2_FOR_ADAFRUIT_GFX& text, Adafruit_GFX& target, int x, const TftMetric& metric) {
  const char* value = metric.text;
  const char* unit = std::strcmp(metric.unit, "计数") == 0 ? "cnt" : metric.unit;
  if (metric.textValue) {
    label(text, x, 33, value, metric.valid ? kWhite : kMuted);
    return;
  }
  font(text, u8g2_font_wqy12_t_gb2312);
  const int unitWidth = unicode(unit) ? text.getUTF8Width(unit) : std::strlen(unit) * 6;
  font(text, u8g2_font_helvB12_tn);
  int width = text.getUTF8Width(value);
  if (width + 3 + unitWidth > 60) {
    font(text, u8g2_font_helvB10_tn);
    width = text.getUTF8Width(value);
  }
  if (width + 3 + unitWidth > 60) {
    font(text, u8g2_font_4x6_tf);
    width = text.getUTF8Width(value);
  }
  text.setForegroundColor(metric.valid ? kWhite : kMuted);
  text.drawUTF8(x, 33, value);
  if (unicode(unit)) {
    label(text, x + width + 3, 33, unit, kMuted);
    return;
  }
  target.setFont(nullptr);
  target.setTextSize(1);
  target.setTextColor(kMuted);
  target.setCursor(x + width + 3, 25);
  target.print(unit);
}
}  // namespace

void tftUiHeader(Adafruit_GFX& target, bool wifi, const char* title) {
  target.fillRect(0, 0, 160, 16, kTftUiBackground);
  target.setFont(nullptr);
  target.setTextWrap(false);
  target.setTextSize(1);
  target.setTextColor(kWhite);
  target.setCursor(8, 4);
  target.print("FIREFLY");
  if (title) {
    U8G2_FOR_ADAFRUIT_GFX text;
    text.begin(target);
    label(text, 57, 12, title, kMuted);
  } else {
    target.setTextColor(kMuted);
    target.setCursor(56, 4);
    target.print("SENSE");
  }
  target.fillCircle(121, 7, 2, wifi ? kConnected : kMuted);
  target.setTextColor(wifi ? kConnected : kMuted);
  target.setCursor(130, 4);
  target.print("WiFi");
}

void tftUiTemperature(Adafruit_GFX& target, const char* value, bool valid) {
  TftPageValues data;
  data.metrics[0].label = "温度"; data.metrics[0].unit = "°C";
  data.metrics[0].valid = valid;
  std::snprintf(data.metrics[0].text, sizeof(data.metrics[0].text), "%s", value);
  tftUiPagePrimary(target, data);
}

void tftUiPagePrimary(Adafruit_GFX& target, const TftPageValues& values) {
  target.fillScreen(kTftUiBackground);
  target.fillRoundRect(4, 0, 152, kTftUiTemperatureHeight, 6, kCard);
  U8G2_FOR_ADAFRUIT_GFX text;
  text.begin(target);
  if (values.page == TftPage::Spectrum) {
    label(text, 10, 13, "相对读数", kMuted);
    font(text, u8g2_font_4x6_tf);
    text.setForegroundColor(kMuted);
    char peak[16];
    std::snprintf(peak, sizeof(peak), "MAX %ucnt", unsigned(values.spectrumPeak));
    text.drawUTF8(100, 12, values.spectrumValid ? peak : "MAX --");
    if (!values.spectrumValid) {
      label(text, 44, 36, "等待光谱数据", kMuted);
      return;
    }
    const uint16_t colors[8] = {0xA15F,0x319F,0x04DF,0x07ED,0xA7E0,0xFD80,0xFA60,0xF8A3};
    const char* wavelength[8] = {"415","445","480","515","555","590","630","680"};
    for (uint8_t i = 0; i < 8; ++i) {
      const int height = values.spectrumPeak ? uint32_t(values.spectrum[i]) * 28 / values.spectrumPeak : 0;
      target.fillRect(10 + i*18, 43-height, 12, height, colors[i]);
      text.drawUTF8(10 + i*18, 52, wavelength[i]);
    }
    return;
  }
  const auto& metric = values.metrics[0];
  const uint16_t accent = values.page == TftPage::Heart ? kHeart :
      values.page == TftPage::Gas ? kCo2 : values.page == TftPage::Pressure ? kHumidity : kTemperature;
  label(text, 12, 17, metric.label, accent);
  if (values.page == TftPage::Environment) {
    target.drawRoundRect(16, 25, 5, 18, 2, accent);
    target.fillCircle(18, 43, 4, accent);
    target.drawFastVLine(18, 30, 12, accent);
  } else if (values.page == TftPage::Heart) {
    target.fillCircle(18, 34, 5, accent); target.fillCircle(26, 34, 5, accent);
    target.fillTriangle(13, 35, 31, 35, 22, 46, accent);
  } else if (values.page == TftPage::Light) {
    target.drawCircle(22, 36, 5, accent);
    target.drawFastHLine(11, 36, 4, accent); target.drawFastHLine(29, 36, 4, accent);
    target.drawFastVLine(22, 25, 4, accent); target.drawFastVLine(22, 44, 4, accent);
  } else if (values.page == TftPage::Pressure) {
    target.drawCircle(22, 38, 8, accent); target.drawLine(22, 38, 27, 32, accent);
  } else {
    target.drawLine(17, 39, 27, 30, accent); target.drawLine(17, 39, 29, 45, accent);
    target.fillCircle(17, 39, 3, accent); target.fillCircle(27, 30, 3, accent); target.fillCircle(29, 45, 3, accent);
  }
  font(text, u8g2_font_wqy12_t_gb2312);
  const int unitWidth = text.getUTF8Width(metric.unit);
  const int right = 152 - (unitWidth ? unitWidth + 4 : 0);
  const uint8_t* sizes[] = {u8g2_font_logisoso28_tn, u8g2_font_logisoso24_tn,
                            u8g2_font_logisoso20_tn, u8g2_font_logisoso16_tn};
  int width = 0;
  for (const auto* size : sizes) {
    font(text, size); width = text.getUTF8Width(metric.text);
    if (width <= right - 34) break;
  }
  text.setForegroundColor(metric.valid ? kWhite : kMuted);
  text.drawUTF8(right - width, 48, metric.text);
  label(text, right + 4, 47, metric.unit, kMuted);
}

void tftUiSecondary(Adafruit_GFX& target, const char* humidity, bool humidityValid,
                    const char* co2, bool co2Valid) {
  TftPageValues data;
  data.metrics[1].label = "湿度"; data.metrics[1].unit = "%"; data.metrics[1].valid = humidityValid;
  data.metrics[2].label = "二氧化碳"; data.metrics[2].unit = "ppm"; data.metrics[2].valid = co2Valid;
  std::snprintf(data.metrics[1].text, sizeof(data.metrics[1].text), "%s", humidity);
  std::snprintf(data.metrics[2].text, sizeof(data.metrics[2].text), "%s", co2);
  tftUiPageSecondary(target, data);
}

void tftUiPageSecondary(Adafruit_GFX& target, const TftPageValues& values) {
  target.fillScreen(kTftUiBackground);
  target.fillRoundRect(4, 0, 73, kTftUiSecondaryHeight, 5, kCard);
  target.fillRoundRect(82, 0, 74, kTftUiSecondaryHeight, 5, kCard);
  U8G2_FOR_ADAFRUIT_GFX text;
  text.begin(target);
  text.setFontMode(1);
  label(text, 12, 14, values.metrics[1].label, kHumidity);
  label(text, 90, 14, values.metrics[2].label, kCo2);
  smallValue(text, target, 12, values.metrics[1]);
  smallValue(text, target, 90, values.metrics[2]);
}

void tftUiFooter(Adafruit_GFX& target, bool running, bool healthy, bool connected,
                 const char* note, uint8_t position, uint8_t count, bool heartPage, bool watchConnected) {
  target.fillRect(0, kTftUiFooterY, 160, 13, kTftUiBackground);
  U8G2_FOR_ADAFRUIT_GFX text;
  text.begin(target);
  text.setFontMode(1);
  label(text, 8, 126, note ? note : !running ? "采集暂停" : healthy ? "实时采集" : "等待数据",
        healthy && (running || heartPage) ? kMuted : kTemperature);
  if (heartPage) label(text, 104, 126, watchConnected ? "手表在线" : "等待连接", watchConnected ? kConnected : kMuted);
  else label(text, 128, 126, connected ? "联机" : "离线", connected ? kConnected : kMuted);
  if (count) {
    char page[8]; std::snprintf(page, sizeof(page), "%u/%u", unsigned(position), unsigned(count));
    target.setFont(nullptr); target.setTextSize(1); target.setTextColor(kMuted);
    target.setCursor(82, 119); target.print(page);
  }
}
