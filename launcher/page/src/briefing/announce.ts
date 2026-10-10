// The panel's words for a screen reader: caption, labels, highlighted groups.
//
// Purpose: give, as plain text, what the map shows in pictures.
// Flow: `captionText` turns a caption block into one text without its
// layout marks; `labelTexts` lists the whole texts of the labels that are on;
// `highlightedNames` names the flight groups the markers highlight.
// Invariants: no `$` and no brackets in a spoken text; a heading loses its
// `>`; a label is spoken whole, never as it types out; a group is named once.

import type { Player } from "./core.ts";

const DOLLAR = 0x24;
const HEADING = 0x3e;
const OPEN = 0x5b;
const CLOSE = 0x5d;

/** Return the caption as one text: lines joined by a space, marks removed. */
export function captionText(bytes: readonly number[]): string {
  const lines: number[][] = [[]];
  for (const byte of bytes) {
    if (byte === DOLLAR) lines.push([]);
    else if (byte !== OPEN && byte !== CLOSE)
      lines[lines.length - 1]?.push(byte);
  }
  return lines
    .map((line) =>
      String.fromCharCode(...(line[0] === HEADING ? line.slice(1) : line)),
    )
    .map((line) => line.trim())
    .filter((line) => line !== "")
    .join(" ");
}

/** Return the caption text now shown (slot 1), or "" when there is none. */
export function currentCaption(player: Player): string {
  const slot = player.slots[1];
  return slot.on ? captionText(player.script.captions[slot.block] ?? []) : "";
}

/** Return the whole texts of the labels that are on, in slot order, none empty. */
export function labelTexts(player: Player): string[] {
  const texts: string[] = [];
  for (const label of player.labels) {
    if (!label.on) continue;
    const text = captionText(player.script.labels[label.text] ?? []);
    if (text !== "") texts.push(text);
  }
  return texts;
}

/** Return the names of the flight groups the markers highlight, each once. */
export function highlightedNames(player: Player): string[] {
  const names: string[] = [];
  for (const marker of player.markers) {
    const name = marker.on
      ? player.bundle.groups[marker.group]?.name
      : undefined;
    if (name !== undefined && name !== "" && !names.includes(name))
      names.push(name);
  }
  return names;
}
