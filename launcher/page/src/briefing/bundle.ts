// Check a briefing bundle that arrived from the launcher.
//
// Purpose: accept a bundle only when every field has the shape and the
// length the player relies on, and say why when it does not.
// Flow: `decodeBundle` takes a parsed JSON value and returns the typed
// bundle or a reason; the small checkers below walk each part.
// Invariants: the check is closed (no extra fields), whole numbers only,
// text characters are codes 0 to 255, and the fixed lengths of the bundle
// (10 teams, 32 texts, 8 points, 70 boxes, 106 crafts, 256 IFFs and glyph
// widths) hold.

import type {
  BoxData,
  BriefingBundle,
  BriefingData,
  EventData,
  FontData,
  GroupData,
  IconSheetData,
  PointData,
  SoundData,
  TeamData,
} from "../generated/briefing.ts";

export type Checked<T> = { readonly value: T } | { readonly reason: string };

type Fields = Record<string, unknown>;

const TEAMS = 10;
const TEXTS = 32;
const POINTS = 8;
const BOXES = 70;
const CRAFTS = 106;
const IFFS = 256;
const GLYPHS = 256;
const BRIEFINGS = 8;
const TEXT_COLORS = 5;

function fail<T>(reason: string): Checked<T> {
  return { reason };
}

