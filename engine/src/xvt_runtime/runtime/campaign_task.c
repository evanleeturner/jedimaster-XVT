#include "xvt_runtime/runtime/campaign_task.h"

#include "aeron/aeron.h"
#include "xvt/assets/file.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/mission_debrief.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt_runtime/runtime/cutscene_task.h"
#include "xvt_runtime/runtime/movie_task.h"
#include "xvt_runtime/storage/storage.h"
#include "xvt_runtime/timing/host_clock.h"

#include <stdlib.h>
#include <string.h>

enum {
	XVT_CAMPAIGN_IDLE,
	XVT_CAMPAIGN_SEQUENCE,
	XVT_CAMPAIGN_MOVIE,
	XVT_CAMPAIGN_DEBRIEF,
	XVT_CAMPAIGN_DEBRIEF_ROSTER,
	XVT_CAMPAIGN_DEBRIEF_RENAME
};

static struct {
	int phase;
	int campaign;
	int pending;
	int packet_type;
	uint32_t wait_start;
	char player_info[16];
} g_campaign;

int XvtCampaignTask_WaitPacket(int packet_type, int** packet) {
	DPID sender;
	uint32_t size;
	int count;
	uint32_t now = XvtTime_GetElapsedTicks();
	*packet = NULL;
	if (!g_campaign.packet_type) {
		g_campaign.packet_type = packet_type;
		g_campaign.wait_start = now;
	}
	/* Ingress is serviced by the frontend host tick; consume a finite packet slice. */
	for (count = 0; count < 32; ++count) {
		int* candidate = Net_GetNextAppPacket(&sender, &size);
		if (candidate && size >= 2 * sizeof(int) && candidate[0] == packet_type) {
			size_t required = candidate[1] == 0
								  ? 2 * sizeof(int)
								  : 3 * sizeof(int) + (packet_type == NET_PACKET_BATTLE_CONTINUATION
														   ? sizeof(BattleSequenceState)
														   : sizeof(CampaignSequenceState));
			if (size >= required) {
				*packet = candidate;
				g_campaign.packet_type = NET_PACKET_NONE;
				return 1;
			}
		}
		/* Preserve the original expected-packet-before-timeout ordering and strict limit. */
		if ((uint32_t)(now - g_campaign.wait_start) > 30000) {
			g_campaign.packet_type = NET_PACKET_NONE;
			Aeron_LogWarn("xvt.campaign", "Timed out waiting for continuation packet %d", packet_type);
			return 0;
		}
		if (!candidate)
			break;
	}
	return XVT_CAMPAIGN_PENDING;
}

static int XvtCampaignTask_Finish(int result) {
	g_campaign.phase = XVT_CAMPAIGN_IDLE;
	g_campaign.pending = 0;
	return result;
}

int XvtCampaignTask_EnterTeams(void) {
	int result;
	if (g_campaign.phase == XVT_CAMPAIGN_IDLE) {
		g_frontendChatTeamOnly = 0;
		g_missionSetupShowDescriptionPanel = 0;
		g_frontendFirstVisibleLine = 0;
		if (!File_CheckGameCdPresent(g_skipMovieChecks))
			XvtStorage_Fatal("Cannot load required flight/voice files", 1);
		Frontend_MarkHostCdAvailable();
		if (!g_hostCdAvailable && g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_NET_CLIENT)
			XvtStorage_Fatal("Cannot load required training mission", 1);
		if (g_skipFrontendEntryMovie ||
			g_missionSetupDebriefTransition == MISSION_SETUP_DEBRIEF_TRANSITION_ENTER_CURRENT_MISSION ||
			g_pilotData.missionSequenceActive != 1)
			return 1;
		g_campaign.campaign = g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES;
		if (!g_campaign.campaign && g_pilotData.missionDirectoryId != MISSION_DIRECTORY_COMBAT_ENGAGEMENTS)
			return 1;
		g_campaign.phase = XVT_CAMPAIGN_SEQUENCE;
	}
	if (g_campaign.phase == XVT_CAMPAIGN_SEQUENCE) {
		result = g_campaign.campaign ? MissionSetup_TryContinueCampaign() : MissionSetup_TryContinueBattle();
		if (result == XVT_CAMPAIGN_PENDING) {
			g_campaign.pending = 1;
			return result;
		}
		if (result == 0) {
			g_remoteBattleSequenceActive = 0;
			g_remoteBattleSequenceContinuationChoice = 0;
			g_remoteBattleLastCompletedMissionIndex = 0;
			g_remoteBattleRebelVictoryCount = 0;
			g_remoteBattleImperialVictoryCount = 0;
		}
		if (!g_campaign.campaign)
			return XvtCampaignTask_Finish(1);
		g_campaign.phase = XVT_CAMPAIGN_MOVIE;
	}
	result = Cutscene_PlayForCurrentMissionPhase(0);
	if (result == XVT_MOVIE_PENDING) {
		g_campaign.pending = 1;
		return XVT_CAMPAIGN_PENDING;
	}
	if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER && result == 0) {
		g_frontendNetPacketScratch.packetType = NET_PACKET_PLAYER_LEFT;
		Net_SendPacketAndFlush(Net_GetHostPlayerId(), &g_frontendNetPacketScratch, sizeof(int));
		Net_ShutdownDirectPlaySession();
		FrontendScreen_SetCallbacks(FrontendNet_JoinGameScreen, FrontendMissionList_FreeScreenResources);
		return XvtCampaignTask_Finish(0);
	}
	return XvtCampaignTask_Finish(1);
}

