/* Tests for xvt/frontend/mission_setup.c, the mission setup screens: the
 * mission lists, the tournament, battle and campaign sequences, the ship list,
 * the lobby roster, the team assignments and the loadout lookups. Each check
 * sets the game state it needs in the game's own tables: the pilot record, the
 * game settings, the lobby roster, the loaded mission and the assignments. The
 * list and sequence checks read files this file writes into a temporary asset
 * folder: mission lists, sequence files, a ship list and small mission files.
 * No game data is read.
 *
 * POSIX only, for the temporary folder (test_asset_folder.h) and for the alarm
 * that stops a check whose call does not return. */
#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_assert.h"
#include "test_asset_folder.h"
#include "xvt/assets/file.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/runtime/campaign_task.h"
#include "xvt_runtime/timing/host_clock.h"

static struct xvt_test_assets g_assets;

/* Clears the state the mission setup code reads: a solo game with no mission
 * list, ship list, roster, assignments or loaded mission, every flight group
 * slot of the teams empty (-1), and the pilot record and game settings zeroed.
 * Opens a fresh asset folder; close_world removes it. */
static void open_world(void)
{
	free(g_mission_list);
	g_mission_list = NULL;
	g_mission_count = 0;
	g_selected_mission_list_index = 0;
	free(g_ship_list);
	g_ship_list = NULL;
	g_ship_count = 0;
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	memset(&g_game_config, 0, sizeof g_game_config);
	memset(&g_front_state, 0, sizeof g_front_state);
	memset(&g_frontend_mission, 0, sizeof g_frontend_mission);
	memset(&g_mission_setup_player_assignments, 0,
	       sizeof g_mission_setup_player_assignments);
	memset(g_mp_roster, 0, sizeof g_mp_roster);
	memset(g_team_player_flight_group_count, 0,
	       sizeof g_team_player_flight_group_count);
	g_team_count = 0;
	for (int i = 0; i < 80; ++i) {
		g_mission_setup_player_flight_group_indices[i] = -1;
	}
	g_local_pilot_network_player_index = 0;
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	xvt_test_open_assets(&g_assets);
}

static void close_world(void)
{
	xvt_test_close_assets(&g_assets);
	free(g_mission_list);
	g_mission_list = NULL;
	g_mission_count = 0;
	free(g_ship_list);
	g_ship_list = NULL;
}

/* Writes the text to path in the asset folder, path written with '/', making
 * the folders on the way. */
static void put_text(const char *path, const char *text)
{
	xvt_test_add_asset(&g_assets, path);
	xvt_test_write_text(g_assets.asset, path, text);
}

/* Writes a mission file of format 12 with the given flight groups, no
 * messages, no global goals and no team records. */
static void put_mission(const char *path, const struct xvt_flight_group *groups,
			int16_t group_count)
{
	static uint8_t file[sizeof(struct frontend_mission)];
	size_t size = 0;
	uint16_t version = 12;
	uint16_t message_count = 0;
	memset(file, 0, sizeof file);
	memcpy(file + size, &version, sizeof version);
	size += sizeof version;
	memcpy(file + size, &group_count, sizeof group_count);
	size += sizeof group_count;
	memcpy(file + size, &message_count, sizeof message_count);
	size += sizeof message_count;
	size += sizeof g_frontend_mission.header;
	memcpy(file + size, groups, group_count * sizeof *groups);
	size += group_count * sizeof *groups;
	/* Ten global goal counts and ten team record flags, all 0. */
	size += 20 * sizeof(uint16_t);
	xvt_test_add_asset(&g_assets, path);
	xvt_test_write_file(g_assets.asset, path, file, size);
}

static void stop_on_alarm(int signal_number)
{
	static const char message[] =
		"check failed: the call did not return within the time allowed\n";
	(void)signal_number;
	if (write(2, message, sizeof message - 1) < 0) {
		_exit(1);
	}
	_exit(1);
}

/* For a check whose call may never return: the program fails after the given
 * seconds. */
static void fail_after_seconds(unsigned int seconds)
{
	signal(SIGALRM, stop_on_alarm);
	alarm(seconds);
}

/* ------------------------------------------------------------------------ */
/* The ship list. */

/* ship_list_load keeps the lines whose name ends in "opt", whatever its case,
 * with those three characters lowercased, and counts them in g_ship_count. Each
 * kept craft type under 17 maps to its entry's index; a type the list does not
 * name keeps its mapping, and types 17 and 20 map to nothing. */
static void check_ship_list_keeps_models(void)
{
	open_world();
	put_text("frontres/frntspec.lst", "XWING.OPT 1\n"
					  "ywing.Opt 2\n"
					  "cockpit.bmp 3\n"
					  "tief.opt 6\n"
					  "station.opt 20\n"
					  "probe.opt 17\n");
	for (int type = 0; type < 18; ++type) {
		g_ship_type_to_ship_list_index[type] = 99;
	}
	ship_list_load();
	XVT_ASSERT_TRUE(g_ship_list != NULL);
	XVT_ASSERT_INT_EQ(g_ship_count, 5);
	XVT_ASSERT_TRUE(strcmp(g_ship_list[0].model_file_name, "XWING.opt") ==
			0);
	XVT_ASSERT_TRUE(strcmp(g_ship_list[1].model_file_name, "ywing.opt") ==
			0);
	XVT_ASSERT_TRUE(strcmp(g_ship_list[2].model_file_name, "tief.opt") ==
			0);
	XVT_ASSERT_TRUE(strcmp(g_ship_list[3].model_file_name, "station.opt") ==
			0);
	XVT_ASSERT_INT_EQ(g_ship_list[2].craft_type, 6);
	XVT_ASSERT_INT_EQ(g_ship_list[3].craft_type, 20);
	XVT_ASSERT_INT_EQ(g_ship_type_to_ship_list_index[1], 0);
	XVT_ASSERT_INT_EQ(g_ship_type_to_ship_list_index[2], 1);
	XVT_ASSERT_INT_EQ(g_ship_type_to_ship_list_index[6], 2);
	XVT_ASSERT_INT_EQ(g_ship_type_to_ship_list_index[3], 99);
	XVT_ASSERT_INT_EQ(g_ship_type_to_ship_list_index[17], 99);
	close_world();
}

/* A model name of 70 characters keeps its first 63. */
static void check_ship_list_name_cut(void)
{
	char text[96];
	open_world();
	memset(text, 'a', 66);
	strcpy(text + 66, ".opt 1\n");
	put_text("frontres/frntspec.lst", text);
	ship_list_load();
	XVT_ASSERT_INT_EQ(g_ship_count, 1);
	XVT_ASSERT_INT_EQ(strlen(g_ship_list[0].model_file_name), 63);
	XVT_ASSERT_TRUE(strncmp(g_ship_list[0].model_file_name, text, 63) == 0);
	close_world();
}

/* The list loads once: while g_ship_list is set nothing changes. Without the
 * file the list is allocated and g_ship_count keeps its value. */
static void check_ship_list_once_and_missing(void)
{
	open_world();
	put_text("frontres/frntspec.lst", "xwing.opt 1\n");
	struct ship_list_entry *kept = malloc(sizeof *kept);
	g_ship_list = kept;
	g_ship_count = 7;
	ship_list_load();
	XVT_ASSERT_TRUE(g_ship_list == kept);
	XVT_ASSERT_INT_EQ(g_ship_count, 7);
	close_world();

	open_world();
	g_ship_count = 7;
	ship_list_load();
	XVT_ASSERT_TRUE(g_ship_list != NULL);
	XVT_ASSERT_INT_EQ(g_ship_count, 7);
	close_world();
}

/* Known failure ship_list_over_100, issue #141: g_ship_list is a heap array of
 * 100 entries. A list naming 101 models writes the last one past its end, and
 * the sanitizer stops the program; no more than 100 should be kept. */
static void check_ship_list_over_100(void)
{
	static char text[101 * 16 + 1];
	open_world();
	text[0] = '\0';
	for (int i = 0; i < 101; ++i) {
		strcat(text, "ship.opt 1\n");
	}
	put_text("frontres/frntspec.lst", text);
	ship_list_load();
	XVT_ASSERT_TRUE(g_ship_count <= 100);
	close_world();
}

/* Known failure ship_list_negative_type, issue #141: the craft type maps into
 * the 18 entries of g_ship_type_to_ship_list_index. A line with type -1 is
 * written before its first entry, and the sanitizer stops the program; a
 * type outside the table should map nothing. */
static void check_ship_list_negative_type(void)
{
	open_world();
	put_text("frontres/frntspec.lst", "xwing.opt -1\n");
	ship_list_load();
	XVT_ASSERT_INT_EQ(g_ship_count, 1);
	close_world();
}

/* ------------------------------------------------------------------------ */
/* The team counts and assignments. */

/* Marks flight group fg of the loaded mission as a player's group on team. */
static void player_group(int fg, int team)
{
	g_frontend_mission.flight_groups[fg].player_number = 1;
	g_frontend_mission.flight_groups[fg].team = (uint8_t)team;
	if (g_frontend_mission.flight_group_count <= fg) {
		g_frontend_mission.flight_group_count = (uint16_t)(fg + 1);
	}
}

/* mission_setup_update_team_counts counts the player flight groups of each
 * team, a group without a player number left out, and g_team_count is the
 * number of teams with at least one. A count from an earlier mission is
 * cleared. */
static void check_team_counts(void)
{
	open_world();
	g_team_player_flight_group_count[5] = 4;
	player_group(0, 0);
	player_group(1, 0);
	player_group(3, 3);
	g_frontend_mission.flight_groups[2].team = 1;
	g_frontend_mission.flight_group_count = 4;
	mission_setup_update_team_counts();
	XVT_ASSERT_INT_EQ(g_team_player_flight_group_count[0], 2);
	XVT_ASSERT_INT_EQ(g_team_player_flight_group_count[1], 0);
	XVT_ASSERT_INT_EQ(g_team_player_flight_group_count[3], 1);
	XVT_ASSERT_INT_EQ(g_team_player_flight_group_count[5], 0);
	XVT_ASSERT_INT_EQ(g_team_count, 2);
	close_world();
}

