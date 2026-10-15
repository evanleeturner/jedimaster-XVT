"""The planted faults of the page's state, wiring, markup and styles.

Purpose:
    Break one rule of ``page/src/state.ts``, ``main.ts``, ``public/index.html``
    or ``public/style.css`` per plant: what the page says for each message,
    its wiring to the socket, its markup (landmarks, names, focus order) and
    its styles (focus, contrast, motion, target size).

Flow:
    ``plants_page_web`` joins this table with the codec and logger tables;
    ``plant_page_web`` applies each plant alone: write ``new`` over ``old``
    in ``path``, run ``npm test`` (and build and ``npm run test:browser``
    when the plant names a browser test or plants the page itself), restore,
    and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across both tables, all starting ``web-``.
    - Each ``old`` text occurs exactly once in its file.
    - Every plant still compiles (``tsc`` passes), so a browser plant is
      tested against the build it planted.
    - ``expect`` entries are ``node: <title>`` or ``browser: <title>``.

Call:
    ``from plants_page_web_view import VIEW_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

STA = "page/src/state.ts"
MAI = "page/src/main.ts"
HTM = "page/public/index.html"
CSS = "page/public/style.css"
N = "node: "
B = "browser: "

STATE_PLANTS: list[Plant] = [
    Plant(
        "web-state-reconnect-shown-early",
        STA,
        '    showReconnect: state.connection === "closed",',
        '    showReconnect: state.connection !== "open",',
        (N + "a new page is connecting and knows nothing",),
    ),
    Plant(
        "web-state-reconnect-never",
        STA,
        '    showReconnect: state.connection === "closed",',
        "    showReconnect: false,",
        (N + "a dropped socket says so",),
    ),
    Plant(
        "web-state-settings-open-when-closed",
        STA,
        '    settingsEnabled: state.connection === "open",',
        '    settingsEnabled: state.connection !== "connecting",',
        (N + "a dropped socket says so",),
    ),
    Plant(
        "web-state-settings-locked",
        STA,
        '    settingsEnabled: state.connection === "open",',
        "    settingsEnabled: false,",
        (N + "an open socket says connected",),
    ),
    Plant(
        "web-state-connecting-text",
        STA,
        '  connecting: "Connecting to the launcher.",',
        '  connecting: "Connected to the launcher.",',
        (N + "a new page is connecting and knows nothing",),
    ),
    Plant(
        "web-state-open-text",
        STA,
        '  open: "Connected to the launcher.",',
        '  open: "Connecting to the launcher.",',
        (N + "an open socket says connected",),
    ),
    Plant(
        "web-state-closed-text",
        STA,
        '  closed: "The connection to the launcher has dropped.",',
        '  closed: "The connection to the launcher is closed.",',
        (N + "a dropped socket says so",),
    ),
    Plant(
        "web-state-closed-forgets",
        STA,
        '      return { ...state, connection: "closed" };',
        '      return { ...initialState(), connection: "closed" };',
        (N + "reconnecting clears the offer",),
    ),
    Plant(
        "web-state-connecting-keeps-problem",
        STA,
        '      return { ...state, connection: "connecting", problem: null };',
        '      return { ...state, connection: "connecting" };',
        (N + "reconnecting clears the offer",),
    ),
    Plant(
        "web-state-opened-keeps-problem",
        STA,
        '      return { ...state, connection: "open", problem: null };',
        '      return { ...state, connection: "open" };',
        (N + "an open socket clears a problem",),
    ),
    Plant(
        "web-state-status-push-ignored",
        STA,
        "        launcherVersion: message.data.launcher_version,\n        pilot: message.data.pilot,\n        problem: null,",
        "        pilot: message.data.pilot,\n        problem: null,",
        (N + "the status push gives the launcher's version",),
    ),
    Plant(
        "web-state-hello-ignored",
        STA,
        "    return { ...state, launcherVersion: result.launcher_version };",
        "    return state;",
        (N + "a hello reply gives the launcher's version",),
    ),
    Plant(
        "web-state-install-ignored",
        STA,
        '  if ("found" in result) return { ...state, install: result };',
        '  if ("found" in result) return state;',
        (N + "an install that was found shows",),
    ),
    Plant(
        "web-state-menus-ignored",
        STA,
        '  if ("menus" in result) return { ...state, menus: result.menus };',
        '  if ("menus" in result) return state;',
        (N + "missions list one heading per menu",),
    ),
    Plant(
        "web-state-settings-reply-ignored",
        STA,
        "  return { ...state, artScaling: result.settings.art_scaling };",
        "  return state;",
        (N + "a settings reply and a settings.changed push",),
    ),
    Plant(
        "web-state-settings-push-ignored",
        STA,
        "      artScaling: message.data.settings.art_scaling,\n      problem: null,",
        "      problem: null,",
        (N + "a settings reply and a settings.changed push",),
    ),
    Plant(
        "web-state-refusal-silent",
        STA,
        "      problem: `The launcher refused a request: ${message.error.message}.`,",
        "      problem: null,",
        (N + "a refusal shows a problem",),
    ),
    Plant(
        "web-state-problem-kept",
        STA,
        "      return applyResult({ ...state, problem: null }, message.result);",
        "      return applyResult(state, message.result);",
        (N + "a refusal shows a problem",),
    ),
    Plant(
        "web-state-opened-mutates",
        STA,
        '      return { ...state, connection: "open", problem: null };',
        '      return Object.assign(state, { connection: "open", problem: null });',
        (N + "reduce returns a new state",),
    ),
    Plant(
        "web-state-requests-change-it",
        STA,
        "  return state;\n}\n\nexport function reduce",
        '  return { ...state, problem: "request" };\n}\n\nexport function reduce',
        (N + "a request message changes nothing",),
    ),
    Plant(
        "web-state-availability-swapped",
        STA,
        '    const word = entry.available ? "available" : "not available";',
        '    const word = entry.available ? "not available" : "available";',
        (N + "missions list one heading per menu",),
    ),
    Plant(
        "web-state-availability-by-colour-only",
        STA,
        "      text: `${place}${entry.title} (${entry.file}), ${word}`,",
        "      text: `${place}${entry.title} (${entry.file})`,",
        (N + "missions list one heading per menu",),
    ),
    Plant(
        "web-state-section-always-shown",
        STA,
        '    const place = entry.section === "" ? "" : `${entry.section}: `;',
        "    const place = `${entry.section}: `;",
        (N + "missions list one heading per menu",),
    ),
    Plant(
        "web-state-heading-lowercase",
        STA,
        "  return name.charAt(0).toUpperCase() + name.slice(1);",
        "  return name;",
        (N + "missions list one heading per menu",),
    ),
    Plant(
        "web-state-unresolved-silent",
        STA,
        "      note: `The game's list for this kind of mission was not found (${menu.game_path}).`,",
        "      note: null,",
        (N + "missions list one heading per menu",),
    ),
    Plant(
        "web-state-empty-menu-silent",
        STA,
        '    note: entries.length === 0 ? "This list has no missions." : null,',
        "    note: null,",
        (N + "a menu that resolved but is empty says so",),
    ),
    Plant(
        "web-state-missions-note-stays",
        STA,
        '      state.menus === null ? "The mission lists have not arrived yet." : null,',
        '      "The mission lists have not arrived yet.",',
        (N + "missions list one heading per menu",),
    ),
    Plant(
        "web-state-no-install-silent",
        STA,
        '    return "No game install was found. Give one with --install when starting the launcher.";',
        '    return "Not known yet.";',
        (N + "an install that was not found says so",),
    ),
    Plant(
        "web-state-install-path-hidden",
        STA,
        "  return `Game install: ${state.install.path}`;",
        '  return "Game install found.";',
        (N + "an install that was found shows",),
    ),
    Plant(
        "web-state-balance-swapped",
        STA,
        '    ? "Balance of Power: found."\n    : "Balance of Power: not found.";',
        '    ? "Balance of Power: not found."\n    : "Balance of Power: found.";',
        (N + "an install that was found shows",),
    ),
    Plant(
        "web-state-balance-known-without-install",
        STA,
        '  if (!state.install?.found) return "Not known yet.";',
        '  if (state.install === null) return "Not known yet.";',
        (N + "an install that was not found says so",),
    ),
    Plant(
        "web-state-version-text",
        STA,
        "        : `Launcher version: ${state.launcherVersion}`,",
        "        : `Version: ${state.launcherVersion}`,",
        (N + "the status push gives the launcher's version",),
    ),
    Plant(
        "web-state-unknown-version-text",
        STA,
        '        ? "Launcher version: not known yet."',
        '        ? "Launcher version: none."',
        (N + "a new page is connecting and knows nothing",),
    ),
]

SITE_PLANTS: list[Plant] = [
    Plant(
        "web-server-wrong-key-accepted",
        "jedimaster/page/server.py",
        '        if not _same(request.query["key"], secret):',
        "        if False:",
        (B + "the page is refused without the key",),
    ),
    Plant(
        "web-server-host-unchecked",
        "jedimaster/page/server.py",
        '    if request.headers.get("Host", "") not in _allowed_hosts(request):',
        "    if False:",
        (B + "the page is refused without the key",),
    ),
    Plant(
        "web-server-no-key-needed",
        "jedimaster/page/server.py",
        '    if not _same(request.cookies.get(COOKIE_NAME, ""), secret):',
        "    if False:",
        (B + "the page is refused without the key",),
    ),
    Plant(
        "web-control-shown-not-sent",
        "jedimaster/page/control.py",
        "        pushes = [self._shown] if self._shown is not None else []",
        "        pushes = []",
        (
            B + "a page opened after a mission was shown lands on its briefing",
            B + "with reduced motion the map shows the marker's final box at once",
        ),
    ),
    Plant(
        "web-main-status-not-asked",
        MAI,
        '    send({ command: "install.status", args: {} });\n',
        "",
        (B + "status shows the connection",),
    ),
    Plant(
        "web-main-missions-not-asked",
        MAI,
        '    send({ command: "missions.list", args: {} });\n',
        "",
        (B + "missions list each menu",),
    ),
    Plant(
        "web-main-settings-not-asked",
        MAI,
        '    send({ command: "settings.get", args: {} });\n',
        "",
        (B + "art scaling has three choices",),
    ),
    Plant(
        "web-main-change-not-sent",
        MAI,
        'scalingGroup.addEventListener("change", onScalingChange);\n',
        "",
        (B + "a setting is saved",),
    ),
    Plant(
        "web-main-wrong-protocol",
        MAI,
        '  url.protocol = url.protocol === "https:" ? "wss:" : "ws:";',
        '  url.protocol = "wss:";',
        (B + "the key opens the page",),
    ),
    Plant(
        "web-main-reconnect-unwired",
        MAI,
        'reconnectButton.addEventListener("click", connect);\n',
        "",
        (B + "a dropped connection is said so",),
    ),
    Plant(
        "web-main-reconnect-always-shown",
        MAI,
        "  reconnectButton.hidden = !view.showReconnect;",
        "  reconnectButton.hidden = false;",
        (B + "a dropped connection is said so",),
    ),
    Plant(
        "web-main-reconnect-never-shown",
        MAI,
        "  reconnectButton.hidden = !view.showReconnect;",
        "  reconnectButton.hidden = true;",
        (B + "a dropped connection is said so",),
    ),
    Plant(
        "web-main-settings-stay-enabled",
        MAI,
        "  scalingGroup.disabled = !view.settingsEnabled;",
        "  scalingGroup.disabled = false;",
        (B + "a dropped connection is said so",),
    ),
    Plant(
        "web-main-missions-not-drawn",
        MAI,
        "    missionList.replaceChildren(...rows.map(missionItem));",
        "    missionList.replaceChildren();",
        ("browser: missions list each menu",),
    ),
    Plant(
        "web-main-choice-not-shown",
        MAI,
        "    input.checked = input.value === view.artScaling;",
        "    input.checked = input.value === view.artScaling && false;",
        (B + "art scaling has three choices",),
    ),
    Plant(
        "web-main-problem-always-shown",
        MAI,
        "  problemLine.hidden = view.problem === null;",
        "  problemLine.hidden = false;",
        (B + "status shows the connection",),
    ),
    Plant(
        "web-main-headings-flat",
        HTM,
        "<h3>Kind</h3>",
        "<h5>Kind</h5>",
        ("browser: Missions shows Kind, then Group, then Mission",),
    ),
    Plant(
        "web-main-list-as-text",
        MAI,
        '  const item = document.createElement("li");\n  item.className = "mission-item";',
        '  const item = document.createElement("div");\n  item.className = "mission-item";',
        ("browser: missions list each menu",),
    ),
    Plant(
        "web-main-inline-style",
        MAI,
        "  setText(connectionLine, view.connectionText);",
        '  setText(connectionLine, view.connectionText);\n  connectionLine.style.color = "red";',
        (B + "the page keeps scripts and styles in their files",),
    ),
    Plant(
        "web-html-skip-link-gone",
        HTM,
        '    <a class="skip-link" href="#main">Skip to the page content</a>\n',
        "",
        (B + "a keyboard alone reaches every control", B + "the skip link lands"),
    ),
    Plant(
        "web-html-main-not-focusable",
        HTM,
        ' id="main" tabindex="-1">',
        ' id="main">',
        (B + "the skip link lands",),
    ),
    Plant(
        "web-html-language-missing",
        HTM,
        '<html lang="en">',
        "<html>",
        (B + "the key opens the page", B + "axe finds nothing in the light scheme"),
    ),
    Plant(
        "web-html-title-changed",
        HTM,
        "<title>jedimaster launcher</title>",
        "<title>page</title>",
        (B + "the key opens the page",),
    ),
    Plant(
        "web-html-h1-changed",
        HTM,
        "<h1>jedimaster launcher</h1>",
        "<h1>Launcher</h1>",
        (B + "the key opens the page",),
    ),
    Plant(
        "web-html-status-not-live",
        HTM,
        '<p class="connection-line" id="status-connection" role="status">',
        '<p class="connection-line" id="status-connection">',
        ("browser: status shows the connection",),
    ),
    Plant(
        "web-html-region-unnamed",
        HTM,
        '                    id="missions"\n                    aria-labelledby="missions-heading"\n',
        '                    id="missions"\n',
        ("browser: missions list each menu",),
    ),
    Plant(
        "web-html-legend-changed",
        HTM,
        "<legend>Art scaling</legend>",
        "<legend>Scaling</legend>",
        (B + "art scaling has three choices",),
    ),
    Plant(
        "web-html-help-unlinked",
        HTM,
        '              aria-describedby="art-engine-fit-help"\n',
        "",
        (B + "art scaling has three choices",),
    ),
    Plant(
        "web-html-label-unlinked",
        HTM,
        'for="art-engine-fit"',
        'for="art-nothing"',
        (
            "browser: art scaling has three choices",
            "browser: axe finds nothing in the dark scheme",
        ),
    ),
    Plant(
        "web-html-radios-in-two-groups",
        HTM,
        '                          name="art-scaling"\n                          type="radio"\n                          value="engine_fit"',
        '                          name="art-scaling-2"\n                          type="radio"\n                          value="engine_fit"',
        ("browser: a keyboard alone reaches every control",),
    ),
    Plant(
        "web-html-inline-handler",
        HTM,
        '<button class="button" id="reconnect" type="button" hidden>',
        '<button class="button" id="reconnect" type="button" hidden onclick="void 0">',
        (B + "the page keeps scripts and styles in their files",),
    ),
    Plant(
        "web-html-inline-script",
        HTM,
        '    <script type="module" src="main.js"></script>\n',
        '    <script type="module" src="main.js"></script>\n    <script>void 0;</script>\n',
        (B + "the page keeps scripts and styles in their files",),
    ),
    Plant(
        "web-html-pilot-menu-not-tabbable",
        HTM,
        'aria-controls="panel-pilot"',
        'aria-controls="panel-pilot"\n                tabindex="-1"',
        ("browser: a keyboard alone reaches every control",),
    ),
    Plant(
        "web-css-focus-ring-gone",
        CSS,
        "  outline: 3px solid var(--focus);",
        "  outline: 1px solid var(--focus);",
        (B + "a keyboard alone reaches every control",),
    ),
    Plant(
        "web-css-focus-ring-removed",
        CSS,
        ":focus-visible {\n  outline: 3px solid var(--focus);\n  outline-offset: 2px;\n}",
        ":focus-visible {\n  outline: none;\n}",
        (B + "a keyboard alone reaches every control",),
    ),
    Plant(
        "web-css-muted-text-faint-dark",
        CSS,
        "  --text-muted: #a3b1c2;",
        "  --text-muted: #4a4f59;",
        ("browser: axe finds nothing in the dark scheme",),
    ),
    Plant(
        "web-css-soft-text-faint-dark",
        CSS,
        "  --text-soft: #c9d4e1;",
        "  --text-soft: #3a4a5e;",
        ("browser: axe finds nothing in the dark scheme",),
    ),
    Plant(
        "web-css-dark-scheme-missing",
        CSS,
        "  --page-bg: #0a0e13;",
        "  --page-bg: #f0f2f5;",
        (
            "browser: the dark scheme has a dark background",
            "browser: the light scheme still has a dark background",
        ),
    ),
    Plant(
        "web-css-radio-too-small",
        CSS,
        ".choice-input {\n  inline-size: 1.5rem;\n  block-size: 1.5rem;",
        ".choice-input {\n  inline-size: 0.5rem;\n  block-size: 0.5rem;",
        (B + "controls are at least 24 by 24 pixels",),
    ),
    Plant(
        "web-css-button-too-small",
        CSS,
        "  min-block-size: 2.75rem;\n  padding: 0.5rem 1.25rem;",
        "  min-block-size: 0.5rem;\n  padding: 0 0.25rem;\n  block-size: 1rem;",
        (B + "controls are at least 24 by 24 pixels",),
    ),
    Plant(
        "web-css-page-wider-than-phone",
        CSS,
        "  max-inline-size: 90rem;\n  margin-inline: auto;",
        "  min-inline-size: 30rem;\n  max-inline-size: 90rem;\n  margin-inline: auto;",
        ("browser: controls are at least 24 by 24 pixels and the page reflows",),
    ),
    Plant(
        "web-css-motion-always",
        CSS,
        "@media (prefers-reduced-motion: no-preference) {",
        "@media all {",
        (B + "smooth scrolling is only for people",),
    ),
    Plant(
        "web-css-no-smooth-scrolling",
        CSS,
        "    scroll-behavior: smooth;",
        "    scroll-behavior: auto;",
        (B + "smooth scrolling is only for people",),
    ),
    Plant(
        "web-css-skip-link-always-shown",
        CSS,
        "  transform: translateY(-120%);",
        "  transform: none;",
        (B + "the skip link lands",),
    ),
    Plant(
        "web-css-skip-link-stays-hidden",
        CSS,
        ".skip-link:focus-visible {\n  transform: translateY(0);\n}",
        ".skip-link:focus-visible {\n  transform: translateY(-120%);\n}",
        (B + "the skip link lands",),
    ),
    Plant(
        "web-css-hidden-attribute-ignored",
        CSS,
        "[hidden] {\n  display: none;\n}",
        "[hidden] {\n  display: block;\n}",
        (B + "a dropped connection is said so",),
    ),
]

SHOW_VIEW_PLANTS: list[Plant] = [
    Plant(
        "web-state-push-not-kept",
        STA,
        "        shown: message.data,",
        "        shown: null,",
        (N + "a show-mission push marks that entry and announces its title",),
    ),
    Plant(
        "web-state-push-not-counted",
        STA,
        "        shownCount: state.shownCount + 1,",
        "        shownCount: state.shownCount,",
        (N + "a show-mission push marks that entry and announces its title",),
    ),
    Plant(
        "web-state-mark-any-type",
        STA,
        "    shown?.mission_type === menu.mission_type\n      ? menu.entries.findIndex(",
        "    shown !== null\n      ? menu.entries.findIndex(",
        ("node: the mark needs the same type as well as the same id",),
    ),
    Plant(
        "web-state-mark-any-id",
        STA,
        "menu.entries.findIndex((entry) => entry.id === shown.id)",
        "menu.entries.findIndex(() => true)",
        (N + "a show-mission push marks that entry and announces its title",),
    ),
    Plant(
        "web-state-mark-last-repeat",
        STA,
        "menu.entries.findIndex((entry) => entry.id === shown.id)",
        "menu.entries.findLastIndex((entry) => entry.id === shown.id)",
        (N + "when an id repeats inside a menu, the first entry is marked",),
    ),
    Plant(
        "web-state-announce-the-type",
        STA,
        "`Showing ${state.shown.title}`",
        "`Showing ${state.shown.mission_type}`",
        (N + "a show-mission push marks that entry and announces its title",),
    ),
    Plant(
        "web-state-announce-before-a-push",
        STA,
        '    shownText: state.shown === null ? "" :',
        '    shownText: state.shown === null ? "Nothing" :',
        (N + "before any push, nothing is marked and nothing is announced",),
    ),
    Plant(
        "web-state-result-clears-the-mark",
        STA,
        '  if ("shown" in result) return state;',
        '  if ("shown" in result) return { ...state, shown: null };',
        (N + "a show-mission result changes nothing",),
    ),
    Plant(
        "web-state-mark-needs-lists-first",
        STA,
        "    shown: message.data,",
        "    shown: state.menus === null ? null : message.data,",
        (N + "the mark appears once the lists arrive after the push",),
    ),
    Plant(
        "web-main-mark-never-set",
        MAI,
        '    if (current) item.setAttribute("aria-current", "true");',
        '    if (current) item.removeAttribute("aria-current");',
        ("browser: a mission shown from a second socket",),
    ),
    Plant(
        "web-main-mark-says-page",
        MAI,
        '    if (current) item.setAttribute("aria-current", "true");',
        '    if (current) item.setAttribute("aria-current", "page");',
        ("browser: a mission shown from a second socket",),
    ),
    Plant(
        "web-main-no-scroll",
        MAI,
        "    missionList.scrollTop = mark.offsetTop - missionList.clientHeight / 2;",
        "    void mark;",
        ("browser: a mission shown from a second socket",),
    ),
    Plant(
        "web-main-focus-moves",
        MAI,
        '  setText(shownTitle, where?.title ?? "");',
        '  setText(shownTitle, where?.title ?? "");\n  if (where !== null) {\n    shownTitle.setAttribute("tabindex", "-1");\n    shownTitle.focus();\n  }',
        ("browser: a mission shown from a second socket",),
    ),
    Plant(
        "web-main-nothing-announced",
        MAI,
        "  setText(shownLine, view.shownText);\n",
        "",
        (B + "a mission shown from a second socket",),
    ),
    Plant(
        "web-html-announcement-silent",
        HTM,
        '<p id="mission-shown" class="mission-shown" aria-live="polite"></p>',
        '<p id="mission-shown" class="mission-shown"></p>',
        (B + "a mission shown from a second socket",),
    ),
    Plant(
        "web-html-announcement-assertive",
        HTM,
        '<p id="mission-shown" class="mission-shown" aria-live="polite"></p>',
        '<p id="mission-shown" class="mission-shown" aria-live="assertive"></p>',
        (B + "a mission shown from a second socket",),
    ),
    Plant(
        "web-css-mark-by-colour-alone",
        CSS,
        '  content: "\\25B6\\00a0" / "";',
        "  content: none;",
        (B + "a mission shown from a second socket",),
    ),
]

MENU_PLANTS: list[Plant] = [
    Plant(
        "web-main-escape-ignored",
        MAI,
        'event.key === "Escape" && openPanel',
        'event.key === "Escapes" && openPanel',
        ("browser: Escape closes the open menu and returns focus to its button",),
    ),
    Plant(
        "web-main-escape-no-refocus",
        MAI,
        'if (event.key === "Escape" && openPanel !== "") closePanel(true);',
        'if (event.key === "Escape" && openPanel !== "") closePanel(false);',
        ("browser: Escape closes the open menu and returns focus to its button",),
    ),
    Plant(
        "web-main-pick-no-refocus",
        MAI,
        "    closePanel(true);\n  }\n}\n\nfunction onCancel",
        "    closePanel(false);\n  }\n}\n\nfunction onCancel",
        (
            "browser: Missions shows Kind, then Group, then Mission, and marks the shown one",
        ),
    ),
    Plant(
        "web-main-pick-keeps-menu-open",
        MAI,
        "    closePanel(true);\n  }\n}\n\nfunction onCancel",
        "  }\n}\n\nfunction onCancel",
        (
            "browser: Missions shows Kind, then Group, then Mission, and marks the shown one",
        ),
    ),
    Plant(
        "web-main-two-panels-open",
        MAI,
        "    const open = panel?.id === id;",
        '    const open = panel?.id === id || (id !== "" && panel?.hidden === false);',
        ("browser: only one menu panel is open at a time",),
    ),
    Plant(
        "web-main-expanded-never-set",
        MAI,
        '    button.setAttribute("aria-expanded", String(open));',
        "    void open;",
        ("browser: aria-expanded on each menu button follows its panel",),
    ),
    Plant(
        "web-main-expanded-always-true",
        MAI,
        '    button.setAttribute("aria-expanded", String(open));',
        '    button.setAttribute("aria-expanded", "true");',
        ("browser: aria-expanded on each menu button follows its panel",),
    ),
    Plant(
        "web-main-outside-click-ignored",
        MAI,
        'document.addEventListener("click", onDocumentClick);\n',
        "",
        ("browser: a click outside closes the open menu without moving focus",),
    ),
    Plant(
        "web-main-outside-click-moves-focus",
        MAI,
        'if (target.closest(".dd, #open-missions") === null) closePanel(false);',
        'if (target.closest(".dd, #open-missions") === null) closePanel(true);',
        ("browser: a click outside closes the open menu without moving focus",),
    ),
    Plant(
        "web-main-click-inside-closes",
        MAI,
        'target.closest(".dd, #open-missions") === null',
        'target.closest("#open-missions") === null',
        ("browser: missions list each menu",),
    ),
    Plant(
        "web-html-live-region-in-panel",
        HTM,
        '<p id="status-version">',
        '<p id="status-version" aria-live="polite">',
        ("browser: no live region sits inside a menu panel",),
    ),
    Plant(
        "web-html-alert-in-panel",
        HTM,
        '<p id="status-install">',
        '<p id="status-install" role="alert">',
        ("browser: no live region sits inside a menu panel",),
    ),
    Plant(
        "web-html-status-role-in-panel",
        HTM,
        '<p id="status-balance">',
        '<p id="status-balance" role="status">',
        ("browser: no live region sits inside a menu panel",),
    ),
    Plant(
        "web-html-options-list-live",
        HTM,
        '<dl class="options-list" id="options-list"></dl>',
        '<dl class="options-list" id="options-list" aria-live="polite"></dl>',
        ("browser: no live region sits inside a menu panel",),
    ),
    Plant(
        "web-html-problem-not-alert",
        HTM,
        '<p class="problem" id="problem" role="alert" hidden></p>',
        '<p class="problem" id="problem" hidden></p>',
        ("browser: no live region sits inside a menu panel",),
    ),
    Plant(
        "web-css-missions-columns-stacked",
        CSS,
        ".missions-body {\n  display: flex;\n  flex-wrap: wrap;",
        ".missions-body {\n  display: flex;\n  flex-direction: column;\n  flex-wrap: wrap;",
        (
            "browser: Missions shows Kind, then Group, then Mission, and marks the shown one",
        ),
    ),
    Plant(
        "web-html-group-heading-renamed",
        HTM,
        "<h3>Group</h3>",
        "<h3>Section</h3>",
        (
            "browser: Missions shows Kind, then Group, then Mission, and marks the shown one",
        ),
    ),
    Plant(
        "web-main-kind-not-pressed",
        MAI,
        "      String(button.dataset.kind === pickedKind),",
        '      "false",',
        (
            "browser: Missions shows Kind, then Group, then Mission, and marks the shown one",
        ),
    ),
    Plant(
        "web-main-group-heading-fixed",
        MAI,
        'setText(groupHeading, pickedGroup === "" ? "Mission" : pickedGroup);',
        'setText(groupHeading, "Mission");',
        (
            "browser: Missions shows Kind, then Group, then Mission, and marks the shown one",
        ),
    ),
    Plant(
        "web-state-empty-note-changed",
        STA,
        'none: "Pick a mission to see its briefing.",',
        'none: "Pick one.",',
        (
            "browser: with nothing shown the centre pane asks for a pick and Open Missions opens the menu",
        ),
    ),
    Plant(
        "web-main-open-missions-dead",
        MAI,
        'openMissionsButton.addEventListener("click", onOpenMissions);\n',
        "",
        (
            "browser: with nothing shown the centre pane asks for a pick and Open Missions opens the menu",
        ),
    ),
    Plant(
        "web-main-open-missions-no-focus",
        MAI,
        '  openerOf("panel-missions")?.focus();\n',
        "",
        (
            "browser: with nothing shown the centre pane asks for a pick and Open Missions opens the menu",
        ),
    ),
    Plant(
        "web-main-shown-heading-always",
        MAI,
        "  shownHeading.hidden = where === null;",
        "  shownHeading.hidden = false;",
        (
            "browser: with nothing shown the centre pane asks for a pick and Open Missions opens the menu",
        ),
    ),
    Plant(
        "web-main-follow-kind-lost",
        MAI,
        "  pickedKind = view.kinds.find((k) => k.label === where.kind)?.id ?? pickedKind;",
        "  void pickedKind;",
        (
            "browser: a mission shown by the bot moves the centre pane and the Missions mark",
        ),
    ),
    Plant(
        "web-main-follow-group-lost",
        MAI,
        "  pickedGroup = where.group;\n",
        '  pickedGroup = "";\n',
        (
            "browser: a mission shown by the bot moves the centre pane and the Missions mark",
        ),
    ),
    Plant(
        "web-main-follow-never",
        MAI,
        "  if (view.shownCount === syncedFor) return;",
        "  if (view.shownCount >= 0) return;",
        (
            "browser: a mission shown by the bot moves the centre pane and the Missions mark",
        ),
    ),
    Plant(
        "web-main-shown-title-unset",
        MAI,
        '  setText(shownTitle, where?.title ?? "");\n',
        '  setText(shownTitle, "");\n',
        (
            "browser: a mission shown by the bot moves the centre pane and the Missions mark",
        ),
    ),
    Plant(
        "web-main-shown-where-no-kind",
        MAI,
        "`${where.kind} \\u00b7 ${where.group}`",
        "`${where.group}`",
        (
            "browser: a mission shown by the bot moves the centre pane and the Missions mark",
        ),
    ),
    Plant(
        "web-main-strip-always-shown",
        MAI,
        "  stripRow.hidden = !strip;",
        "  stripRow.hidden = false;",
        (
            "browser: the battle strip shows while a battle flies, offers Cancel and shows the ending",
        ),
    ),
    Plant(
        "web-main-strip-tag-fixed",
        MAI,
        'view.battleEnding === null ? "Battle flying" : "Battle over",',
        '"Battle flying",',
        (
            "browser: the battle strip shows while a battle flies, offers Cancel and shows the ending",
        ),
    ),
    Plant(
        "web-main-badge-never",
        MAI,
        "  battleBadge.hidden = !view.cancelShown;",
        "  battleBadge.hidden = true;",
        (
            "browser: the battle strip shows while a battle flies, offers Cancel and shows the ending",
        ),
    ),
    Plant(
        "web-main-strip-line-unset",
        MAI,
        '  setText(battleLine, view.battleLine ?? "");',
        '  setText(battleLine, "");',
        (
            "browser: the battle strip shows while a battle flies, offers Cancel and shows the ending",
        ),
    ),
    Plant(
        "web-css-panels-wider-than-phone",
        CSS,
        "    inset-inline: 1rem;\n    inline-size: auto;\n    max-inline-size: none;",
        "    inset-inline: 1rem;\n    inline-size: 40rem;\n    max-inline-size: none;",
        ("browser: with a menu open nothing scrolls sideways at 320 wide",),
    ),
    Plant(
        "web-css-menu-bar-no-wrap",
        CSS,
        ".nav-list {\n  display: flex;\n  flex-wrap: wrap;",
        ".nav-list {\n  display: flex;\n  flex-wrap: nowrap;",
        ("browser: with a menu open nothing scrolls sideways at 320 wide",),
    ),
    Plant(
        "web-css-row-count-faint",
        CSS,
        "  font-size: 0.8125rem;\n  color: var(--text-muted);\n}\n\n.battle-strip",
        "  font-size: 0.8125rem;\n  color: #2a3a4e;\n}\n\n.battle-strip",
        (
            "browser: axe finds nothing in the dark scheme with each menu open",
            "browser: axe finds nothing in the light scheme with each menu open",
        ),
    ),
    Plant(
        "web-html-menu-controls-dangling",
        HTM,
        'aria-controls="panel-status"',
        'aria-controls="panel-nothing"',
        (
            "browser: axe finds nothing in the dark scheme with each menu open",
            "browser: axe finds nothing in the light scheme with each menu open",
        ),
    ),
    Plant(
        "web-css-briefing-never-stacks",
        CSS,
        "@media (width <= 900px) {\n  .briefing-layout {",
        "@media (width <= 300px) {\n  .briefing-layout {",
        (
            "browser: below 900 wide the briefing text sits under the stage, and beside it when wider",
        ),
    ),
    Plant(
        "web-css-briefing-always-stacks",
        CSS,
        "  grid-template-columns: minmax(0, 1fr) 20rem;",
        "  grid-template-columns: minmax(0, 1fr);",
        (
            "browser: below 900 wide the briefing text sits under the stage, and beside it when wider",
        ),
    ),
    Plant(
        "web-state-kind-count-wrong",
        STA,
        "    count: menu.entries.length,",
        "    count: 0,",
        (
            "node: the kinds list each type with its groups in the game's order and its count",
        ),
    ),
    Plant(
        "web-state-kind-note-lost",
        STA,
        "  const note = menuView(menu, shown).note;",
        "  const note = null;",
        (
            "node: the kinds list each type with its groups in the game's order and its count",
        ),
    ),
    Plant(
        "web-state-kind-ungrouped-nameless",
        STA,
        '    const name = entry.section === "" ? UNGROUPED : entry.section;',
        "    const name = entry.section;",
        (
            "node: the kinds list each type with its groups in the game's order and its count",
        ),
    ),
    Plant(
        "web-state-kind-label-raw",
        STA,
        "  const label = titleCase(menu.mission_type);",
        "  const label = menu.mission_type;",
        (
            "node: the kinds list each type with its groups in the game's order and its count",
        ),
    ),
    Plant(
        "web-state-kind-current-never",
        STA,
        "      current:\n        shown?.mission_type === menu.mission_type && shown.id === entry.id,",
        "      current: false,",
        (
            "node: a mission row has a Show request only when available, and the shown one is current",
        ),
    ),
    Plant(
        "web-state-kind-mark-any-type",
        STA,
        "        shown?.mission_type === menu.mission_type && shown.id === entry.id,",
        "        shown !== null && shown.id === entry.id,",
        (
            "node: a mission row has a Show request only when available, and the shown one is current",
        ),
    ),
    Plant(
        "web-state-kind-locked-button",
        STA,
        "shown.id === entry.id,\n      pick:\n        entry.available && isMissionType(menu.mission_type)",
        "shown.id === entry.id,\n      pick:\n        isMissionType(menu.mission_type)",
        (
            "node: a mission row has a Show request only when available, and the shown one is current",
        ),
    ),
    Plant(
        "web-state-kind-pick-label",
        STA,
        "shown.id === entry.id,\n      pick:\n        entry.available && isMissionType(menu.mission_type)\n          ? {\n              missionType: menu.mission_type,\n              id: entry.id,\n              label: `Show ${entry.title}`",
        "shown.id === entry.id,\n      pick:\n        entry.available && isMissionType(menu.mission_type)\n          ? {\n              missionType: menu.mission_type,\n              id: entry.id,\n              label: `Show ${entry.file}`",
        (
            "node: a mission row has a Show request only when available, and the shown one is current",
        ),
    ),
    Plant(
        "web-state-where-no-kind",
        STA,
        "    kind: titleCase(shown.mission_type),",
        "    kind: shown.mission_type,",
        (
            "node: the shown heading names the kind, the group and the title; it is null before a pick",
        ),
    ),
    Plant(
        "web-state-where-section-lost",
        STA,
        '  const section = entry?.section ?? "";',
        '  const section = "";',
        (
            "node: the shown heading names the kind, the group and the title; it is null before a pick",
        ),
    ),
    Plant(
        "web-state-where-before-pick",
        STA,
        "  if (shown === null) return null;\n  const menu",
        '  if (shown === null) return { kind: "", group: "", title: "" };\n  const menu',
        (
            "node: the shown heading names the kind, the group and the title; it is null before a pick",
        ),
    ),
    Plant(
        "web-state-where-title-lost",
        STA,
        "    title: shown.title,\n  };\n}",
        "    title: shown.mission_type,\n  };\n}",
        (
            "node: the shown heading names the kind, the group and the title; it is null before a pick",
        ),
    ),
]

VIEW_PLANTS: list[Plant] = [
    *STATE_PLANTS,
    *SITE_PLANTS,
    *SHOW_VIEW_PLANTS,
    *MENU_PLANTS,
]
