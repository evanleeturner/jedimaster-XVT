#include "xvt/net/frontend_net.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/network_browser.h"
#include "xvt_runtime/runtime/network_dialogs.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/network_task.h"
#endif

#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/movie.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GLOBAL: XVT 0x518278
const unsigned int g_frontendNetXvtDirectPlayAppGuid[4] = { 0x09438C20, 0x11CEE06A, 0xAA008186, 0x575D6C00 };
// GLOBAL: XVT 0xAA6AE8
int g_frontendNetReceivedMissionDescriptionId = 0;
// GLOBAL: XVT 0xAA6CF8
int g_frontendNetReceivedMissionDirectoryId = 0;
// GLOBAL: XVT 0xAA6AF0
FrontendNetPacketScratch g_frontendNetPacketScratch = { 0 };
// GLOBAL: XVT 0xAA62B0
FrontendNetSessionEntry g_frontendNetSessionList[32] = { { 0 } };
// GLOBAL: XVT 0xAA6CF4
int g_frontendNetSessionCount = 0;
// GLOBAL: XVT 0x665438
int g_frontendNetSessionListScrollOffset = 0;
// GLOBAL: XVT 0x52BF50
int g_frontendNetSelectedSessionIdx = -1;
// GLOBAL: XVT 0x52C188
int g_frontendNetPacketSenderPlayerId = 0;
// GLOBAL: XVT 0x52C18C
int g_frontendNetPacketArg0 = 0;
// GLOBAL: XVT 0x52C190
int g_frontendNetPacketArg1 = 0;
// GLOBAL: XVT 0x665D0C
int g_hostGameStartPending = 0;
// GLOBAL: XVT 0x52C204
int g_frontendQuickStartLaunchFlag = 0;
// GLOBAL: XVT 0xAA62A4
int g_frontendNetProbeVersion = 0;
// GLOBAL: XVT 0xAA6A70
int g_frontendNetProbePlayersNeeded = 0;
// GLOBAL: XVT 0xAA6A74
int g_frontendNetProbePasswordRequired = 0;
// GLOBAL: XVT 0xAA6CF0
int g_frontendNetProbeResponseType = 0;
// GLOBAL: XVT 0xA91C90
int g_frontendMissionOpcode99Count = 0;
// GLOBAL: XVT 0xAA6A80
char g_frontendChatInputBuffer[100] = { 0 };
// GLOBAL: XVT 0xB69CD0
char* g_frontendChatLogBuffer = NULL;
// GLOBAL: XVT 0xB6A2C4
int g_frontendChatLogUsedBytes = 0;
// GLOBAL: XVT 0x52BF54
int g_frontendChatTeamOnly = 0;
// GLOBAL: XVT 0x66543C
int g_frontendChatScrollOffset = 0;
// GLOBAL: XVT 0x665440
char g_frontendNetSelectedGameName[32] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

// FUNCTION: XVT 0x4D7020
int FrontendNet_DrawJoinGameList(int resetScroll) {
#ifdef XVT_MODERN
	(void)resetScroll;
	return XvtNetworkBrowser_DrawList();
#else
	RECT rect;
	RECT destination;
	uint32_t tickCount;
	int mouseX;
	int mouseY;
	int clickedIndex;
	int rowIndex;

	if (resetScroll == 0)
		g_frontendNetSessionListScrollOffset = 0;
	tickCount = GetTickCount();
	FrontendDraw_RectAssign(&rect, 88, 94, 416, 109);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_016_GAME_NAME_PLAYERS_NEEDED_LAST_QUERY),
								   &rect, 0, 1, 0xFFFF);
	FrontendCursor_GetPos(&mouseX, &mouseY);
	if (g_frontendNetSessionCount > 6) {
		FrontendDraw_RectAssign(&rect, 421, 114, 430, 204);
		g_frontendNetSessionListScrollOffset =
			FrontendScrollbar_Draw(&rect, g_frontendNetSessionListScrollOffset, g_frontendNetSessionCount, 0,
								   5, (unsigned int)g_colorNavy, 5);
	}
	FrontendDraw_RectAssign(&rect, 88, 114, 419, 128);
	clickedIndex = -1;
	rowIndex = g_frontendNetSessionListScrollOffset;
	while (rowIndex < g_frontendNetSessionListScrollOffset + 6 && rowIndex < g_frontendNetSessionCount) {
		unsigned short textColor;

		if (rowIndex == g_frontendNetSelectedSessionIdx)
			FrontendDraw_Rect(&rect, 0, 0, g_colorNavy, 1);
		if (g_frontendNetSessionList[rowIndex].version != 101u) {
			textColor = g_colorGray;
		} else if (g_frontendNetSessionList[rowIndex].playersNeeded == 0) {
			textColor = g_frontendNetSessionList[rowIndex].queryState != 0 ? g_colorRed : g_colorLightBlue;
		} else {
			textColor = 0xFFFF;
			if (g_frontendNetSessionList[rowIndex].playersNeeded <= 8)
				textColor = g_colorGreen2;
		}
		if (g_frontendNetSessionList[rowIndex].passwordRequired != 0)
			FrontImage_DrawSprite("key", rect.left - 6, rect.top + 1);
		if (g_frontendNetSessionList[rowIndex].version != 101u) {
			if (g_frontendNetSessionList[rowIndex].version == 0) {
				sprintf(g_frontendScratchBuffer, "%s v. ???", g_frontendNetSessionList[rowIndex].gameName);
			} else {
				sprintf(g_frontendScratchBuffer, "%s v. %d.%d", g_frontendNetSessionList[rowIndex].gameName,
						(g_frontendNetSessionList[rowIndex].version - 1) / 100 + 1,
						(g_frontendNetSessionList[rowIndex].version - 1) % 100);
			}
			FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, textColor);
		} else {
			FrontendText_DrawAlignedInRect(12, g_frontendNetSessionList[rowIndex].gameName, &rect, 0, 1,
										   textColor);
		}
		if (g_frontendNetSessionList[rowIndex].playersNeeded <= 8) {
			FrontendDraw_RectCopy(&destination, &rect);
			destination.left = 285;
			sprintf(g_frontendScratchBuffer, "%d", g_frontendNetSessionList[rowIndex].playersNeeded);
			FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &destination, 0, 1, textColor);
			destination.left = 374;
			Frontend_FormatSecondsToClockString(
				(tickCount - g_frontendNetSessionList[rowIndex].lastQueryTick) / 1000);
			FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &destination, 0, 1, textColor);
		}
		if (FrontendDraw_PointInRect(&rect, mouseX, mouseY)) {
			FrontendDraw_RectOutline(&rect, 0, 0, g_colorGreen);
			if (FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0) {
				if (g_gameConfig.sfxDatapadEnabled != 0)
					FrontendSound_PlayUISound("jewelsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume,
											  63);
				clickedIndex = rowIndex;
			}
		}
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		++rowIndex;
	}
	return clickedIndex;
#endif
}

