#define _POSIX_C_SOURCE 200809L
/* Checks the campaign task (xvt_runtime/runtime/campaign_task.h) against the promises in its header: the
 * team-assignment prefix's immediate returns, a network client waiting for the host's continuation and
 * then, past the wait, clearing the remote battle state and leaving the game when no cutscene plays; the
 * debrief prefix's cursor, keyboard, briefing text, cutscenes and network leave; the packet wait's limit
 * and its pending flag; and Reset. The installation check passes on empty files at the names it looks for,
 * in a temporary asset folder; the host clock moves only when the test advances it. Every case starts from
 * Reset, a cleared frontend and pilot, no network session, the clock at 0, and an installed game.
 *
 * Not checked here: a continuation packet actually arriving, the single-player and host continuations
 * (they read the game's mission lists), the end of the program on a missing installation, and the renaming
 * of a promoted player, which needs a DirectPlay session. */
#include "test_assert.h"
#include "test_asset_folder.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt_runtime/runtime/campaign_task.h"
#include "xvt_runtime/runtime/movie_task.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/timing/host_clock.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static XvtTestAssets g_assets;
static CutsceneEntry g_table[1];

static int Placeholder(int frame) { return frame; }

static void Fresh(int session_mode) {
	XvtCampaignTask_Reset();
	XvtMovieTask_Shutdown();
	XvtNetworkSession_Shutdown();
	XvtTest_CloseAssets(&g_assets);
	XvtTime_Reset();
	memset(&g_frontState, 0, sizeof g_frontState);
	memset(&g_pilotData, 0, sizeof g_pilotData);
	g_frontState.screenStates[0].updateFn = Placeholder;
	g_frontendMissionSessionMode = session_mode;
	g_frontendSkipScreenEntrySetup = 0;
	g_missionSetupDebriefTransition = 0;
	g_cutsceneTable = NULL;
	g_cutsceneCount = 0;
	free(g_missionText);
	g_missionText = NULL;
	g_remoteBattleContinuationActive = 0;
	g_remoteBattleSequenceContinuationChoice = 0;
	g_remoteBattleLastCompletedMissionIndex = 0;
	g_remoteBattleRebelVictoryCount = 0;
	g_remoteBattleImperialVictoryCount = 0;
	XvtTest_OpenAssets(&g_assets);
	XvtTest_AddAsset(&g_assets, "wave/PBC/Pb1los07.wav");
	XvtTest_AddAsset(&g_assets, "ivfiles/cal.opt");
	XvtTest_AddAsset(&g_assets, "train/1ta01bf.tie");
}

/* A remote battle in progress, which a failed continuation clears. */
static void RemoteBattle(void) {
	g_remoteBattleContinuationActive = 1;
	g_remoteBattleSequenceContinuationChoice = 2;
	g_remoteBattleLastCompletedMissionIndex = 3;
	g_remoteBattleRebelVictoryCount = 4;
	g_remoteBattleImperialVictoryCount = 5;
}

static void CheckRemoteBattleCleared(void) {
	XVT_ASSERT_INT_EQ(g_remoteBattleContinuationActive, 0);
	XVT_ASSERT_INT_EQ(g_remoteBattleSequenceContinuationChoice, 0);
	XVT_ASSERT_INT_EQ(g_remoteBattleLastCompletedMissionIndex, 0);
	XVT_ASSERT_INT_EQ(g_remoteBattleRebelVictoryCount, 0);
	XVT_ASSERT_INT_EQ(g_remoteBattleImperialVictoryCount, 0);
}

static FrontendScreenUpdateFn TopScreen(void) {
	return g_frontState.screenStates[g_frontState.screenStackTop].updateFn;
}

static void AdvanceMs(int ms) { XvtTime_AdvanceHostClock(ms * 1000); }

static void CheckWaitPacketLimit(void) {
	int dummy = 0;
	int* packet = &dummy;
	Fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_ContinuesWithoutFocus(), 0);

	/* The wait starts with its first call, here 5 s after the clock's start. */
	AdvanceMs(5000);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_WaitPacket(NET_PACKET_CAMPAIGN_CONTINUATION, &packet),
					  XVT_CAMPAIGN_PENDING);
	XVT_ASSERT_TRUE(packet == NULL);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_ContinuesWithoutFocus(), 1);

	/* Exactly 30 s later it still waits; past that it gives up with 0. */
	AdvanceMs(30000);
	packet = &dummy;
	XVT_ASSERT_INT_EQ(XvtCampaignTask_WaitPacket(NET_PACKET_CAMPAIGN_CONTINUATION, &packet),
					  XVT_CAMPAIGN_PENDING);
	XVT_ASSERT_TRUE(packet == NULL);
	AdvanceMs(1);
	packet = &dummy;
	XVT_ASSERT_INT_EQ(XvtCampaignTask_WaitPacket(NET_PACKET_CAMPAIGN_CONTINUATION, &packet), 0);
	XVT_ASSERT_TRUE(packet == NULL);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_ContinuesWithoutFocus(), 0);

	/* The next call starts a new wait. */
	XVT_ASSERT_INT_EQ(XvtCampaignTask_WaitPacket(NET_PACKET_BATTLE_CONTINUATION, &packet),
					  XVT_CAMPAIGN_PENDING);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_ContinuesWithoutFocus(), 1);
}

