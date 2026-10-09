/* Tests for xvt/flight/fediskio.c: fe_disk_io_commit_flight_results, which
 * records a finished flight into the pilot record, and the file helpers. Each
 * commit check sets the flight it needs in the game's own tables (the mission
 * header, flight groups, the players, the goal statuses and scores) and a
 * cleared pilot record, or the record the flights before it left, then reads
 * what the commit wrote: scores and tallies, promotion, kills, team results,
 * the award, and each history record. The file checks read files this file
 * writes into a temporary asset folder. No game data is read.
 *
 * Not checked here: the commit's warnings for a campaign, tournament or battle
 * number past the history tables, and its draw count at battle step 10, which
 * run only after an indexing fault that stops the sanitizer; and lines no
 * input reaches: the 2,500 and 1,250 score steps of a melee award (an award
 * needs a score over 5,000) and the cap of 100 on a training flight's
 * progress (a promotion sets the points to 0 first).
 *
 * POSIX only, for the temporary folder (test_asset_folder.h). */
#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "test_asset_folder.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"

enum {
	TEST_XWING = 1,	       /* Craft type 1, a starfighter with a model. */
	TEST_POINT_VALUE = 10, /* The X-wing model's point value here. */
	TEST_LOCAL_ID = 100,   /* The local player's DirectPlay id. */
	TEST_OTHER_ID = 200,   /* A second human player's DirectPlay id. */
	TEST_PRIMARY = 0,      /* Goal status entries: primary, prevent. */
	TEST_PREVENT = 1,
};

static struct pilot_data g_saved_pilot;
static struct pilot_data g_kept_pilot;
static int g_keep_pilot_record; /* 1: fresh_flight keeps the pilot record. */

/* A combat mission of two flight groups of X-wings: group 0 on team 0 is the
 * local player's, slot 0, two craft a round, one round, no rounds left;
 * group 1 on team 1 is flown by the computer. The local player is the only
 * human, and the pilot record's network entry 0 is that player. Difficulty
 * is hard, the wave setting the default, and no goal has been met. Every
 * mission and sequence the pilot record names is number 1. The pilot record
 * is cleared, unless g_keep_pilot_record is set: then the record the last
 * flight left is kept for the next one. */
static void fresh_flight(void)
{
	if (g_keep_pilot_record) {
		memcpy(&g_kept_pilot, &g_pilot_data, sizeof g_kept_pilot);
	}
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	memset(g_players, 0, sizeof g_players);
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	memset(&g_mission_header, 0, sizeof g_mission_header);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	memset(g_mission_fg_stats, 0, sizeof g_mission_fg_stats);
	memset(g_model_defs, 0, sizeof g_model_defs);
	g_model_defs[get_model_index_from_type(
			     g_craft_type_to_object_type[TEST_XWING])]
		.craft_point_value = TEST_POINT_VALUE;
	g_mission_header.mission_type = MISSION_TYPE_COMBAT;
	g_mission_header.num_flight_groups = 2;
	for (int fg = 0; fg < 2; ++fg) {
		struct mission_flight_group *group =
			&g_mission_flight_groups[fg];
		group->fg.craft_type = TEST_XWING;
		group->fg.team = (uint8_t)fg;
		group->fg.number_of_craft = 2;
		group->fg.number_of_waves = 1;
		group->player_owner_idx = -1;
		g_mission_fg_stats[fg].has_arrived = 1;
	}
	g_mission_flight_groups[0].fg.player_number = 1;
	g_mission_flight_groups[0].player_owner_idx = 0;
	g_local_player = 0;
	g_players[0].network.direct_play_id = TEST_LOCAL_ID;
	g_players[0].team = 0;
	g_players[0].bound_flight_group_idx = 0;
	g_pilot_data.network_players[0].direct_play_id = TEST_LOCAL_ID;
	g_flight_mission_state.difficulty = GAME_DIFFICULTY_HARD;
	g_flight_mission_state.player_flight_group_wave_mode =
		CRAFT_WAVES_DEFAULT;
	g_pilot_data.rating = PILOT_RATING_TRAINEE;
	for (int dir = 0; dir < 6; ++dir) {
		g_pilot_data.mission_description_ids[dir] = 1;
	}
	if (g_keep_pilot_record) {
		memcpy(&g_pilot_data, &g_kept_pilot, sizeof g_pilot_data);
	}
}

/* Sets a team's primary and prevent goal statuses. */
static void set_goals(int team, int primary, int prevent)
{
	g_flight_mission_state.runtime.team_goal_status[team][TEST_PRIMARY] =
		(uint8_t)primary;
	g_flight_mission_state.runtime.team_goal_status[team][TEST_PREVENT] =
		(uint8_t)prevent;
}

/* A second human, slot 1 on team 1 flying group 1, who is network entry 1. */
static void add_second_human(void)
{
	g_players[1].network.direct_play_id = TEST_OTHER_ID;
	g_players[1].team = 1;
	g_players[1].bound_flight_group_idx = 1;
	g_pilot_data.network_players[1].direct_play_id = TEST_OTHER_ID;
	g_mission_flight_groups[1].fg.player_number = 1;
	g_mission_flight_groups[1].player_owner_idx = 1;
}

/* The commit returns 0, and for MISSION_TYPE_SIMULATOR_1 it leaves the pilot
 * record as it was. */
static void check_simulator_not_recorded(void)
{
	fresh_flight();
	g_mission_header.mission_type = MISSION_TYPE_SIMULATOR_1;
	set_goals(0, 1, 0);
	g_players[0].mission_stats.mission_score = 500;
	memcpy(&g_saved_pilot, &g_pilot_data, sizeof g_saved_pilot);
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	XVT_ASSERT_TRUE(memcmp(&g_saved_pilot, &g_pilot_data,
			       sizeof g_saved_pilot) == 0);
}

/* The local player's score is its mission score plus its team's bonus score:
 * it becomes the pilot record's mission score and is added to its total. */
static void check_local_score(void)
{
	fresh_flight();
	g_players[0].mission_stats.mission_score = 1200;
	g_flight_mission_state.runtime.team_scores[TEAM_SCORE_BONUS][0] = 250;
	g_flight_mission_state.runtime.team_scores[TEAM_SCORE_BONUS][1] = 4000;
	g_pilot_data.total_score = 7;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_score, 1450);
	XVT_ASSERT_INT_EQ(g_pilot_data.total_score, 1457);
}

/* A team's result: completed when its primary goal status is 1 and its
 * prevent status is not; its score is its bonus score plus, outside a melee,
 * its human players' mission scores; its time, kills, shared kills and losses
 * are the flight's. Team 3 has a player who is not connected, whose score
 * does not count. */
static void check_team_results(void)
{
	fresh_flight();
	add_second_human();
	set_goals(0, 1, 0);
	set_goals(1, 1, 1);
	set_goals(2, 2, 0);
	set_goals(3, 1, 2);
	struct mission_flight_runtime_state *runtime =
		&g_flight_mission_state.runtime;
	for (int team = 0; team < 4; ++team) {
		runtime->team_scores[TEAM_SCORE_BONUS][team] = 100 * (team + 1);
		runtime->team_scores[TEAM_SCORE_MISSION][team] = 9000;
		runtime->team_mission_completion_time_seconds[team] =
			(unsigned)(60 + team);
		runtime->team_kill_stats[0][team] = (uint16_t)(10 + team);
		runtime->team_kill_stats[1][team] = (uint16_t)(20 + team);
		runtime->team_kill_stats[3][team] = (uint16_t)(30 + team);
	}
	g_players[0].mission_stats.mission_score = 1000;
	g_players[1].mission_stats.mission_score = 2000;
	g_players[2].team = 3;
	g_players[2].mission_stats.mission_score = 5000;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	static const int completed[4] = {1, 0, 0, 1};
	static const int scores[4] = {1100, 2200, 300, 400};
	for (int team = 0; team < 4; ++team) {
		const struct pilot_team *result = &g_pilot_data.teams[team];
		XVT_ASSERT_INT_EQ(result->is_mission_completed,
				  completed[team]);
		XVT_ASSERT_INT_EQ(result->mission_score, scores[team]);
		XVT_ASSERT_INT_EQ(result->mission_time, 60 + team);
		XVT_ASSERT_INT_EQ(result->kills, 10 + team);
		XVT_ASSERT_INT_EQ(result->kills_shared, 20 + team);
		XVT_ASSERT_INT_EQ(result->losses, 30 + team);
	}
}

/* In combat, a team with no player flight group is credited with its primary
 * goal when the players' team missed its own: flown alone, team 1 completes
 * the mission when team 0 did not (round 0), and not when team 0 did (1); in
 * a melee it never is (2). A group with a player slot that no player owns is
 * not a player flight group (3); one a player owns is, so team 1 is not
 * credited (4). */
static void check_goal_credited(void)
{
	static const int team_1_completes[5] = {1, 0, 0, 1, 0};
	for (int round = 0; round < 5; ++round) {
		fresh_flight();
		if (round == 1) {
			set_goals(0, 1, 0);
		} else if (round == 2) {
			g_mission_header.mission_type = MISSION_TYPE_MELEE;
		} else if (round >= 3) {
			g_mission_flight_groups[1].fg.player_number = 2;
			g_mission_flight_groups[1].player_owner_idx =
				round == 3 ? -1 : 1;
		}
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
		XVT_ASSERT_INT_EQ(g_pilot_data.teams[0].is_mission_completed,
				  round == 1);
		XVT_ASSERT_INT_EQ(g_pilot_data.teams[1].is_mission_completed,
				  team_1_completes[round]);
	}
}

/* In a melee a team's score is its bonus score plus its mission score. */
static void check_melee_team_score(void)
{
	fresh_flight();
	g_mission_header.mission_type = MISSION_TYPE_MELEE;
	struct mission_flight_runtime_state *runtime =
		&g_flight_mission_state.runtime;
	runtime->team_scores[TEAM_SCORE_BONUS][1] = 300;
	runtime->team_scores[TEAM_SCORE_MISSION][1] = 4000;
	g_players[0].mission_stats.mission_score = 1000;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.teams[1].mission_score, 4300);
}

/* Outside a melee, a player whose team met its primary goal scores 80 times
 * the craft's point value for each craft its group still had to send: here
 * two craft a round, one round left, 80 * 2 * 10 = 1600. */
static void check_wave_bonus(void)
{
	fresh_flight();
	set_goals(0, 1, 0);
	g_mission_fg_stats[0].waves_remaining = 1;
	g_players[0].mission_stats.mission_score = 1000;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score, 2600);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_score, 2600);
	/* Slot 1 holds no player, though its team is 0. */
	XVT_ASSERT_INT_EQ(g_players[1].mission_stats.mission_score, 0);
}

/* No bonus for the craft left when the player's team missed its primary
 * goal, when the group's supply is unlimited (99 rounds), when the wave
 * setting is not the default, or in a melee. */
static void check_wave_bonus_withheld(void)
{
	for (int why = 0; why < 4; ++why) {
		fresh_flight();
		set_goals(0, why == 0 ? 2 : 1, 0);
		g_mission_fg_stats[0].waves_remaining = 1;
		g_players[0].mission_stats.mission_score = 1000;
		if (why == 1) {
			g_mission_flight_groups[0].fg.number_of_waves = 99;
		} else if (why == 2) {
			g_flight_mission_state.player_flight_group_wave_mode =
				CRAFT_WAVES_UNLIMITED;
		} else if (why == 3) {
			g_mission_header.mission_type = MISSION_TYPE_MELEE;
		}
		fe_disk_io_commit_flight_results(0, 0);
		XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score,
				  1000);
	}
}

/* In a melee, each flight group's rating is
 * g_flight_group_rating_base_by_ai_level of its AI level plus, for a nonzero
 * level, the group's index & 3. It is set outside a sequence and on a
 * tournament's first mission, and left alone on its second. */
static void check_melee_flight_group_rating(void)
{
	fresh_flight();
	g_mission_header.mission_type = MISSION_TYPE_MELEE;
	g_mission_header.num_flight_groups = 7;
	for (int fg = 0; fg < 7; ++fg) {
		g_mission_flight_groups[fg].fg.craft_type = TEST_XWING;
		g_mission_flight_groups[fg].fg.group_ai = (uint8_t)(6 - fg);
	}
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	for (int fg = 0; fg < 7; ++fg) {
		int level = 6 - fg;
		int expected = g_flight_group_rating_base_by_ai_level[level];
		if (level != 0) {
			expected += fg & 3;
		}
		XVT_ASSERT_INT_EQ(g_pilot_data.flight_group_rating[fg],
				  expected);
	}

	for (int step = 0; step < 2; ++step) {
		fresh_flight();
		g_mission_header.mission_type = MISSION_TYPE_MELEE;
		g_pilot_data.mission_sequence_active = 1;
		g_pilot_data.melee_tournament_sequence_state
			.current_mission_index = (uint32_t)step;
		g_mission_flight_groups[1].fg.group_ai = 3;
		g_pilot_data.flight_group_rating[1] = -1;
		fe_disk_io_commit_flight_results(0, 0);
		XVT_ASSERT_INT_EQ(
			g_pilot_data.flight_group_rating[1],
			step == 0
				? g_flight_group_rating_base_by_ai_level[3] + 1
				: -1);
	}
}

/* A campaign mission: a training mission flown while a sequence is active,
 * campaign 2 of four missions, flown alone. */
static void campaign_flight(int mission_index, int completed)
{
	fresh_flight();
	g_mission_header.mission_type = MISSION_TYPE_TRAINING;
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_CAMPAIGNS] = 2;
	g_pilot_data.campaign_sequence_state.mission_count = 4;
	g_pilot_data.campaign_sequence_state.current_mission_index =
		mission_index;
	g_pilot_data.campaign_sequence_state.human_player_count = 1;
	set_goals(0, completed ? 1 : 2, 0);
}

/* The first campaign mission counts an attempt; a completed one raises the
 * next mission to its index plus 1, sets the campaign score to the mission's
 * score (whatever an earlier campaign left there), and makes that the best
 * score; a later completed mission adds its score. Neither is the last
 * mission, and the campaign is not finished. */
static void check_campaign_completed(void)
{
	campaign_flight(0, 1);
	g_pilot_data.campaign_sequence_state.cumulative_score = 999;
	g_players[0].mission_stats.mission_score = 700;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	struct pilot_campaign *record =
		&g_pilot_data.faction_statistics[0].sp_campaigns[2];
	XVT_ASSERT_INT_EQ(record->attempt_count, 1);
	XVT_ASSERT_INT_EQ(record->next_mission_index, 1);
	XVT_ASSERT_INT_EQ(record->best_score, 700);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.campaign_sequence_state.last_mission_completed, 1);
	XVT_ASSERT_INT_EQ(g_pilot_data.campaign_sequence_state.cumulative_score,
			  700);

	memcpy(&g_saved_pilot, &g_pilot_data, sizeof g_saved_pilot);
	campaign_flight(2, 1);
	memcpy(&g_pilot_data.faction_statistics[0].sp_campaigns[2],
	       &g_saved_pilot.faction_statistics[0].sp_campaigns[2],
	       sizeof *record);
	g_pilot_data.campaign_sequence_state.cumulative_score = 700;
	g_players[0].mission_stats.mission_score = 300;
	fe_disk_io_commit_flight_results(0, 0);
	XVT_ASSERT_INT_EQ(record->attempt_count, 1);
	XVT_ASSERT_INT_EQ(record->next_mission_index, 3);
	XVT_ASSERT_INT_EQ(record->best_score, 1000);
	XVT_ASSERT_INT_EQ(g_pilot_data.campaign_sequence_state.cumulative_score,
			  1000);
	XVT_ASSERT_INT_EQ(record->is_finished, 0);
}

