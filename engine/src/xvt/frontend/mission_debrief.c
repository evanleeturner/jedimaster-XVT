#include "xvt/frontend/mission_debrief.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/campaign_task.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/frontend_cleanup.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/mission_dialogs.h"
#endif

#include "xvt/assets/file.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_flight.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GLOBAL: XVT 0x66D918
int g_debriefStatsPageNeedsRebuild = 0;
// GLOBAL: XVT 0x52D1C8
int g_debriefDisconnectedFromNetGame = 0;
// GLOBAL: XVT 0x66D9B8
int g_debriefSkipMissionConfirmPending = -1;
// GLOBAL: XVT 0x66D9E0
int g_briefingTab = 0;
// GLOBAL: XVT 0xA91B90
char g_nextMissionDescription[256] = { 0 };
// GLOBAL: XVT 0x66D8A0
int g_debriefTeamInStandings[10] = { 0 };
// GLOBAL: XVT 0x66D8D8
int g_debriefTeamUseSummaryRows[10] = { 0 };
// GLOBAL: XVT 0x66D900
int g_debriefLocalTeamRankIndex = 0;
// GLOBAL: XVT 0x66D904
int g_debriefActiveTeamCount = 0;
// GLOBAL: XVT 0x66D920
int g_debriefSortedTeamIds[10] = { 0 };
// GLOBAL: XVT 0x66D968
int g_debriefTeamHasPlayer[10] = { 0 };
// GLOBAL: XVT 0x66D990
int g_debriefSortedPlayerIds[8] = { 0 };
// GLOBAL: XVT 0x66D9B0
int g_debriefHasPlayerKillsByRating = 0;
// GLOBAL: XVT 0x66D9C0
int g_debriefKillsOnRankIds[8] = { 0 };
// GLOBAL: XVT 0x66D9F8
int g_debriefKillsFromRankIds[8] = { 0 };
// GLOBAL: XVT 0x66DA18
int g_debriefStandingsTeamIds[10] = { 0 };
// GLOBAL: XVT 0x66DA40
int g_debriefPlayerKillsSharedTotal[3] = { 0 };
// GLOBAL: XVT 0x66DA4C
int g_debriefRankByPilot = 0;
// GLOBAL: XVT 0x66D8C8
int g_debriefTotalKillsSharedByMissionType[4] = { 0 };
// GLOBAL: XVT 0x66D908
int g_debriefAssistTotalByMissionType[4] = { 0 };
// GLOBAL: XVT 0x66D948
int g_debriefPlayerKillsByMissionType[4] = { 0 };
// GLOBAL: XVT 0x66D958
int g_debriefNonPlayerKillsByMissionType[4] = { 0 };
// GLOBAL: XVT 0x66D9B4
int g_debriefHasCraftKillsByTypeSection = 0;
// GLOBAL: XVT 0x66D9E4
int g_debriefHasLossesFromPlayersSection = 0;
// GLOBAL: XVT 0x66D9E8
int g_debriefNonPlayerKillsSharedTotal[3] = { 0 };
// GLOBAL: XVT 0x66D9F4
int g_debriefCraftKillRowHasData = 0;
// GLOBAL: XVT 0x66DA50
int g_debriefLossesToNonPlayerPilotsTotal[1] = { 0 };
// GLOBAL: XVT 0x66DA60
int g_debriefLossesToPlayerPilotsTotal[1] = { 0 };
// GLOBAL: XVT 0x66DA6C
int g_debriefPlayerStatsScrollRow = 0;
// GLOBAL: XVT 0x66DA70
int g_debriefPlayerStatsRowCount = 0;

// FUNCTION: XVT 0x4FE100
int MissionDebrief_Exit(int frameCounter) {
	(void)frameCounter;

	if (g_missionList != NULL) {
		free(g_missionList);
		g_missionList = NULL;
	}
	if (g_briefingText != NULL) {
		free(g_briefingText);
		g_briefingText = NULL;
	}
	FrontImage_FreeResourceByName("background");
	Frontend_ResetScrollableControls();
	FrontendMouse_ClearInputGate();
	return 0;
}

// FUNCTION: XVT 0x4FE160
int MissionDebrief_Update(int frameCounter) {
	enum {
		PLAYER_COUNT = 8,
		BATTLE_RESULT_COUNT = 3,
		MISSION_TEXT_BUFFER_SIZE = 0x1000,
		TEXT_CODE_LABEL = 4,
		TEXT_CODE_RATING = 6,
		WHITE_COLOR = 0xFFFF,
		NETWORK_DISCONNECT_FRAME = 1,
		NETWORK_MESSAGE_FRAME_COUNT = 2,
	};

	int showSequenceContinueButton = 0;
	int localNetworkPlayerIndex = 0;
	int battleResultCounts[BATTLE_RESULT_COUNT];
	RECT rect;
#ifndef XVT_MODERN
	char longName[16];
#endif
	RECT savedClipRect;

	if (frameCounter == 0) {
#ifdef XVT_MODERN
		if (XvtCampaignTask_EnterDebrief() != 1)
			return 0;
#else
		FrontendCursor_Show();
		Keyboard_FlushCharBuffer();
		if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES &&
			g_pilotData.missionSequenceActive == 1) {
			if (g_pilotData.campaignSequenceState.lastMissionCompleted != 0) {
				int cutscenePlayed = Cutscene_PlayForCurrentMissionPhase(1);

				if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
					cutscenePlayed == 0) {
					g_frontendNetPacketScratch.packetType = NET_PACKET_PLAYER_LEFT;
					Net_SendPacketAndFlush(Net_GetHostPlayerId(), &g_frontendNetPacketScratch,
										   sizeof(g_frontendNetPacketScratch.packetType));
					Net_ShutdownDirectPlaySession();
					FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
					return 0;
				}
			}
			g_briefingText = malloc(MISSION_TEXT_BUFFER_SIZE);
		}

		g_frontendChatTeamOnly = 0;
		g_debriefDisconnectedFromNetGame = 0;
		g_frontendFirstVisibleLine = 0;
		for (localNetworkPlayerIndex = 0; localNetworkPlayerIndex < PLAYER_COUNT; ++localNetworkPlayerIndex) {
			if (g_pilotData.networkPlayers[localNetworkPlayerIndex].directPlayId != 0 &&
				g_pilotData.networkPlayers[localNetworkPlayerIndex].hasLeft != 0) {
				Net_ClearPlayerReadyFlagWithLockGuard(
					g_pilotData.networkPlayers[localNetworkPlayerIndex].directPlayId);
				if (Net_GetLocalPlayerId() ==
					g_pilotData.networkPlayers[localNetworkPlayerIndex].directPlayId)
					g_debriefDisconnectedFromNetGame = 1;
			}
		}
		Net_ResetRosterToLocalPlayerWithLockGuard();
		if (g_pilotData.promotionDelta != PILOT_PROMOTION_NONE &&
			g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			longName[0] = (char)(g_pilotData.rating + 1);
			longName[1] = 0;
			Net_SetPlayerNameWithLockGuard(Net_GetLocalPlayerId(), longName, g_pilotData.name);
		}
