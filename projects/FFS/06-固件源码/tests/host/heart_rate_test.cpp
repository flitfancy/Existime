#include "heart_rate_measurement.h"
#include <assert.h>
#include <stdio.h>

int main() {
  HeartRateSample sample;
  assert(!sample.valid(true, 0));
  const uint8_t eight[] = {0, 72};
  assert(sample.ingest(eight, sizeof(eight), 100));
  assert(sample.bpm == 72 && sample.contact == -1 && sample.sequence == 1);
  assert(sample.valid(true, 15100));
  assert(!sample.valid(true, 15101) && !sample.valid(false, 100));
  const uint8_t sixteen[] = {1 | 4 | 2, 0x2c, 0x01};
  assert(sample.ingest(sixteen, sizeof(sixteen), 200));
  assert(sample.bpm == 300 && sample.contact == 1);
  const uint8_t noContact[] = {4, 75};
  assert(!sample.ingest(noContact, sizeof(noContact), 250));
  assert(sample.contact == 0 && !sample.valid(true, 250) && sample.sequence == 2);
  const uint8_t zero[] = {0, 0};
  assert(!sample.ingest(zero, sizeof(zero), 260));
  assert(sample.sampledAt == 200 && sample.sequence == 2);
  const uint8_t withOptional[] = {8 | 16, 82, 5, 0, 0x00, 0x04, 0x10, 0x04};
  assert(sample.ingest(withOptional, sizeof(withOptional), 300));
  assert(sample.bpm == 82 && sample.sequence == 3);
  const uint8_t truncated[] = {1, 0xff};
  const uint8_t badEnergy[] = {8, 83, 1};
  const uint8_t badRR[] = {16, 83, 1};
  const uint8_t missingRR[] = {16, 83};
  assert(!sample.ingest(truncated, sizeof(truncated), 400));
  assert(!sample.ingest(badEnergy, sizeof(badEnergy), 400));
  assert(!sample.ingest(badRR, sizeof(badRR), 400));
  assert(!sample.ingest(missingRR, sizeof(missingRR), 400));
  assert(!sample.ingest(nullptr, 0, 400));
  assert(sample.invalidPackets == 5 && sample.sampledAt == 300 && sample.sequence == 3);
  assert(sample.ingest(eight, sizeof(eight), 0xfffffff0u));
  assert(sample.valid(true, 0x100u));
  assert(!sample.valid(true, 15000u));
  puts("Heart-rate parser/freshness/contact/wrap scenarios passed");
}
