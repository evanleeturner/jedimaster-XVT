/**
 * Draw the debriefing's lines in the game's own font on a canvas.
 *
 * Purpose: put the numbers and names of the 1997 screen where 1997 put them,
 * in fonts 12 and 15 read from the player's install, reusing the briefing's
 * rule for measuring and walking a string of bytes.
 * Flow: `loadFonts` fetches the metrics and the two atlases from the
 * debriefing route; `placeLine` turns a drawn line into glyph records;
 * `paintLines` lays the plates, then draws every glyph tinted in its colour.
 * Invariants: a font that could not be loaded draws nothing and the page
 * says so once; ink colours are the game's (white, yellow) or the page's
 * lifted gray and red; white and yellow keep 4.5:1 on the zone plate, every
 * other ink on the word plate behind its own glyphs;
 * nothing is requested from anywhere but this page's debriefing route.
 */
import type { Rgb } from "../briefing/colors.ts";
import { colorRgb } from "../briefing/colors.ts";
import type { GlyphRecord } from "../briefing/records.ts";
import { drawBytes, measure, toBytes } from "../briefing/text.ts";
import type { FontData } from "../generated/briefing.ts";
import { logger } from "../logger.ts";
import type { DrawnLine, Ink } from "./lines.ts";
import type { Rect } from "./look.ts";
import { DEBRIEF_PATH } from "./look.ts";

export interface Fonts {
  readonly font12: FontData;
  readonly font15: FontData;
}

export interface LoadedFonts extends Fonts {
  readonly atlas12: HTMLCanvasElement;
  readonly atlas15: HTMLCanvasElement;
}

/** The plates' colour: the page's panel colour. */
export const PLATE_RGB: Rgb = [10, 6, 44];
/**
 * How much the plate under the text zone covers the art: the 1997 art shows
 * through, and white and yellow keep 4.5:1 over every pixel of the text zone
 * of all 32 backgrounds in the game's install (measured: 4.9 at the least).
 */
export const ZONE_ALPHA = 0.6;
/** How much a word plate covers: every other ink keeps 4.5:1 over any art. */
export const WORD_ALPHA = 0.97;
/**
 * The page's inks. 1997's gray (96) and red (255, 0, 0) are lifted so that
 * they keep 4.5:1 on a word plate over any art; white and yellow are the
 * game's.
 */
export const INKS: Readonly<Record<Ink, Rgb>> = {
  white: [255, 255, 255],
  yellow: [255, 255, 0],
  gray: [150, 150, 150],
  red: [255, 80, 80],
};
function luminance(rgb: Rgb): number {
  const channel = (value: number): number => {
    const v = value / 255;
    return v <= 0.03928 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4;
  };
  return (
    0.2126 * channel(rgb[0]) +
    0.7152 * channel(rgb[1]) +
    0.0722 * channel(rgb[2])
  );
}

/** The zone plate over a white picture: the brightest art the text can meet. */
const ZONE_OVER_WHITE: Rgb = [
  ZONE_ALPHA * PLATE_RGB[0] + (1 - ZONE_ALPHA) * 255,
  ZONE_ALPHA * PLATE_RGB[1] + (1 - ZONE_ALPHA) * 255,
  ZONE_ALPHA * PLATE_RGB[2] + (1 - ZONE_ALPHA) * 255,
];

/** Return true when `ink` keeps 4.5:1 on the zone plate alone (white, yellow). */
export function readsBare(ink: Rgb): boolean {
  const [hi, lo] = [luminance(ink), luminance(ZONE_OVER_WHITE)].sort(
    (a, b) => b - a,
  );
  return ((hi ?? 0) + 0.05) / ((lo ?? 0) + 0.05) >= 4.5;
}

function plateStyle(alpha: number): string {
  const [r, g, b] = PLATE_RGB;
  return `rgb(${String(r)} ${String(g)} ${String(b)} / ${String(alpha * 100)}%)`;
}

export interface Placed {
  readonly glyphs: readonly GlyphRecord[];
  readonly clipRight: number | null;
  readonly top: number;
}

function isNumbers(value: unknown): value is number[] {
  return (
    Array.isArray(value) &&
    value.every((item): item is number => typeof item === "number")
  );
}

function numberOf(record: Record<string, unknown>, key: string): number | null {
  const value = record[key];
  return typeof value === "number" ? value : null;
}

