/* Tests for xvt/frontend/mission_debrief.c, the debriefing shown after a
 * mission. The standings checks set network players, flight groups and team
 * results in the game's own tables and read back the lists
 * mission_debrief_prepare works out. The outcome text checks write mission
 * files into a temporary asset folder. The page checks draw on a frontend
 * display with no window (test_frontend_display.h), where text and sprites
 * draw nothing, so they check what the pages return; the first-frame checks
 * run the debriefing's first frame there, on mission lists in the asset
 * folder. No game data is read.
 *
 * POSIX only, for the temporary folder (test_asset_folder.h). */
#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "test_asset_folder.h"
#include "test_frontend_display.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_debrief.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/runtime/campaign_task.h"

enum {
	TEST_LOCAL_PLAYER = 0x41,
	TEST_MISSION_ID = 4,
};

static struct xvt_test_assets g_assets;

/* A cleared pilot record, mission and roster, in the given session mode and
 * mission directory, with AI opponents off and no team counts. */
static void fresh_world(int session_mode, int directory)
{
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	memset(&g_frontend_mission, 0, sizeof g_frontend_mission);
	memset(g_front_state.net_players, 0, sizeof g_front_state.net_players);
	g_front_state.net_player_count = 0;
	memset(g_team_player_flight_group_count, 0,
	       sizeof g_team_player_flight_group_count);
	g_team_count = 0;
	g_game_config.ai_opponents = 0;
	g_frontend_mission_session_mode = session_mode;
	g_pilot_data.mission_directory_id = directory;
}

/* Puts network player slot in flight group fg with a DirectPlay id. */
static void add_player(int slot, int id, int fg)
{
	g_pilot_data.network_players[slot].direct_play_id = id;
	g_pilot_data.network_players[slot].flight_group_id = fg;
}

static void check_list(const int *actual, const int *expected, int count)
{
	for (int i = 0; i < count; ++i) {
		XVT_ASSERT_INT_EQ(actual[i], expected[i]);
	}
}

/* A network combat engagement: player slot 0 flies for team 0, slots 2 and 5
 * for team 2, slot 1 is empty. Team 0 scored more, but only team 2 completed
 * the mission. */
