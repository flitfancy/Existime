// EasyEDA Pro independent script.
// Creates SICK motor reverser R4 Lite schematic.
// D1 is the only indicator LED. U1 is a DRV8876 in PH/EN mode; MOTOR_EN
// drives nSLEEP so the dual-open limit state leaves both motor outputs Hi-Z.
//
// Run this on a NEW BLANK schematic page. Keep the proven V2 project as backup.
// R4.1: all standard parts are resolved before drawing; duplicate/missing
// designators stop the script. LCSC C-numbers use the exact-ID API. The two custom
// footprints must already exist in the current account's personal library.
// Before drawing anything, this script creates/reuses two personal devices that
// associate the proven two-pin symbol with those footprints. The five wire
// interfaces therefore arrive on the schematic with their final footprints.

(async function main() {
  const colors = {
    title: '#111827',
    note: '#374151',
    power: '#C2410C',
    signal: '#2563EB',
    motor: '#059669',
    gnd: '#4B5563',
    warn: '#B45309'
  };

  const LED_GREEN = ['C965815', 'XL-2012UGC'];
  const EXPECTED_REFS = Object.freeze([
    'F1', 'SW1',
    'U1', 'U2', 'U3', 'U4', 'U5',
    'J1', 'J2', 'J3', 'J4',
    'D1', 'D5', 'D6', 'D7',
    'R1', 'R5', 'R6', 'R7', 'R8',
    'C1', 'C2', 'C3', 'C4', 'C5', 'C6', 'C7', 'C8', 'C9', 'C10', 'C11'
  ]);
  const REQUIRED_LCSC_IDS = Object.freeze([
    'C557665',  // verified two-pin source symbol for personal wire-pad devices
    'C20539726', 'C575551', 'C14289', 'C17673', 'C965815',
    'C15850', 'C1713', 'C596320', 'C1711', 'C10768', 'C1729',
    'C6820', 'C25612', 'C6762', 'C149504', 'C81598'
  ]);
  const WIRE_COMPONENT_OPTIONS = Object.freeze({ addIntoBom: false, addIntoPcb: true });
  const STOCK_COMPONENT_OPTIONS = Object.freeze({ addIntoBom: false, addIntoPcb: true });
  const WIRE_LIBRARY = Object.freeze({
    large: Object.freeze({
      footprintNames: Object.freeze(['SICK_WIREPAD_2P_5X8_P10', '2P_5X8_P10']),
      deviceName: 'SICK_WIREPAD_DEVICE_2P_5X8_P10',
      description: 'SICK3 two-pin high-current direct-wire SMD pad device; no holes or paste opening.'
    }),
    small: Object.freeze({
      footprintNames: Object.freeze(['SICK_WIREPAD_2P_3X5_P5', '2P_3X5_P5']),
      deviceName: 'SICK_WIREPAD_DEVICE_2P_3X5_P5',
      description: 'SICK3 two-pin signal direct-wire SMD pad device; no holes or paste opening.'
    })
  });
  const deviceCache = {};
  const placedRefs = new Set();
  const LCSC_ID = /^C\d+$/i;

  const t = async (x, y, s, size = 10, bold = false, color = null) => {
    return await eda.sch_PrimitiveText.create(x, y, s, 0, color, null, size, bold, false, false);
  };

  const colorForNet = (net) => {
    if (net === 'GND') return colors.gnd;
    if (net.includes('12V') || net === '+3V3') return colors.power;
    if (net.includes('MOTOR')) return colors.motor;
    return colors.signal;
  };

  const get = (object, method) => {
    try {
      return typeof object?.[method] === 'function' ? object[method]() : undefined;
    } catch (error) {
      return undefined;
    }
  };

  const pinsOf = async (component) => {
    const primitiveId = get(component, 'getState_PrimitiveId');
    if (!primitiveId) return [];
    return (await eda.sch_PrimitiveComponent.getAllPinsByPrimitiveId(primitiveId)) || [];
  };

  const expectedPinCountFor = (ref) => {
    if (/^[RCDFJ]\d+$/.test(ref) || ref === 'SW1' || ref === 'U5') return 2;
    return { U1: 17, U2: 3, U3: 14, U4: 14 }[ref];
  };

  const exactTextMatch = (item, key) => {
    const wanted = String(key).trim().toUpperCase();
    const values = [
      item?.name,
      item?.displayName,
      item?.title,
      item?.deviceName,
      item?.modelName,
      item?.otherProperty?.lcscId,
      item?.otherProperty?.supplierId,
      item?.otherProperty?.['LCSC Part'],
      item?.otherProperty?.['立创编号']
    ];
    return values.some((value) => String(value || '').trim().toUpperCase() === wanted);
  };

  const findDevice = async (keyOrKeys) => {
    const keys = Array.isArray(keyOrKeys) ? keyOrKeys : [keyOrKeys];
    const cacheKey = keys.join('|');
    if (deviceCache[cacheKey]) return deviceCache[cacheKey];

    for (const rawKey of keys) {
      const key = String(rawKey).trim();
      if (deviceCache[key]) {
        deviceCache[cacheKey] = deviceCache[key];
        return deviceCache[key];
      }

      if (LCSC_ID.test(key)) {
        if (typeof eda.lib_Device.getByLcscIds !== 'function') {
          throw new Error('This EasyEDA Pro version has no exact LCSC lookup API; update EasyEDA Pro before running this script.');
        }
        const exactResults = await eda.lib_Device.getByLcscIds([key], undefined, false);
        const exact = Array.isArray(exactResults) ? exactResults[0] : exactResults;
        if (!exact) throw new Error(`Exact LCSC lookup returned no device for ${key}`);
        deviceCache[key] = exact;
        deviceCache[cacheKey] = exact;
        return exact;
      }

      // Non-C-number aliases are allowed only when a result matches exactly.
      // Fuzzy first-result selection is deliberately forbidden.
      const results = await eda.lib_Device.search(key, undefined, undefined, undefined, 20, 1);
      const exact = (results || []).find((item) => exactTextMatch(item, key));
      if (exact) {
        deviceCache[key] = exact;
        deviceCache[cacheKey] = exact;
        return exact;
      }
    }

    throw new Error(`No exact device result for ${keys.join(', ')}`);
  };

  const preflightStandardDevices = async () => {
    for (const lcscId of REQUIRED_LCSC_IDS) await findDevice(lcscId);
  };

  const exactNamedItems = (items, name) => {
    const wanted = String(name).trim().toUpperCase();
    return (items || []).filter((item) => String(item?.name || '').trim().toUpperCase() === wanted);
  };

  const associationRef = (association, modernKey, legacyKey, fallbackLibraryUuid) => {
    const modern = association?.[modernKey];
    if (modern?.uuid) {
      return {
        uuid: String(modern.uuid),
        libraryUuid: String(modern.libraryUuid || fallbackLibraryUuid)
      };
    }
    const legacyUuid = association?.[legacyKey];
    if (!legacyUuid) return undefined;
    return { uuid: String(legacyUuid), libraryUuid: String(fallbackLibraryUuid) };
  };

  const assertWireLibraryApis = () => {
    const required = [
      ['LIB_LibrariesList.getPersonalLibraryUuid', eda.lib_LibrariesList?.getPersonalLibraryUuid],
      ['LIB_Footprint.search', eda.lib_Footprint?.search],
      ['LIB_Device.search', eda.lib_Device?.search],
      ['LIB_Device.get', eda.lib_Device?.get],
      ['LIB_Device.create', eda.lib_Device?.create]
    ];
    const missing = required.filter(([, fn]) => typeof fn !== 'function').map(([name]) => name);
    if (missing.length) {
      throw new Error(`EasyEDA Pro is missing personal-library APIs: ${missing.join(', ')}`);
    }
  };

  const ensureWireDevice = async (spec, personalLibraryUuid, sourceSymbol) => {
    const footprintById = new Map();
    for (const footprintName of spec.footprintNames) {
      const results = await eda.lib_Footprint.search(
        footprintName, personalLibraryUuid, undefined, 50, 1
      );
      for (const footprint of exactNamedItems(results, footprintName)) {
        footprintById.set(`${footprint.libraryUuid || personalLibraryUuid}:${footprint.uuid}`, footprint);
      }
    }
    const exactFootprints = [...footprintById.values()];
    if (exactFootprints.length !== 1) {
      throw new Error(
        `Expected exactly one personal footprint matching ${spec.footprintNames.join(' or ')}; ` +
        `found ${exactFootprints.length}.`
      );
    }
    const footprint = exactFootprints[0];
    const footprintRef = {
      uuid: String(footprint.uuid),
      libraryUuid: String(footprint.libraryUuid || personalLibraryUuid)
    };

    const deviceResults = await eda.lib_Device.search(
      spec.deviceName, personalLibraryUuid, undefined, undefined, 50, 1
    );
    const exactDevices = exactNamedItems(deviceResults, spec.deviceName);
    if (exactDevices.length > 1) {
      throw new Error(`Duplicate personal devices named ${spec.deviceName}; keep exactly one and rerun.`);
    }

    let device;
    if (exactDevices.length === 1) {
      const found = exactDevices[0];
      device = await eda.lib_Device.get(found.uuid, found.libraryUuid || personalLibraryUuid);
      if (!device) throw new Error(`Could not read existing personal device ${spec.deviceName}.`);
    } else {
      const createdUuid = await eda.lib_Device.create(
        personalLibraryUuid,
        spec.deviceName,
        undefined,
        { symbol: sourceSymbol, footprint: footprintRef },
        spec.description,
        {
          name: '2P direct-wire SMD pads',
          designator: 'J?',
          addIntoBom: false,
          addIntoPcb: true
        }
      );
      if (!createdUuid) throw new Error(`Failed to create personal device ${spec.deviceName}.`);
      device = await eda.lib_Device.get(createdUuid, personalLibraryUuid);
      if (!device) {
        device = { libraryUuid: personalLibraryUuid, uuid: createdUuid, name: spec.deviceName };
      }
    }

    const associatedFootprint = associationRef(
      device.association, 'footprint', 'footprintUuid', device.libraryUuid || personalLibraryUuid
    );
    if (!associatedFootprint || associatedFootprint.uuid !== footprintRef.uuid) {
      throw new Error(
        `Personal device ${spec.deviceName} is not associated with ${footprint.name}; ` +
        'fix or delete that device and rerun.'
      );
    }
    return device;
  };

  const prepareWireDevices = async () => {
    assertWireLibraryApis();
    const personalLibraryUuid = await eda.lib_LibrariesList.getPersonalLibraryUuid();
    if (!personalLibraryUuid) {
      throw new Error('No personal library is available. Log in to the online personal workspace and rerun.');
    }

    const sourceSearchItem = await findDevice('C557665');
    const sourceDevice = await eda.lib_Device.get(sourceSearchItem.uuid, sourceSearchItem.libraryUuid);
    if (!sourceDevice) throw new Error('Could not read the verified C557665 two-pin source device.');
    const sourceSymbol = associationRef(
      sourceDevice.association, 'symbol', 'symbolUuid', sourceDevice.libraryUuid
    );
    if (!sourceSymbol) throw new Error('C557665 has no readable source-symbol association.');

    return {
      large: await ensureWireDevice(WIRE_LIBRARY.large, personalLibraryUuid, sourceSymbol),
      small: await ensureWireDevice(WIRE_LIBRARY.small, personalLibraryUuid, sourceSymbol)
    };
  };

  const setDesignator = async (component, designator) => {
    try {
      let editable = component;
      if (typeof editable?.toAsync === 'function') {
        const converted = await editable.toAsync();
        if (converted) editable = converted;
      }
      if (typeof editable?.setState_Designator !== 'function') {
        throw new Error('setState_Designator API is unavailable');
      }
      editable.setState_Designator(designator);
      if (typeof editable?.done === 'function') await editable.done();
      const actual = get(editable, 'getState_Designator');
      if (actual !== undefined && String(actual) !== String(designator)) {
        throw new Error(`designator readback is ${actual}`);
      }
      return editable;
    } catch (error) {
      throw new Error(`Could not set ${designator}: ${error?.message || String(error)}`);
    }
  };

  const place = async (key, ref, x, y, rotation = 0, note = '', options = {}) => {
    let component;
    try {
      if (placedRefs.has(ref)) throw new Error(`duplicate designator ${ref}`);
      const { addIntoBom = true, addIntoPcb = true } = options;
      const device = key && typeof key === 'object' && key.uuid && key.libraryUuid
        ? key
        : await findDevice(key);
      component = await eda.sch_PrimitiveComponent.create(
        device, x, y, undefined, rotation, false, addIntoBom, addIntoPcb
      );
      const expectedPins = expectedPinCountFor(ref);
      const actualPins = (await pinsOf(component)).length;
      if (expectedPins !== undefined && actualPins !== expectedPins) {
        throw new Error(`library identity check failed: expected ${expectedPins} pins, got ${actualPins}`);
      }
      const keyText = key && typeof key === 'object'
        ? (key.name || key.uuid)
        : (Array.isArray(key) ? key[0] : key);
      component = await setDesignator(component, ref);
      placedRefs.add(ref);
      await t(x - 18, y + 28, `${ref} (${keyText})${note ? ` ${note}` : ''}`, 8, true, colors.note);
      return component;
    } catch (error) {
      if (component) {
        try {
          await eda.sch_PrimitiveComponent.delete(component);
        } catch (deleteError) {
          // Preserve the original placement failure.
        }
      }
      await t(x, y, `${ref} place/search failed: ${error?.message || String(error)}`, 10, false, colors.warn);
      throw error;
    }
  };

  const pinByNumber = async (component, number) => {
    const pins = await pinsOf(component);
    return pins.find((pin) => String(get(pin, 'getState_PinNumber')) === String(number));
  };

  const stub = async (component, pinNumber, net, label = net) => {
    if (!component) return;
    const pin = await pinByNumber(component, pinNumber);
    if (!pin) {
      const cx = get(component, 'getState_X') || 0;
      const cy = get(component, 'getState_Y') || 0;
      await t(cx + 40, cy - 18, `missing pin ${pinNumber} -> ${net}`, 8, false, colors.warn);
      return;
    }

    const px = get(pin, 'getState_X');
    const py = get(pin, 'getState_Y');
    const cx = get(component, 'getState_X') || px;
    const dx = px < cx ? -8 : 8;
    const endX = px + dx;
    const endY = py;
    await eda.sch_PrimitiveWire.create([px, py, endX, endY], net, colorForNet(net), 1, null);
    await t(dx < 0 ? endX - 72 : endX + 3, endY + 3, label, 6, false, colorForNet(net));
  };

  const noConnect = async (component, pinNumbers) => {
    if (!component) return;
    for (const number of pinNumbers) {
      try {
        const pin = await pinByNumber(component, number);
        if (!pin) continue;
        let editable = pin;
        if (typeof editable?.toAsync === 'function') {
          const converted = await editable.toAsync();
          if (converted) editable = converted;
        }
        if (typeof editable?.setState_NoConnected === 'function') editable.setState_NoConnected(true);
        if (typeof editable?.done === 'function') await editable.done();
      } catch (error) {
        // Best-effort only.
      }
    }
  };

  const connect = async (component, map) => {
    for (const [pin, net] of Object.entries(map)) await stub(component, pin, net);
  };

  const assertPlacedReferences = () => {
    const missing = EXPECTED_REFS.filter((ref) => !placedRefs.has(ref));
    const extra = [...placedRefs].filter((ref) => !EXPECTED_REFS.includes(ref));
    if (missing.length || extra.length || placedRefs.size !== EXPECTED_REFS.length) {
      throw new Error(
        `reference audit failed; missing=[${missing.join(', ')}], extra=[${extra.join(', ')}]`
      );
    }
  };

  // All global devices and personal-library wire devices are resolved before
  // the first drawing call. A missing exact part or footprint leaves the blank
  // schematic page untouched.
  await preflightStandardDevices();
  const wireDevices = await prepareWireDevices();

  await t(55, 790, 'SICK_MOTOR_REVERSER_R4_LITE - COMPACT SMD AUTO REVERSE', 17, true, colors.title);
  await t(55, 768, 'R4 Lite keeps the proven no-MCU limit/latch logic and replaces the oversized H-bridge.', 10, false, colors.note);
  await t(55, 748, 'J1-J4/SW1 use verified personal wire-pad devices, stay on PCB, and are excluded from BOM.', 10, false, colors.warn);

  // Main power and motor path.
  // Wire devices reuse the verified C557665 two-pin symbol but carry the final
  // personal footprints from the moment they are placed.
  const J1 = await place(wireDevices.large, 'J1', 90, 680, 0, '12V wire pads - 5X8_P10', WIRE_COMPONENT_OPTIONS);
  const SW1 = await place(wireDevices.large, 'SW1', 220, 680, 0, 'off-board switch pads - 5X8_P10', WIRE_COMPONENT_OPTIONS);
  const F1 = await place(['C20539726', 'BSMD2920-400-30V'], 'F1', 350, 680, 0, 'PTC/fuse');
  const U1 = await place(['C575551', 'DRV8876PWPR'], 'U1', 650, 655, 0, 'DRV8876 PH/EN');
  const J2 = await place(wireDevices.large, 'J2', 1010, 660, 0, 'motor wire pads - 5X8_P10', WIRE_COMPONENT_OPTIONS);

  await connect(J1, { 1: '+12V_IN', 2: 'GND' });
  await connect(SW1, { 1: '+12V_IN', 2: '+12V_SW' });
  await connect(F1, { 1: '+12V_SW', 2: '+12V_PROT' });
  await connect(J2, { 1: 'MOTOR_A', 2: 'MOTOR_B' });

  // DRV8876PWPR PWP pins. EN stays high; PH follows the direction latch.
  // MOTOR_EN drives nSLEEP, so dual-open inhibit is Hi-Z rather than brake.
  // PMODE=GND selects PH/EN. IMODE=GND, IPROPI=GND and VREF=3V3 disable
  // current chopping while retaining the driver's automatic-retry OCP.
  await connect(U1, {
    1: '+3V3',
    2: 'DIR_RIGHT_Q',
    3: 'MOTOR_EN',
    5: '+3V3',
    6: 'GND',
    7: 'GND',
    8: 'MOTOR_A',
    9: 'GND',
    10: 'MOTOR_B',
    11: '+12V_PROT',
    12: 'VCP',
    13: 'CPH',
    14: 'CPL',
    15: 'GND',
    16: 'GND',
    17: 'GND'
  });
  await noConnect(U1, [4]);

  // Regulator, driver support, bulk capacitance, TVS, and one power LED.
  const U2 = await place(['C14289', 'HT7533-1'], 'U2', 120, 510, 0, 'HT7533-1 3.3V LDO');
  const R_LED12 = await place('C17673', 'R1', 270, 585, 0, '4.7k 0805');
  const LED12 = await place(LED_GREEN, 'D1', 365, 585, 0, '12V_SW LED');
  const CIN = await place('C15850', 'C2', 120, 450, 0, '10uF');
  const COUT = await place('C1713', 'C3', 220, 450, 0, '10uF 16V 0805');
  // C596320 supplies only a verified standard 1210 symbol/footprint. C1 is
  // excluded from the EasyEDA purchase BOM because the fitted part is the
  // user's 330uF/16V stock capacitor, not the donor's electrical value.
  const CBULK = await place('C596320', 'C1', 465, 560, 0, 'USER STOCK 330uF 16V 1210', STOCK_COMPONENT_OPTIONS);
  const CU1 = await place('C1711', 'C4', 590, 545, 0, 'VM bypass 100nF 50V');
  const TVS = await place(['C10768', 'SMAJ18CA'], 'U5', 470, 500, 0, '18V bidirectional TVS');
  const CVCP = await place('C1711', 'C10', 730, 545, 0, 'VCP-VM 100nF');
  const CFLY = await place(['C1729', 'CL21B223KBANNNC'], 'C11', 830, 545, 0, 'CPH-CPL 22nF');

  // HT7533-1 SOT-89 pinout: 1=GND, 2=VIN, 3=VOUT.
  await connect(U2, { 1: 'GND', 2: '+12V_PROT', 3: '+3V3' });
  await connect(R_LED12, { 1: '+12V_PROT', 2: 'LED_PWR_A' });
  await connect(LED12, { 1: 'GND', 2: 'LED_PWR_A' });
  await connect(CIN, { 1: '+12V_PROT', 2: 'GND' });
  await connect(COUT, { 1: '+3V3', 2: 'GND' });
  await connect(CBULK, { 1: '+12V_PROT', 2: 'GND' });
  await connect(CU1, { 1: '+12V_PROT', 2: 'GND' });
  await connect(TVS, { 1: '+12V_PROT', 2: 'GND' });
  await connect(CVCP, { 1: 'VCP', 2: '+12V_PROT' });
  await connect(CFLY, { 1: 'CPH', 2: 'CPL' });

  // Limit switch inputs and Schmitt cleanup.
  const JL = await place(wireDevices.small, 'J3', 90, 340, 0, 'left limit NC - 3X5_P5', WIRE_COMPONENT_OPTIONS);
  const JR = await place(wireDevices.small, 'J4', 90, 265, 0, 'right limit NC - 3X5_P5', WIRE_COMPONENT_OPTIONS);
  const U3 = await place('C6820', 'U3', 430, 310, 0, '74HC14');
  const R_L_PULL = await place('C25612', 'R5', 250, 350, 0, '10k pullup 0805');
  const R_R_PULL = await place('C25612', 'R6', 250, 275, 0, '10k pullup 0805');
  const C_L_DEB = await place('C1711', 'C5', 250, 315, 0, '100nF 0805');
  const C_R_DEB = await place('C1711', 'C6', 250, 240, 0, '100nF 0805');
  const CU3 = await place('C1711', 'C7', 430, 215, 0, '100nF 0805');

  await connect(JL, { 1: 'L_RAW', 2: 'GND' });
  await connect(JR, { 1: 'R_RAW', 2: 'GND' });
  await connect(R_L_PULL, { 1: '+3V3', 2: 'L_RAW' });
  await connect(R_R_PULL, { 1: '+3V3', 2: 'R_RAW' });
  await connect(C_L_DEB, { 1: 'L_RAW', 2: 'GND' });
  await connect(C_R_DEB, { 1: 'R_RAW', 2: 'GND' });
  await connect(CU3, { 1: '+3V3', 2: 'GND' });
  await connect(U3, {
    1: 'L_RAW',
    2: 'L_SET_N',
    3: 'R_RAW',
    4: 'R_CLR_N',
    5: 'DIR_SET_RC_N',
    6: 'DIR_SET_RC_P',
    7: 'GND',
    8: 'DIR_SET_N',
    9: 'DIR_SET_RC_P',
    11: 'GND',
    13: 'GND',
    14: '+3V3'
  });
  await noConnect(U3, [10, 12]);

  // Direction latch and diode interlocks.
  const U4 = await place('C6762', 'U4', 790, 315, 0, '74HC74');
  const R_SET = await place('C149504', 'R7', 1000, 420, 0, '100k 0805');
  const C_SET = await place('C1711', 'C8', 1000, 380, 0, '100nF 0805');
  const D_SET = await place('C81598', 'D5', 1100, 420, 0, '1N4148W');
  const D_EN_L = await place('C81598', 'D6', 1100, 350, 0, '1N4148W');
  const D_EN_R = await place('C81598', 'D7', 1100, 315, 0, '1N4148W');
  const R_EN_PD = await place('C25612', 'R8', 1000, 315, 0, '10k 0805');
  const CU4 = await place('C1711', 'C9', 790, 215, 0, '100nF 0805');

  await connect(U4, {
    1: 'R_CLR_N',
    2: 'GND',
    3: 'GND',
    4: 'DIR_SET_N',
    5: 'DIR_RIGHT_Q',
    6: 'DIR_LEFT_NQ',
    7: 'GND',
    10: '+3V3',
    11: 'GND',
    12: 'GND',
    13: '+3V3',
    14: '+3V3'
  });
  await noConnect(U4, [8, 9]);
  await connect(R_SET, { 1: '+3V3', 2: 'DIR_SET_RC_N' });
  await connect(C_SET, { 1: 'DIR_SET_RC_N', 2: 'GND' });
  // 1N4148W library pinout: 1=K, 2=A.
  await connect(D_SET, { 1: 'L_SET_N', 2: 'DIR_SET_RC_N' });
  await connect(D_EN_L, { 1: 'MOTOR_EN', 2: 'L_SET_N' });
  await connect(D_EN_R, { 1: 'MOTOR_EN', 2: 'R_CLR_N' });
  await connect(R_EN_PD, { 1: 'MOTOR_EN', 2: 'GND' });
  await connect(CU4, { 1: '+3V3', 2: 'GND' });

  await t(55, 150, 'R4 Lite wiring notes:', 12, true, colors.title);
  await t(55, 137, 'J1/J2/SW1: 5X8_P10. J3/J4: 3X5_P5. BOM exclusion is automatic; PCB conversion stays on.', 9, false, colors.warn);
  await t(55, 120, 'D1 is the only indicator. C1 is user-stock 330uF/16V/1210; do not order donor C596320.', 9, false, colors.note);
  await t(55, 102, 'U3 gates 3 and 4 square the R7/C8/D5 preset path; DIR_SET_N polarity remains active-low.', 9, false, colors.note);
  await t(55, 84, 'J3/J4 limit switches: board pin1 = RAW signal to NC, board pin2 = GND to COM. NO terminal is unused.', 9, false, colors.note);
  await t(55, 66, 'DRV8876: PH=DIR_RIGHT_Q, EN=3V3, nSLEEP=MOTOR_EN, PMODE/IMODE/IPROPI=GND, VREF=3V3.', 9, false, colors.note);
  await t(55, 48, 'Verify U1 PowerPAD/pin map, J1-J4/SW1 copper pads, U2 pinout, and the physical C1 stock fit.', 9, false, colors.warn);

  assertPlacedReferences();
  await eda.sys_ToastMessage.showMessage('R4 Lite schematic created with all five wire-pad footprints bound. Save and run ERC.', 2);
})().catch(async (error) => {
  try {
    await eda.sys_ToastMessage.showMessage(`R4 Lite schematic script stopped: ${error?.message || String(error)}`, 3);
  } catch (toastError) {
    // Preserve the original failure.
  }
  throw error;
});
