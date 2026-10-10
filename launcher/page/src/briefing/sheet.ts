// Write a briefing run as the answer sheets' lines, so it can be compared.
//
// Purpose: print, for a run of frames and button presses, the same lines the
// engine's tool prints: `state`, `sound`, `draw` with its CRC-32, the listed
// frames' records and the presses.
// Flow: `recordLines` makes the canonical lines of a frame's records (glyphs
// joined into text runs); `SheetRun` runs frames and presses and collects
// lines; `runSteps` runs a whole recipe such as `run 100 stop run 20 play`.
// Invariants: a frame is listed when it is frame 1, every 24th frame, the
// first frame after a press, any frame after `listall`, or when its state
// differs from the frame before in anything but the center, the zoom and the
// ages; a run starts at `state 0` and ends with `unknown_glyphs 0`.

import type { BriefingBundle } from "../generated/briefing.ts";
import type { Button, Player } from "./core.ts";
import { loadBriefing, press, scriptFor, updateFrame } from "./core.ts";
import { crc32OfText, hex8 } from "./crc.ts";
import { drawFrame } from "./draw.ts";
import type { DrawRecord } from "./records.ts";
import type { Font } from "./text.ts";

const LIST_EVERY = 24;

function quote(text: string): string {
  let out = "";
  for (const ch of text) {
    const code = ch.charCodeAt(0);
    if (ch === '"' || ch === "\\") out += `\\${ch}`;
    else if (code < 0x20 || code > 0x7e)
      out += `\\x${code.toString(16).padStart(2, "0")}`;
    else out += ch;
  }
  return out;
}

function n(value: number): string {
  return String(value);
}

type Plain = Exclude<DrawRecord, { kind: "glyph" }>;

function plainLine(record: Plain): string {
  switch (record.kind) {
    case "clip":
      return `clip ${n(record.left)} ${n(record.top)} ${n(record.right)} ${n(record.bottom)}`;
    case "vline":
      return `vline ${n(record.x)} ${n(record.top)} ${n(record.bottom)} ${record.color}`;
    case "hline":
      return `hline ${n(record.y)} ${n(record.left)} ${n(record.right)} ${record.color}`;
    case "icon":
      return `icon ${record.sheet} ${n(record.index)} ${n(record.x)} ${n(record.y)}`;
    case "tint":
      return `tint ${record.sheet} ${n(record.index)} ${n(record.x)} ${n(record.y)} ${record.color}`;
    case "fill":
    case "outline":
      return `${record.kind} ${n(record.left)} ${n(record.top)} ${n(record.right)} ${n(record.bottom)} ${record.color}`;
  }
}

interface Run {
  x: number;
  y: number;
  color: string;
  text: string;
  end: number;
}

/**
 * Return the canonical lines of `records`. A glyph joins the run before it
 * when it is drawn at the run's y, in its color, at the x where the last
 * glyph ended (x plus width plus spacing); otherwise it starts a new run.
 */
export function recordLines(
  records: readonly DrawRecord[],
  font: Font,
): string[] {
  const lines: string[] = [];
  let run: Run | null = null;
  const flush = (): void => {
    if (run !== null) {
      lines.push(
        `text ${n(run.x)} ${n(run.y)} ${run.color} "${quote(run.text)}"`,
      );
      run = null;
    }
  };
  for (const record of records) {
    if (record.kind !== "glyph") {
      flush();
      lines.push(plainLine(record));
      continue;
    }
    const width = (font.widths[record.code] ?? 0) + font.spacing;
    const joins =
      run !== null &&
      run.y === record.y &&
      run.color === record.color &&
      run.end === record.x;
    if (run !== null && joins) {
      run.text += String.fromCharCode(record.code);
      run.end += width;
    } else {
      flush();
      run = {
        x: record.x,
        y: record.y,
        color: record.color,
        text: String.fromCharCode(record.code),
        end: record.x + width,
      };
    }
  }
  flush();
  return lines;
}

/** Return the `state` line's fields after `state F `. */
export function stateText(player: Player): string {
  const slotText = player.slots
    .map((s) => `${n(s.on ? 1 : 0)}:${n(s.block)}`)
    .join(",");
  const markers = player.markers
    .map((m, i) => (m.on ? `${n(i)}:${n(m.group)}:${n(m.age)}` : null))
    .filter((m): m is string => m !== null);
  const labels = player.labels
    .map((l, i) =>
      l.on
        ? `${n(i)}:${n(l.text)}:${n(l.x)}:${n(l.y)}:${n(l.row)}:${n(l.age)}`
        : null,
    )
    .filter((l): l is string => l !== null);
  return [
    `play=${n(player.playing ? 1 : 0)}`,
    `t=${n(player.t)}`,
    `c=${n(player.cx)},${n(player.cy)}`,
    `tc=${n(player.tcx)},${n(player.tcy)}`,
    `s=${n(player.sx)},${n(player.sy)}`,
    `ts=${n(player.tsx)},${n(player.tsy)}`,
    `page=${n(player.page)}`,
    `last=${n(player.last)}`,
    `slots=${slotText}`,
    `markers=${markers.length === 0 ? "-" : markers.join(";")}`,
    `labels=${labels.length === 0 ? "-" : labels.join(";")}`,
  ].join(" ");
}

