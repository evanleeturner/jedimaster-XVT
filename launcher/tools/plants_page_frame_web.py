"""The planted faults of the menu frame skin: background table, window rule, skin, markup, style.

Purpose:
    Break one rule of the page's menu frame per plant: which of the twelve
    backgrounds a kind and side pick, when the window is big enough to wear
    the frame, what the skin does with the picture, the forced-colors and
    art-scaling settings, the options copy, and the style sheet's places and
    plate.

Flow:
    ``plant_page_web`` applies each plant alone: write ``new`` over ``old``
    in ``path``, run ``npm test`` (and build and ``npm run test:browser``
    when the plant names a browser test or plants the page itself), restore,
    and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across the table, all starting ``web-frame-``.
    - Each ``old`` text occurs exactly once in its file.
    - ``expect`` entries are ``node: <title>`` or ``browser: <title>`` and
      match by prefix.

Call:
    ``from plants_page_frame_web import FRAME_WEB_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

BGD = "page/src/frame/background.ts"
LAY = "page/src/frame/layout.ts"
SKI = "page/src/frame/skin.ts"
MAI = "page/src/main.ts"
PAN = "page/src/briefing/panel.ts"
HTM = "page/public/index.html"
CSS = "page/public/frame.css"
N = "node: "
B = "browser: "
REQUESTS = B + "the frame's request follows the selected type and the chosen side"
PLACES = B + "framed at 2x: the frame and the map sit at the scaled places"
OPTIONS_COPY = B + "framed: the options copy is out of the accessibility tree"


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return the plant ``web-frame-<pid>``."""
    return Plant(f"web-frame-{pid}", path, old, new, expect)


BACKGROUND_PLANTS: list[Plant] = [
    plant(
        "bg-training-sides-swapped",
        BGD,
        'training: ["gametr", "gameti", "gametr"],',
        'training: ["gameti", "gametr", "gametr"],',
        N + "background of training",
    ),
    plant(
        "bg-training-neutral-imperial",
        BGD,
        'training: ["gametr", "gameti", "gametr"],',
        'training: ["gametr", "gameti", "gameti"],',
        N + "background of training",
    ),
    plant(
        "bg-combat-neutral-rebel",
        BGD,
        'combat: ["gamecr", "gameci", "gamecn"],',
        'combat: ["gamecr", "gameci", "gamecr"],',
        N + "background of combat",
    ),
    plant(
        "bg-battle-sides-swapped",
        BGD,
        'battle: ["gamebr", "gamebi", "gamebn"],',
        'battle: ["gamebi", "gamebr", "gamebn"],',
        N + "background of battle",
    ),
    plant(
        "bg-campaign-neutral-imperial",
        BGD,
        'campaign: ["gamecar", "gamecai", "gamecar"],',
        'campaign: ["gamecar", "gamecai", "gamecai"],',
        N + "background of campaign",
    ),
    plant(
        "bg-melee-sided",
        BGD,
        'melee: ["gamemn", "gamemn", "gamemn"],',
        'melee: ["gamemn", "gamebi", "gamemn"],',
        N + "background of melee",
    ),
    plant(
        "bg-tournament-default-gametr",
        BGD,
        'if (row === undefined) return "gametrn";',
        'if (row === undefined) return "gametr";',
        N + "background of tournament",
        N + "background of other",
        N + "background of null",
    ),
    plant(
        "bg-neutral-picks-rebel",
        BGD,
        'row[side === "rebel" ? 0 : side === "imperial" ? 1 : 2]',
        'row[side === "imperial" ? 1 : 0]',
        N + "background of combat",
    ),
    plant(
        "bg-no-mission-sided",
        BGD,
        "const row = mission === null ? undefined : SIDED[mission];",
        'const row = SIDED[mission ?? "training"];',
        N + "background of null",
    ),
    plant(
        "bg-art-name-extension",
        BGD,
        "return `frame-${background}.png`;",
        "return `frame-${background}.bmp`;",
        N + "the frame art name",
    ),
    plant(
        "bg-route-prefix",
        BGD,
        'export const FRAME_PATH = "/art/frame/";',
        'export const FRAME_PATH = "/art/briefing/";',
        REQUESTS,
    ),
]