/* A campaign flown by two human players counts its attempt in the
 * multiplayer records, not the single-player ones. */
static void check_network_campaign_attempt(void)
{
	campaign_flight(0, 1);
	add_second_human();
	g_pilot_data.campaign_sequence_state.human_player_count = 2;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[0]
				  .mp_campaigns[2]
				  .attempt_count,
			  1);
	XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[0]
				  .sp_campaigns[2]
				  .attempt_count,
			  0);
}

/* A failed campaign mission leaves the next mission and the campaign score,
 * and the best score becomes the campaign score plus the mission's score
 * when that is higher. */
static void check_campaign_failed(void)
{
	campaign_flight(2, 0);
	struct pilot_campaign *record =
		&g_pilot_data.faction_statistics[0].sp_campaigns[2];
	record->next_mission_index = 2;
	record->best_score = 800;
	g_pilot_data.campaign_sequence_state.cumulative_score = 700;
	g_players[0].mission_stats.mission_score = 400;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	XVT_ASSERT_INT_EQ(record->next_mission_index, 2);
	XVT_ASSERT_INT_EQ(record->best_score, 1100);
	XVT_ASSERT_INT_EQ(record->attempt_count, 0);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.campaign_sequence_state.last_mission_completed, 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.campaign_sequence_state.cumulative_score,
			  700);
}

/* A battle mission: a combat mission flown while a sequence is active, at
 * the given step of battle 1, which needs the given wins. */
static void battle_flight(unsigned int step, int wins_needed)
{
	fresh_flight();
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.battle_sequence_state.current_mission_index = step;
	g_pilot_data.battle_sequence_state.victories_needed = wins_needed;
	g_pilot_data.battle_sequence_state.human_player_count = 1;
}

/* Each battle mission's result is kept at its step: team 0 winning is an
 * Imperial victory, team 1 a Rebel victory, and no team winning a draw (team
 * 1's primary goal failed, so it is not credited with it). */
static void check_battle_result_kept(void)
{
	static const struct {
		int winner;
		int result;
	} cases[] = {
		{0, BATTLE_MISSION_RESULT_IMPERIAL_VICTORY},
		{1, BATTLE_MISSION_RESULT_REBEL_VICTORY},
		{-1, BATTLE_MISSION_RESULT_DRAW},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		battle_flight(3, 4);
		g_pilot_data.battle_sequence_state.mission_results[3] = 7;
		g_pilot_data.battle_sequence_state.mission_results[4] = 7;
		if (cases[i].winner >= 0) {
			set_goals(cases[i].winner, 1, 0);
		} else {
			set_goals(1, 2, 0);
		}
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
		XVT_ASSERT_INT_EQ(
			g_pilot_data.battle_sequence_state.mission_results[3],
			cases[i].result);
		XVT_ASSERT_INT_EQ(
			g_pilot_data.battle_sequence_state.mission_results[4],
			7);
	}
}

/* ---- fe_disk_io_commit_flight_results: goals credited to a team --------- */

/* Which team's goals a flight credits. In a mission sequence, the team with no
 * player flight group gets its primary goal when the other team's primary goal
 * is not met and its own is still unset (0). Flown alone outside a sequence,
 * the team with no player flight group gets its primary goal whenever the
 * other team did not meet its own, and the other team's prevent goal is set.
 * With two humans connected outside a sequence, nothing is credited. */
static void check_goal_credited_by_flight_group(void)
{
	/* "none" is the team with no player flight group: 0, 1, or 2 for both
	 * teams having one. */
	static const struct {
		int sequence;
		int humans;
		int none;
		int primary0;
		int primary1;
		int want_primary0;
		int want_primary1;
		int want_prevent0;
		int want_prevent1;
	} cases[] = {
		{1, 1, 1, 2, 0, 2, 1, 0, 0}, {1, 1, 1, 0, 0, 0, 1, 0, 0},
		{1, 1, 1, 1, 0, 1, 0, 0, 0}, {1, 1, 1, 2, 2, 2, 2, 0, 0},
		{1, 1, 0, 0, 2, 1, 2, 0, 0}, {1, 1, 0, 0, 1, 0, 1, 0, 0},
		{1, 1, 0, 2, 2, 2, 2, 0, 0}, {1, 1, 2, 0, 0, 0, 0, 0, 0},
		{0, 1, 0, 0, 2, 1, 2, 0, 1}, {0, 1, 0, 2, 0, 1, 0, 0, 1},
		{0, 1, 0, 0, 1, 0, 1, 0, 0}, {0, 1, 1, 0, 2, 0, 1, 1, 0},
		{0, 1, 1, 1, 0, 1, 0, 0, 0}, {0, 2, 1, 0, 0, 0, 0, 0, 0},
		{0, 2, 0, 0, 0, 0, 0, 0, 0},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		fresh_flight();
		g_pilot_data.mission_sequence_active = cases[i].sequence;
		if (cases[i].none == 0) {
			g_mission_flight_groups[0].player_owner_idx = -1;
		}
		if (cases[i].none != 1) {
			g_mission_flight_groups[1].fg.player_number = 2;
			g_mission_flight_groups[1].player_owner_idx = 1;
		}
		if (cases[i].humans == 2) {
			g_players[1].network.direct_play_id = TEST_OTHER_ID;
		}
		set_goals(0, cases[i].primary0, 0);
		set_goals(1, cases[i].primary1, 0);
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
		const struct mission_flight_runtime_state *runtime =
			&g_flight_mission_state.runtime;
		XVT_ASSERT_INT_EQ(runtime->team_goal_status[0][TEST_PRIMARY],
				  cases[i].want_primary0);
		XVT_ASSERT_INT_EQ(runtime->team_goal_status[1][TEST_PRIMARY],
				  cases[i].want_primary1);
		XVT_ASSERT_INT_EQ(runtime->team_goal_status[0][TEST_PREVENT],
				  cases[i].want_prevent0);
		XVT_ASSERT_INT_EQ(runtime->team_goal_status[1][TEST_PREVENT],
				  cases[i].want_prevent1);
	}
}

/* ---- fe_disk_io_commit_flight_results: the pilot's tallies ------------- */

enum {
	TEST_BASE_TALLY = 1000, /* What every statistics field holds before. */
};

/* Sets every integer of a statistics block to value. */
static void fill_stats(struct pilot_stats *stats, int value)
{
	unsigned char *bytes = (unsigned char *)stats;
	for (size_t i = 0; i + sizeof value <= sizeof *stats;
	     i += sizeof value) {
		memcpy(bytes + i, &value, sizeof value);
	}
}

/* Fills the local player's kills, shots and losses with distinct values: each
 * array entry holds its index plus a number for its array. */
static void fill_local_tallies(int kills_group_0, int kills_group_1)
{
	struct per_mission_kills *kills = &g_players[0].per_mission_kills;
	kills->kills_full_on_flight_group[0] = (uint16_t)kills_group_0;
	kills->kills_shared_on_flight_group[0] = (uint16_t)(kills_group_0 + 1);
	kills->kills_assist_on_flight_group[0] = (uint16_t)(kills_group_0 + 2);
	kills->kills_full_on_flight_group[1] = (uint16_t)kills_group_1;
	kills->kills_shared_on_flight_group[1] = (uint16_t)(kills_group_1 + 1);
	kills->kills_assist_on_flight_group[1] = (uint16_t)(kills_group_1 + 2);
	for (int rating = 0; rating < 25; ++rating) {
		kills->kills_full_on_player_rating[rating] =
			(uint16_t)(rating + 1);
		kills->kills_shared_on_player_rating[rating] =
			(uint16_t)(rating + 2);
		kills->kills_assist_on_player_rating[rating] =
			(uint16_t)(rating + 3);
		kills->killed_by_player_rating[rating] = (uint16_t)(rating + 4);
	}
	for (int level = 0; level < 6; ++level) {
		kills->kills_full_on_ai_rating[level] = (uint16_t)(level + 5);
		kills->kills_shared_on_ai_rating[level] = (uint16_t)(level + 6);
		kills->kills_assist_on_ai_rating[level] = (uint16_t)(level + 7);
		kills->killed_by_ai_rating[level] = (uint16_t)(level + 8);
	}
	kills->friendlies_killed = 6;
	kills->num_special_inspected = 21;
	kills->warhead_hits = 16;
	kills->total_craft_losses = 17;
	kills->losses_by_collisions = 18;
	kills->losses_by_starships = 19;
	kills->losses_by_mines = 20;
	g_players[0].warheads_fired = 15;
	g_players[0].mission_stats.laser_shots_fired = 11;
	g_players[0].mission_stats.ion_shots_fired = 12;
	g_players[0].mission_stats.laser_hits_scored = 13;
	g_players[0].mission_stats.ion_hits_scored = 14;
}

/* Adds to stats, under mission type slot mt, what fill_local_tallies gave the
 * local player on a flight of two groups: kills_group_0 full kills on a group
 * of craft type 1 and kills_group_1 on a group of craft type 2. */
static void add_expected_tallies(struct pilot_stats *stats, int mt,
				 int kills_group_0, int kills_group_1)
{
	stats->total_kills_per_mt[mt] += kills_group_0 + kills_group_1;
	stats->kills_per_craft_per_mt[mt][1] += kills_group_0;
	stats->kills_per_craft_per_mt[mt][2] += kills_group_1;
	stats->kills_shared_per_craft_per_mt[mt][1] += kills_group_0 + 1;
	stats->kills_shared_per_craft_per_mt[mt][2] += kills_group_1 + 1;
	stats->kills_assists_per_craft_per_mt[mt][1] += kills_group_0 + 2;
	stats->kills_assists_per_craft_per_mt[mt][2] += kills_group_1 + 2;
	stats->total_friendlies_killed_per_mt[mt] += 6;
	for (int rating = 0; rating < 25; ++rating) {
		stats->kills_full_on_player_rating_per_mt[mt][rating] +=
			rating + 1;
		stats->kills_shared_on_player_rating_per_mt[mt][rating] +=
			rating + 2;
		stats->kills_assist_on_player_rating_per_mt[mt][rating] +=
			rating + 3;
		stats->killed_by_player_rating_per_mt[mt][rating] += rating + 4;
	}
	for (int level = 0; level < 6; ++level) {
		stats->kills_full_on_ai_rating_per_mt[mt][level] += level + 5;
		stats->kills_shared_on_ai_rating_per_mt[mt][level] += level + 6;
		stats->kills_assist_on_ai_rating_per_mt[mt][level] += level + 7;
		stats->killed_by_ai_rating_per_mt[mt][level] += level + 8;
	}
	stats->num_special_inspected_per_mt[mt] += 21;
	stats->energy_fired_per_mt[mt] += 11 + 12;
	stats->energy_hits_per_mt[mt] += 13 + 14;
	stats->warheads_fired_per_mt[mt] += 15;
	stats->warheads_hits_per_mt[mt] += 16;
	stats->total_craft_losses_per_mt[mt] += 17;
	stats->losses_by_collisions_per_mt[mt] += 18;
	stats->losses_by_starships_per_mt[mt] += 19;
	stats->losses_by_mines_per_mt[mt] += 20;
}

/* The local player's score, kills, shots and losses go into the current
 * faction's statistics and the main statistics under the mission's type
 * (training, melee, combat), and alone into the last-mission statistics at
 * type 0; the flight counts as standalone or in a sequence. A flight group
 * whose craft type has an object type past the statistics' 100 entries (craft
 * type 86) is left out of the kill tallies. */
static void check_pilot_tallies(void)
{
	static const int mission_types[3] = {
		MISSION_TYPE_TRAINING, MISSION_TYPE_MELEE, MISSION_TYPE_COMBAT};
	for (int mt = 0; mt < 3; ++mt) {
		for (int sequence = 0; sequence < 2; ++sequence) {
			fresh_flight();
			g_mission_header.mission_type = mission_types[mt];
			g_mission_header.num_flight_groups = 3;
			g_mission_flight_groups[1].fg.craft_type = 2;
			g_mission_flight_groups[2].fg.craft_type = 86;
			g_mission_flight_groups[2].fg.team = 2;
			g_pilot_data.mission_sequence_active = sequence;
			fill_local_tallies(5, 7);
			g_players[0]
				.per_mission_kills
				.kills_full_on_flight_group[2] = 9;
			g_players[0]
				.per_mission_kills
				.kills_shared_on_flight_group[2] = 9;
			g_players[0].mission_stats.mission_score = 800;
			g_flight_mission_state.runtime
				.team_scores[TEAM_SCORE_BONUS][0] = 234;
			fill_stats(&g_pilot_data.faction_statistics[0].stats,
				   TEST_BASE_TALLY);
			fill_stats(&g_pilot_data.main_stats, TEST_BASE_TALLY);
			XVT_ASSERT_INT_EQ(
				fe_disk_io_commit_flight_results(0, 0), 0);

			struct pilot_stats expected;
			fill_stats(&expected, TEST_BASE_TALLY);
			add_expected_tallies(&expected, mt, 5, 7);
			expected.total_score_per_mt[mt] += 1034;
			expected.standalone_missions_played_per_mt[mt] +=
				sequence == 0;
			expected.sequence_missions_played_per_mt[mt] +=
				sequence == 1;
			XVT_ASSERT_TRUE(
				memcmp(&expected,
				       &g_pilot_data.faction_statistics[0]
						.stats,
				       sizeof expected) == 0);
			XVT_ASSERT_TRUE(memcmp(&expected,
					       &g_pilot_data.main_stats,
					       sizeof expected) == 0);

			struct pilot_stats last;
			memset(&last, 0, sizeof last);
			add_expected_tallies(&last, 0, 5, 7);
			XVT_ASSERT_TRUE(memcmp(&last,
					       &g_pilot_data.last_mission_stats,
					       sizeof last) == 0);
			XVT_ASSERT_INT_EQ(
				g_pilot_data.total_missions_played_count, 1);
			XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[0]
						  .total_missions_played_count,
					  1);
			XVT_ASSERT_INT_EQ(
				g_pilot_data.faction_statistics[0].total_score,
				1034);
			XVT_ASSERT_INT_EQ(g_pilot_data.total_score, 1034);
			XVT_ASSERT_INT_EQ(g_pilot_data.mission_score, 1034);
		}
	}
}

/* ---- fe_disk_io_commit_flight_results: promotion and demotion ---------- */

