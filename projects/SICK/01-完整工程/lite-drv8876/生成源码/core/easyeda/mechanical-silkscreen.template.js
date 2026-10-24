// SkyWorks EasyEDA Pro mechanical and silkscreen finalization script.
// Generated from a validated placement; safe to rerun because it checks exact targets.
(async function skyworksMechanicalSilkscreenMain() {
  const JOB = __SKYWORKS_FINALIZATION_JOB__;
  const MULTI_LAYER = 12;
  const MM_TO_MIL = 39.37007874015748;
  const POSITION_TOLERANCE_MIL = 1;

  const get = (object, method) => {
    try {
      return typeof object?.[method] === 'function' ? object[method]() : undefined;
    } catch (error) {
      return undefined;
    }
  };
  const getAllSafe = async (api, ...args) => {
    try {
      return typeof api?.getAll === 'function' ? (await api.getAll(...args)) || [] : [];
    } catch (error) {
      return [];
    }
  };
  const toast = async (message, type = 3, duration = 3000) => {
    try {
      if (eda?.sys_ToastMessage?.showMessage) await eda.sys_ToastMessage.showMessage(message, type, duration);
      else if (eda?.sys_ToastMessage?.show) await eda.sys_ToastMessage.show(message, type, duration);
    } catch (error) {
      console.warn('Toast unavailable:', error);
    }
  };
  const showError = async (message) => {
    const detail = String(message);
    const phase = detail.split(/\r?\n/, 1)[0].slice(0, 120);
    console.error(detail);
    await toast(`${JOB.project.name}: FAILED - ${phase}`, 2, 600000);
    await eda.sys_MessageBox.showErrorMessage(detail, 'SkyWorks mechanical/silkscreen');
  };
  const confirmApply = async (message) => {
    if (typeof eda?.sys_Dialog?.showConfirmationMessage !== 'function') return false;
    return new Promise((resolve) => {
      eda.sys_Dialog.showConfirmationMessage(
        message,
        'SkyWorks mechanical/silkscreen',
        'Add missing items',
        'Cancel',
        (accepted) => resolve(Boolean(accepted))
      );
    });
  };
  const numberOrNaN = (value) => Number.isFinite(Number(value)) ? Number(value) : Number.NaN;
  const xOf = (item) => numberOrNaN(get(item, 'getState_X'));
  const yOf = (item) => numberOrNaN(get(item, 'getState_Y'));
  const textOf = (item) => String(
    get(item, 'getState_Text') ?? get(item, 'getState_Content') ?? get(item, 'getState_Name') ?? ''
  );
  const holeOf = (item) => get(item, 'getState_Hole');
  const holeDiameter = (item) => {
    const hole = holeOf(item);
    if (Array.isArray(hole)) return numberOrNaN(hole[1]);
    if (hole && typeof hole === 'object') return numberOrNaN(hole.diameter ?? hole.width ?? hole[1]);
    return Number.NaN;
  };

  await toast(`${JOB.project.name}: checking M2 holes and silkscreen`, 3, 600000);
  try {
    if (!eda?.pcb_PrimitiveComponent?.getAll || !eda?.pcb_PrimitivePad?.create || !eda?.pcb_PrimitiveString?.create) {
      await showError('Required PCB APIs are unavailable. Open PCB1 and run this script again.');
      return;
    }
    const components = (await eda.pcb_PrimitiveComponent.getAll()) || [];
    if (components.length !== JOB.project.expectedComponentCount) {
      await showError(`Component preflight failed: ${components.length}/${JOB.project.expectedComponentCount}. Nothing changed.`);
      return;
    }

    const pads = await getAllSafe(eda.pcb_PrimitivePad);
    const strings = await getAllSafe(eda.pcb_PrimitiveString);
    const diameterMil = JOB.mechanical.holeDiameterMm * MM_TO_MIL;
    const missingHoles = [];
    const wrongHoles = [];
    for (const hole of JOB.mechanical.holes) {
      const nearby = pads.filter((pad) =>
        Math.abs(xOf(pad) - hole.x) <= POSITION_TOLERANCE_MIL &&
        Math.abs(yOf(pad) - hole.y) <= POSITION_TOLERANCE_MIL
      );
      if (!nearby.length) {
        missingHoles.push(hole);
        continue;
      }
      const exact = nearby.find((pad) => Math.abs(holeDiameter(pad) - diameterMil) <= 1);
      if (!exact) wrongHoles.push(hole.id);
    }
    if (wrongHoles.length) {
      await showError(`A primitive already occupies a mounting-hole centre but has the wrong drill: ${wrongHoles.join(', ')}.`);
      return;
    }
    const textExistsAtTarget = (target) => strings.some((item) =>
      textOf(item) === target.text &&
      Math.abs(xOf(item) - target.x) <= POSITION_TOLERANCE_MIL &&
      Math.abs(yOf(item) - target.y) <= POSITION_TOLERANCE_MIL
    );
    const missingText = JOB.silkscreen.items.filter((item) => !textExistsAtTarget(item));

    if (!missingHoles.length && !missingText.length) {
      const message = `Nothing to add. All ${JOB.mechanical.holes.length} M2 holes and ${JOB.silkscreen.items.length} labels already exist.`;
      await toast(`${JOB.project.name}: mechanical/silkscreen already complete`, 1, 600000);
      await eda.sys_MessageBox.showInformationMessage(message, 'SkyWorks mechanical/silkscreen');
      return;
    }

    const confirmation = [
      `Add ${missingHoles.length} missing 2.2 mm NPTH mounting hole(s) and ${missingText.length} silkscreen item(s)?`,
      '',
      'Hole centres (mil):',
      ...missingHoles.map((hole) => `  ${hole.id}: ${hole.x}, ${hole.y}`),
      '',
      'The four holes do not change the 128-component schematic/BOM count.',
      'Save PCB1 only after checking 2D, 3D and native DRC.'
    ].join('\n');
    if (!await confirmApply(confirmation)) {
      await toast(`${JOB.project.name}: mechanical/silkscreen cancelled`, 4, 600000);
      return;
    }

    const ellipse = globalThis.EPCB_PrimitivePadShapeType?.ELLIPSE ?? 'ELLIPSE';
    const roundHole = globalThis.EPCB_PrimitivePadHoleType?.ROUND ?? 'ROUND';
    for (const hole of missingHoles) {
      try {
        await eda.pcb_PrimitivePad.create(
          MULTI_LAYER,
          hole.id,
          hole.x,
          hole.y,
          0,
          [ellipse, diameterMil, diameterMil],
          '',
          [roundHole, diameterMil],
          0,
          0,
          0,
          false,
          0,
          undefined,
          null,
          null,
          false
        );
      } catch (error) {
        throw new Error(`CREATE_HOLE ${hole.id} failed: ${error?.message || error}`);
      }
    }

    const alignCenter = globalThis.EPCB_PrimitiveStringAlignMode?.CENTER ?? 5;
    for (const item of missingText) {
      try {
        await eda.pcb_PrimitiveString.create(
          JOB.silkscreen.layer,
          item.x,
          item.y,
          item.text,
          JOB.silkscreen.fontFamily,
          item.size,
          item.width,
          alignCenter,
          item.rotation || 0,
          false,
          0,
          false,
          false
        );
      } catch (error) {
        throw new Error(`CREATE_SILKSCREEN ${item.text} failed: ${error?.message || error}`);
      }
    }

    let nativeDrc = 'not available';
    if (eda?.pcb_Drc?.check) {
      try {
        const details = await eda.pcb_Drc.check(true, true, true);
        nativeDrc = Array.isArray(details) && details.length ? `${details.length} group(s); inspect DRC panel` : 'PASS';
      } catch (error) {
        nativeDrc = `API failed: ${error?.message || error}`;
      }
    }
    const summary = [
      `${JOB.project.name}: MECHANICAL / SILKSCREEN APPLIED`,
      `M2 NPTH added: ${missingHoles.length}`,
      `Silkscreen items added: ${missingText.length}`,
      `Native DRC: ${nativeDrc}`,
      '',
      'Inspect the four holes and readable silkscreen in 2D/3D.',
      'If correct, save PCB1; then configure routing rules before running stage 05.'
    ].join('\n');
    console.log(summary);
    await toast(`${JOB.project.name}: mechanical/silkscreen applied`, 1, 600000);
    await eda.sys_MessageBox.showInformationMessage(summary, 'SkyWorks mechanical/silkscreen');
  } catch (error) {
    await showError(error?.stack || error?.message || String(error));
  }
})();
