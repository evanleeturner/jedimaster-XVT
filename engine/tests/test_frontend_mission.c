/* Tests for xvt/frontend/frontend_mission.c, which loads the pilot's mission
 * for the frontend's screens and sets up the pilot record before a mission
 * flies. The loader checks write a melee mission list and mission files in the
 * frontend's format into a temporary asset folder: two flight groups, one
 * message, a few team goals and records, and eight briefings, two of them for
 * team 1. The launch checks set the roster, the player assignments and the
 * flight groups in the game's own tables. No game data is read.
 *
 * POSIX only, for the temporary folder (test_asset_folder.h). */
#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "test_asset_folder.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_script.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"

enum {
	TEST_MISSION_ID = 7,  /* The pilot's melee, first in the list. */
	TEST_UNLISTED_ID = 9, /* A melee id the list does not hold. */
	TEST_PILOT_TEAM = 1,  /* Briefings 2 and 5 are flagged for it. */
	TEST_CHOSEN_BRIEFING = 5,
	TEST_LOCAL_PLAYER = 0x41,
	TEST_OTHER_PLAYER = 0x42,
};

static struct xvt_test_assets g_assets;

/* A file built in memory before it is written to the asset folder. */
static uint8_t g_file[65536];
static size_t g_file_size;

static void put(const void *data, size_t size)
{
	XVT_ASSERT_TRUE(g_file_size + size <= sizeof g_file);
	memcpy(g_file + g_file_size, data, size);
	g_file_size += size;
}

static void put_word(uint16_t word) { put(&word, sizeof word); }

/* A string as the briefing stores it: a 16-bit length, then the bytes. */
static void put_string(const char *text, size_t length)
{
	put_word((uint16_t)length);
	put(text, length);
}

/* Writes a mission file of the given format version: groups "Red" (IFF 1,
 * team 1) and "Blue" (IFF 0, team 0); a header of mission type 3; one
 * message, in slot 5, of bytes 0x5A; one goal of bytes 0x11 for team 0 and two
 * of 0x22 for team 1; team records for teams 0 and 2 only, of bytes 0x30 plus
 * the team. Briefing b's script lasts 100 + b frames, briefing 0 is flagged
 * for team 0 and briefings 2 and 5 for team 1. Its label 0 reads "L" and the
 * briefing's digit, label 1 is label_one_length bytes of 'x', text block 0
 * reads "T", the digit and " text"; the other strings are empty. */
static void write_mission(const char *name, uint16_t version,
			  size_t label_one_length)
{
	g_file_size = 0;
	put_word(version);
	put_word(2);
	put_word(1);
	struct frontend_mission_header header;
	memset(&header, 0, sizeof header);
	header.mission_type = 3;
	put(&header, sizeof header);
	struct xvt_flight_group groups[2];
	memset(groups, 0, sizeof groups);
	strcpy(groups[0].name, "Red");
	groups[0].iff = 1;
	groups[0].team = 1;
	strcpy(groups[1].name, "Blue");
	put(groups, sizeof groups);
	struct mission_message message;
	memset(&message, 0x5A, sizeof message);
	put_word(5);
	put(&message, sizeof message);
	struct global_goal goal;
	for (int team = 0; team < 10; ++team) {
		int goal_count = team == 0 ? 1 : team == 1 ? 2 : 0;
		put_word((uint16_t)goal_count);
		memset(&goal, team == 0 ? 0x11 : 0x22, sizeof goal);
		for (int i = 0; i < goal_count; ++i) {
			put(&goal, sizeof goal);
		}
	}
	struct team record;
	for (int team = 0; team < 10; ++team) {
		int present = team == 0 || team == 2;
		put_word((uint16_t)present);
		if (present) {
			memset(&record, 0x30 + team, sizeof record);
			put(&record, sizeof record);
		}
	}
	static char long_label[1024];
	memset(long_label, 'x', sizeof long_label);
	XVT_ASSERT_TRUE(label_one_length <= sizeof long_label);
	for (int briefing = 0; briefing < 8; ++briefing) {
		struct frontend_briefing_script script;
		memset(&script, 0, sizeof script);
		script.duration_frames = (int16_t)(100 + briefing);
		put(&script, sizeof script);
		for (int team = 0; team < 10; ++team) {
			uint8_t flag = (team == 0 && briefing == 0) ||
				       (team == TEST_PILOT_TEAM &&
					(briefing == 2 ||
					 briefing == TEST_CHOSEN_BRIEFING));
			put(&flag, 1);
		}
		char label[3] = {'L', (char)('0' + briefing), 0};
		char text[8] = {'T', (char)('0' + briefing), 0};
		strcat(text, " text");
		put_string(label, 2);
		put_string(long_label, label_one_length);
		for (int i = 2; i < 32; ++i) {
			put_word(0);
		}
		put_string(text, strlen(text));
		for (int i = 1; i < 32; ++i) {
			put_word(0);
		}
	}
	xvt_test_write_file(g_assets.asset, name, g_file, g_file_size);
}

