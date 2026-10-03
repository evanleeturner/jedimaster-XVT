/* Checks the mission screens' dialog tails (xvt_runtime/runtime/mission_dialogs.h) against the promises in
 * its header: which screen each action opens, which actions ignore the dialog's result and which run only
 * on a nonzero one, the session shutdowns, the flags and roster the solo debrief abort clears, and that
 * every call turns the overlay text off and returns 0. The test sets the frontend globals itself; every
 * case starts with a placeholder screen on the stack, overlay text on, and no network session.
 *
 * A session shutdown is seen through the network session: Net_ShutdownDirectPlaySession reports every
 * shutdown to it, and it then reads as pending until the close completes.
 *
 * Not checked here: the packets HOST_RESTART, DEBRIEF_HOST_ABORT and the leave actions send, which need a
 * DirectPlay session. */
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

#include <string.h>

static int Placeholder(int frame) { return frame; }

static void Fresh(int session_mode)
{
	XvtNetworkSession_Shutdown();
	memset(&g_frontState, 0, sizeof g_frontState);
	g_frontState.screenStates[0].updateFn = Placeholder;
	g_frontendMissionSessionMode = session_mode;
	g_frontendSkipScreenEntrySetup = 0;
	g_frontendQuickStartLaunchFlag = 0;
	g_frontendGameSessionInProgress = 0;
	g_missionSetupRosterAuthoritative = 0;
	memset(g_mpRoster, 0, sizeof g_mpRoster);
	FrontendButton_EnableOverlayText();
}

static FrontendScreenUpdateFn Screen(void)
{
	return g_frontState.screenStates[g_frontState.screenStackTop].updateFn;
}

static int SessionClosing(void)
{
	return XvtNetworkSession_GetStatus().state ==
	       XVT_NETWORK_SESSION_PENDING;
}

/* Runs action with result and checks the promise every call keeps. */
static void Resume(int result, int action)
{
	XVT_ASSERT_INT_EQ(XvtMissionDialogs_Resume(result, action), 0);
	XVT_ASSERT_INT_EQ(FrontendButton_IsOverlayTextEnabled(), 0);
}

static void CheckNotice(void)
{
	for (int result = 0; result < 2; ++result) {
		Fresh(FRONTEND_MISSION_SESSION_NET_HOST);
		Resume(result, XVT_MISSION_NOTICE);
		XVT_ASSERT_TRUE(Screen() == Placeholder);
		XVT_ASSERT_INT_EQ(SessionClosing(), 0);
	}
}

static void CheckCancelledActionsIgnoreResult(void)
{
	const int actions[] = {
		XVT_MISSION_SETUP_CANCELLED, XVT_MISSION_SETUP_BOOTED,
		XVT_MISSION_TEAM_CANCELLED, XVT_MISSION_ASSIGNMENT_CANCELLED,
		XVT_MISSION_BRIEFING_CANCELLED};
	for (unsigned i = 0; i < sizeof actions / sizeof actions[0]; ++i) {
		for (int result = 0; result < 2; ++result) {
			Fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
			g_frontState.netIsHost = 0;
			Resume(result, actions[i]);
			XVT_ASSERT_TRUE(Screen() == FrontendNet_JoinGameScreen);
		}
	}

	/* TEAM_CANCELLED also shuts down the session. */
	Fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	Resume(0, XVT_MISSION_TEAM_CANCELLED);
	XVT_ASSERT_INT_EQ(SessionClosing(), 1);

	/* BRIEFING_CANCELLED on the host returns to the concourse instead. */
	for (int result = 0; result < 2; ++result) {
		Fresh(FRONTEND_MISSION_SESSION_NET_HOST);
		g_frontState.netIsHost = 1;
		Resume(result, XVT_MISSION_BRIEFING_CANCELLED);
		XVT_ASSERT_TRUE(Screen() == Concourse_Update);
	}
}

static void CheckTeamPrevious(void)
{
	/* Solo, either result reopens mission setup. */
	for (int result = 0; result < 2; ++result) {
		Fresh(FRONTEND_MISSION_SESSION_SINGLEPLAYER);
		Resume(result, XVT_MISSION_TEAM_PREVIOUS);
		XVT_ASSERT_TRUE(Screen() == MissionSetup_Update);
	}
	/* A network session sends return-to-setup instead of reopening it. */
	Fresh(FRONTEND_MISSION_SESSION_NET_HOST);
	Resume(0, XVT_MISSION_TEAM_PREVIOUS);
	XVT_ASSERT_TRUE(Screen() != MissionSetup_Update);
}

