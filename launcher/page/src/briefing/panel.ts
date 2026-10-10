// The Briefing section of the page: the map, its buttons and its words.
//
// Purpose: show one mission's briefing, play it on the 41 ms clock, and
// keep the words for screen readers (caption, labels, highlighted groups).
// Flow: `mountPanel` finds the section's elements once; `show(bundle)` loads
// the pictures and opens the first team's briefing stopped; each animation
// callback runs the frames that are due, paints the last and updates the
// words that changed; buttons call the core's `press`.
// Invariants: nothing moves or sounds before the player presses Play; a
// button that is not offered stays focusable and is `aria-disabled`; hidden
// tabs do not run frames and do not catch up on return; with reduced motion
// the map is drawn settled, the core unchanged.

import type { BriefingBundle } from "../generated/briefing.ts";
import { logger } from "../logger.ts";
import { currentCaption, highlightedNames, labelTexts } from "./announce.ts";
import { BUTTON_SOUND, Sounds } from "./audio.ts";
import { FrameClock } from "./clock.ts";
import type { Button, Player } from "./core.ts";
import { BUTTONS, loadBriefing, offered, press, updateFrame } from "./core.ts";
import { drawFrame } from "./draw.ts";
import type { Scaling } from "./layout.ts";
import { layoutFor, PANEL_H } from "./layout.ts";
import { Painter } from "./paint.ts";
import type { DrawRecord } from "./records.ts";
import { settled } from "./reduced.ts";
import { loadSprites } from "./sprites.ts";

type Kind<T extends Element> = abstract new (...args: never[]) => T;

function find<T extends Element>(kind: Kind<T>, selector: string): T {
  const found = document.querySelector(selector);
  if (!(found instanceof kind)) {
    throw new Error(`invariant: ${selector} is missing from the page`);
  }
  return found;
}

function setText(element: HTMLElement, text: string): void {
  if (element.textContent !== text) element.textContent = text;
}

/** Return the teams the picker lists: those with a briefing, else team 0. */
export function pickerTeams(bundle: BriefingBundle): number[] {
  const teams = bundle.teams
    .filter((t) => t.briefing !== null)
    .map((t) => t.team);
  return teams.length > 0 ? teams.sort((a, b) => a - b) : [0];
}

