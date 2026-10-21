#ifndef MAP_CENTER_PWM_H
#define MAP_CENTER_PWM_H

/* TIM1 gets the 12MHz APB clock; 1MHz counter, 1kHz active-low PA8 PWM. */
#define CENTER_PWM_PRESCALER 11u
#define CENTER_PWM_PERIOD_COUNTS 1000u
static inline unsigned center_pwm_compare(unsigned duty) {
    if (duty > 255) duty = 255;
    return (duty * CENTER_PWM_PERIOD_COUNTS + 127u) / 255u;
}
#endif