/* The melee list, holding "brief.tie" as the pilot's melee, then "other.tie"
 * as melee 3. */
static void write_list(void)
{
	xvt_test_make_subfolder(g_assets.asset, "melee");
	xvt_test_write_text(
		g_assets.asset, "melee/mission.lst",
		"// Melees\n[Melee]\n7\nbrief.tie\nBriefing melee (2)\n"
		"3\nother.tie\nOther melee\n");
}

/* A single-player pilot of team 1 choosing melee id, a fresh asset folder
 * with the melee list, and the briefing's buffers allocated as the briefing
 * screen does. g_frontend_mission holds bytes of 0xEE, which a load replaces. */
static void open_world(int mission_id)
{
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_MELEES;
	g_pilot_data.mission_description_ids[MISSION_DIRECTORY_MELEES] =
		mission_id;
	g_pilot_data.team = TEST_PILOT_TEAM;
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	g_selected_mission_list_index = -1;
	xvt_test_open_assets(&g_assets);
	write_list();
	frontend_mission_init_for_briefing();
	memset(&g_frontend_mission, 0xEE, sizeof g_frontend_mission);
}

/* Frees the briefing's buffers and the mission list and removes the folder. */
static void close_world(void)
{
	briefing_text_free_allocated_buffers();
	memset(g_briefing_map_label_texts, 0,
	       sizeof g_briefing_map_label_texts);
	memset(g_briefing_text_blocks, 0, sizeof g_briefing_text_blocks);
	memset(g_briefing_unused_buffers, 0, sizeof g_briefing_unused_buffers);
	free(g_mission_list);
	g_mission_list = NULL;
	g_mission_count = 0;
	xvt_test_close_assets(&g_assets);
}

/* 1 when every byte of the frontend's mission is 0xEE, as open_world left
 * it. */
static int mission_untouched(void)
{
	const uint8_t *bytes = (const uint8_t *)&g_frontend_mission;
	for (size_t i = 0; i < sizeof g_frontend_mission; ++i) {
		if (bytes[i] != 0xEE) {
			return 0;
		}
	}
	return 1;
}

/* frontend_mission_init_for_briefing sets the briefing up empty: playback
 * on, the selection, centers, indices and page at 0, the scales at 32, one
 * flight group with win_type 1, text slots, markers and labels off, and the
 * map panel at (0, 0) to (360, 236). Its buffers hold 40, 320 and 1024
 * bytes. */
static void check_init_for_briefing(void)
{
	memset(&g_frontend_mission, 0xEE, sizeof g_frontend_mission);
	g_briefing_playback_active = 0;
	g_briefing_selected_mission_point14_flight_group_idx = 4;
	g_briefing_map_center.x = 9;
	g_briefing_map_center.y = 9;
	g_briefing_map_target_center.x = 9;
	g_briefing_map_target_center.y = 9;
	g_active_briefing_index = 3;
	g_briefing_last_narrated_text_block_idx = 3;
	g_briefing_text_page_number = 3;
	g_briefing_map_scale.x = 1;
	g_briefing_map_scale.y = 1;
	g_briefing_map_target_scale.x = 1;
	g_briefing_map_target_scale.y = 1;
	g_briefing_text_slot_active[1] = 1;
	g_briefing_map_fg_marker_active[7] = 1;
	g_briefing_map_label_active[7] = 1;

	frontend_mission_init_for_briefing();
	XVT_ASSERT_INT_EQ(g_briefing_playback_active, 1);
	XVT_ASSERT_INT_EQ(g_briefing_selected_mission_point14_flight_group_idx,
			  0);
	XVT_ASSERT_INT_EQ(g_briefing_map_center.x, 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_center.y, 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_center.x, 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_center.y, 0);
	XVT_ASSERT_INT_EQ(g_active_briefing_index, 0);
	XVT_ASSERT_INT_EQ(g_briefing_last_narrated_text_block_idx, 0);
	XVT_ASSERT_INT_EQ(g_briefing_text_page_number, 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.x, 32);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.y, 32);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_scale.x, 32);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_scale.y, 32);
	XVT_ASSERT_INT_EQ(g_frontend_mission.flight_group_count, 1);
	XVT_ASSERT_INT_EQ(g_frontend_mission.header.win_type, 1);
	XVT_ASSERT_INT_EQ(g_frontend_mission.header.all_waypoints_shown, 0);
	XVT_ASSERT_INT_EQ(g_frontend_mission.format_version, 0);
	XVT_ASSERT_INT_EQ(g_frontend_mission.flight_groups[0].iff, 0);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_active[1], 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_active[7], 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_label_active[7], 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_panel_rect.left, 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_panel_rect.top, 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_panel_rect.right, 360);
	XVT_ASSERT_INT_EQ(g_briefing_map_panel_rect.bottom, 236);
	/* Each buffer takes its whole size; the sanitizer stops a short one. */
	for (int i = 0; i < 32; ++i) {
		memset(g_briefing_map_label_texts[i], 'a', 40);
		memset(g_briefing_text_blocks[i], 'a', 320);
	}
	for (int i = 0; i < 20; ++i) {
		memset(g_briefing_unused_buffers[i], 'a', 1024);
	}
	briefing_text_free_allocated_buffers();
}

