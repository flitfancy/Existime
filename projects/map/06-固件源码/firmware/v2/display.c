#include "display.h"
#include "board_generated.h"
#include "timing.h"

static void light(struct display_frame *f, unsigned led) {
    struct led_driver d = led_drivers[led];
    if (d.device < 5) f->output[d.device] &= (uint8_t)~(1u << d.bit);
    else if (led == 7) f->direct |= 1;
    else if (led == 8) f->direct |= 2;
    else if (led == 43) f->direct |= 4;
}

unsigned breath_duty(uint32_t elapsed) {
    unsigned phase = elapsed % BREATH_PERIOD_MS;
    unsigned ramp = phase <= BREATH_PERIOD_MS / 2 ? phase : BREATH_PERIOD_MS - phase;
    unsigned linear = ramp * 255u / (BREATH_PERIOD_MS / 2);
    return (linear * linear + 127u) / 255u;
}

static void battery_frame(struct display_frame *f, const struct app *a,
                          const struct battery_status *b, bool charging, uint32_t now) {
    if (b->state != BAT_VALID || (uint32_t)(now - b->updated_at) > 2500) {
        f->output[0] = (now / 250) & 1u ? 0 : 255;
        return;
    }
    unsigned bars = b->bars > 8 ? 8 : b->bars;
    uint32_t elapsed = now - a->entered_at;
    unsigned revealed = elapsed / BATTERY_REVEAL_MS;
    if (revealed > bars) revealed = bars;
    f->revealed_bars = (uint8_t)revealed;
    unsigned ring = battery_ring(revealed);
    if (revealed == bars && charging) {
        /* At eight bars there is no ninth LED: the final LED breathes. */
        unsigned next = bars < 8 ? 1u << (7u - bars) : 1u;
        ring &= ~next;
        f->breath_mask = (uint8_t)next;
        f->breath_duty = (uint8_t)breath_duty(elapsed - bars * BATTERY_REVEAL_MS);
    } else if (!bars && !charging && (now / 250) & 1u) ring = 0x80;
    f->output[0] = (uint8_t)~ring;
}

void display_build(struct display_frame *f, const struct app *a,
                   const struct battery_status *b, const struct calibration_flow *c,
                   int direction, bool heading_valid, bool charging,
                   bool save_failed, uint32_t now) {
    *f = (struct display_frame){{255,255,255,255,255},0,0,0,0,0};
    if (app_center_on(a, now)) light(f, 43);
    if (a->mode == NORMAL || a->mode == MENU || a->mode == LED_EDIT || a->mode == BATTERY_VIEW) {
        for (unsigned i = 0; i < PROVINCE_COUNT; ++i) {
            bool on = i < 32 ? !!(a->mask_lo & (1u << i)) : !!(a->mask_hi & (1u << (i - 32)));
            if (a->mode == LED_EDIT && i == a->selected && time_reached(now, a->feedback_until))
                on = !!((now / 250) & 1u);
            if (on) light(f, province_order[i]);
        }
    }
    if (a->mode == NORMAL) {
        if (heading_valid && direction >= 0 && direction < 8) light(f, (unsigned)(35 + direction));
    } else if (a->mode == BATTERY_VIEW) {
        battery_frame(f, a, b, charging, now);
    } else if (a->mode == CAL_COLLECT) {
        struct calibration candidate;
        unsigned n = 0;
        for (unsigned bits = c->collector.faces; bits; bits >>= 1) n += bits & 1u;
        if (collector_finish(&c->collector, now, &candidate)) n = 8;
        if (!time_reached(now, a->feedback_until)) n = (now / 100) & 1u ? 8 : 0;
        for (unsigned i = 0; i < n; ++i) light(f, 35 + i);
    } else if (a->mode == CAL_NORTH) {
        if ((now / 250) & 1u) light(f, 35);
    } else if (a->mode == CAL_SETTLE || a->mode == CAL_SAVE) {
        light(f, 35);
    }
    if (save_failed && a->mode != NORMAL && a->mode != BATTERY_VIEW)
        f->direct = (uint8_t)((f->direct & ~4u) | (((now / 100) & 1u) ? 4u : 0));
    /* Keep the center phase continuous when entering/leaving battery view.
       Editing/calibration still owns its binary blink feedback. */
    f->center_duty = charging && (a->mode == NORMAL || a->mode == BATTERY_VIEW) ?
        (uint8_t)breath_duty(now / 2u) : (f->direct & 4u ? 255 : 0);
}

unsigned display_ring(const struct display_frame *f, uint32_t elapsed_us) {
    unsigned ring = (uint8_t)~f->output[0];
    unsigned on_us = (unsigned)f->breath_duty * RING_PWM_PERIOD_US / 255u;
    if (elapsed_us % RING_PWM_PERIOD_US < on_us) ring |= f->breath_mask;
    return ring;
}

unsigned display_next_edge(const struct display_frame *f, uint32_t elapsed_us) {
    if (!f->breath_mask || !f->breath_duty || f->breath_duty == 255) return UINT32_MAX;
    unsigned phase = elapsed_us % RING_PWM_PERIOD_US;
    unsigned on_us = (unsigned)f->breath_duty * RING_PWM_PERIOD_US / 255u;
    return phase < on_us ? on_us - phase : RING_PWM_PERIOD_US - phase;
}

void display_charge_complete(struct display_frame *f) {
    /* The ring has its own 0x20 expander: leave every province output intact. */
    f->output[0] = 0;
    f->direct |= 4;
    f->center_duty = 255;
    f->breath_mask = f->breath_duty = 0;
    f->revealed_bars = 8;
}
