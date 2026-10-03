#include "xvt/frontend/concourse.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_actions.h"
#include "xvt_runtime/runtime/frontend_movies.h"
#include "xvt_runtime/runtime/network_task.h"
#endif
#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_file_list.h"
#include "xvt/frontend/frontend_joystick.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	CONCOURSE_CHAT_LOG_BUFFER_SIZE = 0x400,
	CONCOURSE_CD_VOLUME_MAX = 0xFFFF,
	CONCOURSE_CD_VOLUME_DIVISOR = 9,
	CONCOURSE_MUSIC_TRACK = 7,
	CONCOURSE_CD_MOVIE_CHECK_LIMIT = 5,
	CONCOURSE_CURSOR_START_X = 32,
	CONCOURSE_CURSOR_START_Y = 127,
	CONCOURSE_TEXT_FADE_FRAMES = 20,
	CONCOURSE_ANIMATION_CYCLE_FRAMES = 32,
	CONCOURSE_VERSION_MAJOR = 2,
	CONCOURSE_VERSION_MINOR = 0,
};

/* Heap array of the saved pilots' names, one 14-byte entry per file in
 * g_pilotFileList, each read from the first 12 bytes of its file by
 * PilotRecord_RebuildPilotList. Freed and set NULL by Concourse_Exit and before
 * each rebuild. */
// GLOBAL: XVT 0xB69CC0
char (*g_pilotListDisplayNames)[14] = NULL;
/* The tournaments directory's mission list for the pilot record pages:
 * PilotRecord_DrawMissionAchievementsPage takes it over from
 * MissionSetup_LoadMissionList. Freed and set NULL by Concourse_Exit and
 * before each reload. */
// GLOBAL: XVT 0xB69CD4
MissionListEntry *g_pilotRecordTournamentMissionList = NULL;
/* Entries in g_pilotRecordMeleeMissionList, set when it loads; freeing the
 * list leaves the count as it was. */
// GLOBAL: XVT 0xB69E20
int g_pilotRecordMeleeMissionCount = 0;
/* The saved pilot files, *.plt in the base game folder, sorted by name;
 * PilotRecord_RebuildPilotList builds it. Freed and set NULL by
 * Concourse_Exit and before each rebuild. */
// GLOBAL: XVT 0xB69E28
FrontendFileList *g_pilotFileList = NULL;
/* The combat engagements directory's single-player mission list for the
 * pilot record pages, loaded like g_pilotRecordTournamentMissionList. */
// GLOBAL: XVT 0xB69E2C
MissionListEntry *g_pilotRecordSingleplayerCombatMissionList = NULL;
/* The campaigns directory's multiplayer mission list for the pilot record
 * pages: loaded like g_pilotRecordTournamentMissionList, but with
 * g_frontendMissionSessionMode at the network host mode during the load. */
// GLOBAL: XVT 0xB6A240
MissionListEntry *g_pilotRecordMultiplayerCampaignMissionList = NULL;
/* The campaigns directory's single-player mission list for the pilot record
 * pages, loaded like g_pilotRecordTournamentMissionList, and also by
 * PilotRecord_DrawCutsceneViewerPage and PilotRecord_DrawCampaignMedalsPage. */
// GLOBAL: XVT 0xB6A258
MissionListEntry *g_pilotRecordSingleplayerCampaignMissionList = NULL;
/* Entries in g_pilotRecordSingleplayerCampaignMissionList, set when it
 * loads; freeing the list leaves the count as it was. */
// GLOBAL: XVT 0xB6A25C
int g_pilotRecordSingleplayerCampaignMissionCount = 0;
/* Entries in g_pilotRecordSingleplayerCombatMissionList, set when it loads;
 * freeing the list leaves the count as it was. */
// GLOBAL: XVT 0xB6A254
int g_pilotRecordSingleplayerCombatMissionCount = 0;
/* The combat engagements directory's multiplayer mission list for the pilot
 * record pages, loaded like g_pilotRecordMultiplayerCampaignMissionList. */
