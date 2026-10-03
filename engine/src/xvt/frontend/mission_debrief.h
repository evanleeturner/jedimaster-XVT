#ifndef XVT_FRONTEND_MISSION_DEBRIEF_H
#define XVT_FRONTEND_MISSION_DEBRIEF_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_debriefDisconnectedFromNetGame;

struct CampaignAwardSpriteEntry {
	/* Campaign list id the record belongs to, from its first line; 0 for a
	 * record the file's end cut short. */
	int campaignId;
	/* Sprite of the campaign's medal, which the campaign medals page
	 * centers on (256, 279); cut to 31 characters. */
	char mainAwardSpriteName[32];
	/* Sprites drawn over the medal for single-player award positions 0 to
	 * 14, from the record's last 15 lines; the 16th stays empty. */
	char singleplayerMissionAwardSpriteNames[16][32];
	/* Sprites drawn over the medal for multiplayer award positions 0 to 14,
	 * from the 15 lines before the single-player ones; the 16th stays
	 * empty. */
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
void MissionDebrief_ReadOutcomeText(char *outResults, int useWinText);
int MissionDebrief_DrawNarrativeTextPage(void);

#ifdef __cplusplus
}
#endif

#endif
