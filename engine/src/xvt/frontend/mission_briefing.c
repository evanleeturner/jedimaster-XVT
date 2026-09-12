#include "xvt/frontend/mission_briefing.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/frontend_cleanup.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/mission_dialogs.h"
#endif

#include "xvt/assets/model_preview.h"
#include "xvt/frontend/briefing_map.h"
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
#include "xvt/frontend/frontend_flight.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GLOBAL: XVT 0xAA60D0
int g_missionBriefingActive = 0;
// GLOBAL: XVT 0x665D7C
MissionBriefingCraftScreenFaction g_missionBriefingCraftScreenFaction = MISSION_BRIEFING_CRAFT_SCREEN_REBEL;
// GLOBAL: XVT 0xA91CA0
int g_mpRosterReadyFlags[8] = { 0 };
// GLOBAL: XVT 0xB69E30
FrontendMissionSessionMode g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
// GLOBAL: XVT 0x52C6D4
int g_briefingSkipPlayerAssignment = 0;
// GLOBAL: XVT 0x665D78
int g_missionBriefingTickNowMs = 0;
// GLOBAL: XVT 0x665D80
MissionBriefingLaunchCountdownState g_missionBriefingLaunchCountdownState = MISSION_BRIEFING_COUNTDOWN_IDLE;
// GLOBAL: XVT 0x665D84
int g_missionBriefingLaunchCountdownMs = 0;
// GLOBAL: XVT 0x665D88
int g_missionBriefingUnusedState = 0;
// GLOBAL: XVT 0x665D8C
int g_missionBriefingLastTickMs = 0;
// GLOBAL: XVT 0x665D90
int g_missionBriefingLastCountdownSecondSent = 0;

// FUNCTION: XVT 0x4EAA90
int MissionBriefing_Exit(int frameCounter) {
	(void)frameCounter;

	if (g_missionList != NULL) {
		free(g_missionList);
		g_missionList = NULL;
	}
	if (g_briefingText != NULL) {
		free(g_briefingText);
		g_briefingText = NULL;
	}
	if (g_shipList != NULL) {
		free(g_shipList);
		g_shipList = NULL;
	}
	ModelPreview_SetNodeSwitchIndex(0);
	g_missionBriefingActive = 0;
	FrontImage_FreeResourceByName("background");
	Frontend_ResetScrollableControls();
	FrontendMouse_ClearInputGate();
	return 0;
}

