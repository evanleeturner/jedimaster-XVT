/* Tests for xvt/frontend/mission_briefing.c, the craft selection screen:
 * its ready test, its briefing map hooks, its exit and its frame,
 * mission_briefing_craft_selection_update. The ready checks set the session's
 * players and the roster's ready flags in the game's own tables. The map
 * checks run on a frontend display with no window (test_frontend_display.h).
 *
 * The frame checks run the screen on that display, in a temporary asset
 * folder (test_asset_folder.h) holding the mission lists, the ship list and a
 * string table this file writes: string n reads "s<n>", so the screen's text
 * choices can be read back. The folder has no images and no craft models;
 * opening them fails, and the screen logs the file it asked for and goes on.
 * No DirectPlay session is open, so nothing is sent: a packet the screen
 * sends is read back from g_frontend_net_packet_scratch, and a packet it
 * receives is put in the receive queue as the network code would. The checks
 * read what the screen leaves in the game's globals, the screen stack, the
 * front end's scratch buffer and the lines it logs, kept by a log sink with
 * DEBUG lines let through. No game data is read.
 *
 * POSIX only, for the temporary folder and for the alarm that stops a check
 * whose call does not return. */
#define _POSIX_C_SOURCE 200809L

#include <SDL3/SDL_log.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_assert.h"
#include "test_asset_folder.h"
#include "test_frontend_display.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_script.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_flight.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/mission_briefing.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/net/net_reliable.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/timing/host_clock.h"

/* A session of four players, of whom those named ready are marked ready,
 * and roster ready flags set for the entries named flagged. */
static void ready_world(int session_mode, const int *ready, int ready_count,
			const int *flagged, int flagged_count)
{
	g_frontend_mission_session_mode = session_mode;
	memset(g_front_state.net_players, 0, sizeof g_front_state.net_players);
	g_front_state.net_player_count = 4;
	for (int i = 0; i < 4; ++i) {
		g_front_state.net_players[i].player_id = 0x41 + i;
	}
	for (int i = 0; i < ready_count; ++i) {
		g_front_state.net_players[ready[i]].ready_flag = 1;
	}
	memset(g_mp_roster_ready_flags, 0, sizeof g_mp_roster_ready_flags);
	for (int i = 0; i < flagged_count; ++i) {
		g_mp_roster_ready_flags[flagged[i]] = 1;
	}
}

/* In network play all players are ready when as many roster ready flags are
 * set as session players are marked ready, whichever players they are; in
 * single player never. */
static void check_all_players_ready(void)
{
	static const int two[2] = {0, 1};
	static const int other_two[2] = {2, 7};
	static const int one[1] = {0};
	ready_world(FRONTEND_MISSION_SESSION_NET_HOST, two, 2, two, 2);
	XVT_ASSERT_INT_EQ(mission_briefing_are_all_network_players_ready(), 1);
	ready_world(FRONTEND_MISSION_SESSION_NET_CLIENT, two, 2, other_two, 2);
	XVT_ASSERT_INT_EQ(mission_briefing_are_all_network_players_ready(), 1);
	ready_world(FRONTEND_MISSION_SESSION_NET_HOST, two, 2, one, 1);
	XVT_ASSERT_INT_EQ(mission_briefing_are_all_network_players_ready(), 0);
	ready_world(FRONTEND_MISSION_SESSION_NET_HOST, one, 1, two, 2);
	XVT_ASSERT_INT_EQ(mission_briefing_are_all_network_players_ready(), 0);
	ready_world(FRONTEND_MISSION_SESSION_SINGLEPLAYER, two, 2, two, 2);
	XVT_ASSERT_INT_EQ(mission_briefing_are_all_network_players_ready(), 0);
	ready_world(FRONTEND_MISSION_SESSION_NET_HOST, NULL, 0, NULL, 0);
	XVT_ASSERT_INT_EQ(mission_briefing_are_all_network_players_ready(), 1);
	memset(g_front_state.net_players, 0, sizeof g_front_state.net_players);
	g_front_state.net_player_count = 0;
}

/* The map at zoom 32 centered on (0, 0) in the 360 by 236 panel, with the
 * points 14 of flight group 0 at (0, 0), drawn at (180, 118), of group 1 at
 * (512, 0), drawn at (244, 118), and of group 2 at (0, 512), drawn at
 * (180, 182). */
static void map_world(void)
{
	static const struct RECT panel = {0, 0, 360, 236};
	g_briefing_map_panel_rect = panel;
	g_briefing_map_center.x = 0;
	g_briefing_map_center.y = 0;
	g_briefing_map_target_center = g_briefing_map_center;
	g_briefing_map_scale.x = 32;
	g_briefing_map_scale.y = 32;
	g_briefing_map_target_scale = g_briefing_map_scale;
	memset(g_briefing_map_fg_marker_active, 0,
	       sizeof g_briefing_map_fg_marker_active);
	memset(g_briefing_map_label_active, 0,
	       sizeof g_briefing_map_label_active);
	memset(&g_frontend_mission, 0, sizeof g_frontend_mission);
	g_frontend_mission.flight_group_count = 3;
	for (int fg = 0; fg < 3; ++fg) {
		g_frontend_mission.flight_groups[fg].mission_point_enabled[14] =
			1;
	}
	g_frontend_mission.flight_groups[1].mission_point_x[14] = 512;
	g_frontend_mission.flight_groups[2].mission_point_y[14] = 512;
	g_active_briefing_index = 0;
}

/* Picks with the cursor at (x, y) and returns the group picked. */
static int pick_at(int x, int y)
{
	static const struct RECT viewport = {0, 0, 640, 480};
	g_briefing_selected_mission_point14_flight_group_idx = 9;
	XVT_ASSERT_INT_EQ(
		mission_briefing_handle_map_mouse_input(
			&viewport, &viewport, 0, 1, 0, (int16_t)x, (int16_t)y),
		1);
	return g_briefing_selected_mission_point14_flight_group_idx;
}

/* The map pick follows the cursor moved one pixel up and left, and returns
 * 1; with input suppressed it picks nothing and returns 0. The pick takes
 * the nearer group, the first of two equally near. Moved, the cursor at
 * x 213 lies 32 pixels from groups 0 and 1 and picks group 0; at x 214, 33
 * and 31 pixels away, it picks group 1. Down the column of groups 0 and 2,
 * y 151 picks group 0 and y 152 group 2. */
static void check_map_mouse_input(void)
{
	map_world();
	static const struct RECT viewport = {0, 0, 640, 480};
	g_briefing_selected_mission_point14_flight_group_idx = 5;
	XVT_ASSERT_INT_EQ(mission_briefing_handle_map_mouse_input(
				  &viewport, &viewport, 1, 1, 0, 245, 119),
			  0);
	XVT_ASSERT_INT_EQ(g_briefing_selected_mission_point14_flight_group_idx,
			  5);
	XVT_ASSERT_INT_EQ(pick_at(245, 119), 1);
	XVT_ASSERT_INT_EQ(pick_at(213, 119), 0);
	XVT_ASSERT_INT_EQ(pick_at(214, 119), 1);
	XVT_ASSERT_INT_EQ(pick_at(181, 151), 0);
	XVT_ASSERT_INT_EQ(pick_at(181, 152), 2);
}

/* Drawing the map panel returns 1, and draws it: with text slot 1 showing a
 * new block a page is counted. */
static void check_draw_map_viewport(void)
{
	map_world();
	static char block[320] = "Escort the convoy.";
	g_briefing_text_blocks[4] = block;
	g_briefing_text_slot_active[1] = 1;
	g_briefing_text_slot_block_idx[1] = 4;
	g_briefing_last_narrated_text_block_idx = 0;
	g_briefing_text_page_number = 0;
	static const struct RECT viewport = {0, 0, 640, 480};
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	XVT_ASSERT_INT_EQ(
		mission_briefing_draw_map_viewport(&viewport, &viewport, 3), 1);
	frontend_display_unlock_back_buffer();
	XVT_ASSERT_INT_EQ(g_briefing_text_page_number, 1);
	XVT_ASSERT_INT_EQ(g_briefing_last_narrated_text_block_idx, 4);
	g_briefing_text_slot_active[1] = 0;
	g_briefing_text_blocks[4] = NULL;
}

/* Leaving the screen frees the mission list, the mission text and the ship
 * list, sets each to NULL, marks the screen inactive and returns 0. */
static void check_exit(void)
{
	g_mission_list = calloc(2, sizeof *g_mission_list);
	g_mission_text = calloc(4096, 1);
	g_ship_list = calloc(100, sizeof *g_ship_list);
	XVT_ASSERT_TRUE(g_mission_list != NULL && g_mission_text != NULL &&
			g_ship_list != NULL);
	g_mission_briefing_craft_selection_active = 1;
	XVT_ASSERT_INT_EQ(mission_briefing_craft_selection_exit(1), 0);
	XVT_ASSERT_TRUE(g_mission_list == NULL);
	XVT_ASSERT_TRUE(g_mission_text == NULL);
	XVT_ASSERT_TRUE(g_ship_list == NULL);
	XVT_ASSERT_INT_EQ(g_mission_briefing_craft_selection_active, 0);
	/* With nothing held it still returns 0. */
	XVT_ASSERT_INT_EQ(mission_briefing_craft_selection_exit(0), 0);
}

/* ------------------------------------------------------------------------ */
/* The craft selection screen's frame: the world it runs in. */

