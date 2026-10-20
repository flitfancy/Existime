#include "ota_session.h"
#include <cstdlib>
#include <iostream>
#include <vector>

extern const char ffs_ota_device_marker[32] = "FIREFLY-SENSE-N16R8-OTA1";
static void require(bool condition, const char* message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
struct Writer : OtaWriter {
  size_t expected = 0;
  unsigned begins = 0, ends = 0, aborts = 0;
  bool failWrite = false, failEnd = false;
  std::vector<uint8_t> bytes;
  bool begin(size_t size) override { ++begins; expected = size; bytes.clear(); return true; }
  size_t write(uint8_t* data, size_t size) override {
    if (failWrite) return 0;
    bytes.insert(bytes.end(), data, data + size); return size;
  }
  bool end() override { ++ends; return !failEnd; }
  void abort() override { ++aborts; }
};
static std::vector<uint8_t> image() {
  std::vector<uint8_t> result(4097, 0);
  result[0] = 0xe9; result[1] = 4; result[12] = 9; result[23] = 1;
  result[32] = 0x32; result[33] = 0x54; result[34] = 0xcd; result[35] = 0xab;
  memcpy(result.data() + 288, ffs_ota_device_marker, 32);
  for (size_t i = 320; i < result.size(); ++i) result[i] = i % 251;
  return result;
}
int main() {
  for (size_t chunk : {size_t(1), size_t(37), size_t(320), size_t(1436), size_t(4097)}) {
    Writer writer; OtaSession session(writer); auto data = image();
    require(session.start(data.size(), 0x640000), "valid size rejected");
    for (size_t i = 0; i < data.size(); i += chunk)
      require(session.write(data.data() + i, std::min(chunk, data.size() - i)), "stream rejected");
    require(session.finish() && session.complete(), "complete upload not activated");
    require(writer.bytes == data && writer.expected == data.size(), "stream bytes changed");
    require(writer.begins == 1 && writer.ends == 1 && !writer.aborts, "unexpected flash lifecycle");
    require(!session.finish() && writer.ends == 1, "repeated completion activated twice");
  }
  for (size_t offset : {size_t(0), size_t(1), size_t(12), size_t(23), size_t(32), size_t(288)}) {
    Writer writer; OtaSession session(writer); auto data = image(); data[offset] ^= 0xff;
    session.start(data.size(), 0x640000);
    require(!session.write(data.data(), data.size()), "wrong image accepted");
    require(!writer.begins && !writer.ends && writer.bytes.empty(), "wrong image touched flash");
  }
  {
    Writer writer; OtaSession session(writer); auto data = image();
    require(!session.start(10, 0x640000), "short image accepted");
    require(!session.start(0x640001, 0x640000), "oversize image accepted");
    session.start(data.size(), 0x640000); session.write(data.data(), 1000);
    require(!session.finish() && !writer.ends && writer.aborts == 1, "partial image activated");
    session.start(data.size(), 0x640000);
    const unsigned beginsBefore = writer.begins;
    require(!session.write(data.data(), data.size() + 1) && writer.begins == beginsBefore, "overrun touched flash");
  }
  {
    Writer writer; OtaSession session(writer); auto data = image();
    writer.failWrite = true; session.start(data.size(), 0x640000);
    require(!session.write(data.data(), data.size()) && writer.aborts == 1 && !writer.ends,
            "failed write activated");
    writer.failWrite = false; writer.failEnd = true; session.start(data.size(), 0x640000);
    session.write(data.data(), data.size());
    require(!session.finish() && !session.complete(), "failed verification reported success");
    writer.failEnd = false; session.start(data.size(), 0x640000);
    session.write(data.data(), data.size()); require(session.finish(), "retry did not recover");
  }
  std::cout << "PASS: OTA streaming, wrong target, truncation, limits, flash errors and retries\n";
}