/* Known failure team_past_count_table, issue #145: there are ten teams, and
 * g_team_player_flight_group_count has an entry for each. A player flight group
 * on team 10 is counted past the table's end, and the sanitizer stops the
 * program; only the group on team 0 should count. */
static void check_team_counts_team_out_of_range(void)
{
	open_world();
	player_group(0, 0);
	player_group(1, 10);
	mission_setup_update_team_counts();
	XVT_ASSERT_INT_EQ(g_team_count, 1);
	close_world();
}

/* A solo game whose pilot (player 1, roster entry 0) holds assignment slot 0
 * and team 0's captain slot. */
static void solo_pilot_assigned(void)
{
	g_mp_roster[0].player_id = 1;
	g_mission_setup_player_assignments.assigned_player_ids[0] = 1;
	g_mission_setup_player_assignments.team_player_ids[0][0] = 1;
}

/* mission_setup_is_team_assignment_valid returns 1 when every ready player
 * holds an assignment slot and each team with players has a captain; 0 when a
 * ready roster entry has no slot or an id of 0, or a team has a player but no
 * captain. */
static void check_team_assignment_valid(void)
{
	open_world();
	g_team_count = 2;
	solo_pilot_assigned();
	XVT_ASSERT_INT_EQ(mission_setup_is_team_assignment_valid(), 1);

	g_mission_setup_player_assignments.team_player_ids[1][3] = 9;
	XVT_ASSERT_INT_EQ(mission_setup_is_team_assignment_valid(), 0);
	g_mission_setup_player_assignments.team_player_ids[1][0] = 8;
	XVT_ASSERT_INT_EQ(mission_setup_is_team_assignment_valid(), 1);

	g_mission_setup_player_assignments.assigned_player_ids[0] = 0;
	g_mission_setup_player_assignments.assigned_player_ids[5] = 1;
	XVT_ASSERT_INT_EQ(mission_setup_is_team_assignment_valid(), 1);
	g_mission_setup_player_assignments.assigned_player_ids[5] = 0;
	XVT_ASSERT_INT_EQ(mission_setup_is_team_assignment_valid(), 0);

	g_mp_roster[0].player_id = 0;
	XVT_ASSERT_INT_EQ(mission_setup_is_team_assignment_valid(), 0);
	close_world();
}

/* Outside a solo game the ready players are those the lobby roster marks
 * ready: with two, roster entry 1 must hold a slot as well. */
static void check_team_assignment_valid_ready_count(void)
{
	open_world();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	g_front_state.net_players[0].ready_flag = 1;
	g_front_state.net_players[4].ready_flag = 1;
	g_team_count = 1;
	solo_pilot_assigned();
	g_mp_roster[1].player_id = 2;
	XVT_ASSERT_INT_EQ(mission_setup_is_team_assignment_valid(), 0);
	g_mission_setup_player_assignments.assigned_player_ids[1] = 2;
	XVT_ASSERT_INT_EQ(mission_setup_is_team_assignment_valid(), 1);
	close_world();
}

/* Known failure team_gap_hidden, issue #145: a mission whose player flight
 * groups are on teams 0 and 2 has teams 0 and 2 to fill, but g_team_count is 2
 * and the captain check walks teams 0 and 1. Team 2 has a player and no
 * captain, so the teams are not ready; the check returns 1 all the same. */
static void check_team_gap_captain(void)
{
	open_world();
	player_group(0, 0);
	player_group(1, 2);
	mission_setup_update_team_counts();
	solo_pilot_assigned();
	g_mission_setup_player_assignments.team_player_ids[2][1] = 5;
	XVT_ASSERT_INT_EQ(mission_setup_is_team_assignment_valid(), 0);
	close_world();
}

/* mission_setup_prune_team_assignments takes out an assigned player who is no
 * longer in the roster: its slot entry becomes 0 and it leaves every team, the
 * players after it moving up. The team slots' flight groups do not move. */
static void check_prune_team_assignments(void)
{
	open_world();
	g_team_count = 2;
	g_mp_roster[0].player_id = 1;
	g_mp_roster[1].player_id = 2;
	int *assigned = g_mission_setup_player_assignments.assigned_player_ids;
	assigned[0] = 1;
	assigned[1] = 2;
	assigned[2] = 3;
	int (*teams)[8] = g_mission_setup_player_assignments.team_player_ids;
	teams[0][0] = 1;
	teams[0][1] = 3;
	teams[0][2] = 2;
	teams[1][0] = 3;
	g_mission_setup_player_flight_group_indices[0] = 10;
	g_mission_setup_player_flight_group_indices[1] = 11;
	g_mission_setup_player_flight_group_indices[2] = 12;
	mission_setup_prune_team_assignments();
	XVT_ASSERT_INT_EQ(assigned[0], 1);
	XVT_ASSERT_INT_EQ(assigned[1], 2);
	XVT_ASSERT_INT_EQ(assigned[2], 0);
	XVT_ASSERT_INT_EQ(teams[0][0], 1);
	XVT_ASSERT_INT_EQ(teams[0][1], 2);
	XVT_ASSERT_INT_EQ(teams[0][2], 0);
	XVT_ASSERT_INT_EQ(teams[0][7], 0);
	XVT_ASSERT_INT_EQ(teams[1][0], 0);
	XVT_ASSERT_INT_EQ(g_mission_setup_player_flight_group_indices[1], 11);
	XVT_ASSERT_INT_EQ(g_mission_setup_player_flight_group_indices[2], 12);
	close_world();
}

/* mission_setup_clear_team_assignments empties every team slot and assignment
 * slot and returns 1. */
static void check_clear_team_assignments(void)
{
	open_world();
	solo_pilot_assigned();
	g_mission_setup_player_assignments.team_player_ids[9][7] = 4;
	XVT_ASSERT_INT_EQ(mission_setup_clear_team_assignments(), 1);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.team_player_ids[0][0], 0);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.team_player_ids[9][7], 0);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.assigned_player_ids[0], 0);
	close_world();
}

/* Auto Assign in a solo game with ten teams: the earlier assignments are
 * cleared, and the pilot goes to the only team with a free place, team 9, in
 * its first slot, holding the assignment slot of its roster entry. The
 * generator is seeded so the run repeats. */
static void check_randomize_teams_solo(void)
{
	open_world();
	g_team_count = 10;
	g_team_player_flight_group_count[9] = 1;
	g_mp_roster[0].player_id = 1;
	g_mission_setup_player_assignments.team_player_ids[5][3] = 9;
	srand(1);
	XVT_ASSERT_INT_EQ(mission_setup_randomize_team_assignments(), 1);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.team_player_ids[9][0], 1);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.team_player_ids[1][0], 0);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.team_player_ids[5][3], 0);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.assigned_player_ids[0], 1);
	close_world();
}

/* Auto Assign with two ready players, in roster entries 3 and 6, and one place
 * on each of two teams: each team's first slot holds one of them, and each
 * player's assignment slot is its roster entry's index. */
static void check_randomize_teams_network(void)
{
	open_world();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	g_front_state.net_players[0].ready_flag = 1;
	g_front_state.net_players[1].ready_flag = 1;
	g_team_count = 2;
	g_team_player_flight_group_count[0] = 1;
	g_team_player_flight_group_count[1] = 1;
	g_mp_roster[3].player_id = 30;
	g_mp_roster[6].player_id = 60;
	XVT_ASSERT_INT_EQ(mission_setup_randomize_team_assignments(), 1);
	int first = g_mission_setup_player_assignments.team_player_ids[0][0];
	int second = g_mission_setup_player_assignments.team_player_ids[1][0];
	XVT_ASSERT_INT_EQ(first + second, 90);
	XVT_ASSERT_TRUE(first == 30 || first == 60);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.team_player_ids[0][1], 0);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.assigned_player_ids[3], 30);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.assigned_player_ids[6], 60);
	close_world();
}

/* Known failure auto_assign_no_teams, issue #146: the function promises 1.
 * With no team holding a player flight group, g_team_count is 0 and the team
 * draw divides by zero, which stops the program. */
static void check_randomize_teams_without_teams(void)
{
	open_world();
	g_mp_roster[0].player_id = 1;
	XVT_ASSERT_INT_EQ(mission_setup_randomize_team_assignments(), 1);
	close_world();
}

/* Known failure auto_assign_too_few_places, issue #146: the function promises
 * 1. Two ready players and a single place: the second player's team draw finds
 * no free place and draws forever. */
static void check_randomize_teams_too_few_places(void)
{
	open_world();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	g_front_state.net_players[0].ready_flag = 1;
	g_front_state.net_players[1].ready_flag = 1;
	g_team_count = 1;
	g_team_player_flight_group_count[0] = 1;
	g_mp_roster[0].player_id = 10;
	g_mp_roster[1].player_id = 20;
	fail_after_seconds(5);
	XVT_ASSERT_INT_EQ(mission_setup_randomize_team_assignments(), 1);
	close_world();
}

/* ------------------------------------------------------------------------ */
/* The lobby roster. */

/* Puts a player in the lobby roster's entry, ready or not. */
static void lobby_player(int entry, int player_id, int ready)
{
	g_front_state.net_players[entry].player_id = (DPID)player_id;
	g_front_state.net_players[entry].ready_flag = ready;
	if (g_front_state.net_player_count <= entry) {
		g_front_state.net_player_count = entry + 1;
	}
}

/* A lobby with players 101 and 102 ready and 103 present but not ready.
 * Team 0's slots hold 101, 103, 102 and 104, with flight groups 10 to 13;
 * assignment slots 0 and 1 hold 101 and 102. The pilot record's network
 * entries hold 105, 102 and 101; the local player is 101. Then the departed
 * players are pruned, which returns 1. */
