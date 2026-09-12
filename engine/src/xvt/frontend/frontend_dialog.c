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

// GLOBAL: XVT 0x665688
char g_frontDialogOkayLabel[128] = { 0 };
// GLOBAL: XVT 0x665708
char g_frontDialogText2OrEdit[256] = { 0 };
// GLOBAL: XVT 0x665808
int g_frontDialogSavedMouseX = 0;
// GLOBAL: XVT 0x66580C
int g_frontDialogSavedMouseY = 0;
// GLOBAL: XVT 0x665810
char g_frontDialogCancelLabel[128] = { 0 };
// GLOBAL: XVT 0x665890
char g_frontDialogText0[256] = { 0 };
// GLOBAL: XVT 0x665990
char g_frontDialogText1[256] = { 0 };
// GLOBAL: XVT 0xAA62A0
int g_dialogResult = 0;

// FUNCTION: XVT 0x4DCB90
int FrontendDialog_ShowConfirmDialog(const char* line1, const char* line2, const char* line3,
									 const char* okayLabel, const char* cancelLabel) {
#ifdef XVT_MODERN
	return XvtDialog_Confirm(line1, line2, line3, okayLabel, cancelLabel, 0);
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

	RECT rect;
	int overlayTextEnabled;

	overlayTextEnabled = FrontendButton_IsOverlayTextEnabled();
	FrontendButton_DisableOverlayText();
	if (g_gameConfig.sfxDatapadEnabled != 0) {
		FrontendSound_PlayUISound("warningsound", SOUND_ALLOW_RESTART, SOUND_NO_LOOP, SOUND_PRIORITY,
								  SOUND_VOLUME_SCALE * g_gameConfig.sfxDatapadVolume, SOUND_CENTER_PAN);
	}
	FrontendDraw_RectAssign(&rect, SCREEN_LEFT, SCREEN_TOP, SCREEN_RIGHT, SCREEN_BOTTOM);
	FrontendCursor_GetPos(&g_frontDialogSavedMouseX, &g_frontDialogSavedMouseY);
	if (line1 != NULL)
		strcpy(g_frontDialogText0, line1);
	else
		g_frontDialogText0[0] = '\0';
	if (line2 != NULL)
		strcpy(g_frontDialogText1, line2);
	else
		g_frontDialogText1[0] = '\0';
	if (line3 != NULL)
		strcpy(g_frontDialogText2OrEdit, line3);
	else
		g_frontDialogText2OrEdit[0] = '\0';
	if (okayLabel != NULL)
		strcpy(g_frontDialogOkayLabel, okayLabel);
	else
		g_frontDialogOkayLabel[0] = '\0';
	if (cancelLabel != NULL)
		strcpy(g_frontDialogCancelLabel, cancelLabel);
	else
		g_frontDialogCancelLabel[0] = '\0';
	FrontendScreen_RunModal(FrontendDialog_ConfirmUpdateCallback, &rect);
	FrontendText_ResetGlyphScratch();
	if (overlayTextEnabled != 0)
		FrontendButton_EnableOverlayText();
	return g_dialogResult;
#endif
}

