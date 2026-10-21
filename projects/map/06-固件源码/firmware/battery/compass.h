#ifndef MAP_COMPASS_H
#define MAP_COMPASS_H
#include <stdint.h>
#include <stdbool.h>
#define HEADING_WINDOW_CDEG 300
int angle_wrap(int angle);
int angle_delta(int a, int b);
int compass_heading(int sensor_angle, int stored_north);
int direction_index(int heading);
struct calibration { int32_t bias[3],radius[3],north;uint32_t valid; };
struct collector { int32_t min[3],max[3];uint32_t samples,faces,started; };
uint32_t integer_sqrt(uint32_t n);
int atan2_cdeg(int32_t y,int32_t x);
void collector_begin(struct collector *c,uint32_t now);
void collector_sample(struct collector *c,const int32_t m[3],const int32_t a[3]);
bool collector_finish(const struct collector *c,uint32_t now,struct calibration *result);
bool compass_angle(const struct calibration *cal,const int32_t m[3],const int32_t a[3],int *angle);
#endif
