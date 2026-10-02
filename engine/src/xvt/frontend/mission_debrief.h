#ifndef XVT_FRONTEND_MISSION_DEBRIEF_H
#define XVT_FRONTEND_MISSION_DEBRIEF_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_debriefDisconnectedFromNetGame;

struct CampaignAwardSpriteEntry {
	int campaignId;
	char mainAwardSpriteName[32];
	char singleplayerMissionAwardSpriteNames[16][32];
	char multiplayerMissionAwardSpriteNames[16][32];
};

extern int g_debriefTeamInStandings[10];
extern int g_debriefTeamHasOnlyAiPilots[10];
extern int g_debriefLocalTeamRankIndex;
extern int g_debriefActiveTeamCount;
extern int g_debriefSortedTeamIds[10];
extern int g_debriefTeamHasPlayer[10];
extern int g_debriefSortedPlayerIds[8];
extern int g_debriefHasPlayerKillsByRating;
extern int g_debriefKillsOnCombatantIds[8];
extern int g_debriefKillsFromCombatantIds[8];
extern int g_debriefStandingsTeamIds[10];
extern int g_debriefPlayerKillsSharedTotal[3];
extern int g_debriefRankByPilot;

int MissionDebrief_Exit(int frameCounter);
int MissionDebrief_Update(int frameCounter);
int MissionDebrief_DrawMissionOverviewPage(int frameCounter);
int MissionDebrief_DrawPlayerStatisticsPage(void);
int MissionDebrief_DrawTeamStatisticsPage(void);
int MissionDebrief_DrawBattleSummaryPage(void);
int MissionDebrief_DrawTournamentSummaryPage(int frameCounter);
int MissionDebrief_DrawTabBar(void);
int MissionDebrief_MarkNetworkPlayersReady(void);
int MissionDebrief_Prepare(void);
void MissionDebrief_BuildText(char* outResults, int useWinText);
int MissionDebrief_DrawNarrativeTextPage(void);

#ifdef __cplusplus
}
#endif

#endif
