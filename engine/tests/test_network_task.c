/* Checks the frontend side of multiplayer (xvt_runtime/runtime/network_task.h) against the promises in its
 * header, as far as they hold with no multiplayer directory: the room compatibility check, the browser's
 * state with an empty snapshot (no selection, no preview, a clamped scroll), a refresh that records the
 * directory's error, when the browser counts as visible, which attempts Begin starts or ignores, and what
 * Resume and Cancel do with an attempt or a failed session. The frontend runs on a display with no window
 * (test_frontend_display.h). Every case starts from Shutdown, a cleared frontend and no settings loaded,
 * so the directory is never configured and no request leaves the machine.
 *
 * Not checked here: a snapshot with rooms, selection kept across refreshes, the mission preview, and a
 * join or host attempt past its first step; they need a multiplayer directory and a DirectPlay peer. */
#include <stdio.h>
#include <string.h>

#include "aeron/aeron.h"
#include "test_assert.h"
#include "test_frontend_display.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/network_task.h"

static AeronDplayDirectoryRoom g_test_room;

static int placeholder(int frame) { return frame; }

static void fresh(void)
{
	xvt_network_task_shutdown();
	xvt_network_session_shutdown();
	xvt_dialog_shutdown();
	xvt_test_close_display();
	memset(&g_front_state, 0, sizeof g_front_state);
	xvt_test_open_display();
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	strcpy(g_pilot_data.name, "Luke");
	g_front_state.screen_states[0].update_fn = placeholder;
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	memset((AeronInputSnapshot *)Aeron_InputSnapshot(), 0,
	       sizeof(AeronInputSnapshot));
}

static frontend_screen_update_fn top_screen(void)
{
	return g_front_state.screen_states[g_front_state.screen_stack_top]
		.update_fn;
}

static void check_compatible(void)
{
	memset(&g_test_room, 0, sizeof g_test_room);
	g_test_room.protocol = AERON_DPLAY_DIRECTORY_PROTOCOL;
	snprintf(g_test_room.game_version, sizeof g_test_room.game_version,
		 "%d", FRONTEND_NET_PROTOCOL_VERSION);
	XVT_ASSERT_INT_EQ(xvt_network_task_compatible(&g_test_room), 1);
	XVT_ASSERT_INT_EQ(xvt_network_task_compatible(NULL), 0);

	/* Another directory protocol. */
	g_test_room.protocol = AERON_DPLAY_DIRECTORY_PROTOCOL + 1;
	XVT_ASSERT_INT_EQ(xvt_network_task_compatible(&g_test_room), 0);
	g_test_room.protocol = AERON_DPLAY_DIRECTORY_PROTOCOL;

	/* Another game version, or the same number written differently. */
	snprintf(g_test_room.game_version, sizeof g_test_room.game_version,
		 "%d", FRONTEND_NET_PROTOCOL_VERSION + 1);
	XVT_ASSERT_INT_EQ(xvt_network_task_compatible(&g_test_room), 0);
	snprintf(g_test_room.game_version, sizeof g_test_room.game_version,
		 "0%d", FRONTEND_NET_PROTOCOL_VERSION);
	XVT_ASSERT_INT_EQ(xvt_network_task_compatible(&g_test_room), 0);
	snprintf(g_test_room.game_version, sizeof g_test_room.game_version,
		 "%d ", FRONTEND_NET_PROTOCOL_VERSION);
	XVT_ASSERT_INT_EQ(xvt_network_task_compatible(&g_test_room), 0);
}