enum {
	/* DirectPlay ids of this machine's player and of another player. */
	LOCAL_ID = 0x41,
	PEER_ID = 200,
	/* The pilot's mission: the second entry of every mission list. */
	MISSION_ID = 3,
	SOLO = FRONTEND_MISSION_SESSION_SINGLEPLAYER,
	CLIENT = FRONTEND_MISSION_SESSION_NET_CLIENT,
	HOST = FRONTEND_MISSION_SESSION_NET_HOST,
	/* The rectangles of the Fly (or Ready) and Back buttons. */
	FLY_X = 40,
	FLY_Y = 440,
	BACK_X = 130,
	BACK_Y = 460,
	LINE_CAPACITY = 4096,
	LINE_SIZE = 256,
	STRING_COUNT = 800,
};

static struct xvt_test_assets g_assets;
static char g_lines[LINE_CAPACITY][LINE_SIZE];
static int g_line_count;
static uint8_t g_peer_sequence;
static uint8_t g_local_sequence;

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

/* Keeps each line the engine writes, without Aeron's "xvt: " category
 * prefix. */
static void catch_line(void *userdata, int category, SDL_LogPriority priority,
		       const char *message)
{
	(void)userdata;
	(void)category;
	(void)priority;
	static const char prefix[] = "xvt: ";
	if (g_line_count >= LINE_CAPACITY) {
		fprintf(stderr, "more than %d lines written\n", LINE_CAPACITY);
		exit(1);
	}
	if (!strncmp(message, prefix, sizeof prefix - 1)) {
		message += sizeof prefix - 1;
	}
	snprintf(g_lines[g_line_count++], LINE_SIZE, "%s", message);
}

/* Returns 1 when the kept line starts with start, followed by a space or
 * nothing. */
static int line_starts_with(int index, const char *start)
{
	size_t length = strlen(start);
	const char *line = g_lines[index];
	return strncmp(line, start, length) == 0 &&
	       (line[length] == ' ' || line[length] == '\0');
}

/* The number of kept lines that start with start: an event name, with any
 * of its first values. */
static int count_lines(const char *start)
{
	int count = 0;
	for (int i = 0; i < g_line_count; ++i) {
		if (line_starts_with(i, start)) {
			++count;
		}
	}
	return count;
}

/* The number after " key=" in the last kept line that starts with start;
 * the check fails when there is none. */
static int line_value(const char *start, const char *key)
{
	char field[64];
	snprintf(field, sizeof field, " %s=", key);
	for (int i = g_line_count - 1; i >= 0; --i) {
		if (!line_starts_with(i, start)) {
			continue;
		}
		const char *value = strstr(g_lines[i], field);
		XVT_ASSERT_TRUE(value != NULL);
		return atoi(value + strlen(field));
	}
	fprintf(stderr, "no line starts with %s\n", start);
	exit(1);
}

/* Writes the text to path in the asset folder, path written with '/', making
 * the folders on the way. */
static void put_text(const char *path, const char *text)
{
	xvt_test_add_asset(&g_assets, path);
	xvt_test_write_text(g_assets.asset, path, text);
}

/* Writes the files the screen reads: every mission list it may open, each
 * holding missions 1, 3 and 5; the ship list, naming a model per craft type;
 * and the string table. */
static void write_assets(void)
{
	static const char *const lists[] = {
		"melee/mission.lst",  "tourn/mission.lst",  "train/rebel.lst",
		"train/imperial.lst", "train/mission.lst",  "combat/rebel.lst",
		"combat/mission.lst", "battle/mission.lst",
	};
	static char strings[STRING_COUNT * 8];
	size_t used = 0;
	xvt_test_open_assets(&g_assets);
	for (size_t i = 0; i < sizeof lists / sizeof lists[0]; ++i) {
		put_text(lists[i], "1\nm1.tie\nFirst (one)\n"
				   "3\nm3.tie\nThird (three)\n"
				   "5\nm5.tie\nFifth (five)\n");
	}
	put_text("frontres/frntspec.lst",
		 "xwing.opt 1\nywing.opt 2\nawing.opt 3\nbwing.opt 4\n"
		 "tief.opt 5\ntiei.opt 6\nz95.opt 14\n");
	for (int i = 0; i < STRING_COUNT; ++i) {
		used += (size_t)snprintf(strings + used, sizeof strings - used,
					 "s%d\n", i);
	}
	put_text("strings.txt", strings);
}

/* The craft selection screen about to open on its first frame, in the given
 * session mode, as the host in FRONTEND_MISSION_SESSION_NET_HOST: a fresh
 * frontend display with the test's string table, the screen on top of the
 * stack, no dialog, no packets queued, no log lines kept, the clock at 0.
 * The pilot plays mission 3 of the given mission type, team 0, side 0, at
 * difficulty 0 with craft selection off and nothing else set. The mission
 * has one flight group, "Red", of X-wings (craft type 1) with no choices.
 * Team 0 has two player flight groups, both group 0: this machine's player,
 * in roster entry 0, flies the first and the other player the second. No one
 * is ready, has entered or holds a reservation. */
static void craft_world(int session_mode, int directory)
{
	xvt_dialog_shutdown();
	frontend_string_unload_table();
	xvt_test_close_display();
	memset(&g_front_state, 0, sizeof g_front_state);
	xvt_test_open_display();
	frontend_string_load_table("strings.txt");
	g_front_state.screen_states[0].update_fn =
		mission_briefing_craft_selection_update;
	g_front_state.net_is_host = session_mode == HOST;
	g_front_state.net_host_player_id =
		session_mode == HOST ? LOCAL_ID : PEER_ID;
	g_front_state.net_players[0].player_id = LOCAL_ID;
	g_peer_sequence = 0;
	g_local_sequence = 0;

	free(g_mission_list);
	g_mission_list = NULL;
	g_mission_count = 0;
	g_selected_mission_list_index = -1;
	free(g_ship_list);
	g_ship_list = NULL;
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	memset(&g_game_config, 0, sizeof g_game_config);
	memset(&g_frontend_mission, 0, sizeof g_frontend_mission);
	memset(g_mp_roster, 0, sizeof g_mp_roster);
	memset(g_mp_roster_ready_flags, 0, sizeof g_mp_roster_ready_flags);
	memset(&g_mission_setup_player_assignments, 0,
	       sizeof g_mission_setup_player_assignments);
	memset(g_mission_setup_player_flight_group_indices, 0,
	       sizeof g_mission_setup_player_flight_group_indices);
	memset(g_team_player_flight_group_count, 0,
	       sizeof g_team_player_flight_group_count);
	g_team_count = 0;
	g_mission_setup_team_assignment_skipped = 0;
	memset(g_mission_setup_reserved_player_ids, 0,
	       sizeof g_mission_setup_reserved_player_ids);
	g_mission_setup_reserved_player_count = 0;
	g_mission_setup_selected_flight_group_index = 0;
	g_frontend_briefing_entered_count = 0;
	g_frontend_quick_start_launch_flag = 0;
	g_frontend_skip_screen_entry_setup = 0;
	memset(&g_frontend_net_packet_scratch, 0,
	       sizeof g_frontend_net_packet_scratch);
	g_host_cd_available = 0;
	g_mission_briefing_craft_selection_active = 0;

	g_frontend_mission_session_mode = session_mode;
	g_pilot_data.mission_directory_id = directory;
	g_pilot_data.mission_description_ids[directory] = MISSION_ID;
	g_frontend_mission.flight_group_count = 1;
	g_frontend_mission.flight_groups[0].craft_type = 1;
	strcpy(g_frontend_mission.flight_groups[0].name, "Red");
	g_mp_roster[0].player_id = LOCAL_ID;
	g_team_count = 1;
	g_team_player_flight_group_count[0] = 2;
	g_mission_setup_player_assignments.team_player_ids[0][0] = LOCAL_ID;
	g_mission_setup_player_assignments.team_player_ids[0][1] = PEER_ID;
	xvt_time_reset();
	g_line_count = 0;
}

/* Closes the last world's dialog, strings and display. */
static void close_craft_world(void)
{
	xvt_dialog_shutdown();
	frontend_string_unload_table();
	xvt_test_close_display();
	mission_briefing_craft_selection_exit(0);
}

/* Runs one frame and returns its result; a click lasts that frame only. */
static int run_frame(int frame_counter)
{
	int result = mission_briefing_craft_selection_update(frame_counter);
	g_front_state.mouse_left_click_latch = 0;
	return result;
}

/* Puts the cursor at (x, y) and clicks there in the next frame. */
static void click(int x, int y)
{
	g_front_state.mouse_x = x;
	g_front_state.mouse_y = y;
	g_front_state.mouse_left_click_latch = 1;
}

/* The update function of the screen on top of the stack. */
static frontend_screen_update_fn top_screen(void)
{
	return g_front_state.screen_states[g_front_state.screen_stack_top]
		.update_fn;
}

/* Queues a packet of word_count ints from the given player, numbered as the
 * next one expected from that player, for the next frame to read. */
static void queue_words(int sender, const int *words, size_t word_count)
{
	struct net_queued_packet *entry =
		&g_front_state.net_runtime_recv_queue
			 [g_front_state.net_runtime_recv_queue_write_index];
	memset(entry, 0, sizeof *entry);
	entry->direct_play_id = (DPID)sender;
	entry->packet_class = 1;
	entry->sequence_byte =
		sender == LOCAL_ID ? g_local_sequence++ : g_peer_sequence++;
	entry->payload_size = (uint32_t)(word_count * sizeof *words);
	memcpy(entry->payload, words, entry->payload_size);
	++g_front_state.net_runtime_recv_queue_write_index;
	++g_front_state.net_runtime_recv_queue_count;
}

