import type { ArtScaling, Message, MissionType, Shown } from "./codec.ts";
import { isMissionType } from "./codec.ts";
import type { DebriefTab, Table } from "./debrief/view.ts";
import { headingLine, resultLabel, tablesOf } from "./debrief/view.ts";
import type { Press, SideLists, SideModel, SideView } from "./frame/side.ts";
import {
  afterShown,
  describeSide,
  keyOfShown,
  listsKnown,
  soloKey,
  START_MODEL,
} from "./frame/side.ts";
import type { BriefingBundle } from "./generated/briefing.ts";
import type {
  Debrief,
  GameOptions,
  InstallStatusResult,
  LaunchStartedData,
  MenuData,
  OkReply,
  ResultSummary,
} from "./generated/control.ts";

export type Connection = "connecting" | "open" | "closed";

export type BriefingState =
  | { readonly status: "none" }
  | { readonly status: "loading" }
  | { readonly status: "missing" }
  | { readonly status: "ready"; readonly bundle: BriefingBundle };

export interface LaunchState {
  readonly started: LaunchStartedData;
  readonly cancelling: boolean;
  /** The ending line once the battle is over, else null. */
  readonly ending: string | null;
}

/** The kept battle results and the debriefing on show. */
export interface DebriefState {
  /** The kept results, the newest first; null until the launcher answers. */
  readonly results: readonly ResultSummary[] | null;
  /** The debriefing on show, null when none is. */
  readonly shown: Debrief | null;
  readonly tab: DebriefTab;
  /** True when a battle's ending brought the debriefing on show and it has not been opened. */
  readonly fresh: boolean;
}

/** How many kept results the page lists, as the launcher keeps them. */
export const RESULTS_KEPT = 20;

export interface State {
  readonly connection: Connection;
  readonly launcherVersion: string | null;
  readonly install: InstallStatusResult | null;
  readonly menus: readonly MenuData[] | null;
  readonly artScaling: ArtScaling | null;
  readonly shown: Shown | null;
  readonly shownCount: number;
  readonly briefing: BriefingState;
  readonly launch: LaunchState | null;
  readonly notice: string | null;
  readonly options: GameOptions | null;
  /**
   * The linked Discord account's pilot name, null when it has none, and
   * undefined until the launcher's status names it (the page cannot know).
   */
  readonly pilot: string | null | undefined;
  readonly debrief: DebriefState;
  /** The side buttons: the selected type, the chosen side, what was shown last. */
  readonly side: SideModel;
  /** The solo lists' mission files, by `soloKey`, as the launcher answered. */
  readonly soloLists: Readonly<Record<string, readonly string[]>>;
  /** False while the shown mission could not be placed for want of a list. */
  readonly sideSettled: boolean;
  /** What the last side button press had to say, null when nothing. */
  readonly sideNote: string | null;
  readonly problem: string | null;
}

export type Action =
  | { readonly kind: "connecting" }
  | { readonly kind: "opened" }
  | { readonly kind: "closed" }
  | { readonly kind: "cancelling" }
  | { readonly kind: "debrief-tab"; readonly tab: DebriefTab }
  | { readonly kind: "debrief-seen" }
  | { readonly kind: "side-pressed"; readonly press: Press }
  | { readonly kind: "message"; readonly message: Message };

export interface MenuEntryView {
  readonly text: string;
  readonly current: boolean;
  /** What the entry's button asks for, or null when the entry is not available. */
  readonly pick: {
    readonly missionType: MissionType;
    readonly id: number;
    readonly label: string;
  } | null;
}

export interface MenuView {
  readonly heading: string;
  readonly note: string | null;
  readonly entries: readonly MenuEntryView[];
}

/** One mission in the Missions menu's list. */
export interface MissionRow {
  readonly title: string;
  readonly current: boolean;
  /** What the Show button asks for, or null when the mission is not available. */
  readonly pick: {
    readonly missionType: MissionType;
    readonly id: number;
    readonly label: string;
  } | null;
}

/** One group (the game's section) of a kind, in the order the game lists it. */
export interface GroupView {
  readonly name: string;
  readonly missions: readonly MissionRow[];
}

/** One kind of mission: its groups, or the reason it has none. */
export interface KindView {
  readonly id: string;
  readonly label: string;
  readonly count: number;
  readonly note: string | null;
  readonly groups: readonly GroupView[];
}

/** Where the shown mission sits, for the heading above the briefing. */
export interface ShownView {
  readonly kind: string;
  readonly group: string;
  readonly title: string;
}

