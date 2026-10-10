import assert from "node:assert/strict";
import { test } from "node:test";

import type { Ev } from "./bundle.fixture.ts";
import { briefing, group, makeBundle } from "./bundle.fixture.ts";
import type { Player } from "./core.ts";
import { loadBriefing, updateFrame } from "./core.ts";
import { drawFrame, pageLine, project } from "./draw.ts";
import { recordLines } from "./sheet.ts";

const DOTS = Array.from({ length: 8 }, () => ({ x: 0, y: 0, enabled: true }));

function setup(
  list: readonly Ev[],
  more: {
    captions?: string[];
    labels?: string[];
    groups?: ReturnType<typeof group>[];
    team?: number;
  } = {},
): Player {
  const bundle = makeBundle({
    briefings: [
      briefing({ events: list, captions: more.captions, labels: more.labels }),
    ],
    groups: more.groups ?? [],
    teamBriefings: { 0: 0, 1: 0 },
  });
  return loadBriefing(bundle, more.team ?? 0);
}

function lines(player: Player): string[] {
  return recordLines(drawFrame(player), player.bundle.font);
}

function texts(player: Player): string[] {
  return lines(player).filter(
    (l) => /^(text|fill) /.test(l) && !l.includes("Pg"),
  );
}

test("the map point is projected from the center at the zoom", () => {
  const player = setup([
    [0, 6, 100, -40],
    [0, 7, 64, 32],
  ]);
  assert.deepEqual(project(player, 100, -40), { x: 180, y: 104 });
  assert.deepEqual(project(player, 228, -40), { x: 212, y: 104 });
  assert.deepEqual(project(player, 100, 88), { x: 180, y: 120 });
});

test("division drops the fraction toward zero", () => {
  const player = setup([]);
  assert.deepEqual(project(player, -100, -100), { x: 168, y: 92 });
  assert.deepEqual(project(player, 100, 100), { x: 192, y: 116 });
});

test("the projection is cut to 16 bits", () => {
  const player = setup([[0, 7, 32767, 32767]]);
  assert.equal(project(player, 32767, 0).x < 0, true);
});

test("a frame begins with the map's two clips and ends with the page line", () => {
  const out = lines(setup([]));
  assert.deepEqual(out.slice(0, 2), ["clip 0 0 360 208", "clip 0 0 359 208"]);
  assert.equal(out.at(-1), 'text 300 194 white "Pg. 0"');
});

test("the grid at zoom 32 and center 0 has minor lines on phases 2 and major on 0", () => {
  const out = lines(setup([]));
  const kind = (word: string): string[] => out.filter((l) => l.endsWith(word));
  assert.deepEqual(
    kind("minor")
      .filter((l) => l.startsWith("vline"))
      .map((l) => l.split(" ")[1]),
    ["-12", "20", "84", "116", "148", "212", "244", "276", "340"],
  );
  assert.deepEqual(kind("major"), [
    "vline 52 0 208 major",
    "vline 180 0 208 major",
    "vline 308 0 208 major",
    "hline -24 0 360 major",
    "hline 104 0 360 major",
  ]);
  assert.equal(kind("minor").filter((l) => l.startsWith("hline")).length, 6);
});

test("the grid draws all its minor lines first, then the major ones", () => {
  const out = lines(setup([]));
  const last = out.map((l) => l.endsWith("minor")).lastIndexOf(true);
  const first = out.findIndex((l) => l.endsWith("major"));
  assert.equal(last < first, true);
});

test("at zoom 16 only phase 2 lines are minor; under 16 there are none", () => {
  const mid = lines(setup([[0, 7, 16, 16]]));
  assert.deepEqual(
    mid
      .filter((l) => l.startsWith("vline") && l.endsWith("minor"))
      .map((l) => l.split(" ")[1]),
    ["20", "84", "148", "212", "276", "340"],
  );
  assert.deepEqual(
    mid
      .filter((l) => l.startsWith("vline") && l.endsWith("major"))
      .map((l) => l.split(" ")[1]),
    ["-12", "52", "116", "180", "244", "308"],
  );
  const small = lines(setup([[0, 7, 8, 8]]));
  assert.equal(
    small.some((l) => l.endsWith("minor")),
    false,
  );
  assert.equal(
    small.some((l) => l.endsWith("major")),
    true,
  );
});

