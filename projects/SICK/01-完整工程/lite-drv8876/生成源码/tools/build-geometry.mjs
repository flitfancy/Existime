import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadProjectConfig, validateProjectConfig } from '../core/project-config.mjs';
import { validateCapture } from '../core/capture-validation.mjs';
import { captureToFootprintGeometry } from '../core/geometry/capture-to-geometry.mjs';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const requestedConfig = process.argv[2];
if (!requestedConfig) throw new Error('Usage: node tools/build-geometry.mjs <project.config.mjs>');

const { config } = await loadProjectConfig(path.resolve(root, requestedConfig));
const configResult = validateProjectConfig(config);
if (!configResult.ok) throw new Error(`Project config is RED:\n${configResult.failures.join('\n')}`);
if (!config.workflow.geometryFile) throw new Error('workflow.geometryFile is required');

const capture = JSON.parse(await readFile(path.resolve(root, config.workflow.captureFile), 'utf8'));
const captureResult = validateCapture(capture, config);
if (!captureResult.ok) {
  console.error('GEOMETRY BUILD: REFUSED because capture is RED');
  for (const failure of captureResult.failures) console.error(` - ${failure}`);
  process.exitCode = 2;
} else {
  const geometry = captureToFootprintGeometry(capture, config);
  const output = path.resolve(root, config.workflow.geometryFile);
  await mkdir(path.dirname(output), { recursive: true });
  await writeFile(output, `${JSON.stringify(geometry, null, 2)}\n`, 'utf8');
  console.log('GEOMETRY BUILD: GREEN');
  console.log(`Components: ${Object.keys(geometry.componentProfiles).length}`);
  console.log(`Unique profiles: ${geometry.profiles.length}`);
  console.log(`Generated: ${path.relative(root, output)}`);
}
