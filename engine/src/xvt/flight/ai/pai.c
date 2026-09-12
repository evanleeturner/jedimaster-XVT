#include "xvt/flight/ai/pai.h"
#include "xvt/assets/file.h"
#include "xvt/flight/ai/pai_targetability.h"
#include "xvt/flight/ai/paifight.h"
#include "xvt/flight/ai/paiman.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"

#include <limits.h>
#include <string.h>

// GLOBAL: XVT 0x9D1320
PaiPlanRecord g_planTable[256];

// GLOBAL: XVT 0x9A73B0
PaiContext g_paiContext;
// GLOBAL: XVT 0x9A8C28
int g_paiSkipToOrder4Checked = 0;
// GLOBAL: XVT 0x9A1FF8
int g_targetRangeScore = 0;
// GLOBAL: XVT 0x524100
uint16_t g_aiSkillValueQ16ByLevel[8] = { 0x0000, 0x4000, 0x8000, 0xC000, 0xFFFF, 0xFFFF, 0x0000, 0x0000 };
// GLOBAL: XVT 0x524110
const uint16_t g_aiThinkIntervalBySkill[8] = { 708, 472, SIMULATION_TICKS_PER_SECOND, 118, 59, 29, 0, 0 };
// GLOBAL: XVT 0x5272F8
const uint8_t g_planReportMessageIdByPlanId[PAI_PLAN_REPORT_MESSAGE_COUNT] = {
	159, 159, 159, 183, 178, 186, 183, 178, 183, 178, 183, 178, 162, 164, 166, 163, 162, 165, 165,
	167, 164, 166, 165, 165, 168, 169, 164, 166, 165, 169, 164, 166, 165, 170, 173, 170, 170, 171,
	170, 172, 170, 174, 189, 189, 174, 175, 174, 176, 177, 187, 181, 179, 180, 182, 181, 159, 178,
	183, 183, 184, 185, 161, 161, 183, 183, 187, 188, 190, 170, 162, 191, 191, 191, 0,
};
// GLOBAL: XVT 0x525150
const char* const g_builtinPlanNameTable[76] = {
	"nullpln",
	"stationaryldrpln",
	"stationaryflwpln",
	"formldr1pln",
	"formflw1pln",
	"formevadeldr1pln",
	"formevadeflw1pln",
	"capldr1pln",
	"capescortersldr1pln",
	"caprespondldr1pln",
	"capldr2pln",
	"capldr3pln",
	"capldr4pln",
	"capldr5pln",
	"capflw1pln",
	"capflw2pln",
	"capflw3pln",
	"capflw4pln",
	"capflw5pln",
	"disableldr1pln",
	"escortldr1pln",
	"escortldr2pln",
	"escortldr3pln",
	"escortldr4pln",
	"escortflw1pln",
	"escortflw2pln",
	"escortflw3pln",
	"escortflw4pln",
	"boardtogivepln",
	"boardtotakepln",
	"boardtoexchangepln",
	"boardtocapturepln",
	"boardtodestroypln",
	"boardtopickuppln",
	"boardtocontactpln",
	"board2pln",
	"board3pln",
	"dropoffldr1pln",
	"dropoffldr2pln",
	"rendezvous1pln",
	"rendezvous2pln",
	"rendezvousflw1pln",
	"disabledpln",
	"waitforboardpln",
	"craftwaitforgopln",
	"flyhomepln",
	"followhomepln",
	"flyhomeevadepln",
	"followhomeevadepln",
	"enterhangarpln",
	"exithangarpln",
	"intohyperspacepln",
	"outofhyperspacepln",
	"starshipintohyperpln",
	"starshipfollowhomepln",
	"starshipstatpln",
	"starshipformpln",
	"starshipfollowpln",
	"starshipwaitreturnpln",
	"starshipwaitcreatepln",
	"starshipprotectpln",
	"starshipescortpln",
	"starshipattackpln",
	"starshipdisablepln",
	"starshipwaitforgopln",
	"variablepln",
	"waitpln",
	"selfdestroypln",
	"boardtorepairpln",
	"capfreeldr1pln",
	"kamikaze1pln",
	"kamikaze2pln",
	"kamikaze3pln",
	"playercontrolledpln",
	"",
};
// GLOBAL: XVT 0x525280
struct PaiPlanTokenDef g_paiTargetTokenDefs[10] = {
	{ "LOCATARGET", 249 },
	{ "LOCBTARGET", 250 },
	{ "ABORTTARGET", AI_TARGET_ABORT },
	{ "NORMALTARGET", 252 },
	{ "PRIMARYTARGET", 253 },
	{ "HOMETARGET", 254 },
	{ "NULLTARGET", 255 },
	{ "NOTARGET", -1 },
	{ "0x80", 128 },
	{ "", 0 },
};
// GLOBAL: XVT 0x5255B8
struct PaiPlanTokenDef g_paiManeuverTokenDefs[33] = {
	{ "NULLMANR", AI_MANEUVER_MODE_NULL },
	{ "TURNINSIDEMANR", AI_MANEUVER_MODE_TURN_INSIDE },
	{ "SPLITSMANR", AI_MANEUVER_MODE_SPLITS },
	{ "IMMELMANNMANR", AI_MANEUVER_MODE_IMMELMANN },
	{ "SCISSORSMANR", AI_MANEUVER_MODE_SCISSORS },
	{ "RENDEZVOUSMANR", AI_MANEUVER_MODE_RENDEZVOUS },
	{ "CRUISEMANR", AI_MANEUVER_MODE_CRUISE },
	{ "HEADTOWARDFULLMANR", AI_MANEUVER_MODE_HEAD_TOWARD_FULL },
	{ "RUNAWAYMANR", AI_MANEUVER_MODE_RUN_AWAY },
	{ "HEADONATTACKMANR", AI_MANEUVER_MODE_HEAD_ON_ATTACK },
	{ "FOLLOWLEADERMANR", AI_MANEUVER_MODE_FOLLOW_LEADER },
	{ "SETUPATTACKMANR", AI_MANEUVER_MODE_SETUP_ATTACK },
	{ "ATTACKMANR", AI_MANEUVER_MODE_ATTACK },
	{ "ZOOMMANR", AI_MANEUVER_MODE_ZOOM },
	{ "DIVEMANR", AI_MANEUVER_MODE_DIVE },
	{ "SPLITSDIVEMANR", AI_MANEUVER_MODE_SPLITS_DIVE },
	{ "SPEEDAWAYMANR", AI_MANEUVER_MODE_SPEED_AWAY },
	{ "ESCORTMANR", AI_MANEUVER_MODE_ESCORT },
	{ "BOARDMANR", AI_MANEUVER_MODE_BOARD },
	{ "AWAITBOARDMANR", AI_MANEUVER_MODE_AWAIT_BOARD },
	{ "HEADTOWARDMANR", AI_MANEUVER_MODE_HEAD_TOWARD },
	{ "INTOHYPERSPACEMANR", AI_MANEUVER_MODE_INTO_HYPERSPACE },
	{ "OUTOFHYPERSPACEMANR", AI_MANEUVER_MODE_OUT_OF_HYPERSPACE },
	{ "ROCKETATTACKMANR", AI_MANEUVER_MODE_ROCKET_ATTACK },
	{ "TURNAWAYMANR", AI_MANEUVER_MODE_TURN_AWAY },
	{ "STOPMANR", AI_MANEUVER_MODE_STOP },
	{ "OUTOFHANGARMANR", AI_MANEUVER_MODE_OUT_OF_HANGAR },
	{ "EVASIVEMANR", AI_MANEUVER_MODE_EVASIVE },
	{ "AVOIDSTARSHIPMANR", AI_MANEUVER_MODE_AVOID_STARSHIP },
	{ "WAITMANR", AI_MANEUVER_MODE_WAIT },
	{ "DROPOFFMANR", AI_MANEUVER_MODE_DROPOFF },
	{ "KAMIKAZEMANR", AI_MANEUVER_MODE_KAMIKAZE },
	{ "", 0 },
};
// GLOBAL: XVT 0x526050
struct PaiPlanTokenDef g_paiOrderTokenDefs[49] = {
	{ "NULLORDR", 0 },
	{ "UPDATECOURSEORDR", 1 },
	{ "UNDERATTACKORDR", 2 },
	{ "STILLATTACKORDR", 3 },
	{ "FLYHOMEORDR", 4 },
	{ "FIGHTERSHOOTORDR", 5 },
	{ "GUNNERSELFDEFENSEORDR", 6 },
	{ "GUNNEROFFENSEORDR", 7 },
	{ "MISSILEDEFENSEORDR", 8 },
	{ "SCANFORTARGETORDR", 9 },
	{ "WAITRUNORDR", 10 },
	{ "BREAKOFFORDR", 11 },
	{ "LEADERDEADORDR", 12 },
	{ "COVERLEADERORDR", 13 },
	{ "FOLLOWLEADATKORDR", 14 },
	{ "ABORTATKORDR", 15 },
	{ "ONTAILORDR", 16 },
	{ "ALWAYSORDR", 17 },
	{ "CHECKESCORTORDR", 18 },
	{ "LEADERGOHOMEORDR", 19 },
	{ "HYPERSPACEORDR", 20 },
	{ "ENTERHANGARORDR", 21 },
	{ "MOTHERSHIPORDR", 22 },
	{ "ESCORTTARGETORDR", 23 },
	{ "LOOKFORDISABLEORDR", 24 },
	{ "ABORTBOARDORDR", 25 },
	{ "RETURNBOARDORDR", 26 },
	{ "AWAITBOARDORDR", 27 },
	{ "MAKEDISABLEDORDR", 28 },
	{ "NEARTARGETORDR", 29 },
	{ "ROCKETSONBOARDORDR", 30 },
	{ "AVOIDHITORDR", 31 },
	{ "WAITFORALLRETURNORDR", 32 },
	{ "WAITFORALLCREATEORDR", 33 },
	{ "EVASIVEORDR", 34 },
	{ "NEWTARGETORDR", 35 },
	{ "AVOIDSTARSHIPORDR", 36 },
	{ "CHECKHYPERORDR", 37 },
	{ "STOPGOHOMEORDR", 38 },
	{ "COMPLETEGOHOMEORDR", 39 },
	{ "COMPLETEGOOTHERORDR", 40 },
	{ "COMPLETEFOLLOWORDR", 41 },
	{ "WAITGOOTHERORDR", 42 },
	{ "ORDERSWITCHORDR", 43 },
	{ "KILLSELFORDR", 44 },
	{ "DROPOFFDESTORDR", 45 },
	{ "ABORTMOTHERWAITORDR", 46 },
	{ "PLAYERINPUTORDR", 47 },
	{ "", 0 },
};
// GLOBAL: XVT 0xA07CF0
uint8_t* g_planDataPtrs[256];
// GLOBAL: XVT 0x9A8E40
uint8_t g_planOrderData[0x20000] = { 0 };
// GLOBAL: XVT 0x9A8068
int g_planCount = 0;
// GLOBAL: XVT 0x9A7A40
uint8_t g_builtinPlanIdByNameIndex[256] = { 0 };
// GLOBAL: XVT 0x524120
uint8_t g_orderLeaderBuiltinPlanNameIndex[40] = {
	0x01, 0x2f, 0x03, 0x05, 0x27, 0x2a, 0x2b, 0x45, 0x08, 0x09, 0x14, 0x13, 0x1c, 0x1d,
	0x1e, 0x1f, 0x20, 0x21, 0x25, 0x42, 0x42, 0x38, 0x3a, 0x3b, 0x3c, 0x3c, 0x3e, 0x3f,
	0x01, 0x35, 0x01, 0x22, 0x44, 0x01, 0x01, 0x01, 0x43, 0x46, 0x01, 0x00,
};
// GLOBAL: XVT 0x524148
const uint8_t g_orderFollowerBuiltinPlanNameIndex[40] = {
	0x02, 0x30, 0x04, 0x06, 0x29, 0x2a, 0x2b, 0x0e, 0x0e, 0x0e, 0x18, 0x0e, 0x1c, 0x1d,
	0x1e, 0x1f, 0x20, 0x21, 0x04, 0x42, 0x42, 0x39, 0x3a, 0x3b, 0x39, 0x39, 0x39, 0x39,
	0x02, 0x36, 0x02, 0x22, 0x44, 0x01, 0x01, 0x01, 0x43, 0x46, 0x01, 0x00,
};

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4028A0
void pai_UpdateAllCraftAI(void) {
	uint16_t objectIndex;
	int16_t savedRandState;

	savedRandState = g_gameRandStateB;
	for (objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
		 objectIndex < g_activeRegionCraftObjectSlotEnd; ++objectIndex) {
		ObjectRecord* object = &g_objectTable[objectIndex];
		MobileObject* mobileObject;
		AiController* controller;

		if (object->objectType == 0)
			continue;
		mobileObject = object->mobj;
		if (mobileObject->state != 0)
			continue;
		g_curCraft = mobileObject->pCraft;
		controller = &g_curCraft->aiController;
		if (g_curCraft->objectKind == CRAFT_OBJECT_KIND_BREAKING_UP ||
			g_curCraft->objectKind == CRAFT_OBJECT_KIND_EXPLODING || controller->thinkTimer > 0)
			continue;
		if (object->playerOwnerIdx == -1) {
			pai_setupcraftcontext(objectIndex);
			g_gameRandStateB = controller->savedRandSeed;
			pai_ProcessPlan();
			controller->savedRandSeed = g_gameRandStateB;
		}
		controller->thinkTimer += controller->thinkInterval;
	}
	g_gameRandStateB = savedRandState;
}

