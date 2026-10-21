#ifndef MAP_APP_H
#define MAP_APP_H
#include <stdint.h>
#include <stdbool.h>
enum mode { NORMAL, MENU, LED_EDIT, CAL_COLLECT, CAL_NORTH, CAL_SETTLE, BATTERY_VIEW, CAL_SAVE };
enum action { ACT_NONE=0, ACT_DIRTY=1, ACT_CAL_BEGIN=2, ACT_CAL_FINISH=4, ACT_NORTH=8, ACT_CANCEL=16 };
struct key_state { uint32_t changed, down_at; uint8_t raw, down, fired, blocked; };
struct app {
    uint32_t mask_lo, mask_hi;
    enum mode mode;
    uint32_t entered_at, chord_at, feedback_until;
    unsigned selected, chord, release_lock;
    struct key_state key[2];
};
void app_init(struct app *a, uint32_t lo, uint32_t hi);
unsigned app_tick(struct app *a, uint32_t now, unsigned raw_buttons);
bool app_selected_on(const struct app *a);
bool app_center_on(const struct app *a,uint32_t now);
void app_set_mode(struct app *a, enum mode mode, uint32_t now);
void app_return_normal(struct app *a, uint32_t now);
#endif
