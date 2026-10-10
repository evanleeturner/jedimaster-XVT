export type Level = "debug" | "info" | "warn" | "error";

export interface Sink {
  debug: (...args: unknown[]) => void;
  info: (...args: unknown[]) => void;
  warn: (...args: unknown[]) => void;
  error: (...args: unknown[]) => void;
}

export interface Logger {
  readonly level: Level;
  readonly debugEnabled: boolean;
  debug: (...args: unknown[]) => void;
  info: (...args: unknown[]) => void;
  warn: (...args: unknown[]) => void;
  error: (...args: unknown[]) => void;
}

const LEVELS: readonly Level[] = ["debug", "info", "warn", "error"];
const DEFAULT_LEVEL: Level = "warn";

function isLevel(value: string | null): value is Level {
  return LEVELS.some((level) => level === value);
}

export function levelFromQuery(search: string): Level {
  const value = new URLSearchParams(search).get("log");
  return isLevel(value) ? value : DEFAULT_LEVEL;
}

export function createLogger(level: Level, sink: Sink): Logger {
  const floor = LEVELS.indexOf(level);
  const gate = (
    own: Level,
    write: (...args: unknown[]) => void,
  ): ((...args: unknown[]) => void) =>
    LEVELS.indexOf(own) >= floor
      ? (...args) => {
          write(...args);
        }
      : () => undefined;
  return {
    level,
    debugEnabled: floor === 0,
    debug: gate("debug", (...args) => {
      sink.debug(...args);
    }),
    info: gate("info", (...args) => {
      sink.info(...args);
    }),
    warn: gate("warn", (...args) => {
      sink.warn(...args);
    }),
    error: gate("error", (...args) => {
      sink.error(...args);
    }),
  };
}

function currentSearch(): string {
  return "location" in globalThis ? globalThis.location.search : "";
}

export const logger: Logger = createLogger(
  levelFromQuery(currentSearch()),
  console,
);
