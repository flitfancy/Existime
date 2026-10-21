#ifndef MAP_CALIBRATION_H
#define MAP_CALIBRATION_H
#include "app.h"
#include "compass.h"
#include "settings.h"

struct calibration_flow {
    struct collector collector;
    struct calibration pending;
    uint32_t settle_count, settle_started, successes_before_save;
    int settle_anchor, settle_sum;
};
bool calibration_action(struct calibration_flow *c, struct app *a,
                        unsigned action, uint32_t now);
void calibration_sample(struct calibration_flow *c, struct app *a,
                        struct settings *settings, struct storage *store,
                        const int32_t mag[3], const int32_t acc[3],
                        uint32_t now, unsigned buttons);
bool calibration_tick(struct calibration_flow *c, struct app *a,
                      const struct storage *store, uint32_t now);
#endif
