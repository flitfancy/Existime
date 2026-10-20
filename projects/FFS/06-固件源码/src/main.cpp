#include <Arduino.h>
#include <driver/gpio.h>
#include <LittleFS.h>
#include <Wire.h>

#include "collector.h"
#include "heart_rate.h"
#include "config.h"
#include "config_eeprom.h"
#include "leds.h"
#include "sensors.h"
#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#include "wifi_config.example.h"
#endif
#include "wifi_link.h"
#include "phone_ble.h"
#include "web_panel.h"
#include "tft_display.h"

FireflyConfig g_config;
bool g_configValid = false;

// ---------------------------------------------------------------------------
// Auto dark（环境光熄灭装饰性 LED）
// ---------------------------------------------------------------------------
static bool s_autoDark = false;       // 当前处于熄灯状态
static bool s_autoDarkArmed = true;   // 需先经历一次亮光才能再次熄灯

// 手动 LED 测试（LEDRUN/LEDALL）优先：退出熄灯并解除武装，直到房间再次
// 变亮（超过 阈值+回差）后才重新武装，避免暗室里测试后灯被立刻按灭。
static void autoDarkManualOverride() {
  s_autoDark = false;
  s_autoDarkArmed = false;
  ledSetDarkMode(false);
}

static void updateAutoDark() {
  if (!kAutoDarkEnabled) {
    return;
  }
  uint32_t als = 0;
  if (!collectorLatestAls(als)) {
    return;  // 尚无新鲜光照数据（开机预热/传感器故障）：保持现状
  }
  if (s_autoDark) {
    if (als > kAutoDarkAlsThreshold + kAutoDarkAlsHysteresis) {
      s_autoDark = false;
      s_autoDarkArmed = true;
      ledSetDarkMode(false);
      Serial.printf("[led] auto-dark OFF als=%lu\n",
                    static_cast<unsigned long>(als));
    }
  } else if (s_autoDarkArmed && als < kAutoDarkAlsThreshold) {
    s_autoDark = true;
    ledSetDarkMode(true);
    Serial.printf("[led] auto-dark ON als=%lu\n",
                  static_cast<unsigned long>(als));
  }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

static void runEepromRead() {
  FireflyConfig cfg;
  const bool valid = eepromLoadConfig(cfg);
  Serial.printf("[eeprom] config_valid=%s\n", valid ? "YES" : "NO");
  if (valid) {
    Serial.printf(
        "[eeprom] ver=%u.%u device=%s date=%08lu interval=%us power=%s "
        "leds=%u heartbeat=%us comp=%u ssid='%s' server=%s:%u mask=0x%02X\n",
        static_cast<unsigned>(cfg.versionMajor),
        static_cast<unsigned>(cfg.versionMinor), cfg.deviceName,
        static_cast<unsigned long>(cfg.dateStamp),
        static_cast<unsigned>(cfg.intervalSec),
        cfg.powerMode == 0 ? "ALL_ON" : "PER_CHANNEL",
        static_cast<unsigned>(cfg.ledsEnabled),
        static_cast<unsigned>(cfg.heartbeatFastSec),
        static_cast<unsigned>(cfg.sgp41CompEnabled), cfg.ssid, cfg.serverIp,
        static_cast<unsigned>(cfg.tcpPort),
        static_cast<unsigned>(cfg.channelMask));
  }
  Serial.println("[FINAL] EEPROM_READ_DONE bytes=256");
}

static void runEepromWrite(uint32_t dateStamp) {
  FireflyConfig cfg = g_config;
  cfg.dateStamp = dateStamp;
  if (eepromSaveConfig(cfg)) {
    g_config = cfg;
    g_configValid = true;
    Serial.printf(
        "[eeprom] WRITE_OK ver=%u.%u device=%s date=%08lu interval=%us "
        "power=%s leds=%u heartbeat=%us comp=%u ssid='%s' server=%s:%u "
        "mask=0x%02X\n",
        static_cast<unsigned>(cfg.versionMajor),
        static_cast<unsigned>(cfg.versionMinor), cfg.deviceName,
        static_cast<unsigned long>(cfg.dateStamp),
        static_cast<unsigned>(cfg.intervalSec),
        cfg.powerMode == 0 ? "ALL_ON" : "PER_CHANNEL",
        static_cast<unsigned>(cfg.ledsEnabled),
        static_cast<unsigned>(cfg.heartbeatFastSec),
        static_cast<unsigned>(cfg.sgp41CompEnabled), cfg.ssid, cfg.serverIp,
        static_cast<unsigned>(cfg.tcpPort),
        static_cast<unsigned>(cfg.channelMask));
    Serial.println("[FINAL] EEPROM_WRITE_PASS readback=verified");
  } else {
    Serial.println("[FINAL] EEPROM_WRITE_FAIL readback=mismatch");
  }
}

static void runAddressScan() {
  const uint8_t addresses[] = {kPowerExpanderAddress, kLedExpanderAddress,
                               kEepromAddress, kMuxAddress};
  const char* names[] = {"U4 power", "U5 LED", "U6 EEPROM", "U3 mux"};
  Serial.println("[SCAN] upstream addresses:");
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t status = i2cProbeAddress(addresses[i]);
    Serial.printf("  0x%02X %s: %s\n", addresses[i], names[i],
                  status == 0 ? "ACK" : "NACK");
  }
  Serial.println("[FINAL] SCAN_DONE");
}

