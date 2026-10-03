/* Checks the join-game screen (xvt_runtime/runtime/network_browser.h) against the promises in its header
 * that hold without a multiplayer directory or the game's art: with an empty room list the list draw
 * clicks nothing and keeps the scroll offset in range, the roster and mission draws return 1, the first
 * frame's setup clears its flags, opens the browser and shows the unconfigured-directory dialog, and on
 * later frames Leave returns to the concourse, Leave and Join stay hidden behind a dialog, and Join is
 * offered only when the network task allows a join. The frontend runs on a display with no window
 * (test_frontend_display.h) with no fonts or images loaded, so nothing visible is drawn. Every case starts
 * from a cleared frontend, no dialog, no settings and an idle network task.
 *
 * Not checked here: rows, colors, the roster and the preview of real rooms, which need a multiplayer
 * directory, and the shared frontend controls, which act through the game's own dialogs. */
#include "test_assert.h"
#include "test_frontend_display.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_actions.h"
#include "xvt_runtime/runtime/network_browser.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/network_task.h"

#include <string.h>

/* The Leave and Join buttons, from the screen's layout. */
enum { LEAVE_X = 120, LEAVE_Y = 460, JOIN_X = 40, JOIN_Y = 440 };

static int placeholder(int frame) { return frame; }

static void fresh(void)
{
	xvt_dialog_shutdown();
	xvt_network_task_shutdown();
	xvt_network_session_shutdown();
	xvt_frontend_action_reset();
	xvt_test_close_display();
	memset(&g_front_state, 0, sizeof g_front_state);
	xvt_test_open_display();
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	g_front_state.screen_states[0].update_fn = placeholder;
	g_game_config.sfx_datapad_enabled = 0;
	g_game_config.help_on = 0;
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_CLIENT;
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
}

static frontend_screen_update_fn top_screen(void)
{
	return g_front_state.screen_states[g_front_state.screen_stack_top]
		.update_fn;
}

static void click(int x, int y)
{
	g_front_state.mouse_x = x;
	g_front_state.mouse_y = y;
	g_front_state.mouse_left_click_latch = 1;
}

static void check_draws_with_no_rooms(void)
{
	fresh();
	*xvt_network_task_scroll_offset() = 4;
	click(200, 120);
	XVT_ASSERT_INT_EQ(xvt_network_browser_draw_list(), -1);
	XVT_ASSERT_INT_EQ(*xvt_network_task_scroll_offset(), 0);
	XVT_ASSERT_INT_EQ(xvt_network_browser_draw_roster(), 1);
	XVT_ASSERT_INT_EQ(xvt_network_browser_draw_mission(), 1);
	strcpy(xvt_network_task_preview()->title, "Title");
	XVT_ASSERT_INT_EQ(xvt_network_browser_draw_mission(), 1);
}

static void check_first_frame(void)
{
	fresh();
	g_frontend_skip_screen_entry_setup = 1;
	g_config_connection_type_editable = 1;
	g_frontend_game_session_in_progress = 1;
	XVT_ASSERT_INT_EQ(xvt_network_browser_screen(0), 0);
	XVT_ASSERT_INT_EQ(g_frontend_skip_screen_entry_setup, 0);
	XVT_ASSERT_INT_EQ(g_config_connection_type_editable, 0);
	XVT_ASSERT_INT_EQ(g_frontend_game_session_in_progress, 0);

	/* The browser was opened, and with no directory configured a confirm dialog is up. */
	XVT_ASSERT_INT_EQ(xvt_network_task_browser_error(),
			  AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
	XVT_ASSERT_TRUE(top_screen() == placeholder);
}

static void check_leave(void)
{
	fresh();
	click(LEAVE_X, LEAVE_Y);
	XVT_ASSERT_INT_EQ(xvt_network_browser_screen(1), 0);
	XVT_ASSERT_TRUE(top_screen() == concourse_update);
}

static void check_dialog_hides_buttons(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_dialog_begin(placeholder, NULL),
			  XVT_DIALOG_PENDING);
	click(LEAVE_X, LEAVE_Y);
	XVT_ASSERT_INT_EQ(xvt_network_browser_screen(1), 0);
	XVT_ASSERT_TRUE(top_screen() == placeholder);
}

static void check_join_needs_the_task(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_task_can_join(), 0);
	click(JOIN_X, JOIN_Y);
	XVT_ASSERT_INT_EQ(xvt_network_browser_screen(1), 0);
	XVT_ASSERT_INT_EQ(xvt_network_task_is_active(), 0);
	XVT_ASSERT_TRUE(top_screen() == placeholder);
}

int main(void)
{
	check_draws_with_no_rooms();
	check_first_frame();
	check_leave();
	check_dialog_hides_buttons();
	check_join_needs_the_task();
	xvt_dialog_shutdown();
	xvt_network_task_shutdown();
	xvt_network_session_shutdown();
	xvt_test_close_display();
	return 0;
}
