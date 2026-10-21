#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "app.h"
#include "compass.h"
#include "settings.h"
#include "board_generated.h"
#include "battery.h"
#include "calibration.h"
#include "display.h"
#include "leds.h"
#include "sensors.h"
#include "center_pwm.h"
#include "charger.h"

static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t now;
static unsigned feed(struct app *a,unsigned keys,unsigned duration){unsigned result=0;while(duration--)result|=app_tick(a,++now,keys);return result;}
static unsigned tap(struct app *a,unsigned key,unsigned duration){unsigned r=feed(a,key,duration);return r|feed(a,0,50);}
static void tests_buttons(void){
    struct app a;app_init(&a,0,0);now=0;
    feed(&a,3,3000);CHECK(a.mode==NORMAL);feed(&a,3,60);CHECK(a.mode==MENU);
    feed(&a,0,80);CHECK(a.mode==MENU);CHECK(!a.mask_lo);
    tap(&a,2,100);CHECK(a.mode==LED_EDIT);CHECK(a.selected==0);
    tap(&a,2,100);CHECK(a.selected==1);CHECK(!a.mask_lo);
    tap(&a,1,100);CHECK(a.selected==0);
    tap(&a,1,100);CHECK(a.selected==33);
    unsigned r=tap(&a,2,1100);CHECK(r&ACT_DIRTY);CHECK(a.mask_hi==2);CHECK(a.selected==33);
    r=tap(&a,1,1100);CHECK(r&ACT_DIRTY);CHECK(a.mask_hi==0);CHECK(a.selected==33);
    tap(&a,2,1100);CHECK(a.mask_hi==2);
    r=tap(&a,1,3100);CHECK(a.mode==NORMAL);CHECK(a.mask_hi==2);CHECK(!(r&ACT_DIRTY));
    /* Chord cancellation does not navigate or write even after release skew. */
    app_set_mode(&a,LED_EDIT,now);a.selected=4;
    feed(&a,1,100);feed(&a,3,1200);feed(&a,2,200);feed(&a,0,100);
    CHECK(a.selected==4);CHECK(a.mask_hi==2);CHECK(a.mask_lo==0);
    /* Contact bounce cannot produce a click. */
    for(unsigned i=0;i<20;++i){feed(&a,2,5);feed(&a,0,5);}CHECK(a.selected==4);
    /* A long key 1 never produces a short navigation on release. */
    tap(&a,2,2000);CHECK(a.selected==4);CHECK(a.mask_lo==16);
    /* Three-second exit works in every editor and never fires collection finish. */
    for(unsigned m=MENU;m<=CAL_SETTLE;++m){app_set_mode(&a,(enum mode)m,now);r=tap(&a,1,3200);CHECK(a.mode==NORMAL);CHECK(r&ACT_CANCEL);}
    /* Failed, short chord in normal mode must not leave latent actions. */
    feed(&a,3,1000);feed(&a,0,100);CHECK(a.mode==NORMAL);
    feed(&a,3,3100);feed(&a,0,100);CHECK(a.mode==MENU);
    r=tap(&a,1,100);CHECK(a.mode==CAL_COLLECT);CHECK(r&ACT_CAL_BEGIN);
    r=tap(&a,2,1100);CHECK(r&ACT_CAL_FINISH);CHECK(a.mode==CAL_COLLECT);
    app_set_mode(&a,CAL_NORTH,now);r=tap(&a,2,1100);CHECK(r&ACT_NORTH);
    /* Unsigned timer wrap: hold crossing 0xffffffff still takes three seconds. */
    app_init(&a,0,0);now=0xfffffc00u;feed(&a,3,3000);CHECK(a.mode==NORMAL);feed(&a,3,60);CHECK(a.mode==MENU);
    /* Every province index, including both high bits, can be set and cleared. */
    app_init(&a,0,0);app_set_mode(&a,LED_EDIT,now);
    for(unsigned i=0;i<34;++i){a.selected=i;tap(&a,2,1100);CHECK(app_selected_on(&a));tap(&a,1,1100);CHECK(!app_selected_on(&a));}
}
static void tests_angles(void){
    for(int h=0;h<36000;++h){
        int d=direction_index(h),n=((h+2250)/4500)&7,delta=abs(angle_delta(h,n*4500));
        CHECK(d==(delta<=300?n:-1));
    }
    for(int i=0;i<3600;++i){
        double a=i*3.141592653589793/1800;
        int x=(int)lround(cos(a)*5000),y=(int)lround(sin(a)*5000);
        CHECK(abs(angle_delta(atan2_cdeg(y,x),i*10))<=6);
    }
    CHECK(integer_sqrt(0)==0);CHECK(integer_sqrt(1000000)==1000);CHECK(integer_sqrt(0xffffffffu)==65535);
    /* Use physical clockwise bearings with existing raw-sensor north anchors,
       including the actual saved 4-degree anchor and zero-degree wrap. */
    const int anchors[]={0,400,35900};
    for(unsigned n=0;n<sizeof(anchors)/sizeof(anchors[0]);++n){
        struct calibration saved={{-78,19,-127},{232,221,323},anchors[n],1};
        for(int bearing=0;bearing<8;++bearing){
            double raw=(anchors[n]-bearing*4500)*3.141592653589793/18000;
            int32_t field[3]={saved.bias[0]+(int32_t)lround(saved.radius[0]*cos(raw)),
                              saved.bias[1]+(int32_t)lround(saved.radius[1]*sin(raw)),saved.bias[2]};
            int32_t gravity[3]={0,0,1000};int sensor;
            CHECK(compass_angle(&saved,field,gravity,&sensor));
            int clockwise=compass_heading(sensor,saved.north);
            CHECK(abs(angle_delta(clockwise,bearing*4500))<=25);
            CHECK(direction_index(clockwise)==bearing);
        }
    }
    CHECK(compass_heading(400,400)==0);
    CHECK(compass_heading(27400,400)==9000);  /* physical east */
    CHECK(compass_heading(9400,400)==27000);  /* physical west */
    CHECK(compass_heading(18400,400)==18000);
    CHECK(direction_index(300)==0&&direction_index(301)==-1);
    CHECK(direction_index(35700)==0&&direction_index(35699)==-1);
    struct calibration cal={{100,-40,20},{300,250,400},0,1};
    int32_t a[3]={0,0,1000},m[3]={400,-40,20};int heading;
    CHECK(compass_angle(&cal,m,a,&heading));CHECK(abs(angle_delta(heading,0))<6);
    m[0]=100;m[1]=210;CHECK(compass_angle(&cal,m,a,&heading));CHECK(abs(angle_delta(heading,9000))<6);
    a[2]=0;a[0]=1000;CHECK(!compass_angle(&cal,m,a,&heading));
    a[0]=0;a[2]=2000;CHECK(!compass_angle(&cal,m,a,&heading));
    a[2]=1000;m[0]=32000;CHECK(!compass_angle(&cal,m,a,&heading));
    struct collector c;collector_begin(&c,0);struct calibration out;
    CHECK(!collector_finish(&c,30000,&out));
    for(unsigned i=0;i<360;++i){
        unsigned axis=i%3;int sign=(i%6<3)?1:-1;
        int32_t g[3]={0},field[3]={100,-40,20};g[axis]=sign*1000;field[axis]+=sign*(int[]){300,250,400}[axis];
        collector_sample(&c,field,g);
    }
    CHECK(c.faces==63);CHECK(!collector_finish(&c,19999,&out));CHECK(collector_finish(&c,20000,&out));
    CHECK(out.bias[0]==100&&out.bias[1]==-40&&out.bias[2]==20);
    CHECK(out.radius[0]==300&&out.radius[1]==250&&out.radius[2]==400);
    c.faces=31;CHECK(!collector_finish(&c,30000,&out));
    /* Synthetic magnetic field with inclination, all headings, roll/pitch +/-10deg. */
    struct calibration sphere={{0,0,0},{1000,1000,1000},0,1};
    for(int yaw=0;yaw<360;yaw+=3)for(int roll=-10;roll<=10;roll+=10)for(int pitch=-10;pitch<=10;pitch+=10){
        double r=roll*3.141592653589793/180,p=pitch*3.141592653589793/180,y=yaw*3.141592653589793/180;
        double sr=sin(r),cr=cos(r),sp=sin(p),cp=cos(p),sy=sin(y),cy=cos(y);
        double rz[3]={-sp,cp*sr,cp*cr};
        double rx[3]={cy*cp,cy*sp*sr+sy*cr,cy*sp*cr-sy*sr};
        int32_t gravity[3],field[3];
        for(unsigned j=0;j<3;++j){gravity[j]=(int32_t)lround(1000*rz[j]);field[j]=(int32_t)lround(1000*rx[j]-500*rz[j]);}
        CHECK(compass_angle(&sphere,field,gravity,&heading));CHECK(abs(angle_delta(heading,yaw*100))<=30);
    }
}

