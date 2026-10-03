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

#include <stdio.h>
#include <string.h>

static AeronDplayDirectoryRoom g_testRoom;

static int Placeholder(int frame) { return frame; }

static void Fresh(void)
{
	XvtNetworkTask_Shutdown();
	XvtNetworkSession_Shutdown();
	XvtDialog_Shutdown();
	XvtTest_CloseDisplay();
	memset(&g_frontState, 0, sizeof g_frontState);
	XvtTest_OpenDisplay();
	memset(&g_pilotData, 0, sizeof g_pilotData);
	strcpy(g_pilotData.name, "Luke");
	g_frontState.screenStates[0].updateFn = Placeholder;
	g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_HOST;
	memset((AeronInputSnapshot *)Aeron_InputSnapshot(), 0,
	       sizeof(AeronInputSnapshot));
}

static FrontendScreenUpdateFn TopScreen(void)
{
	return g_frontState.screenStates[g_frontState.screenStackTop].updateFn;
}

static void CheckCompatible(void)
{
	memset(&g_testRoom, 0, sizeof g_testRoom);
	g_testRoom.protocol = AERON_DPLAY_DIRECTORY_PROTOCOL;
	snprintf(g_testRoom.game_version, sizeof g_testRoom.game_version, "%d",
		 FRONTEND_NET_PROTOCOL_VERSION);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Compatible(&g_testRoom), 1);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Compatible(NULL), 0);

	/* Another directory protocol. */
	g_testRoom.protocol = AERON_DPLAY_DIRECTORY_PROTOCOL + 1;
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Compatible(&g_testRoom), 0);
	g_testRoom.protocol = AERON_DPLAY_DIRECTORY_PROTOCOL;

	/* Another game version, or the same number written differently. */
	snprintf(g_testRoom.game_version, sizeof g_testRoom.game_version, "%d",
		 FRONTEND_NET_PROTOCOL_VERSION + 1);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Compatible(&g_testRoom), 0);
	snprintf(g_testRoom.game_version, sizeof g_testRoom.game_version, "0%d",
		 FRONTEND_NET_PROTOCOL_VERSION);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Compatible(&g_testRoom), 0);
	snprintf(g_testRoom.game_version, sizeof g_testRoom.game_version, "%d ",
		 FRONTEND_NET_PROTOCOL_VERSION);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Compatible(&g_testRoom), 0);
}

static void CheckEmptyBrowser(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Snapshot()->room_count, 0);
	XVT_ASSERT_TRUE(XvtNetworkTask_SelectedRoom() == NULL);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_SelectedIndex(), -1);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_SnapshotAge(), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_BrowserError(),
			  AERON_DPLAY_DIRECTORY_ERROR_NONE);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_CanJoin(), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Preview()->title[0], 0);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Preview()->text[0], 0);

	/* An index out of range clears the selection and the preview. */
	strcpy(XvtNetworkTask_Preview()->title, "stale");
	strcpy(XvtNetworkTask_Preview()->text, "stale");
	XvtNetworkTask_ToggleSelection(0);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_SelectedIndex(), -1);
	XVT_ASSERT_TRUE(XvtNetworkTask_SelectedRoom() == NULL);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Preview()->title[0], 0);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Preview()->text[0], 0);
	XvtNetworkTask_ToggleSelection(-3);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_SelectedIndex(), -1);

	/* The scroll offset is the browser's own, for the list draw to read and write. */
	XVT_ASSERT_TRUE(XvtNetworkTask_ScrollOffset() ==
			XvtNetworkTask_ScrollOffset());
	*XvtNetworkTask_ScrollOffset() = 4;
	XVT_ASSERT_INT_EQ(*XvtNetworkTask_ScrollOffset(), 4);
}

static void CheckRefreshRecordsError(void)
{
	Fresh();
	XvtNetworkTask_Refresh();
	XVT_ASSERT_INT_EQ(XvtNetworkTask_BrowserError(),
			  AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_CanJoin(), 0);
}

static void CheckOpenBrowser(void)
{
	Fresh();
	g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	XvtNetworkTask_OpenBrowser();
	/* Client mode, a refresh started (and refused for want of a directory), any session left. */
	XVT_ASSERT_INT_EQ(g_frontendMissionSessionMode,
			  FRONTEND_MISSION_SESSION_NET_CLIENT);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_BrowserError(),
			  AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_GetStatus().state,
			  XVT_NETWORK_SESSION_PENDING);
}

static void CheckBrowserVisible(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkTask_BrowserVisible(), 0);
	/* The join screen anywhere on the stack. */
	g_frontState.screenStates[0].updateFn = FrontendNet_JoinGameScreen;
	g_frontState.screenStates[1].updateFn = Placeholder;
	g_frontState.screenStackTop = 1;
	XVT_ASSERT_INT_EQ(XvtNetworkTask_BrowserVisible(), 1);
	g_frontState.screenStackTop = 0;
	XVT_ASSERT_INT_EQ(XvtNetworkTask_BrowserVisible(), 1);
	/* ...but not past the top of the stack. */
	g_frontState.screenStates[0].updateFn = Placeholder;
	g_frontState.screenStates[1].updateFn = FrontendNet_JoinGameScreen;
	XVT_ASSERT_INT_EQ(XvtNetworkTask_BrowserVisible(), 0);

	/* Not while an attempt runs. */
	g_frontState.screenStates[0].updateFn = FrontendNet_JoinGameScreen;
	XvtNetworkTask_Begin(XVT_NETWORK_HOST);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_IsActive(), 1);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_BrowserVisible(), 0);
}

