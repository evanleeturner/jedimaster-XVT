import assert from "node:assert/strict";
import { afterEach, beforeEach, test } from "node:test";

import { makeBundle } from "./bundle.fixture.ts";
import { hasArea, Painter } from "./paint.ts";
import type { DrawRecord } from "./records.ts";
import type { Sprites } from "./sprites.ts";

// A canvas that writes down what it is asked to draw.
class FakeContext {
  readonly ops: string[] = [];
  fillStyle = "";
  globalCompositeOperation = "source-over";
  imageSmoothingEnabled = true;
  readonly images: Uint8ClampedArray[] = [];

  #note(name: string, args: readonly unknown[]): void {
    this.ops.push(`${name} ${args.map((a) => this.#show(a)).join(" ")}`.trim());
  }

  #show(value: unknown): string {
    if (typeof value === "number") return String(value);
    if (value instanceof FakeCanvas) return `<${value.name}>`;
    return "?";
  }

  save(): void {
    this.#note("save", []);
  }
  restore(): void {
    this.#note("restore", []);
  }
  beginPath(): void {
    this.#note("beginPath", []);
  }
  rect(...args: number[]): void {
    this.#note("rect", args);
  }
  clip(): void {
    this.#note("clip", []);
  }
  fillRect(...args: number[]): void {
    this.ops.push(`fillRect ${args.join(" ")} ${this.fillStyle}`);
  }
  drawImage(image: FakeCanvas, ...args: number[]): void {
    this.#note("drawImage", [image, ...args]);
  }
  createImageData(width: number, height: number): { data: Uint8ClampedArray } {
    return { data: new Uint8ClampedArray(width * height * 4) };
  }
  putImageData(image: { data: Uint8ClampedArray }): void {
    this.images.push(image.data);
    this.#note("putImageData", []);
  }
}

class FakeCanvas {
  width = 0;
  height = 0;
  id = "target";
  readonly context = new FakeContext();
  readonly name: string;
  constructor(name: string) {
    this.name = name;
  }
  getContext(): FakeContext {
    return this.context;
  }
}

const made: FakeCanvas[] = [];
const before = Object.getOwnPropertyDescriptor(globalThis, "document");

beforeEach(() => {
  made.length = 0;
  Object.defineProperty(globalThis, "document", {
    configurable: true,
    value: {
      createElement: () => {
        const canvas = new FakeCanvas(`made${String(made.length)}`);
        made.push(canvas);
        return canvas;
      },
    },
  });
});

afterEach(() => {
  if (before === undefined) Reflect.deleteProperty(globalThis, "document");
  else Object.defineProperty(globalThis, "document", before);
});

function canvasOf(name: string): FakeCanvas {
  return new FakeCanvas(name);
}

interface Setup {
  readonly painter: Painter;
  readonly panel: FakeContext;
  readonly target: FakeCanvas;
}

function setup(
  options: Partial<Parameters<typeof makeBundle>[0]> = {},
  sprites: Partial<Sprites> = {},
): Setup {
  const target = canvasOf("target");
  const bundle = makeBundle({
    backgrounds: { 0: "under-a.png", 1: "under-b.png" },
    teamBriefings: { 0: 0, 1: 0 },
    ...options,
  });
  const full: Sprites = {
    sheets: new Map([["mapicon0", canvasOf("sheet0") as never]]),
    planes: new Map(),
    atlas: canvasOf("atlas") as never,
    headingAtlas: canvasOf("headingAtlas") as never,
    under: new Map([
      ["under-a.png", canvasOf("underA") as never],
      ["under-b.png", canvasOf("underB") as never],
    ]),
    ...sprites,
  };
  const painter = new Painter(target as never, bundle, full);
  const first = made[0];
  assert.ok(first !== undefined);
  return { painter, panel: first.context, target };
}

function index(ops: readonly string[], prefix: string, from = 0): number {
  const at = ops.findIndex((op, i) => i >= from && op.startsWith(prefix));
  assert.ok(at >= 0, `${prefix} is drawn`);
  return at;
}

const GLYPH: DrawRecord = {
  kind: "glyph",
  code: 65,
  x: 5,
  y: 6,
  color: "white",
};