static void CheckResetForgetsWait(void) {
	int* packet = NULL;
	Fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_WaitPacket(NET_PACKET_CAMPAIGN_CONTINUATION, &packet),
					  XVT_CAMPAIGN_PENDING);
	AdvanceMs(20000);
	XvtCampaignTask_Reset();
	XVT_ASSERT_INT_EQ(XvtCampaignTask_ContinuesWithoutFocus(), 0);
	/* A wait after Reset starts its own 30 s: 20 s on, the old limit is past but the new one is not. */
	XVT_ASSERT_INT_EQ(XvtCampaignTask_WaitPacket(NET_PACKET_CAMPAIGN_CONTINUATION, &packet),
					  XVT_CAMPAIGN_PENDING);
	AdvanceMs(20000);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_WaitPacket(NET_PACKET_CAMPAIGN_CONTINUATION, &packet),
					  XVT_CAMPAIGN_PENDING);
}

static void CheckTeamsReturnAtOnce(void) {
	/* No mission sequence. */
	Fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	g_pilotData.missionDirectoryId = MISSION_DIRECTORY_TRAINING_EXERCISES;
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterTeams(), 1);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_IsPending(), 0);

	/* A sequence, but the entry movie is skipped, or the debrief chose to enter the current mission. */
	Fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	g_pilotData.missionSequenceActive = 1;
	g_frontendSkipScreenEntrySetup = 1;
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterTeams(), 1);
	Fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	g_pilotData.missionSequenceActive = 1;
	g_missionSetupDebriefTransition = MISSION_SETUP_DEBRIEF_TRANSITION_ENTER_CURRENT_MISSION;
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterTeams(), 1);

	/* A sequence in a directory other than training or combat. */
	Fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	g_pilotData.missionSequenceActive = 1;
	g_pilotData.missionDirectoryId = MISSION_DIRECTORY_MELEES;
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterTeams(), 1);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_IsPending(), 0);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_ContinuesWithoutFocus(), 0);
}

static void CheckTeamsCampaignClient(void) {
	Fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	g_pilotData.missionSequenceActive = 1;
	g_pilotData.missionDirectoryId = MISSION_DIRECTORY_TRAINING_EXERCISES;
	RemoteBattle();

	/* The client waits for the host's continuation. */
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterTeams(), XVT_CAMPAIGN_PENDING);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_IsPending(), 1);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_ContinuesWithoutFocus(), 1);
	AdvanceMs(1000);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterTeams(), XVT_CAMPAIGN_PENDING);
	XVT_ASSERT_TRUE(TopScreen() == Placeholder);

	/* None comes: the continuation returns 0 and the remote battle state is cleared. With no cutscene
	 * table the cutscene result is 0, so the client leaves: session shut down, join screen, 0. */
	AdvanceMs(30000);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterTeams(), 0);
	CheckRemoteBattleCleared();
	XVT_ASSERT_TRUE(TopScreen() == FrontendNet_JoinGameScreen);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_GetStatus().state, XVT_NETWORK_SESSION_PENDING);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_IsPending(), 0);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_ContinuesWithoutFocus(), 0);
}

static void CheckTeamsBattleClient(void) {
	Fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	g_pilotData.missionSequenceActive = 1;
	g_pilotData.missionDirectoryId = MISSION_DIRECTORY_COMBAT_ENGAGEMENTS;
	RemoteBattle();
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterTeams(), XVT_CAMPAIGN_PENDING);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_IsPending(), 1);

	/* A battle plays no cutscenes: once the wait gives up, the state is cleared and the prefix is done. */
	AdvanceMs(30001);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterTeams(), 1);
	CheckRemoteBattleCleared();
	XVT_ASSERT_TRUE(TopScreen() == Placeholder);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_IsPending(), 0);
}

static void CheckResetForgetsPrefix(void) {
	Fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	g_pilotData.missionSequenceActive = 1;
	g_pilotData.missionDirectoryId = MISSION_DIRECTORY_COMBAT_ENGAGEMENTS;
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterTeams(), XVT_CAMPAIGN_PENDING);
	XvtCampaignTask_Reset();
	XVT_ASSERT_INT_EQ(XvtCampaignTask_IsPending(), 0);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_ContinuesWithoutFocus(), 0);
	/* The next call starts the prefix over, with a fresh 30 s wait. */
	AdvanceMs(29000);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterTeams(), XVT_CAMPAIGN_PENDING);
	AdvanceMs(29000);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterTeams(), XVT_CAMPAIGN_PENDING);
}