static void check_empty_browser(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_task_snapshot()->room_count, 0);
	XVT_ASSERT_TRUE(xvt_network_task_selected_room() == NULL);
	XVT_ASSERT_INT_EQ(xvt_network_task_selected_index(), -1);
	XVT_ASSERT_INT_EQ(xvt_network_task_snapshot_age(), 0);
	XVT_ASSERT_INT_EQ(xvt_network_task_browser_error(),
			  AERON_DPLAY_DIRECTORY_ERROR_NONE);
	XVT_ASSERT_INT_EQ(xvt_network_task_can_join(), 0);
	XVT_ASSERT_INT_EQ(xvt_network_task_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_network_task_preview()->title[0], 0);
	XVT_ASSERT_INT_EQ(xvt_network_task_preview()->text[0], 0);

	/* An index out of range clears the selection and the preview. */
	strcpy(xvt_network_task_preview()->title, "stale");
	strcpy(xvt_network_task_preview()->text, "stale");
	xvt_network_task_toggle_selection(0);
	XVT_ASSERT_INT_EQ(xvt_network_task_selected_index(), -1);
	XVT_ASSERT_TRUE(xvt_network_task_selected_room() == NULL);
	XVT_ASSERT_INT_EQ(xvt_network_task_preview()->title[0], 0);
	XVT_ASSERT_INT_EQ(xvt_network_task_preview()->text[0], 0);
	xvt_network_task_toggle_selection(-3);
	XVT_ASSERT_INT_EQ(xvt_network_task_selected_index(), -1);

	/* The scroll offset is the browser's own, for the list draw to read and write. */
	XVT_ASSERT_TRUE(xvt_network_task_scroll_offset() ==
			xvt_network_task_scroll_offset());
	*xvt_network_task_scroll_offset() = 4;
	XVT_ASSERT_INT_EQ(*xvt_network_task_scroll_offset(), 4);
}

static void check_refresh_records_error(void)
{
	fresh();
	xvt_network_task_refresh();
	XVT_ASSERT_INT_EQ(xvt_network_task_browser_error(),
			  AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED);
	XVT_ASSERT_INT_EQ(xvt_network_task_can_join(), 0);
}

static void check_open_browser(void)
{
	fresh();
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	xvt_network_task_open_browser();
	/* Client mode, a refresh started (and refused for want of a directory), any session left. */
	XVT_ASSERT_INT_EQ(g_frontend_mission_session_mode,
			  FRONTEND_MISSION_SESSION_NET_CLIENT);
	XVT_ASSERT_INT_EQ(xvt_network_task_browser_error(),
			  AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED);
	XVT_ASSERT_INT_EQ(xvt_network_session_get_status().state,
			  XVT_NETWORK_SESSION_PENDING);
}

static void check_browser_visible(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_task_browser_visible(), 0);
	/* The join screen anywhere on the stack. */
	g_front_state.screen_states[0].update_fn =
		frontend_net_join_game_screen;
	g_front_state.screen_states[1].update_fn = placeholder;
	g_front_state.screen_stack_top = 1;
	XVT_ASSERT_INT_EQ(xvt_network_task_browser_visible(), 1);
	g_front_state.screen_stack_top = 0;
	XVT_ASSERT_INT_EQ(xvt_network_task_browser_visible(), 1);
	/* ...but not past the top of the stack. */
	g_front_state.screen_states[0].update_fn = placeholder;
	g_front_state.screen_states[1].update_fn =
		frontend_net_join_game_screen;
	XVT_ASSERT_INT_EQ(xvt_network_task_browser_visible(), 0);

	/* Not while an attempt runs. */
	g_front_state.screen_states[0].update_fn =
		frontend_net_join_game_screen;
	xvt_network_task_begin(XVT_NETWORK_HOST);
	XVT_ASSERT_INT_EQ(xvt_network_task_is_active(), 1);
	XVT_ASSERT_INT_EQ(xvt_network_task_browser_visible(), 0);
}

static void check_begin(void)
{
	/* A join needs CanJoin; with no room selected it is ignored. */
	fresh();
	xvt_network_task_begin(XVT_NETWORK_CONNECT);
	XVT_ASSERT_INT_EQ(xvt_network_task_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_network_session_get_status().state,
			  XVT_NETWORK_SESSION_IDLE);

	/* Either host action starts a host attempt in the network session. */
	const int hosts[] = {XVT_NETWORK_HOST, XVT_NETWORK_AUTO_HOST};
	for (unsigned i = 0; i < 2; ++i) {
		fresh();
		xvt_network_task_begin(hosts[i]);
		XVT_ASSERT_INT_EQ(xvt_network_task_is_active(), 1);
		XVT_ASSERT_INT_EQ(xvt_network_session_get_status().state,
				  XVT_NETWORK_SESSION_PENDING);
		XVT_ASSERT_INT_EQ(xvt_network_task_can_join(), 0);
	}

	/* While an attempt runs, Begin is ignored. */
	fresh();
	xvt_network_task_begin(XVT_NETWORK_HOST);
	xvt_network_session_shutdown();
	xvt_network_task_begin(XVT_NETWORK_AUTO_HOST);
	XVT_ASSERT_INT_EQ(xvt_network_session_get_status().state,
			  XVT_NETWORK_SESSION_IDLE);
	XVT_ASSERT_INT_EQ(xvt_network_task_is_active(), 1);
}

