# SICK-E Lite R4 design contract

## Required behavior

- 12 V brushed-DC motor, fixed speed, no MCU.
- J3 and J4 use normally-closed switch contacts: COM to GND, NC to RAW.
- Power-on starts travel using the retained default direction.
- Opening either limit reverses direction; restoring it does not reverse again.
- Opening both limits disables the bridge; either input returning allows motion.
- J1-J4 and SW1 are direct-wire copper pads, not purchased connectors.

## Architecture

```text
J1 -> SW1 -> F1 -> +12V_PROT -> DRV8876 -> J2 motor
                         |     -> SMAJ18CA + 330uF/16V stock
                         `---- -> HT7533-1 -> +3V3 logic

J3/J4 NC limits -> RC + 74HC14 -> 74HC74 direction latch -> DRV8876 PH
                         |-> D6/D7 diode OR -> MOTOR_EN -> DRV8876 nSLEEP
R7/C8 + D5 -> two spare 74HC14 gates -> clean active-low preset
```

## DRV8876 decision

U1 is `DRV8876PWPR` (LCSC C575551), HTSSOP-16 PowerPAD. It was selected
because its PH/EN mode consumes the existing direction and enable semantics
without adding logic gates. DRV8871/DRV8870 expose IN1/IN2 rather than an
independent PH/EN interface and are not direct replacements for this design.

The TI data sheet specifies 4.5 V to 37 V operation and VIH(min)=1.5 V, so the
12 V motor rail and 3.3 V logic domain are compatible.

### PWP pin contract

| Pin | Name | Net / handling |
| ---: | --- | --- |
| 1 | EN/IN1 | +3V3, always enabled while awake |
| 2 | PH/IN2 | `DIR_RIGHT_Q` |
| 3 | nSLEEP | `MOTOR_EN` |
| 4 | nFAULT | NC |
| 5 | VREF | +3V3 |
| 6 | IPROPI | GND |
| 7 | IMODE | GND |
| 8 | OUT1 | `MOTOR_A` |
| 9 | PGND | GND |
| 10 | OUT2 | `MOTOR_B` |
| 11 | VM | +12V_PROT |
| 12 | VCP | C10 to +12V_PROT |
| 13 | CPH | C11 to CPL |
| 14 | CPL | C11 to CPH |
| 15 | GND | GND |
| 16 | PMODE | GND, selects PH/EN |
| EP | PowerPAD | GND; expected as pad 17 in EasyEDA capture |

`IPROPI=GND` with `VREF>GND` disables internal current chopping. `IMODE=GND`
keeps automatic-retry overcurrent protection. `MOTOR_EN` drives nSLEEP rather
than EN so the dual-open state produces Hi-Z motor outputs, matching the R3
inhibit behavior more closely than PH/EN braking.

## Limit and direction logic

| Left open | Right open | L_SET_N | R_CLR_N | MOTOR_EN | Result |
| ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 0 | 1 | 1 | 1 | Hold previous direction and drive |
| 1 | 0 | 0 | 1 | 1 | Asynchronous preset: Q=1; A high/B low |
| 0 | 1 | 1 | 0 | 1 | Asynchronous clear: Q=0; A low/B high |
| 1 | 1 | 0 | 0 | 0 | DRV8876 sleep; A/B Hi-Z |

The 74HC74 PRE# and CLR# combination remains non-stable when both limits are
open. MOTOR_EN removes bridge drive during that interval. Direction after both
limits return together is intentionally not guaranteed, matching the inherited
behavioral boundary.

## Power-on preset repair

R7/C8/D5 now create `DIR_SET_RC_N`. U3 gates 3 and 4 form two cascaded
Schmitt inversions:

```text
DIR_SET_RC_N -> U3 pin 5 -> pin 6 DIR_SET_RC_P
DIR_SET_RC_P -> U3 pin 9 -> pin 8 DIR_SET_N -> U4 PRE#
```

The two inversions preserve active-low polarity while removing the slow edge
previously applied directly to the 74HC74 asynchronous input.

## Power support

- C1: user-stock 330 uF/16 V capacitor in a standard 1210 (3225 metric)
  two-pad footprint. The user accepts its 16 V rail margin for this 12 V
  personal prototype.
- C4: 100 nF VM bypass close to U1 pins 11/9.
- C10: 100 nF from VCP to VM.
- C11: 22 nF from CPH to CPL.
- U5: SMAJ18CA bidirectional TVS across +12V_PROT and GND.
- C2/C3 remain the HT7533-1 input/output capacitors.
- C7 and C9 are U3/U4 local 100 nF decouplers.

## PCB intent

- R4-L2 compact placement candidate outline: 50 mm x 30 mm, two layers.
- Continuous bottom GND plane; exposed U1 PowerPAD tied to it with thermal vias.
- Keep C4, C10 and C11 inside the shortest practical U1 loops.
- Keep MOTOR_A/MOTOR_B and +12V_PROT away from RAW/preset nets.
- Put all five wire-pad interfaces at board edges and provide enclosure strain
  relief.
- The 2026-08-12 live capture proved the actual footprint geometry and the
  R4-L2 offline placement is collision-free. Dimensions and placement remain a
  prototype candidate until the placement is applied and visually inspected in
  EasyEDA.

## Primary data sheets

- DRV8876: https://www.ti.com/lit/ds/symlink/drv8876.pdf
- HT75xx-1: https://www.holtek.com/webapi/116711/HT75xx-1v281.pdf

## Prototype acceptance

The local configuration, behavior tests and schematic-script syntax pass. The
generated EasyEDA schematic completed its design-rule check on 2026-08-12 with
0 fatal errors, 0 errors and one accepted warning: `DIR_LEFT_NQ` is the unused
complementary output of the active U4 flip-flop and therefore forms a deliberate
one-pin net. The user also confirmed the stocked C1 fits the standard 1210
footprint. The personal-prototype schematic checkpoint is passed. PCB/order
readiness still requires live footprint geometry capture, PCB DRC and Gerber
review.
