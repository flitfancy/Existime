const add = (placements, zone, rows) => {
  for (const [ref, x, y, rotation = 0] of rows) placements.push({ ref, x, y, rotation, zone });
};

// R4-L2 compact placement based on copper envelopes captured from the live PCB.
// Coordinates are mil. The five direct-wire interfaces remain on board edges;
// support parts are packed around their owning IC instead of spread by zones.
export function createPlacements() {
  const placements = [];

  add(placements, 'POWER_ENTRY', [
    ['J1', 340, 970],
    ['SW1', 965, 970],
    ['F1', 1490, 970]
  ]);

  add(placements, 'MOTOR_POWER', [
    ['U5', 1800, 800],
    ['C1', 1790, 990],
    ['U1', 1370, 420],
    ['C4', 1535, 460, 90],
    ['C10', 1330, 670, 90],
    ['C11', 1430, 670, 90],
    ['J2', 1770, 400, 90]
  ]);

  add(placements, 'LOGIC_POWER', [
    ['U2', 450, 700],
    ['C2', 650, 745],
    ['C3', 650, 650],
    ['R1', 820, 745],
    ['D1', 980, 745]
  ]);

  add(placements, 'LIMIT_INPUT', [
    ['J3', 200, 470],
    ['J4', 200, 150],
    ['R5', 460, 520],
    ['C5', 460, 410],
    ['R6', 460, 200],
    ['C6', 460, 90],
    ['U3', 720, 320],
    ['C7', 720, 530]
  ]);

  add(placements, 'LOGIC_INTERLOCK', [
    ['U4', 1080, 320],
    ['C9', 1080, 530],
    ['R7', 1190, 90],
    ['C8', 1030, 90],
    ['D5', 1310, 135, 90],
    ['D6', 1390, 135, 90],
    ['D7', 1470, 135, 90],
    ['R8', 850, 90]
  ]);

  return placements;
}
