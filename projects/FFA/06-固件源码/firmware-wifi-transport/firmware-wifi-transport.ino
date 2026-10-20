#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "mbedtls/sha256.h"
#include "firmware_version.h"
#if __has_include("wifi_credentials.local.h")
#include "wifi_credentials.local.h"
#else
#include "wifi_credentials.example.h"
#endif


constexpr gpio_num_t PDM_CLK = GPIO_NUM_4;
constexpr gpio_num_t MIC_LEFT_DATA = GPIO_NUM_5;
constexpr gpio_num_t MIC_RIGHT_DATA = GPIO_NUM_1;
constexpr gpio_num_t AMP_ENABLE = GPIO_NUM_38;
constexpr gpio_num_t AMP_DIN = GPIO_NUM_21;
constexpr gpio_num_t AMP_BCLK = GPIO_NUM_47;
constexpr gpio_num_t AMP_LRCLK = GPIO_NUM_48;
constexpr gpio_num_t USER_BUTTON = GPIO_NUM_14;
constexpr gpio_num_t LED_BLUE = GPIO_NUM_39;
constexpr gpio_num_t LED_RED = GPIO_NUM_40;
constexpr gpio_num_t LED_GREEN = GPIO_NUM_41;
constexpr uint32_t MIC_RATE = 24000;
constexpr uint32_t DEFAULT_PLAY_RATE = 32000;
constexpr uint32_t MIN_PLAY_RATE = 32000;
constexpr uint32_t MAX_PLAY_RATE = 48000;
constexpr uint16_t MAX_BLOCK_SAMPLES = 960;
constexpr uint32_t RING_SECONDS = 12;
constexpr uint32_t PREBUFFER_SECONDS = 3;
constexpr uint32_t LIVE_RING_SECONDS = 2;
constexpr uint32_t LIVE_PREBUFFER_MS = 160;
constexpr uint32_t MAX_STREAM_SECONDS = 60 * 30;
constexpr uint32_t INPUT_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_SESSION_TIMEOUT_MS = 15000;
constexpr uint16_t WIFI_AUDIO_PORT = 7866;
constexpr uint16_t WIFI_MIC_PORT = 7867;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 35;
constexpr uint32_t BUTTON_DOUBLE_MS = 360;
constexpr uint32_t BUTTON_LONG_MS = 3000;
constexpr uint32_t LED_PWM_HZ = 5000;
constexpr uint32_t V2_MAGIC = 0x50564646;
constexpr uint8_t V2_VERSION = 1;
constexpr size_t V2_MAX_PAYLOAD_BYTES = 8192;
constexpr uint32_t V2_AUTH_TIMEOUT_MS = 3000;
constexpr uint32_t V2_STATUS_INTERVAL_MS = 1000;

enum MicMode : uint8_t { MIC_LEFT = 1, MIC_RIGHT = 2, MIC_BOTH = 3 };
enum ButtonEvent : uint8_t {
  BUTTON_SINGLE_CLICK = 1,
  BUTTON_DOUBLE_CLICK = 2,
  BUTTON_LIGHTS_CHANGED = 3,
};

enum V2FrameType : uint8_t {
  V2_AUTH = 1,
  V2_AUTH_RESULT = 2,
  V2_PING = 3,
  V2_PONG = 4,
  V2_PLAY_START = 10,
  V2_AUDIO_DATA = 11,
  V2_PLAY_END = 12,
  V2_PAUSE = 13,
  V2_RESUME = 14,
  V2_STOP = 15,
  V2_SET_VOLUME = 16,
  V2_REBOOT = 17,
  V2_MIC_CONFIG = 18,
  V2_DEVICE_STATUS = 20,
  V2_BUFFER_STATUS = 21,
  V2_PLAY_PROGRESS = 22,
  V2_ERROR = 23,
  V2_BUTTON = 24,
  V2_FIRMWARE_QUERY = 30,
  V2_FIRMWARE_INFO = 31,
  V2_OTA_BEGIN = 32,
  V2_OTA_DATA = 33,
  V2_OTA_END = 34,
  V2_OTA_ABORT = 35,
  V2_OTA_STATUS = 36,
  V2_OTA_CONFIRM = 37,
};

struct __attribute__((packed)) V2FrameHeader {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint16_t flags;
  uint32_t sequence;
  uint32_t payloadLength;
  uint32_t checksum;
};

static_assert(sizeof(V2FrameHeader) == 20, "FFVP header must be 20 bytes");

struct __attribute__((packed)) V2DeviceStatusPayload {
  uint32_t uptimeMs;
  int32_t rssiDbm;
  uint32_t ipAddress;
  uint8_t wifiConnected;
  uint8_t authenticated;
  uint16_t reserved;
};

struct __attribute__((packed)) V2PlayStartPayload {
  uint32_t rate;
  uint8_t channels;
  uint8_t sampleBytes;
  uint16_t prebufferMs;
  uint32_t totalFrames;
};

struct __attribute__((packed)) V2BufferStatusPayload {
  uint32_t acceptedFrames;
  uint32_t playedFrames;
  uint32_t bufferedFrames;
  uint32_t capacityFrames;
};

struct __attribute__((packed)) V2ErrorPayload {
  uint8_t code;
  uint8_t context;
  uint16_t reserved;
  uint32_t value;
  uint32_t detail;
};

struct V2OutboundFrame {
  uint8_t type;
  uint8_t payloadLength;
  uint16_t flags;
  uint8_t payload[16];
};

struct MicCommand {
  uint32_t address;
  uint16_t port;
  uint8_t enabled;
  uint8_t gain;
};

static_assert(sizeof(V2PlayStartPayload) == 12, "PLAY_START payload must be 12 bytes");
static_assert(sizeof(V2BufferStatusPayload) == 16, "BUFFER_STATUS payload must be 16 bytes");
static_assert(sizeof(V2ErrorPayload) == 12, "ERROR payload must be 12 bytes");

struct __attribute__((packed)) AudioHeader {
  uint32_t magic;
  uint32_t sequence;
  uint16_t bytes;
  uint16_t flags;
  uint32_t crc;
};

