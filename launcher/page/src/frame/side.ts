// The eight side buttons of the 1997 Mission Selection, as pure rules.
//
// Purpose: say, without the DOM, which of the eight buttons is pressed, which
// light sprites burn beside them, and what a click on each does when flying
// solo: pick a mission type, step through its missions, or pick the pilot's
// side.
// Flow: `describeSide` turns the model (the selected type, the chosen side,
// the last mission shown per list) into the eight buttons and the sprite
// names to draw; `press` turns a click into the next model plus the mission
// to show, if any; `afterShown` keeps the model true to a mission the page
// was told to show, wherever the pick came from.
// Invariants: slots are the game's, 1 to 8 (Training, Melee, Tournament,
// Combat, Battle, Rebel pilot, Imperial pilot, Campaign); a button draws its
// "d" sprite when selected and "u" otherwise; the light sprites follow the
// 1997 rule (see `lightNames`). A step visits each mission of a list at most
// once, wrapping; when none is available it ends after one lap, where the
// 1997 loop would never end (that is not copied). A sided kind (training,
// combat, campaign) walks its solo list for the chosen side, every entry
// offered (a solo entry always flies) and keyed by its file; the other kinds
// walk their network menu, keyed by id and offered only when available.
// The multiplayer form (the host decides, the buttons grey) is a later
// round: the view is a function of its inputs, and `INACTIVE` is the state
// such an input would use.

import type { MissionType } from "../codec.ts";
import type { MenuData } from "../generated/control.ts";

export type Faction = "rebel" | "imperial";
export type SlotState = 0 | 1 | 2;
export const INACTIVE = 0;
export const ACTIVE = 1;
export const SELECTED = 2;

export type SideButtonId = MissionType | Faction;

export interface SideButton {
  readonly id: SideButtonId;
  /** The game's slot, 1 to 8. */
  readonly slot: number;
  /** The words the page uses for the button. */
  readonly label: string;
  /** The sprite name stem: `game1` for Training, `reg6` for Rebel pilot. */
  readonly stem: string;
}

/** The eight buttons in the order they sit down the screen, top first. */
export const SIDE_BUTTONS: readonly SideButton[] = [
  { id: "training", slot: 1, label: "Training", stem: "game1" },
  { id: "melee", slot: 2, label: "Melee", stem: "game2" },
  { id: "tournament", slot: 3, label: "Tournament", stem: "game3" },
  { id: "combat", slot: 4, label: "Combat engagement", stem: "game4" },
  { id: "battle", slot: 5, label: "Battle", stem: "game5" },
  { id: "campaign", slot: 8, label: "Campaign", stem: "game8" },
  { id: "rebel", slot: 6, label: "Rebel pilot", stem: "reg6" },
  { id: "imperial", slot: 7, label: "Imperial pilot", stem: "reg7" },
];

/** The slot order the lights are taken in: 1 to 5, 8, 6, 7. */
const LIGHT_ORDER = [1, 2, 3, 4, 5, 8, 6, 7] as const;

export const SIDE_PATH = "/art/side/";

/** The mission kinds that have one list for each side. */
const SIDED: readonly MissionType[] = ["training", "combat", "campaign"];

/** The kinds that take the pilot's side into account, as `launch.menu` does. */
export function isSided(kind: MissionType): boolean {
  return SIDED.includes(kind);
}

/** What the page remembers about the eight buttons. */
export interface SideModel {
  /** The selected type, null until one is shown or picked. */
  readonly kind: MissionType | null;
  /** The chosen pilot side. */
  readonly side: Faction;
  /** The last mission shown for each list, by `listKey`. */
  readonly remembered: Readonly<Record<string, MissionKey>>;
}

export const START_MODEL: SideModel = {
  kind: null,
  side: "rebel",
  remembered: {},
};

/** The lists the page knows: the network menus and the solo files of each sided list. */
export interface SideLists {
  readonly menus: readonly MenuData[] | null;
  /** The solo list's mission files in order, by `soloKey`; a list that never came is absent. */
  readonly solo: Readonly<Record<string, readonly string[]>>;
}

/**
 * How a mission is named to the launcher: by id in a network menu, or by side
 * and file in a sided kind's solo list.
 */
export type MissionKey =
  | { readonly by: "id"; readonly id: number }
  | { readonly by: "file"; readonly side: Faction; readonly file: string };

/** One mission of a list, in the list's order. */
export interface Choice {
  readonly key: MissionKey;
  readonly available: boolean;
}

/** A mission the page is to ask the launcher to show. */
export interface Show {
  readonly missionType: MissionType;
  readonly key: MissionKey;
}

/** Return true when two keys name the same mission (a file in any letter case). */
export function sameKey(a: MissionKey | null, b: MissionKey | null): boolean {
  if (a === null || b === null) return false;
  if (a.by === "id") return b.by === "id" && a.id === b.id;
  return (
    b.by === "file" &&
    a.side === b.side &&
    a.file.toLowerCase() === b.file.toLowerCase()
  );
}

