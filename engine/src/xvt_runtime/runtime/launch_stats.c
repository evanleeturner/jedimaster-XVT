#include "xvt_runtime/runtime/launch_stats.h"

#include <stdio.h>
#include <string.h>

#include "xvt/flight/mission/mission.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/log/log.h"

/* The result's tables are as long as the pilot record's. */
typedef char xvt_launch_craft_rows_fit
	[(sizeof(g_pilot_data.last_mission_stats.kills_per_craft_per_mt[0]) ==
	  sizeof(int) * XVT_LAUNCH_CRAFT_TYPES)
		 ? 1
		 : -1];
typedef char xvt_launch_pilot_rank_rows_fit
	[(sizeof(g_pilot_data.last_mission_stats
			 .kills_full_on_player_rating_per_mt[0]) ==
	  sizeof(int) * XVT_LAUNCH_PILOT_RANKS)
		 ? 1
		 : -1];
typedef char xvt_launch_ai_rank_rows_fit
	[(sizeof(g_pilot_data.last_mission_stats
			 .kills_full_on_ai_rating_per_mt[0]) ==
	  sizeof(int) * XVT_LAUNCH_AI_RANKS)
		 ? 1
		 : -1];
typedef char
	xvt_launch_player_rows_fit[(sizeof(g_pilot_data.kills_full_on_player) ==
				    sizeof(int) * XVT_LAUNCH_PLAYERS)
					   ? 1
					   : -1];
typedef char xvt_launch_group_rows_fit
	[(sizeof(g_pilot_data.kills_full_on_flight_group) ==
	  sizeof(int) * XVT_LAUNCH_FLIGHT_GROUPS)
		 ? 1
		 : -1];

/* The flight's place of the player with DirectPlay id id, or -1. */
static int flight_slot_of(int id)
{
	for (int slot = 0; slot < XVT_LAUNCH_PLAYERS; ++slot) {
		if (g_players[slot].network.direct_play_id == id) {
			return slot;
		}
	}
	return -1;
}

/* Fills r's players with every entry of the pilot record that holds a
 * player, in entry order, from the numbers the game's end of battle wrote
 * there and into the player's place in the flight (its battle.player_result
 * line prints the same). */
static void read_players(struct xvt_launch_result *r)
{
	r->player_count = 0;
	for (int entry = 0; entry < XVT_LAUNCH_PLAYERS; ++entry) {
		const struct pilot_network_player *n =
			&g_pilot_data.network_players[entry];
		if (n->direct_play_id == 0) {
			continue;
		}
		struct xvt_launch_player *p = &r->players[r->player_count++];
		memset(p, 0, sizeof(*p));
		p->entry = entry;
		p->slot = flight_slot_of(n->direct_play_id);
		snprintf(p->name, sizeof(p->name), "%.12s", n->friendly_name);
		p->team = -1;
		p->rating = n->rating;
		p->score = n->total_score;
		p->kills = n->kills;
		p->shared = n->kills_shared;
		p->assists = n->kills_assist;
		p->losses = n->total_losses;
		p->has_left = n->has_left;
		if (p->slot >= 0) {
			const struct player_data *f = &g_players[p->slot];
			p->team = f->team;
			p->flight_group = f->bound_flight_group_idx;
			p->shots = f->mission_stats.laser_shots_fired +
				   f->mission_stats.ion_shots_fired;
			p->hits = f->mission_stats.laser_hits_scored +
				  f->mission_stats.ion_hits_scored;
			p->warheads = f->warheads_fired;
			p->warhead_hits = f->per_mission_kills.warhead_hits;
		}
		XVT_LOG_DEBUG(
			"launch.result_player entry=%d slot=%d name=\"%s\" team=%d fg=%d rating=%d score=%d kills=%d shared=%d assists=%d losses=%d shots=%d hits=%d warheads=%d warhead_hits=%d has_left=%d",
			p->entry, p->slot, p->name, p->team, p->flight_group,
			p->rating, p->score, p->kills, p->shared, p->assists,
			p->losses, p->shots, p->hits, p->warheads,
			p->warhead_hits, p->has_left);
	}
}

