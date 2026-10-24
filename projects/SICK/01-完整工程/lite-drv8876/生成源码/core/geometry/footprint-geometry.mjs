import fs from 'node:fs';

const EPSILON = 1e-6;

export const naturalCompare = (left, right) => String(left).localeCompare(
  String(right),
  undefined,
  { numeric: true, sensitivity: 'base' }
);

const normalizeRotation = (value) => ((Number(value) % 360) + 360) % 360;

const rotatePoint = (x, y, rotation) => {
  const angle = normalizeRotation(rotation);
  if (angle === 0) return { x, y };
  if (angle === 90) return { x: -y, y: x };
  if (angle === 180) return { x: -x, y: -y };
  if (angle === 270) return { x: y, y: -x };
  const radians = angle * Math.PI / 180;
  return {
    x: x * Math.cos(radians) - y * Math.sin(radians),
    y: x * Math.sin(radians) + y * Math.cos(radians)
  };
};

const transformAabb = (local, originX, originY, rotation) => {
  const points = [
    rotatePoint(local.minX, local.minY, rotation),
    rotatePoint(local.minX, local.maxY, rotation),
    rotatePoint(local.maxX, local.minY, rotation),
    rotatePoint(local.maxX, local.maxY, rotation)
  ];
  const xs = points.map((point) => point.x + originX);
  const ys = points.map((point) => point.y + originY);
  return {
    minX: Math.min(...xs),
    minY: Math.min(...ys),
    maxX: Math.max(...xs),
    maxY: Math.max(...ys)
  };
};

const inflate = (box, amount) => ({
  minX: box.minX - amount,
  minY: box.minY - amount,
  maxX: box.maxX + amount,
  maxY: box.maxY + amount
});

const intersects = (left, right) => (
  left.maxX > right.minX + EPSILON &&
  left.minX < right.maxX - EPSILON &&
  left.maxY > right.minY + EPSILON &&
  left.minY < right.maxY - EPSILON
);

const contains = (outer, inner) => (
  inner.minX >= outer.x0 - EPSILON &&
  inner.maxX <= outer.x1 + EPSILON &&
  inner.minY >= outer.y0 - EPSILON &&
  inner.maxY <= outer.y1 + EPSILON
);

const aabbDistance = (left, right) => {
  const dx = Math.max(0, left.minX - right.maxX, right.minX - left.maxX);
  const dy = Math.max(0, left.minY - right.maxY, right.minY - left.maxY);
  return Math.hypot(dx, dy);
};

const padLayersOverlap = (left, right) => {
  const leftLayer = Number(left.layer);
  const rightLayer = Number(right.layer);
  return leftLayer === 12 || rightLayer === 12 || leftLayer === rightLayer;
};

export function loadFootprintGeometry(filePath) {
  const manifest = JSON.parse(fs.readFileSync(filePath, 'utf8'));
  if (![ 'skyworks-footprint-geometry/v1', 'firefly-footprint-geometry/v1' ].includes(manifest.schema)) {
    throw new Error(`Unsupported footprint geometry schema: ${manifest.schema}`);
  }
  const profiles = new Map(manifest.profiles.map((profile) => [profile.key, profile]));
  return { manifest, profiles };
}

export function placeFootprintGeometry(placement, geometry, assemblyClearanceMil = 20) {
  const profileKey = geometry.manifest.componentProfiles[placement.ref];
  const profile = geometry.profiles.get(profileKey);
  if (!profile) throw new Error(`${placement.ref}: missing footprint geometry profile`);
  const padNets = geometry.manifest.componentPadNets?.[placement.ref] || {};
  const copperBounds = transformAabb(
    profile.copperBounds,
    placement.x,
    placement.y,
    placement.rotation
  );
  const pads = profile.pads.map((pad) => ({
    ref: placement.ref,
    number: pad.number,
    net: String(padNets[pad.number] || ''),
    layer: pad.layer,
    shape: pad.shape,
    bounds: transformAabb(
      pad.localCopperAabb,
      placement.x,
      placement.y,
      placement.rotation
    )
  }));
  return {
    ...placement,
    profileKey,
    footprintName: profile.footprint?.name || profileKey,
    copperBounds,
    assemblyBounds: inflate(copperBounds, assemblyClearanceMil / 2),
    pads
  };
}