static uint8_t eeprom[256];
static int bytes_remaining=-1;
static bool busy,io_fail;
static uint8_t mock_mag[128],mock_acc[128],mock_leds[5][4];
static bool sensor_fail,led_fail_once;
static unsigned mock_direct,mock_center_duty,led_writes[5];
static uint32_t mock_us;
uint32_t micros(void){return mock_us;}
void direct_leds(unsigned mask){mock_direct=mask;}
void center_led_duty(unsigned duty){CHECK(duty<=255);mock_center_duty=duty;}
bool i2c_read(uint8_t addr,uint8_t reg,uint8_t *data,unsigned count){
    if(addr==0x19||addr==0x1e){
        if(sensor_fail)return false;
        uint8_t *regs=addr==0x19?mock_acc:mock_mag;
        reg&=0x7f;CHECK(reg+count<=128);memcpy(data,regs+reg,count);
        if(count==6)regs[addr==0x19?0x27:0x67]=0;
        return true;
    }
    CHECK(addr==0x50);if(io_fail)return false;CHECK(reg+count<=256);memcpy(data,eeprom+reg,count);return true;
}
bool read_reg(uint8_t addr,uint8_t reg,uint8_t *value){
    if(addr==0x19||addr==0x1e)return i2c_read(addr,reg,value,1);
    CHECK(addr>=0x20&&addr<=0x24&&reg<4);
    if(led_fail_once){led_fail_once=false;return false;}
    *value=mock_leds[addr-0x20][reg];mock_us+=10;return true;
}
bool write_reg(uint8_t addr,uint8_t reg,uint8_t value){
    if(addr==0x19||addr==0x1e){
        if(sensor_fail)return false;
        CHECK(reg<128);(addr==0x19?mock_acc:mock_mag)[reg]=value;return true;
    }
    CHECK(addr>=0x20&&addr<=0x24&&reg<4);
    mock_us+=200;
    if(led_fail_once){led_fail_once=false;return false;}
    ++led_writes[addr-0x20];mock_leds[addr-0x20][reg]=value;
    if(reg==1)mock_leds[addr-0x20][0]=value;
    return true;
}
bool i2c_ready(uint8_t address){CHECK(address==0x50);if(io_fail)return false;if(busy){busy=false;return false;}return true;}
bool i2c_write(uint8_t addr,uint8_t reg,const uint8_t *data,unsigned count){
    CHECK(addr==0x50);CHECK((reg&7u)+count<=8);CHECK(reg+count<=256);
    if(io_fail)return false;
    for(unsigned i=0;i<count;++i){if(bytes_remaining==0){io_fail=true;return false;}eeprom[reg+i]=data[i];if(bytes_remaining>0)--bytes_remaining;}
    busy=true;return true;
}
static void tests_storage(void){
    uint8_t encoded[56],corrupt[56];struct settings in={0},out;uint32_t seq;
    in.mask_lo=0x13579bdf;in.mask_hi=3;in.cal=(struct calibration){{-100,40,-20},{300,250,400},35999,1};
    record_encode(encoded,&in,0xffffffffu);CHECK(record_decode(encoded,&out,&seq));CHECK(seq==0xffffffffu);CHECK(memcmp(&in,&out,sizeof(in))==0);
    for(unsigned bit=0;bit<448;++bit){memcpy(corrupt,encoded,56);corrupt[bit/8]^=1u<<(bit%8);CHECK(!record_decode(corrupt,&out,&seq));}
    /* Power interruption at every byte of the complete 60-byte transaction. */
    for(int cut=0;cut<=60;++cut){
        struct settings old={0},older={0},current={0};old.mask_lo=7;older.mask_lo=3;current.mask_lo=9;
        memset(eeprom,255,256);record_encode(eeprom,&old,10);record_encode(eeprom+128,&older,9);
        bytes_remaining=-1;io_fail=false;busy=false;
        struct storage s;storage_load(&s,&out);CHECK(out.mask_lo==7);CHECK(s.sequence==10);
        bytes_remaining=cut;storage_request(&s);
        for(unsigned ms=0;ms<300&&!io_fail;++ms)storage_tick(&s,&current,ms);
        io_fail=false;busy=false;bytes_remaining=-1;
        struct storage restored;storage_load(&restored,&out);
        CHECK(out.mask_lo==(cut==60?9u:7u));CHECK(restored.sequence==(cut==60?11u:10u));
    }
    /* New edits while a save is active remain dirty and get a second transaction. */
    memset(eeprom,255,256);struct storage s;struct settings current={0};storage_load(&s,&out);
    CHECK(!s.loaded);current.mask_lo=1;storage_request(&s);storage_tick(&s,&current,0);
    current.mask_lo=2;storage_request(&s);
    for(unsigned ms=1;ms<1000;++ms)storage_tick(&s,&current,ms);
    CHECK(s.successes==2);CHECK(!s.dirty);storage_load(&s,&out);CHECK(out.mask_lo==2);
    /* Sequence wrap chooses 0 over 0xffffffff. */
    current.mask_lo=3;record_encode(eeprom,&current,0xffffffffu);current.mask_lo=4;record_encode(eeprom+128,&current,0);
    storage_load(&s,&out);CHECK(out.mask_lo==4);CHECK(s.sequence==0);
    /* NACK timeout preserves the previously accepted record. */
    storage_request(&s);io_fail=true;storage_tick(&s,&current,0);CHECK(s.errors==1);CHECK(s.dirty);CHECK(!s.phase);io_fail=false;
}
static void tests_battery(void){
    uint32_t supply,voltage;
    CHECK(!battery_convert(2000,0,1500,&supply,&voltage));
    CHECK(!battery_convert(2000,1364,0xffff,&supply,&voltage));
    CHECK(!battery_convert(4096,1364,1500,&supply,&voltage));
    CHECK(!battery_convert(2000,100,1500,&supply,&voltage));
    for(unsigned vdda=2000;vdda<=3600;vdda+=100)for(unsigned bat=1500;bat<=5200;bat+=10){
        if(bat>vdda*2)continue;
        unsigned raw=(bat*4095u+vdda)/(vdda*2),ref=(4500000u+vdda/2)/vdda;
        CHECK(battery_convert(raw,ref,1500,&supply,&voltage));
        CHECK(abs((int)supply-(int)vdda)<=2);CHECK(abs((int)voltage-(int)bat)<=4);
    }
    unsigned previous=0;
    for(unsigned mv=0;mv<=6000;++mv){unsigned percent=battery_percent(mv);CHECK(percent>=previous&&percent<=100);previous=percent;}
    CHECK(battery_percent(1800)==0);CHECK(battery_percent(2500)==50);CHECK(battery_percent(3200)==100);CHECK(battery_percent(5000)==100);
    CHECK(battery_bars(0)==0);CHECK(battery_bars(1)==1);CHECK(battery_bars(12)==1);CHECK(battery_bars(13)==2);CHECK(battery_bars(100)==8);
    CHECK(battery_ring(0)==0);CHECK(battery_ring(1)==0x80);CHECK(battery_ring(4)==0xf0);CHECK(battery_ring(8)==255);
    struct battery_status b={0};
    battery_update(&b,true,0,1364,1500,100);CHECK(b.state==BAT_NO_VOLTAGE);CHECK(b.samples==1);CHECK(b.errors==0);
    battery_update(&b,true,1551,1364,1500,1100);CHECK(b.state==BAT_VALID);CHECK(b.percent>=45&&b.percent<=55);
    unsigned old=b.filtered_q8;battery_update(&b,true,1700,1364,1500,2100);CHECK(b.filtered_q8>old);CHECK(b.filtered_q8<b.voltage_mv*256u);
    battery_update(&b,false,0,0,0,3100);CHECK(b.state==BAT_ADC_ERROR);CHECK(b.percent==0&&b.bars==0);CHECK(b.errors==1);
    battery_update(&b,true,1551,1364,1500,4100);CHECK(b.state==BAT_VALID);CHECK(b.filtered_q8==b.voltage_mv*256u);
    battery_update(&b,true,3500,1364,1500,5100);CHECK(b.state==BAT_RANGE_ERROR);CHECK(b.percent==0);
    /* The old lithium validity policy must not reject the 1.8V input end. */
    for(unsigned mv=1800;mv<=5000;mv+=100){
        struct battery_status low={0};unsigned raw=(mv*4095u+3300u)/6600u;
        battery_update(&low,true,raw,1364,1500,mv);
        CHECK(low.state==BAT_VALID);CHECK(low.bars<=8);
    }
    struct app a;app_init(&a,0xa5a5a5a5u,2);now=0;
    tap(&a,1,100);CHECK(a.mode==BATTERY_VIEW);CHECK(a.mask_lo==0xa5a5a5a5u&&a.mask_hi==2);
    feed(&a,0,2900);CHECK(a.mode==BATTERY_VIEW);feed(&a,0,100);CHECK(a.mode==NORMAL);
    tap(&a,1,1100);CHECK(a.mode==NORMAL); /* Long 0 is not a battery-view shortcut. */
    tap(&a,1,100);CHECK(a.mode==BATTERY_VIEW);feed(&a,3,3100);CHECK(a.mode==MENU);feed(&a,0,100);CHECK(a.mode==MENU);
    for(unsigned m=NORMAL;m<=BATTERY_VIEW;++m){
        a.mode=(enum mode)m;unsigned on=0,off=0;
        for(unsigned t=0;t<2000;t+=25)if(app_center_on(&a,t))++on;else ++off;
        if(m==NORMAL||m==BATTERY_VIEW)CHECK(on==80&&off==0);else CHECK(on>0&&off>0);
    }
}
static void tests_calibration_flow(void){
    struct settings settings={0},restored;
    settings.mask_lo=16;settings.mask_hi=2;
    settings.cal=(struct calibration){{10,20,30},{300,300,300},1234,1};
    memset(eeprom,255,sizeof(eeprom));record_encode(eeprom,&settings,40);
    io_fail=false;busy=false;bytes_remaining=-1;
    struct storage store;storage_load(&store,&settings);
    struct app a;app_init(&a,settings.mask_lo,settings.mask_hi);
    struct calibration_flow c={0};
    now=0;app_set_mode(&a,CAL_COLLECT,now);
    calibration_action(&c,&a,ACT_CAL_BEGIN,now);
    for(unsigned i=0;i<360;++i){
        unsigned axis=i%3;int sign=i%6<3?1:-1;
        int32_t g[3]={0},m[3]={100,-40,20};
        g[axis]=sign*1000;m[axis]+=sign*(int[]){300,250,400}[axis];
        calibration_sample(&c,&a,&settings,&store,m,g,500+i*50,0);
    }
    CHECK(c.collector.faces==63&&c.collector.samples==360);
    calibration_action(&c,&a,ACT_CAL_FINISH,20000);CHECK(a.mode==CAL_NORTH);
    CHECK(settings.cal.north==1234&&!store.dirty);
    /* Cancelling collection/alignment must retain the previous calibration. */
    CHECK(calibration_action(&c,&a,ACT_CANCEL,20001));CHECK(settings.cal.north==1234);
    calibration_action(&c,&a,ACT_NORTH,21000);CHECK(a.mode==CAL_SETTLE);
    const int32_t mag[3]={400,-40,20},flat[3]={0,0,1000},upright[3]={1000,0,0};
    calibration_sample(&c,&a,&settings,&store,mag,upright,21350,0);CHECK(!c.settle_count);
    calibration_sample(&c,&a,&settings,&store,mag,flat,21400,0);CHECK(c.settle_count==1);
    calibration_sample(&c,&a,&settings,&store,mag,flat,21450,2);CHECK(!c.settle_count);
    for(unsigned i=0;i<39;++i){
        calibration_sample(&c,&a,&settings,&store,mag,flat,21500+i*50,0);
        CHECK(a.mode==CAL_SETTLE);
    }
    calibration_sample(&c,&a,&settings,&store,mag,flat,23450,0);
    CHECK(a.mode==CAL_SAVE&&store.dirty);CHECK(settings.cal.valid);
    CHECK(!calibration_tick(&c,&a,&store,23451));
    struct storage previous;storage_load(&previous,&restored);CHECK(restored.cal.north==1234);
    /* Holds and chords cannot claim success or enter another page while saving. */
    now=23451;CHECK(!feed(&a,1,3200));CHECK(a.mode==CAL_SAVE);
    CHECK(!feed(&a,3,3200));CHECK(a.mode==CAL_SAVE);
    io_fail=true;storage_tick(&store,&settings,now);CHECK(store.errors==1);
    CHECK(!calibration_tick(&c,&a,&store,now)&&a.mode==CAL_SAVE);
    io_fail=false;
    for(uint32_t t=now+10000;t<now+10300;++t)storage_tick(&store,&settings,t);
    CHECK(!store.dirty&&!store.phase&&store.successes==1);
    CHECK(calibration_tick(&c,&a,&store,now+10300));CHECK(a.mode==NORMAL&&a.release_lock);
    CHECK(!calibration_tick(&c,&a,&store,now+10301));
    CHECK(!feed(&a,2,100));CHECK(a.mode==NORMAL);
    CHECK(!feed(&a,0,100));CHECK(a.mode==NORMAL);
    storage_load(&previous,&restored);
    CHECK(restored.cal.valid&&abs(angle_delta(restored.cal.north,0))<6);
    CHECK(restored.mask_lo==16&&restored.mask_hi==2&&previous.sequence==41);
    CHECK(restored.cal.bias[0]==100&&restored.cal.radius[1]==250);
    tap(&a,1,100);CHECK(a.mode==BATTERY_VIEW); /* A subsequent deliberate click works. */
    app_set_mode(&a,CAL_SETTLE,0xfffffe00u);
    CHECK(!calibration_tick(&c,&a,&store,0xfffffe00u+15001u));CHECK(a.mode==CAL_NORTH);
}

