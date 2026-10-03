#include "xvt/frontend/frontend_dialog.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/dialog_task.h"
#endif
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/net.h"

/* The confirm dialog's OK label, shown only as the OK button's tooltip; empty
 * picks a layout without it (FrontendDialog_ConfirmUpdateCallback). Written as
 * each dialog opens: by FrontendDialog_ShowConfirmDialog and
 * FrontendDialog_ShowNetworkAbortError in the original build, by
 * XvtDialog_Confirm in the modern build. */
// GLOBAL: XVT 0x665688
char g_frontDialogOkayLabel[128] = {0};
/* The third line of the confirm dialog's message, drawn 40 pixels below the
 * first. Written as each dialog opens, like g_frontDialogOkayLabel. */
// GLOBAL: XVT 0x665708
char g_frontDialogLine3[256] = {0};
/* Cursor x when the last confirm, network-error or pilot-name dialog opened.
 * The original build moves the cursor back there when a confirm dialog with
 * neither label closes; the modern build writes it through XvtDialog_Confirm
 * and never reads it. */
// GLOBAL: XVT 0x665808
int g_frontDialogSavedMouseX = 0;
/* Cursor y when the last dialog opened; see g_frontDialogSavedMouseX. */
// GLOBAL: XVT 0x66580C
int g_frontDialogSavedMouseY = 0;
/* The confirm dialog's Cancel label, shown only as the Cancel button's tooltip;
 * empty picks a layout without that button. Written as each dialog opens, like
 * g_frontDialogOkayLabel. */
// GLOBAL: XVT 0x665810
char g_frontDialogCancelLabel[128] = {0};
/* The first line of the confirm dialog's message, or, in the pilot-name prompt,
 * the name being typed, at most 12 characters; the prompt clears it as it opens
 * and its caller copies the name out. Written as each confirm dialog opens,
 * like g_frontDialogOkayLabel. */
// GLOBAL: XVT 0x665890
char g_frontDialogLine1OrEdit[256] = {0};
/* The second line of the confirm dialog's message, drawn 20 pixels below the
 * first. Written as each dialog opens, like g_frontDialogOkayLabel. */
// GLOBAL: XVT 0x665990
char g_frontDialogLine2[256] = {0};
/* The last confirm dialog's answer: 1 for OK or Enter, 0 for Cancel, Esc or a
 * network dismissal. Written by FrontendDialog_ConfirmUpdateCallback and
 * FrontendDialog_NetworkAbortErrorCallback; the modern build's XvtDialog_Update
 * sets it to 0 as each dialog opens. */
// GLOBAL: XVT 0xAA62A0
int g_dialogResult = 0;

/* Shows the confirm dialog, three message lines and the OK and Cancel labels,
 * NULL meaning empty, and returns its answer: 1 for OK, 0 for Cancel or a
 * network dismissal. The original build runs it modally over the whole screen:
 * it turns overlay text off, plays "warningsound" when
 * g_gameConfig.sfxDatapadEnabled is set, saves the cursor position in
 * g_frontDialogSavedMouseX and g_frontDialogSavedMouseY, copies the texts into
 * the g_frontDialog buffers with strcpy, unchecked against their 256 and 128
 * bytes, runs FrontendDialog_ConfirmUpdateCallback, stops the text fade, turns
 * overlay text back on when it was on and returns g_dialogResult. The modern
 * build returns XvtDialog_Confirm's result: XVT_DIALOG_PENDING (-1) while the
 * dialog runs, then the answer on the first call after it closes. */
