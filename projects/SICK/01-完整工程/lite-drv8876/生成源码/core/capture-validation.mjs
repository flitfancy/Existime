import { naturalCompare } from './project-config.mjs';

const padNumber = (pad) => String(pad?.number ?? '');
const padNet = (pad) => String(pad?.net ?? '');
const padHasDrill = (pad) => {
  const hole = pad?.hole;
  if (hole === null || hole === undefined || hole === '' || hole === false) return false;
  if (Array.isArray(hole)) return hole.length > 0;
  if (typeof hole === 'object') return Object.keys(hole).length > 0;
  return true;
};

export function validateCapture(capture, config) {
  const failures = [];
  const expectedRefs = config.pcb.expectedRefs;
  const expectedSet = new Set(expectedRefs);
  const components = Array.isArray(capture?.components) ? capture.components : [];
  const refs = components.map((component) => String(component?.ref || ''));
  const byRef = new Map();
  for (const component of components) {
    const ref = String(component?.ref || '');
    if (!byRef.has(ref)) byRef.set(ref, []);
    byRef.get(ref).push(component);
  }

  if (capture?.schema !== 'firefly-pcb-capture/v1') {
    failures.push(`capture schema: expected firefly-pcb-capture/v1, got ${capture?.schema || '[missing]'}`);
  }
  if (components.length !== config.pcb.expectedComponentCount) {
    failures.push(`component array count: expected ${config.pcb.expectedComponentCount}, got ${components.length}`);
  }
  if (Number(capture?.componentCount) !== config.pcb.expectedComponentCount) {
    failures.push(`componentCount: expected ${config.pcb.expectedComponentCount}, got ${capture?.componentCount}`);
  }

  const missing = expectedRefs.filter((ref) => !byRef.has(ref));
  const unexpected = [...new Set(refs)].filter((ref) => ref && !expectedSet.has(ref));
  const duplicates = [...byRef].filter(([, values]) => values.length !== 1);
  if (missing.length) failures.push(`missing refs: ${missing.sort(naturalCompare).join(', ')}`);
  if (unexpected.length) failures.push(`unexpected refs: ${unexpected.sort(naturalCompare).join(', ')}`);
  if (duplicates.length) {
    failures.push(`duplicate refs: ${duplicates.map(([ref, values]) => `${ref} x${values.length}`).join(', ')}`);
  }

  for (const ref of expectedRefs) {
    const component = byRef.get(ref)?.[0];
    if (!component || byRef.get(ref).length !== 1) continue;
    const pads = Array.isArray(component.pads) ? component.pads : [];
    const expectedCount = config.pcb.padCounts[ref];
    if (pads.length !== expectedCount) {
      failures.push(`${ref} pad count: expected ${expectedCount}, got ${pads.length}`);
    }
  }

  for (const [ref, expectedPins] of Object.entries(config.pcb.criticalNets || {})) {
    const component = byRef.get(ref)?.[0];
    if (!component || byRef.get(ref).length !== 1) continue;
    const pads = new Map((component.pads || []).map((pad) => [padNumber(pad), pad]));
    for (const [number, expectedNet] of Object.entries(expectedPins)) {
      const pad = pads.get(String(number));
      if (!pad) {
        failures.push(`${ref}.${number}: pad missing`);
        continue;
      }
      const actualNet = padNet(pad);
      if (actualNet !== String(expectedNet)) {
        failures.push(
          `${ref}.${number}: expected ${expectedNet || '[NC]'}, got ${actualNet || '[NC]'}`
        );
      }
    }
  }

  for (const policy of config.pcb.footprintPolicies || []) {
    const component = byRef.get(policy.ref)?.[0];
    if (!component) continue;
    const numbers = (component.pads || []).map(padNumber).sort(naturalCompare);
    for (const required of (policy.requiredPads || []).map(String)) {
      if (!numbers.includes(required)) failures.push(`${policy.ref}: required pad ${required} missing`);
    }
    for (const forbidden of (policy.forbiddenPads || []).map(String)) {
      if (numbers.includes(forbidden)) failures.push(`${policy.ref}: forbidden pad ${forbidden} present`);
    }
    if (policy.forbidDrilledPads) {
      const drilledPads = (component.pads || []).filter(padHasDrill).map(padNumber);
      if (drilledPads.length) {
        failures.push(`${policy.ref}: drilled pads forbidden, found ${drilledPads.join(', ')}`);
      }
    }
  }

  for (const reported of capture?.failures || []) {
    const text = String(reported);
    if (text && !failures.some((failure) => failure.includes(text))) {
      failures.push(`capture reported: ${text}`);
    }
  }

  return {
    ok: failures.length === 0,
    failures,
    stats: {
      components: components.length,
      pads: components.reduce((sum, component) => sum + (component.pads?.length || 0), 0),
      nativeDrcStatus: capture?.nativeDrc?.status || 'unknown'
    }
  };
}
