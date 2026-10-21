#ifndef MAP_BATTERY_PROFILE_H
#define MAP_BATTERY_PROFILE_H
/* Two series alkaline AA/AAA cells. This voltage index is not measured SOC.
 * Change these compile-time thresholds and rebuild for another battery type;
 * the firmware never tries to identify chemistry from terminal voltage. */
#ifndef MAP_BATTERY_EMPTY_MV
#define MAP_BATTERY_EMPTY_MV 1800u
#endif
#ifndef MAP_BATTERY_FULL_MV
#define MAP_BATTERY_FULL_MV 3200u
#endif
#define MAP_BATTERY_VALID_MIN_MV 1000u
#define MAP_BATTERY_VALID_MAX_MV 5200u
_Static_assert(MAP_BATTERY_FULL_MV > MAP_BATTERY_EMPTY_MV, "Invalid battery indication range");
_Static_assert(MAP_BATTERY_EMPTY_MV >= MAP_BATTERY_VALID_MIN_MV, "Empty below valid input range");
_Static_assert(MAP_BATTERY_FULL_MV <= MAP_BATTERY_VALID_MAX_MV, "Full above valid input range");
#endif