/** Return `value` as font data, or null when it is not the route's shape. */
export function parseFont(value: unknown): FontData | null {
  if (typeof value !== "object" || value === null) return null;
  const record = value as Record<string, unknown>;
  const [spacing, height, columns, cellWidth, cellHeight] = [
    "spacing",
    "height",
    "columns",
    "cell_width",
    "cell_height",
  ].map((key) => numberOf(record, key));
  const { widths, text_colors: colors, picture } = record;
  if (
    spacing === null ||
    spacing === undefined ||
    height === null ||
    height === undefined ||
    columns === null ||
    columns === undefined ||
    cellWidth === null ||
    cellWidth === undefined ||
    cellHeight === null ||
    cellHeight === undefined ||
    !isNumbers(widths) ||
    !isNumbers(colors) ||
    typeof picture !== "string"
  ) {
    return null;
  }
  return {
    spacing,
    height,
    columns,
    cell_width: cellWidth,
    cell_height: cellHeight,
    widths,
    text_colors: colors,
    picture,
  };
}

/** Return fonts 12 and 15 from the route's JSON, or null when either is missing. */
export function parseFonts(json: unknown): Fonts | null {
  if (typeof json !== "object" || json === null) return null;
  const record = json as Record<string, unknown>;
  const font12 = parseFont(record.font12);
  const font15 = parseFont(record.font15);
  return font12 === null || font15 === null ? null : { font12, font15 };
}

function colorOf(name: string, font: FontData): Rgb {
  const ink = INKS[name as Ink] as Rgb | undefined;
  return ink ?? colorRgb(name, font.text_colors);
}

/**
 * Return where each glyph of `line` goes: centred in its rectangle by the
 * game's rule (`left + (width >> 1) - (text width >> 1)`, the font's height
 * likewise), else from its x.
 */
export function placeLine(fonts: Fonts, line: DrawnLine): Placed {
  const font = line.font === 15 ? fonts.font15 : fonts.font12;
  const bytes = toBytes(line.text);
  let x = line.x;
  let y = line.y;
  const centred: Rect | undefined = line.centred;
  if (centred !== undefined) {
    x =
      centred.left +
      ((centred.right - centred.left) >> 1) -
      (measure(font, bytes) >> 1);
    y =
      centred.top + ((centred.bottom - centred.top) >> 1) - (font.height >> 1);
  }
  const drawn = drawBytes(font, bytes, line.ink, x, y);
  return {
    glyphs: drawn.glyphs,
    clipRight: line.clipRight ?? null,
    top: y,
  };
}

/**
 * Return the word plates of a placed line: one behind each run of glyphs in
 * an ink that does not read on the zone plate alone, covering the glyphs'
 * cells and one pixel round them, cut where the line is cut.
 */
export function wordPlates(placed: Placed, font: FontData): Rect[] {
  const plates: Rect[] = [];
  let run: { left: number; right: number; top: number; bottom: number } | null =
    null;
  for (const glyph of placed.glyphs) {
    if (readsBare(colorOf(glyph.color, font))) {
      run = null;
      continue;
    }
    const right = glyph.x + font.cell_width + 1;
    const bottom = glyph.y + font.cell_height + 1;
    if (run === null) {
      run = { left: glyph.x - 1, right, top: glyph.y - 1, bottom };
      plates.push(run);
    } else {
      run.right = right;
      run.bottom = Math.max(run.bottom, bottom);
    }
  }
  const cut = placed.clipRight;
  return plates
    .map((p) => (cut === null ? p : { ...p, right: Math.min(p.right, cut) }))
    .filter((p) => p.right > p.left);
}

function loadImage(name: string): Promise<HTMLImageElement | null> {
  return new Promise((resolve) => {
    const image = new Image();
    image.addEventListener("load", () => {
      resolve(image);
    });
    image.addEventListener("error", () => {
      logger.warn("a debriefing font picture did not load", name);
      resolve(null);
    });
    image.src = `${DEBRIEF_PATH}${name}`;
  });
}

function atlasOf(image: HTMLImageElement): HTMLCanvasElement {
  const canvas = document.createElement("canvas");
  canvas.width = image.naturalWidth;
  canvas.height = image.naturalHeight;
  canvas.getContext("2d")?.drawImage(image, 0, 0);
  return canvas;
}

