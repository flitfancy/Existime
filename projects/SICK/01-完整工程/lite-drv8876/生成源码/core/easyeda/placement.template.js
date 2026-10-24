// SkyWorks EasyEDA Pro independent placement script.
// Generated from a GREEN project capture and offline geometry validation.
(async function skyworksPlacementMain() {
  const JOB = __SKYWORKS_PLACEMENT_JOB__;
  const TOP = 1;
  const BOTTOM = 2;
  const TOLERANCE_MIL = 1;
  const U1_THERMAL_VIA_COUNT = 4;
  const U1_THERMAL_VIA_MAX_DISTANCE_MIL = 140;
  const TAG = `${JOB.project.name} ${JOB.revision}`;

  const toast = async (message, type = 3, duration = 3000) => {
    try {
      if (eda?.sys_ToastMessage?.showMessage) {
        await eda.sys_ToastMessage.showMessage(message, type, duration);
      } else if (eda?.sys_ToastMessage?.show) {
        await eda.sys_ToastMessage.show(message, type, duration);
      }
    } catch (error) {
      console.warn('Toast unavailable:', error);
    }
  };
  const finalToast = async (message, type) => toast(message, type, 600000);
  const get = (object, method) => {
    try {
      return typeof object?.[method] === 'function' ? object[method]() : undefined;
    } catch (error) {
      return undefined;
    }
  };
  const refOf = (component) => String(
    get(component, 'getState_Designator') || get(component, 'getState_Name') || ''
  ).trim();
  const rotationOf = (component) => ((Number(get(component, 'getState_Rotation')) % 360) + 360) % 360;
  const normalizeRotation = (value) => ((Number(value) % 360) + 360) % 360;
  const getAllSafe = async (api, ...args) => {
    try {
      return typeof api?.getAll === 'function' ? (await api.getAll(...args)) || [] : [];
    } catch (error) {
      return [];
    }
  };
  const showError = async (lines) => {
    const message = Array.isArray(lines) ? lines.join('\n') : String(lines);
    console.error(`${TAG}: ${message}`);
    const compact = message.replace(/\s+/g, ' ').slice(0, 260);
    await finalToast(`${TAG}: FAILED | ${compact}`, 2);
    try {
      if (typeof eda?.sys_Dialog?.showInformationMessage === 'function') {
        eda.sys_Dialog.showInformationMessage(message, 'SkyWorks placement error', 'OK');
      }
    } catch (error) {
      console.warn('Placement error dialog unavailable:', error);
    }
  };
  const showInformation = async (message) => {
    console.log(message);
    await finalToast(`${JOB.project.name} ${JOB.revision}: PASS`, 1);
    try {
      if (typeof eda?.sys_Dialog?.showInformationMessage === 'function') {
        eda.sys_Dialog.showInformationMessage(message, 'SkyWorks placement', 'OK');
      }
    } catch (error) {
      console.warn('Placement information dialog unavailable:', error);
    }
  };
  const confirmApply = async (message) => {
    if (typeof eda?.sys_Dialog?.showConfirmationMessage !== 'function') {
      await showError('Reliable confirmation API unavailable. Nothing was changed.');
      return false;
    }
    return new Promise((resolve) => {
      try {
        eda.sys_Dialog.showConfirmationMessage(
          message,
          'SkyWorks placement',
          `Apply ${JOB.revision}`,
          'Cancel',
          (mainButtonClicked) => resolve(Boolean(mainButtonClicked))
        );
      } catch (error) {
        console.error(`${TAG}: confirmation failed`, error);
        resolve(false);
      }
    });
  };
  const indexComponents = (components) => {
    const byRef = new Map();
    for (const component of components) {
      const ref = refOf(component);
      if (!byRef.has(ref)) byRef.set(ref, []);
      byRef.get(ref).push(component);
    }
    return byRef;
  };
  const checkCoordinates = async () => {
    const refreshed = (await eda.pcb_PrimitiveComponent.getAll()) || [];
    const byRef = indexComponents(refreshed);
    const failures = [];
    for (const entry of JOB.placements) {
      const values = byRef.get(entry.ref) || [];
      if (values.length !== 1) {
        failures.push(`${entry.ref}: found ${values.length}`);
        continue;
      }
      const component = values[0];
      const actualX = Number(get(component, 'getState_X'));
      const actualY = Number(get(component, 'getState_Y'));
      const actualRotation = rotationOf(component);
      const layer = Number(get(component, 'getState_Layer'));
      if (!Number.isFinite(actualX) || !Number.isFinite(actualY)) {
        failures.push(`${entry.ref}: unreadable coordinate`);
      } else if (Math.abs(actualX - entry.x) > TOLERANCE_MIL || Math.abs(actualY - entry.y) > TOLERANCE_MIL) {
        failures.push(`${entry.ref}: expected (${entry.x},${entry.y}), got (${actualX},${actualY})`);
      }
      if (actualRotation !== normalizeRotation(entry.rotation)) {
        failures.push(`${entry.ref}: expected rotation ${entry.rotation}, got ${actualRotation}`);
      }
      if (layer !== TOP) failures.push(`${entry.ref}: expected top layer, got ${layer}`);
    }
    return failures;
  };
  const viaStateOf = (via) => ({
    via,
    primitiveId: String(get(via, 'getState_PrimitiveId') || '').trim(),
    net: String(get(via, 'getState_Net') || '').trim(),
    x: Number(get(via, 'getState_X')),
    y: Number(get(via, 'getState_Y')),
    holeDiameter: Number(get(via, 'getState_HoleDiameter')),
    diameter: Number(get(via, 'getState_Diameter')),
    primitiveLock: Boolean(get(via, 'getState_PrimitiveLock'))
  });
  const formatVia = (via, u1) => {
    const distance = Number.isFinite(via.x) && Number.isFinite(via.y)
      ? Math.hypot(via.x - u1.x, via.y - u1.y).toFixed(1)
      : 'unreadable';
    return `${via.net || '[NoNet]'}@(${via.x},${via.y}) dU1=${distance} hole=${via.holeDiameter} dia=${via.diameter}`;
  };

  await toast(`${TAG} ${JOB.mode} started`, 3, 600000);
  let originals = [];
  let thermalViaOriginals = [];
  let changesStarted = false;
  let committed = false;
  let activeStage = 'preflight';
  const rollback = async () => {
    const failures = [];
    if (!changesStarted) return failures;
    if (typeof eda?.pcb_PrimitiveVia?.modify === 'function') {
      for (const original of [...thermalViaOriginals].reverse()) {
        try {
          await eda.pcb_PrimitiveVia.modify(original.via, {
            x: original.x,
            y: original.y,
            primitiveLock: original.primitiveLock
          });
        } catch (error) {
          failures.push(`via ${original.primitiveId || '[unknown]'}: ${error?.message || error}`);
        }
      }
    }
    if (typeof eda?.pcb_PrimitiveComponent?.modify === 'function') {
      for (const original of [...originals].reverse()) {
        try {
          await eda.pcb_PrimitiveComponent.modify(original.component, {
            layer: original.layer,
            x: original.x,
            y: original.y,
            rotation: original.rotation,
            primitiveLock: false
          });
        } catch (error) {
          failures.push(`${original.ref}: ${error?.message || error}`);
        }
      }
    }
    return failures;
  };

  try {
    if (!eda?.pcb_PrimitiveComponent?.getAll) {
      await showError('PCB component API unavailable. Open PCB1 and run again.');
      return;
    }
    const components = (await eda.pcb_PrimitiveComponent.getAll()) || [];
    const byRef = indexComponents(components);
    const expected = new Set(JOB.placements.map((entry) => entry.ref));
    const missing = [...expected].filter((ref) => !byRef.has(ref));
    const duplicates = [...byRef].filter(([ref, values]) => expected.has(ref) && values.length !== 1)
      .map(([ref, values]) => `${ref} x${values.length}`);
    const unexpected = [...byRef.keys()].filter((ref) => ref && !expected.has(ref));
    if (components.length !== JOB.placements.length || missing.length || duplicates.length || unexpected.length) {
      await showError([
        'Placement preflight failed. Nothing was changed.',
        `components: ${components.length}/${JOB.placements.length}`,
        `missing: ${missing.join(', ') || 'none'}`,
        `duplicates: ${duplicates.join(', ') || 'none'}`,
        `unexpected: ${unexpected.join(', ') || 'none'}`
      ]);
      return;
    }

    if (JOB.mode === 'verify') {
      const failures = await checkCoordinates();
      if (failures.length) {
        await showError(['Placement coordinate verification failed.', ...failures.slice(0, 20)]);
        return;
      }
      await showInformation([
        `${TAG}: VERIFY PASS`,
        `components: ${JOB.placements.length}`,
        'This check did not move components or run global DRC.'
      ].join('\n'));
      return;
    }
    if (JOB.mode !== 'apply') {
      await showError(`Unsupported mode: ${JOB.mode}`);
      return;
    }
    if (!eda?.pcb_PrimitiveComponent?.modify) {
      await showError('PCB component modify API unavailable. Nothing was changed.');
      return;
    }
    const topCopper = await getAllSafe(eda?.pcb_PrimitiveLine, undefined, TOP);
    const bottomCopper = await getAllSafe(eda?.pcb_PrimitiveLine, undefined, BOTTOM);
    const vias = await getAllSafe(eda?.pcb_PrimitiveVia);
    const pours = await getAllSafe(eda?.pcb_PrimitivePour);
    if (topCopper.length || bottomCopper.length || pours.length) {
      await showError([
        'Placement refused because routed copper already exists. Nothing was changed.',
        `top lines: ${topCopper.length}; bottom lines: ${bottomCopper.length}; vias: ${vias.length}; pours: ${pours.length}`
      ]);
      return;
    }

    originals = JOB.placements.map((entry) => {
      const component = byRef.get(entry.ref)[0];
      return {
        ref: entry.ref,
        component,
        x: Number(get(component, 'getState_X')),
        y: Number(get(component, 'getState_Y')),
        rotation: rotationOf(component),
        layer: Number(get(component, 'getState_Layer'))
      };
    });
    const unreadable = originals.filter((item) => ![
      item.x, item.y, item.rotation, item.layer
    ].every(Number.isFinite));
    if (unreadable.length) {
      await showError(`Original component state is unreadable: ${unreadable.map((item) => item.ref).join(', ')}. Nothing was changed.`);
      return;
    }

    const currentU1 = originals.find((item) => item.ref === 'U1');
    const targetU1 = JOB.placements.find((item) => item.ref === 'U1');
    const viaRecords = vias.map(viaStateOf);
    if (viaRecords.length) {
      const readableVias = viaRecords.every((via) => [
        via.x, via.y, via.holeDiameter, via.diameter
      ].every(Number.isFinite));
      const allGnd = viaRecords.every((via) => via.net === 'GND');
      const uniquePrimitiveIds = (
        viaRecords.every((via) => via.primitiveId) &&
        new Set(viaRecords.map((via) => via.primitiveId)).size === viaRecords.length
      );
      const allNearU1 = readableVias && viaRecords.every((via) => (
        Math.hypot(via.x - currentU1.x, via.y - currentU1.y) <= U1_THERMAL_VIA_MAX_DISTANCE_MIL
      ));
      const u1RotationUnchanged = normalizeRotation(currentU1.rotation) === normalizeRotation(targetU1.rotation);
      const thermalArrayAccepted = (
        viaRecords.length === U1_THERMAL_VIA_COUNT && uniquePrimitiveIds &&
        allGnd && allNearU1 && u1RotationUnchanged
      );
      if (!thermalArrayAccepted) {
        await showError([
          'Placement refused because existing vias are not proven as the U1 thermal array. Nothing was changed.',
          `required: exactly ${U1_THERMAL_VIA_COUNT} GND vias within ${U1_THERMAL_VIA_MAX_DISTANCE_MIL} mil of U1 and unchanged U1 rotation`,
          `found: ${viaRecords.length}; unique IDs: ${uniquePrimitiveIds}; all GND: ${allGnd}; all near U1: ${allNearU1}; U1 rotation unchanged: ${u1RotationUnchanged}`,
          ...viaRecords.map((via) => formatVia(via, currentU1))
        ]);
        return;
      }
      if (typeof eda?.pcb_PrimitiveVia?.modify !== 'function') {
        await showError('U1 thermal vias were identified, but the via modify API is unavailable. Nothing was changed.');
        return;
      }
      thermalViaOriginals = viaRecords;
    }

    activeStage = 'confirmation';
    const confirmation = [
      `Move ${JOB.placements.length} components to compact ${JOB.revision}?`,
      `Board target: ${JOB.board.widthMm} x ${JOB.board.heightMm} mm`,
      '',
      'Current board outline is left untouched; create/replace it separately after visual approval.',
      thermalViaOriginals.length
        ? `${thermalViaOriginals.length} verified GND thermal vias will move with U1.`
        : 'No standalone vias were found.',
      'No routing, copper pour, save, schematic edit, or global DRC is performed.'
    ].join('\n');
    const accepted = await confirmApply(confirmation);
    if (!accepted) {
      await finalToast(`${TAG}: cancelled`, 4);
      return;
    }

    changesStarted = true;
    activeStage = 'component-move';
    for (const entry of JOB.placements) {
      await eda.pcb_PrimitiveComponent.modify(byRef.get(entry.ref)[0], {
        layer: TOP,
        x: entry.x,
        y: entry.y,
        rotation: entry.rotation,
        primitiveLock: false
      });
    }
    if (thermalViaOriginals.length) {
      activeStage = 'thermal-via-move';
      const dx = targetU1.x - currentU1.x;
      const dy = targetU1.y - currentU1.y;
      for (const original of thermalViaOriginals) {
        const modified = await eda.pcb_PrimitiveVia.modify(original.via, {
          x: original.x + dx,
          y: original.y + dy,
          primitiveLock: false
        });
        if (!modified) throw new Error(`Via ${original.primitiveId || '[unknown]'} modify returned undefined.`);
      }
    }
    activeStage = 'coordinate-readback';
    const failures = await checkCoordinates();
    if (thermalViaOriginals.length) {
      const dx = targetU1.x - currentU1.x;
      const dy = targetU1.y - currentU1.y;
      const refreshedVias = (await eda.pcb_PrimitiveVia.getAll()) || [];
      const byId = new Map(refreshedVias.map((via) => [
        String(get(via, 'getState_PrimitiveId') || '').trim(), viaStateOf(via)
      ]));
      for (const original of thermalViaOriginals) {
        const actual = byId.get(original.primitiveId) || viaStateOf(original.via);
        const targetX = original.x + dx;
        const targetY = original.y + dy;
        if (!Number.isFinite(actual.x) || !Number.isFinite(actual.y) ||
            Math.abs(actual.x - targetX) > TOLERANCE_MIL || Math.abs(actual.y - targetY) > TOLERANCE_MIL) {
          failures.push(`via ${original.primitiveId || '[unknown]'}: expected (${targetX},${targetY}), got (${actual.x},${actual.y})`);
        }
      }
    }
    if (failures.length) {
      throw new Error(`Coordinate readback failed:\n${failures.slice(0, 20).join('\n')}`);
    }

    committed = true;
    activeStage = 'complete';
    await showInformation([
      `${TAG}: APPLY PASS`,
      `components moved and read back: ${JOB.placements.length}`,
      `U1 thermal vias moved and read back: ${thermalViaOriginals.length}`,
      `compact board target: ${JOB.board.widthMm} x ${JOB.board.heightMm} mm`,
      'Board outline was intentionally not changed.',
      '',
      'Save PCB1, inspect the compact placement, then run the verification script.'
    ].join('\n'));
  } catch (error) {
    const rollbackFailures = committed ? [] : await rollback();
    await showError([
      `Placement aborted at [${activeStage}]: ${error?.stack || error}`,
      rollbackFailures.length
        ? `Rollback needs attention: ${rollbackFailures.join('; ')}`
        : 'Rollback completed; save was not requested.'
    ]);
  }
})();
