// Draw one frame of the briefing panel as records.
//
// Purpose: from a player's state, list in order everything the setup screen
// draws in its 360 x 240 panel: the caption, the map grid, the markers, the
// labels, the icons and the page line.
// Flow: `drawFrame` counts the page while drawing the caption, then calls
// the small drawers below in the game's order.
// Invariants: records are panel pixels; the map area is (0,0) to (360,208);
// shades are `shade N` (0 to 39); a frame draws the same records for the
// same state, except that the page number is counted once per new caption.

import type { BriefingBundle, GroupData } from "../generated/briefing.ts";
import { CAPTION_TOP, drawCaption } from "./caption.ts";
import type { Label, Marker, Player } from "./core.ts";
import { cut16 } from "./core.ts";
import type { DrawRecord } from "./records.ts";
import { drawBytes, measure, toBytes } from "./text.ts";

export const MAP_WIDTH = 360;
export const MAP_HEIGHT = 208;
const CENTER_X = 180;
const CENTER_Y = 104;
const GRID = 256;
const PAGE_X = 300;
const PAGE_Y = 194;
const SHADE_BASE_BY_IFF: Readonly<Record<number, number>> = {
  1: 8,
  2: 24,
  3: 16,
  4: 8,
  5: 32,
};
const SHEET_BY_IFF: Readonly<Record<number, number>> = {
  0: 0,
  1: 1,
  2: 2,
  3: 3,
  4: 1,
  5: 4,
};

/** Return the record name of shade `n`; the 40 shades are 0 to 39, any other is black. */
function shade(n: number): string {
  return n >= 0 && n < 40 ? `shade ${String(n)}` : "x0";
}

/** Return the map point (mx, my) as panel pixels, cut to 16 bits. */
export function project(
  player: Player,
  mx: number,
  my: number,
): { x: number; y: number } {
  return {
    x: cut16(Math.trunc((player.sx * (mx - player.cx)) / GRID) + CENTER_X),
    y: cut16(Math.trunc((player.sy * (my - player.cy)) / GRID) + CENTER_Y),
  };
}

interface Lines {
  start: number;
  phase: number;
}

function firstLine(center: number, zoom: number, half: number): Lines {
  let phase = Math.trunc(center / GRID);
  if (center > 0 && (center & 255) !== 0) phase += 1;
  let start = half + ((zoom * (-center & 255)) >> 8);
  while (start > 0 && zoom > 0) {
    start -= zoom;
    phase -= 1;
  }
  return { start, phase };
}

function lineSteps(first: Lines, zoom: number, limit: number): Lines[] {
  const steps: Lines[] = [];
  if (zoom <= 0) return steps;
  for (
    let at = first.start, phase = first.phase;
    at < limit;
    at += zoom, phase += 1
  ) {
    steps.push({ start: at, phase });
  }
  return steps;
}

function drawGrid(player: Player): DrawRecord[] {
  const records: DrawRecord[] = [];
  const across = lineSteps(
    firstLine(player.cx, player.sx, CENTER_X),
    player.sx,
    MAP_WIDTH,
  );
  const down = lineSteps(
    firstLine(player.cy, player.sy, CENTER_Y),
    player.sy,
    MAP_HEIGHT,
  );
  const wanted = (phase: number, major: boolean): boolean => {
    if (major) return (phase & 3) === 0;
    return player.sx >= 32 ? (phase & 3) !== 0 : (phase & 3) === 2;
  };
  for (const major of player.sx >= 16 ? [false, true] : [true]) {
    const color = major ? "major" : "minor";
    for (const { start, phase } of across) {
      if (wanted(phase, major)) {
        records.push({
          kind: "vline",
          x: start,
          top: 0,
          bottom: MAP_HEIGHT,
          color,
        });
      }
    }
    for (const { start, phase } of down) {
      if (wanted(phase, major)) {
        records.push({
          kind: "hline",
          y: start,
          left: 0,
          right: MAP_WIDTH,
          color,
        });
      }
    }
  }
  return records;
}

interface Place {
  readonly group: GroupData;
  readonly box: number;
  readonly left: number;
  readonly top: number;
  readonly width: number;
  readonly height: number;
  readonly enabled: boolean;
}

function placeOf(player: Player, group: GroupData | undefined): Place | null {
  if (group === undefined || group.craft_type < 0) return null;
  const box = player.bundle.craft_boxes[group.craft_type];
  const shape = box === undefined ? undefined : player.bundle.boxes[box];
  const point = group.points[player.script.index];
  if (box === undefined || shape === undefined || point === undefined)
    return null;
  const width = shape.right - shape.left + 1;
  const height = shape.bottom - shape.top + 1;
  const at = project(player, point.x, point.y);
  return {
    group,
    box,
    left: at.x - (width >> 1),
    top: at.y - (height >> 1),
    width,
    height,
    enabled: point.enabled,
  };
}

const NO_GROUP: GroupData = {
  number: 0,
  name: "",
  craft_type: 0,
  iff: 0,
  team: 0,
  player_number: 0,
  points: Array.from({ length: 8 }, () => ({ x: 0, y: 0, enabled: false })),
};