static void engagement_world(void)
{
	fresh_world(FRONTEND_MISSION_SESSION_NET_HOST,
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	g_frontend_mission.flight_group_count = 4;
	g_frontend_mission.flight_groups[1].team = 1;
	g_frontend_mission.flight_groups[2].team = 2;
	g_frontend_mission.flight_groups[3].team = 2;
	add_player(0, TEST_LOCAL_PLAYER, 0);
	add_player(2, 0x43, 2);
	add_player(5, 0x46, 3);
	g_pilot_data.teams[0].mission_score = 500;
	g_pilot_data.teams[2].mission_score = 100;
	g_pilot_data.teams[2].is_mission_completed = 1;
	g_pilot_data.network_players[0].total_score = 900;
	g_pilot_data.network_players[2].total_score = 200;
	g_pilot_data.network_players[5].total_score = 300;
	g_pilot_data.kills_full_on_player[0] = 1;
	g_pilot_data.kills_full_on_player[2] = 3;
	g_pilot_data.kills_full_on_player[5] = 2;
	g_pilot_data.kills_full_from_player[0] = 2;
	g_pilot_data.kills_full_from_player[5] = 5;
	g_pilot_data.team = 2;
}

/* Outside melees and tournaments the teams with a network player are
 * counted and sorted with a completed mission ahead of a higher score;
 * players are sorted the same way by their team's completion, then their own
 * score; the "killed" and "killed by" lists follow the full kills on and from
 * each player, highest first; the tournament standings stay empty. Each list
 * ends with -1. */
static void check_prepare_engagement(void)
{
	engagement_world();
	XVT_ASSERT_INT_EQ(mission_debrief_prepare(), 1);
	static const int has_player[10] = {1, 0, 1, 0, 0, 0, 0, 0, 0, 0};
	check_list(g_debrief_team_has_player, has_player, 10);
	XVT_ASSERT_INT_EQ(g_debrief_active_team_count, 2);
	static const int teams[10] = {2, 0, -1, -1, -1, -1, -1, -1, -1, -1};
	check_list(g_debrief_sorted_team_ids, teams, 10);
	static const int players[8] = {5, 2, 0, -1, -1, -1, -1, -1};
	check_list(g_debrief_sorted_player_ids, players, 8);
	static const int killed[8] = {2, 5, 0, -1, -1, -1, -1, -1};
	check_list(g_debrief_kills_on_combatant_ids, killed, 8);
	static const int killed_by[8] = {5, 0, 2, -1, -1, -1, -1, -1};
	check_list(g_debrief_kills_from_combatant_ids, killed_by, 8);
	static const int none[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
	check_list(g_debrief_standings_team_ids, none, 10);
	static const int zeros[10] = {0};
	check_list(g_debrief_team_in_standings, zeros, 10);
	check_list(g_debrief_team_has_only_ai_pilots, zeros, 10);
	XVT_ASSERT_INT_EQ(g_debrief_local_team_rank_index, 0);
	XVT_ASSERT_INT_EQ(g_debrief_rank_by_pilot, 0);
}

/* A team moves ahead only of a lower score, a player only of a lower score
 * and a combatant only of fewer kills, so of two equal entries the one
 * already placed stays ahead. Team 1, which did not complete the mission
 * either, ties team 0's score: team 2, inserted last, pushes team 0 back, and
 * team 0 stays behind team 1. Players 2 and 5 tie on score and on kills on and
 * from the local player; player 6, on team 1, has none. */
static void check_prepare_ties(void)
{
	engagement_world();
	add_player(6, 0x47, 1);
	g_pilot_data.teams[1].mission_score = 500;
	g_pilot_data.network_players[5].total_score = 200;
	g_pilot_data.kills_full_on_player[5] = 3;
	g_pilot_data.kills_full_from_player[2] = 5;
	mission_debrief_prepare();
	static const int teams[4] = {2, 1, 0, -1};
	check_list(g_debrief_sorted_team_ids, teams, 4);
	static const int order[5] = {2, 5, 0, 6, -1};
	check_list(g_debrief_sorted_player_ids, order, 5);
	check_list(g_debrief_kills_on_combatant_ids, order, 5);
	check_list(g_debrief_kills_from_combatant_ids, order, 5);

	/* With a lower score team 1 falls behind team 0. */
	g_pilot_data.teams[1].mission_score = 50;
	mission_debrief_prepare();
	static const int lower[4] = {2, 0, 1, -1};
	check_list(g_debrief_sorted_team_ids, lower, 4);
}

/* The local team's position in the sorted teams is found when, as the
 * search also needs, the team at that position has a player: teams 0 and 1
 * with team 1 ahead puts the local team 0 second. */
static void check_prepare_local_rank(void)
{
	engagement_world();
	g_pilot_data.network_players[2].flight_group_id = 1;
	g_pilot_data.network_players[5].flight_group_id = 1;
	g_pilot_data.teams[1].is_mission_completed = 1;
	g_pilot_data.team = 0;
	mission_debrief_prepare();
	XVT_ASSERT_INT_EQ(g_debrief_sorted_team_ids[0], 1);
	XVT_ASSERT_INT_EQ(g_debrief_sorted_team_ids[1], 0);
	XVT_ASSERT_INT_EQ(g_debrief_local_team_rank_index, 1);
}

/* Known failure local_team_rank_by_team_id, issue #68: the local team's rank
 * is its position in g_debrief_sorted_team_ids. Teams 0 and 2 have players
 * and team 2 completed the mission, so it sorts first and the local team 0
 * second; the search reads g_debrief_team_has_player at position 1, which is
 * team 1's flag, finds nothing, and leaves the rank at 0, first place. The
 * global's comment describes the search; the fix will have to change it. */
static void check_local_team_rank_by_team_id(void)
{
	engagement_world();
	g_pilot_data.team = 0;
	mission_debrief_prepare();
	XVT_ASSERT_INT_EQ(g_debrief_sorted_team_ids[1], 0);
	XVT_ASSERT_INT_EQ(g_debrief_local_team_rank_index, 1);
}

/* A single-player melee: the local player flies group 0 for team 0; groups 1
 * (team 1) and 3 (team 3) are player groups no human flies, group 2 (team 1)
 * is not a player group. Teams 0, 1 and 3 are in the tournament standings,
 * and so is team 8, whose flag no entry past 7 is read for. Teams 0 and 3
 * tie on mission score and on total score. */
static void melee_world(void)
{
	fresh_world(FRONTEND_MISSION_SESSION_SINGLEPLAYER,
		    MISSION_DIRECTORY_MELEES);
	g_frontend_mission.flight_group_count = 4;
	g_frontend_mission.flight_groups[0].player_number = 1;
	g_frontend_mission.flight_groups[1].team = 1;
	g_frontend_mission.flight_groups[1].player_number = 1;
	g_frontend_mission.flight_groups[2].team = 1;
	g_frontend_mission.flight_groups[3].team = 3;
	g_frontend_mission.flight_groups[3].player_number = 2;
	add_player(0, TEST_LOCAL_PLAYER, 0);
	struct melee_tournament_sequence_state *state =
		&g_pilot_data.melee_tournament_sequence_state;
	for (int team = 0; team < 10; ++team) {
		state->team_standings[team]
			.ai_opponent_source_team_and_type_flag = -1;
	}
	state->team_standings[0].ai_opponent_source_team_and_type_flag = 0;
	state->team_standings[1].ai_opponent_source_team_and_type_flag = 0;
	state->team_standings[3].ai_opponent_source_team_and_type_flag = 0;
	state->team_standings[8].ai_opponent_source_team_and_type_flag = 0;
	state->team_standings[0].total_score = 100;
	state->team_standings[1].total_score = 300;
	state->team_standings[3].total_score = 100;
	g_pilot_data.teams[0].mission_score = 10;
	g_pilot_data.teams[1].mission_score = 30;
	g_pilot_data.teams[3].mission_score = 10;
	g_pilot_data.teams[0].is_mission_completed = 1;
	g_pilot_data.kills_full_on_flight_group[1] = 2;
	g_pilot_data.kills_full_on_flight_group[3] = 2;
	g_pilot_data.kills_shared_on_flight_group[3] = 1;
	g_pilot_data.kills_full_from_flight_group[1] = 4;
	g_pilot_data.kills_full_from_flight_group[3] = 4;
	g_pilot_data.kills_shared_from_flight_group[3] = 1;
	g_team_count = 4;
	for (int team = 0; team < 4; ++team) {
		g_team_player_flight_group_count[team] = 1;
	}
}

/* In a single-player melee the teams of player groups no human flies count,
 * marked as AI-only teams; the standings take the teams flagged in the
 * sequence (0 to 7 only) by total score, a tie keeping the earlier team
 * ahead; teams sort by mission score alone, the same way;
 * the groups no human flies join the "killed" and "killed by" lists as 8
 * plus their index, by full kills and then shared ones; and with no team
 * holding more than one player group, pilots are ranked rather than
 * teams. */
static void check_prepare_melee(void)
{
	melee_world();
	XVT_ASSERT_INT_EQ(mission_debrief_prepare(), 1);
	static const int has_player[10] = {1, 1, 0, 1, 0, 0, 0, 0, 0, 0};
	check_list(g_debrief_team_has_player, has_player, 10);
	static const int ai_only[10] = {0, 1, 0, 1, 0, 0, 0, 0, 0, 0};
	check_list(g_debrief_team_has_only_ai_pilots, ai_only, 10);
	XVT_ASSERT_INT_EQ(g_debrief_active_team_count, 3);
	static const int in_standings[10] = {1, 1, 0, 1, 0, 0, 0, 0, 0, 0};
	check_list(g_debrief_team_in_standings, in_standings, 10);
	static const int standings[10] = {1, 0, 3, -1, -1, -1, -1, -1, -1, -1};
	check_list(g_debrief_standings_team_ids, standings, 10);
	static const int teams[10] = {1, 0, 3, -1, -1, -1, -1, -1, -1, -1};
	check_list(g_debrief_sorted_team_ids, teams, 10);
	static const int killed[8] = {11, 9, 0, -1, -1, -1, -1, -1};
	check_list(g_debrief_kills_on_combatant_ids, killed, 8);
	static const int killed_by[8] = {11, 9, 0, -1, -1, -1, -1, -1};
	check_list(g_debrief_kills_from_combatant_ids, killed_by, 8);
	XVT_ASSERT_INT_EQ(g_debrief_rank_by_pilot, 1);

	/* A team with two player groups ranks teams again. */
	melee_world();
	g_team_player_flight_group_count[2] = 2;
	mission_debrief_prepare();
	XVT_ASSERT_INT_EQ(g_debrief_rank_by_pilot, 0);

	/* In a network melee with two humans and no AI opponents, groups no
	 * human flies make no team. */
	melee_world();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	add_player(4, 0x45, 2);
	mission_debrief_prepare();
	XVT_ASSERT_INT_EQ(g_debrief_team_has_player[3], 0);
	XVT_ASSERT_INT_EQ(g_debrief_team_has_only_ai_pilots[3], 0);
	XVT_ASSERT_INT_EQ(g_debrief_active_team_count, 2);
}

/* Players 0x41 and 0x42 are in the session roster, not yet ready, with a
 * third, 0x50, the debriefing does not list. Network player slot 3 holds
 * 0x42. */
static void ready_world(void)
{
	fresh_world(FRONTEND_MISSION_SESSION_NET_HOST,
		    MISSION_DIRECTORY_MELEES);
	g_front_state.net_players[0].player_id = TEST_LOCAL_PLAYER;
	g_front_state.net_players[1].player_id = 0x42;
	g_front_state.net_players[2].player_id = 0x50;
	g_front_state.net_player_count = 3;
	add_player(0, TEST_LOCAL_PLAYER, 0);
	add_player(3, 0x42, 1);
}

/* Every network player with a DirectPlay id is marked ready in the session
 * roster; a roster player the debriefing does not list is left alone. */
static void check_mark_players_ready(void)
{
	ready_world();
	XVT_ASSERT_INT_EQ(mission_debrief_mark_network_players_ready(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[0].ready_flag, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[2].ready_flag, 0);
}

/* Known failure left_player_marked_ready, issue #164: the debriefing clears
 * the ready flag of a player who left during the flight, as
 * mission_debrief_update's comment says, so that the player is no longer
 * counted as admitted. Player 0x42 left and its flag is clear; marking the
 * network players ready sets it again, as the comment over the marking says
 * it does for every player with an id. The fix will have to change that
 * comment. The flag should stay clear. */
static void check_left_player_marked_ready(void)
{
	ready_world();
	g_pilot_data.network_players[3].has_left = 1;
	mission_debrief_mark_network_players_ready();
	XVT_ASSERT_INT_EQ(g_front_state.net_players[0].ready_flag, 1);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[1].ready_flag, 0);
}

/* The exit function frees the list and the text and sets both NULL, forgets
 * the scrollable controls and opens the mouse input gate. It returns 0. */
static void check_exit(void)
{
	g_mission_list = calloc(2, sizeof *g_mission_list);
	XVT_ASSERT_TRUE(g_mission_list != NULL);
	g_mission_count = 2;
	g_mission_text = calloc(4096, 1);
	XVT_ASSERT_TRUE(g_mission_text != NULL);
	g_scrollable_control_count = 3;
	g_front_state.mouse_input_gate = 7;
	XVT_ASSERT_INT_EQ(mission_debrief_exit(9), 0);
	XVT_ASSERT_TRUE(g_mission_list == NULL);
	XVT_ASSERT_TRUE(g_mission_text == NULL);
	XVT_ASSERT_INT_EQ(g_scrollable_control_count, 0);
	XVT_ASSERT_INT_EQ(g_front_state.mouse_input_gate, 0);
	g_mission_count = 0;
}

static uint8_t g_file[16384];

/* Writes a mission file of size bytes whose first word is version: bytes
 * 4096 to 8191 are 'W' and 8192 to 12287 are 'L', the rest '-', so in a
 * 16384-byte file the win text starts at 'W' and the other at 'L'. */
static void write_outcome_file(const char *name, uint16_t version, size_t size)
{
	XVT_ASSERT_TRUE(size <= sizeof g_file);
	memset(g_file, '-', sizeof g_file);
	memset(g_file + 4096, 'W', 4096);
	memset(g_file + 8192, 'L', 4096);
	memcpy(g_file, &version, sizeof version);
	xvt_test_write_file(g_assets.asset, name, g_file, size);
}

/* A training pilot whose mission, id 4, is the second entry of a two-entry
 * list, "m4.tie", in a fresh asset folder with the training and battle
 * folders. */
static void outcome_world(void)
{
	fresh_world(FRONTEND_MISSION_SESSION_SINGLEPLAYER,
		    MISSION_DIRECTORY_TRAINING_EXERCISES);
	g_pilot_data.mission_description_ids[0] = TEST_MISSION_ID;
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_BATTLES] =
		TEST_MISSION_ID;
	g_mission_list = calloc(2, sizeof *g_mission_list);
	XVT_ASSERT_TRUE(g_mission_list != NULL);
	g_mission_count = 2;
	g_mission_list[0].mission_idx = 1;
	strcpy(g_mission_list[0].file_name, "m1.tie");
	g_mission_list[1].mission_idx = TEST_MISSION_ID;
	strcpy(g_mission_list[1].file_name, "m4.tie");
	xvt_test_open_assets(&g_assets);
	xvt_test_make_subfolder(g_assets.asset, "train");
	xvt_test_make_subfolder(g_assets.asset, "battle");
}

static void close_outcome_world(void)
{
	free(g_mission_list);
	g_mission_list = NULL;
	g_mission_count = 0;
	xvt_test_close_assets(&g_assets);
}

/* The text read for the outcome given, after the buffer is filled with
 * 'z'. */
static char *read_outcome(int use_win_text)
{
	static char text[4096];
	memset(text, 'z', sizeof text);
	mission_debrief_read_outcome_text(text, use_win_text);
	return text;
}

/* A version 14 file gives the 4096 bytes that start 12288 bytes before its
 * end after a completed mission, and 8192 bytes before it otherwise, the last
 * byte set to 0. */
static void check_outcome_text_read(void)
{
	outcome_world();
	write_outcome_file("train/m4.tie", 14, 16384);
	char *text = read_outcome(1);
	XVT_ASSERT_INT_EQ(text[0], 'W');
	XVT_ASSERT_INT_EQ(text[4094], 'W');
	XVT_ASSERT_INT_EQ(text[4095], 0);
	text = read_outcome(0);
	XVT_ASSERT_INT_EQ(text[0], 'L');
	XVT_ASSERT_INT_EQ(text[4094], 'L');
	XVT_ASSERT_INT_EQ(text[4095], 0);
	close_outcome_world();
}

/* The text is cleared and stays empty for another version, for a mission the
 * list does not hold, for a file that does not open, and in the battle
 * directory. A NULL buffer returns at once. */
static void check_outcome_text_empty(void)
{
	outcome_world();
	write_outcome_file("train/m4.tie", 13, 16384);
	write_outcome_file("battle/m4.tie", 14, 16384);
	XVT_ASSERT_INT_EQ(read_outcome(1)[0], 0);
	XVT_ASSERT_INT_EQ(read_outcome(1)[4095], 0);

	g_mission_list[1].mission_idx = 5;
	XVT_ASSERT_INT_EQ(read_outcome(1)[0], 0);
	g_mission_list[1].mission_idx = TEST_MISSION_ID;

	strcpy(g_mission_list[1].file_name, "missing.tie");
	XVT_ASSERT_INT_EQ(read_outcome(1)[0], 0);
	strcpy(g_mission_list[1].file_name, "m4.tie");

	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_BATTLES;
	XVT_ASSERT_INT_EQ(read_outcome(1)[0], 0);

	mission_debrief_read_outcome_text(NULL, 1);
	close_outcome_world();
}

/* Known failure short_file_outcome_text, issue #161: a version 14 file of
 * 3000 bytes has no text 12288 bytes before its end. The seek there fails
 * and is not checked, so the read takes the file's bytes from just after
 * its first word, here '-', and the page would show them as text. The text
 * should stay empty, as for any file that has no outcome text. */
static void check_short_file_outcome_text(void)
{
	outcome_world();
	write_outcome_file("train/m4.tie", 14, 3000);
	XVT_ASSERT_INT_EQ(read_outcome(1)[0], 0);
	close_outcome_world();
}

/* The narrative page returns 0 without drawing when there is no mission
 * text, else 1. */
static void check_narrative_page(void)
{
	fresh_world(FRONTEND_MISSION_SESSION_SINGLEPLAYER,
		    MISSION_DIRECTORY_TRAINING_EXERCISES);
	XVT_ASSERT_INT_EQ(mission_debrief_draw_narrative_text_page(), 0);
	g_mission_text = calloc(4096, 1);
	XVT_ASSERT_TRUE(g_mission_text != NULL);
	strcpy(g_mission_text, "The convoy got through.");
	g_pilot_data.mission_sequence_active = 1;
	XVT_ASSERT_INT_EQ(mission_debrief_draw_narrative_text_page(), 1);
	free(g_mission_text);
	g_mission_text = NULL;
}

/* The player statistics page returns 0 in network play when the local
 * player is not among the network players, and 1 when it is or in single
 * player. */
static void check_statistics_page(void)
{
	engagement_world();
	g_front_state.net_players[0].player_id = 0x99;
	XVT_ASSERT_INT_EQ(mission_debrief_draw_player_statistics_page(), 0);
	g_front_state.net_players[0].player_id = 0x46;
	XVT_ASSERT_INT_EQ(mission_debrief_draw_player_statistics_page(), 1);
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	g_front_state.net_players[0].player_id = 0x99;
	XVT_ASSERT_INT_EQ(mission_debrief_draw_player_statistics_page(), 1);
}

/* The other pages and the tab bar draw and return 1: the overview and the
 * tab bar for an engagement and a melee, the tournament summary after a
 * melee sequence's last mission, and the battle summary of a battle under
 * way and of one the Empire won. */
static void check_other_pages(void)
{
	engagement_world();
	g_front_state.net_players[0].player_id = TEST_LOCAL_PLAYER;
	mission_debrief_prepare();
	XVT_ASSERT_INT_EQ(mission_debrief_draw_mission_overview_page(0), 1);
	XVT_ASSERT_INT_EQ(mission_debrief_draw_tab_bar(), 1);

	melee_world();
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.melee_tournament_sequence_state.mission_count = 2;
	g_pilot_data.melee_tournament_sequence_state.current_mission_index = 1;
	mission_debrief_prepare();
	XVT_ASSERT_INT_EQ(mission_debrief_draw_mission_overview_page(0), 1);
	XVT_ASSERT_INT_EQ(mission_debrief_draw_tournament_summary_page(0), 1);
	XVT_ASSERT_INT_EQ(mission_debrief_draw_tab_bar(), 1);

	engagement_world();
	g_pilot_data.mission_sequence_active = 1;
	struct battle_sequence_state *battle =
		&g_pilot_data.battle_sequence_state;
	battle->victories_needed = 2;
	g_mission_list = calloc(1, sizeof *g_mission_list);
	XVT_ASSERT_TRUE(g_mission_list != NULL);
	g_mission_count = 1;
	strcpy(g_mission_list[0].description, "First strike");
	XVT_ASSERT_INT_EQ(mission_debrief_draw_battle_summary_page(), 1);
	battle->current_mission_index = 1;
	XVT_ASSERT_INT_EQ(mission_debrief_draw_battle_summary_page(), 1);
	free(g_mission_list);
	g_mission_list = NULL;
	g_mission_count = 0;
}

/* The first frame of the debriefing of a single-player campaign's second of
 * three missions, not completed, with the campaign's id given: a fresh
 * frontend display and asset folder holding the rebel campaign list, which
 * names campaign 1 and then that campaign, and the rebel training list, which
 * names the pilot's mission 4. The random seed and setup are 1234 and 2, and the sequence's
 * score 77. */
static void campaign_frame_world(int campaign_id)
{
	memset(&g_front_state, 0, sizeof g_front_state);
	xvt_test_open_display();
	xvt_campaign_task_reset();
	fresh_world(FRONTEND_MISSION_SESSION_SINGLEPLAYER,
		    MISSION_DIRECTORY_TRAINING_EXERCISES);
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.mission_description_ids[0] = TEST_MISSION_ID;
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_CAMPAIGNS] =
		campaign_id;
	g_pilot_data.campaign_sequence_state.mission_count = 3;
	g_pilot_data.campaign_sequence_state.current_mission_index = 1;
	g_pilot_data.campaign_sequence_state.cumulative_score = 77;
	g_game_config.random_seed = 1234;
	g_game_config.random_setup = 2;
	char list[160];
	sprintf(list,
		"[Campaigns]\n1\nother.lst\nAnother campaign\n"
		"%d\ncamp.lst\nThe long campaign\n",
		campaign_id);
	xvt_test_open_assets(&g_assets);
	xvt_test_make_subfolder(g_assets.asset, "campaign");
	xvt_test_make_subfolder(g_assets.asset, "train");
	xvt_test_write_text(g_assets.asset, "campaign/rebel.lst", list);
	xvt_test_write_text(g_assets.asset, "train/rebel.lst",
			    "4\nm4.tie\nFirst mission (1)\n");
}