// GLOBAL: XVT 0xB6A2B8
MissionListEntry *g_pilotRecordMultiplayerCombatMissionList = NULL;
/* The training exercises directory's multiplayer mission list for the pilot
 * record pages, loaded like g_pilotRecordMultiplayerCampaignMissionList and
 * also by PilotRecord_DrawCampaignMedalsPage. */
// GLOBAL: XVT 0xB6A2CC
MissionListEntry *g_pilotRecordMultiplayerTrainingMissionList = NULL;
/* The melees directory's mission list for the pilot record pages, loaded
 * like g_pilotRecordTournamentMissionList. */
// GLOBAL: XVT 0xB6A2D0
MissionListEntry *g_pilotRecordMeleeMissionList = NULL;
/* The training exercises directory's single-player mission list for the
 * pilot record pages, loaded like g_pilotRecordTournamentMissionList and also
 * by PilotRecord_DrawCampaignMedalsPage. */
// GLOBAL: XVT 0xB6A2D4
MissionListEntry *g_pilotRecordSingleplayerTrainingMissionList = NULL;
/* Entries in g_pilotRecordSingleplayerTrainingMissionList, set when it
 * loads; freeing the list leaves the count as it was. */
// GLOBAL: XVT 0xB69D18
int g_pilotRecordSingleplayerTrainingMissionCount = 0;
/* Entries in g_pilotRecordMultiplayerCombatMissionList, set when it loads;
 * freeing the list leaves the count as it was. */
// GLOBAL: XVT 0xB6A260
int g_pilotRecordMultiplayerCombatMissionCount = 0;
/* Entries in g_pilotRecordMultiplayerCampaignMissionList, set when it loads;
 * freeing the list leaves the count as it was. */
// GLOBAL: XVT 0xB6A268
int g_pilotRecordMultiplayerCampaignMissionCount = 0;
/* Entries in g_pilotRecordTournamentMissionList, set when it loads; freeing
 * the list leaves the count as it was. */
// GLOBAL: XVT 0xB6A2BC
int g_pilotRecordTournamentMissionCount = 0;
/* Entries in g_pilotRecordMultiplayerTrainingMissionList, set when it
 * loads; freeing the list leaves the count as it was. */
// GLOBAL: XVT 0xBB2814
int g_pilotRecordMultiplayerTrainingMissionCount = 0;

/* Exit function of the concourse: frees g_pilotFileList,
 * g_pilotListDisplayNames, the eight pilot record mission lists and
 * g_battleMissionList, setting each NULL but leaving the counts, frees the
 * "background0" and "background1" images and forgets the scrollable
 * controls. Returns 0; ignores frameCounter. */
