/* Checks the frontend's frame loop (xvt_runtime/runtime/frontend_task.h) against the promises in its
 * header that hold without the game's window and files: RunFrame's update, frame counter, exit callback
 * and pending screen push, a dialog continuation run in place of the update, and the frame held for an
 * opened dialog or the network task; Update's pacing, the dialog that updates alone, a quit result, and when
 * a frame is presented; the wake delay; the joystick polling interval and the CD task tick of
 * ServiceFrameSystems; and Shutdown before Init. The frontend runs on a display with no window
 * (test_frontend_display.h); presented frames are counted through Aeron's classic frame serial, which rises
 * once for each frame the frontend presents. The test's own screens stand in for the game's. Init is never
 * called, so the startup frame does not run. Every case starts from a cleared frontend with a 40 ms frame
 * interval and no dialog, network task or CD fade, the clock running on from the case before; the case
 * that quits runs last, since nothing but Init clears the quit.
 *
 * Not checked here: Init and Shutdown after it, which need the main window, the game's files and the
 * config; the launch task's turn in Update, which needs a queued launch; the hold for a pending campaign
 * prefix, which takes the same path as the network task's hold checked here; drawing the cursor; and the
 * end of the program when the back buffer cannot be locked. */
#include "aeron/compat/host.h"
#include "test_assert.h"
#include "test_frontend_display.h"
#include "xvt/audio/music_cd.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/runtime/cd_task.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_task.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/network_task.h"
#include "xvt_runtime/timing/host_clock.h"

#include <stdint.h>
#include <string.h>

enum { FRAME_MS = 40 };

static int g_screenCalls;
static int g_screenFrame;
static int g_screenReturn;
static int g_screenAction;
static int g_exitCalls;
static int g_otherExitCalls;
static int g_dialogCalls;

enum { ACTION_NONE, ACTION_SWITCH, ACTION_QUEUE, ACTION_DIALOG };

static int OtherScreen(int frame) { return frame * 0; }

static int OtherExit(int frame) {
	(void)frame;
	++g_otherExitCalls;
	return 0;
}

static int DialogScreen(int frame) {
	(void)frame;
	++g_dialogCalls;
	return 0;
}

/* The top screen's update: counts its calls, and on request switches screens, queues a screen push or
 * opens a dialog before returning g_screenReturn. */
static int Screen(int frame) {
	static const RECT whole = { 0, 0, 639, 479 };
	++g_screenCalls;
	g_screenFrame = frame;
	if (g_screenAction == ACTION_SWITCH)
		FrontendScreen_SetCallbacks(OtherScreen, OtherExit);
	else if (g_screenAction == ACTION_QUEUE)
		FrontendScreen_QueuePush(OtherScreen, &whole);
	else if (g_screenAction == ACTION_DIALOG)
		XvtDialog_Begin(DialogScreen, NULL);
	return g_screenReturn;
}

static int Exit(int frame) {
	(void)frame;
	++g_exitCalls;
	return 0;
}

static int Continuation(int result, int context) {
	(void)result;
	(void)context;
	return 5;
}

static void Fresh(void) {
	XvtDialog_Shutdown();
	XvtNetworkTask_Shutdown();
	XvtNetworkSession_Shutdown();
	XvtCdTask_CancelFade();
	XvtTest_CloseDisplay();
	memset(&g_frontState, 0, sizeof g_frontState);
	XvtTest_OpenDisplay();
	memset(&g_pilotData, 0, sizeof g_pilotData);
	g_musicCdMciDeviceId = 0;
	g_frontState.frameIntervalMs = FRAME_MS;
	g_frontState.screenStates[0].updateFn = Screen;
	g_frontState.screenStates[0].exitFn = Exit;
	g_frontState.frameCounter = 3;
	g_screenCalls = g_screenFrame = g_screenReturn = 0;
	g_screenAction = ACTION_NONE;
	g_exitCalls = g_otherExitCalls = g_dialogCalls = 0;
}

static void AdvanceMs(int ms) { XvtTime_AdvanceHostClock(ms * 1000); }

/* The clock runs on across cases, and so does the frame deadline: move past any deadline an earlier case
 * left, so the next Update runs a frame. */
static void FrameDue(void) { AdvanceMs(1000); }

static void CheckRunFrameWithoutUpdate(void) {
	Fresh();
	g_frontState.screenStates[0].updateFn = NULL;
	XVT_ASSERT_INT_EQ(XvtFrontendTask_RunFrame(), 0);
	XVT_ASSERT_INT_EQ(g_exitCalls, 0);
}

static void CheckRunFrame(void) {
	Fresh();
	g_screenReturn = 7;
	XVT_ASSERT_INT_EQ(XvtFrontendTask_RunFrame(), 7);
	XVT_ASSERT_INT_EQ(g_screenCalls, 1);
	XVT_ASSERT_INT_EQ(g_screenFrame, 3);
	XVT_ASSERT_INT_EQ(g_frontState.frameCounter, 4);
	/* The callbacks did not change and the result is not 1: no exit callback. */
	XVT_ASSERT_INT_EQ(g_exitCalls, 0);

	/* A result of 1 runs the exit callback. */
	g_screenReturn = 1;
	XVT_ASSERT_INT_EQ(XvtFrontendTask_RunFrame(), 1);
	XVT_ASSERT_INT_EQ(g_screenFrame, 4);
	XVT_ASSERT_INT_EQ(g_exitCalls, 1);
}