// FUNCTION: XVT 0x402970
void pai_ApplyPendingPlanTargetAndManeuver(unsigned int objectIdx) {
	AiController* controller;
	uint8_t* planData;
	uint16_t targetToken;
	uint8_t maneuverToken;

	controller = &g_curCraft->aiController;
	planData = g_planDataPtrs[controller->pendingPlanId];
	targetToken = *planData++;

	if (targetToken != 0xFFu) {
		if (targetToken == 0xFDu) {
			if (g_missionFlightGroups[g_objectTable[g_paiContext.objectIndex].flightGroupIdx]
					.fg.missionPointEnabled[12] != 0) {
				controller->targetObjIdx = 0x800Cu;
			} else {
				controller->targetObjIdx = 0x8000u;
			}
		} else if (targetToken == 0xFEu) {
			if (g_curCraft->wasCaptured != 0 &&
				g_missionFlightGroups[g_objectTable[g_paiContext.objectIndex].flightGroupIdx]
						.fg.missionPointEnabled[12] != 0) {
				controller->targetObjIdx = 0x800Cu;
			} else if (g_missionFlightGroups[g_objectTable[g_paiContext.objectIndex].flightGroupIdx]
						   .fg.missionPointEnabled[13] != 0) {
				controller->targetObjIdx = 0x800Du;
			} else {
				controller->targetObjIdx = 0x8000u;
			}
		} else if (targetToken == 0xF9u) {
			controller->targetObjIdx = 0x800Cu;
		} else {
			if (g_missionFlightGroups[g_objectTable[g_paiContext.objectIndex].flightGroupIdx]
					.fg.missionPointEnabled[controller->waypointIndex] != 0) {
				controller->targetObjIdx = (uint16_t)(0x8000u + controller->waypointIndex);
			} else {
				controller->targetObjIdx = 0x8000u;
			}
		}

		controller->targetSignature = 0;
		controller->hasLiveTarget = 0;
		if (controller->targetObjIdx != UINT16_MAX)
			pai_UpdateAimPointFromOrderTarget();
	}

	controller->aiPlanState = 0;
	maneuverToken = *planData;
	if (maneuverToken != UINT8_MAX) {
		controller->maneuverMode = maneuverToken;
		paiman_initmaneuver();
	}

	g_curCraft->lastAttackerObjIdx = UINT16_MAX;
	g_curCraft->lastHitTimestamp = 0;
	g_curCraft->aiFlight.threatObjIdx = UINT16_MAX;
	controller->thinkTimer = ((objectIdx & 7) * controller->thinkInterval) >> 3;
}

