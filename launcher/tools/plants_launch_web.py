"""The planted faults of the page's battle and game options: codec, state, wiring, markup.

Purpose:
    Break one rule of the launch and option parts of ``page/`` per plant:
    what the codec accepts and refuses for the new commands, replies and
    pushes, how the state turns them into the battle and options the page
    says, the wiring of Cancel, and the markup.

Flow:
    ``plant_page_web`` applies each plant alone: write ``new`` over ``old``
    in ``path``, run ``npm test`` (and build and ``npm run test:browser``
    when the plant names a browser test or plants the page itself), restore,
    and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across the table, all starting ``web-launch-``.
    - Each ``old`` text occurs exactly once in its file.
    - ``expect`` entries are ``node: <title>`` or ``browser: <title>`` and
      match by prefix.

Call:
    ``from plants_launch_web import LAUNCH_WEB_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

COD = "page/src/codec.ts"
STA = "page/src/state.ts"
MAI = "page/src/main.ts"
HTM = "page/public/index.html"
N = "node: "
B = "browser: "
ACC = N + "accepted example "
REF = N + "refused example "
REQ = N + "a launch or option request with a value off the list is refused"
REQ_OK = N + "the launch and option requests are read back as written"
RES_BAD = N + "a launch or option reply with a wrong value is refused"
RES_OK = N + "the launch and option replies are read back as written"
PUSH = N + "the three new pushes are read back as written and bad ones refused"
SET_ALL = N + "options.set takes every choice of every option and nothing else"
REP_ALL = N + "an options reply holds every choice of every option and nothing else"


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return the plant ``web-launch-<pid>``."""
    return Plant(f"web-launch-{pid}", path, old, new, expect)