#endif

		{
			int useExitCursor = 1;

			if (g_pilotData.missionSequenceActive != 0 &&
				(Net_IsHost() != 0 ||
				 g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER)) {
				useExitCursor = 0;
				if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES) {
					if (g_pilotData.meleeTournamentSequenceState.missionCount -
							g_pilotData.meleeTournamentSequenceState.currentMissionIndex ==
						1) {
						useExitCursor = 1;
					}
				} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
					int missionIndex;

					battleResultCounts[BATTLE_MISSION_RESULT_IMPERIAL_VICTORY] = 0;
					battleResultCounts[BATTLE_MISSION_RESULT_REBEL_VICTORY] = 0;
					battleResultCounts[BATTLE_MISSION_RESULT_DRAW] = 0;
					for (missionIndex = 0;
						 missionIndex <= (int)g_pilotData.battleSequenceState.currentMissionIndex;
						 ++missionIndex) {
						++battleResultCounts[g_pilotData.battleSequenceState.missionResults[missionIndex]];
					}
					if (g_pilotData.battleSequenceState.victoriesNeeded ==
							battleResultCounts[BATTLE_MISSION_RESULT_IMPERIAL_VICTORY] ||
						g_pilotData.battleSequenceState.victoriesNeeded ==
							battleResultCounts[BATTLE_MISSION_RESULT_REBEL_VICTORY]) {
						useExitCursor = 1;
					}
				} else if (g_pilotData.campaignSequenceState.missionCount -
							   g_pilotData.campaignSequenceState.currentMissionIndex ==
						   1) {
					useExitCursor = 1;
				}
			}
			if (useExitCursor != 0)
				FrontendCursor_SetPos(149, 463);
			else
				FrontendCursor_SetPos(37, 445);
		}

		MissionDebrief_Prepare();
		switch (g_pilotData.missionDirectoryId) {
			case MISSION_DIRECTORY_TRAINING_EXERCISES:
				if (g_pilotData.missionSequenceActive == 0)
					g_briefingTab = 1;
				else
					g_briefingTab = 2;
				break;
			case MISSION_DIRECTORY_MELEES:
				if (g_pilotData.missionSequenceActive != 0 &&
					g_pilotData.meleeTournamentSequenceState.missionCount -
							g_pilotData.meleeTournamentSequenceState.currentMissionIndex ==
						1) {
					g_briefingTab = 2;
				} else {
					g_briefingTab = 0;
				}
				break;
			case MISSION_DIRECTORY_TOURNAMENTS:
				if (g_pilotData.meleeTournamentSequenceState.missionCount -
						g_pilotData.meleeTournamentSequenceState.currentMissionIndex ==
					1) {
					g_briefingTab = 2;
				} else {
					g_briefingTab = 0;
				}
				break;
			case MISSION_DIRECTORY_COMBAT_ENGAGEMENTS:
				if (g_pilotData.missionSequenceActive == 0)
					g_briefingTab = 1;
				else
					g_briefingTab = 2;
				break;
			case MISSION_DIRECTORY_BATTLES:
				g_briefingTab = 2;
				break;
			case MISSION_DIRECTORY_CAMPAIGNS:
				g_briefingTab = 1;
				break;
			default:
				break;
		}
		memset(g_mpRosterReadyFlags, 0, sizeof(g_mpRosterReadyFlags));
		g_debriefAssistTotalByMissionType[3] = 0;
		g_debriefSkipMissionConfirmPending = 0;
		g_debriefStatsPageNeedsRebuild = 1;
		g_nextMissionDescription[0] = 0;

		if (g_pilotData.missionSequenceActive == 1) {
			if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES)
				g_pilotData.missionDirectoryId = MISSION_DIRECTORY_CAMPAIGNS;
			else
				++g_pilotData.missionDirectoryId;
			MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
			if (g_missionList != NULL) {
				g_selectedMissionListIndex = 0;
				while ((unsigned int)g_selectedMissionListIndex < g_missionCount &&
					   g_missionList[g_selectedMissionListIndex].missionIdx !=
						   g_pilotData.missionDescriptionIds[g_pilotData.missionDirectoryId]) {
					++g_selectedMissionListIndex;
				}
				if ((unsigned int)g_selectedMissionListIndex < g_missionCount) {
					strcpy(g_nextMissionDescription, g_missionList[g_selectedMissionListIndex].description);
				}
			}

			if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_BATTLES) {
				if (g_pilotData.battleSequenceState.victoriesNeeded !=
						battleResultCounts[BATTLE_MISSION_RESULT_IMPERIAL_VICTORY] &&
					g_pilotData.battleSequenceState.victoriesNeeded !=
						battleResultCounts[BATTLE_MISSION_RESULT_REBEL_VICTORY]) {
					if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_pilotData
							.spBattleContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
							.isActive = 1;
						g_pilotData
							.spBattleContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
							.randomSeed = g_gameConfig.randomSeed;
						g_pilotData
							.spBattleContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
							.battleLengthIndex = (uint8_t)g_gameConfig.battleLengthIndex;
						g_pilotData
							.spBattleContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
							.randomSetup = g_gameConfig.randomSetup;
						memcpy(
							&g_pilotData
								 .spBattleContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
								 .state,
							&g_pilotData.battleSequenceState,
							sizeof(g_pilotData.spBattleContinuations[0].state));
					} else {
						g_pilotData
							.mpBattleContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
							.isActive = Net_IsHost() != 0;
						g_pilotData
							.mpBattleContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
							.randomSeed = g_gameConfig.randomSeed;
						g_pilotData
							.mpBattleContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
							.battleLengthIndex = (uint8_t)g_gameConfig.battleLengthIndex;
						g_pilotData
							.mpBattleContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
							.randomSetup = g_gameConfig.randomSetup;
						memcpy(
							&g_pilotData
								 .mpBattleContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
								 .state,
							&g_pilotData.battleSequenceState,
							sizeof(g_pilotData.mpBattleContinuations[0].state));
					}
				} else if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					g_pilotData.spBattleContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
						.isActive = 0;
				} else {
					g_pilotData.mpBattleContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
						.isActive = 0;
				}
			} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_CAMPAIGNS) {
				if (g_pilotData.campaignSequenceState.missionCount -
							g_pilotData.campaignSequenceState.currentMissionIndex !=
						1 ||
					g_pilotData.campaignSequenceState.lastMissionCompleted != 1) {
					if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_pilotData
							.spCampaignContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
							.isActive = 1;
						g_pilotData
							.spCampaignContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
							.randomSeed = g_gameConfig.randomSeed;
						g_pilotData
							.spCampaignContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
							.randomSetup = g_gameConfig.randomSetup;
						memcpy(&g_pilotData
									.spCampaignContinuations[g_missionList[g_selectedMissionListIndex]
																 .missionIdx]
									.state,
							   &g_pilotData.campaignSequenceState,
							   sizeof(g_pilotData.spCampaignContinuations[0].state));
					} else if (Net_IsHost() != 0) {
						g_pilotData
							.mpCampaignContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
							.isActive = 1;
						g_pilotData
							.mpCampaignContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
							.randomSeed = g_gameConfig.randomSeed;
						g_pilotData
							.mpCampaignContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
							.randomSetup = g_gameConfig.randomSetup;
						memcpy(&g_pilotData
									.mpCampaignContinuations[g_missionList[g_selectedMissionListIndex]
																 .missionIdx]
									.state,
							   &g_pilotData.campaignSequenceState,
							   sizeof(g_pilotData.mpCampaignContinuations[0].state));
					} else {
						g_pilotData
							.mpCampaignContinuations[g_missionList[g_selectedMissionListIndex].missionIdx +
													 12]
							.isActive = 0;
						g_pilotData
							.mpCampaignContinuations[g_missionList[g_selectedMissionListIndex].missionIdx +
													 12]
							.randomSeed = g_gameConfig.randomSeed;
						g_pilotData
							.mpCampaignContinuations[g_missionList[g_selectedMissionListIndex].missionIdx +
													 12]
							.randomSetup = g_gameConfig.randomSetup;
						memcpy(&g_pilotData
									.mpCampaignContinuations
										[g_missionList[g_selectedMissionListIndex].missionIdx + 12]
									.state,
							   &g_pilotData.campaignSequenceState,
							   sizeof(g_pilotData.mpCampaignContinuations[0].state));
					}
				} else if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					g_pilotData.spCampaignContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
						.isActive = 0;
				} else if (Net_IsHost() != 0) {
					g_pilotData.mpCampaignContinuations[g_missionList[g_selectedMissionListIndex].missionIdx]
						.isActive = 0;
				} else {
					g_pilotData
						.mpCampaignContinuations[g_missionList[g_selectedMissionListIndex].missionIdx + 12]
						.isActive = 0;
				}
			}

			if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_CAMPAIGNS)
				g_pilotData.missionDirectoryId = MISSION_DIRECTORY_TRAINING_EXERCISES;
			else
				--g_pilotData.missionDirectoryId;
		}

		MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
		if (g_missionList != NULL) {
			g_selectedMissionListIndex = 0;
			while ((unsigned int)g_selectedMissionListIndex < g_missionCount &&
				   g_missionList[g_selectedMissionListIndex].missionIdx !=
					   g_pilotData.missionDescriptionIds[g_pilotData.missionDirectoryId]) {
				++g_selectedMissionListIndex;
			}
		}
		if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES &&
			g_pilotData.missionSequenceActive == 1) {
			MissionDebrief_BuildText(g_briefingText, g_pilotData.campaignSequenceState.lastMissionCompleted);
		}
		MissionDebrief_MarkNetworkPlayersReady();
		localNetworkPlayerIndex = 0;
		if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			for (; localNetworkPlayerIndex < PLAYER_COUNT; ++localNetworkPlayerIndex) {
				if (Net_GetLocalPlayerId() ==
					g_pilotData.networkPlayers[localNetworkPlayerIndex].directPlayId) {
					break;
				}
			}
		}

		if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES) {
			int playerFlightGroup = g_pilotData.networkPlayers[localNetworkPlayerIndex].flightGroupId;

			if (g_pilotData.missionSequenceActive == 1) {
				if (g_pilotData.campaignSequenceState.lastMissionCompleted != 0) {
					if (g_frontendMission.flightGroups[playerFlightGroup].iff != 0)
						FrontImage_RegisterResourceDefault("frontres\\debcawi.bmp", "background");
					else
						FrontImage_RegisterResourceDefault("frontres\\debcawr.bmp", "background");
				} else {
					if (g_frontendMission.flightGroups[playerFlightGroup].iff != 0)
						FrontImage_RegisterResourceDefault("frontres\\debcali.bmp", "background");
					else
						FrontImage_RegisterResourceDefault("frontres\\debcalr.bmp", "background");
				}
			} else {
				int evaluation = g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[2];

				if (evaluation != 0)
					evaluation = (evaluation == 6) + 1;
				if (evaluation == 1) {
					if (g_frontendMission.flightGroups[playerFlightGroup].iff != 0)
						FrontImage_RegisterResourceDefault("frontres\\debtrwi.bmp", "background");
					else
						FrontImage_RegisterResourceDefault("frontres\\debtrwr.bmp", "background");
				} else if (evaluation == 2) {
					if (g_frontendMission.flightGroups[playerFlightGroup].iff != 0)
						FrontImage_RegisterResourceDefault("frontres\\debtrbi.bmp", "background");
					else
						FrontImage_RegisterResourceDefault("frontres\\debtrbr.bmp", "background");
				} else {
					if (g_frontendMission.flightGroups[playerFlightGroup].iff != 0)
						FrontImage_RegisterResourceDefault("frontres\\debtrli.bmp", "background");
					else
						FrontImage_RegisterResourceDefault("frontres\\debtrlr.bmp", "background");
				}
			}
		} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES) {
			int tournamentResult = 0;
			int meleeResult = 0;

			if (g_pilotData.missionSequenceActive == 1 &&
				g_pilotData.meleeTournamentSequenceState.missionCount -
						g_pilotData.meleeTournamentSequenceState.currentMissionIndex ==
					1) {
				int standingIndex;

				for (standingIndex = 0; standingIndex < 9; ++standingIndex) {
					int team = g_debriefStandingsTeamIds[standingIndex];

					if (team == -1)
						break;
					if (g_pilotData.team == team)
						tournamentResult = 1;
					if (g_pilotData.meleeTournamentSequenceState
							.teamStandings[g_debriefStandingsTeamIds[standingIndex + 1]]
							.totalScore <
						g_pilotData.meleeTournamentSequenceState.teamStandings[team].totalScore) {
						break;
					}
				}
			}
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[1] != 0) {
				tournamentResult =
					(g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[1] == 6) + 1;
			}
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[0] != 0) {
				meleeResult =
					(g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[0] == 6) + 1;
			}
			if (tournamentResult == 1) {
				if (g_pilotData.currentFactionId != 0)
					FrontImage_RegisterResourceDefault("frontres\\debtwi.bmp", "background");
				else
					FrontImage_RegisterResourceDefault("frontres\\debtwr.bmp", "background");
			} else if (tournamentResult == 2) {
				if (g_pilotData.currentFactionId != 0)
					FrontImage_RegisterResourceDefault("frontres\\debtbi.bmp", "background");
				else
					FrontImage_RegisterResourceDefault("frontres\\debtbr.bmp", "background");
			} else if (meleeResult == 1) {
				if (g_pilotData.currentFactionId != 0)
					FrontImage_RegisterResourceDefault("frontres\\debmwi.bmp", "background");
				else
					FrontImage_RegisterResourceDefault("frontres\\debmwr.bmp", "background");
			} else if (meleeResult == 2) {
				if (g_pilotData.currentFactionId != 0)
					FrontImage_RegisterResourceDefault("frontres\\debmbi.bmp", "background");
				else
					FrontImage_RegisterResourceDefault("frontres\\debmbr.bmp", "background");
			} else {
				if (g_pilotData.currentFactionId != 0)
					FrontImage_RegisterResourceDefault("frontres\\debmli.bmp", "background");
				else
					FrontImage_RegisterResourceDefault("frontres\\debmlr.bmp", "background");
			}
		} else {
			if (g_pilotData.teams[1].isMissionCompleted != g_pilotData.teams[0].isMissionCompleted) {
				int playerFlightGroup = g_pilotData.networkPlayers[localNetworkPlayerIndex].flightGroupId;

				if (g_pilotData.teams[0].isMissionCompleted != 1) {
					if (g_frontendMission.flightGroups[playerFlightGroup].team != 0) {
						if (g_pilotData.missionSequenceActive != 0)
							FrontImage_RegisterResourceDefault("frontres\\debbwr.bmp", "background");
						else
							FrontImage_RegisterResourceDefault("frontres\\debcwr.bmp", "background");
					} else if (g_pilotData.missionSequenceActive != 0) {
						FrontImage_RegisterResourceDefault("frontres\\debbli.bmp", "background");
					} else {
						FrontImage_RegisterResourceDefault("frontres\\debcli.bmp", "background");
					}
				} else if (g_frontendMission.flightGroups[playerFlightGroup].team == 0) {
					if (g_pilotData.missionSequenceActive != 0)
						FrontImage_RegisterResourceDefault("frontres\\debbwi.bmp", "background");
					else
						FrontImage_RegisterResourceDefault("frontres\\debcwi.bmp", "background");
				} else if (g_pilotData.missionSequenceActive != 0) {
					FrontImage_RegisterResourceDefault("frontres\\debblr.bmp", "background");
				} else {
					FrontImage_RegisterResourceDefault("frontres\\debclr.bmp", "background");
				}
			} else {
				if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					int playerFlightGroup = g_pilotData.networkPlayers[localNetworkPlayerIndex].flightGroupId;

					if (g_frontendMission.flightGroups[playerFlightGroup].team == 0) {
						if (g_pilotData.missionSequenceActive != 0)
							FrontImage_RegisterResourceDefault("frontres\\debbti.bmp", "background");
						else
							FrontImage_RegisterResourceDefault("frontres\\debcti.bmp", "background");
					} else if (g_pilotData.missionSequenceActive != 0) {
						FrontImage_RegisterResourceDefault("frontres\\debbtr.bmp", "background");
					} else {
						FrontImage_RegisterResourceDefault("frontres\\debctr.bmp", "background");
					}
				} else if (g_pilotData.teams[0].isMissionCompleted != 0 ||
						   g_pilotData.teams[1].isMissionCompleted != 0) {
					int playerFlightGroup = g_pilotData.networkPlayers[localNetworkPlayerIndex].flightGroupId;

					if (g_frontendMission.flightGroups[playerFlightGroup].team == 0) {
						if (g_pilotData.missionSequenceActive != 0)
							FrontImage_RegisterResourceDefault("frontres\\debbti.bmp", "background");
						else
							FrontImage_RegisterResourceDefault("frontres\\debcti.bmp", "background");
					} else if (g_pilotData.missionSequenceActive != 0) {
						FrontImage_RegisterResourceDefault("frontres\\debbtr.bmp", "background");
					} else {
						FrontImage_RegisterResourceDefault("frontres\\debctr.bmp", "background");
					}
				} else if (g_frontendMission
							   .flightGroups[g_pilotData.networkPlayers[localNetworkPlayerIndex]
												 .flightGroupId]
							   .team != 0) {
					if (g_pilotData.missionSequenceActive == 0)
						FrontImage_RegisterResourceDefault("frontres\\debclr.bmp", "background");
					else
						FrontImage_RegisterResourceDefault("frontres\\debbtr.bmp", "background");
				} else if (g_pilotData.missionSequenceActive != 0) {
					FrontImage_RegisterResourceDefault("frontres\\debbti.bmp", "background");
				} else {
					FrontImage_RegisterResourceDefault("frontres\\debcli.bmp", "background");
				}
			}
		}

		if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER)
			MissionSetup_BroadcastStatePacket(0);
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("background", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		FrontImage_DrawSprite("alloff", 0, 0);
		FrontImage_DrawSpriteTranslucent("regoverlay", 0, 0);
		if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER)
			FrontImage_DrawSpriteTranslucent("chatbox", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_ResetGlyphScratchBuffer(20);
	}

	if (g_debriefDisconnectedFromNetGame == 0 || frameCounter >= NETWORK_MESSAGE_FRAME_COUNT) {
		int networkEvent;
		int titleIndex;

		FrontendDraw_RectAssign(&rect, 158, 52, 491, 68);
		FrontendDisplay_GetScreenClipRect(&savedClipRect);
		FrontendDisplay_SetScreenClipRect640x480(&rect);
		sprintf(g_frontendScratchBuffer, "%c%s", TEXT_CODE_LABEL,
				g_missionList[g_selectedMissionListIndex].description);
		titleIndex = (int)strlen(g_frontendScratchBuffer) - 1;
		while (titleIndex != 0 && g_frontendScratchBuffer[titleIndex] != '(')
			--titleIndex;
		if (titleIndex != 0)
			g_frontendScratchBuffer[titleIndex] = 0;
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, WHITE_COLOR);
		FrontendDisplay_SetScreenClipRect640x480(&savedClipRect);

		if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			networkEvent = FrontendNet_ProcessNetworkPackets();
			if (networkEvent == NET_PACKET_HOST_CANCELLED) {
				Net_ShutdownDirectPlaySession();
				FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
				return 0;
			} else if (networkEvent == NET_PACKET_STATE) {
				MissionSetup_PruneFlightAssignments();
			} else if (networkEvent == NET_PACKET_RETURN_TO_SETUP) {
				return 0;
			} else if (networkEvent == NET_PACKET_NEXT_TOURNAMENT_MISSION) {
				int localPlayerId;

				++g_pilotData.meleeTournamentSequenceState.currentMissionIndex;
				localPlayerId = Net_GetLocalPlayerId();
				g_pilotData.launchSessionMarker = 1;
				g_pilotData.localPlayerId = localPlayerId;
				g_pilotData.isHost = Net_IsHost();
				g_pilotData.numHumanPlayersLastMission = Net_CountReadyPlayers();
				g_pilotData.gameMode = g_frontendMissionSessionMode;
				FrontendScreen_SetCallbacks(MissionSetup_EnterNextMission,

#ifdef XVT_MODERN
											XvtFrontendCleanup_NextMission
#else
											(FrontendScreenExitFn)MissionSetup_ExitNextMission
#endif
				);
				return 0;
			} else if (networkEvent == NET_PACKET_NEXT_BATTLE_MISSION) {
				int localPlayerId;

				++g_pilotData.battleSequenceState.currentMissionIndex;
				localPlayerId = Net_GetLocalPlayerId();
				g_pilotData.launchSessionMarker = 1;
				g_pilotData.localPlayerId = localPlayerId;
				g_pilotData.isHost = Net_IsHost();
				g_pilotData.numHumanPlayersLastMission = Net_CountReadyPlayers();
				g_pilotData.gameMode = g_frontendMissionSessionMode;
				FrontendScreen_SetCallbacks(MissionSetup_EnterNextMission,

#ifdef XVT_MODERN
											XvtFrontendCleanup_NextMission
#else
											(FrontendScreenExitFn)MissionSetup_ExitNextMission
#endif
				);
				return 0;
			} else if (networkEvent == NET_PACKET_NEXT_CAMPAIGN_MISSION) {
				int localPlayerId;

				++g_pilotData.campaignSequenceState.currentMissionIndex;
				localPlayerId = Net_GetLocalPlayerId();
				g_pilotData.launchSessionMarker = 1;
				g_pilotData.localPlayerId = localPlayerId;
				g_pilotData.isHost = Net_IsHost();
				g_pilotData.numHumanPlayersLastMission = Net_CountReadyPlayers();
				g_missionSetupDebriefTransition = MISSION_SETUP_DEBRIEF_TRANSITION_ADVANCE_MISSION_DIRECTORY;
				g_frontendQuickStartLaunchFlag = 0;
				g_pilotData.gameMode = g_frontendMissionSessionMode;
				FrontendScreen_SetCallbacks(MissionSetup_EnterNextMission,

#ifdef XVT_MODERN
											XvtFrontendCleanup_NextMission
#else
											(FrontendScreenExitFn)MissionSetup_ExitNextMission
#endif
				);
			} else if (networkEvent == NET_PACKET_REPLAY_CURRENT_MISSION) {
				int localPlayerId = Net_GetLocalPlayerId();

				g_pilotData.launchSessionMarker = 1;
				g_pilotData.localPlayerId = localPlayerId;
				g_pilotData.isHost = Net_IsHost();
				g_pilotData.numHumanPlayersLastMission = Net_CountReadyPlayers();
				g_pilotData.gameMode = g_frontendMissionSessionMode;
				FrontendScreen_SetCallbacks(MissionSetup_EnterCurrentMission,

#ifdef XVT_MODERN
											XvtFrontendCleanup_CurrentMission
#else
											(FrontendScreenExitFn)MissionSetup_ExitCurrentMission
#endif
				);
				return 0;
			} else if (networkEvent == NET_PACKET_REPLAY_CAMPAIGN_MISSION) {
				g_pilotData.localPlayerId = Net_GetLocalPlayerId();
				g_pilotData.launchSessionMarker = 1;
				g_pilotData.isHost = Net_IsHost();
				g_pilotData.numHumanPlayersLastMission = Net_CountReadyPlayers();
				g_pilotData.gameMode = g_frontendMissionSessionMode;
				g_missionSetupDebriefTransition = MISSION_SETUP_DEBRIEF_TRANSITION_ENTER_CURRENT_MISSION;
				g_skipFrontendEntryMovie = 1;
				FrontendScreen_SetCallbacks(MissionSetup_EnterCurrentMission,

#ifdef XVT_MODERN
											XvtFrontendCleanup_CurrentMission
#else
											(FrontendScreenExitFn)MissionSetup_ExitCurrentMission
#endif
				);
			} else if (networkEvent == NET_PACKET_REPLAY_MISSION) {
				memset(g_pilotData.killsFullOnPlayer, 0, sizeof(g_pilotData.killsFullOnPlayer));
				memset(g_pilotData.killsSharedOnPlayer, 0, sizeof(g_pilotData.killsSharedOnPlayer));
				memset(g_pilotData.killsFullOnFlightGroup, 0, sizeof(g_pilotData.killsFullOnFlightGroup));
				memset(g_pilotData.killsSharedOnFlightGroup, 0, sizeof(g_pilotData.killsSharedOnFlightGroup));
				memset(g_pilotData.killsFullFromPlayer, 0, sizeof(g_pilotData.killsFullFromPlayer));
				memset(g_pilotData.killsSharedFromPlayer, 0, sizeof(g_pilotData.killsSharedFromPlayer));
				memset(g_pilotData.killsFullFromFlightGroup, 0, sizeof(g_pilotData.killsFullFromFlightGroup));
				memset(g_pilotData.killsSharedFromFlightGroup, 0,
					   sizeof(g_pilotData.killsSharedFromFlightGroup));
				memset(&g_pilotData.objectStats, 0, sizeof(g_pilotData.objectStats));
				memset(g_pilotData.teams, 0, sizeof(g_pilotData.teams));
				for (localNetworkPlayerIndex = 0; localNetworkPlayerIndex < PLAYER_COUNT;
					 ++localNetworkPlayerIndex) {
					PilotNetworkPlayer* networkPlayer = &g_pilotData.networkPlayers[localNetworkPlayerIndex];

					networkPlayer->totalScore = 0;
					networkPlayer->kills = 0;
					networkPlayer->killsShared = 0;
					networkPlayer->unknown34 = 0;
					networkPlayer->killsAssist = 0;
					networkPlayer->totalLosses = 0;
					networkPlayer->hasLeft = 0;
				}
				MissionSetup_PruneDisconnectedPlayers();
				MissionSetup_PruneTeamAssignments();
				FrontendScreen_SetCallbacks(FlightLoading_GetReadyScreen, NULL);
				return 0;
			} else if (networkEvent == NET_PACKET_SESSION_CANCELLED) {
				g_debriefSkipMissionConfirmPending = 1;
			} else if (networkEvent == NET_PACKET_TEAM_ASSIGNMENTS_READY) {
				g_debriefSkipMissionConfirmPending = 1;
			} else if (networkEvent == NET_PACKET_PLAYER_READY) {
				for (localNetworkPlayerIndex = 0; localNetworkPlayerIndex < PLAYER_COUNT;
					 ++localNetworkPlayerIndex) {
					if (g_mpRoster[localNetworkPlayerIndex].playerId == g_frontendNetPacketSenderPlayerId) {
						g_mpRosterReadyFlags[localNetworkPlayerIndex] = 0;
						break;
					}
				}
			} else if (networkEvent == NET_PACKET_RETURN_TO_MISSION_SELECTION) {
				g_skipFrontendEntryMovie = 0;
				g_frontendQuickStartLaunchFlag = 0;
				g_frontendSinglePlayerFlightSessionActive = 0;
				g_missionSetupRosterAuthoritative = 0;
				MpRoster_CompactActiveEntries();
				FrontendScreen_SetCallbacks(MissionSetup_Update, MissionSetup_Exit);
				return 0;
			}
			FrontendNet_UpdateAndDrawPanel(frameCounter);
		}

		if (g_briefingTab == 0) {
			MissionDebrief_DrawMissionOverviewPage(frameCounter);
		} else if (g_briefingTab == 1) {
			MissionDebrief_DrawPlayerStatisticsPage();
		} else if (g_briefingTab == 2 && g_pilotData.missionSequenceActive == 1) {
			if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES)
				MissionDebrief_DrawNarrativeTextPage();
			else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES)
				MissionDebrief_DrawMeleeResultsPage(frameCounter);
			else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_COMBAT_ENGAGEMENTS)
				MissionDebrief_DrawSkirmishResultsPage();
		}

		FrontendDraw_RectAssign(&rect, 200, 452, 436, 464);
		if (g_pilotData.name[0] != 0) {
			sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_RATING, g_pilotData.ratingName, 1,
					g_pilotData.name);
			FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, g_colorLightBlue);
			if (g_pilotData.currentFactionId != 0)
				sprintf(g_frontendScratchBuffer, "imptiny%d", (frameCounter % 32) >> 1);
			else
				sprintf(g_frontendScratchBuffer, "rebtiny%d", (frameCounter % 32) >> 1);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 204, 453);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 420, 453);
		}
		MissionDebrief_DrawTabBar();
		if (Frontend_HandleCommonScreenControls(4) == 1)
			return 1;
#ifdef XVT_MODERN
		if (XvtDialog_IsActive())
			return 0;
