// SkyWorks EasyEDA Pro routing verification checkpoint.
(async function skyworksRoutingVerifyMain() {
  const JOB = __SKYWORKS_ROUTING_VERIFY_JOB__;
  const getAllSafe = async (api, ...args) => {
    try {
      return typeof api?.getAll === 'function' ? (await api.getAll(...args)) || [] : [];
    } catch (error) {
      return [];
    }
  };
  try {
    const components = (await eda.pcb_PrimitiveComponent.getAll()) || [];
    const topLines = await getAllSafe(eda?.pcb_PrimitiveLine, undefined, 1);
    const bottomLines = await getAllSafe(eda?.pcb_PrimitiveLine, undefined, 2);
    const vias = await getAllSafe(eda?.pcb_PrimitiveVia);
    const pours = await getAllSafe(eda?.pcb_PrimitivePour);
    let nativeDrc = 'not available';
    if (eda?.pcb_Drc?.check) {
      const details = await eda.pcb_Drc.check(true, true, true);
      nativeDrc = Array.isArray(details) && details.length ? `${details.length} group(s); inspect DRC panel` : 'PASS';
    }
    const failures = [];
    if (components.length !== JOB.project.expectedComponentCount) {
      failures.push(`components ${components.length}/${JOB.project.expectedComponentCount}`);
    }
    if (!topLines.length && !bottomLines.length) failures.push('no routed copper lines found');
    if (pours.length) failures.push('copper pours already exist; inspect GND strategy before continuing');
    const summary = [
      `${JOB.project.name}: ROUTING CHECKPOINT`,
      `Components: ${components.length}/${JOB.project.expectedComponentCount}`,
      `Top lines: ${topLines.length}; bottom lines: ${bottomLines.length}; vias: ${vias.length}; pours: ${pours.length}`,
      `Native DRC: ${nativeDrc}`,
      `Checkpoint findings: ${failures.join('; ') || 'none'}`,
      '',
      'Manual checks still required:',
      '1. SHT41 has only short local traces and no plane/power trunk beneath its thermal zone.',
      '2. SCD40 supply is wide and its air opening remains at the board edge.',
      '3. No route crosses optical apertures or mounting-hole clearances.',
      '4. Remaining GND ratsnest is resolved with a solid plane and stitching vias.',
      '5. Native DRC and unconnected-net count are both zero before fabrication.'
    ].join('\n');
    console.log(summary);
    const isError = failures.length || nativeDrc !== 'PASS';
    if (isError) await eda.sys_MessageBox.showErrorMessage(summary, 'SkyWorks routing checkpoint');
    else await eda.sys_MessageBox.showInformationMessage(summary, 'SkyWorks routing checkpoint');
  } catch (error) {
    await eda.sys_MessageBox.showErrorMessage(error?.stack || error?.message || String(error), 'SkyWorks routing checkpoint');
  }
})();

