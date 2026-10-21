#include "charger.h"

void charger_update(struct charger *c, bool external_power, bool charge_signal, uint32_t now) {
    c->external_power = external_power;
    c->charge_signal = external_power && charge_signal;
    if (!external_power) {
        /* Removing USB must not turn a charging-to-idle edge into completion.
           A notice already confirmed before removal may finish its display. */
        c->phase = CHARGE_NO_INPUT;
        return;
    }
    if (c->phase == CHARGE_NO_INPUT) c->phase = CHARGE_IDLE;
    /* Latch one completion per USB session. Short CHG pulses after completion
       must not keep restarting the three-second indication. */
    if (c->phase == CHARGE_COMPLETE) return;
    if (c->charge_signal) {
        c->phase = CHARGE_ACTIVE;
        return;
    }
    if (c->phase == CHARGE_ACTIVE) {
        c->phase = CHARGE_FINISH_WAIT;
        c->finish_started = now;
    } else if (c->phase == CHARGE_FINISH_WAIT &&
               (uint32_t)(now - c->finish_started) >= CHARGE_FINISH_CONFIRM_MS) {
        /* PGOOD remained valid and CHG stayed high for a full second.
           This rejects brief transitions and the charger's 2Hz timer fault. */
        c->phase = CHARGE_COMPLETE;
        c->notice_pending = true;
        ++c->completions;
    }
}

bool charger_charging(const struct charger *c) {
    return c->external_power && c->phase != CHARGE_COMPLETE &&
        (c->charge_signal || c->phase == CHARGE_FINISH_WAIT);
}

bool charger_notice_tick(struct charger *c, bool eligible, uint32_t now) {
    if (!eligible) {
        if (c->notice_active) c->notice_pending = true;
        c->notice_active = false;
        return false;
    }
    if (c->notice_active && (uint32_t)(now - c->notice_started) >= CHARGE_DONE_NOTICE_MS)
        c->notice_active = false;
    if (c->notice_pending && !c->notice_active) {
        c->notice_pending = false;
        c->notice_active = true;
        c->notice_started = now;
    }
    return c->notice_active;
}

unsigned charger_notice_remaining(const struct charger *c, uint32_t now) {
    uint32_t elapsed = now - c->notice_started;
    return c->notice_active && elapsed < CHARGE_DONE_NOTICE_MS ? CHARGE_DONE_NOTICE_MS - elapsed : 0;
}