export function validateFootprintPlacement({
  placements,
  geometry,
  expectedRefs,
  board,
  zones = [],
  corridors = [],
  assemblyClearanceMil = 20,
  copperClearanceMil = 6,
  protectedAreas = [],
  pairRules = []
}) {
  const failures = [];
  const warnings = [];
  const counts = new Map();
  for (const placement of placements) {
    counts.set(placement.ref, (counts.get(placement.ref) || 0) + 1);
    if (![placement.x, placement.y, placement.rotation].every(Number.isFinite)) {
      failures.push(`${placement.ref}: non-finite placement value`);
    }
    if (normalizeRotation(placement.rotation) % 90 !== 0) {
      failures.push(`${placement.ref}: rotation must be orthogonal`);
    }
  }
  const duplicateRefs = [...counts].filter(([, count]) => count !== 1);
  if (duplicateRefs.length) {
    failures.push(`duplicate refs: ${duplicateRefs.map(([ref, count]) => `${ref} x${count}`).join(', ')}`);
  }
  const actualSet = new Set(placements.map((placement) => placement.ref));
  const expectedSet = new Set(expectedRefs);
  const missing = expectedRefs.filter((ref) => !actualSet.has(ref));
  const unexpected = [...actualSet].filter((ref) => !expectedSet.has(ref));
  if (missing.length) failures.push(`missing refs: ${missing.sort(naturalCompare).join(', ')}`);
  if (unexpected.length) failures.push(`unexpected refs: ${unexpected.sort(naturalCompare).join(', ')}`);

  const placed = placements.map((placement) => (
    placeFootprintGeometry(placement, geometry, assemblyClearanceMil)
  ));
  const byRef = new Map(placed.map((component) => [component.ref, component]));
  const boardArea = {
    x0: board.x0,
    y0: board.y0,
    x1: board.x0 + board.width,
    y1: board.y0 + board.height
  };
  const edgeArea = {
    x0: boardArea.x0 + (board.edgeClearanceMil || 0),
    y0: boardArea.y0 + (board.edgeClearanceMil || 0),
    x1: boardArea.x1 - (board.edgeClearanceMil || 0),
    y1: boardArea.y1 - (board.edgeClearanceMil || 0)
  };
  const outOfBounds = placed.filter((component) => !contains(edgeArea, component.copperBounds));
  if (outOfBounds.length) {
    failures.push(`copper outside board-edge envelope: ${outOfBounds.map((item) => item.ref).sort(naturalCompare).join(', ')}`);
  }

  const zoneMap = new Map(zones.map((zone) => [zone.id, zone]));
  for (const component of placed) {
    const zone = zoneMap.get(component.zone);
    if (!zone) {
      failures.push(`${component.ref}: unknown zone ${component.zone || '(blank)'}`);
      continue;
    }
    if (!contains(zone.areaMil, component.assemblyBounds)) {
      failures.push(`${component.ref}: assembly envelope outside ${zone.id}`);
    }
  }

  for (const corridor of corridors) {
    const intruders = placed.filter((component) => intersects(component.assemblyBounds, {
      minX: corridor.areaMil.x0,
      minY: corridor.areaMil.y0,
      maxX: corridor.areaMil.x1,
      maxY: corridor.areaMil.y1
    }));
    if (intruders.length) {
      failures.push(`${corridor.id} corridor intruders: ${intruders.map((item) => item.ref).sort(naturalCompare).join(', ')}`);
    }
  }

  const assemblyCollisions = [];
  const copperCollisions = [];
  let minimumCopperDistance = Number.POSITIVE_INFINITY;
  for (let leftIndex = 0; leftIndex < placed.length; leftIndex += 1) {
    const left = placed[leftIndex];
    for (let rightIndex = leftIndex + 1; rightIndex < placed.length; rightIndex += 1) {
      const right = placed[rightIndex];
      if (intersects(left.assemblyBounds, right.assemblyBounds)) {
        assemblyCollisions.push(`${left.ref}<->${right.ref}`);
      }
      if (aabbDistance(left.copperBounds, right.copperBounds) >= copperClearanceMil) continue;
      for (const leftPad of left.pads) {
        for (const rightPad of right.pads) {
          if (!padLayersOverlap(leftPad, rightPad)) continue;
          if (leftPad.net && leftPad.net === rightPad.net) continue;
          const distance = aabbDistance(leftPad.bounds, rightPad.bounds);
          minimumCopperDistance = Math.min(minimumCopperDistance, distance);
          if (distance + EPSILON < copperClearanceMil) {
            copperCollisions.push(
              `${left.ref}.${leftPad.number}[${leftPad.net || 'NoNet'}]` +
              `<->${right.ref}.${rightPad.number}[${rightPad.net || 'NoNet'}] ` +
              `${distance.toFixed(2)} mil`
            );
          }
        }
      }
    }
  }
  if (assemblyCollisions.length) {
    failures.push(`assembly-envelope collisions: ${assemblyCollisions.join(', ')}`);
  }
  if (copperCollisions.length) {
    failures.push(`copper clearance < ${copperClearanceMil} mil: ${copperCollisions.join(', ')}`);
  }

  for (const area of protectedAreas) {
    const areaBox = {
      minX: area.areaMil.x0,
      minY: area.areaMil.y0,
      maxX: area.areaMil.x1,
      maxY: area.areaMil.y1
    };
    const allowed = new Set(area.allowRefs || []);
    const intruders = placed.filter((component) => (
      !allowed.has(component.ref) && intersects(component.assemblyBounds, areaBox)
    ));
    if (intruders.length) {
      failures.push(`${area.id} protected-area intruders: ${intruders.map((item) => item.ref).sort(naturalCompare).join(', ')}`);
    }
  }

  for (const rule of pairRules) {
    const leftRefs = rule.leftRefs || [];
    const rightRefs = rule.rightRefs || [];
    const violations = [];
    for (const leftRef of leftRefs) {
      for (const rightRef of rightRefs) {
        const left = byRef.get(leftRef);
        const right = byRef.get(rightRef);
        if (!left || !right) continue;
        const distance = aabbDistance(left.assemblyBounds, right.assemblyBounds);
        if (distance + EPSILON < rule.minimumMil) {
          violations.push(`${leftRef}<->${rightRef} ${distance.toFixed(1)} mil`);
        }
      }
    }
    if (violations.length) failures.push(`${rule.id}: ${violations.join(', ')}`);
  }

  return {
    ok: failures.length === 0,
    failures,
    warnings,
    placed,
    stats: {
      components: placed.length,
      pads: placed.reduce((sum, component) => sum + component.pads.length, 0),
      assemblyCollisions: assemblyCollisions.length,
      copperCollisions: copperCollisions.length,
      minimumCopperDistance: Number.isFinite(minimumCopperDistance)
        ? minimumCopperDistance
        : null,
      outOfBounds: outOfBounds.length
    }
  };
}
