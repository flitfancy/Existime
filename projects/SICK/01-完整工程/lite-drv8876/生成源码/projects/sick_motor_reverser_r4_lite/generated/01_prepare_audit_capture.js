// SkyWorks reusable EasyEDA Pro preparation, audit, and geometry capture template.
// Generated artifacts replace the token below with one validated project config.
(async function main() {
  const CONFIG = {
  "id": "sick-motor-reverser-r4-lite",
  "name": "SICK MOTOR REVERSER R4 LITE",
  "revision": "R4-LITE",
  "pcb": {
    "expectedComponentCount": 31,
    "expectedRefs": [
      "F1",
      "SW1",
      "U1",
      "U2",
      "U3",
      "U4",
      "U5",
      "J1",
      "J2",
      "J3",
      "J4",
      "D1",
      "D5",
      "D6",
      "D7",
      "R1",
      "R5",
      "R6",
      "R7",
      "R8",
      "C1",
      "C2",
      "C3",
      "C4",
      "C5",
      "C6",
      "C7",
      "C8",
      "C9",
      "C10",
      "C11"
    ],
    "padCounts": {
      "F1": 2,
      "SW1": 2,
      "U1": 17,
      "U2": 3,
      "U3": 14,
      "U4": 14,
      "U5": 2,
      "J1": 2,
      "J2": 2,
      "J3": 2,
      "J4": 2,
      "D1": 2,
      "D5": 2,
      "D6": 2,
      "D7": 2,
      "R1": 2,
      "R5": 2,
      "R6": 2,
      "R7": 2,
      "R8": 2,
      "C1": 2,
      "C2": 2,
      "C3": 2,
      "C4": 2,
      "C5": 2,
      "C6": 2,
      "C7": 2,
      "C8": 2,
      "C9": 2,
      "C10": 2,
      "C11": 2
    },
    "criticalNets": {
      "J1": {
        "1": "+12V_IN",
        "2": "GND"
      },
      "SW1": {
        "1": "+12V_IN",
        "2": "+12V_SW"
      },
      "F1": {
        "1": "+12V_SW",
        "2": "+12V_PROT"
      },
      "J2": {
        "1": "MOTOR_A",
        "2": "MOTOR_B"
      },
      "J3": {
        "1": "L_RAW",
        "2": "GND"
      },
      "J4": {
        "1": "R_RAW",
        "2": "GND"
      },
      "U1": {
        "1": "+3V3",
        "2": "DIR_RIGHT_Q",
        "3": "MOTOR_EN",
        "4": "",
        "5": "+3V3",
        "6": "GND",
        "7": "GND",
        "8": "MOTOR_A",
        "9": "GND",
        "10": "MOTOR_B",
        "11": "+12V_PROT",
        "12": "VCP",
        "13": "CPH",
        "14": "CPL",
        "15": "GND",
        "16": "GND",
        "17": "GND"
      },
      "U2": {
        "1": "GND",
        "2": "+12V_PROT",
        "3": "+3V3"
      },
      "U3": {
        "1": "L_RAW",
        "2": "L_SET_N",
        "3": "R_RAW",
        "4": "R_CLR_N",
        "5": "DIR_SET_RC_N",
        "6": "DIR_SET_RC_P",
        "7": "GND",
        "8": "DIR_SET_N",
        "9": "DIR_SET_RC_P",
        "10": "",
        "11": "GND",
        "12": "",
        "13": "GND",
        "14": "+3V3"
      },
      "U4": {
        "1": "R_CLR_N",
        "2": "GND",
        "3": "GND",
        "4": "DIR_SET_N",
        "5": "DIR_RIGHT_Q",
        "6": "DIR_LEFT_NQ",
        "7": "GND",
        "8": "",
        "9": "",
        "10": "+3V3",
        "11": "GND",
        "12": "GND",
        "13": "+3V3",
        "14": "+3V3"
      },
      "U5": {
        "1": "+12V_PROT",
        "2": "GND"
      },
      "D1": {
        "1": "GND",
        "2": "LED_PWR_A"
      },
      "D5": {
        "1": "L_SET_N",
        "2": "DIR_SET_RC_N"
      },
      "D6": {
        "1": "MOTOR_EN",
        "2": "L_SET_N"
      },
      "D7": {
        "1": "MOTOR_EN",
        "2": "R_CLR_N"
      },
      "R1": {
        "1": "+12V_PROT",
        "2": "LED_PWR_A"
      },
      "R5": {
        "1": "+3V3",
        "2": "L_RAW"
      },
      "R6": {
        "1": "+3V3",
        "2": "R_RAW"
      },
      "R7": {
        "1": "+3V3",
        "2": "DIR_SET_RC_N"
      },
      "R8": {
        "1": "MOTOR_EN",
        "2": "GND"
      },
      "C1": {
        "1": "+12V_PROT",
        "2": "GND"
      },
      "C2": {
        "1": "+12V_PROT",
        "2": "GND"
      },
      "C3": {
        "1": "+3V3",
        "2": "GND"
      },
      "C4": {
        "1": "+12V_PROT",
        "2": "GND"
      },
      "C5": {
        "1": "L_RAW",
        "2": "GND"
      },
      "C6": {
        "1": "R_RAW",
        "2": "GND"
      },
      "C7": {
        "1": "+3V3",
        "2": "GND"
      },
      "C8": {
        "1": "DIR_SET_RC_N",
        "2": "GND"
      },
      "C9": {
        "1": "+3V3",
        "2": "GND"
      },
      "C10": {
        "1": "VCP",
        "2": "+12V_PROT"
      },
      "C11": {
        "1": "CPH",
        "2": "CPL"
      }
    },
    "footprintPolicies": [
      {
        "ref": "J1",
        "requiredPads": [
          "1",
          "2"
        ],
        "forbiddenPads": [],
        "forbidDrilledPads": true,
        "note": "Direct-wire power input: 2P_5X8_P10, no plated holes or paste."
      },
      {
        "ref": "SW1",
        "requiredPads": [
          "1",
          "2"
        ],
        "forbiddenPads": [],
        "forbidDrilledPads": true,
        "note": "Off-board power switch: 2P_5X8_P10, no plated holes or paste."
      },
      {
        "ref": "J2",
        "requiredPads": [
          "1",
          "2"
        ],
        "forbiddenPads": [],
        "forbidDrilledPads": true,
        "note": "Direct-wire motor output: 2P_5X8_P10, no plated holes or paste."
      },
      {
        "ref": "J3",
        "requiredPads": [
          "1",
          "2"
        ],
        "forbiddenPads": [],
        "forbidDrilledPads": true,
        "note": "Left NC limit input: 2P_3X5_P5, no plated holes or paste."
      },
      {
        "ref": "J4",
        "requiredPads": [
          "1",
          "2"
        ],
        "forbiddenPads": [],
        "forbidDrilledPads": true,
        "note": "Right NC limit input: 2P_3X5_P5, no plated holes or paste."
      },
      {
        "ref": "U1",
        "requiredPads": [
          "1",
          "2",
          "3",
          "4",
          "5",
          "6",
          "7",
          "8",
          "9",
          "10",
          "11",
          "12",
          "13",
          "14",
          "15",
          "16",
          "17"
        ],
        "forbiddenPads": [],
        "note": "DRV8876PWPR HTSSOP-16 PowerPAD: pad 17 is exposed thermal GND; verify live library mapping."
      },
      {
        "ref": "U2",
        "requiredPads": [
          "1",
          "2",
          "3"
        ],
        "forbiddenPads": [],
        "note": "HT7533-1 SOT-89: 1=GND, 2=VIN, 3=VOUT."
      },
      {
        "ref": "U5",
        "requiredPads": [
          "1",
          "2"
        ],
        "forbiddenPads": [],
        "note": "SMAJ18CA bidirectional TVS across +12V_PROT and GND; polarity independent."
      },
      {
        "ref": "C1",
        "requiredPads": [
          "1",
          "2"
        ],
        "forbiddenPads": [],
        "note": "User-stock 330uF 16V capacitor, standard 1210/3225 two-pad footprint; 16V rail margin accepted for this prototype."
      }
    ]
  },
  "workflow": {
    "captureDownloadName": "sick_motor_reverser_r4_lite_capture.json"
  }
};
  const TITLE = `${CONFIG.name} ${CONFIG.revision} preparation`;
  const startedAt = Date.now();
  const PAD_READ_CONCURRENCY = 12;

  const naturalCompare = (left, right) => String(left).localeCompare(
    String(right), undefined, { numeric: true, sensitivity: 'base' }
  );
  const get = (object, method) => {
    try {
      return typeof object?.[method] === 'function' ? object[method]() : undefined;
    } catch (error) {
      return undefined;
    }
  };
  const text = (value) => value === undefined || value === null ? '' : String(value).trim();
  const finite = (value) => Number.isFinite(Number(value)) ? Number(value) : null;
  const round = (value, digits = 6) => {
    if (!Number.isFinite(value)) return null;
    const scale = 10 ** digits;
    return Math.round(value * scale) / scale;
  };
  const rotation = (value) => ((Number(value || 0) % 360) + 360) % 360;
  const delay = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
  const refOf = (component) => text(get(component, 'getState_Designator') || get(component, 'getState_Name'));
  const idOf = (primitive) => text(get(primitive, 'getState_PrimitiveId'));
  const padNumberOf = (pad) => text(get(pad, 'getState_PadNumber') || get(pad, 'getState_PinNumber'));
  const netOf = (pad) => text(get(pad, 'getState_Net'));
  const padHasDrill = (pad) => {
    const hole = pad?.hole;
    if (hole === null || hole === undefined || hole === '' || hole === false) return false;
    if (Array.isArray(hole)) return hole.length > 0;
    if (typeof hole === 'object') return Object.keys(hole).length > 0;
    return true;
  };

  const publishToast = (message, messageType = 'info', timer = 8) => {
    try {
      if (typeof eda?.sys_Message?.showToastMessage === 'function') {
        eda.sys_Message.showToastMessage(message, messageType, timer);
        return;
      }
      if (typeof eda?.sys_ToastMessage?.showMessage === 'function') {
        eda.sys_ToastMessage.showMessage(message, messageType, timer);
      }
    } catch (error) {
      console.error('Toast publication failed:', error);
    }
  };

  const showFinalDialog = async (message, isFailure) => {
    try {
      if (isFailure) await eda.sys_MessageBox.showErrorMessage(message, TITLE);
      else await eda.sys_MessageBox.showInformationMessage(message, TITLE);
    } catch (error) {
      console.error('Final result dialog failed:', error);
    }
  };

  const confirm = (message, mainButton = 'Continue') => new Promise((resolve) => {
    if (typeof eda?.sys_Dialog?.showConfirmationMessage !== 'function') {
      resolve(false);
      return;
    }
    eda.sys_Dialog.showConfirmationMessage(
      message, TITLE, mainButton, 'Cancel', (accepted) => resolve(Boolean(accepted))
    );
  });

  const mapConcurrent = async (items, limit, worker) => {
    let nextIndex = 0;
    const runners = Array.from({ length: Math.min(limit, items.length) }, async () => {
      while (nextIndex < items.length) {
        const index = nextIndex;
        nextIndex += 1;
        await worker(items[index], index);
      }
    });
    await Promise.all(runners);
  };

  const safeSerializable = (value, depth = 0, seen = new WeakSet()) => {
    if (value === null || value === undefined) return value ?? null;
    if (['string', 'number', 'boolean'].includes(typeof value)) return value;
    if (typeof value === 'bigint') return String(value);
    if (typeof value === 'function') return undefined;
    if (depth > 10) return '[max-depth]';
    if (typeof value !== 'object') return String(value);
    if (seen.has(value)) return '[circular]';
    seen.add(value);
    if (Array.isArray(value)) return value.map((item) => safeSerializable(item, depth + 1, seen));
    const result = {};
    for (const key of Object.keys(value).sort()) {
      try {
        const normalized = safeSerializable(value[key], depth + 1, seen);
        if (normalized !== undefined) result[key] = normalized;
      } catch (error) {
        result[key] = '[unreadable]';
      }
    }
    return result;
  };

  const inverseRotate = (x, y, originX, originY, degrees) => {
    const radians = rotation(degrees) * Math.PI / 180;
    const cosine = Math.cos(radians);
    const sine = Math.sin(radians);
    const dx = x - originX;
    const dy = y - originY;
    return { x: round(dx * cosine + dy * sine), y: round(-dx * sine + dy * cosine) };
  };

  const standardPadSize = (shape) => {
    if (!Array.isArray(shape) || !shape.length) return null;
    const type = String(shape[0] || '').toUpperCase();
    if (['ELLIPSE', 'OVAL', 'RECT'].includes(type)) {
      const width = Number(shape[1]);
      const height = Number(shape[2]);
      return Number.isFinite(width) && Number.isFinite(height) ? { type, width, height } : null;
    }
    if (type === 'NGON') {
      const diameter = Number(shape[1]);
      return Number.isFinite(diameter) ? { type, width: diameter, height: diameter } : null;
    }
    return null;
  };

  const rotatedHalfSize = (width, height, degrees) => {
    const radians = rotation(degrees) * Math.PI / 180;
    const cosine = Math.abs(Math.cos(radians));
    const sine = Math.abs(Math.sin(radians));
    return {
      halfWidth: round((width * cosine + height * sine) / 2),
      halfHeight: round((width * sine + height * cosine) / 2)
    };
  };

  const readPads = async (component) => {
    const primitiveId = idOf(component);
    if (!primitiveId) throw new Error(`${refOf(component) || '[blank]'} has no primitive ID`);
    return (await eda.pcb_PrimitiveComponent.getAllPinsByPrimitiveId(primitiveId)) || [];
  };

  const componentRecord = async (component, failures) => {
    const ref = refOf(component);
    const primitiveId = idOf(component);
    const x = finite(get(component, 'getState_X'));
    const y = finite(get(component, 'getState_Y'));
    const componentRotation = rotation(get(component, 'getState_Rotation'));
    let pads = [];
    try {
      pads = await readPads(component);
    } catch (error) {
      failures.push(`${ref || primitiveId || '[blank]'} pad read failed: ${error?.message || String(error)}`);
    }

    const padRecords = [];
    const bounds = {
      minX: Number.POSITIVE_INFINITY, minY: Number.POSITIVE_INFINITY,
      maxX: Number.NEGATIVE_INFINITY, maxY: Number.NEGATIVE_INFINITY
    };
    let boundsComplete = true;
    for (const pad of pads) {
      const padX = finite(get(pad, 'getState_X'));
      const padY = finite(get(pad, 'getState_Y'));
      const padRotation = rotation(get(pad, 'getState_Rotation'));
      const local = x !== null && y !== null && padX !== null && padY !== null
        ? inverseRotate(padX, padY, x, y, componentRotation)
        : { x: null, y: null };
      const localRotation = rotation(padRotation - componentRotation);
      const shape = safeSerializable(get(pad, 'getState_Pad'));
      const size = standardPadSize(shape);
      let localCopperAabb = null;
      if (size && local.x !== null && local.y !== null) {
        const half = rotatedHalfSize(size.width, size.height, localRotation);
        localCopperAabb = {
          minX: round(local.x - half.halfWidth), minY: round(local.y - half.halfHeight),
          maxX: round(local.x + half.halfWidth), maxY: round(local.y + half.halfHeight)
        };
        bounds.minX = Math.min(bounds.minX, localCopperAabb.minX);
        bounds.minY = Math.min(bounds.minY, localCopperAabb.minY);
        bounds.maxX = Math.max(bounds.maxX, localCopperAabb.maxX);
        bounds.maxY = Math.max(bounds.maxY, localCopperAabb.maxY);
      } else {
        boundsComplete = false;
      }
      padRecords.push({
        number: padNumberOf(pad),
        primitiveId: idOf(pad),
        parentPrimitiveId: text(get(pad, 'getState_ParentComponentPrimitiveId')),
        layer: safeSerializable(get(pad, 'getState_Layer')),
        net: netOf(pad),
        padType: safeSerializable(get(pad, 'getState_PadType')),
        global: { x: padX, y: padY, rotation: padRotation },
        local: { x: local.x, y: local.y, rotation: localRotation },
        shape,
        specialPad: safeSerializable(get(pad, 'getState_SpecialPad')),
        hole: safeSerializable(get(pad, 'getState_Hole')),
        solderMaskAndPasteMaskExpansion: safeSerializable(
          get(pad, 'getState_SolderMaskAndPasteMaskExpansion')
        ),
        localCopperAabb
      });
    }

    const localCopperBounds = Number.isFinite(bounds.minX) ? {
      minX: round(bounds.minX), minY: round(bounds.minY),
      maxX: round(bounds.maxX), maxY: round(bounds.maxY),
      width: round(bounds.maxX - bounds.minX), height: round(bounds.maxY - bounds.minY),
      complete: boundsComplete
    } : null;
    return {
      ref, primitiveId,
      uniqueId: text(get(component, 'getState_UniqueId')),
      name: text(get(component, 'getState_Name')),
      layer: safeSerializable(get(component, 'getState_Layer')),
      x, y, rotation: componentRotation,
      footprint: safeSerializable(get(component, 'getState_Footprint')),
      manufacturer: text(get(component, 'getState_Manufacturer')),
      manufacturerId: text(get(component, 'getState_ManufacturerId')),
      supplier: text(get(component, 'getState_Supplier')),
      supplierId: text(get(component, 'getState_SupplierId')),
      pads: padRecords,
      localCopperBounds
    };
  };

  const applyConfiguredRepairs = async (componentsByRef, repairLog, failures) => {
    for (const policy of CONFIG.pcb.footprintPolicies || []) {
      const repair = policy.repair;
      if (repair?.type !== 'delete-unconnected-pad') continue;
      const matches = componentsByRef.get(policy.ref) || [];
      if (matches.length !== 1) continue;
      let pads;
      try {
        pads = await readPads(matches[0]);
      } catch (error) {
        failures.push(`${policy.ref} repair preflight: ${error?.message || String(error)}`);
        continue;
      }
      const target = pads.find((pad) => padNumberOf(pad) === String(repair.pad));
      if (!target) continue;
      if (netOf(target)) {
        failures.push(`${policy.ref}.${repair.pad} repair refused: pad is connected to ${netOf(target)}`);
        continue;
      }
      if (!eda?.pcb_PrimitivePad?.delete) {
        failures.push(`${policy.ref}.${repair.pad} repair unavailable: pad delete API missing`);
        continue;
      }
      const accepted = !repair.confirmationRequired || await confirm(
        [
          `${policy.ref} contains explicitly forbidden, unconnected pad ${repair.pad}.`,
          '',
          `Delete only ${policy.ref}.${repair.pad}, then audit all components and download a capture?`,
          'The operation is post-checked and can be undone in the editor.'
        ].join('\n'),
        `Delete ${policy.ref}.${repair.pad}`
      );
      if (!accepted) {
        failures.push(`${policy.ref}.${repair.pad} repair cancelled`);
        continue;
      }
      const targetId = idOf(target);
      const deleted = targetId ? await eda.pcb_PrimitivePad.delete(targetId) : false;
      if (!deleted) {
        failures.push(`${policy.ref}.${repair.pad} delete returned false`);
        continue;
      }
      let stillPresent = true;
      for (let attempt = 0; attempt < 5; attempt += 1) {
        const after = await readPads(matches[0]);
        stillPresent = after.some((pad) => padNumberOf(pad) === String(repair.pad));
        if (!stillPresent) break;
        await delay(50);
      }
      if (stillPresent) failures.push(`${policy.ref}.${repair.pad} still present after delete`);
      else repairLog.push(`deleted unconnected ${policy.ref}.${repair.pad}`);
    }
  };

  const auditRecords = (records, failures) => {
    const expectedRefs = CONFIG.pcb.expectedRefs;
    const expectedSet = new Set(expectedRefs);
    const byRef = new Map();
    for (const record of records) {
      if (!byRef.has(record.ref)) byRef.set(record.ref, []);
      byRef.get(record.ref).push(record);
    }
    const missing = expectedRefs.filter((ref) => !byRef.has(ref));
    const unexpected = [...byRef.keys()].filter((ref) => ref && !expectedSet.has(ref));
    const duplicates = [...byRef].filter(([, values]) => values.length !== 1);
    if (records.length !== CONFIG.pcb.expectedComponentCount) {
      failures.push(`component count: expected ${CONFIG.pcb.expectedComponentCount}, got ${records.length}`);
    }
    if (missing.length) failures.push(`missing refs: ${missing.join(', ')}`);
    if (unexpected.length) failures.push(`unexpected refs: ${unexpected.join(', ')}`);
    if (duplicates.length) failures.push(`duplicate refs: ${duplicates.map(([ref, values]) => `${ref} x${values.length}`).join(', ')}`);

    for (const ref of expectedRefs) {
      const record = byRef.get(ref)?.[0];
      if (!record || byRef.get(ref).length !== 1) continue;
      const expectedCount = CONFIG.pcb.padCounts[ref];
      if (record.pads.length !== expectedCount) {
        failures.push(`${ref} pad count: expected ${expectedCount}, got ${record.pads.length}`);
      }
    }
    for (const [ref, expectedPins] of Object.entries(CONFIG.pcb.criticalNets || {})) {
      const record = byRef.get(ref)?.[0];
      if (!record) continue;
      const pads = new Map(record.pads.map((pad) => [String(pad.number), pad]));
      for (const [number, expectedNet] of Object.entries(expectedPins)) {
        const pad = pads.get(String(number));
        if (!pad) failures.push(`${ref}.${number}: pad missing`);
        else if (String(pad.net || '') !== String(expectedNet)) {
          failures.push(`${ref}.${number}: expected ${expectedNet || '[NC]'}, got ${pad.net || '[NC]'}`);
        }
      }
    }
    for (const policy of CONFIG.pcb.footprintPolicies || []) {
      const record = byRef.get(policy.ref)?.[0];
      if (!record) continue;
      const numbers = record.pads.map((pad) => String(pad.number));
      for (const required of (policy.requiredPads || []).map(String)) {
        if (!numbers.includes(required)) failures.push(`${policy.ref}: required pad ${required} missing`);
      }
      for (const forbidden of (policy.forbiddenPads || []).map(String)) {
        if (numbers.includes(forbidden)) failures.push(`${policy.ref}: forbidden pad ${forbidden} present`);
      }
      if (policy.forbidDrilledPads) {
        const drilledPads = record.pads.filter(padHasDrill).map((pad) => String(pad.number));
        if (drilledPads.length) {
          failures.push(`${policy.ref}: drilled pads forbidden, found ${drilledPads.join(', ')}`);
        }
      }
    }
  };

  try {
    if (!eda?.pcb_PrimitiveComponent?.getAll || !eda?.pcb_PrimitiveComponent?.getAllPinsByPrimitiveId) {
      throw new Error('PCB component/pad APIs unavailable. Open the target PCB document and run again.');
    }
    publishToast(`${CONFIG.name} ${CONFIG.revision}: preparation started...`, 'info', 5);
    const components = (await eda.pcb_PrimitiveComponent.getAll()) || [];
    const componentsByRef = new Map();
    for (const component of components) {
      const ref = refOf(component);
      if (!componentsByRef.has(ref)) componentsByRef.set(ref, []);
      componentsByRef.get(ref).push(component);
    }

    const failures = [];
    const repairLog = [];
    await applyConfiguredRepairs(componentsByRef, repairLog, failures);

    const records = new Array(components.length);
    await mapConcurrent(components, PAD_READ_CONCURRENCY, async (component, index) => {
      records[index] = await componentRecord(component, failures);
    });
    records.sort((left, right) => naturalCompare(left.ref, right.ref));
    auditRecords(records, failures);

    let nativeDrc = { status: 'unavailable', items: [] };
    if (eda?.pcb_Drc?.check) {
      try {
        const details = await eda.pcb_Drc.check(true, true, true);
        nativeDrc = { status: 'completed', items: safeSerializable(Array.isArray(details) ? details : []) };
      } catch (error) {
        nativeDrc = { status: 'failed', error: error?.message || String(error), items: [] };
      }
    }

    let documentSource = null;
    let footprintSources = [];
    try {
      if (eda?.sys_FileManager?.getDocumentSource) documentSource = await eda.sys_FileManager.getDocumentSource();
    } catch (error) {
      failures.push(`document source export failed: ${error?.message || String(error)}`);
    }
    try {
      if (eda?.sys_FileManager?.getDocumentFootprintSources) {
        footprintSources = await eda.sys_FileManager.getDocumentFootprintSources() || [];
      }
    } catch (error) {
      failures.push(`footprint source export failed: ${error?.message || String(error)}`);
    }

    const capture = {
      schema: 'firefly-pcb-capture/v1',
      projectId: CONFIG.id,
      projectRevision: CONFIG.revision,
      generatedAt: new Date().toISOString(),
      editor: 'EasyEDA Pro',
      coordinateUnit: 'current PCB canvas unit; expected mil for this project',
      componentCount: records.length,
      componentPadCount: records.reduce((sum, record) => sum + record.pads.length, 0),
      repairs: repairLog,
      failures,
      nativeDrc,
      components: records,
      footprintSources: safeSerializable(footprintSources),
      documentSource: typeof documentSource === 'string' ? documentSource : null
    };
    const json = JSON.stringify(capture, null, 2);
    if (!eda?.sys_FileSystem?.saveFile || typeof Blob === 'undefined') {
      console.log('SKYWORKS_CAPTURE_JSON_BEGIN');
      console.log(json);
      console.log('SKYWORKS_CAPTURE_JSON_END');
      throw new Error('Capture created, but file download API unavailable; copy JSON from console markers.');
    }
    await eda.sys_FileSystem.saveFile(
      new Blob([json], { type: 'application/json;charset=utf-8' }),
      CONFIG.workflow.captureDownloadName
    );

    console.log(`===== ${CONFIG.name} ${CONFIG.revision} PREPARE/AUDIT/CAPTURE =====`);
    console.log(`Components: ${records.length}/${CONFIG.pcb.expectedComponentCount}`);
    console.log(`Pads: ${capture.componentPadCount}`);
    console.log(`Repairs: ${repairLog.join(', ') || 'none'}`);
    console.log(`Failures: ${failures.length}`);
    for (const failure of failures) console.error(` - ${failure}`);

    const elapsed = Date.now() - startedAt;
    const summary = [
      failures.length ? 'PREPARE / AUDIT / CAPTURE: FAIL' : 'PREPARE / AUDIT / CAPTURE: PASS',
      '',
      `Downloaded: ${CONFIG.workflow.captureDownloadName}`,
      `Components: ${records.length}/${CONFIG.pcb.expectedComponentCount}`,
      `Pads: ${capture.componentPadCount}`,
      `Repairs: ${repairLog.join(', ') || 'none'}`,
      `Failures: ${failures.length}`,
      `Native DRC: ${nativeDrc.status}`,
      `Elapsed: ${elapsed} ms`,
      ...(failures.length ? ['', ...failures.slice(0, 10)] : ['', 'Save the current PCB, then run the local status command.'])
    ].join('\n');
    publishToast(
      failures.length
        ? `${CONFIG.name} ${CONFIG.revision} FAIL: ${failures.length} issue(s); capture downloaded.`
        : `${CONFIG.name} ${CONFIG.revision} PASS: capture downloaded. Save the current PCB.`,
      failures.length ? 'error' : 'success', 0
    );
    await showFinalDialog(summary, failures.length > 0);
  } catch (error) {
    const detail = error?.stack || error?.message || String(error);
    console.error(detail);
    publishToast(`${CONFIG.name} ${CONFIG.revision} ABORTED: inspect the result dialog.`, 'error', 0);
    await showFinalDialog(
      `PREPARE / AUDIT / CAPTURE: ABORTED\n\n${detail}\n\nNo unconfirmed repair was assumed successful.`,
      true
    );
  }
})();
