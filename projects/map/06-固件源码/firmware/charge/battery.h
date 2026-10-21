#ifndef MAP_BATTERY_H
#define MAP_BATTERY_H
#include <stdint.h>
#include <stdbool.h>
enum battery_state { BAT_WAITING, BAT_VALID, BAT_NO_VOLTAGE, BAT_RANGE_ERROR, BAT_ADC_ERROR };
struct battery_status {
    uint32_t samples,errors,raw_battery,raw_reference,factory_reference;
    uint32_t vdda_mv,voltage_mv,filtered_q8,percent,bars,state,updated_at;
};
bool battery_convert(uint32_t raw_battery,uint32_t raw_reference,uint32_t calibration,uint32_t *vdda_mv,uint32_t *battery_mv);
unsigned battery_percent(unsigned millivolts);
unsigned battery_bars(unsigned percent);
unsigned battery_ring(unsigned bars);
void battery_update(struct battery_status *b,bool adc_ok,uint32_t raw_battery,uint32_t raw_reference,uint32_t calibration,uint32_t now);
#endif