function highlight(player: Player, marker: Marker): DrawRecord[] {
  const place = placeOf(player, player.bundle.groups[marker.group] ?? NO_GROUP);
  if (place === null) return [];
  const base = SHADE_BASE_BY_IFF[place.group.iff] ?? 0;
  const age = marker.age;
  const box = {
    left: place.left,
    top: place.top,
    right: place.left + place.width - 1,
    bottom: place.top + place.height - 1,
  };
  const rect = (
    kind: "fill" | "outline",
    grow: number,
    color: number,
  ): DrawRecord => ({
    kind,
    left: box.left - grow,
    top: box.top - grow,
    right: box.right + grow,
    bottom: box.bottom + grow,
    color: shade(color),
  });
  if (age >= 12)
    return [rect("fill", 2, base + 2), rect("outline", 2, base + 6)];
  const records: DrawRecord[] = [];
  let level: number;
  let offset: number;
  let count: number;
  if (age < 4) {
    level = base + 7 - 2 * age;
    offset = 16;
    count = age + 1;
  } else {
    level = base + 1;
    offset = 2 * (11 - age);
    count = age < 8 ? 4 : 12 - age;
    if (age >= 8) {
      records.push(rect("fill", -(11 - age), base + 2));
      records.push(rect("outline", -(11 - age), base + age - 6));
    }
  }
  for (let n = 0; n < count; n += 1) {
    for (const [dx, dy] of [
      [-1, -1],
      [1, -1],
      [-1, 1],
      [1, 1],
    ] as const) {
      records.push({
        kind: "tint",
        sheet: player.bundle.grey_sheet?.name ?? "greyicon",
        index: place.box,
        x: place.left + dx * offset,
        y: place.top + dy * offset,
        color: shade(level),
      });
    }
    level += 2;
    offset -= 2;
  }
  return records;
}

function bracketed(text: readonly number[]): number[] {
  return text.map((b) => (b === 0x5b ? 2 : b === 0x5d ? 1 : b));
}

function drawLabel(player: Player, label: Label): DrawRecord[] {
  const raw = player.script.labels[label.text] ?? [];
  const bytes = bracketed(raw);
  const length = bytes.length;
  const at = project(player, label.x, label.y);
  const base = 8 * label.row;
  const n = 2 * label.age;
  const font = player.bundle.font;
  const out: DrawRecord[] = [];
  if (n < length + 2) {
    const shown = Math.min(n, length);
    let level = base + (n === 1 ? 4 : n === 2 ? 2 : 0);
    for (
      let size = shown;
      size > 0 && level <= base + 6;
      size -= 1, level += 1
    ) {
      out.push(
        ...drawBytes(font, bytes.slice(0, size), shade(level), at.x, at.y)
          .glyphs,
      );
    }
    if (length > n) {
      const width = measure(font, bytes.slice(0, shown));
      out.push({
        kind: "fill",
        left: at.x + width + 2,
        top: at.y,
        right: at.x + width + 8,
        bottom: at.y + 6,
        color: shade(base + 7),
      });
    }
  } else {
    const level =
      n === length + 2 ? 7 : n === length + 3 ? 6 : n === length + 4 ? 5 : 4;
    out.push(...drawBytes(font, bytes, shade(base + level), at.x, at.y).glyphs);
  }
  return out;
}

function drawIcons(player: Player): DrawRecord[] {
  const records: DrawRecord[] = [];
  let counted = 0;
  for (const group of player.bundle.groups) {
    const place = placeOf(player, group);
    if (place?.enabled !== true) continue;
    const mine = group.player_number !== 0 && group.team === player.team;
    if (mine) counted += 1;
    const sheet = SHEET_BY_IFF[group.iff] ?? 0;
    records.push({
      kind: "icon",
      sheet: `mapicon${String(sheet)}`,
      index: place.box,
      x: place.left,
      y: place.top,
    });
    if (mine) {
      const text = toBytes(String(counted));
      const x = place.left + place.width;
      const y = place.top + place.height;
      records.push(
        ...drawBytes(player.bundle.font, text, "white", x, y).glyphs,
      );
    }
  }
  return records;
}

/** Return the page word and number, e.g. the front string and the page. */
export function pageLine(bundle: BriefingBundle, page: number): number[] {
  return toBytes(`${bundle.front_string} ${String(page)}`);
}

/**
 * Draw one frame: return its records in order. Counts the page when the
 * caption's block is new (the only change this makes to the player).
 */
export function drawFrame(player: Player): DrawRecord[] {
  const records: DrawRecord[] = [];
  const font = player.bundle.font;
  const slot = player.slots[1];
  if (slot.on) {
    records.push(
      ...drawCaption(font, player.script.captions[slot.block] ?? []),
    );
    if (slot.block !== player.last) {
      player.page += 1;
      player.last = slot.block;
    }
  }
  records.push({
    kind: "clip",
    left: 0,
    top: 0,
    right: MAP_WIDTH,
    bottom: MAP_HEIGHT,
  });
  records.push({
    kind: "clip",
    left: 0,
    top: 0,
    right: MAP_WIDTH - 1,
    bottom: MAP_HEIGHT,
  });
  records.push(...drawGrid(player));
  for (const marker of player.markers) {
    if (marker.on) records.push(...highlight(player, marker));
  }
  for (const label of player.labels) {
    if (label.on) records.push(...drawLabel(player, label));
  }
  records.push(...drawIcons(player));
  records.push(
    ...drawBytes(
      font,
      pageLine(player.bundle, player.page),
      "white",
      PAGE_X,
      PAGE_Y,
    ).glyphs,
  );
  return records;
}

export { CAPTION_TOP };