/* Leaves the debriefing, which frees the list and the text, and closes the
 * folder and the display. */
static void close_campaign_frame_world(void)
{
	mission_debrief_exit(0);
	xvt_test_close_assets(&g_assets);
	xvt_test_close_display();
}

/* On its first frame the debriefing of an unfinished campaign saves the
 * sequence for a later resume in the single-player continuation slot of the
 * campaign's id: active, with the random seed and setup and a copy of the
 * sequence state. It takes the campaign's description from its list, puts
 * the directory back to training and returns 0. */
static void check_first_frame_saves_campaign(void)
{
	campaign_frame_world(3);
	XVT_ASSERT_INT_EQ(mission_debrief_update(0), 0);
	struct campaign_continuation *slot =
		&g_pilot_data.sp_campaign_continuations[3];
	XVT_ASSERT_INT_EQ(slot->is_active, 1);
	XVT_ASSERT_INT_EQ(slot->random_seed, 1234);
	XVT_ASSERT_INT_EQ(slot->random_setup, 2);
	XVT_ASSERT_INT_EQ(slot->sequence_state.current_mission_index, 1);
	XVT_ASSERT_INT_EQ(slot->sequence_state.cumulative_score, 77);
	XVT_ASSERT_INT_EQ(g_pilot_data.mp_campaign_continuations[3].is_active,
			  0);
	XVT_ASSERT_TRUE(strcmp(g_mission_sequence_description,
			       "The long campaign") == 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_directory_id,
			  MISSION_DIRECTORY_TRAINING_EXERCISES);
	close_campaign_frame_world();

	/* After the campaign's last mission, completed, the slot is marked
	 * inactive. */
	campaign_frame_world(3);
	g_pilot_data.sp_campaign_continuations[3].is_active = 1;
	g_pilot_data.campaign_sequence_state.current_mission_index = 2;
	g_pilot_data.campaign_sequence_state.last_mission_completed = 1;
	XVT_ASSERT_INT_EQ(mission_debrief_update(0), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.sp_campaign_continuations[3].is_active,
			  0);
	XVT_ASSERT_INT_EQ(g_pilot_data.sp_campaign_continuations[3].random_seed,
			  0);
	close_campaign_frame_world();
}