static void CheckSwitchRunsCapturedExit(void) {
	Fresh();
	g_screenAction = ACTION_SWITCH;
	XVT_ASSERT_INT_EQ(XvtFrontendTask_RunFrame(), 0);
	/* The exit callback captured before the update runs, not the new screen's. */
	XVT_ASSERT_INT_EQ(g_exitCalls, 1);
	XVT_ASSERT_INT_EQ(g_otherExitCalls, 0);
	XVT_ASSERT_TRUE(g_frontState.screenStates[0].updateFn == OtherScreen);
	XVT_ASSERT_INT_EQ(g_frontState.screenCallbacksDirty, 0);

	/* A callback change already pending when the frame starts also runs the exit callback. */
	Fresh();
	g_frontState.screenCallbacksDirty = 1;
	XVT_ASSERT_INT_EQ(XvtFrontendTask_RunFrame(), 0);
	XVT_ASSERT_INT_EQ(g_exitCalls, 1);
}

static void CheckPendingPush(void) {
	Fresh();
	g_screenAction = ACTION_QUEUE;
	XVT_ASSERT_INT_EQ(XvtFrontendTask_RunFrame(), 0);
	XVT_ASSERT_INT_EQ(g_frontState.screenStackTop, 1);
	XVT_ASSERT_TRUE(g_frontState.screenStates[1].updateFn == OtherScreen);
	FrontendScreen_PopState();
}

static void CheckDialogHoldsFrame(void) {
	Fresh();
	g_screenAction = ACTION_DIALOG;
	g_screenReturn = 1;
	XVT_ASSERT_INT_EQ(XvtFrontendTask_RunFrame(), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 1);
	XVT_ASSERT_INT_EQ(g_frontState.frameCounter, 3);
	XVT_ASSERT_INT_EQ(g_exitCalls, 0);
}

static void CheckContinuationReplacesUpdate(void) {
	Fresh();
	XVT_ASSERT_INT_EQ(XvtDialog_Begin(DialogScreen, NULL), XVT_DIALOG_PENDING);
	XvtDialog_ContinueWith(Continuation, 0);
	XvtDialog_Update();
	g_frontState.charRingBuffer[g_frontState.charWriteIdx++] = 27;
	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 1);

	/* The parent's next frame runs the continuation in place of its update. */
	XVT_ASSERT_INT_EQ(XvtFrontendTask_RunFrame(), 5);
	XVT_ASSERT_INT_EQ(g_screenCalls, 0);
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 0);
	/* The frame after that runs the update again. */
	XvtFrontendTask_RunFrame();
	XVT_ASSERT_INT_EQ(g_screenCalls, 1);
}

static void CheckNetworkTaskHoldsFrame(void) {
	Fresh();
	XvtNetworkTask_Begin(XVT_NETWORK_HOST);
	XVT_ASSERT_INT_EQ(XvtNetworkTask_IsActive(), 1);
	/* The network task is resumed in place of the update, and the frame is held. */
	XVT_ASSERT_INT_EQ(XvtFrontendTask_RunFrame(), 0);
	XVT_ASSERT_INT_EQ(g_screenCalls, 0);
	XVT_ASSERT_INT_EQ(g_frontState.frameCounter, 3);
	XVT_ASSERT_INT_EQ(g_exitCalls, 0);
}

static void CheckTickPacing(void) {
	Fresh();
	FrameDue();
	XvtFrontendTask_Update();
	XVT_ASSERT_INT_EQ(g_screenCalls, 1);
	XvtFrontendTask_Update();
	XVT_ASSERT_INT_EQ(g_screenCalls, 1);
	XVT_ASSERT_INT_EQ(XvtFrontendTask_NextWakeDelayUs(), FRAME_MS * 1000);
	AdvanceMs(FRAME_MS - 1);
	XVT_ASSERT_INT_EQ(XvtFrontendTask_NextWakeDelayUs(), 1000);
	XvtFrontendTask_Update();
	XVT_ASSERT_INT_EQ(g_screenCalls, 1);
	AdvanceMs(1);
	XVT_ASSERT_INT_EQ(XvtFrontendTask_NextWakeDelayUs(), 0);
	XvtFrontendTask_Update();
	XVT_ASSERT_INT_EQ(g_screenCalls, 2);
	XVT_ASSERT_INT_EQ(XvtFrontendTask_ShouldQuit(), 0);
}

static void CheckTickPresents(void) {
	Fresh();
	FrameDue();
	uint64_t serial = AeronDx5_GetClassicFlightFrameSerial();
	XvtFrontendTask_Update();
	XVT_ASSERT_INT_EQ(g_screenCalls, 1);
	XVT_ASSERT_INT_EQ(AeronDx5_GetClassicFlightFrameSerial(), serial + 1);
}