static void tests_display(void){
    const unsigned fills[9]={0,0x80,0xc0,0xe0,0xf0,0xf8,0xfc,0xfe,0xff};
    struct app a;app_init(&a,0xa5a55a5au,2);
    struct calibration_flow c={0};struct display_frame f,normal;
    struct battery_status b={0};b.state=BAT_VALID;
    display_build(&normal,&a,&b,&c,-1,false,false,false,0);
    app_set_mode(&a,BATTERY_VIEW,0);
    for(unsigned bars=0;bars<=8;++bars)for(unsigned charge=0;charge<=1;++charge){
        b.bars=bars;
        for(unsigned step=0;step<=8;++step){
            uint32_t t=step*BATTERY_REVEAL_MS;b.updated_at=t;
            display_build(&f,&a,&b,&c,-1,false,charge,false,t);
            unsigned n=step<bars?step:bars,expected=fills[n];
            unsigned next=charge&&n==bars?(bars<8?0x80u>>bars:1u):0;
            if(next)expected&=~next;
            else if(!bars&&!charge&&((t/250)&1u))expected=0x80;
            CHECK(((unsigned)(uint8_t)~f.output[0])==expected);
            CHECK(f.breath_mask==next);CHECK(f.revealed_bars==n);CHECK(f.direct&4);
            for(unsigned i=1;i<5;++i)CHECK(f.output[i]==normal.output[i]);
        }
    }
    b.bars=3;b.updated_at=99;
    display_build(&f,&a,&b,&c,-1,false,false,false,99);CHECK(f.output[0]==255);
    b.updated_at=100;display_build(&f,&a,&b,&c,-1,false,false,false,100);CHECK(f.output[0]==0x7f);
    b.updated_at=350;display_build(&f,&a,&b,&c,-1,false,true,false,350);
    CHECK((uint8_t)~f.output[0]==0xe0&&f.breath_mask==0x10);
    display_build(&f,&a,&b,&c,-1,false,false,false,350);CHECK(!f.breath_mask);
    b.state=BAT_ADC_ERROR;display_build(&f,&a,&b,&c,-1,false,true,false,350);
    CHECK(!f.breath_mask&&f.output[0]==0); /* Invalid measurement: error pattern, no made-up capacity. */
    b.state=BAT_VALID;b.updated_at=0;
    display_build(&f,&a,&b,&c,-1,false,true,false,2750);CHECK(!f.breath_mask);
    app_set_mode(&a,BATTERY_VIEW,0xffffffe0u);b.updated_at=0xffffffe0u+100u;
    display_build(&f,&a,&b,&c,-1,false,false,false,b.updated_at);CHECK(f.output[0]==0x7f);
    unsigned last=0;
    for(unsigned t=0;t<=BREATH_PERIOD_MS/2;++t){unsigned level=breath_duty(t);CHECK(level>=last&&level<=255);last=level;}
    for(unsigned t=BREATH_PERIOD_MS/2;t<=BREATH_PERIOD_MS;++t){unsigned level=breath_duty(t);CHECK(level<=last);last=level;}
    CHECK(breath_duty(0)==0&&breath_duty(800)==255&&breath_duty(1600)==0);
    const unsigned levels[]={0,1,32,127,128,254,255};
    f=(struct display_frame){{0x1f,255,255,255,255},4,0x10,0,3,255};
    for(unsigned j=0;j<sizeof(levels)/sizeof(levels[0]);++j){
        f.breath_duty=(uint8_t)levels[j];unsigned on=0;
        for(unsigned us=0;us<RING_PWM_PERIOD_US;++us){
            unsigned ring=display_ring(&f,us);CHECK((ring&0xe0)==0xe0);CHECK(!(ring&0x0f));
            if(ring&0x10)++on;
        }
        CHECK(on==levels[j]*RING_PWM_PERIOD_US/255u);
    }
    f.breath_duty=128;
    CHECK(display_next_edge(&f,0)==5019);CHECK(display_next_edge(&f,5019)==4981);
    f.breath_mask=0;CHECK(display_next_edge(&f,1234)==UINT32_MAX);
    app_set_mode(&a,NORMAL,0);
    for(int direction=0;direction<8;++direction){
        display_build(&f,&a,&b,&c,direction,true,false,false,0);
        CHECK((uint8_t)~f.output[0]==(1u<<direction));CHECK(!f.breath_mask&&f.direct&4);
    }
    display_build(&f,&a,&b,&c,0,false,false,false,0);CHECK(f.output[0]==255);
}

