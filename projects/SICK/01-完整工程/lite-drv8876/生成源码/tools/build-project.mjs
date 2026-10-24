import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadProjectConfig, validateProjectConfig } from '../core/project-config.mjs';

const toolDir = path.dirname(fileURLToPath(import.meta.url));
const root = path.dirname(toolDir);
const requestedPath = process.argv[2];
if (!requestedPath) throw new Error('Usage: node tools/build-project.mjs <project.config.mjs>');

const { config, configPath } = await loadProjectConfig(path.resolve(root, requestedPath));
const validation = validateProjectConfig(config);
if (!validation.ok) {
  console.error('PROJECT CONFIG: RED');
  for (const failure of validation.failures) console.error(` - ${failure}`);
  process.exitCode = 1;
} else {
  const templatePath = path.join(root, 'core', 'easyeda', 'prepare_audit_capture.template.js');
  const outputPath = path.resolve(root, config.workflow.generatedScript);
  const template = await readFile(templatePath, 'utf8');
  const token = '__SKYWORKS_PROJECT_CONFIG__';
  if (template.split(token).length !== 2) {
    throw new Error(`Template must contain exactly one ${token} token`);
  }
  const embedded = {
    id: config.id,
    name: config.name,
    revision: config.revision,
    pcb: config.pcb,
    workflow: {
      captureDownloadName: config.workflow.captureDownloadName
    }
  };
  const generated = template.replace(token, JSON.stringify(embedded, null, 2));
  await mkdir(path.dirname(outputPath), { recursive: true });
  await writeFile(outputPath, generated, 'utf8');
  console.log('PROJECT CONFIG: GREEN');
  console.log(`Config: ${path.relative(root, configPath)}`);
  console.log(`References: ${validation.stats.expectedRefs}`);
  console.log(`Critical components: ${validation.stats.criticalRefs}`);
  console.log(`Generated: ${path.relative(root, outputPath)}`);
}
