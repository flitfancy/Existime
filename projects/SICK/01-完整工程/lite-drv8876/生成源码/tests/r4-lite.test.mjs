import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import config from '../projects/sick_motor_reverser_r4_lite/project.config.mjs';
import layoutRules from '../projects/sick_motor_reverser_r4_lite/layout.rules.mjs';
import { createPlacements } from '../projects/sick_motor_reverser_r4_lite/placement.mjs';
import routingClasses from '../projects/sick_motor_reverser_r4_lite/routing.classes.mjs';
import { validateCapture } from '../core/capture-validation.mjs';
import { captureToFootprintGeometry } from '../core/geometry/capture-to-geometry.mjs';
import { validateLayoutRules } from '../core/layout-rules-validation.mjs';
import { validateProjectConfig } from '../core/project-config.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.dirname(here);
const schematicPath = path.resolve(
  root,
  'projects/sick_motor_reverser_r4_lite/generated/00_create_schematic_r4_lite.js'
);

test('geometry builder derives a component-local AABB for EasyEDA POLYGON pads', () => {
  const capture = {
    schema: 'firefly-pcb-capture/v1',
    components: [{
      ref: 'U2',
      x: 100,
      y: 200,
      rotation: 0,
      footprint: { name: 'SOT-89' },
      pads: [{
        number: '2',
        layer: 1,
        net: '+12V_PROT',
        shape: ['POLYGON', [90, 190, 'L', 110, 190, 'L', 110, 210, 'L', 90, 210]],
        localCopperAabb: null
      }]
    }]
  };

  const geometry = captureToFootprintGeometry(capture, { id: 'polygon-fixture', revision: 'test' });
  assert.deepEqual(geometry.profiles[0].pads[0].localCopperAabb, {
    minX: -10,
    minY: -10,
    maxX: 10,
    maxY: 10
  });
});

test('R4 Lite schematic contract is internally consistent and complete', () => {
  const result = validateProjectConfig(config);
  assert.equal(result.ok, true, result.failures.join('\n'));
  assert.equal(result.stats.expectedRefs, 31);
  assert.equal(config.pcb.padCounts.U1, 17);
  assert.equal(Object.keys(config.pcb.criticalNets).length, 31);
  assert.deepEqual(config.pcb.criticalNets.U2, { 1: 'GND', 2: '+12V_PROT', 3: '+3V3' });
  assert.equal(config.board.layers, 2);
});

test('R4-L2 packs all 31 references onto the 50 x 30 mm board and covers contract nets', () => {
  const layout = validateLayoutRules(layoutRules, config);
  assert.equal(layout.ok, true, layout.failures.join('\n'));
  assert.equal(layout.stats.zones, 5);
  assert.equal(layout.stats.assignedRefs, 31);
  assert.deepEqual(config.board, {
    widthMm: 50,
    heightMm: 30,
    layers: 2,
    assemblyHotplateMm: [100, 60]
  });
  assert.deepEqual(layoutRules.boardMil, { width: 1969, height: 1181, edgeClearance: 40 });

  const placements = createPlacements();
  const expectedRefs = [...config.pcb.expectedRefs].sort((a, b) => a.localeCompare(b, undefined, { numeric: true }));
  const placedRefs = placements.map(({ ref }) => ref).sort((a, b) => a.localeCompare(b, undefined, { numeric: true }));
  assert.deepEqual(placedRefs, expectedRefs);
  const zoneByRef = new Map(layoutRules.zones.flatMap((zone) => zone.refs.map((ref) => [ref, zone.id])));
  assert(placements.every((placement) => zoneByRef.get(placement.ref) === placement.zone));

  const contractNets = new Set(
    Object.values(config.pcb.criticalNets)
      .flatMap((map) => Object.values(map))
      .filter(Boolean)
  );
  assert.deepEqual([...routingClasses.allNets].sort(), [...contractNets].sort());
  const classNets = routingClasses.classes.flatMap((rule) => rule.nets);
  assert.equal(new Set(classNets).size, classNets.length);
  assert(!classNets.includes('GND'));
  assert.deepEqual([...classNets, ...routingClasses.planeNets].sort(), [...contractNets].sort());
  assert.equal(new Set(Object.values(routingClasses.netIds)).size, contractNets.size);
});