// FUNCTION: XVT 0x4D73C0
int FrontendNet_JoinGameScreen(int firstFrame) {
#ifdef XVT_MODERN
	return XvtNetworkBrowser_Screen(firstFrame);
#else
	enum {
		SESSION_REFRESH_INTERVAL_FRAMES = 160,
		BRIEFING_TEXT_CAPACITY = 4096,
		JOIN_REQUEST_PACKET_SIZE = 6 * sizeof(int),
		PILOT_BANNER_ANIMATION_FRAMES = 32,
	};

	int packetType;
	int clickedSessionIndex;
	int animationFrame;
	int hostPlayerId;
	RECT rect;
	RECT screenRect;

	g_unusedFrontendConcourseHostLatch = 0;
	if (g_gameConfig.networkType != NET_TRANSPORT_IPX) {
		if (g_skipFrontendEntryMovie != 0) {
			g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
			FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
			return 0;
		}
		if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_NET_CLIENT) {
			switch (g_gameConfig.networkType) {
				case NET_TRANSPORT_TCPIP:
					if (g_gameConfig.ipAddress[0] == '\0') {

						if (FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(FRONTSTR_693_YOU_HAVE_NOT_ENTERED_AN_IP_ADDRESS_FOR),
								FrontendString_Get(FRONTSTR_694_THE_TCP_IP_CONNECTION_PLEASE_ENTER_ONE),
								FrontendString_Get(FRONTSTR_695_IN_THE_CONFIGURATION_SCREEN),
								FrontendString_Get(FRONTSTR_523_OKAY),
								FrontendString_Get(FRONTSTR_019_CANCEL)) == 0) {
							g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
							FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
							return 0;
						}

						FrontendDraw_RectAssign(&screenRect, 0, 0, 640, 480);
						FrontendScreen_QueuePush(Config_OptionsDatapadUpdate, &screenRect);
						return 0;
					}
					break;
				case NET_TRANSPORT_MODEM:
					if (g_gameConfig.phoneNumber[0] == '\0') {

						if (FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(FRONTSTR_696_YOU_HAVE_NOT_ENTERED_A_PHONE_NUMBER_FOR),
								FrontendString_Get(FRONTSTR_697_THE_MODEM_CONNECTION_PLEASE_ENTER_ONE),
								FrontendString_Get(FRONTSTR_698_IN_THE_CONFIGURATION_SCREEN),
								FrontendString_Get(FRONTSTR_523_OKAY),
								FrontendString_Get(FRONTSTR_019_CANCEL)) == 0) {
							g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
							FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
							return 0;
						}

						FrontendDraw_RectAssign(&screenRect, 0, 0, 640, 480);
						FrontendScreen_QueuePush(Config_OptionsDatapadUpdate, &screenRect);
						return 0;
					}
					break;
			}
		}
		return FrontendNet_ConnectToSelectedGameScreen(firstFrame);
	} else {
		if (firstFrame == 0) {
			FrontendCursor_SetPos(415, 121);
			memset(g_mpRoster, 0, sizeof(g_mpRoster));
			Net_ClearPlayerReadyFlags();
			memset(g_frontendNetSelectedGameName, 0, sizeof(g_frontendNetSelectedGameName));
			if (g_frontendChatLogBuffer != NULL) {
				memset(g_frontendChatLogBuffer, 0, 1024);
				g_frontendChatLogUsedBytes = 0;
			}
			if (g_skipFrontendEntryMovie == 0) {
				memset(g_frontendNetSessionList, 0, sizeof(g_frontendNetSessionList));
				g_frontendNetSessionCount = 0;
			}
			g_frontendFirstVisibleLine = 0;
			g_unusedFrontendConcourseHostLatch = 0;
			g_frontendSinglePlayerFlightSessionActive = 0;
			g_frontendNetReceivedMissionDirectoryId = MISSION_DIRECTORY_TRAINING_EXERCISES;
			g_frontendNetReceivedMissionDescriptionId = -1;
			g_frontendNetSelectedSessionIdx = -1;
			g_missionSetupRosterAuthoritative = 0;
			g_skipFrontendEntryMovie = 0;
			if (g_briefingText == NULL) {
				g_briefingText = malloc(BRIEFING_TEXT_CAPACITY);
			}
			FrontImage_RegisterResourceDefault("frontres\\joinback.bmp", "background");
			FrontendDisplay_LockOffscreenSurface();
			FrontImage_DrawSpriteOpaque("background", 0, 0);
			FrontImage_DrawSprite("frame", 0, 0);
			if (g_hostCdAvailable != 0) {
				FrontImage_DrawSprite("allactive", 0, 0);
			} else {
				FrontImage_DrawSprite("clientactive", 0, 0);
			}
			FrontImage_DrawSpriteTranslucent("chatbox", 0, 0);
			FrontImage_DrawSpriteTranslucent("joinoverlay", 0, 0);
			FrontendDisplay_UnlockOffscreenSurface(1);
			FrontendText_ResetGlyphScratchBuffer(20);
		}

		FrontendDraw_RectAssign(&rect, 158, 52, 491, 68);
		FrontendText_DrawCentered(12, g_frontendNetSelectedGameName, &rect, 0xFFFF);
		if (firstFrame % SESSION_REFRESH_INTERVAL_FRAMES == 0 && g_frontendNetSelectedSessionIdx == -1) {
			FrontendNet_RefreshSessionList();
		}
		if (g_frontendNetSelectedSessionIdx != -1) {
			packetType = FrontendNet_ProcessNetworkPackets();
			if (packetType == NET_PACKET_STATE) {
				g_frontendNetSessionList[g_frontendNetSelectedSessionIdx].lastQueryTick = GetTickCount();
				g_frontendNetSessionList[g_frontendNetSelectedSessionIdx].playersNeeded =
					g_frontendNetProbePlayersNeeded;
				g_frontendNetSessionList[g_frontendNetSelectedSessionIdx].passwordRequired =
					(uint8_t)g_frontendNetProbePasswordRequired;
				MpRoster_CompactActiveEntries();
				g_pilotData.missionDirectoryId = g_frontendNetReceivedMissionDirectoryId;
				g_pilotData.missionDescriptionIds[g_frontendNetReceivedMissionDirectoryId] =
					g_frontendNetReceivedMissionDescriptionId;
				MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
				MissionSetup_LoadMissionDescText(g_briefingText);
				if (g_missionList != NULL) {
					for (g_selectedMissionListIndex = 0;
						 (unsigned int)g_selectedMissionListIndex < g_missionCount;
						 ++g_selectedMissionListIndex) {
						if (g_missionList[g_selectedMissionListIndex].missionIdx ==
							g_pilotData.missionDescriptionIds[g_pilotData.missionDirectoryId]) {
							break;
						}
					}
				}
				g_frontendFirstVisibleLine = 0;
			} else if (packetType == NET_PACKET_HOST_CANCELLED ||
					   packetType == NET_PACKET_FRONTEND_MISSION_START ||
					   packetType == NET_PACKET_NEXT_TOURNAMENT_MISSION ||
					   packetType == NET_PACKET_NEXT_BATTLE_MISSION ||
					   packetType == NET_PACKET_REPLAY_MISSION ||
					   packetType == NET_PACKET_RETURN_TO_MISSION_SELECTION ||
					   packetType == NET_PACKET_REPLAY_CURRENT_MISSION) {
				Net_ShutdownDirectPlaySession();
				FrontendNet_RefreshSessionList();
				g_frontendNetProbeResponseType = 0;
				g_frontendNetSelectedSessionIdx = -1;
				g_frontendNetReceivedMissionDescriptionId = -1;
				memset(g_frontendNetSelectedGameName, 0, sizeof(g_frontendNetSelectedGameName));
				memset(g_briefingText, 0, BRIEFING_TEXT_CAPACITY);
			} else if (packetType == NET_PACKET_PLAYER_UNAVAILABLE) {
				Net_ShutdownDirectPlaySession();
				g_frontendNetProbeResponseType = 0;
				g_frontendNetSelectedSessionIdx = -1;
				g_frontendNetReceivedMissionDescriptionId = -1;
				FrontendNet_RefreshSessionList();
				memset(g_frontendNetSelectedGameName, 0, sizeof(g_frontendNetSelectedGameName));
				memset(g_briefingText, 0, BRIEFING_TEXT_CAPACITY);
			} else if (packetType == NET_PACKET_GAME_FULL || packetType == NET_PACKET_VERSION_MISMATCH ||
					   packetType == NET_PACKET_PASSWORD_REQUIRED || packetType == NET_PACKET_ROSTER_LOCKED) {
				Net_ShutdownDirectPlaySession();
				switch (packetType - NET_PACKET_GAME_FULL) {
					case 0:
						FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(FRONTSTR_543_THE_GAME_YOU_ARE_TRYING_TO_JOIN_IS_FULL),
							FrontendString_Get(FRONTSTR_544_PLEASE_TRY_ANOTHER_GAME),
							FrontendString_Get(FRONTSTR_545_SPACE_TRANSLATION_PLACEHOLDER), NULL, NULL);
						break;
					case NET_PACKET_VERSION_MISMATCH - NET_PACKET_GAME_FULL:
						FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(FRONTSTR_546_YOUR_VERSION_OF_THE_PROGRAM_DOES_NOT),
							FrontendString_Get(FRONTSTR_547_MATCH_THE_HOSTS_PLEASE_VERIFY_THAT_YOU),
							FrontendString_Get(FRONTSTR_548_HAVE_THE_CORRECT_PROGRAM), NULL, NULL);
						break;
					case NET_PACKET_PASSWORD_REQUIRED - NET_PACKET_GAME_FULL:
						FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(FRONTSTR_549_THIS_GAME_REQUIRES_A_PASSWORD),
							FrontendString_Get(FRONTSTR_550_PLEASE_ENTER_THE_CORRECT_PASSWORD_IN),
							FrontendString_Get(FRONTSTR_551_THE_NETWORK_CONFIGURATION_SCREEN), NULL, NULL);
						FrontendDraw_RectAssign(&screenRect, 0, 0, 640, 480);
						FrontendScreen_QueuePush(Config_OptionsDatapadUpdate, &screenRect);
						break;
					case NET_PACKET_ROSTER_LOCKED - NET_PACKET_GAME_FULL:
						FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(FRONTSTR_552_THIS_GAME_HAS_ALREADY_STARTED),
							FrontendString_Get(FRONTSTR_553_PLEASE_SELECT_ANOTHER_GAME_TO_JOIN),
							FrontendString_Get(FRONTSTR_554_SPACE_TRANSLATION_PLACEHOLDER), NULL, NULL);
						break;
				}
				g_frontendNetProbeResponseType = 0;
				g_frontendNetSelectedSessionIdx = -1;
				g_frontendNetReceivedMissionDescriptionId = -1;
				FrontendNet_RefreshSessionList();
				memset(g_frontendNetSelectedGameName, 0, sizeof(g_frontendNetSelectedGameName));
				memset(g_briefingText, 0, BRIEFING_TEXT_CAPACITY);
			}
		}

		clickedSessionIndex = FrontendNet_DrawJoinGameList(firstFrame);
		if (clickedSessionIndex != -1) {
			if (g_frontendNetSelectedSessionIdx == clickedSessionIndex) {
				g_frontendNetSelectedSessionIdx = -1;
				g_frontendNetReceivedMissionDescriptionId = -1;
				g_frontendNetProbeResponseType = 0;
				memset(g_frontendNetSelectedGameName, 0, sizeof(g_frontendNetSelectedGameName));
				memset(g_briefingText, 0, BRIEFING_TEXT_CAPACITY);
				Net_ShutdownDirectPlaySession();
			} else {
				g_frontendNetSelectedSessionIdx = clickedSessionIndex;
				if (FrontendNet_ProbeSessionByIndex(clickedSessionIndex) == 0) {
					Net_ShutdownDirectPlaySession();
					g_frontendNetProbeResponseType = 0;
					g_frontendNetSelectedSessionIdx = -1;
					g_frontendNetReceivedMissionDescriptionId = -1;
					FrontendNet_RefreshSessionList();
					memset(g_frontendNetSelectedGameName, 0, sizeof(g_frontendNetSelectedGameName));
					memset(g_briefingText, 0, BRIEFING_TEXT_CAPACITY);
				} else {
					FrontendNet_SortSessions();
					if (g_frontendNetSessionList[g_frontendNetSelectedSessionIdx].playersNeeded > 0) {
						FrontendMouse_ClearClicks();
						FrontendCursor_SetPos(37, 445);
					}
				}
			}
		}
		FrontendNet_DrawJoinGamePlayerRoster();
		FrontendNet_DrawJoinGameMissionBriefing();
		if (g_frontendNetSelectedSessionIdx != -1) {
			FrontendNet_UpdateAndDrawPanel(firstFrame);
		}
		FrontendDraw_RectAssign(&rect, 507, 452, 562, 464);
		sprintf(g_frontendScratchBuffer, "v. %d.%d", 2, 0);
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, 0xFFFF);
		FrontendDraw_RectAssign(&rect, 200, 452, 436, 464);
		if (g_pilotData.name[0] != '\0') {
			sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6, g_pilotData.ratingName, 1, g_pilotData.name);
			FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, g_colorLightBlue);
			animationFrame = (firstFrame % PILOT_BANNER_ANIMATION_FRAMES) >> 1;
			sprintf(g_frontendScratchBuffer, "rebtiny%d", animationFrame);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 204, 453);
			sprintf(g_frontendScratchBuffer, "imptiny%d", animationFrame);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 420, 453);
		}
		FrontendNet_DrawJoinGameSidebarsAndQueryAll();
		if (Frontend_HandleCommonScreenControls(0) == 1) {
			return 1;
		}
		FrontendDraw_RectAssign(&rect, 85, 447, 176, 471);
		if (g_gameConfig.helpOn != 0) {
			FrontendButton_EnableOverlayText();
		}
		FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_569_PREVIOUS));
		if (FrontendButton_DrawSpriteHitTest(&rect, "leaveup", "leavedown",
											 FrontendString_Get(FRONTSTR_258_RETURN_TO_PILOT_RECORDS), 12, 0,
											 8, "buttonsound") != 0) {
			g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
			Net_ShutdownDirectPlaySession();
			FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
		}
		FrontendDraw_RectAssign(&rect, 8, 405, 71, 470);
		if (g_frontendNetSelectedSessionIdx != -1 &&
			g_frontendNetSessionList[g_frontendNetSelectedSessionIdx].playersNeeded > 0 &&
			g_frontendNetSessionList[g_frontendNetSelectedSessionIdx].version ==
				FRONTEND_NET_PROTOCOL_VERSION) {
			FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_018_JOIN));
			if (FrontendButton_DrawSpriteHitTest(&rect, "nextup", "nextdown",
												 FrontendString_Get(FRONTSTR_018_JOIN), 12, 0, 7,
												 "flysound") != 0) {
				g_missionSetupIsHost = 0;
				strcpy(g_pilotData.multiplayerGameName,
					   g_frontendNetSessionList[g_frontendNetSelectedSessionIdx].gameName);
				hostPlayerId = Net_GetHostPlayerId();
				g_frontendNetPacketScratch.packetType = NET_PACKET_JOIN_REQUEST;
				*(int*)&g_frontendNetPacketScratch.payload[0] = FRONTEND_NET_PROTOCOL_VERSION;
				memcpy(&g_frontendNetPacketScratch.payload[4], g_gameConfig.password,
					   sizeof(g_gameConfig.password));
				Net_SendPacketAndFlush(hostPlayerId, &g_frontendNetPacketScratch, JOIN_REQUEST_PACKET_SIZE);
				FrontendScreen_SetCallbacks(FrontendNet_AccessAllianceNetworkScreen, NULL);
			}
		}
		FrontendButton_DisableOverlayText();
		return 0;
	}

	return 0;
#endif
}

// FUNCTION: XVT 0x4D7E70
int FrontendNet_AccessAllianceNetworkScreen(int firstFrame) {
	enum {
		ACCESS_TIMEOUT_FRAME = 480,
		PILOT_BANNER_ANIMATION_FRAMES = 32,
	};

	int networkResult;
	int animationFrame;
	int cancelPressed;
	RECT rect;
	RECT screenRect;

	if (firstFrame == 0) {
		FrontImage_RegisterResourceDefault("frontres\\joinback.bmp", "background");
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("background", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		if (g_hostCdAvailable != 0) {
			FrontImage_DrawSprite("allactive", 0, 0);
		} else {
			FrontImage_DrawSprite("clientactive", 0, 0);
		}
		FrontImage_DrawSpriteTranslucent("chatbox", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_ResetGlyphScratchBuffer(20);
	}

	FrontendDraw_RectAssign(&rect, 84, 107, 434, 433);
	FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_020_ACCESSING_IMPERIAL_NETWORK), &rect, 0xFFFF);
#ifdef XVT_MODERN
	if (XvtNetworkDialogs_AdmissionFailed())
		return 0;
#endif
	networkResult = FrontendNet_ProcessNetworkPackets();
#ifdef XVT_MODERN
	if (g_frontendNetPacketSenderPlayerId != Net_GetHostPlayerId())
		networkResult = 0;
#endif
	if (networkResult == NET_PACKET_GAME_FULL || networkResult == NET_PACKET_VERSION_MISMATCH ||
		networkResult == NET_PACKET_PASSWORD_REQUIRED || networkResult == NET_PACKET_ROSTER_LOCKED ||
		networkResult == NET_PACKET_HOST_CANCELLED || networkResult == NET_PACKET_FRONTEND_GAME_STARTED) {
#ifdef XVT_MODERN
		XvtNetworkSession_Reject();
#else
		Net_ShutdownDirectPlaySessionNoJoinAbort();
#endif
		switch (networkResult - NET_PACKET_FRONTEND_GAME_STARTED) {
			case NET_PACKET_FRONTEND_GAME_STARTED - NET_PACKET_FRONTEND_GAME_STARTED:
			case NET_PACKET_ROSTER_LOCKED - NET_PACKET_FRONTEND_GAME_STARTED:
				FrontendDialog_ShowConfirmDialog(
					FrontendString_Get(FRONTSTR_552_THIS_GAME_HAS_ALREADY_STARTED),
					FrontendString_Get(FRONTSTR_553_PLEASE_SELECT_ANOTHER_GAME_TO_JOIN),
					FrontendString_Get(FRONTSTR_554_SPACE_TRANSLATION_PLACEHOLDER), NULL, NULL);
#ifdef XVT_MODERN
				return XvtDialog_ContinueWith(XvtNetworkDialogs_Resume, XVT_NETWORK_ACCESS_REJECTED);
#endif
				break;
			case NET_PACKET_GAME_FULL - NET_PACKET_FRONTEND_GAME_STARTED:
				FrontendDialog_ShowConfirmDialog(
					FrontendString_Get(FRONTSTR_543_THE_GAME_YOU_ARE_TRYING_TO_JOIN_IS_FULL),
					FrontendString_Get(FRONTSTR_544_PLEASE_TRY_ANOTHER_GAME),
					FrontendString_Get(FRONTSTR_545_SPACE_TRANSLATION_PLACEHOLDER), NULL, NULL);
#ifdef XVT_MODERN
				return XvtDialog_ContinueWith(XvtNetworkDialogs_Resume, XVT_NETWORK_ACCESS_REJECTED);
#endif
				break;
			case NET_PACKET_HOST_CANCELLED - NET_PACKET_FRONTEND_GAME_STARTED:
				FrontendDialog_ShowConfirmDialog(FrontendString_Get(FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
												 FrontendString_Get(FRONTSTR_632_CANCELLED_BY_THE_HOST),
												 FrontendString_Get(FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
												 NULL, NULL);
#ifdef XVT_MODERN
				return XvtDialog_ContinueWith(XvtNetworkDialogs_Resume, XVT_NETWORK_ACCESS_REJECTED);
#endif
				break;
			case NET_PACKET_VERSION_MISMATCH - NET_PACKET_FRONTEND_GAME_STARTED:
				FrontendDialog_ShowConfirmDialog(
					FrontendString_Get(FRONTSTR_546_YOUR_VERSION_OF_THE_PROGRAM_DOES_NOT),
					FrontendString_Get(FRONTSTR_547_MATCH_THE_HOSTS_PLEASE_VERIFY_THAT_YOU),
					FrontendString_Get(FRONTSTR_548_HAVE_THE_CORRECT_PROGRAM), NULL, NULL);
#ifdef XVT_MODERN
				return XvtDialog_ContinueWith(XvtNetworkDialogs_Resume, XVT_NETWORK_ACCESS_REJECTED);
#endif
				break;
			case NET_PACKET_PASSWORD_REQUIRED - NET_PACKET_FRONTEND_GAME_STARTED:
				FrontendDialog_ShowConfirmDialog(
					FrontendString_Get(FRONTSTR_549_THIS_GAME_REQUIRES_A_PASSWORD),
					FrontendString_Get(FRONTSTR_550_PLEASE_ENTER_THE_CORRECT_PASSWORD_IN),
					FrontendString_Get(FRONTSTR_551_THE_NETWORK_CONFIGURATION_SCREEN), NULL, NULL);
#ifdef XVT_MODERN
				return XvtDialog_ContinueWith(XvtNetworkDialogs_Resume, XVT_NETWORK_ACCESS_PASSWORD);
#endif
				FrontendDraw_RectAssign(&screenRect, 0, 0, 640, 480);
				FrontendScreen_QueuePush(Config_OptionsDatapadUpdate, &screenRect);
				break;
		}
		if (g_gameConfig.networkType != 0) {
			FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
		} else {
			g_skipFrontendEntryMovie = 1;
			FrontendScreen_SetCallbacks(FrontendNet_JoinGameScreen, FrontendMissionList_FreeScreenResources);
		}
	} else if (networkResult == NET_PACKET_PLAYER_ADMITTED) {
		memset(g_mpRoster, 0, sizeof(g_mpRoster));
		FrontendScreen_SetCallbacks(MissionSetup_Update, MissionSetup_Exit);
	}

	FrontendDraw_RectAssign(&rect, 507, 452, 562, 464);
	sprintf(g_frontendScratchBuffer, "v. %d.%d", 2, 0);
	FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, 0xFFFF);
	FrontendDraw_RectAssign(&rect, 200, 452, 436, 464);
	if (g_pilotData.name[0] != '\0') {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6, g_pilotData.ratingName, 1, g_pilotData.name);
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, g_colorLightBlue);
		animationFrame = (firstFrame % PILOT_BANNER_ANIMATION_FRAMES) >> 1;
		sprintf(g_frontendScratchBuffer, "rebtiny%d", animationFrame);
		FrontImage_DrawSprite(g_frontendScratchBuffer, 204, 453);
		sprintf(g_frontendScratchBuffer, "imptiny%d", animationFrame);
		FrontImage_DrawSprite(g_frontendScratchBuffer, 420, 453);
	}
	if (Frontend_HandleCommonScreenControls(0) == 1) {
		return 1;
	}
