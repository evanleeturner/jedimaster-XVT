// The pictures the painter draws from: icon sheets, the grey sheet, the font.
//
// Purpose: load the briefing's art from the page's own server and prepare it
// for exact drawing: icon sheets (the grey one too) pass through the 565 screen
// once and each also becomes a plane of intensities (a tint reads the plane of
// the sheet it names), the font atlas stays white ink.
// Flow: `loadSprites` fetches each picture named in the bundle (the art
// route), decodes it and returns a `Sprites`; a missing picture is left out
// and the painter skips what needs it.
// Invariants: pictures come only from `/art/briefing/<name>`; an icon pixel
// with alpha 0 stays transparent; every other pixel is opaque; a picture under
// the map is drawn as it comes (it is already a 565 screen widened to 8 bits).

import type { BriefingBundle } from "../generated/briefing.ts";
import { logger } from "../logger.ts";
import { intensityOf, through565 } from "./colors.ts";

export const ART_PATH = "/art/briefing/";

export interface Grey {
  readonly width: number;
  readonly height: number;
  /** Intensity 0 to 31 per pixel, or -1 where the pixel is transparent. */
  readonly planes: Int8Array;
}

export interface Sprites {
  /** Every sheet, the grey one included, as the screen shows it. */
  readonly sheets: ReadonlyMap<string, HTMLCanvasElement>;
  /** The intensities of every sheet's pixels, by sheet name. */
  readonly planes: ReadonlyMap<string, Grey>;
  readonly atlas: HTMLCanvasElement | null;
  /** Font 12's atlas, for the heading. */
  readonly headingAtlas: HTMLCanvasElement | null;
  /** The pictures under the map, by art name, as the screen shows them. */
  readonly under: ReadonlyMap<string, HTMLCanvasElement>;
}

function loadImage(name: string): Promise<HTMLImageElement | null> {
  return new Promise((resolve) => {
    const image = new Image();
    image.addEventListener("load", () => {
      resolve(image);
    });
    image.addEventListener("error", () => {
      logger.warn("a briefing picture did not load", name);
      resolve(null);
    });
    image.src = `${ART_PATH}${name}`;
  });
}

function pixelsOf(image: HTMLImageElement): ImageData | null {
  const canvas = document.createElement("canvas");
  canvas.width = image.naturalWidth;
  canvas.height = image.naturalHeight;
  const context = canvas.getContext("2d", { willReadFrequently: true });
  if (context === null) return null;
  context.drawImage(image, 0, 0);
  return context.getImageData(0, 0, canvas.width, canvas.height);
}

function quantized(data: ImageData): HTMLCanvasElement {
  const { data: bytes } = data;
  for (let at = 0; at < bytes.length; at += 4) {
    if ((bytes[at + 3] ?? 0) === 0) continue;
    const [r, g, b] = through565([
      bytes[at] ?? 0,
      bytes[at + 1] ?? 0,
      bytes[at + 2] ?? 0,
    ]);
    bytes[at] = r;
    bytes[at + 1] = g;
    bytes[at + 2] = b;
  }
  const canvas = document.createElement("canvas");
  canvas.width = data.width;
  canvas.height = data.height;
  canvas.getContext("2d")?.putImageData(data, 0, 0);
  return canvas;
}

function greyOf(data: ImageData): Grey {
  const planes = new Int8Array(data.width * data.height);
  for (let at = 0; at < planes.length; at += 1) {
    const alpha = data.data[at * 4 + 3] ?? 0;
    planes[at] = alpha === 0 ? -1 : intensityOf(data.data[at * 4 + 2] ?? 0);
  }
  return { width: data.width, height: data.height, planes };
}

function atlasOf(image: HTMLImageElement): HTMLCanvasElement {
  const canvas = document.createElement("canvas");
  canvas.width = image.naturalWidth;
  canvas.height = image.naturalHeight;
  canvas.getContext("2d")?.drawImage(image, 0, 0);
  return canvas;
}

/** Load the bundle's pictures; resolve with whatever could be loaded. */
export async function loadSprites(bundle: BriefingBundle): Promise<Sprites> {
  const sheets = new Map<string, HTMLCanvasElement>();
  const planes = new Map<string, Grey>();
  const names = [
    ...bundle.sheets,
    ...(bundle.grey_sheet === null ? [] : [bundle.grey_sheet]),
  ].map((sheet) => [sheet.name, sheet.picture] as const);
  const underNames = [...new Set(bundle.teams.map((t) => t.background))];
  const [loaded, atlasImage, headingImage, underImages] = await Promise.all([
    Promise.all(names.map(([, picture]) => loadImage(picture))),
    loadImage(bundle.font.picture),
    bundle.heading_font === null
      ? null
      : loadImage(bundle.heading_font.picture),
    Promise.all(underNames.map((name) => loadImage(name))),
  ]);
  names.forEach(([name], i) => {
    const image = loaded[i];
    const data = image === null || image === undefined ? null : pixelsOf(image);
    if (data === null) return;
    planes.set(name, greyOf(data));
    sheets.set(name, quantized(data));
  });
  const under = new Map<string, HTMLCanvasElement>();
  underNames.forEach((name, i) => {
    const image = underImages[i];
    if (image !== null && image !== undefined) under.set(name, atlasOf(image));
  });
  return {
    sheets,
    planes,
    atlas: atlasImage === null ? null : atlasOf(atlasImage),
    headingAtlas: headingImage === null ? null : atlasOf(headingImage),
    under,
  };
}