#endif

		FrontendDraw_RectAssign(&rect, 85, 447, 176, 471);
		if (g_gameConfig.helpOn != 0)
			FrontendButton_EnableOverlayText();
		if (g_pilotData.missionSequenceActive == 0) {
			if (Net_IsHost() == 0 && g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				int disconnectAccepted;

				if (g_debriefDisconnectedFromNetGame != 0) {
					FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_206_DONE));
					disconnectAccepted = FrontendButton_DrawSpriteHitTest(
						&rect, "leaveup", "leavedown", FrontendString_Get(FRONTSTR_206_DONE), 12, 0, 8,
						"buttonsound");
				} else {
					FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_701_DISCONNECT));
					if (FrontendButton_DrawSpriteHitTest(
							&rect, "leaveup", "leavedown",
							FrontendString_Get(FRONTSTR_702_DISCONNECT_FROM_GAME_SESSION), 12, 0, 8,
							"buttonsound") == 0) {
						disconnectAccepted = 0;
					} else {
						disconnectAccepted = FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
							FrontendString_Get(FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
							FrontendString_Get(FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
							FrontendString_Get(FRONTSTR_523_OKAY), FrontendString_Get(FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
						return XvtDialog_ContinueWith(XvtMissionDialogs_Resume,
													  XVT_MISSION_DEBRIEF_CLIENT_LEAVE);
#endif
					}
				}
				if (disconnectAccepted != 0) {
					g_frontendNetPacketScratch.packetType = NET_PACKET_PLAYER_LEFT;
					Net_SendPacketAndFlush(Net_GetHostPlayerId(), &g_frontendNetPacketScratch,
										   sizeof(g_frontendNetPacketScratch.packetType));
					Net_ShutdownDirectPlaySession();
					FrontendButton_DisableOverlayText();
					FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
				}
			} else {
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_700_NEW_MISSION));
				if (FrontendButton_DrawSpriteHitTest(
						&rect, "leaveup", "leavedown",
						FrontendString_Get(FRONTSTR_260_RETURN_TO_SELECT_MISSION), 12, 0, 8,
						"buttonsound") != 0) {
					if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontendNetPacketScratch.packetType = NET_PACKET_RETURN_TO_MISSION_SELECTION;
						Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
											   sizeof(g_frontendNetPacketScratch.packetType));
					} else {
						FrontendButton_DisableOverlayText();
						g_skipFrontendEntryMovie = 0;
						g_frontendQuickStartLaunchFlag = 0;
						g_frontendSinglePlayerFlightSessionActive = 0;
						g_missionSetupRosterAuthoritative = 0;
						FrontendScreen_SetCallbacks(MissionSetup_Update, MissionSetup_Exit);
					}
				}
			}
		} else if (Net_IsHost() == 0 &&
				   g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			int disconnectAccepted;
			const char* tooltipText;

			if (g_debriefDisconnectedFromNetGame != 0) {
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_206_DONE));
				tooltipText = FrontendString_Get(FRONTSTR_206_DONE);
			} else {
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_701_DISCONNECT));
				tooltipText = FrontendString_Get(FRONTSTR_702_DISCONNECT_FROM_GAME_SESSION);
			}
			disconnectAccepted = FrontendButton_DrawSpriteHitTest(&rect, "leaveup", "leavedown", tooltipText,
																  12, 0, 8, "buttonsound");
			if (disconnectAccepted != 0) {
				if (g_debriefDisconnectedFromNetGame == 0) {
					disconnectAccepted = FrontendDialog_ShowConfirmDialog(
						FrontendString_Get(FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
						FrontendString_Get(FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
						FrontendString_Get(FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
						FrontendString_Get(FRONTSTR_523_OKAY), FrontendString_Get(FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
					return XvtDialog_ContinueWith(XvtMissionDialogs_Resume, XVT_MISSION_DEBRIEF_CLIENT_LEAVE);
#endif
				}
				if (disconnectAccepted != 0) {
					g_frontendNetPacketScratch.packetType = NET_PACKET_PLAYER_LEFT;
					Net_SendPacketAndFlush(Net_GetHostPlayerId(), &g_frontendNetPacketScratch,
										   sizeof(g_frontendNetPacketScratch.packetType));
					Net_ShutdownDirectPlaySession();
					FrontendButton_DisableOverlayText();
					FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
				}
			}
		} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES) {
			if (g_pilotData.meleeTournamentSequenceState.missionCount -
					g_pilotData.meleeTournamentSequenceState.currentMissionIndex ==
				1) {
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_700_NEW_MISSION));
				if (FrontendButton_DrawSpriteHitTest(
						&rect, "leaveup", "leavedown",
						FrontendString_Get(FRONTSTR_260_RETURN_TO_SELECT_MISSION), 12, 0, 8,
						"buttonsound") != 0) {
					if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontendNetPacketScratch.packetType = NET_PACKET_RETURN_TO_MISSION_SELECTION;
						Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
											   sizeof(g_frontendNetPacketScratch.packetType));
					} else {
						FrontendButton_DisableOverlayText();
						g_skipFrontendEntryMovie = 0;
						g_frontendQuickStartLaunchFlag = 0;
						g_frontendSinglePlayerFlightSessionActive = 0;
						g_missionSetupRosterAuthoritative = 0;
						FrontendScreen_SetCallbacks(MissionSetup_Update, MissionSetup_Exit);
					}
				}
			} else {
				showSequenceContinueButton = 1;
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_216_ABORT));
				if (FrontendButton_DrawSpriteHitTest(&rect, "leaveup", "leavedown",
													 FrontendString_Get(FRONTSTR_391_ABORT_TOURNAMENT), 12, 0,
													 8, "buttonsound") != 0) {
					if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {

#ifdef XVT_MODERN
						{
							FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								FrontendString_Get(FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								FrontendString_Get(FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								FrontendString_Get(FRONTSTR_523_OKAY),
								FrontendString_Get(FRONTSTR_019_CANCEL));
							return XvtDialog_ContinueWith(XvtMissionDialogs_Resume,
														  XVT_MISSION_DEBRIEF_HOST_ABORT);
						}
#else
						if (FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								FrontendString_Get(FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								FrontendString_Get(FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								FrontendString_Get(FRONTSTR_523_OKAY),
								FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
							g_frontendNetPacketScratch.packetType = NET_PACKET_RETURN_TO_MISSION_SELECTION;
							Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
												   sizeof(g_frontendNetPacketScratch.packetType));
						}
#endif

					}
#ifdef XVT_MODERN
					else {
						FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(FRONTSTR_678_YOU_ARE_CURRENTLY_PLAYING_A_TOURNAMENT),
							FrontendString_Get(FRONTSTR_679_ARE_YOU_SURE_YOU_WANT_TO),
							FrontendString_Get(FRONTSTR_680_TERMINATE_THIS_TOURNAMENT),
							FrontendString_Get(FRONTSTR_523_OKAY), FrontendString_Get(FRONTSTR_019_CANCEL));
						return XvtDialog_ContinueWith(XvtMissionDialogs_Resume,
													  XVT_MISSION_DEBRIEF_SOLO_ABORT);
					}
#else
					else if (FrontendDialog_ShowConfirmDialog(
								 FrontendString_Get(FRONTSTR_678_YOU_ARE_CURRENTLY_PLAYING_A_TOURNAMENT),
								 FrontendString_Get(FRONTSTR_679_ARE_YOU_SURE_YOU_WANT_TO),
								 FrontendString_Get(FRONTSTR_680_TERMINATE_THIS_TOURNAMENT),
								 FrontendString_Get(FRONTSTR_523_OKAY),
								 FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
						FrontendButton_DisableOverlayText();
						g_skipFrontendEntryMovie = 0;
						g_frontendQuickStartLaunchFlag = 0;
						g_frontendSinglePlayerFlightSessionActive = 0;
						g_missionSetupRosterAuthoritative = 0;
						FrontendScreen_SetCallbacks(MissionSetup_Update, MissionSetup_Exit);
					}
#endif
				}
			}
		} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
			int missionIndex;

			battleResultCounts[BATTLE_MISSION_RESULT_IMPERIAL_VICTORY] = 0;
			battleResultCounts[BATTLE_MISSION_RESULT_REBEL_VICTORY] = 0;
			battleResultCounts[BATTLE_MISSION_RESULT_DRAW] = 0;
			for (missionIndex = 0; missionIndex <= (int)g_pilotData.battleSequenceState.currentMissionIndex;
				 ++missionIndex) {
				++battleResultCounts[g_pilotData.battleSequenceState.missionResults[missionIndex]];
			}
			if (g_pilotData.battleSequenceState.victoriesNeeded !=
					battleResultCounts[BATTLE_MISSION_RESULT_IMPERIAL_VICTORY] &&
				g_pilotData.battleSequenceState.victoriesNeeded !=
					battleResultCounts[BATTLE_MISSION_RESULT_REBEL_VICTORY]) {
				showSequenceContinueButton = 1;
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_216_ABORT));
				if (FrontendButton_DrawSpriteHitTest(&rect, "leaveup", "leavedown",
													 FrontendString_Get(FRONTSTR_392_ABORT_BATTLE), 12, 0, 8,
													 "buttonsound") != 0) {
					if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {

#ifdef XVT_MODERN
						{
							FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								FrontendString_Get(FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								FrontendString_Get(FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								FrontendString_Get(FRONTSTR_523_OKAY),
								FrontendString_Get(FRONTSTR_019_CANCEL));
							return XvtDialog_ContinueWith(XvtMissionDialogs_Resume,
														  XVT_MISSION_DEBRIEF_HOST_ABORT);
						}
#else
						if (FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								FrontendString_Get(FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								FrontendString_Get(FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								FrontendString_Get(FRONTSTR_523_OKAY),
								FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
							g_frontendNetPacketScratch.packetType = NET_PACKET_RETURN_TO_MISSION_SELECTION;
							Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
												   sizeof(g_frontendNetPacketScratch.packetType));
						}
#endif

					}
#ifdef XVT_MODERN
					else {
						FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
							FrontendString_Get(FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
							FrontendString_Get(FRONTSTR_683_TERMINATE_THIS_BATTLE),
							FrontendString_Get(FRONTSTR_523_OKAY), FrontendString_Get(FRONTSTR_019_CANCEL));
						return XvtDialog_ContinueWith(XvtMissionDialogs_Resume,
													  XVT_MISSION_DEBRIEF_SOLO_ABORT_CLEAR_ROSTER);
					}
#else
					else if (FrontendDialog_ShowConfirmDialog(
								 FrontendString_Get(FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
								 FrontendString_Get(FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
								 FrontendString_Get(FRONTSTR_683_TERMINATE_THIS_BATTLE),
								 FrontendString_Get(FRONTSTR_523_OKAY),
								 FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
						FrontendButton_DisableOverlayText();
						g_skipFrontendEntryMovie = 0;
						g_frontendQuickStartLaunchFlag = 0;
						g_frontendSinglePlayerFlightSessionActive = 0;
						g_missionSetupRosterAuthoritative = 0;
						memset(g_mpRoster, 0, sizeof(g_mpRoster));
						FrontendScreen_SetCallbacks(MissionSetup_Update, MissionSetup_Exit);
					}
#endif
				}
			} else {
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_700_NEW_MISSION));
				if (FrontendButton_DrawSpriteHitTest(
						&rect, "leaveup", "leavedown",
						FrontendString_Get(FRONTSTR_260_RETURN_TO_SELECT_MISSION), 12, 0, 8,
						"buttonsound") != 0) {
					if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontendNetPacketScratch.packetType = NET_PACKET_RETURN_TO_MISSION_SELECTION;
						Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
											   sizeof(g_frontendNetPacketScratch.packetType));
					} else {
						FrontendButton_DisableOverlayText();
						g_skipFrontendEntryMovie = 0;
						g_frontendQuickStartLaunchFlag = 0;
						g_frontendSinglePlayerFlightSessionActive = 0;
						g_missionSetupRosterAuthoritative = 0;
						FrontendScreen_SetCallbacks(MissionSetup_Update, MissionSetup_Exit);
					}
				}
			}
		} else {
			if (g_pilotData.campaignSequenceState.lastMissionCompleted == 0 ||
				g_pilotData.campaignSequenceState.currentMissionIndex -
						g_pilotData.campaignSequenceState.missionCount !=
					-1) {
				showSequenceContinueButton = 1;
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_216_ABORT));
				if (FrontendButton_DrawSpriteHitTest(&rect, "leaveup", "leavedown",
													 FrontendString_Get(FRONTSTR_782_ABORT_CAMPAIGN), 12, 0,
													 8, "buttonsound") != 0) {
					if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {

#ifdef XVT_MODERN
						{
							FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								FrontendString_Get(FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								FrontendString_Get(FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								FrontendString_Get(FRONTSTR_523_OKAY),
								FrontendString_Get(FRONTSTR_019_CANCEL));
							return XvtDialog_ContinueWith(XvtMissionDialogs_Resume,
														  XVT_MISSION_DEBRIEF_HOST_ABORT);
						}
#else
						if (FrontendDialog_ShowConfirmDialog(
								FrontendString_Get(FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								FrontendString_Get(FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								FrontendString_Get(FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								FrontendString_Get(FRONTSTR_523_OKAY),
								FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
							g_frontendNetPacketScratch.packetType = NET_PACKET_RETURN_TO_MISSION_SELECTION;
							Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
												   sizeof(g_frontendNetPacketScratch.packetType));
						}
#endif

					}
#ifdef XVT_MODERN
					else {
						FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(FRONTSTR_779_YOU_ARE_CURRENTLY_PLAYING_A_CAMPAIGN),
							FrontendString_Get(FRONTSTR_780_ARE_YOU_SURE_YOU_WANT_TO),
							FrontendString_Get(FRONTSTR_781_TERMINATE_THIS_CAMPAIGN),
							FrontendString_Get(FRONTSTR_523_OKAY), FrontendString_Get(FRONTSTR_019_CANCEL));
						return XvtDialog_ContinueWith(XvtMissionDialogs_Resume,
													  XVT_MISSION_DEBRIEF_SOLO_ABORT_CLEAR_ROSTER);
					}
#else
					else if (FrontendDialog_ShowConfirmDialog(
								 FrontendString_Get(FRONTSTR_779_YOU_ARE_CURRENTLY_PLAYING_A_CAMPAIGN),
								 FrontendString_Get(FRONTSTR_780_ARE_YOU_SURE_YOU_WANT_TO),
								 FrontendString_Get(FRONTSTR_781_TERMINATE_THIS_CAMPAIGN),
								 FrontendString_Get(FRONTSTR_523_OKAY),
								 FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
						FrontendButton_DisableOverlayText();
						g_skipFrontendEntryMovie = 0;
						g_frontendQuickStartLaunchFlag = 0;
						g_frontendSinglePlayerFlightSessionActive = 0;
						g_missionSetupRosterAuthoritative = 0;
						memset(g_mpRoster, 0, sizeof(g_mpRoster));
						FrontendScreen_SetCallbacks(MissionSetup_Update, MissionSetup_Exit);
					}
#endif
				}
			} else {
				g_pilotData.factionStatistics[g_pilotData.currentFactionId]
					.spCampaigns[g_pilotData.missionDescriptionIds[MISSION_DIRECTORY_CAMPAIGNS]]
					.isFinished = 1;
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_700_NEW_MISSION));
				if (FrontendButton_DrawSpriteHitTest(
						&rect, "leaveup", "leavedown",
						FrontendString_Get(FRONTSTR_260_RETURN_TO_SELECT_MISSION), 12, 0, 8,
						"buttonsound") != 0) {
					if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontendNetPacketScratch.packetType = NET_PACKET_RETURN_TO_MISSION_SELECTION;
						Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
											   sizeof(g_frontendNetPacketScratch.packetType));
					} else {
						FrontendButton_DisableOverlayText();
						g_skipFrontendEntryMovie = 0;
						g_frontendQuickStartLaunchFlag = 0;
						g_frontendSinglePlayerFlightSessionActive = 0;
						g_missionSetupRosterAuthoritative = 0;
						FrontendScreen_SetCallbacks(MissionSetup_Update, MissionSetup_Exit);
					}
				}
			}
		}

		if (showSequenceContinueButton != 0) {
			FrontendDraw_RectAssign(&rect, 8, 405, 71, 470);
			if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES) {
					FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_666_CONTINUE));
					if (FrontendButton_DrawSpriteHitTest(&rect, "flyup", "flydown",
														 FrontendString_Get(FRONTSTR_317_CONTINUE_TOURNAMENT),
														 12, 0, 7, "flysound") != 0) {
						++g_pilotData.meleeTournamentSequenceState.currentMissionIndex;
						g_pilotData.localPlayerId = Net_GetLocalPlayerId();
						g_pilotData.launchSessionMarker = 1;
						g_pilotData.isHost = 1;
						g_pilotData.numHumanPlayersLastMission = 1;
						g_pilotData.gameMode = g_frontendMissionSessionMode;
						FrontendButton_DisableOverlayText();
						FrontendScreen_SetCallbacks(MissionSetup_EnterNextMission,

#ifdef XVT_MODERN
													XvtFrontendCleanup_NextMission
#else
													(FrontendScreenExitFn)MissionSetup_ExitNextMission
#endif
						);
					}
				} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
					if (g_pilotData.battleSequenceState
							.missionResults[g_pilotData.battleSequenceState.currentMissionIndex] ==
						BATTLE_MISSION_RESULT_DRAW) {
						FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_711_REFLY));
						if (FrontendButton_DrawSpriteHitTest(
								&rect, "flyup", "flydown",
								FrontendString_Get(FRONTSTR_710_REFLY_BATTLE_MISSION), 12, 0, 7,
								"flysound") != 0) {
							g_pilotData.localPlayerId = Net_GetLocalPlayerId();
							g_pilotData.gameMode = g_frontendMissionSessionMode;
							g_pilotData.launchSessionMarker = 1;
							g_pilotData.isHost = 1;
							g_pilotData.numHumanPlayersLastMission = 1;
							FrontendButton_DisableOverlayText();
							FrontendScreen_SetCallbacks(MissionSetup_EnterCurrentMission,

#ifdef XVT_MODERN
														XvtFrontendCleanup_CurrentMission
#else
														(FrontendScreenExitFn)MissionSetup_ExitCurrentMission
#endif
							);
						}
					} else {
						FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_666_CONTINUE));
						if (FrontendButton_DrawSpriteHitTest(&rect, "flyup", "flydown",
															 FrontendString_Get(FRONTSTR_316_CONTINUE_BATTLE),
															 12, 0, 7, "flysound") != 0) {
							++g_pilotData.battleSequenceState.currentMissionIndex;
							g_pilotData.localPlayerId = Net_GetLocalPlayerId();
							g_pilotData.gameMode = g_frontendMissionSessionMode;
							g_pilotData.launchSessionMarker = 1;
							g_pilotData.isHost = 1;
							g_pilotData.numHumanPlayersLastMission = 1;
							FrontendButton_DisableOverlayText();
							g_frontendQuickStartLaunchFlag = 0;
							FrontendScreen_SetCallbacks(MissionSetup_EnterNextMission,

#ifdef XVT_MODERN
														XvtFrontendCleanup_NextMission
#else
														(FrontendScreenExitFn)MissionSetup_ExitNextMission
#endif
							);
						}
					}
				} else if (g_pilotData.campaignSequenceState.lastMissionCompleted != 0) {
					FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_666_CONTINUE));
					if (FrontendButton_DrawSpriteHitTest(&rect, "flyup", "flydown",
														 FrontendString_Get(FRONTSTR_784_CONTINUE_CAMPAIGN),
														 12, 0, 7, "flysound") != 0) {
						++g_pilotData.campaignSequenceState.currentMissionIndex;
						g_pilotData.localPlayerId = Net_GetLocalPlayerId();
						g_pilotData.gameMode = g_frontendMissionSessionMode;
						g_pilotData.launchSessionMarker = 1;
						g_pilotData.isHost = 1;
						g_pilotData.numHumanPlayersLastMission = 1;
						FrontendButton_DisableOverlayText();
						g_missionSetupDebriefTransition =
							MISSION_SETUP_DEBRIEF_TRANSITION_ADVANCE_MISSION_DIRECTORY;
						g_frontendQuickStartLaunchFlag = 0;
						FrontendScreen_SetCallbacks(MissionSetup_EnterNextMission,

#ifdef XVT_MODERN
													XvtFrontendCleanup_NextMission
#else
													(FrontendScreenExitFn)MissionSetup_ExitNextMission
#endif
						);
					}
				} else {
					FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_711_REFLY));
					if (FrontendButton_DrawSpriteHitTest(
							&rect, "flyup", "flydown",
							FrontendString_Get(FRONTSTR_783_REFLY_CAMPAIGN_MISSION), 12, 0, 7,
							"flysound") != 0) {
						g_pilotData.localPlayerId = Net_GetLocalPlayerId();
						g_pilotData.launchSessionMarker = 1;
						g_pilotData.isHost = 1;
						g_pilotData.numHumanPlayersLastMission = 1;
						g_pilotData.gameMode = g_frontendMissionSessionMode;
						FrontendButton_DisableOverlayText();
						g_skipFrontendEntryMovie = 1;
						g_missionSetupDebriefTransition =
							MISSION_SETUP_DEBRIEF_TRANSITION_ENTER_CURRENT_MISSION;
						FrontendScreen_SetCallbacks(MissionSetup_EnterCurrentMission,

#ifdef XVT_MODERN
													XvtFrontendCleanup_CurrentMission
#else
													(FrontendScreenExitFn)MissionSetup_ExitCurrentMission
#endif
						);
					}
				}
			} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES) {
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_666_CONTINUE));
				if (FrontendButton_DrawSpriteHitTest(&rect, "flyup", "flydown",
													 FrontendString_Get(FRONTSTR_317_CONTINUE_TOURNAMENT), 12,
													 0, 7, "flysound") != 0) {
					uint32_t packetTimestamp;

					g_frontendNetPacketScratch.packetType = NET_PACKET_NEXT_TOURNAMENT_MISSION;
					packetTimestamp = GetTickCount();
					memcpy(g_frontendNetPacketScratch.payload, &packetTimestamp, sizeof(packetTimestamp));
					Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
										   sizeof(g_frontendNetPacketScratch.packetType) +
											   sizeof(packetTimestamp));
				}
			} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
				if (g_pilotData.battleSequenceState
						.missionResults[g_pilotData.battleSequenceState.currentMissionIndex] ==
					BATTLE_MISSION_RESULT_DRAW) {
					FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_711_REFLY));
					if (FrontendButton_DrawSpriteHitTest(
							&rect, "flyup", "flydown", FrontendString_Get(FRONTSTR_710_REFLY_BATTLE_MISSION),
							12, 0, 7, "flysound") != 0) {
						uint32_t packetTimestamp;

						g_frontendNetPacketScratch.packetType = NET_PACKET_REPLAY_CURRENT_MISSION;
						packetTimestamp = GetTickCount();
						memcpy(g_frontendNetPacketScratch.payload, &packetTimestamp, sizeof(packetTimestamp));
						Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
											   sizeof(g_frontendNetPacketScratch.packetType) +
												   sizeof(packetTimestamp));
					}
				} else {
					FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_666_CONTINUE));
					if (FrontendButton_DrawSpriteHitTest(&rect, "flyup", "flydown",
														 FrontendString_Get(FRONTSTR_316_CONTINUE_BATTLE), 12,
														 0, 7, "flysound") != 0) {
						uint32_t packetTimestamp;

						g_frontendNetPacketScratch.packetType = NET_PACKET_NEXT_BATTLE_MISSION;
						packetTimestamp = GetTickCount();
						memcpy(g_frontendNetPacketScratch.payload, &packetTimestamp, sizeof(packetTimestamp));
						Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
											   sizeof(g_frontendNetPacketScratch.packetType) +
												   sizeof(packetTimestamp));
					}
				}
			} else if (g_pilotData.campaignSequenceState.lastMissionCompleted != 0) {
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_666_CONTINUE));
				if (FrontendButton_DrawSpriteHitTest(&rect, "flyup", "flydown",
													 FrontendString_Get(FRONTSTR_784_CONTINUE_CAMPAIGN), 12,
													 0, 7, "flysound") != 0) {
					uint32_t packetTimestamp;

					g_frontendNetPacketScratch.packetType = NET_PACKET_NEXT_CAMPAIGN_MISSION;
					packetTimestamp = GetTickCount();
					memcpy(g_frontendNetPacketScratch.payload, &packetTimestamp, sizeof(packetTimestamp));
					Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
										   sizeof(g_frontendNetPacketScratch.packetType) +
											   sizeof(packetTimestamp));
				}
			} else {
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_711_REFLY));
				if (FrontendButton_DrawSpriteHitTest(&rect, "flyup", "flydown",
													 FrontendString_Get(FRONTSTR_783_REFLY_CAMPAIGN_MISSION),
													 12, 0, 7, "flysound") != 0) {
					uint32_t packetTimestamp;

					g_frontendNetPacketScratch.packetType = NET_PACKET_REPLAY_CAMPAIGN_MISSION;
					packetTimestamp = GetTickCount();
					memcpy(g_frontendNetPacketScratch.payload, &packetTimestamp, sizeof(packetTimestamp));
					Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
										   sizeof(g_frontendNetPacketScratch.packetType) +
											   sizeof(packetTimestamp));
				}
			}
		} else if (g_pilotData.missionSequenceActive == 0) {
			FrontendDraw_RectAssign(&rect, 8, 405, 71, 470);
			if (Net_IsHost() != 0 || g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				FrontendButton_SetOverlayText(FrontendString_Get(FRONTSTR_476_FLY_AGAIN));
				if (FrontendButton_DrawSpriteHitTest(&rect, "flyup", "flydown",
													 FrontendString_Get(FRONTSTR_476_FLY_AGAIN), 12, 0, 7,
													 "flysound") != 0) {
					if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_pilotData.isHost = 1;
						g_pilotData.numHumanPlayersLastMission = 1;
						g_pilotData.gameMode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
						memset(g_pilotData.killsFullOnPlayer, 0, sizeof(g_pilotData.killsFullOnPlayer));
						memset(g_pilotData.killsSharedOnPlayer, 0, sizeof(g_pilotData.killsSharedOnPlayer));
						memset(g_pilotData.killsFullOnFlightGroup, 0,
							   sizeof(g_pilotData.killsFullOnFlightGroup));
						memset(g_pilotData.killsSharedOnFlightGroup, 0,
							   sizeof(g_pilotData.killsSharedOnFlightGroup));
						memset(g_pilotData.killsFullFromPlayer, 0, sizeof(g_pilotData.killsFullFromPlayer));
						memset(g_pilotData.killsSharedFromPlayer, 0,
							   sizeof(g_pilotData.killsSharedFromPlayer));
						memset(g_pilotData.killsFullFromFlightGroup, 0,
							   sizeof(g_pilotData.killsFullFromFlightGroup));
						memset(g_pilotData.killsSharedFromFlightGroup, 0,
							   sizeof(g_pilotData.killsSharedFromFlightGroup));
						memset(&g_pilotData.objectStats, 0, sizeof(g_pilotData.objectStats));
						memset(g_pilotData.teams, 0, sizeof(g_pilotData.teams));
						for (localNetworkPlayerIndex = 0; localNetworkPlayerIndex < PLAYER_COUNT;
							 ++localNetworkPlayerIndex) {
							PilotNetworkPlayer* networkPlayer =
								&g_pilotData.networkPlayers[localNetworkPlayerIndex];

							networkPlayer->totalScore = 0;
							networkPlayer->kills = 0;
							networkPlayer->killsShared = 0;
							networkPlayer->unknown34 = 0;
							networkPlayer->killsAssist = 0;
							networkPlayer->totalLosses = 0;
							networkPlayer->hasLeft = 0;
						}
						FrontendScreen_SetCallbacks(FlightLoading_GetReadyScreen, NULL);
					} else {
						uint32_t packetTimestamp;

						g_frontendNetPacketScratch.packetType = NET_PACKET_REPLAY_MISSION;
						packetTimestamp = GetTickCount();
						memcpy(g_frontendNetPacketScratch.payload, &packetTimestamp, sizeof(packetTimestamp));
						Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
											   sizeof(g_frontendNetPacketScratch.packetType) +
												   sizeof(packetTimestamp));
					}
				}
			}
		}
		FrontendButton_DisableOverlayText();
	} else {
		FrontendText_ResetGlyphScratch();
		FrontendDraw_RectAssign(&rect, 84, 107, 434, 433);
		FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_748_DISCONNECTING_FROM_NETWORK_PLEASE_WAIT),
								  &rect, WHITE_COLOR);
	}

	if (g_debriefDisconnectedFromNetGame != 0 && frameCounter == NETWORK_DISCONNECT_FRAME) {
		Net_ShutdownDirectPlaySession();
		sprintf(g_frontendScratchBuffer, "%s.", g_pilotData.multiplayerGameName);
		FrontendDialog_ShowConfirmDialog(FrontendString_Get(FRONTSTR_733_YOU_HAVE_BEEN_DISCONNECTED_FROM),
										 g_frontendScratchBuffer, NULL, NULL, NULL);
#ifdef XVT_MODERN
		return XvtDialog_ContinueWith(XvtMissionDialogs_Resume, XVT_MISSION_NOTICE);
#endif
	} else if (g_flightNetHostAbortReceived != 0 && frameCounter == NETWORK_DISCONNECT_FRAME &&
			   Net_IsHost() == 0) {
		FrontendDialog_ShowConfirmDialog(FrontendString_Get(FRONTSTR_737_THE_HOST_ABORTED_THE_MISSION),
										 FrontendString_Get(FRONTSTR_738_HOWEVER_YOU_ARE_STILL_CONNECTED),
										 FrontendString_Get(FRONTSTR_739_TO_THE_CURRENT_GAME_SESSION), NULL,
										 NULL);
#ifdef XVT_MODERN
		return XvtDialog_ContinueWith(XvtMissionDialogs_Resume, XVT_MISSION_NOTICE);
#endif
	}
	return 0;
}

