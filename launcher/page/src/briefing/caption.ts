// Wrap a caption into lines and draw them, as the setup screen does.
//
// Purpose: split one caption block into 360-pixel lines and give the glyph
// records of each line: white, or yellow and centered for a heading.
// Flow: `wrapCaption` walks the bytes into lines; `drawCaption` draws them,
// the first line's glyph tops at y 208 and 14 pixels between lines.
// Invariants: `[` is color byte 2 and `]` color byte 1 before anything is
// measured; a `$` ends a line and is not drawn; a line starting with `>` is a
// heading drawn without it.

import type { GlyphRecord } from "./records.ts";
import type { Font } from "./text.ts";
import { drawBytes, measure } from "./text.ts";

export const PANEL_WIDTH = 360;
export const CAPTION_TOP = 208;
const DOLLAR = 0x24;
const SPACE = 0x20;
const HEADING = 0x3e;
const OPEN = 0x5b;
const CLOSE = 0x5d;
const WHITESPACE = new Set([0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x20]);

export interface CaptionLine {
  readonly bytes: readonly number[];
  readonly inside: boolean;
}

/** Return the caption's bytes with `[` as byte 2 and `]` as byte 1. */
export function bracketBytes(bytes: readonly number[]): number[] {
  return bytes.map((b) => (b === OPEN ? 2 : b === CLOSE ? 1 : b));
}

function isInside(before: readonly number[]): boolean {
  let depth = 0;
  for (const byte of before) {
    if (byte === 2) depth += 1;
    if (byte === 1) depth -= 1;
  }
  return depth > 0;
}

/**
 * Return the lines `text` wraps into at 360 pixels.
 *
 * A word that makes a line reach 360 pixels ends the line at the last space;
 * when the line has no space, the line ends after that word.
 */
export function wrapCaption(
  font: Font,
  text: readonly number[],
): CaptionLine[] {
  const bytes = bracketBytes(text);
  const lines: CaptionLine[] = [];
  let pos = 0;
  while (pos < bytes.length) {
    const start = pos;
    const line: number[] = [];
    let lastSpace = -1;
    let ended = false;
    while (!ended) {
      const byte = bytes[pos];
      if (byte === undefined) break;
      if (byte === DOLLAR) {
        pos += 1;
        break;
      }
      if (WHITESPACE.has(byte)) {
        line.push(byte);
        if (byte === SPACE) lastSpace = line.length - 1;
        pos += 1;
      } else {
        while (pos < bytes.length) {
          const next = bytes[pos];
          if (next === undefined || next === DOLLAR || WHITESPACE.has(next))
            break;
          line.push(next);
          pos += 1;
        }
      }
      if (measure(font, line) >= PANEL_WIDTH) {
        if (lastSpace >= 0) {
          line.length = lastSpace + 1;
          pos = start + lastSpace + 1;
        }
        ended = true;
      }
    }
    lines.push({ bytes: line, inside: isInside(bytes.slice(0, start)) });
  }
  return lines;
}

/** Return the glyph records of the whole caption `text`. */
export function drawCaption(
  font: Font,
  text: readonly number[],
): GlyphRecord[] {
  const records: GlyphRecord[] = [];
  wrapCaption(font, text).forEach((line, row) => {
    const y = CAPTION_TOP + row * font.height;
    const heading = line.bytes[0] === HEADING;
    const shown = heading ? line.bytes.slice(1) : line.bytes;
    const bytes = line.inside ? [2, ...shown] : shown;
    if (heading) {
      const x = PANEL_WIDTH / 2 - (measure(font, shown) >> 1);
      records.push(...drawBytes(font, bytes, "yellow", x, y + 1).glyphs);
    } else {
      records.push(...drawBytes(font, bytes, "white", 0, y).glyphs);
    }
  });
  return records;
}