/** Return the part of a state that decides whether a frame is listed. */
export function compareText(player: Player): string {
  const markers = player.markers
    .map((m, i) => (m.on ? `${n(i)}:${n(m.group)}` : "-"))
    .join(";");
  const labels = player.labels
    .map((l, i) =>
      l.on ? `${n(i)}:${n(l.text)}:${n(l.x)}:${n(l.y)}:${n(l.row)}` : "-",
    )
    .join(";");
  const slots = player.slots
    .map((s) => `${n(s.on ? 1 : 0)}:${n(s.block)}`)
    .join(",");
  const targets = `${n(player.tcx)},${n(player.tcy)} ${n(player.tsx)},${n(player.tsy)}`;
  return [
    player.playing,
    targets,
    player.page,
    player.last,
    slots,
    markers,
    labels,
  ].join("|");
}

/** One run of the setup screen: frames and presses, and the lines they print. */
export class SheetRun {
  readonly lines: string[] = [];
  readonly player: Player;
  readonly font: Font;
  frame = 0;
  listAll = false;
  #previous: string;
  #pressed = false;

  constructor(bundle: BriefingBundle, team: number) {
    this.player = loadBriefing(bundle, team);
    this.font = bundle.font;
    this.lines.push(`state 0 ${stateText(this.player)}`);
    this.#previous = compareText(this.player);
  }

  /** Run one frame: update, draw, then print its lines. */
  runFrame(): void {
    this.frame += 1;
    const sounds = updateFrame(this.player);
    const records = drawFrame(this.player);
    for (const sound of sounds)
      this.lines.push(`sound ${n(this.frame)} ${sound}`);
    this.lines.push(`state ${n(this.frame)} ${stateText(this.player)}`);
    const text = recordLines(records, this.font);
    const body = text.map((line) => `${line}\n`).join("");
    this.lines.push(
      `draw ${n(this.frame)} crc=${hex8(crc32OfText(body))} records=${n(text.length)}`,
    );
    const now = compareText(this.player);
    const listed =
      this.listAll ||
      this.frame === 1 ||
      this.frame % LIST_EVERY === 0 ||
      this.#pressed ||
      now !== this.#previous;
    if (listed) for (const line of text) this.lines.push(`  ${line}`);
    this.#previous = now;
    this.#pressed = false;
  }

  /** Press a button after the last frame's drawing; print the press and the state. */
  press(button: Button): void {
    const sounds = press(this.player, button);
    this.#pressed = true;
    for (const sound of sounds ?? [])
      this.lines.push(`sound ${n(this.frame)} ${sound}`);
    this.lines.push(
      `press ${n(this.frame)} ${button}${sounds === null ? " ignored" : ""}`,
    );
    this.lines.push(`state ${n(this.frame)} ${stateText(this.player)}`);
  }
}

/** Return the sheet's `briefing I frames D` line for `team` on `bundle`. */
export function briefingLine(bundle: BriefingBundle, team: number): string {
  const { index, runningTime } = scriptFor(bundle, team);
  return `briefing ${n(index)} frames ${n(runningTime)}`;
}

const BUTTON_WORDS: readonly string[] = ["rewind", "stop", "play", "forward"];

function isButton(word: string): word is Button {
  return BUTTON_WORDS.includes(word);
}

/**
 * Run a recipe such as `run 100 stop run 20 play` on `team`'s briefing and
 * return the lines from `state 0` to `unknown_glyphs 0`. Words: `run N`,
 * `rewind`, `stop`, `play`, `forward`, `listall`. Throws on any other word.
 */
export function runSteps(
  bundle: BriefingBundle,
  team: number,
  steps: string,
): string[] {
  const run = new SheetRun(bundle, team);
  const words = steps.split(/\s+/).filter((w) => w !== "");
  for (let i = 0; i < words.length; i += 1) {
    const word = words[i] ?? "";
    if (word === "run") {
      const count = Number(words[i + 1]);
      if (!Number.isInteger(count) || count < 0)
        throw new Error("run needs a count");
      i += 1;
      for (let k = 0; k < count; k += 1) run.runFrame();
    } else if (word === "listall") {
      run.listAll = true;
    } else if (isButton(word)) {
      run.press(word);
    } else {
      throw new Error(`unknown step ${word}`);
    }
  }
  run.lines.push("unknown_glyphs 0");
  return run.lines;
}
