#include "platform.h"
/* STM32C011 ADC1: ST LL ADC reference, VREFINT=channel 10, cal at 0x1FFF756A. */
#define R(a) (*(volatile uint32_t *)(a))
#define ADC_ISR R(0x40012400u)
#define ADC_CR R(0x40012408u)
#define ADC_CFGR1 R(0x4001240cu)
#define ADC_CFGR2 R(0x40012410u)
#define ADC_SMPR R(0x40012414u)
#define ADC_CHSELR R(0x40012428u)
#define ADC_DR R(0x40012440u)
#define ADC_CCR R(0x40012708u)
#define ADVREGEN (1u<<28)
static void wait_us(uint32_t duration){uint32_t begin=micros();while((uint32_t)(micros()-begin)<duration){} }
static bool wait_set(volatile uint32_t *reg,uint32_t mask,unsigned timeout){
    uint32_t begin=micros();
    while(!(*reg&mask))if((uint32_t)(micros()-begin)>timeout)return false;
    return true;
}
static bool wait_clear(volatile uint32_t *reg,uint32_t mask,unsigned timeout){
    uint32_t begin=micros();
    while(*reg&mask)if((uint32_t)(micros()-begin)>timeout)return false;
    return true;
}
bool battery_adc_init(void){
    R(0x40021040u)|=1u<<20; /* RCC APBENR2 ADCEN. */
    R(0x40021030u)|=1u<<20;R(0x40021030u)&=~(1u<<20);
    R(0x50000000u)|=3u<<6;R(0x5000000cu)&=~(3u<<6); /* PA3 analog, no pull. */
    ADC_CFGR2=2u<<30; /* HCLK/4 = 3 MHz, within the ADC clock limits. */
    ADC_CFGR1=1u<<14; /* 12-bit, ascending fixed sequence, auto-wait protects DR from IRQ overrun. */
    ADC_SMPR=7; /* 160.5-cycle acquisition for the high-impedance divider and VREFINT. */
    ADC_CCR=1u<<22;
    ADC_CR=ADVREGEN;wait_us(40);
    ADC_CR=ADVREGEN|(1u<<31);
    if(!wait_clear(&ADC_CR,1u<<31,5000))return false;
    wait_us(5);ADC_ISR=(1u<<11)|1u;ADC_CR=ADVREGEN|1u;
    if(!wait_set(&ADC_ISR,1u,2000))return false;
    ADC_ISR=1u<<13;ADC_CHSELR=(1u<<3)|(1u<<10);
    return wait_set(&ADC_ISR,1u<<13,2000);
}
bool battery_adc_sample(uint16_t *raw_battery,uint16_t *raw_reference,uint16_t *calibration){
    uint32_t sum_battery=0,sum_reference=0;
    *calibration=*(const volatile uint16_t *)0x1fff756au;
    for(unsigned sample=0;sample<9;++sample){
        if(!wait_clear(&ADC_CR,1u<<2,2000))return false;
        ADC_ISR=(1u<<2)|(1u<<3)|(1u<<4);
        ADC_CR=ADVREGEN|(1u<<2);
        if(!wait_set(&ADC_ISR,1u<<2,2000))return false;
        uint32_t bat=ADC_DR&4095u;
        if(!wait_set(&ADC_ISR,1u<<2,2000))return false;
        uint32_t ref=ADC_DR&4095u;
        if(!wait_set(&ADC_ISR,1u<<3,2000)||(ADC_ISR&(1u<<4)))return false;
        if(sample){sum_battery+=bat;sum_reference+=ref;}
    }
    *raw_battery=(uint16_t)((sum_battery+4)/8);
    *raw_reference=(uint16_t)((sum_reference+4)/8);
    return true;
}
