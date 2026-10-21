#include "app.h"
#define DEBOUNCE_MS 30u
void app_init(struct app *a,uint32_t lo,uint32_t hi) {
    *a=(struct app){0};a->mask_lo=lo;a->mask_hi=hi&3u;
}
void app_set_mode(struct app *a,enum mode mode,uint32_t now) {
    a->mode=mode;a->entered_at=now;
}
void app_return_normal(struct app *a,uint32_t now) {
    app_set_mode(a,NORMAL,now);
    a->chord=0;
    a->release_lock=1;
}
bool app_selected_on(const struct app *a) {
    return a->selected<32?!!(a->mask_lo&(1u<<a->selected)):!!(a->mask_hi&(1u<<(a->selected-32)));
}
bool app_center_on(const struct app *a,uint32_t now) {
    if(a->mode==NORMAL||a->mode==BATTERY_VIEW)return true;
    unsigned period=a->mode==MENU?500:a->mode==CAL_COLLECT?200:
        (a->mode==CAL_SETTLE||a->mode==CAL_SAVE)?100:250;
    return !!((now/period)&1u);
}
static unsigned set_selected(struct app *a,bool on) {
    bool old=app_selected_on(a);
    uint32_t *p=a->selected<32?&a->mask_lo:&a->mask_hi;
    uint32_t bit=1u<<(a->selected&31u);
    if(on)*p|=bit;else *p&=~bit;
    return old!=on?ACT_DIRTY:0;
}
unsigned app_tick(struct app *a,uint32_t now,unsigned raw) {
    unsigned actions=0;
    if(a->mode==BATTERY_VIEW&&(uint32_t)(now-a->entered_at)>=3000)app_set_mode(a,NORMAL,now);
    unsigned pressed=0,released=0;
    for(unsigned i=0;i<2;++i) {
        struct key_state *k=&a->key[i];unsigned v=(raw>>i)&1u;
        if(v!=k->raw) {k->raw=v;k->changed=now;}
        if(v!=k->down&&(uint32_t)(now-k->changed)>=DEBOUNCE_MS) {
            k->down=v;
            if(v) { k->down_at=now;k->fired=0;k->blocked=0;pressed|=1u<<i; }
            else released|=1u<<i;
        }
    }
    unsigned down=a->key[0].down|(a->key[1].down<<1);
    /* A calibration is successful only after EEPROM readback. Do not let a
       held/released key cancel that transaction or escape into another page. */
    if(a->mode==CAL_SAVE) {
        a->release_lock=1;
        return 0;
    }
    if(a->release_lock) {
        if(!down&&!raw)a->release_lock=0;
        return 0;
    }
    if(down==3) {
        if(!a->chord) {a->chord=1;a->chord_at=now;}
        a->key[0].blocked=a->key[1].blocked=1;
        if((a->mode==NORMAL||a->mode==BATTERY_VIEW)&&(uint32_t)(now-a->chord_at)>=3000) {
            app_set_mode(a,MENU,now);a->release_lock=1;a->chord=0;
        }
        return 0;
    }
    a->chord=0;
    /* A chord, including a short failed chord, never leaks single-key actions. */
    if(a->mode!=NORMAL&&a->mode!=BATTERY_VIEW&&a->key[0].down&&!a->key[0].blocked&&
       (uint32_t)(now-a->key[0].down_at)>=3000) {
        actions|=ACT_CANCEL;app_return_normal(a,now);return actions;
    }
    struct key_state *one=&a->key[1];
    if(one->down&&!one->blocked&&!one->fired&&(uint32_t)(now-one->down_at)>=1000) {
        one->fired=1;
        if(a->mode==LED_EDIT)actions|=set_selected(a,true);
        else if(a->mode==CAL_COLLECT)actions|=ACT_CAL_FINISH;
        else if(a->mode==CAL_NORTH)actions|=ACT_NORTH;
    }
    for(unsigned i=0;i<2;++i)if(released&(1u<<i)) {
        struct key_state *k=&a->key[i];
        if(k->blocked||k->fired)continue;
        uint32_t held=now-k->down_at;
        if((a->mode==NORMAL||a->mode==BATTERY_VIEW)&&i==0&&held<1000) {
            app_set_mode(a,BATTERY_VIEW,now);
        } else if(a->mode==MENU&&held<1000) {
            if(i==0){app_set_mode(a,CAL_COLLECT,now);actions|=ACT_CAL_BEGIN;}
            else app_set_mode(a,LED_EDIT,now);
        } else if(a->mode==LED_EDIT) {
            if(i==0&&held>=1000)actions|=set_selected(a,false);
            else if(held<1000)a->selected=(a->selected+(i?1:33))%34;
        }
    }
    (void)pressed;
    return actions;
}