// FUNCTION: XVT 0x402B60
void pai_ProcessPlan(void) {
	AiController* controller;
	uint8_t orderId;

	controller = &g_curCraft->aiController;
	if (g_objectTable[g_paiContext.objectIndex].playerOwnerIdx != -1 &&
		strcmp(g_planTable[controller->currentPlanId].name, "escortldr1pln") == 0) {
		paifight_checkescortorder();
	}

	g_paiSkipToOrder4Checked = 0;
	orderId = *g_paiContext.planCursor++;
	if (orderId == 0)
		return;

	while (g_orderTable[orderId]() == 0 ||
		   strcmp(g_planTable[*g_paiContext.planCursor].name, "nullpln") == 0) {
		++g_paiContext.planCursor;
		orderId = *g_paiContext.planCursor++;
		if (orderId == 0)
			return;
	}

	if (strcmp(g_planTable[*g_paiContext.planCursor].name, "variablepln") == 0)
		controller->pendingPlanId = g_paiContext.nullPlanId;
	else
		controller->pendingPlanId = *g_paiContext.planCursor;

	pai_setupcraftcontext(g_paiContext.objectIndex);
	pai_ApplyPendingPlanTargetAndManeuver(g_paiContext.objectIndex);
	if (g_paiSkipToOrder4Checked == 1)
		g_paiSkipToOrder4Checked = 0;
}

// FUNCTION: XVT 0x402CB0
void pai_setupcraftcontext(uint16_t objectIdx) {
	AiController* controller;
	CraftData* targetCraft;
	ObjectRecord* object;

	g_paiContext.objectIndex = objectIdx;
	object = &g_objectTable[objectIdx];
	g_paiContext.craft = object->mobj->pCraft;
	g_paiContext.leaderObjectIndex = (uint8_t)g_paiContext.craft->leader_obj_idx;
	controller = &g_paiContext.craft->aiController;
	g_paiContext.controller = controller;
	if (g_paiContext.leaderObjectIndex == UINT8_MAX) {
		targetCraft = object->mobj->pCraft;
	} else {
		targetCraft = g_objectTable[g_paiContext.leaderObjectIndex].mobj->pCraft;
	}
	g_paiContext.targetCraft = targetCraft;
	g_paiContext.orderFlightGroupIndex = object->flightGroupIdx;
	g_paiContext.orderSlot = controller->currentOrderSlot;
	Mission_ResolveObjectOrMissionPointWorldLoc(objectIdx, g_paiContext.orderFlightGroupIndex);
	g_paiContext.currentPointX = worldlocx;
	g_paiContext.currentPointY = worldlocy;
	g_paiContext.currentPointZ = worldlocz;
	g_paiContext.skillTier = pai_SkillValueToTier(pai_GetEffectiveSkillValue(g_paiContext.craft));
	g_paiContext.planCursor = g_planDataPtrs[controller->pendingPlanId];
	++g_paiContext.planCursor;
	g_paiContext.initialManeuverId = *g_paiContext.planCursor++;
	g_paiContext.requireLiveOrderTarget = 0;
	g_paiContext.nullPlanId = (uint8_t)pai_findplanbyname("nullpln");
}

// FUNCTION: XVT 0x402E00
int pai_SkillValueToTier(uint16_t skillValue) {
	if (skillValue < 0x8000) {
		return 0;
	}
	return skillValue < 0xC000 ? 1 : 2;
}

// FUNCTION: XVT 0x402E20
uint16_t pai_FindMothershipObject(int16_t mothershipFlightGroupIdx) {
	uint16_t objectIndex;
	CraftData* craft;
	ObjectRecord* object;
	uint8_t objectKind;

	objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
	while (objectIndex < g_activeRegionCraftObjectSlotEnd) {
		object = &g_objectTable[objectIndex];
		if (object->objectType != 0) {
			craft = object->mobj->pCraft;
			objectKind = craft->objectKind;
			if (objectKind != CRAFT_OBJECT_KIND_EXPLODING && objectKind != CRAFT_OBJECT_KIND_BREAKING_UP &&
				object->flightGroupIdx == (uint16_t)mothershipFlightGroupIdx &&
				(uint8_t)craft->leader_obj_idx == UINT8_MAX) {
				return objectIndex;
			}
		}

		++objectIndex;
	}

	return UINT16_MAX;
}

// FUNCTION: XVT 0x402EC0
int pai_IsObjectTargetableNearCurrentPoint(int unused, unsigned int objIdx, int expandRange) {
	int targetable;
	int maxRangeScore;

	(void)unused;
	targetable = pai_IsObjectTargetable(objIdx);

	if (targetable) {
		maxRangeScore =
			(uint16_t)MATH2_fraction(0x500u, g_aiSkillValueQ16ByLevel[g_paiContext.skillTier]) + 2560;
		if (expandRange != 0)
			maxRangeScore += (uint16_t)MATH2_fraction((unsigned int)maxRangeScore, 0x5555u);
		if (pai_IsObjectWithinCurrentPointRange(objIdx, (unsigned int)(maxRangeScore << 8)) == 1)
			return 1;
	}
	return 0;
}

