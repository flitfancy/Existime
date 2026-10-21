#include <stdint.h>
#include <stdbool.h>
#include "app.h"
#include "battery.h"
#include "calibration.h"
#include "center_pwm.h"
#include "charger.h"
#include "display.h"
#include "firmware.h"
#include "leds.h"
#include "platform.h"
#include "runtime_generated.h"
#include "sensors.h"
#include "settings.h"
#include "timing.h"
#include "board_generated.h"

__attribute__((section(".status"),used)) volatile struct runtime_status runtime;
static struct app app;
static struct settings settings;
static struct storage store;
static struct calibration_flow calibration;
static struct sensors sensors;
static struct leds leds;
static struct battery_status battery;
static struct charger charger;
static bool adc_ready;
static uint32_t next_poll, next_frame, next_verify, next_diag, next_retry, next_battery;
static uint32_t busy_us, busy_window;

static void settings_from_app(void) {
    settings.mask_lo = app.mask_lo;
    settings.mask_hi = app.mask_hi;
}

static void poll_battery(uint32_t now) {
    if (!adc_ready) adc_ready = battery_adc_init();
    uint16_t raw = 0, reference = 0, factory = 0;
    bool ok = adc_ready && battery_adc_sample(&raw, &reference, &factory);
    if (!ok) adc_ready = false;
    battery_update(&battery, ok, raw, reference, factory, now);
}

static void update_diagnostics(uint32_t now, unsigned raw) {
    runtime.ms = now; runtime.mode = app.mode; runtime.buttons = raw;
    runtime.selected = app.selected; runtime.selected_led = province_order[app.selected];
    runtime.mask_lo = app.mask_lo; runtime.mask_hi = app.mask_hi;
    runtime.calibrated = settings.cal.valid && app.mode != CAL_SAVE;
    runtime.north = (uint32_t)settings.cal.north;
    runtime.heading = (uint32_t)sensors.heading;
    runtime.heading_valid = sensors.heading_valid && app.mode == NORMAL;
    runtime.direction = (uint32_t)(runtime.heading_valid && !charger.notice_active ? sensors.direction : -1);
    runtime.mag_samples = sensors.mag_samples; runtime.acc_samples = sensors.acc_samples;
    runtime.mag_errors = sensors.mag_errors; runtime.acc_errors = sensors.acc_errors;
    runtime.i2c_errors = leds.errors;
    runtime.eeprom_loaded = store.loaded; runtime.save_busy = store.phase;
    runtime.dirty = store.dirty; runtime.save_count = store.successes;
    runtime.save_errors = store.errors; runtime.save_sequence = store.sequence;
    runtime.cal_samples = calibration.collector.samples;
    runtime.cal_faces = calibration.collector.faces; runtime.settle_count = calibration.settle_count;
    for (unsigned i = 0; i < 3; ++i) {
        runtime.mag[i] = (uint32_t)sensors.mag[i]; runtime.acc[i] = (uint32_t)sensors.acc[i];
        runtime.cal_span[i] = (uint32_t)(calibration.collector.max[i] - calibration.collector.min[i]);
        runtime.bias[i] = (uint32_t)settings.cal.bias[i]; runtime.radius[i] = (uint32_t)settings.cal.radius[i];
        runtime.mag_config[i] = sensors.mag_config[i];
    }
    for (unsigned i = 0; i < 5; ++i) {
        runtime.output[i] = leds.sent[i]; runtime.config[i] = leds.config[i]; runtime.input[i] = leds.input[i];
    }
    for (unsigned i = 0; i < 2; ++i) runtime.acc_config[i] = sensors.acc_config[i];
    runtime.led_verified = leds.verified; runtime.direct_mask = leds.direct;
    runtime.who_mag = sensors.who_mag; runtime.who_acc = sensors.who_acc;
    runtime.sensor_ready = (unsigned)sensors.mag_ready | ((unsigned)sensors.acc_ready << 1);
    runtime.gpioa_idr = gpioa_inputs(); runtime.power_flags = 0; /* battery has no charger status inputs. */
    runtime.battery_samples = battery.samples; runtime.battery_errors = battery.errors;
    runtime.battery_raw = battery.raw_battery; runtime.battery_ref_raw = battery.raw_reference;
    runtime.battery_ref_cal = battery.factory_reference; runtime.battery_vdda_mv = battery.vdda_mv;
    runtime.battery_mv = battery.voltage_mv; runtime.battery_filtered_mv = (battery.filtered_q8 + 128u) / 256u;
    runtime.battery_percent = battery.percent; runtime.battery_bars = battery.bars;
    runtime.battery_state = battery.state;
    runtime.battery_age_ms = battery.samples ? now - battery.updated_at : UINT32_MAX;
    runtime.battery_adc_ready = adc_ready;
    runtime.battery_external_power = 0;
    runtime.battery_charging = 0;
    runtime.battery_display_mask = app.mode == BATTERY_VIEW ? (uint8_t)~leds.sent[0] : 0;
    runtime.heading_window_cdeg = HEADING_WINDOW_CDEG;
    runtime.battery_steady_mask = app.mode == BATTERY_VIEW ? (uint8_t)~leds.frame.output[0] : 0;
    runtime.battery_breath_mask = leds.frame.breath_mask;
    runtime.battery_breath_duty = leds.frame.breath_duty;
    runtime.battery_revealed_bars = leds.frame.revealed_bars;
    runtime.pwm_writes = leds.pwm_writes; runtime.ring_max_write_us = leds.max_write_us;
    runtime.sensor_poll_paused = app.mode == BATTERY_VIEW;
    runtime.center_breathing = charger_charging(&charger) && !charger.notice_active &&
        (app.mode == NORMAL || app.mode == BATTERY_VIEW);
    runtime.center_duty = leds.center_duty;
    runtime.center_pwm_compare = center_led_compare();
    runtime.center_pwm_period = CENTER_PWM_PERIOD_COUNTS;
    runtime.center_breath_period_ms = CENTER_BREATH_PERIOD_MS;
    runtime.charge_phase = charger.phase;
    runtime.charge_complete_count = charger.completions;
    runtime.charge_done_pending = charger.notice_pending;
    runtime.charge_done_display = charger.notice_active;
    runtime.charge_done_remaining_ms = charger_notice_remaining(&charger, now);
}