function isRecord(value: unknown): value is Fields {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function exactly(value: Fields, names: readonly string[]): boolean {
  const keys = Object.keys(value);
  return keys.length === names.length && names.every((name) => name in value);
}

function whole(value: unknown): value is number {
  return typeof value === "number" && Number.isSafeInteger(value);
}

function wholes(value: unknown, count: number | null): value is number[] {
  return (
    Array.isArray(value) &&
    (count === null || value.length === count) &&
    value.every(whole)
  );
}

function isByteText(item: unknown): item is string {
  if (typeof item !== "string") return false;
  for (let i = 0; i < item.length; i += 1) {
    if (item.charCodeAt(i) > 0xff) return false;
  }
  return true;
}

function texts(value: unknown, count: number): value is string[] {
  return (
    Array.isArray(value) && value.length === count && value.every(isByteText)
  );
}

function listOf<T>(
  value: unknown,
  count: number | null,
  check: (item: unknown) => Checked<T>,
  what: string,
): Checked<T[]> {
  if (!Array.isArray(value) || (count !== null && value.length !== count)) {
    return fail(
      `${what} must be a list${count === null ? "" : ` of ${String(count)}`}`,
    );
  }
  const kept: T[] = [];
  for (const item of value as unknown[]) {
    const one = check(item);
    if ("reason" in one) return fail(one.reason);
    kept.push(one.value);
  }
  return { value: kept };
}

function teamOf(value: unknown): Checked<TeamData> {
  if (!isRecord(value) || !exactly(value, ["team", "name", "briefing"])) {
    return fail("a team has the wrong fields");
  }
  const { team, name, briefing } = value;
  if (
    !whole(team) ||
    team < 0 ||
    team >= TEAMS ||
    typeof name !== "string" ||
    !(
      briefing === null ||
      (whole(briefing) && briefing >= 0 && briefing < BRIEFINGS)
    )
  ) {
    return fail("a team has a value of the wrong type");
  }
  return { value: { team, name, briefing } };
}

function eventOf(value: unknown): Checked<EventData> {
  if (!isRecord(value) || !exactly(value, ["time", "type", "variables"])) {
    return fail("an event has the wrong fields");
  }
  const { time, type, variables } = value;
  if (!whole(time) || !whole(type) || !wholes(variables, null)) {
    return fail("an event has a value of the wrong type");
  }
  return { value: { time, type, variables } };
}

function briefingOf(value: unknown): Checked<BriefingData> {
  const names = ["index", "running_time", "events", "labels", "captions"];
  if (!isRecord(value) || !exactly(value, names)) {
    return fail("a briefing has the wrong fields");
  }
  const { index, running_time, events, labels, captions } = value;
  const kept = listOf(events, null, eventOf, "events");
  if ("reason" in kept) return fail(kept.reason);
  if (
    !whole(index) ||
    index < 0 ||
    index >= BRIEFINGS ||
    !whole(running_time) ||
    !texts(labels, TEXTS) ||
    !texts(captions, TEXTS)
  ) {
    return fail("a briefing has a value of the wrong type");
  }
  return {
    value: { index, running_time, events: kept.value, labels, captions },
  };
}

function pointOf(value: unknown): Checked<PointData> {
  if (!isRecord(value) || !exactly(value, ["x", "y", "enabled"])) {
    return fail("a point has the wrong fields");
  }
  const { x, y, enabled } = value;
  if (!whole(x) || !whole(y) || typeof enabled !== "boolean") {
    return fail("a point has a value of the wrong type");
  }
  return { value: { x, y, enabled } };
}

function groupOf(value: unknown): Checked<GroupData> {
  const names = [
    "number",
    "name",
    "craft_type",
    "iff",
    "team",
    "player_number",
    "points",
  ];
  if (!isRecord(value) || !exactly(value, names)) {
    return fail("a flight group has the wrong fields");
  }
  const { number, name, craft_type, iff, team, player_number, points } = value;
  const kept = listOf(points, POINTS, pointOf, "points");
  if ("reason" in kept) return fail(kept.reason);
  if (
    !whole(number) ||
    typeof name !== "string" ||
    !whole(craft_type) ||
    !whole(iff) ||
    !whole(team) ||
    !whole(player_number)
  ) {
    return fail("a flight group has a value of the wrong type");
  }
  return {
    value: {
      number,
      name,
      craft_type,
      iff,
      team,
      player_number,
      points: kept.value,
    },
  };
}

function boxOf(value: unknown): Checked<BoxData> {
  if (!isRecord(value) || !exactly(value, ["left", "top", "right", "bottom"])) {
    return fail("a box has the wrong fields");
  }
  const { left, top, right, bottom } = value;
  if (!whole(left) || !whole(top) || !whole(right) || !whole(bottom)) {
    return fail("a box has a value of the wrong type");
  }
  return { value: { left, top, right, bottom } };
}

function sheetOf(value: unknown): Checked<IconSheetData> {
  if (!isRecord(value) || !exactly(value, ["name", "picture"])) {
    return fail("a sheet has the wrong fields");
  }
  const { name, picture } = value;
  if (typeof name !== "string" || typeof picture !== "string") {
    return fail("a sheet has a value of the wrong type");
  }
  return { value: { name, picture } };
}

function soundOf(value: unknown): Checked<SoundData> {
  const sheet = sheetOf(value);
  return sheet;
}

function fontOf(value: unknown): Checked<FontData> {
  const names = [
    "spacing",
    "height",
    "widths",
    "text_colors",
    "columns",
    "cell_width",
    "cell_height",
    "picture",
  ];
  if (!isRecord(value) || !exactly(value, names)) {
    return fail("the font has the wrong fields");
  }
  const {
    spacing,
    height,
    widths,
    text_colors,
    columns,
    cell_width,
    cell_height,
    picture,
  } = value;
  if (
    !whole(spacing) ||
    !whole(height) ||
    !wholes(widths, GLYPHS) ||
    !wholes(text_colors, TEXT_COLORS) ||
    !whole(columns) ||
    !whole(cell_width) ||
    !whole(cell_height) ||
    typeof picture !== "string" ||
    columns < 1 ||
    cell_width < 1 ||
    cell_height < 1
  ) {
    return fail("the font has a value of the wrong type");
  }
  return {
    value: {
      spacing,
      height,
      widths,
      text_colors,
      columns,
      cell_width,
      cell_height,
      picture,
    },
  };
}

/** Return the bundle `value` holds, or the reason it is not one. */
export function decodeBundle(value: unknown): Checked<BriefingBundle> {
  const names = [
    "format",
    "format_version",
    "front_string",
    "teams",
    "briefings",
    "groups",
    "boxes",
    "craft_boxes",
    "iff_sheets",
    "sheets",
    "grey_sheet",
    "font",
    "sounds",
  ];
  if (!isRecord(value) || !exactly(value, names)) {
    return fail("a bundle has the wrong fields");
  }
  if (value.format !== "jedimaster.briefing" || value.format_version !== 1) {
    return fail("a bundle is not of the format this page reads");
  }
  const teams = listOf(value.teams, TEAMS, teamOf, "teams");
  const briefings = listOf(value.briefings, null, briefingOf, "briefings");
  const groups = listOf(value.groups, null, groupOf, "groups");
  const boxes = listOf(value.boxes, BOXES, boxOf, "boxes");
  const sheets = listOf(value.sheets, null, sheetOf, "sheets");
  const sounds = listOf(value.sounds, null, soundOf, "sounds");
  const font = fontOf(value.font);
  const grey =
    value.grey_sheet === null ? { value: null } : sheetOf(value.grey_sheet);
  for (const part of [
    teams,
    briefings,
    groups,
    boxes,
    sheets,
    sounds,
    font,
    grey,
  ]) {
    if ("reason" in part) return fail(part.reason);
  }
  const { front_string, craft_boxes, iff_sheets } = value;
  if (
    typeof front_string !== "string" ||
    !wholes(craft_boxes, CRAFTS) ||
    !texts(iff_sheets, IFFS) ||
    "reason" in teams ||
    "reason" in briefings ||
    "reason" in groups ||
    "reason" in boxes ||
    "reason" in sheets ||
    "reason" in sounds ||
    "reason" in font ||
    "reason" in grey
  ) {
    return fail("a bundle has a value of the wrong type");
  }
  return {
    value: {
      format: "jedimaster.briefing",
      format_version: 1,
      front_string,
      teams: teams.value,
      briefings: briefings.value,
      groups: groups.value,
      boxes: boxes.value,
      craft_boxes,
      iff_sheets,
      sheets: sheets.value,
      grey_sheet: grey.value,
      font: font.value,
      sounds: sounds.value,
    },
  };
}