// FUNCTION: XVT 0x403070
int16_t pai_IsObjectWithinCurrentOrderRange(uint16_t objIdx) {
	uint16_t skillRange;

	skillRange = (uint16_t)MATH2_fraction(0x500, g_aiSkillValueQ16ByLevel[g_paiContext.skillTier]);
	return pai_IsObjectWithinCurrentPointRange(objIdx, (skillRange + 0xA00) << 8) == 1;
}

// FUNCTION: XVT 0x403250
int16_t pai_OrderSlotCanBoardTarget(uint16_t orderSlot) {
	return pai_FindBoardingTargetFromOrder(orderSlot) != -1;
}

// FUNCTION: XVT 0x403270
int16_t pai_FindBoardingTargetFromOrder(uint16_t orderSlot) {
	int16_t result;

	result = pai_FindNearestBoardingTarget(
		g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[orderSlot].target1Type,
		g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[orderSlot].target1,
		g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[orderSlot].target1OrTarget2,
		g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[orderSlot].target2Type,
		g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[orderSlot].target2);
	if (result == -1)
		result = pai_FindNearestBoardingTarget(
			g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
				.fg.orders[orderSlot]
				.secondaryTargetTypes[0],
			g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
				.fg.orders[orderSlot]
				.secondaryTargets[0],
			g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[orderSlot].target3OrTarget4,
			g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
				.fg.orders[orderSlot]
				.secondaryTargetTypes[1],
			g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
				.fg.orders[orderSlot]
				.secondaryTargets[1]);
	return result;
}

// FUNCTION: XVT 0x403360
int pai_IsObjectWithinCurrentPointRange(unsigned int objIdx, unsigned int maxRangeScore) {
	ObjectRecord* object;
	int deltaX;
	int deltaY;
	int deltaZ;

	object = &g_objectTable[objIdx];
	deltaX = g_paiContext.currentPointX - object->world_x;
	deltaY = g_paiContext.currentPointY - object->world_y;
	deltaZ = g_paiContext.currentPointZ - object->world_z;
	if (deltaX < 0)
		deltaX = (int)(0u - (unsigned int)deltaX);
	if (deltaY < 0)
		deltaY = (int)(0u - (unsigned int)deltaY);
	if (deltaZ < 0)
		deltaZ = (int)(0u - (unsigned int)deltaZ);

	if (deltaY < deltaX)
		g_targetRangeScore = deltaX + (deltaY >> 1);
	else
		g_targetRangeScore = deltaY + (deltaX >> 1);

	if (g_targetRangeScore > deltaZ)
		deltaZ >>= 1;
	else
		g_targetRangeScore >>= 1;
	g_targetRangeScore += deltaZ;

	return g_targetRangeScore < (int)maxRangeScore;
}

// FUNCTION: XVT 0x403400
void pai_UpdateAimPointFromOrderTarget(void) {
	Mission_ResolveObjectOrMissionPointWorldLoc(g_paiContext.controller->targetObjIdx,
												g_objectTable[g_paiContext.objectIndex].flightGroupIdx);
	g_paiContext.controller->aimPointX = worldlocx;
	g_paiContext.controller->aimPointY = worldlocy;
	g_paiContext.controller->aimPointZ = worldlocz;
}

// FUNCTION: XVT 0x403470
void pai_SetFlightGroupFormation(unsigned int flightGroupIdx, unsigned int formationType,
								 unsigned int formationSpacing) {
	unsigned int objectIndex;
	ObjectRecord* object;
	CraftData* craft;

	for (objectIndex = (unsigned int)g_activeRegionObjectSlotStart;
		 objectIndex < (unsigned int)g_activeRegionCraftObjectSlotEnd; ++objectIndex) {
		object = &g_objectTable[objectIndex];
		if (object->objectType != 0 && object->flightGroupIdx == flightGroupIdx) {
			craft = object->mobj->pCraft;
			craft->aiFlight.formationType = formationType;
			craft->aiFlight.separation = formationSpacing;
		}
	}
}

// FUNCTION: XVT 0x4034E0
void pai_ObjectRefDirectionToObjectRef(unsigned int fromRef, unsigned int toRef) {
	int targetX;
	int targetY;
	int targetZ;

	Mission_ResolveObjectOrMissionPointWorldLoc(toRef, 0);
	targetX = worldlocx;
	targetY = worldlocy;
	targetZ = worldlocz;

	Mission_ResolveObjectOrMissionPointWorldLoc(fromRef, 0);
	targetX = targetX - worldlocx;
	targetY = targetY - worldlocy;
	targetZ = targetZ - worldlocz;
	trig2_ctop(targetX, targetY, targetZ);
}

// FUNCTION: XVT 0x403540
void pai_ObjectRefUpdateApproxRangeScore(unsigned int fromRef, unsigned int toRef) {
	int deltaX;
	int deltaY;
	int deltaZ;
	int xyScore;

	Mission_ResolveObjectOrMissionPointWorldLoc(fromRef, 0);
	deltaX = worldlocx;
	deltaY = worldlocy;
	deltaZ = worldlocz;

	Mission_ResolveObjectOrMissionPointWorldLoc(toRef, 0);
	deltaX -= worldlocx;
	deltaY -= worldlocy;
	deltaZ -= worldlocz;

	if (deltaX < 0)
		deltaX = -deltaX;
	if (deltaY < 0)
		deltaY = -deltaY;
	if (deltaZ < 0)
		deltaZ = -deltaZ;

	if (deltaX > deltaY)
		xyScore = deltaX + (deltaY >> 1);
	else
		xyScore = deltaY + (deltaX >> 1);

	if (xyScore > deltaZ) {
		g_targetRangeScore = xyScore + (deltaZ >> 1);
	} else {
		g_targetRangeScore = xyScore;
		g_targetRangeScore >>= 1;
		g_targetRangeScore += deltaZ;
	}
}

// FUNCTION: XVT 0x4035D0
void pai_calcrotatedpoint(ObjectRecord* obj, int16_t sideArg, int16_t upArg, int16_t fwdArg) {
	int result;

	/* Rotate local coordinates through the cached Q15 orientation basis. */
	if (obj->mobj->orientMatrixDirty != 0) {
		FVIEW_calcrotatemove(obj->pitch, obj->yaw, obj);
		FVIEW_calcrotateorient(obj->roll, 0, obj);
	}

	g_rotatedX = Math_MulQ15(sideArg, obj->mobj->cachedSideX);
	result = Math_MulQ15(upArg, obj->mobj->cachedUpX);
	g_rotatedX += result;
	g_rotatedX += Math_MulQ15(fwdArg, obj->mobj->cachedFwdX);
	g_rotatedY = Math_MulQ15(sideArg, obj->mobj->cachedSideY);
	g_rotatedY += Math_MulQ15(upArg, obj->mobj->cachedUpY);
	g_rotatedY += Math_MulQ15(fwdArg, obj->mobj->cachedFwdY);
	g_rotatedZ = Math_MulQ15(sideArg, obj->mobj->cachedSideZ);
	g_rotatedZ += Math_MulQ15(upArg, obj->mobj->cachedUpZ);
	g_rotatedZ += Math_MulQ15(fwdArg, obj->mobj->cachedFwdZ);
}