/* The briefing loader finds the pilot's melee, first in the list, and
 * loads its file: the counts, header and both flight groups, the message in
 * the slot the file gives, each team's goals and the team records the file
 * flags present. Briefings 2 and 5 are both for team 1, so the later one is
 * kept: its index, script and strings; the strings of the others are skipped
 * in step. */
static void check_load_with_briefing(void)
{
	open_world(TEST_MISSION_ID);
	write_mission("melee/brief.tie", 14, 3);
	frontend_mission_load_current_with_briefing();
	XVT_ASSERT_INT_EQ(g_selected_mission_list_index, 0);
	XVT_ASSERT_INT_EQ(g_frontend_mission.format_version, 14);
	XVT_ASSERT_INT_EQ(g_frontend_mission.flight_group_count, 2);
	XVT_ASSERT_INT_EQ(g_frontend_mission.message_count, 1);
	XVT_ASSERT_INT_EQ(g_frontend_mission.header.mission_type, 3);
	XVT_ASSERT_TRUE(
		strcmp(g_frontend_mission.flight_groups[1].name, "Blue") == 0);
	XVT_ASSERT_INT_EQ(g_frontend_mission.flight_groups[0].team, 1);
	/* Group 2 was never in the file, and is cleared. */
	XVT_ASSERT_INT_EQ(g_frontend_mission.flight_groups[2].team, 0);
	const uint8_t *slot = (const uint8_t *)&g_frontend_mission.messages[5];
	XVT_ASSERT_INT_EQ(slot[0], 0x5A);
	XVT_ASSERT_INT_EQ(((const uint8_t *)&g_frontend_mission.messages[0])[0],
			  0);
	XVT_ASSERT_INT_EQ(
		((const uint8_t *)&g_frontend_mission.global_goals[0][0])[0],
		0x11);
	XVT_ASSERT_INT_EQ(
		((const uint8_t *)&g_frontend_mission.global_goals[1][1])[0],
		0x22);
	XVT_ASSERT_INT_EQ(
		((const uint8_t *)&g_frontend_mission.global_goals[0][1])[0],
		0);
	XVT_ASSERT_INT_EQ(g_frontend_mission.teams[0].name[0], 0x30);
	XVT_ASSERT_INT_EQ(g_frontend_mission.teams[1].name[0], 0);
	XVT_ASSERT_INT_EQ(g_frontend_mission.teams[2].name[0], 0x32);
	XVT_ASSERT_INT_EQ(g_active_briefing_index, TEST_CHOSEN_BRIEFING);
	XVT_ASSERT_INT_EQ(g_briefing_script.duration_frames,
			  100 + TEST_CHOSEN_BRIEFING);
	XVT_ASSERT_TRUE(strcmp(g_briefing_map_label_texts[0], "L5") == 0);
	XVT_ASSERT_TRUE(strcmp(g_briefing_map_label_texts[1], "xxx") == 0);
	XVT_ASSERT_TRUE(strcmp(g_briefing_map_label_texts[2], "") == 0);
	XVT_ASSERT_TRUE(strcmp(g_briefing_text_blocks[0], "T5 text") == 0);
	XVT_ASSERT_TRUE(strcmp(g_briefing_text_blocks[1], "") == 0);
	close_world();
}

/* A file whose first word is not 12, 13 or 14 leaves the mission holding only
 * that word; a file that does not open leaves it as it was. */
