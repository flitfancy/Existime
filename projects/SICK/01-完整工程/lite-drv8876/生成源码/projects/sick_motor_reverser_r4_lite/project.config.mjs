import { range } from '../../core/project-config.mjs';

const expectedRefs = [
  'F1', 'SW1',
  ...range('U', 1, 5),
  ...range('J', 1, 4),
  'D1', ...range('D', 5, 7),
  'R1', ...range('R', 5, 8),
  ...range('C', 1, 11)
];

const padCountFor = (ref) => {
  if (/^[FRCDJ]\d+$/.test(ref) || ref === 'SW1' || ref === 'U5') return 2;
  return ({ U1: 17, U2: 3, U3: 14, U4: 14 })[ref];
};

const padCounts = Object.fromEntries(expectedRefs.map((ref) => [ref, padCountFor(ref)]));

const criticalNets = {
  J1: { 1: '+12V_IN', 2: 'GND' },
  SW1: { 1: '+12V_IN', 2: '+12V_SW' },
  F1: { 1: '+12V_SW', 2: '+12V_PROT' },
  J2: { 1: 'MOTOR_A', 2: 'MOTOR_B' },
  J3: { 1: 'L_RAW', 2: 'GND' },
  J4: { 1: 'R_RAW', 2: 'GND' },
  U1: {
    1: '+3V3', 2: 'DIR_RIGHT_Q', 3: 'MOTOR_EN', 4: '',
    5: '+3V3', 6: 'GND', 7: 'GND', 8: 'MOTOR_A',
    9: 'GND', 10: 'MOTOR_B', 11: '+12V_PROT', 12: 'VCP',
    13: 'CPH', 14: 'CPL', 15: 'GND', 16: 'GND', 17: 'GND'
  },
  U2: { 1: 'GND', 2: '+12V_PROT', 3: '+3V3' },
  U3: {
    1: 'L_RAW', 2: 'L_SET_N', 3: 'R_RAW', 4: 'R_CLR_N',
    5: 'DIR_SET_RC_N', 6: 'DIR_SET_RC_P', 7: 'GND',
    8: 'DIR_SET_N', 9: 'DIR_SET_RC_P', 10: '',
    11: 'GND', 12: '', 13: 'GND', 14: '+3V3'
  },
  U4: {
    1: 'R_CLR_N', 2: 'GND', 3: 'GND', 4: 'DIR_SET_N',
    5: 'DIR_RIGHT_Q', 6: 'DIR_LEFT_NQ', 7: 'GND',
    8: '', 9: '', 10: '+3V3', 11: 'GND', 12: 'GND',
    13: '+3V3', 14: '+3V3'
  },
  U5: { 1: '+12V_PROT', 2: 'GND' },
  D1: { 1: 'GND', 2: 'LED_PWR_A' },
  D5: { 1: 'L_SET_N', 2: 'DIR_SET_RC_N' },
  D6: { 1: 'MOTOR_EN', 2: 'L_SET_N' },
  D7: { 1: 'MOTOR_EN', 2: 'R_CLR_N' },
  R1: { 1: '+12V_PROT', 2: 'LED_PWR_A' },
  R5: { 1: '+3V3', 2: 'L_RAW' },
  R6: { 1: '+3V3', 2: 'R_RAW' },
  R7: { 1: '+3V3', 2: 'DIR_SET_RC_N' },
  R8: { 1: 'MOTOR_EN', 2: 'GND' },
  C1: { 1: '+12V_PROT', 2: 'GND' },
  C2: { 1: '+12V_PROT', 2: 'GND' },
  C3: { 1: '+3V3', 2: 'GND' },
  C4: { 1: '+12V_PROT', 2: 'GND' },
  C5: { 1: 'L_RAW', 2: 'GND' },
  C6: { 1: 'R_RAW', 2: 'GND' },
  C7: { 1: '+3V3', 2: 'GND' },
  C8: { 1: 'DIR_SET_RC_N', 2: 'GND' },
  C9: { 1: '+3V3', 2: 'GND' },
  C10: { 1: 'VCP', 2: '+12V_PROT' },
  C11: { 1: 'CPH', 2: 'CPL' }
};

