// Read a list of draw records written one a line, as the answer sheets write them.
//
// Purpose: turn the text lines of a contact-sheet list back into draw
// records, so the painter can draw a list exactly as the sheets describe it.
// Flow: `readRecords` takes the text and the font and returns the records
// in order; a `text` line is drawn with the font's rules (one glyph record
// for each glyph it draws); `edge` is read as an `outline`.
// Invariants: blank lines are skipped; a color may hold a space (`shade 9`);
// a quoted string holds `\"`, `\\` and `\xHH` escapes and every other
// character as itself; a line of a kind not listed is an error, never
// skipped.

import type { DrawRecord } from "./records.ts";
import type { Font } from "./text.ts";
import { drawBytes } from "./text.ts";

const WHOLE = "(-?\\d+)";
const SHAPE = new RegExp(
  `^(fill|outline|edge) ${WHOLE} ${WHOLE} ${WHOLE} ${WHOLE} (.+)$`,
);
const CLIP = new RegExp(`^clip ${WHOLE} ${WHOLE} ${WHOLE} ${WHOLE}$`);
const VLINE = new RegExp(`^vline ${WHOLE} ${WHOLE} ${WHOLE} (.+)$`);
const HLINE = new RegExp(`^hline ${WHOLE} ${WHOLE} ${WHOLE} (.+)$`);
const ICON = new RegExp(`^icon (\\S+) ${WHOLE} ${WHOLE} ${WHOLE}$`);
const TINT = new RegExp(`^tint (\\S+) ${WHOLE} ${WHOLE} ${WHOLE} (.+)$`);
const TEXT = new RegExp(`^text ${WHOLE} ${WHOLE} (.+?) "(.*)"$`);

/** Return the bytes a quoted string stands for (`\"`, `\\`, `\xHH`, else itself). */
export function unquote(quoted: string): number[] {
  const bytes: number[] = [];
  for (let at = 0; at < quoted.length; at += 1) {
    const ch = quoted.charAt(at);
    if (ch !== "\\") {
      bytes.push(quoted.charCodeAt(at) & 0xff);
    } else if (quoted.charAt(at + 1) === "x") {
      bytes.push(parseInt(quoted.slice(at + 2, at + 4), 16));
      at += 3;
    } else {
      bytes.push(quoted.charCodeAt(at + 1) & 0xff);
      at += 1;
    }
  }
  return bytes;
}

function num(text: string | undefined): number {
  return Number(text);
}

function recordsOf(line: string, font: Font): DrawRecord[] {
  let m: RegExpExecArray | null;
  if ((m = CLIP.exec(line)) !== null) {
    return [
      {
        kind: "clip",
        left: num(m[1]),
        top: num(m[2]),
        right: num(m[3]),
        bottom: num(m[4]),
      },
    ];
  }
  if ((m = VLINE.exec(line)) !== null) {
    return [
      {
        kind: "vline",
        x: num(m[1]),
        top: num(m[2]),
        bottom: num(m[3]),
        color: m[4] ?? "",
      },
    ];
  }
  if ((m = HLINE.exec(line)) !== null) {
    return [
      {
        kind: "hline",
        y: num(m[1]),
        left: num(m[2]),
        right: num(m[3]),
        color: m[4] ?? "",
      },
    ];
  }
  if ((m = ICON.exec(line)) !== null) {
    return [
      {
        kind: "icon",
        sheet: m[1] ?? "",
        index: num(m[2]),
        x: num(m[3]),
        y: num(m[4]),
      },
    ];
  }
  if ((m = TINT.exec(line)) !== null) {
    return [
      {
        kind: "tint",
        sheet: m[1] ?? "",
        index: num(m[2]),
        x: num(m[3]),
        y: num(m[4]),
        color: m[5] ?? "",
      },
    ];
  }
  if ((m = SHAPE.exec(line)) !== null) {
    return [
      {
        kind: m[1] === "fill" ? "fill" : "outline",
        left: num(m[2]),
        top: num(m[3]),
        right: num(m[4]),
        bottom: num(m[5]),
        color: m[6] ?? "",
      },
    ];
  }
  if ((m = TEXT.exec(line)) !== null) {
    const bytes = unquote(m[4] ?? "");
    return drawBytes(font, bytes, m[3] ?? "", num(m[1]), num(m[2])).glyphs;
  }
  throw new Error(`not a record line: ${line}`);
}

/** Return the draw records of the list `text`, in order. Throws on a line that is none. */
export function readRecords(text: string, font: Font): DrawRecord[] {
  const records: DrawRecord[] = [];
  for (const line of text.split("\n")) {
    const trimmed = line.trim();
    if (trimmed !== "") records.push(...recordsOf(trimmed, font));
  }
  return records;
}