// FUNCTION: XVT 0x4EAB20
int MissionBriefing_Update(int frameCounter) {
	enum {
		MAX_PLAYERS = 8,
		LAUNCH_COUNTDOWN_MS = 60000,
		PACKET_COUNTDOWN = 'd',
	};

	int teamIndex;
	int textIndex;
	int craftType;
	int craftSelectable;
	int armamentSelectableCount;
	int configurationAllowed;
	int packetType;
	int rosterIndex;
	int slotIndex;
	int actionTriggered;
	int packetCountdownMs;
	RECT rect;
	RECT savedClipRect;

	if (frameCounter == 0) {
		FrontendCursor_SetPos(37, 445);
		g_missionBriefingUnusedState = 0;
		g_briefingSkipPlayerAssignment = 0;
		g_missionBriefingLaunchCountdownState = MISSION_BRIEFING_COUNTDOWN_IDLE;
		g_missionBriefingActive = 1;

		if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES) {
			for (teamIndex = 0; teamIndex < g_teamCount; ++teamIndex) {
				if (g_teamFgCountScratch[teamIndex] > 1) {
					break;
				}
			}
			if (teamIndex == g_teamCount) {
				g_briefingSkipPlayerAssignment = 1;
			}
		}

		MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
		if (g_missionList != NULL) {
			g_selectedMissionListIndex = 0;
			if (g_missionCount > 0) {
				do {
					if (g_missionList[g_selectedMissionListIndex].missionIdx ==
						g_pilotData.missionDescriptionIds[g_pilotData.missionDirectoryId]) {
						break;
					}
					++g_selectedMissionListIndex;
				} while (g_missionCount > (unsigned int)g_selectedMissionListIndex);
			}
		}

		MissionSetup_InitCraftLoadout();
		ShipList_Load();
		craftType = MissionSetup_GetCraftType(-1);
		ModelPreview_LoadModel(g_shipList[g_shipTypeToShipListIndex[craftType]].modelFileName);
		ModelPreview_SetNodeSwitchIndex(
			g_frontendMission.flightGroups[g_missionSetupSelectedFlightGroupIndex].markings);
		ModelPreview_SetWhiteDirectionalLight(-1, 0, 1);
		ModelPreview_SetObjectAngleDDegrees(0.0f);

		if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
			g_frontendQuickStartLaunchFlag == 1) {
			if (g_missionSetupSelectedPresetCraftOptionIndex == 0) {
				g_mpRoster[0].craftTypeOverride = 0;
			} else {
				g_mpRoster[0].craftTypeOverride = MissionSetup_GetCraftType(-1);
			}
			g_mpRoster[0].pilotRating = g_pilotData.rating;
			g_mpRoster[0].warheadOptionIndex = g_missionSetupSelectedWarheadOptionIndex;
			g_mpRoster[0].beamOptionIndex = g_missionSetupSelectedBeamOptionIndex;
			g_mpRosterReadyFlags[0] = 1;
			g_mpRoster[0].craftOptionIndex = g_missionSetupSelectedFlightGroupCraftOptionIndex - 1;
			g_mpRoster[0].countermeasureOptionIndex = g_missionSetupSelectedCountermeasureOptionIndex;
			FrontendMission_InitPlayerState();
			FrontendScreen_SetCallbacks(FlightLoading_GetReadyScreen, NULL);
			return 0;
		}

		if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
				g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
				craftType = MissionSetup_GetCraftType(-1);
				if (craftType < 1 || (craftType > 4 && craftType != 14)) {
					g_missionBriefingCraftScreenFaction = MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL;
					FrontImage_RegisterResourceDefault("frontres\\craftsi.bmp", "background");
				} else {
					g_missionBriefingCraftScreenFaction = MISSION_BRIEFING_CRAFT_SCREEN_REBEL;
					FrontImage_RegisterResourceDefault("frontres\\craftsr.bmp", "background");
				}
			} else if (g_pilotData.currentFactionId == 0) {
				g_missionBriefingCraftScreenFaction = MISSION_BRIEFING_CRAFT_SCREEN_REBEL;
				FrontImage_RegisterResourceDefault("frontres\\craftsr.bmp", "background");
			} else {
				g_missionBriefingCraftScreenFaction = MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL;
				FrontImage_RegisterResourceDefault("frontres\\craftsi.bmp", "background");
			}
		} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
				   g_pilotData.missionDirectoryId == MISSION_DIRECTORY_BATTLES) {
			if (g_pilotData.team == 0) {
				g_missionBriefingCraftScreenFaction = MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL;
				FrontImage_RegisterResourceDefault("frontres\\craftmi.bmp", "background");
			} else {
				g_missionBriefingCraftScreenFaction = MISSION_BRIEFING_CRAFT_SCREEN_REBEL;
				FrontImage_RegisterResourceDefault("frontres\\craftmr.bmp", "background");
			}
		} else {
			craftType = MissionSetup_GetCraftType(-1);
			if (craftType >= 1 && (craftType <= 4 || craftType == 14)) {
				g_missionBriefingCraftScreenFaction = MISSION_BRIEFING_CRAFT_SCREEN_REBEL;
				FrontImage_RegisterResourceDefault("frontres\\craftmr.bmp", "background");
			} else {
				g_missionBriefingCraftScreenFaction = MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL;
				FrontImage_RegisterResourceDefault("frontres\\craftmi.bmp", "background");
			}
		}

		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("background", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		if (g_hostCdAvailable != 0) {
			FrontImage_DrawSprite("allactive", 0, 0);
		} else {
			FrontImage_DrawSprite("clientactive", 0, 0);
		}
		if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			FrontImage_DrawSpriteTranslucent("chatbox", 0, 0);
		}
		FrontImage_DrawSpriteTranslucent("regoverlay", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_ResetGlyphScratchBuffer(20);

		if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			if ((g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES &&
				 g_pilotData.missionSequenceActive == 1 &&
				 g_gameConfig.difficulty == GAME_DIFFICULTY_EASY_CHEAT) ||
				g_gameConfig.craftSelection != CRAFT_SELECTION_HOST_ONLY || Net_IsHost() != 0) {
				g_frontendNetPacketScratch.packetType = NET_PACKET_CRAFT_LOADOUT;
				*(int*)&g_frontendNetPacketScratch.payload[0] =
					g_frontendMission.flightGroups[g_missionSetupSelectedFlightGroupIndex]
						.optionalCraftCategory;
				*(int*)&g_frontendNetPacketScratch.payload[sizeof(int)] =
					g_missionSetupSelectedPresetCraftOptionIndex;
				*(int*)&g_frontendNetPacketScratch.payload[2 * sizeof(int)] =
					g_missionSetupSelectedFlightGroupCraftOptionIndex;
				*(int*)&g_frontendNetPacketScratch.payload[3 * sizeof(int)] =
					g_missionSetupSelectedWarheadOptionIndex;
				*(int*)&g_frontendNetPacketScratch.payload[4 * sizeof(int)] =
					g_missionSetupSelectedBeamOptionIndex;
				*(int*)&g_frontendNetPacketScratch.payload[5 * sizeof(int)] =
					g_missionSetupSelectedCountermeasureOptionIndex;
				*(int*)&g_frontendNetPacketScratch.payload[6 * sizeof(int)] =
					g_missionSetupSelectedWaveCountMinusOne;
				*(int*)&g_frontendNetPacketScratch.payload[7 * sizeof(int)] =
					g_missionSetupSelectedCraftCount;
				Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, 9 * sizeof(int));
			}
			g_frontendNetPacketScratch.packetType = NET_PACKET_BRIEFING_ENTERED;
			Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
								   sizeof(g_frontendNetPacketScratch.packetType));
		}
		g_missionBriefingLaunchCountdownMs = LAUNCH_COUNTDOWN_MS;
		g_missionBriefingTickNowMs = GetTickCount();
		g_missionBriefingLastTickMs = g_missionBriefingTickNowMs;
	}

	FrontendDraw_RectAssign(&rect, 158, 52, 491, 68);
	FrontendDisplay_GetScreenClipRect(&savedClipRect);
	FrontendDisplay_SetScreenClipRect640x480(&rect);
	sprintf(g_frontendScratchBuffer, "%c%s", 4, g_missionList[g_selectedMissionListIndex].description);
	for (textIndex = (int)strlen(g_frontendScratchBuffer) - 1; textIndex > 0; --textIndex) {
		if (g_frontendScratchBuffer[textIndex] == '(') {
			g_frontendScratchBuffer[textIndex] = '\0';
			break;
		}
	}
	FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, 0xFFFF);
	FrontendDisplay_SetScreenClipRect640x480(&savedClipRect);
	craftSelectable = 0;
	armamentSelectableCount = 0;

	configurationAllowed = 1;
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES &&
		g_pilotData.missionSequenceActive == 1 && g_gameConfig.difficulty != GAME_DIFFICULTY_EASY_CHEAT) {
		configurationAllowed = 0;
	}
	if (configurationAllowed != 0) {
		if (g_missionSetupFlightGroupCraftOptionCount > 1 || g_missionSetupPresetCraftOptionCount > 1) {
			craftSelectable = 1;
		}
		if (g_missionSetupWarheadOptionCount > 1) {
			armamentSelectableCount = 1;
		}
		if (g_missionSetupBeamOptionCount > 1) {
			craftType = MissionSetup_GetCraftType(-1);
			if (craftType < 1 || (craftType > 5 && craftType != 14)) {
				++armamentSelectableCount;
			}
		}
		if (g_missionSetupCountermeasureOptionCount > 1) {
			++armamentSelectableCount;
		}
	}

	FrontendDraw_RectAssign(&rect, 84, 90, 434, 108);
	if (craftSelectable != 0 || armamentSelectableCount != 0) {
		sprintf(g_frontendScratchBuffer, "%s %s %c%s", FrontendString_Get(FRONTSTR_263_CRAFT_CONFIGURATION),
				FrontendString_Get(FRONTSTR_604_FOR_FLIGHT_GROUP), 4,
				g_frontendMission.flightGroups[g_missionSetupSelectedFlightGroupIndex].name);
	} else {
		sprintf(g_frontendScratchBuffer, "%s %s %c%s", FrontendString_Get(FRONTSTR_606_CRAFT_REVIEW),
				FrontendString_Get(FRONTSTR_604_FOR_FLIGHT_GROUP), 4,
				g_frontendMission.flightGroups[g_missionSetupSelectedFlightGroupIndex].name);
	}
	FrontendText_DrawCentered(15, g_frontendScratchBuffer, &rect, 0xFFFF);

	if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		packetType = FrontendNet_ProcessNetworkPackets();
		if (packetType == NET_PACKET_HOST_CANCELLED) {
			Net_ShutdownDirectPlaySession();
			if (Net_IsHost() == 0) {
				FrontendDialog_ShowConfirmDialog(FrontendString_Get(FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
												 FrontendString_Get(FRONTSTR_632_CANCELLED_BY_THE_HOST),
												 FrontendString_Get(FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
												 NULL, NULL);
#ifdef XVT_MODERN
				return XvtDialog_ContinueWith(XvtMissionDialogs_Resume, XVT_MISSION_BRIEFING_CANCELLED);
#endif
			}
			if (Net_IsHost() != 0) {
				g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
				FrontendScreen_SetCallbacks(Concourse_Update, (FrontendScreenExitFn)Concourse_Exit);
			} else {
				g_skipFrontendEntryMovie = 1;
				g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_CLIENT;
				FrontendScreen_SetCallbacks(FrontendNet_JoinGameScreen,
											(FrontendScreenExitFn)FrontendMissionList_FreeScreenResources);
			}
		} else if (packetType == NET_PACKET_STATE) {
			MissionSetup_PruneFlightAssignments();
			g_frontendNetPacketScratch.packetType = NET_PACKET_CRAFT_LOADOUT;
			*(int*)&g_frontendNetPacketScratch.payload[0] =
				g_frontendMission.flightGroups[g_missionSetupSelectedFlightGroupIndex].optionalCraftCategory;
			*(int*)&g_frontendNetPacketScratch.payload[sizeof(int)] =
				g_missionSetupSelectedPresetCraftOptionIndex;
			*(int*)&g_frontendNetPacketScratch.payload[2 * sizeof(int)] =
				g_missionSetupSelectedFlightGroupCraftOptionIndex;
			*(int*)&g_frontendNetPacketScratch.payload[3 * sizeof(int)] =
				g_missionSetupSelectedWarheadOptionIndex;
			*(int*)&g_frontendNetPacketScratch.payload[4 * sizeof(int)] =
				g_missionSetupSelectedBeamOptionIndex;
			*(int*)&g_frontendNetPacketScratch.payload[5 * sizeof(int)] =
				g_missionSetupSelectedCountermeasureOptionIndex;
			*(int*)&g_frontendNetPacketScratch.payload[6 * sizeof(int)] =
				g_missionSetupSelectedWaveCountMinusOne;
			*(int*)&g_frontendNetPacketScratch.payload[7 * sizeof(int)] = g_missionSetupSelectedCraftCount;
			Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, 9 * sizeof(int));
		} else if (packetType == NET_PACKET_BRIEFING_ENTERED) {
			if (Net_IsHost() != 0 && g_gameConfig.craftSelection == CRAFT_SELECTION_HOST_ONLY &&
				Net_GetLocalPlayerId() != g_frontendNetPacketSenderPlayerId) {
				g_frontendNetPacketScratch.packetType = NET_PACKET_CRAFT_LOADOUT;
				*(int*)&g_frontendNetPacketScratch.payload[0] =
					g_frontendMission.flightGroups[g_missionSetupSelectedFlightGroupIndex]
						.optionalCraftCategory;
				*(int*)&g_frontendNetPacketScratch.payload[sizeof(int)] =
					g_missionSetupSelectedPresetCraftOptionIndex;
				*(int*)&g_frontendNetPacketScratch.payload[2 * sizeof(int)] =
					g_missionSetupSelectedFlightGroupCraftOptionIndex;
				*(int*)&g_frontendNetPacketScratch.payload[3 * sizeof(int)] =
					g_missionSetupSelectedWarheadOptionIndex;
				*(int*)&g_frontendNetPacketScratch.payload[4 * sizeof(int)] =
					g_missionSetupSelectedBeamOptionIndex;
				*(int*)&g_frontendNetPacketScratch.payload[5 * sizeof(int)] =
					g_missionSetupSelectedCountermeasureOptionIndex;
				*(int*)&g_frontendNetPacketScratch.payload[6 * sizeof(int)] =
					g_missionSetupSelectedWaveCountMinusOne;
				*(int*)&g_frontendNetPacketScratch.payload[7 * sizeof(int)] =
					g_missionSetupSelectedCraftCount;
				Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, 9 * sizeof(int));
			}
		} else if (packetType == NET_PACKET_RETURN_TO_SETUP) {
			g_skipFrontendEntryMovie = 1;
			FrontendScreen_SetCallbacks(MissionSetup_Update, (FrontendScreenExitFn)MissionSetup_Exit);
			return 0;
		} else if (packetType == NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS) {
			FrontendMission_InitPlayerState();
			FrontendScreen_SetCallbacks(FlightLoading_GetReadyScreen, NULL);
			return 0;
		} else if (packetType == NET_PACKET_CRAFT_LOADOUT) {
			if (g_gameConfig.craftSelection == CRAFT_SELECTION_HOST_ONLY && Net_IsHost() == 0) {
				craftType = MissionSetup_GetCraftType(-1);
				ModelPreview_LoadModel(g_shipList[g_shipTypeToShipListIndex[craftType]].modelFileName);
				ModelPreview_SetWhiteDirectionalLight(-1, 0, 1);
			}
		} else if (packetType == PACKET_COUNTDOWN) {
			packetCountdownMs = g_frontendNetPacketArg0;
			if (packetCountdownMs < g_missionBriefingLaunchCountdownMs) {
				g_missionBriefingLaunchCountdownMs = packetCountdownMs;
			}
		} else if (packetType == NET_PACKET_RELEASE_FLIGHT_RESERVATION) {
			for (slotIndex = 0; g_missionSetupReservedPlayerCount > slotIndex; ++slotIndex) {
				if (g_missionSetupReservedPlayerIds[slotIndex] == g_frontendNetPacketArg0) {
					--g_missionSetupReservedPlayerCount;
					break;
				}
			}
			for (; slotIndex < MAX_PLAYERS - 1; ++slotIndex) {
				g_missionSetupReservedPlayerIds[slotIndex] = g_missionSetupReservedPlayerIds[slotIndex + 1];
			}
			g_missionSetupReservedPlayerIds[MAX_PLAYERS - 1] = 0;
		} else if (packetType == NET_PACKET_FLIGHT_RESERVATION) {
			for (slotIndex = 0; g_missionSetupReservedPlayerCount > slotIndex; ++slotIndex) {
				if (g_missionSetupReservedPlayerIds[slotIndex] == g_frontendNetPacketArg0) {
					break;
				}
			}
			if (slotIndex == g_missionSetupReservedPlayerCount) {
				g_missionSetupReservedPlayerIds[g_missionSetupReservedPlayerCount++] =
					g_frontendNetPacketArg0;
			}
		} else if (packetType == NET_PACKET_PILOT_RATING) {
			for (rosterIndex = 0; rosterIndex < MAX_PLAYERS; ++rosterIndex) {
				if (g_mpRoster[rosterIndex].playerId == g_frontendNetPacketSenderPlayerId) {
					g_mpRoster[rosterIndex].pilotRating = g_frontendNetPacketArg0;
					break;
				}
			}
		}
	}

	MissionSetup_DrawCraftLoadout();
	if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		MissionSetup_DrawPlayerLoadouts(frameCounter);
		FrontendNet_UpdateAndDrawPanel(frameCounter);
	}

	FrontendDraw_RectAssign(&rect, 84, 419, 430, 434);
	if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
		g_gameConfig.craftSelection != CRAFT_SELECTION_OFF) {
		if (craftSelectable != 0 || armamentSelectableCount != 0) {
			if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
				g_gameConfig.craftSelection == CRAFT_SELECTION_ON) {
				strcpy(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_600_SELECT_YOUR));
			} else if (Net_IsHost() != 0) {
				strcpy(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_601_SELECT_EVERYBODY_S));
			} else {
				strcpy(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_602_HOST_IS_SELECTING));
			}
			if (craftSelectable != 0) {
				strcat(g_frontendScratchBuffer, " ");
				strcat(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_609_CRAFT));
			}
			if (armamentSelectableCount != 0) {
				if (craftSelectable != 0) {
					strcat(g_frontendScratchBuffer, " ");
					strcat(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_515_AND));
				}
				strcat(g_frontendScratchBuffer, " ");
				strcat(g_frontendScratchBuffer, FrontendString_Get(FRONTSTR_607_ARMAMENTS));
			}
			strcat(g_frontendScratchBuffer, ".");
			FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, g_colorGreen);
		} else {
			FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_608_REVIEW_YOUR_CRAFT_AND_ARMAMENTS),
									  &rect, g_colorRed);
		}
	} else {
		FrontendText_DrawCentered(12, FrontendString_Get(FRONTSTR_603_CRAFT_SELECTION_IS_DISABLED), &rect,
								  g_colorRed);
	}

	FrontendDraw_RectAssign(&rect, 200, 452, 436, 464);
	if (g_pilotData.name[0] != '\0') {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6, g_pilotData.ratingName, 1, g_pilotData.name);
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, g_colorLightBlue);
		if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES ||
			g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
			g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
			craftType = MissionSetup_GetCraftType(-1);
			if (craftType >= 1 && (craftType <= 4 || craftType == 14)) {
				sprintf(g_frontendScratchBuffer, "rebtiny%d", (frameCounter % 32) >> 1);
			} else {
				sprintf(g_frontendScratchBuffer, "imptiny%d", (frameCounter % 32) >> 1);
			}
		} else if (g_pilotData.team == 0) {
			sprintf(g_frontendScratchBuffer, "imptiny%d", (frameCounter % 32) >> 1);
		} else {
			sprintf(g_frontendScratchBuffer, "rebtiny%d", (frameCounter % 32) >> 1);
		}
		FrontImage_DrawSprite(g_frontendScratchBuffer, 204, 453);
		FrontImage_DrawSprite(g_frontendScratchBuffer, 420, 453);
	}

	if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendDraw_RectAssign(&rect, 507, 452, 562, 464);
		if (Net_CountReadyPlayers() <= g_frontendMissionOpcode99Count) {
			if (g_missionBriefingLaunchCountdownState == MISSION_BRIEFING_COUNTDOWN_IDLE) {
				g_missionBriefingTickNowMs = GetTickCount();
				g_missionBriefingLastTickMs = g_missionBriefingTickNowMs;
				g_missionBriefingLastCountdownSecondSent = LAUNCH_COUNTDOWN_MS / 1000;
				g_missionBriefingLaunchCountdownState = MISSION_BRIEFING_COUNTDOWN_ACTIVE;
			} else {
				Frontend_FormatSecondsToClockString(g_missionBriefingLaunchCountdownMs / 1000);
				FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, 0xFFFF);
			}
		}
	}

	FrontendDraw_RectAssign(&rect, 85, 447, 176, 471);
	if (g_gameConfig.helpOn != 0) {
		FrontendButton_EnableOverlayText();
	}
	if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (g_briefingSkipPlayerAssignment != 0) {
			if (g_missionSetupTeamAssignmentSkipped != 0) {
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_216_ABORT));
				actionTriggered = FrontendButton_DrawSpriteHitTest(
					&rect, "leaveup", "leavedown", FrontendString_Get(FRONTSTR_260_RETURN_TO_SELECT_MISSION),
					12, 0, 8, "buttonsound");
				if (actionTriggered != 0) {
					actionTriggered = FrontendDialog_ShowConfirmDialog(
						FrontendString_Get(FRONTSTR_752_ARE_YOU_SURE_YOU_WANT_TO),
						FrontendString_Get(FRONTSTR_753_RESTART_THE_GAME_AND_RETURN),
						FrontendString_Get(FRONTSTR_754_TO_SELECT_MISSION),
						FrontendString_Get(FRONTSTR_523_OKAY), FrontendString_Get(FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
					return XvtDialog_ContinueWith(XvtMissionDialogs_Resume, XVT_MISSION_SOLO_BACK_TO_SETUP);
#endif
				}
			} else {
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_569_PREVIOUS));
				actionTriggered = FrontendButton_DrawSpriteHitTest(
					&rect, "leaveup", "leavedown", FrontendString_Get(FRONTSTR_261_RETURN_TO_SELECT_TEAMS),
					12, 0, 8, "buttonsound");
			}
		} else {
			FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_569_PREVIOUS));
			actionTriggered = FrontendButton_DrawSpriteHitTest(
				&rect, "leaveup", "leavedown",
				FrontendString_Get(FRONTSTR_262_RETURN_TO_BRIEFING_AND_PILOT_ASSIGNMENT), 12, 0, 8,
				"buttonsound");
		}
		if (actionTriggered != 0) {
			if (g_briefingSkipPlayerAssignment != 0) {
				if (g_missionSetupTeamAssignmentSkipped != 0) {
					g_skipFrontendEntryMovie = 1;
					FrontendScreen_SetCallbacks(MissionSetup_Update, (FrontendScreenExitFn)MissionSetup_Exit);
				} else {
					FrontendScreen_SetCallbacks(MissionSetup_TeamAssignmentUpdate,

#ifdef XVT_MODERN
												XvtFrontendCleanup_MissionResources
#else
												(FrontendScreenExitFn)
													FrontendMissionList_FreeScreenResourcesAndClearInputGate
#endif
					);
				}
				return 0;
			}
			FrontendScreen_SetCallbacks(MissionSetup_FlightAssignmentUpdate,
										MissionSetup_FreeScreenResources);
			return 0;
		}
	} else if (Net_IsHost() != 0) {
		FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_668_RESTART));