static void CheckBegin(void)
{
	/* A join needs CanJoin; with no room selected it is ignored. */
	Fresh();
	XvtNetworkTask_Begin(XVT_NETWORK_CONNECT);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_GetStatus().state,
			  XVT_NETWORK_SESSION_IDLE);

	/* Either host action starts a host attempt in the network session. */
	const int hosts[] = {XVT_NETWORK_HOST, XVT_NETWORK_AUTO_HOST};
	for (unsigned i = 0; i < 2; ++i) {
		Fresh();
		XvtNetworkTask_Begin(hosts[i]);
		XVT_ASSERT_INT_EQ(XvtNetworkTask_IsActive(), 1);
		XVT_ASSERT_INT_EQ(XvtNetworkSession_GetStatus().state,
				  XVT_NETWORK_SESSION_PENDING);
		XVT_ASSERT_INT_EQ(XvtNetworkTask_CanJoin(), 0);
	}

	/* While an attempt runs, Begin is ignored. */
	Fresh();
	XvtNetworkTask_Begin(XVT_NETWORK_HOST);
	XvtNetworkSession_Shutdown();
	XvtNetworkTask_Begin(XVT_NETWORK_AUTO_HOST);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_GetStatus().state,
			  XVT_NETWORK_SESSION_IDLE);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_IsActive(), 1);
}

static void CheckResumeWithoutAttempt(void)
{
	int result = 77;
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Resume(&result), 0);
	XVT_ASSERT_INT_EQ(result, 77);

	/* A failed session shows the failure dialog, with result 0. */
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginHost(NULL, "Luke", "", 0), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Resume(&result), 1);
	XVT_ASSERT_INT_EQ(result, 0);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 1);
}

static void CheckResumeDuringAttempt(void)
{
	int result = 77;
	Fresh();
	XvtNetworkTask_Begin(XVT_NETWORK_HOST);
	/* The session is ticked; it is still closing the previous session, so the attempt goes on. */
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Resume(&result), 1);
	XVT_ASSERT_INT_EQ(result, 0);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_IsActive(), 1);

	/* Escape with the window focused cancels, returning to the host screen. */
	AeronInputSnapshot *input = (AeronInputSnapshot *)Aeron_InputSnapshot();
	input->has_focus = 1;
	input->key_pressed[AERON_KEY_ESCAPE] = 1;
	result = 77;
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Resume(&result), 1);
	XVT_ASSERT_INT_EQ(result, 0);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_IsActive(), 0);
	XVT_ASSERT_TRUE(TopScreen() == FrontendNet_HostGameScreen);
}

static void CheckResumeFinishesFailedAttempt(void)
{
	int result = 77;
	Fresh();
	XvtNetworkTask_Begin(XVT_NETWORK_HOST);
	/* The session fails during the attempt: the attempt ends and the failure dialog opens. */
	XvtNetworkSession_HostLost();
	XVT_ASSERT_INT_EQ(XvtNetworkTask_Resume(&result), 1);
	XVT_ASSERT_INT_EQ(result, 0);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 1);
}

static void CheckCancel(void)
{
	/* A host attempt returns to the host screen, an auto host too. */
	const int hosts[] = {XVT_NETWORK_HOST, XVT_NETWORK_AUTO_HOST};
	for (unsigned i = 0; i < 2; ++i) {
		Fresh();
		XvtNetworkTask_Begin(hosts[i]);
		XvtNetworkTask_Cancel();
		XVT_ASSERT_INT_EQ(XvtNetworkTask_IsActive(), 0);
		XVT_ASSERT_TRUE(TopScreen() == FrontendNet_HostGameScreen);
	}
}

static void CheckShutdown(void)
{
	Fresh();
	XvtNetworkTask_Refresh();
	*XvtNetworkTask_ScrollOffset() = 3;
	XvtNetworkTask_Begin(XVT_NETWORK_HOST);
	XvtNetworkTask_Shutdown();
	XVT_ASSERT_INT_EQ(XvtNetworkTask_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_BrowserError(),
			  AERON_DPLAY_DIRECTORY_ERROR_NONE);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_SelectedIndex(), -1);
	XVT_ASSERT_INT_EQ(*XvtNetworkTask_ScrollOffset(), 0);
	/* The network session is reset. */
	XVT_ASSERT_INT_EQ(XvtNetworkSession_GetStatus().state,
			  XVT_NETWORK_SESSION_PENDING);
}

int main(void)
{
	CheckCompatible();
	CheckEmptyBrowser();
	CheckRefreshRecordsError();
	CheckOpenBrowser();
	CheckBrowserVisible();
	CheckBegin();
	CheckResumeWithoutAttempt();
	CheckResumeDuringAttempt();
	CheckResumeFinishesFailedAttempt();
	CheckCancel();
	CheckShutdown();
	XvtNetworkTask_Shutdown();
	XvtNetworkSession_Shutdown();
	XvtDialog_Shutdown();
	XvtTest_CloseDisplay();
	return 0;
}