#ifdef XVT_MODERN
	if (XvtDialog_IsActive())
		return 0;
#endif
	if (g_gameConfig.helpOn != 0) {
		FrontendButton_EnableOverlayText();
	}
	FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_019_CANCEL));
	FrontendDraw_RectAssign(&rect, 85, 447, 176, 471);
	cancelPressed = FrontendButton_DrawSpriteHitTest(
		&rect, "leaveup", "leavedown", FrontendString_Get(FRONTSTR_019_CANCEL), 12, 0, 8, "buttonsound");
	FrontendButton_DisableOverlayText();
#ifdef XVT_MODERN
	if (cancelPressed != 0) {
		XvtNetworkSession_Cancel();
#else
	if (cancelPressed != 0 || firstFrame == ACCESS_TIMEOUT_FRAME) {
		Net_ShutdownDirectPlaySessionNoJoinAbort();
#endif
		g_skipFrontendEntryMovie = 1;
		FrontendScreen_SetCallbacks(FrontendNet_JoinGameScreen, FrontendMissionList_FreeScreenResources);
		FrontImage_FreeResourceByName("background");
	}
	return 0;
}

// FUNCTION: XVT 0x4D8350
int FrontendNet_DrawJoinGameMissionBriefing(void) {
#ifdef XVT_MODERN
	return XvtNetworkBrowser_DrawMission();
#else
	RECT rect;

	FrontendDraw_RectAssign(&rect, 88, 309, 430, 324);
	if (g_frontendNetReceivedMissionDescriptionId != -1) {
		if (g_missionList != NULL && g_selectedMissionListIndex < g_missionCount) {
			sprintf(g_frontendScratchBuffer, "%s %c%s", FrontendString_Get(FRONTSTR_187_MISSION), 4,
					g_missionList[g_selectedMissionListIndex].description);
		} else {
			strcpy(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_187_MISSION));
		}
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);
	} else {
		FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_187_MISSION), &rect, 0, 1, 0xFFFF);
	}

	if (g_frontendNetReceivedMissionDescriptionId != -1 && g_briefingText != NULL) {
		unsigned int lineCount;

		FrontendDraw_RectAssign(&rect, 88, 328, 420, 426);
		lineCount = FrontendText_DrawWrapped(12, g_briefingText, &rect, 0xFFFF, 4, 4096) + 1;
		if (lineCount > 6) {
			FrontendDraw_RectAssign(&rect, 421, 328, 430, 426);
			g_frontendFirstVisibleLine = FrontendScrollbar_Draw(&rect, g_frontendFirstVisibleLine, lineCount,
																0, 5, (unsigned int)g_colorNavy, 6);
			FrontendDraw_RectAssign(&rect, 88, 328, 420, 426);
		} else {
			FrontendDraw_RectAssign(&rect, 88, 328, 430, 426);
		}
		FrontendText_DrawWrapped(12, g_briefingText, &rect, 0xFFFF, 4, g_frontendFirstVisibleLine);
	}
	return 1;
#endif
}

// FUNCTION: XVT 0x4D8540
int FrontendNet_DrawJoinGamePlayerRoster(void) {
#ifdef XVT_MODERN
	return XvtNetworkBrowser_DrawRoster();
#else
	RECT rect;
	RECT previousClipRect;
	int displayedCount;
	int rosterIndex;

	FrontendDraw_RectAssign(&rect, 88, 218, 430, 233);
	FrontendText_DrawAlignedInRect(12, FrontendString_Get(FRONTSTR_201_PLAYERS_IN_GAME), &rect, 0, 1, 0xFFFF);
	if (g_frontendNetReceivedMissionDescriptionId != -1) {
		displayedCount = 0;
		Net_CountReadyPlayers();
		FrontendDraw_RectAssign(&rect, 88, 238, 258, 252);
		for (rosterIndex = 0; rosterIndex < 8; ++rosterIndex) {
			if (g_mpRoster[rosterIndex].playerId != 0) {
				FrontendDisplay_GetScreenClipRect(&previousClipRect);
				++displayedCount;
				FrontendDisplay_SetScreenClipRect640x480(&rect);
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
						FrontendString_Get(FRONTSTR_154_DRONE + g_mpRoster[rosterIndex].pilotRating), 4,
						g_mpRoster[rosterIndex].name);
				FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer, &rect, 0, 1, 0xFFFF);
				FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);
				if (displayedCount == 4) {
					FrontendDraw_RectAssign(&rect, 259, 238, 430, 252);
				} else {
					FrontendDraw_RectOffsetXY(&rect, 0, 15);
				}
			}
		}
	}
	return 1;
#endif
}

