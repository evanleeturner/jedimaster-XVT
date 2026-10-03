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

/* The loaded AI plans; a plan id is an index into it. Each entry holds the
 * plan's name, whether the plan text defined it, and where its bytes start in
 * g_planOrderData. pai_loadplans clears it and fills it from the .plo file, or
 * pai_CompilePlansFromText fills it from the .pln text. The world state saves
 * and restores it: Flight_SaveWorldState and Flight_RestoreWorldState in the
 * original build, XvtSnapshot_Encode and XvtSnapshot_DecodePrefix in the modern
 * one. */
// GLOBAL: XVT 0x9D1320
PaiPlanRecord g_planTable[256];

/* The craft the AI is thinking for and values taken from it, shared by the
 * plan, order and maneuver code. pai_setupcraftcontext fills it for one craft;
 * the order and target search functions then change some of its fields. Many
 * functions write it, chiefly pai_setupcraftcontext. */
// GLOBAL: XVT 0x9A73B0
PaiContext g_paiContext;
/* 1 once paiorder_completegootherorder or paiorder_orderswitchorder has tested
 * the flight group's skip-to-order-4 trigger pair during the current
 * pai_ProcessPlan pass and found it false; both skip the test while it is 1.
 * pai_ProcessPlan sets it to 0 when a pass starts and when a pass switches the
 * plan. */
// GLOBAL: XVT 0x9A8C28
int g_paiSkipToOrder4Checked = 0;
/* The last rough distance a range test worked out, in world units; callers read
 * it right after the call that sets it. pai_ObjectRefUpdateRoughDistance and
 * pai_IsObjectWithinRangeOfCraft take the larger of the X and Y offsets plus
 * half the smaller, then add half the Z offset when that sum is the larger,
 * else halve the sum and add the Z offset. Many functions write it, chiefly
 * those two. */
// GLOBAL: XVT 0x9A1FF8
int g_lastRoughDistance = 0;
/* AI skill as a fraction of 65,536 for each flight group AI level:
 * Mission_InitFlightGroupObjectSlot copies the entry for g_spawnGroupAI into a
 * new craft's aiSkill. Levels 4 and 5 hold 0xFFFF, which MATH2_fraction treats
 * as a whole; entries 6 and 7 are 0. pai_IsObjectTargetableNearCraft and
 * pai_IsObjectWithinSkillRangeOfCraft index it by the skill tier, 0 to 2,
 * instead. Nothing writes it. */
// GLOBAL: XVT 0x524100
uint16_t g_aiSkillValueQ16ByLevel[8] = {0x0000, 0x4000, 0x8000, 0xC000,
					0xFFFF, 0xFFFF, 0x0000, 0x0000};
/* Ticks between two AI thinks of a craft for each flight group AI level, from
 * 708 down to 29; Mission_InitFlightGroupObjectSlot copies the entry into the
 * craft's thinkInterval. Entries 6 and 7 are 0. */
// GLOBAL: XVT 0x524110
const uint16_t g_aiThinkIntervalBySkill[8] = {
	708, 472, SIMULATION_TICKS_PER_SECOND, 118, 59, 29, 0, 0};
/* For each plan id from 0 to 73, the index in g_strInFlightMessages of the
 * status line a craft running that plan reports; Flight_ProcessPlayerActions
 * and the HUD's target displays read it. */
// GLOBAL: XVT 0x5272F8
const uint8_t g_planReportMessageIdByPlanId[PAI_PLAN_REPORT_MESSAGE_COUNT] = {
	159, 159, 159, 183, 178, 186, 183, 178, 183, 178, 183, 178, 162,
	164, 166, 163, 162, 165, 165, 167, 164, 166, 165, 165, 168, 169,
	164, 166, 165, 169, 164, 166, 165, 170, 173, 170, 170, 171, 170,
	172, 170, 174, 189, 189, 174, 175, 174, 176, 177, 187, 181, 179,
	180, 182, 181, 159, 178, 183, 183, 184, 185, 161, 161, 183, 183,
	187, 188, 190, 170, 162, 191, 191, 191, 0,
};
/* The 75 plan names the code refers to by number, in that order, ended by an
 * empty name. Only pai_cacheBuiltinPlanIds reads it. */
