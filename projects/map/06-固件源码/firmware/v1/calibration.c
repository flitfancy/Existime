#include "calibration.h"

bool calibration_action(struct calibration_flow *c, struct app *a,
                        unsigned action, uint32_t now) {
    if (action & ACT_CAL_BEGIN) {
        *c = (struct calibration_flow){0};
        collector_begin(&c->collector, now);
        return true;
    }
    if (action & ACT_CAL_FINISH) {
        if (collector_finish(&c->collector, now, &c->pending))
            app_set_mode(a, CAL_NORTH, now);
        else
            a->feedback_until = now + 1000;
    }
    if (action & ACT_NORTH) {
        app_set_mode(a, CAL_SETTLE, now);
        c->settle_count = 0;
    }
    if (action & ACT_CANCEL) {
        c->settle_count = 0;
        return true;
    }
    return false;
}

void calibration_sample(struct calibration_flow *c, struct app *a,
                        struct settings *settings, struct storage *store,
                        const int32_t mag[3], const int32_t acc[3],
                        uint32_t now, unsigned buttons) {
    if (a->mode == CAL_COLLECT) {
        if ((uint32_t)(now - a->entered_at) >= 500)
            collector_sample(&c->collector, mag, acc);
        return;
    }
    if (a->mode != CAL_SETTLE) return;
    /* Button movement never counts as stable north alignment. */
    if (buttons || (uint32_t)(now - a->entered_at) < 300) {
        c->settle_count = 0;
        return;
    }
    int angle;
    if (!compass_angle(&c->pending, mag, acc, &angle)) {
        c->settle_count = 0;
        return;
    }
    if (!c->settle_count) {
        c->settle_anchor = angle;
        c->settle_sum = 0;
        c->settle_started = now;
    }
    int delta = angle_delta(angle, c->settle_anchor);
    if (delta > 150 || delta < -150) {
        c->settle_count = 0;
        return;
    }
    c->settle_sum += delta;
    ++c->settle_count;
    if (c->settle_count < 40 || (uint32_t)(now - c->settle_started) < 1800)
        return;

    c->pending.north = angle_wrap(c->settle_anchor + c->settle_sum / (int)c->settle_count);
    c->pending.valid = 1;
    settings->cal = c->pending;
    settings->mask_lo = a->mask_lo;
    settings->mask_hi = a->mask_hi;
    c->successes_before_save = store->successes;
    storage_request(store);
    app_set_mode(a, CAL_SAVE, now);
    a->release_lock = 1;
}

bool calibration_tick(struct calibration_flow *c, struct app *a,
                      const struct storage *store, uint32_t now) {
    if (a->mode == CAL_SETTLE && (uint32_t)(now - a->entered_at) > 15000) {
        c->settle_count = 0;
        app_set_mode(a, CAL_NORTH, now);
    }
    if (a->mode != CAL_SAVE || store->phase || store->dirty ||
        store->successes == c->successes_before_save) return false;

    /* storage_tick has checked the full committed record against EEPROM. */
    app_return_normal(a, now);
    return true;
}
