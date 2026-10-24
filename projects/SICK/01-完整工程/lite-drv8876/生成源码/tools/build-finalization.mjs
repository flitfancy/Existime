import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import {
  validateFinalizationConfig,
  validateMountingHoleClearance
} from '../core/finalization-validation.mjs';
import { loadFootprintGeometry } from '../core/geometry/footprint-geometry.mjs';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const requestedConfig = process.argv[2];
if (!requestedConfig) throw new Error('Usage: node tools/build-finalization.mjs <finalization.config.mjs>');

const configPath = path.resolve(root, requestedConfig);
const config = (await import(`${pathToFileURL(configPath).href}?t=${Date.now()}`)).default;
const result = validateFinalizationConfig(config);
if (!result.ok) throw new Error(`FINALIZATION CONFIG IS RED:\n${result.failures.map((item) => ` - ${item}`).join('\n')}`);

const importFresh = async (file) => (await import(`${pathToFileURL(file).href}?t=${Date.now()}`));
const geometry = loadFootprintGeometry(path.resolve(root, config.sources.geometryFile));
const placementModule = await importFresh(path.resolve(root, config.sources.placementModule));
if (typeof placementModule.createPlacements !== 'function') {
  throw new Error('sources.placementModule must export createPlacements().');
}
const mountingClearance = validateMountingHoleClearance({
  config,
  placements: placementModule.createPlacements(),
  geometry
});
if (!mountingClearance.ok) {
  throw new Error(`MOUNTING-HOLE CLEARANCE IS RED:\n${mountingClearance.failures.map((item) => ` - ${item}`).join('\n')}`);
}

const loadTemplate = (name) => readFile(path.join(root, 'core/easyeda', name), 'utf8');
const render = (template, token, payload) => {
  if (template.split(token).length !== 2) throw new Error(`Template must contain exactly one ${token}.`);
  return template.replace(token, JSON.stringify(payload, null, 2));
};
const writeOutput = async (relative, content) => {
  const output = path.resolve(root, relative);
  await mkdir(path.dirname(output), { recursive: true });
  await writeFile(output, content, 'utf8');
  console.log(`Generated: ${path.relative(root, output)}`);
};

const [mechanicalTemplate, routingTemplate, verifyTemplate] = await Promise.all([
  loadTemplate('mechanical-silkscreen.template.js'),
  loadTemplate('routing-stage.template.js'),
  loadTemplate('routing-verify.template.js')
]);

await writeOutput(config.outputs.mechanicalScript, render(
  mechanicalTemplate,
  '__SKYWORKS_FINALIZATION_JOB__',
  {
    project: config.project,
    boardMil: config.boardMil,
    mechanical: config.mechanical,
    silkscreen: config.silkscreen,
    sht41: config.sht41
  }
));

for (const stage of [...config.routing.stages].sort((a, b) => a.order - b.order)) {
  await writeOutput(stage.script, render(
    routingTemplate,
    '__SKYWORKS_ROUTING_JOB__',
    {
      project: config.project,
      routing: {
        allNets: config.routing.allNets,
        permanentlyIgnoredNets: config.routing.permanentlyIgnoredNets,
        ruleClasses: config.routing.ruleClasses
      },
      stage,
      sht41: config.sht41
    }
  ));
}

await writeOutput(config.outputs.verificationScript, render(
  verifyTemplate,
  '__SKYWORKS_ROUTING_VERIFY_JOB__',
  { project: config.project, sht41: config.sht41 }
));

const guide = `# ${config.project.name} ${config.project.revision} routing guide

Generated from \`finalization.config.mjs\`. Do not edit generated routing scripts by hand.

## Run order

1. Run \`04_apply_mounting_holes_and_silkscreen.js\`; inspect 2D/3D and save.
2. In EasyEDA Design Rules, create/assign the exact net classes below.
3. Run stage 05, inspect and save or undo.
4. Run stage 06, inspect and save or undo.
5. Run stage 07, inspect and save or undo.
6. Add a solid GND plane and useful stitching vias manually.
7. Run \`08_verify_routing.js\`, native DRC and the unconnected-net check.

## Required design-rule classes

${config.routing.ruleClasses.map((rule) =>
  `- **${rule.id}** - ${rule.widthMil} mil width, ${rule.clearanceMil} mil clearance\n  - ${rule.nets.join(', ')}`
).join('\n')}

GND is intentionally excluded from all three auto-routing stages. Route it with a continuous plane after the signal topology has been reviewed.

## Staged routing

${[...config.routing.stages].sort((a, b) => a.order - b.order).map((stage) =>
  `${stage.order}. **${stage.id}** - ${stage.instruction}\n   - ${stage.nets.join(', ')}`
).join('\n')}

## SHT41 / S1 special treatment

- Local copper-restriction rectangle: (${config.sht41.copperRestrictionMil.x0}, ${config.sht41.copperRestrictionMil.y0}) to (${config.sht41.copperRestrictionMil.x1}, ${config.sht41.copperRestrictionMil.y1}) mil.
${config.sht41.rules.map((rule) => `- ${rule}`).join('\n')}

This is a local thermal-isolation rule, not a split-ground rule. Keep heat-producing parts and wide power trunks outside the S1 zone; keep the board-wide ground plane continuous outside it.

## M2 mounting holes

- Finished drill: ${config.mechanical.holeDiameterMm} mm NPTH.
${config.mechanical.holes.map((hole) => `- ${hole.id}: (${hole.x}, ${hole.y}) mil`).join('\n')}

## Recovery rule

Each routing stage is deliberately separate. If a stage produces poor topology, undo that stage before saving, adjust placement or manual pre-routes, then rerun only that stage.
`;
await writeOutput(config.outputs.routingGuide, guide);

console.log(
  `FINALIZATION BUILD: GREEN (${result.stats.holes} holes, ${result.stats.silkscreenItems} labels, ` +
  `${result.stats.routedNets} staged nets, minimum hole-to-component copper clearance ` +
  `${mountingClearance.stats.minimumCopperClearanceMil.toFixed(2)} mil)`
);
