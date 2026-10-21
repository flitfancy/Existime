#ifndef MAP_TIMING_H
#define MAP_TIMING_H
#include <stdint.h>

static inline int time_reached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}
#endif