// GLOBAL: XVT 0x525150
const char *const g_builtinPlanNameTable[76] = {
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
/* The target tokens of the plan text and the byte each compiles to, ended by an
 * empty name. NOTARGET compiles to 255, as NULLTARGET does. Only
 * pai_FindTargetTokenIndex and pai_CompilePlansFromText read it. */
// GLOBAL: XVT 0x525280
struct PaiPlanTokenDef g_paiTargetTokenDefs[10] = {
	{"LOCATARGET", 249},
	{"LOCBTARGET", 250},
	{"ABORTTARGET", AI_TARGET_ABORT},
	{"NORMALTARGET", 252},
	{"PRIMARYTARGET", 253},
	{"HOMETARGET", 254},
	{"NULLTARGET", 255},
	{"NOTARGET", -1},
	{"0x80", 128},
	{"", 0},
};
/* The maneuver tokens of the plan text and the maneuver mode each compiles to,
 * ended by an empty name. No token names AI_MANEUVER_MODE_AVOID_ATTACKER or
 * AI_MANEUVER_MODE_DODGE. Only pai_FindManeuverTokenIndex and
 * pai_CompilePlansFromText read it. */
// GLOBAL: XVT 0x5255B8
struct PaiPlanTokenDef g_paiManeuverTokenDefs[33] = {
	{"NULLMANR", AI_MANEUVER_MODE_NULL},
	{"TURNINSIDEMANR", AI_MANEUVER_MODE_TURN_INSIDE},
	{"SPLITSMANR", AI_MANEUVER_MODE_SPLITS},
	{"IMMELMANNMANR", AI_MANEUVER_MODE_IMMELMANN},
	{"SCISSORSMANR", AI_MANEUVER_MODE_SCISSORS},
	{"RENDEZVOUSMANR", AI_MANEUVER_MODE_RENDEZVOUS},
	{"CRUISEMANR", AI_MANEUVER_MODE_CRUISE},
	{"HEADTOWARDFULLMANR", AI_MANEUVER_MODE_HEAD_TOWARD_FULL},
	{"RUNAWAYMANR", AI_MANEUVER_MODE_RUN_AWAY},
	{"HEADONATTACKMANR", AI_MANEUVER_MODE_HEAD_ON_ATTACK},
	{"FOLLOWLEADERMANR", AI_MANEUVER_MODE_FOLLOW_LEADER},
	{"SETUPATTACKMANR", AI_MANEUVER_MODE_SETUP_ATTACK},
	{"ATTACKMANR", AI_MANEUVER_MODE_ATTACK},
	{"ZOOMMANR", AI_MANEUVER_MODE_ZOOM},
	{"DIVEMANR", AI_MANEUVER_MODE_DIVE},
	{"SPLITSDIVEMANR", AI_MANEUVER_MODE_SPLITS_DIVE},
	{"SPEEDAWAYMANR", AI_MANEUVER_MODE_SPEED_AWAY},
	{"ESCORTMANR", AI_MANEUVER_MODE_ESCORT},
	{"BOARDMANR", AI_MANEUVER_MODE_BOARD},
	{"AWAITBOARDMANR", AI_MANEUVER_MODE_AWAIT_BOARD},
	{"HEADTOWARDMANR", AI_MANEUVER_MODE_HEAD_TOWARD},
	{"INTOHYPERSPACEMANR", AI_MANEUVER_MODE_INTO_HYPERSPACE},
	{"OUTOFHYPERSPACEMANR", AI_MANEUVER_MODE_OUT_OF_HYPERSPACE},
	{"ROCKETATTACKMANR", AI_MANEUVER_MODE_ROCKET_ATTACK},
	{"TURNAWAYMANR", AI_MANEUVER_MODE_TURN_AWAY},
	{"STOPMANR", AI_MANEUVER_MODE_STOP},
	{"OUTOFHANGARMANR", AI_MANEUVER_MODE_OUT_OF_HANGAR},
	{"EVASIVEMANR", AI_MANEUVER_MODE_EVASIVE},
	{"AVOIDSTARSHIPMANR", AI_MANEUVER_MODE_AVOID_STARSHIP},
	{"WAITMANR", AI_MANEUVER_MODE_WAIT},
	{"DROPOFFMANR", AI_MANEUVER_MODE_DROPOFF},
	{"KAMIKAZEMANR", AI_MANEUVER_MODE_KAMIKAZE},
	{"", 0},
};
/* The order tokens of the plan text and the g_orderTable index each compiles
 * to, 0 to 47, ended by an empty name; NULLORDR (0) ends a plan. Only
 * pai_FindOrderTokenIndex and pai_CompilePlansFromText read it. */
// GLOBAL: XVT 0x526050
struct PaiPlanTokenDef g_paiOrderTokenDefs[49] = {
	{"NULLORDR", 0},
	{"UPDATECOURSEORDR", 1},
	{"UNDERATTACKORDR", 2},
	{"STILLATTACKORDR", 3},
	{"FLYHOMEORDR", 4},
	{"FIGHTERSHOOTORDR", 5},
	{"GUNNERSELFDEFENSEORDR", 6},
	{"GUNNEROFFENSEORDR", 7},
	{"MISSILEDEFENSEORDR", 8},
	{"SCANFORTARGETORDR", 9},
	{"WAITRUNORDR", 10},
	{"BREAKOFFORDR", 11},
	{"LEADERDEADORDR", 12},
	{"COVERLEADERORDR", 13},
	{"FOLLOWLEADATKORDR", 14},
	{"ABORTATKORDR", 15},
	{"ONTAILORDR", 16},
	{"ALWAYSORDR", 17},
	{"CHECKESCORTORDR", 18},
	{"LEADERGOHOMEORDR", 19},
	{"HYPERSPACEORDR", 20},
	{"ENTERHANGARORDR", 21},
	{"MOTHERSHIPORDR", 22},
	{"ESCORTTARGETORDR", 23},
	{"LOOKFORDISABLEORDR", 24},
	{"ABORTBOARDORDR", 25},
	{"RETURNBOARDORDR", 26},
	{"AWAITBOARDORDR", 27},
	{"MAKEDISABLEDORDR", 28},
	{"NEARTARGETORDR", 29},
	{"ROCKETSONBOARDORDR", 30},
	{"AVOIDHITORDR", 31},
	{"WAITFORALLRETURNORDR", 32},
	{"WAITFORALLCREATEORDR", 33},
	{"EVASIVEORDR", 34},
	{"NEWTARGETORDR", 35},
	{"AVOIDSTARSHIPORDR", 36},
	{"CHECKHYPERORDR", 37},
	{"STOPGOHOMEORDR", 38},
	{"COMPLETEGOHOMEORDR", 39},
	{"COMPLETEGOOTHERORDR", 40},
	{"COMPLETEFOLLOWORDR", 41},
	{"WAITGOOTHERORDR", 42},
	{"ORDERSWITCHORDR", 43},
	{"KILLSELFORDR", 44},
	{"DROPOFFDESTORDR", 45},
	{"ABORTMOTHERWAITORDR", 46},
	{"PLAYERINPUTORDR", 47},
	{"", 0},
};
/* For each plan id, where the plan's bytes start in g_planOrderData. Written by
 * pai_CompilePlansFromText and by pai_loadplans, which first zeroes only its
 * first 256 bytes, not the whole array. */
// GLOBAL: XVT 0xA07CF0
uint8_t *g_planDataPtrs[256];
/* The compiled plans, back to back. Each plan is a target byte, a maneuver
 * byte, then pairs of an order id and the plan id to switch to when that order
 * fires, ended by order 0. pai_CompilePlansFromText writes it from the .pln
 * text; pai_loadplans reads it from the .plo file. The world checksum covers it
 * in both builds. */
// GLOBAL: XVT 0x9A8E40
uint8_t g_planOrderData[0x20000] = {0};
/* Plans loaded. pai_loadplans sets it to 0, then, after reading the .plo file,
 * to the number of named entries in g_planTable; pai_CompilePlansFromText adds
 * 1 for each plan it compiles. Beyond that, only the world state save, restore
 * and checksum use it. */
// GLOBAL: XVT 0x9A8068
int g_planCount = 0;
/* For each name in g_builtinPlanNameTable, the plan id loaded under that name,
 * or 0 when no plan has it. pai_cacheBuiltinPlanIds fills it at flight start;
 * the world state saves and restores it in both builds. */
// GLOBAL: XVT 0x9A7A40
uint8_t g_builtinPlanIdByNameIndex[256] = {0};
/* For each mission order, 0 to 39, the g_builtinPlanNameTable index of the
 * order's leader plan: the plan a craft with no leader flies, and the one
 * Mission_InitFlightGroupObjectSlot stores as every new craft's currentPlanId.
 * Nothing writes it. */
// GLOBAL: XVT 0x524120
uint8_t g_orderLeaderBuiltinPlanNameIndex[40] = {
	0x01, 0x2f, 0x03, 0x05, 0x27, 0x2a, 0x2b, 0x45, 0x08, 0x09,
	0x14, 0x13, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x25, 0x42,
	0x42, 0x38, 0x3a, 0x3b, 0x3c, 0x3c, 0x3e, 0x3f, 0x01, 0x35,
	0x01, 0x22, 0x44, 0x01, 0x01, 0x01, 0x43, 0x46, 0x01, 0x00,
};
/* For each mission order, 0 to 39, the g_builtinPlanNameTable index of the plan
 * the order gives a craft that follows a leader. */
// GLOBAL: XVT 0x524148
const uint8_t g_orderFollowerBuiltinPlanNameIndex[40] = {
	0x02, 0x30, 0x04, 0x06, 0x29, 0x2a, 0x2b, 0x0e, 0x0e, 0x0e,
	0x18, 0x0e, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x04, 0x42,
	0x42, 0x39, 0x3a, 0x3b, 0x39, 0x39, 0x39, 0x39, 0x02, 0x36,
	0x02, 0x22, 0x44, 0x01, 0x01, 0x01, 0x43, 0x46, 0x01, 0x00,
};

/* Runs one AI think for each craft whose think timer has run out. It walks the
 * active region's craft slots and skips empty slots, objects whose mobile
 * object family is not 0, craft breaking up or exploding, and craft whose
 * thinkTimer is above 0. For a craft no player flies it sets up g_paiContext,
 * loads the craft's own random seed into g_gameRandFeedbackState, runs
 * pai_ProcessPlan and stores the seed back. Every craft it does not skip,
 * player craft too, then gets thinkInterval added to its thinkTimer. Restores
 * g_gameRandFeedbackState at the end; leaves g_curCraft at the last craft
 * visited. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4028A0
void pai_UpdateAllCraftAI(void)
{
	uint16_t objectIndex;
	int16_t savedRandState;

	savedRandState = g_gameRandFeedbackState;
	for (objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
	     objectIndex < g_activeRegionCraftObjectSlotEnd; ++objectIndex) {
		ObjectRecord *object = &g_objectTable[objectIndex];
		MobileObject *mobileObject;
		AiController *controller;

		if (object->objectType == 0) {
			continue;
		}
		mobileObject = object->mobj;
		if (mobileObject->family != 0) {
			continue;
		}
		g_curCraft = mobileObject->pCraft;
		controller = &g_curCraft->aiController;
		if (g_curCraft->objectKind == CRAFT_OBJECT_KIND_BREAKING_UP ||
		    g_curCraft->objectKind == CRAFT_OBJECT_KIND_EXPLODING ||
		    controller->thinkTimer > 0) {
			continue;
		}
		if (object->playerOwnerIdx == -1) {
			pai_setupcraftcontext(objectIndex);
			g_gameRandFeedbackState = controller->savedRandSeed;
			pai_ProcessPlan();
			controller->savedRandSeed = g_gameRandFeedbackState;
		}
		controller->thinkTimer += controller->thinkInterval;
	}
	g_gameRandFeedbackState = savedRandState;
}

/* Starts g_curCraft's pendingPlanId plan. When the plan's target byte is not
 * 255 it points targetObjIdx at a mission point of the flight group of the
 * object g_paiContext.objectIndex names: for LOCATARGET (249) point 12; for
 * PRIMARYTARGET (253) point 12 when enabled; for HOMETARGET (254) point 12 when
 * capturedByFlightGroup is set and point 12 is enabled, else point 13 when
 * enabled; for any other byte the point waypointIndex names when enabled. Where
 * the point is not enabled it uses 0x8000, the group's current point. It then
 * clears targetSignature and hasLiveTarget and sets the aim point there. When
 * the maneuver byte is not 255 it sets maneuverMode and runs
 * paiman_initmaneuver. Always clears secondaryManeuverTimer, sets
 * lastAttackerObjIdx and aiFlight.threatObjIdx to 0xFFFF and
 * lastHitMissionSecond to 0, and sets thinkTimer to (objectIdx mod 8) eighths
 * of thinkInterval, which spreads craft thinks over the interval. Does not set
 * currentPlanId. */
// FUNCTION: XVT 0x402970
void pai_ApplyPendingPlanTargetAndManeuver(unsigned int objectIdx)
{
	AiController *controller;
	uint8_t *planData;
	uint16_t targetToken;
	uint8_t maneuverToken;

	controller = &g_curCraft->aiController;
	planData = g_planDataPtrs[controller->pendingPlanId];
	targetToken = *planData++;

	if (targetToken != 0xFFu) {
		if (targetToken == 0xFDu) {
			if (g_missionFlightGroups
				    [g_objectTable[g_paiContext.objectIndex]
					     .flightGroupIdx]
					    .fg.missionPointEnabled[12] != 0) {
				controller->targetObjIdx = 0x800Cu;
			} else {
				controller->targetObjIdx = 0x8000u;
			}
		} else if (targetToken == 0xFEu) {
			if (g_curCraft->capturedByFlightGroup != 0 &&
			    g_missionFlightGroups
					    [g_objectTable[g_paiContext
								   .objectIndex]
						     .flightGroupIdx]
						    .fg
						    .missionPointEnabled[12] !=
				    0) {
				controller->targetObjIdx = 0x800Cu;
			} else if (g_missionFlightGroups
					   [g_objectTable[g_paiContext
								  .objectIndex]
						    .flightGroupIdx]
						   .fg
						   .missionPointEnabled[13] !=
				   0) {
				controller->targetObjIdx = 0x800Du;
			} else {
				controller->targetObjIdx = 0x8000u;
			}
		} else if (targetToken == 0xF9u) {
			controller->targetObjIdx = 0x800Cu;
		} else {
			if (g_missionFlightGroups
				    [g_objectTable[g_paiContext.objectIndex]
					     .flightGroupIdx]
					    .fg.missionPointEnabled
						    [controller
							     ->waypointIndex] !=
			    0) {
				controller->targetObjIdx =
					(uint16_t)(0x8000u +
						   controller->waypointIndex);
			} else {
				controller->targetObjIdx = 0x8000u;
			}
		}

		controller->targetSignature = 0;
		controller->hasLiveTarget = 0;
		if (controller->targetObjIdx != UINT16_MAX) {
			pai_UpdateAimPointFromOrderTarget();
		}
	}

	controller->secondaryManeuverTimer = 0;
	maneuverToken = *planData;
	if (maneuverToken != UINT8_MAX) {
		controller->maneuverMode = maneuverToken;
		paiman_initmaneuver();
	}

	g_curCraft->lastAttackerObjIdx = UINT16_MAX;
	g_curCraft->lastHitMissionSecond = 0;
	g_curCraft->aiFlight.threatObjIdx = UINT16_MAX;
	controller->thinkTimer =
		((objectIdx & 7) * controller->thinkInterval) >> 3;
}

/* Runs the orders of the plan set up in g_paiContext. It calls each order's
 * handler in turn and switches to the first order's plan whose handler returns
 * nonzero and whose plan is not nullpln; "variablepln" stands for the plan in
 * g_paiContext.nullPlanId. Switching sets pendingPlanId, sets up the context
 * again and runs pai_ApplyPendingPlanTargetAndManeuver. Returns at order 0
 * without a switch. Sets g_paiSkipToOrder4Checked to 0 when it starts and when
 * it switches. A player craft on escortldr1pln first runs
 * paifight_checkescortorder, but pai_UpdateAllCraftAI, the only caller, never
 * passes a player craft. */
// FUNCTION: XVT 0x402B60
void pai_ProcessPlan(void)
{
	AiController *controller;
	uint8_t orderId;

	controller = &g_curCraft->aiController;
	if (g_objectTable[g_paiContext.objectIndex].playerOwnerIdx != -1 &&
	    strcmp(g_planTable[controller->currentPlanId].name,
		   "escortldr1pln") == 0) {
		paifight_checkescortorder();
	}

	g_paiSkipToOrder4Checked = 0;
	orderId = *g_paiContext.planCursor++;
	if (orderId == 0) {
		return;
	}

	while (g_orderTable[orderId]() == 0 ||
	       strcmp(g_planTable[*g_paiContext.planCursor].name, "nullpln") ==
		       0) {
		++g_paiContext.planCursor;
		orderId = *g_paiContext.planCursor++;
		if (orderId == 0) {
			return;
		}
	}

	if (strcmp(g_planTable[*g_paiContext.planCursor].name, "variablepln") ==
	    0) {
		controller->pendingPlanId = g_paiContext.nullPlanId;
	} else {
		controller->pendingPlanId = *g_paiContext.planCursor;
	}

	pai_setupcraftcontext(g_paiContext.objectIndex);
	pai_ApplyPendingPlanTargetAndManeuver(g_paiContext.objectIndex);
	if (g_paiSkipToOrder4Checked == 1) {
		g_paiSkipToOrder4Checked = 0;
	}
}

/* Fills g_paiContext for one craft object: its index, craft and controller, its
 * leader's index (255 for none) and the leader's craft or its own, its flight
 * group, current order slot, world position and skill tier, and a plan cursor
 * at the first order of the pendingPlanId plan, with that plan's maneuver byte
 * in initialManeuverId. Clears requireUndisabledTarget and sets nullPlanId to
 * nullpln's plan id. Also sets g_worldLocX, g_worldLocY and g_worldLocZ to the
 * craft's position. Leaves targetSearchFlags and the search origin alone. Does
 * not check that the object is a craft. */
// FUNCTION: XVT 0x402CB0
void pai_setupcraftcontext(uint16_t objectIdx)
{
	AiController *controller;
	CraftData *leaderOrSelfCraft;
	ObjectRecord *object;

	g_paiContext.objectIndex = objectIdx;
	object = &g_objectTable[objectIdx];
	g_paiContext.craft = object->mobj->pCraft;
	g_paiContext.leaderObjectIndex =
		(uint8_t)g_paiContext.craft->leader_obj_idx;
	controller = &g_paiContext.craft->aiController;
	g_paiContext.controller = controller;
	if (g_paiContext.leaderObjectIndex == UINT8_MAX) {
		leaderOrSelfCraft = object->mobj->pCraft;
	} else {
		leaderOrSelfCraft =
			g_objectTable[g_paiContext.leaderObjectIndex]
				.mobj->pCraft;
	}
	g_paiContext.leaderOrSelfCraft = leaderOrSelfCraft;
	g_paiContext.craftFlightGroupIndex = object->flightGroupIdx;
	g_paiContext.orderSlot = controller->currentOrderSlot;
	Mission_ResolveObjectOrMissionPointWorldLoc(
		objectIdx, g_paiContext.craftFlightGroupIndex);
	g_paiContext.craftPositionX = g_worldLocX;
	g_paiContext.craftPositionY = g_worldLocY;
	g_paiContext.craftPositionZ = g_worldLocZ;
	g_paiContext.skillTier = pai_SkillValueToTier(
		pai_GetEffectiveSkillValue(g_paiContext.craft));
	g_paiContext.planCursor = g_planDataPtrs[controller->pendingPlanId];
	++g_paiContext.planCursor;
	g_paiContext.initialManeuverId = *g_paiContext.planCursor++;
	g_paiContext.requireUndisabledTarget = 0;
	g_paiContext.nullPlanId =
		(uint8_t)pai_FindPlanIdByNameOrZero("nullpln");
}

/* Returns the skill tier of a skill value: 0 below 0x8000, 1 below 0xC000, else
 * 2. */
// FUNCTION: XVT 0x402E00
int pai_SkillValueToTier(uint16_t skillValue)
{
	if (skillValue < 0x8000) {
		return 0;
	}
	return skillValue < 0xC000 ? 1 : 2;
}

/* Returns the index of the first craft in the active region's craft slots that
 * belongs to the flight group, is not breaking up or exploding, and has no
 * leader; 0xFFFF when there is none. Any flight group works; the callers pass a
 * mothership's. */
// FUNCTION: XVT 0x402E20
uint16_t pai_FindMothershipObject(int16_t mothershipFlightGroupIdx)
{
	uint16_t objectIndex;
	CraftData *craft;
	ObjectRecord *object;
	uint8_t objectKind;

	objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
	while (objectIndex < g_activeRegionCraftObjectSlotEnd) {
		object = &g_objectTable[objectIndex];
		if (object->objectType != 0) {
			craft = object->mobj->pCraft;
			objectKind = craft->objectKind;
			if (objectKind != CRAFT_OBJECT_KIND_EXPLODING &&
			    objectKind != CRAFT_OBJECT_KIND_BREAKING_UP &&
			    object->flightGroupIdx ==
				    (uint16_t)mothershipFlightGroupIdx &&
			    (uint8_t)craft->leader_obj_idx == UINT8_MAX) {
				return objectIndex;
			}
		}

		++objectIndex;
	}

	return UINT16_MAX;
}

/* Returns 1 when pai_IsObjectTargetable accepts the object and its rough
 * distance from the craft's position in g_paiContext is below a skill range,
 * else 0. The range is 2,560 plus 0x500 times g_aiSkillValueQ16ByLevel[skill
 * tier] over 65,536, times 256 world units: 655,360, 737,280 or 819,200 for
 * tiers 0 to 2. A nonzero expandRange adds 0x5555 over 65,536 of it, about a
 * third. The first argument is ignored. Sets g_lastRoughDistance. */
// FUNCTION: XVT 0x402EC0
int pai_IsObjectTargetableNearCraft(int unused, unsigned int objIdx,
				    int expandRange)
{
	int targetable;
	int maxRangeScore;

	(void)unused;
	targetable = pai_IsObjectTargetable(objIdx);

	if (targetable) {
		maxRangeScore =
			(uint16_t)MATH2_fraction(
				0x500u,
				g_aiSkillValueQ16ByLevel[g_paiContext
								 .skillTier]) +
			2560;
		if (expandRange != 0) {
			maxRangeScore += (uint16_t)MATH2_fraction(
				(unsigned int)maxRangeScore, 0x5555u);
		}
		if (pai_IsObjectWithinRangeOfCraft(
			    objIdx, (unsigned int)(maxRangeScore << 8)) == 1) {
			return 1;
		}
	}
	return 0;
}

/* Returns 1 when the object's rough distance from the craft's position in
 * g_paiContext is below the unexpanded skill range of
 * pai_IsObjectTargetableNearCraft, else 0. Does not check that the object can
 * be targeted. Sets g_lastRoughDistance. */
// FUNCTION: XVT 0x403070
int16_t pai_IsObjectWithinSkillRangeOfCraft(uint16_t objIdx)
{
	uint16_t skillRange;

	skillRange = (uint16_t)MATH2_fraction(
		0x500, g_aiSkillValueQ16ByLevel[g_paiContext.skillTier]);
	return pai_IsObjectWithinRangeOfCraft(objIdx, (skillRange + 0xA00)
							      << 8) == 1;
}

/* Returns 1 when pai_FindBoardingTargetFromOrder finds a target for the order
 * slot, else 0. */
// FUNCTION: XVT 0x403250
int16_t pai_OrderSlotCanBoardTarget(uint16_t orderSlot)
{
	return pai_FindBoardingTargetFromOrder(orderSlot) != -1;
}

/* Returns the nearest boarding target that matches the order slot's first pair
 * of target conditions, or when none does, its second pair; -1 when neither
 * finds one. Uses the craft's flight group in g_paiContext. */
// FUNCTION: XVT 0x403270
int16_t pai_FindBoardingTargetFromOrder(uint16_t orderSlot)
{
	int16_t result;

	result = pai_FindNearestBoardingTarget(
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderSlot]
			.target1Type,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderSlot]
			.target1,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderSlot]
			.target1OrTarget2,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderSlot]
			.target2Type,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderSlot]
			.target2);
	if (result == -1) {
		result = pai_FindNearestBoardingTarget(
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderSlot]
				.secondaryTargetTypes[0],
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderSlot]
				.secondaryTargets[0],
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderSlot]
				.target3OrTarget4,
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderSlot]
				.secondaryTargetTypes[1],
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderSlot]
				.secondaryTargets[1]);
	}
	return result;
}