// FUNCTION: XVT 0x4037B0
void pai_RotateLocalVectorToWorldScratch(ObjectRecord* objRecord, int localSide, int localUp, int localFwd) {
	int result;

	if (objRecord->mobj->orientMatrixDirty != 0) {
		FVIEW_calcrotatemove(objRecord->pitch, objRecord->yaw, objRecord);
		FVIEW_calcrotateorient(objRecord->roll, 0, objRecord);
	}

	g_rotatedX = Math_MulQ15(localSide, objRecord->mobj->cachedSideX);
	result = Math_MulQ15(localUp, objRecord->mobj->cachedUpX);
	g_rotatedX += result;
	g_rotatedX += Math_MulQ15(localFwd, objRecord->mobj->cachedFwdX);
	g_rotatedY = Math_MulQ15(localSide, objRecord->mobj->cachedSideY);
	g_rotatedY += Math_MulQ15(localUp, objRecord->mobj->cachedUpY);
	g_rotatedY += Math_MulQ15(localFwd, objRecord->mobj->cachedFwdY);
	g_rotatedZ = Math_MulQ15(localSide, objRecord->mobj->cachedSideZ);
	g_rotatedZ += Math_MulQ15(localUp, objRecord->mobj->cachedUpZ);
	g_rotatedZ += Math_MulQ15(localFwd, objRecord->mobj->cachedFwdZ);
}

// FUNCTION: XVT 0x403990
void pai_CalcAnglesToAimPoint(void) {
	ObjectRecord* object;

	object = &g_objectTable[g_paiContext.objectIndex];
	trig2_ctop(g_paiContext.controller->aimPointX - object->world_x,
			   g_paiContext.controller->aimPointY - object->world_y,
			   g_paiContext.controller->aimPointZ - object->world_z);
}

// FUNCTION: XVT 0x4039E0
int16_t pai_FindNearestBoardingTarget(uint16_t target1Type, uint16_t target1, int16_t targetOrMode,
									  uint16_t target2Type, uint16_t target2) {
	uint16_t objectIdx;
	uint16_t nearestObject = UINT16_MAX;
	unsigned int nearestRange = UINT_MAX;

	for (objectIdx = (uint16_t)g_activeRegionObjectSlotStart; objectIdx < g_activeRegionCraftObjectSlotEnd;
		 ++objectIdx) {
		int16_t firstMatch;
		int16_t secondMatch;
		int16_t count;
		CraftData* craft;
		ObjectRecord* object;
		AiController* controller;
		const char* planName;

		if (g_objectTable[objectIdx].objectType == 0)
			continue;
		firstMatch = Mission_ObjectMatchesTriggerVariable(objectIdx, target1Type, target1);
		secondMatch = Mission_ObjectMatchesTriggerVariable(objectIdx, target2Type, target2);
		if (targetOrMode == 1)
			firstMatch |= secondMatch;
		else
			firstMatch &= secondMatch;
		if (firstMatch == 0)
			continue;

		count = 0;
		object = &g_objectTable[objectIdx];
		craft = g_objectTable[objectIdx].mobj->pCraft;
		controller = &craft->aiController;
		planName = g_planTable[controller->currentPlanId].name;
		if (strcmp(planName, "nullpln") == 0 || strcmp(planName, "stationaryldrpln") == 0 ||
			strcmp(planName, "stationaryflwpln") == 0 || object->genusId == CRAFT_GENUS_PLATFORM) {
			count = 1;
		} else if (strcmp(g_planTable[g_paiContext.controller->currentPlanId].name, "boardtocapturepln") ==
					   0 ||
				   strcmp(g_planTable[g_paiContext.controller->currentPlanId].name, "boardtodestroypln") ==
					   0) {
			if (craft->workingSubsystems == 0)
				count = 1;
		} else if (craft->workingSubsystems == 0 ||
				   controller->maneuverMode == AI_MANEUVER_MODE_AWAIT_BOARD ||
				   controller->maneuverMode == AI_MANEUVER_MODE_STOP) {
			count = 1;
		}

		if (count != 0) {
			uint16_t otherIdx;
			uint16_t sigIdx;

			count = 0;
			for (otherIdx = (uint16_t)g_activeRegionObjectSlotStart;
				 otherIdx < g_activeRegionCraftObjectSlotEnd; ++otherIdx) {
				ObjectRecord* other = &g_objectTable[otherIdx];
				if (other->objectType != 0 && otherIdx != g_paiContext.objectIndex) {
					CraftData* otherCraft;
					AiController* otherController;
					int currentPlanId;

					otherCraft = other->mobj->pCraft;
					currentPlanId = otherCraft->aiController.currentPlanId;
					otherController = &otherCraft->aiController;
					if (strcmp(g_planTable[currentPlanId].name, "boardtogivepln") == 0 ||
						strcmp(g_planTable[currentPlanId].name, "boardtotakepln") == 0 ||
						strcmp(g_planTable[currentPlanId].name, "boardtoexchangepln") == 0 ||
						strcmp(g_planTable[currentPlanId].name, "boardtocapturepln") == 0 ||
						strcmp(g_planTable[currentPlanId].name, "boardtodestroypln") == 0 ||
						strcmp(g_planTable[currentPlanId].name, "boardtopickuppln") == 0 ||
						strcmp(g_planTable[currentPlanId].name, "boardtocontactpln") == 0 ||
						strcmp(g_planTable[currentPlanId].name, "boardtorepairpln") == 0) {
						if (otherController->targetObjIdx == objectIdx)
							++count;
						else if (otherCraft->carriedObjectIndex == objectIdx)
							++count;
					}
				}
			}
			for (sigIdx = 0; sigIdx < g_curCraft->aiFlight.objSignatureCount; ++sigIdx)
				if (g_curCraft->aiFlight.objSignatures[sigIdx] == object->objectSignature)
					++count;
			if (count == 0) {
				pai_ObjectRefUpdateApproxRangeScore(g_paiContext.objectIndex, objectIdx);
				if ((unsigned int)g_targetRangeScore >= nearestRange)
					continue;
				nearestRange = g_targetRangeScore;
				nearestObject = objectIdx;
			}
		}
	}

	for (objectIdx = (uint16_t)g_regionMainObjectSlotEnd;
		 (int)(g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount) > objectIdx; ++objectIdx) {
		int16_t firstMatch;
		int16_t secondMatch;
		ObjectRecord* object = &g_objectTable[objectIdx];
		uint16_t objectType = object->objectType;

		if (objectType != 0 && (g_modelTypeTable[objectType].flags & 2) != 0) {
			firstMatch =
				Mission_FlightGroupMatchesTriggerVariable(object->flightGroupIdx, target1Type, target1);
			secondMatch = Mission_FlightGroupMatchesTriggerVariable(g_objectTable[objectIdx].flightGroupIdx,
																	target2Type, target2);
			if (targetOrMode == 1)
				firstMatch |= secondMatch;
			else
				firstMatch &= secondMatch;
			if (firstMatch != 0) {
				int16_t reservedCount = 0;
				uint16_t otherIdx;
				uint16_t sigIdx;

				for (otherIdx = (uint16_t)g_activeRegionObjectSlotStart;
					 otherIdx < g_activeRegionCraftObjectSlotEnd; ++otherIdx) {
					ObjectRecord* other = &g_objectTable[otherIdx];
					if (other->objectType != 0 && otherIdx != g_paiContext.objectIndex) {
						int currentPlanId;
						AiController* otherController;

						otherController = &other->mobj->pCraft->aiController;
						currentPlanId = otherController->currentPlanId;
						if ((strcmp(g_planTable[currentPlanId].name, "boardtogivepln") == 0 ||
							 strcmp(g_planTable[currentPlanId].name, "boardtotakepln") == 0 ||
							 strcmp(g_planTable[currentPlanId].name, "boardtoexchangepln") == 0 ||
							 strcmp(g_planTable[currentPlanId].name, "boardtocapturepln") == 0 ||
							 strcmp(g_planTable[currentPlanId].name, "boardtodestroypln") == 0 ||
							 strcmp(g_planTable[currentPlanId].name, "boardtocontactpln") == 0 ||
							 strcmp(g_planTable[currentPlanId].name, "boardtorepairpln") == 0) &&
							otherController->targetObjIdx == objectIdx)
							++reservedCount;
					}
				}
				{
					uint8_t signatureCount;

					sigIdx = 0;
					signatureCount = g_curCraft->aiFlight.objSignatureCount;
					if (signatureCount != 0) {
						do {
							if (g_curCraft->aiFlight.objSignatures[sigIdx] ==
								g_objectTable[objectIdx].objectSignature)
								++reservedCount;
							++sigIdx;
						} while (sigIdx < signatureCount);
					}
				}
				if (reservedCount == 0) {
					pai_ObjectRefUpdateApproxRangeScore(g_paiContext.objectIndex, objectIdx);
					if ((unsigned int)g_targetRangeScore >= nearestRange)
						continue;
					nearestRange = g_targetRangeScore;
					nearestObject = objectIdx;
				}
			}
		}
	}
	return (int16_t)nearestObject;
}