export const UNGROUPED = "All missions";

/** One kept result in the Debriefing panel's picker. */
export interface ResultOption {
  readonly id: number;
  readonly label: string;
  readonly current: boolean;
}

/** The Debriefing panel: the picker, the shown debriefing's heading, tab and tables. */
export interface DebriefView {
  readonly note: string | null;
  readonly options: readonly ResultOption[];
  readonly heading: string | null;
  readonly tab: DebriefTab;
  readonly tables: readonly Table[];
  readonly fresh: boolean;
  /** The debriefing on show, for the 1997 screen; null when none is. */
  readonly shown: Debrief | null;
  /** The tabs that are not offered (the framed screen's kind has none of them). */
  readonly hiddenTabs: readonly DebriefTab[];
}

export interface View {
  readonly connectionText: string;
  readonly showReconnect: boolean;
  readonly versionText: string;
  readonly installText: string;
  readonly balanceText: string;
  readonly menus: readonly MenuView[];
  readonly kinds: readonly KindView[];
  readonly shownWhere: ShownView | null;
  /** The shown mission's kind (`training`...), or null when none is shown. */
  readonly shownType: MissionType | null;
  /** The launcher's version, or null before it arrives. */
  readonly launcherVersion: string | null;
  readonly missionsNote: string | null;
  readonly artScaling: ArtScaling | null;
  readonly shownText: string;
  readonly shownCount: number;
  readonly briefing: BriefingState;
  readonly briefingNote: string | null;
  readonly settingsEnabled: boolean;
  readonly battleNote: string;
  readonly battleLine: string | null;
  readonly battleEnding: string | null;
  readonly cancelEnabled: boolean;
  readonly cancelShown: boolean;
  readonly optionsRows: readonly OptionRow[] | null;
  /** The Pilot panel's sentence. */
  readonly pilotLine: string;
  /** The frame's bottom bar words. */
  readonly pilotBar: string;
  readonly debrief: DebriefView;
  /** The eight side buttons and their lights. */
  readonly side: SideView;
  /** The selected mission type, null when none is. */
  readonly sideKind: MissionType | null;
  /** The chosen pilot side. */
  readonly sideFaction: SideModel["side"];
  /** What the last side button press had to say, null when nothing. */
  readonly sideNote: string | null;
  readonly problem: string | null;
}

export interface OptionRow {
  readonly name: string;
  readonly label: string;
  readonly value: string;
}

export const PAGE_VERSION = "0.1.0";

export function initialState(): State {
  return {
    connection: "connecting",
    launcherVersion: null,
    install: null,
    menus: null,
    artScaling: null,
    shown: null,
    shownCount: 0,
    briefing: { status: "none" },
    launch: null,
    notice: null,
    options: null,
    pilot: undefined,
    debrief: { results: null, shown: null, tab: "overview", fresh: false },
    side: START_MODEL,
    soloLists: {},
    sideSettled: true,
    sideNote: null,
    problem: null,
  };
}

function listsOf(state: State): SideLists {
  return { menus: state.menus, solo: state.soloLists };
}

/** Return the state with the shown mission placed on the side buttons, once its lists are known. */
function settle(state: State): State {
  const shown = state.shown;
  if (state.sideSettled || shown === null) return state;
  const lists = listsOf(state);
  if (!listsKnown(lists, shown.mission_type)) return state;
  const key = keyOfShown(shown);
  const side = afterShown(state.side, lists, shown.mission_type, key);
  return { ...state, side, sideSettled: true };
}

function applyResult(state: State, result: OkReply["result"]): State {
  if ("bundle" in result) {
    if (result.found && result.bundle !== null) {
      return { ...state, briefing: { status: "ready", bundle: result.bundle } };
    }
    return { ...state, briefing: { status: "missing" } };
  }
  if ("schema_revision" in result) {
    return { ...state, launcherVersion: result.launcher_version };
  }
  if ("mission_type" in result && "side" in result) {
    if (result.side === "none") return state;
    const files = result.entries.map((entry) => entry.file.toLowerCase());
    const key = soloKey(result.mission_type, result.side);
    return settle({
      ...state,
      soloLists: { ...state.soloLists, [key]: files },
    });
  }
  if ("pilots" in result || "mission_type" in result) return state;
  if ("started" in result) return state;
  if ("cancelled" in result) {
    return result.cancelled ? state : { ...state, notice: result.message };
  }
  if ("options" in result) return { ...state, options: result.options };
  if ("found" in result) return { ...state, install: result };
  if ("menus" in result) return settle({ ...state, menus: result.menus });
  if ("shown" in result) return state;
  if ("results" in result) {
    return { ...state, debrief: { ...state.debrief, results: result.results } };
  }
  if ("debrief" in result) {
    if (result.debrief === null) {
      return { ...state, notice: "That battle result is no longer kept." };
    }
    return {
      ...state,
      debrief: { ...state.debrief, shown: result.debrief, fresh: false },
    };
  }
  return { ...state, artScaling: result.settings.art_scaling };
}

