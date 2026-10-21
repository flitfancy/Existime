#ifndef MAP_CHARGER_H
#define MAP_CHARGER_H
#include <stdint.h>
#include <stdbool.h>

#define CHARGE_FINISH_CONFIRM_MS 1000u
#define CHARGE_DONE_NOTICE_MS 3000u
enum charge_phase { CHARGE_NO_INPUT, CHARGE_IDLE, CHARGE_ACTIVE, CHARGE_FINISH_WAIT, CHARGE_COMPLETE };
struct charger {
    enum charge_phase phase;
    uint32_t finish_started, notice_started, completions;
    bool external_power, charge_signal, notice_pending, notice_active;
};
void charger_update(struct charger *c, bool external_power, bool charge_signal, uint32_t now);
bool charger_charging(const struct charger *c);
bool charger_notice_tick(struct charger *c, bool eligible, uint32_t now);
unsigned charger_notice_remaining(const struct charger *c, uint32_t now);
#endif