uint32_t streamTotalFrames = 0;
int16_t *ringBuffer = nullptr;
uint32_t ringCapacity = 0;
volatile uint32_t ringRead = 0;
volatile uint32_t ringWrite = 0;
volatile uint32_t ringCount = 0;
volatile uint32_t receivedFrames = 0;
volatile bool streamActive = false;
volatile bool liveStream = false;
volatile bool uploadComplete = false;
volatile bool abortRequested = false;
volatile bool pauseRequested = false;
volatile bool resumeRequested = false;
volatile bool playPaused = false;
volatile uint32_t playedFramesCurrent = 0;
i2s_chan_handle_t micRx = nullptr;
i2s_chan_handle_t speakerTx = nullptr;
bool micReady = false;
bool speakerReady = false;
uint32_t streamRate = DEFAULT_PLAY_RATE;
volatile uint8_t playVolume = 100;
volatile bool micStreaming = false;
MicMode micMode = MIC_BOTH;
uint8_t micGain = 4;
uint32_t micSequence = 0;
volatile uint32_t micOverflows = 0;
unsigned micWarmup = 0;
portMUX_TYPE ringMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t serialMutex = nullptr;
TaskHandle_t playbackTaskHandle = nullptr;
WiFiServer wifiServer(WIFI_AUDIO_PORT);
WiFiClient wifiClient;
WiFiUDP micUdp;
IPAddress micUdpTarget;
bool wifiConfigured = false;
bool wifiServerStarted = false;
bool mdnsStarted = false;
uint32_t lastWifiAttemptMs = 0;
bool wifiAuthenticated = false;
uint32_t wifiClientAcceptedMs = 0;
uint32_t lastWifiActivityMs = 0;
uint8_t v2HeaderBytes[sizeof(V2FrameHeader)] = {};
uint8_t v2Payload[V2_MAX_PAYLOAD_BYTES] = {};
size_t v2HeaderHave = 0;
size_t v2PayloadHave = 0;
V2FrameHeader v2Header = {};
uint32_t v2LastStatusMs = 0;
uint32_t v2LastBufferStatusMs = 0;
uint32_t v2TxSequence = 1;
uint32_t streamPrebufferFrames = 0;
QueueHandle_t v2OutboundQueue = nullptr;
QueueHandle_t micCommandQueue = nullptr;
uint16_t micUdpPort = WIFI_MIC_PORT;
volatile bool otaActive = false;
void abortOta();

void failStream(uint8_t code, uint32_t value, uint32_t detail);
uint32_t bufferedFrames();
bool pushFrames(const int16_t *samples, uint32_t count);
bool beginV2Playback(const V2PlayStartPayload &request, uint16_t flags);
Preferences uiPreferences;
bool lightsEnabled = true;
bool buttonRawPressed = false;
bool buttonStablePressed = false;
bool buttonLongHandled = false;
uint8_t buttonClickCount = 0;
uint8_t pendingButtonEvent = 0;
uint32_t buttonRawChangedMs = 0;
uint32_t buttonPressedMs = 0;
uint32_t buttonReleasedMs = 0;
volatile uint32_t ledErrorUntilMs = 0;
volatile float playbackEnvelope = 0.0f;

// Arduino otherwise confirms a new image before our hardware and network checks.
extern "C" bool verifyRollbackLater() { return true; }

uint32_t crcUpdate(uint32_t crc, const uint8_t *data, size_t length) {
  while (length--) {
    crc ^= *data++;
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return crc;
}

uint32_t crc32(const uint8_t *data, size_t length) {
  return crcUpdate(0xffffffff, data, length) ^ 0xffffffff;
}

bool IRAM_ATTR onMicOverflow(i2s_chan_handle_t, i2s_event_data_t *, void *) {
  micOverflows++;
  return false;
}

uint16_t micChannels() {
  return micMode == MIC_BOTH ? 2 : 1;
}

const char *micModeName() {
  return micMode == MIC_LEFT ? "left/U6" : micMode == MIC_RIGHT ? "right/U7" : "both/U6+U7";
}

i2s_pdm_rx_gpio_config_t micPins() {
  i2s_pdm_rx_gpio_config_t pins = {};
  pins.clk = PDM_CLK;
  for (auto &pin : pins.dins) pin = GPIO_NUM_NC;
  if (micMode == MIC_BOTH) {
    pins.dins[0] = MIC_LEFT_DATA;
    pins.dins[1] = MIC_RIGHT_DATA;
  } else {
    pins.dins[0] = micMode == MIC_LEFT ? MIC_LEFT_DATA : MIC_RIGHT_DATA;
  }
  return pins;
}

i2s_pdm_rx_slot_config_t micSlots() {
  i2s_pdm_rx_slot_config_t slots = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_16BIT, micMode == MIC_BOTH ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO);
  if (micMode == MIC_BOTH) {
    slots.slot_mask = static_cast<i2s_pdm_slot_mask_t>(
        I2S_PDM_RX_LINE0_SLOT_LEFT | I2S_PDM_RX_LINE1_SLOT_RIGHT);
  } else {
    slots.slot_mask = micMode == MIC_LEFT ? I2S_PDM_SLOT_LEFT : I2S_PDM_SLOT_RIGHT;
  }
  return slots;
}

void sendStatus() {
  if (serialMutex) xSemaphoreTake(serialMutex, portMAX_DELAY);
  Serial.printf("[ffa] READY mic=%s channels=%u mic_gain=%ux mic_rate=%u play_rate=%u volume=%u ring_seconds=%u prebuffer_seconds=%u full_duplex=1\n",
                micModeName(), micChannels(), micGain, MIC_RATE, streamRate, playVolume,
                RING_SECONDS, PREBUFFER_SECONDS);
  if (serialMutex) xSemaphoreGive(serialMutex);
}

bool reconfigureMic() {
  if (!micReady || micStreaming) return false;
  if (i2s_channel_disable(micRx) != ESP_OK) return false;
  auto pins = micPins();
  auto slots = micSlots();
  if (i2s_channel_reconfig_pdm_rx_gpio(micRx, &pins) != ESP_OK ||
      i2s_channel_reconfig_pdm_rx_slot(micRx, &slots) != ESP_OK ||
      i2s_channel_enable(micRx) != ESP_OK) return false;
  micWarmup = 50;
  sendStatus();
  return true;
}

bool setSpeakerRate(uint32_t rate) {
  if (!speakerReady) return false;
  if (i2s_channel_disable(speakerTx) != ESP_OK) return false;
  i2s_std_clk_config_t clock = I2S_STD_CLK_DEFAULT_CONFIG(rate);
  if (i2s_channel_reconfig_std_clock(speakerTx, &clock) != ESP_OK ||
      i2s_channel_enable(speakerTx) != ESP_OK) return false;
  streamRate = rate;
  return true;
}

