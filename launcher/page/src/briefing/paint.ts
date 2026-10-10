// Draw a frame's records on a canvas.
//
// Purpose: show the 360 x 240 panel: clips, lines, icons, tinted icons,
// fills and outlines with both edges, and glyphs in their colors, every color
// through the 565 screen.
// Flow: `Painter.paint` draws the records into a 360 x 240 canvas that is
// not on the page, then copies it to the page's canvas at the layout's
// scale, without smoothing (the browser smooths the shown size when asked).
// Invariants: a rectangle includes both edges; a clip is cut by the panel; a
// tinted icon draws every pixel of the grey sheet that is not transparent,
// each channel of the tint times the pixel's intensity over 31.

import type { BriefingBundle } from "../generated/briefing.ts";
import type { Rgb } from "./colors.ts";
import { colorRgb, exactColor, tintRgb } from "./colors.ts";
import type { PanelLayout } from "./layout.ts";
import { PANEL_H, PANEL_W } from "./layout.ts";
import type { DrawRecord } from "./records.ts";
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

export class Painter {
  readonly #target: HTMLCanvasElement;
  readonly #bundle: BriefingBundle;
  readonly #sprites: Sprites;
  readonly #panel = make(PANEL_W, PANEL_H);
  readonly #inks = new Map<string, HTMLCanvasElement>();
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

  #ink(name: string): HTMLCanvasElement | null {
    const atlas = this.#sprites.atlas;
    if (atlas === null) return null;
    const found = this.#inks.get(name);
    if (found !== undefined) return found;
    const canvas = make(atlas.width, atlas.height);
    const context = contextOf(canvas);
    context.drawImage(atlas, 0, 0);
    context.globalCompositeOperation = "source-in";
    context.fillStyle = css(this.#rgb(name));
    context.fillRect(0, 0, canvas.width, canvas.height);
    this.#inks.set(name, canvas);
    return canvas;
  }

  #tinted(index: number, color: string): HTMLCanvasElement | null {
    const grey = this.#sprites.grey;
    const box = this.#bundle.boxes[index];
    if (grey === null || box === undefined) return null;
    const key = `${String(index)} ${color}`;
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

  /** Draw `records` into the panel and copy it to the page's canvas. */
  paint(records: readonly DrawRecord[]): void {
    const context = contextOf(this.#panel);
    context.save();
    context.fillStyle = "rgb(0 0 0)";
    context.fillRect(0, 0, PANEL_W, PANEL_H);
    context.beginPath();
    context.rect(0, 0, PANEL_W, PANEL_H);
    context.clip();
    for (const record of records) this.#draw(context, record);
    context.restore();
    const out = contextOf(this.#target);
    out.imageSmoothingEnabled = false;
    out.drawImage(this.#panel, 0, 0, this.#target.width, this.#target.height);
  }

  #draw(context: CanvasRenderingContext2D, record: DrawRecord): void {
    switch (record.kind) {
      case "clip": {
        context.restore();
        context.save();
        const left = Math.max(0, record.left);
        const top = Math.max(0, record.top);
        const right = Math.min(PANEL_W - 1, record.right);
        const bottom = Math.min(PANEL_H - 1, record.bottom);
        context.beginPath();
        context.rect(left, top, right - left + 1, bottom - top + 1);
        context.clip();
        return;
      }
      case "vline":
        context.fillStyle = css(this.#rgb(record.color));
        context.fillRect(
          record.x,
          record.top,
          1,
          record.bottom - record.top + 1,
        );
        return;
      case "hline":
        context.fillStyle = css(this.#rgb(record.color));
        context.fillRect(
          record.left,
          record.y,
          record.right - record.left + 1,
          1,
        );
        return;
      case "fill":
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
        const canvas = this.#tinted(record.index, record.color);
        if (canvas !== null) context.drawImage(canvas, record.x, record.y);
        return;
      }
      case "glyph":
        this.#glyph(context, record.code, record.x, record.y, record.color);
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
    code: number,
    x: number,
    y: number,
    color: string,
  ): void {
    const ink = this.#ink(color);
    const {
      columns,
      cell_width: cellWidth,
      cell_height: cellHeight,
    } = this.#bundle.font;
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