test('DRV8876 PH/EN mapping preserves direction and makes dual-open state Hi-Z', () => {
  assert.deepEqual(config.pcb.criticalNets.U1, {
    1: '+3V3', 2: 'DIR_RIGHT_Q', 3: 'MOTOR_EN', 4: '',
    5: '+3V3', 6: 'GND', 7: 'GND', 8: 'MOTOR_A',
    9: 'GND', 10: 'MOTOR_B', 11: '+12V_PROT', 12: 'VCP',
    13: 'CPH', 14: 'CPL', 15: 'GND', 16: 'GND', 17: 'GND'
  });

  const limitStep = (previousQ, leftOpen, rightOpen) => {
    const lSetN = leftOpen ? 0 : 1;
    const rClrN = rightOpen ? 0 : 1;
    let q = previousQ;
    if (!lSetN && rClrN) q = 1;
    if (lSetN && !rClrN) q = 0;
    return {
      q,
      motorEn: Number(Boolean(lSetN || rClrN)),
      nonstableAsync: !lSetN && !rClrN
    };
  };

  const bridge = ({ q, motorEn }) => {
    if (!motorEn) return { motorA: 'Z', motorB: 'Z', state: 'sleep' };
    return q
      ? { motorA: 1, motorB: 0, state: 'forward' }
      : { motorA: 0, motorB: 1, state: 'reverse' };
  };

  assert.deepEqual(bridge(limitStep(1, false, false)), { motorA: 1, motorB: 0, state: 'forward' });
  assert.deepEqual(bridge(limitStep(1, false, true)), { motorA: 0, motorB: 1, state: 'reverse' });
  assert.deepEqual(bridge(limitStep(0, true, false)), { motorA: 1, motorB: 0, state: 'forward' });
  assert.deepEqual(bridge(limitStep(1, true, true)), { motorA: 'Z', motorB: 'Z', state: 'sleep' });
});

test('spare 74HC14 gates square the slow power-up preset edge without changing polarity', () => {
  const u3 = config.pcb.criticalNets.U3;
  assert.equal(u3[5], 'DIR_SET_RC_N');
  assert.equal(u3[6], 'DIR_SET_RC_P');
  assert.equal(u3[9], 'DIR_SET_RC_P');
  assert.equal(u3[8], 'DIR_SET_N');
  assert.equal(config.pcb.criticalNets.U4[4], 'DIR_SET_N');
  assert.equal(config.pcb.criticalNets.D5[2], 'DIR_SET_RC_N');
});

test('driver support pins select PH/EN, automatic-retry OCP, and no current chopping', () => {
  const u1 = config.pcb.criticalNets.U1;
  assert.equal(u1[1], '+3V3', 'EN must remain high in active mode');
  assert.equal(u1[3], 'MOTOR_EN', 'dual-open interlock must drive nSLEEP');
  assert.equal(u1[5], '+3V3', 'VREF must be above ground');
  assert.equal(u1[6], 'GND', 'IPROPI grounded disables current regulation');
  assert.equal(u1[7], 'GND', 'IMODE low selects automatic-retry OCP only');
  assert.equal(u1[16], 'GND', 'PMODE low selects PH/EN mode');
});

test('schematic uses exact Lite parts and excludes obsolete R3 driver support', async () => {
  const source = await readFile(schematicPath, 'utf8');
  for (const code of ['C575551', 'C596320', 'C10768', 'C1729', 'C14289', 'C6820', 'C6762']) {
    assert(source.includes(code), `missing exact part ${code}`);
  }
  for (const obsolete of ['C2653552', 'C4747964', 'VNH5019', 'ENA_DIAGA', 'ENB_DIAGB', 'CS_DIS']) {
    assert(!source.includes(obsolete), `obsolete R3 driver detail remains: ${obsolete}`);
  }
  assert(source.includes("place('C596320', 'C1'"));
  assert(source.includes('do not order donor C596320'));
  assert(source.includes("await noConnect(U1, [4])"));
});

test('stock C1 uses the verified donor directly and never depends on a personal-device association', async () => {
  const source = await readFile(schematicPath, 'utf8');
  assert(!source.includes('prepareStockBulkDevice'));
  assert(!source.includes('SICK_STOCK_C_330UF_16V_1210'));
  assert(source.includes("const CBULK = await place('C596320', 'C1'"));
  assert(source.includes('STOCK_COMPONENT_OPTIONS'));
  assert(source.includes('addIntoBom: false, addIntoPcb: true'));
});