// FUNCTION: XVT 0x500650
int MissionDebrief_DrawMissionOverviewPage(int frameCounter) {
	enum {
		PLAYER_COUNT = 8,
		TEAM_COUNT = 10,
		FLIGHT_GROUP_RANK_ID_BASE = PLAYER_COUNT,
		BATTLE_RESULT_COUNT = 3,
		IMPERIAL_TEAM_ID = 0,
		REBEL_TEAM_ID = 1,
		TEXT_FONT_SIZE = 12,
		TITLE_FONT_SIZE = 15,
		TITLE_LEFT = 84,
		TITLE_TOP = 90,
		TITLE_RIGHT = 434,
		TITLE_BOTTOM = 106,
		HEADER_Y = 114,
		FIRST_ROW_Y = 134,
		TEAM_NAME_X = 88,
		PLAYER_NAME_X = 103,
		SCORE_X = 228,
		KILLS_X = 298,
		DEATHS_X = 363,
		KILLED_NAME_RIGHT = 206,
		KILLED_VALUE_X = 208,
		KILLED_BY_NAME_X = 259,
		KILLED_BY_NAME_RIGHT = 377,
		KILLED_BY_VALUE_X = 379,
		ROW_HEIGHT = 15,
		ROW_CLIP_HEIGHT = 14,
		PULSE_PERIOD = 24,
		PULSE_PAIR_MASK = ~1,
		PULSE_PAIR_SHIFT = 1,
		TEXT_CODE_VALUE = 1,
		TEXT_CODE_PLACED = 2,
		TEXT_CODE_LABEL = 4,
		TEXT_CODE_RATING = 6,
		WHITE_COLOR = 0xFFFF,
	};

	int place;
	int previousScore;
	uint16_t placementCode;
	int teamPosition;
	int textY;
	int textX;
	RECT rect;
	RECT savedClipRect;

	if (Keyboard_IsKeyDown(0x12) && Keyboard_IsKeyDown(0x10) && Keyboard_IsKeyDown(0x11)) {
		FrontImage_DrawSprite("lh2", 184, 362);
	}
	FrontendDraw_RectAssign(&rect, TITLE_LEFT, TITLE_TOP, TITLE_RIGHT, TITLE_BOTTOM);
	if (g_pilotData.missionSequenceActive == 0) {
		FrontendText_DrawCentered(TITLE_FONT_SIZE, FrontendString_Get(FRONTSTR_311_MISSION_OVERVIEW), &rect,
								  WHITE_COLOR);
	} else {
		if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES) {
			if (g_pilotData.meleeTournamentSequenceState.missionCount -
					g_pilotData.meleeTournamentSequenceState.currentMissionIndex ==
				1) {
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_VALUE,
						FrontendString_Get(FRONTSTR_687_TOURNAMENT_MISSION_OVERVIEW), TEXT_CODE_LABEL,
						FrontendString_Get(FRONTSTR_206_DONE));
			} else {
				sprintf(g_frontendScratchBuffer, "%c%s %c%d %s %d %s", TEXT_CODE_VALUE,
						FrontendString_Get(FRONTSTR_687_TOURNAMENT_MISSION_OVERVIEW), TEXT_CODE_LABEL,
						g_pilotData.meleeTournamentSequenceState.currentMissionIndex + 1,
						FrontendString_Get(FRONTSTR_335_OF),
						g_pilotData.meleeTournamentSequenceState.missionCount,
						FrontendString_Get(FRONTSTR_340_MISSIONS));
			}
		} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
			int missionResultCounts[BATTLE_RESULT_COUNT];
			int missionIndex;
			int playerTeamResult;

			missionResultCounts[BATTLE_MISSION_RESULT_IMPERIAL_VICTORY] = 0;
			missionResultCounts[BATTLE_MISSION_RESULT_REBEL_VICTORY] = 0;
			missionResultCounts[BATTLE_MISSION_RESULT_DRAW] = 0;
			for (missionIndex = 0; missionIndex <= (int)g_pilotData.battleSequenceState.currentMissionIndex;
				 ++missionIndex) {
				++missionResultCounts[g_pilotData.battleSequenceState.missionResults[missionIndex]];
			}
			switch (g_pilotData.team) {
				case IMPERIAL_TEAM_ID:
					playerTeamResult = 0;
					break;
				case REBEL_TEAM_ID:
					playerTeamResult = 1;
					break;
				default:
					playerTeamResult = 0;
					break;
			}
			if (missionResultCounts[BATTLE_MISSION_RESULT_IMPERIAL_VICTORY] ==
					g_pilotData.battleSequenceState.victoriesNeeded ||
				missionResultCounts[BATTLE_MISSION_RESULT_REBEL_VICTORY] ==
					g_pilotData.battleSequenceState.victoriesNeeded) {
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_VALUE,
						FrontendString_Get(FRONTSTR_689_BATTLE_MISSION_OVERVIEW), TEXT_CODE_LABEL,
						FrontendString_Get(FRONTSTR_206_DONE));
			} else {
				sprintf(g_frontendScratchBuffer, "%c%s %c%d %s %d %s", TEXT_CODE_VALUE,
						FrontendString_Get(FRONTSTR_689_BATTLE_MISSION_OVERVIEW), TEXT_CODE_LABEL,
						missionResultCounts[playerTeamResult], FrontendString_Get(FRONTSTR_335_OF),
						g_pilotData.battleSequenceState.victoriesNeeded,
						FrontendString_Get(FRONTSTR_690_VICTORIES_NEEDED));
			}
		} else if (g_pilotData.campaignSequenceState.missionCount ==
					   g_pilotData.campaignSequenceState.currentMissionIndex &&
				   g_pilotData.campaignSequenceState.lastMissionCompleted == 1) {
			sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_VALUE,
					FrontendString_Get(FRONTSTR_785_CAMPAIGN_OVERVIEW), TEXT_CODE_LABEL,
					FrontendString_Get(FRONTSTR_206_DONE));
		} else {
			sprintf(g_frontendScratchBuffer, "%c%s %c%s%d", TEXT_CODE_VALUE,
					FrontendString_Get(FRONTSTR_785_CAMPAIGN_OVERVIEW), TEXT_CODE_LABEL,
					FrontendString_Get(FRONTSTR_344_MISSION),
					g_pilotData.campaignSequenceState.currentMissionIndex + 1);
		}
		FrontendText_DrawCentered(TITLE_FONT_SIZE, g_frontendScratchBuffer, &rect, WHITE_COLOR);
	}

	FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_330_SCORE), SCORE_X, HEADER_Y,
					  g_colorLightBlue);
	FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_336_KILLS), KILLS_X, HEADER_Y,
					  g_colorLightBlue);
	FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_539_DEATHS), DEATHS_X, HEADER_Y,
					  g_colorLightBlue);

	textY = FIRST_ROW_Y;
	place = 0;
	previousScore = g_pilotData.teams[g_debriefSortedTeamIds[0]].missionScore;
	for (teamPosition = 0; teamPosition < TEAM_COUNT; ++teamPosition) {

		textX = TEAM_NAME_X;
		if (g_debriefSortedTeamIds[teamPosition] == -1) {
			break;
		}
		if (g_debriefTeamHasPlayer[g_debriefSortedTeamIds[teamPosition]] != 0) {
			if (g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].missionScore != previousScore) {
				place = teamPosition;
				previousScore = g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].missionScore;
			}
			if (place < 3) {
				placementCode = TEXT_CODE_PLACED;
			} else {
				placementCode = TEXT_CODE_VALUE;
			}
			if (g_debriefRankByPilot == 0) {
				if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
					g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
					sprintf(g_frontendScratchBuffer, "%c%d. %c%s", placementCode, place + 1, TEXT_CODE_VALUE,
							g_frontendMission.teams[g_debriefSortedTeamIds[teamPosition]].name);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEAM_NAME_X, textY,
									  WHITE_COLOR);
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].missionScore);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, SCORE_X, textY, WHITE_COLOR);
					sprintf(g_frontendScratchBuffer, "%d (%d)",
							g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].kills,
							g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].killsShared);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, KILLS_X, textY, WHITE_COLOR);
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].killsAssist);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, DEATHS_X, textY, WHITE_COLOR);
					textX = PLAYER_NAME_X;
					textY += ROW_HEIGHT;
				} else if (g_debriefTeamUseSummaryRows[g_debriefSortedTeamIds[teamPosition]] != 0) {
					sprintf(g_frontendScratchBuffer, "%c%d. %c%s", placementCode, place + 1, TEXT_CODE_VALUE,
							g_frontendMission.teams[g_debriefSortedTeamIds[teamPosition]].name);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEAM_NAME_X, textY,
									  WHITE_COLOR);
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].missionScore);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, SCORE_X, textY, WHITE_COLOR);
					sprintf(g_frontendScratchBuffer, "%d (%d)",
							g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].kills,
							g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].killsShared);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, KILLS_X, textY, WHITE_COLOR);
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].killsAssist);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, DEATHS_X, textY, WHITE_COLOR);
					textY += ROW_HEIGHT;
					textX = PLAYER_NAME_X;
				} else {
					sprintf(g_frontendScratchBuffer, "%c%d. %c%s", placementCode, place + 1, TEXT_CODE_VALUE,
							g_frontendMission.teams[g_debriefSortedTeamIds[teamPosition]].name);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEAM_NAME_X, textY,
									  WHITE_COLOR);
					textY += ROW_HEIGHT;
					textX = PLAYER_NAME_X;
				}
			} else if (g_debriefTeamUseSummaryRows[g_debriefSortedTeamIds[teamPosition]] != 0) {
				int flightGroupIndex;

				for (flightGroupIndex = 0; flightGroupIndex < (int16_t)g_frontendMission.flightGroupCount;
					 ++flightGroupIndex) {
					if (g_frontendMission.flightGroups[flightGroupIndex].playerNumber != 0 &&
						g_frontendMission.flightGroups[flightGroupIndex].team ==
							g_debriefSortedTeamIds[teamPosition]) {
						sprintf(g_frontendScratchBuffer, "%c%d. %c%s %c%s", placementCode, place + 1,
								TEXT_CODE_RATING,
								FrontendString_Get(
									(FrontendStringId)(FRONTSTR_154_DRONE +
													   g_pilotData.flightGroupRating[flightGroupIndex])),
								TEXT_CODE_VALUE, g_frontendMission.flightGroups[flightGroupIndex].name);
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, WHITE_COLOR);
						sprintf(g_frontendScratchBuffer, "%d",
								g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].missionScore);
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, SCORE_X, textY,
										  WHITE_COLOR);
						sprintf(g_frontendScratchBuffer, "%d (%d)",
								g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].kills,
								g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].killsShared);
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, KILLS_X, textY,
										  WHITE_COLOR);
						textX = DEATHS_X;
						sprintf(g_frontendScratchBuffer, "%d",
								g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].killsAssist);
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, WHITE_COLOR);
						textY += ROW_HEIGHT;
					}
				}
			}

			if (g_debriefTeamUseSummaryRows[g_debriefSortedTeamIds[teamPosition]] == 0) {
				int sortedPlayerIndex;

				for (sortedPlayerIndex = 0; sortedPlayerIndex < PLAYER_COUNT; ++sortedPlayerIndex) {
					int playerIndex;

					playerIndex = g_debriefSortedPlayerIds[sortedPlayerIndex];
					if (playerIndex == -1) {
						break;
					}
					if (g_frontendMission.flightGroups[g_pilotData.networkPlayers[playerIndex].flightGroupId]
							.team != g_debriefSortedTeamIds[teamPosition]) {
						continue;
					}

					FrontendDraw_RectAssign(&rect, textX, textY, 225, textY + ROW_CLIP_HEIGHT);
					FrontendDisplay_GetScreenClipRect(&savedClipRect);
					FrontendDisplay_SetScreenClipRect640x480(&rect);
					if (g_debriefRankByPilot == 0) {
						if (g_pilotData.networkPlayers[playerIndex].hasLeft != 0) {
							sprintf(g_frontendScratchBuffer, "[%s %s]",
									FrontendString_Get(
										(FrontendStringId)(FRONTSTR_154_DRONE +
														   g_pilotData.networkPlayers[playerIndex].rating)),
									g_pilotData.networkPlayers[playerIndex].friendlyName);
						} else {
							sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_RATING,
									FrontendString_Get(
										(FrontendStringId)(FRONTSTR_154_DRONE +
														   g_pilotData.networkPlayers[playerIndex].rating)),
									TEXT_CODE_VALUE, g_pilotData.networkPlayers[playerIndex].friendlyName);
						}
					} else if (g_pilotData.networkPlayers[playerIndex].hasLeft != 0) {
						sprintf(g_frontendScratchBuffer, "%c%d. %c[%s %s]", placementCode, place + 1,
								TEXT_CODE_VALUE,
								FrontendString_Get(
									(FrontendStringId)(FRONTSTR_154_DRONE +
													   g_pilotData.networkPlayers[playerIndex].rating)),
								g_pilotData.networkPlayers[playerIndex].friendlyName);
					} else {
						sprintf(g_frontendScratchBuffer, "%c%d. %c%s %c%s", placementCode, place + 1,
								TEXT_CODE_RATING,
								FrontendString_Get(
									(FrontendStringId)(FRONTSTR_154_DRONE +
													   g_pilotData.networkPlayers[playerIndex].rating)),
								TEXT_CODE_VALUE, g_pilotData.networkPlayers[playerIndex].friendlyName);
					}

					if (g_pilotData.networkPlayers[playerIndex].hasLeft != 0) {
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, g_colorGray);
					} else {
						int localPlayerId = Net_GetLocalPlayerId();
						int color = g_colorLightBlue;
						if (localPlayerId == g_pilotData.networkPlayers[playerIndex].directPlayId ||
							g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							FrontendText_Draw(
								TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY,
								g_pulseColorRamp[((frameCounter % PULSE_PERIOD) & PULSE_PAIR_MASK) >>
												 PULSE_PAIR_SHIFT]);
						} else {
							FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, color);
						}
					}
					FrontendDisplay_SetScreenClipRect640x480(&savedClipRect);

					if ((g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
						 g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) &&
						g_debriefRankByPilot != 0) {
						sprintf(g_frontendScratchBuffer, "%d",
								g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].missionScore);
					} else {
						sprintf(g_frontendScratchBuffer, "%d",
								g_pilotData.networkPlayers[playerIndex].totalScore);
					}
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, SCORE_X, textY, WHITE_COLOR);
					if (g_debriefRankByPilot != 0) {
						sprintf(g_frontendScratchBuffer, "%d (%d)",
								g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].kills,
								g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].killsShared);
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, KILLS_X, textY,
										  WHITE_COLOR);
						sprintf(g_frontendScratchBuffer, "%d",
								g_pilotData.teams[g_debriefSortedTeamIds[teamPosition]].killsAssist);
					} else {
						sprintf(g_frontendScratchBuffer, "%d (%d)",
								g_pilotData.networkPlayers[playerIndex].kills,
								g_pilotData.networkPlayers[playerIndex].killsShared);
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, KILLS_X, textY,
										  WHITE_COLOR);
						sprintf(g_frontendScratchBuffer, "%d",
								g_pilotData.networkPlayers[playerIndex].totalLosses);
					}
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, DEATHS_X, textY, WHITE_COLOR);
					textY += ROW_HEIGHT;
					textX = g_debriefRankByPilot == 0 ? PLAYER_NAME_X : TEAM_NAME_X;
				}
			}

			if ((g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
				 g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) &&
				g_debriefRankByPilot == 0) {
				int flightGroupIndex;

				for (flightGroupIndex = 0; flightGroupIndex < (int16_t)g_frontendMission.flightGroupCount;
					 ++flightGroupIndex) {
					int playerIndex;

					if (g_frontendMission.flightGroups[flightGroupIndex].playerNumber == 0 ||
						g_frontendMission.flightGroups[flightGroupIndex].team !=
							g_debriefSortedTeamIds[teamPosition]) {
						continue;
					}
					for (playerIndex = 0; playerIndex < PLAYER_COUNT; ++playerIndex) {
						if (g_pilotData.networkPlayers[playerIndex].directPlayId != 0 &&
							g_pilotData.networkPlayers[playerIndex].flightGroupId == flightGroupIndex) {
							break;
						}
					}
					if (playerIndex == PLAYER_COUNT) {
						sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_RATING,
								FrontendString_Get(
									(FrontendStringId)(FRONTSTR_154_DRONE +
													   g_pilotData.flightGroupRating[flightGroupIndex])),
								TEXT_CODE_VALUE, g_frontendMission.flightGroups[flightGroupIndex].name);
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, WHITE_COLOR);
						textY += ROW_HEIGHT;
					}
				}
			}
		}
	}

	{
		int killedHeaderY;
		int killedRowY;
		int killedByRowY;
		int killIndex;
		int rankId;

		killedHeaderY = textY + ROW_HEIGHT;
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_542_KILLED), textX, killedHeaderY,
						  g_colorLightBlue);
		killedRowY = killedHeaderY + ROW_HEIGHT;
		for (killIndex = 0; killIndex < PLAYER_COUNT; ++killIndex) {

			rankId = g_debriefKillsOnRankIds[killIndex];
			if (rankId == -1) {
				break;
			}
			if (rankId < FLIGHT_GROUP_RANK_ID_BASE) {
				if (Net_GetLocalPlayerId() == g_pilotData.networkPlayers[rankId].directPlayId ||
					(g_pilotData.killsFullOnPlayer[rankId] == 0 &&
					 g_pilotData.killsSharedOnPlayer[rankId] == 0)) {
					continue;
				}
				FrontendDraw_RectAssign(&rect, TEAM_NAME_X, killedRowY, KILLED_NAME_RIGHT,
										killedRowY + ROW_CLIP_HEIGHT);
				FrontendDisplay_GetScreenClipRect(&savedClipRect);
				FrontendDisplay_SetScreenClipRect640x480(&rect);
				if (g_pilotData.networkPlayers[rankId].hasLeft != 0) {
					sprintf(g_frontendScratchBuffer, "[%s %s]",
							FrontendString_Get((FrontendStringId)(FRONTSTR_154_DRONE +
																  g_pilotData.networkPlayers[rankId].rating)),
							g_pilotData.networkPlayers[rankId].friendlyName);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEAM_NAME_X, killedRowY,
									  g_colorGray);
				} else {
					sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_RATING,
							FrontendString_Get((FrontendStringId)(FRONTSTR_154_DRONE +
																  g_pilotData.networkPlayers[rankId].rating)),
							TEXT_CODE_LABEL, g_pilotData.networkPlayers[rankId].friendlyName);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEAM_NAME_X, killedRowY,
									  WHITE_COLOR);
				}
				FrontendDisplay_SetScreenClipRect640x480(&savedClipRect);
				sprintf(g_frontendScratchBuffer, "%d(%d)", g_pilotData.killsFullOnPlayer[rankId],
						g_pilotData.killsSharedOnPlayer[rankId]);
			} else {
				int flightGroupIndex;

				flightGroupIndex = rankId - FLIGHT_GROUP_RANK_ID_BASE;
				if (g_debriefTeamUseSummaryRows[g_frontendMission.flightGroups[flightGroupIndex].team] == 0 ||
					(g_pilotData.killsFullOnFlightGroup[flightGroupIndex] == 0 &&
					 g_pilotData.killsSharedOnFlightGroup[flightGroupIndex] == 0)) {
					continue;
				}
				sprintf(
					g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_RATING,
					FrontendString_Get((FrontendStringId)(FRONTSTR_154_DRONE +
														  g_pilotData.flightGroupRating[flightGroupIndex])),
					TEXT_CODE_LABEL, g_frontendMission.flightGroups[flightGroupIndex].name);
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEAM_NAME_X, killedRowY,
								  WHITE_COLOR);
				sprintf(g_frontendScratchBuffer, "%d(%d)",
						g_pilotData.killsFullOnFlightGroup[flightGroupIndex],
						g_pilotData.killsSharedOnFlightGroup[flightGroupIndex]);
			}
			FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, KILLED_VALUE_X, killedRowY,
							  WHITE_COLOR);
			killedRowY += ROW_HEIGHT;
		}

		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_541_KILLED_BY), KILLED_BY_NAME_X,
						  killedHeaderY, g_colorLightBlue);
		killedByRowY = killedHeaderY + ROW_HEIGHT;
		for (killIndex = 0; killIndex < PLAYER_COUNT; ++killIndex) {

			rankId = g_debriefKillsOnRankIds[killIndex];
			if (rankId == -1) {
				break;
			}
			if (rankId < FLIGHT_GROUP_RANK_ID_BASE) {
				if (Net_GetLocalPlayerId() == g_pilotData.networkPlayers[rankId].directPlayId ||
					(g_pilotData.killsFullFromPlayer[rankId] == 0 &&
					 g_pilotData.killsSharedFromPlayer[rankId] == 0)) {
					continue;
				}
				FrontendDraw_RectAssign(&rect, KILLED_BY_NAME_X, killedByRowY, KILLED_BY_NAME_RIGHT,
										killedByRowY + ROW_CLIP_HEIGHT);
				FrontendDisplay_GetScreenClipRect(&savedClipRect);
				FrontendDisplay_SetScreenClipRect640x480(&rect);
				if (g_pilotData.networkPlayers[rankId].hasLeft != 0) {
					sprintf(g_frontendScratchBuffer, "[%s %s]",
							FrontendString_Get((FrontendStringId)(FRONTSTR_154_DRONE +
																  g_pilotData.networkPlayers[rankId].rating)),
							g_pilotData.networkPlayers[rankId].friendlyName);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, KILLED_BY_NAME_X, killedByRowY,
									  g_colorGray);
				} else {
					sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_RATING,
							FrontendString_Get((FrontendStringId)(FRONTSTR_154_DRONE +
																  g_pilotData.networkPlayers[rankId].rating)),
							TEXT_CODE_LABEL, g_pilotData.networkPlayers[rankId].friendlyName);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, KILLED_BY_NAME_X, killedByRowY,
									  WHITE_COLOR);
				}
				FrontendDisplay_SetScreenClipRect640x480(&savedClipRect);
				sprintf(g_frontendScratchBuffer, "%d(%d)", g_pilotData.killsFullFromPlayer[rankId],
						g_pilotData.killsSharedFromPlayer[rankId]);
			} else {
				int flightGroupIndex;

				flightGroupIndex = rankId - FLIGHT_GROUP_RANK_ID_BASE;
				if (g_debriefTeamUseSummaryRows[g_frontendMission.flightGroups[flightGroupIndex].team] == 0 ||
					(g_pilotData.killsFullFromFlightGroup[flightGroupIndex] == 0 &&
					 g_pilotData.killsSharedFromFlightGroup[flightGroupIndex] == 0)) {
					continue;
				}
				sprintf(
					g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_RATING,
					FrontendString_Get((FrontendStringId)(FRONTSTR_154_DRONE +
														  g_pilotData.flightGroupRating[flightGroupIndex])),
					TEXT_CODE_LABEL, g_frontendMission.flightGroups[flightGroupIndex].name);
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, KILLED_BY_NAME_X, killedByRowY,
								  WHITE_COLOR);
				sprintf(g_frontendScratchBuffer, "%d(%d)",
						g_pilotData.killsFullFromFlightGroup[flightGroupIndex],
						g_pilotData.killsSharedFromFlightGroup[flightGroupIndex]);
			}
			FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, KILLED_BY_VALUE_X, killedByRowY,
							  WHITE_COLOR);
			killedByRowY += ROW_HEIGHT;
		}
	}
	return 1;
}

