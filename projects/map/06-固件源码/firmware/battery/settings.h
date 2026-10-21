#ifndef MAP_SETTINGS_H
#define MAP_SETTINGS_H
#include "compass.h"
#define RECORD_WORDS 14
struct settings { uint32_t mask_lo,mask_hi;struct calibration cal; };
uint32_t record_crc(const uint8_t *data,unsigned count);
bool record_decode(const uint8_t bytes[56],struct settings *s,uint32_t *sequence);
void record_encode(uint8_t bytes[56],const struct settings *s,uint32_t sequence);
struct storage {
    struct settings saved;
    uint32_t sequence, loaded, dirty, generation, snapshot_generation;
    uint32_t phase, address, position, deadline, retry_at, successes, errors;
    unsigned slot;
    uint8_t bytes[56];
};
void storage_load(struct storage *s,struct settings *out);
void storage_request(struct storage *s);
void storage_tick(struct storage *s,const struct settings *current,uint32_t now);
#endif
