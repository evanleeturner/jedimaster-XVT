import assert from "node:assert/strict";
import { test } from "node:test";

import { makeBundle } from "./bundle.fixture.ts";
import { pickerTeams } from "./panel.ts";

test("the picker lists the teams that have a briefing, lowest first", () => {
  assert.deepEqual(
    pickerTeams(makeBundle({ teamBriefings: { 7: 0, 2: 0, 4: 0 } })),
    [2, 4, 7],
  );
});

test("a bundle where no team has a briefing offers team 0 alone", () => {
  assert.deepEqual(pickerTeams(makeBundle({ teamBriefings: {} })), [0]);
});
