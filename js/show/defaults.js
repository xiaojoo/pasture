// The show a fresh operator gets, and the numbers the choreography page reads
// before an aircraft reports its own pack.
//
// Exported rather than written twice: the choreography dock and the management
// console both start from this object, and the console's saved shows overwrite it
// on purpose. A second copy would be a second default to keep in sync.
export const DEFAULT_PLAN = {
  drones: 24,
  separation: 2.0,
  maxSpeed: 6.0,
  geofence: 120,
  rtlAlt: 35,
  acts: [
    { shape: 0, scale: 18, alt: 40, hold: 12, move: 12, colour: 0x00e0a0 },
    { shape: 1, scale: 18, alt: 40, hold: 12, move: 14, colour: 0xffd000 },
  ],
};

export const DEFAULT_BATT = 100;
export const DEFAULT_WIND = 0;

export const clonePlan = plan => JSON.parse(JSON.stringify(plan));