/** Fetch the two fonts; null (with a WARNING) when any part is missing. */
export async function loadFonts(): Promise<LoadedFonts | null> {
  try {
    const response = await fetch(`${DEBRIEF_PATH}fonts.json`);
    if (!response.ok) {
      logger.warn("the debriefing fonts did not load", response.status);
      return null;
    }
    const fonts = parseFonts((await response.json()) as unknown);
    if (fonts === null) {
      logger.warn("the debriefing fonts are not the shape the page reads");
      return null;
    }
    const [one, two] = await Promise.all([
      loadImage(fonts.font12.picture),
      loadImage(fonts.font15.picture),
    ]);
    if (one === null || two === null) return null;
    return { ...fonts, atlas12: atlasOf(one), atlas15: atlasOf(two) };
  } catch (error) {
    logger.warn("the debriefing fonts could not be fetched", String(error));
    return null;
  }
}

export class InkPainter {
  readonly #inks = new Map<string, HTMLCanvasElement>();
  readonly #canvas: HTMLCanvasElement;
  #fonts: LoadedFonts | null = null;

  constructor(canvas: HTMLCanvasElement) {
    this.#canvas = canvas;
  }

  setFonts(fonts: LoadedFonts | null): void {
    this.#fonts = fonts;
    this.#inks.clear();
  }

  get ready(): boolean {
    return this.#fonts !== null;
  }

  #ink(name: string, big: boolean, fonts: LoadedFonts): HTMLCanvasElement {
    const key = `${big ? "15" : "12"} ${name}`;
    const found = this.#inks.get(key);
    if (found !== undefined) return found;
    const atlas = big ? fonts.atlas15 : fonts.atlas12;
    const canvas = document.createElement("canvas");
    canvas.width = atlas.width;
    canvas.height = atlas.height;
    const context = canvas.getContext("2d");
    if (context !== null) {
      context.drawImage(atlas, 0, 0);
      context.globalCompositeOperation = "source-in";
      const [r, g, b] = colorOf(name, big ? fonts.font15 : fonts.font12);
      context.fillStyle = `rgb(${String(r)} ${String(g)} ${String(b)})`;
      context.fillRect(0, 0, canvas.width, canvas.height);
    }
    this.#inks.set(key, canvas);
    return canvas;
  }

  /**
   * Clear the canvas, lay the zone `plates`, then every line's word plates,
   * then draw `lines` at `factor` times 1x.
   */
  paint(
    lines: readonly DrawnLine[],
    plates: readonly Rect[],
    factor: number,
  ): void {
    const canvas = this.#canvas;
    canvas.width = 640 * factor;
    canvas.height = 480 * factor;
    const fonts = this.#fonts;
    const context = canvas.getContext("2d");
    if (context === null || fonts === null) return;
    context.imageSmoothingEnabled = false;
    context.scale(factor, factor);
    const fill = (rects: readonly Rect[], alpha: number): void => {
      context.fillStyle = plateStyle(alpha);
      for (const rect of rects) {
        context.fillRect(
          rect.left,
          rect.top,
          rect.right - rect.left,
          rect.bottom - rect.top,
        );
      }
    };
    fill(plates, ZONE_ALPHA);
    const placedLines = lines.map((line) => {
      const big = line.font === 15;
      return { big, placed: placeLine(fonts, line) };
    });
    fill(
      placedLines.flatMap(({ big, placed }) =>
        wordPlates(placed, big ? fonts.font15 : fonts.font12),
      ),
      WORD_ALPHA,
    );
    for (const { big, placed } of placedLines) {
      const font = big ? fonts.font15 : fonts.font12;
      context.save();
      if (placed.clipRight !== null) {
        context.beginPath();
        context.rect(0, 0, placed.clipRight, 480);
        context.clip();
      }
      for (const glyph of placed.glyphs) {
        context.drawImage(
          this.#ink(glyph.color, big, fonts),
          (glyph.code % font.columns) * font.cell_width,
          Math.floor(glyph.code / font.columns) * font.cell_height,
          font.cell_width,
          font.cell_height,
          glyph.x,
          glyph.y,
          font.cell_width,
          font.cell_height,
        );
      }
      context.restore();
    }
  }
}