// FUNCTION: XVT 0x501710
int MissionDebrief_DrawPlayerStatisticsPage(void) {
	enum {
		PLAYER_COUNT = 8,
		CRAFT_TYPE_COUNT = 100,
		PLAYER_RATING_COUNT = 25,
		AI_RATING_COUNT = 6,
		MISSION_TYPE = 0,
		MISSION_TYPE_COUNT = 1,
		AWARD_COUNT = 4,
		VISIBLE_ROW_COUNT = 21,
		SCROLL_PAGE_STEP = 5,
		TEXT_FONT_SIZE = 12,
		TITLE_FONT_SIZE = 15,
		TEXT_X = 88,
		VALUE_X = 268,
		FIRST_ROW_Y = 111,
		ROW_HEIGHT = 15,
		TEXT_CODE_VALUE = 1,
		TEXT_CODE_LABEL = 4,
		TEXT_CODE_RATING = 6,
		WHITE_COLOR = 0xFFFF,
		HIGHEST_RATING = PLAYER_RATING_COUNT - 1,
	};

	RECT rect;
	char missionTimeText[20];
	int localPlayerIndex;
	int scrollRow;
	int row;
	int textY;
	int awardIndex;
	int craftType;
	int playerIndex;
	int rating;
	int missionType;
	int totalRows;

	row = 0;
	FrontendDraw_RectAssign(&rect, 84, 90, 434, 106);
	FrontendText_DrawCentered(TITLE_FONT_SIZE, FrontendString_Get(FRONTSTR_312_PLAYER_STATISTICS), &rect,
							  WHITE_COLOR);

	if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		localPlayerIndex = 0;
	} else {
		localPlayerIndex = 0;
		for (localPlayerIndex = 0; localPlayerIndex < PLAYER_COUNT; ++localPlayerIndex) {
			if (Net_GetLocalPlayerId() == g_pilotData.networkPlayers[localPlayerIndex].directPlayId) {
				break;
			}
		}
	}
	if (localPlayerIndex == PLAYER_COUNT) {
		return 0;
	}

	if (g_debriefStatsPageNeedsRebuild != 0) {
		int rowCount;

		rowCount = 13;
		g_debriefPlayerStatsScrollRow = 0;
		g_debriefStatsPageNeedsRebuild = 0;
		if (g_frontendMission.header.goalsUnimportant == 0 &&
			g_pilotData.missionDirectoryId != MISSION_DIRECTORY_MELEES) {
			g_debriefPlayerStatsRowCount = rowCount;
			if (g_pilotData.missionDirectoryId != MISSION_DIRECTORY_TOURNAMENTS) {
				++rowCount;
			}
		}
		if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			rowCount += 5;
		}
		if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES) {
			++rowCount;
		}
		if (g_pilotData.promotionDelta != PILOT_PROMOTION_NONE) {
			++rowCount;
		}
		for (awardIndex = 0; awardIndex < AWARD_COUNT; ++awardIndex) {
			if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[awardIndex] != 0) {
				++rowCount;
			}
		}

		g_debriefLossesToNonPlayerPilotsTotal[MISSION_TYPE] = 0;
		g_debriefLossesToPlayerPilotsTotal[MISSION_TYPE] = 0;
		g_debriefNonPlayerKillsSharedTotal[MISSION_TYPE] = 0;
		g_debriefNonPlayerKillsByMissionType[MISSION_TYPE] = 0;
		g_debriefPlayerKillsSharedTotal[MISSION_TYPE] = 0;
		g_debriefPlayerKillsByMissionType[MISSION_TYPE] = 0;
		g_debriefTotalKillsSharedByMissionType[MISSION_TYPE] = 0;
		g_debriefAssistTotalByMissionType[MISSION_TYPE] = 0;
		g_debriefHasCraftKillsByTypeSection = 0;
		for (craftType = 0; craftType < CRAFT_TYPE_COUNT; ++craftType) {
			g_debriefCraftKillRowHasData = 0;
			for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
				if (g_pilotData.objectStats.killsPerCraftPerMT[missionType][craftType] != 0 ||
					g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][craftType] != 0) {
					g_debriefCraftKillRowHasData = 1;
					g_debriefHasCraftKillsByTypeSection = 1;
				}
			}
			if (g_debriefCraftKillRowHasData != 0) {
				++rowCount;
			}
		}
		if (g_debriefHasCraftKillsByTypeSection != 0) {
			rowCount += 2;
		}

		g_debriefHasPlayerKillsByRating = 0;
		if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			for (playerIndex = 0; playerIndex < PLAYER_COUNT; ++playerIndex) {
				if (g_pilotData.killsFullOnPlayer[playerIndex] != 0 ||
					g_pilotData.killsSharedOnPlayer[playerIndex] != 0) {
					g_debriefHasPlayerKillsByRating = 1;
					++rowCount;
				}
			}
			if (g_debriefHasPlayerKillsByRating != 0) {
				rowCount += 2;
			}
		}

		g_debriefPlayerStatsRowCount = rowCount;
		g_debriefHasLossesFromPlayersSection = 0;
		if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			for (playerIndex = 0; playerIndex < PLAYER_COUNT; ++playerIndex) {
				if (g_pilotData.killsFullFromPlayer[playerIndex] != 0 ||
					g_pilotData.killsSharedFromPlayer[playerIndex] != 0) {
					g_debriefHasLossesFromPlayersSection = 1;
					++rowCount;
				}
			}
			g_debriefPlayerStatsRowCount = rowCount;
			if (g_debriefHasLossesFromPlayersSection != 0) {
				g_debriefPlayerStatsRowCount += 2;
			}
		}

		for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
			for (craftType = 0; craftType < CRAFT_TYPE_COUNT; ++craftType) {
				int assistCount;
				int sharedKillCount;

				assistCount = g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][craftType];
				sharedKillCount = g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][craftType];
				g_debriefAssistTotalByMissionType[missionType] += assistCount;
				g_debriefTotalKillsSharedByMissionType[missionType] += sharedKillCount;
			}
		}
		for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
			for (rating = 0; rating < PLAYER_RATING_COUNT; ++rating) {
				int fullKillCount;
				int sharedKillCount;

				fullKillCount = g_pilotData.objectStats.killsFullOnPlayerRatingPerMT[missionType][rating];
				sharedKillCount = g_pilotData.objectStats.killsSharedOnPlayerRatingPerMT[missionType][rating];
				g_debriefPlayerKillsByMissionType[missionType] += fullKillCount;
				g_debriefPlayerKillsSharedTotal[missionType] += sharedKillCount;
			}
		}
		for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
			for (rating = 0; rating < AI_RATING_COUNT; ++rating) {
				int fullKillCount;
				int sharedKillCount;

				fullKillCount = g_pilotData.objectStats.killsFullOnAIRatingPerMT[missionType][rating];
				sharedKillCount = g_pilotData.objectStats.killsSharedOnAIRatingPerMT[missionType][rating];
				g_debriefNonPlayerKillsByMissionType[missionType] += fullKillCount;
				g_debriefNonPlayerKillsSharedTotal[missionType] += sharedKillCount;
			}
		}
		for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
			for (rating = 0; rating < PLAYER_RATING_COUNT; ++rating) {
				g_debriefLossesToPlayerPilotsTotal[missionType] +=
					g_pilotData.objectStats.killedByPlayerRatingPerMT[missionType][rating];
			}
		}
		for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
			for (rating = 0; rating < AI_RATING_COUNT; ++rating) {
				g_debriefLossesToNonPlayerPilotsTotal[missionType] +=
					g_pilotData.objectStats.killedByAIRatingPerMT[missionType][rating];
			}
		}
	}

	FrontendDraw_RectAssign(&rect, 425, 107, 434, 433);
	totalRows = g_debriefPlayerStatsRowCount;
	if (totalRows > VISIBLE_ROW_COUNT) {
		scrollRow = FrontendScrollbar_Draw(&rect, g_debriefPlayerStatsScrollRow, totalRows, 0,
										   SCROLL_PAGE_STEP, g_colorNavy, 10);
	} else {
		scrollRow = g_debriefPlayerStatsScrollRow;
	}
	g_debriefPlayerStatsScrollRow = scrollRow;

	textY = FIRST_ROW_Y;

	if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES) {
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			const char* rankingGroup;

			if (g_debriefRankByPilot != 0) {
				rankingGroup = FrontendString_Get(FRONTSTR_368_PILOTS);
			} else {
				rankingGroup = FrontendString_Get(FRONTSTR_691_TEAMS);
			}
			sprintf(g_frontendScratchBuffer, "%c%s %c%s %c%s %c%d %c%s", TEXT_CODE_LABEL,
					FrontendString_Get(FRONTSTR_367_PLACE), TEXT_CODE_VALUE,
					FrontendString_Get((FrontendStringId)(FRONTSTR_318_1ST + g_debriefLocalTeamRankIndex)),
					TEXT_CODE_LABEL, FrontendString_Get(FRONTSTR_335_OF), TEXT_CODE_VALUE,
					g_debriefActiveTeamCount, TEXT_CODE_LABEL, rankingGroup);
			FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEXT_X, textY, WHITE_COLOR);
			textY += ROW_HEIGHT;
		}
		++row;
	}

	if (g_frontendMission.header.goalsUnimportant == 0 &&
		g_pilotData.missionDirectoryId != MISSION_DIRECTORY_MELEES &&
		g_pilotData.missionDirectoryId != MISSION_DIRECTORY_TOURNAMENTS) {
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			int playerTeamResult;
			int opposingTeamResult;
			const char* resultText;

			playerTeamResult = g_pilotData.teams[g_pilotData.team].isMissionCompleted;
			if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES) {
				opposingTeamResult = playerTeamResult == 0;
			} else {
				opposingTeamResult = g_pilotData.teams[g_pilotData.team ^ 1].isMissionCompleted;
			}
			if (opposingTeamResult == playerTeamResult) {
				resultText = FrontendString_Get(FRONTSTR_347_DRAW);
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_LABEL,
						FrontendString_Get(FRONTSTR_369_RESULT), TEXT_CODE_VALUE, resultText);
			} else if (playerTeamResult == 1) {
				Frontend_FormatSecondsToClockString(
					g_pilotData
						.teams[g_frontendMission
								   .flightGroups[g_pilotData.networkPlayers[localPlayerIndex].flightGroupId]
								   .team]
						.missionTime);
				strcpy(missionTimeText, g_frontendScratchBuffer);
				sprintf(g_frontendScratchBuffer, "%c%s %c%s %s.", TEXT_CODE_LABEL,
						FrontendString_Get(FRONTSTR_369_RESULT), TEXT_CODE_VALUE,
						FrontendString_Get(FRONTSTR_370_COMPLETED_MISSION_IN), missionTimeText);
			} else {
				resultText = FrontendString_Get(FRONTSTR_371_FAILED_MISSION);
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_LABEL,
						FrontendString_Get(FRONTSTR_369_RESULT), TEXT_CODE_VALUE, resultText);
			}
			FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEXT_X, textY, WHITE_COLOR);
			textY += ROW_HEIGHT;
		}
		++row;
	}

	if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
		sprintf(g_frontendScratchBuffer, "%c%s %c%d", TEXT_CODE_LABEL,
				FrontendString_Get(FRONTSTR_333_MISSION_SCORE), TEXT_CODE_VALUE,
				g_pilotData.networkPlayers[localPlayerIndex].totalScore);
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEXT_X, textY, WHITE_COLOR);
		textY += ROW_HEIGHT;
	}
	++row;

	if (g_pilotData.promotionDelta != PILOT_PROMOTION_NONE) {
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			sprintf(g_frontendScratchBuffer, "rank%d", g_pilotData.rating);
			FrontImage_DrawSprite(g_frontendScratchBuffer, TEXT_X, textY + 1);
			sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_LABEL,
					FrontendString_Get(
						(FrontendStringId)(FRONTSTR_375_NO_PROMOTION + g_pilotData.promotionDelta)),
					TEXT_CODE_RATING, FrontendString_Get((FrontendStringId)(122 + g_pilotData.rating)));
			FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, 145, textY, WHITE_COLOR);
			textY += ROW_HEIGHT;
		}
		++row;
	}

	g_debriefCraftKillRowHasData = 0;
	for (awardIndex = 0; awardIndex < AWARD_COUNT; ++awardIndex) {
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[awardIndex] == 0) {
			continue;
		}
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			int awardTextWidth;
			FrontendStringId awardLevelString;

			if (g_debriefCraftKillRowHasData == 0) {
				FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_377_AWARD), TEXT_X, textY,
								  g_colorLightBlue);
				g_debriefCraftKillRowHasData = 1;
			}
			awardTextWidth =
				FrontendText_MeasureWidth(FrontendString_Get(FRONTSTR_377_AWARD), TEXT_FONT_SIZE) + 10;
			if (awardIndex == 2) {
				if (g_pilotData.currentFactionId == 0) {
					sprintf(g_frontendScratchBuffer, "rcitlvl%d",
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.missionAwards[awardIndex]);
				} else {
					sprintf(g_frontendScratchBuffer, "citlvl%d",
							g_pilotData.factionStatistics[g_pilotData.currentFactionId]
								.missionAwards[awardIndex]);
				}
			} else {
				sprintf(
					g_frontendScratchBuffer, "medlvl%d",
					g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[awardIndex]);
			}
			FrontImage_DrawSprite(g_frontendScratchBuffer, awardTextWidth + TEXT_X, textY);
			if (awardIndex == 2) {
				awardLevelString =
					(FrontendStringId)(659 + g_pilotData.factionStatistics[g_pilotData.currentFactionId]
												 .missionAwards[awardIndex]);
			} else {
				awardLevelString =
					(FrontendStringId)(381 + g_pilotData.factionStatistics[g_pilotData.currentFactionId]
												 .missionAwards[awardIndex]);
			}
			sprintf(g_frontendScratchBuffer, "%s - %s",
					FrontendString_Get((FrontendStringId)(FRONTSTR_378_MELEE_PLAQUE + awardIndex)),
					FrontendString_Get(awardLevelString));
			FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, awardTextWidth + 128, textY,
							  WHITE_COLOR);
			textY += ROW_HEIGHT;
		}
		++row;
	}

	if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
		textY += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_350_SUMMARY_OF_KILLS), TEXT_X, textY,
						  g_colorLightBlue);
		textY += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_297_TOTAL_KILLS), TEXT_X, textY,
						  g_colorLightBlue);
		sprintf(g_frontendScratchBuffer, "%d (%d)", g_pilotData.objectStats.totalKillsPerMT[MISSION_TYPE],
				g_debriefTotalKillsSharedByMissionType[MISSION_TYPE]);
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
		textY += ROW_HEIGHT;
	}
	++row;

	if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_351_PLAYER_KILLS), TEXT_X, textY,
							  g_colorLightBlue);
			if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES) {
				sprintf(g_frontendScratchBuffer, "----");
			} else {
				sprintf(g_frontendScratchBuffer, "%d (%d)", g_debriefPlayerKillsByMissionType[MISSION_TYPE],
						g_debriefPlayerKillsSharedTotal[MISSION_TYPE]);
			}
			FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
			textY += ROW_HEIGHT;
		}
		++row;
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_352_NON_PLAYER_KILLS), TEXT_X,
							  textY, g_colorLightBlue);
			sprintf(g_frontendScratchBuffer, "%d (%d)", g_debriefNonPlayerKillsByMissionType[MISSION_TYPE],
					g_debriefNonPlayerKillsSharedTotal[MISSION_TYPE]);
			FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
			textY += ROW_HEIGHT;
		}
		++row;
	}

	if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_353_ASSISTS), TEXT_X, textY,
						  g_colorLightBlue);
		sprintf(g_frontendScratchBuffer, "%d", g_debriefAssistTotalByMissionType[MISSION_TYPE]);
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
		textY += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_354_HIDDEN_CARGO_FOUND), TEXT_X, textY,
						  g_colorLightBlue);
		sprintf(g_frontendScratchBuffer, "%d",
				g_pilotData.objectStats.numSpecialInspectedPerMT[MISSION_TYPE]);
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
		textY += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
		unsigned int accuracy;

		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_355_LASER_ACCURACY), TEXT_X, textY,
						  g_colorLightBlue);
		accuracy = 0;
		if (g_pilotData.objectStats.energyFiredPerMT[MISSION_TYPE] != 0) {
			accuracy = (unsigned int)(100 * g_pilotData.objectStats.energyHitsPerMT[MISSION_TYPE]) /
					   (unsigned int)g_pilotData.objectStats.energyFiredPerMT[MISSION_TYPE];
		}
		sprintf(g_frontendScratchBuffer, "%d%%", accuracy);
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
		textY += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
		unsigned int accuracy;

		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_356_WARHEAD_ACCURACY), TEXT_X, textY,
						  g_colorLightBlue);
		accuracy = 0;
		if (g_pilotData.objectStats.warheadsFiredPerMT[MISSION_TYPE] != 0) {
			accuracy = (unsigned int)(100 * g_pilotData.objectStats.warheadsHitsPerMT[MISSION_TYPE]) /
					   (unsigned int)g_pilotData.objectStats.warheadsFiredPerMT[MISSION_TYPE];
		}
		sprintf(g_frontendScratchBuffer, "%d%%", accuracy);
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
		textY += ROW_HEIGHT;
	}
	++row;

	if (g_debriefHasPlayerKillsByRating != 0) {
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			textY += ROW_HEIGHT;
		}
		++row;
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_357_PLAYER_KILLS_BY_RANK), TEXT_X,
							  textY, g_colorLightBlue);
			textY += ROW_HEIGHT;
		}
		++row;
		for (rating = HIGHEST_RATING; rating >= 0; --rating) {
			for (playerIndex = 0; playerIndex < PLAYER_COUNT; ++playerIndex) {
				if (g_pilotData.networkPlayers[playerIndex].rating != rating ||
					(g_pilotData.killsFullOnPlayer[playerIndex] == 0 &&
					 g_pilotData.killsSharedOnPlayer[playerIndex] == 0)) {
					continue;
				}
				if (row >= g_debriefPlayerStatsScrollRow &&
					row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
					sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_RATING,
							FrontendString_Get((FrontendStringId)(FRONTSTR_154_DRONE + rating)),
							TEXT_CODE_VALUE, g_pilotData.networkPlayers[playerIndex].friendlyName);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEXT_X, textY,
									  g_colorLightBlue);
					sprintf(g_frontendScratchBuffer, "%d(%d)", g_pilotData.killsFullOnPlayer[playerIndex],
							g_pilotData.killsSharedOnPlayer[playerIndex]);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
					textY += ROW_HEIGHT;
				}
				++row;
			}
		}
	}

	if (g_debriefHasCraftKillsByTypeSection != 0) {
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			textY += ROW_HEIGHT;
		}
		++row;
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_358_CRAFT_KILLS_BY_TYPE), TEXT_X,
							  textY, g_colorLightBlue);
			textY += ROW_HEIGHT;
		}
		++row;
	}
	for (craftType = 0; craftType < CRAFT_TYPE_COUNT; ++craftType) {
		g_debriefCraftKillRowHasData = 0;
		for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
			if (g_pilotData.objectStats.killsPerCraftPerMT[missionType][craftType] != 0 ||
				g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][craftType] != 0) {
				g_debriefCraftKillRowHasData = 1;
			}
		}
		if (g_debriefCraftKillRowHasData == 0) {
			continue;
		}
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get((FrontendStringId)(21 + craftType)), TEXT_X,
							  textY, g_colorRed);
			sprintf(g_frontendScratchBuffer, "%d (%d)",
					g_pilotData.objectStats.killsPerCraftPerMT[MISSION_TYPE][craftType],
					g_pilotData.objectStats.killsSharedPerCraftPerMT[MISSION_TYPE][craftType]);
			FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
			textY += ROW_HEIGHT;
		}
		++row;
	}

	if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
		textY += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_360_TOTAL_LOSSES), TEXT_X, textY,
						  g_colorLightBlue);
		textY += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_361_TOTAL_CRAFT_LOSSES), TEXT_X, textY,
						  g_colorLightBlue);
		sprintf(g_frontendScratchBuffer, "%d", g_pilotData.objectStats.totalCraftLossesPerMT[MISSION_TYPE]);
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
		textY += ROW_HEIGHT;
	}
	++row;

	if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_362_TO_PLAYER_PILOTS), TEXT_X,
							  textY, g_colorLightBlue);
			sprintf(g_frontendScratchBuffer, "%d", g_debriefLossesToPlayerPilotsTotal[MISSION_TYPE]);
			FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
			textY += ROW_HEIGHT;
		}
		++row;
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_363_TO_NON_PLAYER_PILOTS), TEXT_X,
							  textY, g_colorLightBlue);
			sprintf(g_frontendScratchBuffer, "%d", g_debriefLossesToNonPlayerPilotsTotal[MISSION_TYPE]);
			FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
			textY += ROW_HEIGHT;
		}
		++row;
	}

	if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_364_TO_STARSHIPS), TEXT_X, textY,
						  g_colorLightBlue);
		sprintf(g_frontendScratchBuffer, "%d", g_pilotData.objectStats.lossesByStarshipsPerMT[MISSION_TYPE]);
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
		textY += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_365_TO_MINES), TEXT_X, textY,
						  g_colorLightBlue);
		sprintf(g_frontendScratchBuffer, "%d", g_pilotData.objectStats.lossesByMinesPerMT[MISSION_TYPE]);
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
		textY += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
		FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_366_FROM_COLLISIONS), TEXT_X, textY,
						  g_colorLightBlue);
		sprintf(g_frontendScratchBuffer, "%d", g_pilotData.objectStats.lossesByCollisionsPerMT[MISSION_TYPE]);
		FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
		textY += ROW_HEIGHT;
	}
	++row;

	if (g_debriefHasLossesFromPlayersSection != 0) {
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			textY += ROW_HEIGHT;
		}
		++row;
		if (row >= g_debriefPlayerStatsScrollRow && row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
			FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_388_LOSSES_FROM_PLAYERS), TEXT_X,
							  textY, g_colorLightBlue);
			textY += ROW_HEIGHT;
		}
		++row;
		for (rating = HIGHEST_RATING; rating >= 0; --rating) {
			for (playerIndex = 0; playerIndex < PLAYER_COUNT; ++playerIndex) {
				if (g_pilotData.networkPlayers[playerIndex].rating != rating ||
					(g_pilotData.killsFullFromPlayer[playerIndex] == 0 &&
					 g_pilotData.killsSharedFromPlayer[playerIndex] == 0)) {
					continue;
				}
				if (row >= g_debriefPlayerStatsScrollRow &&
					row - g_debriefPlayerStatsScrollRow < VISIBLE_ROW_COUNT) {
					sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_RATING,
							FrontendString_Get((FrontendStringId)(FRONTSTR_154_DRONE + rating)),
							TEXT_CODE_VALUE, g_pilotData.networkPlayers[playerIndex].friendlyName);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, TEXT_X, textY,
									  g_colorLightBlue);
					sprintf(g_frontendScratchBuffer, "%d(%d)", g_pilotData.killsFullFromPlayer[playerIndex],
							g_pilotData.killsSharedFromPlayer[playerIndex]);
					FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, VALUE_X, textY, WHITE_COLOR);
					textY += ROW_HEIGHT;
				}
				++row;
			}
		}
	}

	return 1;
}

