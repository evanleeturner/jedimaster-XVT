/* Checks the network host and join dialogs
 * (xvt_runtime/runtime/network_dialogs.h) against the promises in its header:
 * the access tails, the return to the host or join screen and what it clears,
 * the connecting screen's cancel button, the failure dialog with its return
 * after dismissal or at once, and the admission check. The frontend runs on a
 * display with no window (test_frontend_display.h); its fonts and images are
 * not loaded, so the screens draw nothing visible, and the dialogs are
 * dismissed with Escape. Every case starts from a cleared frontend with a
 * placeholder screen, no dialog and no network session.
 *
 * Not checked here: the text each error shows, beyond there being one. */
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "test_frontend_display.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/net/frontend_net.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/network_dialogs.h"
#include "xvt_runtime/runtime/network_session.h"

static int placeholder(int frame) { return frame; }

static void fresh(void)
{
	xvt_dialog_shutdown();
	xvt_network_session_shutdown();
	xvt_test_close_display();
	memset(&g_front_state, 0, sizeof g_front_state);
	xvt_test_open_display();
	g_front_state.screen_states[0].update_fn = placeholder;
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NONE;
	g_frontend_skip_screen_entry_setup = 0;
	g_game_config.sfx_datapad_enabled = 0;
}

static frontend_screen_update_fn top_screen(void)
{
	return g_front_state.screen_states[g_front_state.screen_stack_top]
		.update_fn;
}

/* Runs the open dialog's first frame, then dismisses it with Escape. */
static void dismiss_dialog(void)
{
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
	xvt_dialog_update();
	g_front_state.char_ring_buffer[g_front_state.char_write_idx++] = 27;
	xvt_dialog_update();
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
}

/* Fills the state Return clears with values it must not keep. */
static void dirty(void)
{
	g_frontend_net_selected_session_idx = 3;
	g_frontend_net_probe_mission_elapsed_seconds = 2;
	g_frontend_net_received_mission_description_id = 17;
	memset(g_frontend_net_selected_game_name, 'g',
	       sizeof g_frontend_net_selected_game_name - 1);
}

static void check_cleared(void)
{
	XVT_ASSERT_INT_EQ(g_frontend_skip_screen_entry_setup, 1);
	XVT_ASSERT_INT_EQ(g_frontend_net_selected_session_idx, -1);
	XVT_ASSERT_INT_EQ(g_frontend_net_probe_mission_elapsed_seconds, 0);
	XVT_ASSERT_INT_EQ(g_frontend_net_received_mission_description_id, -1);
	for (unsigned i = 0; i < sizeof g_frontend_net_selected_game_name;
	     ++i) {
		XVT_ASSERT_INT_EQ(g_frontend_net_selected_game_name[i], 0);
	}
}

static void check_return(void)
{
	fresh();
	dirty();
	g_mission_text = malloc(4096);
	XVT_ASSERT_TRUE(g_mission_text != NULL);
	memset(g_mission_text, 'b', 4096);
	xvt_network_dialogs_return(1);
	XVT_ASSERT_TRUE(top_screen() == frontend_net_host_game_screen);
	XVT_ASSERT_INT_EQ(g_frontend_mission_session_mode,
			  FRONTEND_MISSION_SESSION_NET_HOST);
	check_cleared();
	for (int i = 0; i < 4096; ++i) {
		XVT_ASSERT_INT_EQ(g_mission_text[i], 0);
	}
	free(g_mission_text);
	g_mission_text = NULL;

	/* The join screen, with no briefing text allocated. */
	fresh();
	dirty();
	xvt_network_dialogs_return(0);
	XVT_ASSERT_TRUE(top_screen() == frontend_net_join_game_screen);
	XVT_ASSERT_INT_EQ(g_frontend_mission_session_mode,
			  FRONTEND_MISSION_SESSION_NET_CLIENT);
	check_cleared();
}

static void check_resume(void)
{
	for (int result = 0; result < 2; ++result) {
		fresh();
		XVT_ASSERT_INT_EQ(xvt_network_dialogs_resume(
					  result, XVT_NETWORK_ACCESS_REJECTED),
				  0);
		XVT_ASSERT_TRUE(top_screen() == frontend_net_join_game_screen);
		XVT_ASSERT_TRUE(g_front_state.pending_screen_update_fn == NULL);

		/* A password first queues the options datapad. */
		fresh();
		XVT_ASSERT_INT_EQ(xvt_network_dialogs_resume(
					  result, XVT_NETWORK_ACCESS_PASSWORD),
				  0);
		XVT_ASSERT_TRUE(top_screen() == frontend_net_join_game_screen);
		XVT_ASSERT_TRUE(g_front_state.pending_screen_update_fn ==
				config_options_datapad_update);
	}
}