/** The arguments of `page.show_mission` and `briefing.get` that name a mission. */
export type MissionArgs =
  | { readonly mission_type: MissionType; readonly id: number }
  | {
      readonly mission_type: MissionType;
      readonly side: Faction;
      readonly file: string;
    };

/** Return the request arguments that name mission `key` of `type`. */
export function missionArgs(type: MissionType, key: MissionKey): MissionArgs {
  return key.by === "id"
    ? { mission_type: type, id: key.id }
    : { mission_type: type, side: key.side, file: key.file };
}

/** Return the key a mission the launcher showed is named by. */
export function keyOfShown(
  shown:
    { readonly id: number } | { readonly side: Faction; readonly file: string },
): MissionKey {
  return "file" in shown
    ? { by: "file", side: shown.side, file: shown.file }
    : { by: "id", id: shown.id };
}

/** What a click did: the model after it, the mission to show, a line to say. */
export interface Press {
  readonly model: SideModel;
  readonly show: Show | null;
  readonly note: string | null;
}

/** One button as the page draws it. */
export interface SideButtonView {
  readonly id: SideButtonId;
  readonly slot: number;
  readonly label: string;
  readonly selected: boolean;
  /** True for the selected mission type: a press steps, Shift for back. */
  readonly stepping: boolean;
  /** The button's own sprite name: `game1u`, `reg6d`... */
  readonly sprite: string;
}

export interface SideView {
  readonly buttons: readonly SideButtonView[];
  /** The light sprite names to draw over the screen. */
  readonly lights: readonly string[];
}

/** Return the key the remembered mission of a list is kept under. */
export function listKey(kind: MissionType, side: Faction): string {
  return isSided(kind) ? `${kind}:${side}` : kind;
}

/** Return the key the solo list of a sided kind and side is kept under. */
export function soloKey(kind: string, side: string): string {
  return `${kind}:${side}`;
}

/** Return the eight slot states for solo: slot 1 first. */
export function slotStates(model: SideModel): readonly SlotState[] {
  const states: SlotState[] = [];
  for (let slot = 1; slot <= 8; slot += 1) {
    const button = SIDE_BUTTONS.find((b) => b.slot === slot);
    const selected = button?.id === model.kind || button?.id === model.side;
    states.push(selected ? SELECTED : ACTIVE);
  }
  return states;
}

/**
 * Return the light sprite names for slot states (slot 1 first), by the 1997
 * rule: `active<n>` for each active slot n; for each selected slot, taking the
 * slots in the order 1 to 5, 8, 6, 7 and counting positions `n` in that order,
 * `<n>lita<s>` and `<n>litb<s>`, s being the state of the position before and
 * after. The first and seventh positions get no `lita`, the sixth and eighth
 * no `litb`.
 */
export function lightNames(states: readonly SlotState[]): string[] {
  const names: string[] = [];
  states.forEach((state, at) => {
    if (state === ACTIVE) names.push(`active${String(at + 1)}`);
  });
  const ordered = LIGHT_ORDER.map((slot) => states[slot - 1] ?? INACTIVE);
  ordered.forEach((state, at) => {
    if (state !== SELECTED) return;
    const n = String(at + 1);
    if (at !== 0 && at !== 6) {
      names.push(`${n}lita${String(ordered[at - 1] ?? INACTIVE)}`);
    }
    if (at !== 7 && at !== 5) {
      names.push(`${n}litb${String(ordered[at + 1] ?? INACTIVE)}`);
    }
  });
  return names;
}

/** Return a button's sprite name: its stem and `d` when selected, else `u`. */
export function buttonSprite(stem: string, state: SlotState): string {
  return `${stem}${state === SELECTED ? "d" : "u"}`;
}

/** Return the eight buttons and the lights for a model. */
export function describeSide(model: SideModel): SideView {
  const states = slotStates(model);
  const buttons = SIDE_BUTTONS.map((button) => {
    const state = states[button.slot - 1] ?? INACTIVE;
    return {
      id: button.id,
      slot: button.slot,
      label: button.label,
      selected: state === SELECTED,
      stepping: state === SELECTED && button.id === model.kind,
      sprite: buttonSprite(button.stem, state),
    };
  });
  return { buttons, lights: lightNames(states) };
}

/**
 * Return the missions of the list for a kind and side, in the list's order.
 *
 * A sided kind is its solo list for `side` itself: every file, each offered
 * and keyed by side and file. Any other kind is its network menu, keyed by id
 * and offered only when the menu marks the entry available. An unresolved
 * menu, or a solo list that has not come, is empty.
 */
export function missionChoices(
  lists: SideLists,
  kind: MissionType,
  side: Faction,
): Choice[] {
  if (isSided(kind)) {
    return (lists.solo[soloKey(kind, side)] ?? []).map((file) => ({
      key: { by: "file", side, file },
      available: true,
    }));
  }
  const menu = lists.menus?.find((m) => m.mission_type === kind);
  if (!menu?.resolved) return [];
  return menu.entries.map((e) => ({
    key: { by: "id", id: e.id },
    available: e.available,
  }));
}

