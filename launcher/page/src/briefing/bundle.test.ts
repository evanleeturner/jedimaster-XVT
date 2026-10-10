import assert from "node:assert/strict";
import { test } from "node:test";

import { briefing, group, makeBundle } from "./bundle.fixture.ts";
import { decodeBundle } from "./bundle.ts";

interface Wire {
  format: unknown;
  format_version: unknown;
  front_string: unknown;
  grey_sheet: unknown;
  teams: { team: number }[];
  briefings: {
    index: number;
    running_time: unknown;
    events: unknown[];
    labels: string[];
    captions: string[];
  }[];
  groups: { iff: unknown; points: { enabled: unknown }[] }[];
  boxes: unknown[];
  craft_boxes: unknown[];
  iff_sheets: unknown[];
  font: { widths: number[]; text_colors: number[]; spacing: unknown };
}

function wire(): Wire {
  const bundle = makeBundle({ briefings: [briefing()], groups: [group()] });
  // The text of a bundle read back from JSON has the shape the fixture has.
  return JSON.parse(JSON.stringify(bundle)) as Wire;
}

function accepts(value: unknown): boolean {
  return "value" in decodeBundle(value);
}

function after(change: (value: Wire) => void): boolean {
  const value = wire();
  change(value);
  return accepts(value);
}

function first<T>(list: T[]): T {
  const item = list[0];
  if (item === undefined) throw new Error("invariant: the fixture has one");
  return item;
}

test("a bundle as the launcher writes it is accepted and kept whole", () => {
  const value = wire();
  const checked = decodeBundle(value);
  assert.ok("value" in checked);
  assert.deepEqual(checked.value, value);
});

test("a missing or extra field is refused", () => {
  for (const name of Object.keys(wire())) {
    const without = Object.fromEntries(
      Object.entries(wire()).filter(([key]) => key !== name),
    );
    assert.equal(accepts(without), false, name);
  }
  assert.equal(accepts({ ...wire(), extra: 1 }), false);
});

test("other formats and versions are refused", () => {
  assert.equal(accepts({ ...wire(), format: "other" }), false);
  assert.equal(accepts({ ...wire(), format_version: 2 }), false);
});

test("the fixed lengths are held: 10 teams, 32 texts, 8 points, 70 boxes, 106 crafts, 256 IFFs and widths, 5 colors", () => {
  const cuts: ((value: Wire) => void)[] = [
    (v) => v.teams.pop(),
    (v) => first(v.briefings).captions.pop(),
    (v) => first(v.briefings).labels.push(""),
    (v) => first(v.groups).points.pop(),
    (v) => v.boxes.pop(),
    (v) => v.craft_boxes.pop(),
    (v) => v.iff_sheets.pop(),
    (v) => v.font.widths.pop(),
    (v) => v.font.text_colors.pop(),
  ];
  cuts.forEach((cut, i) => {
    assert.equal(after(cut), false, String(i));
  });
});

test("values of the wrong type are refused", () => {
  const changes: ((value: Wire) => void)[] = [
    (v) => {
      v.front_string = 5;
    },
    (v) => {
      first(v.groups).iff = "1";
    },
    (v) => {
      first(first(v.groups).points).enabled = 1;
    },
    (v) => {
      first(v.briefings).running_time = 1.5;
    },
    (v) => first(v.briefings).events.push({ time: 1 }),
    (v) => {
      v.font.spacing = null;
    },
    (v) => {
      v.grey_sheet = 3;
    },
  ];
  changes.forEach((change, i) => {
    assert.equal(after(change), false, String(i));
  });
});

test("a team number past 9 and a briefing index past 7 are refused", () => {
  assert.equal(
    after((v) => {
      first(v.teams).team = 10;
    }),
    false,
  );
  assert.equal(
    after((v) => {
      first(v.briefings).index = 8;
    }),
    false,
  );
});

test("a text character above 255 is refused; one at 255 is kept", () => {
  assert.equal(
    after((v) => {
      first(v.briefings).captions[0] = "Ā";
    }),
    false,
  );
  assert.equal(
    after((v) => {
      first(v.briefings).captions[0] = "ÿ";
    }),
    true,
  );
});

test("no grey sheet is allowed", () => {
  assert.equal(
    after((v) => {
      v.grey_sheet = null;
    }),
    true,
  );
});

test("things that are not objects are refused", () => {
  for (const value of [null, 3, "x", [], undefined]) {
    assert.equal(accepts(value), false);
  }
});