/* A flight's effect on the pilot's rating. Below the cap (Officer 1st Class
 * for training, Jedi Master for the second simulator, melee and combat) the
 * flight's promotion points are added, and its worse-rating points too while
 * the worse points held are under half the rating's threshold. Reaching the
 * rating's threshold promotes: training then sets the points to 0, melee and
 * combat subtract the threshold; the progress is 100 times the points over the
 * (new) rating's threshold, at most 100, or for negative points 100 times them
 * over 2000. At the cap only negative points count. Below -2000 a pilot above
 * target drone is demoted, with the points set to 0. The rating is recorded
 * as achieved on the mission only for a promotion, or a demotion to a rating
 * below trainee. */
static void check_promotion_and_demotion(void)
{
	static const struct {
		int type;
		int rating;
		int points;
		int worse;
		int gain;
		int worse_gain;
		int want_rating;
		int want_points;
		int want_worse;
		int want_delta;
		int want_percent;
		int achieved; /* Rating slot expected to read 7, or -1. */
	} cases[] = {
		/* Training: add, worse points, promotion, cap, demotion. */
		{MISSION_TYPE_TRAINING, 2, 0, 0, 100, 50, 2, 150, 50, 0, 20,
		 -1},
		{MISSION_TYPE_TRAINING, 2, 700, 0, 100, 0, 3, 0, 0, 1, 0, 3},
		{MISSION_TYPE_TRAINING, 2, 100, 400, 10, 99, 2, 110, 400, 0, 14,
		 -1},
		{MISSION_TYPE_TRAINING, 7, 50, 0, 500, 99, 7, 50, 0, 0, 0, -1},
		{MISSION_TYPE_TRAINING, 7, 50, 0, -300, 0, 7, -250, 0, 0, 0,
		 -1},
		{MISSION_TYPE_TRAINING, 2, 0, 0, -1000, 0, 2, -1000, 0, 0, -50,
		 -1},
		{MISSION_TYPE_TRAINING, 2, -1900, 30, -200, 0, 1, 0, 0, -1, 0,
		 1},
		{MISSION_TYPE_TRAINING, 3, -1900, 30, -200, 0, 2, 0, 0, -1, 0,
		 -1},
		{MISSION_TYPE_TRAINING, 0, 0, 0, -50, 0, 0, 0, 0, 0, 0, -1},
		{MISSION_TYPE_TRAINING, 2, 0, 0, -2000, 0, 2, -2000, 0, 0, -100,
		 -1},
		{MISSION_TYPE_TRAINING, 2, 0, 375, 10, 40, 2, 10, 375, 0, 1,
		 -1},
		{MISSION_TYPE_TRAINING, 2, 650, 0, 100, 0, 3, 0, 0, 1, 0, 3},
		{MISSION_TYPE_TRAINING, 0, -10, 5, 10, 0, 0, 0, 5, 0, 0, -1},
		{MISSION_TYPE_SIMULATOR_2, 7, 0, 0, 100, 0, 7, 100, 0, 0, 3,
		 -1},
		{MISSION_TYPE_SIMULATOR_2, 24, 0, 0, 500, 0, 24, 0, 0, 0, 0,
		 -1},
		/* Melee and combat: the threshold is subtracted. */
		{MISSION_TYPE_MELEE, 2, 700, 0, 100, 0, 3, 50, 0, 1, 4, 3},
		{MISSION_TYPE_COMBAT, 2, 700, 0, 100, 0, 3, 50, 0, 1, 4, 3},
		{MISSION_TYPE_COMBAT, 2, 0, 0, 10, 40, 2, 50, 40, 0, 6, -1},
		{MISSION_TYPE_MELEE, 2, 0, 375, 10, 40, 2, 10, 375, 0, 1, -1},
		{MISSION_TYPE_MELEE, 2, 650, 0, 100, 0, 3, 0, 0, 1, 0, 3},
		{MISSION_TYPE_COMBAT, 0, -10, 5, 10, 0, 0, 0, 5, 0, 0, -1},
		{MISSION_TYPE_MELEE, 2, 0, 0, -2000, 0, 2, -2000, 0, 0, -100,
		 -1},
		{MISSION_TYPE_MELEE, 0, 0, 0, 6250, 0, 1, 6000, 0, 1, 100, 1},
		{MISSION_TYPE_MELEE, 24, 0, 0, 500, 0, 24, 0, 0, 0, 0, -1},
		{MISSION_TYPE_COMBAT, 24, 0, 0, -100, 0, 24, -100, 0, 0, 0, -1},
		{MISSION_TYPE_MELEE, 2, 0, 0, -1000, 0, 2, -1000, 0, 0, -50,
		 -1},
		{MISSION_TYPE_MELEE, 2, -1900, 30, -200, 0, 1, 0, 0, -1, 0, 1},
		{MISSION_TYPE_COMBAT, 1, -1900, 30, -200, 0, 0, 0, 0, -1, 0, 0},
		{MISSION_TYPE_COMBAT, 3, -1900, 30, -200, 0, 2, 0, 0, -1, 0,
		 -1},
		{MISSION_TYPE_COMBAT, 0, 0, 0, -50, 0, 0, 0, 0, 0, 0, -1},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		fresh_flight();
		g_mission_header.mission_type = cases[i].type;
		g_pilot_data.rating = cases[i].rating;
		g_pilot_data.current_rating_promo_points = cases[i].points;
		g_pilot_data.current_rating_worse_promo_points = cases[i].worse;
		g_pilot_data.total_missions_played_count = 6;
		g_players[0].mission_stats.rating_promo_points = cases[i].gain;
		g_players[0].mission_stats.worse_rating_promo_points =
			cases[i].worse_gain;
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
		XVT_ASSERT_INT_EQ(g_pilot_data.rating, cases[i].want_rating);
		XVT_ASSERT_INT_EQ(g_pilot_data.current_rating_promo_points,
				  cases[i].want_points);
		XVT_ASSERT_INT_EQ(
			g_pilot_data.current_rating_worse_promo_points,
			cases[i].want_worse);
		XVT_ASSERT_INT_EQ(g_pilot_data.promotion_delta,
				  cases[i].want_delta);
		XVT_ASSERT_INT_EQ(g_pilot_data.next_promotion_percent,
				  cases[i].want_percent);
		for (int slot = 0; slot < 25; ++slot) {
			XVT_ASSERT_INT_EQ(
				g_pilot_data.rating_achieved_on_mission[slot],
				slot == cases[i].achieved ? 7 : 0);
		}
	}
}

/* ---- fe_disk_io_commit_flight_results: kills and network players ------- */

/* For each human player who is a network entry of the pilot record, matched by
 * DirectPlay id, the local player's kills on and from that player (full and
 * shared) are copied to the entry's number; the local player's kills on and
 * from each flight group are copied to the group's number. */
static void check_kills_by_player_and_group(void)
{
	fresh_flight();
	add_second_human();
	g_players[2].network.direct_play_id = 300;
	g_pilot_data.network_players[4].direct_play_id = 300;
	g_pilot_data.network_players[1].direct_play_id = TEST_OTHER_ID;
	g_pilot_data.network_players[2].direct_play_id = 0;
	struct per_mission_kills *kills = &g_players[0].per_mission_kills;
	for (int slot = 0; slot < 8; ++slot) {
		kills->kills_full_on_player[slot] = (uint16_t)(10 + slot);
		kills->kills_shared_on_player[slot] = (uint16_t)(20 + slot);
		kills->kills_full_from_player[slot] = (uint16_t)(30 + slot);
		kills->kills_shared_from_player[slot] = (uint16_t)(40 + slot);
	}
	for (int fg = 0; fg < 2; ++fg) {
		kills->kills_full_on_flight_group[fg] = (uint16_t)(50 + fg);
		kills->kills_shared_on_flight_group[fg] = (uint16_t)(60 + fg);
		kills->kills_full_from_flight_group[fg] = (uint16_t)(70 + fg);
		kills->kills_shared_from_flight_group[fg] = (uint16_t)(80 + fg);
	}
	kills->kills_full_on_flight_group[2] = 99;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	static const struct {
		int entry;
		int slot;
	} matches[] = {{0, 0}, {1, 1}, {4, 2}};
	for (size_t i = 0; i < sizeof matches / sizeof matches[0]; ++i) {
		int entry = matches[i].entry;
		int slot = matches[i].slot;
		XVT_ASSERT_INT_EQ(g_pilot_data.kills_full_on_player[entry],
				  10 + slot);
		XVT_ASSERT_INT_EQ(g_pilot_data.kills_shared_on_player[entry],
				  20 + slot);
		XVT_ASSERT_INT_EQ(g_pilot_data.kills_full_from_player[entry],
				  30 + slot);
		XVT_ASSERT_INT_EQ(g_pilot_data.kills_shared_from_player[entry],
				  40 + slot);
	}
	XVT_ASSERT_INT_EQ(g_pilot_data.kills_full_on_player[2], 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.kills_full_on_player[3], 0);
	for (int fg = 0; fg < 2; ++fg) {
		XVT_ASSERT_INT_EQ(g_pilot_data.kills_full_on_flight_group[fg],
				  50 + fg);
		XVT_ASSERT_INT_EQ(g_pilot_data.kills_shared_on_flight_group[fg],
				  60 + fg);
		XVT_ASSERT_INT_EQ(g_pilot_data.kills_full_from_flight_group[fg],
				  70 + fg);
		XVT_ASSERT_INT_EQ(
			g_pilot_data.kills_shared_from_flight_group[fg],
			80 + fg);
	}
	/* Flight group 2 is past the mission's two groups. */
	XVT_ASSERT_INT_EQ(g_pilot_data.kills_full_on_flight_group[2], 0);
}

/* A network player's entry in the pilot record gets the player's kills, shared
 * kills and kill assists summed over the mission's flight groups (added to
 * what the entry held), its total score (its mission score plus its team's
 * bonus score) and its craft losses; a player with no matching entry changes
 * none. */
static void check_network_player_totals(void)
{
	fresh_flight();
	add_second_human();
	g_players[2].network.direct_play_id = 300;
	g_pilot_data.network_players[1].kills = 100;
	g_pilot_data.network_players[1].kills_shared = 200;
	g_pilot_data.network_players[1].kills_assist = 300;
	g_pilot_data.network_players[1].total_score = 9999;
	g_pilot_data.network_players[1].total_losses = 9999;
	g_pilot_data.network_players[0].kills = 1;
	g_flight_mission_state.runtime.team_scores[TEAM_SCORE_BONUS][1] = 70;
	g_flight_mission_state.runtime.team_scores[TEAM_SCORE_BONUS][3] = 5;
	g_players[1].mission_stats.mission_score = 400;
	g_players[2].team = 3;
	g_players[2].mission_stats.mission_score = 8000;
	struct per_mission_kills *kills = &g_players[1].per_mission_kills;
	kills->kills_full_on_flight_group[0] = 2;
	kills->kills_full_on_flight_group[1] = 3;
	kills->kills_shared_on_flight_group[0] = 4;
	kills->kills_shared_on_flight_group[1] = 5;
	kills->kills_assist_on_flight_group[0] = 6;
	kills->kills_assist_on_flight_group[1] = 7;
	kills->total_craft_losses = 8;
	/* A flight group past the mission's two counts for nothing. */
	kills->kills_full_on_flight_group[2] = 1000;
	kills->kills_shared_on_flight_group[2] = 1000;
	kills->kills_assist_on_flight_group[2] = 1000;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	const struct pilot_network_player *entry =
		&g_pilot_data.network_players[1];
	XVT_ASSERT_INT_EQ(entry->kills, 105);
	XVT_ASSERT_INT_EQ(entry->kills_shared, 209);
	XVT_ASSERT_INT_EQ(entry->kills_assist, 313);
	XVT_ASSERT_INT_EQ(entry->total_score, 470);
	XVT_ASSERT_INT_EQ(entry->total_losses, 8);
	XVT_ASSERT_INT_EQ(g_pilot_data.network_players[0].kills, 1);
	XVT_ASSERT_INT_EQ(g_pilot_data.network_players[2].total_score, 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.network_players[2].kills, 0);
}

/* ---- fe_disk_io_commit_flight_results: the mission's award ------------- */

enum {
	TEST_AWARD_MELEE = 0, /* Slots of the faction's mission_awards. */
	TEST_AWARD_TOURNAMENT = 1,
	TEST_AWARD_EVALUATION = 2,
	TEST_AWARD_BATTLE = 3,
	TEST_FAILED_AWARD = 6, /* The award that marks a failed mission. */
};

/* A training or combat flight for the award: the local player's team has
 * the goal status given (primary, prevent) and the score; team 1 has a player
 * flight group, so no goal is credited to it; a second human is connected when
 * humans is 2. */
static void award_flight(int mission_type, int humans, int difficulty,
			 int waves, int primary, int prevent, int score)
{
	fresh_flight();
	g_mission_header.mission_type = mission_type;
	g_flight_mission_state.difficulty = (game_difficulty)difficulty;
	g_flight_mission_state.player_flight_group_wave_mode =
		(craft_wave_mode)waves;
	add_second_human();
	if (humans == 1) {
		g_players[1].network.direct_play_id = 0;
	}
	set_goals(0, primary, prevent);
	g_players[0].mission_stats.mission_score = score;
}

/* A training flight's award, when its goal is met (primary 1, prevent not 1):
 * 1 for a score of 50,000 or more, 2 for 20,000, 3 for 0 and 4 below, plus 2 on
 * easy and 1 on medium, at least 5 with unlimited waves, and none (0) above
 * 5. Unmet, it is the failed award (6) for a score of 0 or less, else none. In
 * a sequence it is also the failed award with unlimited waves or a difficulty
 * setting above hard. It is the faction's evaluation award slot. */