/* Queues a packet of the given type and two values from the other player. */
static void queue_packet(int type, int value0, int value1)
{
	int words[16] = {type, value0, value1};
	queue_words(PEER_ID, words, sizeof words / sizeof words[0]);
}

/* The int at word index of the scratch packet's payload. */
static int scratch_word(int index)
{
	int value;
	memcpy(&value, g_frontend_net_packet_scratch.payload + index * 4,
	       sizeof value);
	return value;
}

/* Gives the flight group a choice of every kind: the 11 preset craft, a
 * second warhead, beam and countermeasure. */
static void offer_every_choice(void)
{
	struct xvt_flight_group *group = &g_frontend_mission.flight_groups[0];
	group->optional_craft_category = 1;
	group->warhead = 1;
	group->optional_warheads[0] = 2;
	group->beam = 1;
	group->optional_beams[0] = 2;
	group->countermeasures = 1;
	group->optional_countermeasures[0] = 2;
}

/* A pilot in the second mission of a melee sequence, flying for the Empire
 * a flight group of X-wings that offers the preset craft: the loadout setup
 * steps the preset choice to option 7, the TIE interceptor (craft type 6). */
static void choose_preset_by_side(void)
{
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.melee_tournament_sequence_state.current_mission_index = 1;
	g_pilot_data.current_faction_id = 1;
	g_frontend_mission.flight_groups[0].optional_craft_category = 1;
}

/* Opens the screen with its first frame, then clicks Back in the next one,
 * which leaves the screen or opens a dialog; either way the frame ends
 * there, and g_frontend_scratch_buffer keeps the last text the frame wrote
 * before Back. Returns that frame's result. */
static int open_and_click_back(int back_frame)
{
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	click(BACK_X, BACK_Y);
	return run_frame(back_frame);
}

/* ------------------------------------------------------------------------ */
/* The first frame: the screen's flags, the melee test and the mission. */

/* On its first frame the screen marks itself current and finds the pilot's
 * mission in the list of its mission type, wherever it is in the list:
 * mission 3 is entry 1, mission 1 entry 0 and mission 5 entry 2. Later frames
 * keep both. */
static void check_first_frame_finds_mission(void)
{
	static const int missions[3] = {1, 3, 5};
	for (int i = 0; i < 3; ++i) {
		craft_world(SOLO, MISSION_DIRECTORY_MELEES);
		g_pilot_data.mission_description_ids[MISSION_DIRECTORY_MELEES] =
			missions[i];
		XVT_ASSERT_INT_EQ(run_frame(0), 0);
		XVT_ASSERT_INT_EQ(g_mission_briefing_craft_selection_active, 1);
		XVT_ASSERT_INT_EQ(g_mission_count, 3);
		XVT_ASSERT_INT_EQ(g_selected_mission_list_index, i);
		XVT_ASSERT_INT_EQ(line_value("briefing.craft_setup", "index"),
				  i);
		XVT_ASSERT_INT_EQ(
			count_lines("briefing.craft_mission_unlisted"), 0);
		XVT_ASSERT_INT_EQ(run_frame(1), 0);
		XVT_ASSERT_INT_EQ(g_mission_briefing_craft_selection_active, 1);
		XVT_ASSERT_INT_EQ(g_selected_mission_list_index, i);
	}
	close_craft_world();
}

/* Sets how many player flight groups each of the first teams has, and how
 * many teams there are. */
static void set_teams(int team_count, int first, int second)
{
	g_team_count = team_count;
	g_team_player_flight_group_count[0] = first;
	g_team_player_flight_group_count[1] = second;
}

/* In a melee where no team has more than one player flight group the flight
 * assignment screen was skipped, so the single-player Back button returns to
 * the team assignment screen. A team of two player groups among the first
 * g_team_count teams, or another mission type, sends it back to the flight
 * assignment screen. */
static void check_back_follows_melee_teams(void)
{
	static const struct {
		int directory;
		int team_count;
		int first;
		int second;
		int skip;
	} cases[] = {
		{MISSION_DIRECTORY_MELEES, 2, 1, 1, 1},
		{MISSION_DIRECTORY_MELEES, 2, 1, 2, 0},
		{MISSION_DIRECTORY_MELEES, 1, 1, 2, 1},
		{MISSION_DIRECTORY_MELEES, 1, 2, 0, 0},
		{MISSION_DIRECTORY_TRAINING_EXERCISES, 2, 1, 1, 0},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		craft_world(SOLO, cases[i].directory);
		set_teams(cases[i].team_count, cases[i].first, cases[i].second);
		XVT_ASSERT_INT_EQ(open_and_click_back(1), 0);
		XVT_ASSERT_INT_EQ(line_value("briefing.craft_setup", "skip"),
				  cases[i].skip);
		if (cases[i].skip != 0) {
			XVT_ASSERT_TRUE(top_screen() ==
					mission_setup_team_assignment_update);
			XVT_ASSERT_INT_EQ(
				count_lines(
					"briefing.craft_left reason=\"back_to_teams\""),
				1);
		} else {
			XVT_ASSERT_TRUE(top_screen() ==
					mission_setup_flight_assignment_update);
			XVT_ASSERT_INT_EQ(
				count_lines(
					"briefing.craft_left reason=\"back_to_flights\""),
				1);
		}
	}
	close_craft_world();
}

/* ------------------------------------------------------------------------ */
/* The first frame: the loadout, the ship list and the preview. */

/* The first frame sets up the loadout of the pilot's flight group, here
 * group 1 of A-wings (craft type 3) with markings 2, loads the ship list and
 * loads the preview from the model the ship list names for that craft type,
 * awing.opt, showing the group's markings. */
static void check_preview_of_selected_craft(void)
{
	craft_world(SOLO, MISSION_DIRECTORY_MELEES);
	g_frontend_mission.flight_group_count = 2;
	g_frontend_mission.flight_groups[1].craft_type = 3;
	g_frontend_mission.flight_groups[1].markings = 2;
	strcpy(g_frontend_mission.flight_groups[1].name, "Blue");
	g_mission_setup_player_flight_group_indices[0] = 1;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	XVT_ASSERT_TRUE(g_ship_list != NULL);
	XVT_ASSERT_INT_EQ(g_mission_setup_selected_flight_group_index, 1);
	XVT_ASSERT_INT_EQ(line_value("briefing.craft_setup", "fg"), 1);
	XVT_ASSERT_INT_EQ(line_value("briefing.craft_setup", "craft"), 3);
	XVT_ASSERT_INT_EQ(line_value("briefing.craft_setup", "markings"), 2);
	XVT_ASSERT_INT_EQ(count_lines("preview.load_failed file=\"awing.opt\""),
			  1);
	XVT_ASSERT_INT_EQ(count_lines("preview.load_failed"), 1);
	close_craft_world();
}

/* ------------------------------------------------------------------------ */
/* The first frame: quick start. */

/* A single-player quick start does what the Fly button does, on the first
 * frame: it fills roster entry 0 from the selections, marks it ready and goes
 * to flight_loading_update_ready_screen, returning 0 before the background
 * is picked. With preset option 0 the craft override is 0, and the option
 * index is the flight group craft option minus 1, -1. */
static void check_quick_start(void)
{
	craft_world(SOLO, MISSION_DIRECTORY_MELEES);
	g_frontend_quick_start_launch_flag = 1;
	g_pilot_data.rating = 4;
	g_mp_roster[0].craft_type_override = 9;
	g_mp_roster[0].warhead_option_index = 9;
	g_mp_roster[0].beam_option_index = 9;
	g_mp_roster[0].countermeasure_option_index = 9;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	XVT_ASSERT_TRUE(top_screen() == flight_loading_update_ready_screen);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].craft_type_override, 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].pilot_rating, 4);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].warhead_option_index, 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].beam_option_index, 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].countermeasure_option_index, 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].craft_option_index, -1);
	XVT_ASSERT_INT_EQ(g_mp_roster_ready_flags[0], 1);
	XVT_ASSERT_INT_EQ(count_lines("briefing.craft_quick_start"), 1);
	XVT_ASSERT_INT_EQ(count_lines("briefing.craft_opened"), 0);

	/* With a preset chosen, the override is the preset's craft: the TIE
	 * interceptor, type 6. */
	craft_world(SOLO, MISSION_DIRECTORY_MELEES);
	choose_preset_by_side();
	g_frontend_quick_start_launch_flag = 1;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].craft_type_override, 6);
	XVT_ASSERT_INT_EQ(line_value("briefing.craft_quick_start", "craft"), 6);

	/* A network client opens the screen as usual. */
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	g_frontend_quick_start_launch_flag = 1;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	XVT_ASSERT_TRUE(top_screen() ==
			mission_briefing_craft_selection_update);
	XVT_ASSERT_INT_EQ(g_mp_roster_ready_flags[0], 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.craft_quick_start"), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.craft_opened"), 1);
	close_craft_world();
}

/* ------------------------------------------------------------------------ */
/* The first frame: the background. */

/* The background, and g_mission_briefing_craft_screen_faction, follow the
 * craft in a single-player melee or tournament (the Rebel craft are types 1
 * to 4 and 14), the pilot's faction in other single-player missions, the
 * team in network combat engagements and battles, and the craft in other
 * network missions. Single player loads craftsr.bmp or craftsi.bmp, network
 * play craftmr.bmp or craftmi.bmp. */