test("a frame is drawn background first, then the records, then the heading", () => {
  const { painter, panel } = setup();
  painter.paint(
    [
      { kind: "clip", left: 10, top: 10, right: 50, bottom: 50 },
      {
        kind: "fill",
        left: 20,
        top: 20,
        right: 30,
        bottom: 30,
        color: "white",
      },
      GLYPH,
    ],
    1,
  );
  const ops = panel.ops;
  const black = index(ops, "fillRect 0 0 360 240");
  const under = index(ops, "drawImage <underB>");
  const fill = index(ops, "fillRect 20 20 11 11");
  const glyph = index(ops, "drawImage <made", fill);
  const heading = index(ops, "drawImage <made", glyph + 1);
  assert.ok(black < under && under < fill && fill < glyph && glyph < heading);
});

test("the heading is drawn under the whole panel's clip, not the last clip", () => {
  const { painter, panel } = setup();
  painter.paint(
    [{ kind: "clip", left: 10, top: 10, right: 50, bottom: 50 }, GLYPH],
    0,
  );
  const ops = panel.ops;
  const smaller = index(ops, "rect 10 10 41 41");
  const last = ops.length - 1;
  const whole = ops.lastIndexOf("rect 0 0 360 240");
  assert.ok(whole > smaller);
  const headingGlyph = ops.findIndex(
    (op, i) => i > whole && op.startsWith("drawImage"),
  );
  assert.ok(headingGlyph > whole && headingGlyph < last + 1);
  assert.equal(
    ops.filter((op) => op === "save").length,
    ops.filter((op) => op === "restore").length,
  );
});

test("each glyph of the heading comes from font 12's atlas at the heading's place", () => {
  const { painter, panel } = setup({ heading: "A" });
  painter.paint([], 0);
  const glyphs = panel.ops.filter((op) => op.startsWith("drawImage <made"));
  assert.ok(glyphs.length >= 1);
  const [source, sx, sy, width, height, x, y] = (glyphs[0] ?? "")
    .replace("drawImage ", "")
    .split(" ");
  assert.equal(source?.startsWith("<made"), true);
  assert.deepEqual(
    [sx, sy, width, height, x, y],
    ["9", "64", "9", "16", "2", "-4"],
  );
  const ink = made.find((canvas) => `<${canvas.name}>` === source);
  assert.equal(ink?.context.ops[0], "drawImage <headingAtlas> 0 0");
});

test("a team whose background did not load is black under its records", () => {
  const { painter, panel } = setup(
    { headingFont: false },
    { under: new Map() },
  );
  painter.paint([], 0);
  assert.deepEqual(panel.ops, [
    "save",
    "fillRect 0 0 360 240 rgb(0 0 0)",
    "beginPath",
    "rect 0 0 360 240",
    "clip",
    "restore",
  ]);
});

test("a team's own background is the one drawn", () => {
  const { painter, panel } = setup();
  painter.paint([], 0);
  assert.ok(panel.ops.includes("drawImage <underA> 0 0"));
  assert.equal(panel.ops.includes("drawImage <underB> 0 0"), false);
  panel.ops.length = 0;
  painter.paint([], 1);
  assert.ok(panel.ops.includes("drawImage <underB> 0 0"));
  assert.equal(panel.ops.includes("drawImage <underA> 0 0"), false);
});

test("a list is black and its records, with no background and no heading", () => {
  const { painter, panel } = setup();
  painter.paint(
    [{ kind: "fill", left: 1, top: 1, right: 2, bottom: 2, color: "white" }],
    null,
  );
  assert.equal(
    panel.ops.some((op) => op.startsWith("drawImage")),
    false,
  );
  assert.deepEqual(
    panel.ops.filter((op) => op.startsWith("fillRect")),
    ["fillRect 0 0 360 240 rgb(0 0 0)", "fillRect 1 1 2 2 rgb(255 255 255)"],
  );
});

test("a frame of a bundle with no heading font draws no heading", () => {
  const { painter, panel } = setup({ headingFont: false });
  painter.paint([], 0);
  assert.deepEqual(panel.ops, [
    "save",
    "fillRect 0 0 360 240 rgb(0 0 0)",
    "drawImage <underA> 0 0",
    "beginPath",
    "rect 0 0 360 240",
    "clip",
    "restore",
  ]);
});

test("the panel is copied to the page's canvas whole", () => {
  const { painter, target } = setup();
  target.width = 720;
  target.height = 480;
  painter.paint([], 0);
  assert.deepEqual(target.context.ops, ["drawImage <made0> 0 0 720 480"]);
  assert.equal(target.context.imageSmoothingEnabled, false);
});