static void check_load_with_briefing_refusals(void)
{
	open_world(TEST_MISSION_ID);
	write_mission("melee/brief.tie", 11, 3);
	frontend_mission_load_current_with_briefing();
	XVT_ASSERT_INT_EQ(g_frontend_mission.format_version, 11);
	XVT_ASSERT_INT_EQ(g_frontend_mission.flight_group_count, 0);
	XVT_ASSERT_INT_EQ(g_frontend_mission.header.mission_type, 0);
	close_world();

	open_world(TEST_MISSION_ID);
	frontend_mission_load_current_with_briefing();
	XVT_ASSERT_TRUE(mission_untouched());
	close_world();
}

/* Known failure briefing_unlisted_mission, issue #80: the pilot's melee is
 * not in the list. frontend_mission_load_current returns without loading in
 * this case; the briefing loader goes on and reads the file name of the entry
 * past the end of the list, and the sanitizer stops the program. Its comment
 * says so, and the fix will have to change it. The mission should be left as
 * it was. */
static void check_briefing_unlisted_mission(void)
{
	open_world(TEST_UNLISTED_ID);
	frontend_mission_load_current_with_briefing();
	XVT_ASSERT_TRUE(mission_untouched());
	close_world();
}

/* Known failure briefing_label_past_buffer, issue #81: briefing_text.c gives
 * each map label a 40-byte buffer. Label 1 of every briefing is 45 bytes
 * long; the loader reads all 45 into the chosen briefing's buffer and ends it
 * at byte 45, past the buffer, and the sanitizer stops the program. The label
 * should be cut to fit, and the strings after it still read in step. */
static void check_briefing_label_past_buffer(void)
{
	open_world(TEST_MISSION_ID);
	write_mission("melee/brief.tie", 14, 45);
	frontend_mission_load_current_with_briefing();
	XVT_ASSERT_TRUE(strlen(g_briefing_map_label_texts[1]) < 40);
	XVT_ASSERT_TRUE(strcmp(g_briefing_map_label_texts[0], "L5") == 0);
	XVT_ASSERT_TRUE(strcmp(g_briefing_text_blocks[0], "T5 text") == 0);
	close_world();
}

/* frontend_mission_load_for_briefing sets the briefing up empty, loads the
 * pilot's mission with its briefing and returns 1, also when the mission's
 * file does not open. */
static void check_load_for_briefing(void)
{
	open_world(TEST_MISSION_ID);
	briefing_text_free_allocated_buffers();
	write_mission("melee/brief.tie", 14, 3);
	XVT_ASSERT_INT_EQ(frontend_mission_load_for_briefing(), 1);
	XVT_ASSERT_INT_EQ(g_frontend_mission.flight_group_count, 2);
	XVT_ASSERT_INT_EQ(g_active_briefing_index, TEST_CHOSEN_BRIEFING);
	close_world();

	open_world(TEST_MISSION_ID);
	briefing_text_free_allocated_buffers();
	XVT_ASSERT_INT_EQ(frontend_mission_load_for_briefing(), 1);
	XVT_ASSERT_INT_EQ(g_frontend_mission.flight_group_count, 1);
	close_world();
}

/* frontend_mission_load_file loads a file the way the briefing loader does,
 * without the briefings, into the mission it is given; it leaves that mission
 * as it was when the file does not open, and holding only the first word when
 * that is not 12, 13 or 14. */
static void check_load_file(void)
{
	static struct frontend_mission mission;
	open_world(TEST_MISSION_ID);
	write_mission("brief.tie", 14, 3);
	write_mission("old.tie", 15, 3);
	memset(&mission, 0xEE, sizeof mission);
	frontend_mission_load_file("missing.tie", &mission);
	XVT_ASSERT_INT_EQ(mission.format_version, 0xEEEE);
	XVT_ASSERT_INT_EQ(mission.flight_group_count, 0xEEEE);

	frontend_mission_load_file("brief.tie", &mission);
	XVT_ASSERT_INT_EQ(mission.format_version, 14);
	XVT_ASSERT_INT_EQ(mission.flight_group_count, 2);
	XVT_ASSERT_INT_EQ(mission.message_count, 1);
	XVT_ASSERT_INT_EQ(mission.header.mission_type, 3);
	XVT_ASSERT_TRUE(strcmp(mission.flight_groups[1].name, "Blue") == 0);
	XVT_ASSERT_INT_EQ(mission.flight_groups[2].team, 0);
	XVT_ASSERT_INT_EQ(((const uint8_t *)&mission.messages[5])[0], 0x5A);
	XVT_ASSERT_INT_EQ(((const uint8_t *)&mission.global_goals[0][0])[0],
			  0x11);
	XVT_ASSERT_INT_EQ(((const uint8_t *)&mission.global_goals[1][1])[0],
			  0x22);
	XVT_ASSERT_INT_EQ(((const uint8_t *)&mission.global_goals[1][2])[0], 0);
	XVT_ASSERT_INT_EQ(mission.teams[0].name[0], 0x30);
	XVT_ASSERT_INT_EQ(mission.teams[1].name[0], 0);
	XVT_ASSERT_INT_EQ(mission.teams[2].name[0], 0x32);
	/* The frontend's own mission is not touched. */
	XVT_ASSERT_TRUE(mission_untouched());

	frontend_mission_load_file("old.tie", &mission);
	XVT_ASSERT_INT_EQ(mission.format_version, 15);
	XVT_ASSERT_INT_EQ(mission.flight_group_count, 0);
	close_world();
}