static void check_training_award(void)
{
	static const struct {
		int sequence;
		int difficulty;
		int waves;
		int primary;
		int prevent;
		int score;
		int config_difficulty;
		int want;
	} cases[] = {
		{0, 2, 1, 1, 0, 60000, 0, 1}, {0, 2, 1, 1, 0, 50000, 0, 1},
		{0, 2, 1, 1, 0, 49999, 0, 2}, {0, 2, 1, 1, 0, 20000, 0, 2},
		{0, 2, 1, 1, 0, 19999, 0, 3}, {0, 2, 1, 1, 0, 0, 0, 3},
		{0, 2, 1, 1, 0, -1, 0, 4},    {0, 0, 1, 1, 0, 60000, 0, 3},
		{0, 0, 1, 1, 0, 20000, 0, 4}, {0, 0, 1, 1, 0, 0, 0, 5},
		{0, 0, 1, 1, 0, -1, 0, 0},    {0, 1, 1, 1, 0, 60000, 0, 2},
		{0, 1, 1, 1, 0, 0, 0, 4},     {0, 1, 1, 1, 0, -1, 0, 5},
		{0, 2, 2, 1, 0, 60000, 0, 5}, {0, 0, 2, 1, 0, -1, 0, 0},
		{0, 2, 1, 2, 0, 5, 0, 0},     {0, 2, 1, 0, 0, 0, 0, 6},
		{0, 2, 1, 0, 0, -3, 0, 6},    {0, 2, 1, 0, 0, 7, 0, 0},
		{0, 2, 1, 1, 1, 0, 0, 6},     {0, 2, 1, 1, 1, 9, 0, 0},
		{1, 2, 1, 1, 0, 60000, 0, 1}, {1, 2, 2, 1, 0, 60000, 0, 6},
		{1, 2, 1, 1, 0, 60000, 3, 6}, {1, 2, 1, 1, 0, 60000, 2, 1},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		award_flight(MISSION_TYPE_TRAINING, 1, cases[i].difficulty,
			     cases[i].waves, cases[i].primary, cases[i].prevent,
			     cases[i].score);
		g_pilot_data.mission_sequence_active = cases[i].sequence;
		g_game_config.difficulty =
			(game_difficulty)cases[i].config_difficulty;
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
		const struct pilot_faction *faction =
			&g_pilot_data.faction_statistics[0];
		XVT_ASSERT_INT_EQ(
			faction->mission_awards[TEST_AWARD_EVALUATION],
			cases[i].want);
		XVT_ASSERT_INT_EQ(faction->mission_awards[TEST_AWARD_MELEE], 0);
		XVT_ASSERT_INT_EQ(
			faction->mission_awards[TEST_AWARD_TOURNAMENT], 0);
		XVT_ASSERT_INT_EQ(faction->mission_awards[TEST_AWARD_BATTLE],
				  0);
	}
	g_game_config.difficulty = GAME_DIFFICULTY_EASY;
}

/* A combat flight's award with one human is the training award's rule, except
 * that unlimited waves make it 5 whatever the score and an award past 5 is
 * none; a team whose primary goal failed (2) or whose prevent goal is set is
 * the failed award whatever the score. With two or more humans the score
 * steps are 50,000, 40,000, 30,000, 20,000 and 10,000 for awards 1 to 5, below
 * that none, and unmet goals give the failed award only below -25,000. */
static void check_combat_award(void)
{
	static const struct {
		int humans;
		int difficulty;
		int waves;
		int primary;
		int prevent;
		int score;
		int want;
	} cases[] = {
		{1, 2, 1, 1, 0, 60000, 1},  {1, 2, 1, 1, 0, 20000, 2},
		{1, 2, 1, 1, 0, 0, 3},	    {1, 2, 1, 1, 0, -1, 4},
		{1, 0, 1, 1, 0, 60000, 3},  {1, 0, 1, 1, 0, -1, 0},
		{1, 1, 1, 1, 0, 60000, 2},  {1, 1, 1, 1, 0, -1, 5},
		{1, 2, 2, 1, 0, -1, 5},	    {1, 0, 2, 1, 0, 60000, 5},
		{1, 2, 1, 2, 0, 90000, 6},  {1, 2, 1, 0, 1, 90000, 6},
		{1, 2, 1, 0, 0, 0, 6},	    {1, 2, 1, 0, 0, -1, 6},
		{1, 2, 1, 0, 0, 1, 0},	    {2, 2, 1, 1, 0, 50000, 1},
		{2, 2, 1, 1, 0, 49999, 2},  {2, 2, 1, 1, 0, 40000, 2},
		{2, 2, 1, 1, 0, 39999, 3},  {2, 2, 1, 1, 0, 30000, 3},
		{2, 2, 1, 1, 0, 29999, 4},  {2, 2, 1, 1, 0, 20000, 4},
		{2, 2, 1, 1, 0, 19999, 5},  {2, 2, 1, 1, 0, 10000, 5},
		{2, 2, 1, 1, 0, 9999, 0},   {2, 0, 1, 1, 0, 60000, 1},
		{2, 2, 2, 1, 0, 9999, 5},   {2, 2, 1, 2, 0, 60000, 6},
		{2, 2, 1, 0, 1, 60000, 6},  {2, 2, 1, 0, 0, -25000, 0},
		{2, 2, 1, 0, 0, -25001, 6},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		award_flight(MISSION_TYPE_COMBAT, cases[i].humans,
			     cases[i].difficulty, cases[i].waves,
			     cases[i].primary, cases[i].prevent,
			     cases[i].score);
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
		const struct pilot_faction *faction =
			&g_pilot_data.faction_statistics[0];
		XVT_ASSERT_INT_EQ(
			faction->mission_awards[TEST_AWARD_EVALUATION],
			cases[i].want);
		XVT_ASSERT_INT_EQ(faction->mission_awards[TEST_AWARD_MELEE], 0);
	}
}

/* A melee of the given number of teams, one flight group each, flown by
 * humans players (teams 0 to humans - 1, the local player on team 0); the
 * other teams are computer opponents. Team 0's mission score is own and the
 * local player's score is score. Each other team is above the player's team
 * by 100 when its number is below placement, else below it by gap, so the
 * player's team places placement. */
static void melee_flight(int teams, int humans, int placement, int own, int gap,
			 int score)
{
	fresh_flight();
	g_mission_header.mission_type = MISSION_TYPE_MELEE;
	g_mission_header.num_flight_groups = (uint8_t)teams;
	g_flight_mission_state.ai_opponents_enabled = 1;
	for (int team = 0; team < teams; ++team) {
		struct mission_flight_group *group =
			&g_mission_flight_groups[team];
		group->fg.craft_type = TEST_XWING;
		group->fg.team = (uint8_t)team;
		group->fg.player_number = 1;
		group->fg.number_of_craft = 2;
		group->fg.number_of_waves = 1;
		group->player_owner_idx = team < humans ? team : -1;
		g_mission_fg_stats[team].has_arrived = 1;
		if (team < humans) {
			g_players[team].network.direct_play_id =
				100 * (team + 1);
			g_players[team].team = (uint8_t)team;
			g_players[team].bound_flight_group_idx = (uint8_t)team;
			g_pilot_data.network_players[team].direct_play_id =
				100 * (team + 1);
		}
		g_flight_mission_state.runtime
			.team_scores[TEAM_SCORE_MISSION][team] =
			team == 0 ? own
				  : (team < placement ? own + 100 : own - gap);
	}
	g_players[0].mission_stats.mission_score = score;
}

/* With two or more humans in a melee of two or more teams, the award comes
 * from g_placement_award_levels for the team count and the player team's
 * placement 1 to 3 (when the score is over 5,000), is the failed award for
 * last place among four or more teams, and a first place lowers it by 1, 2 or
 * 3 for a lead over 5,000, 10,000 or 15,000, but never below 1. */
static void check_melee_award_by_placement(void)
{
	for (int teams = 2; teams <= 8; ++teams) {
		for (int placement = 1; placement <= teams; ++placement) {
			melee_flight(teams, 2, placement, 6000, 100, 6000);
			XVT_ASSERT_INT_EQ(
				fe_disk_io_commit_flight_results(0, 0), 0);
			int want = 0;
			if (placement == teams && teams >= 4) {
				want = TEST_FAILED_AWARD;
			} else if (placement < 4) {
				want = g_placement_award_levels[3 * teams - 4 +
								placement];
			}
			XVT_ASSERT_INT_EQ(
				g_pilot_data.faction_statistics[0]
					.mission_awards[TEST_AWARD_MELEE],
				want);
		}
	}
	static const struct {
		int teams;
		int gap;
		int score;
		int want;
	} leads[] = {
		{2, 5000, 6000, 5},  {2, 5001, 6000, 4},  {2, 10000, 6000, 4},
		{2, 10001, 6000, 3}, {2, 15000, 6000, 3}, {2, 15001, 6000, 2},
		{2, 30000, 6000, 2}, {7, 15001, 6000, 1}, {7, 5001, 6000, 1},
		{5, 10001, 6000, 1}, {3, 100, 5000, 0},	  {3, 100, 5001, 5},
	};
	for (size_t i = 0; i < sizeof leads / sizeof leads[0]; ++i) {
		melee_flight(leads[i].teams, 2, 1, 6000, leads[i].gap,
			     leads[i].score);
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
		XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[0]
					  .mission_awards[TEST_AWARD_MELEE],
				  leads[i].want);
	}
}

/* With one human, a melee's award follows the difficulty. Easy: 5 for first
 * place, 6 for fifth or worse. Medium: first place 5 for a score of 0 or
 * less, else 4, or 3 and 2 for a lead over 5,000 and 10,000; second place 5
 * when over two teams play; last place, or worse than sixth, 6. Hard: first
 * place 5 for a score of 0 or less, else 3, or 2 and 1 for a lead over 5,000
 * and 10,000; second place 4 over four teams and 5 over two; third place 5
 * over four teams; worse than seventh 6. */
static void check_melee_award_by_difficulty(void)
{
	static const struct {
		int difficulty;
		int teams;
		int placement;
		int gap;
		int score;
		int want;
	} cases[] = {
		{0, 3, 1, 100, 3000, 5},   {0, 3, 2, 100, 3000, 0},
		{0, 6, 4, 100, 3000, 0},   {0, 6, 5, 100, 3000, 6},
		{0, 6, 6, 100, 3000, 6},   {1, 3, 1, 100, 0, 5},
		{1, 3, 1, 100, 3000, 4},   {1, 3, 1, 5001, 3000, 3},
		{1, 3, 1, 10001, 3000, 2}, {1, 3, 2, 100, 3000, 5},
		{1, 2, 2, 100, 3000, 6},   {1, 4, 3, 100, 3000, 0},
		{1, 4, 4, 100, 3000, 6},   {1, 8, 7, 100, 3000, 6},
		{1, 8, 6, 100, 3000, 0},   {2, 3, 1, 100, 0, 5},
		{2, 3, 1, 100, 3000, 3},   {2, 3, 1, 5001, 3000, 2},
		{2, 3, 1, 10001, 3000, 1}, {2, 5, 2, 100, 3000, 4},
		{2, 3, 2, 100, 3000, 5},   {2, 2, 2, 100, 3000, 0},
		{2, 5, 3, 100, 3000, 5},   {2, 4, 3, 100, 3000, 0},
		{2, 8, 8, 100, 3000, 6},   {2, 8, 7, 100, 3000, 0},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		melee_flight(cases[i].teams, 1, cases[i].placement, 6000,
			     cases[i].gap, cases[i].score);
		g_flight_mission_state.difficulty =
			(game_difficulty)cases[i].difficulty;
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
		XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[0]
					  .mission_awards[TEST_AWARD_MELEE],
				  cases[i].want);
	}
}

/* ---- fe_disk_io_commit_flight_results: the mission's record ------------ */

enum {
	REC_SP_TRAINING, /* The history record a flight is kept in. */
	REC_SP_CAMPAIGN,
	REC_SP_MELEE,
	REC_SP_COMBAT,
	REC_MP_CAMPAIGN,
	REC_MP_TRAINING,
	REC_MP_MELEE,
	REC_MP_COMBAT,
	TEST_MISSION_ID = 7,
	TEST_CAMPAIGN_MISSION_ID = 5,
	TEST_CAMPAIGN_ID = 3,
};

/* What a history record holds, the same for every record type. */
struct record_view {
	int flown;
	int completed;
	int failed;
	int best_score;
	int best_time;
	int best_place;
	int award;
	int margin;
	int first;
	int second;
	int third;
	int eligible;
	int campaign;
};

static int record_is_melee(int kind)
{
	return kind == REC_SP_MELEE || kind == REC_MP_MELEE;
}

static int record_humans(int kind) { return kind >= REC_MP_CAMPAIGN ? 2 : 1; }

static int record_mission_type(int kind)
{
	switch (kind) {
	case REC_SP_MELEE:
	case REC_MP_MELEE:
		return MISSION_TYPE_MELEE;
	case REC_SP_COMBAT:
	case REC_MP_COMBAT:
		return MISSION_TYPE_COMBAT;
	default:
		return MISSION_TYPE_TRAINING;
	}
}

static int record_in_sequence(int kind)
{
	return kind == REC_SP_CAMPAIGN || kind == REC_MP_CAMPAIGN;
}

/* The slot of the faction's award counts a record type moves: melee plaques
 * for melees, evaluations for everything else. */
static int record_count(int kind, int slot)
{
	const struct pilot_faction *faction =
		&g_pilot_data.faction_statistics[0];
	return record_is_melee(kind) ? faction->melee_plaques[slot]
				     : faction->mission_evaluations[slot];
}

static void record_set_count(int kind, int slot, int value)
{
	struct pilot_faction *faction = &g_pilot_data.faction_statistics[0];
	if (record_is_melee(kind)) {
		faction->melee_plaques[slot] = value;
	} else {
		faction->mission_evaluations[slot] = value;
	}
}

static void record_load(int kind, struct record_view *view)
{
	struct pilot_faction *faction = &g_pilot_data.faction_statistics[0];
	memset(view, 0, sizeof *view);
	if (kind == REC_SP_TRAINING || kind == REC_SP_MELEE ||
	    kind == REC_SP_COMBAT) {
		const struct pilot_mission *record =
			kind == REC_SP_TRAINING ? &faction->sp_training_missions
							   [TEST_MISSION_ID]
			: kind == REC_SP_MELEE
				? &faction->sp_melee_missions[TEST_MISSION_ID]
				: &faction->sp_combat_missions[TEST_MISSION_ID];
		view->flown = record->number_times_flown;
		view->completed = record->completed_count;
		view->failed = record->failed_count;
		view->best_score = record->best_score;
		view->best_time = record->best_time;
		view->best_place = record->best_placement;
		view->award = record->award_level;
		view->margin = (int)record->best_margin;
	} else if (kind == REC_SP_CAMPAIGN || kind == REC_MP_CAMPAIGN) {
		const struct pilot_campaign_mission *record =
			kind == REC_SP_CAMPAIGN
				? &faction->sp_campaign_missions
					   [TEST_CAMPAIGN_MISSION_ID - 1]
				: &faction->mp_campaign_missions
					   [TEST_CAMPAIGN_MISSION_ID - 1];
		view->flown = record->number_times_flown;
		view->completed = record->is_completed;
		view->best_score = (int)record->best_score;
		view->best_time = record->best_time;
		view->award = record->award_level;
		view->eligible = record->award_eligible;
		view->campaign = record->campaign_id;
	} else {
		const struct pilot_multiplayer_mission *record =
			kind == REC_MP_TRAINING ? &faction->mp_training_missions
							   [TEST_MISSION_ID]
			: kind == REC_MP_MELEE
				? &faction->mp_melee_missions[TEST_MISSION_ID]
				: &faction->mp_combat_missions[TEST_MISSION_ID];
		view->flown = record->number_times_flown;
		view->completed = record->completed_count;
		view->failed = record->failed_count;
		view->best_score = record->best_score;
		view->best_time = record->best_time;
		view->best_place = record->best_placement;
		view->award = record->award_level;
		view->margin = (int)record->best_margin;
		view->first = record->first_place_count;
		view->second = record->second_place_count;
		view->third = record->third_place_count;
	}
}

