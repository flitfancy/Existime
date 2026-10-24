import { placeFootprintGeometry } from './geometry/footprint-geometry.mjs';

const isFiniteNumber = (value) => Number.isFinite(Number(value));

const duplicates = (values) => {
  const seen = new Set();
  const repeated = new Set();
  for (const value of values) {
    if (seen.has(value)) repeated.add(value);
    seen.add(value);
  }
  return [...repeated];
};

export function validateFinalizationConfig(config) {
  const failures = [];
  if (!config || typeof config !== 'object') {
    return { ok: false, failures: ['Finalization config must be an object.'], stats: {} };
  }
  if (config.schema !== 'skyworks-pcb-finalization/v1') {
    failures.push(`Unsupported schema: ${config.schema || '<missing>'}.`);
  }
  const board = config.boardMil || {};
  for (const key of ['x0', 'y0', 'width', 'height']) {
    if (!isFiniteNumber(board[key])) failures.push(`boardMil.${key} must be finite.`);
  }
  if (!(Number(board.width) > 0) || !(Number(board.height) > 0)) {
    failures.push('Board width and height must be positive.');
  }
  if (!Number.isInteger(config.project?.expectedComponentCount) || config.project.expectedComponentCount < 1) {
    failures.push('project.expectedComponentCount must be a positive integer.');
  }
  if (!config.sources?.geometryFile || !config.sources?.placementModule) {
    failures.push('sources.geometryFile and sources.placementModule are required.');
  }

  const holes = config.mechanical?.holes || [];
  const diameterMm = Number(config.mechanical?.holeDiameterMm);
  const componentCopperClearanceMil = Number(config.mechanical?.componentCopperClearanceMil);
  if (holes.length !== 4) failures.push(`Exactly four mounting holes are required; found ${holes.length}.`);
  if (Math.abs(diameterMm - 2.2) > 0.001) failures.push(`M2 finished hole must be 2.2 mm; found ${diameterMm}.`);
  if (!(componentCopperClearanceMil >= 40)) {
    failures.push('mechanical.componentCopperClearanceMil must be at least 40 mil.');
  }
  for (const duplicate of duplicates(holes.map((hole) => hole.id))) failures.push(`Duplicate mounting-hole id: ${duplicate}.`);
  const radiusMil = diameterMm * 39.37007874015748 / 2;
  const edgeMarginMil = 40;
  for (const hole of holes) {
    if (!hole.id || !isFiniteNumber(hole.x) || !isFiniteNumber(hole.y)) {
      failures.push(`Invalid mounting-hole record: ${JSON.stringify(hole)}.`);
      continue;
    }
    const x = Number(hole.x);
    const y = Number(hole.y);
    const minX = Number(board.x0) + radiusMil + edgeMarginMil;
    const maxX = Number(board.x0) + Number(board.width) - radiusMil - edgeMarginMil;
    const minY = Number(board.y0) + radiusMil + edgeMarginMil;
    const maxY = Number(board.y0) + Number(board.height) - radiusMil - edgeMarginMil;
    if (x < minX || x > maxX || y < minY || y > maxY) {
      failures.push(`${hole.id} (${x}, ${y}) violates drill-to-board-edge margin.`);
    }
  }
  for (let left = 0; left < holes.length; left += 1) {
    for (let right = left + 1; right < holes.length; right += 1) {
      const dx = Number(holes[left].x) - Number(holes[right].x);
      const dy = Number(holes[left].y) - Number(holes[right].y);
      if (Math.hypot(dx, dy) < 300) failures.push(`${holes[left].id} and ${holes[right].id} are too close.`);
    }
  }

  const silkItems = config.silkscreen?.items || [];
  for (const duplicate of duplicates(silkItems.map((item) => item.id))) failures.push(`Duplicate silkscreen id: ${duplicate}.`);
  for (const item of silkItems) {
    if (!item.id || !item.text || !isFiniteNumber(item.x) || !isFiniteNumber(item.y)) {
      failures.push(`Invalid silkscreen record: ${JSON.stringify(item)}.`);
      continue;
    }
    if (Number(item.x) < Number(board.x0) || Number(item.x) > Number(board.x0) + Number(board.width) ||
        Number(item.y) < Number(board.y0) || Number(item.y) > Number(board.y0) + Number(board.height)) {
      failures.push(`${item.id} is outside the board.`);
    }
  }

  const allNets = config.routing?.allNets || [];
  for (const duplicate of duplicates(allNets)) failures.push(`Duplicate allNets entry: ${duplicate}.`);
  const classNets = (config.routing?.ruleClasses || []).flatMap((rule) => rule.nets || []);
  for (const duplicate of duplicates(classNets)) failures.push(`Net belongs to more than one rule class: ${duplicate}.`);
  const ignored = new Set(config.routing?.permanentlyIgnoredNets || []);
  if (!ignored.has('GND')) failures.push('GND must remain permanently ignored by staged routing.');
  const expectedRoutedNets = allNets.filter((net) => !ignored.has(net));
  for (const net of expectedRoutedNets) if (!classNets.includes(net)) failures.push(`Net has no rule class: ${net}.`);
  for (const net of classNets) {
    if (!allNets.includes(net)) failures.push(`Rule-class net is absent from allNets: ${net}.`);
    if (ignored.has(net)) failures.push(`Ignored net must not appear in a routing class: ${net}.`);
  }
  for (const rule of config.routing?.ruleClasses || []) {
    if (!(Number(rule.widthMil) > 0) || !(Number(rule.clearanceMil) > 0)) {
      failures.push(`Invalid width/clearance for rule class ${rule.id}.`);
    }
  }

  const stages = [...(config.routing?.stages || [])].sort((a, b) => a.order - b.order);
  const stageNets = stages.flatMap((stage) => stage.nets || []);
  if (stages.length !== 3 || stages.some((stage, index) => stage.order !== index + 1)) {
    failures.push('Routing stages must be exactly 1 POWER, 2 CRITICAL_SIGNAL, 3 OTHER.');
  }
  for (const duplicate of duplicates(stageNets)) failures.push(`Net belongs to more than one routing stage: ${duplicate}.`);
  for (const net of expectedRoutedNets) if (!stageNets.includes(net)) failures.push(`Routable net has no routing stage: ${net}.`);
  for (const net of stageNets) if (!expectedRoutedNets.includes(net)) failures.push(`Stage contains ignored or unknown net: ${net}.`);
  const outputPaths = [
    config.outputs?.mechanicalScript,
    config.outputs?.routingGuide,
    config.outputs?.verificationScript,
    ...stages.map((stage) => stage.script)
  ];
  if (outputPaths.some((value) => !value)) failures.push('Every generated output path must be defined.');
  for (const duplicate of duplicates(outputPaths.filter(Boolean))) failures.push(`Duplicate output path: ${duplicate}.`);

  const sht = config.sht41 || {};
  if (sht.ref !== 'S1') failures.push('SHT41 special rule must target S1.');
  for (const net of sht.routeNets || []) if (!allNets.includes(net)) failures.push(`SHT41 rule references unknown net: ${net}.`);
  const zone = sht.copperRestrictionMil || {};
  if (![zone.x0, zone.y0, zone.x1, zone.y1].every(isFiniteNumber) ||
      Number(zone.x1) <= Number(zone.x0) || Number(zone.y1) <= Number(zone.y0)) {
    failures.push('SHT41 copperRestrictionMil must be a valid rectangle.');
  }

  return {
    ok: failures.length === 0,
    failures,
    stats: {
      holes: holes.length,
      silkscreenItems: silkItems.length,
      nets: allNets.length,
      routedNets: stageNets.length,
      stages: stages.length
    }
  };
}