uint32_t wifiAddressValue() {
  const IPAddress ip = WiFi.localIP();
  return (static_cast<uint32_t>(ip[0]) << 24) | (static_cast<uint32_t>(ip[1]) << 16) |
         (static_cast<uint32_t>(ip[2]) << 8) | static_cast<uint32_t>(ip[3]);
}

void printWifiDiagnostics() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  const String currentSsid = connected ? WiFi.SSID() : String();
  const String currentBssid = connected ? WiFi.BSSIDstr() : String();
  const String currentIp = connected ? WiFi.localIP().toString() : String();
  if (serialMutex) xSemaphoreTake(serialMutex, portMAX_DELAY);
  Serial.printf("[ffv-wifi] link connected=%u ssid=%s bssid=%s channel=%d rssi=%d ip=%s\n",
                connected ? 1 : 0, currentSsid.c_str(), currentBssid.c_str(),
                connected ? WiFi.channel() : 0, connected ? WiFi.RSSI() : 0,
                currentIp.c_str());
  if (serialMutex) xSemaphoreGive(serialMutex);

  const int count = WiFi.scanNetworks(false, true);
  if (serialMutex) xSemaphoreTake(serialMutex, portMAX_DELAY);
  Serial.printf("[ffv-wifi] scan count=%d target=%s\n", count, WIFI_SSID);
  for (int index = 0; index < count; ++index) {
    if (WiFi.SSID(index) != WIFI_SSID) continue;
    Serial.printf("[ffv-wifi] ap ssid=%s bssid=%s channel=%d rssi=%d\n",
                  WiFi.SSID(index).c_str(), WiFi.BSSIDstr(index).c_str(),
                  WiFi.channel(index), WiFi.RSSI(index));
  }
  Serial.println("[ffv-wifi] scan done");
  if (serialMutex) xSemaphoreGive(serialMutex);
  WiFi.scanDelete();
}

bool isKnownV2Type(uint8_t type) {
  switch (type) {
    case V2_AUTH:
    case V2_AUTH_RESULT:
    case V2_PING:
    case V2_PONG:
    case V2_PLAY_START:
    case V2_AUDIO_DATA:
    case V2_PLAY_END:
    case V2_PAUSE:
    case V2_RESUME:
    case V2_STOP:
    case V2_SET_VOLUME:
    case V2_REBOOT:
    case V2_MIC_CONFIG:
    case V2_DEVICE_STATUS:
    case V2_BUFFER_STATUS:
    case V2_PLAY_PROGRESS:
    case V2_ERROR:
    case V2_BUTTON:
    case V2_FIRMWARE_QUERY:
    case V2_OTA_BEGIN:
    case V2_OTA_DATA:
    case V2_OTA_END:
    case V2_OTA_ABORT:
    case V2_OTA_CONFIRM:
      return true;
    default:
      return false;
  }
}

uint32_t v2Checksum(const V2FrameHeader &header, const uint8_t *payload) {
  uint32_t checksum = crcUpdate(0xffffffff, reinterpret_cast<const uint8_t *>(&header), 16);
  if (header.payloadLength) checksum = crcUpdate(checksum, payload, header.payloadLength);
  return checksum ^ 0xffffffff;
}

bool writeWifiFully(const uint8_t *data, size_t length) {
  while (length && wifiClient.connected()) {
    const size_t written = wifiClient.write(data, length);
    if (!written) return false;
    data += written;
    length -= written;
  }
  return length == 0;
}

bool sendV2Frame(uint8_t type, uint32_t sequence, const void *payload = nullptr,
                 uint32_t payloadLength = 0, uint16_t flags = 0) {
  if (!wifiClient.connected() || payloadLength > V2_MAX_PAYLOAD_BYTES) return false;
  V2FrameHeader header = {V2_MAGIC, V2_VERSION, type, flags, sequence, payloadLength, 0};
  header.checksum = v2Checksum(header, reinterpret_cast<const uint8_t *>(payload));
  if (!writeWifiFully(reinterpret_cast<const uint8_t *>(&header), sizeof(header))) return false;
  return !payloadLength || writeWifiFully(reinterpret_cast<const uint8_t *>(payload), payloadLength);
}

bool queueV2Frame(uint8_t type, const void *payload = nullptr, uint8_t payloadLength = 0,
                  uint16_t flags = 0) {
  V2OutboundFrame frame = {type, payloadLength, flags, {}};
  if (!v2OutboundQueue || payloadLength > sizeof(frame.payload)) return false;
  if (payloadLength) memcpy(frame.payload, payload, payloadLength);
  return xQueueSend(v2OutboundQueue, &frame, pdMS_TO_TICKS(20)) == pdTRUE;
}

void queueV2BufferStatus() {
  const V2BufferStatusPayload status = {
      receivedFrames, playedFramesCurrent, bufferedFrames(), ringCapacity};
  queueV2Frame(V2_BUFFER_STATUS, &status, sizeof(status));
}

void queueV2Error(uint8_t code, uint8_t context, uint32_t value, uint32_t detail) {
  const V2ErrorPayload error = {code, context, 0, value, detail};
  queueV2Frame(V2_ERROR, &error, sizeof(error));
}

void resetV2Parser() {
  v2HeaderHave = 0;
  v2PayloadHave = 0;
  memset(&v2Header, 0, sizeof(v2Header));
}

void closeV2Session() {
  abortOta();
  if (streamActive) {
    abortRequested = true;
    if (playbackTaskHandle) xTaskNotifyGive(playbackTaskHandle);
  }
  micStreaming = false;
  wifiClient.stop();
  wifiAuthenticated = false;
  if (v2OutboundQueue) xQueueReset(v2OutboundQueue);
  resetV2Parser();
}

bool sendV2DeviceStatus(uint32_t sequence) {
  const bool connected = WiFi.status() == WL_CONNECTED;
  const V2DeviceStatusPayload status = {
      millis(), connected ? WiFi.RSSI() : 0, connected ? wifiAddressValue() : 0,
      static_cast<uint8_t>(connected), static_cast<uint8_t>(wifiAuthenticated), 0};
  return sendV2Frame(V2_DEVICE_STATUS, sequence, &status, sizeof(status));
}

bool sendV2Error(uint32_t sequence, uint8_t code, uint8_t context,
                 uint32_t value = 0, uint32_t detail = 0) {
  const V2ErrorPayload error = {code, context, 0, value, detail};
  return sendV2Frame(V2_ERROR, sequence, &error, sizeof(error));
}

