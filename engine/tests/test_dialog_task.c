/* Checks the modal dialog task (xvt_runtime/runtime/dialog_task.h) against the promises in its header:
 * Begin's refusals, the parent state a dialog saves and its end restores, Escape after the first frame,
 * an update that ends the dialog, the held result and who may take it, the confirm and pilot-name entry
 * points, the continuation, and Shutdown. The dialogs run as pushed screens on a frontend display with no
 * window (test_frontend_display.h); the test's own update function stands in for a dialog where the game's
 * dialog would need its images and fonts. Every case starts from a cleared frontend with a placeholder
 * parent screen at frame 5, no dialog and no result.
 *
 * Not checked here: the warning sound, and the program ending when the dialog screen cannot be pushed. */
#include "test_assert.h"
#include "test_frontend_display.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/runtime/dialog_task.h"

#include <string.h>

enum { PARENT_FRAME = 5 };

static int g_updateFrames;
static int g_updateLastFrame;
static int g_updateEnds;
static int g_updateResult;
static int g_continuationCalls;
static int g_continuationResult;
static int g_continuationContext;

static int Parent(int frame) { return frame; }

/* A dialog's update: counts its frames, and when told to, sets the dialog result and ends. */
static int TestUpdate(int frame)
{
	++g_updateFrames;
	g_updateLastFrame = frame;
	if (g_updateEnds) {
		g_dialogResult = g_updateResult;
	}
	return g_updateEnds;
}

static int OtherUpdate(int frame) { return frame; }

static int Continuation(int result, int context)
{
	++g_continuationCalls;
	g_continuationResult = result;
	g_continuationContext = context;
	return 99;
}

static void Fresh(void)
{
	XvtDialog_Shutdown();
	XvtTest_CloseDisplay();
	memset(&g_frontState, 0, sizeof g_frontState);
	XvtTest_OpenDisplay();
	g_frontState.screenStates[0].updateFn = Parent;
	g_frontState.frameCounter = PARENT_FRAME;
	g_gameConfig.sfxDatapadEnabled = 0;
	FrontendButton_DisableOverlayText();
	g_dialogResult = 0;
	g_updateFrames = g_updateLastFrame = g_updateEnds = g_updateResult = 0;
	g_continuationCalls = g_continuationResult = g_continuationContext = 0;
}

static void QueueKeys(const char *keys)
{
	for (; *keys; ++keys) {
		g_frontState.charRingBuffer[g_frontState.charWriteIdx] = *keys;
		g_frontState.charWriteIdx =
			(g_frontState.charWriteIdx + 1) % 1024;
	}
}

static FrontendScreenUpdateFn TopScreen(void)
{
	return g_frontState.screenStates[g_frontState.screenStackTop].updateFn;
}

/* Runs the open dialog's first frame, then ends it with Escape. */
static void EscapeDialog(void)
{
	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 1);
	QueueKeys("\x1b");
	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
}

static void CheckNothingOpen(void)
{
	Fresh();
	int result = 123;
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_IsTextPrompt(), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_TakeResult(&result), 0);
	XVT_ASSERT_INT_EQ(result, 123);
	XVT_ASSERT_INT_EQ(XvtDialog_ContinueWith(Continuation, 1), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_ResumeContinuation(&result), 0);
	XVT_ASSERT_INT_EQ(result, 123);
	XVT_ASSERT_INT_EQ(g_continuationCalls, 0);
	/* A tick with no dialog does nothing. */
	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(g_frontState.screenStackTop, 0);
	XVT_ASSERT_INT_EQ(g_frontState.frameCounter, PARENT_FRAME);
}