function applyMessage(state: State, message: Message): State {
  if ("event" in message) {
    if (message.event === "status") {
      return {
        ...state,
        launcherVersion: message.data.launcher_version,
        pilot: message.data.pilot,
        problem: null,
      };
    }
    if (message.event === "pilot.changed") {
      return { ...state, pilot: message.data.pilot, problem: null };
    }
    if (message.event === "page.show_mission") {
      const lists = listsOf(state);
      const type = message.data.mission_type;
      return {
        ...state,
        shown: message.data,
        shownCount: state.shownCount + 1,
        briefing: { status: "loading" },
        side: afterShown(state.side, lists, type, keyOfShown(message.data)),
        sideSettled: "file" in message.data || listsKnown(lists, type),
        sideNote: null,
        problem: null,
      };
    }
    if (message.event === "launch.started") {
      return {
        ...state,
        launch: { started: message.data, cancelling: false, ending: null },
        notice: null,
        problem: null,
      };
    }
    if (message.event === "launch.ended") {
      const current = state.launch;
      if (current?.started.launch_id !== message.data.launch_id) return state;
      return {
        ...state,
        debrief: withEnding(state.debrief, message.data.debrief),
        launch: {
          started: current.started,
          cancelling: false,
          ending: message.data.line,
        },
        notice: null,
        problem: null,
      };
    }
    if (message.event === "options.changed") {
      return { ...state, options: message.data.options, problem: null };
    }
    return {
      ...state,
      artScaling: message.data.settings.art_scaling,
      problem: null,
    };
  }
  if ("ok" in message) {
    if (message.ok)
      return applyResult({ ...state, problem: null }, message.result);
    return {
      ...state,
      problem: `The launcher refused a request: ${message.error.message}.`,
    };
  }
  return state;
}

export function reduce(state: State, action: Action): State {
  switch (action.kind) {
    case "connecting":
      return { ...state, connection: "connecting", problem: null };
    case "opened":
      return { ...state, connection: "open", problem: null };
    case "closed":
      return { ...state, connection: "closed" };
    case "cancelling":
      if (state.launch?.ending !== null) return state;
      return { ...state, launch: { ...state.launch, cancelling: true } };
    case "debrief-tab":
      return { ...state, debrief: { ...state.debrief, tab: action.tab } };
    case "debrief-seen":
      return { ...state, debrief: { ...state.debrief, fresh: false } };
    case "side-pressed":
      return {
        ...state,
        side: action.press.model,
        sideNote: action.press.note,
      };
    case "message":
      return applyMessage(state, action.message);
  }
}

/** Return the debriefing state after a battle's ending: its debriefing shown and listed first. */
function withEnding(
  state: DebriefState,
  debrief: Debrief | null,
): DebriefState {
  if (debrief === null) return state;
  const summary: ResultSummary = {
    result_id: debrief.result_id,
    ended_at: debrief.ended_at,
    title: debrief.title,
    outcome: debrief.outcome,
    score: debrief.score,
  };
  const results =
    state.results === null
      ? null
      : [
          summary,
          ...state.results.filter((r) => r.result_id !== summary.result_id),
        ].slice(0, RESULTS_KEPT);
  return { results, shown: debrief, tab: "overview", fresh: true };
}

/** Return the Debriefing panel's view. */
export function debriefView(state: DebriefState): DebriefView {
  const shown = state.shown;
  const listed = state.results ?? [];
  const options = listed.map((r) => ({
    id: r.result_id,
    label: resultLabel(r),
    current: r.result_id === shown?.result_id,
  }));
  let note: string | null = null;
  if (state.results === null)
    note = "The kept battle results have not arrived yet.";
  else if (listed.length === 0 && shown === null) {
    note = "No battle results are kept yet. Fly a battle from Discord.";
  }
  return {
    note,
    options,
    heading: shown === null ? null : headingLine(shown),
    tab: state.tab,
    tables: shown === null ? [] : tablesOf(shown, state.tab),
    fresh: state.fresh,
    shown,
    hiddenTabs: [],
  };
}