export class BriefingPanel {
  readonly #body = find(HTMLDivElement, "#briefing-body");
  readonly #note = find(HTMLParagraphElement, "#briefing-note");
  readonly #stage = find(HTMLDivElement, "#briefing-stage");
  readonly #canvas = find(HTMLCanvasElement, "#briefing-canvas");
  readonly #page = find(HTMLParagraphElement, "#briefing-page");
  readonly #caption = find(HTMLParagraphElement, "#briefing-caption");
  readonly #labels = find(HTMLUListElement, "#briefing-labels");
  readonly #highlight = find(HTMLParagraphElement, "#briefing-highlight");
  readonly #soundSwitch = find(HTMLInputElement, "#briefing-sound");
  readonly #teamBox = find(HTMLDivElement, "#briefing-team-box");
  readonly #team = find(HTMLSelectElement, "#briefing-team");
  readonly #buttons = new Map<Button, HTMLButtonElement>(
    BUTTONS.map((b) => [b, find(HTMLButtonElement, `#briefing-${b}`)]),
  );
  readonly #clock = new FrameClock();
  readonly #motion = window.matchMedia("(prefers-reduced-motion: reduce)");
  #bundle: BriefingBundle | null = null;
  #player: Player | null = null;
  #painter: Painter | null = null;
  #sounds: Sounds | null = null;
  #records: readonly DrawRecord[] = [];
  #scaling: Scaling = "whole_pixels";
  #frameRequest = 0;
  #opening = 0;
  #spoken = { caption: "", labels: "", highlight: "" };

  constructor() {
    for (const [button, element] of this.#buttons) {
      element.addEventListener("click", () => {
        this.#onPress(button, element);
      });
    }
    this.#soundSwitch.addEventListener("change", () => {
      if (this.#sounds !== null) this.#sounds.on = this.#soundSwitch.checked;
    });
    this.#team.addEventListener("change", () => {
      this.#openTeam(Number(this.#team.value));
    });
    this.#motion.addEventListener("change", () => {
      this.#repaint();
    });
    document.addEventListener("visibilitychange", () => {
      this.#onVisibility();
    });
    new ResizeObserver(() => {
      this.#place();
    }).observe(this.#stage);
    window.addEventListener("resize", () => {
      this.#place();
    });
  }

  /** Say why there is no briefing to show (hides the map). */
  hide(note: string): void {
    this.#stopLoop();
    this.#bundle = null;
    this.#player = null;
    this.#body.hidden = true;
    this.#note.hidden = false;
    setText(this.#note, note);
  }

  /** Set the art scaling and re-place the map. */
  setScaling(scaling: Scaling): void {
    this.#scaling = scaling;
    this.#place();
  }

  /** Show `bundle`'s briefing, stopped, for the lowest team that has one. */
  async show(bundle: BriefingBundle): Promise<void> {
    this.#stopLoop();
    const ticket = this.#opening + 1;
    this.#opening = ticket;
    setText(this.#note, "Loading the briefing.");
    this.#note.hidden = false;
    this.#body.hidden = true;
    const sprites = await loadSprites(bundle);
    if (ticket !== this.#opening) return;
    this.#bundle = bundle;
    this.#painter = new Painter(this.#canvas, bundle, sprites);
    this.#sounds = new Sounds(bundle);
    this.#sounds.on = this.#soundSwitch.checked;
    const teams = pickerTeams(bundle);
    this.#team.replaceChildren(
      ...teams.map((team) => {
        const option = document.createElement("option");
        option.value = String(team);
        const name = bundle.teams.find((t) => t.team === team)?.name ?? "";
        option.textContent =
          name === ""
            ? `Team ${String(team)}`
            : `Team ${String(team)}: ${name}`;
        return option;
      }),
    );
    this.#teamBox.hidden = teams.length < 2;
    this.#note.hidden = true;
    this.#body.hidden = false;
    this.#openTeam(teams[0] ?? 0);
  }

  #openTeam(team: number): void {
    const bundle = this.#bundle;
    if (bundle === null) return;
    this.#stopLoop();
    const player = loadBriefing(bundle, team);
    press(player, "stop");
    this.#player = player;
    this.#spoken = { caption: "", labels: "", highlight: "" };
    this.#place();
    this.#runFrame();
    this.#startLoop();
  }

  #place(): void {
    const painter = this.#painter;
    if (painter === null) return;
    const width = this.#stage.clientWidth;
    const height = Math.max(PANEL_H, Math.floor(window.innerHeight * 0.7));
    painter.place(layoutFor(this.#scaling, Math.max(width, 1), height));
    this.#repaint();
  }

  #repaint(): void {
    const painter = this.#painter;
    const player = this.#player;
    if (painter === null || player === null) return;
    painter.paint(
      this.#motion.matches ? drawFrame(settled(player)) : this.#records,
    );
  }

  #runFrame(): void {
    const player = this.#player;
    if (player === null) return;
    for (const sound of updateFrame(player)) this.#sounds?.play(sound);
    this.#records = drawFrame(player);
    this.#repaint();
    this.#speak(player);
  }

  #speak(player: Player): void {
    setText(this.#page, `Page ${String(player.page)}`);
    const caption = currentCaption(player);
    if (caption !== this.#spoken.caption) {
      this.#spoken.caption = caption;
      setText(this.#caption, caption);
    }
    const labels = labelTexts(player);
    const labelKey = labels.join("\n");
    if (labelKey !== this.#spoken.labels) {
      this.#spoken.labels = labelKey;
      this.#labels.replaceChildren(
        ...labels.map((text) => {
          const item = document.createElement("li");
          item.textContent = text;
          return item;
        }),
      );
    }
    const names = highlightedNames(player);
    const highlight =
      names.length === 0
        ? "No flight group is highlighted."
        : `Highlighted: ${names.join(", ")}.`;
    if (highlight !== this.#spoken.highlight) {
      this.#spoken.highlight = highlight;
      setText(this.#highlight, highlight);
    }
    for (const [button, element] of this.#buttons) {
      element.setAttribute(
        "aria-disabled",
        offered(player, button) ? "false" : "true",
      );
    }
  }

  #onPress(button: Button, element: HTMLButtonElement): void {
    const player = this.#player;
    if (player === null || element.getAttribute("aria-disabled") === "true")
      return;
    this.#sounds?.unlock();
    const made = press(player, button);
    logger.debug("pressed", button, made);
    if (made === null) return;
    for (const sound of made) this.#sounds?.play(sound);
    this.#sounds?.play(BUTTON_SOUND);
    this.#speak(player);
  }

  #tick = (now: number): void => {
    this.#frameRequest = requestAnimationFrame(this.#tick);
    const due = this.#clock.advance(now);
    for (let i = 0; i < due; i += 1) this.#runFrame();
  };

  #startLoop(): void {
    if (this.#frameRequest !== 0 || document.hidden) return;
    this.#clock.reset();
    this.#frameRequest = requestAnimationFrame(this.#tick);
  }

  #stopLoop(): void {
    if (this.#frameRequest !== 0) cancelAnimationFrame(this.#frameRequest);
    this.#frameRequest = 0;
  }

  #onVisibility(): void {
    if (document.hidden) this.#stopLoop();
    else if (this.#player !== null) this.#startLoop();
  }
}