// FUNCTION: XVT 0x4D8690
int FrontendNet_UpdateAndDrawPanel(int frameCounter) {
	RECT rect;
	int cursorX;
	int cursorY;
	int localPlayerId;
	int teamMessage;
	int lineCount;
	int maxScroll;
	int playerIndex;

	if (frameCounter == 0) {
		Keyboard_FlushCharBuffer();
		g_frontendChatScrollOffset = 0;
	}

	if (g_missionSetupRosterAuthoritative != 0) {
		FrontendCursor_GetPos(&cursorX, &cursorY);
		if (g_frontendChatTeamOnly == 0) {
			FrontImage_DrawSprite("tab2", 0, 0);
			FrontendDraw_RectAssign(&rect, 556, 95, 588, 105);
			FrontendText_DrawCentered(10, FrontendString_Get(FRONTSTR_217_TEAM), &rect, g_colorGray);
			if (FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
				(FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0)) {
				if (g_gameConfig.sfxDatapadEnabled != 0)
					FrontendSound_PlayUISound("jewelsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume,
											  63);
				g_frontendChatTeamOnly = 1;
			}
			FrontImage_DrawSprite("tab1", 0, 0);
			FrontendDraw_RectAssign(&rect, 520, 95, 552, 105);
			FrontendText_DrawCentered(10, FrontendString_Get(FRONTSTR_402_ALL), &rect, 0xFFFF);
		} else {
			FrontImage_DrawSprite("tab1", 0, 0);
			FrontendDraw_RectAssign(&rect, 520, 95, 552, 105);
			FrontendText_DrawCentered(10, FrontendString_Get(FRONTSTR_402_ALL), &rect, g_colorGray);
			if (FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
				(FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0)) {
				if (g_gameConfig.sfxDatapadEnabled != 0)
					FrontendSound_PlayUISound("jewelsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume,
											  63);
				g_frontendChatTeamOnly = 0;
			}
			FrontImage_DrawSprite("tab2", 0, 0);
			FrontendDraw_RectAssign(&rect, 556, 95, 588, 105);
			FrontendText_DrawCentered(10, FrontendString_Get(FRONTSTR_217_TEAM), &rect, 0xFFFF);
		}
	} else {
		g_frontendChatTeamOnly = 0;
	}

	FrontendDraw_RectAssign(&rect, 461, 406, 595, 424);
	if (FrontendText_DrawEditableField(&rect, g_frontendChatInputBuffer, 100, 0, 12, NULL) != 0 &&
		g_frontendChatInputBuffer[0] != 0) {
		g_frontendNetPacketScratch.packetType = NET_PACKET_CHAT;
		localPlayerId = Net_GetLocalPlayerId();
		if (Net_IsPlayerReady(localPlayerId) != 0) {
			teamMessage = g_frontendChatTeamOnly;
			if (g_frontendChatTeamOnly == 1)
				g_frontendScratchBuffer[0] = 2;
			else
				g_frontendScratchBuffer[0] = 3;
		} else {
			g_frontendScratchBuffer[0] = 4;
			teamMessage = g_frontendChatTeamOnly;
		}
		g_frontendScratchBuffer[1] = 0;
		strcat(g_frontendScratchBuffer, g_pilotData.name);
		strcat(g_frontendScratchBuffer, ": ");
		{
			size_t messageLength = strlen(g_frontendScratchBuffer);
			g_frontendScratchBuffer[messageLength] = 1;
			g_frontendScratchBuffer[messageLength + 1] = 0;
		}
		strcat(g_frontendScratchBuffer, g_frontendChatInputBuffer);
		memcpy(g_frontendNetPacketScratch.payload, g_frontendScratchBuffer, strlen(g_frontendScratchBuffer));
		if (teamMessage != 0) {
			playerIndex = 0;
			while (playerIndex < 8 &&
				   Net_GetLocalPlayerId() != g_missionSetupPlayerAssignments.activePlayerIds[playerIndex])
				++playerIndex;
			if (playerIndex == 8) {
				Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, strlen(g_frontendScratchBuffer) + 4);
			} else {
				for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
					int playerId =
						g_missionSetupPlayerAssignments.teamPlayerIds[g_pilotData.team][playerIndex];
					if (playerId != 0) {
						Net_SendPacketAndFlush((DPID)playerId, &g_frontendNetPacketScratch,
											   strlen(g_frontendScratchBuffer) + 4);
					}
				}
			}
		} else {
			Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, strlen(g_frontendScratchBuffer) + 4);
		}
		memset(g_frontendChatInputBuffer, 0, sizeof(g_frontendChatInputBuffer));
	}

	FrontendDraw_RectAssign(&rect, 461, 117, 595, 407);
	lineCount = FrontendText_DrawWrapped(12, g_frontendChatLogBuffer, &rect, 0xFFFF, 2, 1024);
	maxScroll = lineCount - 20;
	if (lineCount <= 20)
		maxScroll = 0;
	if (maxScroll > 0) {
		++maxScroll;
		if (g_frontendChatScrollOffset == maxScroll)
			--g_frontendChatScrollOffset;
		FrontendDraw_RectAssign(&rect, 596, 117, 605, 407);
		g_frontendChatScrollOffset = maxScroll -
									 FrontendScrollbar_Draw(&rect, maxScroll - g_frontendChatScrollOffset - 1,
															maxScroll, 0, 5, (unsigned int)g_colorNavy, 7) -
									 1;
	}
	FrontendDraw_RectAssign(&rect, 461, 117, 595, 398);
	return FrontendText_DrawWrapped(12, g_frontendChatLogBuffer, &rect, 0xFFFF, 2,
									maxScroll - g_frontendChatScrollOffset - 1);
}

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4D8C20
int FrontendNet_ConnectToSelectedGameScreen(int firstFrame) {
	enum {
		CONNECT_ACTION_FRAME = 5,
		CONNECT_RESTORE_DISABLE_FRAME = 4,
		CONNECT_FONT_SIZE = 12,
		CONNECT_TITLE_FONT_SIZE = 15,
		CONNECT_SCREEN_WIDTH = 640,
		CONNECT_SCREEN_HEIGHT = 480,
		CONNECT_MESSAGE_LEFT = 84,
		CONNECT_MESSAGE_RIGHT = 605,
		CONNECT_MESSAGE_TOP = 90,
		CONNECT_MESSAGE_BOTTOM = 106,
		CONNECT_MESSAGE_LOWER_TOP = 415,
		CONNECT_MESSAGE_LOWER_BOTTOM = 429,
		CONNECT_GDI_MESSAGE_BOTTOM = 429,
		BRIEFING_TEXT_CAPACITY = 4096,
		CHAT_LOG_CAPACITY = 1024,
		JOIN_REQUEST_PACKET_SIZE = 24,
	};

	RECT rect;
	char playerInfo[2];
	int hostPlayerId;
	unsigned int networkType;

	if (firstFrame == 0) {
		Net_ClearPlayerReadyFlags();
		if (g_frontendChatLogBuffer != NULL) {
			memset(g_frontendChatLogBuffer, 0, CHAT_LOG_CAPACITY);
			g_frontendChatLogUsedBytes = 0;
		}
		g_frontendNetReceivedMissionDirectoryId = MISSION_DIRECTORY_TRAINING_EXERCISES;
		g_frontendNetReceivedMissionDescriptionId = -1;
		g_frontendNetSelectedSessionIdx = -1;
		g_missionSetupRosterAuthoritative = 0;
		if (g_briefingText == NULL)
			g_briefingText = (char*)malloc(BRIEFING_TEXT_CAPACITY);
		FrontImage_RegisterResourceDefault("frontres\\joinback.bmp", "background");
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("background", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		if (g_hostCdAvailable != 0)
			FrontImage_DrawSprite("allactive", 0, 0);
		else
			FrontImage_DrawSprite("clientactive", 0, 0);
		FrontendDraw_RectAssign(&rect, 0, 0, CONNECT_SCREEN_WIDTH, CONNECT_SCREEN_HEIGHT);
		FrontendText_DrawCentered(CONNECT_TITLE_FONT_SIZE, FrontendString_Get(FRONTSTR_645_CONNECTING), &rect,
								  0xFFFF);
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_ResetGlyphScratch();
		return 0;
	}

	if (firstFrame > 0 && firstFrame < CONNECT_ACTION_FRAME) {
		FrontendCursor_Hide();
		networkType = g_gameConfig.networkType;
		switch ((NetworkTransportType)networkType) {
			case NET_TRANSPORT_TCPIP:
				FrontendDraw_RectAssign(&rect, CONNECT_MESSAGE_LEFT, CONNECT_MESSAGE_LOWER_TOP,
										CONNECT_MESSAGE_RIGHT, CONNECT_MESSAGE_LOWER_BOTTOM);
				FrontendText_DrawCentered(
					CONNECT_FONT_SIZE,
					FrontendString_Get(
						FRONTSTR_740_IF_ATTEMPT_TO_CONNECT_TO_THE_TCP_IP_ADDRESS_FAILS_TIMEOUT_WILL_OCCUR_IN_2_5_MINUTES),
					&rect, g_colorRed);
				FrontendDraw_RectAssign(&rect, CONNECT_MESSAGE_LEFT, CONNECT_MESSAGE_TOP,
										CONNECT_MESSAGE_RIGHT, CONNECT_MESSAGE_BOTTOM);
				FrontendText_DrawCentered(
					CONNECT_FONT_SIZE,
					FrontendString_Get(
						FRONTSTR_740_IF_ATTEMPT_TO_CONNECT_TO_THE_TCP_IP_ADDRESS_FAILS_TIMEOUT_WILL_OCCUR_IN_2_5_MINUTES),
					&rect, g_colorRed);
				break;
			case NET_TRANSPORT_MODEM:
			case NET_TRANSPORT_SERIAL:
				FrontendDraw_RectAssign(&rect, CONNECT_MESSAGE_LEFT, CONNECT_MESSAGE_LOWER_TOP,
										CONNECT_MESSAGE_RIGHT, CONNECT_MESSAGE_LOWER_BOTTOM);
				FrontendText_DrawCentered(
					CONNECT_FONT_SIZE,
					FrontendString_Get(FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT), &rect,
					g_colorRed);
				FrontendDraw_RectAssign(&rect, CONNECT_MESSAGE_LEFT, CONNECT_MESSAGE_TOP,
										CONNECT_MESSAGE_RIGHT, CONNECT_MESSAGE_BOTTOM);
				FrontendText_DrawCentered(
					CONNECT_FONT_SIZE,
					FrontendString_Get(FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT), &rect,
					g_colorRed);
				break;
			default:
				break;
		}
		if (firstFrame == CONNECT_RESTORE_DISABLE_FRAME) {
			FrontendDisplay_DisableOffscreenRestore();
			return 0;
		}
	} else if (firstFrame == CONNECT_ACTION_FRAME) {
		networkType = g_gameConfig.networkType;
		switch ((NetworkTransportType)networkType) {
			case NET_TRANSPORT_TCPIP:
				FrontendDraw_RectAssign(&rect, CONNECT_MESSAGE_LEFT, CONNECT_MESSAGE_LOWER_TOP,
										CONNECT_MESSAGE_RIGHT, CONNECT_MESSAGE_LOWER_BOTTOM);
				FrontendText_DrawCentered(
					CONNECT_FONT_SIZE,
					FrontendString_Get(
						FRONTSTR_740_IF_ATTEMPT_TO_CONNECT_TO_THE_TCP_IP_ADDRESS_FAILS_TIMEOUT_WILL_OCCUR_IN_2_5_MINUTES),
					&rect, g_colorRed);
				FrontendDraw_RectAssign(&rect, CONNECT_MESSAGE_LEFT, CONNECT_MESSAGE_TOP,
										CONNECT_MESSAGE_RIGHT, CONNECT_MESSAGE_BOTTOM);
				FrontendText_DrawCentered(
					CONNECT_FONT_SIZE,
					FrontendString_Get(
						FRONTSTR_740_IF_ATTEMPT_TO_CONNECT_TO_THE_TCP_IP_ADDRESS_FAILS_TIMEOUT_WILL_OCCUR_IN_2_5_MINUTES),
					&rect, g_colorRed);
				break;
			case NET_TRANSPORT_MODEM:
			case NET_TRANSPORT_SERIAL:
				FrontendDraw_RectAssign(&rect, CONNECT_MESSAGE_LEFT, CONNECT_MESSAGE_LOWER_TOP,
										CONNECT_MESSAGE_RIGHT, CONNECT_MESSAGE_LOWER_BOTTOM);
				FrontendText_DrawCentered(
					CONNECT_FONT_SIZE,
					FrontendString_Get(FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT), &rect,
					g_colorRed);
				FrontendDraw_RectAssign(&rect, CONNECT_MESSAGE_LEFT, CONNECT_MESSAGE_TOP,
										CONNECT_MESSAGE_RIGHT, CONNECT_MESSAGE_BOTTOM);
				FrontendText_DrawCentered(
					CONNECT_FONT_SIZE,
					FrontendString_Get(FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT), &rect,
					g_colorRed);
				break;
			default:
				break;
		}

		g_missionSetupIsHost = 0;
		Net_ShutdownDirectPlaySession();
		networkType = g_gameConfig.networkType;
		switch ((NetworkTransportType)networkType) {
			case NET_TRANSPORT_IPX:
			case NET_TRANSPORT_SERIAL:
				g_frontendScratchBuffer[0] = '\0';
				break;
			case NET_TRANSPORT_TCPIP:
				FrontendDisplay_FlipDirectDrawToGDISurface();
				FrontendDraw_RectAssign(&rect, CONNECT_MESSAGE_LEFT, CONNECT_MESSAGE_TOP,
										CONNECT_MESSAGE_RIGHT, CONNECT_GDI_MESSAGE_BOTTOM);
				FrontendDisplay_DrawGdiTextOnSecondaryDisplay(
					&rect, FrontendString_Get(FRONTSTR_645_CONNECTING),
					FrontendString_Get(
						FRONTSTR_740_IF_ATTEMPT_TO_CONNECT_TO_THE_TCP_IP_ADDRESS_FAILS_TIMEOUT_WILL_OCCUR_IN_2_5_MINUTES));
				strcpy(g_frontendScratchBuffer, g_gameConfig.ipAddress);
				break;
			case NET_TRANSPORT_MODEM:
				strcpy(g_frontendScratchBuffer, g_gameConfig.phoneNumber);
				break;
		}

		playerInfo[0] = (char)(g_pilotData.rating + 1);
		playerInfo[1] = '\0';
		FrontendDisplay_UnlockBackBuffer();
		FrontendCursor_ShowOsCursor();
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		if (Net_StartNetworkSession(
				(int)g_frontendNetXvtDirectPlayAppGuid[0], (int)g_frontendNetXvtDirectPlayAppGuid[1],
				(int)g_frontendNetXvtDirectPlayAppGuid[2], (int)g_frontendNetXvtDirectPlayAppGuid[3],
				playerInfo, g_pilotData.name, g_missionSetupIsHost, g_frontendNetSessionList[0].gameName,
				(NetworkTransportType)g_gameConfig.networkType, 0, 1, g_frontendScratchBuffer, NULL) != 0) {
			hostPlayerId = Net_GetHostPlayerId();
			g_frontendNetPacketScratch.packetType = NET_PACKET_JOIN_REQUEST;
			*(int*)g_frontendNetPacketScratch.payload = FRONTEND_NET_PROTOCOL_VERSION;
			memcpy(g_frontendNetPacketScratch.payload + sizeof(int), g_gameConfig.password,
				   sizeof(g_gameConfig.password));
			Net_SendPacketAndFlush(hostPlayerId, &g_frontendNetPacketScratch, JOIN_REQUEST_PACKET_SIZE);
			FrontendScreen_SetCallbacks(FrontendNet_AccessAllianceNetworkScreen, NULL);
		} else {
			g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
			FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
		}
		FrontendDisplay_UnlockBackBuffer();
		networkType = g_gameConfig.networkType;
		switch ((NetworkTransportType)networkType) {
			case NET_TRANSPORT_TCPIP:
				FrontendDraw_RectAssign(&rect, CONNECT_MESSAGE_LEFT, CONNECT_MESSAGE_TOP,
										CONNECT_MESSAGE_RIGHT, CONNECT_GDI_MESSAGE_BOTTOM);
				FrontendDisplay_ClearSecondaryDisplayGdi(&rect);
				break;
			default:
				break;
		}
		FrontendCursor_HideOsCursor();
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		FrontendCursor_Show();
		FrontendDisplay_EnableOffscreenRestore();
	}
	return 0;
}
#endif

// FUNCTION: XVT 0x4D9170
int FrontendNet_DrawJoinGameSidebarsAndQueryAll(void) {
	enum {
		QUERY_BUTTON_LEFT = 22,
		QUERY_BUTTON_TOP = 114,
		QUERY_BUTTON_RIGHT = 42,
		QUERY_BUTTON_BOTTOM = 138,
		QUERY_BUTTON_FONT_SIZE = 12,
		QUERY_BUTTON_HOVER_SLOT = 11,
	};

	int cursorX;
	int cursorY;
	FrontendNavigationSlotState slotStates[8];
	RECT rect;

	slotStates[1] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[2] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[3] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[4] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[5] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[6] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[7] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[0] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	FrontendCursor_GetPos(&cursorX, &cursorY);
	FrontendDraw_RectAssign(&rect, QUERY_BUTTON_LEFT, QUERY_BUTTON_TOP, QUERY_BUTTON_RIGHT,
							QUERY_BUTTON_BOTTOM);
	if (FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
		(FrontendMouse_GetLeftDown() != 0 || FrontendMouse_GetRightDown() != 0 ||
		 FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0)) {
		slotStates[0] = FRONTEND_NAVIGATION_SLOT_SELECTED;
	}
	FrontendButton_DrawEightSlotNavigationState(slotStates);
	FrontendDraw_RectAssign(&rect, QUERY_BUTTON_LEFT, QUERY_BUTTON_TOP, QUERY_BUTTON_RIGHT,
							QUERY_BUTTON_BOTTOM);
	if (FrontendButton_DrawSpriteHitTest(&rect, "join1u", "join1d",
#ifdef XVT_MODERN
										 "Refresh",
#else
										 FrontendString_Get(FRONTSTR_734_QUERY_ALL_GAMES),
#endif
										 QUERY_BUTTON_FONT_SIZE, 0, QUERY_BUTTON_HOVER_SLOT,
										 "jewelsound") != 0) {
#ifdef XVT_MODERN
		XvtNetworkTask_Refresh();
#else
		FrontendNet_ProbeAllSessions();
#endif
	}
	return 1;
}

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4D9280
int FrontendNet_MakeSessionGuidKey(NetSessionGuid guid) {
	return (int)(((unsigned int)guid.data2 << 16) + guid.data3 + guid.data4[1] + guid.data4[0] +
				 guid.data1);
}
#endif

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4D92B0
int FrontendNet_RefreshSessionList(void) {
	enum {
		FRONTEND_NET_SESSION_CAPACITY =
			sizeof(g_frontendNetSessionList) / sizeof(g_frontendNetSessionList[0]),
		FRONTEND_NET_NEW_SESSION_PLAYERS_NEEDED = 9,
		FRONTEND_NET_UI_SOUND_PRIORITY = 255,
		FRONTEND_NET_UI_SOUND_VOLUME_SCALE = 12,
		FRONTEND_NET_UI_SOUND_CENTER_PAN = 63
	};

	NetSessionEnumEntry enumeratedSessions[FRONTEND_NET_SESSION_CAPACITY];
	unsigned int enumeratedSessionCount;
	unsigned int enumeratedIndex;
	int addedSession;
	int existingIndex;

	addedSession = 0;
	if (g_frontendNetSelectedSessionIdx != -1)
		return 0;

	enumeratedSessionCount = (unsigned int)Net_EnumerateAppSessions(
		g_frontendNetXvtDirectPlayAppGuid[0], g_frontendNetXvtDirectPlayAppGuid[1],
		g_frontendNetXvtDirectPlayAppGuid[2], g_frontendNetXvtDirectPlayAppGuid[3], enumeratedSessions,
		FRONTEND_NET_SESSION_CAPACITY, (NetworkTransportType)g_gameConfig.networkType);

	existingIndex = 0;
	if (g_frontendNetSessionCount != 0) {
		do {
			int existingGuidKey =
				FrontendNet_MakeSessionGuidKey(g_frontendNetSessionList[existingIndex].sessionGuid);
			if (existingGuidKey != 0) {
				for (enumeratedIndex = 0; enumeratedIndex < enumeratedSessionCount; ++enumeratedIndex) {
					int enumeratedGuidKey =
						FrontendNet_MakeSessionGuidKey(enumeratedSessions[enumeratedIndex].sessionGuid);
					if (enumeratedGuidKey != 0 && enumeratedGuidKey == existingGuidKey) {
						memset(&enumeratedSessions[enumeratedIndex].sessionGuid, 0,
							   sizeof(enumeratedSessions[enumeratedIndex].sessionGuid));
						break;
					}
				}
				if (enumeratedIndex == enumeratedSessionCount)
					memset(&g_frontendNetSessionList[existingIndex], 0,
						   sizeof(g_frontendNetSessionList[existingIndex]));
			}
			++existingIndex;
		} while ((unsigned int)existingIndex < (unsigned int)g_frontendNetSessionCount);
	}

	for (enumeratedIndex = 0; enumeratedIndex < enumeratedSessionCount; ++enumeratedIndex) {
		int enumeratedGuidKey =
			FrontendNet_MakeSessionGuidKey(enumeratedSessions[enumeratedIndex].sessionGuid);
		if (enumeratedGuidKey != 0) {
			existingIndex = (unsigned int)g_frontendNetSessionCount;
			strcpy(g_frontendNetSessionList[existingIndex].gameName,
				   enumeratedSessions[enumeratedIndex].sessionName);
			memcpy(&g_frontendNetSessionList[existingIndex].sessionGuid,
				   &enumeratedSessions[enumeratedIndex].sessionGuid,
				   sizeof(g_frontendNetSessionList[existingIndex].sessionGuid));
			++g_frontendNetSessionCount;
			addedSession = 1;
			g_frontendNetSessionList[existingIndex].playersNeeded = FRONTEND_NET_NEW_SESSION_PLAYERS_NEEDED;
			g_frontendNetSessionList[existingIndex].lastQueryTick = 0;
			g_frontendNetSessionList[existingIndex].version = FRONTEND_NET_PROTOCOL_VERSION;
			g_frontendNetSessionList[existingIndex].passwordRequired = 0;
		}
	}

	if (addedSession != 0 && g_gameConfig.sfxDatapadEnabled != 0) {
		FrontendSound_PlayUISound("newpsound", 1, 0, FRONTEND_NET_UI_SOUND_PRIORITY,
								  FRONTEND_NET_UI_SOUND_VOLUME_SCALE * g_gameConfig.sfxDatapadVolume,
								  FRONTEND_NET_UI_SOUND_CENTER_PAN);
	}

	existingIndex = 0;
	if (g_frontendNetSessionCount != 0) {
		do {
			int existingGuidKey =
				FrontendNet_MakeSessionGuidKey(g_frontendNetSessionList[existingIndex].sessionGuid);
			if (existingGuidKey == 0) {
				unsigned int replacementIndex;

				for (replacementIndex = existingIndex + 1;
					 (unsigned int)replacementIndex < (unsigned int)g_frontendNetSessionCount; ++replacementIndex) {
					int enumeratedGuidKey = FrontendNet_MakeSessionGuidKey(g_frontendNetSessionList[replacementIndex].sessionGuid);
					if (enumeratedGuidKey != 0) {
						memcpy(&g_frontendNetSessionList[existingIndex],
							   &g_frontendNetSessionList[replacementIndex],
							   sizeof(g_frontendNetSessionList[existingIndex]));
						memset(&g_frontendNetSessionList[replacementIndex], 0,
							   sizeof(g_frontendNetSessionList[replacementIndex]));
						break;
					}
				}
			}
			++existingIndex;
		} while ((unsigned int)existingIndex < (unsigned int)g_frontendNetSessionCount);
	}

	g_frontendNetSessionCount = 0;
	while ((unsigned int)g_frontendNetSessionCount < FRONTEND_NET_SESSION_CAPACITY) {
		existingIndex = (unsigned int)g_frontendNetSessionCount;
		if (FrontendNet_MakeSessionGuidKey(g_frontendNetSessionList[existingIndex].sessionGuid) == 0)
			break;
		++g_frontendNetSessionCount;
	}
	FrontendNet_SortSessions();
	return g_frontendNetSessionCount;
}
#endif

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4D95D0
int FrontendNet_CompareSessionListEntries(const FrontendNetSessionEntry* lhs,
										  const FrontendNetSessionEntry* rhs) {
	int result;
	unsigned int rhsPlayersNeeded;
	unsigned int lhsPlayersNeeded;

	result = (rhs->version == 101u) - (lhs->version == 101u);
	if (result == 0) {
		result = (rhs->queryState == 0) - (lhs->queryState == 0);
		if (result == 0) {
			rhsPlayersNeeded = (unsigned int)rhs->playersNeeded;
			lhsPlayersNeeded = (unsigned int)lhs->playersNeeded;
			result = (rhsPlayersNeeded > 0) - (lhsPlayersNeeded > 0);
			if (result == 0) {
				result = (rhsPlayersNeeded > 8u) - (lhsPlayersNeeded > 8u);
				if (result == 0)
					return strcmp(lhs->gameName, rhs->gameName);
			}
		}
	}

	return result;
}
#endif

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4D9680
int FrontendNet_SortSessions(void) {
	enum { FRONTEND_NET_BRIEFING_TEXT_CAPACITY = 4096 };

	int selectedSessionGuidKey;
	int sessionIndex;

	if (g_frontendNetSessionCount == 0)
		return -1;
	if (g_frontendNetSelectedSessionIdx != -1) {
		selectedSessionGuidKey = FrontendNet_MakeSessionGuidKey(g_frontendNetSessionList[g_frontendNetSelectedSessionIdx].sessionGuid);
	}

	qsort(g_frontendNetSessionList, (size_t)g_frontendNetSessionCount, sizeof(g_frontendNetSessionList[0]),
		  (int (*)(const void*, const void*))FrontendNet_CompareSessionListEntries);
	if (g_frontendNetSelectedSessionIdx != -1) {
		for (sessionIndex = 0; (unsigned int)sessionIndex < (unsigned int)g_frontendNetSessionCount;
			 ++sessionIndex) {
			if (FrontendNet_MakeSessionGuidKey(g_frontendNetSessionList[sessionIndex].sessionGuid) ==
				selectedSessionGuidKey) {
				g_frontendNetSelectedSessionIdx = sessionIndex;
				break;
			}
		}
		if (sessionIndex == g_frontendNetSessionCount) {
			g_frontendNetSelectedSessionIdx = -1;
			g_frontendNetReceivedMissionDescriptionId = -1;
			memset(g_frontendNetSelectedGameName, 0, sizeof(g_frontendNetSelectedGameName));
			memset(g_briefingText, 0, FRONTEND_NET_BRIEFING_TEXT_CAPACITY);
			Net_ShutdownDirectPlaySession();
		}
	}

	return g_frontendNetSelectedSessionIdx;
}
#endif

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4D9780
int FrontendNet_ProbeAllSessions(void) {
	enum { BRIEFING_TEXT_CAPACITY = 4096 };

	int sessionIndex;

	g_frontendNetSelectedSessionIdx = -1;
	g_frontendNetProbeResponseType = 0;
	g_frontendNetReceivedMissionDescriptionId = -1;
	memset(g_frontendNetSelectedGameName, 0, sizeof(g_frontendNetSelectedGameName));
	memset(g_briefingText, 0, BRIEFING_TEXT_CAPACITY);
	Net_ShutdownDirectPlaySession();
	FrontendNet_RefreshSessionList();
	for (sessionIndex = 0; sessionIndex < g_frontendNetSessionCount; ++sessionIndex)
		FrontendNet_ProbeSessionByIndex(sessionIndex);
	Net_ShutdownDirectPlaySession();
	FrontendNet_SortSessions();
	return g_frontendNetSessionCount;
}
#endif

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4D97F0
int FrontendNet_ProbeSessionByIndex(int sessionIdx) {
	enum {
		SESSION_PROBE_PLAYER_COUNT_PACKET = 58,
		SESSION_PROBE_PACKET_SIZE = sizeof(int),
		SESSION_PROBE_TIMEOUT_MS = 2000,
		BRIEFING_TEXT_CAPACITY = 4096,
	};

	GUID sessionGuid;
	char playerInfo[2];
	unsigned int probeStartTick;
	unsigned int currentTick;
	int hostPlayerId;
	int packetType;

	Net_ShutdownDirectPlaySession();
	strcpy(g_frontendNetSelectedGameName, g_frontendNetSessionList[sessionIdx].gameName);
	switch ((NetworkTransportType)g_gameConfig.networkType) {
		case NET_TRANSPORT_IPX:
			g_frontendScratchBuffer[0] = '\0';
			break;
		case NET_TRANSPORT_TCPIP:
			strcpy(g_frontendScratchBuffer, g_gameConfig.ipAddress);
			break;
		case NET_TRANSPORT_MODEM:
			strcpy(g_frontendScratchBuffer, g_gameConfig.phoneNumber);
			break;
		default:
			break;
	}

	memcpy(&sessionGuid, &g_frontendNetSessionList[sessionIdx].sessionGuid, sizeof(sessionGuid));
	playerInfo[0] = (char)(g_pilotData.rating + 1);
	playerInfo[1] = '\0';
	if (Net_StartNetworkSession(
			(int)g_frontendNetXvtDirectPlayAppGuid[0], (int)g_frontendNetXvtDirectPlayAppGuid[1],
			(int)g_frontendNetXvtDirectPlayAppGuid[2], (int)g_frontendNetXvtDirectPlayAppGuid[3], playerInfo,
			g_pilotData.name, 0, g_frontendNetSelectedGameName,
			(NetworkTransportType)g_gameConfig.networkType, 0, 1, g_frontendScratchBuffer,
			&sessionGuid) == 0) {
		if (g_frontendNetSelectedSessionIdx != -1) {
			g_frontendNetSelectedSessionIdx = -1;
			g_frontendNetReceivedMissionDescriptionId = -1;
			g_frontendNetProbeResponseType = 0;
			memset(g_frontendNetSelectedGameName, 0, sizeof(g_frontendNetSelectedGameName));
			memset(g_briefingText, 0, BRIEFING_TEXT_CAPACITY);
			FrontendNet_RefreshSessionList();
		}
		g_frontendNetSessionList[sessionIdx].playersNeeded = 0;
		g_frontendNetSessionList[sessionIdx].lastQueryTick = GetTickCount();
		g_frontendNetSessionList[sessionIdx].version = 0;
		g_frontendNetSessionList[sessionIdx].passwordRequired = 0;
		g_frontendNetSessionList[sessionIdx].queryState = 0;
		return 0;
	}

	if (g_frontendNetSelectedSessionIdx != -1 && sessionIdx == g_frontendNetSelectedSessionIdx) {
		g_frontendNetReceivedMissionDirectoryId = MISSION_DIRECTORY_TRAINING_EXERCISES;
		g_frontendNetReceivedMissionDescriptionId = -1;
	}
	hostPlayerId = Net_GetHostPlayerId();
	g_frontendNetPacketScratch.packetType = NET_PACKET_PROBE_REQUEST;
	Net_SendPacketAndFlush(hostPlayerId, &g_frontendNetPacketScratch, SESSION_PROBE_PACKET_SIZE);
	if (g_frontendNetSelectedSessionIdx != -1 && sessionIdx == g_frontendNetSelectedSessionIdx) {
		g_frontendNetPacketScratch.packetType = NET_PACKET_CHAT_SYNC_REQUEST;
		Net_SendPacketAndFlush(hostPlayerId, &g_frontendNetPacketScratch, SESSION_PROBE_PACKET_SIZE);
	}

	g_frontendNetProbePlayersNeeded = 0;
	g_frontendNetProbeResponseType = 0;
	probeStartTick = GetTickCount();
	do {
		packetType = FrontendNet_ProcessNetworkPackets();
		currentTick = GetTickCount();
		if (packetType == SESSION_PROBE_PLAYER_COUNT_PACKET || packetType == NET_PACKET_PROBE_RESPONSE)
			break;
		if (packetType == NET_PACKET_STATE) {
			if (g_frontendNetSelectedSessionIdx != -1 && sessionIdx == g_frontendNetSelectedSessionIdx) {
				g_pilotData.missionDirectoryId = g_frontendNetReceivedMissionDirectoryId;
				g_pilotData.missionDescriptionIds[g_pilotData.missionDirectoryId] =
					g_frontendNetReceivedMissionDescriptionId;
				MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
				MissionSetup_LoadMissionDescText(g_briefingText);
				if (g_missionList != NULL) {
					g_selectedMissionListIndex = 0;
					while ((unsigned int)g_selectedMissionListIndex < (unsigned int)g_missionCount &&
						   g_missionList[g_selectedMissionListIndex].missionIdx !=
							   g_pilotData.missionDescriptionIds[g_pilotData.missionDirectoryId])
						++g_selectedMissionListIndex;
				}
				g_frontendFirstVisibleLine = 0;
			} else {
				g_frontendNetReceivedMissionDescriptionId = -1;
			}
		}
	} while (currentTick - probeStartTick < SESSION_PROBE_TIMEOUT_MS);

	if (packetType == SESSION_PROBE_PLAYER_COUNT_PACKET) {
		g_frontendNetSessionList[sessionIdx].lastQueryTick = GetTickCount();
		g_frontendNetSessionList[sessionIdx].playersNeeded = g_frontendNetProbePlayersNeeded;
		g_frontendNetSessionList[sessionIdx].version = g_frontendNetProbeVersion;
		g_frontendNetSessionList[sessionIdx].passwordRequired = (uint8_t)g_frontendNetProbePasswordRequired;
		g_frontendNetSessionList[sessionIdx].queryState = 1;
	} else if (packetType == NET_PACKET_PROBE_RESPONSE) {
		g_frontendNetSessionList[sessionIdx].lastQueryTick = GetTickCount();
		g_frontendNetSessionList[sessionIdx].playersNeeded = g_frontendNetProbePlayersNeeded;
		g_frontendNetSessionList[sessionIdx].version = g_frontendNetProbeVersion;
		g_frontendNetSessionList[sessionIdx].passwordRequired = (uint8_t)g_frontendNetProbePasswordRequired;
		g_frontendNetSessionList[sessionIdx].queryState = 0;
	} else {
		g_frontendNetSessionList[sessionIdx].lastQueryTick = GetTickCount();
		g_frontendNetSessionList[sessionIdx].playersNeeded = 0;
		g_frontendNetSessionList[sessionIdx].version = 0;
		g_frontendNetSessionList[sessionIdx].passwordRequired = 0;
		g_frontendNetSessionList[sessionIdx].queryState = 0;
	}
	return 1;
}
#endif

// FUNCTION: XVT 0x4DFD10
int FrontendNet_HostGameExit(int frameCounter) {
	(void)frameCounter;

	FrontImage_FreeResourceByName("background");
	Frontend_ResetScrollableControls();
	return 0;
}

// FUNCTION: XVT 0x4DFD30
int FrontendNet_HostGameScreen(int firstFrame) {
	enum {
		HOST_NAME_MAX_CHARS = 22,
		HOST_TEXT_FIELD_ID = 0,
		HOST_BUTTON_HOVER_SLOT = 20,
	};

	int hostRequested;
#ifndef XVT_MODERN
	char playerInfo[2];
	int localPlayerId;
	int networkType;
#endif
	RECT rect;

	if (firstFrame == 0) {
		g_hostGameStartPending = 0;
		g_skipFrontendEntryMovie = 0;
		g_frontendQuickStartLaunchFlag = 0;
		g_unusedFrontendConcourseHostLatch = 1;
		g_frontendSinglePlayerFlightSessionActive = 0;
		strcpy(g_pilotData.multiplayerGameName, g_pilotData.multiplayerHostName);
		if (g_frontendChatLogBuffer != NULL) {
			memset(g_frontendChatLogBuffer, 0, 1024);
			g_frontendChatLogUsedBytes = 0;
		}
		FrontendCursor_SetPos(417, 291);
		memset(g_mpRoster, 0, sizeof(g_mpRoster));
		Keyboard_FlushCharBuffer();
		FrontImage_RegisterResourceDefault("frontres\\create.bmp", "background");
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("background", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		if (g_hostCdAvailable != 0) {
			FrontImage_DrawSprite("allactive", 0, 0);
		} else {
			FrontImage_DrawSprite("clientactive", 0, 0);
		}
		FrontImage_DrawSpriteTranslucent("createoverlay", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_ResetGlyphScratchBuffer(20);
	}

	if (g_hostGameStartPending == 0) {
		FrontendDraw_RectAssign(&rect, 245, 225, 445, 245);
		FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_017_NAME_THE_GAME_SESSION), &rect, 0xFFFF);
		FrontendDraw_RectAssign(&rect, 250, 245, 440, 265);
		hostRequested = FrontendText_DrawEditableField(&rect, g_pilotData.multiplayerGameName,
													   HOST_NAME_MAX_CHARS, HOST_TEXT_FIELD_ID, 12, NULL);
		FrontendDraw_RectAssign(&rect, 250, 275, 440, 295);
		hostRequested |= FrontendButton_HandleTextButton(&rect, FrontendString_Get(FRONTSTR_004_HOST_GAME),
														 15, 0xFFFF, HOST_BUTTON_HOVER_SLOT, "buttonsound");
		if (hostRequested != 0) {
			if (g_pilotData.multiplayerGameName[0] == '\0') {
				sprintf(g_pilotData.multiplayerGameName, "%s%s", g_pilotData.name,
						FrontendString_Get(FRONTSTR_470_S_GAME));
			}
#ifndef XVT_MODERN
			networkType = g_gameConfig.networkType;
			switch ((NetworkTransportType)networkType) {
				case NET_TRANSPORT_TCPIP:
					FrontendCursor_Hide();
					break;
				case NET_TRANSPORT_MODEM:
				case NET_TRANSPORT_SERIAL:
					FrontendCursor_Hide();
					FrontendDraw_RectAssign(&rect, 84, 415, 605, 429);
					FrontendText_DrawCentered(
						12, FrontendString_Get(FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT), &rect,
						g_colorRed);
					FrontendDraw_RectAssign(&rect, 84, 90, 605, 106);
					FrontendText_DrawCentered(
						12, FrontendString_Get(FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT), &rect,
						g_colorRed);
					break;
				default:
					break;
			}
#endif
			g_hostGameStartPending = 1;
			FrontendDisplay_DisableOffscreenRestore();
		}

		FrontendDraw_RectAssign(&rect, 507, 452, 562, 464);
		sprintf(g_frontendScratchBuffer, "v. %d.%d", 2, 0);
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, 0xFFFF);
		FrontendDraw_RectAssign(&rect, 200, 452, 436, 464);
		if (g_pilotData.name[0] != '\0') {
			sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6, g_pilotData.ratingName, 1, g_pilotData.name);
			FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, g_colorLightBlue);
		}
		if (Frontend_HandleCommonScreenControls(0) == 1) {
			return 1;
		}