/* Returns 1 when the object's rough distance from the craft's position in
 * g_paiContext is below maxRoughDistance, in world units, else 0. Sets
 * g_lastRoughDistance to that distance. The position is the one
 * pai_setupcraftcontext took, not the craft's live one. */
// FUNCTION: XVT 0x403360
int pai_IsObjectWithinRangeOfCraft(unsigned int objIdx,
				   unsigned int maxRoughDistance)
{
	ObjectRecord *object;
	int deltaX;
	int deltaY;
	int deltaZ;

	object = &g_objectTable[objIdx];
	deltaX = g_paiContext.craftPositionX - object->world_x;
	deltaY = g_paiContext.craftPositionY - object->world_y;
	deltaZ = g_paiContext.craftPositionZ - object->world_z;
	if (deltaX < 0) {
		deltaX = (int)(0u - (unsigned int)deltaX);
	}
	if (deltaY < 0) {
		deltaY = (int)(0u - (unsigned int)deltaY);
	}
	if (deltaZ < 0) {
		deltaZ = (int)(0u - (unsigned int)deltaZ);
	}

	if (deltaY < deltaX) {
		g_lastRoughDistance = deltaX + (deltaY >> 1);
	} else {
		g_lastRoughDistance = deltaY + (deltaX >> 1);
	}

	if (g_lastRoughDistance > deltaZ) {
		deltaZ >>= 1;
	} else {
		g_lastRoughDistance >>= 1;
	}
	g_lastRoughDistance += deltaZ;

	return g_lastRoughDistance < (int)maxRoughDistance;
}

