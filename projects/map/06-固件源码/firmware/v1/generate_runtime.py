import json
from pathlib import Path
root=Path(__file__).resolve().parent
fields='magic version ms loops mode buttons selected selected_led mask_lo mask_hi calibrated heading heading_valid direction mag_samples acc_samples mag_errors acc_errors i2c_errors eeprom_loaded save_busy save_count save_errors dirty save_sequence cal_samples cal_faces settle_count'.split()
arrays={'mag':3,'acc':3,'cal_span':3,'bias':3,'radius':3,'output':5,'config':5,'input':5}
tail='led_verified direct_mask gpioa_idr power_flags max_loop_ms who_mag who_acc sensor_ready north cpu_busy_permille max_loop_us'.split()
v3='heading_window_cdeg battery_steady_mask battery_breath_mask battery_breath_duty battery_revealed_bars pwm_writes ring_max_write_us sensor_poll_paused'.split()
v4='center_breathing center_duty center_pwm_compare center_pwm_period center_breath_period_ms'.split()
v5='charge_phase charge_complete_count charge_done_pending charge_done_display charge_done_remaining_ms'.split()
decl=['#ifndef RUNTIME_GENERATED_H','#define RUNTIME_GENERATED_H','#include <stdint.h>','struct runtime_status {']
layout=[];offset=0
for name,n in [(x,1) for x in fields]+list(arrays.items())+[(x,1) for x in tail]+[('mag_config',3),('acc_config',2),('request_save',1)]+[(x,1) for x in 'battery_samples battery_errors battery_raw battery_ref_raw battery_ref_cal battery_vdda_mv battery_mv battery_filtered_mv battery_percent battery_bars battery_state battery_age_ms battery_adc_ready battery_charging battery_external_power battery_display_mask'.split()]+[(x,1) for x in v3+v4+v5]:
    decl.append('    uint32_t '+name+(';' if n==1 else f'[{n}];'))
    minimum=5 if name in v5 else 4 if name in v4 else 3 if name in v3 else 2 if name.startswith('battery_') else 1
    layout.append({'name':name,'offset':offset,'count':n,'min_version':minimum});offset+=4*n
decl+=['};',f'_Static_assert(sizeof(struct runtime_status)=={offset},"RAM ABI mismatch");','#endif']
(root/'runtime_generated.h').write_text('\n'.join(decl)+'\n',encoding='utf-8')
(root/'runtime-layout.json').write_text(json.dumps({'address':0x20000000,'size':offset,'fields':layout},indent=2),encoding='utf-8')
print('Runtime diagnostics bytes:',offset)
