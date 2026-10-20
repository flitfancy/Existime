#pragma once

// Included after the transport helpers. Only the network task owns OTA writes.
struct __attribute__((packed)) FirmwareIdentity {
  char marker[18];
  char version[32];
};
const FirmwareIdentity firmwareIdentity = {"FFVOTA:S3:AUDIO:1", FFV_FIRMWARE_VERSION};
struct __attribute__((packed)) FirmwareInfo {
  uint16_t protocol;
  uint16_t flags; // 1: pending verification, 2: hardware initialized
  uint32_t capacity;
  char partition[16];
  char version[32];
  uint8_t imageSha256[32];
};
struct __attribute__((packed)) OtaStatus {
  uint8_t stage; // 1: ready/writing, 2: rebooting, 3: aborted, 4: confirmed, 255: error
  uint8_t code;
  uint16_t reserved;
  uint32_t received;
  uint32_t total;
  uint32_t detail;
};
static_assert(sizeof(FirmwareInfo) == 88, "Firmware info size");
esp_ota_handle_t otaHandle = 0;
const esp_partition_t *otaPartition = nullptr;
uint32_t otaReceived = 0, otaTotal = 0, otaLastWriteMs = 0;
uint8_t otaExpectedSha[32] = {};
mbedtls_sha256_context otaSha;
bool otaPendingVerify = false;
uint32_t otaVerifyStartedMs = 0;
volatile uint32_t micHealthReads = 0;
uint8_t runningImageSha[32] = {};

bool otaHardwareReady() {
  return micReady && speakerReady && ringBuffer && playbackTaskHandle && micHealthReads >= 20;
}

bool sendFirmwareInfo(uint32_t sequence) {
  const auto *running = esp_ota_get_running_partition();
  const auto *next = esp_ota_get_next_update_partition(nullptr);
  FirmwareInfo info = {1, static_cast<uint16_t>((otaPendingVerify ? 1 : 0) |
      (otaHardwareReady() ? 2 : 0)), next ? next->size : 0, {}, {}, {}};
  if (running) strlcpy(info.partition, running->label, sizeof(info.partition));
  memcpy(info.version, firmwareIdentity.version, sizeof(info.version));
  memcpy(info.imageSha256, runningImageSha, sizeof(info.imageSha256));
  return sendV2Frame(V2_FIRMWARE_INFO, sequence, &info, sizeof(info));
}

void abortOta() {
  if (otaActive) {
    esp_ota_abort(otaHandle);
    mbedtls_sha256_free(&otaSha);
  }
  otaActive = false;
  otaHandle = 0;
}

bool replyOta(uint32_t sequence, uint8_t stage, uint8_t code = 0, uint32_t detail = 0) {
  OtaStatus status = {stage, code, 0, otaReceived, otaTotal, detail};
  return sendV2Frame(V2_OTA_STATUS, sequence, &status, sizeof(status));
}

bool failOta(uint32_t sequence, uint8_t code, uint32_t detail = 0) {
  abortOta();
  return replyOta(sequence, 255, code, detail);
}

