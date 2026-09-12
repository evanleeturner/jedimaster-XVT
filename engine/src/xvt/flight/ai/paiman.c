#include "xvt/flight/ai/paiman.h"
#ifdef XVT_MODERN
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/reference_motion.h"
#endif
#include "xvt/assets/model_bounds.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"

#include <string.h>

// GLOBAL: XVT 0x527768
const int16_t g_aiCourseOrderLocalOffsetXByVar[28] = { -3072, 0,     3072,  -3072, 0,     3072,  -3072,
													   0,     3072,  -3072, 0,     3072,  -3072, 0,
													   3072,  -3072, 0,     3072,  -3072, 0,     3072,
													   -3072, 0,     3072,  -3072, 0,     3072,  0 };
// GLOBAL: XVT 0x5277A0
const int16_t g_aiCourseOrderLocalOffsetYByVar[28] = { 3072,  3072,  3072,  3072,  3072,  3072,  3072,
													   3072,  3072,  0,     0,     0,     0,     0,
													   0,     0,     0,     0,     -3072, -3072, -3072,
													   -3072, -3072, -3072, -3072, -3072, -3072, 0 };
// GLOBAL: XVT 0x5277D8
const int16_t g_aiCourseOrderLocalOffsetZByVar[28] = { 3072,  3072,  3072,  0,     0,     0,     -3072,
													   -3072, -3072, 3072,  3072,  3072,  0,     0,
													   0,     -3072, -3072, -3072, 3072,  3072,  3072,
													   0,     0,     0,     -3072, -3072, -3072, 0 };

// GLOBAL: XVT 0x527970
const int16_t g_formPosX[34][6] = {
	{ 0, 1, -1, 2, -2, 3 },     { 0, 1, -2, -3, 2, 3 },     { 0, 0, 0, 0, 0, 0 },    { 0, 1, -1, 2, -2, 3 },
	{ 1, 2, 3, 4, 5, 6 },       { -1, -2, -3, -4, -5, -6 }, { 0, -1, 0, -1, 0, -1 }, { 0, 1, -1, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0 },       { 0, 1, -1, 1, -1, 0 },     { 0, 1, -1, 2, -2, 3 },  { 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0 },       { 0, 0, 0, 0, 0, 0 },       { 0, 0, 0, 0, 0, 0 },    { 0, 1, 2, 3, 4, 5 },
	{ -1, -2, -3, -4, -5, -6 }, { 0, 0, 1, 2, 3, 4 },       { 1, 2, 3, 4, 5, 6 },    { 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0 },       { 0, 1, -1, 2, -2, 3 },     { 0, 1, -1, 2, -2, 3 },  { -1, 1, -1, 1, -1, 1 },
	{ 0, 0, 0, 0, 0, 0 },       { -1, 1, -1, 1, -1, 1 },    { 0, 0, 0, -1, 1, 0 },   { 0, 1, -1, 0, 0, 0 },
	{ 0, 2, -3, 3, -2, 0 },     { 0, 0, 0, 0, 0, 0 },       { 0, 2, -3, 3, -2, 0 },  { -1, 1, -2, 2, -1, 1 },
	{ 0, 0, 0, 0, 0, 0 },       { -1, 1, -2, 2, -1, 1 },
};
// GLOBAL: XVT 0x527B08
const int16_t g_formPosY[34][6] = {
	{ 0, -1, -1, -2, -2, -3 },  { 3, 2, 1, 0, -1, -2 },    { -1, -2, -3, -4, -5, -6 },
	{ 0, 0, 0, 0, 0, 0 },       { 0, -1, -2, -3, -4, -5 }, { 0, -1, -2, -3, -4, -5 },
	{ 0, 0, -1, -1, -2, -2 },   { 1, 0, 0, -1, 0, 0 },     { 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, -1 },      { 1, 2, 2, 3, 3, 4 },      { 0, -1, -1, -2, -2, -3 },
	{ 1, 2, 2, 3, 3, 4 },       { 0, 1, 2, 3, 4, 5 },      { 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0 },       { 0, 0, 0, 0, 0, 0 },      { 0, 1, 2, 3, 4, 5 },
	{ -1, -2, -3, -4, -5, -6 }, { 0, -1, -2, -3, -4, -5 }, { 0, -1, -2, -3, -4, -5 },
	{ 0, 0, 0, 0, 0, 0 },       { 0, 0, 0, 0, 0, 0 },      { 2, 2, 0, 0, -2, -2 },
	{ 2, 2, 0, 0, -2, -2 },     { 0, 0, 0, 0, 0, 0 },      { 1, 0, 0, 0, 0, -1 },
	{ 0, 0, 0, 0, 1, -1 },      { 3, -3, 1, 1, -3, 0 },    { 3, -3, 1, 1, -3, 0 },
	{ 0, 0, 0, 0, 0, 0 },       { 2, 2, 0, 0, -2, -2 },    { 2, 2, 0, 0, -2, -2 },
	{ 0, 0, 0, 0, 0, 0 },
};
// GLOBAL: XVT 0x527CA0
const int16_t g_formPosZ[34][6] = {
	{ 0, 0, 0, 0, 0, 0 },       { 0, 1, -1, -2, 2, 3 }, { 0, 0, 0, 0, 0, 0 },       { 0, 0, 0, 0, 0, 0 },
	{ 0, 1, 2, 3, 4, 5 },       { 0, 1, 2, 3, 4, 5 },   { 0, 0, 0, 0, 0, 0 },       { 0, 0, 0, 0, 1, -1 },
	{ 0, 1, 2, 3, 4, 5 },       { 0, 1, 1, -1, -1, 0 }, { 0, 0, 0, 0, 0, 0 },       { 0, 1, -1, 2, -2, 3 },
	{ 0, 1, -1, 2, -2, 3 },     { 0, 0, 0, 0, 0, 0 },   { -1, -2, -3, -4, -5, -6 }, { 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0 },       { 0, 1, 2, 3, 4, 5 },   { 1, 2, 3, 4, 5, 6 },       { 0, 1, 2, 3, 4, 5 },
	{ -1, -2, -3, -4, -5, -6 }, { 0, 1, 1, 2, 2, 3 },   { 0, -1, -1, -2, -2, -3 },  { 0, 0, 0, 0, 0, 0 },
	{ 1, -1, 1, -1, 1, -1 },    { 2, 2, 0, 0, -2, -2 }, { 0, 1, -1, 0, 0, 0 },      { 1, 0, 0, -1, 0, 0 },
	{ 0, 0, 0, 0, 0, 0 },       { 0, 2, -3, 3, -2, 0 }, { 3, -3, 1, 1, -3, 0 },     { 0, 0, 0, 0, 0, 0 },
	{ 1, -1, 2, -2, 1, -1 },    { 2, 2, 0, 0, -2, -2 },
};
// GLOBAL: XVT 0x527E38
const int16_t g_formationDivisor[34] = { 1, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
										 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 3, 2, 2, 2 };

// GLOBAL: XVT 0x527760
uint16_t g_aiTurnAwayStateDelayBySkill[4] = { 5, 3, 1, 0 };

// GLOBAL: XVT 0x527810
uint16_t g_orderThrottleToCraftThrottleSpeed[12] = { 0,     6553,  13108, 19662, 26216, 32768,
													 39322, 45876, 52430, 58984, 65535, 0 };

// GLOBAL: XVT 0x527950
const uint16_t g_aiAvoidAttackerDelaySecondsByGroupAI[8] = { 8, 6, 5, 3, 2, 1, 0, 0 };

