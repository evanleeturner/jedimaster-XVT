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

static int Placeholder(int frame) { return frame; }

static void Fresh(void)
{
	XvtDialog_Shutdown();
	XvtNetworkTask_Shutdown();
	XvtNetworkSession_Shutdown();
	XvtFrontendAction_Reset();
	XvtTest_CloseDisplay();
	memset(&g_frontState, 0, sizeof g_frontState);
	XvtTest_OpenDisplay();
	memset(&g_pilotData, 0, sizeof g_pilotData);
	g_frontState.screenStates[0].updateFn = Placeholder;
	g_gameConfig.sfxDatapadEnabled = 0;
	g_gameConfig.helpOn = 0;
	g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_CLIENT;
	g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
}

static FrontendScreenUpdateFn TopScreen(void)
{
	return g_frontState.screenStates[g_frontState.screenStackTop].updateFn;
}

static void Click(int x, int y)
{
	g_frontState.mouseX = x;
	g_frontState.mouseY = y;
	g_frontState.mouseLeftClickLatch = 1;
}

static void CheckDrawsWithNoRooms(void)
{
	Fresh();
	*XvtNetworkTask_ScrollOffset() = 4;
	Click(200, 120);
	XVT_ASSERT_INT_EQ(XvtNetworkBrowser_DrawList(), -1);
	XVT_ASSERT_INT_EQ(*XvtNetworkTask_ScrollOffset(), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkBrowser_DrawRoster(), 1);
	XVT_ASSERT_INT_EQ(XvtNetworkBrowser_DrawMission(), 1);
	strcpy(XvtNetworkTask_Preview()->title, "Title");
	XVT_ASSERT_INT_EQ(XvtNetworkBrowser_DrawMission(), 1);
}

static void CheckFirstFrame(void)
{
	Fresh();
	g_frontendSkipScreenEntrySetup = 1;
	g_configConnectionTypeEditable = 1;
	g_frontendGameSessionInProgress = 1;
	XVT_ASSERT_INT_EQ(XvtNetworkBrowser_Screen(0), 0);
	XVT_ASSERT_INT_EQ(g_frontendSkipScreenEntrySetup, 0);
	XVT_ASSERT_INT_EQ(g_configConnectionTypeEditable, 0);
	XVT_ASSERT_INT_EQ(g_frontendGameSessionInProgress, 0);

	/* The browser was opened, and with no directory configured a confirm dialog is up. */
	XVT_ASSERT_INT_EQ(XvtNetworkTask_BrowserError(),
			  AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 1);
	XVT_ASSERT_TRUE(TopScreen() == Placeholder);
}

static void CheckLeave(void)
{
	Fresh();
	Click(LEAVE_X, LEAVE_Y);
	XVT_ASSERT_INT_EQ(XvtNetworkBrowser_Screen(1), 0);
	XVT_ASSERT_TRUE(TopScreen() == Concourse_Update);
}

static void CheckDialogHidesButtons(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtDialog_Begin(Placeholder, NULL),
			  XVT_DIALOG_PENDING);
	Click(LEAVE_X, LEAVE_Y);
	XVT_ASSERT_INT_EQ(XvtNetworkBrowser_Screen(1), 0);
	XVT_ASSERT_TRUE(TopScreen() == Placeholder);
}

static void CheckJoinNeedsTheTask(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkTask_CanJoin(), 0);
	Click(JOIN_X, JOIN_Y);
	XVT_ASSERT_INT_EQ(XvtNetworkBrowser_Screen(1), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_IsActive(), 0);
	XVT_ASSERT_TRUE(TopScreen() == Placeholder);
}

int main(void)
{
	CheckDrawsWithNoRooms();
	CheckFirstFrame();
	CheckLeave();
	CheckDialogHidesButtons();
	CheckJoinNeedsTheTask();
	XvtDialog_Shutdown();
	XvtNetworkTask_Shutdown();
	XvtNetworkSession_Shutdown();
	XvtTest_CloseDisplay();
	return 0;
}