static void check_background_by_side(void)
{
	static const struct {
		int mode;
		int directory;
		int craft;
		int faction;
		int team;
		int imperial;
		const char *file;
	} cases[] = {
		{SOLO, MISSION_DIRECTORY_MELEES, 1, 0, 0, 0, "craftsr"},
		{SOLO, MISSION_DIRECTORY_MELEES, 4, 0, 0, 0, "craftsr"},
		{SOLO, MISSION_DIRECTORY_TOURNAMENTS, 5, 0, 0, 1, "craftsi"},
		{SOLO, MISSION_DIRECTORY_MELEES, 14, 1, 0, 0, "craftsr"},
		{SOLO, MISSION_DIRECTORY_MELEES, 0, 0, 0, 1, "craftsi"},
		{SOLO, MISSION_DIRECTORY_TRAINING_EXERCISES, 5, 0, 0, 0,
		 "craftsr"},
		{SOLO, MISSION_DIRECTORY_TRAINING_EXERCISES, 1, 1, 0, 1,
		 "craftsi"},
		{CLIENT, MISSION_DIRECTORY_COMBAT_ENGAGEMENTS, 1, 0, 0, 1,
		 "craftmi"},
		{CLIENT, MISSION_DIRECTORY_BATTLES, 5, 0, 1, 0, "craftmr"},
		{CLIENT, MISSION_DIRECTORY_MELEES, 4, 0, 0, 0, "craftmr"},
		{CLIENT, MISSION_DIRECTORY_MELEES, 14, 0, 0, 0, "craftmr"},
		{CLIENT, MISSION_DIRECTORY_MELEES, 5, 0, 0, 1, "craftmi"},
		{CLIENT, MISSION_DIRECTORY_MELEES, 0, 0, 0, 1, "craftmi"},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		char line[64];
		craft_world(cases[i].mode, cases[i].directory);
		g_frontend_mission.flight_groups[0].craft_type =
			(craft_species)cases[i].craft;
		g_pilot_data.current_faction_id = cases[i].faction;
		g_pilot_data.team = cases[i].team;
		g_host_cd_available = (int)(i % 2);
		g_mission_briefing_craft_screen_faction =
			cases[i].imperial != 0
				? MISSION_BRIEFING_CRAFT_SCREEN_REBEL
				: MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL;
		XVT_ASSERT_INT_EQ(run_frame(0), 0);
		XVT_ASSERT_INT_EQ(g_mission_briefing_craft_screen_faction,
				  cases[i].imperial);
		snprintf(line, sizeof line,
			 "image.open_failed file=\"frontres\\%s.bmp\"",
			 cases[i].file);
		XVT_ASSERT_INT_EQ(count_lines(line), 1);
		XVT_ASSERT_INT_EQ(
			line_value("briefing.craft_opened", "faction"),
			cases[i].imperial);
	}
	close_craft_world();
}

/* ------------------------------------------------------------------------ */
/* The first frame: the entry packets. */

/* In network play the first frame sends everyone the player's loadout, then
 * NET_PACKET_BRIEFING_ENTERED, which keeps the loadout's eight ints in the
 * scratch packet: the flight group's optional craft category, the preset,
 * craft, warhead, beam and countermeasure choices, the wave count and the
 * craft count. Here the loadout setup chose preset 7. */
static void check_entry_packets(void)
{
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	g_game_config.craft_selection = CRAFT_SELECTION_ON;
	choose_preset_by_side();
	g_frontend_mission.flight_groups[0].number_of_waves = 3;
	g_frontend_mission.flight_groups[0].number_of_craft = 4;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_BRIEFING_ENTERED);
	static const int loadout[8] = {1, 7, 0, 0, 0, 0, 3, 4};
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(scratch_word(i), loadout[i]);
	}
	XVT_ASSERT_INT_EQ(count_lines("briefing.loadout_sent by=\"entry\""), 1);
	XVT_ASSERT_INT_EQ(
		line_value("briefing.loadout_sent by=\"entry\"", "preset"), 7);
	XVT_ASSERT_INT_EQ(line_value("briefing.craft_opened", "mode"), CLIENT);

	/* Single player sends nothing. */
	craft_world(SOLO, MISSION_DIRECTORY_MELEES);
	g_frontend_mission.flight_groups[0].number_of_craft = 4;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type, 0);
	XVT_ASSERT_INT_EQ(scratch_word(7), 0);
	close_craft_world();
}

/* With host-only craft selection a client sends no loadout on entry, only
 * NET_PACKET_BRIEFING_ENTERED, unless it is in a training sequence at
 * GAME_DIFFICULTY_EASY_CHEAT; the host sends it. With craft selection on or
 * off every player sends it. */
static void check_entry_loadout_host_only(void)
{
	static const struct {
		int mode;
		int selection;
		int directory;
		int sequence;
		int difficulty;
		int sent;
	} cases[] = {
		{CLIENT, CRAFT_SELECTION_HOST_ONLY, MISSION_DIRECTORY_MELEES, 0,
		 GAME_DIFFICULTY_EASY, 0},
		{HOST, CRAFT_SELECTION_HOST_ONLY, MISSION_DIRECTORY_MELEES, 0,
		 GAME_DIFFICULTY_EASY, 1},
		{CLIENT, CRAFT_SELECTION_HOST_ONLY,
		 MISSION_DIRECTORY_TRAINING_EXERCISES, 1,
		 GAME_DIFFICULTY_EASY_CHEAT, 1},
		{CLIENT, CRAFT_SELECTION_HOST_ONLY,
		 MISSION_DIRECTORY_TRAINING_EXERCISES, 1,
		 GAME_DIFFICULTY_MEDIUM, 0},
		{CLIENT, CRAFT_SELECTION_HOST_ONLY,
		 MISSION_DIRECTORY_TRAINING_EXERCISES, 0,
		 GAME_DIFFICULTY_EASY_CHEAT, 0},
		{CLIENT, CRAFT_SELECTION_HOST_ONLY, MISSION_DIRECTORY_MELEES, 1,
		 GAME_DIFFICULTY_EASY_CHEAT, 0},
		{CLIENT, CRAFT_SELECTION_OFF, MISSION_DIRECTORY_MELEES, 0,
		 GAME_DIFFICULTY_EASY, 1},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		craft_world(cases[i].mode, cases[i].directory);
		g_game_config.craft_selection =
			(craft_selection_mode)cases[i].selection;
		g_game_config.difficulty = (game_difficulty)cases[i].difficulty;
		g_pilot_data.mission_sequence_active = cases[i].sequence;
		g_frontend_mission.flight_groups[0].number_of_craft = 4;
		/* A ready player not yet entered: no launch, no countdown. */
		g_front_state.net_players[1].ready_flag = 1;
		XVT_ASSERT_INT_EQ(run_frame(0), 0);
		XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
				  NET_PACKET_BRIEFING_ENTERED);
		XVT_ASSERT_INT_EQ(scratch_word(7), cases[i].sent != 0 ? 4 : 0);
		XVT_ASSERT_INT_EQ(
			count_lines("briefing.loadout_sent by=\"entry\""),
			cases[i].sent);
	}
	close_craft_world();
}

/* ------------------------------------------------------------------------ */
/* The title, the craft and armament choices and the header. */

/* Sets the flight group's optional craft category, with one optional craft
 * for category 4, and its armaments: for each kind, 0 gives none, 1 a
 * default only and 2 a default and one optional choice. */
static void offer_choices(int category, int warheads, int beams,
			  int countermeasures)
{
	struct xvt_flight_group *group = &g_frontend_mission.flight_groups[0];
	group->optional_craft_category = (uint8_t)category;
	if (category == 4) {
		group->optional_craft[0] = (craft_species)2;
	}
	group->warhead = warheads != 0;
	group->optional_warheads[0] = warheads == 2 ? 2 : 0;
	group->beam = beams != 0;
	group->optional_beams[0] = beams == 2 ? 2 : 0;
	group->countermeasures = countermeasures != 0;
	group->optional_countermeasures[0] = countermeasures == 2 ? 2 : 0;
}

/* The first frame logs whether the player may configure the craft, and the
 * choices offered: a craft choice when there is more than one flight group
 * craft or preset, and one armament per kind with more than one choice (a
 * default alone is no choice), the
 * beam only for a craft outside types 1 to 5 and 14. In a training sequence
 * below GAME_DIFFICULTY_EASY_CHEAT nothing may be configured. Later frames
 * log nothing. */
