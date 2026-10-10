import assert from "node:assert/strict";
import { test } from "node:test";

import type { Level, Sink } from "./logger.ts";
import { createLogger, levelFromQuery } from "./logger.ts";

function recordingSink(): { sink: Sink; calls: string[] } {
  const calls: string[] = [];
  const note =
    (level: string) =>
    (...args: unknown[]): void => {
      calls.push(`${level}:${args.map(String).join(",")}`);
    };
  return {
    sink: {
      debug: note("debug"),
      info: note("info"),
      warn: note("warn"),
      error: note("error"),
    },
    calls,
  };
}

function writeAll(level: Level): string[] {
  const { sink, calls } = recordingSink();
  const log = createLogger(level, sink);
  log.debug("d", 1);
  log.info("i");
  log.warn("w");
  log.error("e");
  return calls;
}

test("the query picks the level", () => {
  assert.equal(levelFromQuery("?log=debug"), "debug");
  assert.equal(levelFromQuery("?log=info"), "info");
  assert.equal(levelFromQuery("?a=1&log=error"), "error");
});

test("no query, or an unknown level, gives warn", () => {
  assert.equal(levelFromQuery(""), "warn");
  assert.equal(levelFromQuery("?log=verbose"), "warn");
  assert.equal(levelFromQuery("?log="), "warn");
  assert.equal(levelFromQuery("?other=debug"), "warn");
});

test("debug level writes every call, arguments kept", () => {
  assert.deepEqual(writeAll("debug"), [
    "debug:d,1",
    "info:i",
    "warn:w",
    "error:e",
  ]);
});

test("info level drops debug", () => {
  assert.deepEqual(writeAll("info"), ["info:i", "warn:w", "error:e"]);
});

test("warn level writes warn and error only", () => {
  assert.deepEqual(writeAll("warn"), ["warn:w", "error:e"]);
});

test("error level writes error only", () => {
  assert.deepEqual(writeAll("error"), ["error:e"]);
});

test("debugEnabled is true only at debug", () => {
  const { sink } = recordingSink();
  assert.equal(createLogger("debug", sink).debugEnabled, true);
  assert.equal(createLogger("info", sink).debugEnabled, false);
  assert.equal(createLogger("warn", sink).level, "warn");
});