test('schematic placement and net stubs exactly implement the R4 contract', async () => {
  const source = await readFile(schematicPath, 'utf8');
  const variableToRef = new Map();
  const placedRefs = [];

  for (const match of source.matchAll(
    /const\s+(\w+)\s*=\s*await place\([^\r\n]*?,\s*'((?:SW|[A-Z])\d+)'\s*,/g
  )) {
    variableToRef.set(match[1], match[2]);
    placedRefs.push(match[2]);
  }

  const natural = (items) => [...items].sort((a, b) =>
    a.localeCompare(b, undefined, { numeric: true })
  );
  assert.deepEqual(natural(placedRefs), natural(config.pcb.expectedRefs));
  assert.equal(new Set(placedRefs).size, placedRefs.length, 'duplicate place() designator');

  const implementedNets = Object.fromEntries(
    config.pcb.expectedRefs.map((ref) => [ref, {}])
  );
  for (const match of source.matchAll(/await connect\((\w+),\s*\{([\s\S]*?)\}\);/g)) {
    const ref = variableToRef.get(match[1]);
    assert(ref, `connect() uses unknown component variable ${match[1]}`);
    Object.assign(implementedNets[ref], vm.runInNewContext(`({${match[2]}})`));
  }
  for (const match of source.matchAll(/await noConnect\((\w+),\s*\[([^\]]*)\]\);/g)) {
    const ref = variableToRef.get(match[1]);
    assert(ref, `noConnect() uses unknown component variable ${match[1]}`);
    const pins = vm.runInNewContext(`[${match[2]}]`);
    for (const pin of pins) implementedNets[ref][pin] = '';
  }

  assert.deepEqual(implementedNets, config.pcb.criticalNets);
  assert(source.includes('await preflightStandardDevices();'));
  assert(source.includes('assertPlacedReferences();'));
});

test('all five direct-wire interfaces stay copper-only and reject drilled pads', () => {
  const wireRefs = ['J1', 'J2', 'J3', 'J4', 'SW1'];
  const policies = config.pcb.footprintPolicies.filter((policy) => wireRefs.includes(policy.ref));
  assert.deepEqual(policies.map((policy) => policy.ref).sort(), [...wireRefs].sort());
  assert(policies.every((policy) => policy.forbidDrilledPads === true));

  const miniConfig = {
    pcb: {
      expectedRefs: ['J1'],
      expectedComponentCount: 1,
      padCounts: { J1: 2 },
      criticalNets: { J1: { 1: '+12V_IN', 2: 'GND' } },
      footprintPolicies: [{
        ref: 'J1', requiredPads: ['1', '2'], forbiddenPads: [], forbidDrilledPads: true
      }]
    }
  };
  const capture = {
    schema: 'firefly-pcb-capture/v1',
    componentCount: 1,
    components: [{
      ref: 'J1',
      pads: [
        { number: '1', net: '+12V_IN', hole: null },
        { number: '2', net: 'GND', hole: null }
      ]
    }]
  };
  assert.equal(validateCapture(capture, miniConfig).ok, true);
  const drilled = structuredClone(capture);
  drilled.components[0].pads[0].hole = ['ROUND', 40];
  assert.equal(validateCapture(drilled, miniConfig).ok, false);
});