test("at zoom 32 or more every phase but 0 is minor; the zoom of x decides for both axes", () => {
  const out = lines(setup([[0, 7, 64, 16]]));
  const at = (kind: string, word: string): string[] =>
    out
      .filter((l) => l.startsWith(kind) && l.endsWith(word))
      .map((l) => l.split(" ")[1] ?? "");
  assert.deepEqual(at("vline", "minor"), ["-12", "52", "116", "244", "308"]);
  assert.deepEqual(at("vline", "major"), ["180"]);
  assert.equal(at("hline", "minor").length, 11);
  assert.deepEqual(at("hline", "major"), ["40", "104", "168"]);
});

test("a flight group is an icon from its box, centered on its point", () => {
  const player = setup([], { groups: [group({ iff: 2 })] });
  assert.deepEqual(
    lines(player).filter((l) => l.startsWith("icon")),
    ["icon mapicon2 0 176 98"],
  );
});

test("the sheet follows the IFF: 0 to 3, 4 shares 1, 5 is sheet 4, others sheet 0", () => {
  const sheets = [0, 1, 2, 3, 4, 5, 6, 200].map((iff) => {
    const out = lines(setup([], { groups: [group({ iff })] }));
    return out.find((l) => l.startsWith("icon"))?.split(" ")[1];
  });
  assert.deepEqual(sheets, [
    "mapicon0",
    "mapicon1",
    "mapicon2",
    "mapicon3",
    "mapicon1",
    "mapicon4",
    "mapicon0",
    "mapicon0",
  ]);
});

test("the briefing point of the briefing is the one used", () => {
  const points = DOTS.map((p, i) => ({ ...p, x: 32 * i, enabled: i === 3 }));
  const player = setup([], { groups: [group({ points })] });
  assert.equal(
    lines(player).some((l) => l.startsWith("icon")),
    false,
  );
  const second = loadBriefing(
    makeBundle({
      briefings: [briefing({ index: 3 })],
      teamBriefings: { 0: 3 },
      groups: [group({ points })],
    }),
    0,
  );
  assert.deepEqual(
    lines(second).filter((l) => l.startsWith("icon")),
    ["icon mapicon0 0 188 98"],
  );
});

test("a craft without a box draws no icon", () => {
  const player = setup([], {
    groups: [group({ craft_type: 106 }), group({ craft_type: 255 })],
  });
  assert.equal(
    lines(player).some((l) => l.startsWith("icon")),
    false,
  );
});

test("a player number is counted over drawn groups of the viewer's team", () => {
  const groups = [
    group({ player_number: 5 }),
    group({ player_number: 0 }),
    group({
      player_number: 9,
      points: DOTS.map((p) => ({ ...p, enabled: false })),
    }),
    group({ player_number: 2, team: 1 }),
    group({ player_number: 3 }),
  ];
  const out = lines(setup([], { groups }));
  assert.deepEqual(
    out.filter((l) => l.startsWith("text") && !l.includes("Pg")),
    ['text 184 110 white "1"', 'text 184 110 white "2"'],
  );
  const other = lines(setup([], { groups, team: 1 }));
  assert.deepEqual(
    other.filter((l) => l.startsWith("text") && !l.includes("Pg")),
    ['text 184 110 white "1"'],
  );
});

test("a new marker draws one ring of four tinted copies, 16 away, in base + 7", () => {
  const player = setup([[1, 9, 0]], { groups: [group()] });
  updateFrame(player);
  const tints = lines(player).filter((l) => l.startsWith("tint"));
  assert.deepEqual(tints, [
    "tint greyicon 0 160 82 shade 7",
    "tint greyicon 0 192 82 shade 7",
    "tint greyicon 0 160 114 shade 7",
    "tint greyicon 0 192 114 shade 7",
  ]);
});

