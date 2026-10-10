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
        "        launcherVersion: message.data.launcher_version,\n        problem: null,",
        "        problem: null,",
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
        "    return { text: `${place}${entry.title} (${entry.file}), ${word}` };",
        "    return { text: `${place}${entry.title} (${entry.file})` };",
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
        "  missionsBody.replaceChildren(...view.menus.flatMap(menuElements));",
        "  missionsBody.replaceChildren();",
        (B + "missions list each menu",),
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
        MAI,
        '  const heading = document.createElement("h3");',
        '  const heading = document.createElement("h5");',
        (B + "missions list each menu",),
    ),
    Plant(
        "web-main-list-as-text",
        MAI,
        '      const item = document.createElement("li");',
        '      const item = document.createElement("div");',
        (B + "missions list each menu",),
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
        '<div class="status-region" role="status">',
        '<div class="status-region">',
        (B + "status shows the connection",),
    ),
    Plant(
        "web-html-region-unnamed",
        HTM,
        ' id="missions" aria-labelledby="missions-heading">',
        ' id="missions">',
        (B + "missions list each menu",),
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
        '<label class="choice-label" for="art-engine-fit">Engine fit</label>',
        '<label class="choice-label" for="art-nothing">Engine fit</label>',
        (
            B + "art scaling has three choices",
            B + "axe finds nothing in the light scheme",
        ),
    ),
    Plant(
        "web-html-radios-in-two-groups",
        HTM,
        '              name="art-scaling"\n              type="radio"\n              value="engine_fit"',
        '              name="art-scaling-2"\n              type="radio"\n              value="engine_fit"',
        (B + "a keyboard alone reaches every control",),
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
        "web-html-nav-link-lost",
        HTM,
        '          <li><a class="nav-link" href="#missions">Missions</a></li>\n',
        "",
        (B + "a keyboard alone reaches every control",),
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
        "web-css-muted-text-faint-light",
        CSS,
        "  --text-muted: #464b55;",
        "  --text-muted: #a0a4ad;",
        (B + "axe finds nothing in the light scheme",),
    ),
    Plant(
        "web-css-muted-text-faint-dark",
        CSS,
        "    --text-muted: #b9bec7;",
        "    --text-muted: #4a4f59;",
        (B + "axe finds nothing in the dark scheme",),
    ),
    Plant(
        "web-css-link-faint-dark",
        CSS,
        "    --link: #8db6ff;",
        "    --link: #2a4a80;",
        (B + "axe finds nothing in the dark scheme",),
    ),
    Plant(
        "web-css-dark-scheme-missing",
        CSS,
        "@media (prefers-color-scheme: dark) {\n  :root {",
        "@media (prefers-color-scheme: nothing) {\n  :root {",
        (B + "the dark scheme has a dark background",),
    ),
    Plant(
        "web-css-radio-too-small",
        CSS,
        "  inline-size: 1.5rem;\n  block-size: 1.5rem;",
        "  inline-size: 0.5rem;\n  block-size: 0.5rem;",
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
        "  max-inline-size: 60rem;",
        "  min-inline-size: 30rem;\n  max-inline-size: 60rem;",
        (B + "controls are at least 24 by 24 pixels and the page reflows",),
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

VIEW_PLANTS: list[Plant] = [*STATE_PLANTS, *SITE_PLANTS]
