# R3 to R4 Lite schematic difference

| Area | R3 | R4 Lite |
| --- | --- | --- |
| H-bridge | VNH5019, 33 pads | DRV8876PWPR, HTSSOP-16 PowerPAD |
| Direction | Q and /Q into INA/INB | Q into PH; EN tied high |
| Dual-open inhibit | MOTOR_EN to VNH enables | MOTOR_EN to DRV8876 nSLEEP, outputs Hi-Z |
| Driver support | R9-R12 DIAG/CS network | Removed; VREF=3V3, IPROPI/IMODE/PMODE=GND |
| Driver capacitors | VM bypass only | VM bypass + VCP/VM 100 nF + CPH/CPL 22 nF |
| Bulk | R3 contract 470 uF; physical board 3x220 uF | One user-stock 330 uF/16 V, 1210 capacitor |
| Indicators | Four LEDs and four resistors | One 12 V power LED and R1 |
| Input TVS | Present only on physical R3 board | SMAJ18CA retained in contract as U5 |
| Power-on preset | R7/C8 edge directly into PRE# | Two spare 74HC14 gates square the edge |
| Interfaces | Five direct-wire pad devices | Unchanged |
| Behavior | NC limits, auto reverse, dual-open stop | Unchanged at interface level |

R4 has 31 schematic references and 26 fitted parts versus R3's 38 references
and 33 fitted parts. Motor-current sizing is recorded as an accepted prototype
risk by user instruction rather than a schematic blocker.