LAYOUT_PLANTS: list[Plant] = [
    plant(
        "lay-639-accepted",
        LAY,
        "if (width < FRAME_W || height < FRAME_H) return null;",
        "if (width < FRAME_W - 1 || height < FRAME_H) return null;",
        N + "no layout below 640 x 480",
        B + "plain at 639 px wide",
    ),
    plant(
        "lay-640-refused",
        LAY,
        "if (width < FRAME_W || height < FRAME_H) return null;",
        "if (width <= FRAME_W || height < FRAME_H) return null;",
        N + "whole pixels: the largest whole multiple, sharp",
    ),
    plant(
        "lay-479-accepted",
        LAY,
        "if (width < FRAME_W || height < FRAME_H) return null;",
        "if (width < FRAME_W || height < FRAME_H - 1) return null;",
        N + "no layout below 640 x 480",
        B + "plain when the window is high enough but too short",
    ),
    plant(
        "lay-480-refused",
        LAY,
        "if (width < FRAME_W || height < FRAME_H) return null;",
        "if (width < FRAME_W || height <= FRAME_H) return null;",
        N + "whole pixels: the largest whole multiple, sharp",
    ),
    plant(
        "lay-scale-is-factor",
        LAY,
        "scale: layout.width / FRAME_W",
        "scale: layout.factor",
        N + "engine fit: the largest size that fits, smoothed",
    ),
]

SKIN_PLANTS: list[Plant] = [
    plant(
        "skin-forced-colors-ignored",
        SKI,
        "image === null || this.#colors.matches",
        "image === null",
        B + "plain under forced colors",
    ),
    plant(
        "skin-scaling-ignored",
        SKI,
        "frameLayout(this.#scaling, window.innerWidth, window.innerHeight)",
        'frameLayout("whole_pixels", window.innerWidth, window.innerHeight)',
        B + "framed: engine fit shows the largest size that fits, smoothed",
    ),
    plant(
        "skin-scaling-not-applied",
        SKI,
        "    this.#scaling = scaling;\n    this.#apply();",
        "    this.#scaling = scaling;",
        B + "framed: engine fit shows the largest size that fits, smoothed",
    ),
    plant(
        "main-frame-scaling-unused",
        MAI,
        "    frameSkin.setScaling(view.artScaling);\n",
        "",
        B + "framed: engine fit shows the largest size that fits, smoothed",
    ),
    plant(
        "main-frame-mission-unused",
        MAI,
        "  frameSkin.setMission(view.sideKind);\n",
        "",
        REQUESTS,
    ),
    plant(
        "main-frame-side-unused",
        MAI,
        "  frameSkin.setSide(view.sideFaction);\n",
        "",
        REQUESTS,
    ),
    plant(
        "skin-smoothing-swapped",
        SKI,
        '${layout.smooth ? "auto" : "pixelated"}',
        '${layout.smooth ? "pixelated" : "auto"}',
        B + "framed: engine fit shows the largest size that fits, smoothed",
    ),
    plant(
        "skin-painted-before-load",
        SKI,
        "    const image = new Image();\n",
        "    const image = new Image();\n    this.#image = image;\n    this.#apply();\n",
        B + "the page stays plain while the frame art is still loading",
    ),
    plant(
        "skin-failed-picture-kept",
        SKI,
        '      logger.warn("the menu frame did not load", background);\n',
        '      logger.warn("the menu frame did not load", background);\n      this.#loaded.set(background, image);\n',
        B + "a picture that failed is asked for again",
    ),
    plant(
        "skin-map-drawn-at-panel-width",
        PAN,
        "painter.place({ factor, width: PANEL_W, height: PANEL_H, smooth });",
        "painter.place({ factor, width: PANEL_W * factor, height: PANEL_H, smooth });",
        PLACES,
    ),
    plant(
        "skin-escape-swallowed",
        SKI,
        "    this.#choose();\n  }\n\n  /** Set the art scaling",
        '    window.addEventListener("keydown", (event) => {\n'
        '      if (document.documentElement.classList.contains("framed"))\n'
        "        event.stopPropagation();\n"
        "    }, true);\n"
        "    this.#choose();\n  }\n\n  /** Set the art scaling",
        B + "framed: Escape closes",
    ),
    plant(
        "skin-focus-lost-after-escape",
        SKI,
        "    this.#choose();\n  }\n\n  /** Set the art scaling",
        '    window.addEventListener("keyup", (event) => {\n'
        '      if (event.key !== "Escape") return;\n'
        '      if (!document.documentElement.classList.contains("framed")) return;\n'
        "      if (document.activeElement instanceof HTMLElement)\n"
        "        document.activeElement.blur();\n"
        "    });\n"
        "    this.#choose();\n  }\n\n  /** Set the art scaling",
        B + "framed: Escape closes",
    ),
]