const pointToAabbDistance = (x, y, box) => {
  const dx = Math.max(0, box.minX - x, x - box.maxX);
  const dy = Math.max(0, box.minY - y, y - box.maxY);
  return Math.hypot(dx, dy);
};

export function validateMountingHoleClearance({ config, placements, geometry }) {
  const failures = [];
  if (!Array.isArray(placements) || !placements.length) {
    return { ok: false, failures: ['Mounting-hole validation requires placements.'], stats: {} };
  }
  const placed = placements.map((placement) => placeFootprintGeometry(placement, geometry, 0));
  const radiusMil = Number(config.mechanical.holeDiameterMm) * 39.37007874015748 / 2;
  const requiredMil = Number(config.mechanical.componentCopperClearanceMil);
  let minimumMil = Number.POSITIVE_INFINITY;
  let closest = null;
  for (const hole of config.mechanical.holes) {
    for (const component of placed) {
      const clearanceMil = pointToAabbDistance(Number(hole.x), Number(hole.y), component.copperBounds) - radiusMil;
      if (clearanceMil < minimumMil) {
        minimumMil = clearanceMil;
        closest = { hole: hole.id, ref: component.ref, clearanceMil };
      }
      if (clearanceMil + 1e-6 < requiredMil) {
        failures.push(
          `${hole.id}<->${component.ref} copper clearance ${clearanceMil.toFixed(2)} mil is below ${requiredMil} mil.`
        );
      }
    }
  }
  return {
    ok: failures.length === 0,
    failures,
    stats: {
      minimumCopperClearanceMil: Number.isFinite(minimumMil) ? minimumMil : null,
      closest
    }
  };
}
