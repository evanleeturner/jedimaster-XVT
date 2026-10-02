/* Checks the network host and join dialogs (xvt_runtime/runtime/network_dialogs.h) against the promises in
 * its header: the access tails, the return to the host or join screen and what it clears, the connecting
 * screen's cancel button, the failure dialog with its return after dismissal or at once, and the admission
 * check. The frontend runs on a display with no window (test_frontend_display.h); its fonts and images are
 * not loaded, so the screens draw nothing visible, and the dialogs are dismissed with Escape. Every case
 * starts from a cleared frontend with a placeholder screen, no dialog and no network session.
 *
 * Not checked here: the text each error shows, beyond there being one. */
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

#include <stdlib.h>
#include <string.h>

static int Placeholder(int frame) { return frame; }

static void Fresh(void) {
	XvtDialog_Shutdown();
	XvtNetworkSession_Shutdown();
	XvtTest_CloseDisplay();
	memset(&g_frontState, 0, sizeof g_frontState);
	XvtTest_OpenDisplay();
	g_frontState.screenStates[0].updateFn = Placeholder;
	g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
	g_frontendSkipScreenEntrySetup = 0;
	g_gameConfig.sfxDatapadEnabled = 0;
}

static FrontendScreenUpdateFn TopScreen(void) {
	return g_frontState.screenStates[g_frontState.screenStackTop].updateFn;
}

/* Runs the open dialog's first frame, then dismisses it with Escape. */
static void DismissDialog(void) {
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 1);
	XvtDialog_Update();
	g_frontState.charRingBuffer[g_frontState.charWriteIdx++] = 27;
	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
}

/* Fills the state Return clears with values it must not keep. */
static void Dirty(void) {
	g_frontendNetSelectedSessionIdx = 3;
	g_frontendNetProbeMissionElapsedSeconds = 2;
	g_frontendNetReceivedMissionDescriptionId = 17;
	memset(g_frontendNetSelectedGameName, 'g', sizeof g_frontendNetSelectedGameName - 1);
}

static void CheckCleared(void) {
	XVT_ASSERT_INT_EQ(g_frontendSkipScreenEntrySetup, 1);
	XVT_ASSERT_INT_EQ(g_frontendNetSelectedSessionIdx, -1);
	XVT_ASSERT_INT_EQ(g_frontendNetProbeMissionElapsedSeconds, 0);
	XVT_ASSERT_INT_EQ(g_frontendNetReceivedMissionDescriptionId, -1);
	for (unsigned i = 0; i < sizeof g_frontendNetSelectedGameName; ++i)
		XVT_ASSERT_INT_EQ(g_frontendNetSelectedGameName[i], 0);
}

static void CheckReturn(void) {
	Fresh();
	Dirty();
	g_briefingText = malloc(4096);
	XVT_ASSERT_TRUE(g_briefingText != NULL);
	memset(g_briefingText, 'b', 4096);
	XvtNetworkDialogs_Return(1);
	XVT_ASSERT_TRUE(TopScreen() == FrontendNet_HostGameScreen);
	XVT_ASSERT_INT_EQ(g_frontendMissionSessionMode, FRONTEND_MISSION_SESSION_NET_HOST);
	CheckCleared();
	for (int i = 0; i < 4096; ++i)
		XVT_ASSERT_INT_EQ(g_briefingText[i], 0);
	free(g_briefingText);
	g_briefingText = NULL;

	/* The join screen, with no briefing text allocated. */
	Fresh();
	Dirty();
	XvtNetworkDialogs_Return(0);
	XVT_ASSERT_TRUE(TopScreen() == FrontendNet_JoinGameScreen);
	XVT_ASSERT_INT_EQ(g_frontendMissionSessionMode, FRONTEND_MISSION_SESSION_NET_CLIENT);
	CheckCleared();
}

static void CheckResume(void) {
	for (int result = 0; result < 2; ++result) {
		Fresh();
		XVT_ASSERT_INT_EQ(XvtNetworkDialogs_Resume(result, XVT_NETWORK_ACCESS_REJECTED), 0);
		XVT_ASSERT_TRUE(TopScreen() == FrontendNet_JoinGameScreen);
		XVT_ASSERT_TRUE(g_frontState.pendingScreenUpdateFn == NULL);

		/* A password first queues the options datapad. */
		Fresh();
		XVT_ASSERT_INT_EQ(XvtNetworkDialogs_Resume(result, XVT_NETWORK_ACCESS_PASSWORD), 0);
		XVT_ASSERT_TRUE(TopScreen() == FrontendNet_JoinGameScreen);
		XVT_ASSERT_TRUE(g_frontState.pendingScreenUpdateFn == Config_OptionsDatapadUpdate);
	}
}

