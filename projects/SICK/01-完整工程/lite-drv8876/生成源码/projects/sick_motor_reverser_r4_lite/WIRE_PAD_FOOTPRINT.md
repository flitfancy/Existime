# Direct-Wire SMD Footprints

Create two personal EasyEDA Pro footprints. They replace every purchased wiring
terminal on this board. Both footprints use top-layer SMD copper only: there are
no plated or non-plated holes and no stencil-paste opening.

The R4 schematic script automatically sets `J1`, `J2`, `J3`, `J4`, and `SW1`
to `Exclude from BOM` / `加入BOM = 否` while keeping PCB conversion enabled so
the copper pads still appear on the board. Because the public schematic
component API exposes an existing component's associated footprint as read-only,
R4 instead creates or reuses two complete personal devices before drawing the
schematic. Each device combines the verified C557665 two-pin symbol with one of
these personal footprints. Only the one-time footprint drawing remains manual;
a newly generated R4 page does not require manual footprint assignment. An
older already-created page still needs manual replacement or clean regeneration.

Run `generated/00b_apply_wirepad_flags.js` after generation to enforce the
BOM/PCB flags and verify all five associated footprint names.

## High-current footprint

- Accepted names: `SICK_WIREPAD_2P_5X8_P10` or `2P_5X8_P10`
- Use for: `J1`, `J2`, `SW1`
- Pad 1: rectangular, 5.0 mm x 8.0 mm, center X = -5.0 mm, Y = 0
- Pad 2: rectangular, 5.0 mm x 8.0 mm, center X = +5.0 mm, Y = 0
- Pitch: 10.0 mm
- Solder mask expansion: 0.10 mm
- Paste opening: disabled
- Pad numbers: exactly `1` and `2`

## Signal footprint

- Accepted names: `SICK_WIREPAD_2P_3X5_P5` or `2P_3X5_P5`
- Use for: `J3`, `J4`
- Pad 1: rectangular, 3.0 mm x 5.0 mm, center X = -2.5 mm, Y = 0
- Pad 2: rectangular, 3.0 mm x 5.0 mm, center X = +2.5 mm, Y = 0
- Pitch: 5.0 mm
- Solder mask expansion: 0.10 mm
- Paste opening: disabled
- Pad numbers: exactly `1` and `2`

The long pad dimension points from the board edge toward the interior. Place all
five footprints at board edges so wires leave without crossing components.

## Electrical assignments

| Ref | Footprint | Pad 1 | Pad 2 | External connection |
| --- | --- | --- | --- | --- |
| J1 | 5X8_P10 | +12V_IN | GND | 12 V power cable |
| SW1 | 5X8_P10 | +12V_IN | +12V_SW | Two wires to off-board SPST switch |
| J2 | 5X8_P10 | MOTOR_A | MOTOR_B | Motor cable |
| J3 | 3X5_P5 | L_RAW | GND | Left microswitch NC and COM |
| J4 | 3X5_P5 | R_RAW | GND | Right microswitch NC and COM |

Use at least 2.5 mm copper or a suitable copper region on J1/SW1/J2 current
paths. Tin the high-current pads generously and use the user's selected motor
wire gauge. Add enclosure strain relief; the copper-to-FR4 bond must not
carry cable force.

## Release gate

Run `generated/01_prepare_audit_capture.js` after PCB conversion. The workflow
rejects J1-J4 or SW1 if any contains a drilled pad. This catches accidental use
of the old screw-terminal or rocker-switch footprints.
