#include "leds.h"
#include "platform.h"
#include "timing.h"

void leds_init(struct leds *l) {
    *l = (struct leds){0};
    l->frame = (struct display_frame){{255,255,255,255,255},0,0,0,0,0};
    for (unsigned i = 0; i < 5; ++i) {
        l->sent[i] = 255;
        l->input[i] = 255;
        if (write_reg((uint8_t)(0x20 + i), 1, 255) && write_reg((uint8_t)(0x20 + i), 3, 0))
            l->known |= (uint8_t)(1u << i);
        else ++l->errors;
    }
    direct_leds(0);
    center_led_duty(0);
}

void leds_set_frame(struct leds *l, const struct display_frame *f, uint32_t now_us) {
    if (f->breath_mask != l->frame.breath_mask) l->pwm_epoch = now_us;
    l->frame = *f;
}

void leds_service(struct leds *l) {
    if (l->center_duty != l->frame.center_duty) {
        center_led_duty(l->frame.center_duty);
        l->center_duty = l->frame.center_duty;
    }
    for (unsigned i = 0; i < 5; ++i) {
        uint32_t now = micros();
        uint8_t value = i ? l->frame.output[i] : (uint8_t)~display_ring(&l->frame, now - l->pwm_epoch);
        if ((l->known & (1u << i)) && value == l->sent[i]) continue;
        if ((l->retry_mask & (1u << i)) && !time_reached(now, l->retry_at[i])) continue;
        if (write_reg((uint8_t)(0x20 + i), 1, value)) {
            l->sent[i] = value;
            l->known |= (uint8_t)(1u << i);
            l->retry_mask &= (uint8_t)~(1u << i);
            if (!i && l->frame.breath_mask) ++l->pwm_writes;
        } else {
            ++l->errors;
            l->known &= (uint8_t)~(1u << i);
            l->verified &= (uint8_t)~(1u << i);
            l->retry_at[i] = now + 10000;
            l->retry_mask |= (uint8_t)(1u << i);
        }
        uint32_t elapsed = micros() - now;
        if (elapsed > l->max_write_us) l->max_write_us = elapsed;
    }
    if (l->direct != l->frame.direct) {
        direct_leds(l->frame.direct);
        l->direct = l->frame.direct;
    }
}

void leds_verify_next(struct leds *l) {
    unsigned i = l->verify_index;
    l->verify_index = (uint8_t)((i + 1) % 5);
    uint8_t output = 255, config = 255, input = 255;
    bool ok = read_reg((uint8_t)(0x20 + i), 1, &output) &&
        read_reg((uint8_t)(0x20 + i), 3, &config) && read_reg((uint8_t)(0x20 + i), 0, &input);
    l->config[i] = config;
    l->input[i] = input;
    if (ok && (l->known & (1u << i)) && output == l->sent[i] && !config && input == output) {
        l->verified |= (uint8_t)(1u << i);
    } else {
        ++l->errors;
        l->verified &= (uint8_t)~(1u << i);
        if (!write_reg((uint8_t)(0x20 + i), 1, l->sent[i]) ||
            !write_reg((uint8_t)(0x20 + i), 3, 0)) l->known &= (uint8_t)~(1u << i);
    }
}

unsigned leds_next_edge(const struct leds *l, uint32_t now_us) {
    return display_next_edge(&l->frame, now_us - l->pwm_epoch);
}