/** Return the first available mission of a list, or null. */
export function firstAvailable(choices: readonly Choice[]): MissionKey | null {
  return choices.find((c) => c.available)?.key ?? null;
}

/**
 * Return the next available mission after `current` (the previous one if
 * `backward`), wrapping, or null when none is available.
 *
 * With `current` null or not in the list a forward step gives the first
 * available mission and a backward step the last. A list holding only the
 * current mission gives it again. The walk is one lap at most: the 1997
 * loop that never ends when nothing is available is not copied.
 */
export function stepMission(
  choices: readonly Choice[],
  current: MissionKey | null,
  backward: boolean,
): MissionKey | null {
  const count = choices.length;
  if (count === 0) return null;
  const at = choices.findIndex((c) => sameKey(c.key, current));
  const direction = backward ? -1 : 1;
  const start = at >= 0 ? at : backward ? 0 : count - 1;
  for (let lap = 1; lap <= count; lap += 1) {
    const choice =
      choices[(((start + direction * lap) % count) + count) % count];
    if (choice?.available === true) return choice.key;
  }
  return null;
}

function labelOf(kind: MissionType): string {
  return SIDE_BUTTONS.find((b) => b.id === kind)?.label ?? kind;
}

function noMission(kind: MissionType): string {
  return `No ${labelOf(kind)} mission is available to show.`;
}

function isFaction(id: SideButtonId): id is Faction {
  return id === "rebel" || id === "imperial";
}

function remember(
  model: SideModel,
  kind: MissionType,
  mission: MissionKey,
): SideModel {
  const key = listKey(kind, model.side);
  return { ...model, remembered: { ...model.remembered, [key]: mission } };
}

function open(
  model: SideModel,
  lists: SideLists,
  kind: MissionType,
  backward: boolean,
  fresh: boolean,
): Press {
  const choices = missionChoices(lists, kind, model.side);
  const kept = model.remembered[listKey(kind, model.side)] ?? null;
  const keptOpen = choices.some((c) => sameKey(c.key, kept) && c.available);
  const next =
    fresh && keptOpen
      ? kept
      : fresh
        ? firstAvailable(choices)
        : stepMission(choices, kept, backward);
  if (next === null) return { model, show: null, note: noMission(kind) };
  return {
    model: remember(model, kind, next),
    show: { missionType: kind, key: next },
    note: null,
  };
}

/**
 * Return what a click on `button` does.
 *
 * An unselected type is selected and shows its remembered mission if still
 * available, else its first available one (a sided kind in the chosen side's
 * list); with none available the type is still selected, nothing is shown and
 * the note says so. The selected type shows the next available mission, or
 * the previous one if `backward`. The side that is not chosen is chosen, the
 * type kept, and a sided type shows that side's remembered or first available
 * mission; the chosen side's own button does nothing, as in 1997.
 */
export function press(
  model: SideModel,
  lists: SideLists,
  button: SideButtonId,
  backward: boolean,
): Press {
  if (isFaction(button)) {
    if (button === model.side) return { model, show: null, note: null };
    const next = { ...model, side: button };
    if (next.kind === null || !isSided(next.kind)) {
      return { model: next, show: null, note: null };
    }
    return open(next, lists, next.kind, false, true);
  }
  if (button !== model.kind) {
    return open({ ...model, kind: button }, lists, button, false, true);
  }
  return open(model, lists, button, backward, false);
}

/**
 * Return the model after the page was told to show the mission `key` of `type`.
 *
 * The type is selected and the mission is remembered for its list. A solo
 * mission (side and file) sets the side it names. A network entry of a sided
 * type sets the side whose solo list holds the entry's file, the chosen
 * side's list asked first; the side is kept, and nothing is remembered, when
 * neither list holds it.
 */
export function afterShown(
  model: SideModel,
  lists: SideLists,
  type: MissionType,
  key: MissionKey,
): SideModel {
  if (key.by === "file") {
    return remember({ ...model, kind: type, side: key.side }, type, key);
  }
  const kinded = { ...model, kind: type };
  if (!isSided(type)) return remember(kinded, type, key);
  const file = lists.menus
    ?.find((m) => m.mission_type === type)
    ?.entries.find((e) => e.id === key.id)?.file;
  if (file === undefined) return kinded;
  const lower = file.toLowerCase();
  const other: Faction = model.side === "rebel" ? "imperial" : "rebel";
  for (const side of [model.side, other]) {
    if ((lists.solo[soloKey(type, side)] ?? []).includes(lower)) {
      return remember({ ...kinded, side }, type, {
        by: "file",
        side,
        file: lower,
      });
    }
  }
  return kinded;
}

/** True when every list `afterShown` needs for `type` has arrived. */
export function listsKnown(lists: SideLists, type: MissionType): boolean {
  if (lists.menus === null) return false;
  if (!isSided(type)) return true;
  return (
    soloKey(type, "rebel") in lists.solo &&
    soloKey(type, "imperial") in lists.solo
  );
}
