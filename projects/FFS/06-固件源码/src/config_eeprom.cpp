#include "config_eeprom.h"

#include <Wire.h>

namespace {

bool eepromReadBlock(uint16_t offset, uint8_t* buf, size_t len) {
  if (offset + len > 256) {
    return false;
  }
  Wire.beginTransmission(kEepromAddress);
  Wire.write(static_cast<uint8_t>(offset));
  if (Wire.endTransmission() != 0) {
    return false;
  }
  const size_t got =
      Wire.requestFrom(static_cast<uint8_t>(kEepromAddress), len);
  if (got != len) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    buf[i] = Wire.read();
  }
  return true;
}

// AT24C02 has an 8-byte page; never cross a page boundary in one write.
bool eepromWritePage(uint16_t offset, const uint8_t* buf, size_t len) {
  if (len == 0 || len > 8 || (offset & 0x7) + len > 8) {
    return false;
  }
  Wire.beginTransmission(kEepromAddress);
  Wire.write(static_cast<uint8_t>(offset));
  for (size_t i = 0; i < len; ++i) {
    Wire.write(buf[i]);
  }
  const uint8_t status = Wire.endTransmission();
  delay(5);  // AT24C02 write cycle time.
  return status == 0;
}

bool eepromWriteBlock(uint16_t offset, const uint8_t* buf, size_t len) {
  if (offset + len > 256) {
    return false;
  }
  size_t done = 0;
  while (done < len) {
    const uint16_t cur = offset + done;
    const size_t chunk =
        (len - done < 8 - static_cast<size_t>(cur & 0x7))
            ? (len - done)
            : (8 - static_cast<size_t>(cur & 0x7));
    if (!eepromWritePage(cur, buf + done, chunk)) {
      return false;
    }
    done += chunk;
  }
  return true;
}

bool eepromSerialize(const FireflyConfig& cfg, uint8_t out[kEepromConfigBytes]) {
  memset(out, 0, kEepromConfigBytes);
  uint16_t o = 0;
  auto putU16 = [&](uint16_t v) {
    memcpy(out + o, &v, 2);
    o += 2;
  };
  auto putU8 = [&](uint8_t v) { out[o++] = v; };
  auto putU32 = [&](uint32_t v) {
    memcpy(out + o, &v, 4);
    o += 4;
  };
  auto putStr = [&](const char* s, size_t n) {
    strncpy(reinterpret_cast<char*>(out + o), s ? s : "", n);
    o += n;
  };
  putStr(cfg.signature, kEepromSignatureLen);
  putU8(cfg.versionMajor);
  putU8(cfg.versionMinor);
  putU8(cfg.deviceId);
  putU8(cfg.channelMask);
  memcpy(out + o, cfg.addresses, sizeof(cfg.addresses));
  o += sizeof(cfg.addresses);
  putU8(cfg.intervalSec);
  putU8(cfg.powerMode);
  putU8(cfg.ledsEnabled);
  putU8(cfg.heartbeatFastSec);
  putU8(cfg.sgp41CompEnabled);
  putU8(cfg.reserved);
  putU16(cfg.tcpPort);
  putStr(cfg.ssid, 32);
  putStr(cfg.password, 64);
  putStr(cfg.serverIp, 16);
  putStr(cfg.deviceName, 8);
  putU32(cfg.dateStamp);
  return o == kEepromConfigBytes;
}

bool eepromDeserialize(const uint8_t in[kEepromConfigBytes],
                       FireflyConfig& cfg) {
  uint16_t o = 0;
  auto getU16 = [&]() -> uint16_t {
    uint16_t v = 0;
    memcpy(&v, in + o, 2);
    o += 2;
    return v;
  };
  auto getU8 = [&]() -> uint8_t { return in[o++]; };
  auto getU32 = [&]() -> uint32_t {
    uint32_t v = 0;
    memcpy(&v, in + o, 4);
    o += 4;
    return v;
  };
  auto getStr = [&](char* dst, size_t n) {
    memcpy(dst, in + o, n);
    dst[n - 1] = '\0';
    o += n;
  };
  getStr(cfg.signature, kEepromSignatureLen);
  cfg.versionMajor = getU8();
  cfg.versionMinor = getU8();
  cfg.deviceId = getU8();
  cfg.channelMask = getU8();
  memcpy(cfg.addresses, in + o, sizeof(cfg.addresses));
  o += sizeof(cfg.addresses);
  cfg.intervalSec = getU8();
  cfg.powerMode = getU8();
  cfg.ledsEnabled = getU8();
  cfg.heartbeatFastSec = getU8();
  cfg.sgp41CompEnabled = getU8();
  cfg.reserved = getU8();
  cfg.tcpPort = getU16();
  getStr(cfg.ssid, sizeof(cfg.ssid));
  getStr(cfg.password, sizeof(cfg.password));
  getStr(cfg.serverIp, sizeof(cfg.serverIp));
  getStr(cfg.deviceName, sizeof(cfg.deviceName));
  cfg.dateStamp = getU32();
  return o == kEepromConfigBytes;
}

}  // namespace

uint32_t eepromCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      const uint32_t mask =
          static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u));
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

bool eepromSaveConfig(const FireflyConfig& cfg) {
  uint8_t buf[kEepromConfigBytes];
  if (!eepromSerialize(cfg, buf)) {
    return false;
  }
  const uint32_t crc = eepromCrc32(buf, sizeof(buf));
  if (!eepromWriteBlock(0, buf, sizeof(buf))) {
    return false;
  }
  if (!eepromWriteBlock(kEepromCrcOffset,
                        reinterpret_cast<const uint8_t*>(&crc), 4)) {
    return false;
  }
  uint8_t rb[kEepromConfigBytes];
  if (!eepromReadBlock(0, rb, sizeof(rb))) {
    return false;
  }
  return memcmp(rb, buf, sizeof(buf)) == 0;
}

bool eepromLoadConfig(FireflyConfig& cfg) {
  uint8_t buf[kEepromConfigBytes];
  if (!eepromReadBlock(0, buf, sizeof(buf))) {
    return false;
  }
  uint32_t crcStored = 0;
  if (!eepromReadBlock(kEepromCrcOffset,
                       reinterpret_cast<uint8_t*>(&crcStored), 4)) {
    return false;
  }
  if (eepromCrc32(buf, sizeof(buf)) != crcStored) {
    return false;
  }
  FireflyConfig parsed;
  if (!eepromDeserialize(buf, parsed)) {
    return false;
  }
  if (strncmp(parsed.signature, kEepromSignatureStr,
              sizeof(kEepromSignatureStr) - 1) != 0 ||
      parsed.versionMajor != kEepromConfigVersionMajor ||
      parsed.versionMinor != kEepromConfigVersionMinor) {
    return false;
  }
  cfg = parsed;
  return true;
}