static void tests_center_charging(void){
    struct app a;app_init(&a,0x103u,2);
    struct calibration_flow c={0};struct battery_status b={0};b.state=BAT_VALID;b.bars=3;
    struct display_frame f,reference;
    display_build(&reference,&a,&b,&c,2,true,false,false,0);
    const unsigned times[]={0,800,1600,2400,3200};
    const unsigned levels[]={0,63,255,63,0};
    CHECK(CENTER_BREATH_PERIOD_MS==3200u);
    for(unsigned i=0;i<5;++i){
        b.updated_at=times[i];
        display_build(&f,&a,&b,&c,2,true,true,false,times[i]);
        CHECK(f.center_duty==levels[i]&&f.direct&4);
        CHECK(f.output[0]==reference.output[0]);
        for(unsigned dev=1;dev<5;++dev)CHECK(f.output[dev]==reference.output[dev]);
    }
    for(unsigned mode=NORMAL;mode<=CAL_SAVE;++mode){
        a.mode=(enum mode)mode;
        for(unsigned t=0;t<=6400;t+=20){
            b.updated_at=t;
            display_build(&f,&a,&b,&c,-1,false,true,false,t);
            if(mode!=NORMAL&&mode!=BATTERY_VIEW){
                CHECK(f.center_duty==0||f.center_duty==255);
                CHECK(f.center_duty==((f.direct&4)?255:0));
            }
            display_build(&f,&a,&b,&c,-1,false,false,false,t);
            CHECK(f.center_duty==((f.direct&4)?255:0));
            if(mode==NORMAL||mode==BATTERY_VIEW)CHECK(f.center_duty==255);
        }
    }
    a.mode=NORMAL;
    display_build(&f,&a,&b,&c,-1,false,true,false,1600);CHECK(f.center_duty==255);
    a.mode=BATTERY_VIEW; /* Same timestamp, no center phase jump between pages. */
    display_build(&f,&a,&b,&c,-1,false,true,false,1600);CHECK(f.center_duty==255);
    a.mode=CAL_SAVE;
    for(unsigned t=0;t<400;t+=20){
        display_build(&f,&a,&b,&c,-1,false,true,true,t);
        CHECK(f.center_duty==(((t/100)&1u)?255:0));
    }
    CHECK(center_pwm_compare(0)==0&&center_pwm_compare(255)==1000);
    CHECK(center_pwm_compare(128)==502&&center_pwm_compare(256)==1000);
    unsigned previous=0;
    for(unsigned duty=0;duty<=255;++duty){
        unsigned compare=center_pwm_compare(duty);
        CHECK(compare>=previous&&compare<=1000);previous=compare;
    }
}