static void check_craft_choices(void)
{
	static const struct {
		int craft;
		int category;
		int warheads;
		int beams;
		int countermeasures;
		int sequence;
		int difficulty;
		int allowed;
		int craft_choice;
		int armaments;
	} cases[] = {
		{1, 0, 0, 0, 0, 0, GAME_DIFFICULTY_EASY, 1, 0, 0},
		{6, 0, 1, 1, 1, 0, GAME_DIFFICULTY_EASY, 1, 0, 0},
		{6, 1, 2, 2, 2, 0, GAME_DIFFICULTY_EASY, 1, 1, 3},
		{1, 1, 2, 2, 2, 0, GAME_DIFFICULTY_EASY, 1, 1, 2},
		{5, 2, 0, 2, 0, 0, GAME_DIFFICULTY_EASY, 1, 1, 0},
		{14, 3, 0, 2, 0, 0, GAME_DIFFICULTY_EASY, 1, 1, 0},
		{0, 0, 0, 2, 0, 0, GAME_DIFFICULTY_EASY, 1, 0, 1},
		{1, 4, 0, 0, 0, 0, GAME_DIFFICULTY_EASY, 1, 1, 0},
		{1, 0, 2, 0, 0, 0, GAME_DIFFICULTY_EASY, 1, 0, 1},
		{1, 0, 0, 0, 2, 0, GAME_DIFFICULTY_EASY, 1, 0, 1},
		{6, 1, 2, 2, 2, 1, GAME_DIFFICULTY_MEDIUM, 0, 0, 0},
		{6, 1, 2, 2, 2, 1, GAME_DIFFICULTY_EASY_CHEAT, 1, 1, 3},
		{6, 1, 2, 2, 2, 0, GAME_DIFFICULTY_MEDIUM, 1, 1, 3},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		craft_world(SOLO, MISSION_DIRECTORY_TRAINING_EXERCISES);
		g_frontend_mission.flight_groups[0].craft_type =
			(craft_species)cases[i].craft;
		offer_choices(cases[i].category, cases[i].warheads,
			      cases[i].beams, cases[i].countermeasures);
		g_pilot_data.mission_sequence_active = cases[i].sequence;
		g_game_config.difficulty = (game_difficulty)cases[i].difficulty;
		XVT_ASSERT_INT_EQ(run_frame(0), 0);
		XVT_ASSERT_INT_EQ(count_lines("briefing.craft_choices"), 1);
		XVT_ASSERT_INT_EQ(
			line_value("briefing.craft_choices", "allowed"),
			cases[i].allowed);
		XVT_ASSERT_INT_EQ(
			line_value("briefing.craft_choices", "craft_choice"),
			cases[i].craft_choice);
		XVT_ASSERT_INT_EQ(
			line_value("briefing.craft_choices", "armaments"),
			cases[i].armaments);
		XVT_ASSERT_INT_EQ(run_frame(1), 0);
		XVT_ASSERT_INT_EQ(run_frame(2), 0);
		XVT_ASSERT_INT_EQ(count_lines("briefing.craft_choices"), 1);
	}
	close_craft_world();
}

/* ------------------------------------------------------------------------ */
/* The packets read each frame in network play. */

/* When the host cancels the game a client shuts the session down and opens a
 * dialog saying so, returning what xvt_dialog_continue_with returns, 0; the
 * frame ends there, so the last text it wrote is the header, and the session
 * mode is left to the dialog. */
static void check_host_cancelled_client(void)
{
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	strcpy(g_pilot_data.name, "Luke");
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	queue_packet(NET_PACKET_HOST_CANCELLED, 0, 0);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
	XVT_ASSERT_INT_EQ(g_frontend_mission_session_mode, CLIENT);
	XVT_ASSERT_INT_EQ(count_lines("mission.setup_cancelled"), 1);
	XVT_ASSERT_TRUE(strcmp(g_frontend_scratch_buffer, "s606 s604 \4Red") ==
			0);
	close_craft_world();
}

/* A host that gets the cancel sends itself to the concourse with the session
 * mode NONE, without a dialog. */
static void check_host_cancelled_host(void)
{
	craft_world(HOST, MISSION_DIRECTORY_MELEES);
	g_front_state.net_players[1].ready_flag = 1;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	queue_packet(NET_PACKET_HOST_CANCELLED, 0, 0);
	run_frame(1);
	XVT_ASSERT_INT_EQ(g_frontend_mission_session_mode,
			  FRONTEND_MISSION_SESSION_NONE);
	XVT_ASSERT_TRUE(top_screen() == concourse_update);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
	XVT_ASSERT_INT_EQ(count_lines("mission.setup_cancelled"), 1);
	close_craft_world();
}

/* A lobby state packet prunes the flight assignments, taking out the players
 * the new roster lacks, and resends the loadout: here a client with
 * host-only craft selection, which sent none on entry. */
static void check_state_resends_loadout(void)
{
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	g_game_config.craft_selection = CRAFT_SELECTION_HOST_ONLY;
	g_frontend_mission.flight_groups[0].number_of_waves = 3;
	g_frontend_mission.flight_groups[0].number_of_craft = 4;
	g_mission_setup_player_assignments.assigned_player_ids[0] = LOCAL_ID;
	g_mission_setup_player_assignments.assigned_player_ids[1] = PEER_ID;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	XVT_ASSERT_INT_EQ(scratch_word(7), 0);
	/* The lobby state: one player, this machine's. */
	int words[20] = {NET_PACKET_STATE};
	words[13] = 1;
	words[14] = LOCAL_ID;
	queue_words(PEER_ID, words, 20);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.team_player_ids[0][1], 0);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_CRAFT_LOADOUT);
	XVT_ASSERT_INT_EQ(scratch_word(6), 3);
	XVT_ASSERT_INT_EQ(scratch_word(7), 4);
	XVT_ASSERT_INT_EQ(count_lines("briefing.loadout_sent by=\"state\""), 1);
	close_craft_world();
}

/* With host-only craft selection the host resends its loadout when another
 * player enters the screen, but not for its own entry, which comes back to
 * it. Without host-only selection, or on a client, nothing is resent. */
static void check_entered_resends_loadout(void)
{
	static const struct {
		int mode;
		int selection;
		int sender;
		int resent;
	} cases[] = {
		{HOST, CRAFT_SELECTION_HOST_ONLY, PEER_ID, 1},
		{HOST, CRAFT_SELECTION_HOST_ONLY, LOCAL_ID, 0},
		{HOST, CRAFT_SELECTION_ON, PEER_ID, 0},
		{CLIENT, CRAFT_SELECTION_HOST_ONLY, PEER_ID, 0},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		craft_world(cases[i].mode, MISSION_DIRECTORY_MELEES);
		g_game_config.craft_selection =
			(craft_selection_mode)cases[i].selection;
		g_frontend_mission.flight_groups[0].number_of_craft = 4;
		g_front_state.net_players[1].ready_flag = 1;
		g_front_state.net_players[2].ready_flag = 1;
		XVT_ASSERT_INT_EQ(run_frame(0), 0);
		memset(&g_frontend_net_packet_scratch, 0,
		       sizeof g_frontend_net_packet_scratch);
		int words[2] = {NET_PACKET_BRIEFING_ENTERED};
		queue_words(cases[i].sender, words, 2);
		XVT_ASSERT_INT_EQ(run_frame(1), 0);
		XVT_ASSERT_INT_EQ(g_frontend_briefing_entered_count, 1);
		XVT_ASSERT_INT_EQ(
			count_lines(
				"briefing.loadout_sent by=\"player_entered\""),
			cases[i].resent);
		XVT_ASSERT_INT_EQ(
			g_frontend_net_packet_scratch.packet_type,
			cases[i].resent != 0 ? NET_PACKET_CRAFT_LOADOUT : 0);
		XVT_ASSERT_INT_EQ(scratch_word(7),
				  cases[i].resent != 0 ? 4 : 0);
	}
	close_craft_world();
}

/* A return-to-setup packet takes the player back to mission_setup_update,
 * skipping its entry setup, and the frame returns 0 there: the last text it
 * wrote is the header, "craft configuration" (263) with a craft choice. */
static void check_return_to_setup(void)
{
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	strcpy(g_pilot_data.name, "Luke");
	offer_choices(1, 0, 0, 0);
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	queue_packet(NET_PACKET_RETURN_TO_SETUP, 0, 0);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_TRUE(top_screen() == mission_setup_update);
	XVT_ASSERT_INT_EQ(g_frontend_skip_screen_entry_setup, 1);
	XVT_ASSERT_INT_EQ(count_lines("mission.setup_returned"), 1);
	XVT_ASSERT_TRUE(strcmp(g_frontend_scratch_buffer, "s263 s604 \4Red") ==
			0);
	close_craft_world();
}

/* The launch packet starts the flight: the player goes to
 * flight_loading_update_ready_screen, and the frame returns 0 there. */
static void check_launch_packet(void)
{
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	strcpy(g_pilot_data.name, "Luke");
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	int words[64] = {NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS};
	queue_words(PEER_ID, words, 64);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_TRUE(top_screen() == flight_loading_update_ready_screen);
	XVT_ASSERT_INT_EQ(count_lines("briefing.launch_received"), 1);
	XVT_ASSERT_INT_EQ(line_value("briefing.launch_received", "frame"), 1);
	XVT_ASSERT_INT_EQ(line_value("briefing.launch_received", "ms"), 60000);
	XVT_ASSERT_TRUE(strcmp(g_frontend_scratch_buffer, "s606 s604 \4Red") ==
			0);
	close_craft_world();
}

/* With host-only craft selection a client's preview follows the host's
 * loadout packet: preset option 3 of the preset craft is the Y-wing (craft
 * type 2), whose model is ywing.opt. The host, or a client without host-only
 * selection, loads no new preview. */
