// The draw records of one frame of the briefing panel.
//
// Purpose: describe, without any picture, what a frame draws, so the core
// can be compared with the answer sheets and the painter can draw it.
// Flow: `draw.ts` builds a list of `DrawRecord`; `sheet.ts` writes them as
// text lines; `paint.ts` draws them on a canvas.
// Invariants: coordinates are panel pixels (0,0 to 359,239); a rectangle
// includes both edges; a glyph is one drawn character.

export type ColorName = string;

export interface ClipRecord {
  readonly kind: "clip";
  readonly left: number;
  readonly top: number;
  readonly right: number;
  readonly bottom: number;
}

export interface VlineRecord {
  readonly kind: "vline";
  readonly x: number;
  readonly top: number;
  readonly bottom: number;
  readonly color: ColorName;
}

export interface HlineRecord {
  readonly kind: "hline";
  readonly y: number;
  readonly left: number;
  readonly right: number;
  readonly color: ColorName;
}

export interface IconRecord {
  readonly kind: "icon";
  readonly sheet: string;
  readonly index: number;
  readonly x: number;
  readonly y: number;
}

export interface TintRecord {
  readonly kind: "tint";
  readonly sheet: string;
  readonly index: number;
  readonly x: number;
  readonly y: number;
  readonly color: ColorName;
}

export interface RectRecord {
  readonly kind: "fill" | "outline";
  readonly left: number;
  readonly top: number;
  readonly right: number;
  readonly bottom: number;
  readonly color: ColorName;
}

export interface GlyphRecord {
  readonly kind: "glyph";
  readonly code: number;
  readonly x: number;
  readonly y: number;
  readonly color: ColorName;
}

export type DrawRecord =
  | ClipRecord
  | VlineRecord
  | HlineRecord
  | IconRecord
  | TintRecord
  | RectRecord
  | GlyphRecord;