#ifdef XVT_MODERN
		if (XvtDialog_IsActive())
			return 0;
#endif
		FrontendDraw_RectAssign(&rect, 85, 447, 176, 471);
		if (g_gameConfig.helpOn != 0) {
			FrontendButton_EnableOverlayText();
		}
		FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_569_PREVIOUS));
		if (FrontendButton_DrawSpriteHitTest(&rect, "leaveup", "leavedown",
											 FrontendString_Get(FRONTSTR_258_RETURN_TO_PILOT_RECORDS), 12, 0,
											 8, "buttonsound") != 0) {
			g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
			FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
		}
		FrontendButton_DisableOverlayText();
		return 0;
	}

#ifndef XVT_MODERN
	networkType = g_gameConfig.networkType;
	if (networkType >= NET_TRANSPORT_MODEM && networkType <= NET_TRANSPORT_SERIAL) {
		FrontendDraw_RectAssign(&rect, 84, 415, 605, 429);
		FrontendText_DrawCentered(12,
								  FrontendString_Get(FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT),
								  &rect, g_colorRed);
		FrontendDraw_RectAssign(&rect, 84, 90, 605, 106);
		FrontendText_DrawCentered(12,
								  FrontendString_Get(FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT),
								  &rect, g_colorRed);
	}
#endif
	strcpy(g_pilotData.multiplayerHostName, g_pilotData.multiplayerGameName);
	g_missionSetupIsHost = 1;
	g_hostGameStartPending = 0;
	FrontendCursor_Show();