#include "ota_transport.h"

bool handleV2Frame() {
  if (v2Checksum(v2Header, v2Payload) != v2Header.checksum) {
    return false;
  }

  lastWifiActivityMs = millis();
  if (!wifiAuthenticated) {
    if (v2Header.type != V2_AUTH || v2Header.payloadLength != sizeof(WIFI_PAIRING_KEY)) return false;
    const bool accepted = memcmp(v2Payload, WIFI_PAIRING_KEY, sizeof(WIFI_PAIRING_KEY)) == 0;
    memset(v2Payload, 0, sizeof(WIFI_PAIRING_KEY));
    const uint8_t result = accepted ? 0 : 1;
    if (!sendV2Frame(V2_AUTH_RESULT, v2Header.sequence, &result, sizeof(result))) return false;
    wifiAuthenticated = accepted;
    v2LastStatusMs = millis();
    if (accepted && pendingButtonEvent) {
      const uint8_t button[] = {pendingButtonEvent, static_cast<uint8_t>(lightsEnabled)};
      pendingButtonEvent = 0;
      queueV2Frame(V2_BUTTON, button, sizeof(button));
    }
    return accepted && sendFirmwareInfo(v2Header.sequence);
  }

  if (v2Header.type >= V2_FIRMWARE_QUERY && v2Header.type <= V2_OTA_CONFIRM)
    return handleOtaFrame();
  if (otaActive && v2Header.type != V2_PING)
    return replyOta(v2Header.sequence, 255, 1);
  switch (v2Header.type) {
    case V2_PING:
      return sendV2Frame(V2_PONG, v2Header.sequence, v2Payload, v2Header.payloadLength,
                         v2Header.flags);
    case V2_PLAY_START: {
      if (v2Header.payloadLength != sizeof(V2PlayStartPayload))
        return sendV2Error(v2Header.sequence, 1, v2Header.type,
                           v2Header.payloadLength, sizeof(V2PlayStartPayload));
      V2PlayStartPayload request = {};
      memcpy(&request, v2Payload, sizeof(request));
      if (!beginV2Playback(request, v2Header.flags))
        return sendV2Error(v2Header.sequence, 2, v2Header.type,
                           request.totalFrames, request.rate);
      const V2BufferStatusPayload status = {
          receivedFrames, playedFramesCurrent, bufferedFrames(), ringCapacity};
      v2LastBufferStatusMs = millis();
      return sendV2Frame(V2_BUFFER_STATUS, v2Header.sequence, &status, sizeof(status));
    }
    case V2_AUDIO_DATA: {
      if (!streamActive || uploadComplete || abortRequested ||
          !v2Header.payloadLength || (v2Header.payloadLength & 1)) {
        return sendV2Error(v2Header.sequence, 3, v2Header.type,
                           v2Header.payloadLength, receivedFrames);
      }
      const uint32_t frames = v2Header.payloadLength / sizeof(int16_t);
      if ((!liveStream && receivedFrames + frames > streamTotalFrames) || frames > ringCapacity)
        return sendV2Error(v2Header.sequence, 4, v2Header.type,
                           receivedFrames + frames, streamTotalFrames);
      while (wifiClient.connected() && streamActive && !abortRequested &&
             ringCapacity - bufferedFrames() < frames && millis() - lastWifiActivityMs < WIFI_SESSION_TIMEOUT_MS)
        vTaskDelay(pdMS_TO_TICKS(1));
      if (!wifiClient.connected() || !streamActive || abortRequested ||
          millis() - lastWifiActivityMs >= WIFI_SESSION_TIMEOUT_MS) return false;
      if (!pushFrames(reinterpret_cast<const int16_t *>(v2Payload), frames))
        return sendV2Error(v2Header.sequence, 5, v2Header.type, frames, bufferedFrames());
      receivedFrames += frames;
      return true;
    }
    case V2_PLAY_END:
      if (!streamActive || uploadComplete || abortRequested)
        return sendV2Error(v2Header.sequence, 6, v2Header.type);
      if (!liveStream && receivedFrames != streamTotalFrames)
        return sendV2Error(v2Header.sequence, 7, v2Header.type,
                           receivedFrames, streamTotalFrames);
      if (liveStream) streamTotalFrames = receivedFrames;
      uploadComplete = true;
      if (playbackTaskHandle) xTaskNotifyGive(playbackTaskHandle);
      return true;
    case V2_PAUSE:
      if (streamActive && !playPaused) {
        pauseRequested = true;
        if (playbackTaskHandle) xTaskNotifyGive(playbackTaskHandle);
      }
      return true;
    case V2_RESUME:
      if (streamActive && playPaused) {
        resumeRequested = true;
        if (playbackTaskHandle) xTaskNotifyGive(playbackTaskHandle);
      }
      return true;
    case V2_STOP:
      if (streamActive) {
        abortRequested = true;
        if (playbackTaskHandle) xTaskNotifyGive(playbackTaskHandle);
      } else {
        const V2BufferStatusPayload stopped = {};
        queueV2Frame(V2_PLAY_PROGRESS, &stopped, sizeof(stopped), 32);
      }
      return true;
    case V2_SET_VOLUME:
      if (v2Header.payloadLength != 1 || v2Payload[0] > 100)
        return sendV2Error(v2Header.sequence, 8, v2Header.type,
                           v2Header.payloadLength ? v2Payload[0] : 0, 100);
      playVolume = v2Payload[0];
      return true;
    case V2_REBOOT:
      sendV2DeviceStatus(v2Header.sequence);
      delay(50);
      ESP.restart();
      return true;
    case V2_MIC_CONFIG: {
      if (v2Header.payloadLength != 4 || v2Payload[0] > 1 ||
          (v2Payload[1] != 1 && v2Payload[1] != 2 && v2Payload[1] != 4 && v2Payload[1] != 8))
        return sendV2Error(v2Header.sequence, 10, v2Header.type);
      uint16_t port;
      memcpy(&port, v2Payload + 2, sizeof(port));
      if (!port) return sendV2Error(v2Header.sequence, 10, v2Header.type);
      const MicCommand command = {static_cast<uint32_t>(wifiClient.remoteIP()), port,
                                   v2Payload[0], v2Payload[1]};
      xQueueOverwrite(micCommandQueue, &command);
      return true;
    }
    default:
      return sendV2Error(v2Header.sequence, 9, v2Header.type);
  }
}