// FUNCTION: XVT 0x4DCB90
int FrontendDialog_ShowConfirmDialog(const char *line1, const char *line2,
				     const char *line3, const char *okayLabel,
				     const char *cancelLabel)
{
#ifdef XVT_MODERN
	return XvtDialog_Confirm(line1, line2, line3, okayLabel, cancelLabel,
				 0);
#else
	enum {
		SCREEN_LEFT = 0,
		SCREEN_TOP = 0,
		SCREEN_RIGHT = 640,
		SCREEN_BOTTOM = 480,
		SOUND_ALLOW_RESTART = 1,
		SOUND_NO_LOOP = 0,
		SOUND_PRIORITY = 255,
		SOUND_VOLUME_SCALE = 12,
		SOUND_CENTER_PAN = 63,
	};

	struct RECT rect;
	int overlayTextEnabled;

	overlayTextEnabled = FrontendButton_IsOverlayTextEnabled();
	FrontendButton_DisableOverlayText();
	if (g_gameConfig.sfxDatapadEnabled != 0) {
		FrontendSound_PlayUISound("warningsound", SOUND_ALLOW_RESTART,
					  SOUND_NO_LOOP, SOUND_PRIORITY,
					  SOUND_VOLUME_SCALE *
						  g_gameConfig.sfxDatapadVolume,
					  SOUND_CENTER_PAN);
	}
	FrontendDraw_RectAssign(&rect, SCREEN_LEFT, SCREEN_TOP, SCREEN_RIGHT,
				SCREEN_BOTTOM);
	FrontendCursor_GetPos(&g_frontDialogSavedMouseX,
			      &g_frontDialogSavedMouseY);
	if (line1 != NULL) {
		strcpy(g_frontDialogLine1OrEdit, line1);
	} else {
		g_frontDialogLine1OrEdit[0] = '\0';
	}
	if (line2 != NULL) {
		strcpy(g_frontDialogLine2, line2);
	} else {
		g_frontDialogLine2[0] = '\0';
	}
	if (line3 != NULL) {
		strcpy(g_frontDialogLine3, line3);
	} else {
		g_frontDialogLine3[0] = '\0';
	}
	if (okayLabel != NULL) {
		strcpy(g_frontDialogOkayLabel, okayLabel);
	} else {
		g_frontDialogOkayLabel[0] = '\0';
	}
	if (cancelLabel != NULL) {
		strcpy(g_frontDialogCancelLabel, cancelLabel);
	} else {
		g_frontDialogCancelLabel[0] = '\0';
	}
	FrontendScreen_RunModal(FrontendDialog_ConfirmUpdateCallback, &rect);
	FrontendText_StopTextFade();
	if (overlayTextEnabled != 0) {
		FrontendButton_EnableOverlayText();
	}
	return g_dialogResult;
#endif
}

/* The confirm dialog's frame function. On frame 0 it flushes the typed
 * characters, puts the cursor at (184, 255) on the OK button, or at (496, 255)
 * when only a Cancel label is set, draws the "dialogbox" sprite translucent
 * three times onto the offscreen surface, starts a 20-frame text fade-in and
 * returns 0. On later frames it sets g_dialogResult to 0 and ends when
 * FrontendDialog_HasNetworkDismissPacket returns 1, draws the three message
 * lines centered in the size-15 font in 0xFFFF, from y 225, 20 pixels apart,
 * and runs the buttons. With neither label it shows an OK button whose tooltip
 * is string FRONTSTR_523_OKAY; when it is pressed, the original build moves the
 * cursor back to g_frontDialogSavedMouseX and Y. With only one label, that one
 * button; with both, both. OK, or Enter, sets g_dialogResult to 1; Cancel, or
 * Esc, sets it to 0. Returns 1 when the dialog ends, else 0. Each later frame
 * takes one character from the keyboard buffer, and with both buttons up to
 * three. */