static void check_loadout_packet_preview(void)
{
	static const struct {
		int mode;
		int selection;
		int loaded;
	} cases[] = {
		{CLIENT, CRAFT_SELECTION_HOST_ONLY, 1},
		{HOST, CRAFT_SELECTION_HOST_ONLY, 0},
		{CLIENT, CRAFT_SELECTION_ON, 0},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		craft_world(cases[i].mode, MISSION_DIRECTORY_MELEES);
		g_game_config.craft_selection =
			(craft_selection_mode)cases[i].selection;
		g_frontend_mission.flight_groups[0].optional_craft_category = 1;
		g_front_state.net_players[1].ready_flag = 1;
		XVT_ASSERT_INT_EQ(run_frame(0), 0);
		int words[9] = {
			NET_PACKET_CRAFT_LOADOUT, 1, 3, 0, 0, 0, 0, 0, 1};
		queue_words(PEER_ID, words, 9);
		XVT_ASSERT_INT_EQ(run_frame(1), 0);
		XVT_ASSERT_INT_EQ(count_lines("briefing.craft_preview"),
				  cases[i].loaded);
		XVT_ASSERT_INT_EQ(
			count_lines("preview.load_failed file=\"ywing.opt\""),
			cases[i].loaded);
		if (cases[i].loaded != 0) {
			XVT_ASSERT_INT_EQ(
				line_value("briefing.craft_preview", "craft"),
				2);
		}
	}
	close_craft_world();
}

/* A countdown packet lower than the countdown replaces it, and the
 * countdown then starts from it; a higher or equal one is ignored. */
static void check_countdown_packet(void)
{
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	g_front_state.net_players[1].ready_flag = 1;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	queue_packet(NET_PACKET_BRIEFING_COUNTDOWN, 70000, 0);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	queue_packet(NET_PACKET_BRIEFING_COUNTDOWN, 60000, 0);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_lowered"), 0);
	queue_packet(NET_PACKET_BRIEFING_COUNTDOWN, 30000, 0);
	XVT_ASSERT_INT_EQ(run_frame(2), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_lowered"), 1);
	XVT_ASSERT_INT_EQ(line_value("briefing.countdown_lowered", "ms"),
			  30000);
	XVT_ASSERT_INT_EQ(line_value("briefing.countdown_lowered", "previous"),
			  60000);
	g_frontend_briefing_entered_count = 1;
	XVT_ASSERT_INT_EQ(run_frame(3), 0);
	XVT_ASSERT_INT_EQ(line_value("briefing.countdown_started", "ms"),
			  30000);
	close_craft_world();
}

/* Sets the reserved player table: count ids from the list. */
static void set_reservations(const int *ids, int count)
{
	memset(g_mission_setup_reserved_player_ids, 0,
	       sizeof g_mission_setup_reserved_player_ids);
	memcpy(g_mission_setup_reserved_player_ids, ids,
	       (size_t)count * sizeof *ids);
	g_mission_setup_reserved_player_count = count;
}

/* Checks the reserved player table against count ids from the list, the
 * other entries 0. */
static void check_reservations(const int *ids, int count)
{
	XVT_ASSERT_INT_EQ(g_mission_setup_reserved_player_count, count);
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_mission_setup_reserved_player_ids[i],
				  i < count ? ids[i] : 0);
	}
}

/* A release takes the player out of the reserved player table, the later
 * entries moving down and the last becoming 0; a release of a player not in
 * the entries in use changes nothing, even when the player is in the entry
 * past them. A release from a full table moves all 8 entries. */
static void check_release_reservation(void)
{
	static const int held[3] = {5, 6, 7};
	static const int after_six[2] = {5, 7};
	static const int after_five[1] = {7};
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	set_reservations(held, 3);
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	queue_packet(NET_PACKET_RELEASE_FLIGHT_RESERVATION, 6, 0);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	check_reservations(after_six, 2);
	XVT_ASSERT_INT_EQ(line_value("mission.setup_reservation", "held"), 0);
	XVT_ASSERT_INT_EQ(line_value("mission.setup_reservation", "reserved"),
			  2);
	queue_packet(NET_PACKET_RELEASE_FLIGHT_RESERVATION, 9, 0);
	XVT_ASSERT_INT_EQ(run_frame(2), 0);
	check_reservations(after_six, 2);
	queue_packet(NET_PACKET_RELEASE_FLIGHT_RESERVATION, 5, 0);
	XVT_ASSERT_INT_EQ(run_frame(3), 0);
	check_reservations(after_five, 1);

	set_reservations(held, 3);
	g_mission_setup_reserved_player_count = 2;
	queue_packet(NET_PACKET_RELEASE_FLIGHT_RESERVATION, 7, 0);
	XVT_ASSERT_INT_EQ(run_frame(4), 0);
	check_reservations(held, 2);

	static const int full[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	static const int after_three[7] = {1, 2, 4, 5, 6, 7, 8};
	set_reservations(full, 8);
	queue_packet(NET_PACKET_RELEASE_FLIGHT_RESERVATION, 3, 0);
	XVT_ASSERT_INT_EQ(run_frame(5), 0);
	check_reservations(after_three, 7);
	close_craft_world();
}

/* A reservation adds the player at the end of the reserved player table
 * unless it is there already; the eighth fills the table without a
 * warning. */
static void check_add_reservation(void)
{
	static const int held[7] = {1, 2, 3, 4, 5, 6, 7};
	static const int full[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	set_reservations(held, 1);
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	queue_packet(NET_PACKET_FLIGHT_RESERVATION, 2, PEER_ID);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	check_reservations(held, 2);
	XVT_ASSERT_INT_EQ(line_value("mission.setup_reservation", "held"), 1);
	XVT_ASSERT_INT_EQ(line_value("mission.setup_reservation", "reserved"),
			  2);
	queue_packet(NET_PACKET_FLIGHT_RESERVATION, 1, PEER_ID);
	XVT_ASSERT_INT_EQ(run_frame(2), 0);
	check_reservations(held, 2);
	XVT_ASSERT_INT_EQ(count_lines("mission.setup_reservation"), 1);
	set_reservations(held, 7);
	queue_packet(NET_PACKET_FLIGHT_RESERVATION, 8, PEER_ID);
	XVT_ASSERT_INT_EQ(run_frame(3), 0);
	check_reservations(full, 8);
	XVT_ASSERT_INT_EQ(count_lines("briefing.reservations_full"), 0);
	close_craft_world();
}

/* A pilot rating packet sets the rating of the sender's roster entry; from a
 * sender not in the roster it changes nothing. */
static void check_pilot_rating(void)
{
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	g_mp_roster[2].player_id = PEER_ID;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	queue_packet(NET_PACKET_PILOT_RATING, 7, 0);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[2].pilot_rating, 7);
	XVT_ASSERT_INT_EQ(line_value("mission.setup_rating_received", "index"),
			  2);

	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	queue_packet(NET_PACKET_PILOT_RATING, 7, 0);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_mp_roster[i].pilot_rating, 0);
	}
	XVT_ASSERT_INT_EQ(count_lines("mission.setup_rating_received"), 0);
	close_craft_world();
}

/* ------------------------------------------------------------------------ */
/* The instruction line, the pilot line and the countdown's start. */

/* Returns 1 when text starts as the instruction line does: "s600 ",
 * "s601 " or "s602 ". */
static int starts_instruction(const char *text)
{
	return strncmp(text, "s600 ", 5) == 0 ||
	       strncmp(text, "s601 ", 5) == 0 || strncmp(text, "s602 ", 5) == 0;
}

/* Under the loadout the screen says what may be chosen, with string n read
 * as "s<n>": "select your" (600) in single player or with craft selection
 * on, "select everybody's" (601) on the host and "host is selecting" (602)
 * on a client with host-only selection, then "craft" (609), "and" (515) and
 * "armaments" (607) as offered. With nothing to choose, or with craft
 * selection off in network play, it draws a fixed text instead and leaves
 * the scratch buffer to the loadout's own text. */
static void check_instruction_line(void)
{
	static const struct {
		int mode;
		int selection;
		int category;
		int warheads;
		const char *text;
	} cases[] = {
		{SOLO, CRAFT_SELECTION_OFF, 1, 2, "s600 s609 s515 s607."},
		{SOLO, CRAFT_SELECTION_OFF, 1, 0, "s600 s609."},
		{SOLO, CRAFT_SELECTION_OFF, 0, 2, "s600 s607."},
		{SOLO, CRAFT_SELECTION_OFF, 0, 0, NULL},
		{CLIENT, CRAFT_SELECTION_ON, 1, 0, "s600 s609."},
		{HOST, CRAFT_SELECTION_HOST_ONLY, 1, 0, "s601 s609."},
		{CLIENT, CRAFT_SELECTION_HOST_ONLY, 1, 0, "s602 s609."},
		{CLIENT, CRAFT_SELECTION_OFF, 1, 0, NULL},
		{CLIENT, CRAFT_SELECTION_ON, 0, 0, NULL},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		craft_world(cases[i].mode, MISSION_DIRECTORY_MELEES);
		g_game_config.craft_selection =
			(craft_selection_mode)cases[i].selection;
		offer_choices(cases[i].category, cases[i].warheads, 0, 0);
		g_front_state.net_players[1].ready_flag = 1;
		XVT_ASSERT_INT_EQ(open_and_click_back(1), 0);
		const char *text = g_frontend_scratch_buffer;
		int matches = cases[i].text != NULL
				      ? strcmp(text, cases[i].text) == 0
				      : !starts_instruction(text);
		if (!matches) {
			fprintf(stderr, "case %zu: got \"%s\"\n", i, text);
			exit(1);
		}
	}
	close_craft_world();
}

/* With a pilot named, even with one letter, the pilot line ends with the
 * side's small emblem, rebtiny<n> or imptiny<n>, n being
 * (frame_counter % 32) / 2: by the craft in training, melees and
 * tournaments, by the team elsewhere (team 0 Imperial). */