/* 1 when team has a player flight group that arrived, as the game's end of
 * battle counts a team in the battle. */
static int team_flew(int team)
{
	int groups = g_mission_header.num_flight_groups;
	if (groups > XVT_LAUNCH_FLIGHT_GROUPS) {
		groups = XVT_LAUNCH_FLIGHT_GROUPS;
	}
	for (int fg = 0; fg < groups; ++fg) {
		if (g_mission_flight_groups[fg].fg.player_number != 0 &&
		    g_mission_fg_stats[fg].has_arrived != 0 &&
		    g_mission_flight_groups[fg].fg.team == team) {
			return 1;
		}
	}
	return 0;
}

/* Fills r's teams with each of teams 0 to 7 that flew or met its goals, the
 * teams the game's end of battle prints a battle.team_result line for. */
static void read_teams(struct xvt_launch_result *r)
{
	r->team_count = 0;
	for (int team = 0; team < XVT_LAUNCH_TEAMS; ++team) {
		const struct pilot_team *s = &g_pilot_data.teams[team];
		if (!team_flew(team) && s->is_mission_completed == 0) {
			continue;
		}
		struct xvt_launch_team *t = &r->teams[r->team_count++];
		t->team = team;
		t->completed = s->is_mission_completed;
		t->score = s->mission_score;
		t->kills = s->kills;
		t->shared = s->kills_shared;
		t->losses = s->losses;
		t->seconds = s->mission_time;
		XVT_LOG_DEBUG(
			"launch.result_team team=%d completed=%d score=%d kills=%d shared=%d losses=%d seconds=%d",
			t->team, t->completed, t->score, t->kills, t->shared,
			t->losses, t->seconds);
	}
}

/* Copies count numbers from from to to. */
static void copy_row(int *to, const int *from, int count)
{
	memcpy(to, from, sizeof(int) * (size_t)count);
}

/* The sum of the count numbers of row. */
static int row_sum(const int *row, int count)
{
	int sum = 0;
	for (int i = 0; i < count; ++i) {
		sum += row[i];
	}
	return sum;
}

/* Fills d with the local pilot's own record of the battle: row 0 of
 * last_mission_stats, where the game's end of battle counts the mission it
 * just flew whatever its type, and the pilot record's kills of and by each
 * player and flight group. */
