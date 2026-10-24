# SICK-E Lite R4

The Lite design keeps the proven no-MCU, two-NC-limit behavior while replacing
the oversized H-bridge and deleting nonessential indicators/diagnostics.

Authoritative design files:

- `DESIGN.md`: electrical contract and pin map.
- `project.config.mjs`: machine-checkable references, pads and nets.
- `BOM_R4_LITE.csv`: sortable fitted BOM.
- `SCHEMATIC_DIFF_R3_TO_R4.md`: bounded R3-to-R4 changes.
- `generated/00_create_schematic_r4_lite.js`: EasyEDA schematic generator.
- `generated/02_apply_placement.js`: capture-validated compact R4-L2 placement.
- `generated/03_verify_placement.js`: read-only placement verification.

Do not place or route from inherited R3 coordinates. Run the schematic script,
ERC and live capture first. The archived 2026-08-12 capture and compact R4-L2
placement have passed the offline geometry check; routing still requires online
placement inspection and routed-board DRC.