bool drainV2OutboundQueue() {
  if (!v2OutboundQueue) return true;
  V2OutboundFrame frame = {};
  while (xQueueReceive(v2OutboundQueue, &frame, 0) == pdTRUE) {
    if (!sendV2Frame(frame.type, v2TxSequence++, frame.payload, frame.payloadLength, frame.flags))
      return false;
  }
  return true;
}

bool receiveV2Frames() {
  size_t budget = 16384;
  while (wifiClient.connected() && wifiClient.available() && budget) {
    if (v2HeaderHave < sizeof(V2FrameHeader)) {
      const size_t available = static_cast<size_t>(wifiClient.available());
      const size_t take = min(min(sizeof(V2FrameHeader) - v2HeaderHave, available), budget);
      const int received = wifiClient.read(v2HeaderBytes + v2HeaderHave, take);
      if (received <= 0) return true;
      v2HeaderHave += static_cast<size_t>(received);
      budget -= static_cast<size_t>(received);
      if (v2HeaderHave != sizeof(V2FrameHeader)) continue;
      memcpy(&v2Header, v2HeaderBytes, sizeof(v2Header));
      if (v2Header.magic != V2_MAGIC || v2Header.version != V2_VERSION ||
          !isKnownV2Type(v2Header.type) || v2Header.payloadLength > V2_MAX_PAYLOAD_BYTES) {
        return false;
      }
      if (!v2Header.payloadLength) {
        if (!handleV2Frame()) return false;
        resetV2Parser();
      }
      continue;
    }

    const size_t available = static_cast<size_t>(wifiClient.available());
    const size_t take = min(min(static_cast<size_t>(v2Header.payloadLength) - v2PayloadHave,
                                available), budget);
    const int received = wifiClient.read(v2Payload + v2PayloadHave, take);
    if (received <= 0) return true;
    v2PayloadHave += static_cast<size_t>(received);
    budget -= static_cast<size_t>(received);
    if (v2PayloadHave == v2Header.payloadLength) {
      if (!handleV2Frame()) return false;
      resetV2Parser();
    }
  }
  return true;
}

void wifiNetworkTask(void *) {
  for (;;) {
    maintainOta();
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !wifiServerStarted) {
      wifiServer.begin();
      wifiServer.setNoDelay(true);
      wifiServerStarted = true;
    } else if (!connected && wifiServerStarted) {
      closeV2Session();
      wifiServer.end();
      wifiServerStarted = false;
    }

    if (wifiServerStarted && !wifiClient.connected()) {
      WiFiClient candidate = wifiServer.accept();
      if (candidate) {
        closeV2Session();
        wifiClient = candidate;
        wifiClient.setNoDelay(true);
        wifiClientAcceptedMs = millis();
        lastWifiActivityMs = millis();
      }
    }

    if (wifiClient.connected()) {
      if (!receiveV2Frames()) {
        closeV2Session();
      } else if (wifiAuthenticated && !drainV2OutboundQueue()) {
        closeV2Session();
      } else if (!wifiAuthenticated && millis() - wifiClientAcceptedMs > V2_AUTH_TIMEOUT_MS) {
        closeV2Session();
      } else if (wifiAuthenticated && millis() - lastWifiActivityMs > WIFI_SESSION_TIMEOUT_MS) {
        closeV2Session();
      } else if (wifiAuthenticated && streamActive &&
                 millis() - v2LastBufferStatusMs >= 250) {
        v2LastBufferStatusMs = millis();
        const V2BufferStatusPayload status = {
            receivedFrames, playedFramesCurrent, bufferedFrames(), ringCapacity};
        if (!sendV2Frame(V2_BUFFER_STATUS, v2TxSequence++, &status, sizeof(status))) closeV2Session();
      } else if (wifiAuthenticated && millis() - v2LastStatusMs >= V2_STATUS_INTERVAL_MS) {
        v2LastStatusMs = millis();
        if (!sendV2DeviceStatus(v2LastStatusMs)) closeV2Session();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(wifiClient.connected() ? 1 : 10));
  }
}

void beginWifi() {
  wifiConfigured = WIFI_SSID[0] != '\0';
  if (!wifiConfigured) return;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname("firefly-voice");
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttemptMs = millis();
}

void maintainWifi() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    if (!mdnsStarted && MDNS.begin("firefly-voice")) {
      MDNS.addService("ffv-audio", "tcp", WIFI_AUDIO_PORT);
      mdnsStarted = true;
    }
  } else {
    micUdp.stop();
    if (mdnsStarted) {
      MDNS.end();
      mdnsStarted = false;
    }
  }
  if (!connected && wifiConfigured && millis() - lastWifiAttemptMs > 15000) {
    WiFi.reconnect();
    lastWifiAttemptMs = millis();
  }
}

uint8_t breathe(uint32_t periodMs, uint8_t low, uint8_t high) {
  const float phase = static_cast<float>(millis() % periodMs) * 6.2831853f / periodMs;
  const float amount = 0.5f - 0.5f * cosf(phase);
  return low + static_cast<uint8_t>((high - low) * amount);
}

void writeLed(gpio_num_t pin, uint8_t brightness) {
  ledcWrite(static_cast<uint8_t>(pin), 255 - brightness);
}

void setLed(uint8_t red, uint8_t green, uint8_t blue) {
  if (!lightsEnabled) red = green = blue = 0;
  writeLed(LED_RED, red);
  writeLed(LED_GREEN, green);
  writeLed(LED_BLUE, blue);
}

void updateLed() {
  static uint32_t lastUpdateMs = 0;
  if (millis() - lastUpdateMs < 20) return;
  lastUpdateMs = millis();
  if (ledErrorUntilMs && static_cast<int32_t>(ledErrorUntilMs - millis()) > 0) {
    setLed((millis() / 180) % 2 ? 190 : 12, 0, 0);
    return;
  }
  if (streamActive) {
    if (playPaused) {
      const uint8_t value = breathe(1800, 8, 100);
      setLed(value, value / 3, 0);
    } else {
      const float shaped = sqrtf(constrain(playbackEnvelope, 0.0f, 1.0f));
      const uint8_t value = 10 + static_cast<uint8_t>(220.0f * shaped);
      setLed(0, value / 2, value);
    }
    return;
  }
  if (micStreaming) {
    const uint8_t value = breathe(2200, 6, 96);
    setLed(0, value, value / 5);
    return;
  }
  if (wifiConfigured && WiFi.status() != WL_CONNECTED) {
    const uint8_t value = (millis() / 180) % 2 ? 120 : 4;
    setLed(0, 0, value);
    return;
  }
  const uint8_t value = breathe(3600, 3, 38);
  setLed(0, value / 2, value);
}