test("a young marker draws age + 1 rings, each two pixels nearer and two shades up", () => {
  const player = setup([[1, 9, 0]], { groups: [group({ iff: 1 })] });
  for (let i = 0; i < 3; i += 1) updateFrame(player);
  const tints = lines(player).filter((l) => l.startsWith("tint"));
  assert.equal(tints.length, 4 * 3);
  assert.equal(tints[0], "tint greyicon 0 160 82 shade 11");
  assert.equal(tints[4], "tint greyicon 0 162 84 shade 13");
  assert.equal(tints[8], "tint greyicon 0 164 86 shade 15");
});

test("a marker of age 4 to 7 draws four rings from offset 2 * (11 - age)", () => {
  const player = setup([[1, 9, 0]], { groups: [group()] });
  for (let i = 0; i < 6; i += 1) updateFrame(player);
  const tints = lines(player).filter((l) => l.startsWith("tint"));
  assert.equal(tints.length, 16);
  assert.equal(tints[0], "tint greyicon 0 164 86 shade 1");
  assert.equal(tints[12], "tint greyicon 0 170 92 shade 7");
});

test("a marker of age 8 to 11 first fills and outlines a shrunk box, then draws its rings", () => {
  const player = setup([[1, 9, 0]], { groups: [group()] });
  for (let i = 0; i < 10; i += 1) updateFrame(player);
  const out = lines(player).filter((l) => /^(fill|outline|tint)/.test(l));
  assert.deepEqual(out.slice(0, 2), [
    "fill 178 100 181 107 shade 2",
    "outline 178 100 181 107 shade 3",
  ]);
  assert.equal(out.length, 2 + 4 * 3);
  assert.equal(out[2], "tint greyicon 0 172 94 shade 1");
});

test("a marker of age 12 or more is only a box grown by 2, filled and outlined", () => {
  const player = setup([[0, 9, 0]], { groups: [group()] });
  const out = lines(player).filter((l) => /^(fill|outline|tint)/.test(l));
  assert.deepEqual(out, [
    "fill 174 96 185 111 shade 2",
    "outline 174 96 185 111 shade 6",
  ]);
});

test("the shade base follows the IFF", () => {
  const bases = [0, 1, 2, 3, 4, 5, 9].map((iff) => {
    const out = lines(setup([[0, 9, 0]], { groups: [group({ iff })] }));
    return out
      .find((l) => l.startsWith("fill"))
      ?.split(" ")
      .at(-1);
  });
  assert.deepEqual(bases, ["2", "10", "26", "18", "10", "34", "2"]);
});

test("a marker on a group that does not exist draws the all-zero group", () => {
  const out = lines(setup([[0, 9, 40]], { groups: [] }));
  assert.equal(
    out.some((l) => l.startsWith("fill 174 96")),
    true,
  );
});

test("a label types itself out: older letters are brighter, a block follows", () => {
  const player = setup([[1, 18, 0, 0, 0, 1]], { labels: ["abc"] });
  updateFrame(player);
  updateFrame(player);
  assert.deepEqual(texts(player), [
    'text 180 104 shade 10 "ab"',
    'text 180 104 shade 11 "a"',
    "fill 194 104 200 110 shade 15",
  ]);
});

test("a label that has just started shows only its block", () => {
  const player = setup([[1, 18, 0, 0, 0, 0]], { labels: ["abc"] });
  updateFrame(player);
  assert.deepEqual(texts(player), ["fill 182 104 188 110 shade 7"]);
});

test("a typing label draws its prefixes from base up to base + 6 and no further", () => {
  const player = setup([[1, 18, 0, 0, 0, 0]], { labels: ["abcdefghij"] });
  for (let i = 0; i < 5; i += 1) updateFrame(player);
  const all = texts(player);
  assert.deepEqual(
    all.slice(0, -1).map((l) => l.split(" ").slice(3).join(" ")),
    [
      'shade 0 "abcdefgh"',
      'shade 1 "abcdefg"',
      'shade 2 "abcdef"',
      'shade 3 "abcde"',
      'shade 4 "abcd"',
      'shade 5 "abc"',
      'shade 6 "ab"',
    ],
  );
  assert.equal(all.at(-1), "fill 230 104 236 110 shade 7");
});

test("a typing label's first prefix is base + 2 at age 1 and base from age 2", () => {
  const colors = [1, 2, 3].map((age) => {
    const player = setup([[1, 18, 0, 0, 0, 0]], { labels: ["abcdef"] });
    for (let i = 0; i <= age; i += 1) updateFrame(player);
    return texts(player)[0]?.split(" ").slice(3, 5).join(" ");
  });
  assert.deepEqual(colors, ["shade 2", "shade 0", "shade 0"]);
});

