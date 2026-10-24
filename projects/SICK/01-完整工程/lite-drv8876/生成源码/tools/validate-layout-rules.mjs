import path from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';
import { loadProjectConfig, validateProjectConfig } from '../core/project-config.mjs';
import { validateLayoutRules } from '../core/layout-rules-validation.mjs';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const requestedConfig = process.argv[2];
if (!requestedConfig) throw new Error('Usage: node tools/validate-layout-rules.mjs <project.config.mjs>');

const { config } = await loadProjectConfig(path.resolve(root, requestedConfig));
const configResult = validateProjectConfig(config);
if (!configResult.ok) throw new Error(`Project config is RED:\n${configResult.failures.join('\n')}`);
const rulesPath = path.resolve(root, config.workflow.layoutRulesFile);
const rules = (await import(`${pathToFileURL(rulesPath).href}?t=${Date.now()}`)).default;
const result = validateLayoutRules(rules, config);
console.log(`LAYOUT RULES: ${result.ok ? 'GREEN' : 'RED'}`);
console.log(`Zones: ${result.stats.zones}; assigned refs: ${result.stats.assignedRefs}; corridors: ${result.stats.corridors}`);
for (const failure of result.failures) console.log(` - ${failure}`);
if (!result.ok) process.exitCode = 2;