static void tests_led_transport(void){
    memset(mock_leds,0,sizeof(mock_leds));memset(led_writes,0,sizeof(led_writes));
    mock_us=0;led_fail_once=false;
    struct leds l;leds_init(&l);CHECK(l.known==31&&mock_direct==0);
    for(unsigned i=0;i<5;++i)leds_verify_next(&l);CHECK(l.verified==31);
    struct display_frame f={{0x1f,0xfe,0xfd,0xfb,0xf7},5,0x10,128,3,255};
    leds_set_frame(&l,&f,mock_us);unsigned initial_writes[5];
    for(unsigned i=0;i<5;++i)initial_writes[i]=led_writes[i];
    uint32_t until=mock_us+120000;
    while(mock_us<until){
        leds_service(&l);CHECK(((uint8_t)~mock_leds[0][1]&0xe0)==0xe0);mock_us+=100;
    }
    CHECK(l.pwm_writes>15&&mock_direct==5&&mock_center_duty==255);
    for(unsigned i=1;i<5;++i){CHECK(mock_leds[i][1]==f.output[i]);CHECK(led_writes[i]==initial_writes[i]+1);}
    f.breath_mask=0;f.breath_duty=0;f.output[0]=255;
    leds_set_frame(&l,&f,mock_us);leds_service(&l);CHECK(mock_leds[0][1]==255);
    for(unsigned i=0;i<5;++i)initial_writes[i]=led_writes[i];
    f.center_duty=37;leds_set_frame(&l,&f,mock_us);leds_service(&l);
    CHECK(mock_center_duty==37&&l.center_duty==37&&mock_direct==5);
    for(unsigned i=0;i<5;++i)CHECK(led_writes[i]==initial_writes[i]);
    f.center_duty=255;leds_set_frame(&l,&f,mock_us);leds_service(&l);CHECK(mock_center_duty==255);
    /* NACKs clear verification, preserve desired frame and retry at a bounded rate. */
    f.output[2]=0xf0;led_fail_once=true;
    leds_set_frame(&l,&f,mock_us);leds_service(&l);CHECK(l.errors==1&&!(l.known&4));
    unsigned writes=led_writes[2];leds_service(&l);CHECK(led_writes[2]==writes);
    mock_us+=10000;leds_service(&l);CHECK(l.known&4);CHECK(mock_leds[2][1]==0xf0);
    mock_leds[0][3]=255;l.verify_index=0;leds_verify_next(&l);CHECK(!(l.verified&1));
    for(unsigned i=0;i<5;++i)leds_verify_next(&l);CHECK(l.verified==31);
    /* No expired, unused retry deadline may block output after half-range wrap. */
    mock_us=0x80001000u;f.output[1]=0x55;
    leds_set_frame(&l,&f,mock_us);leds_service(&l);CHECK(mock_leds[1][1]==0x55);
    mock_us=0xfffffff0u;f.output[1]=0xaa;led_fail_once=true;
    leds_set_frame(&l,&f,mock_us);leds_service(&l);CHECK(!(l.known&2));
    mock_us+=11000;leds_service(&l);CHECK(mock_leds[1][1]==0xaa&&l.known&2);
}