const CONNECTION_TEXT: Record<Connection, string> = {
  connecting: "Connecting to the launcher.",
  open: "Connected to the launcher.",
  closed: "The connection to the launcher has dropped.",
};

const BRIEFING_NOTE: Record<BriefingState["status"], string | null> = {
  none: "Pick a mission to see its briefing.",
  loading: "Loading the briefing.",
  missing: "This mission's briefing could not be shown.",
  ready: null,
};

export const OPTION_LABELS: Record<string, string> = {
  difficulty: "Difficulty",
  collisions: "Collisions",
  craft_jumping: "Craft jumping",
  random_setup: "Random setup",
  battle_length: "Battle length",
  mission_time_limit:
    "Mission time limit (multiplayer only: no effect in solo play)",
  continue_sequence: "Battle or campaign",
  sith: "Sith",
};

const VALUE_WORDS: Record<string, string> = {
  easy: "Easy",
  medium: "Medium",
  hard: "Hard",
  easy_cheat: "Easy with cheats (campaigns)",
  on: "On",
  off: "Off",
  none: "No limit",
  default: "The mission's own limit",
  restart: "Start again",
  continue: "Continue",
};

const SITH_WORDS: Record<string, string> = {
  none: "None",
  apprentices: "Apprentices",
  lord: "A Lord",
};

export function optionWords(name: string, value: string): string {
  if (name === "sith") return SITH_WORDS[value] ?? value;
  if (name === "battle_length") return `${value} wins`;
  if (name === "mission_time_limit" && /^[0-9]+$/.test(value)) {
    return value === "1" ? "1 minute" : `${value} minutes`;
  }
  return VALUE_WORDS[value] ?? value;
}

const OPTION_ORDER: (keyof GameOptions)[] = [
  "difficulty",
  "collisions",
  "craft_jumping",
  "random_setup",
  "battle_length",
  "mission_time_limit",
  "continue_sequence",
  "sith",
];

export const NO_PILOT_LINE =
  "No pilot yet. Link Discord with /link and the launcher makes one.";

/** The Pilot panel's sentence for a pilot name, for none, or before the launcher said. */
export function pilotLine(pilot: string | null | undefined): string {
  if (pilot === undefined) return "The pilot is not known yet.";
  return pilot === null ? NO_PILOT_LINE : `Your pilot is ${pilot}.`;
}

/** The frame's bottom bar words for a pilot name, for none, or before the launcher said. */
export function pilotBar(pilot: string | null | undefined): string {
  if (pilot === undefined) return "Pilot: not known yet";
  return pilot === null ? "Pilot: none yet" : `Pilot: ${pilot}`;
}

function optionRows(options: GameOptions | null): OptionRow[] | null {
  if (options === null) return null;
  return OPTION_ORDER.map((name) => ({
    name,
    label: OPTION_LABELS[name] ?? name,
    value: optionWords(name, options[name]),
  }));
}

function battleView(
  state: State,
): Pick<
  View,
  "battleNote" | "battleLine" | "battleEnding" | "cancelEnabled" | "cancelShown"
> {
  const { launch, notice } = state;
  if (launch === null) {
    return {
      battleNote: notice ?? "No battle is flying. Battles start in Discord.",
      battleLine: null,
      battleEnding: null,
      cancelEnabled: false,
      cancelShown: false,
    };
  }
  const { started, cancelling, ending } = launch;
  const line = `${started.title}, flown by ${started.pilot}.`;
  if (ending !== null) {
    return {
      battleNote: notice ?? "The battle is over.",
      battleLine: line,
      battleEnding: ending,
      cancelEnabled: false,
      cancelShown: false,
    };
  }
  return {
    battleNote:
      notice ?? (cancelling ? "Cancelling the battle." : "A battle is flying."),
    battleLine: line,
    battleEnding: null,
    cancelEnabled: !cancelling && state.connection === "open",
    cancelShown: true,
  };
}

function titleCase(name: string): string {
  return name.charAt(0).toUpperCase() + name.slice(1);
}

