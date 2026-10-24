// SkyWorks EasyEDA Pro staged auto-routing script.
// This script routes one explicit net group only. It never routes GND or creates pours.
(async function skyworksRoutingStageMain() {
  const JOB = __SKYWORKS_ROUTING_JOB__;

  const toast = async (message, type = 3, duration = 3000) => {
    try {
      if (eda?.sys_ToastMessage?.showMessage) await eda.sys_ToastMessage.showMessage(message, type, duration);
      else if (eda?.sys_ToastMessage?.show) await eda.sys_ToastMessage.show(message, type, duration);
    } catch (error) {
      console.warn('Toast unavailable:', error);
    }
  };
  const showError = async (message) => {
    console.error(message);
    await toast(`${JOB.project.name}: routing ${JOB.stage.id} FAILED`, 2, 600000);
    await eda.sys_MessageBox.showErrorMessage(String(message), 'SkyWorks staged routing');
  };
  const confirmApply = async (message) => {
    if (typeof eda?.sys_Dialog?.showConfirmationMessage !== 'function') return false;
    return new Promise((resolve) => {
      eda.sys_Dialog.showConfirmationMessage(
        message,
        `SkyWorks routing ${JOB.stage.id}`,
        `Route stage ${JOB.stage.order}`,
        'Cancel',
        (accepted) => resolve(Boolean(accepted))
      );
    });
  };

  await toast(`${JOB.project.name}: routing ${JOB.stage.id} preflight`, 3, 600000);
  try {
    if (!eda?.pcb_Document?.autoRouting || !eda?.pcb_PrimitiveComponent?.getAll) {
      await showError('EasyEDA staged auto-routing API is unavailable. Open PCB1 in EasyEDA Pro v3.2.162 or newer.');
      return;
    }
    const components = (await eda.pcb_PrimitiveComponent.getAll()) || [];
    if (components.length !== JOB.project.expectedComponentCount) {
      await showError(`Component preflight failed: ${components.length}/${JOB.project.expectedComponentCount}. Nothing changed.`);
      return;
    }
    const stageSet = new Set(JOB.stage.nets);
    const ignoreNets = JOB.routing.allNets.filter((net) => !stageSet.has(net));
    for (const net of JOB.routing.permanentlyIgnoredNets) {
      if (!ignoreNets.includes(net)) ignoreNets.push(net);
    }
    const rules = JOB.routing.ruleClasses
      .filter((rule) => rule.nets.some((net) => stageSet.has(net)))
      .map((rule) => `${rule.id}: ${rule.widthMil} mil width / ${rule.clearanceMil} mil clearance`);
    const confirmation = [
      `${JOB.project.name} routing stage ${JOB.stage.order}: ${JOB.stage.id}`,
      `${JOB.stage.nets.length} explicit net(s); GND is ignored.`,
      '',
      JOB.stage.instruction,
      '',
      'Required EasyEDA design-rule classes:',
      ...rules.map((rule) => `  ${rule}`),
      '',
      'Run only after those net classes are assigned in Design Rules.',
      'The API is beta. Inspect every route, then save if accepted or undo if rejected.'
    ].join('\n');
    if (!await confirmApply(confirmation)) {
      await toast(`${JOB.project.name}: routing ${JOB.stage.id} cancelled`, 4, 600000);
      return;
    }

    const corner45 = globalThis.EPCB_AutoRoutingCornerStyle?.DEGREE_45 ?? 'DEGREE_45';
    const completion = globalThis.EPCB_AutoRoutingOptimization?.COMPLETION ?? 'COMPLETION';
    const props = {
      // Current API type uses RoutingNets; the official example still shows nets.
      // Supplying both keeps this script compatible across the documented transition.
      RoutingNets: JOB.stage.nets,
      nets: JOB.stage.nets,
      ignoreNets,
      layers: [1, 2],
      cornerStyle: corner45,
      optimization: completion
    };
    console.log('SkyWorks autoRouting props:', props);
    const result = await eda.pcb_Document.autoRouting(props);
    console.log('SkyWorks autoRouting result:', result);

    let nativeDrc = 'not available';
    if (eda?.pcb_Drc?.check) {
      try {
        const details = await eda.pcb_Drc.check(true, true, true);
        nativeDrc = Array.isArray(details) && details.length ? `${details.length} group(s); inspect DRC panel` : 'PASS';
      } catch (error) {
        nativeDrc = `API failed: ${error?.message || error}`;
      }
    }
    const resultText = result && typeof result === 'object'
      ? `routed ${result.routedNets ?? '?'}/${result.totalNets ?? '?'} nets`
      : String(result ?? 'no result payload');
    const summary = [
      `${JOB.project.name}: ROUTING ${JOB.stage.id} FINISHED`,
      `API result: ${resultText}`,
      `Native DRC: ${nativeDrc}`,
      '',
      'Do not assume PASS means the topology is ideal.',
      'Inspect 45-degree geometry, vias, SHT41 local copper and unfinished ratsnest.',
      'Save PCB1 only if accepted; otherwise undo this stage.'
    ].join('\n');
    await toast(`${JOB.project.name}: routing ${JOB.stage.id} finished`, 1, 600000);
    await eda.sys_MessageBox.showInformationMessage(summary, 'SkyWorks staged routing');
  } catch (error) {
    await showError(error?.stack || error?.message || String(error));
  }
})();