static void sensor_vector(uint8_t *regs,unsigned base,int x,int y,int z,int scale){
    const int v[3]={x,y,z};
    for(unsigned i=0;i<3;++i){uint16_t raw=(uint16_t)(int16_t)(v[i]*scale);regs[base+2*i]=(uint8_t)raw;regs[base+2*i+1]=(uint8_t)(raw>>8);}
}
static void sensor_sample(struct sensors *s,const struct calibration *cal,uint32_t t,int mx,int my,int az){
    sensor_vector(mock_mag,0x68,mx,my,0,1);mock_mag[0x67]=8;
    sensor_vector(mock_acc,0x28,0,0,az,16);mock_acc[0x27]=8;
    sensors_poll(s,cal,t);
}
static void tests_sensors(void){
    memset(mock_mag,0,sizeof(mock_mag));memset(mock_acc,0,sizeof(mock_acc));sensor_fail=false;
    mock_mag[0x4f]=0x40;mock_acc[0x0f]=0x33;
    struct sensors s={0};sensors_init(&s);CHECK(s.mag_ready&&s.acc_ready);
    struct calibration cal={{0,0,0},{500,500,500},0,1};
    sensor_sample(&s,&cal,100,500,0,1000);CHECK(s.heading_valid&&s.direction==-1);
    sensor_sample(&s,&cal,150,500,0,1000);CHECK(s.direction==-1);
    sensor_sample(&s,&cal,200,500,0,1000);CHECK(s.direction==0);
    /* Actual board feedback: negative sensor-Y corresponds to physical east. */
    sensor_sample(&s,&cal,250,0,-500,1000);CHECK(s.direction==-1);
    for(unsigned i=0;i<30;++i)sensor_sample(&s,&cal,300+i*50,0,-500,1000);
    CHECK(s.direction==2&&abs(angle_delta(s.heading,9000))<=300);
    for(unsigned i=0;i<30;++i)sensor_sample(&s,&cal,1800+i*50,0,500,1000);
    CHECK(s.direction==6&&abs(angle_delta(s.heading,27000))<=300);
    for(unsigned i=0;i<30;++i)sensor_sample(&s,&cal,3300+i*50,-500,0,1000);
    CHECK(s.direction==4&&abs(angle_delta(s.heading,18000))<=300);
    for(unsigned i=0;i<30;++i)sensor_sample(&s,&cal,4800+i*50,500,0,1000);
    CHECK(s.direction==0&&abs(angle_delta(s.heading,0))<=300);
    /* Raw tilt invalidates heading immediately, even while the filter is flat. */
    sensor_vector(mock_acc,0x28,1000,0,0,16);mock_acc[0x27]=8;
    sensors_poll(&s,&cal,6300);CHECK(!s.heading_valid&&s.direction==-1);
    sensors_reset_heading(&s);sensor_sample(&s,&cal,6350,500,0,1000);
    sensors_poll(&s,&cal,6700);CHECK(!s.heading_valid&&s.direction==-1);
    sensor_fail=true;sensors_poll(&s,&cal,6800);CHECK(s.mag_errors==1&&s.acc_errors==1);
    sensor_fail=false;sensors_reset_heading(&s);cal.valid=0;
    sensor_sample(&s,&cal,6850,500,0,1000);CHECK(!s.heading_valid&&s.direction==-1);
}

