#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ESP image header (24), first segment header (8), app descriptor (256),
// then our fixed custom descriptor. The linker keeps this order.
constexpr size_t kOtaPrefixSize = 320;
extern const char ffs_ota_device_marker[32];

class OtaWriter {
 public:
  virtual ~OtaWriter() = default;
  virtual bool begin(size_t size) = 0;
  virtual size_t write(uint8_t* data, size_t size) = 0;
  virtual bool end() = 0;
  virtual void abort() = 0;
};

// Stream the application into the inactive partition. Never activate a partial
// upload, and check chip/board identity before the first flash write.
class OtaSession {
 public:
  explicit OtaSession(OtaWriter& writer) : writer_(writer) {}

  bool start(size_t size, size_t capacity) {
    abort();
    expected_ = size;
    received_ = prefixSize_ = 0;
    complete_ = false;
    error_ = nullptr;
    if (size < kOtaPrefixSize) return fail("image-too-small");
    if (size > capacity) return fail("image-too-large");
    return true;
  }

  bool write(uint8_t* data, size_t size) {
    if (error_ || complete_) return false;
    if (size > expected_ - received_) return fail("length-mismatch");
    received_ += size;
    if (prefixSize_ < kOtaPrefixSize) {
      const size_t take = size < kOtaPrefixSize - prefixSize_
                              ? size : kOtaPrefixSize - prefixSize_;
      memcpy(prefix_ + prefixSize_, data, take);
      prefixSize_ += take;
      data += take;
      size -= take;
      if (prefixSize_ < kOtaPrefixSize) return true;
      if (prefix_[0] != 0xe9 || prefix_[1] == 0 || prefix_[1] > 16 ||
          prefix_[12] != 9 || prefix_[13] != 0 || prefix_[23] != 1 ||
          prefix_[32] != 0x32 || prefix_[33] != 0x54 ||
          prefix_[34] != 0xcd || prefix_[35] != 0xab ||
          memcmp(prefix_ + 288, ffs_ota_device_marker, 32) != 0) {
        return fail("wrong-device-image");
      }
      if (!writer_.begin(expected_)) return fail("flash-begin-failed");
      active_ = true;
      if (writer_.write(prefix_, kOtaPrefixSize) != kOtaPrefixSize)
        return fail("flash-write-failed");
    }
    if (size && writer_.write(data, size) != size)
      return fail("flash-write-failed");
    return true;
  }

  bool finish() {
    if (error_ || complete_) return false;
    if (!active_ || received_ != expected_) return fail("incomplete-upload");
    if (!writer_.end()) return fail("image-verification-failed");
    active_ = false;
    complete_ = true;
    return true;
  }

  void abort() {
    if (active_) writer_.abort();
    active_ = false;
  }
  bool fail(const char* reason) {
    abort();
    error_ = reason;
    return false;
  }
  const char* error() const { return error_; }
  bool complete() const { return complete_; }
  size_t received() const { return received_; }

 private:
  OtaWriter& writer_;
  uint8_t prefix_[kOtaPrefixSize] = {};
  size_t expected_ = 0, received_ = 0, prefixSize_ = 0;
  bool active_ = false, complete_ = false;
  const char* error_ = "no-upload";
};