/* frontend_mission_load_current loads the pilot's melee without its
 * briefings, and returns without loading when the list lacks it. */
static void check_load_current(void)
{
	open_world(TEST_MISSION_ID);
	write_mission("melee/brief.tie", 12, 3);
	g_active_briefing_index = 0;
	frontend_mission_load_current();
	XVT_ASSERT_INT_EQ(g_selected_mission_list_index, 0);
	XVT_ASSERT_INT_EQ(g_frontend_mission.format_version, 12);
	XVT_ASSERT_INT_EQ(g_frontend_mission.flight_group_count, 2);
	XVT_ASSERT_TRUE(
		strcmp(g_frontend_mission.flight_groups[0].name, "Red") == 0);
	XVT_ASSERT_INT_EQ(g_frontend_mission.teams[2].name[0], 0x32);
	XVT_ASSERT_INT_EQ(g_active_briefing_index, 0);
	close_world();

	open_world(TEST_UNLISTED_ID);
	frontend_mission_load_current();
	XVT_ASSERT_TRUE(mission_untouched());
	close_world();
}

/* A single-player pilot of team 2 in the training directory, with a sequence
 * active: the local player, id 0x41, has roster entry 0 and the assignment
 * slot that gives flight group 3, on IFF 1; roster entries 1 and 4 hold
 * other players, whom single player drops and DirectPlay does not list. Every
 * result the launch should clear is set. */
static void launch_world(void)
{
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	memset(g_mp_roster, 0, sizeof g_mp_roster);
	memset(&g_front_state, 0, sizeof g_front_state);
	memset(&g_frontend_mission, 0, sizeof g_frontend_mission);
	memset(&g_mission_setup_player_assignments, 0,
	       sizeof g_mission_setup_player_assignments);
	memset(g_mission_setup_player_flight_group_indices, 0,
	       sizeof g_mission_setup_player_flight_group_indices);
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	g_game_config.ai_opponents = 0;
	g_team_count = 0;
	g_front_state.net_players[0].player_id = TEST_LOCAL_PLAYER;
	g_front_state.net_player_count = 1;

	struct mp_roster_entry *local = &g_mp_roster[0];
	strcpy(local->name, "ABCDEFGHIJKLM");
	local->player_id = TEST_LOCAL_PLAYER;
	local->pilot_rating = 3;
	local->craft_type_override = 0;
	local->craft_option_index = 2;
	local->warhead_option_index = 4;
	local->beam_option_index = 5;
	local->countermeasure_option_index = 6;
	g_mp_roster[1].player_id = 0x44;
	g_mp_roster[4].player_id = TEST_OTHER_PLAYER;
	g_mission_setup_player_assignments.team_player_ids[2][1] =
		TEST_LOCAL_PLAYER;
	g_mission_setup_player_flight_group_indices[2 * 8 + 1] = 3;
	g_frontend_mission.flight_groups[3].iff = 1;
	g_frontend_mission.flight_groups[3].optional_craft[2] = 5;

	g_pilot_data.team = 2;
	g_pilot_data.mission_directory_id =
		MISSION_DIRECTORY_TRAINING_EXERCISES;
	g_pilot_data.mission_description_ids[0] = 11;
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.saved_mission_description_id = 12;
	g_pilot_data.mission_score = 50;
	g_pilot_data.kills_full_on_player[3] = 1;
	g_pilot_data.kills_shared_on_flight_group[47] = 1;
	g_pilot_data.kills_shared_from_flight_group[47] = 1;
	g_pilot_data.teams[9].mission_score = 1;
	memset(&g_pilot_data.last_mission_stats, 0x11,
	       sizeof g_pilot_data.last_mission_stats);
	struct pilot_network_player *entry = &g_pilot_data.network_players[0];
	entry->total_score = 9;
	entry->kills = 9;
	entry->kills_shared = 9;
	entry->craft_inspected = 9;
	entry->kills_assist = 9;
	entry->total_losses = 9;
	entry->has_left = 1;
	g_local_pilot_network_player_index = 6;
}