// FUNCTION: XVT 0x4DCD30
int FrontendDialog_ConfirmUpdateCallback(int frameCounter)
{
	enum {
		CURSOR_OK_X = 184,
		CURSOR_CANCEL_X = 496,
		CURSOR_Y = 255,
		DIALOG_LEFT = 166,
		DIALOG_TOP = 225,
		DIALOG_RIGHT = 518,
		DIALOG_BOTTOM = 245,
		DIALOG_LINE_HEIGHT = 20,
		OK_BUTTON_LEFT = 171,
		OK_BUTTON_TOP = 240,
		OK_BUTTON_RIGHT = 202,
		BUTTON_BOTTOM = 275,
		CANCEL_BUTTON_LEFT = 481,
		CANCEL_BUTTON_RIGHT = 513,
		DIALOG_FONT_SIZE = 15,
		BUTTON_FONT_SIZE = 12,
		OK_HELD_SLOT = 20,
		CANCEL_HELD_SLOT = 21,
		TEXT_COLOR = 0xFFFF,
		KEY_ENTER = 13,
		KEY_ESCAPE = 27,
		SAVE_OFFSCREEN_BACKUP = 1,
		TEXT_FADE_FRAMES = 20,
	};

	struct RECT rect;
	int finished;
	int pressed;

	finished = 0;
	if (frameCounter == 0) {
		Keyboard_FlushCharBuffer();
		if (g_frontDialogOkayLabel[0] || !g_frontDialogCancelLabel[0]) {
			FrontendCursor_SetPos(CURSOR_OK_X, CURSOR_Y);
		} else {
			FrontendCursor_SetPos(CURSOR_CANCEL_X, CURSOR_Y);
		}
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteTranslucent("dialogbox", 0, 0);
		FrontImage_DrawSpriteTranslucent("dialogbox", 0, 0);
		FrontImage_DrawSpriteTranslucent("dialogbox", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(SAVE_OFFSCREEN_BACKUP);
		FrontendText_StartTextFadeIn(TEXT_FADE_FRAMES);
		return 0;
	}

	if (FrontendDialog_HasNetworkDismissPacket() != 0) {
		g_dialogResult = 0;
		finished = 1;
	}
	FrontendDraw_RectAssign(&rect, DIALOG_LEFT, DIALOG_TOP, DIALOG_RIGHT,
				DIALOG_BOTTOM);
	rect.bottom = rect.top + DIALOG_LINE_HEIGHT;
	FrontendText_DrawCentered(DIALOG_FONT_SIZE, g_frontDialogLine1OrEdit,
				  &rect, TEXT_COLOR);
	FrontendDraw_RectOffsetXY(&rect, 0, DIALOG_LINE_HEIGHT);
	FrontendText_DrawCentered(DIALOG_FONT_SIZE, g_frontDialogLine2, &rect,
				  TEXT_COLOR);
	FrontendDraw_RectOffsetXY(&rect, 0, DIALOG_LINE_HEIGHT);
	FrontendText_DrawCentered(DIALOG_FONT_SIZE, g_frontDialogLine3, &rect,
				  TEXT_COLOR);

	if (!g_frontDialogOkayLabel[0] && !g_frontDialogCancelLabel[0]) {
		FrontendDraw_RectAssign(&rect, OK_BUTTON_LEFT, OK_BUTTON_TOP,
					OK_BUTTON_RIGHT, BUTTON_BOTTOM);
		pressed = FrontendButton_HandleSpriteButton(
			&rect, "dialogok", "dialogokd",
			FrontendString_Get(FRONTSTR_523_OKAY), BUTTON_FONT_SIZE,
			TEXT_COLOR, OK_HELD_SLOT, "buttonsound");
		if (Keyboard_DequeueChar() == KEY_ENTER) {
			pressed = 1;
		}
		if (pressed != 0) {
			finished = 1;
			g_dialogResult = 1;
#ifndef XVT_MODERN
			FrontendCursor_SetPos(g_frontDialogSavedMouseX,
					      g_frontDialogSavedMouseY);
#endif
		}
	} else if (!g_frontDialogOkayLabel[0]) {
		FrontendDraw_RectAssign(&rect, CANCEL_BUTTON_LEFT,
					OK_BUTTON_TOP, CANCEL_BUTTON_RIGHT,
					BUTTON_BOTTOM);
		pressed = FrontendButton_HandleSpriteButton(
			&rect, "dialogcancel", "dialogcanceld",
			g_frontDialogCancelLabel, BUTTON_FONT_SIZE, TEXT_COLOR,
			CANCEL_HELD_SLOT, "buttonsound");
		if (Keyboard_DequeueChar() == KEY_ESCAPE) {
			pressed = 1;
		}
		if (pressed != 0) {
			g_dialogResult = 0;
			finished = 1;
		}
	} else if (!g_frontDialogCancelLabel[0]) {
		FrontendDraw_RectAssign(&rect, OK_BUTTON_LEFT, OK_BUTTON_TOP,
					OK_BUTTON_RIGHT, BUTTON_BOTTOM);
		pressed = FrontendButton_HandleSpriteButton(
			&rect, "dialogok", "dialogokd", g_frontDialogOkayLabel,
			BUTTON_FONT_SIZE, TEXT_COLOR, OK_HELD_SLOT,
			"buttonsound");
		if (Keyboard_DequeueChar() == KEY_ENTER) {
			pressed = 1;
		}
		if (pressed != 0) {
			finished = 1;
			g_dialogResult = 1;
		}
	} else {
		FrontendDraw_RectAssign(&rect, OK_BUTTON_LEFT, OK_BUTTON_TOP,
					OK_BUTTON_RIGHT, BUTTON_BOTTOM);
		pressed = FrontendButton_HandleSpriteButton(
			&rect, "dialogok", "dialogokd", g_frontDialogOkayLabel,
			BUTTON_FONT_SIZE, TEXT_COLOR, OK_HELD_SLOT,
			"buttonsound");
		if (Keyboard_PeekChar() == KEY_ENTER) {
			pressed = 1;
			Keyboard_DequeueChar();
		}
		if (pressed != 0) {
			finished = 1;
			g_dialogResult = 1;
		}
		FrontendDraw_RectAssign(&rect, CANCEL_BUTTON_LEFT,
					OK_BUTTON_TOP, CANCEL_BUTTON_RIGHT,
					BUTTON_BOTTOM);
		pressed = FrontendButton_HandleSpriteButton(
			&rect, "dialogcancel", "dialogcanceld",
			g_frontDialogCancelLabel, BUTTON_FONT_SIZE, TEXT_COLOR,
			CANCEL_HELD_SLOT, "buttonsound");
		if (Keyboard_DequeueChar() == KEY_ESCAPE) {
			pressed = 1;
			Keyboard_DiscardChar();
		}
		if (pressed != 0) {
			g_dialogResult = 0;
			finished = 1;
		}
	}
	return finished != 0;
}

/* Returns 1 when Net_PollForPacketTypeOrBacklog reports a queued packet of any
 * of the 15 lobby packet types below, or a backlog of more than 512 packets,
 * else 0. Each poll pumps incoming packets; nothing is taken from the queue. */
// FUNCTION: XVT 0x4DD0F0
int FrontendDialog_HasNetworkDismissPacket(void)
{

	if (Net_PollForPacketTypeOrBacklog(NET_PACKET_PLAYER_ADMITTED) != 0) {
		return 1;
	}
	if (Net_PollForPacketTypeOrBacklog(NET_PACKET_HOST_CANCELLED) != 0) {
		return 1;
	}
	if (Net_PollForPacketTypeOrBacklog(NET_PACKET_PLAYER_LEFT) != 0) {
		return 1;
	}
	if (Net_PollForPacketTypeOrBacklog(NET_PACKET_TEAM_ASSIGNMENTS_READY) !=
	    0) {
		return 1;
	}
	if (Net_PollForPacketTypeOrBacklog(NET_PACKET_FRONTEND_MISSION_START) !=
	    0) {
		return 1;
	}
	if (Net_PollForPacketTypeOrBacklog(NET_PACKET_FRONTEND_OPCODE_74) !=
	    0) {
		return 1;
	}
	if (Net_PollForPacketTypeOrBacklog(
		    NET_PACKET_NEXT_TOURNAMENT_MISSION) != 0) {
		return 1;
	}
	if (Net_PollForPacketTypeOrBacklog(NET_PACKET_NEXT_BATTLE_MISSION) !=
	    0) {
		return 1;
	}
	if (Net_PollForPacketTypeOrBacklog(NET_PACKET_REPLAY_CURRENT_MISSION) !=
	    0) {
		return 1;
	}
	if (Net_PollForPacketTypeOrBacklog(NET_PACKET_RETURN_TO_SETUP) != 0) {
		return 1;
	}
	if (Net_PollForPacketTypeOrBacklog(NET_PACKET_REPLAY_MISSION) != 0) {
		return 1;
	}
	if (Net_PollForPacketTypeOrBacklog(NET_PACKET_PLAYER_KICKED) != 0) {
		return 1;
	}
	if (Net_PollForPacketTypeOrBacklog(NET_PACKET_SESSION_CANCELLED) != 0) {
		return 1;
	}
	if (Net_PollForPacketTypeOrBacklog(
		    NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS) != 0) {
		return 1;
	}
	return Net_PollForPacketTypeOrBacklog(
		       NET_PACKET_RETURN_TO_MISSION_SELECTION) != 0;
}

/* Asks for a new pilot's name and copies it into outName as 12 characters and a
 * NUL, so outName needs 13 bytes. The original build turns overlay text off,
 * saves the cursor position, clears g_frontDialogLine1OrEdit, runs
 * FrontendDialog_CreatePilotNameCallback modally over the whole screen until a
 * name is accepted, stops the text fade, turns overlay text back on when it was
 * on and returns 1. The modern build returns XvtDialog_PilotName's result:
 * XVT_DIALOG_PENDING (-1) while the prompt runs, then, on the first call after
 * it closes, the name copied the same way and 1 when one was entered, else
 * 0. */
// FUNCTION: XVT 0x4DD220
int FrontendDialog_PromptForPilotName(char *outName)
{
#ifdef XVT_MODERN
	return XvtDialog_PilotName(outName);
#else
	enum {
		SCREEN_LEFT = 0,
		SCREEN_TOP = 0,
		SCREEN_RIGHT = 640,
		SCREEN_BOTTOM = 480,
		PILOT_NAME_LENGTH = 12,
	};

	struct RECT rect;
	int overlayTextEnabled;

	overlayTextEnabled = FrontendButton_IsOverlayTextEnabled();
	FrontendButton_DisableOverlayText();
	FrontendDraw_RectAssign(&rect, SCREEN_LEFT, SCREEN_TOP, SCREEN_RIGHT,
				SCREEN_BOTTOM);
	FrontendCursor_GetPos(&g_frontDialogSavedMouseX,
			      &g_frontDialogSavedMouseY);
	memset(g_frontDialogLine1OrEdit, 0, sizeof(g_frontDialogLine1OrEdit));
	FrontendScreen_RunModal(FrontendDialog_CreatePilotNameCallback, &rect);
	FrontendText_StopTextFade();
	if (overlayTextEnabled != 0) {
		FrontendButton_EnableOverlayText();
	}
	memcpy(outName, g_frontDialogLine1OrEdit, PILOT_NAME_LENGTH);
	outName[PILOT_NAME_LENGTH] = '\0';
	return 1;
#endif
}

/* The pilot-name prompt's frame function. On frame 0 it puts the cursor at
 * (417, 291), flushes the typed characters, registers frontres\create.bmp as
 * the image "backname" and draws it opaque on the offscreen surface, then the
 * "frame" sprite, "allactive" (or "clientactive" when g_hostCdAvailable is 0)
 * and "createoverlay" translucent. Every frame it draws the heading string
 * FRONTSTR_716_CREATE_A_NEW_PILOT, an edit field over g_frontDialogLine1OrEdit
 * that takes up to 12 characters, none of \ * $, and a text button labeled
 * FRONTSTR_717_CREATE_PILOT. Enter, the field's own Enter or Tab, or the button
 * accepts; with a name typed it then frees "backname" and returns 1. Returns 0
 * otherwise. An Esc next in the keyboard buffer is taken; the modern build then
 * clears the name, frees "backname" and returns 1, while the original build
 * keeps prompting. */
// FUNCTION: XVT 0x4DD2C0
int FrontendDialog_CreatePilotNameCallback(int frameCounter)
{
	struct RECT rect;
	int accepted;

	if (frameCounter == 0) {
		FrontendCursor_SetPos(417, 291);
		Keyboard_FlushCharBuffer();
		FrontImage_RegisterResourceDefault("frontres\\create.bmp",
						   "backname");
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("backname", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		if (g_hostCdAvailable != 0) {
			FrontImage_DrawSprite("allactive", 0, 0);
		} else {
			FrontImage_DrawSprite("clientactive", 0, 0);
		}
		FrontImage_DrawSpriteTranslucent("createoverlay", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
	}

	FrontendDraw_RectAssign(&rect, 245, 225, 445, 245);
	FrontendText_DrawCentered(
		15, FrontendString_Get(FRONTSTR_716_CREATE_A_NEW_PILOT), &rect,
		0xFFFF);
	FrontendDraw_RectAssign(&rect, 250, 245, 440, 265);
	accepted = FrontendText_HandleEditableField(
		&rect, g_frontDialogLine1OrEdit, 13, 0, 12, "\\*$");
	FrontendDraw_RectAssign(&rect, 250, 275, 440, 295);
	accepted |= FrontendButton_HandleTextButton(
		&rect, FrontendString_Get(FRONTSTR_717_CREATE_PILOT), 15,
		0xFFFF, 20, "buttonsound");

	if (Keyboard_PeekChar() == 13) {
		Keyboard_DiscardChar();
		accepted = 1;
	} else if (Keyboard_PeekChar() == 27) {
		Keyboard_DiscardChar();
#ifdef XVT_MODERN
		g_frontDialogLine1OrEdit[0] = 0;
		FrontImage_FreeResourceByName("backname");
		return 1;
#endif
	}

	if (!accepted || g_frontDialogLine1OrEdit[0] == '\0') {
		return 0;
	}
	FrontImage_FreeResourceByName("backname");
	return 1;
}

/* Only the original build calls this, when destroying the local DirectPlay
 * player took longer than NET_DESTROY_PLAYER_TIMEOUT_MS. Shows the dialog the
 * way FrontendDialog_ShowConfirmDialog does, with
 * FrontendDialog_NetworkAbortErrorCallback, which no network packet dismisses,
 * and returns g_dialogResult. The modern build's body returns
 * XvtDialog_Confirm's result for the same dialog. */
// FUNCTION: XVT 0x4DD480
int FrontendDialog_ShowNetworkAbortError(const char *line1, const char *line2,
					 const char *line3,
					 const char *okayLabel,
					 const char *cancelLabel)
{
#ifdef XVT_MODERN
	return XvtDialog_Confirm(line1, line2, line3, okayLabel, cancelLabel,
				 1);
#else
	enum {
		SCREEN_LEFT = 0,
		SCREEN_TOP = 0,
		SCREEN_RIGHT = 640,
		SCREEN_BOTTOM = 480,
		SOUND_ALLOW_RESTART = 1,
		SOUND_NO_LOOP = 0,
		SOUND_PRIORITY = 255,
		SOUND_VOLUME_SCALE = 12,
		SOUND_CENTER_PAN = 63,
	};

	struct RECT rect;
	int overlayTextEnabled;

	overlayTextEnabled = FrontendButton_IsOverlayTextEnabled();
	FrontendButton_DisableOverlayText();
	if (g_gameConfig.sfxDatapadEnabled != 0) {
		FrontendSound_PlayUISound("warningsound", SOUND_ALLOW_RESTART,
					  SOUND_NO_LOOP, SOUND_PRIORITY,
					  SOUND_VOLUME_SCALE *
						  g_gameConfig.sfxDatapadVolume,
					  SOUND_CENTER_PAN);
	}
	FrontendDraw_RectAssign(&rect, SCREEN_LEFT, SCREEN_TOP, SCREEN_RIGHT,
				SCREEN_BOTTOM);
	FrontendCursor_GetPos(&g_frontDialogSavedMouseX,
			      &g_frontDialogSavedMouseY);
	if (line1 != NULL) {
		strcpy(g_frontDialogLine1OrEdit, line1);
	} else {
		g_frontDialogLine1OrEdit[0] = '\0';
	}
	if (line2 != NULL) {
		strcpy(g_frontDialogLine2, line2);
	} else {
		g_frontDialogLine2[0] = '\0';
	}
	if (line3 != NULL) {
		strcpy(g_frontDialogLine3, line3);
	} else {
		g_frontDialogLine3[0] = '\0';
	}
	if (okayLabel != NULL) {
		strcpy(g_frontDialogOkayLabel, okayLabel);
	} else {
		g_frontDialogOkayLabel[0] = '\0';
	}
	if (cancelLabel != NULL) {
		strcpy(g_frontDialogCancelLabel, cancelLabel);
	} else {
		g_frontDialogCancelLabel[0] = '\0';
	}
	FrontendScreen_RunModal(FrontendDialog_NetworkAbortErrorCallback,
				&rect);
	FrontendText_StopTextFade();
	if (overlayTextEnabled != 0) {
		FrontendButton_EnableOverlayText();
	}
	return g_dialogResult;
#endif
}

/* The network-error dialog's frame function: the same as
 * FrontendDialog_ConfirmUpdateCallback, without the check for a dismissing
 * network packet. Returns 1 when the dialog ends, else 0. Only the original
 * build reaches it. */
// FUNCTION: XVT 0x4DD620
int FrontendDialog_NetworkAbortErrorCallback(int frameCounter)
{
	struct RECT rect;
	int finished;
	int pressed;

	finished = 0;
	if (frameCounter == 0) {
		Keyboard_FlushCharBuffer();
		if (g_frontDialogOkayLabel[0] != '\0' ||
		    g_frontDialogCancelLabel[0] == '\0') {
			FrontendCursor_SetPos(184, 255);
		} else {
			FrontendCursor_SetPos(496, 255);
		}
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteTranslucent("dialogbox", 0, 0);
		FrontImage_DrawSpriteTranslucent("dialogbox", 0, 0);
		FrontImage_DrawSpriteTranslucent("dialogbox", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_StartTextFadeIn(20);
		return 0;
	}

	FrontendDraw_RectAssign(&rect, 166, 225, 518, 245);
	rect.bottom = rect.top + 20;
	FrontendText_DrawCentered(15, g_frontDialogLine1OrEdit, &rect, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 20);
	FrontendText_DrawCentered(15, g_frontDialogLine2, &rect, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 20);
	FrontendText_DrawCentered(15, g_frontDialogLine3, &rect, 0xFFFF);

	if (g_frontDialogOkayLabel[0] == '\0') {
		if (g_frontDialogCancelLabel[0] == '\0') {
			FrontendDraw_RectAssign(&rect, 171, 240, 202, 275);
			pressed = FrontendButton_HandleSpriteButton(
				&rect, "dialogok", "dialogokd",
				FrontendString_Get(FRONTSTR_523_OKAY), 12,
				0xFFFF, 20, "buttonsound");
			if (Keyboard_DequeueChar() == 13) {
				pressed = 1;
			}
			if (pressed != 0) {
				finished = 1;
				g_dialogResult = 1;
#ifndef XVT_MODERN
				FrontendCursor_SetPos(g_frontDialogSavedMouseX,
						      g_frontDialogSavedMouseY);
#endif
			}
		} else {
			FrontendDraw_RectAssign(&rect, 481, 240, 513, 275);
			pressed = FrontendButton_HandleSpriteButton(
				&rect, "dialogcancel", "dialogcanceld",
				g_frontDialogCancelLabel, 12, 0xFFFF, 21,
				"buttonsound");
			if (Keyboard_DequeueChar() == 27) {
				pressed = 1;
			}
			if (pressed != 0) {
				g_dialogResult = 0;
				finished = 1;
			}
		}
	} else {
		FrontendDraw_RectAssign(&rect, 171, 240, 202, 275);
		if (g_frontDialogCancelLabel[0] == '\0') {
			pressed = FrontendButton_HandleSpriteButton(
				&rect, "dialogok", "dialogokd",
				g_frontDialogOkayLabel, 12, 0xFFFF, 20,
				"buttonsound");
			if (Keyboard_DequeueChar() == 13) {
				pressed = 1;
			}
			if (pressed != 0) {
				finished = 1;
				g_dialogResult = 1;
			}
		} else {
			pressed = FrontendButton_HandleSpriteButton(
				&rect, "dialogok", "dialogokd",
				g_frontDialogOkayLabel, 12, 0xFFFF, 20,
				"buttonsound");
			if (Keyboard_PeekChar() == 13) {
				pressed = 1;
				Keyboard_DequeueChar();
			}
			if (pressed != 0) {
				finished = 1;
				g_dialogResult = 1;
			}
			FrontendDraw_RectAssign(&rect, 481, 240, 513, 275);
			pressed = FrontendButton_HandleSpriteButton(
				&rect, "dialogcancel", "dialogcanceld",
				g_frontDialogCancelLabel, 12, 0xFFFF, 21,
				"buttonsound");
			if (Keyboard_DequeueChar() == 27) {
				pressed = 1;
				Keyboard_DiscardChar();
			}
			if (pressed != 0) {
				g_dialogResult = 0;
				finished = 1;
			}
		}
	}
	return finished != 0;
}