// FUNCTION: XVT 0x4BE050
int Concourse_Exit(int frameCounter)
{
	(void)frameCounter;

	if (g_pilotFileList != NULL) {
		FrontendFileList_Free(g_pilotFileList);
		g_pilotFileList = NULL;
	}
	if (g_pilotListDisplayNames != NULL) {
		free(g_pilotListDisplayNames);
		g_pilotListDisplayNames = NULL;
	}
	if (g_pilotRecordSingleplayerTrainingMissionList != NULL) {
		free(g_pilotRecordSingleplayerTrainingMissionList);
		g_pilotRecordSingleplayerTrainingMissionList = NULL;
	}
	if (g_pilotRecordMultiplayerTrainingMissionList != NULL) {
		free(g_pilotRecordMultiplayerTrainingMissionList);
		g_pilotRecordMultiplayerTrainingMissionList = NULL;
	}
	if (g_pilotRecordMeleeMissionList != NULL) {
		free(g_pilotRecordMeleeMissionList);
		g_pilotRecordMeleeMissionList = NULL;
	}
	if (g_pilotRecordTournamentMissionList != NULL) {
		free(g_pilotRecordTournamentMissionList);
		g_pilotRecordTournamentMissionList = NULL;
	}
	if (g_pilotRecordSingleplayerCombatMissionList != NULL) {
		free(g_pilotRecordSingleplayerCombatMissionList);
		g_pilotRecordSingleplayerCombatMissionList = NULL;
	}
	if (g_pilotRecordMultiplayerCombatMissionList != NULL) {
		free(g_pilotRecordMultiplayerCombatMissionList);
		g_pilotRecordMultiplayerCombatMissionList = NULL;
	}
	if (g_battleMissionList != NULL) {
		free(g_battleMissionList);
		g_battleMissionList = NULL;
	}
	if (g_pilotRecordSingleplayerCampaignMissionList != NULL) {
		free(g_pilotRecordSingleplayerCampaignMissionList);
		g_pilotRecordSingleplayerCampaignMissionList = NULL;
	}
	if (g_pilotRecordMultiplayerCampaignMissionList != NULL) {
		free(g_pilotRecordMultiplayerCampaignMissionList);
		g_pilotRecordMultiplayerCampaignMissionList = NULL;
	}
	FrontImage_FreeResourceByName("background0");
	FrontImage_FreeResourceByName("background1");
	Frontend_ResetScrollableControls();
	return 0;
}

/* Update function of the concourse: the main screen with the pilot list and the
 * pilot record pages. The modern build first waits out a movie viewer and
 * finishes a pending common action. On frame 0 (in the modern build, only
 * without a pending pilot action), first, unless the modern build has a
 * concourse action pending: the original build ends the game, returning 1,
 * after a message box when no joystick is found, and asks for the game CD until
 * it is found, returning 1 on Cancel; the modern build stops with a fatal error
 * when the flight data is missing. It loads the image and sound lists and
 * checks for the host CD. The original build, unless movie checks are off or
 * the game was started to host or join, checks the movie CD when
 * factionStatistics[0].cdMovieCheckCounter is 0, asking for it until it is
 * there and returning 1 on Cancel, then sets the counter to 1; otherwise it
 * counts the counter up, back to 0 at 5. Then it sets g_skipMovieChecks to 1.
 * After that it shows any pending CD music warning, puts the cursor at (32,
 * 127), empties the chat log, sets g_pilotRecordPage to 0 and
 * g_frontendMissionSessionMode to none, marks the pilot record pages for
 * rebuilding, and clears other session flags; with the intro skipped it also
 * copies the current faction's mission choice into the pilot. Started with
 * "ishost", it returns 1 after an error box without the host CD, else opens an
 * internet game as host and goes to mission setup, or stays here when that
 * fails (the modern build hands this to XvtNetworkTask_Begin); started with
 * "isclient", it goes to the join screen. Otherwise it loads the two
 * backgrounds and starts a 20-frame text fade. Every frame it runs the pilot
 * selection panel and draws "v. 2.0", the pilot's rating and name between two
 * animated rebel or imperial emblems, the pilot record page g_pilotRecordPage
 * picks and the navigation controls. Returns 1 when
 * Frontend_HandleCommonScreenControls(0) returns 1, else 0. */
