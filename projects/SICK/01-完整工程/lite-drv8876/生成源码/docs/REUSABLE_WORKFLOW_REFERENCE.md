# FIREFLY REAL-WORLD SENSE

This directory is the durable engineering home for Firefly sensing hardware.
It separates reusable EasyEDA automation from the configuration and history of
one board revision.

## What belongs here

- design principles that should survive individual boards;
- one machine-readable configuration per hardware project/revision;
- reusable capture, audit, geometry, and placement validation code;
- generated EasyEDA scripts that can be rebuilt from configuration;
- regression tests proving bad inputs fail and good inputs pass;
- a concise current-state record and an explicit next action.

Chat transcripts, screenshots, and one-off coordinate repairs are evidence or
history, not sources of truth.

## Current project

`projects/firefly_r1_1` describes the 90 x 55 mm MCU-free static sensing board:
six environmental sensors, one protected extension channel, per-channel power
gating, I2C isolation, identity EEPROM, interface protection, and status LEDs.

The captured PCB is GREEN: 128 components and 419 real pads, with the corrected
four-pad SHT41 footprint. V6.4 placement has passed offline collision checks and
native PCB DRC. Finalization is now data-driven: mounting holes, silkscreen,
staged routing and routing verification are generated from one configuration.

## Commands

Run from this directory with Node.js 20 or newer:

```powershell
node tools/build-project.mjs projects/firefly_r1_1/project.config.mjs
node tools/import-capture.mjs projects/firefly_r1_1/project.config.mjs S:\Soft\Chrome\firefly_pcb_capture_r1_1.json
node tools/status.mjs projects/firefly_r1_1/project.config.mjs
node tools/build-geometry.mjs projects/firefly_r1_1/project.config.mjs
node tools/build-finalization.mjs projects/firefly_r1_1/finalization.config.mjs
node --test tests/*.test.mjs
```

`build-project` validates the single project configuration and emits one
EasyEDA preparation/audit/capture script. `import-capture` moves a downloaded
capture into the project source of truth. `status` prints the exact gate and
next action. `build-geometry` refuses RED captures. `build-finalization`
validates mechanical coordinates and routing ownership, then generates scripts
04 through 08 plus the exact routing-rule guide. Tests include bad and good
capture, net, footprint, collision, mounting-hole, and routing-stage paths.

## Structure

- `docs/` — design philosophy, retrospective, workflow, and current state.
- `core/` — project-independent validation and EasyEDA templates.
- `projects/` — project/revision-specific configuration and generated output.
- `tools/` — build, import, status, and geometry commands.
- `tests/` — deterministic regression tests.
- `archive/` — historical material that must not drive current generation.

## Stage gates

1. Configuration validates.
2. Schematic ERC and semantic intent checks pass.
3. PCB preparation/audit/capture passes.
4. Captured real footprint geometry imports cleanly.
5. Offline placement passes board, zone, assembly, copper, and physical rules.
6. EasyEDA placement applies and its post-check plus native DRC pass.
7. Apply M2 NPTH holes and silkscreen, then inspect 2D/3D.
8. Route power, critical signals and low-priority nets as separate checkpoints.
9. Add the GND plane, then complete DRC, unconnected-net and fabrication review.

No later gate may silently compensate for a failed earlier gate.
