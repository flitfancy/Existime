#include "phone_ble.h"
#include "phone_ble_protocol.h"
#include "web_panel.h"
#include <NimBLEDevice.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <new>

namespace {
struct State {
  PhoneBle::Session session;
  bool ready = false;
  bool subscriptionWanted = false;
  bool secure = false;
  bool txPending = false;
  int txStatus = -1;
  uint32_t completed = 0, failed = 0, rejected = 0;
  uint16_t lastRows = 0;
};
struct Bundle {
  PhoneBle::Job job;
  char* bytes = nullptr;
  size_t offsets[PhoneBle::kMaxRows] = {};
  size_t rows = 0;
  uint32_t uptime = 0;
  ~Bundle() { heap_caps_free(bytes); }
};
State state;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t jobs = nullptr;
SemaphoreHandle_t txDone = nullptr;
NimBLEServer* server = nullptr;
NimBLECharacteristic* data = nullptr;
char bootId[33] = {};

bool current(const PhoneBle::Job& job) {
  portENTER_CRITICAL(&lock); const bool result = state.session.current(job); portEXIT_CRITICAL(&lock); return result;
}
void finish(const PhoneBle::Job& job, bool ok, size_t rows) {
  portENTER_CRITICAL(&lock);
  const bool samePeer = state.session.current(job);
  if (state.session.finish(job)) {
    state.txPending = false;
    if (ok) { ++state.completed; state.lastRows = rows; } else ++state.failed;
  }
  portEXIT_CRITICAL(&lock);
  // A cancelled old job cannot disconnect a new phone which reused its connection handle.
  if (!ok && samePeer) server->disconnect(job.peer);
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* source, NimBLEConnInfo& info) override {
    if (!info.isSlave()) return;
    portENTER_CRITICAL(&lock);
    const bool accepted = state.session.connect(info.getConnHandle());
    if (accepted) { state.secure = false; state.subscriptionWanted = false; state.txPending = false; }
    else ++state.rejected;
    portEXIT_CRITICAL(&lock);
    if (!accepted) source->disconnect(info.getConnHandle());
    else {
      source->updateConnParams(info.getConnHandle(), 12, 24, 0, 600);
      source->setDataLen(info.getConnHandle(), 251);
    }
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int) override {
    if (!info.isSlave()) return;
    portENTER_CRITICAL(&lock);
    if (state.session.peer == info.getConnHandle()) {
      state.session.disconnect(info.getConnHandle());
      state.secure = state.subscriptionWanted = state.txPending = false;
    }
    portEXIT_CRITICAL(&lock);
    if (txDone) xSemaphoreGive(txDone);
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    if (!info.isSlave()) return;
    portENTER_CRITICAL(&lock);
    if (state.session.peer == info.getConnHandle()) {
      state.secure = info.isEncrypted() && info.isBonded();
      state.session.subscribe(info.getConnHandle(), state.subscriptionWanted && state.secure, info.isEncrypted(), info.isBonded());
    }
    portEXIT_CRITICAL(&lock);
  }
} serverCallbacks;

class DataCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& info, uint16_t value) override {
    if (!info.isSlave()) return;
    portENTER_CRITICAL(&lock);
    if (state.session.peer == info.getConnHandle()) {
      state.subscriptionWanted = (value & 1) != 0;
      state.secure = info.isEncrypted() && info.isBonded();
      // NimBLE may start encryption when a bonded peer restores its CCCD. Wait for
      // authentication completion rather than disconnecting during that handshake.
      state.session.subscribe(info.getConnHandle(), state.subscriptionWanted && state.secure, info.isEncrypted(), info.isBonded());
    }
    portEXIT_CRITICAL(&lock);
  }
  void onStatus(NimBLECharacteristic*, int code) override {
    // NimBLE 2.5.0's newer overload receives an unpopulated ConnInfo for NOTIFY_TX.
    // Only this characteristic sends, and exactly one phone notification is in flight.
    portENTER_CRITICAL(&lock);
    const bool pending = state.txPending;
    if (pending) { state.txStatus = code; state.txPending = false; }
    portEXIT_CRITICAL(&lock);
    if (pending && txDone) xSemaphoreGive(txDone);
  }
} dataCallbacks;

class ControlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) override {
    const auto value = characteristic->getValue();
    uint32_t id = 0;
    const bool valid = PhoneBle::command(value.data(), value.length(), id);
    portENTER_CRITICAL(&lock);
    const bool accepted = valid && info.isSlave() && state.session.request(info.getConnHandle(), id, info.isEncrypted(), info.isBonded());
    if (!accepted) ++state.rejected;
    portEXIT_CRITICAL(&lock);
    if (!accepted && info.isSlave()) server->disconnect(info.getConnHandle());
  }
} controlCallbacks;

bool sendRecord(const std::string& record, const PhoneBle::Job& job, uint32_t started, uint32_t budget) {
  if (!current(job)) return false;
  PhoneBle::Fragments cursor(record, server->getPeerMTU(job.peer));
  if (!cursor.valid()) return false;
  uint32_t retryStarted = millis();
  while (!cursor.complete()) {
    if (!current(job) || uint32_t(millis() - started) >= budget) return false;
    const auto peer = server->getPeerInfoByHandle(job.peer);
    if (!peer.isEncrypted() || !peer.isBonded()) return false;
    while (xSemaphoreTake(txDone, 0) == pdTRUE) {}
    portENTER_CRITICAL(&lock); state.txStatus = -1; state.txPending = true; portEXIT_CRITICAL(&lock);
    if (!data->notify(reinterpret_cast<const uint8_t*>(record.data()) + cursor.offset(), cursor.length(), job.peer)) {
      portENTER_CRITICAL(&lock);
      state.txPending = false; const int status = state.txStatus;
      portEXIT_CRITICAL(&lock);
      // NOTIFY_TX is an enqueue result, not a peer acknowledgement. A full mbuf/
      // controller queue must retry the same bytes without advancing the cursor.
      const bool congestion = status == -1 || status == BLE_HS_ENOMEM || status == BLE_HS_EBUSY || status == BLE_HS_EAGAIN;
      if (!congestion || uint32_t(millis() - retryStarted) >= 1000) return false;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (xSemaphoreTake(txDone, pdMS_TO_TICKS(2000)) != pdTRUE) return false;
    portENTER_CRITICAL(&lock); const int result = state.txStatus; portEXIT_CRITICAL(&lock);
    if (result != 0 || !current(job)) return false;
    cursor.sent();
    retryStarted = millis();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  return true;
}
void sender(void*) {
  for (;;) {
    Bundle* bundle = nullptr;
    if (xQueueReceive(jobs, &bundle, portMAX_DELAY) != pdTRUE || !bundle) continue;
    const auto job = bundle->job;
    const uint32_t started = millis();
    const uint32_t budget = server->getPeerMTU(job.peer) <= 23 ? 55000 : 25000;
    bool ok = sendRecord(PhoneBle::begin(job.request, bundle->uptime, bootId, bundle->rows), job, started, budget);
    for (size_t i = 0; ok && i < bundle->rows; ++i) {
      const char* csv = bundle->bytes + bundle->offsets[i];
      ok = sendRecord(PhoneBle::row(job.request, csv, strlen(csv)), job, started, budget);
    }
    // No end record is sent after an error, timeout, unsubscribe or disconnect.
    if (ok) ok = sendRecord(PhoneBle::end(job.request, bundle->rows), job, started, budget);
    finish(job, ok, bundle->rows);
    delete bundle;
  }
}
} // namespace

bool phoneBleSetup() {
#ifdef BENCH_SDA
  return false; // Never advertise a bench pin-map image as the production N16R8 board.
#else
  if (!NimBLEDevice::isInitialized()) return false;
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setSecurityAuth(true, false, true); // Bonding + Secure Connections, Just Works on this displayless board.
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  jobs = xQueueCreate(1, sizeof(Bundle*));
  txDone = xSemaphoreCreateBinary();
  if (!jobs || !txDone) return false;
  server = NimBLEDevice::createServer();
  if (!server) return false;
  server->setCallbacks(&serverCallbacks, false);
  server->advertiseOnDisconnect(false); // Main loop restarts only when no accepted phone remains.
  auto service = server->createService(PhoneBle::kServiceUuid);
  if (!service) return false;
  auto info = service->createCharacteristic(PhoneBle::kInfoUuid, NIMBLE_PROPERTY::READ, 512);
  data = service->createCharacteristic(PhoneBle::kDataUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY, 512);
  auto control = service->createCharacteristic(PhoneBle::kControlUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC, 5);
  if (!info || !data || !control) return false;
  char boardId[32]; snprintf(boardId, sizeof(boardId), "ffs-%012llx", static_cast<unsigned long long>(ESP.getEfuseMac()));
  const auto identity = PhoneBle::info(boardId); info->setValue(identity);
  snprintf(bootId, sizeof(bootId), "%08lx%08lx%08lx%08lx", static_cast<unsigned long>(esp_random()),
      static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
  data->setCallbacks(&dataCallbacks); control->setCallbacks(&controlCallbacks);
  if (!server->start()) return false;
  auto advertising = NimBLEDevice::getAdvertising();
  if (!advertising->addServiceUUID(PhoneBle::kServiceUuid)) return false;
  advertising->enableScanResponse(true);
  advertising->setName("FIREFLY-SENSE");
  advertising->setPreferredParams(12, 24);
  if (xTaskCreate(sender, "phone-ble-send", 6144, nullptr, 1, nullptr) != pdPASS) return false;
  portENTER_CRITICAL(&lock); state.ready = true; portEXIT_CRITICAL(&lock);
  Serial.println("[ble-phone] protocol 1 ready; encrypted phone snapshots enabled.");
  return true;
#endif
}

bool phoneBleReady() { portENTER_CRITICAL(&lock); const bool ready = state.ready; portEXIT_CRITICAL(&lock); return ready; }
void phoneBleTick() {
  if (!phoneBleReady()) return;
  portENTER_CRITICAL(&lock);
  const bool advertise = state.session.peer == PhoneBle::kNoPeer;
  PhoneBle::Job job;
  const bool pending = state.session.take(job);
  portEXIT_CRITICAL(&lock);
  static uint32_t nextAdvertisingAttempt = 0;
  if (advertise && int32_t(millis() - nextAdvertisingAttempt) >= 0) {
    auto advertising = NimBLEDevice::getAdvertising();
    if (!advertising->isAdvertising()) advertising->start();
    nextAdvertisingAttempt = millis() + 1000;
  }
  if (!pending) return;
  Bundle* bundle = new (std::nothrow) Bundle;
  if (!bundle) { finish(job, false, 0); return; }
  bundle->job = job;
  const size_t bytes = std::max<size_t>(1, webPanelSnapshotBytes());
  if (bytes <= PhoneBle::kMaxRows * (PhoneBle::kMaxCsvBytes + 1)) {
    bundle->bytes = static_cast<char*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    // Preserve headroom for sensors, Wi-Fi and the BLE host when PSRAM is unavailable.
    if (!bundle->bytes && heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) > bytes + 65536)
      bundle->bytes = static_cast<char*>(heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (!bundle->bytes || !webPanelCopyRows(bundle->bytes, bytes, bundle->offsets, PhoneBle::kMaxRows, bundle->rows, bundle->uptime)
      || !current(job) || xQueueSend(jobs, &bundle, 0) != pdTRUE) {
    finish(job, false, 0); delete bundle;
  }
}

String phoneBleJson() {
  portENTER_CRITICAL(&lock); const State value = state; portEXIT_CRITICAL(&lock);
  String out = "{\"protocol\":1,\"ready\":"; out += value.ready ? "true" : "false";
  out += ",\"connected\":"; out += value.session.peer != PhoneBle::kNoPeer ? "true" : "false";
  out += ",\"encrypted_bond\":"; out += value.secure ? "true" : "false";
  out += ",\"subscribed\":"; out += value.session.subscribed ? "true" : "false";
  out += ",\"busy\":"; out += value.session.busy ? "true" : "false";
  out += ",\"completed\":" + String(value.completed) + ",\"failed\":" + String(value.failed);
  out += ",\"rejected\":" + String(value.rejected) + ",\"last_rows\":" + String(value.lastRows) + '}';
  return out;
}
