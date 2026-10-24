import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';
import { loadProjectConfig, validateProjectConfig } from '../core/project-config.mjs';
import { validateCapture } from '../core/capture-validation.mjs';
import { validateLayoutRules } from '../core/layout-rules-validation.mjs';
import { loadFootprintGeometry, validateFootprintPlacement } from '../core/geometry/footprint-geometry.mjs';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const requestedConfig = process.argv[2];
if (!requestedConfig) throw new Error('Usage: node tools/build-placement.mjs <project.config.mjs>');
const importFresh = async (file) => (await import(`${pathToFileURL(file).href}?t=${Date.now()}`));

const { config } = await loadProjectConfig(path.resolve(root, requestedConfig));
const configResult = validateProjectConfig(config);
if (!configResult.ok) throw new Error(`Project config is RED:\n${configResult.failures.join('\n')}`);
const capture = JSON.parse(await readFile(path.resolve(root, config.workflow.captureFile), 'utf8'));
const captureResult = validateCapture(capture, config);
if (!captureResult.ok) {
  console.error('PLACEMENT BUILD: REFUSED because capture is RED');
  for (const failure of captureResult.failures) console.error(` - ${failure}`);
  process.exitCode = 2;
} else {
  const rules = (await importFresh(path.resolve(root, config.workflow.layoutRulesFile))).default;
  const rulesResult = validateLayoutRules(rules, config);
  if (!rulesResult.ok) throw new Error(`Layout rules are RED:\n${rulesResult.failures.join('\n')}`);
  const geometry = loadFootprintGeometry(path.resolve(root, config.workflow.geometryFile));
  const placementModule = await importFresh(path.resolve(root, config.workflow.placementModule));
  if (typeof placementModule.createPlacements !== 'function') {
    throw new Error('placementModule must export createPlacements()');
  }
  const placements = placementModule.createPlacements();
  const result = validateFootprintPlacement({
    placements,
    geometry,
    expectedRefs: config.pcb.expectedRefs,
    board: {
      x0: 0,
      y0: 0,
      width: rules.boardMil.width,
      height: rules.boardMil.height,
      edgeClearanceMil: rules.boardMil.edgeClearance
    },
    zones: rules.zones,
    corridors: rules.corridors,
    assemblyClearanceMil: rules.clearancesMil.assembly,
    copperClearanceMil: rules.clearancesMil.copper,
    protectedAreas: rules.protectedAreas,
    pairRules: rules.pairRules
  });
  if (!result.ok) throw new Error(`Offline placement is RED:\n${result.failures.join('\n')}`);

  const template = await readFile(path.join(root, 'core/easyeda/placement.template.js'), 'utf8');
  const token = '__SKYWORKS_PLACEMENT_JOB__';
  if (template.split(token).length !== 2) throw new Error(`Placement template must contain one ${token}`);
  const render = (mode) => template.replace(token, JSON.stringify({
    project: { id: config.id, name: config.name },
    revision: rules.revision,
    mode,
    board: {
      x0: 0,
      y0: 0,
      width: rules.boardMil.width,
      height: rules.boardMil.height,
      widthMm: config.board.widthMm,
      heightMm: config.board.heightMm,
      outlineWidth: 1
    },
    placements
  }, null, 2));
  const outputs = [
    [config.workflow.generatedPlacementScript, render('apply')],
    [config.workflow.generatedPlacementVerification, render('verify')]
  ];
  for (const [relative, source] of outputs) {
    const output = path.resolve(root, relative);
    await mkdir(path.dirname(output), { recursive: true });
    await writeFile(output, source, 'utf8');
    console.log(`Generated: ${path.relative(root, output)}`);
  }
  console.log(`PLACEMENT BUILD: GREEN (${result.stats.components} components, ${result.stats.pads} pads, zero offline collisions)`);
}