CODEC_PLANTS: list[Plant] = [
    plant(
        "player-any-text",
        COD,
        'return typeof value === "string" && PLAYER.test(value);',
        'return typeof value === "string";',
        REQ,
    ),
    plant(
        "side-any",
        COD,
        'return sided ? side === "rebel" || side === "imperial" : side === "none";',
        "return true;",
        REQ,
    ),
    plant(
        "pilot-long",
        COD,
        "pilot.length > 12",
        "pilot.length > 120",
        REQ,
        REF + "request-launch-solo-long-pilot",
    ),
    plant(
        "pilot-empty",
        COD,
        "pilot.length < 1 ||",
        "pilot.length < 0 ||",
        REQ,
        REF + "request-launch-solo-empty-pilot",
    ),
    plant(
        "mission-long", COD, "mission.length > 64 ||", "mission.length > 640 ||", REQ
    ),
    plant(
        "solo-extra-field",
        COD,
        'const names = ["player", "mission_type", "side", "mission", "pilot"];\n      if (\n        !hasExactly(args, names) ||',
        'const names = ["player", "mission_type", "side", "mission", "pilot"];\n      if (\n        false ||',
        REQ,
        REF + "request-launch-solo-extra-engine",
    ),
    plant(
        "cancel-zero",
        COD,
        "!isWhole(launchId, 1) ||",
        "!isWhole(launchId, 0) ||",
        REQ,
        REF + "request-launch-cancel-zero",
    ),
    plant(
        "menu-side-unfit",
        COD,
        '        !isSide(side) ||\n        !sideFits(mission_type, side)\n      ) {\n        return { ok: false, reason: "launch.menu',
        '        !isSide(side)\n      ) {\n        return { ok: false, reason: "launch.menu',
        REQ,
    ),
    plant(
        "options-set-any-value",
        COD,
        "!isOptionName(name) ||\n        (text !== null && !OPTION_TEXTS[name].some((known) => known === text))",
        "!isOptionName(name) ||\n        false",
        REQ,
        SET_ALL,
        REF + "request-options-set-number",
    ),
    plant(
        "options-set-any-name",
        COD,
        "        !isOptionName(name) ||\n",
        "",
        REQ,
        REF + "request-options-set-bad-name",
    ),
    plant(
        "options-get-any-player",
        COD,
        '(player !== null && !isPlayer(player))\n      ) {\n        return { ok: false, reason: "options.get',
        'false\n      ) {\n        return { ok: false, reason: "options.get',
        REQ,
    ),
    plant(
        "options-time-limit-19", COD, "length: 20 }", "length: 19 }", SET_ALL, REP_ALL
    ),
    plant(
        "options-values-open",
        COD,
        "if (text !== null && !OPTION_TEXTS[name].some((known) => known === text)) {\n      return fail(",
        "if (false) {\n      return fail(",
        RES_BAD,
        REP_ALL,
        REF + "reply-ok-options-bad-value",
    ),
    plant(
        "solo-menu-side-unfit",
        COD,
        "    !isSide(side) ||\n    !sideFits(mission_type, side) ||\n    typeof game_path",
        "    !isSide(side) ||\n    typeof game_path",
        RES_BAD,
    ),
    plant(
        "pilots-engine-flag-unchecked",
        COD,
        '      typeof engine_given !== "boolean"\n',
        "      false\n",
        RES_BAD,
    ),
    plant(
        "pilots-not-text",
        COD,
        '      !pilots.every((p) => typeof p === "string")',
        "      false",
        RES_BAD,
    ),
    plant(
        "launch-result-negative",
        COD,
        "      !isWhole(launch_id, 0) ||\n      launch_id > MAX_ID ||\n      typeof message",
        "      launch_id > MAX_ID ||\n      typeof message",
        RES_BAD,
        REF + "reply-ok-launch-solo-negative-id",
    ),
    plant(
        "cancel-result-flag",
        COD,
        'if (typeof cancelled !== "boolean" || typeof message !== "string") {',
        "if (false) {",
        RES_BAD,
    ),
    plant(
        "options-result-player",
        COD,
        'if ((player !== null && !isPlayer(player)) || "reason" in kept) {',
        'if ("reason" in kept) {',
        RES_BAD,
    ),
    plant(
        "options-result-values",
        COD,
        'if ((player !== null && !isPlayer(player)) || "reason" in kept) {',
        "if (player !== null && !isPlayer(player)) {",
        RES_BAD,
        REP_ALL,
    ),
    plant(
        "started-id-zero",
        COD,
        "      !hasExactly(data, names) ||\n      !isWhole(launch_id, 1) ||",
        "      !hasExactly(data, names) ||\n      !isWhole(launch_id, 0) ||",
        REF + "push-launch-started-id-zero",
    ),
    plant(
        "ended-id-zero",
        COD,
        '      !hasExactly(data, ["launch_id", "status", "line"]) ||\n      !isWhole(launch_id, 1) ||',
        '      !hasExactly(data, ["launch_id", "status", "line"]) ||\n      !isWhole(launch_id, 0) ||',
        PUSH,
    ),
    plant(
        "ended-any-status",
        COD,
        "      known === undefined ||\n      typeof line",
        "      typeof line",
        PUSH,
        REF + "push-launch-ended-bad-status",
    ),
    plant(
        "options-changed-values",
        COD,
        '      (player !== null && !isPlayer(player)) ||\n      "reason" in kept\n    ) {\n      return { ok: false, reason: "options.changed',
        '      (player !== null && !isPlayer(player))\n    ) {\n      return { ok: false, reason: "options.changed',
        PUSH,
    ),
    plant(
        "revision",
        COD,
        "schema_revision === 6",
        "schema_revision === 5",
        ACC + "reply-ok-hello.json",
    ),
    plant(
        "reply-shapes-merged",
        COD,
        'hasExactly(value, ["found", "pilots", "engine_given"])',
        'hasExactly(value, ["found", "pilotz", "engine_given"])',
        RES_OK,
        ACC + "reply-ok-pilots-list",
    ),
]

