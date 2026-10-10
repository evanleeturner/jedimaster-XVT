// The briefing player's core: the script, the glide, the buttons.
//
// Purpose: run a mission's briefing for one team exactly as the game's setup
// screen does, one frame at a time, with no screen and no clock.
// Flow: `loadBriefing` builds a `Player` for a team; `updateFrame` is a
// frame's first part (the glide and one script step, only while playing);
// `press` is a button; `draw.ts` draws the panel from the player.
// Invariants: all numbers are whole; centers and zooms are cut to 16 bits;
// a step played "at once" makes no sound and starts markers and labels at
// age 80; the player holds no DOM and no timer.

import type { BriefingBundle } from "../generated/briefing.ts";
import { toBytes } from "./text.ts";

export const DEFAULT_RUNNING_TIME = 200;
export const END_EVENT = 34;
export const AGE_AT_ONCE = 80;
export const MARKERS = 8;
export const LABELS = 8;
export const FRAME_MS = 41;
const NO_DATA = 0;

export interface ScriptEvent {
  readonly time: number;
  readonly type: number;
  readonly variables: readonly number[];
}

export interface Script {
  readonly index: number;
  readonly runningTime: number;
  readonly events: readonly ScriptEvent[];
  readonly labels: readonly (readonly number[])[];
  readonly captions: readonly (readonly number[])[];
}

export interface Slot {
  on: boolean;
  block: number;
}

export interface Marker {
  on: boolean;
  group: number;
  age: number;
}

export interface Label {
  on: boolean;
  text: number;
  x: number;
  y: number;
  row: number;
  age: number;
}

export type Button = "play" | "stop" | "rewind" | "forward";
export const BUTTONS: readonly Button[] = ["play", "stop", "rewind", "forward"];

export interface Player {
  readonly bundle: BriefingBundle;
  readonly team: number;
  readonly script: Script;
  playing: boolean;
  t: number;
  cursor: number;
  cx: number;
  cy: number;
  tcx: number;
  tcy: number;
  sx: number;
  sy: number;
  tsx: number;
  tsy: number;
  slots: [Slot, Slot];
  markers: Marker[];
  labels: Label[];
  page: number;
  last: number;
  stopPoint: boolean;
  slotsChanged: boolean;
  sounds: string[];
}

/** Return `value` cut to 16 bits (-32768 to 32767). */
export function cut16(value: number): number {
  return (value << 16) >> 16;
}

function variable(event: ScriptEvent, n: number): number {
  return event.variables[n] ?? NO_DATA;
}

/** Return the briefing a team without a briefing of its own sees. */
export function defaultScript(): Script {
  return {
    index: 0,
    runningTime: DEFAULT_RUNNING_TIME,
    events: [{ time: 9999, type: END_EVENT, variables: [] }],
    labels: Array.from({ length: 32 }, () => []),
    captions: Array.from({ length: 32 }, () => []),
  };
}

/** Return the script `team` sees in `bundle`, or the default one. */
export function scriptFor(bundle: BriefingBundle, team: number): Script {
  const choice = bundle.teams.find((t) => t.team === team)?.briefing ?? null;
  const found = bundle.briefings.find((b) => b.index === choice);
  if (found === undefined) return defaultScript();
  return {
    index: found.index,
    runningTime: found.running_time,
    events: found.events.map((e) => ({
      time: e.time,
      type: e.type,
      variables: e.variables,
    })),
    labels: found.labels.map(toBytes),
    captions: found.captions.map(toBytes),
  };
}

function emptyMarkers(): Marker[] {
  return Array.from({ length: MARKERS }, () => ({
    on: false,
    group: 0,
    age: 0,
  }));
}

function emptyLabels(): Label[] {
  return Array.from({ length: LABELS }, () => ({
    on: false,
    text: 0,
    x: 0,
    y: 0,
    row: 0,
    age: 0,
  }));
}

function soundOf(player: Player, group: number): string {
  const iff = player.bundle.groups[group]?.iff;
  return iff === 1 ? "sfxTarget2" : "sfxTarget1";
}

function applyEvent(player: Player, event: ScriptEvent, atOnce: boolean): void {
  const { type } = event;
  const age = atOnce ? AGE_AT_ONCE : 0;
  if (type === 1) {
    player.stopPoint = true;
  } else if (type === 3) {
    player.slots[0].on = false;
    player.slots[1].on = false;
    player.slotsChanged = true;
  } else if (type === 4 || type === 5) {
    const slot = player.slots[type - 4];
    if (slot !== undefined) {
      slot.on = true;
      slot.block = variable(event, 0);
    }
  } else if (type === 6) {
    player.tcx = variable(event, 0);
    player.tcy = variable(event, 1);
    if (event.time === 0 || atOnce) {
      player.cx = player.tcx;
      player.cy = player.tcy;
    }
  } else if (type === 7) {
    player.tsx = variable(event, 0);
    player.tsy = variable(event, 1);
    if (event.time === 0 || atOnce) {
      player.sx = player.tsx;
      player.sy = player.tsy;
    }
  } else if (type === 8) {
    for (const marker of player.markers) marker.on = false;
  } else if (type >= 9 && type <= 16) {
    const marker = player.markers[type - 9];
    if (marker === undefined) return;
    marker.on = true;
    marker.group = variable(event, 0);
    marker.age = age;
    if (!atOnce) player.sounds.push(soundOf(player, marker.group));
  } else if (type === 17) {
    for (const label of player.labels) label.on = false;
  } else if (type >= 18 && type <= 25) {
    const label = player.labels[type - 18];
    if (label === undefined) return;
    label.on = true;
    label.text = variable(event, 0);
    label.x = variable(event, 1);
    label.y = variable(event, 2);
    label.row = variable(event, 3);
    label.age = age;
    const text = player.script.labels[label.text];
    if (!atOnce && text !== undefined && text.length > 0) {
      player.sounds.push("sfxText");
    }
  }
}

