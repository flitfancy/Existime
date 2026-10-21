#ifndef MAP_SENSORS_H
#define MAP_SENSORS_H
#include "compass.h"

struct sensors {
    int32_t mag[3], acc[3], filtered_mag[3], filtered_acc[3];
    uint32_t mag_at, acc_at, mag_samples, acc_samples, mag_errors, acc_errors;
    int heading, direction, candidate_direction;
    unsigned direction_count;
    uint8_t who_mag, who_acc, mag_config[3], acc_config[2];
    bool mag_ready, acc_ready, have_mag, have_acc, new_mag, heading_valid;
};
void sensors_init(struct sensors *s);
void sensors_poll(struct sensors *s, const struct calibration *cal, uint32_t now);
void sensors_reset_heading(struct sensors *s);
#endif