static void prune_departed_world(void)
{
	open_world();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	lobby_player(0, 101, 1);
	lobby_player(1, 102, 1);
	lobby_player(2, 103, 0);
	g_team_count = 1;
	int *team = g_mission_setup_player_assignments.team_player_ids[0];
	team[0] = 101;
	team[1] = 103;
	team[2] = 102;
	team[3] = 104;
	for (int i = 0; i < 4; ++i) {
		g_mission_setup_player_flight_group_indices[i] = 10 + i;
	}
	g_mission_setup_player_assignments.assigned_player_ids[0] = 101;
	g_mission_setup_player_assignments.assigned_player_ids[1] = 102;
	g_pilot_data.network_players[0].direct_play_id = 105;
	g_pilot_data.network_players[1].direct_play_id = 102;
	g_pilot_data.network_players[2].direct_play_id = 101;
	XVT_ASSERT_INT_EQ(mission_setup_prune_disconnected_players(), 1);
}

/* 103, not ready, and 104, absent, leave team 0; the ready players stay, 102
 * moving up over the freed slot, and the assignment slots keep both. */
static void check_prune_departed_team_slots(void)
{
	prune_departed_world();
	int *team = g_mission_setup_player_assignments.team_player_ids[0];
	XVT_ASSERT_INT_EQ(team[0], 101);
	XVT_ASSERT_INT_EQ(team[1], 102);
	XVT_ASSERT_INT_EQ(team[2], 0);
	XVT_ASSERT_INT_EQ(team[3], 0);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.assigned_player_ids[0], 101);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.assigned_player_ids[1], 102);
	close_world();
}

/* 102's flight group, 12, moves up with it, and every freed slot, slot 7
 * included, has flight group -1. */
static void check_prune_departed_flight_groups(void)
{
	prune_departed_world();
	int *flight_groups = g_mission_setup_player_flight_group_indices;
	XVT_ASSERT_INT_EQ(flight_groups[0], 10);
	XVT_ASSERT_INT_EQ(flight_groups[1], 12);
	XVT_ASSERT_INT_EQ(flight_groups[2], -1);
	XVT_ASSERT_INT_EQ(flight_groups[3], -1);
	XVT_ASSERT_INT_EQ(flight_groups[7], -1);
	close_world();
}

/* The pilot record's entry of 105, gone, is cleared, 102's is kept, and the
 * local player, 101, is found in entry 2. */
static void check_prune_departed_pilot_entries(void)
{
	prune_departed_world();
	XVT_ASSERT_INT_EQ(g_pilot_data.network_players[0].direct_play_id, 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.network_players[1].direct_play_id, 102);
	XVT_ASSERT_INT_EQ(g_pilot_data.network_players[2].direct_play_id, 101);
	XVT_ASSERT_INT_EQ(g_local_pilot_network_player_index, 2);
	close_world();
}

/* With eight players in the lobby roster, an assignment slot whose player is
 * not among the ready ones is cleared, with the g_mp_roster entry of the same
 * index. */
static void check_prune_departed_full_lobby(void)
{
	open_world();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	for (int i = 0; i < 8; ++i) {
		lobby_player(i, 101 + i, 1);
		g_mp_roster[i].player_id = 101 + i;
		g_mission_setup_player_assignments.assigned_player_ids[i] =
			101 + i;
	}
	g_mission_setup_player_assignments.assigned_player_ids[2] = 110;
	g_mp_roster[2].player_id = 110;
	mission_setup_prune_disconnected_players();
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.assigned_player_ids[2], 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[2].player_id, 0);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.assigned_player_ids[3], 104);
	XVT_ASSERT_INT_EQ(g_mp_roster[3].player_id, 104);
	close_world();
}

/* Known failure departed_assignment_kept, issue #58: the function drops the
 * players who left the lobby from the mission's assignments. With two players
 * in the lobby, assignment slot 2 still holds 103, who left, and so does
 * g_mp_roster entry 2; both should be cleared, as they are with eight players
 * in the lobby. The function's comment describes the fault. */
static void check_prune_departed_small_lobby(void)
{
	open_world();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	lobby_player(0, 101, 1);
	lobby_player(1, 102, 1);
	for (int i = 0; i < 3; ++i) {
		g_mp_roster[i].player_id = 101 + i;
		g_mission_setup_player_assignments.assigned_player_ids[i] =
			101 + i;
	}
	mission_setup_prune_disconnected_players();
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.assigned_player_ids[2], 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[2].player_id, 0);
	close_world();
}

/* mp_roster_compact_active_entries moves the entries with a player to the
 * front in their order and zeroes the entries they leave; it returns 1. */
static void check_roster_compact(void)
{
	open_world();
	g_mp_roster[1].player_id = 5;
	strcpy(g_mp_roster[1].name, "Five");
	g_mp_roster[3].player_id = 7;
	g_mp_roster[3].craft_type_override = 4;
	XVT_ASSERT_INT_EQ(mp_roster_compact_active_entries(), 1);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].player_id, 5);
	XVT_ASSERT_TRUE(strcmp(g_mp_roster[0].name, "Five") == 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[1].player_id, 7);
	XVT_ASSERT_INT_EQ(g_mp_roster[1].craft_type_override, 4);
	XVT_ASSERT_INT_EQ(g_mp_roster[2].player_id, 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[3].player_id, 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[3].craft_type_override, 0);
	XVT_ASSERT_TRUE(g_mp_roster[1].name[0] == '\0');
	close_world();
}

/* ------------------------------------------------------------------------ */
/* The mission lists and the description text. */

/* mission_setup_count_mission_list_entries skips comment and section lines,
 * counts the three-line entries, leaves out the one the file ends inside, and
 * leaves the stream at its end. */
static void check_count_list_entries(void)
{
	open_world();
	put_text("train/count.lst", "// Missions\n"
				    "1\n"
				    "M1.TIE\n"
				    "First\n"
				    "[Second]\n"
				    "2\n"
				    "m2.tie\n"
				    "Second\n"
				    "// note\n"
				    "3\n"
				    "m3.tie\n");
	xvt_file *stream = file_open("train\\count.lst", "r");
	XVT_ASSERT_TRUE(stream != NULL);
	XVT_ASSERT_INT_EQ(mission_setup_count_mission_list_entries(stream), 2);
	XVT_ASSERT_TRUE(FILE_GETS(g_frontend_scratch_buffer, 256, stream) ==
			NULL);
	file_close(stream);
	close_world();
}

/* In a solo game, training reads rebel.lst for faction 0 and imperial.lst
 * for any other, and melees read mission.lst; in a network game training
 * reads mission.lst too. Each load replaces the list before it. */
static void check_load_list_file_choice(void)
{
	open_world();
	put_text("train/rebel.lst", "1\nreb1.tie\nRebel one\n");
	put_text("train/imperial.lst", "5\nimp1.tie\nImperial one\n");
	put_text("train/mission.lst", "9\nnet1.tie\nNetwork one\n");
	put_text("melee/mission.lst", "3\nmel1.tie\nMelee one\n");
	mission_setup_load_mission_list(MISSION_DIRECTORY_TRAINING_EXERCISES);
	XVT_ASSERT_INT_EQ(g_mission_count, 1);
	XVT_ASSERT_INT_EQ(g_mission_list[0].mission_idx, 1);
	g_pilot_data.current_faction_id = 1;
	mission_setup_load_mission_list(MISSION_DIRECTORY_TRAINING_EXERCISES);
	XVT_ASSERT_INT_EQ(g_mission_list[0].mission_idx, 5);
	mission_setup_load_mission_list(MISSION_DIRECTORY_MELEES);
	XVT_ASSERT_INT_EQ(g_mission_list[0].mission_idx, 3);
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_CLIENT;
	mission_setup_load_mission_list(MISSION_DIRECTORY_TRAINING_EXERCISES);
	XVT_ASSERT_INT_EQ(g_mission_count, 1);
	XVT_ASSERT_INT_EQ(g_mission_list[0].mission_idx, 9);
	close_world();
}

/* Four entries: a plain one before any section, an "&" one, and two "*"
 * campaign entries, in sections Rookie and Veteran. File names are lowercased
 * and lose their marker; "&" is unavailable; a "*" entry is unavailable while
 * the campaign mission with its id has never been flown. */
static const char g_marked_list[] = "// comment line\n"
				    "2\n"
				    "First.TIE\n"
				    "First mission\n"
				    "[Rookie]\n"
				    "3\n"
				    "& second.tie\n"
				    "Second mission\n"
				    "// another comment\n"
				    "4\n"
				    "* 12 34 Camp.tie\n"
				    "Campaign four\n"
				    "[Veteran]\n"
				    "6\n"
				    "* 1 2 camp6.tie\n"
				    "Campaign six\n";

/* Loads the marked list as the melee list of a solo game in which faction 1
 * flew campaign mission 4 and faction 0 flew campaign mission 6. */
static void load_marked_list_solo(void)
{
	open_world();
	put_text("melee/mission.lst", g_marked_list);
	g_pilot_data.current_faction_id = 1;
	g_pilot_data.faction_statistics[1]
		.sp_campaign_missions[3]
		.number_times_flown = 1;
	g_pilot_data.faction_statistics[0]
		.sp_campaign_missions[5]
		.number_times_flown = 1;
	mission_setup_load_mission_list(MISSION_DIRECTORY_MELEES);
}

/* The comment lines are skipped and the four entries read: ids, file names
 * lowercased and without their markers, titles, and the section each sits in,
 * none before the first. */