/** Play script step `t`, with its sounds or at once; the sounds are left in `player.sounds`. */
export function playStep(player: Player, atOnce: boolean): void {
  player.stopPoint = false;
  player.slotsChanged = false;
  const { events } = player.script;
  for (;;) {
    const event = events[player.cursor];
    if (event === undefined || event.time > player.t) break;
    player.cursor += 1;
    if (event.time === player.t) applyEvent(player, event, atOnce);
  }
  player.t += 1;
}

/** Start the briefing over: the view, the slots, the markers, then step 0 at once. */
export function startOver(player: Player): void {
  player.cx = 0;
  player.cy = 0;
  player.tcx = 0;
  player.tcy = 0;
  player.sx = 32;
  player.sy = 32;
  player.tsx = 32;
  player.tsy = 32;
  player.slots[0].on = false;
  player.slots[1].on = false;
  player.markers = emptyMarkers();
  player.labels = emptyLabels();
  player.t = 0;
  player.cursor = 0;
  playStep(player, true);
}

/** Return a player on `team`'s briefing, playing, at the start. */
export function loadBriefing(bundle: BriefingBundle, team: number): Player {
  const player: Player = {
    bundle,
    team,
    script: scriptFor(bundle, team),
    playing: true,
    t: 0,
    cursor: 0,
    cx: 0,
    cy: 0,
    tcx: 0,
    tcy: 0,
    sx: 32,
    sy: 32,
    tsx: 32,
    tsy: 32,
    slots: [
      { on: false, block: 0 },
      { on: false, block: 0 },
    ],
    markers: emptyMarkers(),
    labels: emptyLabels(),
    page: 0,
    last: 0,
    stopPoint: false,
    slotsChanged: false,
    sounds: [],
  };
  startOver(player);
  player.sounds = [];
  return player;
}

function moveToward(value: number, target: number, step: number): number {
  if (target < value) {
    const moved = cut16(value - step);
    return moved < target ? target : moved;
  }
  if (target > value) {
    const moved = cut16(value + step);
    return moved > target ? target : moved;
  }
  return value;
}

function gap(a: number, b: number): number {
  return cut16(Math.abs(a - b));
}

function glide(player: Player): void {
  const apart = Math.max(
    gap(player.tsx, player.sx),
    gap(player.tsy, player.sy),
  );
  let step = apart >= 12 ? 8 : 2;
  if (player.sx < 10) step = 1;
  player.sx = moveToward(player.sx, player.tsx, step);
  player.sy = moveToward(player.sy, player.tsy, step);
  const unit = player.sx === 0 ? 1 : Math.trunc(256 / player.sx) + 1;
  const far = Math.max(gap(player.tcx, player.cx), gap(player.tcy, player.cy));
  const move = Math.trunc(far / unit) >= 16 ? 4 * unit : 2 * unit;
  player.cx = moveToward(player.cx, player.tcx, move);
  player.cy = moveToward(player.cy, player.tcy, move);
}

function age(player: Player): void {
  for (const marker of player.markers) if (marker.on) marker.age += 1;
  for (const label of player.labels) if (label.on) label.age += 1;
}

/**
 * Run a frame's update: while playing, glide, age, then play one step with
 * its sounds or, at the running time, start over. Returns the sounds.
 */
export function updateFrame(player: Player): string[] {
  player.sounds = [];
  if (!player.playing) return [];
  glide(player);
  age(player);
  if (player.t >= player.script.runningTime) {
    player.page = 0;
    player.last = 0;
    startOver(player);
    player.sounds = [];
  } else {
    playStep(player, false);
  }
  return player.sounds;
}

function forward(player: Player): void {
  const origin = player.t;
  startOver(player);
  let lastType = 0;
  let seen = false;
  let textFrames = 0;
  for (;;) {
    if (lastType === END_EVENT) break;
    lastType = player.script.events[player.cursor]?.type ?? END_EVENT;
    if (player.slotsChanged) {
      textFrames = 0;
      seen = false;
    }
    if (player.slots[0].on || player.slots[1].on) seen = true;
    if (seen) textFrames += 1;
    if ((player.stopPoint || textFrames === 1) && origin <= player.t) break;
    playStep(player, true);
  }
  if (player.stopPoint || textFrames === 1) {
    const target = player.t;
    while (target >= player.t) playStep(player, false);
  } else {
    player.page = 0;
    player.last = 0;
    startOver(player);
  }
  if (player.slots[1].block === player.last) {
    player.page = 0;
    player.last = 0;
    startOver(player);
  }
}

/**
 * Press a button after a frame's drawing; return the sounds the press made
 * (Forward plays one step with sounds), or null when the button is not
 * offered now (the press does nothing).
 */
export function press(player: Player, button: Button): string[] | null {
  player.sounds = [];
  if (button === "rewind") {
    player.page = 0;
    player.last = 0;
    startOver(player);
    player.sounds = [];
  } else if (button === "stop") {
    if (!player.playing) return null;
    player.playing = false;
  } else if (button === "play") {
    if (player.playing) return null;
    player.playing = true;
  } else {
    if (!player.playing) return null;
    forward(player);
  }
  return player.sounds;
}

/** Return true when `button` is offered in the player's present state. */
export function offered(player: Player, button: Button): boolean {
  if (button === "rewind") return true;
  if (button === "play") return !player.playing;
  return player.playing;
}
