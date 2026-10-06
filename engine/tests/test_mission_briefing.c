/* Tests for xvt/frontend/mission_briefing.c, the craft selection screen:
 * its ready test, its briefing map hooks and its exit. The ready checks set
 * the session's players and the roster's ready flags in the game's own
 * tables. The map checks run on a frontend display with no window
 * (test_frontend_display.h). No game data is read.
 *
 * Not checked here: the screen's frame, mission_briefing_craft_selection_update,
 * and the launch packet it sends; they need the game's mission, ship and
 * model files and a network session. */
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "test_frontend_display.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_script.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_briefing.h"
#include "xvt/frontend/mission_setup.h"

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

int main(void)
{
	memset(&g_front_state, 0, sizeof g_front_state);
	check_all_players_ready();
	xvt_test_open_display();
	check_map_mouse_input();
	check_draw_map_viewport();
	check_exit();
	xvt_test_close_display();
	return 0;
}