/* Known failure campaign_id_past_table, issue #168: pilot_record.h gives the
 * pilot record 25 single-player campaign continuation slots, indexed by the
 * campaign's id. The campaign list names campaign 25; the first frame saves
 * its progress in slot 25 without checking the id, which writes over the
 * first network campaign slot, the table that follows. Nothing outside the 25
 * slots should change. */
static void check_campaign_id_past_table(void)
{
	campaign_frame_world(25);
	mission_debrief_update(0);
	struct campaign_continuation *next =
		&g_pilot_data.mp_campaign_continuations[0];
	XVT_ASSERT_INT_EQ(next->is_active, 0);
	XVT_ASSERT_INT_EQ(next->random_seed, 0);
	close_campaign_frame_world();
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
			{"local_team_rank_by_team_id",
			 check_local_team_rank_by_team_id},
			{"left_player_marked_ready",
			 check_left_player_marked_ready},
			{"short_file_outcome_text",
			 check_short_file_outcome_text},
			{"campaign_id_past_table",
			 check_campaign_id_past_table},
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
	memset(&g_front_state, 0, sizeof g_front_state);
	check_prepare_engagement();
	check_prepare_ties();
	check_prepare_local_rank();
	check_prepare_melee();
	check_mark_players_ready();
	check_exit();
	check_outcome_text_read();
	check_outcome_text_empty();

	xvt_test_open_display();
	check_narrative_page();
	check_statistics_page();
	check_other_pages();
	xvt_test_close_display();
	check_first_frame_saves_campaign();
	return 0;
}
