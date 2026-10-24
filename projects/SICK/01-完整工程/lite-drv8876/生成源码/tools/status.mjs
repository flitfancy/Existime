import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadProjectConfig, validateProjectConfig } from '../core/project-config.mjs';
import { validateCapture } from '../core/capture-validation.mjs';

const toolDir = path.dirname(fileURLToPath(import.meta.url));
const root = path.dirname(toolDir);
const requestedPath = process.argv[2];
if (!requestedPath) throw new Error('Usage: node tools/status.mjs <project.config.mjs>');

const { config } = await loadProjectConfig(path.resolve(root, requestedPath));
const configResult = validateProjectConfig(config);
console.log(`${config.name} ${config.revision}`);
console.log('='.repeat(`${config.name} ${config.revision}`.length));
if (!configResult.ok) {
  console.error('STATE: CONFIG RED');
  for (const failure of configResult.failures) console.error(` - ${failure}`);
  console.error('NEXT: repair the project configuration, then rebuild.');
  process.exitCode = 1;
} else {
  console.log(`Config: GREEN (${configResult.stats.expectedRefs} expected components)`);
  let capture;
  try {
    capture = JSON.parse(await readFile(path.resolve(config.workflow.captureFile), 'utf8'));
  } catch (error) {
    console.log('Capture: MISSING/UNREADABLE');
    console.log(`NEXT: build and run ${config.workflow.generatedScript} on the target PCB document.`);
    process.exitCode = 2;
  }
  if (capture) {
    const result = validateCapture(capture, config);
    console.log(`Capture: ${result.ok ? 'GREEN' : 'RED'} (${result.stats.components} components, ${result.stats.pads} pads, native DRC ${result.stats.nativeDrcStatus})`);
    if (!result.ok) {
      for (const failure of result.failures) console.log(` - ${failure}`);
      console.log(`NEXT: run ${config.workflow.generatedScript} on the target PCB document, confirm only configured repairs, save, and recapture.`);
      process.exitCode = 2;
    } else {
      try {
        const geometry = JSON.parse(await readFile(path.resolve(config.workflow.geometryFile), 'utf8'));
        await readFile(path.resolve(config.workflow.generatedPlacementScript), 'utf8');
        await readFile(path.resolve(config.workflow.generatedPlacementVerification), 'utf8');
        console.log(`Placement: READY (${Object.keys(geometry.componentProfiles || {}).length} component profiles)`);
        console.log(`NEXT: run ${config.workflow.generatedPlacementScript} on PCB1, save, inspect, then run ${config.workflow.generatedPlacementVerification}.`);
      } catch (error) {
        console.log('Placement: NOT BUILT');
        console.log('NEXT: build real footprint geometry and the offline-validated placement revision.');
      }
    }
  }
}
