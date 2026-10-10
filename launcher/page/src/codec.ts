import type {
  ErrorReply,
  HelloRequest,
  InstallStatusRequest,
  LauncherControlMessage,
  MenuData,
  MenuEntryData,
  MissionsListRequest,
  OkReply,
  Settings,
  SettingsChangedPush,
  SettingsGetRequest,
  SettingsSetRequest,
  StatusPush,
} from "./generated/control.ts";

export type Message = LauncherControlMessage;
export type Request =
  | HelloRequest
  | InstallStatusRequest
  | MissionsListRequest
  | SettingsGetRequest
  | SettingsSetRequest;
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

function resultOf(value: unknown): Parsed<OkReply["result"]> {
  if (!isRecord(value)) return fail("a result must be an object");
  if (hasExactly(value, ["launcher_version", "schema_revision"])) {
    const { launcher_version, schema_revision } = value;
    if (typeof launcher_version === "string" && schema_revision === 1) {
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
    const { launcher_version, install_found } = data;
    if (
      !hasExactly(data, ["launcher_version", "install_found"]) ||
      typeof launcher_version !== "string" ||
      typeof install_found !== "boolean"
    ) {
      return { ok: false, reason: "status data has the wrong fields" };
    }
    const push: StatusPush = {
      event,
      data: { launcher_version, install_found },
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
