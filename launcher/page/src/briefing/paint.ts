// Draw a frame's records on a canvas.
//
// Purpose: show the 360 x 240 panel: the picture under the map, clips, lines,
// icons, tinted icons, fills and outlines with both edges, glyphs in their
// colors (every color through the 565 screen), and the heading.
// Flow: `Painter.paint` draws, into a 360 x 240 canvas that is not on the
// page, the team's background (black when there is none), the records and
// then the heading under the whole panel's clip; a list of records with no
// team is black and the records alone. It copies the canvas to the page's
// canvas at the layout's scale, without smoothing (the browser smooths the
// shown size when asked).
// Invariants: a rectangle includes both edges but draws only with an area; a
// line with its ends the wrong way round is drawn between them all the same; a
// clip turned inside out is ignored (the one before stays) and one past the
// panel is cut by it; a tinted icon draws every pixel of its sheet that is
// not transparent, each 5-6-5 field of the tint times the pixel's intensity
// (the blue field of that sheet's pixel) over 31.

import type { BriefingBundle, FontData } from "../generated/briefing.ts";
import type { Rgb } from "./colors.ts";
import { colorRgb, exactColor, tintRgb } from "./colors.ts";
import { headingGlyphs } from "./heading.ts";
import type { PanelLayout } from "./layout.ts";
import { PANEL_H, PANEL_W } from "./layout.ts";
import type { DrawRecord, GlyphRecord } from "./records.ts";
import type { Sprites } from "./sprites.ts";

function css(rgb: Rgb): string {
  return `rgb(${String(rgb[0])} ${String(rgb[1])} ${String(rgb[2])})`;
}

function make(width: number, height: number): HTMLCanvasElement {
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  return canvas;
}

function contextOf(canvas: HTMLCanvasElement): CanvasRenderingContext2D {
  const context = canvas.getContext("2d");
  if (context === null) throw new Error("invariant: a 2d canvas is available");
  return context;
}

let sizeSheet: CSSStyleSheet | null = null;

function shownSize(): CSSStyleSheet {
  if (sizeSheet === null) {
    sizeSheet = new CSSStyleSheet();
    document.adoptedStyleSheets = [...document.adoptedStyleSheets, sizeSheet];
  }
  return sizeSheet;
}

/**
 * Return whether a rectangle draws anything: the game draws a fill or an
 * outline only when its right edge is past its left and its bottom past its
 * top (a box one pixel wide or high, or turned inside out, draws nothing).
 */
export function hasArea(box: {
  left: number;
  top: number;
  right: number;
  bottom: number;
}): boolean {
  return box.right > box.left && box.bottom > box.top;
}

export class Painter {
  readonly #target: HTMLCanvasElement;
  readonly #bundle: BriefingBundle;
  readonly #sprites: Sprites;
  readonly #panel = make(PANEL_W, PANEL_H);
  readonly #inks = new Map<string, HTMLCanvasElement>();
  readonly #headingInks = new Map<string, HTMLCanvasElement>();
  readonly #tints = new Map<string, HTMLCanvasElement>();

  constructor(
    target: HTMLCanvasElement,
    bundle: BriefingBundle,
    sprites: Sprites,
  ) {
    this.#target = target;
    this.#bundle = bundle;
    this.#sprites = sprites;
  }

  /**
   * Size the page's canvas for `layout`; the next `paint` fills it. The shown
   * size is a rule in a constructed style sheet, never a `style` attribute.
   */
  place(layout: PanelLayout): void {
    this.#target.width = PANEL_W * layout.factor;
    this.#target.height = PANEL_H * layout.factor;
    const sheet = shownSize();
    const rendering = layout.smooth ? "auto" : "pixelated";
    sheet.replaceSync(
      `#${this.#target.id} { width: ${String(layout.width)}px; ` +
        `height: ${String(layout.height)}px; image-rendering: ${rendering}; }`,
    );
  }

  #rgb(name: string): Rgb {
    return colorRgb(name, this.#bundle.font.text_colors);
  }

  #ink(name: string, heading: boolean): HTMLCanvasElement | null {
    const atlas = heading ? this.#sprites.headingAtlas : this.#sprites.atlas;
    if (atlas === null) return null;
    const inks = heading ? this.#headingInks : this.#inks;
    const found = inks.get(name);
    if (found !== undefined) return found;
    const canvas = make(atlas.width, atlas.height);
    const context = contextOf(canvas);
    context.drawImage(atlas, 0, 0);
    context.globalCompositeOperation = "source-in";
    context.fillStyle = css(this.#rgb(name));
    context.fillRect(0, 0, canvas.width, canvas.height);
    inks.set(name, canvas);
    return canvas;
  }