MARKUP_PLANTS: list[Plant] = [
    plant(
        "html-options-copy-readable",
        HTM,
        '<div class="frame-only options-copy" id="options-copy" aria-hidden="true">',
        '<div class="frame-only options-copy" id="options-copy">',
        OPTIONS_COPY,
    ),
    plant(
        "html-frame-art-readable",
        HTM,
        '        height="480"\n        aria-hidden="true"\n      ></canvas>',
        '        height="480"\n      ></canvas>',
        B + "framed: the frame picture is out of the accessibility tree",
    ),
    plant(
        "main-options-copy-focusable",
        MAI,
        '      item.className = "options-copy-item";\n',
        '      item.className = "options-copy-item";\n      item.tabIndex = 0;\n',
        OPTIONS_COPY,
    ),
    plant(
        "main-options-copy-values-dropped",
        MAI,
        "      value.textContent = row.value;\n      item.append(label, value);",
        '      value.textContent = "";\n      item.append(label, value);',
        OPTIONS_COPY,
    ),
]

STYLE_PLANTS: list[Plant] = [
    plant(
        "css-stage-row-off",
        CSS,
        "html.framed .briefing-empty,\nhtml.framed .briefing-stage {\n  position: absolute;\n  inset: 116px auto auto 84px;",
        "html.framed .briefing-empty,\nhtml.framed .briefing-stage {\n  position: absolute;\n  inset: 117px auto auto 84px;",
        PLACES,
    ),
    plant(
        "css-stage-column-off",
        CSS,
        "html.framed .briefing-empty,\nhtml.framed .briefing-stage {\n  position: absolute;\n  inset: 116px auto auto 84px;",
        "html.framed .briefing-empty,\nhtml.framed .briefing-stage {\n  position: absolute;\n  inset: 116px auto auto 85px;",
        PLACES,
    ),
    plant(
        "css-plate-too-clear",
        CSS,
        "  --plate: rgb(10 6 44 / 80%);",
        "  --plate: rgb(10 6 44 / 40%);",
        B + "framed: text over a white picture keeps 4.5:1 on the plate",
    ),
    plant(
        "css-plate-just-short",
        CSS,
        "  --plate: rgb(10 6 44 / 80%);",
        "  --plate: rgb(10 6 44 / 64%);",
        B + "framed: text over a white picture keeps 4.5:1 on the plate",
    ),
]

FRAME_WEB_PLANTS: list[Plant] = [
    *BACKGROUND_PLANTS,
    *LAYOUT_PLANTS,
    *SKIN_PLANTS,
    *MARKUP_PLANTS,
    *STYLE_PLANTS,
]