/* In single player the launch keeps only roster entry 0, zeroes the pilot's
 * mission results, and copies the local player into network player 0: the
 * name cut to 12 characters, the craft and rating, the warhead, beam and
 * countermeasure choices less 1, zeroed results and the flight group of its
 * assignment slot. The training sequence takes the player count, and the
 * faction is the group's IFF, whose faction_statistics entry takes the
 * pilot's choice. */
static void check_launch_single_player(void)
{
	launch_world();
	frontend_mission_init_player_state();
	XVT_ASSERT_INT_EQ(g_mp_roster[1].player_id, 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[4].player_id, 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].player_id, TEST_LOCAL_PLAYER);
	XVT_ASSERT_INT_EQ(g_pilot_data.mission_score, 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.kills_full_on_player[3], 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.kills_shared_on_flight_group[47], 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.kills_shared_from_flight_group[47], 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.teams[9].mission_score, 0);
	XVT_ASSERT_INT_EQ(((const uint8_t *)&g_pilot_data.last_mission_stats)
				  [sizeof g_pilot_data.last_mission_stats - 1],
			  0);
	XVT_ASSERT_INT_EQ(g_local_pilot_network_player_index, 0);

	struct pilot_network_player *entry = &g_pilot_data.network_players[0];
	XVT_ASSERT_TRUE(strcmp(entry->friendly_name, "ABCDEFGHIJKL") == 0);
	XVT_ASSERT_INT_EQ(entry->direct_play_id, TEST_LOCAL_PLAYER);
	XVT_ASSERT_INT_EQ(entry->rating, 3);
	XVT_ASSERT_INT_EQ(entry->craft_id, 0);
	XVT_ASSERT_INT_EQ(entry->craft_option, 2);
	XVT_ASSERT_INT_EQ(entry->warhead_option, 3);
	XVT_ASSERT_INT_EQ(entry->beam_option, 4);
	XVT_ASSERT_INT_EQ(entry->countermeasure_option, 5);
	XVT_ASSERT_INT_EQ(entry->flight_group_id, 3);
	XVT_ASSERT_INT_EQ(entry->total_score, 0);
	XVT_ASSERT_INT_EQ(entry->kills, 0);
	XVT_ASSERT_INT_EQ(entry->kills_shared, 0);
	XVT_ASSERT_INT_EQ(entry->craft_inspected, 0);
	XVT_ASSERT_INT_EQ(entry->kills_assist, 0);
	XVT_ASSERT_INT_EQ(entry->total_losses, 0);
	XVT_ASSERT_INT_EQ(entry->has_left, 0);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.campaign_sequence_state.human_player_count, 1);
	XVT_ASSERT_INT_EQ(g_pilot_data.battle_sequence_state.human_player_count,
			  0);

	XVT_ASSERT_INT_EQ(g_pilot_data.current_faction_id, 1);
	struct pilot_faction *faction = &g_pilot_data.faction_statistics[1];
	XVT_ASSERT_INT_EQ(faction->team, 2);
	XVT_ASSERT_INT_EQ(faction->mission_directory_id,
			  MISSION_DIRECTORY_TRAINING_EXERCISES);
	XVT_ASSERT_INT_EQ(faction->mission_description_ids[0], 11);
	XVT_ASSERT_INT_EQ(faction->mission_sequence_active, 1);
	XVT_ASSERT_INT_EQ(faction->saved_mission_description_id, 12);
	XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[0].team, 0);
}

/* A craft choice whose optional craft is CRAFT_SPECIES_UNKNOWN becomes -1. */
static void check_launch_unknown_optional_craft(void)
{
	launch_world();
	g_frontend_mission.flight_groups[3].optional_craft[2] =
		CRAFT_SPECIES_UNKNOWN;
	frontend_mission_init_player_state();
	XVT_ASSERT_INT_EQ(g_pilot_data.network_players[0].craft_option, -1);
}

/* With the local player in no roster entry, g_local_pilot_network_player_index
 * is left at the 0 the launch sets. */
