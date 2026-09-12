#ifndef XVT_FRONTEND_CONCOURSE_H
#define XVT_FRONTEND_CONCOURSE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern char (*g_pilotListDisplayNames)[14];
extern FrontendFileList* g_pilotFileList;
extern MissionListEntry* g_pilotRecordTournamentMissionList;
extern int g_pilotRecordTournamentMissionCount;
extern MissionListEntry* g_pilotRecordMeleeMissionList;
extern int g_pilotRecordMeleeMissionCount;
extern MissionListEntry* g_pilotRecordSingleplayerCombatMissionList;
extern int g_pilotRecordSingleplayerCombatMissionCount;
extern MissionListEntry* g_pilotRecordMultiplayerCombatMissionList;
extern int g_pilotRecordMultiplayerCombatMissionCount;
extern MissionListEntry* g_pilotRecordSingleplayerCampaignMissionList;
extern int g_pilotRecordSingleplayerCampaignMissionCount;
extern MissionListEntry* g_pilotRecordMultiplayerCampaignMissionList;
extern int g_pilotRecordMultiplayerCampaignMissionCount;
extern MissionListEntry* g_pilotRecordSingleplayerTrainingMissionList;
extern int g_pilotRecordSingleplayerTrainingMissionCount;
extern MissionListEntry* g_pilotRecordMultiplayerTrainingMissionList;
extern int g_pilotRecordMultiplayerTrainingMissionCount;

int Concourse_Exit(int frameCounter);
int Concourse_Update(int frameCounter);

#ifdef __cplusplus
}
#endif

#endif