static void CheckTickRunsOnlyTheDialog(void) {
	Fresh();
	FrameDue();
	XVT_ASSERT_INT_EQ(XvtDialog_Begin(DialogScreen, NULL), XVT_DIALOG_PENDING);
	XvtFrontendTask_Update();
	XVT_ASSERT_INT_EQ(g_dialogCalls, 1);
	XVT_ASSERT_INT_EQ(g_screenCalls, 0);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 1);

	/* The tick that ends the dialog keeps its last presented frame: nothing new is presented. */
	g_frontState.charRingBuffer[g_frontState.charWriteIdx++] = 27;
	AdvanceMs(FRAME_MS);
	uint64_t serial = AeronDx5_GetClassicFlightFrameSerial();
	XvtFrontendTask_Update();
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
	XVT_ASSERT_INT_EQ(AeronDx5_GetClassicFlightFrameSerial(), serial);
	XVT_ASSERT_INT_EQ(g_screenCalls, 0);
}

static void CheckWakeDelayTakesCdSooner(void) {
	Fresh();
	FrameDue();
	XvtFrontendTask_Update();
	g_musicCdMciDeviceId = 1;
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(0, 512, 0), 1);
	XVT_ASSERT_INT_EQ(XvtFrontendTask_NextWakeDelayUs(), 1000);
	XvtCdTask_CancelFade();
	XVT_ASSERT_INT_EQ(XvtFrontendTask_NextWakeDelayUs(), FRAME_MS * 1000);
}

static void CheckServiceFrameSystems(void) {
	Fresh();
	/* A joystick marked present that cannot be read is dropped when it is polled. */
	XvtFrontendTask_ServiceFrameSystems();
	AdvanceMs(100);
	g_frontState.joystickPresent[0] = 1;
	g_frontState.joystickPresent[1] = 1;
	g_frontState.joyDeviceIds[0] = 77;
	g_frontState.joyDeviceIds[1] = 78;
	XvtFrontendTask_ServiceFrameSystems();
	XVT_ASSERT_INT_EQ(g_frontState.joystickPresent[0], 0);
	XVT_ASSERT_INT_EQ(g_frontState.joystickPresent[1], 0);

	/* Not again before 100 ms have passed. */
	g_frontState.joystickPresent[0] = 1;
	AdvanceMs(99);
	XvtFrontendTask_ServiceFrameSystems();
	XVT_ASSERT_INT_EQ(g_frontState.joystickPresent[0], 1);
	AdvanceMs(1);
	XvtFrontendTask_ServiceFrameSystems();
	XVT_ASSERT_INT_EQ(g_frontState.joystickPresent[0], 0);

	/* The CD task is ticked: a due fade step moves the volume. */
	g_musicCdMciDeviceId = 1;
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(0, 512, 0), 1);
	g_frontState.cdAudioTrackCache.currentAuxVolume = 12345;
	AdvanceMs(1);
	XvtFrontendTask_ServiceFrameSystems();
	XVT_ASSERT_INT_EQ(g_frontState.cdAudioTrackCache.currentAuxVolume, 256);
	XvtCdTask_CancelFade();
}

static void CheckShutdownBeforeInit(void) {
	static CutsceneEntry table[1];
	Fresh();
	g_cutsceneTable = table;
	g_cutsceneCount = 1;
	XvtFrontendTask_Shutdown();
	XVT_ASSERT_TRUE(g_cutsceneTable == table);
	XVT_ASSERT_INT_EQ(g_cutsceneCount, 1);
	g_cutsceneTable = NULL;
	g_cutsceneCount = 0;
}

static void CheckQuit(void) {
	Fresh();
	FrameDue();
	g_screenReturn = 2;
	XvtFrontendTask_Update();
	XVT_ASSERT_INT_EQ(g_screenCalls, 1);
	XVT_ASSERT_INT_EQ(XvtFrontendTask_ShouldQuit(), 1);
	/* Nothing runs once the quit is set. */
	AdvanceMs(FRAME_MS * 5);
	XvtFrontendTask_Update();
	XVT_ASSERT_INT_EQ(g_screenCalls, 1);
	XVT_ASSERT_INT_EQ(XvtFrontendTask_ShouldQuit(), 1);
}

int main(void) {
	XvtTime_Reset();
	CheckRunFrameWithoutUpdate();
	CheckRunFrame();
	CheckSwitchRunsCapturedExit();
	CheckPendingPush();
	CheckDialogHoldsFrame();
	CheckContinuationReplacesUpdate();
	CheckNetworkTaskHoldsFrame();
	CheckTickPacing();
	CheckTickPresents();
	CheckTickRunsOnlyTheDialog();
	CheckWakeDelayTakesCdSooner();
	CheckServiceFrameSystems();
	CheckShutdownBeforeInit();
	CheckQuit();
	XvtDialog_Shutdown();
	XvtNetworkTask_Shutdown();
	XvtNetworkSession_Shutdown();
	XvtTest_CloseDisplay();
	return 0;
}
