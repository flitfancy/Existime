#include "phone_ble_protocol.h"
#include <cassert>
#include <array>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
  using namespace PhoneBle;
  uint32_t id = 0;
  uint8_t good[] = {1, 0xff, 0xff, 0xff, 0xff};
  assert(command(good, 5, id) && id == 0xffffffffu);
  assert(!command(good, 4, id));
  uint8_t zero[] = {1, 0, 0, 0, 0}, wrong[] = {2, 1, 0, 0, 0};
  assert(!command(zero, 5, id) && !command(wrong, 5, id));
  assert(identifier("board-A") && !identifier("../board") && !identifier(""));
  assert(info("board-A").find("FIREFLY-SENSE N16R8") != std::string::npos);
  assert(begin(7, 0xffffffffu, "boot-A", 0).find("4294967295") != std::string::npos);
  assert(begin(7, 0, "boot", 201).empty());
  const std::string csv = "CSV,1000,1,6,CH6 \"BLE\" \\ 心率,1,NA";
  const auto encoded = row(7, csv.data(), csv.size());
  assert(encoded.find("\\\"BLE\\\"") != std::string::npos && encoded.find("\\\\") != std::string::npos);
  assert(row(7, "CSV\nother", 9).empty());
  assert(row(7, "", 0).empty());
  assert(row(7, std::string(2049, 'x').data(), 2049).empty());
  assert(row(7, std::string(1000, '\t').data(), 1000).empty());
  for (uint16_t mtu : {uint16_t(23), uint16_t(247), uint16_t(517)}) {
    Fragments cursor(encoded, mtu);
    std::string reconstructed;
    while (!cursor.complete()) {
      const auto offset = cursor.offset(), length = cursor.length();
      assert(length <= mtu - 3 && length <= 244);
      // A failed attempt cannot advance and must not manufacture the end record.
      for (int congestion = 0; congestion < 5; ++congestion) assert(cursor.offset() == offset && cursor.length() == length);
      reconstructed.append(encoded, offset, length); cursor.sent();
    }
    assert(reconstructed == encoded);
  }
  assert(!Fragments(encoded, 22).valid());
  Session session;
  assert(session.connect(4) && !session.connect(5));
  assert(!session.request(4, 7, true, true));
  assert(!session.subscribe(4, true, false, true));
  assert(session.subscribe(4, true, true, true));
  assert(!session.request(4, 7, true, false));
  assert(session.request(4, 7, true, true));
  assert(!session.request(4, 8, true, true));
  Job first; assert(session.take(first) && session.current(first));
  assert(!session.take(first));
  session.disconnect(99); assert(session.current(first));
  session.disconnect(4); assert(!session.current(first));
  assert(session.connect(4) && session.subscribe(4, true, true, true));
  assert(session.request(4, 7, true, true));
  Job second; assert(session.take(second));
  assert(!session.finish(first) && session.current(second));
  assert(session.subscribe(4, false, true, true) && !session.current(second));
  assert(session.subscribe(4, true, true, true) && session.request(4, 9, true, true));
  Job third; assert(session.take(third) && session.finish(third) && !session.busy);
  if (argc == 2) {
    std::array<std::string, 40> cells; cells.fill("NA");
    cells[0] = "1000"; cells[1] = "1"; cells[2] = "6"; cells[3] = "CH6 \"BLE\" \\ 心率"; cells[4] = "1";
    cells[31] = "1.3.6"; cells[32] = "5"; cells[33] = "independent-v1";
    cells[36] = "76"; cells[37] = "1"; cells[39] = "streaming";
    std::string fullCsv = "CSV"; for (const auto& cell : cells) fullCsv += ',' + cell;
    std::ofstream fixture(argv[1], std::ios::binary);
    fixture << begin(7, 6000, "boot-A", 1) << row(7, fullCsv.data(), fullCsv.size()) << end(7, 1);
  }
  std::cout << "Phone BLE: command, JSON escaping, limits, MTU fragments, encrypted/bonded access, concurrency and stale sessions passed\n";
}
