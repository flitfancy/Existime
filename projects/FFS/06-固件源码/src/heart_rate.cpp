#include "heart_rate.h"
#include "heart_rate_measurement.h"
#include "collector.h"
#include "config.h"
#include "web_panel.h"
#include "wifi_link.h"
#include "phone_ble.h"
#include "leds.h"
#include <NimBLEDevice.h>

namespace {
// Bench-discovered name + HRS advertisement; never select an arbitrary monitor.
constexpr char kWatchName[] = "Xiaomi C4A9";
constexpr char kWatchAddress[] = "ed:fc:17:92:9b:91";
enum class LinkState : uint8_t { Starting, Scanning, Connecting, Subscribing, Waiting, Streaming, Retry, Error };
struct Status {
  HeartRateSample sample;
  LinkState state = LinkState::Starting;
  bool connected = false;
  bool subscribed = false;
  char address[18] = {};
  int rssi = 0;
  int disconnectReason = 0;
  uint32_t attempts = 0;
  uint32_t disconnects = 0;
  uint32_t scanRounds = 0;
  uint32_t heartAdvertisements = 0;
  uint32_t lastAdvertisementAt = 0;
};
struct Candidate {
  char address[18] = {};
  uint8_t type = 0;
  int rssi = 0;
  bool pending = false;
};
Status status;
Candidate candidate;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

Status snapshot() {
  portENTER_CRITICAL(&lock);
  Status value = status;
  portEXIT_CRITICAL(&lock);
  return value;
}
void setState(LinkState state) {
  portENTER_CRITICAL(&lock);
  status.state = state;
  portEXIT_CRITICAL(&lock);
}
const char* stateName(const Status& value, uint32_t now) {
  if (value.subscribed) {
    if (value.sample.contact == 0) return "no_contact";
    if (value.sample.valid(value.connected, now)) return "streaming";
    return value.sample.hasSample ? "stale" : "waiting";
  }
  switch (value.state) {
    case LinkState::Starting: return "starting";
    case LinkState::Scanning: return "scanning";
    case LinkState::Connecting: return "connecting";
    case LinkState::Subscribing: return "subscribing";
    case LinkState::Waiting: return "waiting";
    case LinkState::Streaming: return "streaming";
    case LinkState::Retry: return "retrying";
    default: return "error";
  }
}
class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* device) override {
    if (!device->isAdvertisingService(NimBLEUUID(uint16_t(0x180d)))) return;
    portENTER_CRITICAL(&lock);
    ++status.heartAdvertisements;
    status.lastAdvertisementAt = millis();
    portEXIT_CRITICAL(&lock);
    if (device->getName() != kWatchName && device->getAddress().toString() != kWatchAddress) return;
    Candidate found;
    snprintf(found.address, sizeof(found.address), "%s", device->getAddress().toString().c_str());
    found.type = device->getAddressType();
    found.rssi = device->getRSSI();
    found.pending = true;
    portENTER_CRITICAL(&lock);
    if (!status.connected && !candidate.pending) candidate = found;
    portEXIT_CRITICAL(&lock);
  }
} scanCallbacks;

class ClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient*, int reason) override {
    portENTER_CRITICAL(&lock);
    status.connected = false;
    status.subscribed = false;
    status.state = LinkState::Retry;
    status.disconnectReason = reason;
    ++status.disconnects;
    portEXIT_CRITICAL(&lock);
  }
} clientCallbacks;

void notify(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  const uint32_t now = millis();
  portENTER_CRITICAL(&lock);
  status.sample.ingest(data, length, now);
  portEXIT_CRITICAL(&lock);
}

