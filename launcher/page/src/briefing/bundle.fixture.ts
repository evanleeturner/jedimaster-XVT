// A made-up briefing bundle for the tests: no game data.
//
// Purpose: build bundles with the shapes the player reads, from a few
// options, so a test names only what it cares about.
// Flow: `makeBundle` fills the fixed tables (boxes, crafts, IFFs, font) with
// made-up values and takes the briefings and groups the test gives.
// Invariants: every glyph is 6 pixels wide except the space (4) and the color
// bytes (0); the font height is 14 and its spacing 0; box `n` is 8 wide and
// 12 high; every craft uses box 0 unless a test says otherwise.

import type {
  BriefingBundle,
  BriefingData,
  EventData,
  GroupData,
} from "../generated/briefing.ts";

export type Ev = readonly [number, number, ...number[]];

export function events(...list: Ev[]): EventData[] {
  return list.map(([time, type, ...variables]) => ({ time, type, variables }));
}

export function texts(...list: string[]): string[] {
  return [...list, ...Array.from({ length: 32 - list.length }, () => "")];
}

export interface BriefingOptions {
  readonly index?: number;
  readonly runningTime?: number;
  readonly events?: readonly Ev[];
  readonly labels?: readonly string[];
  readonly captions?: readonly string[];
}

export function briefing(options: BriefingOptions = {}): BriefingData {
  return {
    index: options.index ?? 0,
    running_time: options.runningTime ?? 200,
    events: events(...(options.events ?? []), [9999, 34]),
    labels: texts(...(options.labels ?? [])),
    captions: texts(...(options.captions ?? [])),
  };
}

export function group(options: Partial<GroupData> = {}): GroupData {
  return {
    number: 0,
    name: "Alpha",
    craft_type: 0,
    iff: 0,
    team: 0,
    player_number: 0,
    points: Array.from({ length: 8 }, () => ({ x: 0, y: 0, enabled: true })),
    ...options,
  };
}

export interface BundleOptions {
  readonly briefings?: readonly BriefingData[];
  readonly teamBriefings?: Readonly<Record<number, number>>;
  readonly groups?: readonly GroupData[];
  readonly frontString?: string;
}

const WIDTHS = Array.from({ length: 256 }, (_, code) =>
  code <= 6 ? 0 : code === 32 ? 4 : 6,
);

export function makeBundle(options: BundleOptions = {}): BriefingBundle {
  const chosen = options.teamBriefings ?? { 0: 0 };
  return {
    format: "jedimaster.briefing",
    format_version: 1,
    front_string: options.frontString ?? "Pg.",
    teams: Array.from({ length: 10 }, (_, team) => ({
      team,
      name: `Team ${String(team)}`,
      briefing: chosen[team] ?? null,
    })),
    briefings: [...(options.briefings ?? [briefing()])],
    groups: [...(options.groups ?? [])],
    boxes: Array.from({ length: 70 }, () => ({
      left: 0,
      top: 0,
      right: 7,
      bottom: 11,
    })),
    craft_boxes: Array.from({ length: 106 }, () => 0),
    iff_sheets: Array.from({ length: 256 }, () => "mapicon0"),
    sheets: [0, 1, 2, 3, 4].map((n) => ({
      name: `mapicon${String(n)}`,
      picture: `mapicon${String(n)}.png`,
    })),
    grey_sheet: { name: "greyicon", picture: "greyicon.png" },
    font: {
      spacing: 0,
      height: 14,
      widths: WIDTHS,
      text_colors: [2016, 63488, 65504, 12703, 33823],
      columns: 16,
      cell_width: 8,
      cell_height: 14,
      picture: "times10.png",
    },
    sounds: [],
  };
}
