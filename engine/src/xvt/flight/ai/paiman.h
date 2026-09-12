#ifndef XVT_FLIGHT_AI_PAIMAN_H
#define XVT_FLIGHT_AI_PAIMAN_H

#include "xvt/flight/ai/pai.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const int16_t g_aiCourseOrderLocalOffsetXByVar[28];
extern const int16_t g_aiCourseOrderLocalOffsetYByVar[28];
extern const int16_t g_aiCourseOrderLocalOffsetZByVar[28];
extern const int16_t g_formPosX[34][6];
extern const int16_t g_formPosY[34][6];
extern const int16_t g_formPosZ[34][6];
extern const int16_t g_formationDivisor[34];

typedef int16_t (*AiCourseOrderManeuverProc)(void);

typedef void (*AiManeuverInitProc)(void);

extern AiCourseOrderManeuverProc g_aiCourseOrderManeuverTable[AI_MANEUVER_MODE_COUNT];
extern AiCourseOrderManeuverProc g_aiCourseOrderManeuverMode;
extern uint16_t g_orderThrottleToCraftThrottleSpeed[12];
/* The matching translation unit defines this before use; modern consumers need the declaration. */
#ifdef XVT_MODERN
extern uint16_t g_aiTurnAwayStateDelayBySkill[4];
#endif

void paiman_initmaneuver(void);
void paiman_initturninsidemaneuver(void);
int16_t paiman_turninsidemaneuver(void);
void paiman_UpdateTurnInsideHeading(unsigned int fallbackObjIdx);
void paiman_initsplitsmaneuver(void);
int16_t paiman_splitsmaneuver(void);
void paiman_initimmelmannmaneuver(void);
int16_t paiman_immelmannmaneuver(void);
void paiman_initscissorsmaneuver(void);
int16_t paiman_scissorsmaneuver(void);
void paiman_initrendezvousmaneuver(void);
int16_t paiman_rendezvousmaneuver(void);
void paiman_initcruisemaneuver(void);
int16_t paiman_cruisemaneuver(void);
void paiman_AdvanceOrderWaypoint(int objectIndex);
void paiman_initheadtowardfullmaneuver(void);
int16_t paiman_headtowardfullmaneuver(void);
void paiman_initrunawaymaneuver(void);
int16_t paiman_runawaymaneuver(void);
void paiman_initheadonattackmaneuver(void);
int16_t paiman_headonattackmaneuver(void);
void nullsub_15(void);
int16_t paiman_followleadermaneuver(void);
void paiman_initsetupattackmaneuver(void);
int16_t paiman_setupattackmaneuver(void);
void paiman_initattackmaneuver(void);
int16_t paiman_attackmaneuver(void);
void paiman_initzoommaneuver(void);
int16_t paiman_zoommaneuver(void);
void paiman_initdivemaneuver(void);
int16_t paiman_zoommaneuver_2(void);
void paiman_initsplitsdivemaneuver(void);
int16_t paiman_splitsmaneuver_2(void);
void paiman_initspeedawaymaneuver(void);
int16_t paiman_speedawaymaneuver(void);
void paiman_SetupSpeedAwayTurn(unsigned int objectIdx);
void paiman_initintohyperspacemaneuver(void);
int16_t paiman_intohyperspacemaneuver(void);
void paiman_initoutofhyperspacemaneuver(void);
int16_t paiman_outofhyperspacemaneuver(void);
void nullsub_16(void);
int16_t paiman_escortmaneuver(void);
void paiman_initboardmaneuver(void);
int16_t paiman_boardmaneuver(void);
void paiman_TransferObjectToAiTeam(unsigned int objectIdx, CraftData* craft, uint8_t ownerFlag);
void paiman_initawaitboardmaneuver(void);
int16_t paiman_awaitboardmaneuver(void);
void paiman_initheadtowardmaneuver(void);
int16_t paiman_headtowardmaneuver(void);
void paiman_initturnawaymaneuver(void);
int16_t paiman_turnawaymaneuver(void);
void paiman_setupturnawaycourse(unsigned int objectIdx);
void paiman_initoutofhangarmaneuver(void);
int16_t paiman_outofhangarmaneuver(void);
void paiman_initavoidstarshipmaneuver(void);
int16_t paiman_avoidstarshipmaneuver(void);
void paiman_initwaitmaneuver(void);
int16_t paiman_waitmaneuver(void);
void paiman_initawaitboardmaneuver_2(void);
int16_t paiman_dropoffmaneuver(void);
void paiman_initkamikazemaneuver(void);
int16_t paiman_kamikazemaneuver(void);
void paiman_initavoidattackermaneuver(void);
int16_t paiman_avoidattackermaneuver(void);
void paiman_initkamikazemaneuver_2(void);
int16_t paiman_dodgemaneuver(void);
void paiman_setflighttotarget(uint16_t pitchBias, int driveHeading);
void paiman_initcruiseandrunawaycontrols(void);
void paiman_attacktarget(int16_t yawOffset);
void paiman_calcplanelead(int targetObjIdx);
void paiman_calcformation(void);
void paiman_setturn(int turnStep);
void paiman_setpower(int objIdx, int throttle);
void paiman_setspeed(int objIdx, unsigned int desiredSpeed);

#ifdef __cplusplus
}
#endif

#endif