int XvtCampaignTask_EnterDebrief(void) {
	int result;
	if (g_campaign.phase == XVT_CAMPAIGN_IDLE) {
		FrontendCursor_Show();
		Keyboard_FlushCharBuffer();
		if (g_pilotData.missionDirectoryId != MISSION_DIRECTORY_TRAINING_EXERCISES ||
			g_pilotData.missionSequenceActive != 1 || !g_pilotData.campaignSequenceState.lastMissionCompleted)
			g_campaign.phase = XVT_CAMPAIGN_DEBRIEF_ROSTER;
		else
			g_campaign.phase = XVT_CAMPAIGN_DEBRIEF;
	}
	if (g_campaign.phase == XVT_CAMPAIGN_DEBRIEF) {
		result = Cutscene_PlayForCurrentMissionPhase(1);
		if (result == XVT_MOVIE_PENDING) {
			g_campaign.pending = 1;
			return XVT_CAMPAIGN_PENDING;
		}
		if (g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_SINGLEPLAYER && result == 0) {
			g_frontendNetPacketScratch.packetType = NET_PACKET_PLAYER_LEFT;
			Net_SendPacketAndFlush(Net_GetHostPlayerId(), &g_frontendNetPacketScratch, sizeof(int));
			Net_ShutdownDirectPlaySession();
			FrontendScreen_SetCallbacks(Concourse_Update, Concourse_Exit);
			return XvtCampaignTask_Finish(0);
		}
		g_campaign.phase = XVT_CAMPAIGN_DEBRIEF_ROSTER;
	}
	if (g_campaign.phase == XVT_CAMPAIGN_DEBRIEF_ROSTER) {
		if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TRAINING_EXERCISES &&
			g_pilotData.missionSequenceActive == 1)
			g_briefingText = malloc(4096);
		g_frontendChatTeamOnly = 0;
		g_debriefDisconnectedFromNetGame = 0;
		g_frontendFirstVisibleLine = 0;
		for (int i = 0; i < 8; ++i) {
			if (g_pilotData.networkPlayers[i].directPlayId && g_pilotData.networkPlayers[i].hasLeft) {
				Net_ClearPlayerReadyFlagWithLockGuard(g_pilotData.networkPlayers[i].directPlayId);
				if (Net_GetLocalPlayerId() == g_pilotData.networkPlayers[i].directPlayId)
					g_debriefDisconnectedFromNetGame = 1;
			}
		}
		Net_ResetRosterToLocalPlayerWithLockGuard();
		if (g_pilotData.promotionDelta == PILOT_PROMOTION_NONE ||
			g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_SINGLEPLAYER)
			return XvtCampaignTask_Finish(1);
		g_campaign.player_info[0] = (char)(g_pilotData.rating + 1);
		g_campaign.player_info[1] = 0;
		g_campaign.phase = XVT_CAMPAIGN_DEBRIEF_RENAME;
	}
	result = Net_SetPlayerNameWithLockGuard(Net_GetLocalPlayerId(), g_campaign.player_info, g_pilotData.name);
	if (result == XVT_CAMPAIGN_PENDING) {
		g_campaign.pending = 1;
		return result;
	}
	return XvtCampaignTask_Finish(1);
}

int XvtCampaignTask_IsPending(void) { return g_campaign.pending; }

int XvtCampaignTask_ContinuesWithoutFocus(void) { return g_campaign.packet_type != 0; }

void XvtCampaignTask_Reset(void) {
	memset(&g_campaign, 0, sizeof(g_campaign));
	XvtCutsceneTask_Reset();
}