static void check_resume_without_attempt(void)
{
	int result = 77;
	fresh();
	XVT_ASSERT_INT_EQ(xvt_network_task_resume(&result), 0);
	XVT_ASSERT_INT_EQ(result, 77);

	/* A failed session shows the failure dialog, with result 0. */
	XVT_ASSERT_INT_EQ(xvt_network_session_begin_host(NULL, "Luke", "", 0),
			  0);
	XVT_ASSERT_INT_EQ(xvt_network_task_resume(&result), 1);
	XVT_ASSERT_INT_EQ(result, 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
}

static void check_resume_during_attempt(void)
{
	int result = 77;
	fresh();
	xvt_network_task_begin(XVT_NETWORK_HOST);
	/* The session is ticked; it is still closing the previous session, so the attempt goes on. */
	XVT_ASSERT_INT_EQ(xvt_network_task_resume(&result), 1);
	XVT_ASSERT_INT_EQ(result, 0);
	XVT_ASSERT_INT_EQ(xvt_network_task_is_active(), 1);

	/* Escape with the window focused cancels, returning to the host screen. */
	AeronInputSnapshot *input = (AeronInputSnapshot *)Aeron_InputSnapshot();
	input->has_focus = 1;
	input->key_pressed[AERON_KEY_ESCAPE] = 1;
	result = 77;
	XVT_ASSERT_INT_EQ(xvt_network_task_resume(&result), 1);
	XVT_ASSERT_INT_EQ(result, 0);
	XVT_ASSERT_INT_EQ(xvt_network_task_is_active(), 0);
	XVT_ASSERT_TRUE(top_screen() == frontend_net_host_game_screen);
}

static void check_resume_finishes_failed_attempt(void)
{
	int result = 77;
	fresh();
	xvt_network_task_begin(XVT_NETWORK_HOST);
	/* The session fails during the attempt: the attempt ends and the failure dialog opens. */
	xvt_network_session_host_lost();
	XVT_ASSERT_INT_EQ(xvt_network_task_resume(&result), 1);
	XVT_ASSERT_INT_EQ(result, 0);
	XVT_ASSERT_INT_EQ(xvt_network_task_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
}

static void check_cancel(void)
{
	/* A host attempt returns to the host screen, an auto host too. */
	const int hosts[] = {XVT_NETWORK_HOST, XVT_NETWORK_AUTO_HOST};
	for (unsigned i = 0; i < 2; ++i) {
		fresh();
		xvt_network_task_begin(hosts[i]);
		xvt_network_task_cancel();
		XVT_ASSERT_INT_EQ(xvt_network_task_is_active(), 0);
		XVT_ASSERT_TRUE(top_screen() == frontend_net_host_game_screen);
	}
}

static void check_shutdown(void)
{
	fresh();
	xvt_network_task_refresh();
	*xvt_network_task_scroll_offset() = 3;
	xvt_network_task_begin(XVT_NETWORK_HOST);
	xvt_network_task_shutdown();
	XVT_ASSERT_INT_EQ(xvt_network_task_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_network_task_browser_error(),
			  AERON_DPLAY_DIRECTORY_ERROR_NONE);
	XVT_ASSERT_INT_EQ(xvt_network_task_selected_index(), -1);
	XVT_ASSERT_INT_EQ(*xvt_network_task_scroll_offset(), 0);
	/* The network session is reset. */
	XVT_ASSERT_INT_EQ(xvt_network_session_get_status().state,
			  XVT_NETWORK_SESSION_PENDING);
}

int main(void)
{
	check_compatible();
	check_empty_browser();
	check_refresh_records_error();
	check_open_browser();
	check_browser_visible();
	check_begin();
	check_resume_without_attempt();
	check_resume_during_attempt();
	check_resume_finishes_failed_attempt();
	check_cancel();
	check_shutdown();
	xvt_network_task_shutdown();
	xvt_network_session_shutdown();
	xvt_dialog_shutdown();
	xvt_test_close_display();
	return 0;
}
