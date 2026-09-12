#include "xvt_runtime/runtime/dialog_task.h"

#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/runtime/presentation.h"

#include "aeron/aeron.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/runtime/frontend_task.h"
#include "xvt_runtime/storage/storage.h"

#include <stdio.h>
#include <string.h>

static struct {
	FrontendScreenUpdateFn update;
	RECT rect;
	int active;
	int pushed;
	int complete;
	int result;
	int overlay;
	int cursor_x;
	int cursor_y;
	int cursor_visible;
	int dirty;
	int parent_frame;
	int restore;
	int pilot;
	XvtDialogContinuation continuation;
	int context;
} g_dialog;

int XvtDialog_ContinueWith(XvtDialogContinuation continuation, int context) {
	g_dialog.continuation = continuation;
	g_dialog.context = context;
	return 0;
}

int XvtDialog_ResumeContinuation(int* frame_result) {
	XvtDialogContinuation continuation;
	int result;
	int context;
	if (!g_dialog.complete || !g_dialog.continuation)
		return 0;
	continuation = g_dialog.continuation;
	context = g_dialog.context;
	g_dialog.continuation = NULL;
	XvtDialog_TakeResult(&result);
	*frame_result = continuation(result, context);
	return 1;
}

int XvtDialog_Begin(FrontendScreenUpdateFn update, const RECT* rect) {
	if (g_dialog.active || g_dialog.complete)
		return XVT_DIALOG_PENDING;
	XvtPresentation_RequireClassic();
	g_dialog.update = update;
	g_dialog.rect = rect ? *rect : (RECT) { 0, 0, 639, 479 };
	g_dialog.overlay = FrontendButton_IsOverlayTextEnabled();
	g_dialog.cursor_x = g_frontState.mouseX;
	g_dialog.cursor_y = g_frontState.mouseY;
	g_dialog.cursor_visible = g_frontState.cursorVisible;
	g_dialog.dirty = g_frontState.screenCallbacksDirty;
	g_dialog.parent_frame = g_frontState.frameCounter;
	g_dialog.restore = g_frontState.offscreenRestoreEnabled;
	g_dialog.active = 1;
	XvtInput_UpdateMouseCapture(Aeron_InputSnapshot());
	g_dialog.pushed = 0;
	FrontendButton_DisableOverlayText();
	Aeron_LogInfo("xvt.dialog", "Dialog opened");
	return XVT_DIALOG_PENDING;
}

static void XvtDialog_End(void) {
	FrontendDisplay_UnlockBackBuffer();
	if (g_dialog.pushed)
		FrontendScreen_PopState();
	g_frontState.frameCounter = g_dialog.parent_frame;
	g_frontState.screenCallbacksDirty = g_dialog.dirty;
	g_frontState.offscreenRestoreEnabled = g_dialog.restore;
	g_frontState.cursorVisible = g_dialog.cursor_visible;
	FrontendCursor_SetPos(g_dialog.cursor_x, g_dialog.cursor_y);
	FrontendText_ResetGlyphScratch();
	if (g_dialog.overlay)
		FrontendButton_EnableOverlayText();
	else
		FrontendButton_DisableOverlayText();
	Keyboard_FlushCharBuffer();
	g_frontState.mouseClickLatch = g_frontState.mouseRightClickLatch = 0;
	g_dialog.active = 0;
	g_dialog.pushed = 0;
	g_dialog.complete = 1;
	Aeron_LogInfo("xvt.dialog", "Dialog completed (%d)", g_dialog.result);
}

void XvtDialog_Tick(void) {
	int result;
	if (!g_dialog.active)
		return;
	if (!g_dialog.pushed) {
		if (!FrontendScreen_PushState(g_dialog.update, &g_dialog.rect)) {
			XvtStorage_Fatal("Cannot allocate dialog screen", 1);
			g_dialog.result = 0;
			XvtDialog_End();
			return;
		}
		g_dialog.pushed = 1;
		g_frontState.screenStates[g_frontState.screenStackTop].exitFn = NULL;
		g_frontState.frameCounter = 0;
		g_frontState.screenCallbacksDirty = 0;
		g_dialogResult = 0;
		Keyboard_FlushCharBuffer();
		g_frontState.mouseClickLatch = g_frontState.mouseRightClickLatch = 0;
	}
	if (g_frontState.frameCounter > 0 && Keyboard_PeekChar() == 27) {
		g_dialog.result = 0;
		if (g_dialog.pilot) {
			g_frontDialogText0[0] = 0;
			FrontImage_FreeResourceByName("backname");
		}
		XvtDialog_End();
		return;
	}
	result = XvtFrontendTask_RunFrame();
	if (result == 1) {
		g_dialog.result = g_dialog.pilot ? g_frontDialogText0[0] != 0 : g_dialogResult;
		XvtDialog_End();
	}
}

int XvtDialog_IsActive(void) { return g_dialog.active; }

int XvtDialog_IsTextPrompt(void) { return g_dialog.active && g_dialog.pilot; }

int XvtDialog_HasResult(void) { return g_dialog.complete; }

int XvtDialog_TakeResult(int* result) {
	if (!g_dialog.complete)
		return 0;
	*result = g_dialog.result;
	g_dialog.complete = 0;
	return 1;
}

int XvtDialog_Confirm(const char* a, const char* b, const char* c, const char* okay, const char* cancel,
					  int network) {
	int result;
	if (XvtDialog_TakeResult(&result))
		return result;
	if (g_dialog.active)
		return XVT_DIALOG_PENDING;
	snprintf(g_frontDialogText0, sizeof(g_frontDialogText0), "%s", a ? a : "");
	snprintf(g_frontDialogText1, sizeof(g_frontDialogText1), "%s", b ? b : "");
	snprintf(g_frontDialogText2OrEdit, sizeof(g_frontDialogText2OrEdit), "%s", c ? c : "");
	snprintf(g_frontDialogOkayLabel, sizeof(g_frontDialogOkayLabel), "%s", okay ? okay : "");
	snprintf(g_frontDialogCancelLabel, sizeof(g_frontDialogCancelLabel), "%s", cancel ? cancel : "");
	FrontendCursor_GetPos(&g_frontDialogSavedMouseX, &g_frontDialogSavedMouseY);
	if (g_gameConfig.sfxDatapadEnabled)
		FrontendSound_PlayUISound("warningsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
	g_dialog.pilot = 0;
	return XvtDialog_Begin(
		network ? FrontendDialog_NetworkAbortErrorCallback : FrontendDialog_ConfirmUpdateCallback, NULL);
}

int XvtDialog_PilotName(char* name) {
	int result;
	if (XvtDialog_TakeResult(&result)) {
		memcpy(name, g_frontDialogText0, 12);
		name[12] = 0;
		return result;
	}
	if (!g_dialog.active) {
		memset(g_frontDialogText0, 0, sizeof(g_frontDialogText0));
		g_dialog.pilot = 1;
		XvtDialog_Begin(FrontendDialog_CreatePilotNameCallback, NULL);
	}
	return XVT_DIALOG_PENDING;
}

void XvtDialog_Shutdown(void) {
	if (g_dialog.active) {
		if (g_dialog.pilot)
			FrontImage_FreeResourceByName("backname");
		XvtDialog_End();
	}
	memset(&g_dialog, 0, sizeof(g_dialog));
}