static void check_load_list_names_and_sections(void)
{
	load_marked_list_solo();
	XVT_ASSERT_INT_EQ(g_mission_count, 4);
	const struct mission_list_entry *entry = &g_mission_list[0];
	XVT_ASSERT_INT_EQ(entry->mission_idx, 2);
	XVT_ASSERT_TRUE(strcmp(entry->file_name, "first.tie") == 0);
	XVT_ASSERT_TRUE(strcmp(entry->description, "First mission") == 0);
	XVT_ASSERT_TRUE(entry->section_name[0] == '\0');
	entry = &g_mission_list[1];
	XVT_ASSERT_INT_EQ(entry->mission_idx, 3);
	XVT_ASSERT_TRUE(strcmp(entry->file_name, "second.tie") == 0);
	XVT_ASSERT_TRUE(strcmp(entry->section_name, "Rookie") == 0);
	entry = &g_mission_list[2];
	XVT_ASSERT_INT_EQ(entry->mission_idx, 4);
	XVT_ASSERT_TRUE(strcmp(entry->file_name, "camp.tie") == 0);
	XVT_ASSERT_TRUE(strcmp(entry->description, "Campaign four") == 0);
	XVT_ASSERT_TRUE(strcmp(entry->section_name, "Rookie") == 0);
	entry = &g_mission_list[3];
	XVT_ASSERT_INT_EQ(entry->mission_idx, 6);
	XVT_ASSERT_TRUE(strcmp(entry->file_name, "camp6.tie") == 0);
	XVT_ASSERT_TRUE(strcmp(entry->section_name, "Veteran") == 0);
	close_world();
}

/* The plain entry is available and the "&" entry is not. In a solo game a
 * "*" entry counts the current faction's flights: faction 1 flew campaign
 * mission 4, so entry 4 is available, and never flew 6, which faction 0 did. */
static void check_load_list_marked_entries_solo(void)
{
	load_marked_list_solo();
	XVT_ASSERT_INT_EQ(g_mission_list[0].is_unavailable, 0);
	XVT_ASSERT_INT_EQ(g_mission_list[1].is_unavailable, 1);
	XVT_ASSERT_INT_EQ(g_mission_list[2].is_unavailable, 0);
	XVT_ASSERT_INT_EQ(g_mission_list[3].is_unavailable, 1);
	close_world();
}

/* Outside a solo game a "*" entry counts network flights by either faction:
 * faction 1 flew campaign mission 6, nobody flew 4. */
static void check_load_list_entries_network(void)
{
	open_world();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	put_text("melee/mission.lst", g_marked_list);
	g_pilot_data.faction_statistics[1]
		.mp_campaign_missions[5]
		.number_times_flown = 1;
	g_pilot_data.faction_statistics[0]
		.sp_campaign_missions[3]
		.number_times_flown = 1;
	mission_setup_load_mission_list(MISSION_DIRECTORY_MELEES);
	XVT_ASSERT_INT_EQ(g_mission_count, 4);
	XVT_ASSERT_INT_EQ(g_mission_list[2].is_unavailable, 1);
	XVT_ASSERT_INT_EQ(g_mission_list[3].is_unavailable, 0);
	close_world();
}

/* Writes a binary mission file: the format word, then filler bytes up to size,
 * the last tail_size of them counting 0, 1, 2 ... */
static void put_versioned_file(const char *path, uint16_t version, size_t size,
			       size_t tail_size)
{
	static uint8_t file[8192];
	XVT_ASSERT_TRUE(size <= sizeof file);
	memset(file, 'f', size);
	memcpy(file, &version, sizeof version);
	for (size_t i = 0; i < tail_size; ++i) {
		file[size - tail_size + i] = (uint8_t)(1 + i % 250);
	}
	xvt_test_add_asset(&g_assets, path);
	xvt_test_write_file(g_assets.asset, path, file, size);
}

static char g_text[4096];

/* Loads the description text of the selected mission of the current type into
 * g_text, first filled with 'x'. */
static void load_text(int mission_id)
{
	g_pilot_data
		.mission_description_ids[g_pilot_data.mission_directory_id] =
		mission_id;
	memset(g_text, 'x', sizeof g_text);
	mission_setup_load_mission_desc_text(g_text);
}

/* A battle's text is its sequence file after the count line and the lines it
 * counts: printable characters and line ends only, at most 4095 of them, then
 * a terminator. */
static void check_desc_text_sequence(void)
{
	static char long_text[5100];
	open_world();
	put_text("battle/mission.lst",
		 "0\nb1.lst\nBattle one\n1\nb2.lst\nBattle two\n");
	put_text("battle/b1.lst", "2\n"
				  "m1.tie\n"
				  "m2.tie\n"
				  "The battle\tbegins.\x01\n"
				  "Second line\n");
	strcpy(long_text, "0\n");
	memset(long_text + 2, 'a', 5000);
	strcpy(long_text + 5002, "\n");
	put_text("battle/b2.lst", long_text);
	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_BATTLES;
	mission_setup_load_mission_list(MISSION_DIRECTORY_BATTLES);
	load_text(0);
	XVT_ASSERT_TRUE(strcmp(g_text, "The battlebegins.\nSecond line\n") ==
			0);
	XVT_ASSERT_INT_EQ(g_text[4095], 0);
	load_text(1);
	XVT_ASSERT_INT_EQ(strlen(g_text), 4095);
	XVT_ASSERT_INT_EQ(g_text[4094], 'a');
	close_world();
}

/* A combat engagement list of mission files of formats 12 (id 7), 14 (id 8),
 * 13 (id 11) and 11 (id 9), and one that does not exist (id 10). */
static void mission_file_world(void)
{
	open_world();
	put_text("combat/rebel.lst", "7\nc12.tie\nTwelve\n"
				     "8\nc14.tie\nFourteen\n"
				     "11\nc13.tie\nThirteen\n"
				     "9\nc11.tie\nEleven\n"
				     "10\nmissing.tie\nMissing\n");
	put_versioned_file("combat/c12.tie", 12, 3000, 1024);
	put_versioned_file("combat/c14.tie", 14, 6000, 4096);
	put_versioned_file("combat/c13.tie", 13, 5000, 4096);
	put_versioned_file("combat/c11.tie", 11, 3000, 1024);
	g_pilot_data.mission_directory_id =
		MISSION_DIRECTORY_COMBAT_ENGAGEMENTS;
	mission_setup_load_mission_list(MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
}

/* A mission file's text is its last 4096 bytes for formats 14 and 13 and its
 * last 1024 for format 12, the last byte replaced by a terminator. */
static void check_desc_text_mission_formats(void)
{
	mission_file_world();
	load_text(7);
	XVT_ASSERT_INT_EQ(g_text[0], 1);
	XVT_ASSERT_INT_EQ(g_text[1022], 1 + 1022 % 250);
	XVT_ASSERT_INT_EQ(g_text[1023], 0);
	XVT_ASSERT_INT_EQ(g_text[1024], 0);
	load_text(8);
	XVT_ASSERT_INT_EQ(g_text[0], 1);
	XVT_ASSERT_INT_EQ(g_text[4094], 1 + 4094 % 250);
	XVT_ASSERT_INT_EQ(g_text[4095], 0);
	load_text(11);
	XVT_ASSERT_INT_EQ(g_text[0], 1);
	XVT_ASSERT_INT_EQ(g_text[4094], 1 + 4094 % 250);
	XVT_ASSERT_INT_EQ(g_text[4095], 0);
	close_world();
}

/* Another format, a file that does not open, or an id the list lacks leave
 * the whole text zeroed. A NULL buffer is ignored. */
static void check_desc_text_empty(void)
{
	mission_file_world();
	load_text(9);
	XVT_ASSERT_INT_EQ(g_text[0], 0);
	XVT_ASSERT_INT_EQ(g_text[4095], 0);
	load_text(10);
	XVT_ASSERT_INT_EQ(g_text[0], 0);
	load_text(77);
	XVT_ASSERT_INT_EQ(g_text[0], 0);
	XVT_ASSERT_INT_EQ(g_text[2000], 0);
	XVT_ASSERT_INT_EQ(g_text[4095], 0);
	mission_setup_load_mission_desc_text(NULL);
	close_world();
}

/* ------------------------------------------------------------------------ */
/* The tournament, battle and campaign sequences. */

/* A solo game on the battle type, battle 0 selected, its list loaded. The
 * battle file lists CE1.TIE and ce2.tie; the combat engagement list holds
 * ce2.tie as entry 0 (id 8) and ce1.tie as entry 1 (id 7), and the combat
 * engagement type's selected mission was 99. Each combat engagement file has
 * player flight groups on teams 0 and 1. */
static void battle_world(const char *battle_file)
{
	open_world();
	put_text("battle/mission.lst", "0\nbattle1.lst\nFirst battle\n");
	put_text("battle/battle1.lst", battle_file);
	put_text("combat/rebel.lst", "8\nce2.tie\nSecond\n7\nce1.tie\nFirst\n");
	put_text("combat/mission.lst",
		 "8\nce2.tie\nSecond\n7\nce1.tie\nFirst\n");
	static struct xvt_flight_group groups[2];
	memset(groups, 0, sizeof groups);
	groups[0].player_number = 1;
	groups[1].player_number = 1;
	groups[1].team = 1;
	put_mission("combat/ce1.tie", groups, 2);
	put_mission("combat/ce2.tie", groups, 2);
	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_BATTLES;
	g_pilot_data
		.mission_description_ids[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS] =
		99;
	g_game_config.battle_length_index = BATTLE_LENGTH_THREE_WINS;
	mission_setup_load_mission_list(MISSION_DIRECTORY_BATTLES);
}

static const char g_two_mission_battle[] = "2\nCE1.TIE\nce2.tie\n";

/* A battle starts at its first mission without random setup: three wins for
 * the length "three wins", ordinal 0, the type moves to combat engagement
 * with its earlier selection saved, and ce1.tie, entry 1 of that list, is the
 * selected mission, its id kept as the battle's current mission. */
static void check_first_battle_mission(void)
{
	battle_world(g_two_mission_battle);
	struct battle_sequence_state *battle =
		&g_pilot_data.battle_sequence_state;
	battle->mission_ordinals[0] = 5;
	XVT_ASSERT_INT_EQ(mission_setup_select_first_sequence_mission(), 1);
	XVT_ASSERT_INT_EQ(battle->victories_needed,
			  BATTLE_LENGTH_THREE_WINS + 2);
	XVT_ASSERT_INT_EQ(battle->mission_ordinals[0], 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_sequence_active, 1);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_directory_id,
			  MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	XVT_ASSERT_INT_EQ(g_pilot_data.saved_mission_description_id, 99);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_description_ids
				  [MISSION_DIRECTORY_COMBAT_ENGAGEMENTS],
			  7);
	XVT_ASSERT_INT_EQ(battle->current_mission_id, 7);
	XVT_ASSERT_INT_EQ(battle->mission_list_indices[0], 1);
	XVT_ASSERT_INT_EQ(g_selected_mission_list_index, 1);
	close_world();
}