/* Sets the controller's aim point to the world position of its targetObjIdx: an
 * object, or a mission point of the craft's flight group, 0x8000 standing for
 * the group's current point. Also sets g_worldLocX, g_worldLocY and
 * g_worldLocZ. Does not check for 0xFFFF, no target. */
// FUNCTION: XVT 0x403400
void pai_UpdateAimPointFromOrderTarget(void)
{
	Mission_ResolveObjectOrMissionPointWorldLoc(
		g_paiContext.controller->targetObjIdx,
		g_objectTable[g_paiContext.objectIndex].flightGroupIdx);
	g_paiContext.controller->aimPointX = g_worldLocX;
	g_paiContext.controller->aimPointY = g_worldLocY;
	g_paiContext.controller->aimPointZ = g_worldLocZ;
}

/* Sets the formation type and separation of every craft of the flight group in
 * the active region's craft slots. */
// FUNCTION: XVT 0x403470
void pai_SetFlightGroupFormation(unsigned int flightGroupIdx,
				 unsigned int formationType,
				 unsigned int formationSpacing)
{
	unsigned int objectIndex;
	ObjectRecord *object;
	CraftData *craft;

	for (objectIndex = (unsigned int)g_activeRegionObjectSlotStart;
	     objectIndex < (unsigned int)g_activeRegionCraftObjectSlotEnd;
	     ++objectIndex) {
		object = &g_objectTable[objectIndex];
		if (object->objectType != 0 &&
		    object->flightGroupIdx == flightGroupIdx) {
			craft = object->mobj->pCraft;
			craft->aiFlight.formationType = formationType;
			craft->aiFlight.separation = formationSpacing;
		}
	}
}

/* Works out the direction and distance from one object or mission point to
 * another with trig2_ctop, which sets trig2_xyangle, trig2_pitch and
 * trig2_polardistance. A mission point reference is read from flight group 0's
 * points. Leaves g_worldLocX, g_worldLocY and g_worldLocZ at fromRef. */