void worker(void*) {
  if (!NimBLEDevice::init("FIREFLY-SENSE")) { setState(LinkState::Error); vTaskDelete(nullptr); return; }
  if (!phoneBleSetup()) Serial.println("[ble-phone] setup failed; watch reception remains available.");
  auto scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, true);
  scan->setActiveScan(true);
  scan->setInterval(160);
  scan->setWindow(40);
  scan->setMaxResults(0);
  auto client = NimBLEDevice::createClient();
  if (!client) { setState(LinkState::Error); vTaskDelete(nullptr); return; }
  client->setClientCallbacks(&clientCallbacks, false);
  client->setConnectTimeout(5000);
  client->setConnectRetries(0);
  // Interval 30-50 ms; supervision timeout 6 s.
  client->setConnectionParams(24, 40, 0, 600);
  uint32_t nextScan = 0;
  uint32_t subscribedAt = 0;
  for (;;) {
    const uint32_t now = millis();
    Status value = snapshot();
    if (client->isConnected() && value.subscribed) {
      const uint32_t lastValid = value.sample.hasSample ? value.sample.sampledAt : subscribedAt;
      if (uint32_t(now - lastValid) > 30000) client->disconnect();
    } else if (!client->isConnected()) {
      // Reconcile the transport even if disconnect races with GATT discovery.
      portENTER_CRITICAL(&lock);
      status.connected = false;
      status.subscribed = false;
      portEXIT_CRITICAL(&lock);
      Candidate target;
      portENTER_CRITICAL(&lock);
      target = candidate;
      candidate.pending = false;
      portEXIT_CRITICAL(&lock);
      if (target.pending) {
        scan->stop();
        portENTER_CRITICAL(&lock);
        status.state = LinkState::Connecting;
        status.connected = false;
        status.subscribed = false;
        // A new connection must obtain a new measurement before becoming valid.
        status.sample.hasSample = false;
        status.sample.contact = -1;
        snprintf(status.address, sizeof(status.address), "%s", target.address);
        status.rssi = target.rssi;
        ++status.attempts;
        portEXIT_CRITICAL(&lock);
        if (client->connect(NimBLEAddress(target.address, target.type))) {
          portENTER_CRITICAL(&lock);
          status.connected = true;
          status.state = LinkState::Subscribing;
          portEXIT_CRITICAL(&lock);
          auto service = client->getService(NimBLEUUID(uint16_t(0x180d)));
          auto measurement = service ? service->getCharacteristic(NimBLEUUID(uint16_t(0x2a37))) : nullptr;
          if (measurement && measurement->canNotify() && measurement->subscribe(true, notify)) {
            subscribedAt = millis();
            portENTER_CRITICAL(&lock);
            if (status.connected) {
              status.subscribed = true;
              status.state = LinkState::Waiting;
            }
            portEXIT_CRITICAL(&lock);
          } else {
            client->disconnect();
            setState(LinkState::Retry);
          }
        } else {
          portENTER_CRITICAL(&lock);
          status.disconnectReason = client->getLastError();
          status.state = LinkState::Retry;
          portEXIT_CRITICAL(&lock);
        }
        portENTER_CRITICAL(&lock);
        candidate.pending = false;
        portEXIT_CRITICAL(&lock);
        nextScan = millis() + 3000;
      } else if (int32_t(now - nextScan) >= 0 && !scan->isScanning()) {
        setState(LinkState::Scanning);
        if (scan->start(5000, false, true)) {
          portENTER_CRITICAL(&lock);
          ++status.scanRounds;
          portEXIT_CRITICAL(&lock);
        }
        nextScan = now + 8000;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
}

void heartRateSetup() {
  // Connection and GATT operations can wait; keep them off the sensor/HTTP loop.
  if (xTaskCreate(worker, "heart-rate", 6144, nullptr, 1, nullptr) != pdPASS) setState(LinkState::Error);
}

void heartRateTick() {
  static uint32_t nextReport = 0;
  static uint32_t cycle = 0;
  const uint32_t now = millis();
  if (int32_t(now - nextReport) < 0) return;
  nextReport = now + 2000;
  const Status value = snapshot();
  const bool valid = value.sample.valid(value.connected && value.subscribed, now);
  String line = "CSV," + String(now) + ',' + String(cycle++) + ",6,CH6 BLE-HR," + (valid ? "1" : "0");
  // Keep the existing 36 field positions, then append schema-5 heart fields.
  for (size_t i = 5; i < 28; ++i) line += ",NA";
  line += ',';
  line += value.sample.hasSample ? String(now - value.sample.sampledAt) : String("NA");
  line += ',' + String(value.sample.sequence) + ",0," + kFirmwareVersion + ',' + String(kSensorSchemaVersion);
  line += ','; line += kSchedulerName;
  line += ",NA," + String(wifiRssiDbm()) + ',';
  line += valid ? String(value.sample.bpm) : String("NA");
  line += value.connected && value.subscribed ? ",1," : ",0,";
  line += value.sample.contact < 0 ? String("NA") : String(value.sample.contact);
  line += ','; line += stateName(value, now);
  Serial.println(line);
  wifiSendLine(line);
  webPanelAddRow(line);
  ringLogLine(line);
}

HeartRateDisplayValues heartRateDisplayValues() {
  const uint32_t now = millis();
  const Status value = snapshot();
  HeartRateDisplayValues out;
  out.connected = value.connected && value.subscribed;
  out.valid = value.sample.valid(out.connected, now);
  out.hasSample = value.sample.hasSample;
  out.bpm = out.valid ? value.sample.bpm : 0;
  out.contact = value.sample.contact;
  out.ageMs = out.hasSample ? uint32_t(now - value.sample.sampledAt) : 0;
  return out;
}

String heartRateJson() {
  const uint32_t now = millis();
  const Status value = snapshot();
  const bool valid = value.sample.valid(value.connected && value.subscribed, now);
  String out = "{\"target_name\":\""; out += kWatchName;
  out += "\",\"address\":\""; out += value.address;
  out += "\",\"state\":\""; out += stateName(value, now);
  out += "\",\"connected\":"; out += value.connected ? "true" : "false";
  out += ",\"subscribed\":"; out += value.subscribed ? "true" : "false";
  out += ",\"heart_rate_bpm\":"; out += valid ? String(value.sample.bpm) : String("null");
  out += ",\"sample_age_ms\":"; out += value.sample.hasSample ? String(now - value.sample.sampledAt) : String("null");
  out += ",\"sample_seq\":" + String(value.sample.sequence);
  out += ",\"sensor_contact\":"; out += value.sample.contact < 0 ? String("null") : String(value.sample.contact);
  out += ",\"notifications\":" + String(value.sample.notifications) + ",\"invalid_packets\":" + String(value.sample.invalidPackets);
  out += ",\"connection_attempts\":" + String(value.attempts) + ",\"disconnects\":" + String(value.disconnects);
  out += ",\"disconnect_reason\":" + String(value.disconnectReason) + ",\"scan_rounds\":" + String(value.scanRounds);
  out += ",\"heart_advertisements\":" + String(value.heartAdvertisements) + ",\"last_advertisement_age_ms\":";
  out += value.heartAdvertisements ? String(now - value.lastAdvertisementAt) : String("null");
  out += ",\"rssi_dbm\":"; out += value.rssi;
  out += ",\"indicator\":"; out += ledHeartRateJson();
  out += '}';
  return out;
}