test('generated scripts contain no unresolved token and preserve wire-pad preflight', async () => {
  for (const relativePath of [config.workflow.generatedScript, path.relative(root, schematicPath)]) {
    const source = await readFile(path.resolve(root, relativePath), 'utf8');
    assert(!source.includes('__SKYWORKS_PROJECT_CONFIG__'), relativePath);
  }

  const source = await readFile(schematicPath, 'utf8');
  for (const footprintName of ['2P_5X8_P10', '2P_3X5_P5']) {
    assert(source.includes(footprintName));
  }
  assert(source.includes('eda.lib_Device.getByLcscIds([key], undefined, false)'));
  assert(source.includes('const WIRE_COMPONENT_OPTIONS = Object.freeze({ addIntoBom: false, addIntoPcb: true });'));
  assert.equal((source.match(/place\(wireDevices\.large/g) || []).length, 3);
  assert.equal((source.match(/place\(wireDevices\.small/g) || []).length, 2);
});

test('wire verifier accepts runtime footprint UUIDs and reports all five verified', async () => {
  const source = await readFile(
    path.resolve(root, 'projects/sick_motor_reverser_r4_lite/generated/00b_apply_wirepad_flags.js'),
    'utf8'
  );
  const refs = ['J1', 'J2', 'J3', 'J4', 'SW1'];
  const largeRefs = new Set(['J1', 'J2', 'SW1']);
  const components = refs.map((ref) => ({
    getState_Designator: () => ref,
    getState_Footprint: () => largeRefs.has(ref) ? 'fp-large' : 'fp-small',
    getState_AddIntoBom: () => false,
    getState_AddIntoPcb: () => true,
    toAsync: async function toAsync() { return this; },
    setState_AddIntoBom: () => {},
    setState_AddIntoPcb: () => {},
    done: async () => {}
  }));
  const messages = [];
  const eda = {
    sch_PrimitiveComponent: { getAll: async () => components },
    lib_LibrariesList: { getPersonalLibraryUuid: async () => 'personal' },
    lib_Footprint: {
      search: async (name) => {
        if (name === '2P_5X8_P10') return [{ name, uuid: 'fp-large', libraryUuid: 'personal' }];
        if (name === '2P_3X5_P5') return [{ name, uuid: 'fp-small', libraryUuid: 'personal' }];
        return [];
      },
      get: async (uuid) => uuid === 'fp-large'
        ? { name: '2P_5X8_P10', uuid, libraryUuid: 'personal' }
        : { name: '2P_3X5_P5', uuid, libraryUuid: 'personal' }
    },
    sys_Dialog: { showInformationMessage: () => {} },
    sys_ToastMessage: { showMessage: async (message, type) => messages.push({ message, type }) }
  };
  const context = vm.createContext({ eda, console: { table: () => {} } });
  await vm.runInContext(source, context);
  assert.equal(messages.at(-1)?.message, 'Wire flags and all five custom footprints verified.');
});

test('placement verifier reports through current SYS_Dialog when SYS_MessageBox is absent', async () => {
  const source = await readFile(
    path.resolve(root, 'projects/sick_motor_reverser_r4_lite/generated/03_verify_placement.js'),
    'utf8'
  );
  const placements = createPlacements();
  const components = placements.map((entry) => ({
    getState_Designator: () => entry.ref,
    getState_X: () => entry.x,
    getState_Y: () => entry.y,
    getState_Rotation: () => entry.rotation,
    getState_Layer: () => 1
  }));
  const dialogs = [];
  const messages = [];
  const eda = {
    pcb_PrimitiveComponent: { getAll: async () => components },
    pcb_Drc: { check: async () => [] },
    sys_Dialog: {
      showInformationMessage: (message, title) => dialogs.push({ message, title })
    },
    sys_ToastMessage: {
      showMessage: async (message, type) => messages.push({ message, type })
    }
  };
  const context = vm.createContext({ eda, console: { log: () => {}, error: () => {}, warn: () => {} } });
  await vm.runInContext(source, context);
  assert.equal(dialogs.length, 1);
  assert.match(dialogs[0].message, /VERIFY PASS/);
  assert.equal(messages.at(-1)?.message, 'SICK MOTOR REVERSER R4 LITE R4-L2: PASS');
});

test('compact placement rolls every component back when a live move fails', async () => {
  const source = await readFile(
    path.resolve(root, 'projects/sick_motor_reverser_r4_lite/generated/02_apply_placement.js'),
    'utf8'
  );
  const targets = createPlacements();
  const originalByRef = new Map(targets.map((entry, index) => [entry.ref, {
    x: 3000 + index * 25,
    y: 2000 + index * 15,
    rotation: 0,
    layer: 1
  }]));
  const components = targets.map((entry) => {
    const state = { ref: entry.ref, ...originalByRef.get(entry.ref) };
    return {
      state,
      getState_Designator: () => state.ref,
      getState_X: () => state.x,
      getState_Y: () => state.y,
      getState_Rotation: () => state.rotation,
      getState_Layer: () => state.layer
    };
  });
  const dialogs = [];
  const messages = [];
  let forwardMoves = 0;
  let injected = false;
  const eda = {
    pcb_PrimitiveComponent: {
      getAll: async () => components,
      modify: async (component, next) => {
        if (!injected && forwardMoves === 5) {
          injected = true;
          throw new Error('injected move failure');
        }
        if (!injected) forwardMoves += 1;
        Object.assign(component.state, next);
        return component;
      }
    },
    pcb_PrimitiveLine: { getAll: async () => [] },
    pcb_PrimitiveVia: { getAll: async () => [] },
    pcb_PrimitivePour: { getAll: async () => [] },
    sys_Dialog: {
      showConfirmationMessage: (_message, _title, _ok, _cancel, done) => done(true),
      showInformationMessage: (message, title) => dialogs.push({ message, title })
    },
    sys_ToastMessage: {
      showMessage: async (message, type) => messages.push({ message, type })
    }
  };
  const context = vm.createContext({ eda, console: { log: () => {}, error: () => {}, warn: () => {} } });
  await vm.runInContext(source, context);

  assert(dialogs.some(({ message }) => message.includes('injected move failure')));
  assert(messages.at(-1)?.message.includes('FAILED | Placement aborted at [component-move]'));
  for (const component of components) {
    const original = originalByRef.get(component.state.ref);
    assert.equal(component.state.x, original.x, `${component.state.ref} x`);
    assert.equal(component.state.y, original.y, `${component.state.ref} y`);
    assert.equal(component.state.rotation, original.rotation, `${component.state.ref} rotation`);
    assert.equal(component.state.layer, original.layer, `${component.state.ref} layer`);
  }
});

test('compact placement carries a four-via U1 thermal array with U1', async () => {
  const source = await readFile(
    path.resolve(root, 'projects/sick_motor_reverser_r4_lite/generated/02_apply_placement.js'),
    'utf8'
  );
  const targets = createPlacements();
  const currentU1 = { x: 1500, y: 650 };
  const components = targets.map((entry, index) => {
    const state = {
      ref: entry.ref,
      x: entry.ref === 'U1' ? currentU1.x : 2600 + index * 30,
      y: entry.ref === 'U1' ? currentU1.y : 1800 + index * 20,
      rotation: 0,
      layer: 1
    };
    return {
      state,
      getState_Designator: () => state.ref,
      getState_X: () => state.x,
      getState_Y: () => state.y,
      getState_Rotation: () => state.rotation,
      getState_Layer: () => state.layer
    };
  });
  const viaOffsets = [[-30, -30], [30, -30], [-30, 30], [30, 30]];
  const vias = viaOffsets.map(([dx, dy], index) => {
    const state = { id: `thermal-${index}`, net: 'GND', x: currentU1.x + dx, y: currentU1.y + dy };
    return {
      state,
      getState_PrimitiveId: () => state.id,
      getState_Net: () => state.net,
      getState_X: () => state.x,
      getState_Y: () => state.y,
      getState_HoleDiameter: () => 12,
      getState_Diameter: () => 24,
      getState_PrimitiveLock: () => false
    };
  });
  const dialogs = [];
  const messages = [];
  const eda = {
    pcb_PrimitiveComponent: {
      getAll: async () => components,
      modify: async (component, next) => {
        Object.assign(component.state, next);
        return component;
      }
    },
    pcb_PrimitiveLine: { getAll: async () => [] },
    pcb_PrimitiveVia: {
      getAll: async () => vias,
      modify: async (via, next) => {
        Object.assign(via.state, next);
        return via;
      }
    },
    pcb_PrimitivePour: { getAll: async () => [] },
    sys_Dialog: {
      showConfirmationMessage: (_message, _title, _ok, _cancel, done) => done(true),
      showInformationMessage: (message, title) => dialogs.push({ message, title })
    },
    sys_ToastMessage: {
      showMessage: async (message, type) => messages.push({ message, type })
    }
  };
  const context = vm.createContext({ eda, console: { log: () => {}, error: () => {}, warn: () => {} } });
  await vm.runInContext(source, context);

  assert(dialogs.some(({ message }) => message.includes('APPLY PASS')));
  assert.equal(messages.at(-1)?.message, 'SICK MOTOR REVERSER R4 LITE R4-L2: PASS');
  const targetU1 = targets.find(({ ref }) => ref === 'U1');
  for (let index = 0; index < vias.length; index += 1) {
    assert.equal(vias[index].state.x, targetU1.x + viaOffsets[index][0]);
    assert.equal(vias[index].state.y, targetU1.y + viaOffsets[index][1]);
  }
});
