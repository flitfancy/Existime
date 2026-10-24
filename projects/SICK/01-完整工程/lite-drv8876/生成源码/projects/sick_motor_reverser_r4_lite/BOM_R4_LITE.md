# SICK-E Lite R4 BOM

## Summary

- Schematic references: 31
- Purchased/fitted SMT parts per board: 26
- Copper-only wire interfaces: 5 (`J1-J4`, `SW1`)
- Purchased board connectors: 0
- Provisional board: two layers, 50 mm x 30 mm

The part count drops from the R3 contract's 38 references/33 fitted parts to 31
references/26 fitted parts. The largest reduction comes from removing three
status LEDs and four VNH5019 diagnostic/current-sense resistors. DRV8876 needs
two charge-pump capacitors, and the proven input TVS is retained.

## Frozen fitted BOM

| Function | References | Qty | Part | LCSC | Package |
| --- | --- | ---: | --- | --- | --- |
| H-bridge | U1 | 1 | DRV8876PWPR | C575551 | HTSSOP-16-EP |
| 3.3 V LDO | U2 | 1 | HT7533-1 | C14289 | SOT-89-3 |
| Schmitt logic | U3 | 1 | SN74HC14DR | C6820 | SOIC-14 |
| Direction latch | U4 | 1 | SN74HC74DR | C6762 | SOIC-14 |
| Input TVS | U5 | 1 | SMAJ18CA | C10768 | DO-214AC |
| Resettable fuse | F1 | 1 | BSMD2920-400-30V | C20539726 | 2920 |
| Power LED | D1 | 1 | XL-2012UGC | C965815 | 0805 |
| Interlock diodes | D5-D7 | 3 | 1N4148W | C81598 | SOD-123 |
| Resistors | R1, R5-R8 | 5 | 4.7 k / 10 k / 100 k | See CSV | 0805 |
| Bulk capacitor | C1 | 1 | 330 uF, 16 V, user stock | N/A | 1210 (3225 metric) |
| LDO capacitors | C2, C3 | 2 | 10 uF | C15850 / C1713 | 0805 |
| 100 nF capacitors | C4-C10 | 7 | 100 nF, 50 V, X7R | C1711 | 0805 |
| Flying capacitor | C11 | 1 | 22 nF, 50 V, X7R | C1729 | 0805 |

The sortable source is `BOM_R4_LITE.csv`.

## Driver configuration

- U1 pin 1 EN = +3V3.
- U1 pin 2 PH = `DIR_RIGHT_Q`.
- U1 pin 3 nSLEEP = `MOTOR_EN`; the dual-open state therefore becomes Hi-Z.
- U1 pin 5 VREF = +3V3; pin 6 IPROPI = GND; pin 7 IMODE = GND.
- U1 pin 16 PMODE = GND, selecting PH/EN mode.
- U1 pin 4 nFAULT is intentionally unconnected in Lite R4.
- Exposed PowerPAD is GND and requires the live library/capture check.

## Accepted prototype risk

The user explicitly waived motor-current sizing as a design blocker and
accepted the 16 V rating of the stocked 330 uF C1 for this 12 V prototype. F1,
driver thermal capacity, copper width, input transients and C1 rail margin
remain prototype assumptions to verify on the assembled board; they do not
block the schematic contract. `C596320` is used by the script only as a standard
1210 symbol/footprint donor and is not a purchase item.
