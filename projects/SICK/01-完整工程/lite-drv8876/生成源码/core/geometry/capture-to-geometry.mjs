import { createHash } from 'node:crypto';

const finiteBox = (box) => (
  box && [box.minX, box.minY, box.maxX, box.maxY].every(Number.isFinite)
);

const round = (value) => Math.round(value * 1e6) / 1e6;

const rotation = (value) => {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? ((numeric % 360) + 360) % 360 : 0;
};

const inverseRotate = (x, y, originX, originY, degrees) => {
  const radians = rotation(degrees) * Math.PI / 180;
  const cosine = Math.cos(radians);
  const sine = Math.sin(radians);
  const dx = x - originX;
  const dy = y - originY;
  return {
    x: round(dx * cosine + dy * sine),
    y: round(-dx * sine + dy * cosine)
  };
};

const polygonPoints = (shape) => {
  if (!Array.isArray(shape) || String(shape[0] || '').toUpperCase() !== 'POLYGON') return null;
  const payload = Array.isArray(shape[1]) ? shape[1] : shape[1]?.value;
  if (!Array.isArray(payload)) return null;
  const commands = payload
    .filter((token) => typeof token === 'string' && !Number.isFinite(Number(token)))
    .map((token) => token.toUpperCase());
  if (commands.some((command) => !['L', 'M', 'Z'].includes(command))) return null;
  const coordinates = payload.map(Number).filter(Number.isFinite);
  if (coordinates.length < 6 || coordinates.length % 2 !== 0) return null;
  return Array.from({ length: coordinates.length / 2 }, (_, index) => ({
    x: coordinates[index * 2],
    y: coordinates[index * 2 + 1]
  }));
};

const derivedPolygonAabb = (component, pad) => {
  const points = polygonPoints(pad.shape);
  const originX = Number(component.x);
  const originY = Number(component.y);
  if (!points || !Number.isFinite(originX) || !Number.isFinite(originY)) return null;
  const local = points.map((point) => (
    inverseRotate(point.x, point.y, originX, originY, component.rotation)
  ));
  return {
    minX: Math.min(...local.map((point) => point.x)),
    minY: Math.min(...local.map((point) => point.y)),
    maxX: Math.max(...local.map((point) => point.x)),
    maxY: Math.max(...local.map((point) => point.y))
  };
};

const boundsForPads = (pads) => {
  const boxes = pads.map((pad) => pad.localCopperAabb).filter(finiteBox);
  if (boxes.length !== pads.length || !boxes.length) {
    throw new Error('Every pad must contain a finite localCopperAabb');
  }
  return {
    minX: Math.min(...boxes.map((box) => box.minX)),
    minY: Math.min(...boxes.map((box) => box.minY)),
    maxX: Math.max(...boxes.map((box) => box.maxX)),
    maxY: Math.max(...boxes.map((box) => box.maxY))
  };
};

const profileFor = (component) => {
  const pads = [...component.pads]
    .sort((left, right) => String(left.number).localeCompare(String(right.number), undefined, { numeric: true }))
    .map((pad) => ({
      number: String(pad.number),
      layer: Number(pad.layer),
      shape: pad.shape,
      localCopperAabb: finiteBox(pad.localCopperAabb)
        ? pad.localCopperAabb
        : derivedPolygonAabb(component, pad)
    }));
  const profile = {
    footprint: component.footprint || {},
    copperBounds: boundsForPads(pads),
    pads
  };
  const signature = JSON.stringify(profile);
  return {
    key: `fp-${createHash('sha256').update(signature).digest('hex').slice(0, 16)}`,
    signature,
    profile
  };
};

export function captureToFootprintGeometry(capture, project) {
  const profilesBySignature = new Map();
  const componentProfiles = {};
  const componentPadNets = {};

  for (const component of capture.components || []) {
    const ref = String(component.ref || '');
    if (!ref) throw new Error('Capture contains a component with a blank reference');
    const normalized = profileFor(component);
    if (!profilesBySignature.has(normalized.signature)) {
      profilesBySignature.set(normalized.signature, {
        key: normalized.key,
        ...normalized.profile
      });
    }
    componentProfiles[ref] = normalized.key;
    componentPadNets[ref] = Object.fromEntries(
      component.pads.map((pad) => [String(pad.number), String(pad.net || '')])
    );
  }

  return {
    schema: 'skyworks-footprint-geometry/v1',
    project: {
      id: project.id,
      revision: project.revision
    },
    source: {
      captureSchema: capture.schema,
      componentCount: capture.components?.length || 0,
      generatedAt: new Date().toISOString()
    },
    profiles: [...profilesBySignature.values()].sort((left, right) => left.key.localeCompare(right.key)),
    componentProfiles,
    componentPadNets
  };
}