// FUNCTION: XVT 0x4034E0
void pai_ObjectRefDirectionToObjectRef(unsigned int fromRef, unsigned int toRef)
{
	int targetX;
	int targetY;
	int targetZ;

	Mission_ResolveObjectOrMissionPointWorldLoc(toRef, 0);
	targetX = g_worldLocX;
	targetY = g_worldLocY;
	targetZ = g_worldLocZ;

	Mission_ResolveObjectOrMissionPointWorldLoc(fromRef, 0);
	targetX = targetX - g_worldLocX;
	targetY = targetY - g_worldLocY;
	targetZ = targetZ - g_worldLocZ;
	trig2_ctop(targetX, targetY, targetZ);
}

/* Sets g_lastRoughDistance to the rough distance, in world units, between two
 * objects or mission points. A mission point reference is read from flight
 * group 0's points. Leaves g_worldLocX, g_worldLocY and g_worldLocZ at
 * toRef. */
// FUNCTION: XVT 0x403540
void pai_ObjectRefUpdateRoughDistance(unsigned int fromRef, unsigned int toRef)
{
	int deltaX;
	int deltaY;
	int deltaZ;
	int xyScore;

	Mission_ResolveObjectOrMissionPointWorldLoc(fromRef, 0);
	deltaX = g_worldLocX;
	deltaY = g_worldLocY;
	deltaZ = g_worldLocZ;

	Mission_ResolveObjectOrMissionPointWorldLoc(toRef, 0);
	deltaX -= g_worldLocX;
	deltaY -= g_worldLocY;
	deltaZ -= g_worldLocZ;

	if (deltaX < 0) {
		deltaX = -deltaX;
	}
	if (deltaY < 0) {
		deltaY = -deltaY;
	}
	if (deltaZ < 0) {
		deltaZ = -deltaZ;
	}

	if (deltaX > deltaY) {
		xyScore = deltaX + (deltaY >> 1);
	} else {
		xyScore = deltaY + (deltaX >> 1);
	}

	if (xyScore > deltaZ) {
		g_lastRoughDistance = xyScore + (deltaZ >> 1);
	} else {
		g_lastRoughDistance = xyScore;
		g_lastRoughDistance >>= 1;
		g_lastRoughDistance += deltaZ;
	}
}

/* Turns a vector given along an object's side, up and forward axes into world
 * axes, in g_rotatedX, g_rotatedY and g_rotatedZ. The axes are 1.15 fixed
 * point, so the result keeps the input's unit. When the object's
 * orientMatrixDirty is set it first rebuilds the object's cached move vector
 * and axes with FVIEW_calcrotatemove and FVIEW_calcrotateorient, which also set
 * the shared matrix globals those two write. */