bool handleOtaFrame() {
  const uint32_t sequence = v2Header.sequence;
  if (v2Header.type == V2_FIRMWARE_QUERY) return sendFirmwareInfo(sequence);
  if (v2Header.type == V2_OTA_CONFIRM) {
    if (v2Header.payloadLength != 32 || memcmp(v2Payload, runningImageSha, 32) || !otaHardwareReady())
      return replyOta(sequence, 255, 8);
    esp_err_t result = otaPendingVerify ? esp_ota_mark_app_valid_cancel_rollback() : ESP_OK;
    if (result != ESP_OK) return replyOta(sequence, 255, 8, result);
    otaPendingVerify = false;
    return replyOta(sequence, 4) && sendFirmwareInfo(sequence);
  }
  if (v2Header.type == V2_OTA_ABORT) {
    abortOta();
    return replyOta(sequence, 3);
  }
  if (v2Header.type == V2_OTA_BEGIN) {
    if (otaActive || otaPendingVerify || streamActive || v2Header.payloadLength != 36)
      return replyOta(sequence, 255, 1);
    memcpy(&otaTotal, v2Payload, 4);
    memcpy(otaExpectedSha, v2Payload + 4, 32);
    otaPartition = esp_ota_get_next_update_partition(nullptr);
    if (!otaPartition || otaTotal < 288 || otaTotal > otaPartition->size)
      return replyOta(sequence, 255, 2);
    // Mic DMA continues to drain, but no audio UDP or playback runs during flash writes.
    micStreaming = false;
    otaReceived = 0;
    esp_err_t result = esp_ota_begin(otaPartition, OTA_WITH_SEQUENTIAL_WRITES, &otaHandle);
    if (result != ESP_OK) return replyOta(sequence, 255, 3, result);
    mbedtls_sha256_init(&otaSha);
    mbedtls_sha256_starts(&otaSha, 0);
    otaActive = true;
    otaLastWriteMs = millis();
    return replyOta(sequence, 1);
  }
  if (!otaActive) return replyOta(sequence, 255, 4);
  if (v2Header.type == V2_OTA_DATA) {
    uint32_t offset = 0;
    if (v2Header.payloadLength <= 4) return failOta(sequence, 4);
    memcpy(&offset, v2Payload, 4);
    const size_t count = v2Header.payloadLength - 4;
    if (offset != otaReceived || count > 4096 || count > otaTotal - otaReceived)
      return failOta(sequence, 4);
    if (!offset && (count < 24 || v2Payload[4] != 0xe9 || v2Payload[16] != 9 || v2Payload[17] != 0))
      return failOta(sequence, 5); // Only ESP32-S3 application images.
    esp_err_t result = esp_ota_write(otaHandle, v2Payload + 4, count);
    if (result != ESP_OK) return failOta(sequence, 3, result);
    mbedtls_sha256_update(&otaSha, v2Payload + 4, count);
    otaReceived += count;
    otaLastWriteMs = millis();
    return replyOta(sequence, 1);
  }
  if (v2Header.type == V2_OTA_END) {
    if (v2Header.payloadLength || otaReceived != otaTotal) return failOta(sequence, 4);
    uint8_t digest[32];
    mbedtls_sha256_finish(&otaSha, digest);
    if (memcmp(digest, otaExpectedSha, 32)) return failOta(sequence, 6);
    // Verify the project marker in the stored image before making it bootable.
    uint8_t block[512];
    bool identityFound = false;
    for (uint32_t pos = 0; pos < otaTotal && !identityFound; pos += 495) {
      size_t count = min(static_cast<uint32_t>(sizeof(block)), otaTotal - pos);
      if (esp_partition_read(otaPartition, pos, block, count) != ESP_OK) return failOta(sequence, 3);
      for (size_t i = 0; i + sizeof(firmwareIdentity.marker) <= count; ++i)
        if (!memcmp(block + i, firmwareIdentity.marker, sizeof(firmwareIdentity.marker))) { identityFound = true; break; }
      vTaskDelay(1);
    }
    if (!identityFound) return failOta(sequence, 5);
    esp_err_t result = esp_ota_end(otaHandle);
    mbedtls_sha256_free(&otaSha);
    otaActive = false;
    otaHandle = 0;
    if (result != ESP_OK) return replyOta(sequence, 255, 7, result);
    result = esp_ota_set_boot_partition(otaPartition);
    if (result != ESP_OK) return replyOta(sequence, 255, 7, result);
    replyOta(sequence, 2);
    delay(150);
    ESP.restart();
    return false;
  }
  return failOta(sequence, 4);
}

void initializeOtaBoot() {
  const auto *running = esp_ota_get_running_partition();
  esp_ota_img_states_t imageState;
  otaPendingVerify = running && esp_ota_get_state_partition(running, &imageState) == ESP_OK &&
      imageState == ESP_OTA_IMG_PENDING_VERIFY;
  otaVerifyStartedMs = millis();
  if (running) esp_partition_get_sha256(running, runningImageSha);
}

void maintainOta() {
  if (otaActive && millis() - otaLastWriteMs > 15000) abortOta();
  if (otaPendingVerify && millis() - otaVerifyStartedMs > 90000) {
    esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP.restart();
  }
}
