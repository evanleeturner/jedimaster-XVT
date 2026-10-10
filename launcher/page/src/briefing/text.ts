// Measure and draw strings in font 10, by the game's rules.
//
// Purpose: turn a string of bytes into glyph draw records and measure its
// width, with the game's color bytes.
// Flow: `toBytes` makes bytes from a text; `measure` sums glyph widths;
// `drawBytes` walks the bytes and emits one glyph record per drawn glyph.
// Invariants: bytes 1 to 6 are colors and draw nothing (byte 1 returns to the
// color the draw began with, byte b from 2 to 6 is text color code b - 1); a
// glyph of width 0 draws nothing; a draw stops at its first byte 0 or when
// the pen stands at x 640 or more before a byte.

import type { FontData } from "../generated/briefing.ts";
import type { GlyphRecord } from "./records.ts";

export type Font = Pick<FontData, "widths" | "spacing" | "height">;

export const RESET_BYTE = 1;
export const LAST_COLOR_BYTE = 6;
const STOP_X = 640;

/** Return the bytes of `text`, one per character (the low 8 bits). */
export function toBytes(text: string): number[] {
  return Array.from(text, (_, i) => text.charCodeAt(i) & 0xff);
}

function widthOf(font: Font, byte: number): number {
  return font.widths[byte] ?? 0;
}

function untilZero(bytes: readonly number[]): readonly number[] {
  const end = bytes.indexOf(0);
  return end < 0 ? bytes : bytes.slice(0, end);
}

/**
 * Return the width the game measures for `bytes`: the sum of width plus
 * spacing over the bytes above 6 before the first byte 0, less one spacing.
 */
export function measure(font: Font, bytes: readonly number[]): number {
  let total = 0;
  for (const byte of untilZero(bytes)) {
    if (byte > LAST_COLOR_BYTE) total += widthOf(font, byte) + font.spacing;
  }
  return total - font.spacing;
}

export interface Drawn {
  readonly glyphs: GlyphRecord[];
  readonly pen: number;
}

/**
 * Draw `bytes` from (x, y) beginning in `color`; return the glyph records
 * and where the pen ended.
 */
export function drawBytes(
  font: Font,
  bytes: readonly number[],
  color: string,
  x: number,
  y: number,
): Drawn {
  const glyphs: GlyphRecord[] = [];
  let pen = x;
  let current = color;
  for (const byte of untilZero(bytes)) {
    if (pen >= STOP_X) break;
    if (byte === RESET_BYTE) {
      current = color;
    } else if (byte <= LAST_COLOR_BYTE) {
      current = `code${String(byte - 1)}`;
    } else {
      const width = widthOf(font, byte);
      if (width > 0)
        glyphs.push({ kind: "glyph", code: byte, x: pen, y, color: current });
      pen += width + font.spacing;
    }
  }
  return { glyphs, pen };
}