STATE_PLANTS: list[Plant] = [
    plant(
        "cancel-after-end",
        STA,
        "      if (state.launch?.ending !== null) return state;",
        "      if (state.launch === null) return state;",
        N + "pressing Cancel with nothing flying changes nothing",
    ),
    plant(
        "ended-for-any-battle",
        STA,
        "      if (current?.started.launch_id !== message.data.launch_id) return state;",
        "      if (current === null) return state;",
        N + "an ending for another battle is ignored",
    ),
    plant(
        "ending-kept-on-new-battle",
        STA,
        "launch: { started: message.data, cancelling: false, ending: null },",
        "launch: { started: message.data, cancelling: false, ending: state.launch?.ending ?? null },",
        N + "a new battle clears the last ending",
    ),
    plant(
        "cancel-enabled-when-closed",
        STA,
        'cancelEnabled: !cancelling && state.connection === "open",',
        "cancelEnabled: !cancelling,",
        N + "a dropped connection stops Cancel",
    ),
    plant(
        "cancel-shown-after-end",
        STA,
        "      battleEnding: ending,\n      cancelEnabled: false,\n      cancelShown: false,",
        "      battleEnding: ending,\n      cancelEnabled: false,\n      cancelShown: true,",
        N + "the ending line replaces Cancel",
    ),
    plant(
        "cancel-twice",
        STA,
        "cancelEnabled: !cancelling &&",
        "cancelEnabled: true ||",
        N + "Cancel can be pressed once",
    ),
    plant(
        "note-no-battle",
        STA,
        '"No battle is flying. Battles start in Discord."',
        '"Nothing is flying."',
        N + "with no battle the page says",
        B + "with no battle the page says so",
    ),
    plant(
        "label-time-limit",
        STA,
        'mission_time_limit:\n    "Mission time limit (multiplayer only: no effect in solo play)",',
        'mission_time_limit: "Time limit",',
        N + "the options reply fills the rows",
        B + "the game options are shown read-only",
    ),
    plant(
        "battle-length-words",
        STA,
        'if (name === "battle_length") return `${value} wins`;',
        'if (name === "battle_length") return value;',
        N + "the options reply fills the rows",
    ),
    plant(
        "minute-plural",
        STA,
        'return value === "1" ? "1 minute" : `${value} minutes`;',
        "return `${value} minutes`;",
        N + "the options reply fills the rows",
    ),
    plant(
        "no-limit-words",
        STA,
        'none: "No limit",',
        'none: "None",',
        N + "options.changed redraws the rows",
    ),
    plant(
        "unset-words",
        STA,
        'if (value === null) return "As the game has it";',
        'if (value === null) return "";',
        N + "the options reply fills the rows",
    ),
    plant(
        "options-reply-ignored",
        STA,
        'if ("options" in result) return { ...state, options: result.options };',
        'if ("options" in result) return state;',
        N + "the options reply fills the rows",
    ),
    plant(
        "options-push-ignored",
        STA,
        "return { ...state, options: message.data.options, problem: null };",
        "return state;",
        N + "options.changed redraws the rows",
        B + "the game options are shown read-only",
    ),
    plant(
        "cancel-refusal-hidden",
        STA,
        "return result.cancelled ? state : { ...state, notice: result.message };",
        "return state;",
        N + "a refused cancel is shown",
    ),
    plant(
        "started-ignored",
        STA,
        "launch: { started: message.data, cancelling: false, ending: null },\n        notice: null,",
        "launch: state.launch,\n        notice: null,",
        N + "a started battle is shown with Cancel",
        B + "a battle started from a second socket",
    ),
    plant(
        "ended-line-lost",
        STA,
        "          ending: message.data.line,",
        '          ending: "",',
        N + "the ending line replaces Cancel",
        B + "a battle that ends by itself",
    ),
    plant(
        "battle-line-words",
        STA,
        "const line = `${started.title}, flown by ${started.pilot}.`;",
        "const line = `${started.title}.`;",
        N + "a started battle is shown with Cancel",
        B + "a battle started from a second socket",
    ),
]

