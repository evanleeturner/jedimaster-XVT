import { decodeBundle } from "./briefing/bundle.ts";
import type {
  BriefingGetRequest,
  ErrorReply,
  GameOptions,
  HelloRequest,
  InstallStatusRequest,
  LaunchCancelRequest,
  LaunchEndedPush,
  LauncherControlMessage,
  LaunchMenuRequest,
  LaunchSoloRequest,
  LaunchStartedPush,
  MenuData,
  MenuEntryData,
  MissionsListRequest,
  OkReply,
  OptionsChangedPush,
  OptionsGetRequest,
  OptionsSetRequest,
  PilotChangedPush,
  PilotsListRequest,
  Settings,
  SettingsChangedPush,
  SettingsGetRequest,
  SettingsSetRequest,
  ShowMissionPush,
  ShowMissionRequest,
  ShownMission,
  StatusPush,
} from "./generated/control.ts";

export type Message = LauncherControlMessage;
export type Request =
  | HelloRequest
  | InstallStatusRequest
  | MissionsListRequest
  | SettingsGetRequest
  | SettingsSetRequest
  | ShowMissionRequest
  | BriefingGetRequest
  | LaunchMenuRequest
  | PilotsListRequest
  | LaunchSoloRequest
  | LaunchCancelRequest
  | OptionsGetRequest
  | OptionsSetRequest;
export type Decoded =
  | { readonly ok: true; readonly message: Message }
  | { readonly ok: false; readonly reason: string };

export const MAX_ID = 2 ** 31 - 1;
export const ART_SCALING_VALUES = [
  "whole_pixels",
  "engine_fit",
  "sharp_bilinear",
] as const;
export type ArtScaling = Settings["art_scaling"];
export const MISSION_TYPES = [
  "training",
  "melee",
  "tournament",
  "combat",
  "battle",
  "campaign",
] as const;
export type MissionType = ShownMission["mission_type"];
export const SIDES = ["rebel", "imperial", "none"] as const;
export const OPTION_NAMES = [
  "difficulty",
  "collisions",
  "craft_jumping",
  "random_setup",
  "battle_length",
  "mission_time_limit",
  "continue_sequence",
  "sith",
] as const;
export type OptionName = (typeof OPTION_NAMES)[number];
export const LAUNCH_STATUSES = [
  "flown",
  "refused",
  "not_reached",
  "failed",
  "cancelled",
  "error",
] as const;
const OPTION_TEXTS: Record<OptionName, readonly string[]> = {
  difficulty: ["easy", "medium", "hard", "easy_cheat"],
  collisions: ["on", "off"],
  craft_jumping: ["on", "off"],
  random_setup: ["on", "off"],
  battle_length: ["2", "3", "4"],
  mission_time_limit: [
    "none",
    "default",
    ...Array.from({ length: 20 }, (_, i) => String(i + 1)),
  ],
  continue_sequence: ["restart", "continue"],
  sith: ["none", "apprentices", "lord"],
};
const PLAYER = /^[0-9]{1,20}$/;
const ERROR_CODES = [
  "bad_message",
  "unknown_command",
  "bad_arguments",
] as const;

type Fields = Record<string, unknown>;
type Parsed<T> = { readonly value: T } | { readonly reason: string };