static void check_connecting(void)
{
	fresh();
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	/* Not clicked, or clicked outside the cancel button. */
	g_front_state.mouse_x = 100;
	g_front_state.mouse_y = 460;
	XVT_ASSERT_INT_EQ(xvt_network_dialogs_connecting(), 0);
	g_front_state.mouse_x = 300;
	g_front_state.mouse_left_click_latch = 1;
	XVT_ASSERT_INT_EQ(xvt_network_dialogs_connecting(), 0);
	/* Clicked on it. */
	g_front_state.mouse_x = 100;
	XVT_ASSERT_INT_EQ(xvt_network_dialogs_connecting(), 1);
	frontend_display_unlock_back_buffer();
}

static void check_failed_waits_for_dismissal(void)
{
	for (int host = 0; host < 2; ++host) {
		fresh();
		XVT_ASSERT_INT_EQ(
			xvt_network_session_begin_host(NULL, "Luke", "", 0), 0);
		XVT_ASSERT_INT_EQ(xvt_network_session_get_status().state,
				  XVT_NETWORK_SESSION_FAILED);
		xvt_network_dialogs_show_failure(
			AERON_DPLAY_DIRECTORY_ERROR_TIMEOUT, host);

		/* The session is reset, and the message waits in a confirm dialog. */
		XVT_ASSERT_TRUE(xvt_network_session_get_status().state !=
				XVT_NETWORK_SESSION_FAILED);
		XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
		XVT_ASSERT_TRUE(g_front_dialog_line1_or_edit[0] != 0);
		XVT_ASSERT_TRUE(top_screen() == placeholder);

		/* Once dismissed, the frontend resumes the dialog's
		 * continuation, which returns to the screen. */
		dismiss_dialog();
		int frame_result = -1;
		XVT_ASSERT_INT_EQ(xvt_dialog_resume_continuation(&frame_result),
				  1);
		XVT_ASSERT_TRUE(top_screen() ==
				(host ? frontend_net_host_game_screen
				      : frontend_net_join_game_screen));
		XVT_ASSERT_INT_EQ(g_frontend_mission_session_mode,
				  host ? FRONTEND_MISSION_SESSION_NET_HOST
				       : FRONTEND_MISSION_SESSION_NET_CLIENT);
	}
}

static void check_failed_every_error_has_a_message(void)
{
	for (int error = AERON_DPLAY_DIRECTORY_ERROR_NONE;
	     error <= AERON_DPLAY_DIRECTORY_ERROR_BUSY; ++error) {
		fresh();
		memset(g_front_dialog_line1_or_edit, 0,
		       sizeof g_front_dialog_line1_or_edit);
		xvt_network_dialogs_show_failure(
			(AeronDplayDirectoryError)error, 0);
		XVT_ASSERT_TRUE(g_front_dialog_line1_or_edit[0] != 0);
	}
}

static void check_failed_returns_at_once(void)
{
	fresh();
	/* An untaken result from an earlier dialog: the failure's Confirm takes
	 * it and does not wait. */
	XVT_ASSERT_INT_EQ(
		xvt_dialog_confirm("earlier", NULL, NULL, NULL, NULL, 0),
		XVT_DIALOG_PENDING);
	dismiss_dialog();
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 1);
	xvt_network_dialogs_show_failure(AERON_DPLAY_DIRECTORY_ERROR_FULL, 1);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
	XVT_ASSERT_TRUE(top_screen() == frontend_net_host_game_screen);
}

static void check_admission_failed(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_dialogs_report_admission_failure(), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_join("\x02", "Luke", NULL),
			  0);
	XVT_ASSERT_INT_EQ(xvt_network_session_get_status().state,
			  XVT_NETWORK_SESSION_FAILED);

	/* Reported as a join's failure: once dismissed, back to the join screen. */
	XVT_ASSERT_INT_EQ(xvt_network_dialogs_report_admission_failure(), 1);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
	dismiss_dialog();
	int frame_result = -1;
	XVT_ASSERT_INT_EQ(xvt_dialog_resume_continuation(&frame_result), 1);
	XVT_ASSERT_TRUE(top_screen() == frontend_net_join_game_screen);
}

int main(void)
{
	check_return();
	check_resume();
	check_connecting();
	check_failed_waits_for_dismissal();
	check_failed_every_error_has_a_message();
	check_failed_returns_at_once();
	check_admission_failed();
	xvt_dialog_shutdown();
	xvt_network_session_shutdown();
	xvt_test_close_display();
	return 0;
}
