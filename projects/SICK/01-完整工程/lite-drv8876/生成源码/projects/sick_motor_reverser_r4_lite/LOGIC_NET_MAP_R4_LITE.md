# R4 Lite logic and net map

| ID | Net | Role |
| --- | --- | --- |
| N01 | GND | Continuous bottom plane and U1 PowerPAD return |
| N02 | +12V_IN | J1 input to SW1 |
| N03 | +12V_SW | SW1 to F1 |
| N04 | +12V_PROT | Protected motor/LDO rail |
| N05 | MOTOR_A | U1 OUT1 to J2 pin 1 |
| N06 | MOTOR_B | U1 OUT2 to J2 pin 2 |
| N07 | +3V3 | Logic rail |
| N08 | L_RAW | Left NC limit input |
| N09 | R_RAW | Right NC limit input |
| N10 | L_SET_N | Clean left active-low preset request |
| N11 | R_CLR_N | Clean right active-low clear request |
| N12 | DIR_SET_RC_N | Raw R7/C8/D5 preset waveform |
| N13 | DIR_SET_RC_P | First Schmitt inversion |
| N14 | DIR_SET_N | Second Schmitt inversion into U4 PRE# |
| N15 | DIR_RIGHT_Q | Direction Q; U1 PH |
| N16 | DIR_LEFT_NQ | Complementary direction/test node |
| N17 | MOTOR_EN | Diode-OR enable; U1 nSLEEP |
| N18 | VCP | U1 charge-pump output |
| N19 | CPH | Charge-pump flying node |
| N20 | CPL | Charge-pump flying node |
| N21 | LED_PWR_A | Power LED anode resistor node |

## State contract

| Left open | Right open | Q | MOTOR_EN | MOTOR_A | MOTOR_B |
| ---: | ---: | --- | ---: | --- | --- |
| 0 | 0 | hold | 1 | follows Q | follows Q |
| 1 | 0 | 1 | 1 | high | low |
| 0 | 1 | 0 | 1 | low | high |
| 1 | 1 | non-stable async | 0 | Hi-Z | Hi-Z |

The state table is enforced by `tests/r4-lite.test.mjs` and the pin-to-net
contract in `project.config.mjs`.