#ifdef XVT_MODERN
		if (FrontendButton_DrawSpriteHitTest(&rect, "leaveup", "leavedown",
											 FrontendString_Get(FRONTSTR_260_RETURN_TO_SELECT_MISSION), 12, 0,
											 8, "buttonsound") != 0) {
			FrontendDialog_ShowConfirmDialog(FrontendString_Get(FRONTSTR_752_ARE_YOU_SURE_YOU_WANT_TO),
											 FrontendString_Get(FRONTSTR_753_RESTART_THE_GAME_AND_RETURN),
											 FrontendString_Get(FRONTSTR_754_TO_SELECT_MISSION),
											 FrontendString_Get(FRONTSTR_523_OKAY),
											 FrontendString_Get(FRONTSTR_019_CANCEL));
			return XvtDialog_ContinueWith(XvtMissionDialogs_Resume, XVT_MISSION_HOST_RESTART);
		}
#else
		if (FrontendButton_DrawSpriteHitTest(&rect, "leaveup", "leavedown",
											 FrontendString_Get(FRONTSTR_260_RETURN_TO_SELECT_MISSION), 12, 0,
											 8, "buttonsound") != 0 &&
			FrontendDialog_ShowConfirmDialog(FrontendString_Get(FRONTSTR_752_ARE_YOU_SURE_YOU_WANT_TO),
											 FrontendString_Get(FRONTSTR_753_RESTART_THE_GAME_AND_RETURN),
											 FrontendString_Get(FRONTSTR_754_TO_SELECT_MISSION),
											 FrontendString_Get(FRONTSTR_523_OKAY),
											 FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontendNetPacketScratch.packetType = NET_PACKET_RETURN_TO_SETUP;
			Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
								   sizeof(g_frontendNetPacketScratch.packetType));
		}