// FUNCTION: XVT 0x403FE0
int16_t pai_IsPlanCompleteForOrderSlot(uint16_t planId, uint16_t orderSlot) {
	int16_t result;

	result = 0;
	if (strcmp(g_planTable[planId].name, "formldr1pln") == 0 ||
		strcmp(g_planTable[planId].name, "formevadeldr1pln") == 0 ||
		strcmp(g_planTable[planId].name, "starshipformpln") == 0 ||
		strcmp(g_planTable[planId].name, "rendezvous1pln") == 0 ||
		strcmp(g_planTable[planId].name, "disabledpln") == 0 ||
		strcmp(g_planTable[planId].name, "waitforboardpln") == 0) {
		if (g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[orderSlot].variable1 <=
			g_paiContext.controller->orderScratch.goalProgress[orderSlot]) {
			result = 1;
		}
	} else if (strcmp(g_planTable[planId].name, "capfreeldr1pln") == 0 ||
			   strcmp(g_planTable[planId].name, "capescortersldr1pln") == 0 ||
			   strcmp(g_planTable[planId].name, "caprespondldr1pln") == 0 ||
			   strcmp(g_planTable[planId].name, "escortldr1pln") == 0 ||
			   strcmp(g_planTable[planId].name, "disableldr1pln") == 0 ||
			   strcmp(g_planTable[planId].name, "starshipprotectpln") == 0 ||
			   strcmp(g_planTable[planId].name, "starshipattackpln") == 0 ||
			   strcmp(g_planTable[planId].name, "starshipdisablepln") == 0) {
		if (paifight_OrderSlotHasRemainingTargets(orderSlot) == 0 &&
			paifight_OrderSlotHasFutureTargets(orderSlot) == 0) {
			result = 1;
		}
	} else if (strcmp(g_planTable[planId].name, "boardtogivepln") == 0 ||
			   strcmp(g_planTable[planId].name, "boardtotakepln") == 0 ||
			   strcmp(g_planTable[planId].name, "boardtoexchangepln") == 0 ||
			   strcmp(g_planTable[planId].name, "boardtocapturepln") == 0 ||
			   strcmp(g_planTable[planId].name, "boardtodestroypln") == 0 ||
			   strcmp(g_planTable[planId].name, "boardtopickuppln") == 0 ||
			   strcmp(g_planTable[planId].name, "boardtocontactpln") == 0 ||
			   strcmp(g_planTable[planId].name, "boardtorepairpln") == 0) {
		if (g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[orderSlot].variable2 <=
			g_paiContext.controller->orderScratch.goalProgress[orderSlot]) {
			result = 1;
		}
	} else if (strcmp(g_planTable[planId].name, "dropoffldr1pln") == 0) {
		if ((unsigned int)
				g_missionFgStats[(uint16_t)(g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
												.fg.orders[orderSlot]
												.variable2 -
											1)]
					.outcomeCount[FLIGHT_GROUP_OUTCOME_TOTAL] <=
			g_paiContext.controller->orderScratch.goalProgress[orderSlot]) {
			result = 1;
		}
	} else if (strcmp(g_planTable[planId].name, "waitpln") == 0) {
		if (g_paiContext.controller->maneuverTimer == 0)
			result = 1;
	} else if (strcmp(g_planTable[planId].name, "starshipwaitreturnpln") == 0) {
		if (paiorder_waitforallreturnorder() != 0)
			result = 1;
	} else if (strcmp(g_planTable[planId].name, "starshipwaitcreatepln") == 0 &&
			   paiorder_waitforallcreateorder() != 0) {
		result = 1;
	}
	return result;
}

// FUNCTION: XVT 0x404380
int16_t pai_IsBoardingPlanCompleteForOrderSlot(uint16_t planId, uint16_t orderSlot) {
	int16_t result;

	result = 0;
	if ((strcmp(g_planTable[planId].name, "boardtogivepln") == 0 ||
		 strcmp(g_planTable[planId].name, "boardtotakepln") == 0 ||
		 strcmp(g_planTable[planId].name, "boardtoexchangepln") == 0 ||
		 strcmp(g_planTable[planId].name, "boardtocapturepln") == 0 ||
		 strcmp(g_planTable[planId].name, "boardtodestroypln") == 0 ||
		 strcmp(g_planTable[planId].name, "boardtopickuppln") == 0 ||
		 strcmp(g_planTable[planId].name, "boardtocontactpln") == 0 ||
		 strcmp(g_planTable[planId].name, "boardtorepairpln") == 0) &&
		paifight_OrderSlotHasRemainingTargets(orderSlot) == 0 &&
		paifight_OrderSlotHasFutureTargets(orderSlot) == 0) {
		result = 1;
	}
	return result;
}

