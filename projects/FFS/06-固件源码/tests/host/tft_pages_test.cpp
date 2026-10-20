#include "Arduino.h"
#include "sensors.h"
#include "heart_rate.h"
#include "heart_rate_measurement.h"
#include "tft_pages.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

uint32_t testNow = 0;
static SensorValues sensors[6];
static bool available[6] = {true,true,true,true,true,true};
static bool running = true;
bool collectorLatestSensor(uint8_t channel, SensorValues& out) {
  assert(channel < 6);
  out = sensors[channel];
  return available[channel] && running;
}
struct Status { HeartRateSample sample; bool connected = false, subscribed = false; } status;
Status snapshot() { return status; }
#include "heart_display_under_test.inc"

int main() {
  TftCarousel pages;
  pages.restart(100);
  assert(!pages.tick(8099,false) && pages.page() == TftPage::Environment);
  for (unsigned i=1; i<=5; ++i) {
    assert(pages.tick(100+i*8000,false));
    assert(uint8_t(pages.page()) == i%5);
  }
  for (unsigned i=1; i<=5; ++i) pages.tick(40100+i*8000,true);
  assert(pages.page() == TftPage::Heart);
  assert(pages.tick(80101,false) && pages.page() == TftPage::Environment);
  pages.select(TftPage::Gas,90000);
  assert(!pages.automatic() && !pages.tick(200000,false) && pages.page() == TftPage::Gas);
  pages.setAutomatic(true,200000);
  assert(!pages.tick(207999,false) && pages.tick(208000,false));
  assert(pages.page() == TftPage::Spectrum);
  TftCarousel wrapped;
  wrapped.restart(0xFFFFF000u);
  assert(!wrapped.tick(uint32_t(0xFFFFF000u+7999u),false));
  assert(wrapped.tick(uint32_t(0xFFFFF000u+8000u),false));
  assert(wrapped.page() == TftPage::Pressure);

  sensors[0].tempC=27.7f; sensors[0].rh=58.8f;
  sensors[1].pressurePa=101234; sensors[1].tempC=28.1f;
  sensors[5].co2Ppm=1853; sensors[5].tempC=29.1f; sensors[5].rh=52.4f;
  auto data=tftPageValues(TftPage::Environment);
  assert(data.healthy && std::strcmp(data.metrics[0].text,"27.7")==0 && std::strcmp(data.metrics[2].text,"1853")==0);
  data=tftPageValues(TftPage::Pressure);
  assert(std::strcmp(data.metrics[0].text,"1012.3")==0 && std::strcmp(data.metrics[0].unit,"hPa")==0);
  sensors[0].tempC=std::numeric_limits<float>::quiet_NaN();
  data=tftPageValues(TftPage::Environment); assert(!data.metrics[0].valid && std::strcmp(data.metrics[0].text,"--")==0);
  sensors[3].alsRaw=0; sensors[3].uvRaw=0; sensors[2].flickerHz=1;
  data=tftPageValues(TftPage::Light);
  assert(data.metrics[0].valid && std::strcmp(data.metrics[0].text,"0")==0);
  assert(data.metrics[2].textValue && std::strcmp(data.metrics[2].text,"未知")==0);
  sensors[2].flickerHz=0; data=tftPageValues(TftPage::Light); assert(std::strcmp(data.metrics[2].text,"未检出")==0);
  sensors[2].flickerHz=100; data=tftPageValues(TftPage::Light); assert(std::strcmp(data.metrics[2].text,"100")==0 && std::strcmp(data.metrics[2].unit,"Hz")==0);
  sensors[4].vocIndex=0; sensors[4].noxIndex=0;
  data=tftPageValues(TftPage::Gas); assert(!data.metrics[0].valid && std::strcmp(data.footerNote,"气体预热")==0);
  sensors[4].vocIndex=120; sensors[4].noxIndex=5;
  data=tftPageValues(TftPage::Gas); assert(data.healthy && std::strcmp(data.metrics[0].text,"120")==0 && !data.footerNote);
  for (unsigned i=0; i<12; ++i) sensors[2].spectral[i]=i+1;
  sensors[2].spectral[4]=555; sensors[2].spectral[5]=666;
  data=tftPageValues(TftPage::Spectrum);
  assert(data.spectrum[3]==4 && data.spectrum[4]==7 && data.spectrum[7]==10);
  assert(data.spectrumPeak==10 && std::strcmp(data.metrics[1].text,"11")==0 && std::strcmp(data.metrics[2].text,"12")==0);
  available[2]=false; data=tftPageValues(TftPage::Spectrum);
  assert(!data.spectrumValid && data.spectrumPeak==0 && std::strcmp(data.metrics[1].text,"--")==0);
  running=false; data=tftPageValues(TftPage::Pressure); assert(!data.healthy && std::strcmp(data.metrics[0].text,"--")==0); running=true;

  status.connected=status.subscribed=true; testNow=1000;
  const uint8_t packet[]={0x06,72}; assert(status.sample.ingest(packet,2,testNow));
  auto hr=heartRateDisplayValues(); assert(hr.connected && hr.valid && hr.bpm==72);
  data=tftPageValues(TftPage::Heart); assert(data.healthy && std::strcmp(data.metrics[0].text,"72")==0);
  testNow=16001; hr=heartRateDisplayValues(); assert(!hr.valid && hr.bpm==0);
  data=tftPageValues(TftPage::Heart); assert(std::strcmp(data.metrics[0].text,"--")==0 && std::strcmp(data.footerNote,"心率过期")==0);
  const uint8_t poor[]={0x04,80}; status.sample.ingest(poor,2,testNow);
  hr=heartRateDisplayValues(); assert(!hr.valid && hr.bpm==0 && hr.contact==0);
  data=tftPageValues(TftPage::Heart); assert(std::strcmp(data.metrics[1].text,"不良")==0);
  status.subscribed=false; hr=heartRateDisplayValues(); assert(!hr.connected && !hr.valid);
  data=tftPageValues(TftPage::Heart); assert(!data.metrics[2].valid && std::strcmp(data.footerNote,"等待手表")==0);
  std::cout << "PASS: 8s carousel, conditional watch/skip, timer wrap, cached units/spectrum/warmup and real heart freshness/contact rules\n";
}
