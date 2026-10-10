// Paint lists and frames of the briefing panel and hand back their pixels.
//
// Purpose: the page's half of the pixel check: draw what the setup screen
// draws (a list of records on black, or a mission's briefing played to a
// frame) with the page's own painter, and return the panel's 360 x 240
// pixels packed to the game's 5-6-5 screen, so they can be compared with the
// game's own screens.
// Flow: `paintJobs` loads the pictures of each bundle once, then for each job
// paints a canvas and reads it back; `base64Of` makes the bytes text.
// Invariants: a list job is painted with no team (black, the records, no
// heading); a frame job plays the briefing from its start, one `updateFrame`
// and `drawFrame` per frame, and paints the last frame with the team; the
// pictures come from the art route of the page that runs this.

import type { BriefingBundle } from "../generated/briefing.ts";
import { loadBriefing, updateFrame } from "./core.ts";
import { drawFrame } from "./draw.ts";
import { PANEL_H, PANEL_W } from "./layout.ts";
import { readRecords } from "./lines.ts";
import { packPanel } from "./pack.ts";
import { Painter } from "./paint.ts";
import type { DrawRecord } from "./records.ts";
import type { Sprites } from "./sprites.ts";
import { loadSprites } from "./sprites.ts";

export interface ListJob {
  readonly name: string;
  readonly kind: "list";
  readonly bundle: number;
  readonly text: string;
}

export interface FrameJob {
  readonly name: string;
  readonly kind: "frame";
  readonly bundle: number;
  readonly team: number;
  readonly frame: number;
}

export type PixelJob = ListJob | FrameJob;

const CHUNK = 0x2000;

/** Return `bytes` as base64 text. */
export function base64Of(bytes: Uint8Array): string {
  let text = "";
  for (let at = 0; at < bytes.length; at += CHUNK) {
    text += String.fromCharCode(...bytes.subarray(at, at + CHUNK));
  }
  return btoa(text);
}

function recordsOfFrame(
  bundle: BriefingBundle,
  job: FrameJob,
): readonly DrawRecord[] {
  const player = loadBriefing(bundle, job.team);
  let records: readonly DrawRecord[] = [];
  for (let frame = 0; frame < job.frame; frame += 1) {
    updateFrame(player);
    records = drawFrame(player);
  }
  return records;
}

function pixelsOf(canvas: HTMLCanvasElement): Uint8Array {
  const context = canvas.getContext("2d", { willReadFrequently: true });
  if (context === null) throw new Error("invariant: a 2d canvas is available");
  return packPanel(context.getImageData(0, 0, PANEL_W, PANEL_H).data);
}

/**
 * Return, for each job, its name and the panel's packed pixels as base64.
 * Throws when a list holds a line that is no record or a job names a bundle
 * that is not given.
 */
export async function paintJobs(
  bundles: readonly BriefingBundle[],
  jobs: readonly PixelJob[],
): Promise<Record<string, string>> {
  const sprites = new Map<number, Sprites>();
  const out: Record<string, string> = {};
  for (const job of jobs) {
    const bundle = bundles[job.bundle];
    if (bundle === undefined)
      throw new Error(`no bundle ${String(job.bundle)}`);
    let loaded = sprites.get(job.bundle);
    if (loaded === undefined) {
      loaded = await loadSprites(bundle);
      sprites.set(job.bundle, loaded);
    }
    const canvas = document.createElement("canvas");
    canvas.width = PANEL_W;
    canvas.height = PANEL_H;
    const painter = new Painter(canvas, bundle, loaded);
    if (job.kind === "list") {
      painter.paint(readRecords(job.text, bundle.font), null);
    } else {
      painter.paint(recordsOfFrame(bundle, job), job.team);
    }
    out[job.name] = base64Of(pixelsOf(canvas));
  }
  return out;
}