#endif

	} else {
		FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_204_LEAVE));

#ifdef XVT_MODERN
		if (FrontendButton_DrawSpriteHitTest(&rect, "leaveup", "leavedown",
											 FrontendString_Get(FRONTSTR_204_LEAVE), 12, 0, 8,
											 "buttonsound") != 0) {
			FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
				FrontendString_Get(FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
				FrontendString_Get(FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
				FrontendString_Get(FRONTSTR_523_OKAY), FrontendString_Get(FRONTSTR_019_CANCEL));
			return XvtDialog_ContinueWith(XvtMissionDialogs_Resume, XVT_MISSION_CLIENT_LEAVE);
		}
#else
		if (FrontendButton_DrawSpriteHitTest(&rect, "leaveup", "leavedown",
											 FrontendString_Get(FRONTSTR_204_LEAVE), 12, 0, 8,
											 "buttonsound") != 0 &&
			FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
				FrontendString_Get(FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
				FrontendString_Get(FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
				FrontendString_Get(FRONTSTR_523_OKAY), FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
			g_skipFrontendEntryMovie = 1;
			g_frontendNetPacketScratch.packetType = NET_PACKET_PLAYER_LEFT;
			Net_SendPacketAndFlush(Net_GetHostPlayerId(), &g_frontendNetPacketScratch,
								   sizeof(g_frontendNetPacketScratch.packetType));
			Net_ShutdownDirectPlaySession();
			FrontendScreen_SetCallbacks(FrontendNet_JoinGameScreen,
										(FrontendScreenExitFn)FrontendMissionList_FreeScreenResources);
		}
#endif
	}

	FrontendDraw_RectAssign(&rect, 8, 405, 71, 470);
	if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_200_FLY));
		if (FrontendButton_DrawSpriteHitTest(&rect, "flyup", "flydown", FrontendString_Get(FRONTSTR_200_FLY),
											 12, 0, 7, "flysound") != 0 &&
			g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			if (g_missionSetupSelectedPresetCraftOptionIndex == 0) {
				g_mpRoster[0].craftTypeOverride = 0;
			} else {
				g_mpRoster[0].craftTypeOverride = MissionSetup_GetCraftType(-1);
			}
			g_mpRoster[0].pilotRating = g_pilotData.rating;
			g_mpRoster[0].warheadOptionIndex = g_missionSetupSelectedWarheadOptionIndex;
			g_mpRoster[0].beamOptionIndex = g_missionSetupSelectedBeamOptionIndex;
			g_mpRosterReadyFlags[0] = 1;
			g_mpRoster[0].craftOptionIndex = g_missionSetupSelectedFlightGroupCraftOptionIndex - 1;
			g_mpRoster[0].countermeasureOptionIndex = g_missionSetupSelectedCountermeasureOptionIndex;
			FrontendMission_InitPlayerState();
			FrontendScreen_SetCallbacks(FlightLoading_GetReadyScreen, NULL);
			FrontendButton_DisableOverlayText();
			return 0;
		}
	} else {
		if (g_gameConfig.craftSelection == CRAFT_SELECTION_HOST_ONLY) {
			if (Net_IsHost() != 0 && Net_CountReadyPlayers() <= g_frontendMissionOpcode99Count) {
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_200_FLY));
				if (FrontendButton_DrawSpriteHitTest(&rect, "flyup", "flydown",
													 FrontendString_Get(FRONTSTR_200_FLY), 12, 0, 7,
													 "flysound") != 0) {
					MissionBriefing_BroadcastRosterAndAssignments();
				}
			}
		}
		if (g_gameConfig.craftSelection != CRAFT_SELECTION_HOST_ONLY) {
			for (rosterIndex = 0; rosterIndex < MAX_PLAYERS; ++rosterIndex) {
				if (Net_GetLocalPlayerId() == g_mpRoster[rosterIndex].playerId) {
					break;
				}
			}
			if (g_mpRosterReadyFlags[rosterIndex] != 0) {
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_575_RECONFIGURE));
				if (FrontendButton_DrawSpriteHitTest(&rect, "flydown", "flyup",
													 FrontendString_Get(FRONTSTR_575_RECONFIGURE), 12, 0, 7,
													 "flysound") != 0) {
					g_frontendNetPacketScratch.packetType = NET_PACKET_PLAYER_UNREADY;
					Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
										   sizeof(g_frontendNetPacketScratch.packetType));
				}
			} else {
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_574_READY));
				if (FrontendButton_DrawSpriteHitTest(&rect, "flyup", "flydown",
													 FrontendString_Get(FRONTSTR_574_READY), 12, 0, 7,
													 "flysound") != 0) {
					g_frontendNetPacketScratch.packetType = NET_PACKET_PLAYER_READY;
					Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
										   sizeof(g_frontendNetPacketScratch.packetType));
				}
			}
		}
	}

	FrontendButton_DisableOverlayText();
	if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER && Net_IsHost() != 0 &&
		MissionBriefing_AreAllNetworkPlayersReady() != 0) {
		MissionBriefing_BroadcastRosterAndAssignments();
	}
	if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		g_missionBriefingLaunchCountdownState == MISSION_BRIEFING_COUNTDOWN_ACTIVE) {
		g_missionBriefingTickNowMs = GetTickCount();
		g_missionBriefingLaunchCountdownMs += g_missionBriefingLastTickMs - g_missionBriefingTickNowMs;
		if (Net_IsHost() != 0 &&
			g_missionBriefingLastCountdownSecondSent != g_missionBriefingLaunchCountdownMs / 1000) {
			g_missionBriefingLastCountdownSecondSent = g_missionBriefingLaunchCountdownMs / 1000;
			*(int*)g_frontendNetPacketScratch.payload = g_missionBriefingLaunchCountdownMs;
			g_frontendNetPacketScratch.packetType = NET_PACKET_BRIEFING_COUNTDOWN;
			Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, 2 * sizeof(int));
		}
		if (g_missionBriefingLaunchCountdownMs < 0) {
			g_missionBriefingLaunchCountdownMs = 0;
			g_missionBriefingLaunchCountdownState = MISSION_BRIEFING_COUNTDOWN_EXPIRED;
			if (Net_IsHost() != 0) {
				MissionBriefing_BroadcastRosterAndAssignments();
			}
			return 0;
		}
		g_missionBriefingLastTickMs = g_missionBriefingTickNowMs;
	}

	MissionSetup_UpdateCraftLoadout();
	return Frontend_HandleCommonScreenControls(1) == 1;
}