WIRING_PLANTS: list[Plant] = [
    plant(
        "options-not-asked",
        MAI,
        '    send({ command: "options.get", args: { player: null } });\n',
        "",
        B + "with no battle the page says so",
    ),
    plant(
        "cancel-not-wired",
        MAI,
        'cancelButton.addEventListener("click", onCancel);',
        "",
        B + "a battle started from a second socket",
        B + "Cancel works from the keyboard",
        B + "a page opened while a battle flies",
    ),
    plant(
        "cancel-wrong-launch",
        MAI,
        "args: { launch_id: launch.started.launch_id },",
        "args: { launch_id: launch.started.launch_id + 1 },",
        B + "a battle started from a second socket",
    ),
    plant(
        "cancel-focus-lost",
        MAI,
        "  battleStrip.focus();\n",
        "",
        "browser: Cancel works from the keyboard",
    ),
    plant(
        "cancel-always-shown",
        MAI,
        "cancelButton.hidden = !view.cancelShown;",
        "cancelButton.hidden = false;",
        B + "with no battle the page says so",
        B + "a battle that ends by itself",
    ),
    plant(
        "ending-always-shown",
        MAI,
        "battleEnding.hidden = view.battleEnding === null;",
        "battleEnding.hidden = false;",
        B + "with no battle the page says so",
    ),
    plant(
        "note-not-live",
        HTM,
        '        role="status"\n        tabindex="-1"\n      >',
        '        tabindex="-1"\n      >',
        "browser: Cancel works from the keyboard",
    ),
    plant(
        "note-not-focusable",
        HTM,
        '        role="status"\n        tabindex="-1"\n      >',
        '        role="status"\n      >',
        "browser: Cancel works from the keyboard",
    ),
    plant(
        "a-launch-button",
        HTM,
        '<p id="mission-shown" class="mission-shown" aria-live="polite"></p>',
        '<button class="button" type="button">Launch a battle</button>\n      <p id="mission-shown" class="mission-shown" aria-live="polite"></p>',
        "browser: no control on the page starts a battle",
    ),
    plant(
        "options-editable",
        HTM,
        '<dl class="options-list" id="options-list"></dl>',
        '<dl class="options-list" id="options-list"></dl>\n                  <input type="text" aria-label="Difficulty" />',
        "browser: no control on the page starts a battle",
        "browser: the game options are shown read-only",
    ),
    plant(
        "battle-menu-not-tabbable",
        HTM,
        'aria-controls="panel-battle"',
        'aria-controls="panel-battle"\n                tabindex="-1"',
        "browser: a keyboard alone reaches every control",
    ),
    plant(
        "options-menu-not-tabbable",
        HTM,
        'aria-controls="panel-options"',
        'aria-controls="panel-options"\n                tabindex="-1"',
        "browser: a keyboard alone reaches every control",
    ),
    plant(
        "options-unlabelled",
        HTM,
        '                    id="options"\n                    aria-labelledby="options-heading"\n',
        '                    id="options"\n',
        "browser: the game options are shown read-only",
    ),
    plant(
        "cancel-no-label",
        HTM,
        "          Cancel the battle\n",
        "          Go\n",
        B + "a battle started from a second socket",
        B + "Cancel works from the keyboard",
    ),
]