#ifndef XVT_MODERN
	networkType = g_gameConfig.networkType;
	switch ((NetworkTransportType)networkType) {
		case NET_TRANSPORT_IPX:
			FrontendCursor_ShowOsCursor();
			g_frontendScratchBuffer[0] = '\0';
			break;
		case NET_TRANSPORT_TCPIP:
			FrontendDisplay_FlipDirectDrawToGDISurface();
			FrontendDraw_RectAssign(&rect, 84, 90, 605, 429);
			FrontendDisplay_DrawGdiTextOnSecondaryDisplay(
				&rect, FrontendString_Get(FRONTSTR_020_ACCESSING_IMPERIAL_NETWORK), NULL);
			FrontendCursor_ShowOsCursor();
			strcpy(g_frontendScratchBuffer, g_gameConfig.ipAddress);
			break;
		case NET_TRANSPORT_MODEM:
			FrontendDisplay_FlipDirectDrawToGDISurface();
			FrontendCursor_ShowOsCursor();
			strcpy(g_frontendScratchBuffer, g_gameConfig.phoneNumber);
			break;
		case NET_TRANSPORT_SERIAL:
			FrontendDisplay_FlipDirectDrawToGDISurface();
			FrontendCursor_ShowOsCursor();
			break;
	}
	playerInfo[0] = (char)(g_pilotData.rating + 1);
	playerInfo[1] = '\0';
#endif
#ifdef XVT_MODERN
	XvtNetworkTask_Begin(XVT_NETWORK_HOST);
	return 0;
#else
	if (Net_StartNetworkSession(
			(int)g_frontendNetXvtDirectPlayAppGuid[0], (int)g_frontendNetXvtDirectPlayAppGuid[1],
			(int)g_frontendNetXvtDirectPlayAppGuid[2], (int)g_frontendNetXvtDirectPlayAppGuid[3], playerInfo,
			g_pilotData.name, g_missionSetupIsHost, g_pilotData.multiplayerGameName,
			(NetworkTransportType)g_gameConfig.networkType, 0, 0, g_frontendScratchBuffer, NULL) == 0) {
		FrontendDisplay_UnlockBackBuffer();
		FrontendCursor_HideOsCursor();
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
		FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
		FrontendDisplay_EnableOffscreenRestore();
		return 0;
	}
	FrontendDisplay_UnlockBackBuffer();
	switch ((NetworkTransportType)g_gameConfig.networkType) {
		case NET_TRANSPORT_TCPIP:
			FrontendDraw_RectAssign(&rect, 84, 90, 605, 429);
			FrontendDisplay_ClearSecondaryDisplayGdi(&rect);
			break;
		default:
			break;
	}
	FrontendCursor_HideOsCursor();
	g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	localPlayerId = Net_GetLocalPlayerId();
	Net_SetPlayerReady(localPlayerId);
	memset(g_mpRoster, 0, sizeof(g_mpRoster));
	strcpy(g_mpRoster[0].name, g_pilotData.name);
	g_mpRoster[0].playerId = Net_GetLocalPlayerId();
	g_mpRoster[0].pilotRating = g_pilotData.rating;
	FrontendDisplay_ClearOffscreenSurface();
	FrontendScreen_SetCallbacks(MissionSetup_Update, MissionSetup_Exit);
	FrontendDisplay_EnableOffscreenRestore();
	return 0;
#endif
}

