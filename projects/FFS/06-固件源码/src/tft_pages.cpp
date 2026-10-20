#include "tft_pages.h"

#include <cmath>
#include <cstdio>
#include "collector.h"
#include "sensors.h"
#include "heart_rate.h"

namespace {
void number(TftMetric& out, const char* label, const char* unit, bool valid, uint32_t value) {
  out.label = label; out.unit = unit; out.valid = valid;
  if (valid) std::snprintf(out.text, sizeof(out.text), "%lu", static_cast<unsigned long>(value));
}
void decimal(TftMetric& out, const char* label, const char* unit, bool valid, float value) {
  out.label = label; out.unit = unit; out.valid = valid;
  if (valid) std::snprintf(out.text, sizeof(out.text), "%.1f", static_cast<double>(value));
}
void word(TftMetric& out, const char* label, bool valid, const char* value) {
  out.label = label; out.valid = valid; out.textValue = true;
  if (valid) std::snprintf(out.text, sizeof(out.text), "%s", value);
}
bool temperature(bool ready, float value) {
  return ready && std::isfinite(value) && value >= -40 && value <= 125;
}
bool humidity(bool ready, float value) {
  return ready && std::isfinite(value) && value >= 0 && value <= 100;
}
}  // namespace

const char* tftPageTitle(TftPage page) {
  const char* titles[] = {"环境", "气压", "光照", "气体", "光谱", "心率"};
  return uint8_t(page) < 6 ? titles[uint8_t(page)] : "环境";
}
const char* tftPageKey(TftPage page) {
  const char* keys[] = {"environment", "pressure", "light", "gas", "spectrum", "heart"};
  return uint8_t(page) < 6 ? keys[uint8_t(page)] : "environment";
}

TftPageValues tftPageValues(TftPage page) {
  TftPageValues out;
  out.page = page;
  SensorValues first, second;
  bool a = false, b = false;
  switch (page) {
    case TftPage::Environment:
      a = collectorLatestSensor(0, first); b = collectorLatestSensor(5, second);
      decimal(out.metrics[0], "温度", "°C", temperature(a, first.tempC), first.tempC);
      decimal(out.metrics[1], "湿度", "%", humidity(a, first.rh), first.rh);
      number(out.metrics[2], "二氧化碳", "ppm", b && second.co2Ppm != 0, second.co2Ppm);
      break;
    case TftPage::Pressure:
      a = collectorLatestSensor(1, first); b = collectorLatestSensor(5, second);
      decimal(out.metrics[0], "气压", "hPa", a && std::isfinite(first.pressurePa) &&
              first.pressurePa >= 30000 && first.pressurePa <= 125000, first.pressurePa / 100);
      decimal(out.metrics[1], "气压温度", "°C", temperature(a, first.tempC), first.tempC);
      decimal(out.metrics[2], "CO2温度", "°C", temperature(b, second.tempC), second.tempC);
      break;
    case TftPage::Light:
      a = collectorLatestSensor(3, first); b = collectorLatestSensor(2, second);
      number(out.metrics[0], "光照读数", "计数", a, first.alsRaw);
      number(out.metrics[1], "紫外读数", "计数", a, first.uvRaw);
      if (second.flickerHz <= 1)
        word(out.metrics[2], "频闪", b, second.flickerHz == 0 ? "未检出" : "未知");
      else number(out.metrics[2], "频闪", "Hz", b, second.flickerHz);
      break;
    case TftPage::Gas:
      a = collectorLatestSensor(4, first); b = collectorLatestSensor(5, second);
      number(out.metrics[0], "VOC指数", "", a && first.vocIndex > 0 && first.vocIndex <= 500, first.vocIndex);
      number(out.metrics[1], "NOx指数", "", a && first.noxIndex > 0 && first.noxIndex <= 500, first.noxIndex);
      decimal(out.metrics[2], "CO2湿度", "%", humidity(b, second.rh), second.rh);
      if (a && (!out.metrics[0].valid || !out.metrics[1].valid)) out.footerNote = "气体预热";
      break;
    case TftPage::Spectrum: {
      a = collectorLatestSensor(2, first);
      // Same channel order as the existing CSV, skipping first-pass clear/NIR.
      const uint8_t order[8] = {0, 1, 2, 3, 6, 7, 8, 9};
      out.spectrumValid = a;
      for (uint8_t i = 0; i < 8; ++i) {
        out.spectrum[i] = a ? first.spectral[order[i]] : 0;
        if (out.spectrum[i] > out.spectrumPeak) out.spectrumPeak = out.spectrum[i];
      }
      number(out.metrics[0], "相对光谱", "计数", a, out.spectrumPeak);
      number(out.metrics[1], "宽带光", "计数", a, first.spectral[10]);
      number(out.metrics[2], "近红外", "计数", a, first.spectral[11]);
      break;
    }
    case TftPage::Heart: {
      const auto hr = heartRateDisplayValues();
      out.watchConnected = hr.connected;
      number(out.metrics[0], "心率", "bpm", hr.valid, hr.bpm);
      word(out.metrics[1], "传感接触", hr.connected,
           hr.contact < 0 ? "未知" : hr.contact ? "良好" : "不良");
      number(out.metrics[2], "数据年龄", "秒", hr.connected && hr.hasSample, hr.ageMs / 1000);
      out.footerNote = !hr.connected ? "等待手表" : hr.contact == 0 ? "检查佩戴" :
           hr.valid ? "实时心率" : hr.hasSample && hr.ageMs > 15000 ? "心率过期" : "等待心率";
      break;
    }
  }
  out.healthy = out.metrics[0].valid && out.metrics[1].valid && out.metrics[2].valid;
  return out;
}
