// Where the 360 x 240 panel goes on the page, for each art scaling setting.
//
// Purpose: choose the size the panel is shown at and how its pixels are
// scaled, from the room the page has.
// Flow: `layoutFor(setting, width, height)` returns the canvas's backing
// scale (`factor`), its shown size and whether the browser smooths it.
// Invariants: `whole_pixels` shows the largest whole multiple that fits (at
// least 1), sharp; `engine_fit` shows the largest size of the same shape
// that fits, smoothed; `sharp_bilinear` draws the largest whole multiple
// that fits (at least 1) and shows the largest size that fits, smoothed a
// little; the shape 3:2 is always kept.

export const PANEL_W = 360;
export const PANEL_H = 240;

export type Scaling = "whole_pixels" | "engine_fit" | "sharp_bilinear";

export interface PanelLayout {
  readonly factor: number;
  readonly width: number;
  readonly height: number;
  readonly smooth: boolean;
}

/** Return the layout of the panel in a box of `width` x `height` CSS pixels. */
export function layoutFor(
  setting: Scaling,
  width: number,
  height: number,
): PanelLayout {
  const fit = Math.max(0.1, Math.min(width / PANEL_W, height / PANEL_H));
  const whole = Math.max(1, Math.floor(fit));
  if (setting === "whole_pixels") {
    return {
      factor: whole,
      width: PANEL_W * whole,
      height: PANEL_H * whole,
      smooth: false,
    };
  }
  const shown = {
    width: Math.floor(PANEL_W * fit),
    height: Math.floor(PANEL_H * fit),
  };
  if (setting === "engine_fit") return { factor: 1, ...shown, smooth: true };
  return { factor: whole, ...shown, smooth: true };
}
