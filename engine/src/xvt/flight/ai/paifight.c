#include "xvt/flight/ai/paifight.h"
#include "xvt/flight/ai/pai_targetability.h"

#include "xvt/assets/model_mesh.h"
#include "xvt/flight/ai/paiorder.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"

#include <limits.h>
#include <string.h>

/* Flight group of the nearest object paifight_searchforclosestingroup found,
 * which only it writes; paifight_checkescortorder copies it into
 * escortTargetFG. */
// GLOBAL: XVT 0x9A1FDC
uint8_t g_aiEscortCandidateFgIdx = 0;
/* Two marks per object slot, 1 when the object may be a turret target: element
 * 2 times the slot plus the set number, set 0 for an order's first pair of
 * target conditions and set 1 for its second.
 * paifight_BuildGunnerTargetCandidateSet writes it;
 * paifight_FindNearestGunnerTargetInCandidateSet reads it. */
// GLOBAL: XVT 0x99F930
uint8_t g_paifightGunnerTargetCandidateSet[976] = {0};
/* X of the point turret and mine target searches measure from, in world units:
 * a turret's hardpoint in world space, the craft's position, or a mine's
 * position. Written by paifight_missiledefenseorder,
 * paifight_gunnerselfdefenseorder, paifight_gunneroffenseorder and
 * laser_UpdateMineWeaponFire. */
// GLOBAL: XVT 0xA08144
int g_paifightSearchOriginX = 0;
/* Y of the point turret and mine target searches measure from; see
 * g_paifightSearchOriginX. */
// GLOBAL: XVT 0xA08140
int g_paifightSearchOriginY = 0;
/* Z of the point turret and mine target searches measure from; see
 * g_paifightSearchOriginX. */
// GLOBAL: XVT 0xA08148
int g_paifightSearchOriginZ = 0;
/* Counts the line-of-fire tests paifight_FindNearestGunnerTargetInCandidateSet
 * runs; set to 0 each time the simulation is run up to a new target time, by
 * Flight_StepSimToTime in the original build and XvtFlightSim_StepToTime in the
 * modern one. Nothing reads it. */
// GLOBAL: XVT 0xA8F750
int g_gunnerCollisionProbeCount = 0;
/* Distance in world units, by skill tier 0 to 2, within which
 * paifight_fightershootorder fires the cannons, before its changes for the
 * target's speed and size. */
// GLOBAL: XVT 0x524290
const unsigned int g_aiFighterShootMaxRangeBySkill[3] = {0x6000, 0x8000,
							 0xA000};
/* Shots in each cannon burst paifight_fightershootorder sets, by skill tier 0
 * to 2; entry 3 is 0. */
// GLOBAL: XVT 0x52429C
const uint8_t g_aiFighterShootBurstLengthBySkill[4] = {3, 4, 5, 0};

/* Order 9: returns 1 after giving the craft a target, with its signature and
 * hasLiveTarget 1, else 0; only while the craft is on its plan's maneuver. The
 * candidate target comes first when there is one (not 0xFFFF or
 * AI_TARGET_ABORT) and it can be targeted; one that cannot is cleared. Else it
 * searches by the current plan, with all three targetSearchFlags (7) and
 * requireUndisabledTarget set for disableldr1pln: the nearest order target on
 * capfreeldr1pln, disableldr1pln or kamikaze1pln, the nearest escort leader on
 * capescortersldr1pln, else the nearest attacker of an order target. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x45C370
int16_t paifight_scanfortargetorder(void)
{
	enum { TARGET_SEARCH_ALL_REQUIREMENTS = 7 };

	uint16_t candidateTargetIdx;
	int16_t targetObject;
	PaiPlanRecord *plan;

	if (g_paiContext.controller->maneuverMode ==
	    g_paiContext.initialManeuverId) {
		candidateTargetIdx =
			g_paiContext.controller->candidateTargetIdx;
		if (candidateTargetIdx != UINT16_MAX &&
		    candidateTargetIdx != AI_TARGET_ABORT) {
			int validTarget;

			validTarget =
				pai_IsObjectTargetable(candidateTargetIdx);
			if (validTarget != 0) {
				g_paiContext.controller->targetObjIdx =
					candidateTargetIdx;
				g_paiContext.controller->targetSignature =
					g_objectTable[candidateTargetIdx]
						.objectSignature;
				g_paiContext.controller->hasLiveTarget = 1;
				return 1;
			}
			g_paiContext.controller->candidateTargetIdx =
				UINT16_MAX;
		}

		plan = &g_planTable[g_paiContext.controller->currentPlanId];
		if (strcmp(plan->name, "disableldr1pln") == 0) {
			g_paiContext.requireUndisabledTarget = 1;
		} else {
			g_paiContext.requireUndisabledTarget = 0;
		}
		g_paiContext.targetSearchFlags = TARGET_SEARCH_ALL_REQUIREMENTS;
		if (strcmp(plan->name, "capfreeldr1pln") == 0 ||
		    strcmp(plan->name, "disableldr1pln") == 0 ||
		    strcmp(plan->name, "kamikaze1pln") == 0) {
			targetObject = paifight_FindAttackOrderTargetFromOrder(
				g_paiContext.orderSlot);
		} else if (strcmp(plan->name, "capescortersldr1pln") == 0) {
			targetObject = paifight_TargetEscortLeaderFromOrder(
				g_paiContext.orderSlot);
		} else {
			targetObject =
				paifight_FindAttackerOfOrderTargetFromOrder(
					g_paiContext.orderSlot);
		}
		if (targetObject != -1) {
			g_paiContext.controller->targetObjIdx =
				(uint16_t)targetObject;
			g_paiContext.controller->targetSignature =
				g_objectTable[(uint16_t)targetObject]
					.objectSignature;
			g_paiContext.controller->hasLiveTarget = 1;
			return 1;
		}
	}
	return 0;
}

/* Returns what paifight_FindNearestAttackOrderTarget finds for the order slot's
 * first pair of target conditions, or when that is -1, for its second pair. */
// FUNCTION: XVT 0x45C630
int16_t paifight_FindAttackOrderTargetFromOrder(uint16_t orderSlot)
{
	int orderIndex;
	int16_t result;

	orderIndex = orderSlot;
	result = paifight_FindNearestAttackOrderTarget(
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target1Type,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target1,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target1OrTarget2,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target2Type,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target2);
	if (result == -1) {
		result = paifight_FindNearestAttackOrderTarget(
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.secondaryTargetTypes[0],
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.secondaryTargets[0],
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.target3OrTarget4,
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.secondaryTargetTypes[1],
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.secondaryTargets[1]);
	}
	return result;
}

/* Returns the nearest object that matches the target conditions (either one
 * when targetOrMode is 1, else both), or -1 when none qualifies. A craft must
 * be outside the craft's own flight group, not the one the player told it to
 * avoid, targetable, in working order and not captured by its own team when
 * requireUndisabledTarget is set, and within skill range when targetSearchFlags
 * has 4; a static object with behavior flag 2 must be targetable and, with flag
 * 4, within range. It first counts these; with none it returns -1. Of them it
 * takes the nearest that passes paifight_TargetHasAttackCapacity when flag 1 is
 * set, measuring craft from the search origin in g_paiContext when flag 0x20 is
 * set, else from the craft, and skipping a craft farther than 0x4000 with its
 * decoy beam on. Sets g_lastRoughDistance. */
// FUNCTION: XVT 0x45C720
int16_t paifight_FindNearestAttackOrderTarget(int16_t target1Type,
					      uint16_t target1,
					      int16_t targetOrMode,
					      int16_t target2Type,
					      uint16_t target2)
{
	enum {
		TARGET_RELATION_OR = 1,
		TARGETABLE_STATIC_MODEL_FLAG = 2,
		TARGET_SEARCH_REQUIRE_CAPACITY = 1,
		TARGET_SEARCH_REQUIRE_ORDER_RANGE = 4,
		TARGET_SEARCH_USE_ORIGIN = 0x20,
		ACTIVE_DECOY_IGNORE_RANGE = 0x4000
	};

	int candidateCount = 0;
	int16_t trigger1Matches;
	int16_t trigger2Matches;
	uint16_t bestObject;
	unsigned int bestScore;
	MobileObject *mobileObject;
	CraftData *craft;
	int validTarget;

	{
		uint16_t objectIndex;

		for (objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
		     objectIndex < (int)g_activeRegionCraftObjectSlotEnd;
		     ++objectIndex) {
			int objectArrayIndex;

			objectArrayIndex = objectIndex;
			if (g_objectTable[objectArrayIndex].objectType == 0 ||
			    g_objectTable[objectArrayIndex].flightGroupIdx ==
				    g_paiContext.craftFlightGroupIndex) {
				continue;
			}
			trigger1Matches = Mission_ObjectMatchesTriggerVariable(
				objectIndex, target1Type, target1);
			trigger2Matches = Mission_ObjectMatchesTriggerVariable(
				objectIndex, target2Type, target2);
			if (targetOrMode == TARGET_RELATION_OR) {
				trigger1Matches |= trigger2Matches;
			} else {
				trigger1Matches &= trigger2Matches;
			}
			if (trigger1Matches == 0 ||
			    g_curCraft->playerCommandAvoidTargetObjIdx ==
				    objectIndex) {
				continue;
			}

			validTarget = pai_IsObjectTargetable(objectIndex);
			if (validTarget == 0) {
				continue;
			}
			mobileObject = g_objectTable[objectArrayIndex].mobj;
			craft = mobileObject->pCraft;
			if ((g_paiContext.requireUndisabledTarget == 0 ||
			     (craft->workingSubsystems != 0 &&
			      (g_paiContext.requireUndisabledTarget == 0 ||
			       craft->capturedByFlightGroup == 0 ||
			       g_objectTable[g_paiContext.objectIndex]
					       .mobj->team !=
				       g_objectTable[objectArrayIndex]
					       .mobj->team))) &&
			    ((g_paiContext.targetSearchFlags &
			      TARGET_SEARCH_REQUIRE_ORDER_RANGE) == 0 ||
			     pai_IsObjectWithinSkillRangeOfCraft(
				     objectIndex))) {
				++candidateCount;
			}
		}
	}

	{
		uint16_t objectIndex;

		for (objectIndex = (uint16_t)g_regionMainObjectSlotEnd;
		     objectIndex <
		     g_regionStaticObjectSlotCount + g_regionMainObjectSlotEnd;
		     ++objectIndex) {
			int objectArrayIndex;
			uint16_t objectType;

			objectArrayIndex = objectIndex;
			objectType = g_objectTable[objectArrayIndex].objectType;
			if (objectType == 0 ||
			    (g_objectTypeTable[objectType].behaviorFlags &
			     TARGETABLE_STATIC_MODEL_FLAG) == 0) {
				continue;
			}
			trigger1Matches = Mission_ObjectMatchesTriggerVariable(
				objectIndex, target1Type, target1);
			trigger2Matches = Mission_ObjectMatchesTriggerVariable(
				objectIndex, target2Type, target2);
			if (targetOrMode == TARGET_RELATION_OR) {
				trigger1Matches |= trigger2Matches;
			} else {
				trigger1Matches &= trigger2Matches;
			}
			if (trigger1Matches == 0 ||
			    g_curCraft->playerCommandAvoidTargetObjIdx ==
				    objectIndex) {
				continue;
			}

			validTarget = pai_IsObjectTargetable(objectIndex);
			if (validTarget != 0 &&
			    ((g_paiContext.targetSearchFlags &
			      TARGET_SEARCH_REQUIRE_ORDER_RANGE) == 0 ||
			     pai_IsObjectWithinSkillRangeOfCraft(
				     objectIndex))) {
				++candidateCount;
			}
		}
	}

	if (candidateCount == 0) {
		return -1;
	}
	bestScore = UINT_MAX;
	bestObject = UINT16_MAX;

	{
		uint16_t objectIndex;

		for (objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
		     objectIndex < (int)g_activeRegionCraftObjectSlotEnd;
		     ++objectIndex) {
			int objectArrayIndex;

			objectArrayIndex = objectIndex;
			if (g_objectTable[objectArrayIndex].objectType == 0 ||
			    g_objectTable[objectArrayIndex].flightGroupIdx ==
				    g_paiContext.craftFlightGroupIndex) {
				continue;
			}
			trigger1Matches = Mission_ObjectMatchesTriggerVariable(
				objectIndex, target1Type, target1);
			trigger2Matches = Mission_ObjectMatchesTriggerVariable(
				objectIndex, target2Type, target2);
			if (targetOrMode == TARGET_RELATION_OR) {
				trigger1Matches |= trigger2Matches;
			} else {
				trigger1Matches &= trigger2Matches;
			}
			if (trigger1Matches == 0 ||
			    g_curCraft->playerCommandAvoidTargetObjIdx ==
				    objectIndex) {
				continue;
			}

			validTarget = pai_IsObjectTargetable(objectIndex);
			if (validTarget == 0) {
				continue;
			}
			mobileObject = g_objectTable[objectArrayIndex].mobj;
			craft = mobileObject->pCraft;
			if ((g_paiContext.requireUndisabledTarget == 0 ||
			     (craft->workingSubsystems != 0 &&
			      (g_paiContext.requireUndisabledTarget == 0 ||
			       craft->capturedByFlightGroup == 0 ||
			       g_objectTable[g_paiContext.objectIndex]
					       .mobj->team !=
				       g_objectTable[objectArrayIndex]
					       .mobj->team))) &&
			    ((g_paiContext.targetSearchFlags &
			      TARGET_SEARCH_REQUIRE_ORDER_RANGE) == 0 ||
			     pai_IsObjectWithinSkillRangeOfCraft(
				     objectIndex)) &&
			    ((g_paiContext.targetSearchFlags &
			      TARGET_SEARCH_REQUIRE_CAPACITY) == 0 ||
			     paifight_TargetHasAttackCapacity(
				     objectIndex, (uint16_t)candidateCount))) {
				if ((g_paiContext.targetSearchFlags &
				     TARGET_SEARCH_USE_ORIGIN) != 0) {
					g_lastRoughDistance = collide_roughdistance3d(
						g_objectTable[objectArrayIndex]
								.world_x -
							g_paiContext
								.targetSearchOriginX,
						g_objectTable[objectArrayIndex]
								.world_y -
							g_paiContext
								.targetSearchOriginY,
						g_objectTable[objectArrayIndex]
								.world_z -
							g_paiContext
								.targetSearchOriginZ);
				} else {
					pai_ObjectRefUpdateRoughDistance(
						g_paiContext.objectIndex,
						objectIndex);
				}
				if ((g_lastRoughDistance <=
					     ACTIVE_DECOY_IGNORE_RANGE ||
				     Object_HasActiveDecoyBeam(objectIndex) !=
					     1) &&
				    bestScore >
					    (unsigned int)g_lastRoughDistance) {
					bestScore = g_lastRoughDistance;
					bestObject = objectIndex;
				}
			}
		}
	}

	{
		uint16_t objectIndex;

		for (objectIndex = (uint16_t)g_regionMainObjectSlotEnd;
		     objectIndex <
		     g_regionStaticObjectSlotCount + g_regionMainObjectSlotEnd;
		     ++objectIndex) {
			int objectArrayIndex;
			uint16_t objectType;

			objectArrayIndex = objectIndex;
			objectType = g_objectTable[objectArrayIndex].objectType;
			if (objectType == 0 ||
			    (g_objectTypeTable[objectType].behaviorFlags &
			     TARGETABLE_STATIC_MODEL_FLAG) == 0) {
				continue;
			}
			trigger1Matches = Mission_ObjectMatchesTriggerVariable(
				objectIndex, target1Type, target1);
			trigger2Matches = Mission_ObjectMatchesTriggerVariable(
				objectIndex, target2Type, target2);
			if (targetOrMode == TARGET_RELATION_OR) {
				trigger1Matches |= trigger2Matches;
			} else {
				trigger1Matches &= trigger2Matches;
			}
			if (trigger1Matches == 0 ||
			    g_curCraft->playerCommandAvoidTargetObjIdx ==
				    objectIndex) {
				continue;
			}

			validTarget = pai_IsObjectTargetable(objectIndex);
			if (validTarget != 0 &&
			    ((g_paiContext.targetSearchFlags &
			      TARGET_SEARCH_REQUIRE_ORDER_RANGE) == 0 ||
			     pai_IsObjectWithinSkillRangeOfCraft(
				     objectIndex)) &&
			    ((g_paiContext.targetSearchFlags &
			      TARGET_SEARCH_REQUIRE_CAPACITY) == 0 ||
			     paifight_TargetHasAttackCapacity(
				     objectIndex, (uint16_t)candidateCount))) {
				pai_ObjectRefUpdateRoughDistance(
					g_paiContext.objectIndex, objectIndex);
				if (bestScore >
				    (unsigned int)g_lastRoughDistance) {
					bestScore = g_lastRoughDistance;
					bestObject = objectIndex;
				}
			}
		}
	}
	return (int16_t)bestObject;
}

/* Returns what paifight_TargetNearestEscortLeader finds for the order slot's
 * first pair of target conditions, or when that is -1, for its second pair. */
