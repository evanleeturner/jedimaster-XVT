#include "xvt/frontend/frontend_flight.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/launch_task.h"
#endif

#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/flight/flight.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_debrief.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GLOBAL: XVT 0x669798
int g_flightLoadingReadyScreenStartTick = 0;
// GLOBAL: XVT 0x669794
int g_flightLoadingReadyScreenCurrentTick = 0;
// GLOBAL: XVT 0xB69CDC
int g_unusedFlightLoadingReadyScreenFlag = 0;
// GLOBAL: XVT 0xB6A2C8
int g_frontendLaunchHumanPlayerCount = 0;
// GLOBAL: XVT 0x66DA78
char g_frontendFlightCommandLine[256] = { 0 };

// FUNCTION: XVT 0x4FB350
int FlightLoading_GetReadyScreen(int frameCounter) {
	RECT rect;
	int readyPlayerCount;

	if (frameCounter == 0) {
		readyPlayerCount = 1;
		FrontendCursor_Hide();
		g_unusedFlightLoadingReadyScreenFlag = 1;
		g_missionSetupIsHost = Net_IsHost();
		if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			g_missionSetupIsHost = 1;
		} else {
			readyPlayerCount = Net_CountReadyPlayers();
		}
		g_frontendLaunchHumanPlayerCount = readyPlayerCount;
		g_pilotData.numHumanPlayersLastMission = readyPlayerCount;
		g_flightLoadingReadyScreenStartTick = GetTickCount();
		FrontImage_RegisterResourceDefault("frontres\\wait.bmp", "background");
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("background", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		FrontImage_DrawSprite("alloff", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_ResetGlyphScratch();
	}

	FrontendDraw_RectAssign(&rect, 0, 0, 639, 479);
	FrontendText_DrawCentered(15, FrontendString_Get(FRONTSTR_205_PREPARE_FOR_LAUNCH), &rect, 0xFFFF);
	if (g_missionSetupIsHost != 0) {
		g_flightLoadingReadyScreenCurrentTick = GetTickCount();
		if (g_flightLoadingReadyScreenStartTick + 2000 < g_flightLoadingReadyScreenCurrentTick) {
			MissionSetup_BroadcastStatePacket(0);
			g_unusedFlightLoadingReadyScreenFlag = 1;
			FrontImage_FreeResourceByName("background");
			FrontendScreen_SetCallbacks(FrontendFlight_LaunchSession, FrontendFlight_NoOpExit);
			return 0;
		}
	}
	g_flightLoadingReadyScreenCurrentTick = GetTickCount();
	if (g_flightLoadingReadyScreenStartTick + 4000 < g_flightLoadingReadyScreenCurrentTick) {
		g_unusedFlightLoadingReadyScreenFlag = 1;
		FrontImage_FreeResourceByName("background");
		FrontendScreen_SetCallbacks(FrontendFlight_LaunchSession, FrontendFlight_NoOpExit);
		return 0;
	}
	return 0;
}

// FUNCTION: XVT 0x506690
int FrontendFlight_NoOpExit(int frameCounter) {
	(void)frameCounter;

	return 0;
}

// FUNCTION: XVT 0x5066A0
int FrontendFlight_LaunchSession(int frameCounter) {
#ifdef XVT_MODERN
	(void)frameCounter;
	return XvtLaunchTask_Queue();
#else
	enum {
		MUSIC_VOLUME_MAX = 0xFFFF,
		MUSIC_VOLUME_LEVEL_COUNT = 9,
		MUSIC_FADE_DIVISOR = 8,
		MUSIC_FADE_DURATION_MS = 1000,
		FLIGHT_WINDOW_PROC_MODE = 1,
		FRONTEND_WINDOW_PROC_MODE = 0,
		FRONTEND_MUSIC_TRACK = 7,
		PILOT_RATING_STRING_BASE = 122,
	};

	int launchSucceeded;
	unsigned int missionIndex;
	int musicVolume;

	launchSucceeded = 0;
	if (frameCounter == 0) {
		Config_Write();
		Pilot_Save(0);
		while (1) {
			FrontendCursor_Show();
			if (File_CheckGameCdPresent(g_skipMovieChecks) != 0) {
				break;
			}
			CDAudio_Initialize();
			launchSucceeded = FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(FRONTSTR_706_FAILED_TO_DETECT_RETAIL_XVT_CD),
				FrontendString_Get(FRONTSTR_707_PLEASE_INSERT_THE_X_WING_VS_TIE_FIGHTER_CD),
				FrontendString_Get(FRONTSTR_708_INTO_YOUR_CD_ROM_DRIVE),
				FrontendString_Get(FRONTSTR_523_OKAY), FrontendString_Get(FRONTSTR_019_CANCEL));
			if (launchSucceeded == 0) {
				FrontendCursor_Show();
				return 1;
			}
		}

		FrontendCursor_Hide();
		Frontend_MarkHostCdAvailable();
		if (g_hostCdAvailable == 0 && g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_NET_CLIENT) {

			FrontendCursor_Show();
			FrontendDialog_ShowConfirmDialog(FrontendString_Get(FRONTSTR_749_YOU_SWITCHED_TO_A_CLIENT_CD),
											 FrontendString_Get(FRONTSTR_750_YOU_CANNOT_HOST_A_NETWORK_GAME),
											 FrontendString_Get(FRONTSTR_751_OR_FLY_SOLO_WITH_THE_CLIENT_CD),
											 NULL, NULL);
			if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				g_frontendNetPacketScratch.packetType = NET_PACKET_HOST_CANCELLED;
				Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
									   sizeof(g_frontendNetPacketScratch.packetType));
				Net_ShutdownDirectPlaySession();
			}
			g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
			FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
			return 0;
		}

		if (g_gameConfig.datapadMusicEnabled != 0 && CDAudio_IsPlaybackComplete() == 0) {
			musicVolume = MUSIC_VOLUME_MAX * g_gameConfig.musicVolume / MUSIC_VOLUME_LEVEL_COUNT;
			CDAudio_FadeAuxVolume((unsigned int)musicVolume, (unsigned int)musicVolume / MUSIC_FADE_DIVISOR,
								  MUSIC_FADE_DURATION_MS);
		}
		FrontendDisplay_FlipDirectDrawToGDISurface();
		FrontendDisplay_ReleaseSurfacesForFlight();
		FrontendDisplay_SetWndProcMode(FLIGHT_WINDOW_PROC_MODE);
		MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
		if (g_missionList != NULL) {
			missionIndex = 0;
			for (missionIndex = 0; missionIndex < g_missionCount; ++missionIndex) {
				if (g_missionList[missionIndex].missionIdx ==
					g_pilotData.missionDescriptionIds[g_pilotData.missionDirectoryId]) {
					break;
				}
			}
			if (g_optNoFullscreen != 0) {
				sprintf(
					g_frontendFlightCommandLine, "~%s\\%s~ ~%s~ ~%s~ %u ~%s~ 0 %u nopageflip nofullscreen",
					g_campaignDirNames[g_pilotData.missionDirectoryId], g_missionList[missionIndex].fileName,
					g_pilotData.networkPlayers[g_localPilotNetworkPlayerIndex].formalName, g_pilotData.name,
					g_missionSetupIsHost, g_pilotData.multiplayerGameName, g_frontendLaunchHumanPlayerCount);
			} else {
				sprintf(
					g_frontendFlightCommandLine, "~%s\\%s~ ~%s~ ~%s~ %u ~%s~ 0 %u pageflip fullscreen",
					g_campaignDirNames[g_pilotData.missionDirectoryId], g_missionList[missionIndex].fileName,
					g_pilotData.networkPlayers[g_localPilotNetworkPlayerIndex].formalName, g_pilotData.name,
					g_missionSetupIsHost, g_pilotData.multiplayerGameName, g_frontendLaunchHumanPlayerCount);
			}
			free(g_missionList);
			g_missionList = NULL;
			launchSucceeded = Flight_Main(g_frontendFlightCommandLine);
		}

		FrontendDisplay_ReinitSurfaces();
		FrontendDisplay_SetWndProcMode(FRONTEND_WINDOW_PROC_MODE);
		FrontendSound_LoadList("sfx\\sfx.lst");
		Config_Write();
		strcpy(g_pilotData.ratingName,
			   FrontendString_Get((FrontendStringId)(g_pilotData.rating + PILOT_RATING_STRING_BASE)));
		Pilot_Save(0);
		CDAudio_Initialize();
		CDAudio_EnableLoopCurrentTrack();
		if (g_gameConfig.datapadMusicEnabled != 0) {
			CDAudio_PlayTrackFromTime(FRONTEND_MUSIC_TRACK, 0, 0);
			musicVolume = MUSIC_VOLUME_MAX * g_gameConfig.musicVolume / MUSIC_VOLUME_LEVEL_COUNT;
			CDAudio_SetAuxVolume((unsigned int)musicVolume);
		}
	}

	if (launchSucceeded == 0) {
		FrontendCursor_Show();
		Net_ShutdownDirectPlaySession();
		FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
	} else {
		FrontendScreen_SetCallbacks(MissionDebrief_Update, MissionDebrief_Exit);
	}
	return 0;
#endif
}