static void tests_charge_completion(void){
    /* Former pins, boot noise, elapsed times and timer wrap must never invent
       external charging or a completion notice on battery-only hardware. */
    const uint32_t times[]={0,100,1100,5000,0xfffffff0u,20};
    for(unsigned inputs=0;inputs<4;++inputs)for(unsigned i=0;i<sizeof(times)/sizeof(times[0]);++i){
        struct charger c={.phase=CHARGE_ACTIVE,.notice_pending=true,.notice_active=true,.completions=7};
        charger_update(&c,inputs&1u,inputs&2u,times[i]);
        CHECK(!charger_charging(&c));CHECK(!c.external_power);CHECK(!c.charge_signal);
        CHECK(c.phase==CHARGE_NO_INPUT);CHECK(c.completions==0);
        CHECK(!charger_notice_tick(&c,true,times[i]));CHECK(!charger_notice_remaining(&c,times[i]));
        CHECK(!c.notice_pending&&!c.notice_active);
    }
}

int main(void){
    tests_buttons();tests_angles();tests_storage();tests_battery();
    tests_calibration_flow();tests_display();tests_center_charging();tests_led_transport();tests_sensors();
    tests_charge_completion();
    printf("PASS %u assertions: buttons/chords, +/-3 degree clockwise heading, calibration/save/auto-return, CRC/power-cut storage, 2x alkaline voltage bars, v2 charge rejection, shared display/PWM, LED transport recovery and sensor freshness\n",checks);
    return 0;
}
