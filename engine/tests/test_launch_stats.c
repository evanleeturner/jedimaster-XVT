/* Checks the reading of a finished battle (xvt_runtime/runtime/launch_stats.h)
 * against the promises in its header: every filled entry of the pilot record
 * in entry order with the numbers of its place in the flight, the teams the
 * game's end of battle counts, the local pilot's own tables from row 0, one
 * DEBUG line per player and team and one for the tables, and the result's
 * other members left as they are. The pilot record, flight and mission
 * numbers are made up, each one different, so a member that takes another's
 * number shows. The full roster of eight seats, written to the result file
 * through the door, is checked in test_launch_door.c. */
#include <SDL3/SDL_log.h>
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/launch_result.h"
#include "xvt_runtime/runtime/launch_stats.h"

/* Every launch.result_player, _team and _detail line since it was emptied. */
static char g_lines[4096];

static void catch_line(void *userdata, int category, SDL_LogPriority priority,
		       const char *message)
{
	(void)userdata;
	(void)category;
	(void)priority;
	if (strstr(message, "launch.result_") != NULL) {
		size_t used = strlen(g_lines);
		snprintf(g_lines + used, sizeof(g_lines) - used, "%s\n",
			 message);
	}
}

/* Clears everything the reading looks at. */
static void clear_battle(void)
{
	memset(&g_pilot_data, 0, sizeof(g_pilot_data));
	memset(g_players, 0, sizeof g_players);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	memset(g_mission_fg_stats, 0, sizeof g_mission_fg_stats);
	g_mission_header.num_flight_groups = 0;
	g_lines[0] = '\0';
}

/* Seeds three players: entry 1 in flight place 2, entry 4 in place 0 with
 * a name longer than 12 characters and no end within its 14 bytes, entry 6
 * with no place. Flight group 0 is a players' group of team 2 that
 * arrived. */
static void seed_three_players(void)
{
	clear_battle();
	static const int entries[] = {1, 4, 6};
	static const int slots[] = {2, 0, -1};
	for (int i = 0; i < 3; ++i) {
		struct pilot_network_player *n =
			&g_pilot_data.network_players[entries[i]];
		int base = 1000 * (i + 1);
		n->direct_play_id = 21 + i;
		n->rating = 3 + i;
		n->total_score = base + 1;
		n->kills = base + 2;
		n->kills_shared = base + 3;
		n->kills_assist = base + 4;
		n->total_losses = base + 5;
		if (slots[i] < 0) {
			continue;
		}
		struct player_data *f = &g_players[slots[i]];
		int held = 100 * (i + 1);
		f->network.direct_play_id = 21 + i;
		f->team = 2;
		f->bound_flight_group_idx = (uint16_t)(10 + i);
		f->mission_stats.laser_shots_fired = (uint16_t)(held + 1);
		f->mission_stats.ion_shots_fired = (uint16_t)(held + 2);
		f->mission_stats.laser_hits_scored = (uint16_t)(held + 3);
		f->mission_stats.ion_hits_scored = (uint16_t)(held + 4);
		f->warheads_fired = (uint16_t)(held + 5);
		f->per_mission_kills.warhead_hits = (uint16_t)(held + 6);
	}
	snprintf(g_pilot_data.network_players[1].friendly_name, 14, "Ardent");
	memcpy(g_pilot_data.network_players[4].friendly_name, "Quillon_ABCDEFG",
	       14);
	snprintf(g_pilot_data.network_players[6].friendly_name, 14, "Tallow");
	g_pilot_data.network_players[6].has_left = 1;
	g_mission_flight_groups[0].fg.team = 2;
	g_mission_flight_groups[0].fg.player_number = 1;
	g_mission_fg_stats[0].has_arrived = 1;
	g_mission_header.num_flight_groups = 1;
	g_pilot_data.teams[2].mission_score = 512;
	g_pilot_data.teams[2].mission_time = 333;
	g_pilot_data.teams[2].kills = 13;
	g_pilot_data.teams[2].kills_shared = 14;
	g_pilot_data.teams[2].losses = 15;
}