/* With random setup the first ordinal is one of the battle's two, and the
 * mission selected is the one on that ordinal's line. */
static void check_first_battle_mission_random(void)
{
	battle_world(g_two_mission_battle);
	g_game_config.random_setup = 1;
	XVT_ASSERT_INT_EQ(mission_setup_select_first_sequence_mission(), 1);
	int ordinal = g_pilot_data.battle_sequence_state.mission_ordinals[0];
	XVT_ASSERT_TRUE(ordinal == 0 || ordinal == 1);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_description_ids
				  [MISSION_DIRECTORY_COMBAT_ENGAGEMENTS],
			  ordinal == 0 ? 7 : 8);
	close_world();
}

/* A tournament keeps its file's count as the melee count and starts the
 * first melee: the type moves to melee, melee2.tie's id is selected and its
 * list index kept, and the battle's current mission is left alone. */
static void check_first_tournament_mission(void)
{
	open_world();
	put_text("tourn/mission.lst", "2\ntourn1.lst\nTourney\n");
	put_text("tourn/tourn1.lst", "3\nmelee2.tie\nmelee1.tie\nmelee3.tie\n");
	put_text("melee/mission.lst", "11\nmelee1.tie\nOne\n"
				      "12\nmelee2.tie\nTwo\n");
	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_TOURNAMENTS;
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_TOURNAMENTS] = 2;
	g_pilot_data.battle_sequence_state.current_mission_id = 55;
	mission_setup_load_mission_list(MISSION_DIRECTORY_TOURNAMENTS);
	XVT_ASSERT_INT_EQ(mission_setup_select_first_sequence_mission(), 1);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.melee_tournament_sequence_state.mission_count, 3);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_directory_id,
			  MISSION_DIRECTORY_MELEES);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.mission_description_ids[MISSION_DIRECTORY_MELEES],
		12);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.battle_sequence_state.mission_list_indices[0], 1);
	XVT_ASSERT_INT_EQ(g_pilot_data.battle_sequence_state.current_mission_id,
			  55);
	close_world();
}

/* A campaign keeps its file's count as its mission count and moves to the
 * training type. */
static void check_first_campaign_mission(void)
{
	open_world();
	put_text("campaign/rebel.lst", "4\ncamp1.lst\nCampaign\n");
	put_text("campaign/camp1.lst", "6\ntrain1.tie\n");
	put_text("train/rebel.lst", "20\ntrain1.tie\nTraining\n");
	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_CAMPAIGNS;
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_CAMPAIGNS] = 4;
	mission_setup_load_mission_list(MISSION_DIRECTORY_CAMPAIGNS);
	XVT_ASSERT_INT_EQ(mission_setup_select_first_sequence_mission(), 1);
	XVT_ASSERT_INT_EQ(g_pilot_data.campaign_sequence_state.mission_count,
			  6);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_directory_id,
			  MISSION_DIRECTORY_TRAINING_EXERCISES);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_description_ids
				  [MISSION_DIRECTORY_TRAINING_EXERCISES],
			  20);
	close_world();
}

/* A sequence file that does not open, is empty, or has an empty first
 * mission line starts nothing: 0, and the sequence is not marked active. */
static void check_first_sequence_refusals(void)
{
	battle_world("");
	XVT_ASSERT_INT_EQ(mission_setup_select_first_sequence_mission(), 0);
	close_world();
	battle_world("2\n\nce2.tie\n");
	XVT_ASSERT_INT_EQ(mission_setup_select_first_sequence_mission(), 0);
	close_world();
	battle_world(g_two_mission_battle);
	strcpy(g_mission_list[0].file_name, "absent.lst");
	XVT_ASSERT_INT_EQ(mission_setup_select_first_sequence_mission(), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_sequence_active, 0);
	close_world();
}

/* Known failure first_sequence_count_zero, issue #142: the function returns 0
 * when the battle's first mission line is empty. A battle file whose count is
 * 0 and that names no mission, with random setup on, divides by zero in the
 * draw instead, which stops the program. */
static void check_first_sequence_count_zero(void)
{
	battle_world("0\n");
	g_game_config.random_setup = 1;
	XVT_ASSERT_INT_EQ(mission_setup_select_first_sequence_mission(), 0);
	close_world();
}

/* Known failure first_sequence_unlisted, issue #143: the selected battle,
 * id 5, is not in the battle list of one entry, so there is no sequence file
 * to open and the function should return 0. It builds the file name from the
 * entry past the list's end instead, and the sanitizer stops the program. */
static void check_first_sequence_unlisted(void)
{
	battle_world(g_two_mission_battle);
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_BATTLES] = 5;
	XVT_ASSERT_INT_EQ(mission_setup_select_first_sequence_mission(), 0);
	close_world();
}

/* A battle goes on to step 1 without random setup: the ordinal is the step,
 * so ce2.tie (id 8, entry 0) is chosen, stored for step 1, and loaded with its
 * two teams. */
static void check_next_battle_sequential(void)
{
	battle_world(g_two_mission_battle);
	struct battle_sequence_state *battle =
		&g_pilot_data.battle_sequence_state;
	battle->current_mission_index = 1;
	battle->mission_results[0] = BATTLE_MISSION_RESULT_REBEL_VICTORY;
	battle->mission_list_indices[1] = 5;
	XVT_ASSERT_INT_EQ(mission_setup_select_next_sequence_mission(), 1);
	XVT_ASSERT_INT_EQ(battle->current_mission_index, 1);
	XVT_ASSERT_INT_EQ(battle->mission_ordinals[1], 1);
	XVT_ASSERT_INT_EQ(battle->current_mission_id, 8);
	XVT_ASSERT_INT_EQ(battle->mission_list_indices[1], 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_sequence_active, 1);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_directory_id,
			  MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	XVT_ASSERT_INT_EQ(g_pilot_data.saved_mission_description_id, 99);
	XVT_ASSERT_INT_EQ(g_frontend_mission.flight_group_count, 2);
	XVT_ASSERT_INT_EQ(g_team_count, 2);
	close_world();
}

/* After a draw the step is flown again: the step goes back from 2 to 1 and
 * its stored ordinal, 0, is reused, so ce1.tie (id 7, entry 1) is chosen. */
static void check_next_battle_after_draw(void)
{
	battle_world(g_two_mission_battle);
	struct battle_sequence_state *battle =
		&g_pilot_data.battle_sequence_state;
	battle->current_mission_index = 2;
	battle->mission_results[1] = BATTLE_MISSION_RESULT_DRAW;
	battle->mission_ordinals[1] = 0;
	XVT_ASSERT_INT_EQ(mission_setup_select_next_sequence_mission(), 1);
	XVT_ASSERT_INT_EQ(battle->current_mission_index, 1);
	XVT_ASSERT_INT_EQ(battle->mission_ordinals[1], 0);
	XVT_ASSERT_INT_EQ(battle->current_mission_id, 7);
	XVT_ASSERT_INT_EQ(battle->mission_list_indices[1], 1);
	close_world();
}

/* With random setup the draw takes an ordinal no earlier step used: step 0
 * flew ordinal 1, so step 1 gets ordinal 0, CE1.TIE (id 7). */
static void check_next_battle_random_unused(void)
{
	battle_world(g_two_mission_battle);
	struct battle_sequence_state *battle =
		&g_pilot_data.battle_sequence_state;
	g_game_config.random_setup = 1;
	g_game_config.random_seed = 7;
	battle->current_mission_index = 1;
	battle->mission_results[0] = BATTLE_MISSION_RESULT_IMPERIAL_VICTORY;
	battle->mission_ordinals[0] = 1;
	XVT_ASSERT_INT_EQ(mission_setup_select_next_sequence_mission(), 1);
	XVT_ASSERT_INT_EQ(battle->mission_ordinals[1], 0);
	XVT_ASSERT_INT_EQ(battle->current_mission_id, 7);
	close_world();
}

/* A solo campaign, campaign 4 selected, whose file lists train1.tie to
 * train3.tie, listed in the training list as ids 20 to 22. */
static void campaign_world(void)
{
	open_world();
	put_text("campaign/rebel.lst", "4\ncamp1.lst\nCampaign\n");
	put_text("campaign/camp1.lst",
		 "3\ntrain1.tie\ntrain2.tie\ntrain3.tie\n");
	put_text("train/rebel.lst", "20\ntrain1.tie\nOne\n"
				    "21\ntrain2.tie\nTwo\n"
				    "22\ntrain3.tie\nThree\n");
	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_CAMPAIGNS;
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_CAMPAIGNS] = 4;
	mission_setup_load_mission_list(MISSION_DIRECTORY_CAMPAIGNS);
}

/* A campaign whose last mission failed flies it again: the index goes back
 * from 2 to 1 and train2.tie is chosen. One that succeeded goes on to
 * train3.tie. Either way the type moves to training. */