static void check_launch_without_local_player(void)
{
	launch_world();
	g_front_state.net_players[0].player_id = 0x99;
	frontend_mission_init_player_state();
	XVT_ASSERT_INT_EQ(g_local_pilot_network_player_index, 0);
}

/* In a network game the launch drops only the roster entries of players
 * DirectPlay no longer lists, finds the local player at its own index, gives
 * a combat engagement sequence the player count, and puts the pilot's choice
 * in faction_statistics entry 2, turning off entries 0 and 1's sequences. */
static void check_launch_network(void)
{
	launch_world();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	g_pilot_data.mission_directory_id =
		MISSION_DIRECTORY_COMBAT_ENGAGEMENTS;
	g_pilot_data.mission_description_ids[3] = 13;
	g_pilot_data.faction_statistics[0].mission_sequence_active = 1;
	g_pilot_data.faction_statistics[1].mission_sequence_active = 1;
	/* The local player sits in roster entry 5 this time; entry 4's player
	 * has left DirectPlay's list and entry 6's has not. */
	g_mp_roster[5] = g_mp_roster[0];
	memset(&g_mp_roster[0], 0, sizeof g_mp_roster[0]);
	g_mp_roster[6].player_id = 0x43;
	g_front_state.net_players[1].player_id = 0x43;
	g_front_state.net_player_count = 2;
	frontend_mission_init_player_state();
	XVT_ASSERT_INT_EQ(g_mp_roster[4].player_id, 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[5].player_id, TEST_LOCAL_PLAYER);
	XVT_ASSERT_INT_EQ(g_mp_roster[6].player_id, 0x43);
	XVT_ASSERT_INT_EQ(g_pilot_data.network_players[6].direct_play_id, 0x43);
	XVT_ASSERT_INT_EQ(g_local_pilot_network_player_index, 5);
	XVT_ASSERT_INT_EQ(g_pilot_data.network_players[5].flight_group_id, 3);
	XVT_ASSERT_INT_EQ(g_pilot_data.battle_sequence_state.human_player_count,
			  2);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.campaign_sequence_state.human_player_count, 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.current_faction_id, 1);
	struct pilot_faction *network = &g_pilot_data.faction_statistics[2];
	XVT_ASSERT_INT_EQ(network->team, 2);
	XVT_ASSERT_INT_EQ(network->mission_directory_id,
			  MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	XVT_ASSERT_INT_EQ(network->mission_description_ids[3], 13);
	XVT_ASSERT_INT_EQ(network->mission_sequence_active, 1);
	XVT_ASSERT_INT_EQ(network->saved_mission_description_id, 12);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.faction_statistics[0].mission_sequence_active, 0);
	XVT_ASSERT_INT_EQ(
		g_pilot_data.faction_statistics[1].mission_sequence_active, 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[1].team, 0);
}

/* In a melee the faction follows the local player's craft: 0 for craft types
 * 1 to 4 and 14, else 1, and the other side's faction_statistics entry has
 * its sequence turned off. Single player then stores the melee and
 * tournament choices in the faction's entry. */
static void check_launch_melee_faction(void)
{
	static const struct {
		int craft;
		int faction;
	} cases[] = {{1, 0}, {4, 0}, {14, 0}, {5, 1}, {13, 1}, {15, 1}};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		launch_world();
		g_pilot_data.mission_directory_id = MISSION_DIRECTORY_MELEES;
		g_pilot_data.mission_sequence_active = 0;
		g_pilot_data.mission_description_ids[1] = 21;
		g_pilot_data.mission_description_ids[2] = 22;
		g_pilot_data.faction_statistics[0].mission_sequence_active = 1;
		g_pilot_data.faction_statistics[1].mission_sequence_active = 1;
		g_mp_roster[0].craft_type_override = cases[i].craft;
		frontend_mission_init_player_state();
		int faction = cases[i].faction;
		XVT_ASSERT_INT_EQ(g_pilot_data.current_faction_id, faction);
		XVT_ASSERT_INT_EQ(g_pilot_data.faction_statistics[1 - faction]
					  .mission_sequence_active,
				  0);
		struct pilot_faction *entry =
			&g_pilot_data.faction_statistics[faction];
		XVT_ASSERT_INT_EQ(entry->mission_description_ids[1], 21);
		XVT_ASSERT_INT_EQ(entry->mission_description_ids[2], 22);
		XVT_ASSERT_INT_EQ(entry->mission_sequence_active, 0);
	}
	/* With no craft chosen, the group's craft type decides. */
	launch_world();
	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_TOURNAMENTS;
	g_mp_roster[0].craft_option_index = -1;
	g_frontend_mission.flight_groups[3].craft_type = 2;
	frontend_mission_init_player_state();
	XVT_ASSERT_INT_EQ(g_pilot_data.current_faction_id, 0);
	/* A group craft type of 0 is not one of them. */
	launch_world();
	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_TOURNAMENTS;
	g_mp_roster[0].craft_option_index = -1;
	g_frontend_mission.flight_groups[3].craft_type = 0;
	frontend_mission_init_player_state();
	XVT_ASSERT_INT_EQ(g_pilot_data.current_faction_id, 1);
}

