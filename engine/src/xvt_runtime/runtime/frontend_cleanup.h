#ifndef XVT_RUNTIME_FRONTEND_CLEANUP_H
#define XVT_RUNTIME_FRONTEND_CLEANUP_H

#ifdef __cplusplus
extern "C" {
#endif

int XvtFrontendCleanup_MissionResources(int frame);
int XvtFrontendCleanup_CurrentMission(int frame);
int XvtFrontendCleanup_NextMission(int frame);
int XvtFrontendCleanup_BattleChoice(int frame);

#ifdef __cplusplus
}
#endif

#endif