static void CheckBeginSavesAndEndRestores(void)
{
	Fresh();
	g_frontState.screenCallbacksDirty = 1;
	g_frontState.offscreenRestoreEnabled = 1;
	g_frontState.mouseX = 100;
	g_frontState.mouseY = 200;
	g_frontState.cursorVisible = 1;
	FrontendButton_EnableOverlayText();
	XVT_ASSERT_INT_EQ(XvtDialog_Begin(TestUpdate, NULL),
			  XVT_DIALOG_PENDING);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 1);
	XVT_ASSERT_INT_EQ(XvtDialog_IsTextPrompt(), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 0);

	/* The dialog's frames change what the parent saved. The display has no offscreen surface, so
	 * offscreen restore must be off while frames run. */
	g_frontState.offscreenRestoreEnabled = 0;
	g_frontState.cursorVisible = 0;
	g_frontState.mouseX = 5;
	g_frontState.mouseY = 5;

	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(g_updateFrames, 1);
	XVT_ASSERT_INT_EQ(g_updateLastFrame, 0);
	XVT_ASSERT_TRUE(TopScreen() == TestUpdate);
	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(g_updateFrames, 2);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 1);

	/* An update returning 1 ends the dialog with the dialog's result. */
	g_updateEnds = 1;
	g_updateResult = 7;
	QueueKeys("xy");
	g_frontState.mouseLeftClickLatch = 1;
	g_frontState.mouseRightClickLatch = 1;
	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 1);

	/* The parent's state is back. */
	XVT_ASSERT_INT_EQ(g_frontState.screenStackTop, 0);
	XVT_ASSERT_TRUE(TopScreen() == Parent);
	XVT_ASSERT_INT_EQ(g_frontState.frameCounter, PARENT_FRAME);
	XVT_ASSERT_INT_EQ(g_frontState.screenCallbacksDirty, 1);
	XVT_ASSERT_INT_EQ(g_frontState.offscreenRestoreEnabled, 1);
	XVT_ASSERT_INT_EQ(g_frontState.cursorVisible, 1);
	XVT_ASSERT_INT_EQ(g_frontState.mouseX, 100);
	XVT_ASSERT_INT_EQ(g_frontState.mouseY, 200);
	XVT_ASSERT_INT_EQ(FrontendButton_IsOverlayTextEnabled(), 1);
	/* The keyboard is flushed and the click latches cleared. */
	XVT_ASSERT_INT_EQ(g_frontState.charReadIdx, g_frontState.charWriteIdx);
	XVT_ASSERT_INT_EQ(g_frontState.mouseLeftClickLatch, 0);
	XVT_ASSERT_INT_EQ(g_frontState.mouseRightClickLatch, 0);

	/* The result is held until taken, once. */
	int result = 0;
	XVT_ASSERT_INT_EQ(XvtDialog_TakeResult(&result), 1);
	XVT_ASSERT_INT_EQ(result, 7);
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_TakeResult(&result), 0);
}

static void CheckOverlayOffRestored(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtDialog_Begin(TestUpdate, NULL),
			  XVT_DIALOG_PENDING);
	FrontendButton_EnableOverlayText();
	EscapeDialog();
	XVT_ASSERT_INT_EQ(FrontendButton_IsOverlayTextEnabled(), 0);
}

static void CheckEscape(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtDialog_Begin(TestUpdate, NULL),
			  XVT_DIALOG_PENDING);
	/* Escape before the first frame does not end the dialog. */
	QueueKeys("\x1b");
	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 1);
	XVT_ASSERT_INT_EQ(g_updateFrames, 1);

	/* After it, Escape ends the dialog with result 0, whatever the dialog's result says. */
	g_dialogResult = 9;
	QueueKeys("\x1b");
	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
	int result = -5;
	XVT_ASSERT_INT_EQ(XvtDialog_TakeResult(&result), 1);
	XVT_ASSERT_INT_EQ(result, 0);
	XVT_ASSERT_INT_EQ(g_frontState.screenStackTop, 0);
	XVT_ASSERT_INT_EQ(g_frontState.frameCounter, PARENT_FRAME);
}

static void CheckBeginRefusals(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtDialog_Begin(TestUpdate, NULL),
			  XVT_DIALOG_PENDING);
	/* While a dialog is open another Begin does nothing. */
	XVT_ASSERT_INT_EQ(XvtDialog_Begin(OtherUpdate, NULL),
			  XVT_DIALOG_PENDING);
	XvtDialog_Update();
	XVT_ASSERT_TRUE(TopScreen() == TestUpdate);
	QueueKeys("\x1b");
	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 1);

	/* While a result is untaken, Begin does nothing either. */
	XVT_ASSERT_INT_EQ(XvtDialog_Begin(OtherUpdate, NULL),
			  XVT_DIALOG_PENDING);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 1);
}

