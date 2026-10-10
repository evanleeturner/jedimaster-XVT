"""The planted faults of the page: codec, state, logger, markup, styles, wiring.

Purpose:
    Break one rule of ``page/`` per plant: what the codec accepts and
    refuses, how the state turns messages into what the page says, the
    logger's gate, the page's wiring to the socket, its markup and its
    styles (focus, contrast, motion, target size).

Flow:
    ``plant_page_web`` applies each plant alone: write ``new`` over ``old``
    in ``path``, run ``npm test`` (and build and ``npm run test:browser``
    when the plant names a browser test or plants the page itself), restore,
    and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across the table, all starting ``web-``.
    - Each ``old`` text occurs exactly once in its file.
    - A plant that a browser test must catch still compiles (``tsc``
      passes), so it is tested against the build it planted; a plant that
      only ``node --test`` judges may break the types, which Node strips.
    - ``expect`` entries are ``node: <title>`` or ``browser: <title>``.

Call:
    ``from plants_page_web import WEB_PLANTS``
"""

from __future__ import annotations

import logging

from plants_briefing_web import BRIEFING_WEB_PLANTS
from plants_common import Plant
from plants_page_web_view import VIEW_PLANTS

logger = logging.getLogger(__name__)

COD = "page/src/codec.ts"
LOG = "page/src/logger.ts"
N = "node: "
ACC = N + "accepted example "
REF = N + "refused example "