static void main_loop(void) {
    runtime.magic = MAP_RUNTIME_MAGIC; runtime.version = MAP_APPLICATION_VERSION;
    hardware_init(); leds_init(&leds); storage_load(&store, &settings);
    app_init(&app, settings.mask_lo, settings.mask_hi); sensors_init(&sensors);
    adc_ready = battery_adc_init(); next_battery = millis() + 100;
    for (;;) {
        uint32_t loop_start = micros();
        uint32_t now = millis(); unsigned raw = buttons_raw(); ++runtime.loops;
        unsigned action = app_tick(&app, now, raw);
        bool was_notice = charger.notice_active;
        charger_update(&charger, false, false, now);
        charger_notice_tick(&charger, app.mode == NORMAL || app.mode == BATTERY_VIEW, now);
        if (was_notice != charger.notice_active) next_frame = now;
        if (action & ACT_DIRTY) {
            settings_from_app(); storage_request(&store); app.feedback_until = now + 600;
        }
        if (calibration_action(&calibration, &app, action, now)) sensors_reset_heading(&sensors);

        /* Capacity replaces heading for three seconds. Keep the I2C bus free
           for ring PWM, then acquire fresh compass samples on return. */
        if (app.mode == BATTERY_VIEW) sensors_reset_heading(&sensors);
        else {
            if (time_reached(now, next_poll)) {
                next_poll = now + 20;
                sensors_poll(&sensors, &settings.cal, now);
                if (sensors.new_mag && sensors.have_acc && (uint32_t)(now - sensors.acc_at) < 150)
                    calibration_sample(&calibration, &app, &settings, &store,
                                       sensors.mag, sensors.acc, now, raw);
            }
            if (time_reached(now, next_retry)) {
                next_retry = now + 2000;
                if (!sensors.mag_ready || !sensors.acc_ready) sensors_init(&sensors);
            }
        }
        if (time_reached(now, next_battery)) { next_battery = now + 1000; poll_battery(now); }
        if (runtime.request_save == 0x53415645u) {
            runtime.request_save = 0; settings_from_app(); storage_request(&store);
        }
        storage_tick(&store, &settings, now);
        if (calibration_tick(&calibration, &app, &store, now)) {
            sensors_reset_heading(&sensors); next_frame = now;
        }
        if (time_reached(now, next_frame)) {
            next_frame = now + 20;
            struct display_frame frame;
            display_build(&frame, &app, &battery, &calibration, sensors.direction,
                          sensors.heading_valid, charger_charging(&charger),
                          store.errors && store.dirty, now);
            if (charger.notice_active) display_charge_complete(&frame);
            leds_set_frame(&leds, &frame, micros());
        }
        leds_service(&leds);
        if (app.mode != BATTERY_VIEW && time_reached(now, next_verify)) {
            next_verify = now + 200; leds_verify_next(&leds);
        }
        if (time_reached(now, next_diag)) { next_diag = now + 20; update_diagnostics(now, raw); }
        uint32_t elapsed = millis() - now;
        if (elapsed > runtime.max_loop_ms) runtime.max_loop_ms = elapsed;
        uint32_t work = micros() - loop_start; busy_us += work;
        if (work > runtime.max_loop_us) runtime.max_loop_us = work;
        uint32_t interval = millis() - busy_window;
        if (interval >= 1000) {
            runtime.cpu_busy_permille = busy_us / interval;
            busy_us = 0; busy_window = millis();
        }
        /* Only spin for an imminent PWM edge; I2C never runs in an ISR. */
        if (leds_next_edge(&leds, micros()) > 1000) __asm__ volatile("wfi");
    }
}

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;
__attribute__((noreturn)) void Default_Handler(void) { for (;;) {} }
__attribute__((noreturn)) void Reset_Handler(void) {
    __asm__ volatile("cpsid i");
    uint32_t *src = &_sidata;
    for (uint32_t *p = &_sdata; p < &_edata;) *p++ = *src++;
    for (uint32_t *p = &_sbss; p < &_ebss;) *p++ = 0;
    volatile uint32_t *p = (volatile uint32_t *)&runtime;
    for (unsigned i = 0; i < sizeof(runtime) / 4; ++i) p[i] = 0;
    main_loop(); for (;;) {}
}
void *memset(void *dst, int c, unsigned count) {
    volatile uint8_t *p = dst; while (count--) *p++ = (uint8_t)c; return dst;
}
void *memcpy(void *dst, const void *src, unsigned count) {
    volatile uint8_t *d = dst; const uint8_t *s = src;
    while (count--) *d++ = *s++;
    return dst;
}
