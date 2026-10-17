import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { test } from "node:test";

import type { Message } from "../codec.ts";
import type { Debrief, ResultSummary } from "../generated/control.ts";
import type { Action, State } from "../state.ts";
import { describe, initialState, reduce, RESULTS_KEPT } from "../state.ts";

function play(...actions: Action[]): State {
  return actions.reduce(reduce, initialState());
}

function message(value: Message): Action {
  return { kind: "message", message: value };
}

/** The flown battle's debriefing from the shared accepted example, as result ``id``. */
function debrief(id = 3): Debrief {
  const path = join(
    import.meta.dirname,
    "..",
    "..",
    "..",
    "tests",
    "control-examples",
    "accept",
    "push-launch-ended.json",
  );
  const push = JSON.parse(readFileSync(path, "utf8")) as {
    data: { debrief: Debrief };
  };
  return { ...push.data.debrief, result_id: id };
}

function summary(id: number): ResultSummary {
  const d = debrief(id);
  return {
    result_id: id,
    ended_at: d.ended_at,
    title: d.title,
    outcome: d.outcome,
    score: d.score,
  };
}

const STARTED: Message = {
  event: "launch.started",
  data: {
    launch_id: 4,
    mission_type: "melee",
    side: "none",
    mission: "melee01.tie",
    title: "Two Teams Meet",
    pilot: "Wedge",
  },
};

function ended(d: Debrief | null): Message {
  return {
    event: "launch.ended",
    data: { launch_id: 4, status: "flown", line: "flown", debrief: d },
  };
}

function listed(ids: number[]): Action {
  return message({
    id: 7,
    ok: true,
    result: { results: ids.map(summary) },
  });
}

test("before the launcher answers, the panel says the results have not arrived", () => {
  const view = describe(initialState()).debrief;
  assert.equal(view.note, "The kept battle results have not arrived yet.");
  assert.deepEqual(view.options, []);
  assert.equal(view.heading, null);
  assert.deepEqual(view.tables, []);
});

test("with none kept, the panel says how to get one", () => {
  const view = describe(play(listed([]))).debrief;
  assert.equal(
    view.note,
    "No battle results are kept yet. Fly a battle from Discord.",
  );
});

test("a battle's ending shows its debriefing, new, and lists it first", () => {
  const state = play(
    listed([2, 1]),
    message(STARTED),
    message(ended(debrief())),
  );
  const view = describe(state).debrief;
  assert.equal(view.fresh, true);
  assert.equal(view.tab, "overview");
  assert.deepEqual(
    view.options.map((o) => [o.id, o.current]),
    [
      [3, true],
      [2, false],
      [1, false],
    ],
  );
  assert.ok(view.heading?.startsWith("Two Teams Meet, flown by Wedge"));
  assert.equal(view.tables[0]?.caption, "Mission overview");
  assert.equal(view.note, null);
});

test("the list keeps the launcher's twenty and never lists one twice", () => {
  const ids = Array.from({ length: RESULTS_KEPT }, (_, i) => RESULTS_KEPT - i);
  const state = play(
    listed(ids),
    message(STARTED),
    message(ended(debrief(RESULTS_KEPT + 1))),
  );
  const kept = state.debrief.results?.map((r) => r.result_id) ?? [];
  assert.equal(kept.length, RESULTS_KEPT);
  assert.equal(kept[0], RESULTS_KEPT + 1);
  assert.ok(!kept.includes(1), "the oldest drops off");
  const again = play(listed([3]), message(STARTED), message(ended(debrief(3))));
  assert.deepEqual(
    again.debrief.results?.map((r) => r.result_id),
    [3],
  );
});

test("an ending with no debriefing, or for another battle, changes nothing shown", () => {
  const none = play(listed([1]), message(STARTED), message(ended(null)));
  assert.equal(none.debrief.shown, null);
  assert.equal(none.debrief.fresh, false);
  const other = play(listed([1]), message(ended(debrief())));
  assert.equal(other.debrief.shown, null, "no battle was flying");
});

test("a debriefing asked for is shown, not new; one no longer kept is a notice", () => {
  const got = play(
    message({ id: 8, ok: true, result: { debrief: debrief(2) } }),
  );
  assert.equal(got.debrief.shown?.result_id, 2);
  assert.equal(got.debrief.fresh, false);
  const gone = play(message({ id: 8, ok: true, result: { debrief: null } }));
  assert.equal(gone.notice, "That battle result is no longer kept.");
});

test("the tab picks the tables; opening the panel clears new", () => {
  const shown = play(message(STARTED), message(ended(debrief())));
  const stats = reduce(shown, { kind: "debrief-tab", tab: "statistics" });
  const view = describe(stats).debrief;
  assert.equal(view.tab, "statistics");
  assert.equal(view.tables[0]?.caption, "Result");
  const seen = reduce(stats, { kind: "debrief-seen" });
  assert.equal(seen.debrief.fresh, false);
  assert.equal(seen.debrief.shown?.result_id, 3);
});