static void record_set_award(int kind, int award)
{
	struct pilot_faction *faction = &g_pilot_data.faction_statistics[0];
	switch (kind) {
	case REC_SP_TRAINING:
		faction->sp_training_missions[TEST_MISSION_ID].award_level =
			award;
		break;
	case REC_SP_MELEE:
		faction->sp_melee_missions[TEST_MISSION_ID].award_level = award;
		break;
	case REC_SP_COMBAT:
		faction->sp_combat_missions[TEST_MISSION_ID].award_level =
			award;
		break;
	case REC_SP_CAMPAIGN:
		faction->sp_campaign_missions[TEST_CAMPAIGN_MISSION_ID - 1]
			.award_level = award;
		break;
	case REC_MP_CAMPAIGN:
		faction->mp_campaign_missions[TEST_CAMPAIGN_MISSION_ID - 1]
			.award_level = award;
		break;
	case REC_MP_TRAINING:
		faction->mp_training_missions[TEST_MISSION_ID].award_level =
			award;
		break;
	case REC_MP_MELEE:
		faction->mp_melee_missions[TEST_MISSION_ID].award_level = award;
		break;
	default:
		faction->mp_combat_missions[TEST_MISSION_ID].award_level =
			award;
		break;
	}
}

/* One flight of a record type, keeping the pilot record the flights before it
 * left: the local player's team has the goal status (primary, prevent), the
 * score and the completion time; a melee places the player's team at placement
 * among three, ahead of the next team by gap. */
static void record_flight(int kind, int primary, int prevent, int score,
			  int seconds, int waves, int placement, int gap)
{
	g_keep_pilot_record = 1;
	if (record_is_melee(kind)) {
		melee_flight(3, record_humans(kind), placement, 6000, gap,
			     score);
		set_goals(0, primary, prevent);
	} else {
		award_flight(record_mission_type(kind), record_humans(kind),
			     GAME_DIFFICULTY_HARD, waves, primary, prevent,
			     score);
	}
	g_keep_pilot_record = 0;
	g_pilot_data.mission_sequence_active = record_in_sequence(kind);
	g_pilot_data
		.mission_description_ids[MISSION_DIRECTORY_TRAINING_EXERCISES] =
		record_in_sequence(kind) ? TEST_CAMPAIGN_MISSION_ID
					 : TEST_MISSION_ID;
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_MELEES] =
		TEST_MISSION_ID;
	g_pilot_data
		.mission_description_ids[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS] =
		TEST_MISSION_ID;
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_CAMPAIGNS] =
		TEST_CAMPAIGN_ID;
	g_flight_mission_state.runtime.team_mission_completion_time_seconds[0] =
		(unsigned)seconds;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
}

/* One flight of a record's history and what the record then holds: the award
 * level, and the faction's six award counts. */
struct record_step {
	int primary;
	int prevent;
	int score;
	int seconds;
	int waves;
	int flown;
	int completed;
	int failed;
	int best_score;
	int best_time;
	int award;
	int counts[6];
};

static void check_record_steps(int kind, const struct record_step *steps,
			       size_t count)
{
	g_keep_pilot_record = 0;
	fresh_flight();
	for (size_t i = 0; i < count; ++i) {
		const struct record_step *step = &steps[i];
		record_flight(kind, step->primary, step->prevent, step->score,
			      step->seconds, step->waves, 1, 100);
		struct record_view view;
		record_load(kind, &view);
		XVT_ASSERT_INT_EQ(view.flown, step->flown);
		XVT_ASSERT_INT_EQ(view.completed, step->completed);
		XVT_ASSERT_INT_EQ(view.failed, step->failed);
		XVT_ASSERT_INT_EQ(view.best_score, step->best_score);
		XVT_ASSERT_INT_EQ(view.best_time, step->best_time);
		XVT_ASSERT_INT_EQ(view.award, step->award);
		for (int slot = 0; slot < 6; ++slot) {
			XVT_ASSERT_INT_EQ(record_count(kind, slot),
					  step->counts[slot]);
		}
	}
	struct record_view view;
	if (record_in_sequence(kind)) {
		record_load(kind, &view);
		XVT_ASSERT_INT_EQ(view.campaign, TEST_CAMPAIGN_ID);
		XVT_ASSERT_INT_EQ(view.eligible, 1);
	}
	/* A better award takes the failed award (6) off the record's count. */
	record_set_award(kind, TEST_FAILED_AWARD);
	record_set_count(kind, TEST_FAILED_AWARD - 1, 1);
	record_flight(kind, 1, 0, 60000, 90, CRAFT_WAVES_DEFAULT, 1, 100);
	record_load(kind, &view);
	XVT_ASSERT_INT_EQ(view.award, 1);
	XVT_ASSERT_INT_EQ(record_count(kind, TEST_FAILED_AWARD - 1), 0);
	/* A flight with no award takes a failed award (6) off the record and
	 * its count. */
	record_set_award(kind, TEST_FAILED_AWARD);
	record_set_count(kind, TEST_FAILED_AWARD - 1, 1);
	record_flight(kind, 0, 0, 5, 0, CRAFT_WAVES_DEFAULT, 1, 100);
	record_load(kind, &view);
	XVT_ASSERT_INT_EQ(view.award, 0);
	XVT_ASSERT_INT_EQ(record_count(kind, TEST_FAILED_AWARD - 1), 0);
}

/* A single-player training mission's record counts each flight, those with the
 * primary goal met (1) as completed and those with it failed (2) or the
 * prevent goal set as failed; keeps the highest score and the shortest
 * nonzero time, ignoring flights with unlimited waves; and replaces the award
 * level only with a lower number (a better award), moving one count from the
 * old level to the new. */
static void check_sp_training_record(void)
{
	static const struct record_step steps[] = {
		{1, 0, 3000, 90, 1, 1, 1, 0, 3000, 90, 3, {0, 0, 1, 0, 0, 0}},
		{2, 0, 5000, 120, 1, 2, 1, 1, 5000, 90, 3, {0, 0, 1, 0, 0, 0}},
		{1, 0, 60000, 0, 1, 3, 2, 1, 60000, 90, 1, {1, 0, 0, 0, 0, 0}},
		{1, 0, 100, 80, 1, 4, 3, 1, 60000, 80, 1, {1, 0, 0, 0, 0, 0}},
		{1,
		 0,
		 999999,
		 10,
		 2,
		 5,
		 4,
		 1,
		 60000,
		 80,
		 1,
		 {1, 0, 0, 0, 0, 0}},
		{0, 0, -5, 0, 1, 6, 4, 1, 60000, 80, 1, {1, 0, 0, 0, 0, 0}},
	};
	check_record_steps(REC_SP_TRAINING, steps,
			   sizeof steps / sizeof steps[0]);
}

/* A single-player combat mission's record follows the same rules; a failed
 * primary goal also wins the failed award, which never replaces a better
 * one. */
static void check_sp_combat_record(void)
{
	static const struct record_step steps[] = {
		{1, 0, 3000, 90, 1, 1, 1, 0, 3000, 90, 3, {0, 0, 1, 0, 0, 0}},
		{2, 0, 5000, 120, 1, 2, 1, 1, 5000, 90, 3, {0, 0, 1, 0, 0, 0}},
		{1, 0, 60000, 0, 1, 3, 2, 1, 60000, 90, 1, {1, 0, 0, 0, 0, 0}},
		{1, 0, 100, 80, 1, 4, 3, 1, 60000, 80, 1, {1, 0, 0, 0, 0, 0}},
		{1,
		 0,
		 999999,
		 10,
		 2,
		 5,
		 4,
		 1,
		 60000,
		 80,
		 1,
		 {1, 0, 0, 0, 0, 0}},
	};
	check_record_steps(REC_SP_COMBAT, steps,
			   sizeof steps / sizeof steps[0]);
}

/* In multiplayer a training mission's record keeps the same figures, but every
 * award other than the failed one a flight wins adds one to the faction's
 * count for its level, whether or not it replaces the record's award; the
 * record's better award stays and its old count is not taken off. */
static void check_mp_training_record(void)
{
	static const struct record_step steps[] = {
		{1, 0, 3000, 90, 1, 1, 1, 0, 3000, 90, 3, {0, 0, 1, 0, 0, 0}},
		{2, 0, 5000, 120, 1, 2, 1, 1, 5000, 90, 3, {0, 0, 1, 0, 0, 0}},
		{1, 0, 60000, 0, 1, 3, 2, 1, 60000, 90, 1, {1, 0, 1, 0, 0, 0}},
		{1, 0, 100, 80, 1, 4, 3, 1, 60000, 80, 1, {1, 0, 2, 0, 0, 0}},
		{1,
		 0,
		 999999,
		 10,
		 2,
		 5,
		 4,
		 1,
		 60000,
		 80,
		 1,
		 {1, 0, 2, 0, 1, 0}},
		{0, 0, -5, 0, 1, 6, 4, 1, 60000, 80, 1, {1, 0, 2, 0, 1, 0}},
	};
	check_record_steps(REC_MP_TRAINING, steps,
			   sizeof steps / sizeof steps[0]);
}

/* A multiplayer combat mission's award follows the score steps of 10,000;
 * the failed award neither replaces a better one nor is counted. */
static void check_mp_combat_record(void)
{
	static const struct record_step steps[] = {
		{1, 0, 30000, 90, 1, 1, 1, 0, 30000, 90, 3, {0, 0, 1, 0, 0, 0}},
		{2, 0, 5000, 120, 1, 2, 1, 1, 30000, 90, 3, {0, 0, 1, 0, 0, 0}},
		{1, 0, 60000, 0, 1, 3, 2, 1, 60000, 90, 1, {1, 0, 1, 0, 0, 0}},
		{1, 0, 15000, 80, 1, 4, 3, 1, 60000, 80, 1, {1, 0, 1, 0, 1, 0}},
		{1,
		 0,
		 999999,
		 10,
		 2,
		 5,
		 4,
		 1,
		 60000,
		 80,
		 1,
		 {1, 0, 1, 0, 2, 0}},
	};
	check_record_steps(REC_MP_COMBAT, steps,
			   sizeof steps / sizeof steps[0]);
}

/* A training mission flown in a campaign is kept by its campaign mission
 * entry (the mission number less 1): flights counted, the campaign it was
 * last flown in, completed once, the highest and the shortest figures, and
 * the award, which in a sequence is the failed award for unlimited waves.
 * Single-player moves a count from the old level to the new; multiplayer adds
 * one for every award but the failed one. */
static void check_campaign_mission_record(void)
{
	static const struct record_step sp_steps[] = {
		{1, 0, 3000, 90, 1, 1, 1, 0, 3000, 90, 3, {0, 0, 1, 0, 0, 0}},
		{2, 0, 5000, 120, 1, 2, 1, 0, 5000, 90, 3, {0, 0, 1, 0, 0, 0}},
		{1, 0, 60000, 0, 1, 3, 1, 0, 60000, 90, 1, {1, 0, 0, 0, 0, 0}},
		{1, 0, 100, 80, 1, 4, 1, 0, 60000, 80, 1, {1, 0, 0, 0, 0, 0}},
		{1,
		 0,
		 999999,
		 10,
		 2,
		 5,
		 1,
		 0,
		 60000,
		 80,
		 1,
		 {1, 0, 0, 0, 0, 0}},
	};
	static const struct record_step mp_steps[] = {
		{1, 0, 3000, 90, 1, 1, 1, 0, 3000, 90, 3, {0, 0, 1, 0, 0, 0}},
		{2, 0, 5000, 120, 1, 2, 1, 0, 5000, 90, 3, {0, 0, 1, 0, 0, 0}},
		{1, 0, 60000, 0, 1, 3, 1, 0, 60000, 90, 1, {1, 0, 1, 0, 0, 0}},
		{1, 0, 100, 80, 1, 4, 1, 0, 60000, 80, 1, {1, 0, 2, 0, 0, 0}},
		{1,
		 0,
		 999999,
		 10,
		 2,
		 5,
		 1,
		 0,
		 60000,
		 80,
		 1,
		 {1, 0, 2, 0, 0, 0}},
	};
	check_record_steps(REC_SP_CAMPAIGN, sp_steps,
			   sizeof sp_steps / sizeof sp_steps[0]);
	check_record_steps(REC_MP_CAMPAIGN, mp_steps,
			   sizeof mp_steps / sizeof mp_steps[0]);
}

/* A completed campaign mission is eligible for its award only when the game
 * difficulty setting is no higher than hard and the waves are not unlimited;
 * a failed mission is neither completed nor eligible. */
static void check_campaign_mission_eligible(void)
{
	static const struct {
		int primary;
		int config_difficulty;
		int waves;
		int want_completed;
		int want_eligible;
	} cases[] = {
		{1, 2, 1, 1, 1},
		{1, 3, 1, 1, 0},
		{1, 2, 2, 1, 0},
		{2, 2, 1, 0, 0},
	};
	for (int kind = REC_SP_CAMPAIGN; kind <= REC_MP_CAMPAIGN;
	     kind += REC_MP_CAMPAIGN - REC_SP_CAMPAIGN) {
		for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
			g_keep_pilot_record = 0;
			fresh_flight();
			g_game_config.difficulty =
				(game_difficulty)cases[i].config_difficulty;
			record_flight(kind, cases[i].primary, 0, 3000, 90,
				      cases[i].waves, 1, 100);
			struct record_view view;
			record_load(kind, &view);
			XVT_ASSERT_INT_EQ(view.completed,
					  cases[i].want_completed);
			XVT_ASSERT_INT_EQ(view.eligible,
					  cases[i].want_eligible);
		}
	}
	g_game_config.difficulty = GAME_DIFFICULTY_EASY;
}

/* One melee flight and the record after it. */
struct melee_step {
	int primary;
	int score;
	int seconds;
	int placement;
	int gap;
	int flown;
	int completed;
	int failed;
	int best_score;
	int best_time;
	int best_place;
	int margin;
	int award;
	int places
		[3]; /* Flights finished first, second, third (multiplayer). */
	int counts[6];
};

/* A melee mission's record counts flights, completions and failures, keeps
 * the best score, the shortest time, the best placement and, for a first
 * place, the biggest lead; in multiplayer it also counts first, second and
 * third places. Awards move plaque counts like the other records. */
