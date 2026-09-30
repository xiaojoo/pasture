// Which boards are in charge of their subsystem right now.
//
// A leaf, like state/power.js: the dock is the only writer because the dock is the
// thing watching the simulators, and the readers are the subsystems themselves.
// They need it because "a frame has been parsed" and "the board is running" are not
// the same moment -- the first LIGHT line prints a second after the compile finishes,
// and until then nothing on the ranch should move on the board's behalf.
export const boardUp = {
  light: false, drone: false, water: false, power: false, fire: false, show: false,
};

export function setBoardUp(id, up) {
  if (id in boardUp) boardUp[id] = !!up;
}

export function boardRunning(id) {
  return !!boardUp[id];
}
