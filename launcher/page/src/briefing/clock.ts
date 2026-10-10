// The setup screen's clock: one frame every 41 ms of real time.
//
// Purpose: turn the browser's animation timestamps into whole frames to run.
// Flow: `advance(now)` adds the time since the last call and returns how
// many frames are due; `reset()` forgets the time (used when the tab comes
// back, so it never catches up on what it missed).
// Invariants: one frame is 41 ms (1000 / 24 with the fraction dropped); at
// most `MAX_CATCH_UP` frames are returned by one call and the rest of the
// time is dropped, so a slow tab runs slower and never in a burst.

import { FRAME_MS } from "./core.ts";

export const MAX_CATCH_UP = 4;

export class FrameClock {
  #last: number | null = null;
  #owed = 0;

  /** Return how many frames are due at `now` (milliseconds); the first call returns 0. */
  advance(now: number): number {
    if (this.#last === null) {
      this.#last = now;
      return 0;
    }
    this.#owed += Math.max(0, now - this.#last);
    this.#last = now;
    const due = Math.floor(this.#owed / FRAME_MS);
    this.#owed -= due * FRAME_MS;
    if (due > MAX_CATCH_UP) {
      this.#owed = 0;
      return MAX_CATCH_UP;
    }
    return due;
  }

  /** Forget the elapsed time: the next call to `advance` returns 0. */
  reset(): void {
    this.#last = null;
    this.#owed = 0;
  }
}
