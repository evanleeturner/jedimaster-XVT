#include "xvt_runtime/runtime/network_browser.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/network_metadata.h"
#include "xvt_runtime/runtime/network_task.h"
#include <stdio.h>

int XvtNetworkBrowser_DrawList(void) {
	const AeronDplayDirectorySnapshot* snapshot = XvtNetworkTask_Snapshot();
	int* scroll = XvtNetworkTask_ScrollOffset();
	int mouse_x, mouse_y, clicked = -1;
	RECT rect = { 88, 94, 416, 109 }, column;
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_016_GAME_NAME_PLAYERS_NEEDED_LAST_QUERY),
								   &rect, 0, 1, 0xffff);
	FrontendCursor_GetPos(&mouse_x, &mouse_y);
	if (snapshot->room_count > 6) {
		FrontendDraw_RectAssign(&rect, 421, 114, 430, 204);
		*scroll = FrontendScrollbar_Draw(&rect, *scroll, snapshot->room_count, 0, 5, g_colorNavy, 5);
		int maximum = (int)snapshot->room_count - 6;
		if (*scroll > maximum)
			*scroll = maximum;
		if (*scroll < 0)
			*scroll = 0;
	} else
		*scroll = 0;
	FrontendDraw_RectAssign(&rect, 88, 114, 419, 128);
	for (int i = *scroll; i < *scroll + 6 && (unsigned)i < snapshot->room_count; ++i) {
		const AeronDplayDirectoryRoom* room = &snapshot->rooms[i];
		int compatible = XvtNetworkTask_Compatible(room);
		int free_slots = room->metadata.max_players - room->metadata.players;
		int color = !compatible                ? g_colorGray
					: !room->metadata.joinable ? g_colorRed
					: !free_slots              ? g_colorLightBlue
											   : g_colorGreen2;
		if (i == XvtNetworkTask_SelectedIndex())
			FrontendDraw_Rect(&rect, 0, 0, g_colorNavy, 1);
		if (room->metadata.password_required)
			FrontImage_DrawSprite("key", rect.left - 6, rect.top + 1);
		char name[32];
		XvtNetworkMetadata_FromUtf8(name, sizeof(name), room->metadata.name);
		column = rect;
		column.right = 279;
		RECT clip;
		FrontendDisplay_GetScreenClipRect(&clip);
		FrontendDisplay_SetScreenClipRect640x480(&column);
		FrontendText_DrawAlignedInRect(12, name, &column, 0, 1, color);
		FrontendDisplay_SetScreenClipRect640x480(&clip);
		column = rect;
		column.left = 285;
		snprintf(g_frontendScratchBuffer, sizeof(g_frontendScratchBuffer), "%d", free_slots);
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &column, 0, 1, color);
		column.left = 374;
		Frontend_FormatSecondsToClockString(XvtNetworkTask_SnapshotAge());
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &column, 0, 1, color);
		if (FrontendDraw_PointInRect(&rect, mouse_x, mouse_y)) {
			FrontendDraw_RectOutline(&rect, 0, 0, g_colorGreen);
			if (FrontendMouse_GetLeftClick() || FrontendMouse_GetRightClick()) {
				if (g_gameConfig.sfxDatapadEnabled)
					FrontendSound_PlayUISound("jewelsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume,
											  63);
				clicked = i;
			}
		}
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
	}
	return clicked;
}

int XvtNetworkBrowser_DrawRoster(void) {
	RECT rect = { 88, 218, 430, 233 }, clip;
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_201_PLAYERS_IN_GAME), &rect, 0, 1, 0xffff);
	const AeronDplayDirectoryRoom* room = XvtNetworkTask_SelectedRoom();
	if (!room)
		return 1;
	FrontendDraw_RectAssign(&rect, 88, 238, 258, 252);
	for (unsigned i = 0; i < room->metadata.players; ++i) {
		char name[32];
		unsigned rating = room->metadata.roster[i].rating;
		if (rating > PILOT_RATING_JEDI_MASTER)
			rating = PILOT_RATING_TARGET_DRONE;
		XvtNetworkMetadata_FromUtf8(name, sizeof(name), room->metadata.roster[i].name);
		FrontendDisplay_GetScreenClipRect(&clip);
		FrontendDisplay_SetScreenClipRect640x480(&rect);
		snprintf(g_frontendScratchBuffer, sizeof(g_frontendScratchBuffer), "%c%s %c%s", 6,
				 FrontendString_Get(FRONTSTR_154_DRONE + rating), 4, name);
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xffff);
		FrontendDisplay_SetScreenClipRect640x480(&clip);
		if (i == 3)
			FrontendDraw_RectAssign(&rect, 259, 238, 430, 252);
		else
			FrontendDraw_RectOffsetXY(&rect, 0, 15);
	}
	return 1;
}

