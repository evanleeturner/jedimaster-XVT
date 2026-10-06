/* Tests for xvt/flight/fediskio.c: fe_disk_io_commit_flight_results, which
 * records a finished flight into the pilot record, and the file helpers. Each
 * commit check sets the flight it needs in the game's own tables (the mission
 * header, two flight groups, the players, the goal statuses and scores) and a
 * cleared pilot record, then reads what the commit wrote. The file checks
 * read files this file writes into a temporary asset folder. No game data is
 * read.
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

/* A combat mission of two flight groups of X-wings: group 0 on team 0 is the
 * local player's, slot 0, two craft a round, one round, no rounds left;
 * group 1 on team 1 is flown by the computer. The local player is the only
 * human, and the pilot record's network entry 0 is that player. Difficulty
 * is hard, the wave setting the default, and no goal has been met. Every
 * mission and sequence the pilot record names is number 1. */
static void fresh_flight(void)
{
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
	check_global_stream();
	check_read_all_bytes();
	return 0;
}
