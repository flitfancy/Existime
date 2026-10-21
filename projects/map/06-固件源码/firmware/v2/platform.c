#include "platform.h"
#include "center_pwm.h"
/* STM32C011F4P6, map PCB1: PB6=SCL, PB7=SDA; PA6=D7, PA7=D8, PA8=D43. */
#define REG32(a) (*(volatile uint32_t *)(a))
#define GPIOA 0x50000000u
#define GPIOB 0x50000400u
#define RCC_IOPENR REG32(0x40021034u)
#define MODER(p) REG32((p) + 0x00u)
#define OTYPER(p) REG32((p) + 0x04u)
#define OSPEEDR(p) REG32((p) + 0x08u)
#define PUPDR(p) REG32((p) + 0x0cu)
#define IDR(p) REG32((p) + 0x10u)
#define BSRR(p) REG32((p) + 0x18u)
#define LED_MASK ((1u << 6) | (1u << 7) | (1u << 8))
#define DIRECT_LED_MASK ((1u << 6) | (1u << 7))
#define TIM1 0x40012c00u
#define TIM_REG(offset) REG32(TIM1 + (offset))
#define SCL (1u << 6)
#define SDA (1u << 7)

static void delay(uint32_t n) {
    while (n--) __asm__ volatile ("nop");
}
static void pause_bus(void) { delay(16); }
static void scl_low(void) { BSRR(GPIOB) = SCL << 16; }
static void sda_low(void) { BSRR(GPIOB) = SDA << 16; }
static void sda_release(void) { BSRR(GPIOB) = SDA; }
static bool scl_release(void) {
    BSRR(GPIOB) = SCL;
    for (uint32_t n = 0; n < 2000; ++n) {
        if (IDR(GPIOB) & SCL) return true;
        delay(8);
    }
    return false;
}
static bool start(void) {
    sda_release(); pause_bus();
    if (!scl_release()) return false;
    pause_bus();
    if (!(IDR(GPIOB) & SDA)) return false;
    sda_low(); pause_bus(); scl_low();
    return true;
}
static void stop(void) {
    scl_low(); sda_low(); pause_bus();
    (void)scl_release(); pause_bus(); sda_release(); pause_bus();
}
static bool write_byte(uint8_t b) {
    for (unsigned bit = 0; bit < 8; ++bit) {
        if (b & 0x80u) sda_release(); else sda_low();
        pause_bus();
        if (!scl_release()) return false;
        pause_bus(); scl_low(); b <<= 1;
    }
    sda_release(); pause_bus();
    if (!scl_release()) return false;
    pause_bus();
    bool ack = !(IDR(GPIOB) & SDA);
    scl_low();
    return ack;
}
static bool read_byte_with_ack(uint8_t *out, bool acknowledge) {
    uint8_t b = 0;
    sda_release();
    for (unsigned bit = 0; bit < 8; ++bit) {
        pause_bus();
        if (!scl_release()) return false;
        pause_bus(); b = (uint8_t)((b << 1) | !!(IDR(GPIOB) & SDA));
        scl_low();
    }
    if (acknowledge) sda_low(); else sda_release();
    pause_bus();
    if (!scl_release()) return false;
    pause_bus(); scl_low(); sda_release();
    *out = b;
    return true;
}
bool i2c_ready(uint8_t addr){bool ok=start()&&write_byte((uint8_t)(addr<<1));stop();return ok;}
bool i2c_write(uint8_t addr,uint8_t reg,const uint8_t *data,unsigned count){
 if(addr==0x50){BSRR(GPIOA)=1u<<(2+16);pause_bus();}
 bool ok=start()&&write_byte((uint8_t)(addr<<1))&&write_byte(reg);
 for(unsigned i=0;ok&&i<count;++i)ok=write_byte(data[i]);stop();
 if(addr==0x50){pause_bus();BSRR(GPIOA)=1u<<2;}
 return ok;
}
bool i2c_read(uint8_t addr,uint8_t reg,uint8_t *data,unsigned count){
 bool ok=start()&&write_byte((uint8_t)(addr<<1))&&write_byte(reg)&&start()&&write_byte((uint8_t)((addr<<1)|1u));
 for(unsigned i=0;ok&&i<count;++i)ok=read_byte_with_ack(data+i,i+1<count);stop();return ok;
}
bool write_reg(uint8_t addr,uint8_t reg,uint8_t value){return i2c_write(addr,reg,&value,1);}
bool read_reg(uint8_t addr,uint8_t reg,uint8_t *value){return i2c_read(addr,reg,value,1);}
static volatile uint32_t ticks;
void SysTick_Handler(void){++ticks;}
uint32_t millis(void){return ticks;}
uint32_t micros(void){
 uint32_t before,after,value;
 do{before=ticks;value=REG32(0xe000e018u);after=ticks;}while(before!=after);
 return before*1000u+(11999u-value)/12u;
}
unsigned buttons_raw(void){uint32_t pins=IDR(GPIOA);return (!(pins&2u))|((!(pins&1u))<<1);}
uint32_t gpioa_inputs(void){return IDR(GPIOA);}
void direct_leds(unsigned on){
 uint32_t pins=0;if(on&1)pins|=1u<<6;if(on&2)pins|=1u<<7;
 BSRR(GPIOA)=(DIRECT_LED_MASK&~pins)|(pins<<16);
}
void center_led_duty(unsigned duty){TIM_REG(0x34)=center_pwm_compare(duty);}
unsigned center_led_compare(void){return TIM_REG(0x34);}
static void center_pwm_init(void){
 /* ST STM32C011: PA8 AF2=TIM1_CH1; RCC TIM1EN/TIM1RST bit11.
    PWM1 + inverted polarity drives the LED low during its on-time. */
 REG32(0x40021040u)|=1u<<11;(void)REG32(0x40021040u);
 REG32(0x40021030u)|=1u<<11;REG32(0x40021030u)&=~(1u<<11);
 TIM_REG(0x28)=CENTER_PWM_PRESCALER;
 TIM_REG(0x2c)=CENTER_PWM_PERIOD_COUNTS-1;
 TIM_REG(0x34)=0;
 TIM_REG(0x18)=(6u<<4)|(1u<<3); /* PWM1, compare preload. */
 TIM_REG(0x20)=3u;              /* CC1E, CC1P (active low). */
 TIM_REG(0x14)=1u;              /* Update: load prescaler/ARR/CCR. */
 TIM_REG(0x44)=1u<<15;          /* Advanced-timer main output enable. */
 TIM_REG(0x00)=(1u<<7)|1u;      /* ARR preload and counter enable. */
 REG32(GPIOA+0x24u)=(REG32(GPIOA+0x24u)&~15u)|2u;
 MODER(GPIOA)=(MODER(GPIOA)&~(3u<<16))|(2u<<16);
}
static void output(unsigned port,unsigned pin){
 MODER(port)=(MODER(port)&~(3u<<(pin*2)))|(1u<<(pin*2));
 OSPEEDR(port)&=~(3u<<(pin*2));PUPDR(port)&=~(3u<<(pin*2));
}
void hardware_init(void){
 /* Explicit HSI48/4=12 MHz, AHB/APB divide 1; safe at reset Flash latency. */
 REG32(0x40021000u)=(REG32(0x40021000u)&~(7u<<11))|(2u<<11)|(1u<<8);
 while(!(REG32(0x40021000u)&(1u<<10))){}
 REG32(0x40021008u)&=~(7u|0x7f00u);
 RCC_IOPENR|=3u;(void)RCC_IOPENR;
 BSRR(GPIOA)=LED_MASK;OTYPER(GPIOA)&=~LED_MASK;
 for(unsigned pin=6;pin<=8;++pin)output(GPIOA,pin);
 /* PA2 is unused on v2; the removed sampling switch must not be driven. */
 MODER(GPIOA)|=3u<<4;PUPDR(GPIOA)&=~(3u<<4);
 center_pwm_init();
 /* SW2/0=PA1, SW1/1=PA0. PA4/5 retain inactive external pullups; v2 firmware ignores them. */
 MODER(GPIOA)&=~(0xfu|(3u<<8)|(3u<<10));
 PUPDR(GPIOA)&=~(0xfu|(3u<<8)|(3u<<10));
 BSRR(GPIOB)=SCL|SDA;OTYPER(GPIOB)|=SCL|SDA;
 output(GPIOB,6);output(GPIOB,7);
 for(unsigned i=0;i<9&&!(IDR(GPIOB)&SDA);++i){scl_low();pause_bus();(void)scl_release();pause_bus();}stop();
 REG32(0xe000e014u)=11999;REG32(0xe000e018u)=0;REG32(0xe000e010u)=7;
 __asm__ volatile("cpsie i");
}
