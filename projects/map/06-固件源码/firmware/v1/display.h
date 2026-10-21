#ifndef MAP_DISPLAY_H
#define MAP_DISPLAY_H
#include "app.h"
#include "battery.h"
#include "calibration.h"

#define BATTERY_REVEAL_MS 100u
#define BREATH_PERIOD_MS 1600u
#define CENTER_BREATH_PERIOD_MS (BREATH_PERIOD_MS * 2u)
#define RING_PWM_PERIOD_US 10000u

struct display_frame {
    uint8_t output[5];
    uint8_t direct, breath_mask, breath_duty, revealed_bars, center_duty;
};
unsigned breath_duty(uint32_t elapsed_ms);
void display_build(struct display_frame *frame, const struct app *app,
                   const struct battery_status *battery,
                   const struct calibration_flow *calibration,
                   int direction, bool heading_valid, bool charging,
                   bool save_failed, uint32_t now);
unsigned display_ring(const struct display_frame *frame, uint32_t elapsed_us);
unsigned display_next_edge(const struct display_frame *frame, uint32_t elapsed_us);
void display_charge_complete(struct display_frame *frame);
#endif
