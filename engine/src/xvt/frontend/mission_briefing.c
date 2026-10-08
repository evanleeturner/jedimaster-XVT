#include "xvt/frontend/mission_briefing.h"

#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/mission_setup.h"

/* Per g_mp_roster entry, 1 once that player pressed Ready on the craft
 * selection screen. 7 functions write it: frontend_net_on_player_ready sets an
 * entry to 1 on the player's NET_PACKET_PLAYER_READY,
 * frontend_net_on_player_unready to 0 on NET_PACKET_PLAYER_UNREADY,
 * frontend_net_on_player_left clears all of them when a roster player leaves
 * and, on the host, frontend_net_on_player_unavailable when a player is
 * unavailable; mission_setup_update and mission_debrief_update clear entries;
 * mission_briefing_craft_fill_solo_roster sets entry 0 when a single player
 * flies. */
// GLOBAL: XVT 0xA91CA0
int g_mp_roster_ready_flags[8] = {0};
/* The kind of mission session the frontend runs: FRONTEND_MISSION_SESSION_NONE
 * (0), _SINGLEPLAYER (2), _NET_CLIENT (3) or _NET_HOST (4). Many functions
 * write it, chiefly concourse_update, pilot_create_new, pilot_load_from_path,
 * frontend_net_host_game_screen, the mission setup screens, the network browser
 * and session code and the mission dialogs. Most screens test it to tell single
 * player from network play. */
// GLOBAL: XVT 0xB69E30
frontend_mission_session_mode g_frontend_mission_session_mode =
	FRONTEND_MISSION_SESSION_NONE;

/* Passes the cursor, moved one pixel up and left, to
 * briefing_map_select_flight_group_at_cursor, which picks
 * g_briefing_selected_mission_point14_flight_group_idx by it. Returns 0 without
 * doing so when suppress_input is nonzero, else that function's result, 1.
 * The rectangles it passes, each inset by one pixel, and the button states
 * are ignored there, so the choice follows the cursor with or without a
 * button down. */
// FUNCTION: XVT 0x4F68C0
int16_t mission_briefing_handle_map_mouse_input(
	const struct RECT *viewport_rect, const struct RECT *clip_rect,
	int16_t suppress_input, int left_down, int right_down, int16_t mouse_x,
	int16_t mouse_y)
{
	struct RECT inset_viewport_rect;

	frontend_draw_rect_copy(&inset_viewport_rect, viewport_rect);
	frontend_draw_rect_inset_xy(&inset_viewport_rect, 1, 1);
	struct RECT inset_clip_rect;
	frontend_draw_rect_copy(&inset_clip_rect, clip_rect);
	frontend_draw_rect_inset_xy(&inset_clip_rect, 1, 1);
	if (suppress_input != 0) {
		return 0;
	}
	return briefing_map_select_flight_group_at_cursor(
		&inset_viewport_rect, &inset_clip_rect, left_down, right_down,
		(int16_t)(mouse_x - 1), (int16_t)(mouse_y - 1));
}

/* Draws the briefing map panel through
 * briefing_map_draw_viewport_and_selection, on copies of the two rectangles, and
 * returns its result, which is always 1; highlight_phase is ignored there. */
// FUNCTION: XVT 0x4F6970
int16_t mission_briefing_draw_map_viewport(const struct RECT *viewport_rect,
					   const struct RECT *clip_rect,
					   int16_t highlight_phase)
{
	struct RECT viewport_copy;

	frontend_draw_rect_copy(&viewport_copy, viewport_rect);
	struct RECT clip_copy;
	frontend_draw_rect_copy(&clip_copy, clip_rect);
	return briefing_map_draw_viewport_and_selection(
		&viewport_copy, &clip_copy, highlight_phase);
}