void emitButtonEvent(ButtonEvent event) {
  const uint8_t payload[] = {static_cast<uint8_t>(event), static_cast<uint8_t>(lightsEnabled)};
  if (wifiAuthenticated) queueV2Frame(V2_BUTTON, payload, sizeof(payload));
  else pendingButtonEvent = static_cast<uint8_t>(event);
}

uint32_t bufferedFrames() {
  portENTER_CRITICAL(&ringMux);
  const uint32_t count = ringCount;
  portEXIT_CRITICAL(&ringMux);
  return count;
}

bool pushFrames(const int16_t *samples, uint32_t count) {
  portENTER_CRITICAL(&ringMux);
  if (ringCapacity - ringCount < count) {
    portEXIT_CRITICAL(&ringMux);
    return false;
  }
  const uint32_t first = min(count, ringCapacity - ringWrite);
  memcpy(ringBuffer + ringWrite, samples, first * sizeof(int16_t));
  if (first < count) memcpy(ringBuffer, samples + first, (count - first) * sizeof(int16_t));
  ringWrite = (ringWrite + count) % ringCapacity;
  ringCount += count;
  portEXIT_CRITICAL(&ringMux);
  if (playbackTaskHandle) xTaskNotifyGive(playbackTaskHandle);
  return true;
}

bool popFrames(int16_t *samples, uint32_t count) {
  portENTER_CRITICAL(&ringMux);
  if (ringCount < count) {
    portEXIT_CRITICAL(&ringMux);
    return false;
  }
  const uint32_t first = min(count, ringCapacity - ringRead);
  memcpy(samples, ringBuffer + ringRead, first * sizeof(int16_t));
  if (first < count) memcpy(samples + first, ringBuffer, (count - first) * sizeof(int16_t));
  ringRead = (ringRead + count) % ringCapacity;
  ringCount -= count;
  portEXIT_CRITICAL(&ringMux);
  return true;
}

void releaseStream() {
  digitalWrite(AMP_ENABLE, LOW);
  if (abortRequested) {
    const V2BufferStatusPayload stopped = {
        receivedFrames, playedFramesCurrent, bufferedFrames(), ringCapacity};
    queueV2Frame(V2_PLAY_PROGRESS, &stopped, sizeof(stopped), 32);
  }
  portENTER_CRITICAL(&ringMux);
  ringCapacity = ringRead = ringWrite = ringCount = 0;
  portEXIT_CRITICAL(&ringMux);
  receivedFrames = 0;
  uploadComplete = false;
  abortRequested = false;
  pauseRequested = resumeRequested = playPaused = false;
  playedFramesCurrent = 0;
  playbackEnvelope = 0.0f;
  liveStream = false;
  streamActive = false;
}

bool beginV2Playback(const V2PlayStartPayload &request, uint16_t flags) {
  const bool requestedLive = (flags & 1) != 0;
  const bool validFlags = (flags & ~static_cast<uint16_t>(1)) == 0;
  const bool validRate = request.rate == MIN_PLAY_RATE || request.rate == MAX_PLAY_RATE;
  const bool validFormat = request.channels == 1 && request.sampleBytes == sizeof(int16_t);
  const bool validLength = requestedLive ? request.totalFrames == 0
                                         : request.totalFrames > 0 &&
                                               request.totalFrames <= request.rate * MAX_STREAM_SECONDS;
  if (streamActive || !speakerReady || !psramFound() || !validFlags || !validRate ||
      !validFormat || !validLength || request.prebufferMs > 5000) return false;
  if (!setSpeakerRate(request.rate)) return false;

  const uint32_t capacity = requestedLive ? request.rate * LIVE_RING_SECONDS
                                          : min(request.rate * RING_SECONDS, request.totalFrames);
  if (!ringBuffer) return false;
  portENTER_CRITICAL(&ringMux);
  ringCapacity = capacity;
  ringRead = ringWrite = ringCount = 0;
  portEXIT_CRITICAL(&ringMux);
  streamTotalFrames = request.totalFrames;
  streamRate = request.rate;
  streamPrebufferFrames = min(request.rate * static_cast<uint32_t>(request.prebufferMs) / 1000,
                              capacity);
  liveStream = requestedLive;
  receivedFrames = 0;
  uploadComplete = false;
  abortRequested = false;
  pauseRequested = resumeRequested = playPaused = false;
  playedFramesCurrent = 0;
  playbackEnvelope = 0.0f;
  streamActive = true;
  if (playbackTaskHandle) xTaskNotifyGive(playbackTaskHandle);
  return true;
}

void failStream(uint8_t code, uint32_t value = 0, uint32_t detail = 0) {
  digitalWrite(AMP_ENABLE, LOW);
  queueV2Error(code, V2_AUDIO_DATA, value, detail);
  abortRequested = true;
  ledErrorUntilMs = millis() + 3000;
  if (playbackTaskHandle) xTaskNotifyGive(playbackTaskHandle);
}

bool writeStereo(const int16_t *samples, size_t bytes) {
  size_t written = 0;
  const esp_err_t result = i2s_channel_write(speakerTx, samples, bytes, &written, pdMS_TO_TICKS(2000));
  if (result != ESP_OK || written != bytes) {
    failStream(9, static_cast<uint32_t>(result), static_cast<uint32_t>(written));
    return false;
  }
  return true;
}

bool writeSilence(unsigned blocks) {
  int16_t zeros[512] = {};
  for (unsigned i = 0; i < blocks; ++i) if (!writeStereo(zeros, sizeof(zeros))) return false;
  return true;
}

