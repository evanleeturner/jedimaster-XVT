/* Checks the game runtime's host interface (xvt_runtime/runtime/port.h) against the promises in its header
 * that hold without a window: Init's refusal of a logical size other than 640x480 and the exit code it
 * sets, the port before Init (not initialized, quitting, a Tick that does nothing, a Shutdown that only
 * lifts the classic rendering suppression), the settings request latch, and when the network requires
 * progress. Aeron runs without a window here; the test sets its logical size. Every case starts with the
 * port shut down and the frontend cleared.
 *
 * Not checked here: a successful Init and everything that runs after it (Tick's frames, pausing, quitting,
 * the wake delay and Shutdown after Init), which start the frontend with its main window, the config and
 * the game's files. */
#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "test_assert.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/network_task.h"
#include "xvt_runtime/runtime/port.h"
#include "xvt_runtime/timing/host_clock.h"

#include <string.h>

static int Placeholder(int frame) { return frame; }

static void Fresh(void) {
	XvtPort_Shutdown();
	XvtNetworkTask_Shutdown();
	XvtNetworkSession_Shutdown();
	memset(&g_frontState, 0, sizeof g_frontState);
	memset(&g_pilotData, 0, sizeof g_pilotData);
	g_frontState.screenStates[0].updateFn = Placeholder;
}

static void CheckInitRefusesSize(void) {
	const int sizes[][2] = { { 800, 600 }, { 640, 400 }, { 852, 480 } };
	for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
		Fresh();
		XVT_ASSERT_INT_EQ(Aeron_SetLogicalSize(sizes[i][0], sizes[i][1]), 1);
		XVT_ASSERT_INT_EQ(XvtPort_Init(), 0);
		XVT_ASSERT_INT_EQ(XvtPort_GetExitCode(), 1);
		XVT_ASSERT_INT_EQ(XvtPort_IsInitialized(), 0);
		XVT_ASSERT_INT_EQ(XvtPort_ServiceQuit(), 1);
	}
}

static void CheckBeforeInit(void) {
	Fresh();
	XVT_ASSERT_INT_EQ(XvtPort_IsInitialized(), 0);
	XVT_ASSERT_INT_EQ(XvtPort_ServiceQuit(), 1);

	/* Tick does nothing once ShouldQuit is 1: the host clock does not move. */
	XvtTime_Reset();
	XvtPort_Tick(16000);
	XVT_ASSERT_INT_EQ(XvtTime_GetElapsedUs(), 0);

	/* Shutdown before Init only lifts the classic rendering suppression. */
	AeronDx5_SetClassicFlightRenderingSuppressed(1);
	XvtPort_Shutdown();
	XVT_ASSERT_INT_EQ(AeronDx5_IsClassicFlightRenderingSuppressed(), 0);
	XVT_ASSERT_INT_EQ(XvtPort_IsInitialized(), 0);
}

static void CheckSettingsLatch(void) {
	Fresh();
	XVT_ASSERT_INT_EQ(XvtPort_ConsumeSettingsRequest(), 0);
	XvtPort_RequestSettings();
	XvtPort_RequestSettings();
	XVT_ASSERT_INT_EQ(XvtPort_ConsumeSettingsRequest(), 1);
	XVT_ASSERT_INT_EQ(XvtPort_ConsumeSettingsRequest(), 0);
}

static void CheckNetworkRequiresProgress(void) {
	Fresh();
	XVT_ASSERT_INT_EQ(XvtPort_NetworkRequiresProgress(), 0);

	/* The game browser is visible: the join screen is on the stack. */
	g_frontState.screenStates[0].updateFn = FrontendNet_JoinGameScreen;
	XVT_ASSERT_INT_EQ(XvtPort_NetworkRequiresProgress(), 1);
	g_frontState.screenStates[0].updateFn = Placeholder;
	XVT_ASSERT_INT_EQ(XvtPort_NetworkRequiresProgress(), 0);

	/* A host attempt runs in the network task. */
	XvtNetworkTask_Begin(XVT_NETWORK_HOST);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_IsActive(), 1);
	XVT_ASSERT_INT_EQ(XvtPort_NetworkRequiresProgress(), 1);
	XvtNetworkTask_Shutdown();
	XVT_ASSERT_INT_EQ(XvtPort_NetworkRequiresProgress(), 0);
}

int main(void) {
	CheckInitRefusesSize();
	CheckBeforeInit();
	CheckSettingsLatch();
	CheckNetworkRequiresProgress();
	XvtPort_Shutdown();
	XvtNetworkSession_Shutdown();
	return 0;
}