// FUNCTION: XVT 0x4DCD30
int FrontendDialog_ConfirmUpdateCallback(int frameState) {
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
		OK_HOVER_SLOT = 20,
		CANCEL_HOVER_SLOT = 21,
		TEXT_COLOR = 0xFFFF,
		KEY_ENTER = 13,
		KEY_ESCAPE = 27,
		SAVE_OFFSCREEN_BACKUP = 1,
		GLYPH_SCRATCH_FRAMES = 20,
	};

	RECT rect;
	int finished;
	int pressed;

	finished = 0;
	if (frameState == 0) {
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
		FrontendText_ResetGlyphScratchBuffer(GLYPH_SCRATCH_FRAMES);
		return 0;
	}

	if (FrontendDialog_HasNetworkDismissPacket() != 0) {
		g_dialogResult = 0;
		finished = 1;
	}
	FrontendDraw_RectAssign(&rect, DIALOG_LEFT, DIALOG_TOP, DIALOG_RIGHT, DIALOG_BOTTOM);
	rect.bottom = rect.top + DIALOG_LINE_HEIGHT;
	FrontendText_DrawCentered(DIALOG_FONT_SIZE, g_frontDialogText0, &rect, TEXT_COLOR);
	FrontendDraw_RectOffsetXY(&rect, 0, DIALOG_LINE_HEIGHT);
	FrontendText_DrawCentered(DIALOG_FONT_SIZE, g_frontDialogText1, &rect, TEXT_COLOR);
	FrontendDraw_RectOffsetXY(&rect, 0, DIALOG_LINE_HEIGHT);
	FrontendText_DrawCentered(DIALOG_FONT_SIZE, g_frontDialogText2OrEdit, &rect, TEXT_COLOR);

	if (!g_frontDialogOkayLabel[0] && !g_frontDialogCancelLabel[0]) {
		FrontendDraw_RectAssign(&rect, OK_BUTTON_LEFT, OK_BUTTON_TOP, OK_BUTTON_RIGHT, BUTTON_BOTTOM);
		pressed = FrontendButton_DrawSpriteHitTest(&rect, "dialogok", "dialogokd",
												   FrontendString_Get(FRONTSTR_523_OKAY), BUTTON_FONT_SIZE,
												   TEXT_COLOR, OK_HOVER_SLOT, "buttonsound");
		if (Keyboard_DequeueChar() == KEY_ENTER)
			pressed = 1;
		if (pressed != 0) {
			finished = 1;
			g_dialogResult = 1;
#ifndef XVT_MODERN
			FrontendCursor_SetPos(g_frontDialogSavedMouseX, g_frontDialogSavedMouseY);
#endif
		}
	} else if (!g_frontDialogOkayLabel[0]) {
		FrontendDraw_RectAssign(&rect, CANCEL_BUTTON_LEFT, OK_BUTTON_TOP, CANCEL_BUTTON_RIGHT, BUTTON_BOTTOM);
		pressed =
			FrontendButton_DrawSpriteHitTest(&rect, "dialogcancel", "dialogcanceld", g_frontDialogCancelLabel,
											 BUTTON_FONT_SIZE, TEXT_COLOR, CANCEL_HOVER_SLOT, "buttonsound");
		if (Keyboard_DequeueChar() == KEY_ESCAPE)
			pressed = 1;
		if (pressed != 0) {
			g_dialogResult = 0;
			finished = 1;
		}
	} else if (!g_frontDialogCancelLabel[0]) {
		FrontendDraw_RectAssign(&rect, OK_BUTTON_LEFT, OK_BUTTON_TOP, OK_BUTTON_RIGHT, BUTTON_BOTTOM);
		pressed =
			FrontendButton_DrawSpriteHitTest(&rect, "dialogok", "dialogokd", g_frontDialogOkayLabel,
											 BUTTON_FONT_SIZE, TEXT_COLOR, OK_HOVER_SLOT, "buttonsound");
		if (Keyboard_DequeueChar() == KEY_ENTER)
			pressed = 1;
		if (pressed != 0) {
			finished = 1;
			g_dialogResult = 1;
		}
	} else {
		FrontendDraw_RectAssign(&rect, OK_BUTTON_LEFT, OK_BUTTON_TOP, OK_BUTTON_RIGHT, BUTTON_BOTTOM);
		pressed =
			FrontendButton_DrawSpriteHitTest(&rect, "dialogok", "dialogokd", g_frontDialogOkayLabel,
											 BUTTON_FONT_SIZE, TEXT_COLOR, OK_HOVER_SLOT, "buttonsound");
		if (Keyboard_PeekChar() == KEY_ENTER) {
			pressed = 1;
			Keyboard_DequeueChar();
		}
		if (pressed != 0) {
			finished = 1;
			g_dialogResult = 1;
		}
		FrontendDraw_RectAssign(&rect, CANCEL_BUTTON_LEFT, OK_BUTTON_TOP, CANCEL_BUTTON_RIGHT, BUTTON_BOTTOM);
		pressed =
			FrontendButton_DrawSpriteHitTest(&rect, "dialogcancel", "dialogcanceld", g_frontDialogCancelLabel,
											 BUTTON_FONT_SIZE, TEXT_COLOR, CANCEL_HOVER_SLOT, "buttonsound");
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

// FUNCTION: XVT 0x4DD0F0
int FrontendDialog_HasNetworkDismissPacket(void) {

	if (Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_PLAYER_ADMITTED) != 0)
		return 1;
	if (Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_HOST_CANCELLED) != 0)
		return 1;
	if (Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_PLAYER_LEFT) != 0)
		return 1;
	if (Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_TEAM_ASSIGNMENTS_READY) != 0)
		return 1;
	if (Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_FRONTEND_MISSION_START) != 0)
		return 1;
	if (Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_FRONTEND_OPCODE_74) != 0)
		return 1;
	if (Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_NEXT_TOURNAMENT_MISSION) != 0)
		return 1;
	if (Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_NEXT_BATTLE_MISSION) != 0)
		return 1;
	if (Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_REPLAY_CURRENT_MISSION) != 0)
		return 1;
	if (Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_RETURN_TO_SETUP) != 0)
		return 1;
	if (Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_REPLAY_MISSION) != 0)
		return 1;
	if (Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_PLAYER_KICKED) != 0)
		return 1;
	if (Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_SESSION_CANCELLED) != 0)
		return 1;
	if (Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS) != 0)
		return 1;
	return Net_HasQueuedPacketTypeOrBacklog(NET_PACKET_RETURN_TO_MISSION_SELECTION) != 0;
}

