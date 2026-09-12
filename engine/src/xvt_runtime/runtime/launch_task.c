#include "xvt_runtime/runtime/launch_task.h"

#include "aeron/aeron.h"
#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_flight.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/mission_debrief.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/runtime/cd_task.h"
#include "xvt_runtime/snapshot/render_frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { XVT_LAUNCH_IDLE, XVT_LAUNCH_FADE, XVT_LAUNCH_PENDING, XVT_LAUNCH_RUNNING };

static int g_phase;
static char g_command[sizeof(g_frontendFlightCommandLine)];

int XvtLaunchTask_Queue(void) {
	unsigned int index;
	int length;
	int volume;
	if (g_phase != XVT_LAUNCH_IDLE)
		return 0;
	Config_Write();
	if (!Pilot_Save(0))
		return 1;
	if (!File_CheckGameCdPresent(g_skipMovieChecks)) {
		XvtStorage_Fatal("Required flight/voice data is missing; select a complete installation with --setup",
						 1);
		return 1;
	}
	Frontend_MarkHostCdAvailable();
	if (!g_hostCdAvailable && g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_NET_CLIENT) {
		XvtStorage_Fatal("Required training mission is missing from the installation", 1);
		return 1;
	}
	MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
	for (index = 0; g_missionList && index < g_missionCount; ++index)
		if (g_missionList[index].missionIdx ==
			g_pilotData.missionDescriptionIds[g_pilotData.missionDirectoryId])
			break;
	if (!g_missionList || index == g_missionCount) {
		free(g_missionList);
		g_missionList = NULL;
		XvtStorage_Fatal("Selected mission is absent from the installed mission list", 1);
		return 1;
	}
	length = snprintf(g_command, sizeof(g_command), "~%s\\%s~ ~%s~ ~%s~ %u ~%s~ 0 %u %s",
					  g_campaignDirNames[g_pilotData.missionDirectoryId], g_missionList[index].fileName,
					  g_pilotData.networkPlayers[g_localPilotNetworkPlayerIndex].formalName, g_pilotData.name,
					  g_missionSetupIsHost, g_pilotData.multiplayerGameName, g_frontendLaunchHumanPlayerCount,
					  g_optNoFullscreen ? "nopageflip nofullscreen" : "pageflip fullscreen");
	free(g_missionList);
	g_missionList = NULL;
	if (length < 0 || length >= (int)sizeof(g_command)) {
		XvtStorage_Fatal("Mission launch arguments exceed their supported length", 1);
		return 1;
	}
	memcpy(g_frontendFlightCommandLine, g_command, sizeof(g_command));
	FrontendCursor_Hide();
	if (g_gameConfig.datapadMusicEnabled && !CDAudio_IsPlaybackComplete()) {
		volume = 65535 * g_gameConfig.musicVolume / 9;
		XvtCdTask_BeginFade(volume, volume / 8, 1000);
	}
	g_phase = XVT_LAUNCH_FADE;
	return 0;
}

void XvtLaunchTask_Tick(void) {
	if (g_phase == XVT_LAUNCH_FADE && !XvtCdTask_IsFading()) {
		g_phase = XVT_LAUNCH_PENDING;
		Aeron_LogInfo("xvt.launch", "Flight launch queued: %s", g_command);
	}
	if ((g_phase == XVT_LAUNCH_FADE || g_phase == XVT_LAUNCH_PENDING) && Keyboard_PeekChar() == 27) {
		Keyboard_FlushCharBuffer();
		XvtLaunchTask_Complete(0);
	}
}

int XvtLaunchTask_IsActive(void) { return g_phase != XVT_LAUNCH_IDLE; }

int XvtLaunchTask_HasPendingLaunch(void) { return g_phase == XVT_LAUNCH_PENDING; }

const char* XvtLaunchTask_BeginPendingLaunch(void) {
	if (!XvtLaunchTask_HasPendingLaunch())
		return NULL;
	FrontendDisplay_UnlockBackBuffer();
	FrontendDisplay_FlipDirectDrawToGDISurface();
	FrontendDisplay_ReleaseSurfacesForFlight();
	XvtRenderFrontend_ReleaseSurfaces();
	FrontendDisplay_SetWndProcMode(1);
	g_phase = XVT_LAUNCH_RUNNING;
	return g_command;
}

void XvtLaunchTask_Complete(int succeeded) {
	int launched = g_phase == XVT_LAUNCH_RUNNING;
	if (!XvtLaunchTask_IsActive())
		return;
	g_phase = XVT_LAUNCH_IDLE;
	XvtCdTask_Cancel();
	if (launched) {
		if (!FrontendDisplay_ReinitSurfaces()) {
			XvtStorage_Fatal("Cannot restore frontend surfaces after flight", 1);
			return;
		}
		FrontendDisplay_SetWndProcMode(0);
		FrontendSound_LoadList("sfx\\sfx.lst");
		Config_Write();
		snprintf(g_pilotData.ratingName, sizeof(g_pilotData.ratingName), "%s",
				 FrontendString_Get((FrontendStringId)(g_pilotData.rating + 122)));
		Pilot_Save(0);
		CDAudio_Initialize();
		CDAudio_EnableLoopCurrentTrack();
	}
	if (g_gameConfig.datapadMusicEnabled) {
		CDAudio_PlayTrackFromTime(7, 0, 0);
		CDAudio_SetAuxVolume(65535 * g_gameConfig.musicVolume / 9);
	}
	if (succeeded) {
		FrontendScreen_SetCallbacks(MissionDebrief_Update, MissionDebrief_Exit);
	} else {
		FrontendCursor_Show();
		Net_ShutdownDirectPlaySession();
		FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
	}
	/* Completion occurs outside a screen slice; the previous exit callback is a no-op. */
	g_frontState.screenCallbacksDirty = 0;
	g_frontState.frameCounter = 0;
}

void XvtLaunchTask_Shutdown(void) {
	g_phase = XVT_LAUNCH_IDLE;
	g_command[0] = 0;
}
