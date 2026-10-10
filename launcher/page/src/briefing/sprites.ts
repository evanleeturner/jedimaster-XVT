// The pictures the painter draws from: icon sheets, the grey sheet, the font.
//
// Purpose: load the briefing's art from the page's own server and prepare it
// for exact drawing: icon sheets pass through the 565 screen once, the grey
// sheet becomes a plane of intensities, the font atlas stays white ink.
// Flow: `loadSprites` fetches each picture named in the bundle (the art
// route), decodes it and returns a `Sprites`; a missing picture is left out
// and the painter skips what needs it.
// Invariants: pictures come only from `/art/briefing/<name>`; an icon pixel
// with alpha 0 stays transparent; every other pixel is opaque.

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
  readonly sheets: ReadonlyMap<string, HTMLCanvasElement>;
  readonly grey: Grey | null;
  readonly atlas: HTMLCanvasElement | null;
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
  const names = bundle.sheets.map(
    (sheet) => [sheet.name, sheet.picture] as const,
  );
  const [loaded, greyImage, atlasImage] = await Promise.all([
    Promise.all(names.map(([, picture]) => loadImage(picture))),
    bundle.grey_sheet === null ? null : loadImage(bundle.grey_sheet.picture),
    loadImage(bundle.font.picture),
  ]);
  names.forEach(([name], i) => {
    const data =
      loaded[i] === null || loaded[i] === undefined
        ? null
        : pixelsOf(loaded[i]);
    if (data !== null) sheets.set(name, quantized(data));
  });
  const greyData = greyImage === null ? null : pixelsOf(greyImage);
  return {
    sheets,
    grey: greyData === null ? null : greyOf(greyData),
    atlas: atlasImage === null ? null : atlasOf(atlasImage),
  };
}