// FUNCTION: XVT 0x4DD220
int FrontendDialog_PromptForPilotName(char* outName) {
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

	RECT rect;
	int overlayTextEnabled;

	overlayTextEnabled = FrontendButton_IsOverlayTextEnabled();
	FrontendButton_DisableOverlayText();
	FrontendDraw_RectAssign(&rect, SCREEN_LEFT, SCREEN_TOP, SCREEN_RIGHT, SCREEN_BOTTOM);
	FrontendCursor_GetPos(&g_frontDialogSavedMouseX, &g_frontDialogSavedMouseY);
	memset(g_frontDialogText0, 0, sizeof(g_frontDialogText0));
	FrontendScreen_RunModal(FrontendDialog_CreatePilotNameCallback, &rect);
	FrontendText_ResetGlyphScratch();
	if (overlayTextEnabled != 0)
		FrontendButton_EnableOverlayText();
	memcpy(outName, g_frontDialogText0, PILOT_NAME_LENGTH);
	outName[PILOT_NAME_LENGTH] = '\0';
	return 1;
#endif
}

// FUNCTION: XVT 0x4DD2C0
int FrontendDialog_CreatePilotNameCallback(int frameState) {
	RECT rect;
	int accepted;

	if (frameState == 0) {
		FrontendCursor_SetPos(417, 291);
		Keyboard_FlushCharBuffer();
		FrontImage_RegisterResourceDefault("frontres\\create.bmp", "backname");
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
	FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_716_CREATE_A_NEW_PILOT), &rect, 0xFFFF);
	FrontendDraw_RectAssign(&rect, 250, 245, 440, 265);
	accepted = FrontendText_DrawEditableField(&rect, g_frontDialogText0, 13, 0, 12, "\\*$");
	FrontendDraw_RectAssign(&rect, 250, 275, 440, 295);
	accepted |= FrontendButton_HandleTextButton(&rect, FrontendString_Get(FRONTSTR_717_CREATE_PILOT), 15,
												0xFFFF, 20, "buttonsound");

	if (Keyboard_PeekChar() == 13) {
		Keyboard_DiscardChar();
		accepted = 1;
	} else if (Keyboard_PeekChar() == 27) {
		Keyboard_DiscardChar();
#ifdef XVT_MODERN
		g_frontDialogText0[0] = 0;
		FrontImage_FreeResourceByName("backname");
		return 1;
#endif
	}

	if (!accepted || g_frontDialogText0[0] == '\0') {
		return 0;
	}
	FrontImage_FreeResourceByName("backname");
	return 1;
}

// FUNCTION: XVT 0x4DD480
int FrontendDialog_ShowNetworkAbortError(const char* line1, const char* line2, const char* line3,
										 const char* okayLabel, const char* cancelLabel) {
#ifdef XVT_MODERN
	return XvtDialog_Confirm(line1, line2, line3, okayLabel, cancelLabel, 1);
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

	RECT rect;
	int overlayTextEnabled;

	overlayTextEnabled = FrontendButton_IsOverlayTextEnabled();
	FrontendButton_DisableOverlayText();
	if (g_gameConfig.sfxDatapadEnabled != 0) {
		FrontendSound_PlayUISound("warningsound", SOUND_ALLOW_RESTART, SOUND_NO_LOOP, SOUND_PRIORITY,
								  SOUND_VOLUME_SCALE * g_gameConfig.sfxDatapadVolume, SOUND_CENTER_PAN);
	}
	FrontendDraw_RectAssign(&rect, SCREEN_LEFT, SCREEN_TOP, SCREEN_RIGHT, SCREEN_BOTTOM);
	FrontendCursor_GetPos(&g_frontDialogSavedMouseX, &g_frontDialogSavedMouseY);
	if (line1 != NULL)
		strcpy(g_frontDialogText0, line1);
	else
		g_frontDialogText0[0] = '\0';
	if (line2 != NULL)
		strcpy(g_frontDialogText1, line2);
	else
		g_frontDialogText1[0] = '\0';
	if (line3 != NULL)
		strcpy(g_frontDialogText2OrEdit, line3);
	else
		g_frontDialogText2OrEdit[0] = '\0';
	if (okayLabel != NULL)
		strcpy(g_frontDialogOkayLabel, okayLabel);
	else
		g_frontDialogOkayLabel[0] = '\0';
	if (cancelLabel != NULL)
		strcpy(g_frontDialogCancelLabel, cancelLabel);
	else
		g_frontDialogCancelLabel[0] = '\0';
	FrontendScreen_RunModal(FrontendDialog_NetworkAbortErrorCallback, &rect);
	FrontendText_ResetGlyphScratch();
	if (overlayTextEnabled != 0)
		FrontendButton_EnableOverlayText();
	return g_dialogResult;
#endif
}

