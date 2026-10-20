#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace PhoneBle {
constexpr char kServiceUuid[] = "9f8d0001-7b6a-4b61-8d0f-0ac49f8a0001";
constexpr char kInfoUuid[] = "9f8d0002-7b6a-4b61-8d0f-0ac49f8a0001";
constexpr char kDataUuid[] = "9f8d0003-7b6a-4b61-8d0f-0ac49f8a0001";
constexpr char kControlUuid[] = "9f8d0004-7b6a-4b61-8d0f-0ac49f8a0001";
constexpr size_t kMaxRows = 200;
constexpr size_t kMaxCsvBytes = 2048;
constexpr size_t kMaxRecordBytes = 4096;
constexpr uint16_t kNoPeer = 0xffff;

inline bool command(const uint8_t* bytes, size_t length, uint32_t& id) {
  if (!bytes || length != 5 || bytes[0] != 1) return false;
  id = uint32_t(bytes[1]) | uint32_t(bytes[2]) << 8 | uint32_t(bytes[3]) << 16 | uint32_t(bytes[4]) << 24;
  return id != 0;
}
inline bool identifier(const char* value) {
  if (!value) return false;
  size_t length = 0;
  while (value[length]) {
    const char c = value[length++];
    if (length > 64 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
  }
  return length != 0;
}
inline std::string info(const char* boardId) {
  if (!identifier(boardId)) return {};
  return std::string("{\"model\":\"FIREFLY-SENSE N16R8\",\"ble_protocol\":1,\"board_id\":\"") + boardId + "\"}";
}
inline std::string begin(uint32_t id, uint32_t uptime, const char* bootId, size_t rows) {
  if (!id || rows > kMaxRows || !identifier(bootId)) return {};
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "{\"v\":1,\"type\":\"begin\",\"request_id\":%lu,\"uptime_ms\":%lu,\"boot_id\":\"%s\",\"rows\":%u}\n",
      static_cast<unsigned long>(id), static_cast<unsigned long>(uptime), bootId, static_cast<unsigned>(rows));
  return buffer;
}
inline std::string row(uint32_t id, const char* csv, size_t size) {
  if (!id || !csv || size == 0 || size > kMaxCsvBytes) return {};
  std::string out = "{\"v\":1,\"type\":\"row\",\"request_id\":" + std::to_string(id) + ",\"csv\":\"";
  out.reserve(size + 96);
  for (size_t index = 0; index < size; ++index) {
    const uint8_t c = static_cast<uint8_t>(csv[index]);
    if (c == '\n' || c == '\r' || c == 0) return {}; // A CSV row cannot contain another physical line.
    if (c == '"' || c == '\\') out += '\\';
    if (c < 32) {
      char escaped[7]; snprintf(escaped, sizeof(escaped), "\\u%04x", c); out += escaped;
    } else out += static_cast<char>(c);
    if (out.size() > kMaxRecordBytes - 3) return {};
  }
  out += "\"}\n";
  return out;
}
inline std::string end(uint32_t id, size_t rows) {
  if (!id || rows > kMaxRows) return {};
  return "{\"v\":1,\"type\":\"end\",\"request_id\":" + std::to_string(id) + ",\"rows\":" + std::to_string(rows) + "}\n";
}

struct Job { uint16_t peer = kNoPeer; uint32_t epoch = 0; uint32_t request = 0; };
// The runtime holds a short spinlock only while modifying this POD state.
struct Session {
  uint16_t peer = kNoPeer;
  uint32_t epoch = 0;
  uint32_t requestId = 0;
  bool subscribed = false;
  bool busy = false;
  bool pending = false;
  bool connect(uint16_t value) {
    if (value == kNoPeer || peer != kNoPeer) return false;
    peer = value; ++epoch; subscribed = busy = pending = false; requestId = 0; return true;
  }
  void disconnect(uint16_t value) {
    if (value != peer) return;
    peer = kNoPeer; ++epoch; subscribed = busy = pending = false;
  }
  bool subscribe(uint16_t value, bool enabled, bool encrypted, bool bonded) {
    if (value != peer || (enabled && (!encrypted || !bonded))) return false;
    subscribed = enabled;
    if (!enabled) { ++epoch; busy = pending = false; }
    return true;
  }
  bool request(uint16_t value, uint32_t id, bool encrypted, bool bonded) {
    if (value != peer || !id || !encrypted || !bonded || !subscribed || busy) return false;
    requestId = id; busy = pending = true; return true;
  }
  bool take(Job& job) {
    if (!pending || !busy || peer == kNoPeer) return false;
    job.peer = peer; job.epoch = epoch; job.request = requestId; pending = false; return true;
  }
  bool current(const Job& job) const { return peer == job.peer && epoch == job.epoch && requestId == job.request && busy && subscribed; }
  bool finish(const Job& job) {
    if (!current(job)) return false;
    busy = pending = false; return true;
  }
};

class Fragments {
 public:
  Fragments(const std::string& value, uint16_t mtu) : size_(value.size()), payload_(mtu >= 23 ? std::min<size_t>(mtu - 3, 244) : 0) {}
  bool valid() const { return payload_ && size_ && size_ <= kMaxRecordBytes; }
  size_t offset() const { return offset_; }
  size_t length() const { return valid() ? std::min(payload_, size_ - offset_) : 0; }
  bool complete() const { return valid() && offset_ == size_; }
  void sent() { offset_ += length(); } // Called only after a successful notification enqueue result.
 private:
  size_t size_, payload_, offset_ = 0;
};
} // namespace PhoneBle
