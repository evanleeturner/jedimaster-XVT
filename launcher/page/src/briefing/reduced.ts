// The player as the map shows it with reduced motion.
//
// Purpose: give the painter a still map: the view at its targets, labels
// whole and markers as their final box, while the core keeps running
// unchanged.
// Flow: `settled(player)` returns a copy of the player to draw from; the
// real player is never changed by it.
// Invariants: the copy shares the script and bundle; its page counters equal
// the player's, so drawing it never counts a page.

import type { Player } from "./core.ts";

const SETTLED_AGE = 1000;

/** Return a copy of `player` with the view at its targets and every marker and label fully aged. */
export function settled(player: Player): Player {
  return {
    ...player,
    cx: player.tcx,
    cy: player.tcy,
    sx: player.tsx,
    sy: player.tsy,
    slots: [{ ...player.slots[0] }, { ...player.slots[1] }],
    markers: player.markers.map((m) => ({
      ...m,
      age: m.on ? SETTLED_AGE : m.age,
    })),
    labels: player.labels.map((l) => ({
      ...l,
      age: l.on ? SETTLED_AGE : l.age,
    })),
    sounds: [],
  };
}