static void read_detail(struct xvt_launch_detail *d)
{
	const struct pilot_stats *s = &g_pilot_data.last_mission_stats;
	d->shots = s->energy_fired_per_mt[0];
	d->hits = s->energy_hits_per_mt[0];
	d->warheads = s->warheads_fired_per_mt[0];
	d->warhead_hits = s->warheads_hits_per_mt[0];
	d->losses = s->total_craft_losses_per_mt[0];
	d->losses_to_collisions = s->losses_by_collisions_per_mt[0];
	d->losses_to_starships = s->losses_by_starships_per_mt[0];
	d->losses_to_mines = s->losses_by_mines_per_mt[0];
	d->friendlies_killed = s->total_friendlies_killed_per_mt[0];
	d->special_inspected = s->num_special_inspected_per_mt[0];
	copy_row(d->kills_by_craft, s->kills_per_craft_per_mt[0],
		 XVT_LAUNCH_CRAFT_TYPES);
	copy_row(d->shared_by_craft, s->kills_shared_per_craft_per_mt[0],
		 XVT_LAUNCH_CRAFT_TYPES);
	copy_row(d->assists_by_craft, s->kills_assists_per_craft_per_mt[0],
		 XVT_LAUNCH_CRAFT_TYPES);
	copy_row(d->kills_on_pilot_rank,
		 s->kills_full_on_player_rating_per_mt[0],
		 XVT_LAUNCH_PILOT_RANKS);
	copy_row(d->shared_on_pilot_rank,
		 s->kills_shared_on_player_rating_per_mt[0],
		 XVT_LAUNCH_PILOT_RANKS);
	copy_row(d->assists_on_pilot_rank,
		 s->kills_assist_on_player_rating_per_mt[0],
		 XVT_LAUNCH_PILOT_RANKS);
	copy_row(d->kills_on_ai_rank, s->kills_full_on_ai_rating_per_mt[0],
		 XVT_LAUNCH_AI_RANKS);
	copy_row(d->shared_on_ai_rank, s->kills_shared_on_ai_rating_per_mt[0],
		 XVT_LAUNCH_AI_RANKS);
	copy_row(d->assists_on_ai_rank, s->kills_assist_on_ai_rating_per_mt[0],
		 XVT_LAUNCH_AI_RANKS);
	copy_row(d->killed_by_pilot_rank, s->killed_by_player_rating_per_mt[0],
		 XVT_LAUNCH_PILOT_RANKS);
	copy_row(d->killed_by_ai_rank, s->killed_by_ai_rating_per_mt[0],
		 XVT_LAUNCH_AI_RANKS);
	copy_row(d->kills_on_player, g_pilot_data.kills_full_on_player,
		 XVT_LAUNCH_PLAYERS);
	copy_row(d->shared_on_player, g_pilot_data.kills_shared_on_player,
		 XVT_LAUNCH_PLAYERS);
	copy_row(d->kills_from_player, g_pilot_data.kills_full_from_player,
		 XVT_LAUNCH_PLAYERS);
	copy_row(d->shared_from_player, g_pilot_data.kills_shared_from_player,
		 XVT_LAUNCH_PLAYERS);
	copy_row(d->kills_on_flight_group,
		 g_pilot_data.kills_full_on_flight_group,
		 XVT_LAUNCH_FLIGHT_GROUPS);
	copy_row(d->shared_on_flight_group,
		 g_pilot_data.kills_shared_on_flight_group,
		 XVT_LAUNCH_FLIGHT_GROUPS);
	copy_row(d->kills_from_flight_group,
		 g_pilot_data.kills_full_from_flight_group,
		 XVT_LAUNCH_FLIGHT_GROUPS);
	copy_row(d->shared_from_flight_group,
		 g_pilot_data.kills_shared_from_flight_group,
		 XVT_LAUNCH_FLIGHT_GROUPS);
	XVT_LOG_DEBUG(
		"launch.result_detail shots=%d hits=%d warheads=%d warhead_hits=%d losses=%d kills=%d shared=%d assists=%d player_kills=%d player_shared=%d ai_kills=%d ai_shared=%d lost_to_players=%d lost_to_ai=%d",
		d->shots, d->hits, d->warheads, d->warhead_hits, d->losses,
		row_sum(d->kills_by_craft, XVT_LAUNCH_CRAFT_TYPES),
		row_sum(d->shared_by_craft, XVT_LAUNCH_CRAFT_TYPES),
		row_sum(d->assists_by_craft, XVT_LAUNCH_CRAFT_TYPES),
		row_sum(d->kills_on_pilot_rank, XVT_LAUNCH_PILOT_RANKS),
		row_sum(d->shared_on_pilot_rank, XVT_LAUNCH_PILOT_RANKS),
		row_sum(d->kills_on_ai_rank, XVT_LAUNCH_AI_RANKS),
		row_sum(d->shared_on_ai_rank, XVT_LAUNCH_AI_RANKS),
		row_sum(d->killed_by_pilot_rank, XVT_LAUNCH_PILOT_RANKS),
		row_sum(d->killed_by_ai_rank, XVT_LAUNCH_AI_RANKS));
}

void xvt_launch_stats_read(struct xvt_launch_result *r)
{
	read_players(r);
	read_teams(r);
	read_detail(&r->detail);
}