static void CheckConnecting(void) {
	Fresh();
	g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	/* Not clicked, or clicked outside the cancel button. */
	g_frontState.mouseX = 100;
	g_frontState.mouseY = 460;
	XVT_ASSERT_INT_EQ(XvtNetworkDialogs_Connecting(), 0);
	g_frontState.mouseX = 300;
	g_frontState.mouseLeftClickLatch = 1;
	XVT_ASSERT_INT_EQ(XvtNetworkDialogs_Connecting(), 0);
	/* Clicked on it. */
	g_frontState.mouseX = 100;
	XVT_ASSERT_INT_EQ(XvtNetworkDialogs_Connecting(), 1);
	FrontendDisplay_UnlockBackBuffer();
}

static void CheckFailedWaitsForDismissal(void) {
	for (int host = 0; host < 2; ++host) {
		Fresh();
		XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginHost(NULL, "Luke", "", 0), 0);
		XVT_ASSERT_INT_EQ(XvtNetworkSession_GetStatus().state, XVT_NETWORK_SESSION_FAILED);
		XvtNetworkDialogs_ShowFailure(AERON_DPLAY_DIRECTORY_ERROR_TIMEOUT, host);

		/* The session is reset, and the message waits in a confirm dialog. */
		XVT_ASSERT_TRUE(XvtNetworkSession_GetStatus().state != XVT_NETWORK_SESSION_FAILED);
		XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 1);
		XVT_ASSERT_TRUE(g_frontDialogLine1OrEdit[0] != 0);
		XVT_ASSERT_TRUE(TopScreen() == Placeholder);

		/* Once dismissed, the frontend resumes the dialog's continuation, which returns to the screen. */
		DismissDialog();
		int frame_result = -1;
		XVT_ASSERT_INT_EQ(XvtDialog_ResumeContinuation(&frame_result), 1);
		XVT_ASSERT_TRUE(TopScreen() == (host ? FrontendNet_HostGameScreen : FrontendNet_JoinGameScreen));
		XVT_ASSERT_INT_EQ(g_frontendMissionSessionMode,
						  host ? FRONTEND_MISSION_SESSION_NET_HOST : FRONTEND_MISSION_SESSION_NET_CLIENT);
	}
}

static void CheckFailedEveryErrorHasAMessage(void) {
	for (int error = AERON_DPLAY_DIRECTORY_ERROR_NONE; error <= AERON_DPLAY_DIRECTORY_ERROR_BUSY; ++error) {
		Fresh();
		memset(g_frontDialogLine1OrEdit, 0, sizeof g_frontDialogLine1OrEdit);
		XvtNetworkDialogs_ShowFailure((AeronDplayDirectoryError)error, 0);
		XVT_ASSERT_TRUE(g_frontDialogLine1OrEdit[0] != 0);
	}
}

static void CheckFailedReturnsAtOnce(void) {
	Fresh();
	/* An untaken result from an earlier dialog: the failure's Confirm takes it and does not wait. */
	XVT_ASSERT_INT_EQ(XvtDialog_Confirm("earlier", NULL, NULL, NULL, NULL, 0), XVT_DIALOG_PENDING);
	DismissDialog();
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 1);
	XvtNetworkDialogs_ShowFailure(AERON_DPLAY_DIRECTORY_ERROR_FULL, 1);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
	XVT_ASSERT_TRUE(TopScreen() == FrontendNet_HostGameScreen);
}

static void CheckAdmissionFailed(void) {
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkDialogs_ReportAdmissionFailure(), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginJoin("\x02", "Luke", NULL), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_GetStatus().state, XVT_NETWORK_SESSION_FAILED);

	/* Reported as a join's failure: once dismissed, back to the join screen. */
	XVT_ASSERT_INT_EQ(XvtNetworkDialogs_ReportAdmissionFailure(), 1);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 1);
	DismissDialog();
	int frame_result = -1;
	XVT_ASSERT_INT_EQ(XvtDialog_ResumeContinuation(&frame_result), 1);
	XVT_ASSERT_TRUE(TopScreen() == FrontendNet_JoinGameScreen);
}

int main(void) {
	CheckReturn();
	CheckResume();
	CheckConnecting();
	CheckFailedWaitsForDismissal();
	CheckFailedEveryErrorHasAMessage();
	CheckFailedReturnsAtOnce();
	CheckAdmissionFailed();
	XvtDialog_Shutdown();
	XvtNetworkSession_Shutdown();
	XvtTest_CloseDisplay();
	return 0;
}