// FUNCTION: XVT 0x502A50
int MissionDebrief_DrawTeamFlightGroupStatisticsPage(void) {
	RECT titleRect;
	int totalKills;
	int totalSharedKills;
	int totalDamaged;
	int totalInspected;
	int totalLost;
	int playerIndex;
	int textX;
	int textY;
	PilotNetworkPlayer* player;

	FrontendDraw_RectAssign(&titleRect, 84, 90, 434, 106);
	FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_313_TEAM_STATISTICS), &titleRect, 0xFFFF);
	FrontendText_Draw(12, FrontendString_Get(FRONTSTR_186_PLAYERS), 88, 114, g_colorLightBlue);
	FrontendText_Draw(12, FrontendString_Get(FRONTSTR_330_SCORE), 248, 114, g_colorLightBlue);
	FrontendText_Draw(12, FrontendString_Get(FRONTSTR_336_KILLS), 308, 114, g_colorLightBlue);
	FrontendText_Draw(12, FrontendString_Get(FRONTSTR_337_INSPECTED), 368, 114, g_colorLightBlue);

	totalKills = 0;
	totalSharedKills = 0;
	totalDamaged = 0;
	totalInspected = 0;
	totalLost = 0;
	textX = 88;
	textY = 134;
	for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
		player = &g_pilotData.networkPlayers[playerIndex];
		if (player->directPlayId != 0) {
			if (g_frontendMission.flightGroups[player->flightGroupId].team == g_pilotData.team) {
				totalKills += player->kills;
				totalSharedKills += player->killsShared;
				totalDamaged += player->killsAssist;
				totalInspected += player->unknown34;
				totalLost += player->totalLosses;

				FrontendText_Draw(12, player->friendlyName, textX, textY, 0xFFFF);
				textX += 160;
				sprintf(g_frontendScratchBuffer, "%d", player->totalScore);
				FrontendText_Draw(12, g_frontendScratchBuffer, textX, textY, 0xFFFF);
				textX += 60;
				sprintf(g_frontendScratchBuffer, "%d(%d)", player->kills, player->killsShared);
				FrontendText_Draw(12, g_frontendScratchBuffer, textX, textY, 0xFFFF);
				textX += 60;
				sprintf(g_frontendScratchBuffer, "%d", player->unknown34);
				FrontendText_Draw(12, g_frontendScratchBuffer, textX, textY, 0xFFFF);
			}
			textX = 88;
			textY += 20;
		}
	}

	FrontendText_Draw(12, FrontendString_Get(FRONTSTR_297_TOTAL_KILLS), 88, 306, g_colorLightBlue);
	sprintf(g_frontendScratchBuffer, "%d", totalKills);
	FrontendText_Draw(12, g_frontendScratchBuffer, 288, 306, 0xFFFF);
	FrontendText_Draw(12, FrontendString_Get(FRONTSTR_334_SHARED_KILLS), 88, 326, g_colorLightBlue);
	sprintf(g_frontendScratchBuffer, "%d", totalSharedKills);
	FrontendText_Draw(12, g_frontendScratchBuffer, 288, 326, 0xFFFF);
	FrontendText_Draw(12, FrontendString_Get(FRONTSTR_299_TOTAL_DAMAGED), 88, 346, g_colorLightBlue);
	sprintf(g_frontendScratchBuffer, "%d", totalDamaged);
	FrontendText_Draw(12, g_frontendScratchBuffer, 288, 346, 0xFFFF);
	FrontendText_Draw(12, FrontendString_Get(FRONTSTR_300_TOTAL_INSPECTED), 88, 366, g_colorLightBlue);
	sprintf(g_frontendScratchBuffer, "%d", totalInspected);
	FrontendText_Draw(12, g_frontendScratchBuffer, 288, 366, 0xFFFF);
	FrontendText_Draw(12, FrontendString_Get(FRONTSTR_303_CRAFT_LOST), 88, 386, g_colorLightBlue);
	sprintf(g_frontendScratchBuffer, "%d", totalLost);
	FrontendText_Draw(12, g_frontendScratchBuffer, 288, 386, 0xFFFF);
	return 1;
}

// FUNCTION: XVT 0x502E20
int MissionDebrief_DrawSkirmishResultsPage(void) {
	RECT rect;
	RECT resourceRect;
	int awardTextWidth;
	int imperialVictories = 0;
	int rebelVictories = 0;
	unsigned char resultColor;
	/* The original UI treats the persisted mission index as an unsigned bound. */
	unsigned int missionIndex;
	int victoriesNeeded;
	int awardX;
	char resourceName[52];

	if (Keyboard_IsKeyDown(0x12) && Keyboard_IsKeyDown(0x10) && Keyboard_IsKeyDown(0x78)) {
		FrontImage_DrawSprite("pl2", 175, 342);
	}
	FrontendDraw_RectAssign(&rect, 84, 90, 434, 106);
	FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_314_BATTLE_SUMMARY), &rect, 0xFFFF);
	FrontendDraw_RectAssign(&rect, 84, 110, 434, 115);
	FrontendText_DrawCentered(12, g_nextMissionDescription, &rect, g_colorLightBlue);
	FrontendDraw_RectOffsetXY(&rect, 0, 20);

	for (missionIndex = 0;
		 missionIndex <= g_pilotData.battleSequenceState.currentMissionIndex && missionIndex < 10;
		 ++missionIndex) {
		switch (g_pilotData.battleSequenceState.missionResults[missionIndex]) {
			case BATTLE_MISSION_RESULT_IMPERIAL_VICTORY:
				++imperialVictories;
				break;
			case BATTLE_MISSION_RESULT_REBEL_VICTORY:
				++rebelVictories;
				break;
			default:
				break;
		}
	}

	victoriesNeeded = g_pilotData.battleSequenceState.victoriesNeeded;
	if (imperialVictories == victoriesNeeded) {
		sprintf(g_frontendScratchBuffer, "%c%s: %c%s", 4, FrontendString_Get(FRONTSTR_709_BATTLE_COMPLETED),
				5, FrontendString_Get(FRONTSTR_345_IMPERIAL_VICTORY));
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, 0xFFFF);
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[3] != 0) {
			FrontendDraw_RectOffsetXY(&rect, 0, 15);
			sprintf(g_frontendScratchBuffer, "%s %s - %s", FrontendString_Get(FRONTSTR_377_AWARD),
					FrontendString_Get(FRONTSTR_381_BATTLE_MEDALLION),
					FrontendString_Get(
						(FrontendStringId)(g_pilotData.factionStatistics[g_pilotData.currentFactionId]
											   .missionAwards[3] +
										   381)));
			awardTextWidth = FrontendText_MeasureWidth(g_frontendScratchBuffer, 12) + 10;
			sprintf(resourceName, "medlvl%d",
					g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[3]);
			FrontImage_GetResourceRect(resourceName, &resourceRect);
			awardX = rect.left + ((rect.right - rect.left) >> 1) -
					 ((unsigned int)(resourceRect.right + awardTextWidth - resourceRect.left + 11) >> 1);
			FrontendText_Draw(12, g_frontendScratchBuffer, awardX, rect.top, 0xFFFF);
			FrontImage_DrawSprite(resourceName, awardX + awardTextWidth + 10, rect.top);
		}
	} else if (rebelVictories == victoriesNeeded) {
		sprintf(g_frontendScratchBuffer, "%c%s: %c%s", 4, FrontendString_Get(FRONTSTR_709_BATTLE_COMPLETED),
				3, FrontendString_Get(FRONTSTR_346_REBEL_VICTORY));
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, 0xFFFF);
		if (g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[3] != 0) {
			FrontendDraw_RectOffsetXY(&rect, 0, 15);
			sprintf(g_frontendScratchBuffer, "%s %s - %s", FrontendString_Get(FRONTSTR_377_AWARD),
					FrontendString_Get(FRONTSTR_381_BATTLE_MEDALLION),
					FrontendString_Get(
						(FrontendStringId)(g_pilotData.factionStatistics[g_pilotData.currentFactionId]
											   .missionAwards[3] +
										   381)));
			awardTextWidth = FrontendText_MeasureWidth(g_frontendScratchBuffer, 12) + 10;
			sprintf(resourceName, "medlvl%d",
					g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[3]);
			FrontImage_GetResourceRect(resourceName, &resourceRect);
			awardX = rect.left + ((rect.right - rect.left) >> 1) -
					 ((unsigned int)(resourceRect.right + awardTextWidth - resourceRect.left + 11) >> 1);
			FrontendText_Draw(12, g_frontendScratchBuffer, awardX, rect.top, 0xFFFF);
			FrontImage_DrawSprite(resourceName, awardX + awardTextWidth + 10, rect.top);
		}
	} else {
		int displayVictoriesNeeded = g_pilotData.battleSequenceState.victoriesNeeded;
		sprintf(g_frontendScratchBuffer, "%c%s %c%d", 4,
				FrontendString_Get(FRONTSTR_341_VICTORIES_NEEDED_TO_WIN), 1, displayVictoriesNeeded);
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, 0xFFFF);
	}

	FrontendDraw_RectOffsetXY(&rect, 0, 20);
	sprintf(g_frontendScratchBuffer, "%c%s %c%d   %c%s %c%d", 5,
			FrontendString_Get(FRONTSTR_342_IMPERIAL_VICTORIES), 1, imperialVictories, 3,
			FrontendString_Get(FRONTSTR_343_REBEL_VICTORIES), 1, rebelVictories);
	FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, 20);

	resultColor = (unsigned char)resourceName[0];
	for (missionIndex = 0;
		 missionIndex <= g_pilotData.battleSequenceState.currentMissionIndex && missionIndex < 10;
		 ++missionIndex) {
		switch (g_pilotData.battleSequenceState.missionResults[missionIndex]) {
			case BATTLE_MISSION_RESULT_IMPERIAL_VICTORY:
				resultColor = 5;
				break;
			case BATTLE_MISSION_RESULT_REBEL_VICTORY:
				resultColor = 3;
				break;
			case BATTLE_MISSION_RESULT_DRAW:
				resultColor = 1;
				break;
		}
		sprintf(g_frontendScratchBuffer, "%c%s", 4,
				g_missionList[g_pilotData.battleSequenceState.missionListIndices[missionIndex]].description);
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		sprintf(g_frontendScratchBuffer, "%c%s", resultColor,
				FrontendString_Get(
					(FrontendStringId)(g_pilotData.battleSequenceState.missionResults[missionIndex] + 345)));
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
	}
	return 1;
}

