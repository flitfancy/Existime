// SICK MOTOR REVERSER R4 LITE - read-only via diagnostic.
// No PCB primitive is modified, created, deleted, routed or saved.
(async function diagnoseExistingVias() {
  const TAG = 'SICK MOTOR REVERSER R4 LITE VIA DIAG';
  const TARGET_U1 = { x: 1370, y: 420 };
  const get = (object, method) => {
    try {
      return typeof object?.[method] === 'function' ? object[method]() : undefined;
    } catch (error) {
      return undefined;
    }
  };
  const finite = (value) => Number.isFinite(Number(value)) ? Number(value) : null;
  const text = (value) => value === undefined || value === null ? '' : String(value).trim();
  const refOf = (component) => text(
    get(component, 'getState_Designator') || get(component, 'getState_Name')
  );
  const show = async (message, failed = false) => {
    console[failed ? 'error' : 'log'](message);
    try {
      await eda?.sys_ToastMessage?.showMessage?.(
        failed ? `${TAG}: FAILED` : `${TAG}: PASS`,
        failed ? 2 : 1,
        600000
      );
    } catch (error) {
      console.warn(`${TAG}: toast unavailable`, error);
    }
    try {
      eda?.sys_Dialog?.showInformationMessage?.(
        message,
        failed ? `${TAG} error` : TAG,
        'OK'
      );
    } catch (error) {
      console.warn(`${TAG}: dialog unavailable`, error);
    }
  };

  try {
    if (!eda?.pcb_PrimitiveVia?.getAll || !eda?.pcb_PrimitiveComponent?.getAll) {
      throw new Error('PCB component/via read API unavailable. Open PCB1 and run again.');
    }
    const [components, vias] = await Promise.all([
      eda.pcb_PrimitiveComponent.getAll(),
      eda.pcb_PrimitiveVia.getAll()
    ]);
    const u1Matches = (components || []).filter((component) => refOf(component) === 'U1');
    if (u1Matches.length !== 1) throw new Error(`U1 found ${u1Matches.length} times.`);
    const u1 = {
      x: finite(get(u1Matches[0], 'getState_X')),
      y: finite(get(u1Matches[0], 'getState_Y'))
    };
    if (u1.x === null || u1.y === null) throw new Error('U1 coordinate is unreadable.');
    const records = (vias || []).map((via, index) => {
      const x = finite(get(via, 'getState_X'));
      const y = finite(get(via, 'getState_Y'));
      const dx = x === null ? null : x - u1.x;
      const dy = y === null ? null : y - u1.y;
      return {
        index: index + 1,
        primitiveId: text(get(via, 'getState_PrimitiveId')),
        net: text(get(via, 'getState_Net')),
        x,
        y,
        dxFromU1: dx,
        dyFromU1: dy,
        distanceFromU1: dx === null || dy === null ? null : Math.round(Math.hypot(dx, dy) * 10) / 10,
        holeDiameter: finite(get(via, 'getState_HoleDiameter')),
        diameter: finite(get(via, 'getState_Diameter')),
        viaType: text(get(via, 'getState_ViaType')),
        locked: Boolean(get(via, 'getState_PrimitiveLock'))
      };
    });
    console.table(records);
    const allGnd = records.length > 0 && records.every((via) => via.net === 'GND');
    const allReadable = records.every((via) => via.x !== null && via.y !== null);
    const maxDistance = allReadable && records.length
      ? Math.max(...records.map((via) => via.distanceFromU1))
      : null;
    const likelyU1Thermal = records.length === 4 && allGnd && allReadable && maxDistance <= 140;
    const delta = { x: TARGET_U1.x - u1.x, y: TARGET_U1.y - u1.y };
    const lines = [
      `${TAG}: READ-ONLY PASS`,
      `U1 current: (${u1.x}, ${u1.y}) mil`,
      `U1 R4-L2 target: (${TARGET_U1.x}, ${TARGET_U1.y}) mil`,
      `U1 move delta: (${delta.x}, ${delta.y}) mil`,
      `vias: ${records.length}; all GND: ${allGnd ? 'YES' : 'NO'}; max distance from U1: ${maxDistance ?? 'unreadable'} mil`,
      `classification: ${likelyU1Thermal ? 'LIKELY U1 THERMAL VIA ARRAY' : 'NOT PROVEN AS U1 THERMAL VIAS'}`,
      '',
      ...records.map((via) => (
        `#${via.index} net=${via.net || '[NoNet]'} xy=(${via.x},${via.y}) ` +
        `dU1=(${via.dxFromU1},${via.dyFromU1}) dist=${via.distanceFromU1} ` +
        `hole=${via.holeDiameter} dia=${via.diameter} lock=${via.locked}`
      )),
      '',
      'Nothing was changed.'
    ];
    await show(lines.join('\n'));
  } catch (error) {
    await show(`${TAG}: ABORTED\n\n${error?.stack || error}\n\nNothing was changed.`, true);
  }
})();