// FUNCTION: XVT 0x4EEC10
int MissionBriefing_BroadcastRosterAndAssignments(void) {
	enum { PLAYER_SLOTS_PER_TEAM = 8 };

	uint8_t* output;
	unsigned int packetDwordIndex;
	unsigned int rosterIndex;
	unsigned int teamIndex;
	unsigned int playerIndex;
	int assignmentIndex;

	packetDwordIndex = 1;
	g_frontendNetPacketScratch.packetType = NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS;
	for (rosterIndex = 0; rosterIndex < sizeof(g_mpRoster) / sizeof(g_mpRoster[0]); ++rosterIndex) {
		memcpy(g_frontendNetPacketScratch.payload + (packetDwordIndex - 1) * sizeof(int),
			   &g_mpRoster[rosterIndex].craftTypeOverride, sizeof(g_mpRoster[rosterIndex].craftTypeOverride));
		++packetDwordIndex;
		memcpy(g_frontendNetPacketScratch.payload + (packetDwordIndex - 1) * sizeof(int),
			   &g_mpRoster[rosterIndex].craftOptionIndex, sizeof(g_mpRoster[rosterIndex].craftOptionIndex));
		++packetDwordIndex;
		memcpy(g_frontendNetPacketScratch.payload + (packetDwordIndex - 1) * sizeof(int),
			   &g_mpRoster[rosterIndex].warheadOptionIndex,
			   sizeof(g_mpRoster[rosterIndex].warheadOptionIndex));
		++packetDwordIndex;
		memcpy(g_frontendNetPacketScratch.payload + (packetDwordIndex - 1) * sizeof(int),
			   &g_mpRoster[rosterIndex].beamOptionIndex, sizeof(g_mpRoster[rosterIndex].beamOptionIndex));
		++packetDwordIndex;
		memcpy(g_frontendNetPacketScratch.payload + (packetDwordIndex - 1) * sizeof(int),
			   &g_mpRoster[rosterIndex].countermeasureOptionIndex,
			   sizeof(g_mpRoster[rosterIndex].countermeasureOptionIndex));
		++packetDwordIndex;
	}
	output = g_frontendNetPacketScratch.payload + (packetDwordIndex - 1) * sizeof(int);
	assignmentIndex = 0;
	for (teamIndex = 0;
		 teamIndex < sizeof(g_missionSetupPlayerFlightGroupIndices) /
						 (PLAYER_SLOTS_PER_TEAM * sizeof(g_missionSetupPlayerFlightGroupIndices[0]));
		 ++teamIndex) {
		for (playerIndex = 0; playerIndex < PLAYER_SLOTS_PER_TEAM; ++playerIndex) {
			*output = (uint8_t)g_missionSetupPlayerFlightGroupIndices[assignmentIndex];
			++output;
			++assignmentIndex;
		}
	}
	Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
						   packetDwordIndex * sizeof(int) +
							   sizeof(g_missionSetupPlayerFlightGroupIndices) /
								   sizeof(g_missionSetupPlayerFlightGroupIndices[0]));
	return 1;
}