static void CheckTailsNeedNonzeroResult(void)
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
		Fresh(FRONTEND_MISSION_SESSION_NET_HOST);
		g_frontendQuickStartLaunchFlag = 1;
		g_frontendGameSessionInProgress = 1;
		g_missionSetupRosterAuthoritative = 1;
		g_mpRoster[0].playerId = 5;
		Resume(0, actions[i]);
		XVT_ASSERT_TRUE(Screen() == Placeholder);
		XVT_ASSERT_INT_EQ(SessionClosing(), 0);
		XVT_ASSERT_INT_EQ(g_frontendQuickStartLaunchFlag, 1);
		XVT_ASSERT_INT_EQ(g_frontendGameSessionInProgress, 1);
		XVT_ASSERT_INT_EQ(g_missionSetupRosterAuthoritative, 1);
		XVT_ASSERT_INT_EQ(g_mpRoster[0].playerId, 5);
	}
}

static void CheckLeaveActions(void)
{
	Fresh(FRONTEND_MISSION_SESSION_NET_HOST);
	Resume(1, XVT_MISSION_SETUP_HOST_LEAVE);
	XVT_ASSERT_TRUE(Screen() == Concourse_Update);
	XVT_ASSERT_INT_EQ(SessionClosing(), 1);

	Fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	Resume(1, XVT_MISSION_CLIENT_LEAVE);
	XVT_ASSERT_TRUE(Screen() == FrontendNet_JoinGameScreen);
	XVT_ASSERT_INT_EQ(SessionClosing(), 1);

	Fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	Resume(7, XVT_MISSION_TEAM_CLIENT_LEAVE);
	XVT_ASSERT_TRUE(Screen() == FrontendNet_JoinGameScreen);
	XVT_ASSERT_INT_EQ(SessionClosing(), 1);

	/* From the debrief, a leaving client goes to the concourse. */
	Fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	Resume(1, XVT_MISSION_DEBRIEF_CLIENT_LEAVE);
	XVT_ASSERT_TRUE(Screen() == Concourse_Update);
	XVT_ASSERT_INT_EQ(SessionClosing(), 1);
}

static void CheckSoloBack(void)
{
	Fresh(FRONTEND_MISSION_SESSION_SINGLEPLAYER);
	Resume(1, XVT_MISSION_SOLO_BACK_TO_SETUP);
	XVT_ASSERT_TRUE(Screen() == MissionSetup_Update);

	Fresh(FRONTEND_MISSION_SESSION_SINGLEPLAYER);
	Resume(1, XVT_MISSION_SOLO_BACK_TO_TEAMS);
	XVT_ASSERT_TRUE(Screen() == MissionSetup_TeamAssignmentUpdate);
}

static void CheckDebriefSoloAbort(void)
{
	for (int clear = 0; clear < 2; ++clear) {
		Fresh(FRONTEND_MISSION_SESSION_SINGLEPLAYER);
		g_frontendQuickStartLaunchFlag = 1;
		g_frontendGameSessionInProgress = 1;
		g_missionSetupRosterAuthoritative = 1;
		g_mpRoster[0].playerId = 5;
		g_mpRoster[7].playerId = 9;
		Resume(1, clear ? XVT_MISSION_DEBRIEF_SOLO_ABORT_CLEAR_ROSTER
				: XVT_MISSION_DEBRIEF_SOLO_ABORT);
		XVT_ASSERT_TRUE(Screen() == MissionSetup_Update);
		XVT_ASSERT_INT_EQ(g_frontendQuickStartLaunchFlag, 0);
		XVT_ASSERT_INT_EQ(g_frontendGameSessionInProgress, 0);
		XVT_ASSERT_INT_EQ(g_missionSetupRosterAuthoritative, 0);
		/* Only CLEAR_ROSTER clears the multiplayer roster. */
		XVT_ASSERT_INT_EQ(g_mpRoster[0].playerId, clear ? 0 : 5);
		XVT_ASSERT_INT_EQ(g_mpRoster[7].playerId, clear ? 0 : 9);
	}
}

int main(void)
{
	CheckNotice();
	CheckCancelledActionsIgnoreResult();
	CheckTeamPrevious();
	CheckTailsNeedNonzeroResult();
	CheckLeaveActions();
	CheckSoloBack();
	CheckDebriefSoloAbort();
	XvtNetworkSession_Shutdown();
	return 0;
}