  #tinted(
    sheet: string,
    index: number,
    color: string,
  ): HTMLCanvasElement | null {
    const grey = this.#sprites.planes.get(sheet);
    const box = this.#bundle.boxes[index];
    if (grey === undefined || box === undefined) return null;
    const key = `${sheet} ${String(index)} ${color}`;
    const found = this.#tints.get(key);
    if (found !== undefined) return found;
    const width = box.right - box.left + 1;
    const height = box.bottom - box.top + 1;
    const canvas = make(width, height);
    const context = contextOf(canvas);
    const data = context.createImageData(width, height);
    const tint = exactColor(color, this.#bundle.font.text_colors);
    for (let y = 0; y < height; y += 1) {
      for (let x = 0; x < width; x += 1) {
        const plane =
          grey.planes[(box.top + y) * grey.width + box.left + x] ?? -1;
        if (plane < 0) continue;
        const [r, g, b] = tintRgb(tint, plane);
        const at = (y * width + x) * 4;
        data.data.set([r, g, b, 255], at);
      }
    }
    context.putImageData(data, 0, 0);
    this.#tints.set(key, canvas);
    return canvas;
  }

  /**
   * Draw the panel and copy it to the page's canvas. With a `team`, the panel
   * is the screen's: that team's background (black when it did not load), the
   * `records`, and the team's heading. With `null` it is a list of records on
   * black, nothing else.
   */
  paint(records: readonly DrawRecord[], team: number | null): void {
    const context = contextOf(this.#panel);
    context.save();
    context.fillStyle = "rgb(0 0 0)";
    context.fillRect(0, 0, PANEL_W, PANEL_H);
    if (team !== null) this.#underlay(context, team);
    context.beginPath();
    context.rect(0, 0, PANEL_W, PANEL_H);
    context.clip();
    for (const record of records) this.#draw(context, record);
    if (team !== null) this.#heading(context, team);
    context.restore();
    const out = contextOf(this.#target);
    out.imageSmoothingEnabled = false;
    out.drawImage(this.#panel, 0, 0, this.#target.width, this.#target.height);
  }

  #underlay(context: CanvasRenderingContext2D, team: number): void {
    const name = this.#bundle.teams.find((t) => t.team === team)?.background;
    const picture =
      name === undefined ? undefined : this.#sprites.under.get(name);
    if (picture !== undefined) context.drawImage(picture, 0, 0);
  }

  #heading(context: CanvasRenderingContext2D, team: number): void {
    const font = this.#bundle.heading_font;
    if (font === null) return;
    context.restore();
    context.save();
    context.beginPath();
    context.rect(0, 0, PANEL_W, PANEL_H);
    context.clip();
    for (const glyph of headingGlyphs(this.#bundle, team)) {
      this.#glyph(context, glyph, font, true);
    }
  }

  #draw(context: CanvasRenderingContext2D, record: DrawRecord): void {
    switch (record.kind) {
      case "clip": {
        if (record.right < record.left || record.bottom < record.top) return;
        context.restore();
        context.save();
        const left = Math.max(0, record.left);
        const top = Math.max(0, record.top);
        const right = Math.min(PANEL_W - 1, record.right);
        const bottom = Math.min(PANEL_H - 1, record.bottom);
        context.beginPath();
        if (right >= left && bottom >= top) {
          context.rect(left, top, right - left + 1, bottom - top + 1);
        } else {
          context.rect(0, 0, 0, 0);
        }
        context.clip();
        return;
      }
      case "vline": {
        const top = Math.min(record.top, record.bottom);
        const bottom = Math.max(record.top, record.bottom);
        context.fillStyle = css(this.#rgb(record.color));
        context.fillRect(record.x, top, 1, bottom - top + 1);
        return;
      }
      case "hline": {
        const left = Math.min(record.left, record.right);
        const right = Math.max(record.left, record.right);
        context.fillStyle = css(this.#rgb(record.color));
        context.fillRect(left, record.y, right - left + 1, 1);
        return;
      }
      case "fill":
        if (!hasArea(record)) return;
        context.fillStyle = css(this.#rgb(record.color));
        context.fillRect(
          record.left,
          record.top,
          record.right - record.left + 1,
          record.bottom - record.top + 1,
        );
        return;
      case "outline":
        this.#outline(context, record);
        return;
      case "icon":
        this.#icon(context, record.sheet, record.index, record.x, record.y);
        return;
      case "tint": {
        const canvas = this.#tinted(record.sheet, record.index, record.color);
        if (canvas !== null) context.drawImage(canvas, record.x, record.y);
        return;
      }
      case "glyph":
        this.#glyph(context, record, this.#bundle.font, false);
    }
  }

  #outline(
    context: CanvasRenderingContext2D,
    record: {
      left: number;
      top: number;
      right: number;
      bottom: number;
      color: string;
    },
  ): void {
    if (!hasArea(record)) return;
    context.fillStyle = css(this.#rgb(record.color));
    const { left, top, right, bottom } = record;
    context.fillRect(left, top, right - left + 1, 1);
    context.fillRect(left, bottom, right - left + 1, 1);
    context.fillRect(left, top, 1, bottom - top + 1);
    context.fillRect(right, top, 1, bottom - top + 1);
  }

  #icon(
    context: CanvasRenderingContext2D,
    sheet: string,
    index: number,
    x: number,
    y: number,
  ): void {
    const image = this.#sprites.sheets.get(sheet);
    const box = this.#bundle.boxes[index];
    if (image === undefined || box === undefined) return;
    const width = box.right - box.left + 1;
    const height = box.bottom - box.top + 1;
    context.drawImage(
      image,
      box.left,
      box.top,
      width,
      height,
      x,
      y,
      width,
      height,
    );
  }

  #glyph(
    context: CanvasRenderingContext2D,
    { code, x, y, color }: GlyphRecord,
    font: FontData,
    heading: boolean,
  ): void {
    const ink = this.#ink(color, heading);
    const { columns, cell_width: cellWidth, cell_height: cellHeight } = font;
    if (ink === null) return;
    const sx = (code % columns) * cellWidth;
    const sy = Math.floor(code / columns) * cellHeight;
    context.drawImage(
      ink,
      sx,
      sy,
      cellWidth,
      cellHeight,
      x,
      y,
      cellWidth,
      cellHeight,
    );
  }
}
