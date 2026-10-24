const refs = (...values) => values.flat();

export default {
  schema: 'skyworks-layout-rules/v1',
  projectId: 'sick-motor-reverser-r4-lite',
  revision: 'R4-L2',
  boardMil: {
    width: 1969,
    height: 1181,
    edgeClearance: 40
  },
  zones: [
    {
      id: 'POWER_ENTRY',
      areaMil: { x0: 0, y0: 790, x1: 1680, y1: 1181 },
      refs: refs('J1', 'SW1', 'F1'),
      intent: 'Short 12 V edge chain from input pads through switch and fuse.'
    },
    {
      id: 'MOTOR_POWER',
      areaMil: { x0: 1240, y0: 0, x1: 1969, y1: 1181 },
      refs: refs('U1', 'U5', 'J2', 'C1', 'C4', 'C10', 'C11'),
      intent: 'DRV8876, PowerPAD cooling, TVS, local capacitors and motor pads.'
    },
    {
      id: 'LOGIC_POWER',
      areaMil: { x0: 300, y0: 590, x1: 1070, y1: 790 },
      refs: refs('U2', 'C2', 'C3', 'R1', 'D1'),
      intent: 'LDO and the only indicator, separated from motor switching copper.'
    },
    {
      id: 'LIMIT_INPUT',
      areaMil: { x0: 0, y0: 0, x1: 900, y1: 590 },
      refs: refs('J3', 'J4', 'U3', 'R5', 'R6', 'C5', 'C6', 'C7'),
      intent: 'NC limit pads, debounce and Schmitt cleanup; exclude motor copper.'
    },
    {
      id: 'LOGIC_INTERLOCK',
      areaMil: { x0: 760, y0: 0, x1: 1520, y1: 590 },
      refs: refs('U4', 'R7', 'R8', 'C8', 'C9', 'D5', 'D6', 'D7'),
      intent: 'Direction latch, squared preset path and dual-open diode interlock.'
    }
  ],
  corridors: [],
  clearancesMil: {
    assembly: 30,
    copper: 8
  },
  protectedAreas: [],
  pairRules: [
    {
      id: 'MOTOR_TO_RAW_LIMIT_DISTANCE',
      leftRefs: refs('U1', 'J2', 'C1', 'C4'),
      rightRefs: refs('J3', 'J4', 'R5', 'R6', 'C5', 'C6', 'U3'),
      minimumMil: 250
    }
  ]
};