// GLOBAL: XVT 0x527960
const uint16_t g_aiAvoidAttackerDelayFracQ16ByGroupAI[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

// GLOBAL: XVT 0x527938
const uint16_t g_aiCourseOrderSpeedByManeuverPhase[12] = {
	3600, 3600, 3600, 3600, 3600, 3600, 1800, 1800, 1800, 900, 900, 0,
};

// GLOBAL: XVT 0x527828
AiCourseOrderManeuverProc g_aiCourseOrderManeuverTable[AI_MANEUVER_MODE_COUNT] = {
	paiorder_nullhandler,           paiman_turninsidemaneuver,
	paiman_splitsmaneuver,          paiman_immelmannmaneuver,
	paiman_scissorsmaneuver,        paiman_rendezvousmaneuver,
	paiman_cruisemaneuver,          paiman_headtowardfullmaneuver,
	paiman_runawaymaneuver,         paiman_headonattackmaneuver,
	paiman_followleadermaneuver,    paiman_setupattackmaneuver,
	paiman_attackmaneuver,          paiman_zoommaneuver,
	paiman_zoommaneuver_2,          paiman_splitsmaneuver_2,
	paiman_speedawaymaneuver,       paiman_escortmaneuver,
	paiman_boardmaneuver,           paiman_awaitboardmaneuver,
	paiman_headtowardmaneuver,      paiman_intohyperspacemaneuver,
	paiman_outofhyperspacemaneuver, paiman_attackmaneuver,
	paiman_turnawaymaneuver,        paiman_awaitboardmaneuver,
	paiman_outofhangarmaneuver,     paiman_splitsmaneuver_2,
	paiman_avoidstarshipmaneuver,   paiman_waitmaneuver,
	paiman_dropoffmaneuver,         paiman_kamikazemaneuver,
	paiman_avoidattackermaneuver,   paiman_dodgemaneuver,
};

// GLOBAL: XVT 0x5278B0
AiManeuverInitProc g_maneuverInitTable[AI_MANEUVER_MODE_COUNT] = {
	(AiManeuverInitProc)paiorder_nullhandler,
	paiman_initturninsidemaneuver,
	paiman_initsplitsmaneuver,
	paiman_initimmelmannmaneuver,
	paiman_initscissorsmaneuver,
	paiman_initrendezvousmaneuver,
	paiman_initcruisemaneuver,
	paiman_initheadtowardfullmaneuver,
	paiman_initrunawaymaneuver,
	paiman_initheadonattackmaneuver,
	nullsub_15,
	paiman_initsetupattackmaneuver,
	paiman_initattackmaneuver,
	paiman_initzoommaneuver,
	paiman_initdivemaneuver,
	paiman_initsplitsdivemaneuver,
	paiman_initspeedawaymaneuver,
	nullsub_16,
	paiman_initboardmaneuver,
	paiman_initawaitboardmaneuver,
	paiman_initheadtowardmaneuver,
	paiman_initintohyperspacemaneuver,
	paiman_initoutofhyperspacemaneuver,
	paiman_initattackmaneuver,
	paiman_initturnawaymaneuver,
	paiman_initawaitboardmaneuver,
	paiman_initoutofhangarmaneuver,
	paiman_initsplitsdivemaneuver,
	paiman_initavoidstarshipmaneuver,
	paiman_initwaitmaneuver,
	paiman_initawaitboardmaneuver_2,
	paiman_initkamikazemaneuver,
	paiman_initavoidattackermaneuver,
	paiman_initkamikazemaneuver_2,
};

// GLOBAL: XVT 0x9993F4
AiCourseOrderManeuverProc g_aiCourseOrderManeuverMode;

// GLOBAL: XVT 0x9993F8
AiManeuverInitProc g_aiCurrentManeuverInitProc = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x49F360
void paiman_initmaneuver(void) {
	g_curCraft->pushAccumX = 0;
	g_curCraft->pushAccumY = g_curCraft->pushAccumX;
	g_curCraft->pushAccumZ = g_curCraft->pushAccumY;
	g_curCraft->aiFlight.reactionTimer = 0;
	g_curCraft->aiFlight.maneuverCounter = 0;
	g_curCraft->warheadLockTicks = 0;
	g_curCraft->commandedSpeed =
		g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[g_paiContext.orderSlot].speed;
	g_curCraft->commandedSpeed *= 5;
	g_curCraft->aiFlight.enterFlag = 4;
	g_paiContext.controller->maneuverPhase = 0;
	g_aiCurrentManeuverInitProc = g_maneuverInitTable[g_paiContext.controller->maneuverMode];
	g_aiCurrentManeuverInitProc();
}

// FUNCTION: XVT 0x49F450
void paiman_initturninsidemaneuver(void) {
	g_paiContext.controller->maneuverPhase = GameRand() & 1;
	paiman_UpdateTurnInsideHeading(g_paiContext.objectIndex);
	g_paiContext.controller->maneuverTimer = SIMULATION_TICKS_PER_SECOND * ((GameRand() & 7) + 10);
}

// FUNCTION: XVT 0x49F4A0
int16_t paiman_turninsidemaneuver(void) {
	if (g_paiContext.controller->maneuverTimer == 0) {
		return 1;
	}
	if (g_paiContext.controller->aiPlanState == 0) {
		paiman_UpdateTurnInsideHeading(g_paiContext.objectIndex);
	}
	return 0;
}

// FUNCTION: XVT 0x49F4E0
void paiman_UpdateTurnInsideHeading(unsigned int fallbackObjIdx) {
	uint16_t yaw;
	uint16_t effectiveSkill;
	unsigned int turnStep;

	if (g_curCraft->lastAttackerObjIdx != UINT16_MAX) {
		if ((g_paiContext.controller->maneuverPhase & 1) != 0) {
			yaw = g_objectTable[g_curCraft->lastAttackerObjIdx].yaw - 0x4000;
		} else {
			yaw = g_objectTable[g_curCraft->lastAttackerObjIdx].yaw + 0x4000;
		}
	} else {
		if ((g_paiContext.controller->maneuverPhase & 1) != 0) {
			yaw = g_objectTable[fallbackObjIdx].yaw - 0x4000;
		} else {
			yaw = g_objectTable[fallbackObjIdx].yaw + 0x4000;
		}
	}
	g_paiContext.controller->targetXYAngle = yaw;
	effectiveSkill = pai_GetEffectiveSkillValue(g_curCraft);
	turnStep = effectiveSkill >> 1;
	turnStep += 0x8000;
	paiman_setturn((uint16_t)turnStep);
	g_paiContext.controller->aiPlanState =
		SIMULATION_TICKS_PER_SECOND * g_aiTurnAwayStateDelayBySkill[g_paiContext.skillTier];
}

// FUNCTION: XVT 0x49F5C0
void paiman_initsplitsmaneuver(void) {
	g_curCraft->aiFlight.rollStep = 0xFFFFu;
	g_paiContext.controller->targetRoll = 0x8000;
	g_curCraft->aiFlight.turnState = 0;
	g_curCraft->aiFlight.headingForce = 1;
	g_paiContext.controller->targetZAngle = 0x4000;
	g_curCraft->aiFlight.headingState = 2;
	g_curCraft->aiFlight.headingStep = 0xFFFFu;
}

// FUNCTION: XVT 0x49F620
int16_t paiman_splitsmaneuver(void) {
	return g_curCraft->aiFlight.enterFlag == 4 && g_curCraft->aiFlight.headingState == 3;
}

// FUNCTION: XVT 0x49F650
void paiman_initimmelmannmaneuver(void) {
	uint16_t pitch;

	paiman_setpower(g_paiContext.objectIndex, 0xFFFFu);
	g_curCraft->aiFlight.enterFlag = 1;
	g_curCraft->aiFlight.rollStep = 0xFFFFu;
	g_paiContext.controller->targetRoll = 0;
	g_curCraft->aiFlight.turnState = 0;
	g_paiContext.controller->targetZAngle = 0x4000;
	g_curCraft->aiFlight.headingStep = 0xFFFFu;
	g_curCraft->aiFlight.headingForce = 0;
	pitch = g_curCraft->pitch;
	if (pitch < 0x4000u)
		g_curCraft->aiFlight.headingState = 2;
	else if (pitch > 0x4000u)
		g_curCraft->aiFlight.headingState = 1;
	else
		g_curCraft->aiFlight.headingState = 3;
	g_paiContext.controller->maneuverTimer = 0;
}

// FUNCTION: XVT 0x49F700
int16_t paiman_immelmannmaneuver(void) {
	int maneuverPhase;

	maneuverPhase = g_paiContext.controller->maneuverPhase;
	switch (maneuverPhase) {
		case 0:
			if (g_curCraft->aiFlight.headingState == 3) {
				g_curCraft->aiFlight.headingState = 1;
				g_curCraft->aiFlight.headingStep = -1;
				g_curCraft->aiFlight.headingForce = 1;
				g_paiContext.controller->targetZAngle = 0x4000;
				g_paiContext.controller->maneuverPhase = 1;
			}
			break;
		case 1:
			if (g_curCraft->aiFlight.headingState == 3) {
				g_curCraft->aiFlight.enterFlag = 1;
				g_curCraft->aiFlight.rollStep = -1;
				g_paiContext.controller->targetRoll = 0;
				g_curCraft->aiFlight.turnState = 0;
				g_paiContext.controller->maneuverPhase = 2;
			}
			break;
		case 2:
			if (g_curCraft->aiFlight.enterFlag == 4 && g_curCraft->aiFlight.headingState == 3) {
				return 1;
			}
			break;
		default:
			break;
	}
	return 0;
}

// FUNCTION: XVT 0x49F7E0
void paiman_initscissorsmaneuver(void) {
	uint16_t effectiveSkill;
	int objectIndex;
	uint16_t lastAttackerObjIdx;
	unsigned int turnStep;

	lastAttackerObjIdx = g_curCraft->lastAttackerObjIdx;
	if (lastAttackerObjIdx != UINT16_MAX)
		objectIndex = lastAttackerObjIdx;
	else
		objectIndex = g_paiContext.objectIndex;
	g_paiContext.controller->targetXYAngle = g_objectTable[objectIndex].yaw + 0x8000;
	effectiveSkill = pai_GetEffectiveSkillValue(g_curCraft);
	turnStep = (effectiveSkill >> 1) + 0x8000;
	paiman_setturn((uint16_t)turnStep);
	g_curCraft->aiFlight.enterFlag = 3;
	g_curCraft->aiFlight.rollStep = -1;
	g_paiContext.controller->targetRoll = (uint16_t)GameRand();
	g_paiContext.controller->maneuverTimer = SIMULATION_TICKS_PER_SECOND * ((GameRand() & 7) + 10);
	g_paiContext.controller->aiPlanState = 472;
}

// FUNCTION: XVT 0x49F8B0
int16_t paiman_scissorsmaneuver(void) {
	if (g_paiContext.controller->maneuverTimer == 0) {
		g_curCraft->aiFlight.enterFlag = 4;
		return 1;
	}
	if (g_paiContext.controller->aiPlanState == 0) {
		g_curCraft->aiFlight.turnState = 1;
		g_paiContext.controller->targetXYAngle += 0x8000;
		g_paiContext.controller->targetRoll ^= 0x8000u;
		g_paiContext.controller->aiPlanState = MATH2_fraction((uint16_t)GameRand(), 0xEC) + 472;
		if (g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.groupAI >= 3 &&
			(GameRand() & 3) == 3 && g_curCraft->cmTypeId == COUNTERMEASURE_TYPE_FLARE &&
			g_curCraft->cmAmmoCount != 0 && g_curCraft->cmFireCooldownTimer == 0)
			laser_createcountermeasureprojectile(g_paiContext.objectIndex,
												 COUNTERMEASURE_PROJECTILE_OBJECT_TYPE);
	}
	return 0;
}

// FUNCTION: XVT 0x49F9A0
void paiman_initrendezvousmaneuver(void) {
	uint16_t throttle;

	paiman_setflighttotarget(0, 1);
	throttle = g_orderThrottleToCraftThrottleSpeed[g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
													   .fg.orders[g_paiContext.orderSlot]
													   .throttle];
	if (throttle == 0) {
		throttle = UINT16_MAX;
	}
	paiman_setpower(g_paiContext.objectIndex, throttle);
}

// FUNCTION: XVT 0x49FA10
int16_t paiman_rendezvousmaneuver(void) {
	uint16_t throttle;

	paiman_setflighttotarget(0, 1);
	throttle = g_orderThrottleToCraftThrottleSpeed[g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
													   .fg.orders[g_paiContext.orderSlot]
													   .throttle];
	if (throttle == 0) {
		throttle = UINT16_MAX;
	}
	paiman_setpower(g_paiContext.objectIndex, throttle);
	return 0;
}

// FUNCTION: XVT 0x49FA80
void paiman_initcruisemaneuver(void) {
	paiman_initcruiseandrunawaycontrols();
	if (g_objectTable[g_paiContext.objectIndex].roll < 0x8000) {
		paiman_setflighttotarget(0, 1);
	}
	paiman_setpower(
		g_paiContext.objectIndex,
		g_orderThrottleToCraftThrottleSpeed[g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
												.fg.orders[g_paiContext.orderSlot]
												.throttle]);
	g_paiContext.controller->aiPlanState = SIMULATION_TICKS_PER_SECOND;
}

// FUNCTION: XVT 0x49FB20
int16_t paiman_cruisemaneuver(void) {
	uint8_t waypointIndex;
	uint8_t genusId;
	int zDistance;
	ObjectRecord* object;
	uint16_t yawDifference;
	uint16_t throttleIndex;

	pai_CalcAnglesToAimPoint();
	if (trig2_polardistance < (g_objectTable[g_paiContext.objectIndex].genusId == 4 ? 0x2000 : 0x1000)) {
		waypointIndex = g_paiContext.controller->waypointIndex;
		paiman_AdvanceOrderWaypoint(g_paiContext.objectIndex);
		genusId = g_objectTable[g_paiContext.objectIndex].genusId;
		if ((genusId == 4 || genusId == 3) && g_paiContext.controller->waypointIndex == waypointIndex) {
			g_curCraft->aiFlight.turnState = 3;
			paiman_setpower(g_paiContext.objectIndex, 0);
			g_curCraft->pushAccumX =
				g_paiContext.controller->aimPointX - g_objectTable[g_paiContext.objectIndex].world_x;
			g_curCraft->pushAccumY =
				g_paiContext.controller->aimPointY - g_objectTable[g_paiContext.objectIndex].world_y;
			g_curCraft->pushAccumZ =
				g_paiContext.controller->aimPointZ - g_objectTable[g_paiContext.objectIndex].world_z;
			return 0;
		}
	}

	if (g_paiContext.controller->aiPlanState == 0) {
		if (g_curCraft->aiFlight.diveState != 1 && g_curCraft->aiFlight.climbState != 1) {
			zDistance = g_paiContext.controller->aimPointZ - g_objectTable[g_paiContext.objectIndex].world_z;
			if (zDistance < 0)
				zDistance = -zDistance;
			if (zDistance > 512) {
				g_paiContext.controller->targetZAngle = (uint16_t)pitchQ16;
				if (g_paiContext.controller->targetZAngle <= g_curCraft->pitch)
					g_curCraft->aiFlight.headingState = 1;
				else
					g_curCraft->aiFlight.headingState = 2;
				g_curCraft->aiFlight.climbState = 1;
				paiman_setpower(g_paiContext.objectIndex, 0xC000);
			}
		}

		paiman_setflighttotarget(0, 1);
		g_paiContext.controller->aiPlanState = SIMULATION_TICKS_PER_SECOND;
		if (g_curCraft->aiFlight.turnState == 3 && g_objectTable[g_paiContext.objectIndex].roll != 0) {
			g_curCraft->aiFlight.enterFlag = 1;
			g_curCraft->aiFlight.rollStep = UINT16_MAX;
			g_paiContext.controller->targetRoll = 0;
		}
	}

	throttleIndex =
		g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[g_paiContext.orderSlot].throttle;
	object = &g_objectTable[g_paiContext.objectIndex];
	if (object->genusId != 4) {
		paiman_setpower(g_paiContext.objectIndex, g_orderThrottleToCraftThrottleSpeed[throttleIndex]);
		return 0;
	}

	yawDifference = (uint16_t)(object->yaw - g_paiContext.controller->targetXYAngle);
	if (yawDifference >= 0x8000)
		yawDifference = (uint16_t)(0u - yawDifference);
	if (g_curCraft->aiFlight.turnState == 2 && yawDifference >= 0x1000) {
		paiman_setpower(g_paiContext.objectIndex, 0);
		return 0;
	}

	paiman_setpower(g_paiContext.objectIndex, g_orderThrottleToCraftThrottleSpeed[throttleIndex]);
	return 0;
}

// FUNCTION: XVT 0x49FE90
void paiman_AdvanceOrderWaypoint(int objectIndex) {
	uint8_t currentPlanId;
	uint8_t waypointIndex;
	(void)objectIndex;

	currentPlanId = g_paiContext.controller->currentPlanId;
	waypointIndex = ++g_paiContext.controller->waypointIndex;
	if (waypointIndex > 11 ||
		g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.missionPointEnabled[waypointIndex] ==
			0) {
		g_paiContext.controller->waypointIndex = 4;
		if (strcmp(g_planTable[currentPlanId].name, "formldr1pln") == 0 ||
			strcmp(g_planTable[currentPlanId].name, "formevadeldr1pln") == 0 ||
			strcmp(g_planTable[currentPlanId].name, "starshipformpln") == 0) {
			++g_paiContext.controller->orderScratch.goalProgress[g_paiContext.orderSlot];
		}
	}
	g_paiContext.controller->targetObjIdx = g_paiContext.controller->waypointIndex + 0x8000;
	g_paiContext.controller->targetSignature = 0;
	g_paiContext.controller->hasLiveTarget = 0;
	pai_UpdateAimPointFromOrderTarget();
}

// FUNCTION: XVT 0x49FF70
void paiman_initheadtowardfullmaneuver(void) {
	paiman_setflighttotarget(0, 1);
	paiman_setpower(g_paiContext.objectIndex, UINT16_MAX);
	g_paiContext.controller->aiPlanState = SIMULATION_TICKS_PER_SECOND;
}

// FUNCTION: XVT 0x49FFA0
int16_t paiman_headtowardfullmaneuver(void) {
	if (g_paiContext.controller->aiPlanState == 0) {
		pai_UpdateAimPointFromOrderTarget();
		paiman_setflighttotarget(0, 1);
		paiman_setpower(g_paiContext.objectIndex, UINT16_MAX);
		g_paiContext.controller->aiPlanState = SIMULATION_TICKS_PER_SECOND;
	}
	return 0;
}

// FUNCTION: XVT 0x49FFF0
void paiman_initrunawaymaneuver(void) {
	paiman_initcruiseandrunawaycontrols();
	if (g_objectTable[g_paiContext.objectIndex].roll < 0x8000) {
		paiman_setflighttotarget(0x8000, 1);
	}
}

// FUNCTION: XVT 0x4A0030
int16_t paiman_runawaymaneuver(void) {
	paiman_setflighttotarget(0x8000, 1);
	paiman_setpower(g_paiContext.objectIndex, UINT16_MAX);
	return 0;
}

// FUNCTION: XVT 0x4A0060
void paiman_initheadonattackmaneuver(void) {
	g_paiContext.controller->targetObjIdx = g_curCraft->lastAttackerObjIdx;
	if (g_curCraft->lastAttackerObjIdx != UINT16_MAX) {
		g_paiContext.controller->targetSignature =
			g_objectTable[g_curCraft->lastAttackerObjIdx].objectSignature;
	} else {
		g_paiContext.controller->targetSignature = 0;
	}
	g_paiContext.controller->hasLiveTarget = 1;
	paiman_setflighttotarget(0, 1);
	paiman_setpower(g_paiContext.objectIndex, UINT16_MAX);
	g_paiContext.controller->maneuverTimer = 1888;
}

// FUNCTION: XVT 0x4A00F0
int16_t paiman_headonattackmaneuver(void) {
	if (g_paiContext.controller->maneuverTimer == 0) {
		return 1;
	}
	paiman_setflighttotarget(0, 1);
	paiman_setpower(g_paiContext.objectIndex, UINT16_MAX);
	return 0;
}

// FUNCTION: XVT 0x4A0130
void nullsub_15(void) {}

// FUNCTION: XVT 0x4A0140
int16_t paiman_followleadermaneuver(void) {
	uint16_t leaderObjectIdx = (uint8_t)g_curCraft->leader_obj_idx;
	ObjectRecord* object;
	uint16_t effectiveSkillValue;
	int16_t yaw;
	uint16_t targetAngle;
	uint16_t angleDifference;
	uint16_t objectIndex;

	pai_ObjectRefDirectionToObjectRef((uint16_t)leaderObjectIdx, g_paiContext.objectIndex);
	object = &g_objectTable[g_paiContext.objectIndex];
	if ((object->genusId != CRAFT_GENUS_STARSHIP && trig2_polardistance > 0x10000) ||
		trig2_polardistance > 327680) {
		g_paiContext.controller->aimPointX = g_objectTable[leaderObjectIdx].world_x;
		g_paiContext.controller->aimPointY = g_objectTable[leaderObjectIdx].world_y;
		g_paiContext.controller->aimPointZ = g_objectTable[leaderObjectIdx].world_z;
		paiman_setflighttotarget(0, 1);
		paiman_setpower(g_paiContext.objectIndex, UINT16_MAX);
		g_curCraft->pushAccumX = 0;
		g_curCraft->pushAccumY = 0;
		g_curCraft->pushAccumZ = 0;
		return 0;
	}

	if (g_paiContext.targetCraft->aiFlight.turnState == 2) {
		g_paiContext.controller->targetXYAngle = g_paiContext.targetCraft->aiController.targetXYAngle;
		effectiveSkillValue = pai_GetEffectiveSkillValue(g_curCraft);
		paiman_setturn((effectiveSkillValue >> 3) + 0x4000);
	} else {
		yaw = g_objectTable[leaderObjectIdx].yaw;
		if (object->yaw != yaw) {
			g_paiContext.controller->targetXYAngle = yaw;
			effectiveSkillValue = pai_GetEffectiveSkillValue(g_curCraft);
			paiman_setturn((effectiveSkillValue >> 3) + 0x4000);
		}
	}

	{
		ObjectRecord* leader = &g_objectTable[leaderObjectIdx];
		if (leader->playerOwnerIdx != -1) {
			uint16_t speed;
			uint16_t objectSpeed;

			objectIndex = g_paiContext.objectIndex;
			speed = leader->mobj->speed;
			objectSpeed = g_objectTable[objectIndex].mobj->speed;
			if (speed > objectSpeed) {
				uint16_t throttleSpeed = g_curCraft->throttleSpeed;
				paiman_setpower(objectIndex, throttleSpeed + (uint16_t)(50 * (speed - objectSpeed)));
				if (g_curCraft->throttleSpeed < throttleSpeed)
					paiman_setpower(g_paiContext.objectIndex, UINT16_MAX);
			} else if (speed < objectSpeed) {
				uint16_t throttleSpeed = g_curCraft->throttleSpeed;
				paiman_setpower(objectIndex, throttleSpeed - (uint16_t)(50 * (objectSpeed - speed)));
				if (g_curCraft->throttleSpeed > throttleSpeed)
					paiman_setpower(g_paiContext.objectIndex, 0);
			}
		} else {
			paiman_setpower(g_paiContext.objectIndex, g_paiContext.targetCraft->throttleSpeed);
		}
	}

	{
		uint16_t* craftPitch = &g_curCraft->pitch;

		targetAngle = g_paiContext.targetCraft->pitch;
		angleDifference = *craftPitch - targetAngle;
		if (angleDifference >= 0x8000)
			angleDifference = (uint16_t)-angleDifference;
		if (angleDifference < 0x400) {
			*craftPitch = targetAngle;
			g_curCraft->aiFlight.headingState = 0;
		} else {
			g_paiContext.controller->targetZAngle = targetAngle;
			if (g_curCraft->pitch >= g_paiContext.controller->targetZAngle)
				g_curCraft->aiFlight.headingState = 1;
			else
				g_curCraft->aiFlight.headingState = 2;
			g_curCraft->aiFlight.headingStep = UINT16_MAX;
			g_curCraft->aiFlight.headingForce = 0;
		}
	}

	if (g_paiContext.targetCraft->aiFlight.impactObjIdx == UINT16_MAX) {
		uint16_t* objectRoll = &g_objectTable[g_paiContext.objectIndex].roll;

		targetAngle = g_objectTable[g_paiContext.leaderObjectIndex].roll;
		angleDifference = *objectRoll - targetAngle;
		if (angleDifference >= 0x8000)
			angleDifference = (uint16_t)-angleDifference;
		if (angleDifference < 0x400) {
			*objectRoll = targetAngle;
			g_objectTable[g_paiContext.objectIndex].mobj->orientMatrixDirty = 1;
			g_curCraft->aiFlight.enterFlag = 0;
		} else {
			g_paiContext.controller->targetRoll = targetAngle;
			g_curCraft->aiFlight.rollStep = UINT16_MAX;
			g_curCraft->aiFlight.enterFlag = 1;
		}
	}

	object = &g_objectTable[(uint8_t)g_curCraft->leader_obj_idx];
	if (object->playerOwnerIdx == -1) {
		paiman_calcformation();
	} else if (object->mobj->speed > 10) {
		paiman_calcformation();
	} else {
		g_curCraft->pushAccumX = 0;
		g_curCraft->pushAccumY = 0;
		g_curCraft->pushAccumZ = 0;
	}
	return 0;
}

// FUNCTION: XVT 0x4A0550
void paiman_initsetupattackmaneuver(void) {
	paiman_setpower(g_paiContext.objectIndex, UINT16_MAX);
	paiman_attacktarget(0);
}

// FUNCTION: XVT 0x4A0580
int16_t paiman_setupattackmaneuver(void) {
	uint16_t objectIndex;
	uint16_t yawDifference;

	paiman_attacktarget(0);
	objectIndex = g_paiContext.objectIndex;
	yawDifference = g_objectTable[objectIndex].yaw - g_paiContext.controller->targetXYAngle;
	if (yawDifference >= 0x3000 && yawDifference <= 0xD000) {
		paiman_setpower(objectIndex, 0x8000);
		return 0;
	}
	paiman_setpower(objectIndex, UINT16_MAX);
	return 0;
}

// FUNCTION: XVT 0x4A05F0
void paiman_initattackmaneuver(void) {
	paiman_setpower(g_paiContext.objectIndex, UINT16_MAX);
	paiman_attacktarget(0);
}

// FUNCTION: XVT 0x4A0620
int16_t paiman_attackmaneuver(void) {
	const int fullThrottle = UINT16_MAX;
	const int attackDistance = 0x4000;
	const int maxAngle = 0x4000;
	const int rearAngle = 0xD000;
	unsigned int targetDistance;
	int requiredDistance = 5120;
	uint16_t reactionThreshold;

	g_curCraft->pushAccumX = 0;
	g_curCraft->pushAccumY = g_curCraft->pushAccumX;
	g_curCraft->pushAccumZ = g_curCraft->pushAccumY;
	switch (g_paiContext.controller->maneuverPhase) {
		case 1:
			if (g_paiContext.controller->maneuverTimer == 0)
				return 1;
			break;
		case 0: {

			pai_ObjectRefDirectionToObjectRef(g_paiContext.objectIndex,
											  g_paiContext.controller->targetObjIdx);
			targetDistance = trig2_polardistance;
			if (g_activeRegionCraftObjectSlotEnd <= g_paiContext.controller->targetObjIdx) {
				requiredDistance = 0x1400;
			} else {
				ObjectRecord* target = &g_objectTable[g_paiContext.controller->targetObjIdx];
				if (target->genusId == CRAFT_GENUS_STARSHIP || target->genusId == CRAFT_GENUS_PLATFORM ||
					target->genusId == CRAFT_GENUS_FREIGHTER) {
					uint16_t yawDifference;

					requiredDistance = 0x2000;
					if (target->objectType == 54)
						requiredDistance = 0xA000;
					if (target->objectType == 91)
						requiredDistance += 0x8000;
					if (target->objectType == 90)
						requiredDistance += 0x8000;
					yawDifference = (uint16_t)(trig2_xyangle - target->yaw);
					if (yawDifference >= 0x8000)
						yawDifference = (uint16_t)-yawDifference;
					if (yawDifference < 0x2800 || yawDifference > 0x5800)
						requiredDistance *= 2;
				} else {
					uint8_t groupAi = g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.groupAI;
					ObjectRecord* own;
					uint16_t yawDifference;
					uint16_t pitchDifference;

					requiredDistance = groupAi == 5   ? 1536
									   : groupAi == 4 ? 2304
									   : groupAi == 3 ? 3072
									   : groupAi == 2 ? 4096
													  : 5120;
					own = &g_objectTable[g_paiContext.objectIndex];
					yawDifference = (uint16_t)(own->yaw - target->yaw);
					if (yawDifference >= 0x8000)
						yawDifference = (uint16_t)-yawDifference;
					pitchDifference = (uint16_t)(own->pitch - target->pitch);
					if (pitchDifference >= 0x8000)
						pitchDifference = (uint16_t)-pitchDifference;
					if (pitchDifference > maxAngle || yawDifference > maxAngle)
						requiredDistance *= 2;
				}
			}

			reactionThreshold = g_modelDefs[g_curCraft->modelIndex].reactionThreshold;
			if ((g_curCraft->systemFlags & CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				if (Craft_GetObjectMaxShield(g_paiContext.objectIndex) / 8 > g_curCraft->shieldEnergy[0])
					reactionThreshold >>= 1;
				if (g_curCraft->hullDamage >= g_curCraft->systemDamageHullThreshold)
					reactionThreshold = 1;
			}
			if (requiredDistance <= trig2_polardistance &&
				g_curCraft->aiFlight.reactionTimer < reactionThreshold) {
				paiman_attacktarget(0);
				if (g_paiContext.controller->maneuverMode == AI_MANEUVER_MODE_ATTACK) {
					uint16_t ownYaw = g_objectTable[g_paiContext.objectIndex].yaw;
					uint16_t yawDifference = (uint16_t)(ownYaw - g_paiContext.controller->targetXYAngle);

					if (yawDifference >= 0x3000 && yawDifference <= rearAngle) {
						paiman_setpower(g_paiContext.objectIndex, 43690);
					} else {
						uint16_t targetObjectIndex = g_paiContext.controller->targetObjIdx;

						if (targetDistance > 0x8000) {
							paiman_setpower(g_paiContext.objectIndex, fullThrottle);
						} else {
							ObjectRecord* target = &g_objectTable[targetObjectIndex];

							if (target->mobj == NULL) {
								paiman_setpower(g_paiContext.objectIndex, 0x8000);
							} else if (g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.groupAI >=
									   4) {
								uint16_t targetYawDifference = (uint16_t)(ownYaw - target->yaw);

								if (targetYawDifference >= 0x3000 && targetYawDifference <= rearAngle) {
									paiman_setpower(g_paiContext.objectIndex, fullThrottle);
								} else {
									uint16_t speed = target->mobj->speed;
									uint16_t maxSpeed;

									speed += targetDistance > attackDistance ? 20 : 0;
									maxSpeed = g_curCraft->aiFlight.maxSpeedCache;

									if (speed >= maxSpeed) {
										paiman_setpower(g_paiContext.objectIndex, fullThrottle);
									} else {
										uint16_t throttle = MATH2_divide(speed, maxSpeed);

										paiman_setpower(g_paiContext.objectIndex, throttle);
									}
								}
							} else {
								paiman_setpower(g_paiContext.objectIndex, fullThrottle);
							}
						}
					}
				} else {
					paiman_setpower(g_paiContext.objectIndex, 0xC000);
				}
				if (g_curCraft->waveNumber != 0) {
					CraftData* wingman;
					unsigned int objectIndex;
					int nearestWingman = -1;
					uint8_t nearestWave = UINT8_MAX;

					for (objectIndex = g_activeRegionObjectSlotStart;
						 (unsigned int)g_activeRegionCraftObjectSlotEnd > objectIndex; ++objectIndex) {
						if (g_paiContext.objectIndex != objectIndex &&
							g_objectTable[objectIndex].objectType != 0 &&
							g_objectTable[objectIndex].flightGroupIdx == g_paiContext.orderFlightGroupIndex) {
							wingman = g_objectTable[objectIndex].mobj->pCraft;
							if (g_curCraft->waveNumber > wingman->waveNumber) {
								pai_ObjectRefUpdateApproxRangeScore(g_paiContext.objectIndex, objectIndex);
								if (g_targetRangeScore < 1000 && nearestWave > wingman->waveNumber) {
									nearestWingman = objectIndex;
									nearestWave = wingman->waveNumber;
								}
							}
						}
					}
					if (nearestWingman != -1) {
						g_curCraft->pushAccumX = g_objectTable[g_paiContext.objectIndex].world_x -
												 g_objectTable[nearestWingman].world_x;
						g_curCraft->pushAccumY = g_objectTable[g_paiContext.objectIndex].world_y -
												 g_objectTable[nearestWingman].world_y;
						g_curCraft->pushAccumZ = 2 * (g_objectTable[g_paiContext.objectIndex].world_z -
													  g_objectTable[nearestWingman].world_z);
						return 0;
					}
				}
				break;
			}

			g_curCraft->aiFlight.maneuverCounter = 0;
			g_curCraft->warheadLockTicks = 0;
			if (g_curCraft->aiFlight.reactionTimer < reactionThreshold ||
				g_curCraft->aiFlight.threatObjIdx == UINT16_MAX ||
				g_objectTable[g_curCraft->aiFlight.threatObjIdx].genusId != 0) {
				int16_t yawOffset = (int16_t)((GameRand() & 0x3FFF) + 12288);
				if ((uint16_t)GameRand() >= 0x8000)
					yawOffset = (int16_t)-yawOffset;
				g_paiContext.controller->targetXYAngle =
					(uint16_t)(g_objectTable[g_paiContext.objectIndex].yaw + yawOffset);
				paiman_setturn((pai_GetEffectiveSkillValue(g_curCraft) >> 1) + 0x8000);
				g_paiContext.controller->targetZAngle = (uint16_t)(GameRand() & 0x7FFF);
				g_curCraft->aiFlight.climbState = 0;
				g_curCraft->aiFlight.diveState = 0;
				if (g_paiContext.controller->targetZAngle <= g_curCraft->pitch)
					g_curCraft->aiFlight.headingState = 1;
				else
					g_curCraft->aiFlight.headingState = 2;
				g_curCraft->aiFlight.headingForce = 0;
				g_curCraft->aiFlight.headingStep = UINT16_MAX;
				paiman_setpower(g_paiContext.objectIndex, fullThrottle);
				if (g_activeRegionCraftObjectSlotEnd <= g_paiContext.controller->targetObjIdx) {
					g_paiContext.controller->maneuverTimer =
						SIMULATION_TICKS_PER_SECOND * ((GameRand() & 3) + 2);
				} else if (g_objectTable[g_paiContext.controller->targetObjIdx].genusId ==
							   CRAFT_GENUS_STARSHIP ||
						   g_objectTable[g_paiContext.controller->targetObjIdx].genusId ==
							   CRAFT_GENUS_PLATFORM ||
						   g_objectTable[g_paiContext.controller->targetObjIdx].genusId ==
							   CRAFT_GENUS_FREIGHTER) {
					g_paiContext.controller->maneuverTimer =
						SIMULATION_TICKS_PER_SECOND * ((GameRand() & 7) + 20);
				} else {
					g_paiContext.controller->maneuverTimer =
						SIMULATION_TICKS_PER_SECOND * ((GameRand() & 3) + 2);
				}
				g_paiContext.controller->maneuverPhase = 1;
				return 0;
			}

			{
				ObjectRecord* threat = &g_objectTable[g_curCraft->aiFlight.threatObjIdx];
				uint16_t pitchDifference;
				uint16_t yawDifference;

				yawDifference = (uint16_t)(g_objectTable[g_paiContext.objectIndex].yaw - threat->yaw);
				if (yawDifference >= 0x8000)
					yawDifference = (uint16_t)-yawDifference;
				pitchDifference = (uint16_t)(g_objectTable[g_paiContext.objectIndex].pitch - threat->pitch);
				if (pitchDifference >= 0x8000)
					pitchDifference = (uint16_t)-pitchDifference;
				if (yawDifference >= 0x3000 || pitchDifference >= 0x3000) {
					g_paiContext.controller->maneuverMode =
						g_aiUnderAttackFrontManeuverChoices[GameRand() & 3];
				} else {
					g_paiContext.controller->maneuverMode =
						g_aiUnderAttackSideRearManeuverChoices[GameRand() & 7];
					if (g_curCraft->cmTypeId == COUNTERMEASURE_TYPE_FLARE && g_curCraft->cmAmmoCount != 0 &&
						g_curCraft->cmFireCooldownTimer == 0)
						laser_createcountermeasureprojectile(g_paiContext.objectIndex, 155);
				}
				g_curCraft->lastAttackerObjIdx = g_curCraft->aiFlight.threatObjIdx;
				paiman_initmaneuver();
				paiman_setpower(g_paiContext.objectIndex, fullThrottle);
			}
			return 0;
		}
	}
	return 0;
}

// FUNCTION: XVT 0x4A0E00
void paiman_initzoommaneuver(void) {
	uint16_t pitch;

	paiman_setpower(g_paiContext.objectIndex, 0xFFFF);
	g_curCraft->aiFlight.enterFlag = 3;
	g_curCraft->aiFlight.rollStep = 0xFFFFu;
	g_paiContext.controller->targetRoll = GameRand();
	g_paiContext.controller->targetZAngle = 0x2000 - (GameRand() & 0x0FFF);
	g_paiContext.controller->maneuverTimer = SIMULATION_TICKS_PER_SECOND * ((GameRand() & 3) + 3);
	g_curCraft->aiFlight.climbState = 0;
	g_curCraft->aiFlight.diveState = 0;
	pitch = g_curCraft->pitch;
	if (g_paiContext.controller->targetZAngle <= pitch) {
		g_curCraft->aiFlight.headingState = 1;
	} else {
		g_curCraft->aiFlight.headingState = 2;
	}
	g_curCraft->aiFlight.headingForce = 0;
	g_curCraft->aiFlight.headingStep = 0xFFFFu;
}

// FUNCTION: XVT 0x4A0EF0
int16_t paiman_zoommaneuver(void) { return g_paiContext.controller->maneuverTimer == 0; }

// FUNCTION: XVT 0x4A0F00
void paiman_initdivemaneuver(void) {
	paiman_setpower(g_paiContext.objectIndex, 0xFFFF);
	g_paiContext.controller->targetZAngle = (GameRand() & 0x0FFF) + 0x5800;
	g_curCraft->aiFlight.climbState = 0;
	if (g_paiContext.controller->targetZAngle <= g_curCraft->pitch) {
		g_curCraft->aiFlight.headingState = 1;
	} else {
		g_curCraft->aiFlight.headingState = 2;
	}
	g_curCraft->aiFlight.headingForce = 0;
	g_curCraft->aiFlight.headingStep = 0xFFFFu;
	g_paiContext.controller->maneuverTimer = 1180;
}

// FUNCTION: XVT 0x4A0F90
int16_t paiman_zoommaneuver_2(void) { return g_paiContext.controller->maneuverTimer == 0; }

// FUNCTION: XVT 0x4A0FA0
void paiman_initsplitsdivemaneuver(void) {
	g_curCraft->aiFlight.enterFlag = 1;
	g_curCraft->aiFlight.rollStep = 0xFFFFu;
	g_paiContext.controller->targetRoll = 0x8000u;
	g_curCraft->aiFlight.turnState = 0;
	g_curCraft->aiFlight.headingForce = 1;
	g_paiContext.controller->targetZAngle = (GameRand() & 0x3FFF) + 0x4000;
	g_curCraft->aiFlight.headingState = 2;
	g_curCraft->aiFlight.headingStep = (pai_GetEffectiveSkillValue(g_curCraft) >> 1) + 0x8000;
}

// FUNCTION: XVT 0x4A1030
int16_t paiman_splitsmaneuver_2(void) {
	return g_curCraft->aiFlight.enterFlag == 4 && g_curCraft->aiFlight.headingState == 3;
}

// FUNCTION: XVT 0x4A1060
void paiman_initspeedawaymaneuver(void) {
	uint16_t yaw;
	uint16_t randomOffset;

	paiman_setpower(g_paiContext.objectIndex, 0xFFFF);
	g_paiContext.controller->maneuverTimer = 4720;
	yaw = g_objectTable[g_paiContext.objectIndex].yaw;
	randomOffset = GameRand();
	randomOffset &= 0xFF;
	yaw += randomOffset;
	g_paiContext.controller->targetXYAngle = yaw;
	paiman_SetupSpeedAwayTurn(g_paiContext.objectIndex);
}

// FUNCTION: XVT 0x4A10D0
int16_t paiman_speedawaymaneuver(void) {
	if (g_paiContext.controller->aiPlanState == 0) {
		g_paiContext.controller->targetXYAngle = -g_paiContext.controller->targetXYAngle;
		paiman_SetupSpeedAwayTurn(g_paiContext.objectIndex);
	}
	return g_paiContext.controller->maneuverTimer == 0;
}

// FUNCTION: XVT 0x4A1110
void paiman_SetupSpeedAwayTurn(unsigned int objectIdx) {
	uint16_t pushZ;
	int16_t yawOffset;
	uint16_t effectiveSkill;
	unsigned int turnStep;

	pushZ = (GameRand() & 0x1F) + 50;
	yawOffset = GameRand() & 0xFF;
	yawOffset += 384;
	if (g_curCraft->pushAccumZ >= 0) {
		pushZ = -pushZ;
		yawOffset = -yawOffset;
	}
	g_curCraft->pushAccumZ = pushZ;
	g_paiContext.controller->targetXYAngle = g_objectTable[objectIdx].yaw + yawOffset;
	effectiveSkill = pai_GetEffectiveSkillValue(g_curCraft);
	turnStep = effectiveSkill >> 1;
	turnStep += 0x8000;
	paiman_setturn(turnStep);
	g_paiContext.controller->aiPlanState = 118;
}

// FUNCTION: XVT 0x4A11B0
void paiman_initintohyperspacemaneuver(void) {
	paiman_setflighttotarget(0, 1);
	paiman_setpower(g_paiContext.objectIndex, UINT16_MAX);
	g_paiContext.controller->maneuverPhase = 0;
}

// FUNCTION: XVT 0x4A11E0
int16_t paiman_intohyperspacemaneuver(void) {
	unsigned int team;
	unsigned int groupIndex;
	uint16_t carriedGroupIdx;
	uint16_t flightGroupIdx;
	uint8_t specialCargoCraft;
	uint8_t otherTeamCount;
	CraftData* carriedCraft;
	int specialCargo;

	switch (g_paiContext.controller->maneuverPhase) {
		case 0:
			paiman_setflighttotarget(0, 1);
			if (trig2_polardistance < 0x4000) {
				g_curCraft->objectKind = CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE;
				g_curCraft->aiFlight.enterFlag = 0;
				g_curCraft->aiFlight.headingState = 0;
				g_curCraft->aiFlight.turnState = 0;
				g_paiContext.controller->maneuverPhase = 1;
				g_paiContext.controller->aiPlanState = 944;
				g_paiContext.controller->maneuverTimer = 1652;
			}
			paiman_setpower(g_paiContext.objectIndex, 0xFFFF);
			return 0;
		case 1:
			break;
		default:
			return 0;
	}

	g_curCraft->objectKind = CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE;
	if (g_objectTable[g_paiContext.objectIndex].mobj->speed >= 0xE10) {
		if (g_curCraft->wasCaptured == 0 && g_paiContext.controller->orderStateFlag == 0 &&
			(g_curCraft->aiFlight.goHomeFlag != 0 ||
			 (g_curCraft->aiFlight.missionAbortedFlag == 0 && g_curCraft->aiFlight.departTimerFlag == 0))) {
			flightGroupIdx = g_paiContext.orderFlightGroupIndex;
			++g_missionFgStats[flightGroupIdx].outcomeCount[FLIGHT_GROUP_OUTCOME_DEPARTED];
			specialCargo = 0;
			if (g_missionFlightGroups[flightGroupIdx].fg.specialCargoCraft == g_curCraft->waveNumber) {
				specialCargo = 1;
				g_missionFgStats[flightGroupIdx].specialCargoOutcome[FLIGHT_GROUP_OUTCOME_DEPARTED] = 1;
			}
			Mission_ApplyTeamGoalScoreAllEnabledTeams(12, g_paiContext.orderFlightGroupIndex, specialCargo);
		}
		if (g_curCraft->wasCaptured == 0 && g_paiContext.controller->orderStateFlag == 1 &&
			g_curCraft->aiFlight.missionAbortedFlag == 0 && g_curCraft->aiFlight.departTimerFlag == 0) {
			flightGroupIdx = g_paiContext.orderFlightGroupIndex;
			++g_missionFgStats[flightGroupIdx]
				  .outcomeCount[FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE];
			if (g_missionFlightGroups[flightGroupIdx].fg.specialCargoCraft == g_curCraft->waveNumber)
				g_missionFgStats[flightGroupIdx]
					.specialCargoOutcome[FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE] = 1;
		}
		if (g_curCraft->wasCaptured != 0) {
			team = g_objectTable[g_paiContext.objectIndex].mobj->team;
			++g_missionFgStats[g_paiContext.orderFlightGroupIndex].teamCondition44Count[team];
			specialCargo = 0;
			if (g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.specialCargoCraft ==
				g_curCraft->waveNumber) {
				++g_missionFgStats[g_paiContext.orderFlightGroupIndex].teamCondition44SpecialCargo[team];
				specialCargo = 1;
			}
			Mission_ApplyTeamGoalScoreForTeam(44, g_paiContext.orderFlightGroupIndex, specialCargo,
											  (uint8_t)team);
			for (groupIndex = 0; groupIndex < 10; ++groupIndex) {
				if (groupIndex != team &&
					g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.team != groupIndex) {
					++g_missionFgStats[g_paiContext.orderFlightGroupIndex]
						  .teamCondition44OtherTeamCount[groupIndex];
					if (g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.specialCargoCraft ==
						g_curCraft->waveNumber)
						g_missionFgStats[g_paiContext.orderFlightGroupIndex]
							.teamCondition44OtherTeamSpecialCargo[groupIndex] = 1;
				}
			}
		}
		msg_emitCraftMessage(g_paiContext.objectIndex, g_curCraft, 0x87);
		Mission_RecordCraftOutcome(g_paiContext.objectIndex, g_paiContext.orderFlightGroupIndex,
								   FLIGHT_GROUP_OUTCOME_LEFT_REGION);
		g_objectTable[g_paiContext.objectIndex].objectType = 0;
		Craft_ClearEffectiveAiObjectLink(g_curCraft);
		{
			uint16_t carriedObjectIdx = g_curCraft->carriedObjectIndex;

			if (carriedObjectIdx != UINT16_MAX) {
				if (g_objectTable[carriedObjectIdx].mobj != NULL) {
					carriedGroupIdx = g_objectTable[carriedObjectIdx].flightGroupIdx;
					Mission_RecordCraftOutcome(carriedObjectIdx, carriedGroupIdx,
											   FLIGHT_GROUP_OUTCOME_LEFT_REGION);
					carriedCraft = g_objectTable[carriedObjectIdx].mobj->pCraft;
					if (carriedCraft->wasCaptured != 0) {
						team = g_objectTable[carriedObjectIdx].mobj->team;
						++g_missionFgStats[carriedGroupIdx].teamCondition44Count[team];
						specialCargo = 0;
						if (g_missionFlightGroups[carriedGroupIdx].fg.specialCargoCraft ==
							carriedCraft->waveNumber) {
							++g_missionFgStats[carriedGroupIdx].teamCondition44SpecialCargo[team];
							specialCargo = 1;
						}
						Mission_ApplyTeamGoalScoreForTeam(44, carriedGroupIdx, specialCargo, (uint8_t)team);
						for (groupIndex = 0; groupIndex < 10; ++groupIndex) {
							if (groupIndex != team &&
								g_missionFlightGroups[carriedGroupIdx].fg.team != groupIndex) {
								otherTeamCount = g_missionFgStats[carriedGroupIdx]
													 .teamCondition44OtherTeamCount[groupIndex];
								specialCargoCraft =
									g_missionFlightGroups[carriedGroupIdx].fg.specialCargoCraft;
								++otherTeamCount;
								g_missionFgStats[carriedGroupIdx].teamCondition44OtherTeamCount[groupIndex] =
									otherTeamCount;
								if (specialCargoCraft == carriedCraft->waveNumber)
									g_missionFgStats[carriedGroupIdx]
										.teamCondition44OtherTeamSpecialCargo[groupIndex] = 1;
							}
						}
						specialCargoCraft = g_missionFlightGroups[carriedGroupIdx].fg.specialCargoCraft;
						++g_missionFgStats[carriedGroupIdx]
							  .outcomeCount[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION];
						if (specialCargoCraft == carriedCraft->waveNumber)
							g_missionFgStats[carriedGroupIdx]
								.specialCargoOutcome[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION] = 1;
					} else {
						++g_missionFgStats[carriedGroupIdx]
							  .outcomeCount[FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION];
						specialCargo = 0;
						if (g_missionFlightGroups[carriedGroupIdx].fg.specialCargoCraft ==
							carriedCraft->waveNumber) {
							specialCargo = 1;
							g_missionFgStats[carriedGroupIdx]
								.specialCargoOutcome[FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION] = 1;
						}
						Mission_ApplyTeamGoalScoreAllEnabledTeams(46, carriedGroupIdx, specialCargo);
					}
					g_objectTable[carriedObjectIdx].objectType = 0;
					Craft_ClearEffectiveAiObjectLink(carriedCraft);
				}
			}
		}
	}
	return 0;
}

// FUNCTION: XVT 0x4A1750
void paiman_initoutofhyperspacemaneuver(void) {
	g_curCraft->objectKind = CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE;
	g_objectTable[g_paiContext.objectIndex].mobj->speed = 3600;
	g_paiContext.controller->maneuverPhase = 0;
	g_paiContext.controller->aiPlanState = SIMULATION_TICKS_PER_SECOND;
	g_paiContext.controller->targetObjIdx = 0x8000;
	g_paiContext.controller->targetSignature = 0;
	g_paiContext.controller->hasLiveTarget = 0;
	pai_UpdateAimPointFromOrderTarget();
	g_paiContext.controller->maneuverTimer = 2596;
	g_curCraft->aiFlight.objSignatures[0] = (uint16_t)g_paiContext.controller->thinkInterval;
	g_paiContext.controller->thinkInterval = 59;
}

// FUNCTION: XVT 0x4A17F0
int16_t paiman_outofhyperspacemaneuver(void) {
	AiController* targetController;
	uint8_t reached;
	uint16_t order;
	uint8_t planId;
	uint8_t* outOfHyperspacePlan;

	targetController = &g_paiContext.targetCraft->aiController;
	if (g_paiContext.controller->aiPlanState == 0) {
		++g_paiContext.controller->maneuverPhase;
		if (g_paiContext.controller->maneuverPhase > 10)
			g_paiContext.controller->maneuverPhase = 10;
		g_objectTable[g_paiContext.objectIndex].mobj->speed =
			g_aiCourseOrderSpeedByManeuverPhase[g_paiContext.controller->maneuverPhase];
		g_paiContext.controller->aiPlanState = SIMULATION_TICKS_PER_SECOND;
	}

	reached = 0;
	if (g_curCraft->leader_obj_idx == UINT8_MAX) {
		trig2_ctop(g_paiContext.controller->aimPointX - g_objectTable[g_paiContext.objectIndex].world_x,
				   g_paiContext.controller->aimPointY - g_objectTable[g_paiContext.objectIndex].world_y,
				   g_paiContext.controller->aimPointZ - g_objectTable[g_paiContext.objectIndex].world_z);
		if (trig2_polardistance < 4096 || g_paiContext.controller->maneuverTimer == 0)
			reached = 1;
	} else if (strcmp(g_planTable[targetController->pendingPlanId].name, "outofhyperspacepln") != 0) {
		reached = 1;
	}

	if (reached != 0) {
		order = g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[0].order;
		if (g_curCraft->leader_obj_idx == UINT8_MAX)
			planId = g_builtinPlanIdByNameIndex[g_orderLeaderBuiltinPlanNameIndex[order]];
		else
			planId = g_builtinPlanIdByNameIndex[g_orderFollowerBuiltinPlanNameIndex[order]];

		outOfHyperspacePlan = pai_getplandataptrbyname("outofhyperspacepln");
		outOfHyperspacePlan[3] = planId;
		g_paiContext.controller->thinkInterval = g_curCraft->aiFlight.objSignatures[0];
		g_curCraft->objectKind = CRAFT_OBJECT_KIND_ACTIVE;
		g_paiContext.controller->targetObjIdx = UINT16_MAX;
		g_paiContext.controller->targetSignature = 0;
		g_paiContext.controller->hasLiveTarget = 0;

		if (strcmp(g_planTable[planId].name, "nullpln") == 0 ||
			strcmp(g_planTable[planId].name, "stationaryldrpln") == 0 ||
			strcmp(g_planTable[planId].name, "stationaryflwpln") == 0) {
			g_objectTable[g_paiContext.objectIndex].mobj->speed = 0;
			return 1;
		}

		g_objectTable[g_paiContext.objectIndex].mobj->speed = 250;
		return 1;
	}

	return 0;
}

// FUNCTION: XVT 0x4A1A40
void nullsub_16(void) {}

// FUNCTION: XVT 0x4A1A50
int16_t paiman_escortmaneuver(void) {
	uint16_t escortTargetFG = g_paiContext.controller->escortTargetFG;
	uint16_t objectIndex = g_paiContext.objectIndex;
	uint16_t targetIdx;
	uint16_t objectIdx;
	CraftData* targetCraft;
	AiController* targetController;
	unsigned int escortRange;
	int variable1;

	targetIdx = UINT8_MAX;
	for (objectIdx = (uint16_t)g_activeRegionObjectSlotStart; objectIdx < g_activeRegionCraftObjectSlotEnd;
		 ++objectIdx) {
		if (g_objectTable[objectIdx].objectType != 0) {
			CraftData* candidateCraft = g_objectTable[objectIdx].mobj->pCraft;
			if (g_objectTable[objectIdx].flightGroupIdx == escortTargetFG &&
				candidateCraft->leader_obj_idx == UINT8_MAX) {
				targetIdx = objectIdx;
				break;
			}
		}
	}
	if (targetIdx != UINT8_MAX) {
		pai_ObjectRefDirectionToObjectRef(targetIdx, objectIndex);
		targetCraft = g_objectTable[targetIdx].mobj->pCraft;
		targetController = &targetCraft->aiController;
		escortRange =
			g_modelTypeTable[g_objectTable[targetIdx].objectType].maxBoundsExtent < 3000 ? 0x8000 : 0x20000;
		if ((unsigned int)trig2_polardistance > escortRange || targetCraft->workingSubsystems == 0) {
			g_paiContext.controller->aimPointX = g_objectTable[targetIdx].world_x;
			g_paiContext.controller->aimPointY = g_objectTable[targetIdx].world_y;
			g_paiContext.controller->aimPointZ = g_objectTable[targetIdx].world_z;
			if (targetCraft->workingSubsystems == 0)
				paiman_setflighttotarget(0x4000, 1);
			else
				paiman_setflighttotarget(0, 1);
			if (trig2_polardistance > 0x10000)
				paiman_setpower(objectIndex, UINT16_MAX);
			else
				paiman_setpower(objectIndex, 0x4000);
			g_curCraft->pushAccumX = 0;
			g_curCraft->pushAccumY = 0;
			g_curCraft->pushAccumZ = 0;
			return 0;
		}
		if (targetCraft->aiFlight.turnState == 2) {
			g_paiContext.controller->targetXYAngle = targetController->targetXYAngle;
			paiman_setturn((pai_GetEffectiveSkillValue(g_curCraft) >> 3) + 0x4000);
		} else if (g_objectTable[objectIndex].yaw != g_objectTable[targetIdx].yaw) {
			g_paiContext.controller->targetXYAngle = g_objectTable[targetIdx].yaw;
			paiman_setturn((pai_GetEffectiveSkillValue(g_curCraft) >> 3) + 0x4000);
		}
		if (g_objectTable[targetIdx].mobj->speed > g_objectTable[objectIndex].mobj->speed) {
			uint16_t throttle = g_curCraft->throttleSpeed;
			paiman_setpower(objectIndex,
							throttle + (uint16_t)(50 * (g_objectTable[targetIdx].mobj->speed -
														g_objectTable[objectIndex].mobj->speed)));
			if (g_curCraft->throttleSpeed < throttle)
				paiman_setpower(objectIndex, UINT16_MAX);
		} else if (g_objectTable[targetIdx].mobj->speed < g_objectTable[objectIndex].mobj->speed) {
			uint16_t throttle = g_curCraft->throttleSpeed;
			paiman_setpower(objectIndex, throttle - (uint16_t)(50 * (g_objectTable[objectIndex].mobj->speed -
																	 g_objectTable[targetIdx].mobj->speed)));
			if (g_curCraft->throttleSpeed > throttle)
				paiman_setpower(objectIndex, 0);
		}
		{
			uint16_t pitch = targetCraft->pitch;
			uint16_t pitchDifference = g_curCraft->pitch - pitch;
			if (pitchDifference >= 0x8000u)
				pitchDifference = -pitchDifference;
			if (pitchDifference < 0x400u) {
				g_curCraft->pitch = pitch;
				g_curCraft->aiFlight.headingState = 0;
			} else {
				g_paiContext.controller->targetZAngle = pitch;
				if (g_curCraft->pitch >= g_paiContext.controller->targetZAngle)
					g_curCraft->aiFlight.headingState = 1;
				else
					g_curCraft->aiFlight.headingState = 2;
				g_curCraft->aiFlight.headingStep = UINT16_MAX;
				g_curCraft->aiFlight.headingForce = 0;
			}
		}
		{
			uint16_t roll = g_objectTable[targetIdx].roll;
			uint16_t rollDifference = g_objectTable[objectIndex].roll - roll;
			if (rollDifference >= 0x8000u)
				rollDifference = -rollDifference;
			if (rollDifference < 0x400u) {
				g_objectTable[objectIndex].roll = roll;
				g_objectTable[objectIndex].mobj->orientMatrixDirty = 1;
				g_curCraft->aiFlight.enterFlag = 0;
			} else {
				g_paiContext.controller->targetRoll = roll;
				g_curCraft->aiFlight.rollStep = UINT16_MAX;
				g_curCraft->aiFlight.enterFlag = 1;
			}
		}
		variable1 = g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
						.fg.orders[g_paiContext.orderSlot]
						.variable1;
		pai_calcrotatedpoint(&g_objectTable[targetIdx], g_aiCourseOrderLocalOffsetXByVar[variable1],
							 g_aiCourseOrderLocalOffsetYByVar[variable1],
							 g_aiCourseOrderLocalOffsetZByVar[variable1]);
		if (g_modelTypeTable[g_objectTable[targetIdx].objectType].maxBoundsExtent >= 3000) {
			g_rotatedX *= 16;
			g_rotatedY *= 16;
			g_rotatedZ *= 16;
		}
		{
			uint16_t leaderIdx = (uint8_t)targetCraft->leader_obj_idx;
			if (g_objectTable[leaderIdx].playerOwnerIdx == -1 || g_objectTable[leaderIdx].mobj->speed > 10) {
				g_curCraft->pushAccumX = g_objectTable[targetIdx].world_x -
										 g_objectTable[g_paiContext.objectIndex].world_x + g_rotatedX;
				g_curCraft->pushAccumY = g_objectTable[targetIdx].world_y -
										 g_objectTable[g_paiContext.objectIndex].world_y + g_rotatedY;
				g_curCraft->pushAccumZ = g_objectTable[targetIdx].world_z -
										 g_objectTable[g_paiContext.objectIndex].world_z + g_rotatedZ;
			} else {
				g_curCraft->pushAccumX = 0;
				g_curCraft->pushAccumY = 0;
				g_curCraft->pushAccumZ = 0;
			}
		}
	} else {
		paiman_setflighttotarget(0, 1);
		paiman_setpower(g_paiContext.objectIndex, 0x8000);
	}
	return 0;
}

// FUNCTION: XVT 0x4A2010
void paiman_initboardmaneuver(void) { g_paiContext.controller->maneuverPhase = 0; }

// FUNCTION: XVT 0x4A2020
int16_t paiman_boardmaneuver(void) {
	enum {
		BOARD_PHASE_APPROACH = 0,
		BOARD_PHASE_ALIGN = 1,
		BOARD_PHASE_TRANSFER = 2,
		BOARD_PHASE_SEPARATE = 3,
		DOCKING_SMALL_CRAFT_GENUS_LIMIT = 2,
		DOCKING_UP_FALLBACK = 0x7000,
		MISSION_POINT_APPROACH_Z_OFFSET = 2048,
		MISSION_POINT_DOCK_Z_OFFSET = 128,
		APPROACH_FULL_POWER_DISTANCE = 0x4000,
		APPROACH_HALF_POWER_DISTANCE = 0x2000,
		APPROACH_DOCK_DISTANCE = 2048,
		THROTTLE_QUARTER = 0x4000,
		THROTTLE_HALF = 0x8000,
		READY_MESSAGE_SLOT_COUNT = 10,
		ALIGNMENT_STEP = 0x8000,
		DOCKING_ALIGNMENT_DISTANCE = 16,
		DOCKING_DURATION_PER_ORDER_UNIT = 1180,
		RELOAD_PLAN_STATE = 472,
		RELOAD_CONTINUE_DURATION = 1416,
		SEPARATION_DURATION = 2360,
		SEPARATION_MISSION_POINT_PUSH = 500,
		MINIMUM_VOICE_ORDER_TIME = 2,
		SPECIAL_CARGO_NAME_LENGTH = 16,
		IFF_VISIBILITY_COUNT = 10,
		HUD_FEATURE_COUNT = 13,
		MAX_OBJECT_SIGNATURE_COUNT = 10,
		WARHEAD_LAUNCHER_SECONDARY = 1,
		SECONDARY_WARHEAD_TYPE = 5,
		MINIMUM_WARHEAD_COUNT = 1,
		MAXIMUM_WARHEAD_COUNT = 99,
		FULL_WEAPON_CHARGE = 127,
		SYSTEM_HEALTH_FULL = 100,
		CAPTURE_OWNER_FLAG = 0x80,
		SELF_DESTRUCT_RANDOM_MASK = 0x0F,
		SELF_DESTRUCT_RANDOM_BASE = 15,
		HUD_TARGET_INVALIDATED = -3,
	};

	uint16_t targetObjectIndex = g_paiContext.controller->targetObjIdx;
	uint16_t variable1 =
		g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[g_paiContext.orderSlot].variable1;
	MobileObject* targetMobileObject = g_objectTable[targetObjectIndex].mobj;
	uint8_t* targetFlightGroupIndexPtr = &g_objectTable[targetObjectIndex].flightGroupIdx;
	CraftData* targetCraft = NULL;
	AiController* targetController = NULL;
	uint16_t targetModelIndex = UINT8_MAX;
	uint16_t targetFlightGroupIndex = *targetFlightGroupIndexPtr;
	uint16_t targetSignature = g_objectTable[targetObjectIndex].objectSignature;

	if (targetMobileObject != NULL) {
		targetCraft = targetMobileObject->pCraft;
		targetController = &targetCraft->aiController;
		targetModelIndex = targetCraft->modelIndex;
	}

	switch (g_paiContext.controller->maneuverPhase) {
		case BOARD_PHASE_APPROACH: {
			if (targetMobileObject != NULL) {
				int16_t initialUpOffset;
				int16_t additionalUpOffset;
				int16_t approachUpOffset;

				if (g_objectTable[targetObjectIndex].genusId < DOCKING_SMALL_CRAFT_GENUS_LIMIT) {
					initialUpOffset = g_modelDefs[targetModelIndex].dockFromUp[0] -
									  g_modelDefs[g_curCraft->modelIndex].dockToUp[0];
				} else if (g_objectTable[g_paiContext.objectIndex].genusId <
						   DOCKING_SMALL_CRAFT_GENUS_LIMIT) {
					initialUpOffset = g_modelDefs[targetModelIndex].dockFromUp[1] +
									  g_modelDefs[targetModelIndex].dockFromUp[0] -
									  g_modelDefs[g_curCraft->modelIndex].dockToUp[1];
				} else {
					initialUpOffset = 2 * g_modelDefs[targetModelIndex].dockFromUp[1] -
									  g_modelDefs[g_curCraft->modelIndex].dockToUp[1];
				}
				additionalUpOffset = g_modelDefs[targetModelIndex].dockFromUp[1] -
									 g_modelDefs[g_curCraft->modelIndex].dockToUp[1];
				approachUpOffset = additionalUpOffset + additionalUpOffset + initialUpOffset;
				if (approachUpOffset < 0)
					approachUpOffset = DOCKING_UP_FALLBACK;
				pai_calcrotatedpoint(&g_objectTable[targetObjectIndex], 0, approachUpOffset,
									 g_modelDefs[targetModelIndex].dockForward);
				g_paiContext.controller->aimPointX = g_rotatedX + g_objectTable[targetObjectIndex].world_x;
				g_paiContext.controller->aimPointY = g_rotatedY + g_objectTable[targetObjectIndex].world_y;
				g_paiContext.controller->aimPointZ = g_rotatedZ + g_objectTable[targetObjectIndex].world_z;
			} else {
				Mission_ResolveObjectOrMissionPointWorldLoc(targetObjectIndex, 0);
				g_paiContext.controller->aimPointX = worldlocx;
				g_paiContext.controller->aimPointY = worldlocy;
				g_paiContext.controller->aimPointZ = worldlocz + MISSION_POINT_APPROACH_Z_OFFSET;
			}

			paiman_setflighttotarget(0, 1);
			if (trig2_polardistance > APPROACH_FULL_POWER_DISTANCE)
				paiman_setpower(g_paiContext.objectIndex, UINT16_MAX);
			if (trig2_polardistance > APPROACH_HALF_POWER_DISTANCE) {
				paiman_setpower(g_paiContext.objectIndex, THROTTLE_HALF);
				return 0;
			}
			if (trig2_polardistance > APPROACH_DOCK_DISTANCE) {
				paiman_setpower(g_paiContext.objectIndex, THROTTLE_QUARTER);
				return 0;
			}
			{
				int targetPlayerIndex = g_objectTable[targetObjectIndex].playerOwnerIdx;

				if (targetPlayerIndex != -1 && g_objectTable[targetObjectIndex].mobj != NULL &&
					g_objectTable[targetObjectIndex].mobj->speed != 0) {
					unsigned int messageSlot;

					if (targetPlayerIndex != g_localPlayer)
						return 0;
					for (messageSlot = 0; messageSlot < READY_MESSAGE_SLOT_COUNT; ++messageSlot) {
						if (g_readyMessagePaneQueue[messageSlot].stateOrMessageId ==
							IFMSG_260_SET_YOUR_THROTTLE_TO_0_SO_RELOAD_CRAFT_CAN_DOCK_WITH_YOU)
							break;
					}
					if (messageSlot < READY_MESSAGE_SLOT_COUNT)
						return 0;
					g_msgSenderIff = g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.iff;
					msg_emitInFlightMessage(
						IFMSG_260_SET_YOUR_THROTTLE_TO_0_SO_RELOAD_CRAFT_CAN_DOCK_WITH_YOU, g_localPlayer);
					fsfx_PlaySound(FLIGHT_SOUND_GENERAL_WARNING, -1, g_localPlayer);
					return 0;
				}
			}
			paiman_setpower(g_paiContext.objectIndex, 0);
			g_paiContext.controller->maneuverPhase = BOARD_PHASE_ALIGN;
			return 0;
		}

		case BOARD_PHASE_ALIGN: {
			int pushX;
			int pushY;
			int pushZ;
			uint16_t targetRoll;
			uint16_t targetYaw;
			uint16_t targetPitch;

			if (targetMobileObject != NULL) {
				int16_t dockingUpOffset;

				if (g_objectTable[targetObjectIndex].genusId < DOCKING_SMALL_CRAFT_GENUS_LIMIT) {
					dockingUpOffset = g_modelDefs[targetModelIndex].dockFromUp[0] -
									  g_modelDefs[g_curCraft->modelIndex].dockToUp[0];
				} else if (g_objectTable[g_paiContext.objectIndex].genusId <
						   DOCKING_SMALL_CRAFT_GENUS_LIMIT) {
					dockingUpOffset = g_modelDefs[targetModelIndex].dockFromUp[0] -
									  g_modelDefs[g_curCraft->modelIndex].dockToUp[1];
				} else {
					dockingUpOffset = g_modelDefs[targetModelIndex].dockFromUp[1] -
									  g_modelDefs[g_curCraft->modelIndex].dockToUp[1];
				}
				pai_calcrotatedpoint(&g_objectTable[targetObjectIndex], 0, dockingUpOffset,
									 g_modelDefs[targetModelIndex].dockForward);
				g_curCraft->pushAccumX = g_rotatedX + g_objectTable[targetObjectIndex].world_x -
										 g_objectTable[g_paiContext.objectIndex].world_x;
				pushX = g_curCraft->pushAccumX;
				g_curCraft->pushAccumY = g_rotatedY + g_objectTable[targetObjectIndex].world_y -
										 g_objectTable[g_paiContext.objectIndex].world_y;
				pushY = g_curCraft->pushAccumY;
				g_curCraft->pushAccumZ = g_rotatedZ + g_objectTable[targetObjectIndex].world_z -
										 g_objectTable[g_paiContext.objectIndex].world_z;
				pushZ = g_curCraft->pushAccumZ;
				targetRoll = g_objectTable[targetObjectIndex].roll;
				targetYaw = g_objectTable[targetObjectIndex].yaw;
				targetPitch = g_objectTable[targetObjectIndex].pitch;
			} else {
				Mission_ResolveObjectOrMissionPointWorldLoc(targetObjectIndex, 0);
				g_curCraft->pushAccumX = worldlocx - g_objectTable[g_paiContext.objectIndex].world_x;
				pushX = g_curCraft->pushAccumX;
				g_curCraft->pushAccumY = worldlocy - g_objectTable[g_paiContext.objectIndex].world_y;
				pushY = g_curCraft->pushAccumY;
				g_curCraft->pushAccumZ =
					worldlocz - g_objectTable[g_paiContext.objectIndex].world_z + MISSION_POINT_DOCK_Z_OFFSET;
				pushZ = g_curCraft->pushAccumZ;
				targetRoll = 0;
				targetYaw = 0;
				targetPitch = 0x4000;
			}
			if (g_objectTable[g_paiContext.objectIndex].roll != targetRoll) {
				g_curCraft->aiFlight.enterFlag = 1;
				g_curCraft->aiFlight.rollStep = ALIGNMENT_STEP;
				g_paiContext.controller->targetRoll = targetRoll;
			}
			if (g_objectTable[g_paiContext.objectIndex].yaw != targetYaw) {
				g_curCraft->aiFlight.turnState = 2;
				g_curCraft->aiFlight.turnStep = (int16_t)ALIGNMENT_STEP;
				g_paiContext.controller->targetXYAngle = targetYaw;
			}
			if (g_objectTable[g_paiContext.objectIndex].pitch != g_objectTable[targetObjectIndex].pitch) {
				g_paiContext.controller->targetZAngle = targetPitch;
				g_curCraft->aiFlight.headingStep = ALIGNMENT_STEP;
				g_curCraft->aiFlight.headingForce = 0;
				g_curCraft->aiFlight.headingState =
					g_paiContext.controller->targetZAngle > g_curCraft->pitch ? 2 : 1;
			}
			if (pushX < 0)
				pushX = -pushX;
			if (pushY < 0)
				pushY = -pushY;
			if (pushZ < 0)
				pushZ = -pushZ;
			if (pushX + pushY + pushZ >= DOCKING_ALIGNMENT_DISTANCE)
				return 0;

			g_curCraft->pushAccumX = 0;
			g_curCraft->pushAccumY = 0;
			g_curCraft->pushAccumZ = 0;
			g_paiContext.controller->maneuverPhase = BOARD_PHASE_TRANSFER;
			g_paiContext.controller->maneuverTimer = DOCKING_DURATION_PER_ORDER_UNIT * variable1;
			g_paiContext.controller->aiPlanState = SIMULATION_TICKS_PER_SECOND;
			if (g_objectTable[g_paiContext.objectIndex].mobj != NULL &&
				g_curCraft->aiFlight.orderActionFlag == 0) {
				uint16_t flightGroupIndex = g_paiContext.orderFlightGroupIndex;

				g_curCraft->aiFlight.orderActionFlag = 1;
				++g_missionFgStats[flightGroupIndex].outcomeCount[FLIGHT_GROUP_OUTCOME_FAILED_MISSION];
				if (g_missionFlightGroups[flightGroupIndex].fg.specialCargoCraft == g_curCraft->waveNumber)
					g_missionFgStats[flightGroupIndex]
						.specialCargoOutcome[FLIGHT_GROUP_OUTCOME_FAILED_MISSION] = 1;
			}
			if (targetMobileObject != NULL && targetCraft->aiFlight.boardedAccountingDone == 0) {
				targetCraft->aiFlight.boardedAccountingDone = 1;
				++g_missionFgStats[targetFlightGroupIndex]
					  .outcomeCount[FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION];
				if (g_missionFlightGroups[targetFlightGroupIndex].fg.specialCargoCraft ==
					targetCraft->waveNumber)
					g_missionFgStats[targetFlightGroupIndex]
						.specialCargoOutcome[FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION] = 1;
			}
			msg_formatObjectName(g_paiContext.objectIndex, 1, g_flightTextScratchBuffer);
			msg_addMessagePtr(0, g_flightTextScratchBuffer);
			msg_formatObjectName(targetObjectIndex, 1, g_flightSecondaryObjectNameBuffer);
			msg_addMessagePtr(1, g_flightSecondaryObjectNameBuffer);
			g_msgSenderIff = (uint8_t)g_objectTable[g_paiContext.objectIndex].mobj->iff;
			msg_emitInFlightMessage(IFMSG_234_ARG_HAS_DOCKED_WITH_ARG, g_localPlayer);
			if (variable1 >= MINIMUM_VOICE_ORDER_TIME) {
				const char* planName = g_planTable[g_paiContext.controller->currentPlanId].name;

				if (strcmp(planName, "boardtogivepln") == 0 || strcmp(planName, "boardtoexchangepln") == 0 ||
					strcmp(planName, "boardtocontactpln") == 0 || strcmp(planName, "boardtopickuppln") == 0 ||
					strcmp(planName, "boardtorepairpln") == 0) {
					fsfx_SpeakTacticalOfficerEvent(TACTICAL_VOICE_STATUS,
												   TACTICAL_MSG_BOARDING_STARTED_FRIENDLY,
												   g_paiContext.objectIndex, UINT16_MAX);
				} else if (strcmp(planName, "boardtocapturepln") == 0 ||
						   strcmp(planName, "boardtotakepln") == 0 ||
						   strcmp(planName, "boardtodestroypln") == 0) {
					fsfx_SpeakTacticalOfficerEvent(TACTICAL_VOICE_STATUS,
												   TACTICAL_MSG_BOARDING_STARTED_HOSTILE,
												   g_paiContext.objectIndex, UINT16_MAX);
				}
			}
			fsfx_SpeakTacticalOfficerEvent(TACTICAL_VOICE_STATUS, TACTICAL_MSG_BOARDING_STARTED_TARGET,
										   targetObjectIndex, UINT16_MAX);
			return 0;
		}

		case BOARD_PHASE_TRANSFER: {
			const char* planName = g_planTable[g_paiContext.controller->currentPlanId].name;

			if (g_paiContext.controller->maneuverTimer != 0) {
				uint16_t reloadedOrRepaired = 0;
				uint16_t launcherIndex;

				if (strcmp(planName, "boardtogivepln") != 0 ||
					g_objectTable[targetObjectIndex].playerOwnerIdx == -1 ||
					g_paiContext.controller->aiPlanState != 0 || targetCraft == NULL)
					return 0;
				for (launcherIndex = 0; launcherIndex < targetCraft->warheadLauncherCount; ++launcherIndex) {
					uint16_t weaponSlotIndex;
					uint16_t lastWeaponSlot;

					if (targetCraft->warheadSlotTypeIds[launcherIndex] == 0)
						continue;
					lastWeaponSlot = g_modelDefs[targetModelIndex].warheadLauncherLastSlot[launcherIndex];
					for (weaponSlotIndex =
							 g_modelDefs[targetModelIndex].warheadLauncherFirstSlot[launcherIndex];
						 weaponSlotIndex <= lastWeaponSlot; ++weaponSlotIndex) {
						uint16_t warhead =
							g_missionFlightGroups[g_objectTable[targetObjectIndex].flightGroupIdx].fg.warhead;
						uint16_t desiredCount;
						uint8_t flightGroupStatus;

						if (launcherIndex == WARHEAD_LAUNCHER_SECONDARY)
							warhead = SECONDARY_WARHEAD_TYPE;
						desiredCount =
							MATH2_fraction(g_modelDefs[targetModelIndex].warheadLauncherValue[launcherIndex],
										   g_warheadAmmoCounts[warhead]);
						if (desiredCount == 0)
							desiredCount = MINIMUM_WARHEAD_COUNT;
						flightGroupStatus =
							g_missionFlightGroups[g_objectTable[targetObjectIndex].flightGroupIdx].fg.status1;
						if (flightGroupStatus == 1)
							desiredCount *= 2;
						else if (flightGroupStatus == 2)
							desiredCount >>= 1;
						if (desiredCount == 0)
							desiredCount = MINIMUM_WARHEAD_COUNT;
						if (desiredCount > MAXIMUM_WARHEAD_COUNT)
							desiredCount = MAXIMUM_WARHEAD_COUNT;
						if (targetCraft->weaponSlots[weaponSlotIndex].count < desiredCount) {
							reloadedOrRepaired = 1;
							++targetCraft->weaponSlots[weaponSlotIndex].count;
						}
						targetCraft->weaponSlots[weaponSlotIndex].laserCharge = FULL_WEAPON_CHARGE;
					}
				}
				if (targetCraft->cmTypeId != COUNTERMEASURE_TYPE_NONE)
					targetCraft->cmAmmoCount = g_modelDefs[targetModelIndex].countermeasureCount;
				{
					uint16_t subsystemMask = CRAFT_SUBSYSTEM_FLAG_SHIELDS;
					uint16_t systemIndex;

					for (systemIndex = 0; systemIndex < CRAFT_SUBSYSTEM_COUNT; ++systemIndex) {
						if ((targetCraft->systemFlags & subsystemMask) != 0 &&
							(targetCraft->workingSubsystems & subsystemMask) == 0) {
							reloadedOrRepaired = 1;
							targetCraft->workingSubsystems |= subsystemMask;
							break;
						}
						subsystemMask *= 2;
					}
				}
				g_paiContext.controller->aiPlanState = RELOAD_PLAN_STATE;
				if (reloadedOrRepaired != 0)
					g_paiContext.controller->maneuverTimer = RELOAD_CONTINUE_DURATION;
				return 0;
			}

			if (strcmp(planName, "boardtogivepln") == 0) {
				uint16_t cargoIndex;

				if (targetMobileObject != NULL) {
					for (cargoIndex = 0; cargoIndex < sizeof(targetCraft->specialCargoName); ++cargoIndex)
						targetCraft->specialCargoName[cargoIndex] = g_curCraft->specialCargoName[cargoIndex];
					targetCraft->boardingState = 2;
				}
				if (g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
						.fg.orders[g_paiContext.controller->currentOrderSlot]
						.variable2 <= g_paiContext.controller->orderScratch
											  .goalProgress[g_paiContext.controller->currentOrderSlot] +
										  1)
					g_curCraft->specialCargoName[0] = 0;
				g_curCraft->boardingState = 1;
				if (g_objectTable[targetObjectIndex].playerOwnerIdx != -1 && targetCraft != NULL) {
					if (g_objectTable[targetObjectIndex].playerOwnerIdx == g_localPlayer) {
						int playerObjectIndex = g_players[g_localPlayer].objectIndex;
						int canRestoreHud = 0;

						if (playerObjectIndex != -1) {
							uint8_t playerObjectType = g_objectTable[playerObjectIndex].objectType;

							canRestoreHud = playerObjectType == CRAFT_SPECIES_X_WING ||
											playerObjectType == CRAFT_SPECIES_Y_WING ||
											playerObjectType == CRAFT_SPECIES_A_WING ||
											playerObjectType == CRAFT_SPECIES_Z_95_HEADHUNTER ||
											playerObjectType == CRAFT_SPECIES_B_WING;
						}
						if (canRestoreHud) {
							uint16_t featureMask = 1;
							int featureIndex;

							for (featureIndex = 0; featureIndex < HUD_FEATURE_COUNT; ++featureIndex) {
								if ((targetCraft->damageStats.installedHudFeatureMask & featureMask) != 0 &&
									(targetCraft->damageStats.activeHudFeatureMask & featureMask) == 0)
									targetCraft->damageStats.activeHudFeatureMask |= featureMask;
								featureMask *= 2;
							}
							FlightSurface_Lock();
							Hud_RebuildDisplayForViewState(g_players[g_localPlayer].viewState.hudStateLive,
														   g_localPlayer);
							FlightSurface_Unlock();
						}
					}
					{
						uint16_t systemIndex;

						for (systemIndex = 0; systemIndex < CRAFT_SUBSYSTEM_COUNT; ++systemIndex) {
							targetCraft->systemDisplaySlotBySystem[systemIndex] = (uint8_t)systemIndex;
							targetCraft->systemHealth[systemIndex] = SYSTEM_HEALTH_FULL;
							targetCraft->systemTimer[systemIndex] = 0;
						}
					}
				}
				msg_emitCraftMessage(g_paiContext.objectIndex, g_curCraft,
									 IFMSG_156_REPORTS_DOCKING_OPERATION_COMPLETE);
				fsfx_SpeakTacticalOfficerEvent(TACTICAL_VOICE_STATUS, TACTICAL_MSG_TRANSFER_COMPLETE,
											   g_paiContext.objectIndex, UINT16_MAX);
			} else if (strcmp(planName, "boardtotakepln") == 0) {
				uint16_t cargoIndex;

				if (targetMobileObject != NULL) {
					for (cargoIndex = 0; cargoIndex < sizeof(targetCraft->specialCargoName); ++cargoIndex)
						g_curCraft->specialCargoName[cargoIndex] = targetCraft->specialCargoName[cargoIndex];
					targetCraft->specialCargoName[0] = 0;
					targetCraft->boardingState = 1;
				}
				g_curCraft->boardingState = 2;
				msg_emitCraftMessage(g_paiContext.objectIndex, g_curCraft,
									 IFMSG_156_REPORTS_DOCKING_OPERATION_COMPLETE);
				fsfx_SpeakTacticalOfficerEvent(TACTICAL_VOICE_STATUS, TACTICAL_MSG_BOARDING_COMPLETE,
											   g_paiContext.objectIndex, UINT16_MAX);
			} else if (strcmp(planName, "boardtoexchangepln") == 0) {
				uint16_t cargoIndex;

				if (targetMobileObject != NULL) {
					for (cargoIndex = 0; cargoIndex < sizeof(targetCraft->specialCargoName); ++cargoIndex) {
						char cargoByte = g_curCraft->specialCargoName[cargoIndex];

						g_curCraft->specialCargoName[cargoIndex] = targetCraft->specialCargoName[cargoIndex];
						targetCraft->specialCargoName[cargoIndex] = cargoByte;
					}
					targetCraft->boardingState = 2;
				}
				g_curCraft->boardingState = 2;
				msg_emitCraftMessage(g_paiContext.objectIndex, g_curCraft,
									 IFMSG_156_REPORTS_DOCKING_OPERATION_COMPLETE);
			} else if (strcmp(planName, "boardtocapturepln") == 0) {
				if (targetMobileObject != NULL) {
					PaiContext savedContext;
					CraftData* savedCraft;

					paiman_TransferObjectToAiTeam(targetObjectIndex, targetCraft, CAPTURE_OWNER_FLAG);
					if (targetCraft->wasCaptured != 0) {
						if (targetCraft->aiFlight.maxSpeedCache != 0)
							targetController->pendingPlanId = pai_findplanbyname("flyhomeevadepln");
						else
							targetController->pendingPlanId = pai_findplanbyname("stationaryldrpln");
						msg_emitCraftMessage(targetObjectIndex, targetCraft, IFMSG_139_HAS_BEEN_CAPTURED);
					} else {
						int order = g_missionFlightGroups[targetFlightGroupIndex].fg.orders[0].order;
						uint8_t planNameIndex = targetCraft->leader_obj_idx == UINT8_MAX
													? g_orderLeaderBuiltinPlanNameIndex[order]
													: g_orderFollowerBuiltinPlanNameIndex[order];

						targetController->pendingPlanId = g_builtinPlanIdByNameIndex[planNameIndex];
					}
					fsfx_SpeakTacticalOfficerEvent(TACTICAL_VOICE_STATUS, TACTICAL_MSG_BOARDING_COMPLETE,
												   g_paiContext.objectIndex, UINT16_MAX);
					fsfx_SpeakTacticalOfficerEvent(TACTICAL_VOICE_STATUS, TACTICAL_MSG_CAPTURED_TARGET,
												   targetObjectIndex, UINT16_MAX);
					savedContext = g_paiContext;
					savedCraft = g_curCraft;
					g_curCraft = targetCraft;
					pai_setupcraftcontext(targetObjectIndex);
					pai_ApplyPendingPlanTargetAndManeuver(targetObjectIndex);
					g_curCraft = savedCraft;
					g_paiContext = savedContext;
				}
			} else if (strcmp(planName, "boardtodestroypln") == 0) {
				if (targetMobileObject != NULL) {
					PaiContext savedContext;
					CraftData* savedCraft;

					Mission_CreditDestructionDamageContributors(g_paiContext.objectIndex, targetObjectIndex);
					if (targetMobileObject->lifetimeTimer != 0)
						targetMobileObject->lifetimeTimer =
							SIMULATION_TICKS_PER_SECOND *
							((GameRand() & SELF_DESTRUCT_RANDOM_MASK) + SELF_DESTRUCT_RANDOM_BASE);
					targetController->pendingPlanId = pai_findplanbyname("selfdestroypln");
					savedContext = g_paiContext;
					savedCraft = g_curCraft;
					g_curCraft = targetCraft;
					pai_setupcraftcontext(targetObjectIndex);
					pai_ApplyPendingPlanTargetAndManeuver(targetObjectIndex);
					g_curCraft = savedCraft;
					g_paiContext = savedContext;
				}
				fsfx_SpeakTacticalOfficerEvent(TACTICAL_VOICE_STATUS, TACTICAL_MSG_BOARDING_COMPLETE,
											   g_paiContext.objectIndex, UINT16_MAX);
			} else if (strcmp(planName, "boardtopickuppln") == 0) {
				if (targetMobileObject != NULL) {
					paiman_TransferObjectToAiTeam(targetObjectIndex, targetCraft, CAPTURE_OWNER_FLAG);
					g_curCraft->carriedObjectIndex = targetObjectIndex;
					targetCraft->carrierObjIdx = g_paiContext.objectIndex;
					targetCraft->workingSubsystems = 0;
				} else {
					++g_missionFgStats[*targetFlightGroupIndexPtr]
						  .outcomeCount[FLIGHT_GROUP_OUTCOME_CAPTURED];
					g_objectTable[targetObjectIndex].objectType = 0;
				}
				fsfx_SpeakTacticalOfficerEvent(TACTICAL_VOICE_STATUS, TACTICAL_MSG_TRANSFER_COMPLETE,
											   g_paiContext.objectIndex, UINT16_MAX);
			} else if (strcmp(planName, "boardtocontactpln") == 0) {
				if (targetMobileObject != NULL)
					targetCraft->boardingState = 2;
				msg_emitCraftMessage(g_paiContext.objectIndex, g_curCraft,
									 IFMSG_156_REPORTS_DOCKING_OPERATION_COMPLETE);
				fsfx_SpeakTacticalOfficerEvent(TACTICAL_VOICE_STATUS, TACTICAL_MSG_BOARDING_COMPLETE,
											   g_paiContext.objectIndex, UINT16_MAX);
			} else if (strcmp(planName, "boardtorepairpln") == 0) {
				if (targetMobileObject != NULL) {
					targetCraft->workingSubsystems = targetCraft->systemFlags;
					targetCraft->subsystemDamage = 0;
					targetCraft->objectKind = CRAFT_OBJECT_KIND_ACTIVE;
					targetCraft->boardingState = 3;
				}
				msg_emitCraftMessage(g_paiContext.objectIndex, g_curCraft,
									 IFMSG_156_REPORTS_DOCKING_OPERATION_COMPLETE);
				fsfx_SpeakTacticalOfficerEvent(TACTICAL_VOICE_STATUS, TACTICAL_MSG_BOARDING_COMPLETE,
											   g_paiContext.objectIndex, UINT16_MAX);
				fsfx_SpeakTacticalOfficerEvent(TACTICAL_VOICE_STATUS, TACTICAL_MSG_REPAIRED_TARGET,
											   targetObjectIndex, UINT16_MAX);
			}

			if (targetMobileObject != NULL) {
				MobileObject* boardingMobileObject = g_objectTable[g_paiContext.objectIndex].mobj;

				if (boardingMobileObject->iff == targetMobileObject->iff) {
					uint8_t* visibility = &targetCraft->iffVisibility[boardingMobileObject->team];

					if (*visibility == 0) {
						uint16_t visibilityIndex;
						uint16_t maximumVisibility = 0;

						for (visibilityIndex = 0; visibilityIndex < IFF_VISIBILITY_COUNT; ++visibilityIndex) {
							if (maximumVisibility < targetCraft->iffVisibility[visibilityIndex])
								maximumVisibility = targetCraft->iffVisibility[visibilityIndex];
						}
						*visibility = (uint8_t)(maximumVisibility + 1);
						++g_missionFgStats[*targetFlightGroupIndexPtr]
							  .outcomeCount[FLIGHT_GROUP_OUTCOME_INSPECTED];
						if (g_missionFlightGroups[*targetFlightGroupIndexPtr].fg.specialCargoCraft ==
							targetCraft->waveNumber)
							g_missionFgStats[*targetFlightGroupIndexPtr]
								.specialCargoOutcome[FLIGHT_GROUP_OUTCOME_INSPECTED] = 1;
					}
				} else {
					fsfx_PlaySound(FLIGHT_SOUND_GENERAL_WARNING, -1, g_localPlayer);
				}
			}
			++g_paiContext.controller->orderScratch.goalProgress[g_paiContext.controller->currentOrderSlot];
			g_curCraft->aiFlight.objSignatures[g_curCraft->aiFlight.objSignatureCount++] = targetSignature;
			if (g_curCraft->aiFlight.objSignatureCount >= MAX_OBJECT_SIGNATURE_COUNT)
				--g_curCraft->aiFlight.objSignatureCount;
			if (g_curCraft->aiFlight.objSignatureCount == 1) {
				uint16_t flightGroupIndex = g_paiContext.orderFlightGroupIndex;

				++g_missionFgStats[flightGroupIndex].outcomeCount[FLIGHT_GROUP_OUTCOME_DOCKED];
				if (g_missionFlightGroups[flightGroupIndex].fg.specialCargoCraft == g_curCraft->waveNumber)
					g_missionFgStats[flightGroupIndex].specialCargoOutcome[FLIGHT_GROUP_OUTCOME_DOCKED] = 1;
			}
			if (targetMobileObject != NULL) {
				++targetCraft->aiFlight.orderActionCounter;
				++g_missionFgStats[targetFlightGroupIndex].outcomeCount[FLIGHT_GROUP_OUTCOME_BOARDED];
				if (g_missionFlightGroups[targetFlightGroupIndex].fg.specialCargoCraft ==
					targetCraft->waveNumber)
					g_missionFgStats[targetFlightGroupIndex]
						.specialCargoOutcome[FLIGHT_GROUP_OUTCOME_BOARDED] = 1;
			}
			g_paiContext.controller->maneuverPhase = BOARD_PHASE_SEPARATE;
			g_paiContext.controller->maneuverTimer = SEPARATION_DURATION;
			if (g_players[g_localPlayer].currentTargetObjectIdx == targetObjectIndex)
				g_hudCachedTargetObjectIdx = HUD_TARGET_INVALIDATED;
			if (g_objectTable[targetObjectIndex].playerOwnerIdx != -1)
				g_paiContext.controller->candidateTargetIdx = UINT16_MAX;
			return 0;
		}

		case BOARD_PHASE_SEPARATE:
			if (g_paiContext.controller->maneuverTimer == 0) {
				g_paiContext.controller->targetObjIdx = UINT16_MAX;
				g_paiContext.controller->targetSignature = 0;
				g_paiContext.controller->hasLiveTarget = 0;
				return 1;
			}
			if (targetMobileObject != NULL) {
				pai_calcrotatedpoint(&g_objectTable[g_paiContext.objectIndex], 0, 0x4000, 0);
				g_curCraft->pushAccumX = g_rotatedX + g_objectTable[targetObjectIndex].world_x -
										 g_objectTable[g_paiContext.objectIndex].world_x;
				g_curCraft->pushAccumY = g_rotatedY + g_objectTable[targetObjectIndex].world_y -
										 g_objectTable[g_paiContext.objectIndex].world_y;
				g_curCraft->pushAccumZ = g_rotatedZ + g_objectTable[targetObjectIndex].world_z -
										 g_objectTable[g_paiContext.objectIndex].world_z;
			} else {
				g_curCraft->pushAccumX = 0;
				g_curCraft->pushAccumY = 0;
				g_curCraft->pushAccumZ = SEPARATION_MISSION_POINT_PUSH;
			}
			return 0;

		default:
			return 0;
	}
}

// FUNCTION: XVT 0x4A3750
void paiman_TransferObjectToAiTeam(unsigned int objectIdx, CraftData* craft, uint8_t ownerFlag) {
	ObjectRecord* object;
	MobileObject** currentMobileObjectPtr;
	uint8_t objectTeam;
	int flightGroupIdx;
	uint8_t missionTeam;

	object = &g_objectTable[objectIdx];
	flightGroupIdx = object->flightGroupIdx;
	currentMobileObjectPtr = &g_objectTable[g_paiContext.objectIndex].mobj;
	objectTeam = object->mobj->team;
	if ((*currentMobileObjectPtr)->team != objectTeam) {
		missionTeam = g_missionFlightGroups[flightGroupIdx].fg.team;
		if ((*currentMobileObjectPtr)->team == missionTeam) {
			g_missionFgStats[flightGroupIdx].outcomeCount[FLIGHT_GROUP_OUTCOME_CAPTURED]--;
			if (g_missionFlightGroups[flightGroupIdx].fg.specialCargoCraft == craft->waveNumber) {
				g_missionFgStats[flightGroupIdx].specialCargoOutcome[FLIGHT_GROUP_OUTCOME_CAPTURED] = 0;
			}
			craft->wasCaptured = 0;
		} else {
			if (objectTeam == missionTeam) {
				g_missionFgStats[flightGroupIdx].outcomeCount[FLIGHT_GROUP_OUTCOME_CAPTURED]++;
				if (g_missionFlightGroups[flightGroupIdx].fg.specialCargoCraft == craft->waveNumber) {
					g_missionFgStats[flightGroupIdx].specialCargoOutcome[FLIGHT_GROUP_OUTCOME_CAPTURED] = 1;
				}
			} else {
				g_flightMissionState.runtime
					.teamFgCounters[1][g_missionFlightGroups[craft->wasCaptured & 0x7F].fg.team]
								   [flightGroupIdx]--;
			}
			g_flightMissionState.runtime.teamFgCounters[1][(*currentMobileObjectPtr)->team][flightGroupIdx]++;
			craft->wasCaptured = ownerFlag | (uint8_t)g_paiContext.orderFlightGroupIndex;
		}
	}

	g_objectTable[objectIdx].mobj->iff = g_objectTable[g_paiContext.objectIndex].mobj->iff;
	g_objectTable[objectIdx].mobj->team = g_objectTable[g_paiContext.objectIndex].mobj->team;
	craft->workingSubsystems = craft->systemFlags;
	craft->subsystemDamage = 0;
	craft->objectKind = CRAFT_OBJECT_KIND_ACTIVE;
	craft->lastAttackerObjIdx = UINT16_MAX;
}

// FUNCTION: XVT 0x4A3920
void paiman_initawaitboardmaneuver(void) {
	g_curCraft->aiFlight.enterFlag = 0;
	g_curCraft->aiFlight.headingState = 0;
	g_curCraft->aiFlight.turnState = 0;
	paiman_setpower(g_paiContext.objectIndex, 0);
}

// FUNCTION: XVT 0x4A3960
int16_t paiman_awaitboardmaneuver(void) {
	g_curCraft->aiFlight.enterFlag = 0;
	g_curCraft->aiFlight.headingState = 0;
	g_curCraft->aiFlight.turnState = 0;
	paiman_setpower(g_paiContext.objectIndex, 0);
	return 0;
}

// FUNCTION: XVT 0x4A39A0
void paiman_initheadtowardmaneuver(void) { paiman_setflighttotarget(0, 1); }

// FUNCTION: XVT 0x4A39B0
int16_t paiman_headtowardmaneuver(void) {
	paiman_setflighttotarget(0, 1);
	g_curCraft->aiFlight.enterFlag = 1;
	g_curCraft->aiFlight.rollStep = UINT16_MAX;
	g_paiContext.controller->targetRoll = 0;
	return 0;
}

// FUNCTION: XVT 0x4A39F0
void paiman_initturnawaymaneuver(void) {
	paiman_setupturnawaycourse(g_paiContext.objectIndex);
	g_paiContext.controller->maneuverTimer = 3540;
}

// FUNCTION: XVT 0x4A3A10
int16_t paiman_turnawaymaneuver(void) {
	if (g_paiContext.controller->maneuverTimer == 0)
		return 1;
	if (g_paiContext.controller->aiPlanState == 0)
		paiman_setupturnawaycourse(g_paiContext.objectIndex);
	return 0;
}

// FUNCTION: XVT 0x4A3A50
void paiman_setupturnawaycourse(unsigned int objectIdx) {
	uint16_t yaw;
	uint16_t effectiveSkill;
	unsigned int turnStep;

	if (g_curCraft->lastAttackerObjIdx != UINT16_MAX)
		yaw = g_objectTable[g_curCraft->lastAttackerObjIdx].yaw;
	else
		yaw = g_objectTable[objectIdx].yaw + 0x8000;
	g_paiContext.controller->targetXYAngle = yaw;
	effectiveSkill = pai_GetEffectiveSkillValue(g_curCraft);
	turnStep = effectiveSkill >> 1;
	turnStep += 0x8000;
	paiman_setturn(turnStep);
	g_paiContext.controller->aiPlanState =
		g_aiTurnAwayStateDelayBySkill[g_paiContext.skillTier] * SIMULATION_TICKS_PER_SECOND;
}

// FUNCTION: XVT 0x4A3AF0
void paiman_initoutofhangarmaneuver(void) { g_paiContext.controller->maneuverTimer = 2360; }

// FUNCTION: XVT 0x4A3B00
int16_t paiman_outofhangarmaneuver(void) {
	int maneuverTimer;

	maneuverTimer = g_paiContext.controller->maneuverTimer;
	if (maneuverTimer == 0) {
		uint16_t flightGroupIndex;
		uint16_t order;
		uint8_t planId;
		uint8_t* exitHangarPlan;

		flightGroupIndex = g_paiContext.orderFlightGroupIndex;
		order = g_missionFlightGroups[flightGroupIndex].fg.orders[0].order;
		if (g_curCraft->leader_obj_idx == UINT8_MAX) {
			planId = g_builtinPlanIdByNameIndex[g_orderLeaderBuiltinPlanNameIndex[order]];
		} else {
			planId = g_builtinPlanIdByNameIndex[g_orderFollowerBuiltinPlanNameIndex[order]];
		}
		pai_SetFlightGroupFormation(flightGroupIndex, g_missionFlightGroups[flightGroupIndex].fg.formation,
									g_missionFlightGroups[flightGroupIndex].fg.formationSpacing);
		exitHangarPlan = pai_getplandataptrbyname("exithangarpln");
		exitHangarPlan[3] = planId;
		return 1;
	}

	return 0;
}

// FUNCTION: XVT 0x4A3BB0
void paiman_initavoidstarshipmaneuver(void) {
	uint16_t effectiveSkill;
	unsigned int turnStep;

	g_paiContext.controller->aiPlanState = ((GameRand() & 7) + 15) * SIMULATION_TICKS_PER_SECOND;
	effectiveSkill = pai_GetEffectiveSkillValue(g_curCraft);
	turnStep = effectiveSkill >> 1;
	turnStep += 0x8000;
	paiman_setturn(turnStep);
	g_curCraft->aiFlight.headingStep = UINT16_MAX;
	g_curCraft->aiFlight.headingForce = 0;
	if (g_paiContext.controller->targetZAngle <= g_curCraft->pitch)
		g_curCraft->aiFlight.headingState = 1;
	else
		g_curCraft->aiFlight.headingState = 2;
}

// FUNCTION: XVT 0x4A3C40
int16_t paiman_avoidstarshipmaneuver(void) { return 0; }

// FUNCTION: XVT 0x4A3C50
void paiman_initwaitmaneuver(void) {
	g_paiContext.controller->maneuverTimer =
		g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[g_paiContext.orderSlot].variable1;
	g_paiContext.controller->maneuverTimer *= 1180;
	g_curCraft->aiFlight.enterFlag = 0;
	g_curCraft->aiFlight.headingState = 0;
	g_curCraft->aiFlight.turnState = 0;
	paiman_setpower(g_paiContext.objectIndex, 0);
}

// FUNCTION: XVT 0x4A3CF0
int16_t paiman_waitmaneuver(void) { return 0; }

// FUNCTION: XVT 0x4A3D00
void paiman_initawaitboardmaneuver_2(void) {
	g_curCraft->aiFlight.enterFlag = 0;
	g_curCraft->aiFlight.headingState = 0;
	g_curCraft->aiFlight.turnState = 0;
	paiman_setpower(g_paiContext.objectIndex, 0);
}

// FUNCTION: XVT 0x4A3D40
int16_t paiman_dropoffmaneuver(void) {
	enum {
		TURN_DISTANCE_THRESHOLD = 256,
		ARRIVAL_DISTANCE_THRESHOLD = 32,
		TURN_STATE_ACTIVE = 2,
		FULL_TURN_STEP = INT16_MIN,
		DROPOFF_WAIT_TICKS = 1180,
		DROPOFF_PUSH_DISTANCE = 1500,
	};

	if (g_paiContext.controller->maneuverPhase == 0) {
		uint16_t formationSlotIndex = g_paiContext.controller->waypointIndex;
		uint16_t destinationFlightGroupIndex =
			(uint16_t)(g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
						   .fg.orders[g_paiContext.orderSlot]
						   .variable2 -
					   1);
		uint16_t basisObjectIndex = UINT8_MAX;
		uint16_t objectIndex;
		int minZ;
		int distanceY;
		int distanceX;
		int distanceZ;

		for (objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
			 (int)objectIndex < g_activeRegionCraftObjectSlotEnd; ++objectIndex) {
			ObjectRecord* object = &g_objectTable[objectIndex];

			if (object->objectType != 0 && object->flightGroupIdx == destinationFlightGroupIndex &&
				object->mobj->pCraft->leader_obj_idx == UINT8_MAX) {
				basisObjectIndex = objectIndex;
			}
		}

		Mission_ResolveFormationSlotWorldLoc(destinationFlightGroupIndex, formationSlotIndex,
											 basisObjectIndex);
		minZ = -ModelBounds_GetMinZ(g_objectTable[g_paiContext.objectIndex].objectType);
		g_curCraft->pushAccumX = worldlocx - g_objectTable[g_paiContext.objectIndex].world_x;
		distanceX = g_curCraft->pushAccumX;
		g_curCraft->pushAccumY = worldlocy - g_objectTable[g_paiContext.objectIndex].world_y;
		distanceY = g_curCraft->pushAccumY;
		g_curCraft->pushAccumZ = worldlocz - g_objectTable[g_paiContext.objectIndex].world_z;
		g_curCraft->pushAccumZ += minZ;
		distanceZ = g_curCraft->pushAccumZ;
		trig2_ctop(distanceX, distanceY, distanceZ);

		if (distanceX < 0)
			distanceX = -distanceX;
		if (distanceY < 0)
			distanceY = -distanceY;
		if (distanceZ < 0)
			distanceZ = -distanceZ;

		if (distanceX + distanceY > TURN_DISTANCE_THRESHOLD &&
			g_objectTable[g_paiContext.objectIndex].yaw != (uint16_t)trig2_xyangle) {
			g_curCraft->aiFlight.turnState = TURN_STATE_ACTIVE;
			g_curCraft->aiFlight.turnStep = FULL_TURN_STEP;
			g_paiContext.controller->targetXYAngle = (uint16_t)trig2_xyangle;
		}
		if (distanceX + distanceY + distanceZ < ARRIVAL_DISTANCE_THRESHOLD) {
			PaiContext savedContext = g_paiContext;
			CraftData* savedCraft;

			g_currentFlightGroupIdx = destinationFlightGroupIndex;
			savedCraft = g_curCraft;
			g_spawnLeaderObjIdx = (uint8_t)basisObjectIndex;
			Mission_StartFlightGroupArrival(formationSlotIndex);
			g_curCraft = savedCraft;
			g_paiContext = savedContext;
			++g_paiContext.controller->maneuverPhase;
			g_paiContext.controller->maneuverTimer = DROPOFF_WAIT_TICKS;
			g_curCraft->pushAccumZ = DROPOFF_PUSH_DISTANCE;
		}
	} else if (g_paiContext.controller->maneuverTimer == 0) {
		g_paiContext.controller->maneuverPhase = 0;
		++g_paiContext.controller->waypointIndex;
	}

	g_paiContext.controller->orderScratch.goalProgress[g_paiContext.orderSlot] =
		g_paiContext.controller->waypointIndex;
	return 0;
}

// FUNCTION: XVT 0x4A4050
void paiman_initkamikazemaneuver(void) {
	pai_UpdateAimPointFromOrderTarget();
	paiman_setflighttotarget(0, 1);
	paiman_setpower(g_paiContext.objectIndex, UINT16_MAX);
}

// FUNCTION: XVT 0x4A4080
int16_t paiman_kamikazemaneuver(void) {
	pai_UpdateAimPointFromOrderTarget();
	paiman_setflighttotarget(0, 1);
	return 0;
}

// FUNCTION: XVT 0x4A40A0
void paiman_initavoidattackermaneuver(void) {
	uint16_t pitch;
	uint16_t effectiveSkill;
	unsigned int turnStep;
	uint16_t groupAI;

	g_paiContext.controller->maneuverPhase = GameRand() & 1;
	if (g_paiContext.controller->maneuverPhase) {
		g_paiContext.controller->targetXYAngle =
			(uint16_t)(g_objectTable[g_paiContext.objectIndex].yaw + 0x4000u);
	} else {
		g_paiContext.controller->targetXYAngle =
			(uint16_t)(g_objectTable[g_paiContext.objectIndex].yaw - 0x4000u);
	}

	effectiveSkill = pai_GetEffectiveSkillValue(g_curCraft);
	turnStep = effectiveSkill >> 1;
	turnStep += 0x8000u;
	paiman_setturn(turnStep);

	pitch = g_objectTable[g_paiContext.objectIndex].pitch;
	if (pitch < 0x4000u) {
		g_paiContext.controller->targetZAngle = (uint16_t)(pitch + 0x3000u);
		g_curCraft->aiFlight.headingState = 2;
	} else {
		g_paiContext.controller->targetZAngle = (uint16_t)(pitch - 0x3000u);
		g_curCraft->aiFlight.headingState = 1;
	}

	g_curCraft->aiFlight.headingStep = UINT16_MAX;
	g_curCraft->aiFlight.headingForce = 0;
	g_curCraft->aiFlight.enterFlag = 3;
	g_curCraft->aiFlight.rollStep = UINT16_MAX;
	g_paiContext.controller->targetRoll = (uint16_t)GameRand();
	g_paiContext.controller->maneuverTimer = SIMULATION_TICKS_PER_SECOND * ((GameRand() & 7) + 10);

	groupAI = g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.groupAI;
	g_paiContext.controller->aiPlanState =
		(int16_t)((uint16_t)MATH2_fraction(g_aiAvoidAttackerDelayFracQ16ByGroupAI[groupAI], 0x00ECu) +
				  236u * g_aiAvoidAttackerDelaySecondsByGroupAI[groupAI]);
}

// FUNCTION: XVT 0x4A4260
int16_t paiman_avoidattackermaneuver(void) {
	uint16_t randomAngle;
	uint16_t groupAi;
	int16_t pitch;

	if (g_paiContext.controller->maneuverTimer == 0) {
		g_curCraft->aiFlight.enterFlag = 4;
		return 1;
	}
	if (g_paiContext.controller->aiPlanState == 0) {
		randomAngle = (uint16_t)(GameRand() & 0x0FFF);
		g_paiContext.controller->maneuverPhase ^= 1;
		if (g_paiContext.controller->maneuverPhase != 0)
			g_paiContext.controller->targetXYAngle =
				(uint16_t)(g_objectTable[g_paiContext.objectIndex].yaw + randomAngle + 12288);
		else
			g_paiContext.controller->targetXYAngle =
				(uint16_t)(g_objectTable[g_paiContext.objectIndex].yaw - randomAngle - 12288);
		paiman_setturn((uint16_t)(pai_GetEffectiveSkillValue(g_curCraft) >> 1) + 0x8000u);
		g_paiContext.controller->targetRoll ^= 0x8000u;
		randomAngle = (uint16_t)(GameRand() & 0x0FFF);
		pitch = g_objectTable[g_paiContext.objectIndex].pitch;
		if ((uint16_t)pitch < 0x4000u) {
			g_paiContext.controller->targetZAngle = (uint16_t)(pitch + randomAngle + 0x2000);
			g_curCraft->aiFlight.headingState = 2;
		} else {
			g_paiContext.controller->targetZAngle = (uint16_t)(pitch - randomAngle - 0x2000);
			g_curCraft->aiFlight.headingState = 1;
		}
		groupAi = g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.groupAI;
		g_paiContext.controller->aiPlanState =
			(uint16_t)(SIMULATION_TICKS_PER_SECOND * g_aiAvoidAttackerDelaySecondsByGroupAI[groupAi] +
					   MATH2_fraction(g_aiAvoidAttackerDelayFracQ16ByGroupAI[groupAi], 0xEC));
		g_paiContext.controller->aiPlanState += MATH2_fraction((uint16_t)GameRand(), 0xEC);
		if (groupAi >= 3 && (GameRand() & 7) == 7 && g_curCraft->cmTypeId == COUNTERMEASURE_TYPE_FLARE &&
			g_curCraft->cmAmmoCount != 0 && g_curCraft->cmFireCooldownTimer == 0)
			laser_createcountermeasureprojectile(g_paiContext.objectIndex,
												 COUNTERMEASURE_PROJECTILE_OBJECT_TYPE);
	}
	return 0;
}

// FUNCTION: XVT 0x4A4480
void paiman_initkamikazemaneuver_2(void) {
	pai_UpdateAimPointFromOrderTarget();
	paiman_setflighttotarget(0, 1);
	paiman_setpower(g_paiContext.objectIndex, UINT16_MAX);
}

// FUNCTION: XVT 0x4A44B0
int16_t paiman_dodgemaneuver(void) {
	pai_UpdateAimPointFromOrderTarget();
	paiman_setflighttotarget(0, 1);
	return 0;
}

// FUNCTION: XVT 0x4A44D0
void paiman_setflighttotarget(uint16_t pitchBias, int driveHeading) {
	uint16_t effectiveSkill;
	int aimX;
	int aimY;
	int aimZ;
	int worldX;
	int worldY;
	int worldZ;
	uint16_t pitch;
	unsigned int turnStep;
	int updateHeading;

	aimX = g_paiContext.controller->aimPointX;
	aimY = g_paiContext.controller->aimPointY;
	aimZ = g_paiContext.controller->aimPointZ;
	worldX = g_objectTable[g_paiContext.objectIndex].world_x;
	worldY = g_objectTable[g_paiContext.objectIndex].world_y;
	worldZ = g_objectTable[g_paiContext.objectIndex].world_z;
	aimX -= worldX;
	aimY -= worldY;
	aimZ -= worldZ;
	trig2_ctop(aimX, aimY, aimZ);
	g_paiContext.controller->targetXYAngle = (uint16_t)(pitchBias + trig2_xyangle);
	effectiveSkill = pai_GetEffectiveSkillValue(g_curCraft);
	effectiveSkill >>= 1;
	turnStep = effectiveSkill;
	turnStep += 0x4000;
	paiman_setturn(turnStep);
	updateHeading = driveHeading;
	if (updateHeading != 0) {
		g_paiContext.controller->targetZAngle = (uint16_t)pitchQ16;
		g_curCraft->aiFlight.headingStep = UINT16_MAX;
		g_curCraft->aiFlight.headingForce = 0;
		pitch = g_curCraft->pitch;
		if (pitch >= g_paiContext.controller->targetZAngle) {
			g_curCraft->aiFlight.headingState = 1;
		} else {
			g_curCraft->aiFlight.headingState = 2;
		}
		g_curCraft->aiFlight.climbState = 0;
		g_curCraft->aiFlight.diveState = 0;
	}
}

// FUNCTION: XVT 0x4A45E0
void paiman_initcruiseandrunawaycontrols(void) {
	uint16_t pitch;

	g_curCraft->aiFlight.diveState = 0;
	g_curCraft->aiFlight.climbState = 0;
	g_paiContext.controller->targetZAngle = 0x4000;
	g_curCraft->aiFlight.headingStep = UINT16_MAX;
	g_curCraft->aiFlight.headingForce = 0;
	pitch = g_curCraft->pitch;
	if (pitch < 0x4000) {
		g_curCraft->aiFlight.headingState = 2;
	} else if (pitch > 0x4000) {
		g_curCraft->aiFlight.headingState = 1;
	} else {
		g_curCraft->aiFlight.headingState = 3;
	}
	g_curCraft->aiFlight.enterFlag = 1;
	g_curCraft->aiFlight.rollStep = UINT16_MAX;
	g_paiContext.controller->targetRoll = 0;
	g_curCraft->aiFlight.turnState = 0;
}

// FUNCTION: XVT 0x4A4690
void paiman_attacktarget(int16_t yawOffset) {
	unsigned int objectIndex;
	uint16_t targetObjectIndex;
	uint16_t yawDifference;
	uint16_t effectiveSkill;
	int deltaX;
	int deltaY;
	int deltaZ;

	objectIndex = g_paiContext.objectIndex;
	if (g_paiContext.controller->maneuverMode == AI_MANEUVER_MODE_ROCKET_ATTACK ||
		(targetObjectIndex = g_paiContext.controller->targetObjIdx,
		 g_objectTable[targetObjectIndex].mobj == NULL)) {
		pai_UpdateAimPointFromOrderTarget();
	} else {
		paiman_calcplanelead(targetObjectIndex);
	}
	deltaX = g_paiContext.controller->aimPointX - g_objectTable[objectIndex].world_x;
	deltaY = g_paiContext.controller->aimPointY - g_objectTable[objectIndex].world_y;
	deltaZ = g_paiContext.controller->aimPointZ - g_objectTable[objectIndex].world_z;
	trig2_ctop(deltaX, deltaY, deltaZ);
	g_paiContext.controller->targetXYAngle = yawOffset + trig2_xyangle;
	if (g_curCraft->aiFlight.enterFlag == 2) {
		yawDifference = g_objectTable[objectIndex].yaw - g_paiContext.controller->targetXYAngle;
		if (yawDifference > 0x8000) {
			yawDifference = (uint16_t)(0u - yawDifference);
		}
		if (yawDifference >= 0x2000 || trig2_polardistance < 0x10000) {
			effectiveSkill = pai_GetEffectiveSkillValue(g_curCraft);
			paiman_setturn((unsigned int)(effectiveSkill >> 1) + 0x8000);
		}
	} else {
		effectiveSkill = pai_GetEffectiveSkillValue(g_curCraft);
		paiman_setturn((unsigned int)(effectiveSkill >> 1) + 0x8000);
	}
	if (g_curCraft->aiFlight.enterFlag == 3) {
		g_curCraft->aiFlight.enterFlag = 0;
	}
	if (g_curCraft->pitch != (uint16_t)pitchQ16) {
		g_paiContext.controller->targetZAngle = (uint16_t)pitchQ16;
		g_curCraft->aiFlight.headingStep = UINT16_MAX;
		if (g_paiContext.controller->targetZAngle <= g_curCraft->pitch) {
			g_curCraft->aiFlight.headingState = 1;
		} else {
			g_curCraft->aiFlight.headingState = 2;
		}
		paiman_setpower(objectIndex, UINT16_MAX);
		g_curCraft->aiFlight.climbState = 0;
		g_curCraft->aiFlight.diveState = 0;
		g_curCraft->aiFlight.headingForce = 0;
	}
}

// FUNCTION: XVT 0x4A4840
void paiman_calcplanelead(int targetObjIdx) {
	uint16_t leadTicks;
	int deltaY;
	int deltaZ;
	MobileObject* targetMobile;

	if (g_objectTable[targetObjIdx].mobj->speed == 0) {
		leadTicks = 0;
	} else {
		uint16_t projectileType;
		uint16_t projectileSpeed;
		uint16_t combinedSpeed;
		uint16_t targetSpeed;
		uint16_t yawDifference;
		uint16_t timeDivisor;
		uint16_t travelTicks;
		uint16_t effectiveSkill;
		uint16_t objectIndex;

		objectIndex = g_paiContext.objectIndex;
		pai_ObjectRefDirectionToObjectRef(objectIndex, g_paiContext.controller->targetObjIdx);
		if (strcmp(g_planTable[g_paiContext.controller->currentPlanId].name, "disableldr1pln") == 0 &&
			g_paiContext.controller->hasLiveTarget == 1) {
			projectileType = PROJECTILE_OBJECT_TYPE_ION_LASER;
		} else if (g_curCraft->laserState.projectileTypeId[0] > PROJECTILE_OBJECT_TYPE_FIRST) {
			projectileType = g_curCraft->laserState.projectileTypeId[0];
		} else {
			projectileType = PROJECTILE_OBJECT_TYPE_FIRST;
		}
		projectileSpeed = g_projectileDamageByObjectType.speed[projectileType - PROJECTILE_OBJECT_TYPE_FIRST];
		combinedSpeed = projectileSpeed + g_objectTable[objectIndex].mobj->speed;
		yawDifference = g_objectTable[targetObjIdx].yaw - g_objectTable[objectIndex].yaw;
		targetSpeed = g_objectTable[targetObjIdx].mobj->speed;
		if (yawDifference >= 0x8000) {
			yawDifference = (uint16_t)(0u - yawDifference);
		}
		if (yawDifference < 0x4000) {
			combinedSpeed -= trig2_cosinewordmult(targetSpeed, yawDifference);
		} else {
			combinedSpeed += trig2_cosinewordmult(targetSpeed, yawDifference);
		}
		timeDivisor = combinedSpeed / 5u + 18 * combinedSpeed;
		if (timeDivisor == 0) {
			timeDivisor = 19;
		}
		travelTicks = g_simStepScale * (trig2_polardistance / timeDivisor);
		effectiveSkill = pai_GetEffectiveSkillValue(g_curCraft);
		leadTicks = MATH2_fraction(travelTicks, effectiveSkill);
	}
	targetMobile = g_objectTable[targetObjIdx].mobj;
	deltaY =
#ifdef XVT_MODERN
		(XvtFlightTiming_IsUnlocked() ? XvtReferenceMotion_Axis(targetObjIdx, 1)
									  : (g_objectTable[targetObjIdx].world_y - targetMobile->prevWorldY))
#else
		g_objectTable[targetObjIdx].world_y - targetMobile->prevWorldY
#endif
		;
	deltaZ =
#ifdef XVT_MODERN
		(XvtFlightTiming_IsUnlocked() ? XvtReferenceMotion_Axis(targetObjIdx, 2)
									  : (g_objectTable[targetObjIdx].world_z - targetMobile->prevWorldZ))
#else
		g_objectTable[targetObjIdx].world_z - targetMobile->prevWorldZ
#endif
		;
	g_paiContext.controller->aimPointX =
		g_objectTable[targetObjIdx].world_x +
		leadTicks * (
#ifdef XVT_MODERN
						(XvtFlightTiming_IsUnlocked()
							 ? XvtReferenceMotion_Axis(targetObjIdx, 0)
							 : (g_objectTable[targetObjIdx].world_x - targetMobile->prevWorldX))
#else
						g_objectTable[targetObjIdx].world_x - targetMobile->prevWorldX
#endif
					);
	g_paiContext.controller->aimPointY = g_objectTable[targetObjIdx].world_y + leadTicks * deltaY;
	g_paiContext.controller->aimPointZ = g_objectTable[targetObjIdx].world_z + leadTicks * deltaZ;
}

// FUNCTION: XVT 0x4A4A10
void paiman_calcformation(void) {
	uint16_t modelIndex;
	int16_t boundSizeY;
	int16_t boundSizeX;
	int formationType;
	int formationIndex;
	int16_t displacementX;
	int16_t displacementY;
	int16_t displacementZ;
	int16_t divisor;
	uint16_t boundSizeShift;
	int16_t boundSizeZ;
	int16_t offsetX;
	int16_t offsetZ;
	int16_t offsetY;
	int16_t separation;

	modelIndex = g_curCraft->modelIndex;
	boundSizeY = g_modelDefs[modelIndex].boundSizeY;
	boundSizeX = g_modelDefs[modelIndex].boundSizeX;
	formationType = g_curCraft->aiFlight.formationType;
	boundSizeZ = g_modelDefs[modelIndex].boundSizeZ;
	separation = g_paiContext.targetCraft->aiFlight.separation + 1;
	formationIndex = g_curCraft->waveNumber;
	offsetX = g_formPosX[formationType][formationIndex] - g_formPosX[formationType][0];
	offsetY = g_formPosY[formationType][formationIndex] - g_formPosY[formationType][0];
	offsetZ = g_formPosZ[formationType][formationIndex] - g_formPosZ[formationType][0];
	displacementX = (int16_t)(separation * offsetX) * boundSizeX;
	displacementY = (int16_t)(separation * offsetY) * boundSizeY;
	displacementZ = (int16_t)(separation * offsetZ) * boundSizeZ;
	if (separation == 1) {
		displacementX += offsetX * (boundSizeX / 2);
		displacementZ += offsetZ * (boundSizeZ / 2);
		displacementY += offsetY * (boundSizeY / 2);
	}
	divisor = g_formationDivisor[formationType];
	if (divisor != 1) {
		displacementX /= divisor;
		displacementY /= divisor;
		displacementZ /= divisor;
	}
	pai_calcrotatedpoint(&g_objectTable[g_paiContext.leaderObjectIndex], displacementX, displacementZ,
						 displacementY);
	boundSizeShift = g_modelDefs[modelIndex].boundSizeShift;
	if (boundSizeShift != 0) {
		g_rotatedX <<= boundSizeShift;
		g_rotatedY <<= boundSizeShift;
		g_rotatedZ <<= boundSizeShift;
	}
	g_curCraft->pushAccumX = g_rotatedX + g_objectTable[g_paiContext.leaderObjectIndex].world_x -
							 g_objectTable[g_paiContext.objectIndex].world_x;
	g_curCraft->pushAccumY = g_rotatedY + g_objectTable[g_paiContext.leaderObjectIndex].world_y -
							 g_objectTable[g_paiContext.objectIndex].world_y;
	g_curCraft->pushAccumZ = g_rotatedZ + g_objectTable[g_paiContext.leaderObjectIndex].world_z -
							 g_objectTable[g_paiContext.objectIndex].world_z;
}

// FUNCTION: XVT 0x4A4CB0
void paiman_setturn(int turnStep) {
	uint16_t* yaw;
	uint16_t targetYaw;
	uint16_t yawDifference;
	MobileObject* mobileObject;

	yaw = &g_objectTable[g_paiContext.objectIndex].yaw;
	targetYaw = g_paiContext.controller->targetXYAngle;
	yawDifference = *yaw - targetYaw;
	if (yawDifference >= 0x8000u)
		yawDifference = -yawDifference;
	if (yawDifference <= 0x300u) {
		*yaw = targetYaw;
		g_objectTable[g_paiContext.objectIndex].mobj->orientMatrixDirty = 1;
		mobileObject = g_objectTable[g_paiContext.objectIndex].mobj;
		mobileObject->moveVectorDirty = mobileObject->orientMatrixDirty;
		g_curCraft->aiFlight.turnState = 3;
	} else {
		g_curCraft->aiFlight.turnState = 2;
		g_curCraft->aiFlight.turnStep = turnStep;
	}
}

// FUNCTION: XVT 0x4A4D70
void paiman_setpower(int objIdx, int throttle) {
	(void)objIdx;
	g_curCraft->throttleSpeed = throttle;
}

// FUNCTION: XVT 0x4A4D90
void paiman_setspeed(int objIdx, unsigned int desiredSpeed) {
	uint16_t powerDelta;
	CraftData* craft;
	int16_t maxSpeedCache;
	uint16_t adjustedMaxSpeed;

	craft = g_objectTable[objIdx].mobj->pCraft;
	powerDelta = (uint16_t)(6 - (uint8_t)craft->shieldRedirect - (uint8_t)craft->beamLevel -
							(uint8_t)craft->laserRedirect);
	if (powerDelta >= 0x8000u) {
		powerDelta = (uint16_t)-powerDelta;
		powerDelta <<= 13;
		maxSpeedCache = craft->aiFlight.maxSpeedCache;
		adjustedMaxSpeed =
			(uint16_t)(maxSpeedCache - (uint16_t)MATH2_fraction(powerDelta, (uint16_t)maxSpeedCache));
	} else {
		powerDelta <<= 13;
		maxSpeedCache = craft->aiFlight.maxSpeedCache;
		adjustedMaxSpeed =
			(uint16_t)(maxSpeedCache + (uint16_t)MATH2_fraction(powerDelta, (uint16_t)maxSpeedCache));
	}
	if (adjustedMaxSpeed <= desiredSpeed)
		paiman_setpower(objIdx, 0xFFFF);
	else
		paiman_setpower(objIdx, MATH2_divide((uint16_t)desiredSpeed, adjustedMaxSpeed));
}
