// EasyEDA Pro independent script.
// Run on the generated SICK-E Lite R4 schematic page.
// It enforces J1/J2/J3/J4/SW1: Add into BOM = NO, Add into PCB = YES.
// The public schematic API does not expose a footprint setter, so this script
// verifies the footprint created by the R4 generator but does not replace it.
// Resolves both documented footprint objects and the UUID-only strings
// returned by some V3.2.148 runtime paths. It compares UUIDs when stable and
// otherwise accepts only an exact case-insensitive personal-footprint alias.

(async function main() {
  const expectedFootprints = {
    J1: ['SICK_WIREPAD_2P_5X8_P10', '2P_5X8_P10'],
    J2: ['SICK_WIREPAD_2P_5X8_P10', '2P_5X8_P10'],
    SW1: ['SICK_WIREPAD_2P_5X8_P10', '2P_5X8_P10'],
    J3: ['SICK_WIREPAD_2P_3X5_P5', '2P_3X5_P5'],
    J4: ['SICK_WIREPAD_2P_3X5_P5', '2P_3X5_P5']
  };

  const get = (object, method) => {
    try {
      return typeof object?.[method] === 'function' ? object[method]() : undefined;
    } catch (error) {
      return undefined;
    }
  };

  const exactNamedItems = (items, name) => {
    const wanted = String(name).trim().toUpperCase();
    return (items || []).filter((item) => String(item?.name || '').trim().toUpperCase() === wanted);
  };

  const resolveExpectedFootprint = async (names, personalLibraryUuid) => {
    const byId = new Map();
    for (const name of names) {
      const results = await eda.lib_Footprint.search(name, personalLibraryUuid, undefined, 50, 1);
      for (const item of exactNamedItems(results, name)) {
        byId.set(`${item.libraryUuid || personalLibraryUuid}:${item.uuid}`, item);
      }
    }
    const matches = [...byId.values()];
    if (matches.length !== 1) {
      throw new Error(
        `Expected exactly one personal footprint matching ${names.join(' or ')}; found ${matches.length}.`
      );
    }
    const item = matches[0];
    return {
      aliases: names,
      name: String(item.name || ''),
      uuid: String(item.uuid || ''),
      libraryUuid: String(item.libraryUuid || personalLibraryUuid)
    };
  };

  const footprintInfoOf = async (component, expected, personalLibraryUuid) => {
    const footprint = get(component, 'getState_Footprint');
    if (!footprint) return { name: '', uuid: '', libraryUuid: '', raw: '' };
    if (typeof footprint === 'string') {
      const raw = String(footprint);
      if (raw === expected.uuid) return { ...expected, raw };
      if (expected.aliases.some((name) => name.toUpperCase() === raw.toUpperCase())) {
        return { name: raw, uuid: '', libraryUuid: '', raw };
      }
      if (typeof eda.lib_Footprint?.get === 'function') {
        try {
          const item = await eda.lib_Footprint.get(raw, personalLibraryUuid);
          if (item) {
            return {
              name: String(item.name || ''),
              uuid: String(item.uuid || raw),
              libraryUuid: String(item.libraryUuid || personalLibraryUuid),
              raw
            };
          }
        } catch (error) {
          // Keep the raw runtime value for the diagnostic report.
        }
      }
      return { name: '', uuid: raw, libraryUuid: '', raw };
    }
    const directName = String(
      footprint.name || footprint.footprintName || footprint.displayName ||
      footprint.title || ''
    );
    const direct = {
      name: directName,
      uuid: String(footprint.uuid || ''),
      libraryUuid: String(footprint.libraryUuid || ''),
      raw: String(footprint.uuid || directName || '')
    };
    if (directName) return direct;
    if (
      footprint.uuid && footprint.libraryUuid &&
      typeof eda.lib_Footprint?.get === 'function'
    ) {
      const libraryItem = await eda.lib_Footprint.get(footprint.uuid, footprint.libraryUuid);
      if (libraryItem) {
        return {
          name: String(libraryItem.name || ''),
          uuid: String(libraryItem.uuid || footprint.uuid),
          libraryUuid: String(libraryItem.libraryUuid || footprint.libraryUuid),
          raw: String(footprint.uuid)
        };
      }
    }
    return direct;
  };

  if (
    typeof eda.lib_LibrariesList?.getPersonalLibraryUuid !== 'function' ||
    typeof eda.lib_Footprint?.search !== 'function'
  ) {
    throw new Error('This EasyEDA Pro version cannot resolve personal footprint UUIDs.');
  }
  const personalLibraryUuid = await eda.lib_LibrariesList.getPersonalLibraryUuid();
  if (!personalLibraryUuid) throw new Error('No online personal library is available.');

  const expectedByAliases = new Map();
  for (const names of Object.values(expectedFootprints)) {
    const key = names.join('|');
    if (!expectedByAliases.has(key)) {
      expectedByAliases.set(key, await resolveExpectedFootprint(names, personalLibraryUuid));
    }
  }

  const all = (await eda.sch_PrimitiveComponent.getAll(undefined, false)) || [];
  const byRef = new Map();
  for (const component of all) {
    const ref = String(get(component, 'getState_Designator') || '').trim().toUpperCase();
    if (!expectedFootprints[ref]) continue;
    if (byRef.has(ref)) throw new Error(`Duplicate designator ${ref} on the current page.`);
    byRef.set(ref, component);
  }

  const missing = Object.keys(expectedFootprints).filter((ref) => !byRef.has(ref));
  if (missing.length) {
    throw new Error(`Missing wire symbols on the current page: ${missing.join(', ')}`);
  }

  const summary = [];
  for (const [ref, component] of byRef) {
    let editable = component;
    if (typeof editable?.toAsync === 'function') {
      const converted = await editable.toAsync();
      if (converted) editable = converted;
    }
    if (
      typeof editable?.setState_AddIntoBom !== 'function' ||
      typeof editable?.setState_AddIntoPcb !== 'function' ||
      typeof editable?.done !== 'function'
    ) {
      throw new Error('This EasyEDA Pro version cannot edit schematic BOM/PCB flags; update EasyEDA Pro.');
    }

    editable.setState_AddIntoBom(false);
    editable.setState_AddIntoPcb(true);
    await editable.done();

    const addIntoBom = get(editable, 'getState_AddIntoBom');
    const addIntoPcb = get(editable, 'getState_AddIntoPcb');
    if (addIntoBom !== false || addIntoPcb !== true) {
      throw new Error(`${ref} flag verification failed: BOM=${addIntoBom}, PCB=${addIntoPcb}`);
    }

    const expectedNames = expectedFootprints[ref];
    const expectedFootprint = expectedByAliases.get(expectedNames.join('|'));
    const currentFootprint = await footprintInfoOf(editable, expectedFootprint, personalLibraryUuid);
    const nameMatches = expectedNames.some(
      (name) => currentFootprint.name.toUpperCase() === name.toUpperCase()
    );
    const uuidMatches = Boolean(
      currentFootprint.uuid && expectedFootprint.uuid &&
      currentFootprint.uuid === expectedFootprint.uuid
    );
    summary.push({
      ref,
      addIntoBom,
      addIntoPcb,
      currentFootprint: currentFootprint.name || currentFootprint.uuid || '(not assigned)',
      currentFootprintUuid: currentFootprint.uuid || '(none)',
      expectedFootprint: expectedFootprint.name,
      expectedFootprintUuid: expectedFootprint.uuid,
      footprintMatches: nameMatches || uuidMatches
    });
  }

  summary.sort((a, b) => a.ref.localeCompare(b.ref, undefined, { numeric: true }));
  console.table(summary);

  const report = summary.map((item) => (
    `${item.ref}: ${item.footprintMatches ? 'PASS' : 'FAIL'}\n` +
    `  actual: ${item.currentFootprint} [${item.currentFootprintUuid}]\n` +
    `  expect: ${item.expectedFootprint} [${item.expectedFootprintUuid}]`
  )).join('\n');
  if (typeof eda.sys_Dialog?.showInformationMessage === 'function') {
    eda.sys_Dialog.showInformationMessage(report, 'SICK-E Lite wire-pad footprint audit', 'OK');
  }

  const footprintMismatch = summary.filter((item) => !item.footprintMatches);
  const message = footprintMismatch.length
    ? `Wire flags fixed for 5 refs. Assign/check custom footprints for: ${footprintMismatch.map((item) => item.ref).join(', ')}`
    : 'Wire flags and all five custom footprints verified.';
  await eda.sys_ToastMessage.showMessage(message, footprintMismatch.length ? 1 : 2);
})().catch(async (error) => {
  try {
    await eda.sys_ToastMessage.showMessage(`Wire-pad flag script stopped: ${error?.message || String(error)}`, 3);
  } catch (toastError) {
    // Preserve the original failure.
  }
  throw error;
});