// FUNCTION: XVT 0x404450
int16_t pai_CurrentOrderTargetsMatchObject(uint16_t objectIdx) {
	int16_t primaryMatch;
	int16_t secondaryMatch;
	int16_t match;

	primaryMatch = Mission_ObjectMatchesTriggerVariable(
		objectIdx,
		g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
			.fg.orders[g_paiContext.orderSlot]
			.target1Type,
		g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[g_paiContext.orderSlot].target1);
	match = Mission_ObjectMatchesTriggerVariable(
		objectIdx,
		g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
			.fg.orders[g_paiContext.orderSlot]
			.target2Type,
		g_missionFlightGroups[g_paiContext.orderFlightGroupIndex].fg.orders[g_paiContext.orderSlot].target2);
	if (g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
			.fg.orders[g_paiContext.orderSlot]
			.target1OrTarget2 == 1)
		primaryMatch |= match;
	else
		primaryMatch &= match;

	secondaryMatch =
		Mission_ObjectMatchesTriggerVariable(objectIdx,
											 g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
												 .fg.orders[g_paiContext.orderSlot]
												 .secondaryTargetTypes[0],
											 g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
												 .fg.orders[g_paiContext.orderSlot]
												 .secondaryTargets[0]);
	match = Mission_ObjectMatchesTriggerVariable(objectIdx,
												 g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
													 .fg.orders[g_paiContext.orderSlot]
													 .secondaryTargetTypes[1],
												 g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
													 .fg.orders[g_paiContext.orderSlot]
													 .secondaryTargets[1]);
	if (g_missionFlightGroups[g_paiContext.orderFlightGroupIndex]
			.fg.orders[g_paiContext.orderSlot]
			.target3OrTarget4 == 1)
		secondaryMatch |= match;
	else
		secondaryMatch &= match;

	return primaryMatch || secondaryMatch;
}

// FUNCTION: XVT 0x404620
uint16_t pai_GetEffectiveSkillValue(CraftData* craft) {
	ObjectRecord* linkedObject;

	linkedObject = craft->effectiveAiObjectLink;
	if (linkedObject == 0) {
		return craft->aiSkill;
	}

	if (linkedObject->objectSignature != craft->effectiveAiObjectSignature) {
		craft->effectiveAiObjectLink = 0;
		return craft->aiSkill;
	}

	if (linkedObject->mobj == 0) {
		return craft->aiSkill;
	}
	if (linkedObject->mobj->pCharData != 0) {
		return linkedObject->mobj->pCharData->skillValue;
	}

	return craft->aiSkill;
}

// FUNCTION: XVT 0x404670
int pai_OrderSlotMatchingObjectHasOrderClass(int objectIdx, int orderClass, int targetObjIdx) {
	unsigned int orderSlot;

	if (objectIdx == -1)
		return 0;
	pai_setupcraftcontext(objectIdx);
	for (orderSlot = 0; orderSlot < 3; ++orderSlot) {
		uint8_t order =
			g_missionFlightGroups[g_objectTable[objectIdx].flightGroupIdx].fg.orders[orderSlot].order;
		if (g_orderLeaderBuiltinPlanNameIndex[order] == orderClass) {
			g_paiContext.orderSlot = (uint16_t)orderSlot;
			if (pai_CurrentOrderTargetsMatchObject(targetObjIdx) != 0)
				return 1;
		}
	}
	return 0;
}

// FUNCTION: XVT 0x46AC80
int pai_FindPlanTableIndexByName(const char* planName) {
	const PaiPlanRecord* plan = g_planTable;
	unsigned int planIndex;

	for (planIndex = 0; planIndex < 256; ++plan, ++planIndex) {
		if (strncmp(plan->name, planName, sizeof(plan->name)) == 0) {
			break;
		}
	}
	return planIndex;
}

// FUNCTION: XVT 0x46ACB0
int pai_FindFreePlanTableIndex(void) {
	int planIndex;

	for (planIndex = 0; planIndex < 256; ++planIndex) {
		if (g_planTable[planIndex].name[0] == '\0') {
			break;
		}
	}
	return planIndex;
}

// FUNCTION: XVT 0x46ACD0
int pai_FindTargetTokenIndex(const char* token) {
	int tokenIndex = 0;

	if (g_paiTargetTokenDefs[0].name[0] != '\0') {
		struct PaiPlanTokenDef* tokenDef = g_paiTargetTokenDefs;

		do {
			if (strcmp(tokenDef->name, token) == 0) {
				break;
			}
			++tokenDef;
			++tokenIndex;
		} while (tokenDef->name[0] != '\0');
	}

	return tokenIndex;
}

// FUNCTION: XVT 0x46AD30
int pai_FindManeuverTokenIndex(const char* token) {
	int tokenIndex = 0;

	if (g_paiManeuverTokenDefs[0].name[0] != '\0') {
		struct PaiPlanTokenDef* tokenDef = g_paiManeuverTokenDefs;

		do {
			if (strcmp(tokenDef->name, token) == 0) {
				break;
			}
			++tokenDef;
			++tokenIndex;
		} while (tokenDef->name[0] != '\0');
	}

	return tokenIndex;
}

// FUNCTION: XVT 0x46AD90
int pai_FindOrderTokenIndex(const char* token) {
	int tokenIndex = 0;

	if (g_paiOrderTokenDefs[0].name[0] != '\0') {
		struct PaiPlanTokenDef* tokenDef = g_paiOrderTokenDefs;

		do {
			if (strcmp(tokenDef->name, token) == 0) {
				break;
			}
			++tokenDef;
			++tokenIndex;
		} while (tokenDef->name[0] != '\0');
	}

	return tokenIndex;
}

// FUNCTION: XVT 0x46ADF0
int pai_ReadPlanTextToken(char* token, XvtFile* stream) {
	int tokenIndex = 1;
	char buffer;

	*token = '\0';
	for (;;) {
		do {
			if (File_RawRead(&buffer, 1, 1, stream) != 1)
				return 1;
		} while (buffer == ' ' || buffer == '\t' || buffer == '\n' || buffer == ',' || buffer == '\n');

		if (buffer != ';')
			break;

		do {
			if (File_RawRead(&buffer, 1, 1, stream) != 1)
				return 1;
		} while (buffer != '\n');
	}

	*token = buffer;
	for (;;) {
		if (File_RawRead(&buffer, 1, 1, stream) != 1) {
			token[tokenIndex] = '\0';
			return 1;
		}
		if (buffer == ',' || buffer == '\n' || buffer == ' ' || buffer == '\t' || buffer == '\n')
			break;
		token[tokenIndex] = buffer;
		++tokenIndex;
	}
	token[tokenIndex] = '\0';
	return 1;
}

