#include "xvt_runtime/runtime/mission_dialogs.h"
#include "xvt_runtime/runtime/frontend_cleanup.h"

#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"

#include <string.h>

/* These tails execute in the suspended screen's callback slot, before its exit callback. */
int XvtMissionDialogs_Resume(int result, int action) {
	switch ((XvtMissionDialogAction)action) {
		case XVT_MISSION_NOTICE:
			break;
		case XVT_MISSION_SETUP_CANCELLED:
			g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_CLIENT;
			FrontendScreen_SetCallbacks(FrontendNet_JoinGameScreen, FrontendMissionList_FreeScreenResources);
			break;
		case XVT_MISSION_SETUP_BOOTED:
			FrontendScreen_SetCallbacks(FrontendNet_JoinGameScreen, FrontendMissionList_FreeScreenResources);
			break;
		case XVT_MISSION_TEAM_CANCELLED:
			g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_CLIENT;
			g_skipFrontendEntryMovie = 1;
			FrontendScreen_SetCallbacks(FrontendNet_JoinGameScreen, FrontendMissionList_FreeScreenResources);
			Net_ShutdownDirectPlaySession();
			break;
		case XVT_MISSION_ASSIGNMENT_CANCELLED:
			g_skipFrontendEntryMovie = 1;
			g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_CLIENT;
			FrontendScreen_SetCallbacks(FrontendNet_JoinGameScreen, FrontendMissionList_FreeScreenResources);
			break;
		case XVT_MISSION_BRIEFING_CANCELLED:
			if (Net_IsHost()) {
				g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
				FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
			} else {
				g_skipFrontendEntryMovie = 1;
				g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_CLIENT;
				FrontendScreen_SetCallbacks(FrontendNet_JoinGameScreen,
											FrontendMissionList_FreeScreenResources);
			}
			break;
		case XVT_MISSION_SETUP_HOST_LEAVE:
			if (result) {
				g_skipFrontendEntryMovie = 1;
				g_frontendNetPacketScratch.packetType = NET_PACKET_HOST_CANCELLED;
				Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, sizeof(int));
				Net_ShutdownDirectPlaySession();
				g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
				FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
			}
			break;
		case XVT_MISSION_CLIENT_LEAVE:
		case XVT_MISSION_TEAM_CLIENT_LEAVE:
			if (result) {
				if (action == XVT_MISSION_CLIENT_LEAVE)
					g_skipFrontendEntryMovie = 1;
				g_frontendNetPacketScratch.packetType = NET_PACKET_PLAYER_LEFT;
				Net_SendPacketAndFlush(Net_GetHostPlayerId(), &g_frontendNetPacketScratch, sizeof(int));
				Net_ShutdownDirectPlaySession();
				if (action == XVT_MISSION_TEAM_CLIENT_LEAVE)
					g_skipFrontendEntryMovie = 1;
				FrontendScreen_SetCallbacks(FrontendNet_JoinGameScreen,
											FrontendMissionList_FreeScreenResources);
			}
			break;
		case XVT_MISSION_TEAM_PREVIOUS:
			/* The recovered team-screen caller does not inspect the answer. */
			if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				g_frontendNetPacketScratch.packetType = NET_PACKET_RETURN_TO_SETUP;
				Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, sizeof(int));
			} else {
				FrontendScreen_SetCallbacks(MissionSetup_Update, MissionSetup_Exit);
			}
			break;
		case XVT_MISSION_HOST_RESTART:
			if (result) {
				g_frontendNetPacketScratch.packetType = NET_PACKET_RETURN_TO_SETUP;
				Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, sizeof(int));
			}
			break;
		case XVT_MISSION_SOLO_BACK_TO_SETUP:
			if (result) {
				g_skipFrontendEntryMovie = 1;
				FrontendScreen_SetCallbacks(MissionSetup_Update, MissionSetup_Exit);
			}
			break;
		case XVT_MISSION_SOLO_BACK_TO_TEAMS:
			if (result) {
				g_skipFrontendEntryMovie = 1;
				FrontendScreen_SetCallbacks(MissionSetup_TeamAssignmentUpdate,
											XvtFrontendCleanup_MissionResources);
			}
			break;
		case XVT_MISSION_DEBRIEF_CLIENT_LEAVE:
			if (result) {
				g_frontendNetPacketScratch.packetType = NET_PACKET_PLAYER_LEFT;
				Net_SendPacketAndFlush(Net_GetHostPlayerId(), &g_frontendNetPacketScratch, sizeof(int));
				Net_ShutdownDirectPlaySession();
				FrontendButton_DisableOverlayText();
				FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
			}
			break;
		case XVT_MISSION_DEBRIEF_HOST_ABORT:
			if (result) {
				g_frontendNetPacketScratch.packetType = NET_PACKET_RETURN_TO_MISSION_SELECTION;
				Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, sizeof(int));
			}
			break;
		case XVT_MISSION_DEBRIEF_SOLO_ABORT:
		case XVT_MISSION_DEBRIEF_SOLO_ABORT_CLEAR_ROSTER:
			if (result) {
				FrontendButton_DisableOverlayText();
				g_skipFrontendEntryMovie = 0;
				g_frontendQuickStartLaunchFlag = 0;
				g_frontendSinglePlayerFlightSessionActive = 0;
				g_missionSetupRosterAuthoritative = 0;
				if (action == XVT_MISSION_DEBRIEF_SOLO_ABORT_CLEAR_ROSTER)
					memset(g_mpRoster, 0, sizeof(g_mpRoster));
				FrontendScreen_SetCallbacks(MissionSetup_Update, MissionSetup_Exit);
			}
			break;
	}
	FrontendButton_DisableOverlayText();
	return 0;
}