static void CheckDebriefOutsideTraining(void) {
	Fresh(FRONTEND_MISSION_SESSION_SINGLEPLAYER);
	g_pilotData.missionDirectoryId = MISSION_DIRECTORY_MELEES;
	g_pilotData.missionSequenceActive = 1;
	g_frontState.cursorVisible = 0;
	g_frontState.charRingBuffer[0] = 'k';
	g_frontState.charWriteIdx = 1;
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterDebrief(), 1);
	XVT_ASSERT_INT_EQ(g_frontState.cursorVisible, 1);
	XVT_ASSERT_INT_EQ(g_frontState.charReadIdx, g_frontState.charWriteIdx);
	/* No briefing text outside a training sequence. */
	XVT_ASSERT_TRUE(g_missionText == NULL);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_IsPending(), 0);
}

static void CheckDebriefTrainingSequence(void) {
	/* An active training sequence gets its briefing text, whether or not the mission was completed. */
	for (int completed = 0; completed < 2; ++completed) {
		Fresh(FRONTEND_MISSION_SESSION_SINGLEPLAYER);
		g_pilotData.missionDirectoryId = MISSION_DIRECTORY_TRAINING_EXERCISES;
		g_pilotData.missionSequenceActive = 1;
		g_pilotData.campaignSequenceState.lastMissionCompleted = completed;
		XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterDebrief(), 1);
		XVT_ASSERT_TRUE(g_missionText != NULL);
		XVT_ASSERT_TRUE(TopScreen() == Placeholder);
	}
}

static void CheckDebriefNetworkLeaves(void) {
	/* A completed training mission in a network session with no cutscene table: the cutscene result is
	 * 0, so the player leaves for the concourse. */
	Fresh(FRONTEND_MISSION_SESSION_NET_HOST);
	g_pilotData.missionDirectoryId = MISSION_DIRECTORY_TRAINING_EXERCISES;
	g_pilotData.missionSequenceActive = 1;
	g_pilotData.campaignSequenceState.lastMissionCompleted = 1;
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterDebrief(), 0);
	XVT_ASSERT_TRUE(TopScreen() == Concourse_Update);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_GetStatus().state, XVT_NETWORK_SESSION_PENDING);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_IsPending(), 0);

	/* An incomplete mission plays no cutscenes, so the network player stays. */
	Fresh(FRONTEND_MISSION_SESSION_NET_HOST);
	g_pilotData.missionDirectoryId = MISSION_DIRECTORY_TRAINING_EXERCISES;
	g_pilotData.missionSequenceActive = 1;
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterDebrief(), 1);
	XVT_ASSERT_TRUE(TopScreen() == Placeholder);
}

static void CheckDebriefWaitsForCutscene(void) {
	Fresh(FRONTEND_MISSION_SESSION_SINGLEPLAYER);
	g_pilotData.missionDirectoryId = MISSION_DIRECTORY_TRAINING_EXERCISES;
	g_pilotData.missionSequenceActive = 1;
	g_pilotData.campaignSequenceState.lastMissionCompleted = 1;
	memset(g_table, 0, sizeof g_table);
	strcpy(g_table[0].movieName, "debrief");
	g_table[0].playAfterDebriefing = 1;
	g_cutsceneTable = g_table;
	g_cutsceneCount = 1;
	XvtTest_AddAsset(&g_assets, "movies/debrief.smk");

	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterDebrief(), XVT_CAMPAIGN_PENDING);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_IsPending(), 1);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterDebrief(), XVT_CAMPAIGN_PENDING);

	/* The movie fails on the empty file; single-player, the debrief goes on and finishes. */
	struct timespec pause = { 0, 1000000 };
	for (int i = 0; i < 10000 && XvtMovieTask_IsActive(); ++i) {
		XvtMovieTask_Update();
		nanosleep(&pause, NULL);
	}
	XvtMovieTask_ReapFinished();
	XVT_ASSERT_INT_EQ(XvtCampaignTask_EnterDebrief(), 1);
	XVT_ASSERT_INT_EQ(XvtCampaignTask_IsPending(), 0);
	XVT_ASSERT_TRUE(g_missionText != NULL);
	g_cutsceneTable = NULL;
	g_cutsceneCount = 0;
}

int main(void) {
	CheckWaitPacketLimit();
	CheckResetForgetsWait();
	CheckTeamsReturnAtOnce();
	CheckTeamsCampaignClient();
	CheckTeamsBattleClient();
	CheckResetForgetsPrefix();
	CheckDebriefOutsideTraining();
	CheckDebriefTrainingSequence();
	CheckDebriefNetworkLeaves();
	CheckDebriefWaitsForCutscene();
	XvtCampaignTask_Reset();
	XvtMovieTask_Shutdown();
	XvtNetworkSession_Shutdown();
	XvtTest_CloseAssets(&g_assets);
	free(g_missionText);
	g_missionText = NULL;
	return 0;
}