static void check_melee_record(void)
{
	static const struct melee_step sp_steps[] = {
		{1,
		 3000,
		 90,
		 1,
		 100,
		 1,
		 1,
		 0,
		 3000,
		 90,
		 1,
		 100,
		 3,
		 {0, 0, 0},
		 {0, 0, 1, 0, 0, 0}},
		{2,
		 5000,
		 120,
		 2,
		 100,
		 2,
		 1,
		 1,
		 5000,
		 90,
		 1,
		 100,
		 3,
		 {0, 0, 0},
		 {0, 0, 1, 0, 0, 0}},
		{1,
		 7000,
		 0,
		 1,
		 5001,
		 3,
		 2,
		 1,
		 7000,
		 90,
		 1,
		 5001,
		 2,
		 {0, 0, 0},
		 {0, 1, 0, 0, 0, 0}},
		{1,
		 100,
		 80,
		 3,
		 100,
		 4,
		 3,
		 1,
		 7000,
		 80,
		 1,
		 5001,
		 2,
		 {0, 0, 0},
		 {0, 1, 0, 0, 0, 0}},
	};
	static const struct melee_step mp_steps[] = {
		{1,
		 6000,
		 90,
		 1,
		 100,
		 1,
		 1,
		 0,
		 6000,
		 90,
		 1,
		 100,
		 5,
		 {1, 0, 0},
		 {0, 0, 0, 0, 1, 0}},
		{2,
		 7000,
		 120,
		 2,
		 100,
		 2,
		 1,
		 1,
		 7000,
		 90,
		 1,
		 100,
		 5,
		 {1, 1, 0},
		 {0, 0, 0, 0, 1, 0}},
		{1,
		 8000,
		 80,
		 1,
		 5001,
		 3,
		 2,
		 1,
		 8000,
		 80,
		 1,
		 5001,
		 4,
		 {2, 1, 0},
		 {0, 0, 0, 1, 1, 0}},
		{0,
		 9000,
		 0,
		 3,
		 100,
		 4,
		 2,
		 1,
		 9000,
		 80,
		 1,
		 5001,
		 4,
		 {2, 1, 1},
		 {0, 0, 0, 1, 1, 0}},
	};
	for (int kind = REC_SP_MELEE; kind <= REC_MP_MELEE;
	     kind += REC_MP_MELEE - REC_SP_MELEE) {
		const struct melee_step *steps =
			kind == REC_SP_MELEE ? sp_steps : mp_steps;
		size_t count = kind == REC_SP_MELEE
				       ? sizeof sp_steps / sizeof sp_steps[0]
				       : sizeof mp_steps / sizeof mp_steps[0];
		g_keep_pilot_record = 0;
		fresh_flight();
		for (size_t i = 0; i < count; ++i) {
			const struct melee_step *step = &steps[i];
			record_flight(kind, step->primary, 0, step->score,
				      step->seconds, CRAFT_WAVES_DEFAULT,
				      step->placement, step->gap);
			struct record_view view;
			record_load(kind, &view);
			XVT_ASSERT_INT_EQ(view.flown, step->flown);
			XVT_ASSERT_INT_EQ(view.completed, step->completed);
			XVT_ASSERT_INT_EQ(view.failed, step->failed);
			XVT_ASSERT_INT_EQ(view.best_score, step->best_score);
			XVT_ASSERT_INT_EQ(view.best_time, step->best_time);
			XVT_ASSERT_INT_EQ(view.best_place, step->best_place);
			XVT_ASSERT_INT_EQ(view.margin, step->margin);
			XVT_ASSERT_INT_EQ(view.award, step->award);
			if (kind == REC_MP_MELEE) {
				XVT_ASSERT_INT_EQ(view.first, step->places[0]);
				XVT_ASSERT_INT_EQ(view.second, step->places[1]);
				XVT_ASSERT_INT_EQ(view.third, step->places[2]);
			}
			for (int slot = 0; slot < 6; ++slot) {
				XVT_ASSERT_INT_EQ(record_count(kind, slot),
						  step->counts[slot]);
			}
		}
		record_set_award(kind, TEST_FAILED_AWARD);
		record_set_count(kind, TEST_FAILED_AWARD - 1, 1);
		record_flight(kind, 1, 0, 6000, 0, CRAFT_WAVES_DEFAULT, 1, 100);
		struct record_view view;
		record_load(kind, &view);
		XVT_ASSERT_INT_EQ(view.award, kind == REC_SP_MELEE ? 3 : 5);
		XVT_ASSERT_INT_EQ(record_count(kind, TEST_FAILED_AWARD - 1), 0);
		record_set_award(kind, TEST_FAILED_AWARD);
		record_set_count(kind, TEST_FAILED_AWARD - 1, 1);
		record_flight(kind, 0, 0, 5, 0, CRAFT_WAVES_DEFAULT, 3, 100);
		record_load(kind, &view);
		XVT_ASSERT_INT_EQ(view.award, 0);
		XVT_ASSERT_INT_EQ(record_count(kind, TEST_FAILED_AWARD - 1), 0);
	}
}

/* ---- fe_disk_io_commit_flight_results: campaign standings -------------- */

/* One flight of campaign 2, a training mission at the given index of four, kept
 * on the pilot record the flights before it left. */
static void campaign_step(int index, int completed, int humans, int score)
{
	g_keep_pilot_record = 1;
	campaign_flight(index, completed);
	g_keep_pilot_record = 0;
	g_pilot_data.campaign_sequence_state.human_player_count = humans;
	if (humans == 2) {
		add_second_human();
	}
	g_players[0].mission_stats.mission_score = score;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
}

/* A campaign's record, in the single-player table for one human player and
 * the multiplayer table for more: the first mission counts an attempt; a
 * completed mission raises the next mission to its index plus 1 only when
 * that is higher, and the campaign score of the first mission is its own score
 * while a later mission's is added; the best score is the campaign score after
 * a completed mission, or the campaign score plus the mission's score after
 * a failed one, when higher. */
static void check_campaign_record_progress(void)
{
	static const struct {
		int index;
		int completed;
		int score;
		int attempts;
		int next;
		int best;
		int cumulative;
	} steps[] = {
		{0, 1, 700, 1, 1, 700, 700},	{2, 1, 300, 1, 3, 1000, 1000},
		{1, 1, 50, 1, 3, 1050, 1050},	{2, 0, 400, 1, 3, 1450, 1050},
		{2, 0, -100, 1, 3, 1450, 1050}, {0, 0, 10, 2, 3, 1450, 1050},
	};
	for (int humans = 1; humans <= 2; ++humans) {
		g_keep_pilot_record = 0;
		fresh_flight();
		for (size_t i = 0; i < sizeof steps / sizeof steps[0]; ++i) {
			campaign_step(steps[i].index, steps[i].completed,
				      humans, steps[i].score);
			const struct pilot_faction *faction =
				&g_pilot_data.faction_statistics[0];
			const struct pilot_campaign *kept =
				humans == 1 ? &faction->sp_campaigns[2]
					    : &faction->mp_campaigns[2];
			const struct pilot_campaign *other =
				humans == 1 ? &faction->mp_campaigns[2]
					    : &faction->sp_campaigns[2];
			XVT_ASSERT_INT_EQ(kept->attempt_count,
					  steps[i].attempts);
			XVT_ASSERT_INT_EQ(kept->next_mission_index,
					  steps[i].next);
			XVT_ASSERT_INT_EQ(kept->best_score, steps[i].best);
			XVT_ASSERT_INT_EQ(g_pilot_data.campaign_sequence_state
						  .cumulative_score,
					  steps[i].cumulative);
			XVT_ASSERT_INT_EQ(other->attempt_count, 0);
			XVT_ASSERT_INT_EQ(other->best_score, 0);
		}
	}
}

/* A campaign is marked finished, in the table its players use, by a completed
 * mission that leaves the mission index equal to the mission count (here 4); a
 * mission before it, or a failed one, does not. */
static void check_campaign_finished_at_count(void)
{
	static const struct {
		int index;
		int completed;
		int want;
	} cases[] = {{4, 1, 1}, {4, 0, 0}, {3, 1, 0}, {5, 1, 0}};
	for (int humans = 1; humans <= 2; ++humans) {
		for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
			g_keep_pilot_record = 0;
			fresh_flight();
			campaign_step(cases[i].index, cases[i].completed,
				      humans, 100);
			const struct pilot_faction *faction =
				&g_pilot_data.faction_statistics[0];
			XVT_ASSERT_INT_EQ(faction->sp_campaigns[2].is_finished,
					  humans == 1 ? cases[i].want : 0);
			XVT_ASSERT_INT_EQ(faction->mp_campaigns[2].is_finished,
					  humans == 2 ? cases[i].want : 0);
		}
	}
}

/* ---- fe_disk_io_commit_flight_results: tournament standings ------------ */

enum {
	TEST_TOURNAMENT_ID = 4,
	TEST_BATTLE_ID = 5,
};

/* A melee flight inside tournament 4 of three missions at the given index:
 * teams 0 to teams - 1 take part (the others' standings are marked inactive,
 * -1), with humans human players, kept on the pilot record the flights before
 * it left. */
static void tournament_flight(int teams, int humans, int index)
{
	g_keep_pilot_record = 1;
	fresh_flight();
	g_keep_pilot_record = 0;
	g_mission_header.mission_type = MISSION_TYPE_MELEE;
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_TOURNAMENTS] =
		TEST_TOURNAMENT_ID;
	struct melee_tournament_sequence_state *state =
		&g_pilot_data.melee_tournament_sequence_state;
	state->participating_team_count = teams;
	state->human_player_count = (unsigned)humans;
	state->mission_count = 3;
	state->current_mission_index = index;
	for (int team = 0; team < 10; ++team) {
		state->team_standings[team]
			.ai_opponent_source_team_and_type_flag =
			team < teams ? 0 : -1;
	}
}

/* After each melee of a tournament, every active team's standing adds the
 * team's mission score (bonus plus mission) to its total and counts a first,
 * second or third place by how many active teams scored more (teams with the
 * same score share a place); an inactive team gets no place however much it
 * scored. */
static void check_tournament_standings(void)
{
	static const int scores[2][3] = {{1000, 3000, 3000}, {4000, 3500, 100}};
	g_keep_pilot_record = 0;
	fresh_flight();
	for (int round = 0; round < 2; ++round) {
		tournament_flight(3, 1, round);
		for (int team = 0; team < 3; ++team) {
			g_flight_mission_state.runtime
				.team_scores[TEAM_SCORE_MISSION][team] =
				scores[round][team];
		}
		g_flight_mission_state.runtime
			.team_scores[TEAM_SCORE_BONUS][0] =
			round == 0 ? 500 : 0;
		g_flight_mission_state.runtime
			.team_scores[TEAM_SCORE_BONUS][3] = 99999;
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	}
	const struct melee_tournament_team_standings *standings =
		g_pilot_data.melee_tournament_sequence_state.team_standings;
	static const int totals[3] = {5500, 6500, 3100};
	static const int places[3][3] = {{1, 0, 1}, {1, 1, 0}, {1, 0, 1}};
	for (int team = 0; team < 3; ++team) {
		XVT_ASSERT_INT_EQ(standings[team].total_score, totals[team]);
		XVT_ASSERT_INT_EQ(standings[team].first_place_count,
				  places[team][0]);
		XVT_ASSERT_INT_EQ(standings[team].second_place_count,
				  places[team][1]);
		XVT_ASSERT_INT_EQ(standings[team].third_place_count,
				  places[team][2]);
	}
	XVT_ASSERT_INT_EQ(standings[3].first_place_count, 0);
	XVT_ASSERT_INT_EQ(standings[3].second_place_count, 0);
	XVT_ASSERT_INT_EQ(standings[3].third_place_count, 0);
}

/* The last mission of a tournament, with the standings before it set so that
 * team 0 has 6000 and the placement given: each team below placement has 100
 * more, the others gap less. Returns the tournament award the commit wrote. */
static int tournament_award_of(int teams, int humans, int difficulty,
			       int placement, int gap, int local_total)
{
	tournament_flight(teams, humans, 2);
	g_flight_mission_state.difficulty = (game_difficulty)difficulty;
	struct melee_tournament_team_standings *standings =
		g_pilot_data.melee_tournament_sequence_state.team_standings;
	standings[0].total_score = local_total;
	for (int team = 1; team < teams; ++team) {
		standings[team].total_score = team < placement
						      ? local_total + 100
						      : local_total - gap;
	}
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	return g_pilot_data.faction_statistics[0]
		.mission_awards[TEST_AWARD_TOURNAMENT];
}

/* The tournament award, from the local team's total score and placement among
 * the teams taking part, comes from g_placement_award_levels when the total is
 * over 5,000 and the placement 1 to 3; with fewer than 3 human players it is
 * raised by 2 below hard difficulty and 1 at hard, with 3 or 4 by 1 below hard,
 * and never past 5; a first place lowered by 1 for a lead over 10,000 and 2
 * over 20,000 but not below 1; last place among four or more teams, with no
 * award otherwise, is the failed award. */
static void check_tournament_award(void)
{
	static const struct {
		int teams;
		int humans;
		int difficulty;
		int placement;
		int gap;
		int total;
		int want;
	} cases[] = {
		{4, 1, 2, 1, 100, 6000, 5},   {4, 1, 0, 1, 100, 6000, 5},
		{4, 1, 2, 2, 100, 6000, 5},   {4, 1, 2, 4, 100, 6000, 6},
		{5, 1, 2, 3, 100, 6000, 0},   {6, 1, 2, 1, 100, 6000, 3},
		{6, 1, 0, 1, 100, 6000, 4},   {6, 1, 2, 1, 10001, 6000, 2},
		{6, 1, 2, 1, 10000, 6000, 3}, {6, 1, 2, 1, 20001, 6000, 1},
		{7, 3, 2, 1, 100, 6000, 1},   {7, 3, 0, 2, 100, 6000, 3},
		{7, 5, 0, 2, 100, 6000, 2},   {8, 2, 0, 3, 100, 6000, 5},
		{8, 1, 0, 1, 20001, 6000, 1}, {8, 1, 2, 1, 20001, 6000, 1},
		{3, 1, 2, 3, 100, 6000, 0},   {8, 5, 2, 8, 100, 6000, 6},
		{4, 3, 0, 2, 100, 6000, 5},   {6, 1, 2, 1, 100, 5000, 0},
		{2, 1, 2, 1, 100, 6000, 5},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		g_keep_pilot_record = 0;
		fresh_flight();
		XVT_ASSERT_INT_EQ(
			tournament_award_of(cases[i].teams, cases[i].humans,
					    cases[i].difficulty,
					    cases[i].placement, cases[i].gap,
					    cases[i].total),
			cases[i].want);
	}
}

/* One tournament flight's standings before it (the local team's total, the
 * totals of the two other teams) and what the record then holds. */
struct tournament_step {
	int index;
	int totals[3];
	int attempts;
	int completed;
	int places[3];
	int best_score;
	int best_place;
	int margin;
	int award;
	int trophies[6];
};

/* A tournament's record, in the single-player table for one human player and
 * the multiplayer table for more: the first mission counts an attempt and
 * nothing else; the last counts a completion and the overall place, keeps the
 * best total, the best place, the biggest lead of a first place and the best
 * award. Single-player moves a trophy from the old award to a better one;
 * multiplayer adds one for every award and keeps the old. */
