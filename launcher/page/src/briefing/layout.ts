// Where a picture goes on the page (the 360 x 240 panel, or any size), for each
// art scaling setting.
//
// Purpose: choose the size the panel is shown at and how its pixels are
// scaled, from the room the page has.
// Flow: `layoutIn` does the sums for any size; `layoutFor(setting, width, height)`
// is the panel's case. Both return the canvas's backing
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

/**
 * Return the layout of a picture `baseW` x `baseH` in a box of `width` x
 * `height` CSS pixels, by the rules above (the shape is kept, and the
 * multiple is at least 1 even when the box is smaller).
 */
export function layoutIn(
  setting: Scaling,
  baseW: number,
  baseH: number,
  width: number,
  height: number,
): PanelLayout {
  const fit = Math.max(0.1, Math.min(width / baseW, height / baseH));
  const whole = Math.max(1, Math.floor(fit));
  if (setting === "whole_pixels") {
    return {
      factor: whole,
      width: baseW * whole,
      height: baseH * whole,
      smooth: false,
    };
  }
  const shown = {
    width: Math.floor(baseW * fit),
    height: Math.floor(baseH * fit),
  };
  if (setting === "engine_fit") return { factor: 1, ...shown, smooth: true };
  return { factor: whole, ...shown, smooth: true };
}

/** Return the layout of the panel in a box of `width` x `height` CSS pixels. */
export function layoutFor(
  setting: Scaling,
  width: number,
  height: number,
): PanelLayout {
  return layoutIn(setting, PANEL_W, PANEL_H, width, height);
}