CODEC_PLANTS: list[Plant] = [
    Plant(
        "web-codec-non-objects-pass",
        COD,
        "export function decodeValue(value: unknown): Decoded {\n",
        "export function decodeValue(value: unknown): Decoded {\n"
        '  if (typeof value !== "object" || value === null || Array.isArray(value)\n'
        "    || Object.keys(value).length === 0) {\n"
        "    return { ok: true, message: value as never };\n  }\n",
        (
            REF + "message-array.json",
            REF + "message-empty-object.json",
            REF + "message-null.json",
            REF + "message-text.json",
        ),
    ),
    Plant(
        "web-codec-every-push-passes",
        COD,
        "function pushOf(value: Fields): Decoded {\n",
        "function pushOf(value: Fields): Decoded {\n"
        "  return { ok: true, message: value as never };\n",
        (
            REF + "push-status-missing-data.json",
            REF + "push-unknown-event.json",
            N + "a push refuses an unknown event and a wrong payload",
        ),
    ),
    Plant(
        "web-codec-every-reply-passes",
        COD,
        "function replyOf(value: Fields): Decoded {\n",
        "function replyOf(value: Fields): Decoded {\n"
        "  return { ok: true, message: value as never };\n",
        (
            REF + "reply-error-no-message.json",
            REF + "reply-error-unknown-code.json",
            REF + "reply-ok-string.json",
            REF + "reply-ok-wrong-result.json",
        ),
    ),
    Plant(
        "web-codec-every-request-passes",
        COD,
        "function requestOf(value: Fields): Decoded {\n",
        "function requestOf(value: Fields): Decoded {\n"
        "  return { ok: true, message: value as never };\n",
        (
            REF + "request-args-missing.json",
            REF + "request-hello-no-version.json",
            REF + "request-id-fraction.json",
            REF + "request-id-missing.json",
            REF + "request-id-text.json",
            REF + "request-set-missing-value.json",
            REF + "request-set-unknown-name.json",
            REF + "request-unknown-command.json",
        ),
    ),
    Plant(
        "web-contract-folder-empty",
        "page/src/contract.test.ts",
        '    .filter((name) => name.endsWith(".json"))',
        '    .filter((name) => name.endsWith(".txt"))',
        (N + "the example folders are not empty",),
    ),
    Plant(
        "web-codec-request-id-zero",
        COD,
        "  if (!isWhole(id, 1) || id > MAX_ID) {",
        "  if (!isWhole(id, 0) || id > MAX_ID) {",
        (N + "a request refuses a bad id", REF + "request-id-zero.json"),
    ),
    Plant(
        "web-codec-request-id-over-max",
        COD,
        "  if (!isWhole(id, 1) || id > MAX_ID) {",
        "  if (!isWhole(id, 1)) {",
        (N + "a request refuses a bad id", REF + "request-id-too-large.json"),
    ),
    Plant(
        "web-codec-reply-id-over-max",
        COD,
        "  if (!isWhole(id, 0) || id > MAX_ID) {",
        "  if (!isWhole(id, 0)) {",
        (N + "a reply refuses a bad id",),
    ),
    Plant(
        "web-codec-reply-id-negative",
        COD,
        "  if (!isWhole(id, 0) || id > MAX_ID) {",
        "  if (!isWhole(id, -5) || id > MAX_ID) {",
        (N + "a reply refuses a bad id",),
    ),
    Plant(
        "web-codec-reply-id-zero-refused",
        COD,
        "  if (!isWhole(id, 0) || id > MAX_ID) {",
        "  if (!isWhole(id, 1) || id > MAX_ID) {",
        (
            N + "an error reply is read back as written",
            ACC + "reply-error-bad-message.json",
        ),
    ),
    Plant(
        "web-codec-extra-fields-allowed",
        COD,
        "  return keys.length === names.length && names.every((n) => n in value);",
        "  return names.every((n) => n in value) && keys.length >= 0;",
        (N + "a request refuses missing, extra", REF + "request-extra-field.json"),
    ),
    Plant(
        "web-codec-array-is-a-record",
        COD,
        '  return typeof value === "object" && value !== null && !Array.isArray(value);',
        '  return typeof value === "object" && value !== null;',
        (N + "anything that is not an object is refused",),
    ),
    Plant(
        "web-codec-list-is-anything",
        COD,
        "  return Array.isArray(value);",
        '  return typeof value === "object";',
        (N + "a reply refuses a result that matches no command",),
    ),
    Plant(
        "web-codec-scaling-value-dropped",
        COD,
        '  "engine_fit",\n  "sharp_bilinear",\n] as const;',
        '  "engine_fit",\n] as const;',
        (ACC + "request-settings-set.json", N + "every request is read back"),
    ),
    Plant(
        "web-codec-error-code-dropped",
        COD,
        '  "unknown_command",\n  "bad_arguments",\n] as const;',
        '  "bad_arguments",\n] as const;',
        (ACC + "reply-error-unknown-command.json",),
    ),
    Plant(
        "web-codec-error-message-unchecked",
        COD,
        '    if (known === undefined || typeof message !== "string") {',
        "    if (known === undefined) {",
        (N + "a reply refuses a bad id, a bad flag and a mixed shape",),
    ),
    Plant(
        "web-codec-hello-empty-version",
        COD,
        "        version.length < 1 ||",
        "        version.length < 0 ||",
        (N + "a request refuses arguments its command does not take",),
    ),
    Plant(
        "web-codec-hello-long-version",
        COD,
        "        version.length > 64",
        "        version.length > 65",
        (N + "a request refuses arguments its command does not take",),
    ),
    Plant(
        "web-codec-hello-version-type",
        COD,
        '        typeof version !== "string" ||\n',
        "",
        (N + "a request refuses arguments its command does not take",),
    ),
    Plant(
        "web-codec-no-args-ignored",
        COD,
        "      return empty\n",
        "      return !empty || empty\n",
        (N + "a request refuses arguments its command does not take",),
    ),
    Plant(
        "web-codec-set-name-unchecked",
        COD,
        '        name !== "art_scaling" ||\n',
        "",
        (N + "settings.set refuses a name or value off the list",),
    ),
    Plant(
        "web-codec-set-value-unchecked",
        COD,
        "        !isArtScaling(setting)\n",
        "        setting === undefined\n",
        (N + "settings.set refuses a name or value off the list",),
    ),
    Plant(
        "web-codec-command-case-folded",
        COD,
        "  switch (command) {",
        "  switch (typeof command === 'string' ? command.toLowerCase() : command) {",
        (
            N + "a request refuses an off-list command",
            REF + "request-command-case.json",
        ),
    ),
    Plant(
        "web-codec-hello-revision",
        COD,
        '    if (typeof launcher_version === "string" && schema_revision === 2) {',
        '    if (typeof launcher_version === "string" && schema_revision !== 0) {',
        (
            N + "a reply refuses a result that matches no command",
            REF + "reply-ok-bad-revision.json",
        ),
    ),
    Plant(
        "web-codec-path-never-null",
        COD,
        '      (typeof path === "string" || path === null) &&',
        '      typeof path === "string" &&',
        (ACC + "reply-ok-install-missing.json",),
    ),
    Plant(
        "web-codec-path-any-type",
        COD,
        '      (typeof path === "string" || path === null) &&',
        "      path !== undefined &&",
        (N + "a reply refuses a result that matches no command",),
    ),
    Plant(
        "web-codec-menus-any-entry",
        COD,
        '    const entry = entryOf(item);\n    if ("reason" in entry) return fail(entry.reason);',
        '    const entry = entryOf(item);\n    if ("reason" in entry) continue;',
        (N + "a menu refuses a wrong field or a wrong entry",),
    ),
    Plant(
        "web-codec-entry-id-fraction",
        COD,
        "    !isWhole(id, Number.MIN_SAFE_INTEGER) ||",
        '    typeof id !== "number" ||',
        (N + "a menu refuses a wrong field or a wrong entry",),
    ),
    Plant(
        "web-codec-entry-available-unchecked",
        COD,
        '    typeof available !== "boolean" ||\n',
        "",
        (N + "a menu refuses a wrong field or a wrong entry",),
    ),
    Plant(
        "web-codec-menu-resolved-unchecked",
        COD,
        '    typeof resolved !== "boolean" ||\n    !isList(entries)',
        "    !isList(entries)",
        (N + "a menu refuses a wrong field or a wrong entry",),
    ),
    Plant(
        "web-codec-menus-dropped",
        COD,
        "    return { value: { menus: kept } };",
        "    return { value: { menus: [] } };",
        (N + "every kind of ok reply is read back as written",),
    ),
    Plant(
        "web-codec-settings-unchecked",
        COD,
        '  if (!isArtScaling(scaling)) return fail("art_scaling is not on the list");',
        '  if (scaling === undefined) return fail("art_scaling is not on the list");',
        (N + "a reply refuses a result that matches no command",),
    ),
    Plant(
        "web-codec-status-fields-unchecked",
        COD,
        '      typeof install_found !== "boolean"\n',
        "      false\n",
        (N + "a push refuses an unknown event and a wrong payload",),
    ),
    Plant(
        "web-codec-status-event-renamed",
        COD,
        '  if (event === "status") {',
        '  if (event === "state") {',
        (ACC + "push-status.json",),
    ),
    Plant(
        "web-codec-settings-event-renamed",
        COD,
        '  if (event === "settings.changed") {',
        '  if (event === "settings.updated") {',
        (ACC + "push-settings-changed.json",),
    ),
    Plant(
        "web-codec-push-skipped",
        COD,
        '  if ("event" in value) return pushOf(value);\n',
        "",
        (ACC + "push-status.json",),
    ),
    Plant(
        "web-codec-reply-skipped",
        COD,
        '  if ("ok" in value) return replyOf(value);\n',
        "",
        (ACC + "reply-ok-hello.json",),
    ),
    Plant(
        "web-codec-request-skipped",
        COD,
        '  if ("command" in value) return requestOf(value);\n',
        "",
        (ACC + "request-hello.json",),
    ),
    Plant(
        "web-codec-bad-json-reads-as-empty",
        COD,
        "    value = JSON.parse(text);",
        '    value = JSON.parse(text === "" ? "{}" : text);',
        (N + "decode refuses text that is not JSON",),
    ),
    Plant(
        "web-codec-encode-loses-id",
        COD,
        "  return JSON.stringify(request);",
        "  return JSON.stringify({ ...request, id: 1 });",
        (N + "encodeRequest writes JSON that decode reads back",),
    ),
    Plant(
        "web-codec-ok-flag-loose",
        COD,
        '  if (ok === true && hasExactly(value, ["id", "ok", "result"])) {',
        '  if (ok !== false && hasExactly(value, ["id", "ok", "result"])) {',
        (
            N + "a reply refuses a bad id, a bad flag and a mixed shape",
            REF + "reply-ok-flag-text.json",
        ),
    ),
]