/** True when `entry` of `menu` is the mission shown: by id, or by file for a solo mission. */
function isShown(
  shown: Shown | null,
  menu: MenuData,
  entry: MenuData["entries"][number],
): boolean {
  if (shown?.mission_type !== menu.mission_type) return false;
  return "file" in shown
    ? entry.file.toLowerCase() === shown.file.toLowerCase()
    : entry.id === shown.id;
}

function menuView(menu: MenuData, shown: Shown | null): MenuView {
  const heading = `${titleCase(menu.mission_type)} missions`;
  if (!menu.resolved) {
    return {
      heading,
      note: `The game's list for this kind of mission was not found (${menu.game_path}).`,
      entries: [],
    };
  }
  const target = menu.entries.findIndex((entry) => isShown(shown, menu, entry));
  const entries = menu.entries.map((entry, index) => {
    const place = entry.section === "" ? "" : `${entry.section}: `;
    const word = entry.available ? "available" : "not available";
    return {
      text: `${place}${entry.title} (${entry.file}), ${word}`,
      current: index === target,
      pick:
        entry.available && isMissionType(menu.mission_type)
          ? {
              missionType: menu.mission_type,
              id: entry.id,
              label: `Show ${entry.title}`,
            }
          : null,
    };
  });
  return {
    heading,
    note: entries.length === 0 ? "This list has no missions." : null,
    entries,
  };
}

function kindView(menu: MenuData, shown: Shown | null): KindView {
  const label = titleCase(menu.mission_type);
  const note = menuView(menu, shown).note;
  const groups: { name: string; missions: MissionRow[] }[] = [];
  for (const entry of menu.entries) {
    const name = entry.section === "" ? UNGROUPED : entry.section;
    let group = groups.find((g) => g.name === name);
    if (group === undefined) {
      group = { name, missions: [] };
      groups.push(group);
    }
    group.missions.push({
      title: entry.title,
      current: isShown(shown, menu, entry),
      pick:
        entry.available && isMissionType(menu.mission_type)
          ? {
              missionType: menu.mission_type,
              id: entry.id,
              label: `Show ${entry.title}`,
            }
          : null,
    });
  }
  return {
    id: menu.mission_type,
    label,
    count: menu.entries.length,
    note,
    groups,
  };
}

function shownWhere(
  menus: readonly MenuData[],
  shown: Shown | null,
): ShownView | null {
  if (shown === null) return null;
  const menu = menus.find((m) => m.mission_type === shown.mission_type);
  const entry = menu?.entries.find((e) => isShown(shown, menu, e));
  const section = entry?.section ?? "";
  return {
    kind: titleCase(shown.mission_type),
    group: section === "" ? UNGROUPED : section,
    title: shown.title,
  };
}

function installText(state: State): string {
  if (state.install === null) return "Not known yet.";
  if (!state.install.found || state.install.path === null) {
    return "No game install was found. Give one with --install when starting the launcher.";
  }
  return `Game install: ${state.install.path}`;
}

function balanceText(state: State): string {
  if (!state.install?.found) return "Not known yet.";
  return state.install.balance_of_power
    ? "Balance of Power: found."
    : "Balance of Power: not found.";
}

export function describe(state: State): View {
  const menus = state.menus ?? [];
  return {
    connectionText: CONNECTION_TEXT[state.connection],
    showReconnect: state.connection === "closed",
    versionText:
      state.launcherVersion === null
        ? "Launcher version: not known yet."
        : `Launcher version: ${state.launcherVersion}`,
    installText: installText(state),
    balanceText: balanceText(state),
    menus: menus.map((menu) => menuView(menu, state.shown)),
    kinds: menus.map((menu) => kindView(menu, state.shown)),
    shownWhere: shownWhere(menus, state.shown),
    shownType: state.shown?.mission_type ?? null,
    launcherVersion: state.launcherVersion,
    missionsNote:
      state.menus === null ? "The mission lists have not arrived yet." : null,
    artScaling: state.artScaling,
    shownText: state.shown === null ? "" : `Showing ${state.shown.title}`,
    shownCount: state.shownCount,
    briefing: state.briefing,
    briefingNote: BRIEFING_NOTE[state.briefing.status],
    settingsEnabled: state.connection === "open",
    ...battleView(state),
    optionsRows: optionRows(state.options),
    pilotLine: pilotLine(state.pilot),
    pilotBar: pilotBar(state.pilot),
    debrief: debriefView(state.debrief),
    side: describeSide(state.side),
    sideKind: state.side.kind,
    sideFaction: state.side.side,
    sideNote: state.sideNote,
    problem: state.problem,
  };
}
