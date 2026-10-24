import { naturalCompare } from './project-config.mjs';

const validArea = (area) => (
  area && [area.x0, area.y0, area.x1, area.y1].every(Number.isFinite)
  && area.x1 > area.x0 && area.y1 > area.y0
);

const refsFromRule = (rule) => Array.isArray(rule?.refs) ? rule.refs.map(String) : [];

export function validateLayoutRules(rules, config) {
  const failures = [];
  if (rules?.schema !== 'skyworks-layout-rules/v1') {
    failures.push(`schema: expected skyworks-layout-rules/v1, got ${rules?.schema || '[missing]'}`);
  }
  if (rules?.projectId !== config.id) failures.push(`projectId: expected ${config.id}, got ${rules?.projectId}`);

  const board = rules?.boardMil || {};
  if (![board.width, board.height].every(Number.isFinite) || board.width <= 0 || board.height <= 0) {
    failures.push('boardMil: invalid width/height');
  }

  const zones = Array.isArray(rules?.zones) ? rules.zones : [];
  const zoneIds = new Set();
  const counts = new Map();
  for (const zone of zones) {
    if (!String(zone?.id || '').trim()) failures.push('zone with blank id');
    if (zoneIds.has(zone.id)) failures.push(`duplicate zone id: ${zone.id}`);
    zoneIds.add(zone.id);
    if (!validArea(zone.areaMil)) failures.push(`${zone.id}: invalid areaMil`);
    else if (zone.areaMil.x0 < 0 || zone.areaMil.y0 < 0 || zone.areaMil.x1 > board.width || zone.areaMil.y1 > board.height) {
      failures.push(`${zone.id}: area outside board`);
    }
    for (const ref of refsFromRule(zone)) counts.set(ref, (counts.get(ref) || 0) + 1);
  }

  const expected = config.pcb.expectedRefs;
  const expectedSet = new Set(expected);
  const missing = expected.filter((ref) => !counts.has(ref)).sort(naturalCompare);
  const repeated = [...counts].filter(([, count]) => count !== 1).map(([ref, count]) => `${ref} x${count}`).sort(naturalCompare);
  const stale = [...counts.keys()].filter((ref) => !expectedSet.has(ref)).sort(naturalCompare);
  if (missing.length) failures.push(`unassigned refs: ${missing.join(', ')}`);
  if (repeated.length) failures.push(`multiply assigned refs: ${repeated.join(', ')}`);
  if (stale.length) failures.push(`unknown refs: ${stale.join(', ')}`);

  for (const corridor of rules?.corridors || []) {
    if (!validArea(corridor.areaMil)) failures.push(`${corridor.id}: invalid corridor area`);
    else if (corridor.areaMil.x0 < 0 || corridor.areaMil.y0 < 0 || corridor.areaMil.x1 > board.width || corridor.areaMil.y1 > board.height) {
      failures.push(`${corridor.id}: corridor outside board`);
    }
  }

  for (const area of rules?.protectedAreas || []) {
    if (!validArea(area.areaMil)) failures.push(`${area.id}: invalid protected area`);
    for (const ref of area.allowRefs || []) {
      if (!expectedSet.has(ref)) failures.push(`${area.id}: unknown allowRef ${ref}`);
    }
  }
  for (const rule of rules?.pairRules || []) {
    if (!Number.isFinite(rule.minimumMil) || rule.minimumMil < 0) failures.push(`${rule.id}: invalid minimumMil`);
    for (const ref of [...(rule.leftRefs || []), ...(rule.rightRefs || [])]) {
      if (!expectedSet.has(ref)) failures.push(`${rule.id}: unknown ref ${ref}`);
    }
  }

  return {
    ok: failures.length === 0,
    failures,
    stats: {
      zones: zones.length,
      assignedRefs: counts.size,
      corridors: (rules?.corridors || []).length,
      protectedAreas: (rules?.protectedAreas || []).length,
      pairRules: (rules?.pairRules || []).length
    }
  };
}