// FUNCTION: XVT 0x4F68C0
int16_t MissionBriefing_HandleMapMouseInput(RECT* viewportRect, RECT* clipRect, int16_t suppressInput,
											int leftDown, int rightDown, int16_t mouseX, int16_t mouseY) {
	RECT rect;
	RECT dst;

	FrontendDraw_RectCopy(&dst, viewportRect);
	FrontendDraw_RectInsetXY(&dst, 1, 1);
	FrontendDraw_RectCopy(&rect, clipRect);
	FrontendDraw_RectInsetXY(&rect, 1, 1);
	if (suppressInput != 0) {
		return 0;
	}
	return BriefingMap_MouseInputStub(&dst, &rect, leftDown, rightDown, (int16_t)(mouseX - 1),
									  (int16_t)(mouseY - 1));
}

// FUNCTION: XVT 0x4F6970
int16_t MissionBriefing_DrawMapViewport(RECT* viewportRect, RECT* clipRect, int16_t highlightPhase) {
	RECT viewportCopy;
	RECT clipCopy;

	FrontendDraw_RectCopy(&viewportCopy, viewportRect);
	FrontendDraw_RectCopy(&clipCopy, clipRect);
	return BriefingMap_DrawViewportAndSelection(&viewportCopy, &clipCopy, highlightPhase);
}

// FUNCTION: XVT 0x4FACC0
int MissionBriefing_AreAllNetworkPlayersReady(void) {
	int readyFlagCount;
	int readyFlagIndex;
	int readyPlayerCount;

	readyFlagCount = 0;
	if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		return 0;
	}
	readyPlayerCount = Net_CountReadyPlayers();
	for (readyFlagIndex = 0; readyFlagIndex < 8; ++readyFlagIndex) {
		if (g_mpRosterReadyFlags[readyFlagIndex] != 0) {
			++readyFlagCount;
		}
	}
	readyFlagCount -= readyPlayerCount;
	return readyFlagCount == 0;
}