LOGGER_PLANTS: list[Plant] = [
    Plant(
        "web-logger-default-level",
        LOG,
        'const DEFAULT_LEVEL: Level = "warn";',
        'const DEFAULT_LEVEL: Level = "info";',
        (N + "no query, or an unknown level, gives warn",),
    ),
    Plant(
        "web-logger-any-level",
        LOG,
        "  return LEVELS.some((level) => level === value);",
        "  return value !== null;",
        (N + "no query, or an unknown level, gives warn",),
    ),
    Plant(
        "web-logger-query-name",
        LOG,
        '.get("log")',
        '.get("level")',
        (N + "the query picks the level",),
    ),
    Plant(
        "web-logger-gate-strict",
        LOG,
        "    LEVELS.indexOf(own) >= floor",
        "    LEVELS.indexOf(own) > floor",
        (N + "debug level writes every call", N + "error level writes error only"),
    ),
    Plant(
        "web-logger-gate-open",
        LOG,
        "    LEVELS.indexOf(own) >= floor",
        "    LEVELS.indexOf(own) >= 0",
        (N + "info level drops debug", N + "warn level writes warn and error only"),
    ),
    Plant(
        "web-logger-arguments-lost",
        LOG,
        "          write(...args);",
        "          write(args.length);",
        (N + "debug level writes every call, arguments kept",),
    ),
    Plant(
        "web-logger-debug-flag",
        LOG,
        "    debugEnabled: floor === 0,",
        "    debugEnabled: floor <= 1,",
        (N + "debugEnabled is true only at debug",),
    ),
    Plant(
        "web-logger-level-reported",
        LOG,
        "    level,\n    debugEnabled",
        '    level: "debug",\n    debugEnabled',
        (N + "debugEnabled is true only at debug",),
    ),
    Plant(
        "web-logger-info-to-debug",
        LOG,
        "      sink.info(...args);",
        "      sink.debug(...args);",
        (N + "info level drops debug",),
    ),
    Plant(
        "web-logger-warn-to-info",
        LOG,
        "      sink.warn(...args);",
        "      sink.info(...args);",
        (N + "warn level writes warn and error only",),
    ),
    Plant(
        "web-logger-error-to-warn",
        LOG,
        "      sink.error(...args);",
        "      sink.warn(...args);",
        (N + "error level writes error only",),
    ),
    Plant(
        "web-logger-debug-to-info",
        LOG,
        "      sink.debug(...args);",
        "      sink.info(...args);",
        (N + "debug level writes every call, arguments kept",),
    ),
]

