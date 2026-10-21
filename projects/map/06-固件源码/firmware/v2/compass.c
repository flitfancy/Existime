#include "compass.h"
int angle_wrap(int x) { while(x<0)x+=36000; while(x>=36000)x-=36000; return x; }
int angle_delta(int a,int b) { int x=angle_wrap(a-b);return x>18000?x-36000:x; }
int compass_heading(int sensor_angle,int stored_north) {
    /* Sensor angles increase opposite to map-top clockwise headings. Keep
       north in the original sensor convention so existing calibration survives. */
    return angle_wrap(stored_north-sensor_angle);
}
int direction_index(int heading) {
    heading=angle_wrap(heading);
    int n=((heading+2250)/4500)&7;
    int d=angle_delta(heading,n*4500);
    return d>=-HEADING_WINDOW_CDEG&&d<=HEADING_WINDOW_CDEG?n:-1;
}
uint32_t integer_sqrt(uint32_t n) {
    uint32_t r=0,b=1u<<30;
    while(b>n)b>>=2;
    while(b){if(n>=r+b){n-=r+b;r=(r>>1)+b;}else r>>=1;b>>=2;}
    return r;
}
int atan2_cdeg(int32_t y,int32_t x) {
    /* CORDIC vectoring, angle in 1/100 degree; scale before shifting small inputs. */
    static const int16_t table[]={4500,2657,1404,713,358,179,90,45,22,11,6,3,1};
    if(!x&&!y)return 0;
    int angle=0;
    if(x<0){x=-x;y=-y;angle=18000;}
    x*=1024;y*=1024;
    for(unsigned i=0;i<sizeof(table)/sizeof(table[0]);++i){
        int32_t old=x;
        if(y>0){x+=y>>i;y-=old>>i;angle+=table[i];}
        else if(y<0){x-=y>>i;y+=old>>i;angle-=table[i];}
    }
    return angle_wrap(angle);
}
void collector_begin(struct collector *c,uint32_t now) {
    *c=(struct collector){0};c->started=now;
    for(unsigned i=0;i<3;++i){c->min[i]=32767;c->max[i]=-32768;}
}
void collector_sample(struct collector *c,const int32_t m[3],const int32_t a[3]) {
    int32_t norm=a[0]*a[0]+a[1]*a[1]+a[2]*a[2];
    if(norm<700*700||norm>1300*1300)return;
    for(unsigned i=0;i<3;++i)if(m[i]<=-32000||m[i]>=32000)return;
    for(unsigned i=0;i<3;++i){
        if(m[i]<c->min[i])c->min[i]=m[i];if(m[i]>c->max[i])c->max[i]=m[i];
        if(a[i]>800)c->faces|=1u<<(i*2);
        if(a[i]<-800)c->faces|=2u<<(i*2);
    }
    ++c->samples;
}
bool collector_finish(const struct collector *c,uint32_t now,struct calibration *out) {
    if((uint32_t)(now-c->started)<20000||c->samples<300||c->faces!=63)return false;
    int lo=32767,hi=0;
    struct calibration candidate={0};
    for(unsigned i=0;i<3;++i){
        int span=c->max[i]-c->min[i];
        if(span<180||span>2000)return false;
        candidate.bias[i]=(c->max[i]+c->min[i])/2;
        candidate.radius[i]=span/2;
        if(span<lo)lo=span;if(span>hi)hi=span;
    }
    if(hi>lo*3)return false;
    *out=candidate;return true;
}
bool compass_angle(const struct calibration *cal,const int32_t m[3],const int32_t a[3],int *angle) {
    int32_t n=a[0]*a[0]+a[1]*a[1]+a[2]*a[2];
    if(n<850*850||n>1150*1150)return false;
    uint32_t length=integer_sqrt((uint32_t)n);
    /* Top face up, within 15 degrees of level. Reference is aligned on that plane. */
    if(a[2]<0||(uint32_t)a[2]*1000u<length*966u)return false;
    int32_t g[3],v[3];
    for(unsigned i=0;i<3;++i){
        if(cal->radius[i]<90||cal->radius[i]>1000)return false;
        g[i]=a[i]*1024/(int32_t)length;
        int32_t delta=m[i]-cal->bias[i];
        /* Large disturbances/saturation are invalid, not an arbitrary heading. */
        if(delta>cal->radius[i]*3||delta<-cal->radius[i]*3)return false;
        v[i]=delta*1024/cal->radius[i];
    }
    int32_t dot=(v[0]*g[0]+v[1]*g[1]+v[2]*g[2])/1024;
    int32_t horizontal=v[0]-g[0]*dot/1024;
    int32_t sideways=(g[2]*v[1]-g[1]*v[2])/1024;
    if(horizontal*horizontal+sideways*sideways<100*100)return false;
    *angle=atan2_cdeg(sideways,horizontal);
    return true;
}