static void check_tournament_record(void)
{
	static const struct tournament_step sp_steps[] = {
		{0,
		 {6000, 6100, 5000},
		 1,
		 0,
		 {0, 0, 0},
		 0,
		 0,
		 0,
		 0,
		 {0, 0, 0, 0, 0, 0}},
		{2,
		 {7000, 7100, 6000},
		 1,
		 1,
		 {0, 1, 0},
		 7000,
		 2,
		 0,
		 0,
		 {0, 0, 0, 0, 0, 0}},
		{2,
		 {8000, 7000, 3000},
		 1,
		 2,
		 {1, 1, 0},
		 8000,
		 1,
		 1000,
		 5,
		 {0, 0, 0, 0, 1, 0}},
		{2,
		 {30000, 3000, 2000},
		 1,
		 3,
		 {2, 1, 0},
		 30000,
		 1,
		 27000,
		 3,
		 {0, 0, 1, 0, 0, 0}},
	};
	static const struct tournament_step mp_steps[] = {
		{0,
		 {6000, 6100, 5000},
		 1,
		 0,
		 {0, 0, 0},
		 0,
		 0,
		 0,
		 0,
		 {0, 0, 0, 0, 0, 0}},
		{2,
		 {7000, 7100, 6000},
		 1,
		 1,
		 {0, 1, 0},
		 7000,
		 2,
		 0,
		 0,
		 {0, 0, 0, 0, 0, 0}},
		{2,
		 {8000, 7000, 3000},
		 1,
		 2,
		 {1, 1, 0},
		 8000,
		 1,
		 1000,
		 5,
		 {0, 0, 0, 0, 1, 0}},
		{2,
		 {30000, 3000, 2000},
		 1,
		 3,
		 {2, 1, 0},
		 30000,
		 1,
		 27000,
		 3,
		 {0, 0, 1, 0, 1, 0}},
	};
	for (int humans = 1; humans <= 2; ++humans) {
		const struct tournament_step *steps =
			humans == 1 ? sp_steps : mp_steps;
		g_keep_pilot_record = 0;
		fresh_flight();
		for (int i = 0; i < 4; ++i) {
			tournament_flight(3, humans, steps[i].index);
			struct melee_tournament_team_standings *standings =
				g_pilot_data.melee_tournament_sequence_state
					.team_standings;
			for (int team = 0; team < 3; ++team) {
				standings[team].total_score =
					steps[i].totals[team];
			}
			XVT_ASSERT_INT_EQ(
				fe_disk_io_commit_flight_results(0, 0), 0);
			const struct pilot_faction *faction =
				&g_pilot_data.faction_statistics[0];
			struct pilot_tournament sp;
			struct pilot_multiplayer_tournament mp;
			int attempts;
			int completed;
			int places[3];
			int best_score;
			int best_place;
			int margin;
			int award;
			if (humans == 1) {
				sp = faction->sp_tournaments
					     [TEST_TOURNAMENT_ID];
				attempts = sp.attempt_count;
				completed = sp.completed_count;
				places[0] = sp.first_place_count;
				places[1] = sp.second_place_count;
				places[2] = sp.third_place_count;
				best_score = sp.best_score;
				best_place = sp.best_placement;
				margin = (int)sp.best_margin;
				award = sp.award_level;
			} else {
				mp = faction->mp_tournaments
					     [TEST_TOURNAMENT_ID];
				attempts = mp.attempt_count;
				completed = mp.completed_count;
				places[0] = mp.first_place_count;
				places[1] = mp.second_place_count;
				places[2] = mp.third_place_count;
				best_score = mp.best_score;
				best_place = mp.best_placement;
				margin = (int)mp.best_margin;
				award = mp.award_level;
			}
			XVT_ASSERT_INT_EQ(attempts, steps[i].attempts);
			XVT_ASSERT_INT_EQ(completed, steps[i].completed);
			for (int place = 0; place < 3; ++place) {
				XVT_ASSERT_INT_EQ(places[place],
						  steps[i].places[place]);
			}
			XVT_ASSERT_INT_EQ(best_score, steps[i].best_score);
			XVT_ASSERT_INT_EQ(best_place, steps[i].best_place);
			XVT_ASSERT_INT_EQ(margin, steps[i].margin);
			XVT_ASSERT_INT_EQ(award, steps[i].award);
			for (int slot = 0; slot < 6; ++slot) {
				XVT_ASSERT_INT_EQ(
					faction->tournament_trophies[slot],
					steps[i].trophies[slot]);
			}
			XVT_ASSERT_INT_EQ(
				humans == 1 ? faction->mp_tournaments
						      [TEST_TOURNAMENT_ID]
							      .attempt_count
					    : faction->sp_tournaments
						      [TEST_TOURNAMENT_ID]
							      .attempt_count,
				0);
		}
		/* A better award takes a failed award off its count. */
		g_pilot_data.faction_statistics[0].tournament_trophies[5] = 1;
		if (humans == 1) {
			g_pilot_data.faction_statistics[0]
				.sp_tournaments[TEST_TOURNAMENT_ID]
				.award_level = TEST_FAILED_AWARD;
		} else {
			g_pilot_data.faction_statistics[0]
				.mp_tournaments[TEST_TOURNAMENT_ID]
				.award_level = TEST_FAILED_AWARD;
		}
		tournament_flight(3, humans, 2);
		g_pilot_data.melee_tournament_sequence_state.team_standings[0]
			.total_score = 8000;
		g_pilot_data.melee_tournament_sequence_state.team_standings[1]
			.total_score = 7000;
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
		XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[0]
					  .tournament_trophies[5],
				  0);
		/* A flight with no award clears a failed award and its
		 * count. */
		g_pilot_data.faction_statistics[0].tournament_trophies[5] = 1;
		if (humans == 1) {
			g_pilot_data.faction_statistics[0]
				.sp_tournaments[TEST_TOURNAMENT_ID]
				.award_level = TEST_FAILED_AWARD;
		} else {
			g_pilot_data.faction_statistics[0]
				.mp_tournaments[TEST_TOURNAMENT_ID]
				.award_level = TEST_FAILED_AWARD;
		}
		tournament_flight(3, humans, 2);
		g_pilot_data.melee_tournament_sequence_state.team_standings[0]
			.total_score = 7000;
		g_pilot_data.melee_tournament_sequence_state.team_standings[1]
			.total_score = 7100;
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
		XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[0]
					  .tournament_trophies[5],
				  0);
		XVT_ASSERT_INT_EQ(
			humans == 1
				? g_pilot_data.faction_statistics[0]
					  .sp_tournaments[TEST_TOURNAMENT_ID]
					  .award_level
				: g_pilot_data.faction_statistics[0]
					  .mp_tournaments[TEST_TOURNAMENT_ID]
					  .award_level,
			0);
	}
}

/* ---- fe_disk_io_commit_flight_results: battle standings ---------------- */

/* A flight of battle 5 at the given step, which needs the given wins: team 0
 * (Imperial) wins when winner is 0, team 1 (Rebel) when 1, nobody when -1.
 * The pilot record the flights before it left is kept. */
static void battle_step(int step, int needed, int winner, int humans, int score)
{
	g_keep_pilot_record = 1;
	battle_flight((unsigned)step, needed);
	g_keep_pilot_record = 0;
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_BATTLES] =
		TEST_BATTLE_ID;
	g_pilot_data.battle_sequence_state.human_player_count =
		(unsigned)humans;
	if (winner >= 0) {
		set_goals(winner, 1, 0);
	} else {
		set_goals(1, 2, 0);
	}
	g_players[0].mission_stats.mission_score = score;
}

/* One flight of a battle needing two wins and what the battle's record holds
 * after it, for one human player (the single-player table) and for two (the
 * multiplayer table). */
struct battle_row {
	int step;
	int winner;
	int score;
	int attempts;
	int victories;
	int defeats;
	int best_score;
	int margin[2];
	int award[2];
	int flight_award;
	int medallions[2][6];
};

/* A battle's record counts an attempt at its first mission; after a flight at
 * which a side has the wins needed, a victory or defeat for the local player's
 * side (team 0 Imperial); keeps the best cumulative score, the biggest margin
 * of a won battle (doubled for one human player at hard difficulty) and the
 * best award. A victory's award is set from the margin; a defeat by 2 or more
 * is the failed award. Single-player moves a medallion from the old award to
 * a better one, multiplayer adds one for every award, the failed one too, and
 * keeps the old. */
static void check_battle_record(void)
{
	static const struct battle_row rows[] = {
		{0,
		 0,
		 1000,
		 1,
		 0,
		 0,
		 1000,
		 {0, 0},
		 {0, 0},
		 0,
		 {{0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}},
		{1,
		 1,
		 500,
		 1,
		 0,
		 0,
		 1500,
		 {0, 0},
		 {0, 0},
		 0,
		 {{0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}},
		{2,
		 0,
		 700,
		 1,
		 1,
		 0,
		 2200,
		 {2, 1},
		 {5, 5},
		 5,
		 {{0, 0, 0, 0, 1, 0}, {0, 0, 0, 0, 1, 0}}},
		{0,
		 1,
		 300,
		 2,
		 1,
		 0,
		 2200,
		 {2, 1},
		 {5, 5},
		 0,
		 {{0, 0, 0, 0, 1, 0}, {0, 0, 0, 0, 1, 0}}},
		{1,
		 1,
		 400,
		 2,
		 1,
		 1,
		 2200,
		 {2, 1},
		 {5, 5},
		 6,
		 {{0, 0, 0, 0, 1, 0}, {0, 0, 0, 0, 1, 1}}},
		{0,
		 0,
		 100,
		 3,
		 1,
		 1,
		 2200,
		 {2, 1},
		 {5, 5},
		 0,
		 {{0, 0, 0, 0, 1, 0}, {0, 0, 0, 0, 1, 1}}},
		{1,
		 0,
		 100,
		 3,
		 2,
		 1,
		 2200,
		 {4, 2},
		 {3, 5},
		 3,
		 {{0, 0, 1, 0, 0, 0}, {0, 0, 0, 0, 2, 1}}},
	};
	for (int humans = 1; humans <= 2; ++humans) {
		int table = humans - 1;
		g_keep_pilot_record = 0;
		fresh_flight();
		for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
			battle_step(rows[i].step, 2, rows[i].winner, humans,
				    rows[i].score);
			XVT_ASSERT_INT_EQ(
				fe_disk_io_commit_flight_results(0, 0), 0);
			const struct pilot_faction *faction =
				&g_pilot_data.faction_statistics[0];
			int attempts =
				humans == 1
					? faction->sp_battles[TEST_BATTLE_ID]
						  .attempt_count
					: faction->mp_battles[TEST_BATTLE_ID]
						  .attempt_count;
			int victories =
				humans == 1
					? faction->sp_battles[TEST_BATTLE_ID]
						  .victory_count
					: faction->mp_battles[TEST_BATTLE_ID]
						  .victory_count;
			int defeats =
				humans == 1
					? faction->sp_battles[TEST_BATTLE_ID]
						  .defeat_count
					: faction->mp_battles[TEST_BATTLE_ID]
						  .defeat_count;
			int best = humans == 1
					   ? faction->sp_battles[TEST_BATTLE_ID]
						     .best_score
					   : faction->mp_battles[TEST_BATTLE_ID]
						     .best_score;
			int margin =
				humans == 1
					? (int)faction
						  ->sp_battles[TEST_BATTLE_ID]
						  .best_victory_margin
					: (int)faction
						  ->mp_battles[TEST_BATTLE_ID]
						  .best_victory_margin;
			int award =
				humans == 1
					? faction->sp_battles[TEST_BATTLE_ID]
						  .award_level
					: faction->mp_battles[TEST_BATTLE_ID]
						  .award_level;
			XVT_ASSERT_INT_EQ(attempts, rows[i].attempts);
			XVT_ASSERT_INT_EQ(victories, rows[i].victories);
			XVT_ASSERT_INT_EQ(defeats, rows[i].defeats);
			XVT_ASSERT_INT_EQ(best, rows[i].best_score);
			XVT_ASSERT_INT_EQ(margin, rows[i].margin[table]);
			XVT_ASSERT_INT_EQ(award, rows[i].award[table]);
			XVT_ASSERT_INT_EQ(
				faction->mission_awards[TEST_AWARD_BATTLE],
				humans == 2 && i == 6 ? 5
						      : rows[i].flight_award);
			XVT_ASSERT_INT_EQ(
				faction->sp_battles[TEST_BATTLE_ID].draw_count,
				0);
			for (int slot = 0; slot < 6; ++slot) {
				XVT_ASSERT_INT_EQ(
					faction->battle_medallions[slot],
					rows[i].medallions[table][slot]);
			}
			XVT_ASSERT_INT_EQ(
				(humans == 1
					 ? faction->mp_battles[TEST_BATTLE_ID]
						   .attempt_count
					 : faction->sp_battles[TEST_BATTLE_ID]
						   .attempt_count),
				0);
		}
		/* A better award takes a failed award off its count. */
		battle_step(0, 2, 0, humans, 10);
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
		g_pilot_data.faction_statistics[0].battle_medallions[5] = 1;
		if (humans == 1) {
			g_pilot_data.faction_statistics[0]
				.sp_battles[TEST_BATTLE_ID]
				.award_level = TEST_FAILED_AWARD;
		} else {
			g_pilot_data.faction_statistics[0]
				.mp_battles[TEST_BATTLE_ID]
				.award_level = TEST_FAILED_AWARD;
		}
		battle_step(1, 2, 0, humans, 10);
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
		XVT_ASSERT_INT_EQ(
			g_pilot_data.faction_statistics[0].battle_medallions[5],
			0);
		XVT_ASSERT_INT_EQ(humans == 1
					  ? g_pilot_data.faction_statistics[0]
						    .sp_battles[TEST_BATTLE_ID]
						    .award_level
					  : g_pilot_data.faction_statistics[0]
						    .mp_battles[TEST_BATTLE_ID]
						    .award_level,
				  humans == 1 ? 3 : 5);
		/* A flight with no award takes a failed award off the
		 * record. */
		if (humans == 1) {
			g_pilot_data.faction_statistics[0]
				.sp_battles[TEST_BATTLE_ID]
				.award_level = TEST_FAILED_AWARD;
		} else {
			g_pilot_data.faction_statistics[0]
				.mp_battles[TEST_BATTLE_ID]
				.award_level = TEST_FAILED_AWARD;
		}
		g_pilot_data.faction_statistics[0].battle_medallions[5] = 1;
		battle_step(0, 2, -1, humans, 10);
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
		XVT_ASSERT_INT_EQ(
			g_pilot_data.faction_statistics[0].battle_medallions[5],
			0);
		XVT_ASSERT_INT_EQ(humans == 1
					  ? g_pilot_data.faction_statistics[0]
						    .sp_battles[TEST_BATTLE_ID]
						    .award_level
					  : g_pilot_data.faction_statistics[0]
						    .mp_battles[TEST_BATTLE_ID]
						    .award_level,
				  0);
	}
}

/* A battle's award for a victory comes from the winning margin (doubled with
 * one human player at hard difficulty) plus the balance of human players (the
 * enemy's count less the local side's, doubled when positive): 5 or more is
 * award 1, 4 award 2, 3 award 3, 2 award 4, less award 5. A defeat by a margin
 * of 2 or more is the failed award, a defeat by less and a battle with no
 * winner yet have none. */
static void check_battle_award(void)
{
	enum {
		I = BATTLE_MISSION_RESULT_IMPERIAL_VICTORY,
		R = BATTLE_MISSION_RESULT_REBEL_VICTORY,
		D = BATTLE_MISSION_RESULT_DRAW
	};
	static const struct {
		int before[5]; /* Results of the steps before, D after them. */
		int winner;
		int state_humans;
		int difficulty;
		int allied; /* Human players on team 0, local one included. */
		int enemy;
		int want;
	} cases[] = {
		{{I, I, D}, 0, 1, 2, 1, 0, 1},
		{{I, I, D}, 0, 1, 1, 1, 0, 4},
		{{I, I, D}, 0, 1, 0, 1, 0, 4},
		{{I, I, R, D}, 0, 1, 2, 1, 0, 3},
		{{I, I, R, R, D}, 0, 1, 2, 1, 0, 5},
		{{I, I, D}, 0, 2, 2, 1, 1, 3},
		{{I, I, R, D}, 0, 2, 2, 1, 1, 4},
		{{I, I, R, R, D}, 0, 2, 2, 1, 1, 5},
		{{I, I, R, D}, 0, 3, 2, 1, 2, 2},
		{{I, I, D}, 0, 2, 2, 2, 0, 5},
		{{I, I, D}, 0, 3, 2, 1, 2, 1},
		{{R, R, D}, 1, 1, 2, 1, 0, 6},
		{{R, R, I, I, D}, 1, 1, 2, 1, 0, 0},
		{{I, R, D}, -1, 1, 2, 1, 0, 0},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		g_keep_pilot_record = 0;
		int step = 0;
		while (cases[i].before[step] != D) {
			++step;
		}
		battle_step(step, 3, cases[i].winner, cases[i].state_humans,
			    100);
		g_flight_mission_state.difficulty =
			(game_difficulty)cases[i].difficulty;
		for (int before = 0; before < step; ++before) {
			g_pilot_data.battle_sequence_state
				.mission_results[before] =
				(battle_mission_result)cases[i].before[before];
		}
		for (int extra = 0; extra < cases[i].allied - 1; ++extra) {
			g_players[2 + extra].network.direct_play_id =
				300 + extra;
			g_players[2 + extra].team = 0;
		}
		for (int extra = 0; extra < cases[i].enemy; ++extra) {
			g_players[5 + extra].network.direct_play_id =
				400 + extra;
			g_players[5 + extra].team = 1;
		}
		XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
		XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[0]
					  .mission_awards[TEST_AWARD_BATTLE],
				  cases[i].want);
	}
}