MORE_PLANTS: list[Plant] = [
    plant(
        "case-pilots-list",
        COD,
        'case "pilots.list":',
        'case "pilots.lis":',
        ACC + "request-pilots-list.json",
        REQ_OK,
    ),
    plant(
        "case-launch-menu",
        COD,
        'case "launch.menu": {',
        'case "launch.men": {',
        ACC + "request-launch-menu",
        REQ_OK,
    ),
    plant(
        "case-launch-solo",
        COD,
        'case "launch.solo": {',
        'case "launch.sol": {',
        ACC + "request-launch-solo.json",
        REQ_OK,
    ),
    plant(
        "case-launch-cancel",
        COD,
        'case "launch.cancel": {',
        'case "launch.cance": {',
        ACC + "request-launch-cancel.json",
        REQ_OK,
    ),
    plant(
        "case-options-get",
        COD,
        'case "options.get": {',
        'case "options.ge": {',
        ACC + "request-options-get",
        REQ_OK,
    ),
    plant(
        "case-options-set",
        COD,
        'case "options.set": {',
        'case "options.se": {',
        ACC + "request-options-set",
        REQ_OK,
    ),
    plant(
        "menu-type-free",
        COD,
        '        !hasExactly(args, ["mission_type", "side"]) ||\n        !isMissionType(mission_type) ||\n',
        '        !hasExactly(args, ["mission_type", "side"]) ||\n',
        REF + "request-launch-menu-bad-type.json",
    ),
    plant(
        "solo-side-free",
        COD,
        "        !isSide(side) ||\n        !sideFits(mission_type, side) ||\n        typeof mission",
        "        typeof mission",
        REF + "request-launch-solo-bad-side.json",
    ),
    plant(
        "options-get-any-args",
        COD,
        'if (\n        !hasExactly(args, ["player"]) ||\n        (player !== null && !isPlayer(player))\n      ) {',
        "if (false) {",
        REF + "request-options-get-no-player.json",
        REQ,
    ),
    plant(
        "shape-launch-result",
        COD,
        'hasExactly(value, ["started", "launch_id", "message"])',
        'hasExactly(value, ["started", "launch_ids", "message"])',
        ACC + "reply-ok-launch-solo",
        RES_OK,
    ),
    plant(
        "shape-cancel-result",
        COD,
        'hasExactly(value, ["cancelled", "message"])',
        'hasExactly(value, ["cancelled", "messages"])',
        ACC + "reply-ok-launch-cancel.json",
        RES_OK,
    ),
    plant(
        "shape-options-result",
        COD,
        'hasExactly(value, ["player", "options"])',
        'hasExactly(value, ["player", "option"])',
        ACC + "reply-ok-options",
        RES_OK,
    ),
    plant(
        "shape-solo-menu",
        COD,
        '      "game_path",\n      "resolved",\n      "entries",\n    ])\n  ) {\n    return soloMenuOf',
        '      "game_paths",\n      "resolved",\n      "entries",\n    ])\n  ) {\n    return soloMenuOf',
        ACC + "reply-ok-launch-menu",
        RES_OK,
    ),
    plant(
        "push-started-event",
        COD,
        'if (event === "launch.started") {',
        'if (event === "launch.starte") {',
        ACC + "push-launch-started.json",
        PUSH,
    ),
    plant(
        "push-ended-event",
        COD,
        'if (event === "launch.ended") {',
        'if (event === "launch.ende") {',
        ACC + "push-launch-ended",
        PUSH,
    ),
    plant(
        "push-options-event",
        COD,
        'if (event === "options.changed") {',
        'if (event === "options.change") {',
        ACC + "push-options-changed.json",
        PUSH,
    ),
    plant(
        "options-rows-empty",
        STA,
        "  if (options === null) return null;",
        "  if (options === null) return [];",
        N + "the options are not shown until they arrive",
    ),
    plant(
        "options-label-column-unwrapped",
        "page/public/style.css",
        "grid-template-columns: fit-content(50%) 1fr;",
        "grid-template-columns: max-content 1fr;",
        B + "with a menu open nothing scrolls sideways at 320 wide",
    ),
]

LAUNCH_WEB_PLANTS: list[Plant] = [
    *CODEC_PLANTS,
    *STATE_PLANTS,
    *WIRING_PLANTS,
    *MORE_PLANTS,
]