function isRecord(value: unknown): value is Fields {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function hasExactly(value: Fields, names: readonly string[]): boolean {
  const keys = Object.keys(value);
  return keys.length === names.length && names.every((n) => n in value);
}

function isWhole(value: unknown, low: number): value is number {
  return typeof value === "number" && Number.isInteger(value) && value >= low;
}

function isList(value: unknown): value is unknown[] {
  return Array.isArray(value);
}

export function isArtScaling(value: unknown): value is ArtScaling {
  return ART_SCALING_VALUES.some((known) => known === value);
}

export function isMissionType(value: unknown): value is MissionType {
  return MISSION_TYPES.some((known) => known === value);
}

export function isOptionName(value: unknown): value is OptionName {
  return OPTION_NAMES.some((known) => known === value);
}

function isSide(value: unknown): value is (typeof SIDES)[number] {
  return SIDES.some((known) => known === value);
}

function isPlayer(value: unknown): value is string {
  return typeof value === "string" && PLAYER.test(value);
}

function sideFits(type: MissionType, side: string): boolean {
  const sided = type === "training" || type === "combat" || type === "campaign";
  return sided ? side === "rebel" || side === "imperial" : side === "none";
}

function fail<T>(reason: string): Parsed<T> {
  return { reason };
}

function settingsOf(value: unknown): Parsed<Settings> {
  if (!isRecord(value) || !hasExactly(value, ["art_scaling"])) {
    return fail("settings must be exactly art_scaling");
  }
  const scaling = value.art_scaling;
  if (!isArtScaling(scaling)) return fail("art_scaling is not on the list");
  return { value: { art_scaling: scaling } };
}

function entryOf(value: unknown): Parsed<MenuEntryData> {
  const names = ["section", "id", "available", "file", "title"];
  if (!isRecord(value) || !hasExactly(value, names)) {
    return fail("a menu entry has the wrong fields");
  }
  const { section, id, available, file, title } = value;
  if (
    typeof section !== "string" ||
    !isWhole(id, Number.MIN_SAFE_INTEGER) ||
    typeof available !== "boolean" ||
    typeof file !== "string" ||
    typeof title !== "string"
  ) {
    return fail("a menu entry has a value of the wrong type");
  }
  return { value: { section, id, available, file, title } };
}

function menuOf(value: unknown): Parsed<MenuData> {
  const names = ["mission_type", "game_path", "resolved", "entries"];
  if (!isRecord(value) || !hasExactly(value, names)) {
    return fail("a menu has the wrong fields");
  }
  const { mission_type, game_path, resolved, entries } = value;
  if (
    typeof mission_type !== "string" ||
    typeof game_path !== "string" ||
    typeof resolved !== "boolean" ||
    !isList(entries)
  ) {
    return fail("a menu has a value of the wrong type");
  }
  const kept: MenuEntryData[] = [];
  for (const item of entries) {
    const entry = entryOf(item);
    if ("reason" in entry) return fail(entry.reason);
    kept.push(entry.value);
  }
  return {
    value: { mission_type, game_path, resolved, entries: kept },
  };
}

function optionsOf(value: unknown): Parsed<GameOptions> {
  if (!isRecord(value) || !hasExactly(value, OPTION_NAMES)) {
    return fail("options must be exactly the eight option names");
  }
  const kept: Record<string, string> = {};
  for (const name of OPTION_NAMES) {
    const text = value[name];
    if (!OPTION_TEXTS[name].some((known) => known === text)) {
      return fail(`${name} is not one of its choices`);
    }
    kept[name] = text as string;
  }
  return { value: kept as unknown as GameOptions };
}

function soloMenuOf(value: Fields): Parsed<OkReply["result"]> {
  const { mission_type, side, game_path, resolved, entries } = value;
  if (
    !isMissionType(mission_type) ||
    !isSide(side) ||
    !sideFits(mission_type, side) ||
    typeof game_path !== "string" ||
    typeof resolved !== "boolean" ||
    !isList(entries)
  ) {
    return fail("a solo list has the wrong values");
  }
  const kept = [];
  for (const item of entries) {
    const names = ["section", "file", "title"];
    if (!isRecord(item) || !hasExactly(item, names)) {
      return fail("a solo entry has the wrong fields");
    }
    const { section, file, title } = item;
    if (
      typeof section !== "string" ||
      typeof file !== "string" ||
      typeof title !== "string"
    ) {
      return fail("a solo entry has a value of the wrong type");
    }
    kept.push({ section, file, title });
  }
  return {
    value: { mission_type, side, game_path, resolved, entries: kept },
  };
}

function resultOf(value: unknown): Parsed<OkReply["result"]> {
  if (!isRecord(value)) return fail("a result must be an object");
  if (hasExactly(value, ["launcher_version", "schema_revision"])) {
    const { launcher_version, schema_revision } = value;
    if (typeof launcher_version === "string" && schema_revision === 8) {
      return { value: { launcher_version, schema_revision } };
    }
    return fail("a hello result has the wrong values");
  }
  if (hasExactly(value, ["found", "path", "balance_of_power"])) {
    const { found, path, balance_of_power } = value;
    if (
      typeof found === "boolean" &&
      (typeof path === "string" || path === null) &&
      typeof balance_of_power === "boolean"
    ) {
      return { value: { found, path, balance_of_power } };
    }
    return fail("an install result has the wrong values");
  }
  if (hasExactly(value, ["menus"])) {
    const { menus } = value;
    if (!isList(menus)) return fail("menus must be a list");
    const kept: MenuData[] = [];
    for (const item of menus) {
      const menu = menuOf(item);
      if ("reason" in menu) return fail(menu.reason);
      kept.push(menu.value);
    }
    return { value: { menus: kept } };
  }
  if (hasExactly(value, ["settings"])) {
    const settings = settingsOf(value.settings);
    return "reason" in settings
      ? fail(settings.reason)
      : { value: { settings: settings.value } };
  }
  if (hasExactly(value, ["found", "bundle"])) {
    const { found, bundle } = value;
    if (typeof found !== "boolean") return fail("found must be true or false");
    if (bundle === null) return { value: { found, bundle: null } };
    const checked = decodeBundle(bundle);
    if ("reason" in checked) return fail(checked.reason);
    return { value: { found, bundle: checked.value } };
  }
  if (
    hasExactly(value, [
      "mission_type",
      "side",
      "game_path",
      "resolved",
      "entries",
    ])
  ) {
    return soloMenuOf(value);
  }
  if (hasExactly(value, ["found", "pilots", "engine_given"])) {
    const { found, pilots, engine_given } = value;
    if (
      typeof found !== "boolean" ||
      !isList(pilots) ||
      !pilots.every((p) => typeof p === "string") ||
      typeof engine_given !== "boolean"
    ) {
      return fail("a pilots result has the wrong values");
    }
    return { value: { found, pilots: pilots, engine_given } };
  }
  if (hasExactly(value, ["started", "launch_id", "message"])) {
    const { started, launch_id, message } = value;
    if (
      typeof started !== "boolean" ||
      !isWhole(launch_id, 0) ||
      launch_id > MAX_ID ||
      typeof message !== "string"
    ) {
      return fail("a launch result has the wrong values");
    }
    return { value: { started, launch_id, message } };
  }
  if (hasExactly(value, ["cancelled", "message"])) {
    const { cancelled, message } = value;
    if (typeof cancelled !== "boolean" || typeof message !== "string") {
      return fail("a cancel result has the wrong values");
    }
    return { value: { cancelled, message } };
  }
  if (hasExactly(value, ["player", "options"])) {
    const { player, options } = value;
    const kept = optionsOf(options);
    if ((player !== null && !isPlayer(player)) || "reason" in kept) {
      return fail("an options result has the wrong values");
    }
    return { value: { player, options: kept.value } };
  }
  if (hasExactly(value, ["shown"])) {
    const { shown } = value;
    if (typeof shown !== "boolean") return fail("shown must be true or false");
    return { value: { shown } };
  }
  return fail("a result matches no command's answer");
}

function replyOf(value: Fields): Decoded {
  const { id, ok } = value;
  if (!isWhole(id, 0) || id > MAX_ID) {
    return { ok: false, reason: "a reply id is a whole number from 0" };
  }
  if (ok === true && hasExactly(value, ["id", "ok", "result"])) {
    const result = resultOf(value.result);
    if ("reason" in result) return { ok: false, reason: result.reason };
    const reply: OkReply = { id, ok, result: result.value };
    return { ok: true, message: reply };
  }
  if (ok === false && hasExactly(value, ["id", "ok", "error"])) {
    const error = value.error;
    if (!isRecord(error) || !hasExactly(error, ["code", "message"])) {
      return { ok: false, reason: "an error has the wrong fields" };
    }
    const { code, message } = error;
    const known = ERROR_CODES.find((c) => c === code);
    if (known === undefined || typeof message !== "string") {
      return { ok: false, reason: "an error has the wrong values" };
    }
    const reply: ErrorReply = { id, ok, error: { code: known, message } };
    return { ok: true, message: reply };
  }
  return {
    ok: false,
    reason: "a reply is ok with a result, or not ok with an error",
  };
}

function pushOf(value: Fields): Decoded {
  if (!hasExactly(value, ["event", "data"])) {
    return { ok: false, reason: "a push is exactly event and data" };
  }
  const { event, data } = value;
  if (!isRecord(data))
    return { ok: false, reason: "push data must be an object" };
  if (event === "status") {
    const { launcher_version, install_found, pilot } = data;
    if (
      !hasExactly(data, ["launcher_version", "install_found", "pilot"]) ||
      typeof launcher_version !== "string" ||
      typeof install_found !== "boolean" ||
      (pilot !== null && typeof pilot !== "string")
    ) {
      return { ok: false, reason: "status data has the wrong fields" };
    }
    const push: StatusPush = {
      event,
      data: { launcher_version, install_found, pilot },
    };
    return { ok: true, message: push };
  }
  if (event === "settings.changed") {
    if (!hasExactly(data, ["settings"])) {
      return { ok: false, reason: "settings.changed data is exactly settings" };
    }
    const settings = settingsOf(data.settings);
    if ("reason" in settings) return { ok: false, reason: settings.reason };
    const push: SettingsChangedPush = {
      event,
      data: { settings: settings.value },
    };
    return { ok: true, message: push };
  }
  if (event === "page.show_mission") {
    const names = ["mission_type", "id", "title"];
    const { mission_type, id, title } = data;
    if (
      !hasExactly(data, names) ||
      !isMissionType(mission_type) ||
      !isWhole(id, 0) ||
      id > MAX_ID ||
      typeof title !== "string"
    ) {
      return { ok: false, reason: "page.show_mission data has wrong fields" };
    }
    const push: ShowMissionPush = {
      event,
      data: { mission_type, id, title },
    };
    return { ok: true, message: push };
  }
  if (event === "launch.started") {
    const names = [
      "launch_id",
      "mission_type",
      "side",
      "mission",
      "title",
      "pilot",
    ];
    const { launch_id, mission_type, side, mission, title, pilot } = data;
    if (
      !hasExactly(data, names) ||
      !isWhole(launch_id, 1) ||
      launch_id > MAX_ID ||
      !isMissionType(mission_type) ||
      !isSide(side) ||
      typeof mission !== "string" ||
      typeof title !== "string" ||
      typeof pilot !== "string"
    ) {
      return { ok: false, reason: "launch.started data has wrong fields" };
    }
    const push: LaunchStartedPush = {
      event,
      data: { launch_id, mission_type, side, mission, title, pilot },
    };
    return { ok: true, message: push };
  }
  if (event === "launch.ended") {
    const { launch_id, status, line } = data;
    const known = LAUNCH_STATUSES.find((s) => s === status);
    if (
      !hasExactly(data, ["launch_id", "status", "line"]) ||
      !isWhole(launch_id, 1) ||
      launch_id > MAX_ID ||
      known === undefined ||
      typeof line !== "string"
    ) {
      return { ok: false, reason: "launch.ended data has wrong fields" };
    }
    const push: LaunchEndedPush = {
      event,
      data: { launch_id, status: known, line },
    };
    return { ok: true, message: push };
  }
  if (event === "options.changed") {
    const { player, options } = data;
    const kept = optionsOf(options);
    if (
      !hasExactly(data, ["player", "options"]) ||
      (player !== null && !isPlayer(player)) ||
      "reason" in kept
    ) {
      return { ok: false, reason: "options.changed data has wrong fields" };
    }
    const push: OptionsChangedPush = {
      event,
      data: { player, options: kept.value },
    };
    return { ok: true, message: push };
  }
  if (event === "pilot.changed") {
    const { pilot } = data;
    if (
      !hasExactly(data, ["pilot"]) ||
      (pilot !== null && typeof pilot !== "string")
    ) {
      return { ok: false, reason: "pilot.changed data has wrong fields" };
    }
    const push: PilotChangedPush = { event, data: { pilot } };
    return { ok: true, message: push };
  }
  return { ok: false, reason: "no such push" };
}

function requestOf(value: Fields): Decoded {
  const { id, command, args } = value;
  if (!hasExactly(value, ["id", "command", "args"]) || !isRecord(args)) {
    return { ok: false, reason: "a request is exactly id, command, args" };
  }
  if (!isWhole(id, 1) || id > MAX_ID) {
    return { ok: false, reason: "a request id is a whole number from 1" };
  }
  const empty = Object.keys(args).length === 0;
  switch (command) {
    case "hello": {
      const version = args.page_version;
      if (
        !hasExactly(args, ["page_version"]) ||
        typeof version !== "string" ||
        version.length < 1 ||
        version.length > 64
      ) {
        return {
          ok: false,
          reason: "hello takes page_version, 1 to 64 characters",
        };
      }
      return {
        ok: true,
        message: { id, command, args: { page_version: version } },
      };
    }
    case "install.status":
    case "missions.list":
    case "settings.get":
      return empty
        ? { ok: true, message: { id, command, args: {} } }
        : { ok: false, reason: `${command} takes no arguments` };
    case "settings.set": {
      const { name, value: setting } = args;
      if (
        !hasExactly(args, ["name", "value"]) ||
        name !== "art_scaling" ||
        !isArtScaling(setting)
      ) {
        return {
          ok: false,
          reason: "settings.set takes a setting and a value",
        };
      }
      return {
        ok: true,
        message: { id, command, args: { name, value: setting } },
      };
    }
    case "briefing.get":
    case "page.show_mission": {
      const { mission_type, id: mission } = args;
      if (
        !hasExactly(args, ["mission_type", "id"]) ||
        !isMissionType(mission_type) ||
        !isWhole(mission, 0) ||
        mission > MAX_ID
      ) {
        return {
          ok: false,
          reason: `${command} takes a mission type and an id`,
        };
      }
      return {
        ok: true,
        message: { id, command, args: { mission_type, id: mission } },
      };
    }
    case "pilots.list":
      return empty
        ? { ok: true, message: { id, command, args: {} } }
        : { ok: false, reason: `${command} takes no arguments` };
    case "launch.menu": {
      const { mission_type, side } = args;
      if (
        !hasExactly(args, ["mission_type", "side"]) ||
        !isMissionType(mission_type) ||
        !isSide(side) ||
        !sideFits(mission_type, side)
      ) {
        return { ok: false, reason: "launch.menu takes a kind and its side" };
      }
      return {
        ok: true,
        message: { id, command, args: { mission_type, side } },
      };
    }
    case "launch.solo": {
      const { player, mission_type, side, mission, pilot } = args;
      const names = ["player", "mission_type", "side", "mission", "pilot"];
      if (
        !hasExactly(args, names) ||
        !isPlayer(player) ||
        !isMissionType(mission_type) ||
        !isSide(side) ||
        !sideFits(mission_type, side) ||
        typeof mission !== "string" ||
        mission.length < 1 ||
        mission.length > 64 ||
        typeof pilot !== "string" ||
        pilot.length < 1 ||
        pilot.length > 12
      ) {
        return { ok: false, reason: "launch.solo takes a battle and a pilot" };
      }
      return {
        ok: true,
        message: {
          id,
          command,
          args: { player, mission_type, side, mission, pilot },
        },
      };
    }
    case "launch.cancel": {
      const launchId = args.launch_id;
      if (
        !hasExactly(args, ["launch_id"]) ||
        !isWhole(launchId, 1) ||
        launchId > MAX_ID
      ) {
        return { ok: false, reason: "launch.cancel takes a launch id" };
      }
      return {
        ok: true,
        message: { id, command, args: { launch_id: launchId } },
      };
    }
    case "options.get": {
      const player = args.player;
      if (
        !hasExactly(args, ["player"]) ||
        (player !== null && !isPlayer(player))
      ) {
        return { ok: false, reason: "options.get takes a player or null" };
      }
      return { ok: true, message: { id, command, args: { player } } };
    }
    case "options.set": {
      const { player, name, value: text } = args;
      if (
        !hasExactly(args, ["player", "name", "value"]) ||
        !isPlayer(player) ||
        !isOptionName(name) ||
        !OPTION_TEXTS[name].some((known) => known === text)
      ) {
        return {
          ok: false,
          reason: "options.set takes an option and a choice",
        };
      }
      return {
        ok: true,
        message: {
          id,
          command,
          args: { player, name, value: text as string },
        },
      };
    }
    default:
      return { ok: false, reason: "no such command" };
  }
}

export function decodeValue(value: unknown): Decoded {
  if (!isRecord(value)) {
    return { ok: false, reason: "a message must be an object" };
  }
  if ("event" in value) return pushOf(value);
  if ("ok" in value) return replyOf(value);
  if ("command" in value) return requestOf(value);
  return { ok: false, reason: "a message is a request, a reply or a push" };
}

export function decode(text: string): Decoded {
  let value: unknown;
  try {
    value = JSON.parse(text);
  } catch {
    return { ok: false, reason: "the message is not JSON" };
  }
  return decodeValue(value);
}

export function encodeRequest(request: Request): string {
  return JSON.stringify(request);
}