// FUNCTION: XVT 0x503400
int MissionDebrief_DrawMeleeResultsPage(int frameCounter) {
	enum {
		PLAYER_COUNT = 8,
		MAX_WINNER_ROWS = 9,
		PULSE_PERIOD = 24,
		TITLE_FONT_SIZE = 15,
		TEXT_FONT_SIZE = 12,
		TITLE_LEFT = 84,
		TITLE_TOP = 90,
		TITLE_RIGHT = 434,
		TITLE_BOTTOM = 106,
		DESCRIPTION_TOP = 110,
		DESCRIPTION_BOTTOM = 115,
		CONTENT_LEFT = 88,
		PLAYER_INDENT_LEFT = 103,
		SCORE_COLUMN_X = 238,
		FIRST_PLACE_COLUMN_X = 308,
		SECOND_PLACE_COLUMN_X = 348,
		THIRD_PLACE_COLUMN_X = 388,
		NAME_COLUMN_WIDTH = 148,
		SCORE_COLUMN_OFFSET = 150,
		PLACEMENT_COLUMN_OFFSET = 220,
		PLACEMENT_COLUMN_SPACING = 40,
		LINE_HEIGHT = 15,
		WINNER_LINE_HEIGHT = 17,
		AWARD_GAP = 10,
		AWARD_LAYOUT_PADDING = 11,
		TEXT_CODE_VALUE = 1,
		TEXT_CODE_WINNER = 2,
		TEXT_CODE_LABEL = 4,
		TEXT_CODE_RATING = 6,
	};

	RECT rect;
	RECT resourceRect;
	RECT playerNameClipRect;
	RECT savedClipRect;
	int textY;
	int standingIndex;
	int rankStart;
	int placementCode;
	int previousScore;
	int standingsPosition;
	char resourceName[52];

	FrontendDraw_RectAssign(&rect, TITLE_LEFT, TITLE_TOP, TITLE_RIGHT, TITLE_BOTTOM);
	FrontendText_DrawCentered(TITLE_FONT_SIZE, FrontendString_Get(FRONTSTR_315_TOURNAMENT_SUMMARY), &rect,
							  0xFFFF);
	FrontendDraw_RectAssign(&rect, TITLE_LEFT, DESCRIPTION_TOP, TITLE_RIGHT, DESCRIPTION_BOTTOM);
	FrontendText_DrawCentered(TEXT_FONT_SIZE, g_nextMissionDescription, &rect, g_colorLightBlue);
	FrontendDraw_RectOffsetXY(&rect, 0, 20);

	if (g_pilotData.meleeTournamentSequenceState.missionCount -
			g_pilotData.meleeTournamentSequenceState.currentMissionIndex ==
		1) {
		if (g_debriefRankByPilot != 0) {
			for (standingIndex = 0; standingIndex < MAX_WINNER_ROWS; ++standingIndex) {
				int teamId = g_debriefStandingsTeamIds[standingIndex];

				if (teamId == -1) {
					break;
				}
				if (g_debriefTeamUseSummaryRows[teamId] == 0) {
					int playerIndex;

					for (playerIndex = 0; playerIndex < PLAYER_COUNT; ++playerIndex) {
						PilotNetworkPlayer* player = &g_pilotData.networkPlayers[playerIndex];

						if (player->directPlayId != 0 &&
							g_frontendMission.flightGroups[player->flightGroupId].team == teamId) {
							break;
						}
					}
					if (playerIndex != PLAYER_COUNT) {
						int ratingPlayerIndex = g_debriefStandingsTeamIds[0];

						sprintf(g_frontendScratchBuffer, "%c%s: %c%s %c%s", TEXT_CODE_WINNER,
								FrontendString_Get(FRONTSTR_686_TOURNAMENT_WINNER), TEXT_CODE_RATING,
								FrontendString_Get(
									(FrontendStringId)(FRONTSTR_154_DRONE +
													   g_pilotData.networkPlayers[ratingPlayerIndex].rating)),
								TEXT_CODE_VALUE, g_pilotData.networkPlayers[playerIndex].friendlyName);
					}
					if (playerIndex == PLAYER_COUNT) {
						sprintf(g_frontendScratchBuffer, "%c%s: %c%s", TEXT_CODE_WINNER,
								FrontendString_Get(FRONTSTR_686_TOURNAMENT_WINNER), TEXT_CODE_VALUE,
								g_frontendMission.teams[teamId].name);
						FrontendText_DrawCentered(TITLE_FONT_SIZE, g_frontendScratchBuffer, &rect, 0xFFFF);
						FrontendDraw_RectOffsetXY(&rect, 0, WINNER_LINE_HEIGHT);
					} else {
						int color;
						color = teamId == g_pilotData.team
									? g_pulseColorRamp[((frameCounter % PULSE_PERIOD) & ~1) >> 1]
									: g_colorLightBlue;
						FrontendText_DrawCentered(TITLE_FONT_SIZE, g_frontendScratchBuffer, &rect, color);
						FrontendDraw_RectOffsetXY(&rect, 0, WINNER_LINE_HEIGHT);
						if (teamId == g_pilotData.team) {
							int awardLevel =
								g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[1];

							if (awardLevel != 0) {
								int awardTextWidth;
								int awardX;

								sprintf(g_frontendScratchBuffer, "%s %s - %s",
										FrontendString_Get(FRONTSTR_377_AWARD),
										FrontendString_Get(FRONTSTR_379_TOURNAMENT_TROPHY),
										FrontendString_Get(
											(FrontendStringId)(FRONTSTR_381_BATTLE_MEDALLION + awardLevel)));
								awardTextWidth =
									FrontendText_MeasureWidth(g_frontendScratchBuffer, TEXT_FONT_SIZE) +
									AWARD_GAP;
								sprintf(resourceName, "medlvl%d",
										g_pilotData.factionStatistics[g_pilotData.currentFactionId]
											.missionAwards[1]);
								FrontImage_GetResourceRect(resourceName, &resourceRect);
								awardX = rect.left + ((rect.right - rect.left) >> 1) -
										 ((awardTextWidth + resourceRect.right - resourceRect.left +
										   AWARD_LAYOUT_PADDING) >>
										  1);
								FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, awardX, rect.top,
												  0xFFFF);
								FrontImage_DrawSprite(resourceName, awardX + awardTextWidth + AWARD_GAP,
													  rect.top);
								FrontendDraw_RectOffsetXY(&rect, 0, LINE_HEIGHT);
							}
						}
					}
				} else {
					int flightGroupIndex;

					for (flightGroupIndex = 0; flightGroupIndex < (int16_t)g_frontendMission.flightGroupCount;
						 ++flightGroupIndex) {
						if (g_frontendMission.flightGroups[flightGroupIndex].playerNumber != 0 &&
							g_frontendMission.flightGroups[flightGroupIndex].team == teamId) {
							sprintf(g_frontendScratchBuffer, "%c%s: %c%s %c%s", TEXT_CODE_WINNER,
									FrontendString_Get(FRONTSTR_686_TOURNAMENT_WINNER), TEXT_CODE_RATING,
									FrontendString_Get(
										(FrontendStringId)(FRONTSTR_154_DRONE +
														   g_pilotData.flightGroupRating[flightGroupIndex])),
									TEXT_CODE_VALUE, g_frontendMission.flightGroups[flightGroupIndex].name);
							FrontendText_DrawCentered(TITLE_FONT_SIZE, g_frontendScratchBuffer, &rect,
													  0xFFFF);
							FrontendDraw_RectOffsetXY(&rect, 0, WINNER_LINE_HEIGHT);
						}
					}
				}

				if (g_debriefStandingsTeamIds[standingIndex + 1] == -1 ||
					g_pilotData.meleeTournamentSequenceState
							.teamStandings[g_debriefStandingsTeamIds[standingIndex + 1]]
							.totalScore <
						g_pilotData.meleeTournamentSequenceState.teamStandings[teamId].totalScore) {
					break;
				}
			}
		} else {
			for (standingIndex = 0; standingIndex < MAX_WINNER_ROWS; ++standingIndex) {
				int teamId = g_debriefStandingsTeamIds[standingIndex];

				if (teamId == -1) {
					break;
				}
				sprintf(g_frontendScratchBuffer, "%c%s: %c%s", TEXT_CODE_WINNER,
						FrontendString_Get(FRONTSTR_686_TOURNAMENT_WINNER), TEXT_CODE_VALUE,
						g_frontendMission.teams[teamId].name);
				FrontendText_DrawCentered(TITLE_FONT_SIZE, g_frontendScratchBuffer, &rect, 0xFFFF);
				FrontendDraw_RectOffsetXY(&rect, 0, WINNER_LINE_HEIGHT);
				if (teamId == g_pilotData.team) {
					int awardLevel =
						g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[1];

					if (awardLevel != 0) {
						int awardTextWidth;
						int awardX;

						sprintf(g_frontendScratchBuffer, "%s %s - %s", FrontendString_Get(FRONTSTR_377_AWARD),
								FrontendString_Get(FRONTSTR_379_TOURNAMENT_TROPHY),
								FrontendString_Get(
									(FrontendStringId)(FRONTSTR_381_BATTLE_MEDALLION + awardLevel)));
						awardTextWidth =
							FrontendText_MeasureWidth(g_frontendScratchBuffer, TEXT_FONT_SIZE) + AWARD_GAP;
						sprintf(resourceName, "medlvl%d",
								g_pilotData.factionStatistics[g_pilotData.currentFactionId].missionAwards[1]);
						FrontImage_GetResourceRect(resourceName, &resourceRect);
						awardX = rect.left + ((rect.right - rect.left) >> 1) -
								 ((awardTextWidth + resourceRect.right - resourceRect.left +
								   AWARD_LAYOUT_PADDING) >>
								  1);
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, awardX, rect.top, 0xFFFF);
						FrontImage_DrawSprite(resourceName, awardX + awardTextWidth + AWARD_GAP, rect.top);
						FrontendDraw_RectOffsetXY(&rect, 0, LINE_HEIGHT);
					}
				}

				if (g_debriefStandingsTeamIds[standingIndex + 1] == -1 ||
					g_pilotData.meleeTournamentSequenceState
							.teamStandings[g_debriefStandingsTeamIds[standingIndex + 1]]
							.totalScore <
						g_pilotData.meleeTournamentSequenceState.teamStandings[teamId].totalScore) {
					break;
				}
			}
		}
	}

	FrontendDraw_RectOffsetXY(&rect, 0, 8);
	FrontendText_DrawCentered(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_338_TOURNAMENT_SCORE_TOTALS), &rect,
							  g_colorLightBlue);
	FrontendDraw_RectOffsetXY(&rect, 0, LINE_HEIGHT);
	sprintf(g_frontendScratchBuffer, "%c%s %c%d%c %s %c%d%c %s", TEXT_CODE_LABEL,
			FrontendString_Get(FRONTSTR_339_AFTER), TEXT_CODE_VALUE,
			g_pilotData.meleeTournamentSequenceState.currentMissionIndex + 1, TEXT_CODE_LABEL,
			FrontendString_Get(FRONTSTR_335_OF), TEXT_CODE_VALUE,
			g_pilotData.meleeTournamentSequenceState.missionCount, TEXT_CODE_LABEL,
			FrontendString_Get(FRONTSTR_340_MISSIONS));
	FrontendText_DrawCentered(TEXT_FONT_SIZE, g_frontendScratchBuffer, &rect, 0xFFFF);
	textY = rect.top + LINE_HEIGHT;
	FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_328_TEAM), CONTENT_LEFT, textY,
					  g_colorLightBlue);
	FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_699_TOTAL_SCORE), SCORE_COLUMN_X, textY,
					  g_colorLightBlue);
	FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_318_1ST), FIRST_PLACE_COLUMN_X, textY,
					  g_colorLightBlue);
	FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_319_2ND), SECOND_PLACE_COLUMN_X, textY,
					  g_colorLightBlue);
	FrontendText_Draw(TEXT_FONT_SIZE, FrontendString_Get(FRONTSTR_320_3RD), THIRD_PLACE_COLUMN_X, textY,
					  g_colorLightBlue);
	textY += LINE_HEIGHT;

	rankStart = 0;
	standingsPosition = 0;
	previousScore = g_pilotData.teams[g_debriefStandingsTeamIds[0]].missionScore;
	for (standingIndex = 0; standingIndex < PLAYER_COUNT; ++standingIndex) {
		int textX = CONTENT_LEFT;

		if (g_debriefStandingsTeamIds[standingIndex] == -1) {
			break;
		}
		if (g_debriefTeamInStandings[standingIndex] != 0) {
			if (g_pilotData.meleeTournamentSequenceState
					.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
					.totalScore != previousScore) {
				previousScore = g_pilotData.meleeTournamentSequenceState
									.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
									.totalScore;
				rankStart = standingsPosition;
			}
			placementCode = TEXT_CODE_WINNER;
			if (rankStart >= 3) {
				placementCode = TEXT_CODE_VALUE;
			}
			if (g_debriefRankByPilot == 0) {
				sprintf(g_frontendScratchBuffer, "%c%d. %c%s", placementCode, rankStart + 1, TEXT_CODE_VALUE,
						g_frontendMission.teams[g_debriefStandingsTeamIds[standingIndex]].name);
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, CONTENT_LEFT, textY, 0xFFFF);
				sprintf(g_frontendScratchBuffer, "%d",
						g_pilotData.meleeTournamentSequenceState
							.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
							.totalScore);
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, SCORE_COLUMN_X, textY, 0xFFFF);
				if (g_pilotData.meleeTournamentSequenceState
						.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
						.firstPlaceCount == 0) {
					sprintf(g_frontendScratchBuffer, "---");
				} else {
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.meleeTournamentSequenceState
								.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
								.firstPlaceCount);
				}
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, FIRST_PLACE_COLUMN_X, textY,
								  0xFFFF);
				if (g_pilotData.meleeTournamentSequenceState
						.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
						.secondPlaceCount == 0) {
					sprintf(g_frontendScratchBuffer, "---");
				} else {
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.meleeTournamentSequenceState
								.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
								.secondPlaceCount);
				}
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, SECOND_PLACE_COLUMN_X, textY,
								  0xFFFF);
				if (g_pilotData.meleeTournamentSequenceState
						.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
						.thirdPlaceCount == 0) {
					sprintf(g_frontendScratchBuffer, "---");
				} else {
					sprintf(g_frontendScratchBuffer, "%d",
							g_pilotData.meleeTournamentSequenceState
								.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
								.thirdPlaceCount);
				}
				FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, THIRD_PLACE_COLUMN_X, textY,
								  0xFFFF);
				textY += LINE_HEIGHT;
				textX = PLAYER_INDENT_LEFT;
			} else if (g_debriefTeamUseSummaryRows[g_debriefStandingsTeamIds[standingIndex]] != 0) {
				int flightGroupIndex;

				for (flightGroupIndex = 0; flightGroupIndex < (int16_t)g_frontendMission.flightGroupCount;
					 ++flightGroupIndex) {
					if (g_frontendMission.flightGroups[flightGroupIndex].playerNumber != 0 &&
						g_frontendMission.flightGroups[flightGroupIndex].team ==
							g_debriefStandingsTeamIds[standingIndex]) {
						sprintf(g_frontendScratchBuffer, "%c%d. %c%s %c%s", placementCode, rankStart + 1,
								TEXT_CODE_RATING,
								FrontendString_Get(
									(FrontendStringId)(FRONTSTR_154_DRONE +
													   g_pilotData.flightGroupRating[flightGroupIndex])),
								TEXT_CODE_VALUE, g_frontendMission.flightGroups[flightGroupIndex].name);
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, 0xFFFF);
						sprintf(g_frontendScratchBuffer, "%d",
								g_pilotData.meleeTournamentSequenceState
									.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
									.totalScore);
						textX += SCORE_COLUMN_OFFSET;
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, 0xFFFF);
						textX += PLACEMENT_COLUMN_OFFSET - SCORE_COLUMN_OFFSET;
						if (g_pilotData.meleeTournamentSequenceState
								.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
								.firstPlaceCount == 0) {
							sprintf(g_frontendScratchBuffer, "---");
						} else {
							sprintf(g_frontendScratchBuffer, "%d",
									g_pilotData.meleeTournamentSequenceState
										.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
										.firstPlaceCount);
						}
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, 0xFFFF);
						textX += PLACEMENT_COLUMN_SPACING;
						if (g_pilotData.meleeTournamentSequenceState
								.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
								.secondPlaceCount == 0) {
							sprintf(g_frontendScratchBuffer, "---");
						} else {
							sprintf(g_frontendScratchBuffer, "%d",
									g_pilotData.meleeTournamentSequenceState
										.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
										.secondPlaceCount);
						}
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, 0xFFFF);
						textX += PLACEMENT_COLUMN_SPACING;
						if (g_pilotData.meleeTournamentSequenceState
								.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
								.thirdPlaceCount == 0) {
							sprintf(g_frontendScratchBuffer, "---");
						} else {
							sprintf(g_frontendScratchBuffer, "%d",
									g_pilotData.meleeTournamentSequenceState
										.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
										.thirdPlaceCount);
						}
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, 0xFFFF);
						textY += LINE_HEIGHT;
					}
				}
			}

			if (g_debriefTeamUseSummaryRows[g_debriefStandingsTeamIds[standingIndex]] == 0) {
				int sortedPlayerIndex;

				for (sortedPlayerIndex = 0; sortedPlayerIndex < PLAYER_COUNT; ++sortedPlayerIndex) {
					int playerIndex = g_debriefSortedPlayerIds[sortedPlayerIndex];

					if (playerIndex == -1) {
						break;
					}
					if (g_frontendMission.flightGroups[g_pilotData.networkPlayers[playerIndex].flightGroupId]
							.team == g_debriefStandingsTeamIds[standingIndex]) {
						int color;

						if (g_debriefRankByPilot == 0) {
							sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_RATING,
									FrontendString_Get(
										(FrontendStringId)(FRONTSTR_154_DRONE +
														   g_pilotData.networkPlayers[playerIndex].rating)),
									TEXT_CODE_VALUE, g_pilotData.networkPlayers[playerIndex].friendlyName);
							color =
								Net_GetLocalPlayerId() ==
											g_pilotData.networkPlayers[playerIndex].directPlayId ||
										g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER
									? g_pulseColorRamp[((frameCounter % PULSE_PERIOD) & ~1) >> 1]
									: g_colorLightBlue;
						} else {
							sprintf(g_frontendScratchBuffer, "%c%d. %c%s %c%s", placementCode, rankStart + 1,
									TEXT_CODE_RATING,
									FrontendString_Get(
										(FrontendStringId)(FRONTSTR_154_DRONE +
														   g_pilotData.networkPlayers[playerIndex].rating)),
									TEXT_CODE_VALUE, g_pilotData.networkPlayers[playerIndex].friendlyName);
							FrontendDraw_RectAssign(&playerNameClipRect, textX, textY,
													textX + NAME_COLUMN_WIDTH, textY + LINE_HEIGHT);
							FrontendDisplay_GetScreenClipRect(&savedClipRect);
							FrontendDisplay_SetScreenClipRect640x480(&playerNameClipRect);
							color =
								Net_GetLocalPlayerId() ==
											g_pilotData.networkPlayers[playerIndex].directPlayId ||
										g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER
									? g_pulseColorRamp[((frameCounter % PULSE_PERIOD) & ~1) >> 1]
									: g_colorLightBlue;
							FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, color);
							FrontendDisplay_SetScreenClipRect640x480(&savedClipRect);
							sprintf(g_frontendScratchBuffer, "%d",
									g_pilotData.meleeTournamentSequenceState
										.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
										.totalScore);
							textX += SCORE_COLUMN_OFFSET;
							FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, 0xFFFF);
							textX += PLACEMENT_COLUMN_OFFSET - SCORE_COLUMN_OFFSET;
							if (g_pilotData.meleeTournamentSequenceState
									.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
									.firstPlaceCount == 0) {
								sprintf(g_frontendScratchBuffer, "---");
							} else {
								sprintf(g_frontendScratchBuffer, "%d",
										g_pilotData.meleeTournamentSequenceState
											.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
											.firstPlaceCount);
							}
							FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, 0xFFFF);
							textX += PLACEMENT_COLUMN_SPACING;
							if (g_pilotData.meleeTournamentSequenceState
									.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
									.secondPlaceCount == 0) {
								sprintf(g_frontendScratchBuffer, "---");
							} else {
								sprintf(g_frontendScratchBuffer, "%d",
										g_pilotData.meleeTournamentSequenceState
											.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
											.secondPlaceCount);
							}
							FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, 0xFFFF);
							textX += PLACEMENT_COLUMN_SPACING;
							if (g_pilotData.meleeTournamentSequenceState
									.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
									.thirdPlaceCount == 0) {
								sprintf(g_frontendScratchBuffer, "---");
							} else {
								sprintf(g_frontendScratchBuffer, "%d",
										g_pilotData.meleeTournamentSequenceState
											.teamStandings[g_debriefStandingsTeamIds[standingIndex]]
											.thirdPlaceCount);
							}
							color = 0xFFFF;
						}
						FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, color);
						textY += LINE_HEIGHT;
						textX = g_debriefRankByPilot == 0 ? PLAYER_INDENT_LEFT : CONTENT_LEFT;
					}
				}
			}

			if (g_debriefTeamHasPlayer[standingIndex] != 0 && g_debriefRankByPilot == 0) {
				int flightGroupIndex;

				for (flightGroupIndex = 0; flightGroupIndex < (int16_t)g_frontendMission.flightGroupCount;
					 ++flightGroupIndex) {
					if (g_frontendMission.flightGroups[flightGroupIndex].playerNumber != 0 &&
						g_frontendMission.flightGroups[flightGroupIndex].team ==
							g_debriefStandingsTeamIds[standingIndex]) {
						int playerIndex;

						for (playerIndex = 0; playerIndex < PLAYER_COUNT; ++playerIndex) {
							if (g_pilotData.networkPlayers[playerIndex].flightGroupId == flightGroupIndex &&
								g_pilotData.networkPlayers[playerIndex].directPlayId != 0) {
								break;
							}
						}
						if (playerIndex == PLAYER_COUNT) {
							sprintf(g_frontendScratchBuffer, "%c%s %c%s", TEXT_CODE_RATING,
									FrontendString_Get(
										(FrontendStringId)(FRONTSTR_154_DRONE +
														   g_pilotData.flightGroupRating[flightGroupIndex])),
									TEXT_CODE_VALUE, g_frontendMission.flightGroups[flightGroupIndex].name);
							FrontendText_Draw(TEXT_FONT_SIZE, g_frontendScratchBuffer, textX, textY, 0xFFFF);
							textY += LINE_HEIGHT;
						}
					}
				}
			}
		}
		++standingsPosition;
	}
	return 1;
}