static void check_pilot_line(void)
{
	static const struct {
		int directory;
		int craft;
		int team;
		int frame;
		const char *emblem;
	} cases[] = {
		{MISSION_DIRECTORY_MELEES, 1, 0, 6, "rebtiny3"},
		{MISSION_DIRECTORY_MELEES, 4, 0, 32, "rebtiny0"},
		{MISSION_DIRECTORY_MELEES, 5, 1, 31, "imptiny15"},
		{MISSION_DIRECTORY_TOURNAMENTS, 14, 0, 1, "rebtiny0"},
		{MISSION_DIRECTORY_TOURNAMENTS, 0, 1, 2, "imptiny1"},
		{MISSION_DIRECTORY_TRAINING_EXERCISES, 6, 1, 9, "imptiny4"},
		{MISSION_DIRECTORY_COMBAT_ENGAGEMENTS, 1, 0, 4, "imptiny2"},
		{MISSION_DIRECTORY_COMBAT_ENGAGEMENTS, 6, 1, 4, "rebtiny2"},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		craft_world(SOLO, cases[i].directory);
		strcpy(g_pilot_data.name, "L");
		strcpy(g_pilot_data.rating_name, "Officer");
		g_frontend_mission.flight_groups[0].craft_type =
			(craft_species)cases[i].craft;
		g_pilot_data.team = cases[i].team;
		XVT_ASSERT_INT_EQ(open_and_click_back(cases[i].frame), 0);
		if (strcmp(g_frontend_scratch_buffer, cases[i].emblem) != 0) {
			fprintf(stderr, "case %zu: got \"%s\"\n", i,
				g_frontend_scratch_buffer);
			exit(1);
		}
	}
	close_craft_world();
}

/* In network play the countdown starts, at 60 seconds, on the first frame
 * on which as many players have entered the screen as are ready, once; from
 * the next frame on the screen shows the time left, "MM:SS". Single player
 * has no countdown. */
static void check_countdown_starts(void)
{
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	g_front_state.net_players[1].ready_flag = 1;
	g_front_state.net_players[2].ready_flag = 1;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	queue_packet(NET_PACKET_BRIEFING_ENTERED, 0, 0);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_started"), 0);
	queue_packet(NET_PACKET_BRIEFING_ENTERED, 0, 0);
	XVT_ASSERT_INT_EQ(run_frame(2), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_started"), 1);
	XVT_ASSERT_INT_EQ(line_value("briefing.countdown_started", "entered"),
			  2);
	XVT_ASSERT_INT_EQ(line_value("briefing.countdown_started", "ms"),
			  60000);
	xvt_time_advance_host_clock(15500000);
	XVT_ASSERT_INT_EQ(run_frame(3), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_started"), 1);
	click(BACK_X, BACK_Y);
	XVT_ASSERT_INT_EQ(run_frame(4), 0);
	XVT_ASSERT_TRUE(strcmp(g_frontend_scratch_buffer, "00:44") == 0);

	craft_world(SOLO, MISSION_DIRECTORY_MELEES);
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_started"), 0);
	close_craft_world();
}

/* ------------------------------------------------------------------------ */
/* The Back button. */

/* With both assignment screens skipped, the single-player Back button asks
 * first whether to restart and return to mission selection; the dialog's
 * continuation does the rest, so the screen stays. */
static void check_back_asks_to_restart(void)
{
	craft_world(SOLO, MISSION_DIRECTORY_MELEES);
	set_teams(1, 1, 0);
	g_mission_setup_team_assignment_skipped = 1;
	XVT_ASSERT_INT_EQ(open_and_click_back(1), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
	XVT_ASSERT_TRUE(top_screen() ==
			mission_briefing_craft_selection_update);
	XVT_ASSERT_INT_EQ(
		count_lines(
			"mission.setup_confirm_asked screen=\"craft\" action=\"restart\""),
		1);
	close_craft_world();
}

/* In network play Back asks first: the host whether to restart, a client
 * whether to leave. Without a click nothing is asked. */
static void check_back_network(void)
{
	static const struct {
		int mode;
		const char *line;
	} cases[] = {
		{HOST,
		 "mission.setup_confirm_asked screen=\"craft\" action=\"restart\""},
		{CLIENT,
		 "mission.setup_confirm_asked screen=\"craft\" action=\"leave\""},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		craft_world(cases[i].mode, MISSION_DIRECTORY_MELEES);
		g_game_config.help_on = 1;
		g_front_state.net_players[1].ready_flag = 1;
		XVT_ASSERT_INT_EQ(run_frame(0), 0);
		XVT_ASSERT_INT_EQ(run_frame(1), 0);
		XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
		click(BACK_X, BACK_Y);
		XVT_ASSERT_INT_EQ(run_frame(2), 0);
		XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
		XVT_ASSERT_INT_EQ(count_lines(cases[i].line), 1);
		XVT_ASSERT_INT_EQ(count_lines("mission.setup_confirm_asked"),
				  1);
	}
	close_craft_world();
}

/* ------------------------------------------------------------------------ */
/* The Fly, Ready and Reconfigure buttons. */

/* The single-player Fly button fills roster entry 0 from the selections: the
 * preset's craft, the pilot's rating, the warhead, beam and countermeasure
 * choices and the flight group craft option minus 1; marks it ready and goes
 * to flight_loading_update_ready_screen, returning 0. With preset option 0
 * the craft override is 0. */
static void check_fly_solo(void)
{
	craft_world(SOLO, MISSION_DIRECTORY_MELEES);
	offer_every_choice();
	g_pilot_data.rating = 5;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_TRUE(top_screen() ==
			mission_briefing_craft_selection_update);
	g_mission_setup_selected_preset_craft_option_index = 4;
	g_mission_setup_selected_flight_group_craft_option_index = 2;
	g_mission_setup_selected_warhead_option_index = 1;
	g_mission_setup_selected_beam_option_index = 1;
	g_mission_setup_selected_countermeasure_option_index = 1;
	click(FLY_X, FLY_Y);
	XVT_ASSERT_INT_EQ(run_frame(2), 0);
	XVT_ASSERT_TRUE(top_screen() == flight_loading_update_ready_screen);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].craft_type_override, 3);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].pilot_rating, 5);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].warhead_option_index, 1);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].beam_option_index, 1);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].countermeasure_option_index, 1);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].craft_option_index, 1);
	XVT_ASSERT_INT_EQ(g_mp_roster_ready_flags[0], 1);
	XVT_ASSERT_INT_EQ(line_value("briefing.craft_confirmed", "craft"), 3);

	craft_world(SOLO, MISSION_DIRECTORY_MELEES);
	g_mp_roster[0].craft_type_override = 9;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	click(FLY_X, FLY_Y);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].craft_type_override, 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].craft_option_index, -1);
	close_craft_world();
}

/* With host-only craft selection the host gets Fly once every ready player
 * has entered the screen, and Fly sends everyone the launch. A client gets
 * neither Fly nor Ready. */
static void check_fly_host_only(void)
{
	craft_world(HOST, MISSION_DIRECTORY_MELEES);
	g_game_config.craft_selection = CRAFT_SELECTION_HOST_ONLY;
	g_front_state.net_players[1].ready_flag = 1;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	click(FLY_X, FLY_Y);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.roster_sent"), 0);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_BRIEFING_ENTERED);
	g_frontend_briefing_entered_count = 1;
	click(FLY_X, FLY_Y);
	XVT_ASSERT_INT_EQ(run_frame(2), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.roster_sent by=\"fly\""), 1);
	XVT_ASSERT_INT_EQ(count_lines("briefing.roster_sent"), 1);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS);

	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	g_game_config.craft_selection = CRAFT_SELECTION_HOST_ONLY;
	g_front_state.net_players[1].ready_flag = 1;
	g_frontend_briefing_entered_count = 1;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	click(FLY_X, FLY_Y);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.roster_sent"), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.ready_sent"), 0);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_BRIEFING_ENTERED);
	close_craft_world();
}

/* Otherwise each network player has Ready, which sends
 * NET_PACKET_PLAYER_READY, or, once the player's own roster entry is marked
 * ready, Reconfigure, which sends NET_PACKET_PLAYER_UNREADY; the log names
 * the entry, here 2. Another entry's ready flag does not count. */
static void check_ready_buttons(void)
{
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	g_game_config.craft_selection = CRAFT_SELECTION_ON;
	g_mp_roster[0].player_id = PEER_ID;
	g_mp_roster[2].player_id = LOCAL_ID;
	g_mp_roster_ready_flags[0] = 1;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	click(FLY_X, FLY_Y);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_PLAYER_READY);
	XVT_ASSERT_INT_EQ(line_value("briefing.ready_sent", "ready"), 1);
	XVT_ASSERT_INT_EQ(line_value("briefing.ready_sent", "entry"), 2);
	g_mp_roster_ready_flags[2] = 1;
	click(FLY_X, FLY_Y);
	XVT_ASSERT_INT_EQ(run_frame(2), 0);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_PLAYER_UNREADY);
	XVT_ASSERT_INT_EQ(line_value("briefing.ready_sent", "ready"), 0);
	XVT_ASSERT_INT_EQ(line_value("briefing.ready_sent", "entry"), 2);
	XVT_ASSERT_INT_EQ(run_frame(3), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.ready_sent"), 2);
	close_craft_world();
}

/* ------------------------------------------------------------------------ */
/* The launch and the countdown's steps. */

