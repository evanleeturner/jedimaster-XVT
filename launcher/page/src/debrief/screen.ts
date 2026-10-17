/**
 * The 1997 debriefing screen on the page: the background, the numbers drawn
 * in the game's font, the tab sprites, the Leave button and the side badge.
 *
 * Purpose: when the page wears the menu frame and the Debriefing is open,
 * show the flown battle as the 1997 debriefing screen, inside the frame box.
 * Flow: `update` is told the debriefing on show, the tab, the pilot's side and
 * whether the menu is open; it sets the `debrief-screen` class on the root
 * element (the style sheet then hides the Mission Selection pieces and
 * places the real tab buttons on 1997's rectangles), loads the background
 * `look.ts` picks, and draws the numbers from `lines.ts` with `paint.ts`.
 * Invariants: the drawn layer is hidden from the accessibility tree and the
 * tables stay the way to read the numbers; every picture that answers 404
 * just does not show; nothing is requested while the screen is off; the page
 * is plain (and none of this runs) below the frame's size, in forced colors
 * or with no frame art.
 */
import type { FrameLayout } from "../frame/layout.ts";
import type { Debrief } from "../generated/control.ts";
import { logger } from "../logger.ts";
import type { DrawnLine } from "./lines.ts";
import {
  missionLine,
  pageLines,
  PILOT_PLATE,
  pilotLine,
  plain,
  STATISTICS,
  statisticsRowCount,
  TEXT_PLATE,
} from "./lines.ts";
import type { PilotSide } from "./look.ts";
import {
  artUrl,
  backgroundStem,
  BADGE_FRAMES,
  badgeName,
  pilotSide,
  screenTab,
  SPRITES,
} from "./look.ts";
import { InkPainter, loadFonts } from "./paint.ts";
import type { DebriefTab } from "./view.ts";

type Kind<T extends Element> = new (...args: never[]) => T;

function find<T extends Element>(kind: Kind<T>, selector: string): T {
  const found = document.querySelector(selector);
  if (!(found instanceof kind)) {
    throw new Error(`invariant: ${selector} is missing from the page`);
  }
  return found;
}

export interface ScreenInput {
  readonly debrief: Debrief | null;
  readonly tab: DebriefTab;
  readonly chosen: PilotSide;
  /** True while the Debriefing menu is open. */
  readonly open: boolean;
}

export const SCREEN_CLASS = "debrief-screen";

