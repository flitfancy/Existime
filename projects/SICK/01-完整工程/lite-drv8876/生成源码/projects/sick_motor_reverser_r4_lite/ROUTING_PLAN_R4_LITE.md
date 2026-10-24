# R4 Lite R4-L2 routing plan

The 2026-08-12 EasyEDA capture verified all 31 footprints and 102 pads. The
R4-L2 offline placement reports no collision or board-edge violation. Apply and
visually approve that placement before using this routing sequence.

## Order

1. Place J1/SW1/F1 and route +12V_IN/+12V_SW.
2. Place U1, C4, C10 and C11 as one local cluster. Route VCP/CPH/CPL first.
3. Route +12V_PROT and the MOTOR_A/MOTOR_B copper to J2.
4. Connect U1 PowerPAD, PGND and GND pins into the bottom plane with short
   thermal vias; keep the VM bypass return local.
5. Route +3V3 and logic decouplers.
6. Route RAW/preset asynchronous nets away from motor copper.
7. Route DIR_RIGHT_Q, DIR_LEFT_NQ and MOTOR_EN between U3/U4 and U1.
8. Route the power LED, repour GND, inspect return paths, then run DRC.

## Starting rules

| Class | Width | Clearance | Nets |
| --- | ---: | ---: | --- |
| HIGH_CURRENT_INPUT | 60 mil | 12 mil | +12V_IN, +12V_SW |
| HIGH_CURRENT_MOTOR | 80 mil | 12 mil | +12V_PROT, MOTOR_A, MOTOR_B |
| CHARGE_PUMP | 12 mil | 8 mil | VCP, CPH, CPL |
| LOGIC_POWER | 20 mil | 8 mil | +3V3 |
| SENSITIVE_ASYNC | 10 mil | 10 mil | RAW, set/clear and preset chain |
| BRIDGE_CONTROL | 10 mil | 8 mil | Q, /Q, MOTOR_EN |
| INDICATOR | 8 mil | 8 mil | LED_PWR_A |

The motor/power widths are prototype starting values accepted under the user's
current-sizing waiver. They remain an arrival-test item, not production proof.
