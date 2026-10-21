#ifndef MAP_PLATFORM_H
#define MAP_PLATFORM_H
#include <stdint.h>
#include <stdbool.h>
void hardware_init(void);
uint32_t millis(void);
uint32_t micros(void);
unsigned buttons_raw(void);
uint32_t gpioa_inputs(void);
bool i2c_write(uint8_t address,uint8_t reg,const uint8_t *data,unsigned count);
bool i2c_read(uint8_t address,uint8_t reg,uint8_t *data,unsigned count);
bool i2c_ready(uint8_t address);
bool write_reg(uint8_t address,uint8_t reg,uint8_t value);
bool read_reg(uint8_t address,uint8_t reg,uint8_t *value);
void direct_leds(unsigned on_mask);
void center_led_duty(unsigned duty);
unsigned center_led_compare(void);
bool battery_adc_init(void);
bool battery_adc_sample(uint16_t *raw_battery,uint16_t *raw_reference,uint16_t *calibration);
#endif
