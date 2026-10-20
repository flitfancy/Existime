#pragma once

#include <Adafruit_GFX.h>
#include "tft_pages.h"

constexpr int kTftUiWidth = 160;
constexpr int kTftUiHeight = 128;
constexpr int kTftUiTemperatureY = 18;
constexpr int kTftUiTemperatureHeight = 54;
constexpr int kTftUiSecondaryY = 76;
constexpr int kTftUiSecondaryHeight = 38;
constexpr int kTftUiFooterY = 115;
constexpr uint16_t kTftUiBackground = 0x0000;

void tftUiHeader(Adafruit_GFX& target, bool wifi, const char* title = nullptr);
void tftUiTemperature(Adafruit_GFX& target, const char* value, bool valid);
void tftUiSecondary(Adafruit_GFX& target, const char* humidity, bool humidityValid,
                    const char* co2, bool co2Valid);
void tftUiFooter(Adafruit_GFX& target, bool running, bool healthy, bool connected,
                 const char* note = nullptr, uint8_t position = 0, uint8_t count = 0,
                 bool heartPage = false, bool watchConnected = false);
void tftUiPagePrimary(Adafruit_GFX& target, const TftPageValues& values);
void tftUiPageSecondary(Adafruit_GFX& target, const TftPageValues& values);