static void runLedRun() {
  autoDarkManualOverride();
  constexpr uint8_t kLaps = 3;
  for (uint8_t lap = 1; lap <= kLaps; ++lap) {
    for (uint8_t ch = 0; ch < 6; ++ch) {
      ledSetChannel(ch, true);
      delay(150);
      ledSetChannel(ch, false);
    }
    for (int8_t ch = 5; ch >= 0; --ch) {
      ledSetChannel(static_cast<uint8_t>(ch), true);
      delay(150);
      ledSetChannel(static_cast<uint8_t>(ch), false);
    }
  }
  Serial.println("[FINAL] LED_RUN_DONE");
}

static void runLedAll() {
  autoDarkManualOverride();
  for (uint8_t ch = 0; ch < 6; ++ch) {
    ledSetChannel(ch, true);
  }
  delay(2000);
  ledAllOff();
  Serial.println("[FINAL] LED_ALL_DONE");
}

static void runExportLog() {
  if (!LittleFS.exists(kStreamCsvPath)) {
    Serial.println("[FINAL] EXPORT_DONE rows=0 file=missing");
    return;
  }
  File f = LittleFS.open(kStreamCsvPath, "r");
  uint32_t lines = 0;
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) {
        Serial.println(line);
        ++lines;
      }
    }
    f.close();
  }
  Serial.printf("[FINAL] EXPORT_DONE rows=%lu\n",
                static_cast<unsigned long>(lines));
}

static void runLogClear() {
  const bool removed = LittleFS.remove(kStreamCsvPath);
  Serial.printf("[FINAL] LOGCLR_%s\n", removed ? "DONE" : "NOT_FOUND");
}