// FUNCTION: XVT 0x4BE1B0
int Concourse_Update(int frameCounter)
{
	RECT rect;
#ifndef XVT_MODERN
	char localPlayerInfo[2];
	int localPlayerId;
#endif

#ifdef XVT_MODERN
	if (XvtFrontendMovies_ResumeViewer()) {
		return 0;
	}
	if (XvtFrontendAction_Pending(XVT_ACTION_OWNER_COMMON)) {
		return Frontend_HandleCommonScreenControls(0) == 1;
	}
	if (frameCounter == 0 &&
	    !XvtFrontendAction_Pending(XVT_ACTION_OWNER_PILOT)) {
#else
	if (frameCounter == 0) {
#endif
#ifdef XVT_MODERN
		if (!XvtFrontendAction_Pending(XVT_ACTION_OWNER_CONCOURSE)) {
#endif
			Keyboard_FlushCharBuffer();
#ifndef XVT_MODERN
			if (Joystick_GetCount() == 0) {
				if (ErrorText_LoadLine(
					    2, g_frontendScratchBuffer) == 0) {
					FrontendDisplay_ShowGameMessageBox(
						"ERROR:  Joystick not detected!\n\nThe game will not "
						"work properly\nwithout a joystick "
						"attached.\n\nPress ENTER to exit.");
				} else {
					FrontendDisplay_ShowGameMessageBox(
						g_frontendScratchBuffer);
				}
				return 1;
			}

			if (File_CheckGameCdPresent(g_skipMovieChecks) == 0) {
				int keepRetrying;

				do {
					CDAudio_Initialize();
					FrontendDisplay_ClearBackBuffer();
					if (FrontImage_ResourceExists(
						    "dialogok") != 0) {
						keepRetrying = FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(
								FRONTSTR_706_FAILED_TO_DETECT_RETAIL_XVT_CD),
							FrontendString_Get(
								FRONTSTR_707_PLEASE_INSERT_THE_X_WING_VS_TIE_FIGHTER_CD),
							FrontendString_Get(
								FRONTSTR_708_INTO_YOUR_CD_ROM_DRIVE),
							FrontendString_Get(
								FRONTSTR_523_OKAY),
							FrontendString_Get(
								FRONTSTR_019_CANCEL));
					} else {
						keepRetrying = FrontendDialog_ShowConfirmDialog(
							FrontendString_Get(
								FRONTSTR_706_FAILED_TO_DETECT_RETAIL_XVT_CD),
							FrontendString_Get(
								FRONTSTR_707_PLEASE_INSERT_THE_X_WING_VS_TIE_FIGHTER_CD),
							FrontendString_Get(
								FRONTSTR_761_INTO_YOUR_CD_ROM_DRIVE_AND_PRESS_ENTER),
							FrontendString_Get(
								FRONTSTR_523_OKAY),
							FrontendString_Get(
								FRONTSTR_019_CANCEL));
					}
					if (keepRetrying == 0) {
						return 1;
					}
					File_DetectGameAndCdPaths(
						"\\wave\\PBC\\Pb1los07.wav");
					if (g_cdAudioWarningPending != 0) {
						g_cdAudioWarningPending =
							CDAudio_Initialize() ==
							0;
					} else {
						CDAudio_Initialize();
					}
					CDAudio_EnableLoopCurrentTrack();
					if (g_gameConfig.datapadMusicEnabled !=
					    0) {
						CDAudio_SetAuxVolume(
							CONCOURSE_CD_VOLUME_MAX *
							g_gameConfig
								.musicVolume /
							CONCOURSE_CD_VOLUME_DIVISOR);
						CDAudio_PlayTrackFromTime(
							CONCOURSE_MUSIC_TRACK,
							0, 0);
						CDAudio_SuspendPlayback();
						CDAudio_RequestResumePlayback();
					} else {
						CDAudio_StopCurrentTrack();
					}
				} while (File_CheckGameCdPresent(
						 g_skipMovieChecks) == 0);
			}

#else
		if (!File_CheckGameCdPresent(g_skipMovieChecks)) {
			XvtStorage_Fatal(
				"Required flight/voice assets are missing; select a complete installation with --setup",
				1);
			return 1;
		}
#endif
			FrontImage_LoadResourceList("frontres\\top.lst");
			FrontImage_LoadResourceList("frontres\\side.lst");
			FrontImage_LoadResourceList("frontres\\awards.lst");
			FrontImage_LoadResourceList("frontres\\promo.lst");
			FrontImage_LoadResourceList("frontres\\icons.lst");
			FrontendSound_LoadList("sfx\\sfx.lst");
			FrontendDisplay_ClearBackBuffer();
			Frontend_CheckHostCdPresent();
#ifndef XVT_MODERN
			if (g_skipMovieChecks == 0 && g_optIsHost == 0 &&
			    g_optIsClient == 0) {
				if (g_pilotData.factionStatistics[0]
					    .cdMovieCheckCounter == 0) {
					if (File_CheckRequiredCdMovieAssetsPresent() ==
					    0) {
						do {
							CDAudio_StopCurrentTrack();
							FrontendDisplay_ClearBackBuffer();
							FrontImage_ResourceExists(
								"dialogok");
							if (FrontendDialog_ShowConfirmDialog(
								    FrontendString_Get(
									    FRONTSTR_827_PLEASE_INSERT_BALANCE_OF_POWER_CD),
								    FrontendString_Get(
									    FRONTSTR_828_INTO_YOUR_CD_ROM_DRIVE),
								    FrontendString_Get(
									    FRONTSTR_829_EMPTY_TRANSLATION_PLACEHOLDER),
								    FrontendString_Get(
									    FRONTSTR_523_OKAY),
								    FrontendString_Get(
									    FRONTSTR_019_CANCEL)) ==
							    0) {
								return 1;
							}
							CDAudio_Initialize();
							CDAudio_EnableLoopCurrentTrack();
							if (g_gameConfig
								    .datapadMusicEnabled !=
							    0) {
								CDAudio_SetAuxVolume(
									CONCOURSE_CD_VOLUME_MAX *
									g_gameConfig
										.musicVolume /
									CONCOURSE_CD_VOLUME_DIVISOR);
								CDAudio_PlayTrackFromTime(
									CONCOURSE_MUSIC_TRACK,
									0, 0);
								CDAudio_SuspendPlayback();
								CDAudio_RequestResumePlayback();
							} else {
								CDAudio_StopCurrentTrack();
							}
						} while (
							File_CheckRequiredCdMovieAssetsPresent() ==
							0);
					}
					FrontendDisplay_ClearBackBuffer();
					g_pilotData.factionStatistics[0]
						.cdMovieCheckCounter = 1;
				} else {
					++g_pilotData.factionStatistics[0]
						  .cdMovieCheckCounter;
					if (g_pilotData.factionStatistics[0]
						    .cdMovieCheckCounter >=
					    CONCOURSE_CD_MOVIE_CHECK_LIMIT) {
						g_pilotData.factionStatistics[0]
							.cdMovieCheckCounter =
							0;
					}
				}
			}
#endif
			g_skipMovieChecks = 1;
#ifdef XVT_MODERN
		}
#endif
		if (g_cdAudioWarningPending != 0) {
#ifdef XVT_MODERN
			FrontendDialog_ShowConfirmDialog(
				"Music files could not be opened.",
				"Check BalanceOfPower/MUSIC/TrackNN.ogg in the selected installation.",
				NULL, FrontendString_Get(FRONTSTR_523_OKAY),
				NULL);
			if (XvtDialog_IsActive()) {
				XvtFrontendAction_Trigger(
					XVT_ACTION_OWNER_CONCOURSE, 1, 1);
				return 0;
			}
			XvtFrontendAction_Finish(XVT_ACTION_OWNER_CONCOURSE);
#else
			FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(
					FRONTSTR_765_CD_MUSIC_NOT_AVAILABLE),
				FrontendString_Get(
					FRONTSTR_766_MAKE_SURE_OTHER_CD_AUDIO_PLAYING_APPLICATIONS),
				FrontendString_Get(
					FRONTSTR_767_LIKE_FLEXICD_ARE_NOT_ALREADY_RUNNING),
				FrontendString_Get(FRONTSTR_523_OKAY), NULL);
#endif
			g_cdAudioWarningPending = 0;
		}
		FrontendCursor_SetPos(CONCOURSE_CURSOR_START_X,
				      CONCOURSE_CURSOR_START_Y);
		if (g_frontendChatLogBuffer != NULL) {
			memset(g_frontendChatLogBuffer, 0,
			       CONCOURSE_CHAT_LOG_BUFFER_SIZE);
			g_frontendChatLogUsedBytes = 0;
		}
		g_configConnectionTypeEditable = 1;
		g_frontendSkipScreenEntrySetup = 0;
		g_frontendQuickStartLaunchFlag = 0;
		g_frontendGameSessionInProgress = 0;
		if (g_optSkipIntro != 0) {
			g_pilotData.team =
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.team;
			g_pilotData.missionDirectoryId =
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.missionDirectoryId;
			memcpy(g_pilotData.missionDescriptionIds,
			       g_pilotData
				       .factionStatistics
					       [g_pilotData.currentFactionId]
				       .missionDescriptionIds,
			       sizeof(g_pilotData.missionDescriptionIds));
			g_pilotData.missionSequenceActive =
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.missionSequenceActive;
			g_pilotData.missionSequenceDescriptionId =
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.missionSequenceDescriptionId;
		}
		g_pilotRecordPagesNeedRebuild = 1;
		g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
		g_missionSetupRosterAuthoritative = 0;
		g_pilotRecordPage = 0;

		if (g_optIsHost != 0) {
			if (g_hostCdAvailable == 0) {
				if (ErrorText_LoadLine(
					    3, g_frontendScratchBuffer) == 0) {
					FrontendDisplay_ShowGameMessageBox(
						"ERROR:  Not Host CD!\n\nYou cannot host an internet game with the Client CD.\nYou "
						"must insert the Host CD into your CD-ROM drive\nto host an internet game.\n\nPress "
						"ENTER "
						"to exit.");
				} else {
					FrontendDisplay_ShowGameMessageBox(
						g_frontendScratchBuffer);
				}
				return 1;
			}
			g_optIsHost = 0;
			g_frontendMissionSessionMode =
				FRONTEND_MISSION_SESSION_NET_HOST;
			memset(g_mpRoster, 0, sizeof(g_mpRoster));
			g_gameConfig.networkType = NET_TRANSPORT_TCPIP;
			strcpy(g_pilotData.multiplayerGameName,
			       "Internet game.");
			g_missionSetupIsHost = 1;
#ifndef XVT_MODERN
			switch ((NetworkTransportType)
					g_gameConfig.networkType) {
			case NET_TRANSPORT_IPX:
				g_frontendScratchBuffer[0] = '\0';
				break;
			case NET_TRANSPORT_TCPIP:
				FrontendDisplay_FlipDirectDrawToGDISurface();
				strcpy(g_frontendScratchBuffer,
				       g_gameConfig.ipAddress);
				break;
			case NET_TRANSPORT_MODEM:
				FrontendDisplay_FlipDirectDrawToGDISurface();
				strcpy(g_frontendScratchBuffer,
				       g_gameConfig.phoneNumber);
				break;
			case NET_TRANSPORT_SERIAL:
				break;
			}
			localPlayerInfo[0] = (char)(g_pilotData.rating + 1);
			localPlayerInfo[1] = '\0';
			FrontendCursor_ShowOsCursor();
#endif
#ifdef XVT_MODERN
			XvtNetworkTask_Begin(XVT_NETWORK_AUTO_HOST);
			return 0;
#else
			if (Net_StartNetworkSession(
				    (int)g_frontendNetXvtDirectPlayAppGuid[0],
				    (int)g_frontendNetXvtDirectPlayAppGuid[1],
				    (int)g_frontendNetXvtDirectPlayAppGuid[2],
				    (int)g_frontendNetXvtDirectPlayAppGuid[3],
				    localPlayerInfo, g_pilotData.name,
				    g_missionSetupIsHost,
				    g_pilotData.multiplayerGameName,
				    (NetworkTransportType)
					    g_gameConfig.networkType,
				    0, 0, g_frontendScratchBuffer, NULL) == 0) {
				FrontendDisplay_UnlockBackBuffer();
				FrontendCursor_HideOsCursor();
				g_drawSurfacePtr =
					FrontendDisplay_LockBackBuffer();
				g_frontendMissionSessionMode =
					FRONTEND_MISSION_SESSION_NONE;
				return 0;
			}
			FrontendDisplay_UnlockBackBuffer();
			FrontendCursor_HideOsCursor();
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
			localPlayerId = Net_GetLocalPlayerId();
			Net_SetPlayerReady(localPlayerId);
			memset(g_mpRoster, 0, sizeof(g_mpRoster));
			strcpy(g_mpRoster[0].name, g_pilotData.name);
			g_mpRoster[0].playerId = Net_GetLocalPlayerId();
			g_mpRoster[0].pilotRating = g_pilotData.rating;
			FrontendScreen_SetCallbacks(MissionSetup_Update,
						    MissionSetup_Exit);
			return 0;
#endif
		}
		if (g_optIsClient != 0) {
			g_optIsClient = 0;
			g_frontendMissionSessionMode =
				FRONTEND_MISSION_SESSION_NET_CLIENT;
			g_gameConfig.networkType = NET_TRANSPORT_TCPIP;
			FrontendScreen_SetCallbacks(
				FrontendNet_JoinGameScreen,
				FrontendMissionList_FreeScreenResources);
			return 0;
		}
		FrontImage_RegisterResourceDefault("frontres\\reg0.bmp",
						   "background0");
		FrontImage_RegisterResourceDefault("frontres\\reg1.bmp",
						   "background1");
		PilotRecord_RedrawBackground();
		FrontendText_StartTextFadeIn(CONCOURSE_TEXT_FADE_FRAMES);
	}

	PilotRecord_UpdatePilotSelectionPanel(frameCounter);
