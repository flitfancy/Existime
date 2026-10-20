#pragma once

#include <Arduino.h>

#include "config.h"

// AT24C02 configuration layout (256 bytes total).
constexpr char kEepromSignatureStr[] = "FIREFLY REAL-WORLD SENSE R1.1";
constexpr uint8_t kEepromSignatureLen = 32;
constexpr uint8_t kEepromConfigVersionMajor = 1;
constexpr uint8_t kEepromConfigVersionMinor = 1;
constexpr uint16_t kEepromCrcOffset = 174;
constexpr uint16_t kEepromConfigBytes = 174;       // bytes 0..173 covered by CRC.
constexpr uint32_t kDefaultDateStamp = 20260806;

struct FireflyConfig {
  char signature[kEepromSignatureLen] = "";
  uint8_t versionMajor = kEepromConfigVersionMajor;
  uint8_t versionMinor = kEepromConfigVersionMinor;
  uint8_t deviceId = 0x01;
  uint8_t channelMask = 0x3F;
  uint8_t addresses[6] = {0x44, 0x46, 0x39, 0x53, 0x59, 0x62};
  uint8_t intervalSec = 10;
  uint8_t powerMode = 0;          // 0=all-on, 1=sequential
  uint8_t ledsEnabled = 1;
  uint8_t heartbeatFastSec = 1;
  uint8_t sgp41CompEnabled = 1;
  uint8_t reserved = 0xFF;
  uint16_t tcpPort = 7777;
  char ssid[32] = "";
  char password[64] = "";
  char serverIp[16] = "192.168.1.15";
  char deviceName[8] = "FLY-01";
  uint32_t dateStamp = kDefaultDateStamp;
};

uint32_t eepromCrc32(const uint8_t* data, size_t len);
bool eepromSaveConfig(const FireflyConfig& cfg);
bool eepromLoadConfig(FireflyConfig& cfg);

// Runtime configuration instance owned by main.cpp.
extern FireflyConfig g_config;
extern bool g_configValid;
