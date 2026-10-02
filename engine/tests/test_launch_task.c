/* Checks the frontend-to-flight hand-off (xvt_runtime/runtime/launch_task.h) against the promises in its
 * header that hold while no launch is queued: the idle phase reports no launch, BeginPendingLaunch refuses,
 * Complete and Update do nothing, and Shutdown leaves the task idle. The test sets the frontend state it
 * watches; every case starts from Shutdown and a cleared frontend with a placeholder screen at frame 4.
 *
 * Not checked here: Queue and every phase after it. Queue writes the settings file and the pilot file,
 * checks the installation and reads the game's mission list for the selected mission before it builds the
 * flight command, and completing a flight that ran restores the frontend's window surfaces; a test would
 * need the game's mission files, a loaded configuration and a window. */
#include "test_assert.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/runtime/launch_task.h"
#include "xvt_runtime/runtime/network_session.h"

#include <string.h>

static int Placeholder(int frame) { return frame; }

static void Fresh(void)
{
	XvtLaunchTask_Shutdown();
	XvtNetworkSession_Shutdown();
	memset(&g_frontState, 0, sizeof g_frontState);
	g_frontState.screenStates[0].updateFn = Placeholder;
	g_frontState.frameCounter = 4;
	g_frontState.screenCallbacksDirty = 1;
}

static void CheckIdle(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtLaunchTask_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtLaunchTask_HasPendingLaunch(), 0);
	XVT_ASSERT_TRUE(XvtLaunchTask_BeginPendingLaunch() == NULL);
	XVT_ASSERT_INT_EQ(XvtLaunchTask_IsActive(), 0);
}

static void CheckCompleteIgnoredWhenIdle(void)
{
	for (int succeeded = 0; succeeded < 2; ++succeeded) {
		Fresh();
		g_frontState.cursorVisible = 0;
		XvtLaunchTask_Complete(succeeded);
		/* No screen switch, no frame reset, no cursor, no session shutdown. */
		XVT_ASSERT_TRUE(g_frontState.screenStates[0].updateFn ==
				Placeholder);
		XVT_ASSERT_INT_EQ(g_frontState.frameCounter, 4);
		XVT_ASSERT_INT_EQ(g_frontState.screenCallbacksDirty, 1);
		XVT_ASSERT_INT_EQ(g_frontState.cursorVisible, 0);
		XVT_ASSERT_INT_EQ(XvtNetworkSession_GetStatus().state,
				  XVT_NETWORK_SESSION_IDLE);
		XVT_ASSERT_INT_EQ(XvtLaunchTask_IsActive(), 0);
	}
}

static void CheckTickIgnoredWhenIdle(void)
{
	Fresh();
	/* Escape cancels only a fade or a pending launch; idle, it stays in the keyboard buffer. */
	g_frontState.charRingBuffer[0] = 27;
	g_frontState.charWriteIdx = 1;
	XvtLaunchTask_Update();
	XVT_ASSERT_INT_EQ(g_frontState.charReadIdx, 0);
	XVT_ASSERT_INT_EQ(g_frontState.charWriteIdx, 1);
	XVT_ASSERT_TRUE(g_frontState.screenStates[0].updateFn == Placeholder);
	XVT_ASSERT_INT_EQ(XvtLaunchTask_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtLaunchTask_HasPendingLaunch(), 0);
}

int main(void)
{
	CheckIdle();
	CheckCompleteIgnoredWhenIdle();
	CheckTickIgnoredWhenIdle();
	XvtLaunchTask_Shutdown();
	return 0;
}
