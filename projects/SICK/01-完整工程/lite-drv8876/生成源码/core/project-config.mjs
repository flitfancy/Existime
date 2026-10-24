import path from 'node:path';
import { pathToFileURL } from 'node:url';

export const naturalCompare = (left, right) => String(left).localeCompare(
  String(right),
  undefined,
  { numeric: true, sensitivity: 'base' }
);

export const range = (prefix, first, last) => Array.from(
  { length: last - first + 1 },
  (_, index) => `${prefix}${first + index}`
);

export async function loadProjectConfig(configPath) {
  const absolutePath = path.resolve(configPath);
  const module = await import(`${pathToFileURL(absolutePath).href}?t=${Date.now()}`);
  return { config: module.default, configPath: absolutePath };
}

export function validateProjectConfig(config) {
  const failures = [];
  if (config?.schema !== 'skyworks-hardware-project/v1') {
    failures.push(`schema: expected skyworks-hardware-project/v1, got ${config?.schema || '[missing]'}`);
  }
  for (const key of ['id', 'name', 'revision']) {
    if (!String(config?.[key] || '').trim()) failures.push(`${key}: missing`);
  }

  const refs = Array.isArray(config?.pcb?.expectedRefs) ? config.pcb.expectedRefs : [];
  const counts = new Map();
  for (const ref of refs) counts.set(ref, (counts.get(ref) || 0) + 1);
  const duplicates = [...counts].filter(([, count]) => count !== 1);
  if (duplicates.length) {
    failures.push(`expectedRefs duplicates: ${duplicates.map(([ref, count]) => `${ref} x${count}`).join(', ')}`);
  }
  if (refs.length !== Number(config?.pcb?.expectedComponentCount)) {
    failures.push(
      `expectedRefs count: expected ${config?.pcb?.expectedComponentCount}, got ${refs.length}`
    );
  }

  const padCounts = config?.pcb?.padCounts || {};
  const missingPadCounts = refs.filter((ref) => !Number.isInteger(padCounts[ref]) || padCounts[ref] < 1);
  if (missingPadCounts.length) failures.push(`padCounts missing/invalid: ${missingPadCounts.join(', ')}`);
  const stalePadCounts = Object.keys(padCounts).filter((ref) => !counts.has(ref));
  if (stalePadCounts.length) failures.push(`padCounts stale refs: ${stalePadCounts.join(', ')}`);

  const criticalNets = config?.pcb?.criticalNets || {};
  const staleCriticalRefs = Object.keys(criticalNets).filter((ref) => !counts.has(ref));
  if (staleCriticalRefs.length) failures.push(`criticalNets stale refs: ${staleCriticalRefs.join(', ')}`);

  for (const policy of config?.pcb?.footprintPolicies || []) {
    if (!counts.has(policy.ref)) failures.push(`footprint policy unknown ref: ${policy.ref}`);
    const required = (policy.requiredPads || []).map(String);
    const forbidden = (policy.forbiddenPads || []).map(String);
    const overlap = required.filter((number) => forbidden.includes(number));
    if (overlap.length) failures.push(`${policy.ref} policy required/forbidden overlap: ${overlap.join(', ')}`);
  }

  if (!String(config?.workflow?.captureFile || '').trim()) failures.push('workflow.captureFile: missing');
  if (!String(config?.workflow?.generatedScript || '').trim()) failures.push('workflow.generatedScript: missing');
  if (!String(config?.workflow?.geometryFile || '').trim()) failures.push('workflow.geometryFile: missing');
  if (!String(config?.workflow?.layoutRulesFile || '').trim()) failures.push('workflow.layoutRulesFile: missing');
  if (!String(config?.workflow?.placementModule || '').trim()) failures.push('workflow.placementModule: missing');
  if (!String(config?.workflow?.generatedPlacementScript || '').trim()) failures.push('workflow.generatedPlacementScript: missing');
  if (!String(config?.workflow?.generatedPlacementVerification || '').trim()) failures.push('workflow.generatedPlacementVerification: missing');

  return {
    ok: failures.length === 0,
    failures,
    stats: {
      expectedRefs: refs.length,
      criticalRefs: Object.keys(criticalNets).length,
      footprintPolicies: (config?.pcb?.footprintPolicies || []).length
    }
  };
}