// FUNCTION: XVT 0x4035D0
void pai_calcrotatedpoint(ObjectRecord *obj, int16_t sideArg, int16_t upArg,
			  int16_t fwdArg)
{
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

/* Does the same as pai_calcrotatedpoint, with the vector passed as int. */
// FUNCTION: XVT 0x4037B0
void pai_RotateLocalVectorToWorldScratch(ObjectRecord *objRecord, int localSide,
					 int localUp, int localFwd)
{
	int result;

	if (objRecord->mobj->orientMatrixDirty != 0) {
		FVIEW_calcrotatemove(objRecord->pitch, objRecord->yaw,
				     objRecord);
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

/* Works out the direction and distance from the craft's live position to the
 * controller's aim point with trig2_ctop, which sets trig2_xyangle, trig2_pitch
 * and trig2_polardistance. */
// FUNCTION: XVT 0x403990
void pai_CalcAnglesToAimPoint(void)
{
	ObjectRecord *object;

	object = &g_objectTable[g_paiContext.objectIndex];
	trig2_ctop(g_paiContext.controller->aimPointX - object->world_x,
		   g_paiContext.controller->aimPointY - object->world_y,
		   g_paiContext.controller->aimPointZ - object->world_z);
}

/* Returns the nearest object, by rough distance from the craft in g_paiContext,
 * that matches the target conditions (either one when targetOrMode is 1, else
 * both) and that no other craft is boarding; -1 when none. Among craft it takes
 * only one that can be boarded now: on nullpln, stationaryldrpln or
 * stationaryflwpln, a platform, or with no working subsystems; when the
 * searching craft's plan is not boardtocapturepln or boardtodestroypln, also
 * one waiting to be boarded or stopped. A craft is taken when another craft on
 * a boardto plan targets or carries it. It then looks at the region's static
 * object slots whose type has behavior flag 2, matching by flight group; there
 * only a target counts as taken, and boardtopickuppln is not among the plans
 * checked. Either way an object whose signature is in g_curCraft's docked list
 * is skipped. Sets g_lastRoughDistance. */
// FUNCTION: XVT 0x4039E0
int16_t pai_FindNearestBoardingTarget(uint16_t target1Type, uint16_t target1,
				      int16_t targetOrMode,
				      uint16_t target2Type, uint16_t target2)
{
	uint16_t objectIdx;
	uint16_t nearestObject = UINT16_MAX;
	unsigned int nearestRange = UINT_MAX;

	for (objectIdx = (uint16_t)g_activeRegionObjectSlotStart;
	     objectIdx < g_activeRegionCraftObjectSlotEnd; ++objectIdx) {
		int16_t firstMatch;
		int16_t secondMatch;
		int16_t craftReservedCount;
		CraftData *craft;
		ObjectRecord *object;
		AiController *controller;
		const char *planName;

		if (g_objectTable[objectIdx].objectType == 0) {
			continue;
		}
		firstMatch = Mission_ObjectMatchesTriggerVariable(
			objectIdx, target1Type, target1);
		secondMatch = Mission_ObjectMatchesTriggerVariable(
			objectIdx, target2Type, target2);
		if (targetOrMode == 1) {
			firstMatch |= secondMatch;
		} else {
			firstMatch &= secondMatch;
		}
		if (firstMatch == 0) {
			continue;
		}

		/* Until the reset that starts the count, this local is a 0/1 flag: 1 when the craft can be boarded
		 * now (parked, a platform, disabled, stopped, or waiting to be boarded). */
		craftReservedCount = 0;
		object = &g_objectTable[objectIdx];
		craft = g_objectTable[objectIdx].mobj->pCraft;
		controller = &craft->aiController;
		planName = g_planTable[controller->currentPlanId].name;
		if (strcmp(planName, "nullpln") == 0 ||
		    strcmp(planName, "stationaryldrpln") == 0 ||
		    strcmp(planName, "stationaryflwpln") == 0 ||
		    object->genusId == CRAFT_GENUS_PLATFORM) {
			craftReservedCount = 1;
		} else if (strcmp(g_planTable[g_paiContext.controller
						      ->currentPlanId]
					  .name,
				  "boardtocapturepln") == 0 ||
			   strcmp(g_planTable[g_paiContext.controller
						      ->currentPlanId]
					  .name,
				  "boardtodestroypln") == 0) {
			if (craft->workingSubsystems == 0) {
				craftReservedCount = 1;
			}
		} else if (craft->workingSubsystems == 0 ||
			   controller->maneuverMode ==
				   AI_MANEUVER_MODE_AWAIT_BOARD ||
			   controller->maneuverMode == AI_MANEUVER_MODE_STOP) {
			craftReservedCount = 1;
		}

		if (craftReservedCount != 0) {
			uint16_t otherIdx;
			uint16_t sigIdx;

			craftReservedCount = 0;
			for (otherIdx = (uint16_t)g_activeRegionObjectSlotStart;
			     otherIdx < g_activeRegionCraftObjectSlotEnd;
			     ++otherIdx) {
				ObjectRecord *other = &g_objectTable[otherIdx];
				if (other->objectType != 0 &&
				    otherIdx != g_paiContext.objectIndex) {
					CraftData *otherCraft;
					AiController *otherController;
					int currentPlanId;

					otherCraft = other->mobj->pCraft;
					currentPlanId = otherCraft->aiController
								.currentPlanId;
					otherController =
						&otherCraft->aiController;
					if (strcmp(g_planTable[currentPlanId]
							   .name,
						   "boardtogivepln") == 0 ||
					    strcmp(g_planTable[currentPlanId]
							   .name,
						   "boardtotakepln") == 0 ||
					    strcmp(g_planTable[currentPlanId]
							   .name,
						   "boardtoexchangepln") == 0 ||
					    strcmp(g_planTable[currentPlanId]
							   .name,
						   "boardtocapturepln") == 0 ||
					    strcmp(g_planTable[currentPlanId]
							   .name,
						   "boardtodestroypln") == 0 ||
					    strcmp(g_planTable[currentPlanId]
							   .name,
						   "boardtopickuppln") == 0 ||
					    strcmp(g_planTable[currentPlanId]
							   .name,
						   "boardtocontactpln") == 0 ||
					    strcmp(g_planTable[currentPlanId]
							   .name,
						   "boardtorepairpln") == 0) {
						if (otherController
							    ->targetObjIdx ==
						    objectIdx) {
							++craftReservedCount;
						} else if (
							otherCraft
								->carriedObjectIndex ==
							objectIdx) {
							++craftReservedCount;
						}
					}
				}
			}
			for (sigIdx = 0;
			     sigIdx < g_curCraft->aiFlight.dockedTargetCount;
			     ++sigIdx) {
				if (g_curCraft->aiFlight
					    .dockedTargetSignatures[sigIdx] ==
				    object->objectSignature) {
					++craftReservedCount;
				}
			}
			if (craftReservedCount == 0) {
				pai_ObjectRefUpdateRoughDistance(
					g_paiContext.objectIndex, objectIdx);
				if ((unsigned int)g_lastRoughDistance >=
				    nearestRange) {
					continue;
				}
				nearestRange = g_lastRoughDistance;
				nearestObject = objectIdx;
			}
		}
	}

	for (objectIdx = (uint16_t)g_regionMainObjectSlotEnd;
	     (int)(g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount) >
	     objectIdx;
	     ++objectIdx) {
		int16_t firstMatch;
		int16_t secondMatch;
		ObjectRecord *object = &g_objectTable[objectIdx];
		uint16_t objectType = object->objectType;

		if (objectType != 0 &&
		    (g_objectTypeTable[objectType].behaviorFlags & 2) != 0) {
			firstMatch = Mission_FlightGroupMatchesTriggerVariable(
				object->flightGroupIdx, target1Type, target1);
			secondMatch = Mission_FlightGroupMatchesTriggerVariable(
				g_objectTable[objectIdx].flightGroupIdx,
				target2Type, target2);
			if (targetOrMode == 1) {
				firstMatch |= secondMatch;
			} else {
				firstMatch &= secondMatch;
			}
			if (firstMatch != 0) {
				int16_t reservedCount = 0;
				uint16_t otherIdx;
				uint16_t sigIdx;

				for (otherIdx = (uint16_t)
					     g_activeRegionObjectSlotStart;
				     otherIdx <
				     g_activeRegionCraftObjectSlotEnd;
				     ++otherIdx) {
					ObjectRecord *other =
						&g_objectTable[otherIdx];
					if (other->objectType != 0 &&
					    otherIdx !=
						    g_paiContext.objectIndex) {
						int currentPlanId;
						AiController *otherController;

						otherController =
							&other->mobj->pCraft
								 ->aiController;
						currentPlanId =
							otherController
								->currentPlanId;
						if ((strcmp(g_planTable
								    [currentPlanId]
									    .name,
							    "boardtogivepln") ==
							     0 ||
						     strcmp(g_planTable
								    [currentPlanId]
									    .name,
							    "boardtotakepln") ==
							     0 ||
						     strcmp(g_planTable
								    [currentPlanId]
									    .name,
							    "boardtoexchangepln") ==
							     0 ||
						     strcmp(g_planTable
								    [currentPlanId]
									    .name,
							    "boardtocapturepln") ==
							     0 ||
						     strcmp(g_planTable
								    [currentPlanId]
									    .name,
							    "boardtodestroypln") ==
							     0 ||
						     strcmp(g_planTable
								    [currentPlanId]
									    .name,
							    "boardtocontactpln") ==
							     0 ||
						     strcmp(g_planTable
								    [currentPlanId]
									    .name,
							    "boardtorepairpln") ==
							     0) &&
						    otherController->targetObjIdx ==
							    objectIdx) {
							++reservedCount;
						}
					}
				}
				{
					uint8_t signatureCount;

					sigIdx = 0;
					signatureCount =
						g_curCraft->aiFlight
							.dockedTargetCount;
					if (signatureCount != 0) {
						do {
							if (g_curCraft->aiFlight.dockedTargetSignatures
								    [sigIdx] ==
							    g_objectTable[objectIdx]
								    .objectSignature) {
								++reservedCount;
							}
							++sigIdx;
						} while (sigIdx <
							 signatureCount);
					}
				}
				if (reservedCount == 0) {
					pai_ObjectRefUpdateRoughDistance(
						g_paiContext.objectIndex,
						objectIdx);
					if ((unsigned int)g_lastRoughDistance >=
					    nearestRange) {
						continue;
					}
					nearestRange = g_lastRoughDistance;
					nearestObject = objectIdx;
				}
			}
		}
	}
	return (int16_t)nearestObject;
}

/* Returns 1 when the plan's goal for the order slot is met, else 0. For
 * formldr1pln, formevadeldr1pln, starshipformpln, rendezvous1pln, disabledpln
 * and waitforboardpln: goalProgress has reached the order's variable1. For
 * capfreeldr1pln, capescortersldr1pln, caprespondldr1pln, escortldr1pln,
 * disableldr1pln, starshipprotectpln, starshipattackpln and starshipdisablepln:
 * no target is left, now or later. For the eight boardto plans: goalProgress
 * has reached variable2. For dropoffldr1pln: goalProgress has reached the
 * FLIGHT_GROUP_OUTCOME_TOTAL count of the flight group whose index is
 * variable2 minus 1. For
 * waitpln: the maneuver timer has run out. For starshipwaitreturnpln and
 * starshipwaitcreatepln: the matching wait order returns nonzero. Any other
 * plan returns 0. Uses the craft in g_paiContext. */
// FUNCTION: XVT 0x403FE0
int16_t pai_IsPlanCompleteForOrderSlot(uint16_t planId, uint16_t orderSlot)
{
	int16_t result;

	result = 0;
	if (strcmp(g_planTable[planId].name, "formldr1pln") == 0 ||
	    strcmp(g_planTable[planId].name, "formevadeldr1pln") == 0 ||
	    strcmp(g_planTable[planId].name, "starshipformpln") == 0 ||
	    strcmp(g_planTable[planId].name, "rendezvous1pln") == 0 ||
	    strcmp(g_planTable[planId].name, "disabledpln") == 0 ||
	    strcmp(g_planTable[planId].name, "waitforboardpln") == 0) {
		if (g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[orderSlot]
			    .variable1 <= g_paiContext.controller->orderProgress
						  .goalProgress[orderSlot]) {
			result = 1;
		}
	} else if (strcmp(g_planTable[planId].name, "capfreeldr1pln") == 0 ||
		   strcmp(g_planTable[planId].name, "capescortersldr1pln") ==
			   0 ||
		   strcmp(g_planTable[planId].name, "caprespondldr1pln") == 0 ||
		   strcmp(g_planTable[planId].name, "escortldr1pln") == 0 ||
		   strcmp(g_planTable[planId].name, "disableldr1pln") == 0 ||
		   strcmp(g_planTable[planId].name, "starshipprotectpln") ==
			   0 ||
		   strcmp(g_planTable[planId].name, "starshipattackpln") == 0 ||
		   strcmp(g_planTable[planId].name, "starshipdisablepln") ==
			   0) {
		if (paifight_SearchOrderSlotRemainingTargets(orderSlot) == 0 &&
		    paifight_OrderSlotHasFutureTargets(orderSlot) == 0) {
			result = 1;
		}
	} else if (strcmp(g_planTable[planId].name, "boardtogivepln") == 0 ||
		   strcmp(g_planTable[planId].name, "boardtotakepln") == 0 ||
		   strcmp(g_planTable[planId].name, "boardtoexchangepln") ==
			   0 ||
		   strcmp(g_planTable[planId].name, "boardtocapturepln") == 0 ||
		   strcmp(g_planTable[planId].name, "boardtodestroypln") == 0 ||
		   strcmp(g_planTable[planId].name, "boardtopickuppln") == 0 ||
		   strcmp(g_planTable[planId].name, "boardtocontactpln") == 0 ||
		   strcmp(g_planTable[planId].name, "boardtorepairpln") == 0) {
		if (g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[orderSlot]
			    .variable2 <= g_paiContext.controller->orderProgress
						  .goalProgress[orderSlot]) {
			result = 1;
		}
	} else if (strcmp(g_planTable[planId].name, "dropoffldr1pln") == 0) {
		if ((unsigned int)g_missionFgStats
			    [(uint16_t)(g_missionFlightGroups
						[g_paiContext
							 .craftFlightGroupIndex]
							.fg.orders[orderSlot]
							.variable2 -
					1)]
				    .outcomeCount[FLIGHT_GROUP_OUTCOME_TOTAL] <=
		    g_paiContext.controller->orderProgress
			    .goalProgress[orderSlot]) {
			result = 1;
		}
	} else if (strcmp(g_planTable[planId].name, "waitpln") == 0) {
		if (g_paiContext.controller->maneuverTimer == 0) {
			result = 1;
		}
	} else if (strcmp(g_planTable[planId].name, "starshipwaitreturnpln") ==
		   0) {
		if (paiorder_waitforallreturnorder() != 0) {
			result = 1;
		}
	} else if (strcmp(g_planTable[planId].name, "starshipwaitcreatepln") ==
			   0 &&
		   paiorder_waitforallcreateorder() != 0) {
		result = 1;
	}
	return result;
}

/* Returns 1 when the plan is one of the eight boardto plans and the order slot
 * has no target left, now or later; else 0. */
// FUNCTION: XVT 0x404380
int16_t pai_IsBoardingPlanCompleteForOrderSlot(uint16_t planId,
					       uint16_t orderSlot)
{
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
	    paifight_SearchOrderSlotRemainingTargets(orderSlot) == 0 &&
	    paifight_OrderSlotHasFutureTargets(orderSlot) == 0) {
		result = 1;
	}
	return result;
}

/* Returns 1 when the object matches the target conditions of the craft's
 * current order slot in g_paiContext: the first two, joined by "or" when
 * target1OrTarget2 is 1, else by "and", or the second two, joined the same way
 * by target3OrTarget4. Else 0. */
// FUNCTION: XVT 0x404450
int16_t pai_CurrentOrderTargetsMatchObject(uint16_t objectIdx)
{
	int16_t primaryMatch;
	int16_t secondaryMatch;
	int16_t match;

	primaryMatch = Mission_ObjectMatchesTriggerVariable(
		objectIdx,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[g_paiContext.orderSlot]
			.target1Type,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[g_paiContext.orderSlot]
			.target1);
	match = Mission_ObjectMatchesTriggerVariable(
		objectIdx,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[g_paiContext.orderSlot]
			.target2Type,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[g_paiContext.orderSlot]
			.target2);
	if (g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
		    .fg.orders[g_paiContext.orderSlot]
		    .target1OrTarget2 == 1) {
		primaryMatch |= match;
	} else {
		primaryMatch &= match;
	}

	secondaryMatch = Mission_ObjectMatchesTriggerVariable(
		objectIdx,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[g_paiContext.orderSlot]
			.secondaryTargetTypes[0],
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[g_paiContext.orderSlot]
			.secondaryTargets[0]);
	match = Mission_ObjectMatchesTriggerVariable(
		objectIdx,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[g_paiContext.orderSlot]
			.secondaryTargetTypes[1],
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[g_paiContext.orderSlot]
			.secondaryTargets[1]);
	if (g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
		    .fg.orders[g_paiContext.orderSlot]
		    .target3OrTarget4 == 1) {
		secondaryMatch |= match;
	} else {
		secondaryMatch &= match;
	}

	return primaryMatch || secondaryMatch;
}

/* Returns the AI skill the craft flies with: the skill in the character data of
 * the object effectiveAiObjectLink points to, while that object still has the
 * saved signature and has character data, else the craft's own aiSkill. Clears
 * effectiveAiObjectLink when the signature no longer matches. */
// FUNCTION: XVT 0x404620
uint16_t pai_GetEffectiveSkillValue(CraftData *craft)
{
	ObjectRecord *linkedObject;

	linkedObject = craft->effectiveAiObjectLink;
	if (linkedObject == 0) {
		return craft->aiSkill;
	}

	if (linkedObject->objectSignature !=
	    craft->effectiveAiObjectSignature) {
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

/* Returns 1 when one of the object's flight group orders in slots 0 to 2 gives
 * a leader the plan named by leaderPlanNameIndex and targets the given object,
 * else 0; returns 0 at once for objectIdx -1. Sets up g_paiContext for the
 * object, and leaves g_paiContext.orderSlot at the matching slot, else at the
 * last slot that gave that plan, else at the craft's current slot. */
// FUNCTION: XVT 0x404670
int pai_SetupContextAndFindOrderPlanOnTarget(int objectIdx,
					     int leaderPlanNameIndex,
					     int targetObjIdx)
{
	unsigned int orderSlot;

	if (objectIdx == -1) {
		return 0;
	}
	pai_setupcraftcontext(objectIdx);
	for (orderSlot = 0; orderSlot < 3; ++orderSlot) {
		uint8_t order = g_missionFlightGroups[g_objectTable[objectIdx]
							      .flightGroupIdx]
					.fg.orders[orderSlot]
					.order;
		if (g_orderLeaderBuiltinPlanNameIndex[order] ==
		    leaderPlanNameIndex) {
			g_paiContext.orderSlot = (uint16_t)orderSlot;
			if (pai_CurrentOrderTargetsMatchObject(targetObjIdx) !=
			    0) {
				return 1;
			}
		}
	}
	return 0;
}

/* Returns the index of the first g_planTable entry with the name, compared up
 * to 80 characters, or 256 when none has it. An empty name finds the first free
 * entry. */
// FUNCTION: XVT 0x46AC80
int pai_FindPlanTableIndexByName(const char *planName)
{
	const PaiPlanRecord *plan = g_planTable;
	unsigned int planIndex;

	for (planIndex = 0; planIndex < 256; ++plan, ++planIndex) {
		if (strncmp(plan->name, planName, sizeof(plan->name)) == 0) {
			break;
		}
	}
	return planIndex;
}

/* Returns the index of the first g_planTable entry with an empty name, or 256
 * when none is free. */
// FUNCTION: XVT 0x46ACB0
int pai_FindFreePlanTableIndex(void)
{
	int planIndex;

	for (planIndex = 0; planIndex < 256; ++planIndex) {
		if (g_planTable[planIndex].name[0] == '\0') {
			break;
		}
	}
	return planIndex;
}

/* Returns the index of the target token with the name in g_paiTargetTokenDefs,
 * or the index of its empty end entry when none has it. */
// FUNCTION: XVT 0x46ACD0
int pai_FindTargetTokenIndex(const char *token)
{
	int tokenIndex = 0;

	if (g_paiTargetTokenDefs[0].name[0] != '\0') {
		struct PaiPlanTokenDef *tokenDef = g_paiTargetTokenDefs;

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

/* Returns the index of the maneuver token with the name in
 * g_paiManeuverTokenDefs, or the index of its empty end entry when none has
 * it. */
// FUNCTION: XVT 0x46AD30
int pai_FindManeuverTokenIndex(const char *token)
{
	int tokenIndex = 0;

	if (g_paiManeuverTokenDefs[0].name[0] != '\0') {
		struct PaiPlanTokenDef *tokenDef = g_paiManeuverTokenDefs;

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

/* Returns the index of the order token with the name in g_paiOrderTokenDefs, or
 * the index of its empty end entry when none has it. */
// FUNCTION: XVT 0x46AD90
int pai_FindOrderTokenIndex(const char *token)
{
	int tokenIndex = 0;

	if (g_paiOrderTokenDefs[0].name[0] != '\0') {
		struct PaiPlanTokenDef *tokenDef = g_paiOrderTokenDefs;

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

/* Reads the next token of the plan text into token: it skips spaces, tabs,
 * newlines and commas, and from a semicolon to the end of the line, then copies
 * characters up to a space, tab, newline, comma or the end of the file. Returns
 * 1 on every path; at the end of the file the token is empty. Does not check
 * the token's length. */
// FUNCTION: XVT 0x46ADF0
int pai_ReadPlanTextToken(char *token, XvtFile *stream)
{
	int tokenLength = 1;
	char readChar;

	*token = '\0';
	for (;;) {
		do {
			if (File_RawRead(&readChar, 1, 1, stream) != 1) {
				return 1;
			}
		} while (readChar == ' ' || readChar == '\t' ||
			 readChar == '\n' || readChar == ',' ||
			 readChar == '\n');

		if (readChar != ';') {
			break;
		}

		do {
			if (File_RawRead(&readChar, 1, 1, stream) != 1) {
				return 1;
			}
		} while (readChar != '\n');
	}

	*token = readChar;
	for (;;) {
		if (File_RawRead(&readChar, 1, 1, stream) != 1) {
			token[tokenLength] = '\0';
			return 1;
		}
		if (readChar == ',' || readChar == '\n' || readChar == ' ' ||
		    readChar == '\t' || readChar == '\n') {
			break;
		}
		token[tokenLength] = readChar;
		++tokenLength;
	}
	token[tokenLength] = '\0';
	return 1;
}

/* Compiles the plan text "<baseName>.pln" into g_planTable, g_planOrderData and
 * g_planDataPtrs, then writes them to "<baseName>.plo". A plan is its name, a
 * target token, a maneuver token, then pairs of an order token and a plan name,
 * ended by NULLORDR; a "*" ends the file. A plan named before it is defined
 * gets an entry marked not defined. Returns 0 when the file does not open, a
 * plan is defined twice, the table is full, a token is unknown, the text ends
 * before the "*", or a named plan is never defined; else 1, whether or not the
 * .plo file could be written. Adds 1 to g_planCount for each plan. The .plo
 * file holds the table's size and the table, then 0xFFFF and the first 0xFFFF
 * bytes of g_planOrderData. Does not check that the plans fit in
 * g_planOrderData. */
// FUNCTION: XVT 0x46AED0
int pai_CompilePlansFromText(const char *baseName)
{
	char fileName[256];
	char token[256];
	XvtFile *stream;
	uint8_t *cursor;
	int planIndex;

	strcpy(fileName, baseName);
	strcat(fileName, ".pln");
	FeDiskIo_OpenGlobalStream(fileName, "r", 0, 0);
	stream = (XvtFile *)g_stream;
	if (stream == NULL) {
		return 0;
	}

	cursor = g_planOrderData;
	for (;;) {
		int targetIndex;
		int maneuverIndex;

		if (pai_ReadPlanTextToken(token, stream) == 0) {
			File_RawClose(stream);
			return 0;
		}
		if (token[0] == '*') {
			break;
		}

		planIndex = pai_FindPlanTableIndexByName(token);
		if (planIndex != 256) {
			if (g_planTable[planIndex].isDefined == 1) {
				File_RawClose(stream);
				return 0;
			}
		} else {
			planIndex = pai_FindFreePlanTableIndex();
			if (planIndex == 256) {
				File_RawClose(stream);
				return 0;
			}
		}

		strncpy(g_planTable[planIndex].name, token,
			sizeof(g_planTable[planIndex].name));
		g_planTable[planIndex].name[79] = '\0';
		g_planTable[planIndex].isDefined = 1;
		g_planTable[planIndex].dataOffset =
			(uint32_t)(cursor - g_planOrderData);
		g_planDataPtrs[planIndex] = cursor;

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
		*cursor++ =
			(uint8_t)g_paiManeuverTokenDefs[maneuverIndex].value;

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

			*cursor++ =
				(uint8_t)g_paiOrderTokenDefs[orderIndex].value;
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
			strncpy(g_planTable[freePlanId].name, token,
				sizeof(g_planTable[freePlanId].name));
			g_planTable[freePlanId].name[79] = '\0';
			g_planTable[freePlanId].isDefined = 0;
			*cursor++ = (uint8_t)freePlanId;
		}
	}

	File_RawClose(stream);
	for (planIndex = 0; planIndex < 256; ++planIndex) {
		if (g_planTable[planIndex].name[0] != '\0' &&
		    g_planTable[planIndex].isDefined != 1) {
			return 0;
		}
	}

	strcpy(fileName, baseName);
	strcat(fileName, ".plo");
	FeDiskIo_OpenGlobalStream(fileName, "wb", 0, 1);
	stream = (XvtFile *)g_stream;
	if (stream != NULL) {
		/* From here the same local holds the byte size of each section of the .plo file; each size is written
		 * just before its section. */
		planIndex = (int)sizeof(g_planTable);
		File_RawWrite(&planIndex, sizeof(planIndex), 1, stream);
		File_RawWrite(g_planTable, (size_t)planIndex, 1, stream);
		planIndex = 0xFFFF;
		File_RawWrite(&planIndex, sizeof(planIndex), 1, stream);
		File_RawWrite(g_planOrderData, (size_t)planIndex, 1, stream);
		File_RawClose(stream);
	}

	return 1;
}

/* Loads the AI plans: clears g_planTable and g_planCount and zeroes the first
 * 256 bytes of g_planDataPtrs, then reads "<baseName>.plo": a size and that
 * many bytes into g_planTable, then a size and that many bytes into
 * g_planOrderData. When the file does not open or a read fails, returns what
 * pai_CompilePlansFromText returns. Else points g_planDataPtrs at each named
 * plan's bytes, sets g_planCount to the number of named plans and returns 1.
 * Does not check either size against its array. */
// FUNCTION: XVT 0x46B3D0
int pai_loadplans(char *baseName)
{
	char fileName[256];
	uint32_t bufferSize;
	XvtFile *stream;
	int planIndex;
	int planCount;

	strcpy(fileName, baseName);
	strcat(fileName, ".plo");
	memset(g_planTable, 0, sizeof(g_planTable));
	g_planCount = 0;
	memset(g_planDataPtrs, 0, 0x100);
	FeDiskIo_OpenGlobalStream(fileName, g_fileModeReadBinary, 0, 1);
	stream = (XvtFile *)g_stream;
	if (stream == NULL) {
		return pai_CompilePlansFromText(baseName);
	}

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
			g_planDataPtrs[planIndex] =
				&g_planOrderData[g_planTable[planIndex]
							 .dataOffset];
		}
		g_planCount = planCount;
		++planIndex;
	} while (planIndex < 256);

	return 1;
}

/* Fills g_builtinPlanIdByNameIndex with the plan id of each name in
 * g_builtinPlanNameTable; a name with no plan gets 0, the not-found index 256
 * cut to 8 bits. */
// FUNCTION: XVT 0x46B5B0
void pai_cacheBuiltinPlanIds(void)
{
	int planNameOrdinal;
	const char *const *planNameCursor;
	const char *planName;

	planName = g_builtinPlanNameTable[0];
	planNameOrdinal = 0;
	if (*planName != '\0') {
		planNameCursor = g_builtinPlanNameTable;
		do {
			planNameCursor++;
			g_builtinPlanIdByNameIndex[planNameOrdinal++] =
				(uint8_t)pai_FindPlanTableIndexByName(planName);
			planName = *planNameCursor;
		} while (*planName != '\0');
	}
}

/* Returns where the named plan's bytes start. Does not check that the plan
 * exists: for an unknown name it reads g_planDataPtrs[256], past the end of the
 * array. */
// FUNCTION: XVT 0x46B5F0
uint8_t *pai_getplandataptrbyname(const char *planName)
{
	return g_planDataPtrs[pai_FindPlanTableIndexByName(planName)];
}

/* Returns the plan id of the first g_planTable entry with the name, compared up
 * to 80 characters, or 0 when none has it. */
// FUNCTION: XVT 0x46B610
int pai_FindPlanIdByNameOrZero(const char *planName)
{
	int planIndex;

	for (planIndex = 0; planIndex < 256; ++planIndex) {
		if (strncmp(g_planTable[planIndex].name, planName,
			    sizeof(g_planTable[planIndex].name)) == 0) {
			return planIndex;
		}
	}
	return 0;
}