const wirePolicy = (ref, note) => ({
  ref,
  requiredPads: ['1', '2'],
  forbiddenPads: [],
  forbidDrilledPads: true,
  note
});

export default {
  schema: 'skyworks-hardware-project/v1',
  id: 'sick-motor-reverser-r4-lite',
  name: 'SICK MOTOR REVERSER R4 LITE',
  revision: 'R4-LITE',
  board: {
    widthMm: 50,
    heightMm: 30,
    layers: 2,
    assemblyHotplateMm: [100, 60]
  },
  pcb: {
    expectedComponentCount: expectedRefs.length,
    expectedRefs,
    padCounts,
    criticalNets,
    footprintPolicies: [
      wirePolicy('J1', 'Direct-wire power input: 2P_5X8_P10, no plated holes or paste.'),
      wirePolicy('SW1', 'Off-board power switch: 2P_5X8_P10, no plated holes or paste.'),
      wirePolicy('J2', 'Direct-wire motor output: 2P_5X8_P10, no plated holes or paste.'),
      wirePolicy('J3', 'Left NC limit input: 2P_3X5_P5, no plated holes or paste.'),
      wirePolicy('J4', 'Right NC limit input: 2P_3X5_P5, no plated holes or paste.'),
      {
        ref: 'U1',
        requiredPads: range('', 1, 17),
        forbiddenPads: [],
        note: 'DRV8876PWPR HTSSOP-16 PowerPAD: pad 17 is exposed thermal GND; verify live library mapping.'
      },
      {
        ref: 'U2',
        requiredPads: ['1', '2', '3'],
        forbiddenPads: [],
        note: 'HT7533-1 SOT-89: 1=GND, 2=VIN, 3=VOUT.'
      },
      {
        ref: 'U5',
        requiredPads: ['1', '2'],
        forbiddenPads: [],
        note: 'SMAJ18CA bidirectional TVS across +12V_PROT and GND; polarity independent.'
      },
      {
        ref: 'C1',
        requiredPads: ['1', '2'],
        forbiddenPads: [],
        note: 'User-stock 330uF 16V capacitor, standard 1210/3225 two-pad footprint; 16V rail margin accepted for this prototype.'
      }
    ]
  },
  workflow: {
    captureFile: 'projects/sick_motor_reverser_r4_lite/captures/sick_motor_reverser_r4_lite_capture.json',
    generatedScript: 'projects/sick_motor_reverser_r4_lite/generated/01_prepare_audit_capture.js',
    captureDownloadName: 'sick_motor_reverser_r4_lite_capture.json',
    geometryFile: 'projects/sick_motor_reverser_r4_lite/geometry/footprint_geometry.json',
    layoutRulesFile: 'projects/sick_motor_reverser_r4_lite/layout.rules.mjs',
    placementModule: 'projects/sick_motor_reverser_r4_lite/placement.mjs',
    routingClassesFile: 'projects/sick_motor_reverser_r4_lite/routing.classes.mjs',
    routingPlanFile: 'projects/sick_motor_reverser_r4_lite/ROUTING_PLAN_R4_LITE.md',
    logicNetMapFile: 'projects/sick_motor_reverser_r4_lite/LOGIC_NET_MAP_R4_LITE.md',
    generatedPlacementScript: 'projects/sick_motor_reverser_r4_lite/generated/02_apply_placement.js',
    generatedPlacementVerification: 'projects/sick_motor_reverser_r4_lite/generated/03_verify_placement.js'
  },
  datasheets: {
    motorDriver: 'https://www.ti.com/lit/ds/symlink/drv8876.pdf',
    regulator: 'https://www.holtek.com/webapi/116711/HT75xx-1v281.pdf'
  }
};