// FUNCTION: XVT 0x46AED0
int pai_CompilePlansFromText(const char* baseName) {
	char fileName[256];
	char token[256];
	XvtFile* stream;
	uint8_t* cursor;
	int buffer;

	strcpy(fileName, baseName);
	strcat(fileName, ".pln");
	File_OpenGlobalStream(fileName, "r", 0, 0);
	stream = (XvtFile*)g_stream;
	if (stream == NULL)
		return 0;

	cursor = g_planOrderData;
	for (;;) {
		int targetIndex;
		int maneuverIndex;

		if (pai_ReadPlanTextToken(token, stream) == 0) {
			File_RawClose(stream);
			return 0;
		}
		if (token[0] == '*')
			break;

		buffer = pai_FindPlanTableIndexByName(token);
		if (buffer != 256) {
			if (g_planTable[buffer].isDefined == 1) {
				File_RawClose(stream);
				return 0;
			}
		} else {
			buffer = pai_FindFreePlanTableIndex();
			if (buffer == 256) {
				File_RawClose(stream);
				return 0;
			}
		}

		strncpy(g_planTable[buffer].name, token, sizeof(g_planTable[buffer].name));
		g_planTable[buffer].name[79] = '\0';
		g_planTable[buffer].isDefined = 1;
		g_planTable[buffer].dataOffset = (uint32_t)(cursor - g_planOrderData);
		g_planDataPtrs[buffer] = cursor;

		if (pai_ReadPlanTextToken(token, stream) == 0) {
			File_RawClose(stream);
			return 0;
		}
		targetIndex = pai_FindTargetTokenIndex(token);
		if (g_paiTargetTokenDefs[targetIndex].name[0] == '\0') {
			File_RawClose(stream);
			return 0;
		}
		*cursor++ = (uint8_t)g_paiTargetTokenDefs[targetIndex].value;

		if (pai_ReadPlanTextToken(token, stream) == 0) {
			File_RawClose(stream);
			return 0;
		}
		maneuverIndex = pai_FindManeuverTokenIndex(token);
		if (g_paiManeuverTokenDefs[maneuverIndex].name[0] == '\0') {
			File_RawClose(stream);
			return 0;
		}
		*cursor++ = (uint8_t)g_paiManeuverTokenDefs[maneuverIndex].value;

		for (;;) {
			int orderIndex;
			int nextPlanId;
			int freePlanId;

			if (pai_ReadPlanTextToken(token, stream) == 0) {
				File_RawClose(stream);
				return 0;
			}
			orderIndex = pai_FindOrderTokenIndex(token);
			if (g_paiOrderTokenDefs[orderIndex].name[0] == '\0') {
				File_RawClose(stream);
				return 0;
			}

			*cursor++ = (uint8_t)g_paiOrderTokenDefs[orderIndex].value;
			if (orderIndex == 0) {
				++g_planCount;
				break;
			}

			if (pai_ReadPlanTextToken(token, stream) == 0) {
				File_RawClose(stream);
				return 0;
			}
			nextPlanId = pai_FindPlanTableIndexByName(token);
			if (nextPlanId != 256) {
				*cursor++ = (uint8_t)nextPlanId;
				continue;
			}

			freePlanId = pai_FindFreePlanTableIndex();
			if (freePlanId == 256) {
				File_RawClose(stream);
				return 0;
			}
			strncpy(g_planTable[freePlanId].name, token, sizeof(g_planTable[freePlanId].name));
			g_planTable[freePlanId].name[79] = '\0';
			g_planTable[freePlanId].isDefined = 0;
			*cursor++ = (uint8_t)freePlanId;
		}
	}

	File_RawClose(stream);
	for (buffer = 0; buffer < 256; ++buffer) {
		if (g_planTable[buffer].name[0] != '\0' && g_planTable[buffer].isDefined != 1)
			return 0;
	}

	strcpy(fileName, baseName);
	strcat(fileName, ".plo");
	File_OpenGlobalStream(fileName, "wb", 0, 1);
	stream = (XvtFile*)g_stream;
	if (stream != NULL) {
		buffer = (int)sizeof(g_planTable);
		File_RawWrite(&buffer, sizeof(buffer), 1, stream);
		File_RawWrite(g_planTable, (size_t)buffer, 1, stream);
		buffer = 0xFFFF;
		File_RawWrite(&buffer, sizeof(buffer), 1, stream);
		File_RawWrite(g_planOrderData, (size_t)buffer, 1, stream);
		File_RawClose(stream);
	}

	return 1;
}

// FUNCTION: XVT 0x46B3D0
int pai_loadplans(char* baseName) {
	char fileName[256];
	uint32_t bufferSize;
	XvtFile* stream;
	int planIndex;
	int planCount;

	strcpy(fileName, baseName);
	strcat(fileName, ".plo");
	memset(g_planTable, 0, sizeof(g_planTable));
	g_planCount = 0;
	memset(g_planDataPtrs, 0, 0x100);
	File_OpenGlobalStream(fileName, g_fileModeReadBinary, 0, 1);
	stream = (XvtFile*)g_stream;
	if (stream == NULL)
		return pai_CompilePlansFromText(baseName);

	if (File_RawRead(&bufferSize, sizeof(bufferSize), 1, stream) != 1) {
		File_RawClose(stream);
		return pai_CompilePlansFromText(baseName);
	}
	if (File_RawRead(g_planTable, bufferSize, 1, stream) != 1) {
		File_RawClose(stream);
		return pai_CompilePlansFromText(baseName);
	}
	if (File_RawRead(&bufferSize, sizeof(bufferSize), 1, stream) != 1) {
		File_RawClose(stream);
		return pai_CompilePlansFromText(baseName);
	}
	if (File_RawRead(g_planOrderData, bufferSize, 1, stream) != 1) {
		File_RawClose(stream);
		return pai_CompilePlansFromText(baseName);
	}

	File_RawClose(stream);
	planIndex = 0;
	planCount = g_planCount;
	do {
		if (g_planTable[planIndex].name[0] != '\0') {
			++planCount;
			g_planDataPtrs[planIndex] = &g_planOrderData[g_planTable[planIndex].dataOffset];
		}
		g_planCount = planCount;
		++planIndex;
	} while (planIndex < 256);

	return 1;
}

// FUNCTION: XVT 0x46B5B0
void pai_cacheBuiltinPlanIds(void) {
	int planNameOrdinal;
	const char* const* planNameCursor;
	const char* planName;

	planName = g_builtinPlanNameTable[0];
	planNameOrdinal = 0;
	if (*planName != '\0') {
		planNameCursor = g_builtinPlanNameTable;
		do {
			planNameCursor++;
			g_builtinPlanIdByNameIndex[planNameOrdinal++] = (uint8_t)pai_FindPlanTableIndexByName(planName);
			planName = *planNameCursor;
		} while (*planName != '\0');
	}
}

// FUNCTION: XVT 0x46B5F0
uint8_t* pai_getplandataptrbyname(const char* planName) {
	return g_planDataPtrs[pai_FindPlanTableIndexByName(planName)];
}

// FUNCTION: XVT 0x46B610
int pai_findplanbyname(const char* planName) {
	int planIndex;

	for (planIndex = 0; planIndex < 256; ++planIndex) {
		if (strncmp(g_planTable[planIndex].name, planName, sizeof(g_planTable[planIndex].name)) == 0) {
			return planIndex;
		}
	}
	return 0;
}