/* A mission that only a team past the first two met its goal in is a draw: the
 * battle keeps two sides, Imperial (team 0) and Rebel (team 1). */
static void check_battle_result_other_team(void)
{
	battle_flight(3, 4);
	g_pilot_data.battle_sequence_state.mission_results[3] =
		BATTLE_MISSION_RESULT_IMPERIAL_VICTORY;
	set_goals(1, 2, 0);
	set_goals(2, 1, 0);
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.battle_sequence_state.mission_results[3],
			  BATTLE_MISSION_RESULT_DRAW);
}

/* Known failure campaign_id_past_table, issue #168: the pilot record keeps 25
 * campaigns' history, and the commit indexes it by the campaign's number from
 * the mission list without a check. A solo campaign numbered 25 counts its
 * attempt in the entry after the single-player table, the multiplayer
 * table's first, and the sanitizer stops the program. */
static void check_campaign_id_25(void)
{
	campaign_flight(0, 1);
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_CAMPAIGNS] = 25;
	fe_disk_io_commit_flight_results(0, 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[0]
				  .mp_campaigns[0]
				  .attempt_count,
			  0);
}

/* Known failure craft_type_past_table, issue #187: the commit looks up each
 * flight group's craft type, a byte from the mission file, in the 96-entry
 * g_craft_type_to_object_type without a check. A group of craft type 96 reads
 * past the table, and the sanitizer stops the program. */
static void check_craft_type_96(void)
{
	fresh_flight();
	g_mission_flight_groups[1].fg.craft_type = 96;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
}

/* ---- fe_disk_io_commit_flight_results: flight groups past the count ---- */

/* The commit looks only at the first num_flight_groups flight groups: a group
 * after them, even one a player flies, takes no part in crediting a goal and
 * is not an opponent in a melee. */
static void check_groups_past_count_ignored(void)
{
	fresh_flight();
	g_mission_flight_groups[2].fg.craft_type = TEST_XWING;
	g_mission_flight_groups[2].fg.team = 1;
	g_mission_flight_groups[2].fg.player_number = 2;
	g_mission_flight_groups[2].player_owner_idx = 1;
	g_mission_fg_stats[2].has_arrived = 1;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.teams[1].is_mission_completed, 1);

	melee_flight(2, 1, 1, 6000, 100, 3000);
	g_mission_flight_groups[2].fg.craft_type = TEST_XWING;
	g_mission_flight_groups[2].fg.team = 2;
	g_mission_flight_groups[2].fg.player_number = 1;
	g_mission_flight_groups[2].player_owner_idx = 1;
	g_flight_mission_state.runtime.team_scores[TEAM_SCORE_MISSION][2] =
		9000;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[0]
				  .mission_awards[TEST_AWARD_MELEE],
			  3);
}

/* In a melee, a computer-flown team is an opponent only when the mission has
 * computer opponents enabled; a team with the same score as the player's is
 * not ahead of it; and the lead of a first place is taken over the closest
 * team behind, whichever order the teams come in. */
static void check_melee_opponents_and_leads(void)
{
	melee_flight(3, 1, 3, 6000, 100, 3000);
	g_flight_mission_state.ai_opponents_enabled = 0;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[0]
				  .mission_awards[TEST_AWARD_MELEE],
			  3);

	melee_flight(3, 1, 1, 6000, 0, 3000);
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[0]
				  .mission_awards[TEST_AWARD_MELEE],
			  3);

	melee_flight(3, 1, 1, 6000, 100, 3000);
	g_flight_mission_state.runtime.team_scores[TEAM_SCORE_MISSION][1] =
		-6000;
	g_flight_mission_state.runtime.team_scores[TEAM_SCORE_MISSION][2] =
		-1000;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[0]
				  .mission_awards[TEST_AWARD_MELEE],
			  2);
}

static struct xvt_test_assets g_assets;
static uint8_t g_file_bytes[65541];
static uint8_t g_read_bytes[sizeof g_file_bytes + 512];

/* Writes a file of size bytes into the asset folder, byte i holding i * 7
 * modulo 251. */
static void write_asset(const char *name, size_t size)
{
	for (size_t i = 0; i < size; ++i) {
		g_file_bytes[i] = (uint8_t)(i * 7 % 251);
	}
	xvt_test_write_file(g_assets.asset, name, g_file_bytes, size);
}

/* fe_disk_io_open_global_stream opens a file into g_stream, records the path
 * storage resolved in g_file_name and returns 1; for a missing file, when no
 * failure is required to be fatal, it returns 0 with g_stream NULL.
 * fe_disk_io_read_with_retry_prompt returns the count it was asked for and
 * clears g_file_read_abort_flag when the file holds that many items, and
 * fe_disk_io_close_global_stream returns 0 and sets g_stream to NULL. */
static void check_global_stream(void)
{
	xvt_test_open_assets(&g_assets);
	write_asset("ten.bin", 10);
	g_file_read_abort_flag = 1;
	XVT_ASSERT_INT_EQ(fe_disk_io_open_global_stream("ten.bin", "rb", 0, 0),
			  1);
	XVT_ASSERT_TRUE(g_stream != NULL);
	size_t name_length = strlen(g_file_name);
	XVT_ASSERT_TRUE(name_length >= strlen("ten.bin"));
	XVT_ASSERT_TRUE(strcmp(g_file_name + name_length - strlen("ten.bin"),
			       "ten.bin") == 0);
	memset(g_read_bytes, 0, sizeof g_read_bytes);
	XVT_ASSERT_INT_EQ(
		fe_disk_io_read_with_retry_prompt(g_read_bytes, 2, 5, g_stream),
		5);
	XVT_ASSERT_INT_EQ(g_file_read_abort_flag, 0);
	XVT_ASSERT_TRUE(memcmp(g_read_bytes, g_file_bytes, 10) == 0);
	XVT_ASSERT_INT_EQ(fe_disk_io_close_global_stream(0), 0);
	XVT_ASSERT_TRUE(g_stream == NULL);

	XVT_ASSERT_INT_EQ(
		fe_disk_io_open_global_stream("missing.bin", "rb", 0, 0), 0);
	XVT_ASSERT_TRUE(g_stream == NULL);
	xvt_test_close_assets(&g_assets);
}

/* fe_disk_io_read_all_bytes_or_fatal copies a whole file into dst, closes
 * g_stream, and returns the bytes read as a 16-bit count: 0, 512, 1300, and
 * for a file of 65,541 bytes, all copied, a count wrapped to 5. */
static void check_read_all_bytes(void)
{
	static const size_t sizes[4] = {0, 512, 1300, sizeof g_file_bytes};
	static const int counts[4] = {0, 512, 1300, 5};
	xvt_test_open_assets(&g_assets);
	for (int i = 0; i < 4; ++i) {
		write_asset("all.bin", sizes[i]);
		memset(g_read_bytes, 0xAA, sizeof g_read_bytes);
		XVT_ASSERT_INT_EQ(fe_disk_io_read_all_bytes_or_fatal(
					  "all.bin", g_read_bytes),
				  counts[i]);
		XVT_ASSERT_TRUE(memcmp(g_read_bytes, g_file_bytes, sizes[i]) ==
				0);
		XVT_ASSERT_INT_EQ(g_read_bytes[sizes[i]], 0xAA);
		XVT_ASSERT_TRUE(g_stream == NULL);
	}
	xvt_test_close_assets(&g_assets);
}

/* Known failure wave_bonus_entry_slot, issue #184: the local player, slot 0,
 * is the pilot record's network entry 3. Its team met its primary goal with
 * one round of two craft left, so slot 0 should score the 1600 bonus. The
 * code reads and pays slot 3, the entry's number, instead. */
static void check_wave_bonus_by_slot(void)
{
	fresh_flight();
	g_pilot_data.network_players[0].direct_play_id = 0;
	g_pilot_data.network_players[3].direct_play_id = TEST_LOCAL_ID;
	set_goals(0, 1, 0);
	g_mission_fg_stats[0].waves_remaining = 1;
	g_players[0].mission_stats.mission_score = 1000;
	fe_disk_io_commit_flight_results(0, 0);
	XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score, 2600);
	XVT_ASSERT_INT_EQ(g_players[3].mission_stats.mission_score, 0);
}

/* Known failure teams_8_and_9_results, issue #186: a mission's teams run
 * from 0 to 9 and the pilot record keeps ten team results, but the commit
 * writes only teams 0 to 7. Team 9 met its primary goal with a bonus of 500,
 * and its result still says not completed, score 0. The field's comment
 * already says "for teams 0 to 7". */
static void check_team_9_result(void)
{
	fresh_flight();
	set_goals(9, 1, 0);
	g_flight_mission_state.runtime.team_scores[TEAM_SCORE_BONUS][9] = 500;
	fe_disk_io_commit_flight_results(0, 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.teams[9].is_mission_completed, 1);
	XVT_ASSERT_INT_EQ(g_pilot_data.teams[9].mission_score, 500);
}

/* Known failure ai_level_past_rating_table, issue #187: the melee rating
 * table has an entry for AI levels 0 to 6, and a mission file's level is not
 * checked. A melee group of level 7 reads past the table, and the sanitizer
 * stops the program. */
static void check_ai_level_7_rating(void)
{
	fresh_flight();
	g_mission_header.mission_type = MISSION_TYPE_MELEE;
	g_mission_flight_groups[1].fg.group_ai = 7;
	XVT_ASSERT_INT_EQ(fe_disk_io_commit_flight_results(0, 0), 0);
}

/* Known failure battle_step_past_table, issue #185: a battle keeps ten
 * mission results, and a battle needing six wins can reach step 10. The
 * commit writes step 10's result past the results, over the first step's
 * mission ordinal. */
static void check_battle_step_10(void)
{
	battle_flight(10, 6);
	g_pilot_data.battle_sequence_state.mission_ordinals[0] = 7;
	set_goals(0, 1, 0);
	fe_disk_io_commit_flight_results(0, 0);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.battle_sequence_state.mission_ordinals[0], 7);
}

/* Known failure network_campaign_finished, issue #158: a network campaign
 * of two human players wins its last mission, index 3 of 4. The commit keeps
 * the campaign in the multiplayer records but never marks it finished there:
 * its test waits for an index equal to the mission count, which the index
 * never reaches when a mission is committed. The field's comment describes
 * that test. */
static void check_network_campaign_finished(void)
{
	campaign_flight(3, 1);
	add_second_human();
	g_pilot_data.campaign_sequence_state.human_player_count = 2;
	fe_disk_io_commit_flight_results(0, 0);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.faction_statistics[0].mp_campaigns[2].is_finished,
		1);
}

int main(int argc, char **argv)
{
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		static const struct {
			const char *name;
			void (*check)(void);
		} known_failures[] = {
			{"wave_bonus_entry_slot", check_wave_bonus_by_slot},
			{"teams_8_and_9_results", check_team_9_result},
			{"ai_level_past_rating_table", check_ai_level_7_rating},
			{"battle_step_past_table", check_battle_step_10},
			{"network_campaign_finished",
			 check_network_campaign_finished},
			{"campaign_id_past_table", check_campaign_id_25},
			{"craft_type_past_table", check_craft_type_96},
		};
		for (size_t i = 0;
		     i < sizeof known_failures / sizeof known_failures[0];
		     ++i) {
			if (strcmp(argv[2], known_failures[i].name) == 0) {
				known_failures[i].check();
			}
		}
		return 0;
	}
	check_simulator_not_recorded();
	check_local_score();
	check_team_results();
	check_goal_credited();
	check_melee_team_score();
	check_wave_bonus();
	check_wave_bonus_withheld();
	check_melee_flight_group_rating();
	check_campaign_completed();
	check_campaign_failed();
	check_network_campaign_attempt();
	check_battle_result_kept();
	check_goal_credited_by_flight_group();
	check_pilot_tallies();
	check_promotion_and_demotion();
	check_kills_by_player_and_group();
	check_network_player_totals();
	check_training_award();
	check_combat_award();
	check_melee_award_by_placement();
	check_melee_award_by_difficulty();
	check_sp_training_record();
	check_sp_combat_record();
	check_mp_training_record();
	check_mp_combat_record();
	check_campaign_mission_record();
	check_campaign_mission_eligible();
	check_melee_record();
	check_campaign_record_progress();
	check_campaign_finished_at_count();
	check_tournament_standings();
	check_tournament_award();
	check_tournament_record();
	check_battle_record();
	check_battle_award();
	check_battle_result_other_team();
	check_groups_past_count_ignored();
	check_melee_opponents_and_leads();
	check_global_stream();
	check_read_all_bytes();
	return 0;
}
