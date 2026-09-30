#ifndef XVT_RUNTIME_FRONTEND_CLEANUP_H
#define XVT_RUNTIME_FRONTEND_CLEANUP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Screen exit callbacks with the FrontendScreenExitFn signature, for the modern arms. The original
 * code casts the wrapped functions, which take no argument, to that type; these wrappers ignore
 * frame and return the wrapped function's result. */

/* FrontendMissionList_FreeScreenResourcesAndClearInputGate. */
int XvtFrontendCleanup_MissionResources(int frame);
/* MissionSetup_ExitCurrentMission. */
int XvtFrontendCleanup_CurrentMission(int frame);
/* MissionSetup_ExitNextMission. */
int XvtFrontendCleanup_NextMission(int frame);
/* MissionSetup_BattleChoice_Exit. */
int XvtFrontendCleanup_BattleChoice(int frame);

#ifdef __cplusplus
}
#endif

#endif