void playbackTask(void *) {
  int16_t mono[256];
  int16_t stereo[512];
  for (;;) {
    if (!streamActive) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    if (abortRequested) {
      digitalWrite(AMP_ENABLE, LOW);
      releaseStream();
      continue;
    }

    if (pauseRequested && !playPaused) {
      pauseRequested = false;
      playPaused = true;
      const V2BufferStatusPayload status = {receivedFrames, 0, bufferedFrames(), ringCapacity};
      queueV2Frame(V2_PLAY_PROGRESS, &status, sizeof(status), 4);
    }
    if (resumeRequested && playPaused) {
      resumeRequested = false;
      playPaused = false;
      const V2BufferStatusPayload status = {receivedFrames, 0, bufferedFrames(), ringCapacity};
      queueV2Frame(V2_PLAY_PROGRESS, &status, sizeof(status), 8);
    }
    if (playPaused) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
      continue;
    }

    const uint32_t targetPrebuffer = streamPrebufferFrames;
    if (bufferedFrames() < targetPrebuffer && !uploadComplete) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
      continue;
    }

    digitalWrite(AMP_ENABLE, LOW);
    if (!writeSilence(liveStream ? 4 : 32)) continue;
    queueV2BufferStatus();
    digitalWrite(AMP_ENABLE, HIGH);
    delay(20);

    uint32_t playedFrames = 0;
    playedFramesCurrent = 0;
    uint32_t lastWriteMs = millis();
    uint32_t lastProgressMs = millis();
    const uint32_t startedMs = millis();
    while (streamActive && !abortRequested && (!uploadComplete || playedFrames < streamTotalFrames)) {
      if (pauseRequested && !playPaused) {
        pauseRequested = false;
        playPaused = true;
        digitalWrite(AMP_ENABLE, LOW);
        const V2BufferStatusPayload status = {
            receivedFrames, playedFrames, bufferedFrames(), ringCapacity};
        queueV2Frame(V2_PLAY_PROGRESS, &status, sizeof(status), 4);
      }
      if (resumeRequested && playPaused) {
        resumeRequested = false;
        if (!writeSilence(liveStream ? 4 : 32)) break;
        playPaused = false;
        const V2BufferStatusPayload status = {
            receivedFrames, playedFrames, bufferedFrames(), ringCapacity};
        queueV2Frame(V2_PLAY_PROGRESS, &status, sizeof(status), 8);
        digitalWrite(AMP_ENABLE, HIGH);
        delay(20);
        lastWriteMs = millis();
      }
      if (playPaused) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
        continue;
      }
      const uint32_t needed = uploadComplete
          ? min(static_cast<uint32_t>(256), streamTotalFrames - playedFrames)
          : static_cast<uint32_t>(256);
      if (!needed) break;
      if (!popFrames(mono, needed)) {
        if (millis() - lastWriteMs > 50) {
          digitalWrite(AMP_ENABLE, LOW);
          const V2BufferStatusPayload status = {
              receivedFrames, playedFrames, bufferedFrames(), ringCapacity};
          queueV2Frame(V2_PLAY_PROGRESS, &status, sizeof(status), 16);
          const uint32_t remaining = uploadComplete ? streamTotalFrames - playedFrames : ringCapacity;
          const uint32_t refillTarget = liveStream
              ? min(streamRate * LIVE_PREBUFFER_MS / 1000, remaining)
              : min(streamRate * PREBUFFER_SECONDS, remaining);
          while (streamActive && !abortRequested && bufferedFrames() < refillTarget) {
            if (liveStream && uploadComplete &&
                bufferedFrames() >= streamTotalFrames - playedFrames) break;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
          }
          if (abortRequested || !streamActive) break;
          if (!writeSilence(liveStream ? 4 : 32)) break;
          digitalWrite(AMP_ENABLE, HIGH);
          delay(20);
          lastWriteMs = millis();
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
        continue;
      }
      uint32_t absoluteSum = 0;
      for (uint32_t i = 0; i < needed; ++i) {
        absoluteSum += abs(static_cast<int32_t>(mono[i]));
      }
      const float currentLevel = constrain(
          static_cast<float>(absoluteSum) / static_cast<float>(needed) / 10000.0f,
          0.0f, 1.0f);
      const float response = currentLevel > playbackEnvelope ? 0.10f : 0.025f;
      playbackEnvelope += (currentLevel - playbackEnvelope) * response;
      static int32_t volumeQ15 = 32768;
      const int32_t targetQ15 = static_cast<int32_t>(playVolume) * 32768 / 100;
      const int32_t startQ15 = volumeQ15;
      for (uint32_t i = 0; i < needed; ++i) {
        const int32_t gainQ15 = startQ15 + (targetQ15 - startQ15) * static_cast<int32_t>(i + 1) /
                                               static_cast<int32_t>(needed);
        const int16_t value = static_cast<int16_t>((static_cast<int32_t>(mono[i]) * gainQ15) >> 15);
        stereo[2 * i] = value;
        stereo[2 * i + 1] = value;
      }
      volumeQ15 = targetQ15;
      if (!writeStereo(stereo, needed * 2 * sizeof(int16_t))) break;
      playedFrames += needed;
      playedFramesCurrent = playedFrames;
      lastWriteMs = millis();
      if (millis() - lastProgressMs >= 250) {
        const V2BufferStatusPayload status = {
            receivedFrames, playedFrames, bufferedFrames(), ringCapacity};
        queueV2Frame(V2_PLAY_PROGRESS, &status, sizeof(status));
        lastProgressMs = millis();
      }
    }

    if (abortRequested) {
      releaseStream();
      continue;
    }
    const uint32_t audioWriteMs = millis() - startedMs;
    if (!writeSilence(liveStream ? 4 : 64)) continue;
    digitalWrite(AMP_ENABLE, LOW);
    const V2BufferStatusPayload finalStatus = {
        receivedFrames, playedFrames, bufferedFrames(), ringCapacity};
    releaseStream();
    queueV2Frame(V2_PLAY_PROGRESS, &finalStatus, sizeof(finalStatus), 2);

  }
}

void pollUserButton() {
  const uint32_t now = millis();
  const bool pressed = digitalRead(USER_BUTTON) == LOW;
  if (pressed != buttonRawPressed) {
    buttonRawPressed = pressed;
    buttonRawChangedMs = now;
  }
  if (buttonStablePressed != buttonRawPressed && now - buttonRawChangedMs >= BUTTON_DEBOUNCE_MS) {
    buttonStablePressed = buttonRawPressed;
    if (buttonStablePressed) {
      buttonPressedMs = now;
      buttonLongHandled = false;
    } else if (!buttonLongHandled) {
      buttonClickCount++;
      buttonReleasedMs = now;
    }
  }
  if (buttonStablePressed && !buttonLongHandled && now - buttonPressedMs >= BUTTON_LONG_MS) {
    buttonLongHandled = true;
    buttonClickCount = 0;
    lightsEnabled = !lightsEnabled;
    uiPreferences.begin("ffv-ui", false);
    uiPreferences.putBool("lights", lightsEnabled);
    uiPreferences.end();
    emitButtonEvent(BUTTON_LIGHTS_CHANGED);
  }
  if (!buttonStablePressed && buttonClickCount >= 2) {
    buttonClickCount = 0;
    emitButtonEvent(BUTTON_DOUBLE_CLICK);
  } else if (!buttonStablePressed && buttonClickCount == 1 && now - buttonReleasedMs >= BUTTON_DOUBLE_MS) {
    buttonClickCount = 0;
    if (streamActive) {
      if (playPaused) resumeRequested = true;
      else pauseRequested = true;
    }
    emitButtonEvent(BUTTON_SINGLE_CLICK);
  }
}