export class DebriefScreen {
  readonly #ground = find(HTMLCanvasElement, "#debrief-ground");
  readonly #ink = find(HTMLCanvasElement, "#debrief-ink");
  readonly #leave = find(HTMLButtonElement, "#debrief-leave");
  readonly #page = find(HTMLDivElement, "#debrief-page");
  readonly #scroll = find(HTMLDivElement, "#debrief-scroll");
  readonly #up = find(HTMLButtonElement, "#debrief-scroll-up");
  readonly #down = find(HTMLButtonElement, "#debrief-scroll-down");
  readonly #badges = [
    find(HTMLImageElement, "#debrief-badge-left"),
    find(HTMLImageElement, "#debrief-badge-right"),
  ];
  readonly #sheet = new CSSStyleSheet();
  readonly #painter = new InkPainter(this.#ink);
  readonly #grounds = new Map<string, HTMLImageElement>();
  readonly #onLeave: () => void;
  #layout: FrameLayout | null = null;
  #on = false;
  #stem = "";
  #wanted = "";
  #lastTab: DebriefTab | null = null;
  #lastResult = 0;
  #scrollRow = 0;
  #fontsAsked = false;
  #pictures = false;
  #last: ScreenInput | null = null;
  #drawn = "";

  constructor(onLeave: () => void) {
    this.#onLeave = onLeave;
    document.adoptedStyleSheets = [...document.adoptedStyleSheets, this.#sheet];
    this.#leave.addEventListener("click", () => {
      logger.debug("debriefing left");
      this.#onLeave();
    });
    this.#up.addEventListener("click", () => {
      this.#scrollBy(-STATISTICS.rows + 1);
    });
    this.#down.addEventListener("click", () => {
      this.#scrollBy(STATISTICS.rows - 1);
    });
    this.#page.closest("section")?.addEventListener("wheel", (event) => {
      if (!this.#on || this.#scroll.hidden) return;
      event.preventDefault();
      this.#scrollBy(event.deltaY > 0 ? 3 : -3);
    });
  }

  /** Tell the screen the frame's layout (null when the page is plain). */
  setLayout(layout: FrameLayout | null): void {
    this.#layout = layout;
    this.#drawn = "";
    this.#sheet.replaceSync(
      layout === null
        ? ""
        : `#debrief-ground, #debrief-ink { image-rendering: ${layout.smooth ? "auto" : "pixelated"}; }`,
    );
    if (this.#last !== null) this.update(this.#last);
  }

  /** Whether the screen is showing. */
  get on(): boolean {
    return this.#on;
  }

  #scrollBy(rows: number): void {
    const input = this.#last;
    if (input?.debrief == null) return;
    const most = Math.max(
      0,
      statisticsRowCount(input.debrief) - STATISTICS.rows,
    );
    this.#scrollRow = Math.min(most, Math.max(0, this.#scrollRow + rows));
    this.#draw();
  }

  /** Show, change or hide the screen; return the tab it shows (null when off). */
  update(input: ScreenInput): DebriefTab | null {
    this.#last = input;
    const debrief = input.debrief;
    const on = this.#layout !== null && input.open && debrief !== null;
    this.#on = on;
    document.documentElement.classList.toggle(SCREEN_CLASS, on);
    this.#page.tabIndex = on ? -1 : 0;
    if (debrief === null || !on) {
      this.#stem = "";
      this.#wanted = "";
      return null;
    }
    const tab = screenTab(debrief, input.tab);
    if (tab !== this.#lastTab || debrief.result_id !== this.#lastResult) {
      logger.debug("debriefing tab", { tab, result: debrief.result_id });
      this.#lastTab = tab;
      this.#lastResult = debrief.result_id;
      this.#scrollRow = 0;
    }
    this.#pictureSources(debrief, input.chosen);
    this.#choose(debrief, input.chosen);
    this.#draw();
    return tab;
  }

  #pictureSources(debrief: Debrief, chosen: PilotSide): void {
    if (!this.#pictures) {
      this.#pictures = true;
      this.#tabArt("overview", SPRITES.overview);
      this.#tabArt("statistics", SPRITES.statistics);
      this.#tabArt("leave", SPRITES.leave, this.#leave);
    }
    const side = pilotSide(debrief, chosen);
    if (this.#badges[0]?.dataset.side !== side) {
      for (const badge of this.#badges) this.#badge(badge, side, 0);
    }
    if (!this.#fontsAsked) {
      this.#fontsAsked = true;
      void loadFonts().then((fonts) => {
        this.#painter.setFonts(fonts);
        this.#drawn = "";
        this.#draw();
      });
    }
  }

  #tabArt(
    id: string,
    names: readonly [string, string],
    button?: HTMLButtonElement,
  ): void {
    const owner =
      button ?? find(HTMLButtonElement, `#debrief-tabs [data-tab="${id}"]`);
    const images = owner.querySelectorAll("img");
    names.forEach((name, index) => {
      const image = images[index];
      if (image === undefined) return;
      image.addEventListener("error", () => {
        logger.warn("a debriefing picture did not load", name);
        owner.classList.add("no-art");
        image.hidden = true;
      });
      image.src = artUrl(name);
    });
  }

  #badge(image: HTMLImageElement, side: PilotSide, frame: number): void {
    image.dataset.side = side;
    image.hidden = false;
    image.onerror = () => {
      if (frame + 1 < BADGE_FRAMES) this.#badge(image, side, frame + 1);
      else image.hidden = true;
    };
    image.src = artUrl(badgeName(side, frame));
  }

  #choose(debrief: Debrief, chosen: PilotSide): void {
    const stem = backgroundStem(debrief, chosen);
    if (stem === this.#stem) return;
    this.#stem = stem;
    logger.debug("debriefing background", {
      kind: debrief.mission_type,
      outcome: debrief.outcome,
      side: debrief.side,
      file: stem,
    });
    this.#wanted = stem;
    this.#drawn = "";
    const kept = this.#grounds.get(stem);
    if (kept !== undefined) return;
    const image = new Image();
    image.addEventListener("load", () => {
      this.#grounds.set(stem, image);
      this.#drawn = "";
      this.#draw();
    });
    image.addEventListener("error", () => {
      logger.warn("the debriefing background did not load", stem);
      this.#grounds.delete(stem);
      this.#drawn = "";
      this.#draw();
    });
    image.src = artUrl(stem);
  }

  #lines(debrief: Debrief, tab: DebriefTab): DrawnLine[] {
    return [
      missionLine(debrief),
      pilotLine(debrief),
      ...pageLines(debrief, tab, this.#scrollRow),
    ];
  }

  /** The lines on the canvas now, as the text a reader sees (for tests). */
  #expose(lines: readonly DrawnLine[]): void {
    this.#ink.dataset.lines = JSON.stringify(
      lines.map((l) => ({ x: l.x, y: l.y, text: plain(l.text) })),
    );
  }

  #draw(): void {
    const input = this.#last;
    const layout = this.#layout;
    if (!this.#on || input?.debrief == null || layout === null) return;
    const debrief = input.debrief;
    const tab = screenTab(debrief, input.tab);
    const rows = statisticsRowCount(debrief);
    this.#scroll.hidden = !(tab === "statistics" && rows > STATISTICS.rows);
    this.#up.disabled = this.#scrollRow === 0;
    this.#down.disabled = this.#scrollRow + STATISTICS.rows >= rows;
    const key = [
      this.#wanted,
      debrief.result_id,
      tab,
      this.#scrollRow,
      layout.factor,
      this.#painter.ready,
      this.#grounds.has(this.#wanted),
    ].join(" ");
    if (key === this.#drawn) return;
    this.#drawn = key;
    this.#paintGround(layout.factor);
    const lines = this.#lines(debrief, tab);
    this.#painter.paint(lines, [TEXT_PLATE, PILOT_PLATE], layout.factor);
    this.#expose(this.#painter.ready ? lines : []);
  }

  #paintGround(factor: number): void {
    const canvas = this.#ground;
    canvas.width = 640 * factor;
    canvas.height = 480 * factor;
    const image = this.#grounds.get(this.#wanted);
    const context = canvas.getContext("2d");
    if (context === null || image === undefined) return;
    context.imageSmoothingEnabled = false;
    context.drawImage(image, 0, 0, canvas.width, canvas.height);
  }
}