/* The host sends everyone the launch once every network player is ready:
 * as many roster ready flags set as session players marked ready. A client
 * never sends it. */
static void check_launch_when_all_ready(void)
{
	static const int modes[2] = {HOST, CLIENT};
	for (int i = 0; i < 2; ++i) {
		craft_world(modes[i], MISSION_DIRECTORY_MELEES);
		g_front_state.net_players[0].ready_flag = 1;
		g_front_state.net_players[1].ready_flag = 1;
		g_mp_roster_ready_flags[0] = 1;
		XVT_ASSERT_INT_EQ(run_frame(0), 0);
		XVT_ASSERT_INT_EQ(count_lines("briefing.roster_sent"), 0);
		g_mp_roster_ready_flags[1] = 1;
		XVT_ASSERT_INT_EQ(run_frame(1), 0);
		XVT_ASSERT_INT_EQ(
			count_lines("briefing.roster_sent by=\"all_ready\""),
			modes[i] == HOST ? 1 : 0);
		XVT_ASSERT_INT_EQ(
			g_frontend_net_packet_scratch.packet_type,
			modes[i] == HOST
				? NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS
				: NET_PACKET_BRIEFING_ENTERED);
	}
	close_craft_world();
}

/* While the countdown runs the host sends everyone the time left whenever
 * its whole seconds change. When it goes under 0 it is set to 0, the host
 * sends the launch and the frame returns 0 there, after showing the time;
 * later frames show 00:00 and step nothing, until the next first frame
 * starts the countdown again. */
static void check_countdown_steps_host(void)
{
	craft_world(HOST, MISSION_DIRECTORY_MELEES);
	g_front_state.net_players[1].ready_flag = 1;
	g_frontend_briefing_entered_count = 1;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_started"), 1);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_sent"), 0);
	xvt_time_advance_host_clock(500000);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_sent"), 1);
	XVT_ASSERT_INT_EQ(line_value("briefing.countdown_sent", "ms"), 59500);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_BRIEFING_COUNTDOWN);
	XVT_ASSERT_INT_EQ(scratch_word(0), 59500);
	xvt_time_advance_host_clock(400000);
	XVT_ASSERT_INT_EQ(run_frame(2), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_sent"), 1);
	xvt_time_advance_host_clock(60600000);
	XVT_ASSERT_INT_EQ(run_frame(3), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_sent"), 2);
	XVT_ASSERT_INT_EQ(line_value("briefing.countdown_sent", "ms"), -1500);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_expired"), 1);
	XVT_ASSERT_INT_EQ(line_value("briefing.countdown_expired", "ms"),
			  -1500);
	XVT_ASSERT_INT_EQ(count_lines("briefing.roster_sent by=\"countdown\""),
			  1);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS);
	XVT_ASSERT_TRUE(strcmp(g_frontend_scratch_buffer, "00:59") == 0);
	xvt_time_advance_host_clock(1000000);
	click(BACK_X, BACK_Y);
	XVT_ASSERT_INT_EQ(run_frame(4), 0);
	XVT_ASSERT_TRUE(strcmp(g_frontend_scratch_buffer, "00:00") == 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_sent"), 2);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_expired"), 1);
	xvt_dialog_shutdown();
	queue_packet(NET_PACKET_BRIEFING_COUNTDOWN, -5000, 0);
	XVT_ASSERT_INT_EQ(run_frame(5), 0);
	XVT_ASSERT_INT_EQ(line_value("briefing.countdown_lowered", "previous"),
			  0);
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_started"), 2);
	close_craft_world();
}

/* A client's countdown runs the same way but sends nothing, neither its
 * time nor the launch; it ends when it goes under 0, not at 0, and that
 * frame too returns 0 there. */
static void check_countdown_expires_client(void)
{
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	g_front_state.net_players[1].ready_flag = 1;
	g_frontend_briefing_entered_count = 1;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	xvt_time_advance_host_clock(30000000);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_expired"), 0);
	xvt_time_advance_host_clock(30000000);
	XVT_ASSERT_INT_EQ(run_frame(2), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_expired"), 0);
	xvt_time_advance_host_clock(500000);
	XVT_ASSERT_INT_EQ(run_frame(3), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_expired"), 1);
	XVT_ASSERT_INT_EQ(line_value("briefing.countdown_expired", "ms"), -500);
	XVT_ASSERT_INT_EQ(count_lines("briefing.countdown_sent"), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.roster_sent"), 0);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_BRIEFING_ENTERED);
	XVT_ASSERT_TRUE(strcmp(g_frontend_scratch_buffer, "00:00") == 0);
	close_craft_world();
}

/* ------------------------------------------------------------------------ */
/* Known failures. */

/* Known failure title_unlisted_mission, issue #65: the screen finds the
 * pilot's mission in the list, logging a warning when it is not there, and
 * then draws the entry's description as the title without checking the
 * index, which is the list's count, one past its end. With mission 4 not
 * listed, the frame should still run and return 0. */
static void check_title_unlisted_mission(void)
{
	craft_world(SOLO, MISSION_DIRECTORY_MELEES);
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_MELEES] = 4;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.craft_mission_unlisted"), 1);
	close_craft_world();
}

/* Known failure ninth_reservation, issue #153: the reserved player table has
 * 8 entries. With all 8 in use, a reservation for a ninth player is written
 * past the table and counted; the table should stay full at 8. */
static void check_ninth_reservation(void)
{
	static const int full[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	set_reservations(full, 8);
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	queue_packet(NET_PACKET_FLIGHT_RESERVATION, 9, PEER_ID);
	run_frame(1);
	check_reservations(full, 8);
	close_craft_world();
}

/* Known failure release_on_full_table, issue #153: with all 8 entries in
 * use, a release of a player not in the table still clears entry 7, while
 * the count stays 8. The table should not change. */
static void check_release_on_full_table(void)
{
	static const int full[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	set_reservations(full, 8);
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	queue_packet(NET_PACKET_RELEASE_FLIGHT_RESERVATION, 9, 0);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	check_reservations(full, 8);
	close_craft_world();
}

/* Known failure launch_sent_once, issue #154: the host tests every frame
 * whether all players are ready and sends the launch each time they are,
 * until its own copy of the launch comes back. With both players staying
 * ready for three frames the launch should go out once. */
static void check_launch_sent_once(void)
{
	craft_world(HOST, MISSION_DIRECTORY_MELEES);
	g_front_state.net_players[0].ready_flag = 1;
	g_front_state.net_players[1].ready_flag = 1;
	g_mp_roster_ready_flags[0] = 1;
	g_mp_roster_ready_flags[1] = 1;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	XVT_ASSERT_INT_EQ(run_frame(1), 0);
	XVT_ASSERT_INT_EQ(run_frame(2), 0);
	XVT_ASSERT_INT_EQ(count_lines("briefing.roster_sent by=\"all_ready\""),
			  1);
	close_craft_world();
}

/* Known failure ready_flag_unlisted_player, issue #64: the Ready button
 * looks up this machine's player in the 8-entry roster and reads its ready
 * flag without checking that it was found; when it is not there the read is
 * one past g_mp_roster_ready_flags. The frame should still run and return
 * 0. */
static void check_ready_flag_unlisted_player(void)
{
	craft_world(CLIENT, MISSION_DIRECTORY_MELEES);
	g_game_config.craft_selection = CRAFT_SELECTION_ON;
	g_mp_roster[0].player_id = PEER_ID;
	XVT_ASSERT_INT_EQ(run_frame(0), 0);
	close_craft_world();
}

int main(int argc, char **argv)
{
	fail_after_seconds(60);
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(catch_line, NULL);
	memset(&g_front_state, 0, sizeof g_front_state);
	write_assets();
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		static const struct {
			const char *name;
			void (*check)(void);
		} known_failures[] = {
			{"title_unlisted_mission",
			 check_title_unlisted_mission},
			{"ninth_reservation", check_ninth_reservation},
			{"release_on_full_table", check_release_on_full_table},
			{"launch_sent_once", check_launch_sent_once},
			{"ready_flag_unlisted_player",
			 check_ready_flag_unlisted_player},
		};
		for (size_t i = 0;
		     i < sizeof known_failures / sizeof known_failures[0];
		     ++i) {
			if (strcmp(argv[2], known_failures[i].name) == 0) {
				known_failures[i].check();
			}
		}
		xvt_test_close_assets(&g_assets);
		return 0;
	}
	check_all_players_ready();
	xvt_test_open_display();
	check_map_mouse_input();
	check_draw_map_viewport();
	check_exit();
	xvt_test_close_display();

	check_first_frame_finds_mission();
	check_back_follows_melee_teams();
	check_preview_of_selected_craft();
	check_quick_start();
	check_background_by_side();
	check_entry_packets();
	check_entry_loadout_host_only();
	check_craft_choices();
	check_host_cancelled_client();
	check_host_cancelled_host();
	check_state_resends_loadout();
	check_entered_resends_loadout();
	check_return_to_setup();
	check_launch_packet();
	check_loadout_packet_preview();
	check_countdown_packet();
	check_release_reservation();
	check_add_reservation();
	check_pilot_rating();
	check_instruction_line();
	check_pilot_line();
	check_countdown_starts();
	check_back_asks_to_restart();
	check_back_network();
	check_fly_solo();
	check_fly_host_only();
	check_ready_buttons();
	check_launch_when_all_ready();
	check_countdown_steps_host();
	check_countdown_expires_client();
	xvt_test_close_assets(&g_assets);
	return 0;
}