static void CheckConfirm(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(
		XvtDialog_Confirm("one", NULL, "three", "Okay", NULL, 0),
		XVT_DIALOG_PENDING);
	XVT_ASSERT_INT_EQ(strcmp(g_frontDialogLine1OrEdit, "one"), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_frontDialogLine2, ""), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_frontDialogLine3, "three"), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_frontDialogOkayLabel, "Okay"), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_frontDialogCancelLabel, ""), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 1);
	XVT_ASSERT_INT_EQ(XvtDialog_IsTextPrompt(), 0);

	/* While it is open, Confirm returns -1 and copies nothing. */
	XVT_ASSERT_INT_EQ(XvtDialog_Confirm("other", "b", "c", "d", "e", 0),
			  XVT_DIALOG_PENDING);
	XVT_ASSERT_INT_EQ(strcmp(g_frontDialogLine1OrEdit, "one"), 0);

	XvtDialog_Update();
	XVT_ASSERT_TRUE(TopScreen() == FrontendDialog_ConfirmUpdateCallback);
	QueueKeys("\x1b");
	XvtDialog_Update();

	/* The untaken result comes back first, and no dialog opens. */
	XVT_ASSERT_INT_EQ(XvtDialog_Confirm("two", NULL, NULL, NULL, NULL, 0),
			  0);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 0);

	/* With network set, the network abort dialog opens instead. */
	XVT_ASSERT_INT_EQ(
		XvtDialog_Confirm("net", NULL, NULL, NULL, "Cancel", 1),
		XVT_DIALOG_PENDING);
	XVT_ASSERT_INT_EQ(strcmp(g_frontDialogCancelLabel, "Cancel"), 0);
	XvtDialog_Update();
	XVT_ASSERT_TRUE(TopScreen() ==
			FrontendDialog_NetworkAbortErrorCallback);
	QueueKeys("\x1b");
	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
}

static void CheckConfirmReturnsAnyResult(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtDialog_Begin(TestUpdate, NULL),
			  XVT_DIALOG_PENDING);
	XvtDialog_Update();
	g_updateEnds = 1;
	g_updateResult = 4;
	XvtDialog_Update();
	/* The result of a dialog Confirm did not open is returned all the same. */
	XVT_ASSERT_INT_EQ(XvtDialog_Confirm("a", "b", "c", "d", "e", 0), 4);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
}

static void CheckPilotNameTyped(void)
{
	Fresh();
	char name[13];
	XVT_ASSERT_INT_EQ(XvtDialog_PilotName(name), XVT_DIALOG_PENDING);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 1);
	XVT_ASSERT_INT_EQ(XvtDialog_IsTextPrompt(), 1);
	XVT_ASSERT_INT_EQ(XvtDialog_PilotName(name), XVT_DIALOG_PENDING);
	XvtDialog_Update();
	XVT_ASSERT_TRUE(TopScreen() == FrontendDialog_CreatePilotNameCallback);

	/* The prompt takes one typed character a frame, and Enter. */
	QueueKeys("Luke\r");
	for (int frame = 0; frame < 10 && XvtDialog_IsActive(); ++frame) {
		XvtDialog_Update();
	}
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_IsTextPrompt(), 0);
	memset(name, 'Z', sizeof name);
	XVT_ASSERT_INT_EQ(XvtDialog_PilotName(name), 1);
	XVT_ASSERT_INT_EQ(strcmp(name, "Luke"), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 0);
}

static void CheckPilotNameEscaped(void)
{
	Fresh();
	char name[13];
	XVT_ASSERT_INT_EQ(XvtDialog_PilotName(name), XVT_DIALOG_PENDING);
	EscapeDialog();
	memset(name, 'Z', sizeof name);
	XVT_ASSERT_INT_EQ(XvtDialog_PilotName(name), 0);
	XVT_ASSERT_INT_EQ(name[0], 0);
}

