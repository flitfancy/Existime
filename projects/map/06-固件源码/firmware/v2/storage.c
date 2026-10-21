#include "settings.h"
#include "platform.h"
static bool due(uint32_t now,uint32_t target){return (int32_t)(now-target)>=0;}
void storage_load(struct storage *s,struct settings *out) {
    *s=(struct storage){0};*out=(struct settings){0};
    for(unsigned slot=0;slot<2;++slot){
        struct settings candidate;uint32_t seq;
        if(!i2c_read(0x50,(uint8_t)(slot*128),s->bytes,56)) {++s->errors;continue;}
        if(!record_decode(s->bytes,&candidate,&seq))continue;
        if(!s->loaded||(int32_t)(seq-s->sequence)>0){*out=candidate;s->sequence=seq;s->slot=slot;s->loaded=1;}
    }
    s->saved=*out;
}
void storage_request(struct storage *s){s->dirty=1;++s->generation;}
static void fail(struct storage *s,uint32_t now){++s->errors;s->phase=0;s->retry_at=now+10000;}
void storage_tick(struct storage *s,const struct settings *current,uint32_t now) {
    if(!s->phase){
        if(!s->dirty||!due(now,s->retry_at))return;
        record_encode(s->bytes,current,s->sequence+1);s->snapshot_generation=s->generation;
        s->address=s->loaded?(1-s->slot)*128:0;s->phase=1;
    }
    if(s->phase==1){
        static const uint8_t zero[4]={0};
        if(!i2c_write(0x50,(uint8_t)s->address,zero,4)){fail(s,now);return;}
        s->deadline=now+25;s->phase=2;return;
    }
    if(s->phase==2||s->phase==4||s->phase==6){
        if(!i2c_ready(0x50)){if(due(now,s->deadline))fail(s,now);return;}
        if(s->phase==2){s->position=4;s->phase=3;}
        else if(s->phase==4)s->phase=s->position<56?3:5;
        else s->phase=7;
        return;
    }
    if(s->phase==3){
        unsigned n=8-(s->position&7u); /* AT24C02C eight-byte page boundary. */
        if(n>56-s->position)n=56-s->position;
        if(!i2c_write(0x50,(uint8_t)(s->address+s->position),s->bytes+s->position,n)){fail(s,now);return;}
        s->position+=n;s->deadline=now+25;s->phase=4;return;
    }
    if(s->phase==5){
        if(!i2c_write(0x50,(uint8_t)s->address,s->bytes,4)){fail(s,now);return;}
        s->deadline=now+25;s->phase=6;return;
    }
    if(s->phase==7){
        uint8_t verify[56];uint32_t seq;struct settings candidate;
        if(!i2c_read(0x50,(uint8_t)s->address,verify,56)||!record_decode(verify,&candidate,&seq)){fail(s,now);return;}
        for(unsigned i=0;i<56;++i)if(verify[i]!=s->bytes[i]){fail(s,now);return;}
        s->sequence=seq;s->slot=s->address/128;s->loaded=1;s->saved=candidate;++s->successes;
        if(s->generation==s->snapshot_generation)s->dirty=0;
        s->phase=0;s->retry_at=now+200;
    }
}