void captureMicrophones() {
  MicCommand command = {};
  if (micCommandQueue && xQueueReceive(micCommandQueue, &command, 0) == pdTRUE) {
    micStreaming = command.enabled && wifiAuthenticated && !otaActive;
    micGain = command.gain;
    micUdpTarget = IPAddress(command.address);
    micUdpPort = command.port;
    micWarmup = 50;
    micSequence = 0;
  }
  if (!micReady) return;
  int16_t pcm[960];
  size_t bytes = 0;
  const size_t wanted = 240 * micChannels() * sizeof(int16_t);
  // Never let microphone DMA waiting stall the playback transport producer.
  const esp_err_t result = i2s_channel_read(micRx, pcm, wanted, &bytes, 0);
  if (result != ESP_OK || bytes != wanted) return;
  micHealthReads++;
  if (micWarmup) {
    micWarmup--;
    micOverflows = 0;
    return;
  }
  if (!micStreaming || otaActive) return;

  const size_t samples = bytes / sizeof(int16_t);
  for (size_t i = 0; i < samples; ++i) {
    const int32_t amplified = static_cast<int32_t>(pcm[i]) * micGain;
    pcm[i] = static_cast<int16_t>(constrain(amplified, -32768, 32767));
  }
  const uint32_t overflows = __atomic_exchange_n(&micOverflows, 0, __ATOMIC_RELAXED);
  const uint16_t flags = (overflows ? 1 : 0) | (micChannels() == 2 ? 2 : 0) |
                         (micMode == MIC_LEFT || micMode == MIC_BOTH ? 4 : 0) |
                         (micMode == MIC_RIGHT || micMode == MIC_BOTH ? 8 : 0);
  const AudioHeader header = {0x41564646, micSequence++, static_cast<uint16_t>(bytes), flags,
                              crc32(reinterpret_cast<const uint8_t *>(pcm), bytes)};
  const bool ok = micUdp.beginPacket(micUdpTarget, micUdpPort) == 1 &&
      micUdp.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) == sizeof(header) &&
      micUdp.write(reinterpret_cast<const uint8_t *>(pcm), bytes) == bytes &&
      micUdp.endPacket() == 1;
  if (!ok) micOverflows++;
}

void processUsbDiagnostics() {
  while (Serial.available()) {
    const int rawCommand = Serial.read();
    if (rawCommand == '?') sendStatus();
    else if (rawCommand == 'D') printWifiDiagnostics();
  }
}

void setup() {
  digitalWrite(AMP_ENABLE, LOW);
  pinMode(AMP_ENABLE, OUTPUT);
  pinMode(USER_BUTTON, INPUT_PULLUP);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);
  ledcAttach(LED_RED, LED_PWM_HZ, 8);
  ledcAttach(LED_GREEN, LED_PWM_HZ, 8);
  ledcAttach(LED_BLUE, LED_PWM_HZ, 8);
  uiPreferences.begin("ffv-ui", true);
  lightsEnabled = uiPreferences.getBool("lights", true);
  uiPreferences.end();
  setLed(0, 0, 0);
  Serial.begin(115200);
  delay(1500);

  serialMutex = xSemaphoreCreateMutex();
  v2OutboundQueue = xQueueCreate(32, sizeof(V2OutboundFrame));
  micCommandQueue = xQueueCreate(1, sizeof(MicCommand));
  ringBuffer = static_cast<int16_t *>(heap_caps_malloc(
      MAX_PLAY_RATE * RING_SECONDS * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  beginWifi();
  i2s_chan_config_t micChannel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  micChannel.dma_desc_num = 8;
  micChannel.dma_frame_num = 480;
  if (i2s_new_channel(&micChannel, nullptr, &micRx) == ESP_OK) {
    i2s_pdm_rx_config_t config = {};
    config.clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(MIC_RATE);
    config.slot_cfg = micSlots();
    config.gpio_cfg = micPins();
    i2s_event_callbacks_t callbacks = {};
    callbacks.on_recv_q_ovf = onMicOverflow;
    if (i2s_channel_init_pdm_rx_mode(micRx, &config) == ESP_OK &&
        i2s_channel_register_event_callback(micRx, &callbacks, nullptr) == ESP_OK &&
        i2s_channel_enable(micRx) == ESP_OK) micReady = true;
  }

  i2s_chan_config_t speakerChannel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  speakerChannel.dma_desc_num = 8;
  speakerChannel.dma_frame_num = 256;
  if (i2s_new_channel(&speakerChannel, &speakerTx, nullptr) == ESP_OK) {
    i2s_std_config_t config = {};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(DEFAULT_PLAY_RATE);
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                          I2S_SLOT_MODE_STEREO);
    config.gpio_cfg.mclk = GPIO_NUM_NC;
    config.gpio_cfg.bclk = AMP_BCLK;
    config.gpio_cfg.ws = AMP_LRCLK;
    config.gpio_cfg.dout = AMP_DIN;
    config.gpio_cfg.din = GPIO_NUM_NC;
    if (i2s_channel_init_std_mode(speakerTx, &config) == ESP_OK &&
        i2s_channel_enable(speakerTx) == ESP_OK) speakerReady = true;
  }

  xTaskCreatePinnedToCore(playbackTask, "audio-playback", 8192, nullptr, 1,
                         &playbackTaskHandle, 1);
  initializeOtaBoot();
  xTaskCreatePinnedToCore(wifiNetworkTask, "wifi-network", 6144, nullptr, 3, nullptr, 1);
  sendStatus();
}

void loop() {
  maintainWifi();
  processUsbDiagnostics();
  captureMicrophones();
  if (!otaActive) pollUserButton();
  updateLed();
  delay(1);
}