static void CheckPilotNameAfterConfirm(void)
{
	Fresh();
	char name[13];
	XVT_ASSERT_INT_EQ(XvtDialog_Confirm("ABCDEFGHIJKLMNOP", NULL, NULL,
					    NULL, NULL, 0),
			  XVT_DIALOG_PENDING);
	EscapeDialog();
	/* The confirm's first line, cut to 12 characters and terminated. */
	memset(name, 'Z', sizeof name);
	XVT_ASSERT_INT_EQ(XvtDialog_PilotName(name), 0);
	XVT_ASSERT_INT_EQ(strcmp(name, "ABCDEFGHIJKL"), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
}

static void CheckContinuation(void)
{
	Fresh();
	int frame_result = 0;
	XVT_ASSERT_INT_EQ(XvtDialog_Begin(TestUpdate, NULL),
			  XVT_DIALOG_PENDING);
	XVT_ASSERT_INT_EQ(XvtDialog_ContinueWith(Continuation, 42), 0);
	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(XvtDialog_ResumeContinuation(&frame_result), 0);
	g_updateEnds = 1;
	g_updateResult = 7;
	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);

	/* The continuation runs once with the result and context, and its return is the frame's. */
	XVT_ASSERT_INT_EQ(XvtDialog_ResumeContinuation(&frame_result), 1);
	XVT_ASSERT_INT_EQ(g_continuationCalls, 1);
	XVT_ASSERT_INT_EQ(g_continuationResult, 7);
	XVT_ASSERT_INT_EQ(g_continuationContext, 42);
	XVT_ASSERT_INT_EQ(frame_result, 99);
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 0);
	frame_result = 0;
	XVT_ASSERT_INT_EQ(XvtDialog_ResumeContinuation(&frame_result), 0);
	XVT_ASSERT_INT_EQ(frame_result, 0);
	XVT_ASSERT_INT_EQ(g_continuationCalls, 1);
}

static void CheckShutdownBeforeFirstFrame(void)
{
	Fresh();
	g_frontState.screenCallbacksDirty = 1;
	g_frontState.offscreenRestoreEnabled = 1;
	g_frontState.cursorVisible = 1;
	XVT_ASSERT_INT_EQ(XvtDialog_Begin(TestUpdate, NULL),
			  XVT_DIALOG_PENDING);
	/* The dialog never ran a frame, but the parent's state changed meanwhile. */
	g_frontState.frameCounter = 40;
	g_frontState.screenCallbacksDirty = 0;
	g_frontState.offscreenRestoreEnabled = 0;
	g_frontState.cursorVisible = 0;
	XvtDialog_Shutdown();
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
	XVT_ASSERT_INT_EQ(g_frontState.screenStackTop, 0);
	XVT_ASSERT_INT_EQ(g_frontState.frameCounter, PARENT_FRAME);
	XVT_ASSERT_INT_EQ(g_frontState.screenCallbacksDirty, 1);
	XVT_ASSERT_INT_EQ(g_frontState.offscreenRestoreEnabled, 1);
	XVT_ASSERT_INT_EQ(g_frontState.cursorVisible, 1);
	XVT_ASSERT_INT_EQ(g_updateFrames, 0);
}

static void CheckShutdown(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtDialog_Begin(TestUpdate, NULL),
			  XVT_DIALOG_PENDING);
	XvtDialog_ContinueWith(Continuation, 3);
	XvtDialog_Update();
	XVT_ASSERT_INT_EQ(g_frontState.screenStackTop, 1);

	/* Shutdown ends the open dialog, restoring its parent, and leaves no result. */
	XvtDialog_Shutdown();
	XVT_ASSERT_INT_EQ(XvtDialog_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 0);
	XVT_ASSERT_INT_EQ(g_frontState.screenStackTop, 0);
	XVT_ASSERT_TRUE(TopScreen() == Parent);
	XVT_ASSERT_INT_EQ(g_frontState.frameCounter, PARENT_FRAME);

	/* The continuation is forgotten too. */
	int frame_result = 0;
	XVT_ASSERT_INT_EQ(XvtDialog_Begin(TestUpdate, NULL),
			  XVT_DIALOG_PENDING);
	EscapeDialog();
	XVT_ASSERT_INT_EQ(XvtDialog_ResumeContinuation(&frame_result), 0);
	XVT_ASSERT_INT_EQ(g_continuationCalls, 0);

	/* An untaken result is forgotten. */
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 1);
	XvtDialog_Shutdown();
	XVT_ASSERT_INT_EQ(XvtDialog_HasResult(), 0);
}

int main(void)
{
	CheckNothingOpen();
	CheckBeginSavesAndEndRestores();
	CheckOverlayOffRestored();
	CheckEscape();
	CheckBeginRefusals();
	CheckConfirm();
	CheckConfirmReturnsAnyResult();
	CheckPilotNameTyped();
	CheckPilotNameEscaped();
	CheckPilotNameAfterConfirm();
	CheckContinuation();
	CheckShutdownBeforeFirstFrame();
	CheckShutdown();
	XvtDialog_Shutdown();
	XvtTest_CloseDisplay();
	return 0;
}
