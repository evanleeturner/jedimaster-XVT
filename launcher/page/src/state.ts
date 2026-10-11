import type { ArtScaling, Message, MissionType } from "./codec.ts";
import { isMissionType } from "./codec.ts";
import type { BriefingBundle } from "./generated/briefing.ts";
import type {
  GameOptions,
  InstallStatusResult,
  LaunchStartedData,
  MenuData,
  OkReply,
  ShownMission,
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

export interface State {
  readonly connection: Connection;
  readonly launcherVersion: string | null;
  readonly install: InstallStatusResult | null;
  readonly menus: readonly MenuData[] | null;
  readonly artScaling: ArtScaling | null;
  readonly shown: ShownMission | null;
  readonly shownCount: number;
  readonly briefing: BriefingState;
  readonly launch: LaunchState | null;
  readonly notice: string | null;
  readonly options: GameOptions | null;
  readonly problem: string | null;
}

export type Action =
  | { readonly kind: "connecting" }
  | { readonly kind: "opened" }
  | { readonly kind: "closed" }
  | { readonly kind: "cancelling" }
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

export interface View {
  readonly connectionText: string;
  readonly showReconnect: boolean;
  readonly versionText: string;
  readonly installText: string;
  readonly balanceText: string;
  readonly menus: readonly MenuView[];
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
    problem: null,
  };
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
  if ("pilots" in result || "mission_type" in result) return state;
  if ("started" in result) return state;
  if ("cancelled" in result) {
    return result.cancelled ? state : { ...state, notice: result.message };
  }
  if ("options" in result) return { ...state, options: result.options };
  if ("found" in result) return { ...state, install: result };
  if ("menus" in result) return { ...state, menus: result.menus };
  if ("shown" in result) return state;
  return { ...state, artScaling: result.settings.art_scaling };
}

function applyMessage(state: State, message: Message): State {
  if ("event" in message) {
    if (message.event === "status") {
      return {
        ...state,
        launcherVersion: message.data.launcher_version,
        problem: null,
      };
    }
    if (message.event === "page.show_mission") {
      return {
        ...state,
        shown: message.data,
        shownCount: state.shownCount + 1,
        briefing: { status: "loading" },
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
    case "message":
      return applyMessage(state, action.message);
  }
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

export function optionWords(name: string, value: string | null): string {
  if (value === null) return "As the game has it";
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
];

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

function menuView(menu: MenuData, shown: ShownMission | null): MenuView {
  const heading = `${titleCase(menu.mission_type)} missions`;
  if (!menu.resolved) {
    return {
      heading,
      note: `The game's list for this kind of mission was not found (${menu.game_path}).`,
      entries: [],
    };
  }
  const target =
    shown?.mission_type === menu.mission_type
      ? menu.entries.findIndex((entry) => entry.id === shown.id)
      : -1;
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
    problem: state.problem,
  };
}
