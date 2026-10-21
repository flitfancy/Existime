#ifndef MAP_LEDS_H
#define MAP_LEDS_H
#include "display.h"

struct leds {
    struct display_frame frame;
    uint8_t sent[5], config[5], input[5], known, verified, direct, verify_index, retry_mask, center_duty;
    uint32_t errors, pwm_epoch, pwm_writes, max_write_us, retry_at[5];
};
void leds_init(struct leds *leds);
void leds_set_frame(struct leds *leds, const struct display_frame *frame, uint32_t now_us);
void leds_service(struct leds *leds);
void leds_verify_next(struct leds *leds);
unsigned leds_next_edge(const struct leds *leds, uint32_t now_us);
#endif