static void processCommands() {
  String command;
  if (Serial.available()) {
    command = Serial.readStringUntil('\n');
  } else {
    command = wifiTakeCommand();
  }
  command.trim();
  command.toUpperCase();
  if (command.isEmpty()) {
    return;
  }

  if (tftDisplayCommand(command)) {
    return;
  } else if (command == "X") {
    collectorStop();
  } else if (command.startsWith("MODE")) {
    if (command.length() == 4) {
      const CollectorMode m = collectorGetMode();
      Serial.printf("[mode] current=%u (%s)\n", static_cast<unsigned>(m),
                    m == CollectorMode::Slow
                        ? "slow 60s report"
                        : (m == CollectorMode::Normal ? "normal 10s report"
                                                     : "fast 2s report"));
    } else {
      const long mode = command.substring(5).toInt();
      if (mode >= 1 && mode <= 3) {
      if (!collectorRunning()) {
        collectorSetup();
      }
      collectorSetMode(static_cast<CollectorMode>(mode));
      } else {
        Serial.println("[mode] usage: MODE 1 (slow) / 2 (normal) / 3 (fast)");
      }
    }
  } else if (command == "RUNALL") {
    if (collectorRunning()) {
      collectorEmitNow();
      Serial.println("[FINAL] RUNALL_SNAPSHOT_EMITTED collector remains active");
    } else {
      Serial.println("[FINAL] RUNALL_UNAVAILABLE collector stopped; use MODE 1/2/3");
    }
  } else if (command == "EEPROMREAD") {
    runEepromRead();
  } else if (command.startsWith("EEPROMWRITE")) {
    uint32_t dateStamp = kDefaultDateStamp;
    if (command.length() > 11) {
      dateStamp = strtoul(command.c_str() + 11, nullptr, 10);
    }
    runEepromWrite(dateStamp);
  } else if (command == "WIFISTATUS") {
    wifiStatusPrint();
  } else if (command == "WIFIRECONNECT") {
    wifiReconnect();
  } else if (command == "SCAN") {
    runAddressScan();
  } else if (command == "LEDRUN") {
    runLedRun();
  } else if (command == "LEDALL") {
    runLedAll();
  } else if (command == "EXPORT") {
    runExportLog();
  } else if (command == "LOGCLR") {
    runLogClear();
  } else {
    Serial.printf(
        "[info] unknown '%s'; use MODE 1/2/3, X, RUNALL, EEPROMREAD/WRITE, "
        "WIFISTATUS, WIFIRECONNECT, SCAN, LEDRUN, LEDALL, EXPORT, LOGCLR, TFTSTATUS/TFTTEST\n",
        command.c_str());
  }
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) {
    delay(10);
  }

  Wire.begin(kSdaPin, kSclPin, kI2cFrequencyHz);
  Wire.setTimeOut(kI2cTimeoutMs);
  gpio_pullup_dis(static_cast<gpio_num_t>(kSdaPin));
  gpio_pullup_dis(static_cast<gpio_num_t>(kSclPin));
  pinMode(kResetPin, INPUT);
  pinMode(kIntPin, INPUT);

  if (!LittleFS.begin(true)) {
    Serial.println("[warn] LittleFS mount failed; ring log disabled.");
  }

  strncpy(g_config.signature, kEepromSignatureStr,
          sizeof(g_config.signature) - 1);
  g_configValid = eepromLoadConfig(g_config);
  if (!g_configValid) {
    strncpy(g_config.ssid, WIFI_SSID, sizeof(g_config.ssid) - 1);
    strncpy(g_config.password, WIFI_PASSWORD, sizeof(g_config.password) - 1);
  }
  Serial.printf("[eeprom] config_valid=%s device=%s date=%08lu\n",
                g_configValid ? "YES" : "NO", g_config.deviceName,
                static_cast<unsigned long>(g_config.dateStamp));

  if (!kTftEnabled || powerAccessorySetup()) {
    tftDisplaySetup();
  } else {
    Serial.println("[tft] X3 power setup failed; panel initialization skipped.");
  }
  ledInit();
  wifiSetup();
  webPanelSetup();
  collectorSetup();
  heartRateSetup();
  Serial.printf("[ready] FIREFLY-SENSE firmware=%s schema=%u scheduler=%s "
                "power=all-on mux=one\n",
                kFirmwareVersion, static_cast<unsigned>(kSensorSchemaVersion),
                kSchedulerName);
}

void loop() {
  wifiTick();
  heartRateTick();
  ledUpdateActivity();
  updateAutoDark();
  ledUpdateHeartbeat(wifiLinkConfirmed());
  const auto heart = heartRateDisplayValues();
  ledUpdateHeartRate(heart.valid, heart.bpm);
  collectorTick();
  phoneBleTick();
  webPanelTick();
  processCommands();
  tftDisplayTick();
  delay(2);
}