// FUNCTION: XVT 0x504360
int MissionDebrief_DrawTabBar(void) {
	RECT rect;
	FrontendNavigationSlotState slotStates[8];

	slotStates[0] = g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
					g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
					g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS;
	slotStates[1] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	slotStates[2] = g_pilotData.missionSequenceActive == 1 &&
					(g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES ||
					 g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
					 g_pilotData.missionDirectoryId == MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	slotStates[3] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[4] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[5] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[6] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[7] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[g_briefingTab] = FRONTEND_NAVIGATION_SLOT_SELECTED;
	FrontendButton_DrawEightSlotNavigationState(slotStates);

	FrontendDraw_RectAssign(&rect, 22, 170, 42, 194);
	if (g_briefingTab == 2) {
		if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES) {
			FrontendButton_DrawSpriteAndTooltip(&rect, "deb4d",
												FrontendString_Get(FRONTSTR_822_MISSION_DEBRIEFING), 12, 0);
		} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES) {
			FrontendButton_DrawSpriteAndTooltip(&rect, "debrief4d",
												FrontendString_Get(FRONTSTR_315_TOURNAMENT_SUMMARY), 12, 0);
		} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
			FrontendButton_DrawSpriteAndTooltip(&rect, "debrief4d",
												FrontendString_Get(FRONTSTR_314_BATTLE_SUMMARY), 12, 0);
		}
	} else if (g_pilotData.missionSequenceActive == 1) {
		if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES) {
			if (FrontendButton_DrawSpriteHitTest(&rect, "deb4u", "deb4u",
												 FrontendString_Get(FRONTSTR_822_MISSION_DEBRIEFING), 12, 0,
												 14, "jewelsound")) {
				g_debriefStatsPageNeedsRebuild = 1;
				g_briefingTab = 2;
			}
		} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES) {
			if (FrontendButton_DrawSpriteHitTest(&rect, "debrief4u", "debrief4u",
												 FrontendString_Get(FRONTSTR_315_TOURNAMENT_SUMMARY), 12, 0,
												 14, "jewelsound")) {
				g_debriefStatsPageNeedsRebuild = 1;
				g_briefingTab = 2;
			}
		} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
				   FrontendButton_DrawSpriteHitTest(&rect, "debrief4u", "debrief4u",
													FrontendString_Get(FRONTSTR_314_BATTLE_SUMMARY), 12, 0,
													14, "jewelsound")) {
			g_debriefStatsPageNeedsRebuild = 1;
			g_briefingTab = 2;
		}
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -28);
	if (g_briefingTab == 1) {
		FrontendButton_DrawSpriteAndTooltip(&rect, "debrief2d",
											FrontendString_Get(FRONTSTR_312_PLAYER_STATISTICS), 12, 0);
	} else if (FrontendButton_DrawSpriteHitTest(&rect, "debrief2u", "debrief2u",
												FrontendString_Get(FRONTSTR_312_PLAYER_STATISTICS), 12, 0, 12,
												"jewelsound")) {
		g_debriefStatsPageNeedsRebuild = 1;
		g_briefingTab = 1;
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -28);
	if (slotStates[0] != FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		if (g_briefingTab == 0) {
			FrontendButton_DrawSpriteAndTooltip(&rect, "debrief1d",
												FrontendString_Get(FRONTSTR_311_MISSION_OVERVIEW), 12, 0);
		} else if (FrontendButton_DrawSpriteHitTest(&rect, "debrief1u", "debrief1u",
													FrontendString_Get(FRONTSTR_311_MISSION_OVERVIEW), 12, 0,
													11, "jewelsound")) {
			g_debriefStatsPageNeedsRebuild = 1;
			g_briefingTab = 0;
		}
	}
	return 1;
}

// FUNCTION: XVT 0x5046D0
int MissionDebrief_MarkNetworkPlayersReady(void) {
	int playerIndex;

	for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
		if (g_pilotData.networkPlayers[playerIndex].directPlayId != 0)
			Net_MarkPlayerReadyNoLock(g_pilotData.networkPlayers[playerIndex].directPlayId);
	}
	return 1;
}

// FUNCTION: XVT 0x504700
int MissionDebrief_Prepare(void) {
	int humanPlayerCount;
	int activeTeamCount;
	unsigned int teamIndex;
	int sortIndex;
	int existing;
	int candidate;

	memset(g_debriefTeamHasPlayer, 0, sizeof(g_debriefTeamHasPlayer));
	memset(g_debriefTeamInStandings, 0, sizeof(g_debriefTeamInStandings));
	memset(g_debriefTeamUseSummaryRows, 0, sizeof(g_debriefTeamUseSummaryRows));
	activeTeamCount = 0;
	humanPlayerCount = 0;
	g_debriefLocalTeamRankIndex = activeTeamCount;
	for (teamIndex = 0; teamIndex < 8; ++teamIndex) {
		if (g_pilotData.networkPlayers[teamIndex].directPlayId != 0) {
			int team =
				g_frontendMission.flightGroups[g_pilotData.networkPlayers[teamIndex].flightGroupId].team;
			++humanPlayerCount;
			if (g_debriefTeamHasPlayer[team] == 0) {
				++activeTeamCount;
			}
			g_debriefTeamHasPlayer[team] = 1;
		}
	}
	g_debriefActiveTeamCount = activeTeamCount;
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
		g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
		if (g_gameConfig.aiOpponents != 0 ||
			g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER || humanPlayerCount == 1) {
			int flightGroupsRemaining = (short)g_frontendMission.flightGroupCount;

			for (teamIndex = 0; flightGroupsRemaining != 0; ++teamIndex, --flightGroupsRemaining) {
				if (g_frontendMission.flightGroups[teamIndex].playerNumber != 0) {
					int team = g_frontendMission.flightGroups[teamIndex].team;
					if (g_debriefTeamHasPlayer[team] == 0) {
						++activeTeamCount;
						g_debriefTeamUseSummaryRows[team] = 1;
					}
					g_debriefTeamHasPlayer[team] = 1;
				}
				g_debriefActiveTeamCount = activeTeamCount;
			}
		}
		for (teamIndex = 0; teamIndex < 8; ++teamIndex) {
			g_debriefTeamInStandings[teamIndex] =
				g_pilotData.meleeTournamentSequenceState.teamStandings[teamIndex]
					.aiOpponentSourceTeamAndTypeFlag != -1;
		}
	}

	memset(g_debriefStandingsTeamIds, 0xFF, sizeof(g_debriefStandingsTeamIds));
	memset(g_debriefSortedTeamIds, 0xFF, sizeof(g_debriefSortedTeamIds));
	memset(g_debriefKillsFromRankIds, 0xFF, sizeof(g_debriefKillsFromRankIds));
	memset(g_debriefKillsOnRankIds, 0xFF, sizeof(g_debriefKillsOnRankIds));
	memset(g_debriefSortedPlayerIds, 0xFF, sizeof(g_debriefSortedPlayerIds));

	for (teamIndex = 0; teamIndex < 10; ++teamIndex) {
		int inserted = 0;
		candidate = teamIndex;
		if (g_debriefTeamHasPlayer[teamIndex] == 0) {
			continue;
		}
		for (sortIndex = 0; sortIndex < 10; ++sortIndex) {
			existing = g_debriefSortedTeamIds[sortIndex];
			if (existing == -1) {
				g_debriefSortedTeamIds[sortIndex] = candidate;
				inserted = 1;
				break;
			}
			if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
				g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
				if (!(g_pilotData.teams[candidate].missionScore <=
					  g_pilotData.teams[existing].missionScore)) {
					g_debriefSortedTeamIds[sortIndex] = candidate;
					candidate = existing;
				}
			} else {
				if ((g_pilotData.teams[existing].isMissionCompleted == 0 &&
					 g_pilotData.teams[candidate].isMissionCompleted == 1) ||
					!(g_pilotData.teams[candidate].missionScore <=
					  g_pilotData.teams[existing].missionScore)) {
					g_debriefSortedTeamIds[sortIndex] = candidate;
					candidate = existing;
				}
			}
		}
		(void)inserted;
	}
	for (sortIndex = 0; sortIndex < 10; ++sortIndex) {
		if (g_debriefTeamHasPlayer[sortIndex] != 0 && g_debriefSortedTeamIds[sortIndex] == g_pilotData.team) {
			g_debriefLocalTeamRankIndex = sortIndex;
			break;
		}
	}

	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
		g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
		for (teamIndex = 0; teamIndex < 10; ++teamIndex) {
			candidate = teamIndex;
			if (g_debriefTeamInStandings[teamIndex] == 0) {
				continue;
			}
			for (sortIndex = 0; sortIndex < 8; ++sortIndex) {
				existing = g_debriefStandingsTeamIds[sortIndex];
				if (existing == -1) {
					g_debriefStandingsTeamIds[sortIndex] = candidate;
					break;
				}
				if (!(g_pilotData.meleeTournamentSequenceState.teamStandings[candidate].totalScore <=
					  g_pilotData.meleeTournamentSequenceState.teamStandings[existing].totalScore)) {
					g_debriefStandingsTeamIds[sortIndex] = candidate;
					candidate = existing;
				}
			}
		}
	}

	for (teamIndex = 0; teamIndex < 8; ++teamIndex) {
		candidate = teamIndex;
		if (g_pilotData.networkPlayers[teamIndex].directPlayId == 0) {
			continue;
		}
		for (sortIndex = 0; sortIndex < 8; ++sortIndex) {
			existing = g_debriefSortedPlayerIds[sortIndex];
			if (existing == -1) {
				g_debriefSortedPlayerIds[sortIndex] = candidate;
				break;
			}
			{
				int candidateCompleted =
					g_pilotData
						.teams[g_frontendMission
								   .flightGroups[g_pilotData.networkPlayers[candidate].flightGroupId]
								   .team]
						.isMissionCompleted;
				if ((candidateCompleted == 1 &&
					 g_pilotData
							 .teams[g_frontendMission
										.flightGroups[g_pilotData.networkPlayers[existing].flightGroupId]
										.team]
							 .isMissionCompleted == 0) ||
					((candidateCompleted != 0 ||
					  g_pilotData
							  .teams[g_frontendMission
										 .flightGroups[g_pilotData.networkPlayers[existing].flightGroupId]
										 .team]
							  .isMissionCompleted != 1) &&
					 g_pilotData.networkPlayers[candidate].totalScore >
						 g_pilotData.networkPlayers[existing].totalScore)) {
					g_debriefSortedPlayerIds[sortIndex] = candidate;
					candidate = existing;
				}
			}
		}
	}
	for (teamIndex = 0; teamIndex < 8; ++teamIndex) {
		candidate = teamIndex;
		if (g_pilotData.networkPlayers[teamIndex].directPlayId == 0) {
			continue;
		}
		for (sortIndex = 0; sortIndex < 8; ++sortIndex) {
			existing = g_debriefKillsOnRankIds[sortIndex];
			if (existing == -1) {
				g_debriefKillsOnRankIds[sortIndex] = candidate;
				break;
			}
			if (!((unsigned int)g_pilotData.killsFullOnPlayer[candidate] <=
				  (unsigned int)g_pilotData.killsFullOnPlayer[existing])) {
				g_debriefKillsOnRankIds[sortIndex] = candidate;
				candidate = existing;
			}
		}
	}
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
		g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
		unsigned int flightGroupCount = (short)g_frontendMission.flightGroupCount;
		unsigned int rankingId;

		for (teamIndex = 0; teamIndex < flightGroupCount; ++teamIndex) {
			int playerIndex;

			rankingId = teamIndex + 8;
			if (g_frontendMission.flightGroups[teamIndex].playerNumber == 0) {
				continue;
			}
			for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
				if (g_pilotData.networkPlayers[playerIndex].directPlayId != 0 &&
					(unsigned int)g_pilotData.networkPlayers[playerIndex].flightGroupId == teamIndex) {
					break;
				}
			}
			if (playerIndex != 8) {
				continue;
			}
			for (sortIndex = 0; sortIndex < 8; ++sortIndex) {
				existing = g_debriefKillsOnRankIds[sortIndex];
				if (existing == -1) {
					g_debriefKillsOnRankIds[sortIndex] = rankingId;
					break;
				}
				{
					unsigned int candidateFull;
					unsigned int candidateShared;
					unsigned int existingFull;
					unsigned int existingShared;

					if (rankingId < 8) {
						candidateFull = g_pilotData.killsFullOnPlayer[rankingId];
						candidateShared = g_pilotData.killsSharedOnPlayer[rankingId];
					} else {
						candidateFull = g_pilotData.killsFullOnFlightGroup[rankingId - 8];
						candidateShared = g_pilotData.killsSharedOnFlightGroup[rankingId - 8];
					}
					if (existing < 8) {
						existingFull = g_pilotData.killsFullOnPlayer[existing];
						existingShared = g_pilotData.killsSharedOnPlayer[existing];
					} else {
						existingFull = g_pilotData.killsFullOnFlightGroup[existing - 8];
						existingShared = g_pilotData.killsSharedOnFlightGroup[existing - 8];
					}
					if (existingFull < candidateFull ||
						(existingFull == candidateFull && existingShared < candidateShared)) {
						g_debriefKillsOnRankIds[sortIndex] = rankingId;
						rankingId = existing;
					}
				}
			}
		}
	}
	for (teamIndex = 0; teamIndex < 8; ++teamIndex) {
		candidate = teamIndex;
		if (g_pilotData.networkPlayers[teamIndex].directPlayId == 0) {
			continue;
		}
		for (sortIndex = 0; sortIndex < 8; ++sortIndex) {
			existing = g_debriefKillsFromRankIds[sortIndex];
			if (existing == -1) {
				g_debriefKillsFromRankIds[sortIndex] = candidate;
				break;
			}
			if (!((unsigned int)g_pilotData.killsFullFromPlayer[candidate] <=
				  (unsigned int)g_pilotData.killsFullFromPlayer[existing])) {
				g_debriefKillsFromRankIds[sortIndex] = candidate;
				candidate = existing;
			}
		}
	}

	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
		g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
		unsigned int flightGroupCount = (short)g_frontendMission.flightGroupCount;
		unsigned int rankingId;

		for (teamIndex = 0; teamIndex < flightGroupCount; ++teamIndex) {
			int playerIndex;

			rankingId = teamIndex + 8;
			if (g_frontendMission.flightGroups[teamIndex].playerNumber == 0) {
				continue;
			}
			for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
				if (g_pilotData.networkPlayers[playerIndex].directPlayId != 0 &&
					(unsigned int)g_pilotData.networkPlayers[playerIndex].flightGroupId == teamIndex) {
					break;
				}
			}
			if (playerIndex != 8) {
				continue;
			}
			for (sortIndex = 0; sortIndex < 8; ++sortIndex) {
				existing = g_debriefKillsFromRankIds[sortIndex];
				if (existing == -1) {
					g_debriefKillsFromRankIds[sortIndex] = rankingId;
					break;
				}
				{
					unsigned int candidateFull;
					unsigned int candidateShared;
					unsigned int existingFull;
					unsigned int existingShared;

					if (rankingId < 8) {
						candidateFull = g_pilotData.killsFullFromPlayer[rankingId];
						candidateShared = g_pilotData.killsSharedFromPlayer[rankingId];
					} else {
						candidateFull = g_pilotData.killsFullFromFlightGroup[rankingId - 8];
						candidateShared = g_pilotData.killsSharedFromFlightGroup[rankingId - 8];
					}
					if (existing < 8) {
						existingFull = g_pilotData.killsFullFromPlayer[existing];
						existingShared = g_pilotData.killsSharedFromPlayer[existing];
					} else {
						existingFull = g_pilotData.killsFullFromFlightGroup[existing - 8];
						existingShared = g_pilotData.killsSharedFromFlightGroup[existing - 8];
					}
					if (existingFull < candidateFull ||
						(existingFull == candidateFull && existingShared < candidateShared)) {
						g_debriefKillsFromRankIds[sortIndex] = rankingId;
						rankingId = existing;
					}
				}
			}
		}
	}

	g_debriefRankByPilot = 0;
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES) {
		for (teamIndex = 0; teamIndex < (unsigned int)g_teamCount; ++teamIndex) {
			if (g_teamFgCountScratch[teamIndex] > 1) {
				break;
			}
		}
		if (teamIndex == (unsigned int)g_teamCount) {
			g_debriefRankByPilot = 1;
		}
	}
	return 1;
}

// FUNCTION: XVT 0x504E30
void MissionDebrief_BuildText(char* outResults, int useWinText) {
	unsigned int missionListIndex;
	XvtFile* stream;
	uint16_t missionVersion;

	if (outResults == NULL) {
		return;
	}

	memset(outResults, 0, 4096);
	missionListIndex = 0;
	if (g_missionCount > 0) {
		do {
			if (g_missionList[missionListIndex].missionIdx ==
				g_pilotData.missionDescriptionIds[g_pilotData.missionDirectoryId]) {
				break;
			}
			++missionListIndex;
			if (g_missionCount <= missionListIndex) {
				break;
			}
		} while (1);
	}
	if (g_missionCount <= missionListIndex) {
		return;
	}

	sprintf(g_frontendScratchBuffer, "%s\\%s", g_campaignDirNames[g_pilotData.missionDirectoryId],
			g_missionList[missionListIndex].fileName);
	stream = File_Open(g_frontendScratchBuffer, "rb");
	if (stream == NULL) {
		return;
	}
	if (g_pilotData.missionDirectoryId != MISSION_DIRECTORY_TOURNAMENTS &&
		g_pilotData.missionDirectoryId != MISSION_DIRECTORY_BATTLES &&
		g_pilotData.missionDirectoryId != MISSION_DIRECTORY_CAMPAIGNS) {
		File_ReadWord(stream, &missionVersion);
		if (missionVersion == 14) {
			if (useWinText != 0) {
				File_Seek(stream, -12288, SEEK_END);
			} else {
				File_Seek(stream, -8192, SEEK_END);
			}
			File_ReadCount(stream, outResults, 4096);
			outResults[4095] = 0;
		}
	}
	File_Close(stream);
}

// FUNCTION: XVT 0x504F50
int MissionDebrief_DrawNarrativeTextPage(void) {
	RECT rect;
	int lineCount;

	if (g_briefingText == NULL)
		return 0;

	FrontendDraw_RectAssign(&rect, 88, 90, 430, 106);
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES &&
		g_pilotData.missionSequenceActive == 1) {
		FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_823_TOURNAMENT_DEBRIEFING), &rect, 0xFFFF);
	} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
			   g_pilotData.missionSequenceActive == 1) {
		FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_824_BATTLE_DEBRIEFING), &rect, 0xFFFF);
	} else if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES &&
			   g_pilotData.missionSequenceActive == 1) {
		FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_825_CAMPAIGN_DEBRIEFING), &rect, 0xFFFF);
	} else {
		FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_822_MISSION_DEBRIEFING), &rect, 0xFFFF);
	}

	FrontendDraw_RectAssign(&rect, 88, 111, 420, 431);
	lineCount = FrontendText_DrawWrapped(12, g_briefingText, &rect, 0xFFFF, 4, 4096) + 1;
	if (lineCount > 20) {
		FrontendDraw_RectAssign(&rect, 421, 111, 430, 431);
		g_frontendFirstVisibleLine = FrontendScrollbar_Draw(&rect, g_frontendFirstVisibleLine, lineCount, 0,
															5, (unsigned int)g_colorNavy, 9);
		FrontendDraw_RectAssign(&rect, 88, 111, 420, 431);
	} else {
		FrontendDraw_RectAssign(&rect, 88, 111, 430, 431);
	}
	FrontendText_DrawWrapped(12, g_briefingText, &rect, 0xFFFF, 4, g_frontendFirstVisibleLine);
	return 1;
}