// FUNCTION: XVT 0x4E0490
int FrontendNet_ProcessNetworkPackets(void) {
	enum {
		MAX_PLAYERS = 8,
		TEAM_COUNT = 10,
		PLAYER_NAME_COPY_SIZE = 13,
		ROSTER_PACKET_FIRST_PLAYER_WORD = 13,
		ROSTER_PACKET_COMPACT_FIRST_PLAYER_WORD = 2,
		ROSTER_OPTION_WORD_COUNT = 5,
		CHAT_LOG_CAPACITY = 1024,
		CHAT_LOG_CONTENT_LIMIT = 1022,
		CHAT_SYNC_CHUNK_SIZE = 400,
		FLIGHT_GROUP_ASSIGNMENT_COUNT = 80,
		MISSION_ASSIGNMENT_TEAM_BYTES = 320,
		MISSION_SETUP_PLAYER_LIMIT = 8,
		BEGIN_BUTTON_LOCKOUT_FRAMES = 24,
		MIN_PRESET_CRAFT_CATEGORY = 1,
		MAX_STANDARD_PRESET_CRAFT_CATEGORY = 2,
		SPECIAL_PRESET_CRAFT_CATEGORY = 3,
		SPECIAL_PRESET_CRAFT_OFFSET = 5,
	};

	int* packet;
	int* payload;
	uint8_t* payloadBytes;
	uint8_t* chatChunkBytes;
	uint32_t packetSize;
	DPID senderPlayerId;
	NetPlayerInfo* netPlayer;
	int packetType;
	int packetWordIndex;
	int rosterIndex;
	int playerIndex;
	int teamIndex;
	int slotIndex;
	int sourceSlot;
	int destinationSlot;
	int readyPlayerCount;
	int bytesToDiscard;
	int chunkIndex;
	int chatOffset;
	int remainingBytes;
	int statsCount;
	int craftOption;
	int moviePlayerIndex;
	int assignmentIndex;

	Net_GetHostPlayerId();
	packet = Net_GetNextAppPacket(&senderPlayerId, &packetSize);
	if (Net_DidReadyPlayerLeaveThisFrame() != 0)
		MissionSetup_BroadcastStatePacket(0);
	if (packet == NULL)
		return 0;

	packetType = packet[0];
	g_frontendNetPacketSenderPlayerId = senderPlayerId;
	payload = &packet[1];
	payloadBytes = (uint8_t*)payload;
	switch (packetType) {
		case NET_PACKET_FRONTEND_GAME_STARTED:
		case NET_PACKET_PROBE_RESPONSE:
			g_frontendNetProbeResponseType = payload[0];
			g_frontendNetProbeVersion = payload[1];
			g_frontendNetProbePasswordRequired = payload[2];
			break;

		case NET_PACKET_PROBE_REQUEST:
			MissionSetup_BroadcastStatePacket(senderPlayerId);
			g_frontendNetPacketScratch.packetType = NET_PACKET_PROBE_RESPONSE;
			*(int*)&g_frontendNetPacketScratch.payload[0] = 0;
			*(int*)&g_frontendNetPacketScratch.payload[4] = FRONTEND_NET_PROTOCOL_VERSION;
			*(int*)&g_frontendNetPacketScratch.payload[8] = g_gameConfig.requirePassword;
			Net_SendPacketAndFlush(senderPlayerId, &g_frontendNetPacketScratch, 4 * sizeof(int));
			break;

		case NET_PACKET_STATE:
			Net_ClearPlayerReadyFlags();
			memcpy(g_pilotData.multiplayerGameName, payload, sizeof(g_pilotData.multiplayerGameName));
			g_frontendNetReceivedMissionDirectoryId = payload[9];
			g_frontendNetReceivedMissionDescriptionId = payload[10];
			g_frontendNetProbePlayersNeeded = payload[11] - payload[12];
			readyPlayerCount = payload[12];
			packetWordIndex = ROSTER_PACKET_FIRST_PLAYER_WORD;
			memset(g_mpRoster, 0, sizeof(g_mpRoster));
			for (rosterIndex = 0; rosterIndex < readyPlayerCount; ++rosterIndex) {
				if (payload[packetWordIndex] == 0) {
					++packetWordIndex;
					g_mpRoster[rosterIndex].playerId = 0;
					g_mpRoster[rosterIndex].pilotRating = payload[packetWordIndex++];
					++packetWordIndex;
				} else {
					netPlayer = Net_FindPlayer(payload[packetWordIndex]);
					if (netPlayer != NULL) {
						Net_MarkPlayerReadyNoLock(payload[packetWordIndex]);
						strncpy(g_mpRoster[rosterIndex].name, netPlayer->sessionName, PLAYER_NAME_COPY_SIZE);
						g_mpRoster[rosterIndex].playerId = netPlayer->playerId;
						++packetWordIndex;
						g_mpRoster[rosterIndex].pilotRating = payload[packetWordIndex];
						++packetWordIndex;
						if (Net_IsHost() == 0)
							Net_SetPlayerLatencyMs(g_mpRoster[rosterIndex].playerId,
												   payload[packetWordIndex]);
						++packetWordIndex;
					} else {
						memcpy(g_mpRoster[rosterIndex].name, "No name", sizeof("No name"));
						g_mpRoster[rosterIndex].playerId = payload[packetWordIndex];
						++packetWordIndex;
						g_mpRoster[rosterIndex].pilotRating = payload[packetWordIndex];
						packetWordIndex += 2;
					}
				}
			}
			break;

		case NET_PACKET_JOIN_REQUEST:
#ifdef XVT_MODERN
			if (!Net_IsHost() || packetSize != 6 * sizeof(int)) {
				packetType = NET_PACKET_NONE;
				break;
			}
#endif
			readyPlayerCount = Net_CountReadyPlayers();
			if (g_missionSetupRosterAuthoritative != 0) {
				g_frontendNetPacketScratch.packetType = NET_PACKET_ROSTER_LOCKED;
				Net_SendPacketAndFlush(senderPlayerId, &g_frontendNetPacketScratch, sizeof(int));
			} else if (readyPlayerCount >= MAX_PLAYERS) {
				g_frontendNetPacketScratch.packetType = NET_PACKET_GAME_FULL;
				Net_SendPacketAndFlush(senderPlayerId, &g_frontendNetPacketScratch, sizeof(int));
			} else if (payload[0] != FRONTEND_NET_PROTOCOL_VERSION) {
				g_frontendNetPacketScratch.packetType = NET_PACKET_VERSION_MISMATCH;
				Net_SendPacketAndFlush(senderPlayerId, &g_frontendNetPacketScratch, sizeof(int));
			} else if (g_gameConfig.requirePassword != 0 &&
					   strncmp(g_gameConfig.password, (const char*)&payload[1],
							   sizeof(g_gameConfig.password)) != 0) {
				g_frontendNetPacketScratch.packetType = NET_PACKET_PASSWORD_REQUIRED;
				Net_SendPacketAndFlush(senderPlayerId, &g_frontendNetPacketScratch, sizeof(int));
			} else if (Net_SetPlayerReady(senderPlayerId) != 0) {
				*(int*)&g_frontendNetPacketScratch.payload[0] = senderPlayerId;
				g_missionSetupBeginButtonLockoutFrames = BEGIN_BUTTON_LOCKOUT_FRAMES;
				g_frontendNetPacketScratch.packetType = NET_PACKET_PLAYER_ADMITTED;
				Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, 2 * sizeof(int));
				MissionSetup_BroadcastStatePacket(0);
			} else {
				g_frontendNetPacketScratch.packetType = NET_PACKET_GAME_FULL;
				Net_SendPacketAndFlush(senderPlayerId, &g_frontendNetPacketScratch, sizeof(int));
			}
			break;

		case NET_PACKET_GAME_FULL:
		case NET_PACKET_HOST_CANCELLED:
		case NET_PACKET_PLAYER_KICKED:
		case NET_PACKET_SESSION_CANCELLED:
		case NET_PACKET_VERSION_MISMATCH:
		case NET_PACKET_PASSWORD_REQUIRED:
		case NET_PACKET_ROSTER_LOCKED:
		case NET_PACKET_FLIGHT_ASSIGNMENTS_READY:
			break;

		case NET_PACKET_PLAYER_ADMITTED:
#ifdef XVT_MODERN
			if (packetSize < 2 * sizeof(int) || senderPlayerId != (DPID)Net_GetHostPlayerId() ||
				(XvtNetworkSession_GetStatus().state == XVT_NETWORK_SESSION_ADMISSION &&
				 !XvtNetworkSession_Admission(senderPlayerId, (DPID)payload[0]))) {
				packetType = NET_PACKET_NONE;
				break;
			}
#endif
			for (rosterIndex = 0; rosterIndex < MAX_PLAYERS; ++rosterIndex) {
				if (g_mpRoster[rosterIndex].playerId != 0 &&
					g_mpRoster[rosterIndex].playerId == Net_GetLocalPlayerId()) {
					break;
				}
			}
			if (rosterIndex >= MAX_PLAYERS && Net_GetLocalPlayerId() != payload[0]) {
				packetType = NET_PACKET_NONE;
				break;
			}
			if (g_gameConfig.sfxDatapadEnabled != 0)
				FrontendSound_PlayUISound("newpsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
			Net_MarkPlayerReadyNoLock(payload[0]);
			break;

		case NET_PACKET_PLAYER_LEFT:
			readyPlayerCount = Net_CountReadyPlayers();
			rosterIndex = 0;
			if (readyPlayerCount > 0) {
				do {
					if ((DPID)g_mpRoster[rosterIndex].playerId == senderPlayerId) {
						if (g_gameConfig.sfxDatapadEnabled != 0)
							FrontendSound_PlayUISound("exitpsound", 1, 0, 255,
													  12 * g_gameConfig.sfxDatapadVolume, 63);
						memset(g_mpRosterReadyFlags, 0, sizeof(g_mpRosterReadyFlags));
						Net_ClearPlayerReadyFlagWithLockGuard(senderPlayerId);
						MissionSetup_BroadcastStatePacket(0);
						break;
					}
					++rosterIndex;
				} while (rosterIndex < readyPlayerCount);
			}
			g_frontendNetPacketScratch.packetType = NET_PACKET_MOVIE_SYNC;
			*(int*)&g_frontendNetPacketScratch.payload[0] = 2;
			*(int*)&g_frontendNetPacketScratch.payload[4] = senderPlayerId;
			Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, 3 * sizeof(int));
			break;

		case NET_PACKET_TEAM_ASSIGNMENTS_READY:
			memcpy(&g_missionSetupPlayerAssignments, payload, MISSION_ASSIGNMENT_TEAM_BYTES);
			break;

		case NET_PACKET_FRONTEND_MISSION_START:
			g_pilotData.missionDirectoryId = (MissionDirectoryId)payload[1];
			g_pilotData.missionSequenceActive = payload[2];
			if (g_pilotData.missionSequenceActive == 1) {
				g_pilotData.missionSequenceDescriptionId =
					g_pilotData.missionDescriptionIds[g_pilotData.missionDirectoryId];
				if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES)
					g_pilotData.meleeTournamentSequenceState.missionCount = payload[3];
				else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_COMBAT_ENGAGEMENTS)
					g_pilotData.battleSequenceState.victoriesNeeded = payload[3];
				else
					g_pilotData.campaignSequenceState.missionCount = payload[3];
			}
			g_pilotData.missionDescriptionIds[g_pilotData.missionDirectoryId] = payload[0];
			MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
			if (g_missionList != NULL) {
				g_selectedMissionListIndex = 0;
				while ((unsigned int)g_selectedMissionListIndex < g_missionCount &&
					   g_missionList[g_selectedMissionListIndex].missionIdx !=
						   g_pilotData.missionDescriptionIds[g_pilotData.missionDirectoryId])
					++g_selectedMissionListIndex;
			}
			break;

		case NET_PACKET_NEXT_TOURNAMENT_MISSION:
		case NET_PACKET_NEXT_BATTLE_MISSION:
		case NET_PACKET_REPLAY_CURRENT_MISSION:
		case NET_PACKET_NEXT_CAMPAIGN_MISSION:
		case NET_PACKET_REPLAY_CAMPAIGN_MISSION:
			g_gameConfig.randomSeed = payload[0];
			break;

		case NET_PACKET_CHAT:
			if (g_frontendChatLogBuffer != NULL) {
				packetSize -= sizeof(int);
				bytesToDiscard = CHAT_LOG_CONTENT_LIMIT;
				bytesToDiscard -= g_frontendChatLogUsedBytes;
				bytesToDiscard -= (int)packetSize;
				if (bytesToDiscard < 0) {
					bytesToDiscard = -bytesToDiscard;
					memmove(g_frontendChatLogBuffer, &g_frontendChatLogBuffer[bytesToDiscard],
							CHAT_LOG_CAPACITY - bytesToDiscard);
					g_frontendChatLogUsedBytes -= bytesToDiscard;
				}
				memcpy(&g_frontendChatLogBuffer[g_frontendChatLogUsedBytes], payload, packetSize);
				if ((DPID)Net_GetLocalPlayerId() == senderPlayerId)
					g_frontendChatLogBuffer[g_frontendChatLogUsedBytes] = 5;
				g_frontendChatLogUsedBytes += packetSize;
				g_frontendChatLogBuffer[g_frontendChatLogUsedBytes++] = '\n';
				g_frontendChatLogBuffer[g_frontendChatLogUsedBytes] = '\0';
			}
			break;

		case NET_PACKET_TEAM_ASSIGNMENT:
			if (Net_IsHost() != 0)
				break;
			if (payload[1] == 10) {
				for (playerIndex = 0; playerIndex < MAX_PLAYERS; ++playerIndex) {
					if (g_missionSetupPlayerAssignments.activePlayerIds[playerIndex] == payload[0]) {
						g_missionSetupPlayerAssignments.activePlayerIds[playerIndex] = 0;
						break;
					}
				}
				for (teamIndex = 0; teamIndex < TEAM_COUNT; ++teamIndex) {
					for (slotIndex = 0; slotIndex < MAX_PLAYERS; ++slotIndex) {
						if (g_missionSetupPlayerAssignments.teamPlayerIds[teamIndex][slotIndex] ==
							payload[0]) {
							for (sourceSlot = slotIndex + 1; sourceSlot < MAX_PLAYERS; ++sourceSlot)
								g_missionSetupPlayerAssignments.teamPlayerIds[teamIndex][sourceSlot - 1] =
									g_missionSetupPlayerAssignments.teamPlayerIds[teamIndex][sourceSlot];
							g_missionSetupPlayerAssignments.teamPlayerIds[teamIndex][MAX_PLAYERS - 1] = 0;
							break;
						}
					}
				}
				break;
			}
			for (rosterIndex = 0; rosterIndex < MAX_PLAYERS; ++rosterIndex) {
				if (g_mpRoster[rosterIndex].playerId != 0 &&
					g_mpRoster[rosterIndex].playerId == Net_GetLocalPlayerId()) {
					break;
				}
			}
			if (rosterIndex < MAX_PLAYERS && g_gameConfig.sfxDatapadEnabled != 0)
				FrontendSound_PlayUISound("slotsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
			for (playerIndex = 0; playerIndex < MAX_PLAYERS; ++playerIndex) {
				if (g_missionSetupPlayerAssignments.activePlayerIds[playerIndex] == payload[0])
					break;
			}
			if (playerIndex == MAX_PLAYERS) {
				for (playerIndex = 0; playerIndex < MAX_PLAYERS; ++playerIndex) {
					if (g_missionSetupPlayerAssignments.activePlayerIds[playerIndex] == 0) {
						g_missionSetupPlayerAssignments.activePlayerIds[playerIndex] = payload[0];
						break;
					}
				}
			}
			for (slotIndex = 0; slotIndex < MAX_PLAYERS; ++slotIndex) {
				if (g_missionSetupPlayerAssignments.teamPlayerIds[payload[1]][slotIndex] == payload[0])
					break;
			}
			if (slotIndex == MAX_PLAYERS) {
				destinationSlot = payload[2];
				for (sourceSlot = MAX_PLAYERS - 1; sourceSlot > destinationSlot; --sourceSlot)
					g_missionSetupPlayerAssignments.teamPlayerIds[payload[1]][sourceSlot] =
						g_missionSetupPlayerAssignments.teamPlayerIds[payload[1]][sourceSlot - 1];
				g_missionSetupPlayerAssignments.teamPlayerIds[payload[1]][destinationSlot] = payload[0];
			}
			break;

		case NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY:
			if (payload[2] != -1 && g_gameConfig.sfxDatapadEnabled != 0) {
				for (slotIndex = 0; slotIndex < MAX_PLAYERS; ++slotIndex) {
					if (g_missionSetupPlayerAssignments.teamPlayerIds[payload[0]][slotIndex] ==
						Net_GetLocalPlayerId())
						break;
				}
				if (slotIndex != MAX_PLAYERS)
					FrontendSound_PlayUISound("slotsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
			}
			/* Continue with the shared flight-group assignment. */

		case NET_PACKET_FLIGHT_ASSIGNMENT:
			g_missionSetupPlayerFlightGroupIndices[payload[0] * MAX_PLAYERS + payload[1]] = payload[2];
			break;

		case NET_PACKET_PLAYER_READY:
			for (rosterIndex = 0; rosterIndex < MAX_PLAYERS; ++rosterIndex) {
				if ((DPID)g_mpRoster[rosterIndex].playerId == senderPlayerId) {
					g_mpRosterReadyFlags[rosterIndex] = 1;
					break;
				}
			}
			break;

		case NET_PACKET_PLAYER_UNREADY:
			for (rosterIndex = 0; rosterIndex < MAX_PLAYERS; ++rosterIndex) {
				if ((DPID)g_mpRoster[rosterIndex].playerId == senderPlayerId) {
					g_mpRosterReadyFlags[rosterIndex] = 0;
					break;
				}
			}
			break;

		case NET_PACKET_RETURN_TO_SETUP:
			g_skipFrontendEntryMovie = 1;
			break;

		case NET_PACKET_CLEAR_TEAM_ASSIGNMENTS:
			memset(&g_missionSetupPlayerAssignments, 0, MISSION_ASSIGNMENT_TEAM_BYTES);
			memset(g_missionSetupPlayerAssignments.activePlayerIds, 0,
				   sizeof(g_missionSetupPlayerAssignments.activePlayerIds));
			break;

		case NET_PACKET_TEAM_ASSIGNMENTS:
			memcpy(g_missionSetupPlayerAssignments.teamPlayerIds, payload,
				   sizeof(g_missionSetupPlayerAssignments.teamPlayerIds));
			memcpy(g_missionSetupPlayerAssignments.activePlayerIds,
				   &payload[sizeof(g_missionSetupPlayerAssignments.teamPlayerIds) / sizeof(payload[0])],
				   sizeof(g_missionSetupPlayerAssignments.activePlayerIds));
			break;

		case NET_PACKET_CLEAR_FLIGHT_ASSIGNMENTS:
			memset(&g_missionSetupPlayerFlightGroupIndices[payload[0] * MAX_PLAYERS], 0xFF,
				   MAX_PLAYERS * sizeof(g_missionSetupPlayerFlightGroupIndices[0]));
			break;

		case NET_PACKET_FLIGHT_ASSIGNMENTS:
			memcpy(&g_missionSetupPlayerFlightGroupIndices[payload[0] * MAX_PLAYERS], &payload[1],
				   MAX_PLAYERS * sizeof(g_missionSetupPlayerFlightGroupIndices[0]));
			break;

		case NET_PACKET_MISSION_CHOICE:
			g_frontendNetPacketArg0 = payload[0];
			break;

		case NET_PACKET_BRIEFING_COUNTDOWN:
		case NET_PACKET_ASSIGNMENT_COUNTDOWN:
			g_frontendNetPacketArg0 = payload[0];
			break;

		case NET_PACKET_RETURN_TO_MISSION_SELECTION:
			break;

		case NET_PACKET_RELEASE_TEAM_RESERVATION:
		case NET_PACKET_RELEASE_FLIGHT_RESERVATION:
			g_frontendNetPacketArg0 = payload[0];
			break;

		case NET_PACKET_GAME_OPTIONS:
			for (rosterIndex = 0; rosterIndex < MAX_PLAYERS; ++rosterIndex) {
				if (g_mpRoster[rosterIndex].playerId != 0 &&
					g_mpRoster[rosterIndex].playerId == Net_GetLocalPlayerId()) {
					break;
				}
			}
			if (rosterIndex < MAX_PLAYERS) {
				g_gameConfig.difficulty = payloadBytes[0];
				g_gameConfig.collisions = payloadBytes[4];
				g_gameConfig.craftJumping = payloadBytes[8];
				g_gameConfig.randomSetup = payloadBytes[12];
				g_gameConfig.battleLengthIndex = payloadBytes[16];
				g_gameConfig.inProgressJoin = payloadBytes[24];
				g_gameConfig.craftSelection = payloadBytes[28];
				g_gameConfig.locatePlayers = payloadBytes[32];
				g_gameConfig.craftWaves = payloadBytes[36];
				g_gameConfig.missionTimeLimit = payloadBytes[40];
				g_gameConfig.lastTeamTimeLimitMinutes = payloadBytes[44];
				g_gameConfig.randomSeed = payload[12];
				g_gameConfig.asyncFlag = payloadBytes[52];
				g_gameConfig.aiOpponents = payloadBytes[56];
				g_gameConfig.serverUpdateRate = payloadBytes[60];
				g_gameConfig.combatBalance = payloadBytes[64];
				g_gameConfig.continueBattleOrCampaign = payloadBytes[68];
			}
			break;

		case NET_PACKET_REPLAY_MISSION:
			g_gameConfig.randomSeed = payload[0];
			break;

		case NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS:
			packetWordIndex = 0;
			for (rosterIndex = 0; rosterIndex < MAX_PLAYERS; ++rosterIndex) {
				g_mpRoster[rosterIndex].craftTypeOverride = payload[packetWordIndex++];
				g_mpRoster[rosterIndex].craftOptionIndex = payload[packetWordIndex++];
				g_mpRoster[rosterIndex].warheadOptionIndex = payload[packetWordIndex++];
				g_mpRoster[rosterIndex].beamOptionIndex = payload[packetWordIndex++];
				g_mpRoster[rosterIndex].countermeasureOptionIndex = payload[packetWordIndex++];
			}
			for (assignmentIndex = 0; assignmentIndex < FLIGHT_GROUP_ASSIGNMENT_COUNT; ++assignmentIndex)
				g_missionSetupPlayerFlightGroupIndices[assignmentIndex] =
					((uint8_t*)&payload[packetWordIndex])[assignmentIndex];
			break;

		case NET_PACKET_LOADOUT_ROSTER:
			packetWordIndex = 0;
			for (rosterIndex = 0; rosterIndex < MAX_PLAYERS; ++rosterIndex) {
				g_mpRoster[rosterIndex].craftTypeOverride = payload[packetWordIndex++];
				g_mpRoster[rosterIndex].craftOptionIndex = payload[packetWordIndex++];
				g_mpRoster[rosterIndex].warheadOptionIndex = payload[packetWordIndex++];
				g_mpRoster[rosterIndex].beamOptionIndex = payload[packetWordIndex++];
				g_mpRoster[rosterIndex].countermeasureOptionIndex = payload[packetWordIndex++];
			}
			/* Continue with the network statistics carried by this packet. */

		case NET_PACKET_LOBBY_SELECTION:
			statsCount = payload[12];
			packetWordIndex = 13;
			while (statsCount > 0) {
				if (Net_IsHost() == 0) {
					Net_SetPlayerLatencyMs(payload[packetWordIndex], payload[packetWordIndex + 2]);
					Net_SetPlayerPacketCount(payload[packetWordIndex], payload[packetWordIndex + 3]);
					Net_SetPlayerPacketDropCount(payload[packetWordIndex], payload[packetWordIndex + 4]);
					Net_SetPlayerPacketRetryCount(payload[packetWordIndex], payload[packetWordIndex + 5]);
				}
				packetWordIndex += 6;
				--statsCount;
			}
			break;

		case NET_PACKET_CRAFT_LOADOUT:
			for (rosterIndex = 0; rosterIndex < MAX_PLAYERS; ++rosterIndex) {
				if ((DPID)g_mpRoster[rosterIndex].playerId != senderPlayerId &&
					g_gameConfig.craftSelection != CRAFT_SELECTION_HOST_ONLY)
					continue;
				craftOption = payload[1];
				if (craftOption != 0) {
					if ((unsigned int)payload[0] < MIN_PRESET_CRAFT_CATEGORY)
						g_mpRoster[rosterIndex].craftTypeOverride = 0;
					else if ((unsigned int)payload[0] <= MAX_STANDARD_PRESET_CRAFT_CATEGORY)
						g_mpRoster[rosterIndex].craftTypeOverride = g_presetCraftTypes[craftOption];
					else if (payload[0] == SPECIAL_PRESET_CRAFT_CATEGORY)
						g_mpRoster[rosterIndex].craftTypeOverride =
							g_presetCraftTypes[craftOption + SPECIAL_PRESET_CRAFT_OFFSET];
					else
						g_mpRoster[rosterIndex].craftTypeOverride = 0;
				} else {
					g_mpRoster[rosterIndex].craftTypeOverride = 0;
				}
				g_mpRoster[rosterIndex].craftOptionIndex = payload[2] - 1;
				g_mpRoster[rosterIndex].warheadOptionIndex = payload[3];
				g_mpRoster[rosterIndex].beamOptionIndex = payload[4];
				g_mpRoster[rosterIndex].countermeasureOptionIndex = payload[5];
				if (g_gameConfig.craftSelection == CRAFT_SELECTION_HOST_ONLY) {
					g_missionSetupSelectedPresetCraftOptionIndex = payload[1];
					g_missionSetupSelectedFlightGroupCraftOptionIndex = payload[2];
					g_missionSetupSelectedWarheadOptionIndex = payload[3];
					g_missionSetupSelectedBeamOptionIndex = payload[4];
					g_missionSetupSelectedCountermeasureOptionIndex = payload[5];
					g_missionSetupSelectedWaveCountMinusOne = payload[6];
					g_missionSetupSelectedCraftCount = payload[7];
				}
			}
			break;

		case NET_PACKET_BRIEFING_ENTERED:
			++g_frontendMissionOpcode99Count;
			break;

		case NET_PACKET_TEAM_RESERVATION:
		case NET_PACKET_FLIGHT_RESERVATION:
			g_frontendNetPacketArg0 = payload[0];
			g_frontendNetPacketArg1 = payload[1];
			break;

		case NET_PACKET_PLAYER_UNAVAILABLE:
			if (Net_IsHost() != 0) {
				if (g_gameConfig.sfxDatapadEnabled != 0)
					FrontendSound_PlayUISound("exitpsound", 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume,
											  63);
				memset(g_mpRosterReadyFlags, 0, sizeof(g_mpRosterReadyFlags));
				Net_ClearPlayerReadyFlag(senderPlayerId);
				MissionSetup_BroadcastStatePacket(0);
			}
			packetType = NET_PACKET_NONE;
			break;

		case NET_PACKET_CHAT_SYNC_REQUEST:
			chunkIndex = 0;
			if (g_frontendChatLogBuffer != NULL) {
				remainingBytes = g_frontendChatLogUsedBytes;
				if (remainingBytes == 0) {
					g_frontendNetPacketScratch.packetType = NET_PACKET_CHAT_SYNC_CHUNK;
					*(int*)&g_frontendNetPacketScratch.payload[0] = 0;
					Net_SendPacketAndFlush(senderPlayerId, &g_frontendNetPacketScratch, 2 * sizeof(int));
				} else {
					chatOffset = 0;
					do {
						packetSize = CHAT_SYNC_CHUNK_SIZE;
						if (remainingBytes < (int)packetSize)
							packetSize = remainingBytes;
						g_frontendNetPacketScratch.packetType = NET_PACKET_CHAT_SYNC_CHUNK;
						*(int*)&g_frontendNetPacketScratch.payload[0] = chunkIndex++;
						memcpy(&g_frontendNetPacketScratch.payload[4], &g_frontendChatLogBuffer[chatOffset],
							   packetSize);
						remainingBytes -= packetSize;
						chatOffset += packetSize;
						Net_SendPacketAndFlush(senderPlayerId, &g_frontendNetPacketScratch,
											   packetSize + 2 * sizeof(int));
					} while (remainingBytes > 0);
				}
			}
			break;

		case NET_PACKET_CHAT_SYNC_CHUNK:
			chunkIndex = payload[0];
			packetSize -= 2 * sizeof(int);
			if (chunkIndex == 0) {
				memset(g_frontendChatLogBuffer, 0, CHAT_LOG_CAPACITY);
				g_frontendChatLogUsedBytes = 0;
			}
			chatChunkBytes = (uint8_t*)&payload[1];
			for (playerIndex = 0; playerIndex < (int)packetSize; ++playerIndex) {
				if (chatChunkBytes[playerIndex] <= 6)
					chatChunkBytes[playerIndex] = 6;
			}
			memcpy(&g_frontendChatLogBuffer[CHAT_SYNC_CHUNK_SIZE * chunkIndex], chatChunkBytes, packetSize);
			g_frontendChatLogUsedBytes += packetSize;
			break;

		case NET_PACKET_READY_ROSTER:
			Net_ClearPlayerReadyFlags();
			g_frontendNetProbePlayersNeeded = payload[0] - payload[1];
			readyPlayerCount = payload[1];
			packetWordIndex = ROSTER_PACKET_COMPACT_FIRST_PLAYER_WORD;
			memset(g_mpRoster, 0, sizeof(g_mpRoster));
			for (rosterIndex = 0; rosterIndex < readyPlayerCount; ++rosterIndex) {
				if (payload[packetWordIndex] == 0) {
					++packetWordIndex;
					g_mpRoster[rosterIndex].playerId = 0;
					g_mpRoster[rosterIndex].pilotRating = payload[packetWordIndex++];
					++packetWordIndex;
				} else {
					netPlayer = Net_FindPlayer(payload[packetWordIndex]);
					if (netPlayer != NULL) {
						Net_MarkPlayerReadyNoLock(payload[packetWordIndex]);
						strncpy(g_mpRoster[rosterIndex].name, netPlayer->sessionName, PLAYER_NAME_COPY_SIZE);
						g_mpRoster[rosterIndex].playerId = netPlayer->playerId;
						++packetWordIndex;
						g_mpRoster[rosterIndex].pilotRating = payload[packetWordIndex];
						++packetWordIndex;
						if (Net_IsHost() == 0)
							Net_SetPlayerLatencyMs(g_mpRoster[rosterIndex].playerId,
												   payload[packetWordIndex]);
						++packetWordIndex;
					} else {
						memcpy(g_mpRoster[rosterIndex].name, "No name", sizeof("No name"));
						g_mpRoster[rosterIndex].playerId = payload[packetWordIndex];
						++packetWordIndex;
						g_mpRoster[rosterIndex].pilotRating = payload[packetWordIndex];
						packetWordIndex += 2;
					}
				}
			}
			break;

		case NET_PACKET_PILOT_RATING:
			g_frontendNetPacketArg0 = payload[0];
			break;

		case NET_PACKET_BATTLE_PROGRESS:
			g_remoteBattleSequenceActive = payload[0];
			g_remoteBattleSequenceContinuationChoice = payload[1];
			g_remoteBattleLastCompletedMissionIndex = payload[2];
			g_remoteBattleRebelVictoryCount = payload[3];
			g_remoteBattleImperialVictoryCount = payload[4];
			break;

		case NET_PACKET_MOVIE_SYNC:
			for (moviePlayerIndex = 0; moviePlayerIndex < MAX_PLAYERS; ++moviePlayerIndex) {
				switch (payload[0]) {
					case 0:
						if ((DPID)g_movieMultiplayerSyncPlayers[moviePlayerIndex].playerId ==
							senderPlayerId) {
							g_movieMultiplayerSyncPlayers[moviePlayerIndex].isWaiting = 1;
							moviePlayerIndex = MAX_PLAYERS;
						}
						break;

					case 1:
						g_movieMultiplayerSyncPlayers[moviePlayerIndex].isWaiting = 1;
						break;

					case 2:
						if (payload[1] == g_movieMultiplayerSyncPlayers[moviePlayerIndex].playerId) {
							g_movieMultiplayerSyncPlayers[moviePlayerIndex].playerId = 0;
							moviePlayerIndex = MAX_PLAYERS;
						}
						break;
				}
			}
			break;

		case NET_PACKET_SUBMIT_MISSION_CHOICE:
			g_frontendNetPacketScratch.packetType = NET_PACKET_MISSION_CHOICE;
			*(int*)&g_frontendNetPacketScratch.payload[0] = payload[0];
			Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, 2 * sizeof(int));
			break;

		default:
			packetType = NET_PACKET_NONE;
			break;
	}
	return packetType;
}