SHOW_PLANTS: list[Plant] = [
    Plant(
        "web-codec-hello-promises-revision-1",
        COD,
        '    if (typeof launcher_version === "string" && schema_revision === 2) {',
        '    if (typeof launcher_version === "string" && schema_revision === 1) {',
        (ACC + "reply-ok-hello.json",),
    ),
    Plant(
        "web-codec-mission-types-short",
        COD,
        '  "campaign",\n] as const;\nexport type MissionType',
        "] as const;\nexport type MissionType",
        (ACC + "request-show-mission-id-zero.json",),
    ),
    Plant(
        "web-codec-show-push-type-free",
        COD,
        "      !isMissionType(mission_type) ||\n      !isWhole(id, 0) ||\n      id > MAX_ID ||",
        "      !isWhole(id, 0) ||\n      id > MAX_ID ||",
        (
            REF + "push-show-mission-unknown-type.json",
            N + "a show-mission push refuses a wrong payload",
        ),
    ),
    Plant(
        "web-codec-show-push-negative-id",
        COD,
        "      !isMissionType(mission_type) ||\n      !isWhole(id, 0) ||\n      id > MAX_ID ||",
        "      !isMissionType(mission_type) ||\n      !isWhole(id, -5) ||\n      id > MAX_ID ||",
        (
            REF + "push-show-mission-negative-id.json",
            N + "a show-mission push refuses a wrong payload",
        ),
    ),
    Plant(
        "web-codec-show-push-id-unbounded",
        COD,
        "      !isWhole(id, 0) ||\n      id > MAX_ID ||\n      typeof title",
        "      !isWhole(id, 0) ||\n      typeof title",
        (N + "a show-mission push refuses a wrong payload",),
    ),
    Plant(
        "web-codec-show-push-title-unchecked",
        COD,
        '      id > MAX_ID ||\n      typeof title !== "string"',
        "      id > MAX_ID ||\n      false",
        (N + "a show-mission push refuses a wrong payload",),
    ),
    Plant(
        "web-codec-show-push-extra-field",
        COD,
        "      !hasExactly(data, names) ||",
        "      false ||",
        (N + "a show-mission push refuses a wrong payload",),
    ),
    Plant(
        "web-codec-show-push-unchecked",
        COD,
        "      !hasExactly(data, names) ||\n      !isMissionType(mission_type) ||\n"
        '      !isWhole(id, 0) ||\n      id > MAX_ID ||\n      typeof title !== "string"\n',
        "      false\n",
        (
            REF + "push-show-mission-no-title.json",
            REF + "push-show-mission-unknown-type.json",
            REF + "push-show-mission-negative-id.json",
        ),
    ),
    Plant(
        "web-codec-show-push-title-lost",
        COD,
        "      data: { mission_type, id, title },",
        '      data: { mission_type, id, title: "" },',
        (
            ACC + "push-show-mission.json",
            N + "a push to show a mission is read back as written",
        ),
    ),
    Plant(
        "web-codec-show-request-type-free",
        COD,
        "        !isMissionType(mission_type) ||\n        !isWhole(mission, 0) ||",
        "        !isWhole(mission, 0) ||",
        (
            REF + "request-show-mission-unknown-type.json",
            N + "a show-mission request refuses a wrong type, id or shape",
        ),
    ),
    Plant(
        "web-codec-show-request-negative-id",
        COD,
        "        !isWhole(mission, 0) ||",
        "        !isWhole(mission, -5) ||",
        (
            REF + "request-show-mission-negative-id.json",
            N + "a show-mission request refuses a wrong type, id or shape",
        ),
    ),
    Plant(
        "web-codec-show-request-id-unbounded",
        COD,
        "        !isWhole(mission, 0) ||\n        mission > MAX_ID\n",
        "        !isWhole(mission, 0)\n",
        (
            REF + "request-show-mission-id-too-large.json",
            N + "a show-mission request refuses a wrong type, id or shape",
        ),
    ),
    Plant(
        "web-codec-show-request-extra-argument",
        COD,
        '        !hasExactly(args, ["mission_type", "id"]) ||',
        "        false ||",
        (
            REF + "request-show-mission-extra-argument.json",
            N + "a show-mission request refuses a wrong type, id or shape",
        ),
    ),
    Plant(
        "web-codec-show-request-unlisted",
        COD,
        '    case "page.show_mission": {',
        '    case "page.show_missions": {',
        (
            ACC + "request-show-mission.json",
            N + "every request is read back as written",
        ),
    ),
    Plant(
        "web-codec-show-request-type-lost",
        COD,
        "        message: { id, command, args: { mission_type, id: mission } },",
        '        message: { id, command, args: { mission_type: "training", id: mission } },',
        (N + "every request is read back as written",),
    ),
    Plant(
        "web-codec-show-request-id-lost",
        COD,
        "        message: { id, command, args: { mission_type, id: mission } },",
        "        message: { id, command, args: { mission_type, id: 1 } },",
        (N + "every request is read back as written",),
    ),
    Plant(
        "web-codec-show-result-flag-unchecked",
        COD,
        '    if (typeof shown !== "boolean") return fail("shown must be true or false");\n',
        "",
        (
            REF + "reply-show-mission-shown-text.json",
            N + "a reply refuses a result that matches no command",
        ),
    ),
    Plant(
        "web-codec-show-result-unlisted",
        COD,
        '  if (hasExactly(value, ["shown"])) {',
        "  if (false) {",
        (
            ACC + "reply-ok-show-mission.json",
            N + "every kind of ok reply is read back as written",
        ),
    ),
]

WEB_PLANTS: list[Plant] = [
    *CODEC_PLANTS,
    *LOGGER_PLANTS,
    *VIEW_PLANTS,
    *SHOW_PLANTS,
    *BRIEFING_WEB_PLANTS,
]