int XvtNetworkBrowser_DrawMission(void) {
	XvtNetworkPreview* preview = XvtNetworkTask_Preview();
	RECT rect = { 88, 309, 430, 324 };
	const char* label = FrontendString_Get(FRONTSTR_187_MISSION);
	if (preview->title[0]) {
		snprintf(g_frontendScratchBuffer, sizeof(g_frontendScratchBuffer), "%s %c%s", label, 4,
				 preview->title);
		label = g_frontendScratchBuffer;
	}
	FrontendText_DrawAlignedInRect(12, label, &rect, 0, 1, 0xffff);
	if (!XvtNetworkTask_SelectedRoom())
		return 1;
	FrontendDraw_RectAssign(&rect, 88, 328, 420, 426);
	unsigned lines = FrontendText_DrawWrapped(12, preview->text, &rect, 0xffff, 4, 4096) + 1;
	if (lines > 6) {
		FrontendDraw_RectAssign(&rect, 421, 328, 430, 426);
		preview->scroll = FrontendScrollbar_Draw(&rect, preview->scroll, lines, 0, 5, g_colorNavy, 6);
		FrontendDraw_RectAssign(&rect, 88, 328, 420, 426);
	} else {
		preview->scroll = 0;
		FrontendDraw_RectAssign(&rect, 88, 328, 430, 426);
	}
	FrontendText_DrawWrapped(12, preview->text, &rect, 0xffff, 4, preview->scroll);
	return 1;
}

static int XvtNetworkBrowser_AfterError(int result, int context) {
	(void)result;
	(void)context;
	return 0;
}

int XvtNetworkBrowser_Screen(int first_frame) {
	RECT rect;
	if (!first_frame) {
		FrontendCursor_SetPos(415, 121);
		g_skipFrontendEntryMovie = 0;
		g_unusedFrontendConcourseHostLatch = 0;
		g_frontendSinglePlayerFlightSessionActive = 0;
		FrontImage_RegisterResourceDefault("frontres\\joinback.bmp", "background");
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("background", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		FrontImage_DrawSprite(g_hostCdAvailable ? "allactive" : "clientactive", 0, 0);
		FrontImage_DrawSpriteTranslucent("chatbox", 0, 0);
		FrontImage_DrawSpriteTranslucent("joinoverlay", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_ResetGlyphScratchBuffer(20);
		XvtNetworkTask_OpenBrowser();
		if (XvtNetworkTask_BrowserError() == AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED) {
			XvtDialog_Confirm("The multiplayer directory is not configured.", "", "",
							  FrontendString_Get(FRONTSTR_523_OKAY), NULL, 0);
			return XvtDialog_ContinueWith(XvtNetworkBrowser_AfterError, 0);
		}
	}
	const AeronDplayDirectoryRoom* selected = XvtNetworkTask_SelectedRoom();
	char name[32] = { 0 };
	if (selected)
		XvtNetworkMetadata_FromUtf8(name, sizeof(name), selected->metadata.name);
	FrontendDraw_RectAssign(&rect, 158, 52, 491, 68);
	FrontendText_DrawCentered(12, name, &rect, 0xffff);
	int clicked = FrontendNet_DrawJoinGameList(first_frame);
	if (clicked >= 0)
		XvtNetworkTask_Select(clicked);
	FrontendNet_DrawJoinGamePlayerRoster();
	FrontendNet_DrawJoinGameMissionBriefing();
	FrontendDraw_RectAssign(&rect, 461, 117, 595, 398);
	const AeronDplayDirectorySnapshot* snapshot = XvtNetworkTask_Snapshot();
	const char* status = XvtNetworkTask_BrowserError() ? "Directory unavailable. Press Refresh to try again."
						 : snapshot->refresh.state == AERON_DPLAY_DIRECTORY_PENDING ? "Refreshing games..."
						 : !snapshot->room_count                                    ? "No games found."
																					: "";
	FrontendText_DrawWrapped(12, status, &rect, 0xffff, 2, 0);
	FrontendDraw_RectAssign(&rect, 507, 452, 562, 464);
	FrontendText_DrawCentered(12, "v. 2.0", &rect, 0xffff);
	FrontendDraw_RectAssign(&rect, 200, 452, 436, 464);
	if (g_pilotData.name[0]) {
		snprintf(g_frontendScratchBuffer, sizeof(g_frontendScratchBuffer), "%c%s %c%s", 6,
				 g_pilotData.ratingName, 1, g_pilotData.name);
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, g_colorLightBlue);
		snprintf(g_frontendScratchBuffer, sizeof(g_frontendScratchBuffer), "rebtiny%d",
				 (first_frame % 32) >> 1);
		FrontImage_DrawSprite(g_frontendScratchBuffer, 204, 453);
		snprintf(g_frontendScratchBuffer, sizeof(g_frontendScratchBuffer), "imptiny%d",
				 (first_frame % 32) >> 1);
		FrontImage_DrawSprite(g_frontendScratchBuffer, 420, 453);
	}
	FrontendNet_DrawJoinGameSidebarsAndQueryAll();
	if (Frontend_HandleCommonScreenControls(0))
		return 1;
	if (XvtDialog_IsActive())
		return 0;
	if (g_gameConfig.helpOn)
		FrontendButton_EnableOverlayText();
	FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_569_PREVIOUS));
	FrontendDraw_RectAssign(&rect, 85, 447, 176, 471);
	if (FrontendButton_DrawSpriteHitTest(&rect, "leaveup", "leavedown",
										 FrontendString_Get(FRONTSTR_258_RETURN_TO_PILOT_RECORDS), 12, 0, 8,
										 "buttonsound")) {
		g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
		FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
	} else if (XvtNetworkTask_CanJoin()) {
		FrontendDraw_RectAssign(&rect, 8, 405, 71, 470);
		FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_018_JOIN));
		if (FrontendButton_DrawSpriteHitTest(&rect, "nextup", "nextdown",
											 FrontendString_Get(FRONTSTR_018_JOIN), 12, 0, 7, "flysound"))
			XvtNetworkTask_Begin(XVT_NETWORK_CONNECT);
	}
	FrontendButton_DisableOverlayText();
	return 0;
}
