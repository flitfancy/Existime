const highCurrentInput = ['+12V_IN', '+12V_SW'];
const highCurrentMotor = ['+12V_PROT', 'MOTOR_A', 'MOTOR_B'];
const logicPower = ['+3V3'];
const sensitiveAsync = [
  'L_RAW', 'R_RAW', 'L_SET_N', 'R_CLR_N',
  'DIR_SET_RC_N', 'DIR_SET_RC_P', 'DIR_SET_N'
];
const bridgeControl = ['DIR_RIGHT_Q', 'DIR_LEFT_NQ', 'MOTOR_EN'];
const chargePump = ['VCP', 'CPH', 'CPL'];
const indicators = ['LED_PWR_A'];

const netIds = Object.freeze({
  GND: 'N01',
  '+12V_IN': 'N02',
  '+12V_SW': 'N03',
  '+12V_PROT': 'N04',
  MOTOR_A: 'N05',
  MOTOR_B: 'N06',
  '+3V3': 'N07',
  L_RAW: 'N08',
  R_RAW: 'N09',
  L_SET_N: 'N10',
  R_CLR_N: 'N11',
  DIR_SET_RC_N: 'N12',
  DIR_SET_RC_P: 'N13',
  DIR_SET_N: 'N14',
  DIR_RIGHT_Q: 'N15',
  DIR_LEFT_NQ: 'N16',
  MOTOR_EN: 'N17',
  VCP: 'N18',
  CPH: 'N19',
  CPL: 'N20',
  LED_PWR_A: 'N21'
});

export default {
  schema: 'skyworks-routing-classes/v1',
  projectId: 'sick-motor-reverser-r4-lite',
  revision: 'R4-L2',
  netIds,
  allNets: [
    ...highCurrentInput,
    ...highCurrentMotor,
    ...logicPower,
    ...sensitiveAsync,
    ...bridgeControl,
    ...chargePump,
    ...indicators,
    'GND'
  ],
  planeNets: ['GND'],
  classes: [
    {
      id: 'HIGH_CURRENT_INPUT',
      priority: 1,
      nets: highCurrentInput,
      widthMil: 60,
      clearanceMil: 12,
      policy: 'Manual wide trace or copper region; prototype starting width.'
    },
    {
      id: 'HIGH_CURRENT_MOTOR',
      priority: 1,
      nets: highCurrentMotor,
      widthMil: 80,
      clearanceMil: 12,
      policy: 'Shortest top copper; fan out immediately after U1/J2 pads.'
    },
    {
      id: 'CHARGE_PUMP',
      priority: 1,
      nets: chargePump,
      widthMil: 12,
      clearanceMil: 8,
      policy: 'Keep C10/C11 loops local to U1; do not route through logic zones.'
    },
    {
      id: 'LOGIC_POWER',
      priority: 2,
      nets: logicPower,
      widthMil: 20,
      clearanceMil: 8,
      policy: 'Short decoupling branches; route after motor and charge-pump loops.'
    },
    {
      id: 'SENSITIVE_ASYNC',
      priority: 2,
      nets: sensitiveAsync,
      widthMil: 10,
      clearanceMil: 10,
      policy: 'Keep away from MOTOR_A/MOTOR_B and do not run parallel to switching copper.'
    },
    {
      id: 'BRIDGE_CONTROL',
      priority: 2,
      nets: bridgeControl,
      widthMil: 10,
      clearanceMil: 8,
      policy: 'Direct routes between U3/U4 and U1 with probe access.'
    },
    {
      id: 'INDICATOR',
      priority: 3,
      nets: indicators,
      widthMil: 8,
      clearanceMil: 8,
      policy: 'Route last and allow detours around control nets.'
    }
  ],
  stages: [
    { order: 1, id: 'POWER_AND_CHARGE_PUMP', classIds: ['HIGH_CURRENT_INPUT', 'HIGH_CURRENT_MOTOR', 'CHARGE_PUMP'] },
    { order: 2, id: 'CONTROL_AND_RAIL', classIds: ['LOGIC_POWER', 'SENSITIVE_ASYNC', 'BRIDGE_CONTROL'] },
    { order: 3, id: 'INDICATOR', classIds: ['INDICATOR'] },
    { order: 4, id: 'GROUND_PLANE', planeNets: ['GND'] }
  ]
};