// FUNCTION: XVT 0x45D1C0
int16_t paifight_TargetEscortLeaderFromOrder(uint16_t orderSlot)
{
	int orderIndex;
	int16_t result;

	orderIndex = orderSlot;
	result = paifight_TargetNearestEscortLeader(
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target1Type,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target1,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target1OrTarget2,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target2Type,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target2);
	if (result == -1) {
		result = paifight_TargetNearestEscortLeader(
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.secondaryTargetTypes[0],
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.secondaryTargets[0],
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.target3OrTarget4,
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.secondaryTargetTypes[1],
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.secondaryTargets[1]);
	}
	return result;
}

/* Returns the nearest craft on escortldr1pln whose escortTargetFG is a flight
 * group that matches the target conditions (either one when targetRelationOp is
 * 1, else both), or -1. The craft must not be the one the player told this
 * craft to avoid, must be targetable, within skill range when targetSearchFlags
 * has 4, and pass paifight_TargetHasAttackCapacity when it has 1. Makes the one
 * it finds the controller's target, with its signature and hasLiveTarget 1.
 * Sets g_lastRoughDistance. */
// FUNCTION: XVT 0x45D2B0
int16_t paifight_TargetNearestEscortLeader(int16_t target1Type,
					   uint16_t target1,
					   int16_t targetRelationOp,
					   int16_t target2Type,
					   uint16_t target2)
{
	uint16_t flightGroupIdx;
	uint16_t bestObjectIndex;
	unsigned int bestRangeScore;
	int16_t matchesTarget1;
	int16_t matchesTarget2;
	uint16_t objectIndex;
	CraftData *craft;
	int validTarget;

	bestObjectIndex = UINT16_MAX;
	bestRangeScore = UINT32_MAX;
	flightGroupIdx = 0;
	if ((int16_t)g_missionHeader.numFlightGroups > 0) {
		do {
			matchesTarget1 =
				Mission_FlightGroupMatchesTriggerVariable(
					flightGroupIdx, target1Type, target1);
			matchesTarget2 =
				Mission_FlightGroupMatchesTriggerVariable(
					flightGroupIdx, target2Type, target2);
			if (targetRelationOp == 1) {
				matchesTarget1 |= matchesTarget2;
			} else {
				matchesTarget1 &= matchesTarget2;
			}

			if (matchesTarget1 != 0) {
				objectIndex =
					(uint16_t)g_activeRegionObjectSlotStart;
				while (objectIndex <
				       (int)g_activeRegionCraftObjectSlotEnd) {
					if (g_objectTable[objectIndex]
						    .objectType != 0) {
						craft = g_objectTable
								[objectIndex]
									.mobj
									->pCraft;
						if (strcmp(g_planTable
								   [craft->aiController
									    .currentPlanId]
									   .name,
							   "escortldr1pln") ==
							    0 &&
						    craft->aiController
								    .escortTargetFG ==
							    flightGroupIdx &&
						    g_curCraft->playerCommandAvoidTargetObjIdx !=
							    objectIndex) {
							validTarget = pai_IsObjectTargetable(
								objectIndex);

							if (validTarget != 0 &&
							    ((g_paiContext
								      .targetSearchFlags &
							      4) == 0 ||
							     pai_IsObjectWithinSkillRangeOfCraft(
								     objectIndex)) &&
							    ((g_paiContext
								      .targetSearchFlags &
							      1) == 0 ||
							     paifight_TargetHasAttackCapacity(
								     objectIndex,
								     UINT8_MAX))) {
								pai_ObjectRefUpdateRoughDistance(
									g_paiContext
										.objectIndex,
									objectIndex);
								if (bestRangeScore >
								    (unsigned int)
									    g_lastRoughDistance) {
									bestObjectIndex =
										objectIndex;
									bestRangeScore =
										g_lastRoughDistance;
								}
							}
						}
					}
					++objectIndex;
				}
			}
			++flightGroupIdx;
		} while (flightGroupIdx <
			 (int16_t)g_missionHeader.numFlightGroups);
	}

	if (bestObjectIndex != UINT16_MAX) {
		g_paiContext.controller->targetObjIdx = bestObjectIndex;
		g_paiContext.controller->targetSignature =
			g_objectTable[bestObjectIndex].objectSignature;
		g_paiContext.controller->hasLiveTarget = 1;
	}
	return bestObjectIndex;
}

/* Returns what paifight_FindNearestAttackerOfMatchingTarget finds for the order
 * slot's first pair of target conditions, or when that is -1, for its second
 * pair. */
