/* Checks the mission screens' dialog tails (xvt_runtime/runtime/mission_dialogs.h) against the promises in
 * its header: which screen each action opens, which actions ignore the dialog's result and which run only
 * on a nonzero one, the session shutdowns, the flags and roster the solo debrief abort clears, and that
 * every call turns the overlay text off and returns 0. The test sets the frontend globals itself; every
 * case starts with a placeholder screen on the stack, overlay text on, and no network session.
 *
 * A session shutdown is seen through the network session: net_shutdown_direct_play_session reports every
 * shutdown to it, and it then reads as pending until the close completes.
 *
 * Not checked here: the packets HOST_RESTART, DEBRIEF_HOST_ABORT and the leave actions send, which need a
 * DirectPlay session. */
#include <string.h>

#include "test_assert.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/net/frontend_net.h"
#include "xvt_runtime/runtime/mission_dialogs.h"
#include "xvt_runtime/runtime/network_session.h"

static int placeholder(int frame) { return frame; }

static void fresh(int session_mode)
{
	xvt_network_session_shutdown();
	memset(&g_front_state, 0, sizeof g_front_state);
	g_front_state.screen_states[0].update_fn = placeholder;
	g_frontend_mission_session_mode = session_mode;
	g_frontend_skip_screen_entry_setup = 0;
	g_frontend_quick_start_launch_flag = 0;
	g_frontend_game_session_in_progress = 0;
	g_mission_setup_roster_authoritative = 0;
	memset(g_mp_roster, 0, sizeof g_mp_roster);
	frontend_button_enable_overlay_text();
}

static frontend_screen_update_fn screen(void)
{
	return g_front_state.screen_states[g_front_state.screen_stack_top]
		.update_fn;
}

static int session_closing(void)
{
	return xvt_network_session_get_status().state ==
	       XVT_NETWORK_SESSION_PENDING;
}

/* Runs action with result and checks the promise every call keeps. */
static void resume(int result, int action)
{
	XVT_ASSERT_INT_EQ(xvt_mission_dialogs_resume(result, action), 0);
	XVT_ASSERT_INT_EQ(frontend_button_is_overlay_text_enabled(), 0);
}

static void check_notice(void)
{
	for (int result = 0; result < 2; ++result) {
		fresh(FRONTEND_MISSION_SESSION_NET_HOST);
		resume(result, XVT_MISSION_NOTICE);
		XVT_ASSERT_TRUE(screen() == placeholder);
		XVT_ASSERT_INT_EQ(session_closing(), 0);
	}
}

static void check_cancelled_actions_ignore_result(void)
{
	const int actions[] = {
		XVT_MISSION_SETUP_CANCELLED, XVT_MISSION_SETUP_BOOTED,
		XVT_MISSION_TEAM_CANCELLED, XVT_MISSION_ASSIGNMENT_CANCELLED,
		XVT_MISSION_BRIEFING_CANCELLED};
	for (unsigned i = 0; i < sizeof actions / sizeof actions[0]; ++i) {
		for (int result = 0; result < 2; ++result) {
			fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
			g_front_state.net_is_host = 0;
			resume(result, actions[i]);
			XVT_ASSERT_TRUE(screen() ==
					frontend_net_join_game_screen);
		}
	}

	/* TEAM_CANCELLED also shuts down the session. */
	fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	resume(0, XVT_MISSION_TEAM_CANCELLED);
	XVT_ASSERT_INT_EQ(session_closing(), 1);

	/* BRIEFING_CANCELLED on the host returns to the concourse instead. */
	for (int result = 0; result < 2; ++result) {
		fresh(FRONTEND_MISSION_SESSION_NET_HOST);
		g_front_state.net_is_host = 1;
		resume(result, XVT_MISSION_BRIEFING_CANCELLED);
		XVT_ASSERT_TRUE(screen() == concourse_update);
	}
}

static void check_team_previous(void)
{
	/* Solo, either result reopens mission setup. */
	for (int result = 0; result < 2; ++result) {
		fresh(FRONTEND_MISSION_SESSION_SINGLEPLAYER);
		resume(result, XVT_MISSION_TEAM_PREVIOUS);
		XVT_ASSERT_TRUE(screen() == mission_setup_update);
	}
	/* A network session sends return-to-setup instead of reopening it. */
	fresh(FRONTEND_MISSION_SESSION_NET_HOST);
	resume(0, XVT_MISSION_TEAM_PREVIOUS);
	XVT_ASSERT_TRUE(screen() != mission_setup_update);
}