/* The three players in entry order, each member from its own source. */
static void check_players_read_in_entry_order(void)
{
	seed_three_players();
	struct xvt_launch_result r;
	memset(&r, 0, sizeof(r));
	xvt_launch_stats_read(&r);
	XVT_ASSERT_INT_EQ(r.player_count, 3);
	const struct xvt_launch_player *p = &r.players[0];
	XVT_ASSERT_INT_EQ(p->entry, 1);
	XVT_ASSERT_INT_EQ(p->slot, 2);
	XVT_ASSERT_INT_EQ(strcmp(p->name, "Ardent"), 0);
	XVT_ASSERT_INT_EQ(p->team, 2);
	XVT_ASSERT_INT_EQ(p->flight_group, 10);
	XVT_ASSERT_INT_EQ(p->rating, 3);
	XVT_ASSERT_INT_EQ(p->score, 1001);
	XVT_ASSERT_INT_EQ(p->kills, 1002);
	XVT_ASSERT_INT_EQ(p->shared, 1003);
	XVT_ASSERT_INT_EQ(p->assists, 1004);
	XVT_ASSERT_INT_EQ(p->losses, 1005);
	XVT_ASSERT_INT_EQ(p->shots, 203);
	XVT_ASSERT_INT_EQ(p->hits, 207);
	XVT_ASSERT_INT_EQ(p->warheads, 105);
	XVT_ASSERT_INT_EQ(p->warhead_hits, 106);
	XVT_ASSERT_INT_EQ(p->has_left, 0);
	p = &r.players[1];
	XVT_ASSERT_INT_EQ(p->entry, 4);
	XVT_ASSERT_INT_EQ(p->slot, 0);
	/* A name is cut to 12 characters, even one with no end of its own. */
	XVT_ASSERT_INT_EQ(strcmp(p->name, "Quillon_ABCD"), 0);
	XVT_ASSERT_INT_EQ(p->flight_group, 11);
	XVT_ASSERT_INT_EQ(p->score, 2001);
	XVT_ASSERT_INT_EQ(p->shots, 403);
	p = &r.players[2];
	XVT_ASSERT_INT_EQ(p->entry, 6);
	XVT_ASSERT_INT_EQ(p->slot, -1);
	XVT_ASSERT_INT_EQ(p->team, -1);
	XVT_ASSERT_INT_EQ(p->flight_group, 0);
	XVT_ASSERT_INT_EQ(p->score, 3001);
	XVT_ASSERT_INT_EQ(p->losses, 3005);
	XVT_ASSERT_INT_EQ(p->shots, 0);
	XVT_ASSERT_INT_EQ(p->warhead_hits, 0);
	XVT_ASSERT_INT_EQ(p->has_left, 1);
	XVT_ASSERT_INT_EQ(r.team_count, 1);
	XVT_ASSERT_INT_EQ(r.teams[0].team, 2);
	XVT_ASSERT_INT_EQ(r.teams[0].completed, 0);
	XVT_ASSERT_INT_EQ(r.teams[0].score, 512);
	XVT_ASSERT_INT_EQ(r.teams[0].kills, 13);
	XVT_ASSERT_INT_EQ(r.teams[0].shared, 14);
	XVT_ASSERT_INT_EQ(r.teams[0].losses, 15);
	XVT_ASSERT_INT_EQ(r.teams[0].seconds, 333);
	XVT_ASSERT_TRUE(
		strstr(g_lines,
		       "launch.result_player entry=1 slot=2 name=\"Ardent\" team=2 fg=10 rating=3 score=1001 kills=1002 shared=1003 assists=1004 losses=1005 shots=203 hits=207 warheads=105 warhead_hits=106 has_left=0\n") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(g_lines,
		       "launch.result_player entry=6 slot=-1 name=\"Tallow\" team=-1 fg=0 rating=5 score=3001 kills=3002 shared=3003 assists=3004 losses=3005 shots=0 hits=0 warheads=0 warhead_hits=0 has_left=1\n") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(g_lines,
		       "launch.result_team team=2 completed=0 score=512 kills=13 shared=14 losses=15 seconds=333\n") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(g_lines,
		       "launch.result_detail shots=0 hits=0 warheads=0 warhead_hits=0 losses=0 kills=0 shared=0 assists=0 player_kills=0 player_shared=0 ai_kills=0 ai_shared=0 lost_to_players=0 lost_to_ai=0\n") !=
		NULL);
}