#ifdef XVT_MODERN
	if (XvtDialog_IsActive()) {
		return 0;
	}
#endif
	FrontendDraw_RectAssign(&rect, 507, 452, 562, 464);
	sprintf(g_frontendScratchBuffer, "v. %d.%d", CONCOURSE_VERSION_MAJOR,
		CONCOURSE_VERSION_MINOR);
	FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, 0xFFFF);
	FrontendDraw_RectAssign(&rect, 200, 452, 436, 464);
	if (g_pilotData.name[0] != '\0') {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
			g_pilotData.ratingName, 1, g_pilotData.name);
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect,
					  g_colorYellow);
		if (g_pilotData.currentFactionId == 0) {
			sprintf(g_frontendScratchBuffer, "rebtiny%d",
				(frameCounter %
				 CONCOURSE_ANIMATION_CYCLE_FRAMES) >>
					1);
		} else {
			sprintf(g_frontendScratchBuffer, "imptiny%d",
				(frameCounter %
				 CONCOURSE_ANIMATION_CYCLE_FRAMES) >>
					1);
		}
		FrontImage_DrawSprite(g_frontendScratchBuffer, 204, 453);
		FrontImage_DrawSprite(g_frontendScratchBuffer, 420, 453);
	}
	switch (g_pilotRecordPage) {
	case 0:
		PilotRecord_DrawPilotStatisticsPage();
		break;
	case 1:
		PilotRecord_DrawPilotAwardsPage();
		break;
	case 2:
		PilotRecord_DrawPilotRatingPage();
		break;
	case 3:
		PilotRecord_DrawMissionAchievementsPage();
		break;
	case 4:
		PilotRecord_DrawCampaignMedalsPage();
		break;
	case 5:
		PilotRecord_DrawCutsceneViewerPage();
		break;
	default:
		break;
	}
	PilotRecord_UpdateNavigationControls();
	return Frontend_HandleCommonScreenControls(0) == 1;
}
