// Where the 640 x 480 menu frame goes on the page, for each art scaling.
//
// Purpose: choose the size the frame is shown at and how its pixels are
// scaled, from the window; below 640 x 480 there is no frame at all.
// Flow: `frameLayout(setting, width, height)` returns null when the window
// is smaller than the frame, else the panel layout rules at the frame's
// size plus `scale`, the shown width over 640.
// Invariants: the rules are `layoutIn`'s (whole multiple sharp, fitted
// smoothed, whole multiple drawn and fitted shown); the frame never
// shows smaller than 1x.

import type { PanelLayout, Scaling } from "../briefing/layout.ts";
import { layoutIn } from "../briefing/layout.ts";

export const FRAME_W = 640;
export const FRAME_H = 480;

export interface FrameLayout extends PanelLayout {
  /** The shown width over 640: what the page's `zoom` is set to. */
  readonly scale: number;
}

/** Return the frame's layout in `width` x `height` CSS pixels, or null if too small. */
export function frameLayout(
  setting: Scaling,
  width: number,
  height: number,
): FrameLayout | null {
  if (width < FRAME_W || height < FRAME_H) return null;
  const layout = layoutIn(setting, FRAME_W, FRAME_H, width, height);
  return { ...layout, scale: layout.width / FRAME_W };
}
