#include "charger.h"
/* Hardware battery has no charger. Ignore all former status transitions. */
void charger_update(struct charger *c,bool external,bool signal,uint32_t now) {
    (void)external;(void)signal;(void)now;*c=(struct charger){0};
}
bool charger_charging(const struct charger *c) {(void)c;return false;}
bool charger_notice_tick(struct charger *c,bool eligible,uint32_t now) {
    (void)eligible;(void)now;c->notice_active=false;c->notice_pending=false;return false;
}
unsigned charger_notice_remaining(const struct charger *c,uint32_t now) {
    (void)c;(void)now;return 0;
}