static void check_next_campaign_mission(void)
{
	struct campaign_sequence_state *campaign =
		&g_pilot_data.campaign_sequence_state;
	for (int completed = 0; completed < 2; ++completed) {
		campaign_world();
		campaign->current_mission_index = 2;
		campaign->last_mission_completed = completed;
		XVT_ASSERT_INT_EQ(mission_setup_select_next_sequence_mission(),
				  1);
		XVT_ASSERT_INT_EQ(campaign->current_mission_index,
				  completed ? 2 : 1);
		XVT_ASSERT_INT_EQ(g_pilot_data.mission_directory_id,
				  MISSION_DIRECTORY_TRAINING_EXERCISES);
		XVT_ASSERT_INT_EQ(
			g_pilot_data.mission_description_ids
				[MISSION_DIRECTORY_TRAINING_EXERCISES],
			completed ? 22 : 21);
		close_world();
	}
}

/* A failed first mission takes the index below 0, and an ordinal below 0
 * reads no mission line: the count line, still holding its line end, is taken
 * as the file name, which no list entry matches, so the training selection
 * stays as it was. */
static void check_next_campaign_before_first(void)
{
	campaign_world();
	g_pilot_data
		.mission_description_ids[MISSION_DIRECTORY_TRAINING_EXERCISES] =
		21;
	g_pilot_data.campaign_sequence_state.current_mission_index = 0;
	g_pilot_data.campaign_sequence_state.last_mission_completed = 0;
	XVT_ASSERT_INT_EQ(mission_setup_select_next_sequence_mission(), 1);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.campaign_sequence_state.current_mission_index, -1);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_description_ids
				  [MISSION_DIRECTORY_TRAINING_EXERCISES],
			  21);
	close_world();
}

/* A tournament's next melee is the one at its current index: melee3.tie. */
static void check_next_tournament_mission(void)
{
	open_world();
	put_text("tourn/mission.lst", "2\ntourn1.lst\nTourney\n");
	put_text("tourn/tourn1.lst", "3\nmelee1.tie\nmelee2.tie\nmelee3.tie\n");
	put_text("melee/mission.lst", "11\nmelee1.tie\nOne\n"
				      "13\nmelee3.tie\nThree\n");
	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_TOURNAMENTS;
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_TOURNAMENTS] = 2;
	g_pilot_data.melee_tournament_sequence_state.current_mission_index = 2;
	mission_setup_load_mission_list(MISSION_DIRECTORY_TOURNAMENTS);
	XVT_ASSERT_INT_EQ(mission_setup_select_next_sequence_mission(), 1);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_directory_id,
			  MISSION_DIRECTORY_MELEES);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.mission_description_ids[MISSION_DIRECTORY_MELEES],
		13);
	close_world();
}

/* A sequence file that does not open, is empty, or names its mission on an
 * empty line gives 0. */
static void check_next_sequence_refusals(void)
{
	battle_world("");
	g_pilot_data.battle_sequence_state.current_mission_index = 1;
	XVT_ASSERT_INT_EQ(mission_setup_select_next_sequence_mission(), 0);
	close_world();
	battle_world("2\nce1.tie\n\n");
	g_pilot_data.battle_sequence_state.current_mission_index = 1;
	XVT_ASSERT_INT_EQ(mission_setup_select_next_sequence_mission(), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_sequence_active, 0);
	close_world();
	battle_world(g_two_mission_battle);
	strcpy(g_mission_list[0].file_name, "absent.lst");
	g_pilot_data.battle_sequence_state.current_mission_index = 1;
	XVT_ASSERT_INT_EQ(mission_setup_select_next_sequence_mission(), 0);
	close_world();
}

/* Known failure next_sequence_draw_endless, issue #142: the function returns
 * 1, or 0 when it cannot read the mission. With random setup, a battle file of
 * two missions and both already flown at steps 0 and 1, the draw for step 2
 * finds no unused ordinal and never ends. */
static void check_next_sequence_draw_endless(void)
{
	battle_world(g_two_mission_battle);
	struct battle_sequence_state *battle =
		&g_pilot_data.battle_sequence_state;
	g_game_config.random_setup = 1;
	battle->current_mission_index = 2;
	battle->mission_results[1] = BATTLE_MISSION_RESULT_REBEL_VICTORY;
	battle->mission_ordinals[0] = 0;
	battle->mission_ordinals[1] = 1;
	fail_after_seconds(5);
	int result = mission_setup_select_next_sequence_mission();
	XVT_ASSERT_TRUE(result == 0 || result == 1);
	close_world();
}

/* Known failure next_sequence_count_zero, issue #142: a battle file whose
 * count is 0, with random setup on, divides by zero in the draw, which stops
 * the program; the function should return 0, as it does for a file it cannot
 * read a mission from. */
static void check_next_sequence_count_zero(void)
{
	battle_world("0\nce1.tie\n");
	g_game_config.random_setup = 1;
	g_pilot_data.battle_sequence_state.current_mission_index = 1;
	XVT_ASSERT_INT_EQ(mission_setup_select_next_sequence_mission(), 0);
	close_world();
}

/* Known failure next_sequence_unlisted, issue #143: the selected battle, id 5,
 * is not in the battle list, so there is no sequence file to open and the
 * function should return 0. It builds the file name from the entry past the
 * list's end instead, and the sanitizer stops the program. */
static void check_next_sequence_unlisted(void)
{
	battle_world(g_two_mission_battle);
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_BATTLES] = 5;
	g_pilot_data.battle_sequence_state.current_mission_index = 1;
	XVT_ASSERT_INT_EQ(mission_setup_select_next_sequence_mission(), 0);
	close_world();
}

/* ------------------------------------------------------------------------ */
/* Continuing a saved battle or campaign. */

/* The battle world above with the combat engagement type selected, as when a
 * combat engagement sequence starts, and battle 0's saved continuation in the
 * given table: active, step 0 flown (ordinal 0, a Rebel victory) with a score
 * of 77. */
static void saved_battle_world(struct battle_continuation *saved)
{
	battle_world(g_two_mission_battle);
	g_pilot_data.mission_directory_id =
		MISSION_DIRECTORY_COMBAT_ENGAGEMENTS;
	g_game_config.continue_battle_or_campaign = SEQUENCE_CONTINUE;
	saved->is_active = 1;
	saved->sequence_state.mission_results[0] =
		BATTLE_MISSION_RESULT_REBEL_VICTORY;
	saved->sequence_state.cumulative_score = 77;
}

/* A solo game continues an active saved battle: its state comes back, the
 * step goes on from 0 to 1 and that step's mission, ce2.tie (id 8), is
 * chosen; the launch marker is set and the function returns 1. */
static void check_continue_battle_solo(void)
{
	saved_battle_world(&g_pilot_data.sp_battle_continuations[0]);
	XVT_ASSERT_INT_EQ(mission_setup_try_continue_battle(), 1);
	XVT_ASSERT_INT_EQ(g_pilot_data.launch_session_marker, 1);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.battle_sequence_state.current_mission_index, 1);
	XVT_ASSERT_INT_EQ(g_pilot_data.battle_sequence_state.cumulative_score,
			  77);
	XVT_ASSERT_INT_EQ(g_pilot_data.battle_sequence_state.current_mission_id,
			  8);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_directory_id,
			  MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	close_world();
}

/* With Restart chosen the saved battle is not continued: the type goes back
 * to combat engagement, the entry is no longer active, and the function
 * returns 0. */
static void check_continue_battle_solo_restart(void)
{
	saved_battle_world(&g_pilot_data.sp_battle_continuations[0]);
	g_game_config.continue_battle_or_campaign = SEQUENCE_RESTART;
	XVT_ASSERT_INT_EQ(mission_setup_try_continue_battle(), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_directory_id,
			  MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	XVT_ASSERT_INT_EQ(g_pilot_data.sp_battle_continuations[0].is_active, 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.launch_session_marker, 0);
	close_world();
}

/* A network host continues from mp_battle_continuations: with the saved
 * battle there it returns 1; with it in the solo table only, it returns 0 and
 * the type goes back to combat engagement. */
static void check_continue_battle_host(void)
{
	saved_battle_world(&g_pilot_data.mp_battle_continuations[0]);
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	g_front_state.net_is_host = 1;
	XVT_ASSERT_INT_EQ(mission_setup_try_continue_battle(), 1);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.battle_sequence_state.current_mission_index, 1);
	close_world();

	saved_battle_world(&g_pilot_data.sp_battle_continuations[0]);
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	g_front_state.net_is_host = 1;
	XVT_ASSERT_INT_EQ(mission_setup_try_continue_battle(), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_directory_id,
			  MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	close_world();
}

/* A network client whose clock starts at 0 and that has no wait under way. */
static void client_waiting_world(void)
{
	open_world();
	xvt_campaign_task_reset();
	xvt_time_reset();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_CLIENT;
}

/* A client waits for the host's campaign continuation and returns
 * XVT_CAMPAIGN_PENDING meanwhile; after more than 30000 ms it gives up with 0,
 * the type set back to training and its own saved entry (campaign 4 plus 12)
 * left as it was. */
static void check_continue_campaign_client_timeout(void)
{
	client_waiting_world();
	g_pilot_data.mission_directory_id =
		MISSION_DIRECTORY_TRAINING_EXERCISES;
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_CAMPAIGNS] = 4;
	g_pilot_data.mp_campaign_continuations[4 + 12].is_active = 1;
	XVT_ASSERT_INT_EQ(mission_setup_try_continue_campaign(),
			  XVT_CAMPAIGN_PENDING);
	xvt_time_advance_host_clock(30001 * 1000);
	XVT_ASSERT_INT_EQ(mission_setup_try_continue_campaign(), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_directory_id,
			  MISSION_DIRECTORY_TRAINING_EXERCISES);
	XVT_ASSERT_INT_EQ(g_pilot_data.mp_campaign_continuations[16].is_active,
			  1);
	close_world();
}

/* Known failure battle_client_timeout_type, issue #60: the function is entered
 * with the combat engagement type and, when the battle does not continue,
 * leaves it there, as every other path does (try_continue_campaign sets its
 * type outright). A client whose wait for the host's continuation runs out
 * lowers the type without having raised it, to tournament. The function's
 * comment describes the fault. */