/* At the first mission of a melee sequence the launch marks the teams that
 * take part with 0 and the rest with -1, and stores the player and team
 * counts: in a network game without AI opponents only the teams with an
 * assigned player take part. */
static void check_launch_melee_first_mission(void)
{
	launch_world();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_MELEES;
	g_pilot_data.melee_tournament_sequence_state.current_mission_index = 0;
	g_team_count = 4;
	for (int team = 0; team < 4; ++team) {
		g_team_player_flight_group_count[team] = 2;
	}
	struct melee_tournament_sequence_state *state =
		&g_pilot_data.melee_tournament_sequence_state;
	for (int team = 0; team < 10; ++team) {
		state->team_standings[team]
			.ai_opponent_source_team_and_type_flag = 5;
	}
	frontend_mission_init_player_state();
	XVT_ASSERT_INT_EQ(
		state->team_standings[2].ai_opponent_source_team_and_type_flag,
		0);
	XVT_ASSERT_INT_EQ(
		state->team_standings[0].ai_opponent_source_team_and_type_flag,
		-1);
	XVT_ASSERT_INT_EQ(
		state->team_standings[9].ai_opponent_source_team_and_type_flag,
		-1);
	XVT_ASSERT_INT_EQ(state->human_player_count, 1);
	XVT_ASSERT_INT_EQ(state->participating_team_count, 1);

	/* With AI opponents every team takes part, and each player flight
	 * group's team is marked. */
	launch_world();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_MELEES;
	g_game_config.ai_opponents = 1;
	g_team_count = 4;
	g_frontend_mission.flight_group_count = 2;
	g_frontend_mission.flight_groups[1].player_number = 1;
	g_frontend_mission.flight_groups[1].team = 3;
	frontend_mission_init_player_state();
	XVT_ASSERT_INT_EQ(
		state->team_standings[3].ai_opponent_source_team_and_type_flag,
		0);
	XVT_ASSERT_INT_EQ(
		state->team_standings[2].ai_opponent_source_team_and_type_flag,
		0);
	XVT_ASSERT_INT_EQ(
		state->team_standings[1].ai_opponent_source_team_and_type_flag,
		-1);
	XVT_ASSERT_INT_EQ(state->participating_team_count, 4);
	g_game_config.ai_opponents = 0;
}

/* Known failure faction_past_table, issue #82: pilot_record.h gives the pilot
 * record four faction_statistics entries, and in single player the launch
 * picks one by the IFF of the local player's flight group. With that IFF at 4
 * the choice is written past the table, over the campaign and continuation
 * records that follow it. Its comment says it does not check the IFF; the fix
 * will have to change that. Nothing after the table should change. */
static void check_faction_past_table(void)
{
	static uint8_t before[sizeof g_pilot_data];
	launch_world();
	/* No sequence, so nothing else in those records is due to change. */
	g_pilot_data.mission_sequence_active = 0;
	g_frontend_mission.flight_groups[3].iff = 4;
	size_t start = offsetof(struct pilot_data, campaign_sequence_state);
	memcpy(before, &g_pilot_data, sizeof g_pilot_data);
	frontend_mission_init_player_state();
	XVT_ASSERT_TRUE(memcmp((const uint8_t *)&g_pilot_data + start,
			       before + start,
			       sizeof g_pilot_data - start) == 0);
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
			{"briefing_unlisted_mission",
			 check_briefing_unlisted_mission},
			{"briefing_label_past_buffer",
			 check_briefing_label_past_buffer},
			{"faction_past_table", check_faction_past_table},
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
	check_init_for_briefing();
	check_load_with_briefing();
	check_load_with_briefing_refusals();
	check_load_for_briefing();
	check_load_file();
	check_load_current();
	check_launch_single_player();
	check_launch_unknown_optional_craft();
	check_launch_without_local_player();
	check_launch_network();
	check_launch_melee_faction();
	check_launch_melee_first_mission();
	return 0;
}
