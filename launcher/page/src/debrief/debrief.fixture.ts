/** Made-up debriefings for the debriefing screen's tests. */
import type {
  Debrief,
  DebriefDetail,
  DebriefPlayer,
  DebriefTeam,
} from "../generated/control.ts";

export function player(
  entry: number,
  name: string,
  team: number,
  score: number,
  more: Partial<DebriefPlayer> = {},
): DebriefPlayer {
  return {
    entry,
    name,
    rating: 2,
    rank: "Trainee",
    team,
    flight_group: entry,
    flight_group_name: `Group ${String(entry)}`,
    score,
    kills: 0,
    shared: 0,
    assists: 0,
    losses: 0,
    shots: 0,
    hits: 0,
    warheads: 0,
    warhead_hits: 0,
    has_left: false,
    ...more,
  };
}

export function team(
  number: number,
  name: string,
  score: number,
  completed = false,
): DebriefTeam {
  return {
    team: number,
    name,
    completed,
    score,
    kills: number + 1,
    shared: 1,
    losses: 2,
    seconds: 0,
  };
}

export function detail(more: Partial<DebriefDetail> = {}): DebriefDetail {
  return {
    shots: 212,
    hits: 61,
    warheads: 4,
    warhead_hits: 3,
    losses: 3,
    losses_to_collisions: 1,
    losses_to_starships: 1,
    losses_to_mines: 0,
    friendlies_killed: 0,
    special_inspected: 2,
    by_craft: [
      { craft_type: 5, name: "TIE Fighter", kills: 3, shared: 1, assists: 1 },
      { craft_type: 9, name: "TIE Bomber", kills: 2, shared: 0, assists: 2 },
    ],
    on_pilot_ranks: [{ rank: 6, kills: 2, shared: 0, assists: 0 }],
    on_ai_ranks: [{ rank: 1, kills: 2, shared: 1, assists: 2 }],
    killed_by_pilot_ranks: [{ rank: 6, times: 1 }],
    killed_by_ai_ranks: [{ rank: 2, times: 1 }],
    on_players: [{ entry: 1, name: "Vader", kills: 2, shared: 0 }],
    from_players: [{ entry: 1, name: "Vader", kills: 1, shared: 0 }],
    on_flight_groups: [{ flight_group: 2, name: "Gold", kills: 3, shared: 1 }],
    from_flight_groups: [
      { flight_group: 3, name: "Blue", kills: 1, shared: 0 },
    ],
    ...more,
  };
}

/** A three-pilot, two-team battle (team 0 completed its goals) in the `combat` kind. */
export function debrief(more: Partial<Debrief> = {}): Debrief {
  return {
    result_id: 3,
    ended_at: "2026-10-16T21:30:05Z",
    mission_type: "combat",
    side: "imperial",
    mission: "skirm.tie",
    title: "Skirmish (Combat)",
    pilot: "Wedge",
    outcome: "completed",
    score: 1450,
    kills: 5,
    losses: 1,
    goals_met_seconds: 412,
    local_entry: 0,
    players: [
      player(0, "Wedge", 0, 1450, { kills: 5, shared: 1, losses: 1 }),
      player(1, "Vader", 1, 980, { has_left: true, rank: "Of 2nd" }),
      player(2, "Luke", 0, 1500),
    ],
    teams: [team(0, "Red", 1450, true), team(1, "Blue", 980, false)],
    detail: detail(),
    ...more,
  };
}

/** The same battle as a lone pilot's solo flight: no network, one player. */
export function solo(more: Partial<Debrief> = {}): Debrief {
  return debrief({
    players: [player(0, "Wedge", 0, 1450, { kills: 5, shared: 1, losses: 1 })],
    ...more,
  });
}