static void check_tails_need_nonzero_result(void)
{
	const int actions[] = {XVT_MISSION_SETUP_HOST_LEAVE,
			       XVT_MISSION_CLIENT_LEAVE,
			       XVT_MISSION_TEAM_CLIENT_LEAVE,
			       XVT_MISSION_SOLO_BACK_TO_SETUP,
			       XVT_MISSION_SOLO_BACK_TO_TEAMS,
			       XVT_MISSION_DEBRIEF_CLIENT_LEAVE,
			       XVT_MISSION_DEBRIEF_SOLO_ABORT,
			       XVT_MISSION_DEBRIEF_SOLO_ABORT_CLEAR_ROSTER,
			       XVT_MISSION_HOST_RESTART,
			       XVT_MISSION_DEBRIEF_HOST_ABORT};
	for (unsigned i = 0; i < sizeof actions / sizeof actions[0]; ++i) {
		fresh(FRONTEND_MISSION_SESSION_NET_HOST);
		g_frontend_quick_start_launch_flag = 1;
		g_frontend_game_session_in_progress = 1;
		g_mission_setup_roster_authoritative = 1;
		g_mp_roster[0].player_id = 5;
		resume(0, actions[i]);
		XVT_ASSERT_TRUE(screen() == placeholder);
		XVT_ASSERT_INT_EQ(session_closing(), 0);
		XVT_ASSERT_INT_EQ(g_frontend_quick_start_launch_flag, 1);
		XVT_ASSERT_INT_EQ(g_frontend_game_session_in_progress, 1);
		XVT_ASSERT_INT_EQ(g_mission_setup_roster_authoritative, 1);
		XVT_ASSERT_INT_EQ(g_mp_roster[0].player_id, 5);
	}
}

static void check_leave_actions(void)
{
	fresh(FRONTEND_MISSION_SESSION_NET_HOST);
	resume(1, XVT_MISSION_SETUP_HOST_LEAVE);
	XVT_ASSERT_TRUE(screen() == concourse_update);
	XVT_ASSERT_INT_EQ(session_closing(), 1);

	fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	resume(1, XVT_MISSION_CLIENT_LEAVE);
	XVT_ASSERT_TRUE(screen() == frontend_net_join_game_screen);
	XVT_ASSERT_INT_EQ(session_closing(), 1);

	fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	resume(7, XVT_MISSION_TEAM_CLIENT_LEAVE);
	XVT_ASSERT_TRUE(screen() == frontend_net_join_game_screen);
	XVT_ASSERT_INT_EQ(session_closing(), 1);

	/* From the debrief, a leaving client goes to the concourse. */
	fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	resume(1, XVT_MISSION_DEBRIEF_CLIENT_LEAVE);
	XVT_ASSERT_TRUE(screen() == concourse_update);
	XVT_ASSERT_INT_EQ(session_closing(), 1);
}

static void check_solo_back(void)
{
	fresh(FRONTEND_MISSION_SESSION_SINGLEPLAYER);
	resume(1, XVT_MISSION_SOLO_BACK_TO_SETUP);
	XVT_ASSERT_TRUE(screen() == mission_setup_update);

	fresh(FRONTEND_MISSION_SESSION_SINGLEPLAYER);
	resume(1, XVT_MISSION_SOLO_BACK_TO_TEAMS);
	XVT_ASSERT_TRUE(screen() == mission_setup_team_assignment_update);
}

static void check_debrief_solo_abort(void)
{
	for (int clear = 0; clear < 2; ++clear) {
		fresh(FRONTEND_MISSION_SESSION_SINGLEPLAYER);
		g_frontend_quick_start_launch_flag = 1;
		g_frontend_game_session_in_progress = 1;
		g_mission_setup_roster_authoritative = 1;
		g_mp_roster[0].player_id = 5;
		g_mp_roster[7].player_id = 9;
		resume(1, clear ? XVT_MISSION_DEBRIEF_SOLO_ABORT_CLEAR_ROSTER
				: XVT_MISSION_DEBRIEF_SOLO_ABORT);
		XVT_ASSERT_TRUE(screen() == mission_setup_update);
		XVT_ASSERT_INT_EQ(g_frontend_quick_start_launch_flag, 0);
		XVT_ASSERT_INT_EQ(g_frontend_game_session_in_progress, 0);
		XVT_ASSERT_INT_EQ(g_mission_setup_roster_authoritative, 0);
		/* Only CLEAR_ROSTER clears the multiplayer roster. */
		XVT_ASSERT_INT_EQ(g_mp_roster[0].player_id, clear ? 0 : 5);
		XVT_ASSERT_INT_EQ(g_mp_roster[7].player_id, clear ? 0 : 9);
	}
}

int main(void)
{
	check_notice();
	check_cancelled_actions_ignore_result();
	check_team_previous();
	check_tails_need_nonzero_result();
	check_leave_actions();
	check_solo_back();
	check_debrief_solo_abort();
	xvt_network_session_shutdown();
	return 0;
}