test("a whole label is base + 7, 6, 5 for its last three steps, then base + 4", () => {
  const shades = (text: string, ages: number[]): string[] =>
    ages.map((age) => {
      const player = setup([[1, 18, 0, 0, 0, 2]], { labels: [text] });
      for (let i = 0; i <= age; i += 1) updateFrame(player);
      return texts(player)
        .map((l) => l.split(" ")[4])
        .join("|");
    });
  assert.deepEqual(shades("abcd", [3, 4, 5, 6]), ["23", "21", "20", "20"]);
  assert.deepEqual(shades("abc", [3, 4]), ["22", "20"]);
});

test("a shade past the 40 is drawn black (x0)", () => {
  const player = setup([[1, 18, 0, 0, 0, 6]], { labels: ["a"] });
  for (let i = 0; i < 6; i += 1) updateFrame(player);
  assert.equal(
    lines(player).some((l) => l.includes(" x0")),
    true,
  );
});

test("the caption is drawn first, the page is counted, and the page line shows it", () => {
  const player = setup([[0, 5, 1]], { captions: ["", "Words here"] });
  const out = lines(player);
  assert.equal(out[0], 'text 0 208 white "Words here"');
  assert.equal(out.at(-1), 'text 300 194 white "Pg. 1"');
});

test("the page line is the front string, a space and the page", () => {
  const bundle = makeBundle({ frontString: "Seite" });
  assert.deepEqual(
    pageLine(bundle, 12),
    Array.from("Seite 12", (c) => c.charCodeAt(0)),
  );
});

test("a busy frame holds the kinds of record the painter draws, and two clips", () => {
  const player = setup(
    [
      [0, 5, 1],
      [0, 9, 0],
      [0, 18, 0, 0, 0, 0],
    ],
    { captions: ["", "x"], labels: ["l"], groups: [group()] },
  );
  const records = drawFrame(player);
  assert.deepEqual([...new Set(records.map((r) => r.kind))].sort(), [
    "clip",
    "fill",
    "glyph",
    "hline",
    "icon",
    "outline",
    "vline",
  ]);
  assert.equal(records.filter((r) => r.kind === "clip").length, 2);
});

test("with the center off the origin the grid's first line follows the center", () => {
  const out = lines(setup([[0, 6, 300, -260]]));
  const majors = (kind: string): string[] =>
    out
      .filter((l) => l.startsWith(kind) && l.endsWith("major"))
      .map((l) => l.split(" ")[1] ?? "");
  assert.deepEqual(majors("vline"), ["14", "142", "270"]);
  assert.deepEqual(majors("hline"), ["8", "136"]);
});

test("a line that falls exactly on x 0 is the first line", () => {
  const out = lines(setup([[0, 7, 36, 36]]));
  const at = (word: string): string[] =>
    out
      .filter((l) => l.startsWith("vline") && l.endsWith(word))
      .map((l) => l.split(" ")[1] ?? "");
  assert.deepEqual(at("minor"), ["0", "72", "108", "144", "216", "252", "288"]);
  assert.deepEqual(at("major"), ["36", "180", "324"]);
});

test("a label as long as its typed count has no block after it", () => {
  const player = setup([[1, 18, 0, 0, 0, 0]], { labels: ["abcd"] });
  for (let i = 0; i < 3; i += 1) updateFrame(player);
  const out = texts(player);
  assert.equal(
    out.some((l) => l.startsWith("fill")),
    false,
  );
  assert.equal(out[0], 'text 180 104 shade 0 "abcd"');
});

test("brackets in a label switch to color code 1 and back to the label's shade", () => {
  const player = setup([[1, 18, 0, 0, 0, 0]], { labels: ["a[b]c"] });
  for (let i = 0; i < 9; i += 1) updateFrame(player);
  assert.deepEqual(texts(player), [
    'text 180 104 shade 4 "a"',
    'text 186 104 code1 "b"',
    'text 192 104 shade 4 "c"',
  ]);
});