// FUNCTION: XVT 0x4DD620
int FrontendDialog_NetworkAbortErrorCallback(int frameState) {
	RECT rect;
	int finished;
	int pressed;

	finished = 0;
	if (frameState == 0) {
		Keyboard_FlushCharBuffer();
		if (g_frontDialogOkayLabel[0] != '\0' || g_frontDialogCancelLabel[0] == '\0') {
			FrontendCursor_SetPos(184, 255);
		} else {
			FrontendCursor_SetPos(496, 255);
		}
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteTranslucent("dialogbox", 0, 0);
		FrontImage_DrawSpriteTranslucent("dialogbox", 0, 0);
		FrontImage_DrawSpriteTranslucent("dialogbox", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_ResetGlyphScratchBuffer(20);
		return 0;
	}

	FrontendDraw_RectAssign(&rect, 166, 225, 518, 245);
	rect.bottom = rect.top + 20;
	FrontendText_DrawCentered(15, g_frontDialogText0, &rect, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 20);
	FrontendText_DrawCentered(15, g_frontDialogText1, &rect, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 20);
	FrontendText_DrawCentered(15, g_frontDialogText2OrEdit, &rect, 0xFFFF);

	if (g_frontDialogOkayLabel[0] == '\0') {
		if (g_frontDialogCancelLabel[0] == '\0') {
			FrontendDraw_RectAssign(&rect, 171, 240, 202, 275);
			pressed = FrontendButton_DrawSpriteHitTest(&rect, "dialogok", "dialogokd",
													   FrontendString_Get(FRONTSTR_523_OKAY), 12, 0xFFFF, 20,
													   "buttonsound");
			if (Keyboard_DequeueChar() == 13) {
				pressed = 1;
			}
			if (pressed != 0) {
				finished = 1;
				g_dialogResult = 1;
#ifndef XVT_MODERN
				FrontendCursor_SetPos(g_frontDialogSavedMouseX, g_frontDialogSavedMouseY);
#endif
			}
		} else {
			FrontendDraw_RectAssign(&rect, 481, 240, 513, 275);
			pressed =
				FrontendButton_DrawSpriteHitTest(&rect, "dialogcancel", "dialogcanceld",
												 g_frontDialogCancelLabel, 12, 0xFFFF, 21, "buttonsound");
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
			pressed = FrontendButton_DrawSpriteHitTest(&rect, "dialogok", "dialogokd", g_frontDialogOkayLabel,
													   12, 0xFFFF, 20, "buttonsound");
			if (Keyboard_DequeueChar() == 13) {
				pressed = 1;
			}
			if (pressed != 0) {
				finished = 1;
				g_dialogResult = 1;
			}
		} else {
			pressed = FrontendButton_DrawSpriteHitTest(&rect, "dialogok", "dialogokd", g_frontDialogOkayLabel,
													   12, 0xFFFF, 20, "buttonsound");
			if (Keyboard_PeekChar() == 13) {
				pressed = 1;
				Keyboard_DequeueChar();
			}
			if (pressed != 0) {
				finished = 1;
				g_dialogResult = 1;
			}
			FrontendDraw_RectAssign(&rect, 481, 240, 513, 275);
			pressed =
				FrontendButton_DrawSpriteHitTest(&rect, "dialogcancel", "dialogcanceld",
												 g_frontDialogCancelLabel, 12, 0xFFFF, 21, "buttonsound");
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
