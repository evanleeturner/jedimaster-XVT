// The game's menu frame around the page: when it is worn, and in what size.
//
// Purpose: wear the 1997 setup screen's metal frame (read from the player's
// own install) as a skin over the page's plain layout, whenever the window
// has room for it.
// Flow: the page tells the skin the shown mission's kind, the shown team's
// background and the art scaling; the skin picks the frame picture (see
// `background.ts`), loads it from `/art/frame/<name>`, and on every change
// of picture, scaling, window size or forced colors decides whether the
// page is framed. Framed sets the `framed` class on the root element, the
// scale as `--s` in a constructed style sheet (never a `style` attribute),
// and draws the picture on the frame canvas; the listener hears the layout
// (null when plain) so the briefing map is drawn at the same scale.
// Invariants: framed needs all of: the picture loaded, a window of at least
// 640 x 480 CSS pixels, and no forced colors; otherwise the page is plain,
// exactly the unskinned layout. A picture that fails to load is not kept
// and is asked for again only when the wanted background changes. Nothing is
// ever requested from anywhere but this page's own frame route.

import type { Scaling } from "../briefing/layout.ts";
import { logger } from "../logger.ts";
import type { Side } from "./background.ts";
import {
  FRAME_PATH,
  frameArt,
  frameBackground,
  teamKind,
} from "./background.ts";
import type { FrameLayout } from "./layout.ts";
import { FRAME_H, FRAME_W, frameLayout } from "./layout.ts";

export type FrameListener = (layout: FrameLayout | null) => void;

export class FrameSkin {
  readonly #canvas: HTMLCanvasElement;
  readonly #listener: FrameListener;
  readonly #sheet = new CSSStyleSheet();
  readonly #colors = window.matchMedia("(forced-colors: active)");
  readonly #loaded = new Map<string, HTMLImageElement>();
  #scaling: Scaling = "whole_pixels";
  #mission: string | null = null;
  #side: Side = "neutral";
  #wanted = "";
  #image: HTMLImageElement | null = null;
  #ticket = 0;
  #layout: FrameLayout | null = null;
  #drawn = "";

  constructor(canvas: HTMLCanvasElement, listener: FrameListener) {
    this.#canvas = canvas;
    this.#listener = listener;
    document.adoptedStyleSheets = [...document.adoptedStyleSheets, this.#sheet];
    window.addEventListener("resize", () => {
      this.#apply();
    });
    this.#colors.addEventListener("change", () => {
      this.#apply();
    });
    this.#choose();
  }

  /** Set the art scaling and re-place the frame. */
  setScaling(scaling: Scaling): void {
    this.#scaling = scaling;
    this.#apply();
  }

  /** Set the shown mission's kind (`training`...), or null for none. */
  setMission(kind: string | null): void {
    this.#mission = kind;
    this.#choose();
  }

  /** Set the shown team's under-map art name, or null for none. */
  setTeam(art: string | null): void {
    this.#side = teamKind(art);
    this.#choose();
  }

  #choose(): void {
    const background = frameBackground(this.#mission, this.#side);
    if (background === this.#wanted) return;
    this.#wanted = background;
    logger.debug("frame background", background);
    const kept = this.#loaded.get(background);
    if (kept !== undefined) {
      this.#image = kept;
      this.#apply();
      return;
    }
    const ticket = this.#ticket + 1;
    this.#ticket = ticket;
    const image = new Image();
    image.addEventListener("load", () => {
      if (ticket !== this.#ticket) return;
      this.#loaded.set(background, image);
      this.#image = image;
      this.#apply();
    });
    image.addEventListener("error", () => {
      logger.warn("the menu frame did not load", background);
      if (ticket !== this.#ticket) return;
      this.#image = null;
      this.#apply();
    });
    image.src = `${FRAME_PATH}${frameArt(background)}`;
  }

  #apply(): void {
    const image = this.#image;
    const layout =
      image === null || this.#colors.matches
        ? null
        : frameLayout(this.#scaling, window.innerWidth, window.innerHeight);
    document.documentElement.classList.toggle("framed", layout !== null);
    if (layout === null || image === null) {
      this.#sheet.replaceSync("");
      this.#drawn = "";
    } else {
      this.#sheet.replaceSync(
        `:root { --s: ${String(layout.scale)}; } ` +
          `#${this.#canvas.id} { image-rendering: ` +
          `${layout.smooth ? "auto" : "pixelated"}; }`,
      );
      this.#draw(image, layout.factor);
    }
    const before = JSON.stringify(this.#layout);
    this.#layout = layout;
    if (before !== JSON.stringify(layout)) this.#listener(layout);
  }

  #draw(image: HTMLImageElement, factor: number): void {
    const key = `${image.src}@${String(factor)}`;
    if (key === this.#drawn) return;
    this.#drawn = key;
    this.#canvas.width = FRAME_W * factor;
    this.#canvas.height = FRAME_H * factor;
    const context = this.#canvas.getContext("2d");
    if (context === null) return;
    context.imageSmoothingEnabled = false;
    context.drawImage(image, 0, 0, this.#canvas.width, this.#canvas.height);
  }
}