/* The reading fills players, teams and detail only. */
static void check_other_members_are_left(void)
{
	seed_three_players();
	struct xvt_launch_result r;
	memset(&r, 0, sizeof(r));
	r.exit_code = XVT_LAUNCH_EXIT_FLOWN;
	r.outcome = XVT_LAUNCH_OUTCOME_COMPLETED;
	r.score = 77;
	r.kills = 78;
	r.losses = 79;
	r.goals_met_seconds = 80;
	snprintf(r.why, sizeof(r.why), "kept");
	r.sith.setting = 2;
	r.sith.lord_lost = 81;
	r.local_entry = 4;
	xvt_launch_stats_read(&r);
	XVT_ASSERT_INT_EQ(r.exit_code, XVT_LAUNCH_EXIT_FLOWN);
	XVT_ASSERT_INT_EQ(r.outcome, XVT_LAUNCH_OUTCOME_COMPLETED);
	XVT_ASSERT_INT_EQ(r.score, 77);
	XVT_ASSERT_INT_EQ(r.kills, 78);
	XVT_ASSERT_INT_EQ(r.losses, 79);
	XVT_ASSERT_INT_EQ(r.goals_met_seconds, 80);
	XVT_ASSERT_INT_EQ(strcmp(r.why, "kept"), 0);
	XVT_ASSERT_INT_EQ(r.sith.setting, 2);
	XVT_ASSERT_INT_EQ(r.sith.lord_lost, 81);
	XVT_ASSERT_INT_EQ(r.local_entry, 4);
	XVT_ASSERT_INT_EQ(r.player_count, 3);
}

/* An empty pilot record gives no players, no teams and tables of 0, even
 * when the result held others before. */
static void check_an_empty_record(void)
{
	clear_battle();
	struct xvt_launch_result r;
	memset(&r, 0x5a, sizeof(r));
	xvt_launch_stats_read(&r);
	XVT_ASSERT_INT_EQ(r.player_count, 0);
	XVT_ASSERT_INT_EQ(r.team_count, 0);
	XVT_ASSERT_INT_EQ(r.detail.shots, 0);
	XVT_ASSERT_INT_EQ(r.detail.kills_by_craft[0], 0);
	XVT_ASSERT_INT_EQ(r.detail.shared_from_flight_group[47], 0);
	XVT_ASSERT_INT_EQ(r.detail.killed_by_ai_rank[5], 0);
}

/* A mission header that names more flight groups than the game keeps is
 * read only as far as the 48 it keeps. */
static void check_flight_groups_past_48(void)
{
	clear_battle();
	g_mission_header.num_flight_groups = 300;
	g_mission_flight_groups[47].fg.team = 7;
	g_mission_flight_groups[47].fg.player_number = 1;
	g_mission_fg_stats[47].has_arrived = 1;
	struct xvt_launch_result r;
	memset(&r, 0, sizeof(r));
	xvt_launch_stats_read(&r);
	XVT_ASSERT_INT_EQ(r.team_count, 1);
	XVT_ASSERT_INT_EQ(r.teams[0].team, 7);
}

int main(void)
{
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(catch_line, NULL);
	check_players_read_in_entry_order();
	check_other_members_are_left();
	check_an_empty_record();
	check_flight_groups_past_48();
	clear_battle();
	return 0;
}
