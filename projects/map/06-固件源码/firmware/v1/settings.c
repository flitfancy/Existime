#include "settings.h"
#define RECORD_MAGIC 0x3150414du /* MAP1 */
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
uint32_t record_crc(const uint8_t *p,unsigned n) {
    uint32_t crc=0xffffffffu;
    while(n--){crc^=*p++;for(unsigned i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u& (0u-(crc&1u)));}
    return ~crc;
}
void record_encode(uint8_t *p,const struct settings *s,uint32_t seq) {
    put32(p,RECORD_MAGIC);put32(p+4,1);put32(p+8,seq);
    put32(p+12,s->mask_lo);put32(p+16,s->mask_hi&3);put32(p+20,s->cal.valid);
    for(unsigned i=0;i<3;++i){put32(p+24+i*4,(uint32_t)s->cal.bias[i]);put32(p+36+i*4,(uint32_t)s->cal.radius[i]);}
    put32(p+48,(uint32_t)s->cal.north);put32(p+52,record_crc(p,52));
}
bool record_decode(const uint8_t *p,struct settings *s,uint32_t *seq) {
    if(get32(p)!=RECORD_MAGIC||get32(p+4)!=1||get32(p+52)!=record_crc(p,52))return false;
    struct settings t={0};t.mask_lo=get32(p+12);t.mask_hi=get32(p+16);t.cal.valid=get32(p+20);
    if(t.mask_hi>3||t.cal.valid>1)return false;
    for(unsigned i=0;i<3;++i){
        t.cal.bias[i]=(int32_t)get32(p+24+i*4);t.cal.radius[i]=(int32_t)get32(p+36+i*4);
        if(t.cal.valid&&(t.cal.radius[i]<90||t.cal.radius[i]>1000||t.cal.bias[i]<-32768||t.cal.bias[i]>32767))return false;
    }
    t.cal.north=(int32_t)get32(p+48);
    if(t.cal.north<0||t.cal.north>=36000)return false;
    *s=t;*seq=get32(p+8);return true;
}
