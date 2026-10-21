#include "battery.h"
bool battery_convert(uint32_t raw,uint32_t ref,uint32_t cal,uint32_t *vdda,uint32_t *mv) {
    if(raw>4095||ref<300||ref>4095||cal<1000||cal>2000)return false;
    uint32_t supply=(3000u*cal+ref/2)/ref;
    if(supply<1900||supply>3700)return false;
    /* R57=R58=100k: battery voltage is twice the PA3 input voltage. */
    *vdda=supply;*mv=(raw*supply*2u+2047u)/4095u;
    return true;
}
unsigned battery_percent(unsigned mv) {
    /* A generic display policy for 4.2V lithium cells, not a characterized fuel gauge. */
    static const uint16_t voltage[]={3300,3500,3600,3670,3740,3800,3900,4000,4100,4200};
    static const uint8_t percent[]={0,5,10,20,40,60,75,85,95,100};
    if(mv<=voltage[0])return 0;
    for(unsigned i=1;i<sizeof(voltage)/sizeof(voltage[0]);++i)
        if(mv<voltage[i])return percent[i-1]+(mv-voltage[i-1])*(percent[i]-percent[i-1])/(voltage[i]-voltage[i-1]);
    return 100;
}
unsigned battery_bars(unsigned percent) {
    if(percent>100)percent=100;
    return (percent*8u+99u)/100u;
}
unsigned battery_ring(unsigned bars) {
    if(bars>8)bars=8;
    /* D42/NW=12.5%, filling towards D35/N=100%, matching the actual labels. */
    return bars?((0xffu<<(8u-bars))&0xffu):0;
}
void battery_update(struct battery_status *b,bool ok,uint32_t raw,uint32_t ref,uint32_t cal,uint32_t now) {
    b->raw_battery=raw;b->raw_reference=ref;b->factory_reference=cal;
    uint32_t supply,mv;
    if(!ok||!battery_convert(raw,ref,cal,&supply,&mv)){
        ++b->errors;b->state=BAT_ADC_ERROR;b->bars=0;b->percent=0;return;
    }
    ++b->samples;b->updated_at=now;b->vdda_mv=supply;b->voltage_mv=mv;
    enum battery_state state=mv<2300?BAT_NO_VOLTAGE:mv>4350?BAT_RANGE_ERROR:BAT_VALID;
    if(state!=BAT_VALID){b->state=state;b->filtered_q8=0;b->percent=0;b->bars=0;return;}
    /* Reset the filter when the input returns after disconnection or a measurement error. */
    b->filtered_q8=b->state!=BAT_VALID?mv*256u:(b->filtered_q8*3u+mv*256u+2u)/4u;
    b->state=state;b->percent=battery_percent((b->filtered_q8+128u)/256u);
    b->bars=battery_bars(b->percent);
}