test("a rectangle draws only when its right is past its left and its bottom past its top", () => {
  assert.equal(hasArea({ left: 1, top: 1, right: 2, bottom: 2 }), true);
  assert.equal(hasArea({ left: 1, top: 1, right: 1, bottom: 5 }), false);
  assert.equal(hasArea({ left: 1, top: 1, right: 5, bottom: 1 }), false);
  assert.equal(hasArea({ left: 5, top: 1, right: 1, bottom: 5 }), false);
  assert.equal(hasArea({ left: 1, top: 5, right: 5, bottom: 1 }), false);
});

test("a fill or an outline with no area draws nothing", () => {
  const { painter, panel } = setup();
  painter.paint(
    [
      { kind: "fill", left: 4, top: 4, right: 4, bottom: 4, color: "white" },
      { kind: "outline", left: 8, top: 8, right: 9, bottom: 8, color: "white" },
      { kind: "fill", left: 9, top: 9, right: 3, bottom: 3, color: "white" },
    ],
    null,
  );
  assert.equal(panel.ops.filter((op) => op.startsWith("fillRect")).length, 1);
});

test("a line with its ends the wrong way round is drawn between them", () => {
  const { painter, panel } = setup();
  painter.paint(
    [
      { kind: "vline", x: 3, top: 9, bottom: 5, color: "white" },
      { kind: "hline", y: 4, left: 12, right: 10, color: "white" },
    ],
    null,
  );
  const lines = panel.ops.filter((op) => op.startsWith("fillRect")).slice(1);
  assert.deepEqual(lines, [
    "fillRect 3 5 1 5 rgb(255 255 255)",
    "fillRect 10 4 3 1 rgb(255 255 255)",
  ]);
});

test("a clip turned inside out is ignored and the one before stays", () => {
  const { painter, panel } = setup();
  painter.paint(
    [
      { kind: "clip", left: 10, top: 10, right: 20, bottom: 20 },
      { kind: "clip", left: 15, top: 15, right: 14, bottom: 30 },
      { kind: "clip", left: 15, top: 15, right: 30, bottom: 14 },
    ],
    null,
  );
  assert.equal(panel.ops.filter((op) => op.startsWith("rect")).length, 2);
});

test("a clip past the panel is cut by it and one outside it holds nothing", () => {
  const { painter, panel } = setup();
  painter.paint(
    [
      { kind: "clip", left: -10, top: -10, right: 400, bottom: 300 },
      { kind: "clip", left: 400, top: 10, right: 500, bottom: 20 },
    ],
    null,
  );
  assert.deepEqual(
    panel.ops.filter((op) => op.startsWith("rect")),
    ["rect 0 0 360 240", "rect 0 0 360 240", "rect 0 0 0 0"],
  );
});

test("a tint reads the plane of the sheet it names", () => {
  const grey = { width: 8, height: 12, planes: new Int8Array(96).fill(31) };
  const half = { width: 8, height: 12, planes: new Int8Array(96).fill(15) };
  const { painter, panel } = setup(
    {},
    {
      planes: new Map([
        ["greyicon", grey],
        ["mapicon2", half],
      ]),
    },
  );
  painter.paint(
    [
      { kind: "tint", sheet: "greyicon", index: 0, x: 0, y: 0, color: "white" },
      { kind: "tint", sheet: "mapicon2", index: 0, x: 0, y: 0, color: "white" },
      { kind: "tint", sheet: "mapicon4", index: 0, x: 0, y: 0, color: "white" },
    ],
    null,
  );
  assert.equal(panel.ops.filter((op) => op.startsWith("drawImage")).length, 2);
  const [first, second] = made.slice(1).map((c) => c.context.images[0]?.[0]);
  assert.deepEqual([first, second], [255, 123]);
});

test("a plain icon of the grey sheet is drawn when the sheet is there", () => {
  const { painter, panel } = setup(
    {},
    { sheets: new Map([["greyicon", canvasOf("grey") as never]]) },
  );
  painter.paint(
    [{ kind: "icon", sheet: "greyicon", index: 0, x: 3, y: 4 }],
    null,
  );
  assert.ok(panel.ops.includes("drawImage <grey> 0 0 8 12 3 4 8 12"));
});
