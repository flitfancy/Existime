# SICK-E Lite R4 design workflow

Standalone data contract, EasyEDA generator and validation tools for
`S:\NatureCraft\sick-e_lite`.

## Current checkpoint

- Requirements and architecture are frozen in
  `projects/sick_motor_reverser_r4_lite/DESIGN.md`.
- DRV8876PWPR replaces VNH5019 and uses the retained PH + enable semantics.
- The R7/C8 preset edge is squared with two spare 74HC14 gates.
- The five direct-wire interfaces remain excluded from BOM and enabled for PCB.
- The standalone schematic generator preflights all exact LCSC devices and
  checks that all 31 designators were placed exactly once.
- C1 directly uses the verified C596320 standard-1210 device as a
  symbol/footprint donor with BOM disabled and PCB enabled; it does not depend
  on a personal stock-capacitor device association.
- Static regression verifies every scripted pin-to-net stub against
  `project.config.mjs`.
- The live PCB capture is GREEN: 31 components, 102 pads, and native DRC
  completed with zero returned items.
- R4-L2 packs the captured footprints into a 50 x 30 mm candidate and passes
  the offline board-edge, zone, assembly-collision and copper-clearance checks.
- The apply script uses transactional move/readback/rollback behavior derived
  from the proven FIREFLY AUDIO PCB V5 workflow; board outline remains a
  separate checkpoint.

## Commands

```powershell
npm.cmd run build
npm.cmd test
npm.cmd run check:schematic
npm.cmd run check:pcb-plan
npm.cmd run status
```

`status` reports whether the capture and generated placement scripts are ready.

## EasyEDA order

1. Read `projects/sick_motor_reverser_r4_lite/WIRE_PAD_FOOTPRINT.md`.
2. On a new blank schematic page run
   `generated/00_create_schematic_r4_lite.js`.
3. Run `generated/00b_apply_wirepad_flags.js`, save, then run ERC.
4. Confirm U1 is DRV8876PWPR with pins 1-16 and exposed PowerPAD 17/GND.
5. Convert to a new PCB and run `generated/01_prepare_audit_capture.js`.
6. Import the capture and validate real geometry.
7. Run `generated/02_apply_placement.js` on an unrouted `PCB1`, save and inspect
   it, then run `generated/03_verify_placement.js`.

The R3 source under `S:\NatureCraft\sick-e` is not modified by this workflow.
