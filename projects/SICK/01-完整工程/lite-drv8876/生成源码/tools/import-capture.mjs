import { mkdir, readFile, rename, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadProjectConfig, validateProjectConfig } from '../core/project-config.mjs';
import { validateCapture } from '../core/capture-validation.mjs';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const [requestedConfig, requestedCapture] = process.argv.slice(2);
if (!requestedConfig || !requestedCapture) {
  throw new Error('Usage: node tools/import-capture.mjs <project.config.mjs> <downloaded-capture.json>');
}

const { config } = await loadProjectConfig(path.resolve(root, requestedConfig));
const configResult = validateProjectConfig(config);
if (!configResult.ok) throw new Error(`Project config is RED:\n${configResult.failures.join('\n')}`);

const sourcePath = path.resolve(requestedCapture);
const sourceText = await readFile(sourcePath, 'utf8');
const capture = JSON.parse(sourceText);
const result = validateCapture(capture, config);
const destination = path.resolve(root, config.workflow.captureFile);
const temporary = `${destination}.incoming`;
await mkdir(path.dirname(destination), { recursive: true });
await writeFile(temporary, `${JSON.stringify(capture, null, 2)}\n`, 'utf8');
await rename(temporary, destination);

console.log(`Imported: ${sourcePath}`);
console.log(`Stored: ${destination}`);
console.log(`Capture: ${result.ok ? 'GREEN' : 'RED'}`);
for (const failure of result.failures) console.log(` - ${failure}`);
if (!result.ok) process.exitCode = 2;
