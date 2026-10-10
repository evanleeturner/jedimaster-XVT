import type { ArtScaling, Message } from "./codec.ts";
import type {
  InstallStatusResult,
  MenuData,
  OkReply,
  ShownMission,
} from "./generated/control.ts";

export type Connection = "connecting" | "open" | "closed";

export interface State {
  readonly connection: Connection;
  readonly launcherVersion: string | null;
  readonly install: InstallStatusResult | null;
  readonly menus: readonly MenuData[] | null;
  readonly artScaling: ArtScaling | null;
  readonly shown: ShownMission | null;
  readonly shownCount: number;
  readonly problem: string | null;
}

export type Action =
  | { readonly kind: "connecting" }
  | { readonly kind: "opened" }
  | { readonly kind: "closed" }
  | { readonly kind: "message"; readonly message: Message };

export interface MenuEntryView {
  readonly text: string;
  readonly current: boolean;
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
  readonly settingsEnabled: boolean;
  readonly problem: string | null;
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
    problem: null,
  };
}

function applyResult(state: State, result: OkReply["result"]): State {
  if ("schema_revision" in result) {
    return { ...state, launcherVersion: result.launcher_version };
  }
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
        problem: null,
      };
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
    case "message":
      return applyMessage(state, action.message);
  }
}

const CONNECTION_TEXT: Record<Connection, string> = {
  connecting: "Connecting to the launcher.",
  open: "Connected to the launcher.",
  closed: "The connection to the launcher has dropped.",
};

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
    settingsEnabled: state.connection === "open",
    problem: state.problem,
  };
}