static void check_continue_battle_client_timeout(void)
{
	client_waiting_world();
	g_pilot_data.mission_directory_id =
		MISSION_DIRECTORY_COMBAT_ENGAGEMENTS;
	XVT_ASSERT_INT_EQ(mission_setup_try_continue_battle(),
			  XVT_CAMPAIGN_PENDING);
	xvt_time_advance_host_clock(30001 * 1000);
	XVT_ASSERT_INT_EQ(mission_setup_try_continue_battle(), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_directory_id,
			  MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	close_world();
}

/* ------------------------------------------------------------------------ */
/* The loadout lookups. */

enum {
	LOCAL_GROUP = 2,       /* The local player's selected flight group. */
	ROSTER_PLAYER = 4,     /* The g_mp_roster entry of the other player. */
	PLAYER_ID = 42,	       /* That player's id. */
	FIRST_MATCH_GROUP = 5, /* The group of its slot on team 1. */
	LATER_MATCH_GROUP = 6, /* The group of its slot on team 3. */
};

/* The local player's selected flight group is group 2. The player in roster
 * entry 4 holds slot 1 of team 1 (two places, flight group 5) and slot 0 of
 * team 3 (one place, flight group 6); a slot on team 4 past its one place
 * names flight group 7 and is never searched. */
static void loadout_world(void)
{
	open_world();
	g_frontend_mission.flight_group_count = 8;
	g_mission_setup_selected_flight_group_index = LOCAL_GROUP;
	g_mission_setup_selected_warhead_option_index = 0;
	g_mission_setup_selected_beam_option_index = 0;
	g_mission_setup_selected_countermeasure_option_index = 0;
	g_mission_setup_selected_flight_group_craft_option_index = 0;
	g_mission_setup_selected_preset_craft_option_index = 0;
	g_mission_setup_preset_craft_option_count = 0;
	g_mp_roster[ROSTER_PLAYER].player_id = PLAYER_ID;
	g_mp_roster[ROSTER_PLAYER].craft_option_index = -1;
	g_team_player_flight_group_count[1] = 2;
	g_team_player_flight_group_count[3] = 1;
	g_team_player_flight_group_count[4] = 1;
	int (*teams)[8] = g_mission_setup_player_assignments.team_player_ids;
	teams[1][1] = PLAYER_ID;
	g_mission_setup_player_flight_group_indices[1 * 8 + 1] =
		FIRST_MATCH_GROUP;
	teams[3][0] = PLAYER_ID;
	g_mission_setup_player_flight_group_indices[3 * 8 + 0] =
		LATER_MATCH_GROUP;
	teams[4][1] = PLAYER_ID;
	g_mission_setup_player_flight_group_indices[4 * 8 + 1] = 7;
	for (int fg = 0; fg < 8; ++fg) {
		g_frontend_mission.flight_groups[fg].craft_type = 5;
	}
}

static struct xvt_flight_group *group(int fg)
{
	return &g_frontend_mission.flight_groups[fg];
}

/* mission_setup_get_warhead_type for the local choice gives the warhead's name
 * offset through g_warhead_type_map: the group's default warhead for option 0
 * (0 when it has none), else its optional warhead at the option minus 1. */
static void check_warhead_type_local(void)
{
	loadout_world();
	group(LOCAL_GROUP)->warhead = 10;
	group(LOCAL_GROUP)->optional_warheads[1] = 3;
	XVT_ASSERT_INT_EQ(mission_setup_get_warhead_type(-1),
			  g_warhead_type_map[10]);
	g_mission_setup_selected_warhead_option_index = 2;
	XVT_ASSERT_INT_EQ(mission_setup_get_warhead_type(-1),
			  g_warhead_type_map[3]);
	group(LOCAL_GROUP)->optional_warheads[0] = 4;
	g_mission_setup_selected_warhead_option_index = 1;
	XVT_ASSERT_INT_EQ(mission_setup_get_warhead_type(-1),
			  g_warhead_type_map[4]);
	g_mission_setup_selected_warhead_option_index = 0;
	group(LOCAL_GROUP)->warhead = 1;
	XVT_ASSERT_INT_EQ(mission_setup_get_warhead_type(-1),
			  g_warhead_type_map[1]);
	group(LOCAL_GROUP)->warhead = 0;
	XVT_ASSERT_INT_EQ(mission_setup_get_warhead_type(-1), 0);
	close_world();
}

/* For a roster player the group is the one of its last slot found within the
 * teams' places, here flight group 6, and its warhead_option_index chooses as
 * the local option does. */
static void check_warhead_type_roster(void)
{
	loadout_world();
	group(FIRST_MATCH_GROUP)->warhead = 4;
	group(LATER_MATCH_GROUP)->warhead = 5;
	group(LATER_MATCH_GROUP)->optional_warheads[1] = 9;
	group(7)->warhead = 6;
	XVT_ASSERT_INT_EQ(mission_setup_get_warhead_type(ROSTER_PLAYER),
			  g_warhead_type_map[5]);
	g_mp_roster[ROSTER_PLAYER].warhead_option_index = 2;
	XVT_ASSERT_INT_EQ(mission_setup_get_warhead_type(ROSTER_PLAYER),
			  g_warhead_type_map[9]);
	g_mp_roster[ROSTER_PLAYER].warhead_option_index = 0;
	group(LATER_MATCH_GROUP)->warhead = 0;
	XVT_ASSERT_INT_EQ(mission_setup_get_warhead_type(ROSTER_PLAYER), 0);
	close_world();
}

/* mission_setup_get_beam_type for the local choice gives the beam code:
 * always 0 for craft types 1 to 4 and 14, else the group's default beam for
 * option 0, or its optional beam at the option minus 1. */
static void check_beam_type_local(void)
{
	loadout_world();
	group(LOCAL_GROUP)->beam = 1;
	group(LOCAL_GROUP)->optional_beams[0] = 3;
	XVT_ASSERT_INT_EQ(mission_setup_get_beam_type(-1), 1);
	group(LOCAL_GROUP)->craft_type = 0;
	XVT_ASSERT_INT_EQ(mission_setup_get_beam_type(-1), 1);
	g_mission_setup_selected_beam_option_index = 1;
	XVT_ASSERT_INT_EQ(mission_setup_get_beam_type(-1), 3);
	static const int beamless[] = {1, 4, 14};
	for (size_t i = 0; i < sizeof beamless / sizeof beamless[0]; ++i) {
		group(LOCAL_GROUP)->craft_type = (craft_species)beamless[i];
		XVT_ASSERT_INT_EQ(mission_setup_get_beam_type(-1), 0);
	}
	group(LOCAL_GROUP)->craft_type = 5;
	XVT_ASSERT_INT_EQ(mission_setup_get_beam_type(-1), 3);
	g_mission_setup_selected_beam_option_index = 0;
	group(LOCAL_GROUP)->beam = 0;
	XVT_ASSERT_INT_EQ(mission_setup_get_beam_type(-1), 0);
	close_world();
}

/* For a roster player: the group of its last slot found and its
 * beam_option_index; 0 for a craft type override of 1, 4 or 14, but the beam
 * for an override of 5 and for the group's own craft type 0. */
static void check_beam_type_roster(void)
{
	loadout_world();
	group(LATER_MATCH_GROUP)->beam = 1;
	group(LATER_MATCH_GROUP)->optional_beams[1] = 4;
	group(FIRST_MATCH_GROUP)->beam = 2;
	XVT_ASSERT_INT_EQ(mission_setup_get_beam_type(ROSTER_PLAYER), 1);
	g_mp_roster[ROSTER_PLAYER].beam_option_index = 2;
	XVT_ASSERT_INT_EQ(mission_setup_get_beam_type(ROSTER_PLAYER), 4);
	static const int beamless[] = {1, 4, 14};
	for (size_t i = 0; i < sizeof beamless / sizeof beamless[0]; ++i) {
		g_mp_roster[ROSTER_PLAYER].craft_type_override = beamless[i];
		XVT_ASSERT_INT_EQ(mission_setup_get_beam_type(ROSTER_PLAYER),
				  0);
	}
	g_mp_roster[ROSTER_PLAYER].craft_type_override = 5;
	XVT_ASSERT_INT_EQ(mission_setup_get_beam_type(ROSTER_PLAYER), 4);
	g_mp_roster[ROSTER_PLAYER].craft_type_override = 0;
	group(LATER_MATCH_GROUP)->craft_type = 0;
	XVT_ASSERT_INT_EQ(mission_setup_get_beam_type(ROSTER_PLAYER), 4);
	g_mp_roster[ROSTER_PLAYER].beam_option_index = 0;
	group(LATER_MATCH_GROUP)->beam = 0;
	XVT_ASSERT_INT_EQ(mission_setup_get_beam_type(ROSTER_PLAYER), 0);
	close_world();
}

/* mission_setup_get_countermeasure_type for the local choice gives the group's
 * default countermeasure for option 0 (0 for none), else its optional one at
 * the option minus 1. */
static void check_countermeasure_type_local(void)
{
	loadout_world();
	group(LOCAL_GROUP)->countermeasures = 1;
	group(LOCAL_GROUP)->optional_countermeasures[2] = 2;
	XVT_ASSERT_INT_EQ(mission_setup_get_countermeasure_type(-1), 1);
	g_mission_setup_selected_countermeasure_option_index = 3;
	XVT_ASSERT_INT_EQ(mission_setup_get_countermeasure_type(-1), 2);
	g_mission_setup_selected_countermeasure_option_index = 0;
	group(LOCAL_GROUP)->countermeasures = 0;
	XVT_ASSERT_INT_EQ(mission_setup_get_countermeasure_type(-1), 0);
	close_world();
}

/* For a roster player: the group of its last slot found and its
 * countermeasure_option_index. */
static void check_countermeasure_type_roster(void)
{
	loadout_world();
	group(LATER_MATCH_GROUP)->countermeasures = 1;
	group(LATER_MATCH_GROUP)->optional_countermeasures[0] = 2;
	group(FIRST_MATCH_GROUP)->countermeasures = 3;
	XVT_ASSERT_INT_EQ(mission_setup_get_countermeasure_type(ROSTER_PLAYER),
			  1);
	g_mp_roster[ROSTER_PLAYER].countermeasure_option_index = 1;
	XVT_ASSERT_INT_EQ(mission_setup_get_countermeasure_type(ROSTER_PLAYER),
			  2);
	g_mp_roster[ROSTER_PLAYER].countermeasure_option_index = 0;
	group(LATER_MATCH_GROUP)->countermeasures = 0;
	XVT_ASSERT_INT_EQ(mission_setup_get_countermeasure_type(ROSTER_PLAYER),
			  0);
	close_world();
}

/* mission_setup_get_craft_type for the local choice: without presets, the
 * group's craft for option 0, else its optional craft at the option minus 1.
 * With presets, the group's craft for option 0; g_preset_craft_types at the
 * option for categories 1 and 2 and at the option plus 5 for category 3; the
 * option itself for any other category. */
static void check_craft_type_local(void)
{
	loadout_world();
	group(LOCAL_GROUP)->craft_type = 9;
	group(LOCAL_GROUP)->optional_craft[1] = 11;
	XVT_ASSERT_INT_EQ(mission_setup_get_craft_type(-1), 9);
	g_mission_setup_selected_flight_group_craft_option_index = 2;
	XVT_ASSERT_INT_EQ(mission_setup_get_craft_type(-1), 11);

	g_mission_setup_preset_craft_option_count = 6;
	XVT_ASSERT_INT_EQ(mission_setup_get_craft_type(-1), 9);
	g_mission_setup_selected_preset_craft_option_index = 3;
	group(LOCAL_GROUP)->optional_craft_category = 1;
	XVT_ASSERT_INT_EQ(mission_setup_get_craft_type(-1),
			  g_preset_craft_types[3]);
	group(LOCAL_GROUP)->optional_craft_category = 2;
	g_mission_setup_selected_preset_craft_option_index = 4;
	XVT_ASSERT_INT_EQ(mission_setup_get_craft_type(-1),
			  g_preset_craft_types[4]);
	group(LOCAL_GROUP)->optional_craft_category = 3;
	g_mission_setup_selected_preset_craft_option_index = 2;
	XVT_ASSERT_INT_EQ(mission_setup_get_craft_type(-1),
			  g_preset_craft_types[2 + 5]);
	group(LOCAL_GROUP)->optional_craft_category = 4;
	XVT_ASSERT_INT_EQ(mission_setup_get_craft_type(-1), 2);
	close_world();
}

/* mission_setup_get_craft_type for a roster player: the craft type override
 * when nonzero, else the group's optional craft at craft_option_index when
 * that is 0 to 9, else (-1 or 10) the group's own craft. */
static void check_craft_type_roster(void)
{
	loadout_world();
	group(LATER_MATCH_GROUP)->craft_type = 8;
	group(LATER_MATCH_GROUP)->optional_craft[0] = 12;
	group(LATER_MATCH_GROUP)->optional_craft[9] = 13;
	group(FIRST_MATCH_GROUP)->craft_type = 3;
	XVT_ASSERT_INT_EQ(mission_setup_get_craft_type(ROSTER_PLAYER), 8);
	g_mp_roster[ROSTER_PLAYER].craft_option_index = 0;
	XVT_ASSERT_INT_EQ(mission_setup_get_craft_type(ROSTER_PLAYER), 12);
	g_mp_roster[ROSTER_PLAYER].craft_option_index = 9;
	XVT_ASSERT_INT_EQ(mission_setup_get_craft_type(ROSTER_PLAYER), 13);
	g_mp_roster[ROSTER_PLAYER].craft_option_index = 10;
	XVT_ASSERT_INT_EQ(mission_setup_get_craft_type(ROSTER_PLAYER), 8);
	g_mp_roster[ROSTER_PLAYER].craft_type_override = 15;
	XVT_ASSERT_INT_EQ(mission_setup_get_craft_type(ROSTER_PLAYER), 15);
	close_world();
}

/* ------------------------------------------------------------------------ */
/* The network campaign's background. */

/* A network game whose campaign list names c1.lst to c5.lst. c1's first
 * mission has its first player flight group (the second group) on IFF 0;
 * c2's has its first player group on IFF 1, a later one on IFF 0; c3.lst does
 * not exist; c4.lst has no mission line; c5.lst names a mission file that does
 * not exist. */
static void campaign_background_world(void)
{
	open_world();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	put_text("campaign/mission.lst", "1\nc1.lst\nOne\n"
					 "2\nc2.lst\nTwo\n"
					 "3\nc3.lst\nThree\n"
					 "4\nc4.lst\nFour\n"
					 "5\nc5.lst\nFive\n");
	put_text("campaign/c1.lst", "2\ntrain1.tie\n");
	put_text("campaign/c2.lst", "2\ntrain2.tie\n");
	put_text("campaign/c4.lst", "2\n");
	put_text("campaign/c5.lst", "2\nmissing.tie\n");
	static struct xvt_flight_group groups[2];
	memset(groups, 0, sizeof groups);
	groups[0].iff = 1;
	groups[1].player_number = 1;
	groups[1].iff = 0;
	put_mission("train/train1.tie", groups, 2);
	groups[0].player_number = 1;
	put_mission("train/train2.tie", groups, 2);
	mission_setup_load_mission_list(MISSION_DIRECTORY_CAMPAIGNS);
}

/* mission_setup_use_rebel_background returns 1 when the first player flight
 * group of the campaign's first mission is on IFF 0 and 0 when it is not; 1
 * when the campaign file does not open or has no mission line. */
static void check_rebel_background(void)
{
	campaign_background_world();
	g_selected_mission_list_index = 0;
	XVT_ASSERT_INT_EQ(mission_setup_use_rebel_background(), 1);
	g_selected_mission_list_index = 1;
	XVT_ASSERT_INT_EQ(mission_setup_use_rebel_background(), 0);
	g_selected_mission_list_index = 2;
	XVT_ASSERT_INT_EQ(mission_setup_use_rebel_background(), 1);
	g_selected_mission_list_index = 3;
	XVT_ASSERT_INT_EQ(mission_setup_use_rebel_background(), 1);
	close_world();
}

/* Writes the byte over the stack the next call from the same caller will use
 * for its locals. */
static void fill_stack(unsigned char byte)
{
	volatile unsigned char area[128 * 1024];
	for (size_t i = 0; i < sizeof area; ++i) {
		area[i] = byte;
	}
}

/* Known failure rebel_background_unread_mission, issue #63: the function
 * returns 1 when it cannot read the campaign's file, the Rebel background
 * being its fallback. When the campaign's first mission file does not exist,
 * it reads the flight groups of a copy the loader never wrote. The stack is
 * first filled with 1s, so that copy shows a player group on IFF 1 and the
 * function returns 0. */
static void check_rebel_background_unread_mission(void)
{
	campaign_background_world();
	g_selected_mission_list_index = 4;
	fill_stack(1);
	XVT_ASSERT_INT_EQ(mission_setup_use_rebel_background(), 1);
	close_world();
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
			{"ship_list_over_100", check_ship_list_over_100},
			{"ship_list_negative_type",
			 check_ship_list_negative_type},
			{"team_past_count_table",
			 check_team_counts_team_out_of_range},
			{"team_gap_hidden", check_team_gap_captain},
			{"auto_assign_no_teams",
			 check_randomize_teams_without_teams},
			{"auto_assign_too_few_places",
			 check_randomize_teams_too_few_places},
			{"departed_assignment_kept",
			 check_prune_departed_small_lobby},
			{"first_sequence_count_zero",
			 check_first_sequence_count_zero},
			{"first_sequence_unlisted",
			 check_first_sequence_unlisted},
			{"next_sequence_draw_endless",
			 check_next_sequence_draw_endless},
			{"next_sequence_count_zero",
			 check_next_sequence_count_zero},
			{"next_sequence_unlisted",
			 check_next_sequence_unlisted},
			{"rebel_background_unread_mission",
			 check_rebel_background_unread_mission},
			{"battle_client_timeout_type",
			 check_continue_battle_client_timeout},
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
	/* The main run takes a second or two; a call that never returns fails it
	 * instead of holding it up. */
	fail_after_seconds(30);
	check_ship_list_keeps_models();
	check_ship_list_name_cut();
	check_ship_list_once_and_missing();
	check_team_counts();
	check_team_assignment_valid();
	check_team_assignment_valid_ready_count();
	check_prune_team_assignments();
	check_clear_team_assignments();
	check_randomize_teams_solo();
	check_randomize_teams_network();
	check_prune_departed_team_slots();
	check_prune_departed_flight_groups();
	check_prune_departed_pilot_entries();
	check_prune_departed_full_lobby();
	check_roster_compact();
	check_count_list_entries();
	check_load_list_file_choice();
	check_load_list_names_and_sections();
	check_load_list_marked_entries_solo();
	check_load_list_entries_network();
	check_desc_text_sequence();
	check_desc_text_mission_formats();
	check_desc_text_empty();
	check_first_battle_mission();
	check_first_battle_mission_random();
	check_first_tournament_mission();
	check_first_campaign_mission();
	check_first_sequence_refusals();
	check_next_battle_sequential();
	check_next_battle_after_draw();
	check_next_battle_random_unused();
	check_next_campaign_mission();
	check_next_campaign_before_first();
	check_next_tournament_mission();
	check_next_sequence_refusals();
	check_continue_battle_solo();
	check_continue_battle_solo_restart();
	check_continue_battle_host();
	check_continue_campaign_client_timeout();
	check_warhead_type_local();
	check_warhead_type_roster();
	check_beam_type_local();
	check_beam_type_roster();
	check_countermeasure_type_local();
	check_countermeasure_type_roster();
	check_craft_type_local();
	check_craft_type_roster();
	check_rebel_background();
	return 0;
}