// FUNCTION: XVT 0x45D5F0
int16_t paifight_FindAttackerOfOrderTargetFromOrder(uint16_t orderSlot)
{
	int16_t result;

	result = paifight_FindNearestAttackerOfMatchingTarget(
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
		result = paifight_FindNearestAttackerOfMatchingTarget(
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

/* Returns the nearest craft attacking a craft that matches the target
 * conditions (either one when targetOrMode is 1, else both) and that some team
 * has attacked; -1 when none. An attacker is a craft in setup attack, attack or
 * rocket attack on that craft, or its aiFlight.threatObjIdx; it is skipped when
 * the player told this craft to avoid the attacked craft. It must be
 * targetable, an enemy of the craft's team when a player flies it, within skill
 * range when targetSearchFlags has 4, and pass paifight_TargetHasAttackCapacity
 * when it has 1. It is measured from the search origin in g_paiContext with
 * flag 0x20, else from the craft; with flag 0x10 it must also lie within
 * AI_TARGET_RANGE_MAX and be an enemy. Sets g_lastRoughDistance. */
// FUNCTION: XVT 0x45D6E0
int16_t paifight_FindNearestAttackerOfMatchingTarget(int16_t target1Type,
						     uint16_t target1,
						     int16_t targetOrMode,
						     int16_t target2Type,
						     uint16_t target2)
{
	uint16_t targetObjectIndex;
	uint16_t bestObjectIndex;
	unsigned int bestRangeScore;

	targetObjectIndex = (uint16_t)g_activeRegionObjectSlotStart;
	bestRangeScore = UINT32_MAX;
	bestObjectIndex = UINT16_MAX;
	while (targetObjectIndex < (int)g_activeRegionCraftObjectSlotEnd) {
		if (g_objectTable[targetObjectIndex].objectType != 0) {
			uint16_t teamIndex;
			CraftData *targetCraft;
			int16_t hasAttacker;
			int16_t matchesTarget1;
			int16_t matchesTarget2;

			teamIndex = 0;
			targetCraft =
				g_objectTable[targetObjectIndex].mobj->pCraft;
			hasAttacker = 0;
			do {
				if (targetCraft->attackedByTeam[teamIndex] !=
				    0) {
					hasAttacker = 1;
				}
				++teamIndex;
			} while (teamIndex < 10);

			if (hasAttacker != 0) {
				matchesTarget1 =
					Mission_ObjectMatchesTriggerVariable(
						targetObjectIndex, target1Type,
						target1);
				matchesTarget2 =
					Mission_ObjectMatchesTriggerVariable(
						targetObjectIndex, target2Type,
						target2);
				if (targetOrMode == 1) {
					matchesTarget1 |= matchesTarget2;
				} else {
					matchesTarget1 &= matchesTarget2;
				}

				if (matchesTarget1 != 0) {
					uint16_t objectIndex;

					objectIndex = (uint16_t)
						g_activeRegionObjectSlotStart;
					while (objectIndex <
					       (int)g_activeRegionCraftObjectSlotEnd) {
						CraftData *craft;
						AiController *controller;
						int16_t maneuverMode;

						if (g_objectTable[objectIndex]
							    .objectType != 0) {
							craft = g_objectTable[objectIndex]
									.mobj
									->pCraft;
							controller =
								&craft->aiController;
							maneuverMode =
								controller
									->maneuverMode;
							if ((((maneuverMode ==
								       AI_MANEUVER_MODE_SETUP_ATTACK ||
							       maneuverMode ==
								       AI_MANEUVER_MODE_ATTACK ||
							       maneuverMode ==
								       AI_MANEUVER_MODE_ROCKET_ATTACK) &&
							      controller->targetObjIdx ==
								      targetObjectIndex) ||
							     targetCraft->aiFlight
									     .threatObjIdx ==
								     objectIndex) &&
							    g_curCraft->playerCommandAvoidTargetObjIdx !=
								    targetObjectIndex) {
								int validTarget;

								validTarget = pai_IsObjectTargetable(
									objectIndex);

								if (validTarget !=
								    0) {
									if (g_objectTable[objectIndex]
										    .playerOwnerIdx !=
									    -1) {
										int objectTeam;
										int sourceTeam;

										objectTeam =
											g_missionFlightGroups
												[g_objectTable[objectIndex]
													 .flightGroupIdx]
													.fg
													.team;
										sourceTeam =
											g_objectTable[g_paiContext
													      .objectIndex]
												.mobj
												->team;
										validTarget =
											objectTeam == sourceTeam
												? 0
												: g_missionTeams[sourceTeam]
														  .allies[objectTeam] <
													  1;
									}

									if (validTarget !=
										    0 &&
									    ((g_paiContext
										      .targetSearchFlags &
									      4) == 0 ||
									     pai_IsObjectWithinSkillRangeOfCraft(
										     objectIndex)) &&
									    ((g_paiContext
										      .targetSearchFlags &
									      1) == 0 ||
									     paifight_TargetHasAttackCapacity(
										     objectIndex,
										     UINT8_MAX))) {
										if ((g_paiContext
											     .targetSearchFlags &
										     0x20) !=
										    0) {
											g_lastRoughDistance = collide_roughdistance3d(
												g_objectTable[objectIndex]
														.world_x -
													g_paiContext
														.targetSearchOriginX,
												g_objectTable[objectIndex]
														.world_y -
													g_paiContext
														.targetSearchOriginY,
												g_objectTable[objectIndex]
														.world_z -
													g_paiContext
														.targetSearchOriginZ);
										} else {
											pai_ObjectRefUpdateRoughDistance(
												g_paiContext
													.objectIndex,
												objectIndex);
										}

										if ((g_paiContext
											     .targetSearchFlags &
										     0x10) !=
										    0) {
											if (g_lastRoughDistance >
											    AI_TARGET_RANGE_MAX) {
												validTarget =
													0;
											} else {
												int objectTeam;
												int sourceTeam;

												objectTeam =
													g_missionFlightGroups
														[g_objectTable[objectIndex]
															 .flightGroupIdx]
															.fg
															.team;
												sourceTeam =
													g_objectTable[g_paiContext
															      .objectIndex]
														.mobj
														->team;
												validTarget =
													objectTeam == sourceTeam
														? 0
														: g_missionTeams[sourceTeam]
																  .allies[objectTeam] <
															  1;
											}
										} else {
											validTarget =
												1;
										}

										if (validTarget !=
											    0 &&
										    (unsigned int)g_lastRoughDistance <
											    bestRangeScore) {
											bestObjectIndex =
												objectIndex;
											bestRangeScore =
												g_lastRoughDistance;
										}
									}
								}
							}
						}
						++objectIndex;
					}
				}
			}
		}
		++targetObjectIndex;
	}
	return (int16_t)bestObjectIndex;
}

/* Returns 1 when fewer craft than the limit are in setup attack, attack or
 * rocket attack on the target (those told to avoid it not counted), else 0. For
 * a target in the craft slots the limit is 100 when candidateCount is 1, else 6
 * for a starship or platform, 4 for a freighter and 2 for the rest; for any
 * other target, 2. For a player's target the mission difficulty sets it: 4 or 2
 * at difficulty 0, 8 or 3 at 1, 100 or 4 at 2, the first when candidateCount is
 * 1. */
// FUNCTION: XVT 0x45DBA0
int16_t paifight_TargetHasAttackCapacity(uint16_t targetObjIdx,
					 uint16_t candidateCount)
{
	uint16_t attackerCount;
	uint16_t objectIndex;
	ObjectRecord *object;
	CraftData *craft;
	AiController *controller;
	uint16_t attackTargetObjIdx;
	uint8_t maneuverMode;
	uint16_t candidateTotal;
	uint8_t genusId;
	uint16_t attackerLimit;
	int difficulty;

	attackerCount = 0;
	for (objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
	     objectIndex < g_activeRegionCraftObjectSlotEnd; ++objectIndex) {
		object = &g_objectTable[objectIndex];
		if (object->objectType != 0) {
			craft = object->mobj->pCraft;
			controller = &craft->aiController;
			attackTargetObjIdx = controller->targetObjIdx;
			if (targetObjIdx == attackTargetObjIdx &&
			    objectIndex != targetObjIdx &&
			    craft->playerCommandAvoidTargetObjIdx !=
				    attackTargetObjIdx) {
				maneuverMode = controller->maneuverMode;
				if (maneuverMode ==
					    AI_MANEUVER_MODE_SETUP_ATTACK ||
				    maneuverMode == AI_MANEUVER_MODE_ATTACK ||
				    maneuverMode ==
					    AI_MANEUVER_MODE_ROCKET_ATTACK) {
					++attackerCount;
				}
			}
		}
	}
	if (targetObjIdx < g_activeRegionCraftObjectSlotEnd) {
		candidateTotal = candidateCount;
		if (candidateCount != 1) {
			genusId = g_objectTable[targetObjIdx].genusId;
			if (genusId == 4 || genusId == 5) {
				attackerLimit = 6;
			} else if (genusId == 3) {
				attackerLimit = 4;
			} else {
				attackerLimit = 2;
			}
		} else {
			attackerLimit = 100;
		}
	} else {
		attackerLimit = 2;
		candidateTotal = candidateCount;
	}
	if (g_objectTable[targetObjIdx].playerOwnerIdx != -1) {
		difficulty = g_flightMissionState.difficulty;
		if (difficulty != 0) {
			if (difficulty == 1) {
				attackerLimit = candidateTotal == 1 ? 8 : 3;
			} else if (difficulty == 2) {
				attackerLimit = candidateTotal == 1 ? 100 : 4;
			}
		} else {
			attackerLimit = candidateTotal == 1 ? 4 : 2;
		}
	}

	return attackerCount < attackerLimit;
}

/* Besides answering, this sets g_paiContext's requireUndisabledTarget and targetSearchFlags for the
 * search, and for an escort-leader order (capescortersldr1pln) makes the nearest escort leader the AI's
 * target. */
/* Returns 1 when the search for the order slot's leader plan finds a target,
 * else 0: the nearest order target for capfreeldr1pln, disableldr1pln and
 * kamikaze1pln, the nearest escort leader for capescortersldr1pln, else the
 * nearest attacker of an order target. Sets g_lastRoughDistance. */
// FUNCTION: XVT 0x45DD00
int16_t paifight_SearchOrderSlotTarget(uint16_t orderSlot)
{
	PaiPlanRecord *plan;
	int16_t targetObject;

	plan = &g_planTable
		       [g_builtinPlanIdByNameIndex
				[g_orderLeaderBuiltinPlanNameIndex
					 [g_missionFlightGroups
						  [g_paiContext
							   .craftFlightGroupIndex]
							  .fg.orders[orderSlot]
							  .order]]];
	if (strcmp(plan->name, "disableldr1pln") == 0) {
		g_paiContext.requireUndisabledTarget = 1;
	} else {
		g_paiContext.requireUndisabledTarget = 0;
	}
	g_paiContext.targetSearchFlags = 7;
	if (strcmp(plan->name, "capfreeldr1pln") == 0 ||
	    strcmp(plan->name, "disableldr1pln") == 0 ||
	    strcmp(plan->name, "kamikaze1pln") == 0) {
		targetObject =
			paifight_FindAttackOrderTargetFromOrder(orderSlot);
	} else if (strcmp(plan->name, "capescortersldr1pln") == 0) {
		targetObject = paifight_TargetEscortLeaderFromOrder(orderSlot);
	} else {
		targetObject =
			paifight_FindAttackerOfOrderTargetFromOrder(orderSlot);
	}
	return targetObject != -1;
}

/* Besides answering, this sets g_paiContext's requireUndisabledTarget and targetSearchFlags for the
 * search, and for an escort-leader order (capescortersldr1pln) makes the nearest escort leader the AI's
 * target. */
/* Returns 1 when the order slot still has a target, else 0: for a
 * capescortersldr1pln leader plan, an escort leader found (made the target),
 * else at least one target counted by
 * paifight_CountRemainingOrderTargetsFromOrderSlot. Its targetSearchFlags of 2
 * leaves out the range and capacity tests. */
// FUNCTION: XVT 0x45DDF0
int16_t paifight_SearchOrderSlotRemainingTargets(uint16_t orderSlot)
{
	PaiPlanRecord *plan;
	int16_t result;

	plan = &g_planTable
		       [g_builtinPlanIdByNameIndex
				[g_orderLeaderBuiltinPlanNameIndex
					 [g_missionFlightGroups
						  [g_paiContext
							   .craftFlightGroupIndex]
							  .fg.orders[orderSlot]
							  .order]]];
	if (strcmp(plan->name, "disableldr1pln") == 0) {
		g_paiContext.requireUndisabledTarget = 1;
	} else {
		g_paiContext.requireUndisabledTarget = 0;
	}
	g_paiContext.targetSearchFlags = 2;
	if (strcmp(plan->name, "capescortersldr1pln") == 0) {
		result = paifight_TargetEscortLeaderFromOrder(orderSlot);
	} else {
		result = paifight_CountRemainingOrderTargetsFromOrderSlot(
			orderSlot);
	}
	return result != -1;
}

/* Returns what paifight_CountRemainingOrderTargets counts for the order slot's
 * first pair of target conditions, or when that is -1, for its second pair. */
// FUNCTION: XVT 0x45DEB0
int16_t paifight_CountRemainingOrderTargetsFromOrderSlot(uint16_t orderSlot)
{
	int orderIndex;
	int16_t result;

	orderIndex = orderSlot;
	result = paifight_CountRemainingOrderTargets(
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target1Type,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target1,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target1OrTarget2,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target2Type,
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[orderIndex]
			.target2);
	if (result == -1) {
		result = paifight_CountRemainingOrderTargets(
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.secondaryTargetTypes[0],
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.secondaryTargets[0],
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.target3OrTarget4,
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.secondaryTargetTypes[1],
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderIndex]
				.secondaryTargets[1]);
	}
	return result;
}

/* Returns how many targetable objects belong to flight groups that match the
 * target conditions (either one when targetRelationOp is 1, else both), or -1
 * when none do. Craft of the craft's own flight group do not count, nor, with
 * requireUndisabledTarget set, craft not in working order or captured by its
 * own team; static objects count when their type has behavior flag 2. */
// FUNCTION: XVT 0x45DFA0
int16_t paifight_CountRemainingOrderTargets(int16_t target1Type,
					    uint16_t target1,
					    int16_t targetRelationOp,
					    int16_t target2Type,
					    uint16_t target2)
{
	uint16_t objectIndex;
	uint16_t staticObjectIndex;
	int targetCount;
	int objectArrayIndex;
	uint16_t flightGroupIdx;
	int16_t matchesTarget1;
	int16_t matchesTarget2;
	int validTarget;
	ObjectRecord *object;
	MobileObject *mobileObject;
	CraftData *craft;

	objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
	targetCount = 0;
	while (objectIndex < (int)g_activeRegionCraftObjectSlotEnd) {
		objectArrayIndex = objectIndex;
		if (g_objectTable[objectArrayIndex].objectType != 0) {
			flightGroupIdx =
				g_objectTable[objectArrayIndex].flightGroupIdx;
			if (g_paiContext.craftFlightGroupIndex !=
			    flightGroupIdx) {
				matchesTarget1 =
					Mission_FlightGroupMatchesTriggerVariable(
						flightGroupIdx, target1Type,
						target1);
				matchesTarget2 =
					Mission_FlightGroupMatchesTriggerVariable(
						flightGroupIdx, target2Type,
						target2);
				if (targetRelationOp == 1) {
					matchesTarget1 |= matchesTarget2;
				} else {
					matchesTarget1 &= matchesTarget2;
				}

				if (matchesTarget1 != 0) {
					validTarget = pai_IsObjectTargetable(
						objectIndex);

					if (validTarget != 0) {
						mobileObject =
							g_objectTable
								[objectArrayIndex]
									.mobj;
						craft = mobileObject->pCraft;
						if (g_paiContext.requireUndisabledTarget ==
							    0 ||
						    (craft->workingSubsystems !=
							     0 &&
						     (g_paiContext.requireUndisabledTarget ==
							      0 ||
						      craft->capturedByFlightGroup ==
							      0 ||
						      g_objectTable[g_paiContext
									    .objectIndex]
								      .mobj
								      ->team !=
							      mobileObject
								      ->team))) {
							++targetCount;
						}
					}
				}
			}
		}
		++objectIndex;
	}

	staticObjectIndex = (uint16_t)g_regionMainObjectSlotEnd;
	while (staticObjectIndex < (int)(g_regionMainObjectSlotEnd +
					 g_regionStaticObjectSlotCount)) {
		object = &g_objectTable[staticObjectIndex];
		if (object->objectType != 0 &&
		    (g_objectTypeTable[object->objectType].behaviorFlags & 2) !=
			    0) {
			flightGroupIdx = object->flightGroupIdx;
			matchesTarget1 =
				Mission_FlightGroupMatchesTriggerVariable(
					flightGroupIdx, target1Type, target1);
			matchesTarget2 =
				Mission_FlightGroupMatchesTriggerVariable(
					flightGroupIdx, target2Type, target2);
			if (targetRelationOp == 1) {
				matchesTarget1 |= matchesTarget2;
			} else {
				matchesTarget1 &= matchesTarget2;
			}

			if (matchesTarget1 != 0) {
				validTarget = pai_IsObjectTargetable(
					staticObjectIndex);
				if (validTarget != 0) {
					++targetCount;
				}
			}
		}
		++staticObjectIndex;
	}

	if (targetCount != 0) {
		return (int16_t)targetCount;
	}
	return -1;
}

/* Order 23: returns 1 after giving the craft a target, with its signature and
 * hasLiveTarget 1, else 0; only while the craft is on its plan's maneuver. The
 * candidate target comes first, as in paifight_scanfortargetorder. Else it
 * takes the nearest other craft, within a rough 0x40000, whose own target is a
 * live object of flight group escortTargetFG and that passes
 * paifight_TargetHasAttackCapacity. Sets g_lastRoughDistance. */
// FUNCTION: XVT 0x45E440
int16_t paifight_escorttargetorder(void)
{
	uint16_t sourceObjIdx;
	uint16_t candidateTargetIdx;
	unsigned int objectIndex;
	ObjectRecord *object;
	int validTarget;
	uint16_t bestTargetObjIdx;
	uint16_t scanObjIdx;
	unsigned int bestRangeScore;
	uint16_t escortFlightGroupIdx;
	int targetsEscortFlightGroup;
	uint16_t targetObjIdx;

	sourceObjIdx = g_paiContext.objectIndex;
	if (g_paiContext.controller->maneuverMode ==
	    g_paiContext.initialManeuverId) {
		candidateTargetIdx =
			g_paiContext.controller->candidateTargetIdx;
		if (candidateTargetIdx != UINT16_MAX &&
		    candidateTargetIdx != AI_TARGET_ABORT) {
			objectIndex = candidateTargetIdx;
			validTarget = pai_IsObjectTargetable(objectIndex);

			if (validTarget != 0) {
				g_paiContext.controller->targetObjIdx =
					candidateTargetIdx;
				g_paiContext.controller->targetSignature =
					g_objectTable[candidateTargetIdx]
						.objectSignature;
				g_paiContext.controller->hasLiveTarget = 1;
				return 1;
			}
			g_paiContext.controller->candidateTargetIdx =
				UINT16_MAX;
		}

		bestTargetObjIdx = UINT16_MAX;
		scanObjIdx = (uint16_t)g_activeRegionObjectSlotStart;
		bestRangeScore = UINT32_MAX;
		escortFlightGroupIdx = g_paiContext.controller->escortTargetFG;
		for (; scanObjIdx < g_activeRegionCraftObjectSlotEnd;
		     ++scanObjIdx) {
			object = &g_objectTable[scanObjIdx];
			if (object->objectType != 0 &&
			    g_paiContext.objectIndex != scanObjIdx) {
				targetsEscortFlightGroup = 0;
				targetObjIdx =
					object->mobj->pCraft->aiController
						.targetObjIdx;
				if (targetObjIdx < 0x8000 &&
				    targetObjIdx != UINT16_MAX) {
					ObjectRecord *target =
						&g_objectTable[targetObjIdx];
					if (target->objectType != 0) {
						targetsEscortFlightGroup =
							target->flightGroupIdx ==
							escortFlightGroupIdx;
					}
				}
				if (targetsEscortFlightGroup != 0 &&
				    paifight_TargetHasAttackCapacity(scanObjIdx,
								     0xFF)) {
					pai_ObjectRefUpdateRoughDistance(
						sourceObjIdx, scanObjIdx);
					if (bestRangeScore >
					    (unsigned int)g_lastRoughDistance) {
						if (g_lastRoughDistance <
						    0x40000) {
							bestTargetObjIdx =
								scanObjIdx;
							bestRangeScore =
								g_lastRoughDistance;
						}
					}
				}
			}
		}

		if (bestTargetObjIdx != UINT16_MAX) {
			g_paiContext.controller->targetObjIdx =
				bestTargetObjIdx;
			g_paiContext.controller->targetSignature =
				g_objectTable[bestTargetObjIdx].objectSignature;
			g_paiContext.controller->hasLiveTarget = 1;
			return 1;
		}
	}
	return 0;
}

/* Order 5: aims and fires the craft's cannons and warheads at its target;
 * returns 0 on every path, at once when no subsystem works. With a target that
 * cannot be targeted it sets every cannon group's link mode to 0. Else the
 * cannons fire when the target lies within 0x800 of the craft's yaw and pitch
 * and inside g_aiFighterShootMaxRangeBySkill; for a target in the region's main
 * slots that is 0x4000 less when it moves faster than 25 with its yaw within
 * 0x2000 of the craft's, 0x2000 less within 0x5000, and 0x6000 more for a
 * platform or starship. Despite their names, the FAST_TARGET_HEAD_ON constants
 * cover the first case, the two flying roughly the same way. The link mode is 3
 * within 0x2000, 2 within 0x4000, else 1, with a burst from
 * g_aiFighterShootBurstLengthBySkill; ion cannons fire only on disableldr1pln
 * with a live target, and other cannons not then. For a target in the craft
 * slots without an active decoy beam, in rocket attack, it fires warheads of
 * the class the target calls for when the target's front shield and remaining
 * hull outlast the homing warheads already aimed at it, fewer of them are
 * coming than a limit, weapons are not inhibited and it has fired fewer than
 * its per-maneuver limit. On disableldr1pln the hull counts only as a tenth of
 * hullMax, once its damage passes that, and the craft's own warheads count as
 * already coming. It builds warheadLockTicks by thinkInterval (three quarters
 * of it below tier 2) while within 0x300 and fire range, and lets it fall
 * otherwise; at 472 times the tier plus 1 (half that at tier 2 with a damaged
 * hull) it picks targetComponent and fires each ready launcher, counting
 * warheadsFiredThisManeuver. */
// FUNCTION: XVT 0x45E780
int16_t paifight_fightershootorder(void)
{
	enum {
		MAX_SHOOTING_SKILL_TIER = 2,
		FAST_TARGET_SPEED = 25,
		ANGLE_HALF_TURN = 0x8000,
		MAX_TARGET_ANGLE = 0x800,
		MAX_WARHEAD_ANGLE = 0x300,
		FAST_TARGET_HEAD_ON_ANGLE = 0x2000,
		FAST_TARGET_OBLIQUE_ANGLE = 0x5000,
		FAST_TARGET_HEAD_ON_RANGE_REDUCTION = 0x4000,
		FAST_TARGET_OBLIQUE_RANGE_REDUCTION = 0x2000,
		LARGE_TARGET_RANGE_BONUS = 0x6000,
		LASER_LINK_CLOSE_RANGE = 0x2000,
		LASER_LINK_MEDIUM_RANGE = 0x4000,
		LASER_LINK_ALL_CANNONS = 3,
		LARGE_TARGET_INCOMING_LIMIT = 16,
		SMALL_TARGET_INCOMING_LIMIT = 2,
		LARGE_TARGET_MANEUVER_LIMIT = 6,
		SMALL_TARGET_MANEUVER_LIMIT = 1,
		LARGE_TARGET_WARHEAD_CLASS = 2,
		SMALL_TARGET_WARHEAD_CLASS = 1,
		SPECIAL_WARHEAD_EXTRA_COUNT = 7,
		SPECIAL_WARHEAD_INHIBIT_LIMIT = 0x49C,
		MISSION_VERSION_SPECIAL_WARHEADS = 14,
		DISABLE_HULL_THRESHOLD_DIVISOR = 10,
		LARGE_TARGET_DAMAGE_SHIFT = 4,
		FREIGHTER_DAMAGE_SHIFT = 2,
		WARHEAD_LOCK_TICKS_PER_SKILL_LEVEL = 472,
		LOWER_SKILL_LOCK_RATE = 0xC000,
		WEAPON_INHIBIT_BEAM_EFFECT_SLOT = 2,
		PAIRED_FIRE_LAUNCHER_FLAGS = 3,
		NORMAL_LAUNCHER_FLAGS = 1,
		IMBALANCED_LAUNCHER_FLAGS = 0x81
	};

	uint16_t incomingLimit;
	uint16_t warheadsPerManeuverLimit;
	uint16_t currentPlanId;
	uint16_t targetAngle;
	uint16_t pitchAngle;
	uint16_t requiredWarheadClass;
	uint16_t targetIndex;
	unsigned int targetDurability;
	unsigned int incomingDamage;
	uint16_t cannonClassCount;
	unsigned int fireRange;
	uint16_t burstRemaining;
	unsigned int launcherCount;
	uint16_t linkMode;
	uint16_t slotIndex;
	uint16_t launcherIndex;
	uint16_t projectileObjectIndex;
	unsigned int maxRange;
	int isValidTarget;

	if (g_curCraft->workingSubsystems == 0) {
		return 0;
	}

	targetIndex = g_paiContext.controller->targetObjIdx;
	burstRemaining = 0;
	isValidTarget = pai_IsObjectTargetable(targetIndex);

	if (isValidTarget != 0) {
		maxRange =
			g_aiFighterShootMaxRangeBySkill[g_paiContext.skillTier];
		if ((int)g_regionMainObjectSlotEnd > targetIndex) {
			ObjectRecord *targetObject;
			MobileObject *targetMobile;

			targetObject = &g_objectTable[targetIndex];
			targetMobile = targetObject->mobj;
			if (targetMobile != NULL &&
			    targetMobile->speed > FAST_TARGET_SPEED) {
				targetAngle =
					(uint16_t)(g_objectTable
							   [g_paiContext
								    .objectIndex]
								   .yaw -
						   targetObject->yaw);
				if (targetAngle >= ANGLE_HALF_TURN) {
					targetAngle = (uint16_t)-targetAngle;
				}
				if (targetAngle < FAST_TARGET_HEAD_ON_ANGLE) {
					maxRange -=
						FAST_TARGET_HEAD_ON_RANGE_REDUCTION;
				} else if (targetAngle <
					   FAST_TARGET_OBLIQUE_ANGLE) {
					maxRange -=
						FAST_TARGET_OBLIQUE_RANGE_REDUCTION;
				}
			}
			if (targetObject->genusId == CRAFT_GENUS_PLATFORM ||
			    targetObject->genusId == CRAFT_GENUS_STARSHIP) {
				maxRange += LARGE_TARGET_RANGE_BONUS;
			}
		}

		pai_ObjectRefDirectionToObjectRef(g_paiContext.objectIndex,
						  targetIndex);
		targetAngle =
			(uint16_t)(trig2_xyangle -
				   g_objectTable[g_paiContext.objectIndex].yaw);
		if (targetAngle >= ANGLE_HALF_TURN) {
			targetAngle = (uint16_t)-targetAngle;
		}
		pitchAngle = (uint16_t)(trig2_pitch - g_curCraft->pitch);
		if (pitchAngle >= ANGLE_HALF_TURN) {
			pitchAngle = (uint16_t)-pitchAngle;
		}
		if (targetAngle >= MAX_TARGET_ANGLE ||
		    pitchAngle >= MAX_TARGET_ANGLE ||
		    (unsigned int)trig2_polardistance >= maxRange) {
			linkMode = 0;
		} else {
			linkMode = LASER_LINK_ALL_CANNONS;
			if (trig2_polardistance >= LASER_LINK_CLOSE_RANGE) {
				linkMode = (uint16_t)(trig2_polardistance <
						      LASER_LINK_MEDIUM_RANGE);
				++linkMode;
			}
			burstRemaining = g_aiFighterShootBurstLengthBySkill
				[g_paiContext.skillTier];
		}

		cannonClassCount = g_curCraft->cannonGroupCount;
		currentPlanId = g_paiContext.controller->currentPlanId;
		for (slotIndex = 0; slotIndex < cannonClassCount; ++slotIndex) {
			uint16_t slotLink;

			if (linkMode != 0) {
				if (g_curCraft->laserState
					    .projectileTypeId[slotIndex] !=
				    PROJECTILE_OBJECT_TYPE_ION_LASER) {
					if (g_paiContext.controller
						    ->hasLiveTarget != 0) {
						slotLink =
							strcmp(g_planTable[currentPlanId]
								       .name,
							       "disableldr1pln") !=
									0
								? linkMode
								: 0;
					} else {
						slotLink = linkMode;
					}
				} else if (strcmp(g_planTable[currentPlanId]
							  .name,
						  "disableldr1pln") == 0 &&
					   g_paiContext.controller
							   ->hasLiveTarget ==
						   1) {
					slotLink = linkMode;
				} else {
					slotLink = 0;
				}
			} else {
				slotLink = 0;
			}
			g_curCraft->laserState.linkMode[slotIndex] = slotLink;
			g_curCraft->laserState.burstRemaining[slotIndex] =
				burstRemaining;
		}

		if ((int)g_activeRegionCraftObjectSlotEnd <= targetIndex ||
		    Object_HasActiveDecoyBeam(targetIndex)) {
			return 0;
		}

		{
			CraftData *targetCraft;

			if (g_objectTable[targetIndex].genusId ==
				    CRAFT_GENUS_STARSHIP ||
			    g_objectTable[targetIndex].genusId ==
				    CRAFT_GENUS_PLATFORM ||
			    g_objectTable[targetIndex].genusId ==
				    CRAFT_GENUS_FREIGHTER ||
			    (g_objectTable[targetIndex].genusId ==
				     CRAFT_GENUS_TRANSPORT &&
			     g_missionFileVersion ==
				     MISSION_VERSION_SPECIAL_WARHEADS)) {
				requiredWarheadClass =
					LARGE_TARGET_WARHEAD_CLASS;
				warheadsPerManeuverLimit =
					LARGE_TARGET_MANEUVER_LIMIT;
				incomingLimit = LARGE_TARGET_INCOMING_LIMIT;
				/* Typed literals preserve the original unsigned range selection. */
				fireRange =
					g_paiContext.skillTier ==
							MAX_SHOOTING_SKILL_TIER
						? 244332u
						: 203610u;
			} else {
				fireRange = 101805u;
				incomingLimit = SMALL_TARGET_INCOMING_LIMIT;
				requiredWarheadClass =
					SMALL_TARGET_WARHEAD_CLASS;
				warheadsPerManeuverLimit =
					SMALL_TARGET_MANEUVER_LIMIT;
			}

			targetCraft = g_objectTable[targetIndex].mobj->pCraft;
			targetDurability =
				(unsigned int)targetCraft->shieldEnergy[0];
			incomingDamage = 0;
			if (strcmp(g_planTable[currentPlanId].name,
				   "disableldr1pln") == 0) {
				unsigned int hullThreshold;

				hullThreshold = targetCraft->hullMax /
						DISABLE_HULL_THRESHOLD_DIVISOR;
				if (targetCraft->hullDamage > hullThreshold) {
					targetDurability += hullThreshold;
				}
				launcherCount =
					g_curCraft->warheadLauncherCount;
				for (launcherIndex = 0;
				     launcherIndex < launcherCount;
				     ++launcherIndex) {
					uint8_t projectileType;
					unsigned int missileDamage;

					projectileType =
						g_curCraft->warheadSlotTypeIds
							[launcherIndex];
					if (g_projectileTypeData.warheadClass
							    [projectileType -
							     PROJECTILE_OBJECT_TYPE_FIRST] ==
						    requiredWarheadClass ||
					    (g_missionFileVersion ==
						     MISSION_VERSION_SPECIAL_WARHEADS &&
					     (projectileType ==
						      WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE ||
					      projectileType ==
						      WARHEAD_OBJECT_TYPE_ION_PULSE) &&
					     requiredWarheadClass ==
						     LARGE_TARGET_WARHEAD_CLASS)) {
						missileDamage =
							g_projectileTypeData.damage
								[projectileType -
								 PROJECTILE_OBJECT_TYPE_FIRST];
						if (g_objectTable[targetIndex]
								    .genusId ==
							    CRAFT_GENUS_STARSHIP ||
						    g_objectTable[targetIndex]
								    .genusId ==
							    CRAFT_GENUS_PLATFORM) {
							missileDamage >>=
								LARGE_TARGET_DAMAGE_SHIFT;
						}
						if (g_objectTable[targetIndex]
							    .genusId ==
						    CRAFT_GENUS_FREIGHTER) {
							missileDamage >>=
								FREIGHTER_DAMAGE_SHIFT;
						}
						incomingDamage += missileDamage;
					}
				}
			} else {
				targetDurability = targetCraft->hullMax +
						   targetDurability -
						   targetCraft->hullDamage;
			}

			for (projectileObjectIndex =
				     g_projectileObjectSlotStart;
			     projectileObjectIndex <
			     (int)g_projectileObjectSlotEnd;
			     ++projectileObjectIndex) {
				ObjectRecord *projectileObject;

				projectileObject =
					&g_objectTable[projectileObjectIndex];
				if (projectileObject->objectType >=
				    PROJECTILE_OBJECT_TYPE_FIRST) {
					WarheadGuidanceState *guidance;

					guidance = projectileObject->mobj
							   ->pWarheadGuidance;
					if (guidance->homingTier != 0 &&
					    guidance->targetObjIdx ==
						    targetIndex) {
						unsigned int missileDamage;

						missileDamage =
							g_projectileTypeData.damage
								[projectileObject
									 ->objectType -
								 PROJECTILE_OBJECT_TYPE_FIRST];
						if (g_objectTable[targetIndex]
								    .genusId ==
							    CRAFT_GENUS_STARSHIP ||
						    g_objectTable[targetIndex]
								    .genusId ==
							    CRAFT_GENUS_PLATFORM) {
							missileDamage >>=
								LARGE_TARGET_DAMAGE_SHIFT;
						}
						if (g_objectTable[targetIndex]
							    .genusId ==
						    CRAFT_GENUS_FREIGHTER) {
							missileDamage >>=
								FREIGHTER_DAMAGE_SHIFT;
						}
						incomingDamage += missileDamage;
					}
				}
			}
			if (targetDurability > incomingDamage) {
				uint16_t incomingCount;

				incomingCount = 0;
				for (projectileObjectIndex =
					     g_projectileObjectSlotStart;
				     projectileObjectIndex <
				     (int)g_projectileObjectSlotEnd;
				     ++projectileObjectIndex) {
					ObjectRecord *projectileObject;
					uint8_t projectileType;

					projectileObject =
						&g_objectTable
							[projectileObjectIndex];
					projectileType =
						projectileObject->objectType;
#ifdef XVT_MODERN
					/* Impact effects remain in projectile slots after a hit. */
					if (projectileType <
						    PROJECTILE_OBJECT_TYPE_FIRST ||
					    projectileType >=
						    PROJECTILE_OBJECT_TYPE_FIRST +
							    PROJECTILE_OBJECT_TYPE_COUNT) {
						continue;
					}
#endif
					if (projectileType != 0 &&
					    (g_projectileTypeData.warheadClass
							     [projectileType -
							      PROJECTILE_OBJECT_TYPE_FIRST] ==
						     requiredWarheadClass ||
					     (g_missionFileVersion ==
						      MISSION_VERSION_SPECIAL_WARHEADS &&
					      (projectileType ==
						       WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE ||
					       projectileType ==
						       WARHEAD_OBJECT_TYPE_ION_PULSE) &&
					      requiredWarheadClass ==
						      LARGE_TARGET_WARHEAD_CLASS))) {
						WarheadGuidanceState *guidance;

						guidance =
							projectileObject->mobj
								->pWarheadGuidance;
						if (guidance->homingTier != 0 &&
						    guidance->targetObjIdx ==
							    targetIndex) {
							++incomingCount;
							if (g_missionFileVersion ==
								    MISSION_VERSION_SPECIAL_WARHEADS &&
							    (projectileType ==
								     WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE ||
							     projectileType ==
								     WARHEAD_OBJECT_TYPE_ION_PULSE) &&
							    requiredWarheadClass ==
								    LARGE_TARGET_WARHEAD_CLASS) {
								incomingCount +=
									SPECIAL_WARHEAD_EXTRA_COUNT;
							}
						}
					}
				}
				if (incomingCount < incomingLimit &&
				    g_paiContext.controller->maneuverMode ==
					    AI_MANEUVER_MODE_ROCKET_ATTACK &&
				    g_curCraft->weaponFireInhibitTimer == 0 &&
				    g_curCraft->beamEffectAccum
						    [WEAPON_INHIBIT_BEAM_EFFECT_SLOT] ==
					    0 &&
				    g_curCraft->aiFlight
						    .warheadsFiredThisManeuver <
					    warheadsPerManeuverLimit) {
					if (targetAngle >= MAX_WARHEAD_ANGLE ||
					    pitchAngle >= MAX_WARHEAD_ANGLE ||
					    (unsigned int)trig2_polardistance >=
						    fireRange) {
						g_curCraft->warheadLockTicks -=
							g_paiContext.controller
								->thinkInterval;
						if (g_curCraft
							    ->warheadLockTicks <
						    0) {
							g_curCraft
								->warheadLockTicks =
								0;
						}
					} else {
						uint16_t lockThreshold;

						lockThreshold =
							(uint16_t)(WARHEAD_LOCK_TICKS_PER_SKILL_LEVEL *
								   (g_paiContext
									    .skillTier +
								    1));
						if (g_paiContext.skillTier ==
						    MAX_SHOOTING_SKILL_TIER) {
							g_curCraft
								->warheadLockTicks +=
								g_paiContext
									.controller
									->thinkInterval;
							if (g_curCraft
								    ->hullDamage >=
							    g_curCraft
								    ->systemDamageHullThreshold) {
								lockThreshold >>=
									1;
							}
						} else {
							g_curCraft
								->warheadLockTicks += MATH2_fraction(
								g_paiContext
									.controller
									->thinkInterval,
								LOWER_SKILL_LOCK_RATE);
						}
						if (g_curCraft
							    ->warheadLockTicks >=
						    (int)lockThreshold) {
							for (launcherIndex = 0;
							     launcherIndex <
							     g_curCraft
								     ->warheadLauncherCount;
							     ++launcherIndex) {
								uint8_t projectileType;

								if (g_curCraft->warheadLauncherCooldownTicks
									    [launcherIndex] ==
								    0) {
									projectileType =
										g_curCraft
											->warheadSlotTypeIds
												[launcherIndex];
									if (g_projectileTypeData
											    .warheadClass
												    [projectileType -
												     PROJECTILE_OBJECT_TYPE_FIRST] ==
										    requiredWarheadClass ||
									    (g_missionFileVersion ==
										     MISSION_VERSION_SPECIAL_WARHEADS &&
									     (projectileType ==
										      WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE ||
									      projectileType ==
										      WARHEAD_OBJECT_TYPE_ION_PULSE) &&
									     requiredWarheadClass ==
										     LARGE_TARGET_WARHEAD_CLASS &&
									     (targetIndex ==
										      UINT16_MAX ||
									      g_objectTable[targetIndex]
											      .mobj
											      ->pCraft
											      ->weaponFireInhibitTimer ==
										      0 ||
									      (uint16_t)g_objectTable[targetIndex]
											      .mobj
											      ->pCraft
											      ->weaponFireInhibitTimer <
										      SPECIAL_WARHEAD_INHIBIT_LIMIT))) {
										if (g_paiContext.skillTier ==
											    MAX_SHOOTING_SKILL_TIER &&
										    g_curCraft->hullDamage >=
											    g_curCraft
												    ->systemDamageHullThreshold) {
											g_curCraft
												->warheadLauncherFlags
													[launcherIndex] =
												PAIRED_FIRE_LAUNCHER_FLAGS;
										} else {
											int weaponSlotIndex;

											weaponSlotIndex =
												g_modelDefs[g_curCraft
														    ->modelIndex]
													.warheadLauncherFirstSlot
														[launcherIndex];
											g_curCraft
												->warheadLauncherFlags
													[launcherIndex] =
												NORMAL_LAUNCHER_FLAGS;
											if (g_curCraft
												    ->weaponSlots
													    [weaponSlotIndex]
												    .ammoCount <
											    g_curCraft
												    ->weaponSlots
													    [weaponSlotIndex +
													     1]
												    .ammoCount) {
												g_curCraft
													->warheadLauncherFlags
														[launcherIndex] =
													(int8_t)IMBALANCED_LAUNCHER_FLAGS;
											}
										}
										g_paiContext
											.controller
											->targetComponent =
											paifight_SelectTargetComponentMesh(
												targetIndex);
										laser_firewarheadsystem(
											g_paiContext
												.objectIndex,
											launcherIndex);
										++g_curCraft
											  ->aiFlight
											  .warheadsFiredThisManeuver;
										if (g_objectTable[targetIndex]
												    .genusId ==
											    CRAFT_GENUS_STARFIGHTER ||
										    g_objectTable[targetIndex]
												    .genusId ==
											    CRAFT_GENUS_TRANSPORT) {
											g_curCraft
												->warheadLockTicks =
												0;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	} else {
		for (slotIndex = 0; slotIndex < g_curCraft->cannonGroupCount;
		     ++slotIndex) {
			g_curCraft->laserState.linkMode[slotIndex] = 0;
		}
	}
	return 0;
}

/* Returns the mesh of the target to aim warheads at: one of its main hull or
 * fuselage meshes, the one nearest the craft for object type 54, else one
 * picked with GameRandRange. Returns 0 for a target outside the craft slots. */
// FUNCTION: XVT 0x45F1E0
uint16_t paifight_SelectTargetComponentMesh(uint16_t targetObjIdx)
{
	uint16_t selectedMeshIdx = 0;
	int meshCount;
	unsigned int nearestDistance = 0x1000000;
	uint16_t candidateCount = 0;
	int selectNearest;
	int objectType;
	int cachedMeshCount;
	uint8_t candidateMeshes[52];

	selectNearest = g_objectTable[targetObjIdx].objectType == 54;
	candidateCount = 0;
	candidateMeshes[0] = 0;
	if (g_activeRegionCraftObjectSlotEnd > targetObjIdx) {
		uint16_t meshIndex;

		objectType = g_objectTable[targetObjIdx].objectType;
		if (objectType < 73) {
			meshCount = g_objectTypeMeshCache[objectType].meshCount;
		} else {
			meshCount =
				ModelMesh_GetObjectTypeMeshCount(objectType);
		}

		for (meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
			int adjustedMeshIndex = meshIndex;
			MeshComponentType meshType;

			objectType = g_objectTable[targetObjIdx].objectType;
			if (objectType < 73) {
				if (adjustedMeshIndex < 0) {
					meshType = MESH_COMPONENT_00_DEFAULT;
				} else {
					cachedMeshCount =
						g_objectTypeMeshCache
							[objectType]
								.meshCount;
					if (cachedMeshCount <= meshIndex) {
						adjustedMeshIndex =
							cachedMeshCount - 1;
					}
					meshType =
						g_objectTypeMeshCache[objectType]
							.meshTypes
								[adjustedMeshIndex];
				}
			} else {
				meshType = ModelMesh_GetObjectTypeMeshType(
					objectType, meshIndex);
			}

			if ((uint16_t)meshType == MESH_COMPONENT_01_MAIN_HULL ||
			    (uint16_t)meshType == MESH_COMPONENT_03_FUSELAGE) {
				if (selectNearest) {
					unsigned int distance =
						Object_DirectionAndDistanceToMeshCenter(
							g_paiContext
								.objectIndex,
							targetObjIdx,
							meshIndex);

					if (distance < nearestDistance) {
						selectedMeshIdx = meshIndex;
						nearestDistance = distance;
					}
				} else {
					candidateMeshes[candidateCount++] =
						(uint8_t)meshIndex;
				}
			}
		}

		if (selectNearest) {
			return selectedMeshIdx;
		}
		return candidateMeshes[GameRandRange(candidateCount)];
	}
	return 0;
}

/* Order 8: fires defense warheads from launchers of the craft; returns 0 on
 * every path. Not for a starfighter, a craft breaking up, with no subsystem
 * working or with weapons inhibited. Each launcher slot with an intact mesh,
 * holding warhead type 0x90 or 0x95 and a round, first waits out its
 * missileDefenseCooldown, one per think. Then it picks a target, measured from
 * the slot's hardpoint, which it puts in g_paifightSearchOriginX, Y and Z
 * (hardpoint doubled for object type 53): the nearest homing warhead aimed at
 * the craft more than 0x4000 and less than 0x40000 away that no homing warhead
 * of tier 5 or more chases, else the nearest targetable craft within 0x40000
 * with fewer than two homing warheads on it that attacks the craft or is an
 * enemy player's craft targeting it. It fires a homing warhead at it with a
 * random homing tier of 3 to 6 and a cooldown of 20 thinks. The attack test
 * compares a craft's own lastAttackerObjIdx and threatObjIdx with its own
 * index. Leaves the slot's turret target and targetComponent set. Sets
 * g_lastRoughDistance. */
// FUNCTION: XVT 0x45F380
int16_t paifight_missiledefenseorder(void)
{
	uint16_t launcherIndex;
	uint16_t weaponSlotIndex;
	int16_t selectedTarget;
	unsigned int bestRange;
	uint16_t *turretTargetIndex;
	int candidateValid;

	if (g_curCraft->objectKind == CRAFT_OBJECT_KIND_BREAKING_UP) {
		return 0;
	}
	if (g_curCraft->workingSubsystems == 0) {
		return 0;
	}
	if (g_curCraft->weaponFireInhibitTimer != 0) {
		return 0;
	}
	if (g_objectTable[g_paiContext.objectIndex].genusId == 0) {
		return 0;
	}

	launcherIndex = 0;
	while (launcherIndex < g_curCraft->warheadLauncherCount) {
		weaponSlotIndex =
			g_modelDefs[g_curCraft->modelIndex]
				.warheadLauncherFirstSlot[launcherIndex];
		while (g_modelDefs[g_curCraft->modelIndex]
				       .warheadLauncherLastSlot[launcherIndex] +
			       1 >
		       weaponSlotIndex) {
			uint8_t meshIndex =
				g_modelDefs[g_curCraft->modelIndex]
					.weaponHardpoints[weaponSlotIndex]
					.meshIdx;

			if (g_curCraft->componentState[meshIndex] == 0) {
				if ((g_curCraft->weaponSlots[weaponSlotIndex]
						     .projectileTypeId ==
					     0x90 ||
				     g_curCraft->weaponSlots[weaponSlotIndex]
						     .projectileTypeId ==
					     0x95) &&
				    g_curCraft->weaponSlots[weaponSlotIndex]
						    .ammoCount != 0) {
					if (g_curCraft
						    ->weaponSlots
							    [weaponSlotIndex]
						    .missileDefenseCooldown !=
					    0) {
						--g_curCraft
							  ->weaponSlots
								  [weaponSlotIndex]
							  .missileDefenseCooldown;
					} else {
						uint16_t candidateIndex;

						turretTargetIndex =
							&g_curCraft
								 ->turretTargetStates
									 [weaponSlotIndex]
								 .targetObjIdx;
						*turretTargetIndex = UINT16_MAX;
						g_paifightSearchOriginX =
							g_paiContext
								.craftPositionX;
						g_paifightSearchOriginY =
							g_paiContext
								.craftPositionY;
						g_paifightSearchOriginZ =
							g_paiContext
								.craftPositionZ;
						pai_calcrotatedpoint(
							&g_objectTable
								[g_paiContext
									 .objectIndex],
							g_modelDefs[g_curCraft
									    ->modelIndex]
								.weaponHardpoints
									[weaponSlotIndex]
								.x,
							g_modelDefs[g_curCraft
									    ->modelIndex]
								.weaponHardpoints
									[weaponSlotIndex]
								.z,
							g_modelDefs[g_curCraft
									    ->modelIndex]
								.weaponHardpoints
									[weaponSlotIndex]
								.y);
						if (g_objectTable
							    [g_paiContext
								     .objectIndex]
								    .objectType ==
						    53) {
							g_rotatedX *= 2;
							g_rotatedY *= 2;
							g_rotatedZ *= 2;
						}
						g_paifightSearchOriginX +=
							g_rotatedX;
						g_paifightSearchOriginY +=
							g_rotatedY;
						g_paifightSearchOriginZ +=
							g_rotatedZ;

						selectedTarget = -1;
						bestRange = 0x40000;
						for (candidateIndex = (uint16_t)
							     g_projectileObjectSlotStart;
						     candidateIndex <
						     g_projectileObjectSlotEnd;
						     ++candidateIndex) {
							ObjectRecord *projectile =
								&g_objectTable
									[candidateIndex];
							WarheadGuidanceState
								*guidance;
							uint16_t otherIndex;
							int16_t incomingCount;
							int range;

							if (projectile
								    ->objectType ==
							    0) {
								continue;
							}
							guidance = &g_projectileGuidanceStates[(
								uint16_t)(candidateIndex -
									  g_projectileObjectSlotStart)];
							if (guidance->homingTier ==
								    0 ||
							    guidance->targetObjIdx !=
								    g_paiContext
									    .objectIndex) {
								continue;
							}

							incomingCount = 0;
							for (otherIndex = (uint16_t)
								     g_projectileObjectSlotStart;
							     otherIndex <
							     g_projectileObjectSlotEnd;
							     ++otherIndex) {
								WarheadGuidanceState
									*otherGuidance;
								if (g_objectTable[otherIndex]
										    .objectType ==
									    0 ||
								    otherIndex ==
									    candidateIndex) {
									continue;
								}
								otherGuidance = &g_projectileGuidanceStates[(
									uint16_t)(otherIndex -
										  g_projectileObjectSlotStart)];
								if (otherGuidance->homingTier >=
									    5 &&
								    otherGuidance->targetObjIdx ==
									    candidateIndex) {
									++incomingCount;
								}
							}
							if ((uint16_t)
								    incomingCount >=
							    1) {
								continue;
							}

							range = collide_roughdistance3d(
								projectile->world_x -
									g_paifightSearchOriginX,
								projectile->world_y -
									g_paifightSearchOriginY,
								projectile->world_z -
									g_paifightSearchOriginZ);
							g_lastRoughDistance =
								range;
							if (range > 0x4000) {
								if ((unsigned int)
									    range <
								    bestRange) {
									selectedTarget =
										candidateIndex;
									bestRange =
										range;
								}
							}
						}

						if (selectedTarget != -1) {
							*turretTargetIndex =
								selectedTarget;
						} else {
							bestRange = 0x40000;
							selectedTarget = -1;
							for (candidateIndex = (uint16_t)
								     g_activeRegionObjectSlotStart;
							     candidateIndex <
							     g_activeRegionCraftObjectSlotEnd;
							     ++candidateIndex) {
								uint16_t
									incomingCount;
								uint16_t
									otherIndex;
								unsigned int
									range;
								{
									ObjectRecord *candidate =
										&g_objectTable
											[candidateIndex];
									CraftData
										*candidateCraft;
									if (candidate
										    ->objectType ==
									    0) {
										continue;
									}
									candidateCraft =
										candidate
											->mobj
											->pCraft;
									if (!((g_paiContext.objectIndex ==
										       candidateCraft
											       ->aiController
											       .targetObjIdx &&
									       (candidateCraft->aiController
												.maneuverMode ==
											AI_MANEUVER_MODE_ATTACK ||
										candidateCraft->aiController
												.maneuverMode ==
											AI_MANEUVER_MODE_ROCKET_ATTACK)) ||
									      candidateCraft->lastAttackerObjIdx ==
										      (uint16_t)
											      candidateIndex ||
									      candidateCraft->aiFlight
											      .threatObjIdx ==
										      candidateIndex)) {
										int playerOwner =
											candidate
												->playerOwnerIdx;
										int ownTeam;
										int candidateTeam;
										int isEnemy;
										if (playerOwner ==
										    -1) {
											continue;
										}
										candidateTeam =
											g_missionFlightGroups
												[candidate
													 ->flightGroupIdx]
													.fg
													.team;
										ownTeam =
											g_objectTable[g_paiContext
													      .objectIndex]
												.mobj
												->team;
										if (ownTeam ==
										    candidateTeam) {
											isEnemy =
												0;
										} else {
											isEnemy =
												g_missionTeams[ownTeam]
													.allies[candidateTeam] ==
												0;
										}
										if (isEnemy !=
										    1) {
											continue;
										}
										if (g_players[playerOwner]
											    .currentTargetObjectIdx !=
										    g_paiContext
											    .objectIndex) {
											continue;
										}
									}
								}

								candidateValid = pai_IsObjectTargetable(
									candidateIndex);
								if (candidateValid ==
								    0) {
									continue;
								}

								incomingCount =
									0;
								for (otherIndex = (uint16_t)
									     g_projectileObjectSlotStart;
								     otherIndex <
								     g_projectileObjectSlotEnd;
								     ++otherIndex) {
									WarheadGuidanceState
										*otherGuidance;
									if (g_objectTable[otherIndex]
											    .objectType ==
										    0 ||
									    otherIndex ==
										    candidateIndex) {
										continue;
									}
									otherGuidance = &g_projectileGuidanceStates[(
										uint16_t)(otherIndex -
											  g_projectileObjectSlotStart)];
									if (otherGuidance->homingTier !=
										    0 &&
									    otherGuidance->targetObjIdx ==
										    candidateIndex) {
										++incomingCount;
									}
								}
								if (incomingCount >=
								    2) {
									continue;
								}

								range = collide_roughdistance3d(
									g_objectTable[candidateIndex]
											.world_x -
										g_paifightSearchOriginX,
									g_objectTable[candidateIndex]
											.world_y -
										g_paifightSearchOriginY,
									g_objectTable[candidateIndex]
											.world_z -
										g_paifightSearchOriginZ);
								g_lastRoughDistance =
									range;
								if (range <
								    bestRange) {
									selectedTarget =
										candidateIndex;
									bestRange =
										range;
								}
							}
						}

						if (selectedTarget != -1) {
							*turretTargetIndex =
								selectedTarget;
						}
						if (*turretTargetIndex !=
						    UINT16_MAX) {
							uint16_t oldTarget =
								g_paiContext
									.controller
									->targetObjIdx;
							int projectileIndex;
							g_paiContext.controller
								->targetObjIdx =
								*turretTargetIndex;
							g_paiContext.controller
								->targetComponent =
								paifight_SelectTargetComponentMesh(
									g_paiContext
										.controller
										->targetObjIdx);
							projectileIndex = laser_firemissile(
								g_paiContext
									.objectIndex,
								weaponSlotIndex,
								g_curCraft
									->weaponSlots
										[weaponSlotIndex]
									.projectileTypeId,
								UINT16_MAX);
							if (projectileIndex !=
							    -1) {
								g_projectileGuidanceStates
									[projectileIndex]
										.homingTier =
									(GameRand() &
									 3) +
									3;
								g_curCraft
									->weaponSlots
										[weaponSlotIndex]
									.missileDefenseCooldown =
									20;
							}
							g_paiContext.controller
								->targetObjIdx =
								oldTarget;
						}
					}
				}
			}
			++weaponSlotIndex;
		}
		++launcherIndex;
	}
	return 0;
}

/* Order 6: aims each turret of the craft at whatever attacks it; returns 0 on
 * every path. Clears requireUndisabledTarget. Unless the flight group's status1
 * or status2 is 14, each turret slot (weapon type 2) whose retarget timer has
 * run out loses its target and gets ammoCount 0; for a craft breaking up, with
 * no subsystem working or with weapons inhibited it stops there. The search
 * origin is the turret's hardpoint (doubled for an Imperial Star Destroyer), or
 * the craft's position for the Super Star Destroyer. The turret keeps the last
 * attacker when it can be targeted, lies within AI_TARGET_RANGE_MAX (plus the
 * model's extent for the Super Star Destroyer), the line of fire is clear, it
 * is not the board2pln target nor a shieldless disableldr1pln target. Else it
 * clears lastAttackerObjIdx and takes the nearest targetable craft attacking
 * this one with a clear line of fire. A turret given a target adds 472 ticks to
 * its retarget timer; on the Super Star Destroyer a turret whose last attacker
 * two turrets already have stays idle. Last, a starfighter with countermeasure
 * rounds answers the first homing warhead aimed at it within
 * g_aiWarheadThreatRangeBySkill (three times that for concussion missiles) when
 * its countermeasure system works: chaff adds 10 to an empty
 * chaffActiveSeconds, using a round unless the flight group's status1 or
 * status2 is 21; a flare fires when none chases that warhead yet. Sets the
 * collision probe globals and g_lastRoughDistance. */
// FUNCTION: XVT 0x45FBF0
int16_t paifight_gunnerselfdefenseorder(void)
{
	enum {
		DISABLED_FLIGHT_GROUP_STATUS = 14,
		GUNNER_WEAPON_TYPE = 2,
		RETARGET_COOLDOWN_TICKS = 472,
		MAX_SHARED_TURRETS = 2,
		CHAFF_ACTIVE_SECONDS = 10,
		UNLIMITED_AMMO_STATUS = 21,
	};

	int isSuperStarDestroyer;
	int flightGroupArrayIndex;
	uint16_t weaponSlotIndex;
	uint8_t objectType;

	g_paiContext.requireUndisabledTarget = 0;
	objectType = g_objectTable[g_paiContext.objectIndex].objectType;
	isSuperStarDestroyer = objectType == CRAFT_SPECIES_SUPER_STAR_DESTROYER;
	flightGroupArrayIndex =
		g_objectTable[g_paiContext.objectIndex].flightGroupIdx;
	if (g_missionFlightGroups[flightGroupArrayIndex].fg.status1 !=
		    DISABLED_FLIGHT_GROUP_STATUS &&
	    g_missionFlightGroups[flightGroupArrayIndex].fg.status2 !=
		    DISABLED_FLIGHT_GROUP_STATUS) {
		for (weaponSlotIndex = 0;
		     weaponSlotIndex < g_curCraft->laserSlotCount;
		     ++weaponSlotIndex) {
			TurretTargetState *turretState;
			uint16_t lastAttackerObjIdx;
			int validTarget;
			int searchForTarget;

			if (g_curCraft->weaponSlots[weaponSlotIndex]
				    .projectileTypeId != GUNNER_WEAPON_TYPE) {
				continue;
			}
			turretState =
				&g_curCraft
					 ->turretTargetStates[weaponSlotIndex];
			if (turretState->retargetCooldownTimer > 0) {
				continue;
			}

			turretState->targetObjIdx = UINT16_MAX;
			g_curCraft->weaponSlots[weaponSlotIndex].ammoCount = 0;
			if (g_curCraft->objectKind ==
				    CRAFT_OBJECT_KIND_BREAKING_UP ||
			    g_curCraft->workingSubsystems == 0 ||
			    g_curCraft->weaponFireInhibitTimer != 0) {
				continue;
			}

			g_paifightSearchOriginX = g_paiContext.craftPositionX;
			g_paifightSearchOriginY = g_paiContext.craftPositionY;
			g_paifightSearchOriginZ = g_paiContext.craftPositionZ;
			if (!isSuperStarDestroyer) {
				pai_calcrotatedpoint(
					&g_objectTable[g_paiContext
							       .objectIndex],
					g_modelDefs[g_curCraft->modelIndex]
						.weaponHardpoints
							[weaponSlotIndex]
						.x,
					g_modelDefs[g_curCraft->modelIndex]
						.weaponHardpoints
							[weaponSlotIndex]
						.z,
					g_modelDefs[g_curCraft->modelIndex]
						.weaponHardpoints
							[weaponSlotIndex]
						.y);
				if (g_objectTable[g_paiContext.objectIndex]
					    .objectType ==
				    CRAFT_SPECIES_IMPERIAL_STAR_DESTROYER) {
					g_rotatedX *= 2;
					g_rotatedY *= 2;
					g_rotatedZ *= 2;
				}
				g_paifightSearchOriginX += g_rotatedX;
				g_paifightSearchOriginY += g_rotatedY;
				g_paifightSearchOriginZ += g_rotatedZ;
				g_collisionSegmentStartWorldX =
					g_paifightSearchOriginX;
				g_collisionSegmentStartWorldY =
					g_paifightSearchOriginY;
				g_collisionSegmentStartWorldZ =
					g_paifightSearchOriginZ;
			}

			lastAttackerObjIdx = g_curCraft->lastAttackerObjIdx;
			validTarget =
				pai_IsObjectTargetable(lastAttackerObjIdx);

			searchForTarget = validTarget == 0;
			if (validTarget != 0) {
				unsigned int maxRangeScore;
				int clearSweep;

				g_lastRoughDistance = collide_roughdistance3d(
					g_objectTable[lastAttackerObjIdx]
							.world_x -
						g_paifightSearchOriginX,
					g_objectTable[lastAttackerObjIdx]
							.world_y -
						g_paifightSearchOriginY,
					g_objectTable[lastAttackerObjIdx]
							.world_z -
						g_paifightSearchOriginZ);
				maxRangeScore = AI_TARGET_RANGE_MAX;
				if (isSuperStarDestroyer) {
					maxRangeScore +=
						g_objectTypeTable
							[g_objectTable
								 [g_paiContext
									  .objectIndex]
									 .objectType]
								.maxBoundsExtent;
				}
				if ((unsigned int)g_lastRoughDistance >=
				    maxRangeScore) {
					searchForTarget = 1;
				} else {
					clearSweep = 1;
					if (!isSuperStarDestroyer) {
						Mission_ResolveObjectOrMissionPointWorldLoc(
							lastAttackerObjIdx, 0);
						g_collisionProbeWorldX =
							g_worldLocX;
						g_collisionProbeWorldY =
							g_worldLocY;
						g_collisionProbeWorldZ =
							g_worldLocZ;
						clearSweep =
							collide_CheckSweptModelCollision(
								g_paiContext
									.objectIndex,
								g_paiContext
									.objectIndex) ==
							0;
					}
					if (!clearSweep ||
					    (strcmp(g_planTable
							    [g_paiContext
								     .controller
								     ->currentPlanId]
								    .name,
						    "board2pln") == 0 &&
					     g_paiContext.controller
							     ->targetObjIdx ==
						     lastAttackerObjIdx)) {
						searchForTarget = 1;
					} else if (
						strcmp(g_planTable
							       [g_paiContext
									.controller
									->currentPlanId]
								       .name,
						       "disableldr1pln") == 0 &&
						lastAttackerObjIdx ==
							g_paiContext.controller
								->targetObjIdx &&
						g_objectTable[g_paiContext
								      .controller
								      ->targetObjIdx]
								.mobj->pCraft
								->shieldEnergy
									[0] ==
							0) {
						searchForTarget = 1;
					} else {
						if (isSuperStarDestroyer) {
							int sharedTurretCount;
							int turretIndex;

							sharedTurretCount = 0;
							for (turretIndex = 0;
							     turretIndex <
								     g_curCraft
									     ->laserSlotCount &&
							     sharedTurretCount <
								     MAX_SHARED_TURRETS;
							     ++turretIndex) {
								if (g_curCraft
									    ->turretTargetStates
										    [turretIndex]
									    .targetObjIdx ==
								    lastAttackerObjIdx) {
									++sharedTurretCount;
								}
							}
							if (sharedTurretCount >=
							    MAX_SHARED_TURRETS) {
								continue;
							}
						}
						turretState->targetObjIdx =
							lastAttackerObjIdx;
						turretState
							->retargetCooldownTimer +=
							RETARGET_COOLDOWN_TICKS;
						continue;
					}
				}
			}

			if (searchForTarget) {
				unsigned int bestRangeScore;
				int16_t bestTargetObjIdx;
				uint16_t candidateObjIdx;

				g_curCraft->lastAttackerObjIdx = UINT16_MAX;
				bestRangeScore = AI_TARGET_RANGE_MAX;
				bestTargetObjIdx = -1;
				if (isSuperStarDestroyer) {
					bestRangeScore +=
						g_objectTypeTable
							[g_objectTable
								 [g_paiContext
									  .objectIndex]
									 .objectType]
								.maxBoundsExtent;
				}

				for (candidateObjIdx = (uint16_t)
					     g_activeRegionObjectSlotStart;
				     candidateObjIdx <
				     g_activeRegionCraftObjectSlotEnd;
				     ++candidateObjIdx) {
					int candidateArrayIndex;
					ObjectRecord *candidateObject;
					CraftData *candidateCraft;
					int candidateValid;
					int clearSweep;

					candidateArrayIndex = candidateObjIdx;
					if (g_objectTable[candidateArrayIndex]
						    .objectType == 0) {
						continue;
					}
					candidateCraft =
						g_objectTable
							[candidateArrayIndex]
								.mobj->pCraft;
					if (candidateCraft->aiController
							    .targetObjIdx !=
						    g_paiContext.objectIndex ||
					    (candidateCraft->aiController
							     .maneuverMode !=
						     AI_MANEUVER_MODE_ATTACK &&
					     candidateCraft->aiController
							     .maneuverMode !=
						     AI_MANEUVER_MODE_ROCKET_ATTACK)) {
						continue;
					}

					candidateValid = 0;
					candidateObject =
						&g_objectTable
							[candidateArrayIndex];
					candidateValid = pai_IsObjectTargetable(
						candidateObjIdx);
					if (candidateValid == 0) {
						continue;
					}

					g_lastRoughDistance = collide_roughdistance3d(
						candidateObject->world_x -
							g_paifightSearchOriginX,
						candidateObject->world_y -
							g_paifightSearchOriginY,
						candidateObject->world_z -
							g_paifightSearchOriginZ);
					if (isSuperStarDestroyer) {
						int turretIndex;

						for (turretIndex = 0;
						     turretIndex <
						     g_curCraft->laserSlotCount;
						     ++turretIndex) {
							if (g_curCraft
								    ->turretTargetStates
									    [turretIndex]
								    .targetObjIdx ==
							    candidateObjIdx) {
								g_lastRoughDistance +=
									AI_TURRET_TARGET_PENALTY;
							}
						}
					}
					if ((unsigned int)g_lastRoughDistance >=
					    bestRangeScore) {
						continue;
					}

					clearSweep = 1;
					if (!isSuperStarDestroyer) {
						Mission_ResolveObjectOrMissionPointWorldLoc(
							candidateObjIdx, 0);
						g_collisionProbeWorldX =
							g_worldLocX;
						g_collisionProbeWorldY =
							g_worldLocY;
						g_collisionProbeWorldZ =
							g_worldLocZ;
						clearSweep =
							collide_CheckSweptModelCollision(
								g_paiContext
									.objectIndex,
								g_paiContext
									.objectIndex) ==
							0;
					}
					if (clearSweep) {
						bestTargetObjIdx = (int16_t)
							candidateObjIdx;
						bestRangeScore = (unsigned int)
							g_lastRoughDistance;
					}
				}

				if (bestTargetObjIdx != -1) {
					turretState->targetObjIdx =
						bestTargetObjIdx;
					if (turretState
						    ->retargetCooldownTimer <=
					    0) {
						turretState
							->retargetCooldownTimer +=
							RETARGET_COOLDOWN_TICKS;
					}
				}
			}
		}
	}

	if (g_objectTable[g_paiContext.objectIndex].genusId ==
		    CRAFT_GENUS_STARFIGHTER &&
	    g_curCraft->cmTypeId != COUNTERMEASURE_TYPE_NONE &&
	    g_curCraft->cmAmmoCount != 0) {
		uint16_t threatProjectileObjIdx;
		int threatRange;

		threatRange =
			g_aiWarheadThreatRangeBySkill[g_paiContext.skillTier];
		for (threatProjectileObjIdx =
			     (uint16_t)g_projectileObjectSlotStart;
		     threatProjectileObjIdx < g_projectileObjectSlotEnd;
		     ++threatProjectileObjIdx) {
			ObjectRecord *projectileObject;
			WarheadGuidanceState *guidance;
			int maxRangeScore;

			projectileObject =
				&g_objectTable[threatProjectileObjIdx];
			if (projectileObject->objectType == 0) {
				continue;
			}
			guidance = projectileObject->mobj->pWarheadGuidance;
			if (guidance->homingTier == 0 ||
			    guidance->targetObjIdx !=
				    g_paiContext.objectIndex) {
				continue;
			}
			if (projectileObject->objectType ==
				    WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE ||
			    projectileObject->objectType ==
				    WARHEAD_OBJECT_TYPE_ADVANCED_CONCUSSION_MISSILE) {
				maxRangeScore = 3 * threatRange;
			} else {
				maxRangeScore = threatRange;
			}
			if (pai_IsObjectWithinRangeOfCraft(
				    threatProjectileObjIdx, maxRangeScore) ==
			    1) {
				if ((g_curCraft->workingSubsystems &
				     CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES) !=
				    0) {
					if (g_curCraft->cmTypeId ==
					    COUNTERMEASURE_TYPE_CHAFF) {
						if (g_curCraft
							    ->chaffActiveSeconds ==
						    0) {
							g_curCraft
								->chaffActiveSeconds +=
								CHAFF_ACTIVE_SECONDS;
							if (g_missionFlightGroups[g_paiContext
											  .craftFlightGroupIndex]
									    .fg
									    .status1 !=
								    UNLIMITED_AMMO_STATUS &&
							    g_missionFlightGroups[g_paiContext
											  .craftFlightGroupIndex]
									    .fg
									    .status2 !=
								    UNLIMITED_AMMO_STATUS) {
								--g_curCraft
									  ->cmAmmoCount;
							}
						}
					} else if (g_curCraft->cmTypeId ==
						   COUNTERMEASURE_TYPE_FLARE) {
						uint16_t flareObjIdx;
						int flareAlreadyTargetingThreat;

						flareAlreadyTargetingThreat = 0;
						for (flareObjIdx = (uint16_t)
							     g_projectileObjectSlotStart;
						     flareObjIdx <
						     g_projectileObjectSlotEnd;
						     ++flareObjIdx) {
							ObjectRecord
								*flareObject;
							WarheadGuidanceState
								*flareGuidance;

							flareObject =
								&g_objectTable
									[flareObjIdx];
							if (flareObject
								    ->objectType !=
							    COUNTERMEASURE_PROJECTILE_OBJECT_TYPE) {
								continue;
							}
							flareGuidance =
								flareObject
									->mobj
									->pWarheadGuidance;
							if (flareGuidance->homingTier !=
								    0 &&
							    flareGuidance->targetObjIdx ==
								    threatProjectileObjIdx) {
								flareAlreadyTargetingThreat =
									1;
								break;
							}
						}
						if (!flareAlreadyTargetingThreat &&
						    g_curCraft->cmFireCooldownTimer ==
							    0) {
							laser_createcountermeasureprojectile(
								g_paiContext
									.objectIndex,
								COUNTERMEASURE_PROJECTILE_OBJECT_TYPE);
						}
					}
				}
				return 0;
			}
		}
	}
	return 0;
}

/* Order 7: aims the craft's idle turrets at its order's targets; returns 0 on
 * every path. Nothing happens for a craft breaking up, with no subsystem
 * working, of a flight group with status1 or status2 14, or whose model has no
 * gunner mount. It sets targetSearchFlags 0x30 and requireUndisabledTarget, 1
 * on starshipdisablepln or disableldr1pln. Each turret slot (weapon type 2)
 * with no target and its retarget timer run out gets
 * SIMULATION_TICKS_PER_SECOND more on the timer and ammoCount 0; the two
 * candidate sets are built once for the order's two pairs of target conditions.
 * The search origin is the turret's hardpoint (doubled for an Imperial Star
 * Destroyer), or the craft's position for the Super Star Destroyer, also kept
 * in g_paiContext. On starshipprotectpln and starshipescortpln the turret takes
 * the nearest attacker of an order target; else the nearest target in set 0,
 * else set 1, with ammoCount 1 on the disable plans. A target found adds
 * SIMULATION_TICKS_PER_SECOND more to the timer. */
// FUNCTION: XVT 0x4606E0
int16_t paifight_gunneroffenseorder(void)
{
	enum {
		DISABLED_FLIGHT_GROUP_STATUS = 14,
		GUNNER_WEAPON_TYPE = 2,
		GUNNER_MOUNT_TYPE = 2,
		TARGET_SEARCH_FLAGS = 0x30,
	};

	int candidateSetsNeedBuild;
	int16_t hasGunnerMount;
	uint16_t mountIndex;
	uint16_t weaponSlotIndex;
	int expandedProbe;

	candidateSetsNeedBuild = 1;
	if (g_curCraft->objectKind == CRAFT_OBJECT_KIND_BREAKING_UP) {
		return 0;
	}
	if (g_curCraft->workingSubsystems == 0) {
		return 0;
	}

	if (g_missionFlightGroups[g_objectTable[g_paiContext.objectIndex]
					  .flightGroupIdx]
			    .fg.status1 == DISABLED_FLIGHT_GROUP_STATUS ||
	    g_missionFlightGroups[g_objectTable[g_paiContext.objectIndex]
					  .flightGroupIdx]
			    .fg.status2 == DISABLED_FLIGHT_GROUP_STATUS) {
		return 0;
	}

	mountIndex = 0;
	hasGunnerMount = 0;
	for (; mountIndex < 2; ++mountIndex) {
		if (g_modelDefs[g_curCraft->modelIndex]
			    .laserGroupMountType[mountIndex] ==
		    GUNNER_MOUNT_TYPE) {
			hasGunnerMount = 1;
		}
	}
	if (!hasGunnerMount) {
		return 0;
	}

	g_paiContext.requireUndisabledTarget = 0;
	g_paiContext.targetSearchFlags = TARGET_SEARCH_FLAGS;
	if (strcmp(g_planTable[g_paiContext.controller->currentPlanId].name,
		   "starshipprotectpln") != 0 &&
	    strcmp(g_planTable[g_paiContext.controller->currentPlanId].name,
		   "starshipescortpln") != 0) {
		g_paiContext.requireUndisabledTarget =
			strcmp(g_planTable[g_paiContext.controller
						   ->currentPlanId]
				       .name,
			       "starshipdisablepln") == 0 ||
			strcmp(g_planTable[g_paiContext.controller
						   ->currentPlanId]
				       .name,
			       "disableldr1pln") == 0;
	}

	expandedProbe = g_objectTable[g_paiContext.objectIndex].objectType ==
			CRAFT_SPECIES_SUPER_STAR_DESTROYER;
	for (weaponSlotIndex = 0; weaponSlotIndex < g_curCraft->laserSlotCount;
	     ++weaponSlotIndex) {
		TurretTargetState *turretState =
			&g_curCraft->turretTargetStates[weaponSlotIndex];
		int16_t targetObjectIndex;

		if (g_curCraft->weaponSlots[weaponSlotIndex].projectileTypeId !=
			    GUNNER_WEAPON_TYPE ||
		    turretState->targetObjIdx != UINT16_MAX ||
		    turretState->retargetCooldownTimer > 0) {
			continue;
		}

		turretState->retargetCooldownTimer +=
			SIMULATION_TICKS_PER_SECOND;
		if (candidateSetsNeedBuild == 1) {
			paifight_BuildGunnerTargetCandidateSet(
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.target1Type,
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.target1,
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.target1OrTarget2,
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.target2Type,
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.target2,
				0);
			paifight_BuildGunnerTargetCandidateSet(
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.secondaryTargetTypes[0],
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.secondaryTargets[0],
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.target3OrTarget4,
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.secondaryTargetTypes[1],
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.secondaryTargets[1],
				1);
			candidateSetsNeedBuild = 0;
		}

		g_paifightSearchOriginX = g_paiContext.craftPositionX;
		g_paifightSearchOriginY = g_paiContext.craftPositionY;
		g_paifightSearchOriginZ = g_paiContext.craftPositionZ;
		if (expandedProbe) {
			g_paiContext.targetSearchOriginX =
				g_paiContext.craftPositionX;
			g_paiContext.targetSearchOriginY =
				g_paiContext.craftPositionY;
			g_paiContext.targetSearchOriginZ =
				g_paiContext.craftPositionZ;
		} else {
			pai_calcrotatedpoint(
				&g_objectTable[g_paiContext.objectIndex],
				g_modelDefs[g_curCraft->modelIndex]
					.weaponHardpoints[weaponSlotIndex]
					.x,
				g_modelDefs[g_curCraft->modelIndex]
					.weaponHardpoints[weaponSlotIndex]
					.z,
				g_modelDefs[g_curCraft->modelIndex]
					.weaponHardpoints[weaponSlotIndex]
					.y);
			if (g_objectTable[g_paiContext.objectIndex]
				    .objectType ==
			    CRAFT_SPECIES_IMPERIAL_STAR_DESTROYER) {
				g_rotatedX *= 2;
				g_rotatedY *= 2;
				g_rotatedZ *= 2;
			}
			g_paifightSearchOriginX += g_rotatedX;
			g_paiContext.targetSearchOriginX =
				g_paifightSearchOriginX;
			g_paifightSearchOriginY += g_rotatedY;
			g_paiContext.targetSearchOriginY =
				g_paifightSearchOriginY;
			g_paifightSearchOriginZ += g_rotatedZ;
			g_paiContext.targetSearchOriginZ =
				g_paifightSearchOriginZ;
			g_collisionSegmentStartWorldX = g_paifightSearchOriginX;
			g_collisionSegmentStartWorldY = g_paifightSearchOriginY;
			g_collisionSegmentStartWorldZ = g_paifightSearchOriginZ;
		}

		g_curCraft->weaponSlots[weaponSlotIndex].ammoCount = 0;
		if (strcmp(g_planTable[g_paiContext.controller->currentPlanId]
				   .name,
			   "starshipprotectpln") == 0 ||
		    strcmp(g_planTable[g_paiContext.controller->currentPlanId]
				   .name,
			   "starshipescortpln") == 0) {
			targetObjectIndex =
				paifight_FindAttackerOfOrderTargetFromOrder(
					g_paiContext.orderSlot);
			turretState->targetObjIdx = targetObjectIndex;
			if (targetObjectIndex != -1) {
				turretState->retargetCooldownTimer +=
					SIMULATION_TICKS_PER_SECOND;
			}
			continue;
		}

		targetObjectIndex =
			paifight_FindNearestGunnerTargetInCandidateSet(
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.target1Type,
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.target1,
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.target1OrTarget2,
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.target2Type,
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.target2,
				0);
		if (targetObjectIndex != -1) {
			turretState->targetObjIdx = targetObjectIndex;
			turretState->retargetCooldownTimer +=
				SIMULATION_TICKS_PER_SECOND;
			if (strcmp(g_planTable[g_paiContext.controller
						       ->currentPlanId]
					   .name,
				   "starshipdisablepln") == 0 ||
			    strcmp(g_planTable[g_paiContext.controller
						       ->currentPlanId]
					   .name,
				   "disableldr1pln") == 0) {
				g_curCraft->weaponSlots[weaponSlotIndex]
					.ammoCount = 1;
			}
		} else {
			targetObjectIndex = paifight_FindNearestGunnerTargetInCandidateSet(
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.secondaryTargetTypes[0],
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.secondaryTargets[0],
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.target3OrTarget4,
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.secondaryTargetTypes[1],
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.secondaryTargets[1],
				1);
			if (targetObjectIndex != -1) {
				turretState->targetObjIdx = targetObjectIndex;
				turretState->retargetCooldownTimer +=
					SIMULATION_TICKS_PER_SECOND;
				if (strcmp(g_planTable[g_paiContext.controller
							       ->currentPlanId]
						   .name,
					   "starshipdisablepln") == 0 ||
				    strcmp(g_planTable[g_paiContext.controller
							       ->currentPlanId]
						   .name,
					   "disableldr1pln") == 0) {
					g_curCraft->weaponSlots[weaponSlotIndex]
						.ammoCount = 1;
				}
			}
		}
	}
	return 0;
}

/* Returns the nearest turret target from g_paifightSearchOriginX, Y and Z, or
 * -1: a craft marked in candidate set candidateSetIdx, or a targetable static
 * object with behavior flag 2 that matches the target conditions, closer than
 * AI_TARGET_RANGE_MAX (plus the model's extent for the Super Star Destroyer,
 * which also adds AI_TURRET_TARGET_PENALTY for each turret already on it), with
 * a clear line of fire except on the Super Star Destroyer. In a version 14
 * mission it then gives up the pick when a craft in the craft slots that is not
 * an enemy blocks the line of fire: any such craft when the turret's craft is
 * stopped, else one that is not a starfighter, transport or utility vehicle.
 * Sets the collision probe globals, g_lastRoughDistance and
 * g_gunnerCollisionProbeCount. */
// FUNCTION: XVT 0x460DD0
int16_t paifight_FindNearestGunnerTargetInCandidateSet(
	int16_t target1Type, uint16_t target1, int16_t target1OrTarget2,
	int16_t target2Type, uint16_t target2, int candidateSetIdx)
{
	enum {
		TARGETABLE_STATIC_MODEL_FLAG = 2,
		MISSION_VERSION_WITH_GUNNER_OBSTRUCTION_CHECK = 14,
	};

	uint16_t bestObjectIndex;
	unsigned int bestRangeScore;
	int expandedProbe;
	uint16_t objectIndex;

	bestObjectIndex = UINT16_MAX;
	expandedProbe = g_objectTable[g_paiContext.objectIndex].objectType ==
			CRAFT_SPECIES_SUPER_STAR_DESTROYER;
	bestRangeScore = AI_TARGET_RANGE_MAX;
	if (expandedProbe) {
		bestRangeScore +=
			g_objectTypeTable
				[g_objectTable[g_paiContext.objectIndex]
					 .objectType]
					.maxBoundsExtent;
	}

	for (objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
	     objectIndex < g_activeRegionCraftObjectSlotEnd; ++objectIndex) {
		int objectArrayIndex;
		int clearSweep;

		if (g_paifightGunnerTargetCandidateSet[2 * objectIndex +
						       candidateSetIdx] == 0) {
			continue;
		}

		objectArrayIndex = objectIndex;
		g_lastRoughDistance = collide_roughdistance3d(
			g_objectTable[objectArrayIndex].world_x -
				g_paifightSearchOriginX,
			g_objectTable[objectArrayIndex].world_y -
				g_paifightSearchOriginY,
			g_objectTable[objectArrayIndex].world_z -
				g_paifightSearchOriginZ);
		if (expandedProbe) {
			int turretIndex;

			for (turretIndex = 0;
			     turretIndex < g_curCraft->laserSlotCount;
			     ++turretIndex) {
				if (g_curCraft->turretTargetStates[turretIndex]
					    .targetObjIdx == objectIndex) {
					g_lastRoughDistance +=
						AI_TURRET_TARGET_PENALTY;
				}
			}
		}
		if ((unsigned int)g_lastRoughDistance >= bestRangeScore) {
			continue;
		}

		if (expandedProbe) {
			clearSweep = 1;
		} else {
			g_collisionProbeWorldX =
				g_objectTable[objectArrayIndex].world_x;
			g_collisionProbeWorldY =
				g_objectTable[objectArrayIndex].world_y;
			g_collisionProbeWorldZ =
				g_objectTable[objectArrayIndex].world_z;
			++g_gunnerCollisionProbeCount;
			clearSweep = collide_CheckSweptModelCollision(
					     g_paiContext.objectIndex,
					     g_paiContext.objectIndex) == 0;
		}
		if (clearSweep) {
			bestObjectIndex = objectIndex;
			bestRangeScore = (unsigned int)g_lastRoughDistance;
		}
	}

	{
		uint16_t staticObjectIndex;

		for (staticObjectIndex = (uint16_t)g_regionMainObjectSlotEnd;
		     staticObjectIndex <
		     g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
		     ++staticObjectIndex) {
			int objectArrayIndex;
			int16_t matchesTarget1;
			int16_t matchesTarget2;
			int validTarget;
			int clearSweep;

			objectArrayIndex = staticObjectIndex;
			if (g_objectTable[objectArrayIndex].objectType == 0 ||
			    (g_objectTypeTable[g_objectTable[objectArrayIndex]
						       .objectType]
				     .behaviorFlags &
			     TARGETABLE_STATIC_MODEL_FLAG) == 0) {
				continue;
			}

			matchesTarget1 = Mission_ObjectMatchesTriggerVariable(
				staticObjectIndex, target1Type, target1);
			matchesTarget2 = Mission_ObjectMatchesTriggerVariable(
				staticObjectIndex, target2Type, target2);
			if (target1OrTarget2 == 1) {
				matchesTarget1 |= matchesTarget2;
			} else {
				matchesTarget1 &= matchesTarget2;
			}
			if (matchesTarget1 == 0) {
				continue;
			}

			validTarget = pai_IsObjectTargetable(staticObjectIndex);
			if (validTarget == 0) {
				continue;
			}

			Mission_ResolveObjectOrMissionPointWorldLoc(
				staticObjectIndex, 0);
			g_lastRoughDistance = collide_roughdistance3d(
				g_worldLocX - g_paifightSearchOriginX,
				g_worldLocY - g_paifightSearchOriginY,
				g_worldLocZ - g_paifightSearchOriginZ);
			if (expandedProbe) {
				int turretIndex;

				for (turretIndex = 0;
				     turretIndex < g_curCraft->laserSlotCount;
				     ++turretIndex) {
					if (g_curCraft
						    ->turretTargetStates
							    [turretIndex]
						    .targetObjIdx ==
					    staticObjectIndex) {
						g_lastRoughDistance +=
							AI_TURRET_TARGET_PENALTY;
					}
				}
			}
			if ((unsigned int)g_lastRoughDistance >=
			    bestRangeScore) {
				continue;
			}

			if (expandedProbe) {
				clearSweep = 1;
			} else {
				g_collisionProbeWorldX = g_worldLocX;
				g_collisionProbeWorldY = g_worldLocY;
				g_collisionProbeWorldZ = g_worldLocZ;
				++g_gunnerCollisionProbeCount;
				clearSweep =
					collide_CheckSweptModelCollision(
						g_paiContext.objectIndex,
						g_paiContext.objectIndex) == 0;
			}
			if (clearSweep) {
				bestObjectIndex = staticObjectIndex;
				bestRangeScore =
					(unsigned int)g_lastRoughDistance;
			}
		}
	}

	if (!expandedProbe && bestRangeScore > AI_TARGET_RANGE_MAX) {
		bestObjectIndex = UINT16_MAX;
	}

	if (bestObjectIndex != UINT16_MAX &&
	    g_missionFileVersion ==
		    MISSION_VERSION_WITH_GUNNER_OBSTRUCTION_CHECK) {
		CraftData *savedCraft;
		uint16_t blockerObjectIndex;

		savedCraft = g_curCraft;
		for (blockerObjectIndex =
			     (uint16_t)g_activeRegionObjectSlotStart;
		     blockerObjectIndex < g_activeRegionCraftObjectSlotEnd;
		     ++blockerObjectIndex) {
			int blockerArrayIndex;
			ObjectRecord *blocker;
			int blockerTeam;
			MobileObject *sourceMobile;
			int sourceTeam;
			int enemy;

			blockerArrayIndex = blockerObjectIndex;
			blocker = &g_objectTable[blockerArrayIndex];
			blockerTeam =
				g_missionFlightGroups[blocker->flightGroupIdx]
					.fg.team;
			sourceMobile =
				g_objectTable[g_paiContext.objectIndex].mobj;
			sourceTeam = sourceMobile->team;
			enemy = sourceTeam != blockerTeam &&
				g_missionTeams[sourceTeam].allies[blockerTeam] <
					1;
			if (!enemy &&
			    (sourceMobile->speed == 0 ||
			     (blocker->genusId != CRAFT_GENUS_STARFIGHTER &&
			      blocker->genusId != CRAFT_GENUS_TRANSPORT &&
			      blocker->genusId !=
				      CRAFT_GENUS_UTILITY_VEHICLE))) {
				g_lastRoughDistance = collide_roughdistance3d(
					blocker->world_x -
						g_paifightSearchOriginX,
					blocker->world_y -
						g_paifightSearchOriginY,
					blocker->world_z -
						g_paifightSearchOriginZ);
				g_lastRoughDistance -=
					g_objectTypeTable[blocker->objectType]
						.maxBoundsExtent;
				if ((unsigned int)g_lastRoughDistance <=
				    bestRangeScore) {
					ObjectRecord *bestObject =
						&g_objectTable[bestObjectIndex];

					g_collisionProbeWorldX =
						bestObject->world_x;
					g_collisionProbeWorldY =
						bestObject->world_y;
					g_collisionProbeWorldZ =
						bestObject->world_z;
					if (collide_CheckSweptModelCollision(
						    blockerObjectIndex,
						    blockerObjectIndex) != 0) {
						bestObjectIndex = UINT16_MAX;
						break;
					}
				}
			}
		}
		g_curCraft = savedCraft;
	}

	return (int16_t)bestObjectIndex;
}

/* Fills candidate set candidateSetIdx of g_paifightGunnerTargetCandidateSet:
 * marks each live craft in the active region's craft slots that matches the
 * target conditions, is targetable and, with requireUndisabledTarget set, in
 * working order; clears the mark of every other slot there. */
// FUNCTION: XVT 0x461460
void paifight_BuildGunnerTargetCandidateSet(
	int16_t target1Type, uint16_t target1, int16_t target1OrTarget2,
	int16_t target2Type, uint16_t target2, uint16_t candidateSetIdx)
{
	uint16_t objectIndex;
	int candidateSetOffset;

	for (objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
	     objectIndex < g_activeRegionCraftObjectSlotEnd; ++objectIndex) {
		int16_t matchesFirst;
		int16_t matchesSecond;
		CraftData *craft;
		ObjectRecord *object;

		candidateSetOffset = candidateSetIdx;
		g_paifightGunnerTargetCandidateSet[2 * objectIndex +
						   candidateSetOffset] = 0;
		object = &g_objectTable[objectIndex];
		if (object->objectType == 0) {
			continue;
		}
		matchesFirst = Mission_ObjectMatchesTriggerVariable(
			objectIndex, target1Type, target1);
		matchesSecond = Mission_ObjectMatchesTriggerVariable(
			objectIndex, target2Type, target2);
		if (target1OrTarget2 == 1) {
			matchesFirst |= matchesSecond;
		} else {
			matchesFirst &= matchesSecond;
		}
		if (matchesFirst == 0) {
			continue;
		}
		craft = g_objectTable[objectIndex].mobj->pCraft;
		if (g_paiContext.requireUndisabledTarget != 0 &&
		    craft->workingSubsystems == 0) {
			continue;
		}

		if (pai_IsObjectTargetable(objectIndex)) {
			g_paifightGunnerTargetCandidateSet[2 * objectIndex +
							   candidateSetOffset] =
				1;
		}
	}
}

/* Returns the nearest targetable object that matches the target conditions,
 * measured from g_paifightSearchOriginX, Y and Z, or -1 when none lies within
 * AI_TARGET_RANGE_MAX. A craft must be in working order when
 * requireUndisabledTarget is set; a static object needs behavior flag 2. With
 * requireClearSweep nonzero the line of fire must be clear. Sets
 * g_lastRoughDistance and the collision probe globals. Only
 * laser_UpdateMineWeaponFire calls it. */
// FUNCTION: XVT 0x461690
int16_t paifight_FindNearestMatchingTargetFromOrigin(
	int16_t target1Type, uint16_t target1, int16_t target1OrTarget2,
	int16_t target2Type, uint16_t target2, int16_t requireClearSweep)
{
	enum {
		TARGETABLE_STATIC_MODEL_FLAG = 2,
	};

	unsigned int bestRangeScore;
	uint16_t bestObjectIndex;
	uint16_t objectIndex;
	int objectArrayIndex;
	int16_t matchesTarget1;
	int16_t matchesTarget2;
	int validTarget;
	unsigned int rangeScore;

	bestObjectIndex = UINT16_MAX;
	bestRangeScore = UINT_MAX;
	for (objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
	     objectIndex < g_activeRegionCraftObjectSlotEnd; ++objectIndex) {
		objectArrayIndex = objectIndex;
		if (g_objectTable[objectArrayIndex].objectType == 0) {
			continue;
		}

		matchesTarget1 = Mission_ObjectMatchesTriggerVariable(
			objectIndex, target1Type, target1);
		matchesTarget2 = Mission_ObjectMatchesTriggerVariable(
			objectIndex, target2Type, target2);
		if (target1OrTarget2 == 1) {
			matchesTarget1 |= matchesTarget2;
		} else {
			matchesTarget1 &= matchesTarget2;
		}
		if (matchesTarget1 == 0) {
			continue;
		}
		if (g_paiContext.requireUndisabledTarget != 0 &&
		    g_objectTable[objectArrayIndex]
				    .mobj->pCraft->workingSubsystems == 0) {
			continue;
		}

		validTarget = pai_IsObjectTargetable(objectIndex);
		if (validTarget == 0) {
			continue;
		}

		rangeScore = (unsigned int)collide_roughdistance3d(
			g_objectTable[objectArrayIndex].world_x -
				g_paifightSearchOriginX,
			g_objectTable[objectArrayIndex].world_y -
				g_paifightSearchOriginY,
			g_objectTable[objectArrayIndex].world_z -
				g_paifightSearchOriginZ);
		g_lastRoughDistance = (int)rangeScore;
		if (rangeScore >= bestRangeScore) {
			continue;
		}
		if (requireClearSweep != 0) {
			Mission_ResolveObjectOrMissionPointWorldLoc(objectIndex,
								    0);
			g_collisionProbeWorldX = g_worldLocX;
			g_collisionProbeWorldY = g_worldLocY;
			g_collisionProbeWorldZ = g_worldLocZ;
			if (collide_CheckSweptModelCollision(
				    g_paiContext.objectIndex,
				    g_paiContext.objectIndex) != 0) {
				continue;
			}
			rangeScore = (unsigned int)g_lastRoughDistance;
		}
		bestObjectIndex = objectIndex;
		bestRangeScore = rangeScore;
	}

	for (objectIndex = (uint16_t)g_regionMainObjectSlotEnd;
	     objectIndex <
	     g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
	     ++objectIndex) {
		objectArrayIndex = objectIndex;
		if (g_objectTable[objectArrayIndex].objectType == 0 ||
		    (g_objectTypeTable[g_objectTable[objectArrayIndex]
					       .objectType]
			     .behaviorFlags &
		     TARGETABLE_STATIC_MODEL_FLAG) == 0) {
			continue;
		}

		matchesTarget1 = Mission_ObjectMatchesTriggerVariable(
			objectIndex, target1Type, target1);
		matchesTarget2 = Mission_ObjectMatchesTriggerVariable(
			objectIndex, target2Type, target2);
		if (target1OrTarget2 == 1) {
			matchesTarget1 |= matchesTarget2;
		} else {
			matchesTarget1 &= matchesTarget2;
		}
		if (matchesTarget1 == 0) {
			continue;
		}

		validTarget = pai_IsObjectTargetable(objectIndex);
		if (validTarget == 0) {
			continue;
		}

		Mission_ResolveObjectOrMissionPointWorldLoc(objectIndex, 0);
		rangeScore = (unsigned int)collide_roughdistance3d(
			g_worldLocX - g_paifightSearchOriginX,
			g_worldLocY - g_paifightSearchOriginY,
			g_worldLocZ - g_paifightSearchOriginZ);
		g_lastRoughDistance = (int)rangeScore;
		if (rangeScore >= bestRangeScore) {
			continue;
		}
		if (requireClearSweep != 0) {
			g_collisionProbeWorldX = g_worldLocX;
			g_collisionProbeWorldY = g_worldLocY;
			g_collisionProbeWorldZ = g_worldLocZ;
			if (collide_CheckSweptModelCollision(
				    g_paiContext.objectIndex,
				    g_paiContext.objectIndex) != 0) {
				continue;
			}
			rangeScore = (unsigned int)g_lastRoughDistance;
		}
		bestObjectIndex = objectIndex;
		bestRangeScore = rangeScore;
	}

	if (bestRangeScore > AI_TARGET_RANGE_MAX) {
		bestObjectIndex = UINT16_MAX;
	}
	return (int16_t)bestObjectIndex;
}

/* Order 13: returns 1 after making the leader's last attacker the craft's
 * target, with its signature and hasLiveTarget 0, else 0. That happens when the
 * attacker is a live starfighter or transport, the leader is active, no other
 * craft of the flight group targets it yet, and, for a flight group a player
 * owns, the player has not told the craft to avoid it. Returns 0 for a craft
 * with no leader. */
// FUNCTION: XVT 0x461C00
int16_t paifight_coverleaderorder(void)
{
	CraftData *leaderCraft;
	uint16_t lastAttackerObjIdx;
	uint16_t objectIndex;
	int alreadyCovered;

	if (g_paiContext.leaderObjectIndex == UINT8_MAX) {
		return 0;
	}

	leaderCraft =
		g_objectTable[g_paiContext.leaderObjectIndex].mobj->pCraft;
	if (leaderCraft == NULL) {
		return 0;
	}

	lastAttackerObjIdx = leaderCraft->lastAttackerObjIdx;
	if (lastAttackerObjIdx != UINT16_MAX &&
	    g_objectTable[lastAttackerObjIdx].objectType != 0 &&
	    leaderCraft->objectKind == CRAFT_OBJECT_KIND_ACTIVE) {
		if ((g_missionFlightGroups
				     [g_objectTable[g_paiContext.objectIndex]
					      .flightGroupIdx]
					     .playerOwnerIdx == -1 ||
		     g_curCraft->playerCommandAvoidTargetObjIdx !=
			     lastAttackerObjIdx) &&
		    (g_objectTable[lastAttackerObjIdx].genusId == 0 ||
		     g_objectTable[lastAttackerObjIdx].genusId == 1)) {
			alreadyCovered = 0;
			for (objectIndex =
				     (uint16_t)g_activeRegionObjectSlotStart;
			     objectIndex < g_activeRegionCraftObjectSlotEnd;
			     ++objectIndex) {
				if (objectIndex != g_paiContext.objectIndex) {
					ObjectRecord *candidateObject;

					candidateObject =
						&g_objectTable[objectIndex];
					if (candidateObject->objectType != 0 &&
					    candidateObject->flightGroupIdx ==
						    g_paiContext
							    .craftFlightGroupIndex) {
						CraftData *candidateCraft;

						candidateCraft =
							candidateObject->mobj
								->pCraft;
						if (candidateCraft != NULL &&
						    candidateCraft->aiController
								    .targetObjIdx ==
							    lastAttackerObjIdx) {
							alreadyCovered = 1;
						}
					}
				}
			}

			if (!alreadyCovered) {
				g_paiContext.controller->targetObjIdx =
					lastAttackerObjIdx;
				g_paiContext.controller->targetSignature =
					g_objectTable[lastAttackerObjIdx]
						.objectSignature;
				g_paiContext.controller->hasLiveTarget = 0;
				return 1;
			}
		}
	}

	return 0;
}

/* Order 14: returns 1 after giving the craft a target near its leader's, with
 * its signature and hasLiveTarget 1, else 0; 0 at once when an AI leader is not
 * attacking. Sets requireUndisabledTarget on disableldr1pln. The candidate
 * target comes first, when it can be targeted and, under a player leader, lies
 * within a rough 0x50000; one farther away makes it return 0. Without a leader
 * it tests object 255, the no-leader index, as the leader. Else it starts from
 * an AI leader's target, or under a player leader from the last craft hostile
 * to the leader whose last attacker the player flies. From that craft it takes,
 * starting craftOrdinal slots on, the first craft of the same flight group that
 * is targetable near this craft (range expanded) and, on capfreeldr1pln,
 * disableldr1pln or kamikaze1pln, matches the current order; for a static
 * object, the first after it that does both. Does not check lastAttackerObjIdx
 * for 0xFFFF before reading that object. The avoid test compares with the loop
 * count, not the candidate, and a static candidate that fails the first test
 * stops the search there. */
// FUNCTION: XVT 0x461DA0
int16_t paifight_followleadatkorder(void)
{
	enum { PLAYER_LEADER_TARGET_RANGE = 0x50000 };

	AiController *leaderController;
	PaiPlanRecord *plan;
	int leaderObjectIndex;
	int leaderPlayerOwner;
	uint16_t maneuverMode;
	uint16_t candidateTargetIdx;
	uint16_t targetObjIdx;
	int validTarget;
	ObjectRecord *target;

	leaderObjectIndex = g_paiContext.leaderObjectIndex;
	leaderPlayerOwner = g_objectTable[leaderObjectIndex].playerOwnerIdx;
	leaderController = &g_paiContext.leaderOrSelfCraft->aiController;
	maneuverMode = leaderController->maneuverMode;
	plan = &g_planTable[g_paiContext.controller->currentPlanId];
	if (strcmp(plan->name, "disableldr1pln") == 0) {
		g_paiContext.requireUndisabledTarget = 1;
	} else {
		g_paiContext.requireUndisabledTarget = 0;
	}
	if (maneuverMode != AI_MANEUVER_MODE_ATTACK &&
	    maneuverMode != AI_MANEUVER_MODE_ROCKET_ATTACK &&
	    leaderPlayerOwner == -1) {
		return 0;
	}

	candidateTargetIdx = g_paiContext.controller->candidateTargetIdx;
	if (candidateTargetIdx != UINT16_MAX &&
	    candidateTargetIdx != AI_TARGET_ABORT) {
		validTarget = pai_IsObjectTargetable(candidateTargetIdx);

		if (validTarget != 0) {
			if (leaderPlayerOwner != -1) {
				pai_ObjectRefUpdateRoughDistance(
					g_paiContext.objectIndex,
					candidateTargetIdx);
				if (g_lastRoughDistance >
				    PLAYER_LEADER_TARGET_RANGE) {
					return 0;
				}
			}
			g_paiContext.controller->targetObjIdx =
				candidateTargetIdx;
			g_paiContext.controller->targetSignature =
				g_objectTable[candidateTargetIdx]
					.objectSignature;
			g_paiContext.controller->hasLiveTarget = 1;
			return 1;
		}

		g_paiContext.controller->candidateTargetIdx = UINT16_MAX;
	}

	targetObjIdx = UINT16_MAX;
	leaderPlayerOwner = g_objectTable[leaderObjectIndex].playerOwnerIdx;
	if (leaderPlayerOwner == -1) {
		targetObjIdx = leaderController->targetObjIdx;
	} else {
		uint16_t objectIndex;

		for (objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
		     (int)objectIndex < g_activeRegionCraftObjectSlotEnd;
		     ++objectIndex) {
			ObjectRecord *object = &g_objectTable[objectIndex];
			if (object->objectType != 0) {
				CraftData *craft = object->mobj->pCraft;
				if ((g_paiContext.requireUndisabledTarget ==
					     0 ||
				     craft->workingSubsystems != 0) &&
				    g_curCraft->playerCommandAvoidTargetObjIdx !=
					    objectIndex &&
				    g_objectTable[craft->lastAttackerObjIdx]
						    .playerOwnerIdx ==
					    leaderPlayerOwner) {
					int objectTeam =
						g_missionFlightGroups
							[object->flightGroupIdx]
								.fg.team;
					int leaderTeam =
						g_objectTable[leaderObjectIndex]
							.mobj->team;
					int isHostile;

					if (objectTeam == leaderTeam) {
						isHostile = 0;
					} else {
						isHostile =
							g_missionTeams[leaderTeam]
								.allies[objectTeam] ==
							0;
					}
					if (isHostile != 0) {
						targetObjIdx = objectIndex;
					}
				}
			}
		}
	}

	if (targetObjIdx == UINT16_MAX) {
		return 0;
	}

	target = &g_objectTable[targetObjIdx];
	if (target->mobj != NULL) {
		if (target->mobj->pCraft != NULL) {
			uint16_t flightGroupIdx;
			uint16_t candidateIndex;
			uint16_t objectIndex;
			flightGroupIdx = target->flightGroupIdx;
			candidateIndex = (uint16_t)(g_curCraft->craftOrdinal +
						    targetObjIdx);
			if ((int)candidateIndex >=
			    g_activeRegionCraftObjectSlotEnd) {
				candidateIndex =
					(uint16_t)g_activeRegionObjectSlotStart;
			}

			for (objectIndex =
				     (uint16_t)g_activeRegionObjectSlotStart;
			     (int)objectIndex <
			     g_activeRegionCraftObjectSlotEnd;
			     ++objectIndex) {
				ObjectRecord *candidate =
					&g_objectTable[candidateIndex];
				CraftData *candidateCraft =
					candidate->mobj->pCraft;
				if (candidateCraft == NULL) {
					if ((int)++candidateIndex >=
					    g_activeRegionCraftObjectSlotEnd) {
						candidateIndex = (uint16_t)
							g_activeRegionObjectSlotStart;
					}
					continue;
				}
				if ((g_paiContext.requireUndisabledTarget !=
					     0 &&
				     candidateCraft->workingSubsystems == 0) ||
				    g_curCraft->playerCommandAvoidTargetObjIdx ==
					    objectIndex) {
					if ((int)++candidateIndex >=
					    g_activeRegionCraftObjectSlotEnd) {
						candidateIndex = (uint16_t)
							g_activeRegionObjectSlotStart;
					}
					continue;
				}
				if (candidate->objectType != 0 &&
				    candidate->flightGroupIdx ==
					    flightGroupIdx &&
				    pai_IsObjectTargetableNearCraft(
					    g_paiContext.objectIndex,
					    candidateIndex, 1)) {
					if (strcmp(plan->name,
						   "capfreeldr1pln") != 0 &&
					    strcmp(plan->name,
						   "disableldr1pln") != 0 &&
					    strcmp(plan->name,
						   "kamikaze1pln") != 0) {
						g_paiContext.controller
							->targetObjIdx =
							candidateIndex;
						g_paiContext.controller
							->targetSignature =
							g_objectTable[candidateIndex]
								.objectSignature;
						g_paiContext.controller
							->hasLiveTarget = 1;
						return 1;
					}
					if (pai_CurrentOrderTargetsMatchObject(
						    candidateIndex) != 0) {
						g_paiContext.controller
							->targetObjIdx =
							candidateIndex;
						g_paiContext.controller
							->targetSignature =
							g_objectTable[candidateIndex]
								.objectSignature;
						g_paiContext.controller
							->hasLiveTarget = 1;
						return 1;
					}
				}
				if ((int)++candidateIndex >=
				    g_activeRegionCraftObjectSlotEnd) {
					candidateIndex = (uint16_t)
						g_activeRegionObjectSlotStart;
				}
			}
		}
	} else {
		uint16_t staticCandidateIdx = (uint16_t)(targetObjIdx + 1);
		uint16_t scanned;
		uint16_t flightGroupIdx = target->flightGroupIdx;
		if ((int)staticCandidateIdx >=
		    g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount) {
			staticCandidateIdx =
				(uint16_t)g_regionMainObjectSlotEnd;
		}
		for (scanned = (uint16_t)g_regionMainObjectSlotEnd;
		     (int)scanned <
		     g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
		     ++scanned) {
			ObjectRecord *candidate =
				&g_objectTable[staticCandidateIdx];
			if ((g_paiContext.requireUndisabledTarget == 0 ||
			     candidate->typeSpecificWord != 0) &&
			    g_curCraft->playerCommandAvoidTargetObjIdx !=
				    staticCandidateIdx) {
				if (candidate->objectType != 0 &&
				    candidate->flightGroupIdx ==
					    flightGroupIdx &&
				    pai_IsObjectTargetableNearCraft(
					    g_paiContext.objectIndex,
					    staticCandidateIdx, 1) &&
				    pai_CurrentOrderTargetsMatchObject(
					    staticCandidateIdx) != 0) {
					g_paiContext.controller->targetObjIdx =
						staticCandidateIdx;
					g_paiContext.controller
						->targetSignature =
						g_objectTable[staticCandidateIdx]
							.objectSignature;
					g_paiContext.controller->hasLiveTarget =
						1;
					return 1;
				}
				if ((int)++staticCandidateIdx >=
				    g_regionMainObjectSlotEnd +
					    g_regionStaticObjectSlotCount) {
					staticCandidateIdx = (uint16_t)
						g_regionMainObjectSlotEnd;
				}
			}
		}
	}

	return 0;
}

/* Order 18: sets escortTargetFG to the flight group of the nearest object that
 * matches the current order's first pair of target conditions, or else its
 * second pair; to 255 when neither finds one. Returns 0 on every path. */
// FUNCTION: XVT 0x462550
int16_t paifight_checkescortorder(void)
{
	g_paiContext.controller->escortTargetFG = -1;
	if (paifight_searchforclosestingroup(
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[g_paiContext.orderSlot]
			    .target1Type,
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[g_paiContext.orderSlot]
			    .target1,
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[g_paiContext.orderSlot]
			    .target1OrTarget2,
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[g_paiContext.orderSlot]
			    .target2Type,
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[g_paiContext.orderSlot]
			    .target2) != -1) {
		g_paiContext.controller->escortTargetFG =
			g_aiEscortCandidateFgIdx;
		return 0;
	}

	if (paifight_searchforclosestingroup(
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[g_paiContext.orderSlot]
			    .secondaryTargetTypes[0],
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[g_paiContext.orderSlot]
			    .secondaryTargets[0],
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[g_paiContext.orderSlot]
			    .target3OrTarget4,
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[g_paiContext.orderSlot]
			    .secondaryTargetTypes[1],
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[g_paiContext.orderSlot]
			    .secondaryTargets[1]) != -1) {
		g_paiContext.controller->escortTargetFG =
			g_aiEscortCandidateFgIdx;
		return 0;
	}

	return 0;
}

/* Returns the nearest live object, in the craft slots or the static slots, of a
 * flight group that matches the target conditions, or -1; sets
 * g_aiEscortCandidateFgIdx to its flight group and g_lastRoughDistance. Does
 * not check that the object can be targeted. */
// FUNCTION: XVT 0x462690
int16_t paifight_searchforclosestingroup(int16_t target1Type, uint16_t target1,
					 int16_t target1OrTarget2,
					 int16_t target2Type, uint16_t target2)
{
	uint16_t flightGroupIdx;
	uint16_t bestObjectIndex;
	unsigned int bestRangeScore;
	int16_t matchesTarget1;
	int16_t matchesTarget2;
	uint16_t objectIndex;

	flightGroupIdx = 0;
	bestObjectIndex = (int16_t)UINT16_MAX;
	bestRangeScore = UINT32_MAX;
	while (g_missionHeader.numFlightGroups > flightGroupIdx) {
		matchesTarget1 = Mission_FlightGroupMatchesTriggerVariable(
			flightGroupIdx, target1Type, target1);
		matchesTarget2 = Mission_FlightGroupMatchesTriggerVariable(
			flightGroupIdx, target2Type, target2);
		if (target1OrTarget2 == 1) {
			matchesTarget1 |= matchesTarget2;
		} else {
			matchesTarget1 &= matchesTarget2;
		}

		if (matchesTarget1 != 0) {
			for (objectIndex =
				     (uint16_t)g_activeRegionObjectSlotStart;
			     objectIndex <
			     (int)g_activeRegionCraftObjectSlotEnd;
			     ++objectIndex) {
				if (g_objectTable[objectIndex].objectType !=
					    0 &&
				    g_objectTable[objectIndex].flightGroupIdx ==
					    flightGroupIdx) {
					pai_ObjectRefUpdateRoughDistance(
						g_paiContext.objectIndex,
						objectIndex);
					if (bestRangeScore >
					    (unsigned int)g_lastRoughDistance) {
						bestObjectIndex = objectIndex;
						bestRangeScore =
							g_lastRoughDistance;
						g_aiEscortCandidateFgIdx =
							(uint8_t)flightGroupIdx;
					}
				}
			}

			for (objectIndex = (uint16_t)g_regionMainObjectSlotEnd;
			     objectIndex < (int)(g_regionMainObjectSlotEnd +
						 g_regionStaticObjectSlotCount);
			     ++objectIndex) {
				if (g_objectTable[objectIndex].objectType !=
					    0 &&
				    g_objectTable[objectIndex].flightGroupIdx ==
					    flightGroupIdx) {
					pai_ObjectRefUpdateRoughDistance(
						g_paiContext.objectIndex,
						objectIndex);
					if (bestRangeScore >
					    (unsigned int)g_lastRoughDistance) {
						bestObjectIndex = objectIndex;
						bestRangeScore =
							g_lastRoughDistance;
						g_aiEscortCandidateFgIdx =
							(uint8_t)flightGroupIdx;
					}
				}
			}
		}
		++flightGroupIdx;
	}

	return bestObjectIndex;
}

/* Returns 1 when paifight_HasFutureFgTargets finds groups still to come for the
 * order slot's first pair of target conditions or its second pair, else 0. */
// FUNCTION: XVT 0x462830
int16_t paifight_OrderSlotHasFutureTargets(uint16_t orderSlot)
{
	int orderIndex;

	orderIndex = orderSlot;
	if (paifight_HasFutureFgTargets(
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[orderIndex]
			    .target1Type,
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[orderIndex]
			    .target1,
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[orderIndex]
			    .target1OrTarget2,
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[orderIndex]
			    .target2Type,
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[orderIndex]
			    .target2) != 0) {
		return 1;
	}

	return paifight_HasFutureFgTargets(
		       g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			       .fg.orders[orderIndex]
			       .secondaryTargetTypes[0],
		       g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			       .fg.orders[orderIndex]
			       .secondaryTargets[0],
		       g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			       .fg.orders[orderIndex]
			       .target3OrTarget4,
		       g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			       .fg.orders[orderIndex]
			       .secondaryTargetTypes[1],
		       g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			       .fg.orders[orderIndex]
			       .secondaryTargets[1]) != 0;
}

/* Returns 1 when a flight group that matches the target conditions (either one
 * when targetRelationOp is 1, else both), with arrivalEnabled set or owned by a
 * player, has not arrived yet or has wavesRemaining above 0; else 0. */
// FUNCTION: XVT 0x462930
int16_t paifight_HasFutureFgTargets(int16_t target1Type, uint16_t target1,
				    int16_t targetRelationOp,
				    int16_t target2Type, uint16_t target2)
{
	uint16_t flightGroupIdx;
	int16_t matchesTarget1;
	int16_t matchesTarget2;

	for (flightGroupIdx = 0;
	     flightGroupIdx < (int16_t)g_missionHeader.numFlightGroups;
	     ++flightGroupIdx) {
		if (g_missionFgStats[flightGroupIdx].arrivalEnabled != 0 ||
		    g_missionFlightGroups[flightGroupIdx].playerOwnerIdx !=
			    -1) {
			matchesTarget1 =
				Mission_FlightGroupMatchesTriggerVariable(
					flightGroupIdx, target1Type, target1);
			matchesTarget2 =
				Mission_FlightGroupMatchesTriggerVariable(
					flightGroupIdx, target2Type, target2);

			if (targetRelationOp == 1) {
				matchesTarget1 |= matchesTarget2;
			} else {
				matchesTarget1 &= matchesTarget2;
			}

			if (matchesTarget1 != 0) {
				if (g_missionFgStats[flightGroupIdx]
					    .hasArrived == 0) {
					return 1;
				}
				if (g_missionFgStats[flightGroupIdx]
					    .wavesRemaining != 0) {
					return 1;
				}
			}
		}
	}

	return 0;
}
