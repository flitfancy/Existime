#include "wifi_link.h"

#include "config.h"
#include "config_eeprom.h"
#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#include "wifi_config.example.h"
#endif

#include <WiFi.h>

static WiFiClient s_client;
static unsigned long s_lastAttemptAt = 0;
static uint32_t s_failCount = 0;
static bool s_linkConfirmed = false;
static unsigned long s_lastPongAt = 0;
static unsigned long s_lastPingAt = 0;
static String s_pendingCommand;

static String helloLine() {
  String line;
  line.reserve(128);
  line += "HELLO,firmware=";
  line += kFirmwareVersion;
  line += ",schema=";
  line += static_cast<unsigned>(kSensorSchemaVersion);
  line += ",scheduler=";
  line += kSchedulerName;
  line += ",power=all-on,mux=one";
  return line;
}

void wifiSetup() {
  WiFi.mode(WIFI_STA);
  // ESP-IDF 4.4 requires modem sleep for Wi-Fi/BLE radio coexistence.
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(g_config.ssid, g_config.password);
  Serial.printf("[wifi] connecting to '%s' (2.4 GHz)...\n", g_config.ssid);
}

void wifiSendLine(const String& line) {
  if (!s_client.connected()) {
    return;
  }
  if (s_client.println(line) <= 0) {
    s_client.stop();
  }
}

bool wifiLinkConfirmed() { return s_linkConfirmed; }

int wifiRssiDbm() {
  return WiFi.status() == WL_CONNECTED ? static_cast<int>(WiFi.RSSI()) : 0;
}

String wifiTakeCommand() {
  const String c = s_pendingCommand;
  s_pendingCommand = "";
  return c;
}

void wifiTick() {
  const uint32_t now = millis();
  const uint32_t retryMs =
      s_failCount >= 6 ? 30000UL : (WIFI_RETRY_SECONDS * 1000UL);

  if (WiFi.status() != WL_CONNECTED) {
    if (now - s_lastAttemptAt >= retryMs) {
      s_lastAttemptAt = now;
      ++s_failCount;
      if (s_client.connected()) {
        s_client.stop();
      }
      WiFi.disconnect();
      WiFi.begin(g_config.ssid, g_config.password);
    }
    return;
  }
  s_failCount = 0;

  if (!s_client.connected()) {
    if (now - s_lastAttemptAt >= retryMs) {
      s_lastAttemptAt = now;
      ++s_failCount;
      s_client.stop();  // close stale half-open sockets before reconnecting.
      IPAddress serverIp;
      serverIp.fromString(g_config.serverIp);
      if (s_client.connect(serverIp, g_config.tcpPort, 500)) {
        wifiSendLine(helloLine());
        wifiSendLine(kStreamCsvHeader);
      }
    }
    return;
  }

  // Two-way heartbeat: PING every 3 s; PONG confirms the link.
  if (now - s_lastPingAt >= 3000) {
    s_lastPingAt = now;
    wifiSendLine("PING " +
                 String(static_cast<unsigned long>(millis() / 1000)));
  }
  while (s_client.available()) {
    String line;
    line.reserve(64);
    while (s_client.available() && line.length() < 128) {
      const char c = static_cast<char>(s_client.read());
      if (c == '\n') {
        break;
      }
      line += c;
    }
    line.trim();
    if (line.startsWith("PONG")) {
      s_lastPongAt = now;
      s_linkConfirmed = true;
    } else if (line.startsWith("PING")) {
      wifiSendLine("PONG " +
                   String(static_cast<unsigned long>(millis() / 1000)));
    } else if (!line.isEmpty()) {
      s_pendingCommand = line;
    }
  }
  if (now - s_lastPongAt > kHeartbeatConfirmTimeoutMs) {
    s_linkConfirmed = false;
    // 链路确认超时：主动断开半死连接，下一轮 tick 即走重连分支。
    // 此前只把它当 LED 信号，半开 TCP 会拖到 LWIP 重传超时（几十分钟）
    // 才翻脸；现在最迟约 15-20 秒自愈。
    if (s_client.connected()) {
      s_client.stop();
    }
    s_lastPongAt = now;  // 防止本轮反复 stop
    ++s_failCount;       // 连续失败自动降频到 30s 重试，避免打爆
  }
}

void wifiStatusPrint() {
  const wl_status_t status = WiFi.status();
  Serial.printf(
      "[wifi] state=%d connected=%s ip=%s rssi=%ddBm client=%s link2way=%s "
      "server=%s:%u\n",
      static_cast<int>(status), status == WL_CONNECTED ? "YES" : "NO",
      WiFi.localIP().toString().c_str(),
      status == WL_CONNECTED ? static_cast<int>(WiFi.RSSI()) : 0,
      s_client.connected() ? "UP" : "DOWN",
      s_linkConfirmed ? "YES" : "NO", g_config.serverIp,
      static_cast<unsigned>(g_config.tcpPort));
}

void wifiReconnect() {
  s_client.stop();
  s_linkConfirmed = false;
  s_lastPongAt = 0;
  WiFi.disconnect();
  WiFi.begin(g_config.ssid, g_config.password);
  Serial.println("[wifi] reconnect requested.");
}
