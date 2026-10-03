#include "xvt/flight/ai/paiorder.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/pai_targetability.h"
#include "xvt/flight/ai/paifight.h"
#include "xvt/flight/ai/paiman.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"
#include <string.h>

/* Rough distance in world units, by skill tier 0 to 2, beyond which
 * paiorder_stillattackorder forgets an attacker that is not a warhead. Entry 3
 * is 0. Nothing writes it. */
// GLOBAL: XVT 0x5243A8
int g_aiStillAttackLastAttackerRangeBySkill[4] = {0x8000, 0xC000, 0xE000, 0};

/* Class 0, 1 or 2 of an object's bearing for each eighth of a circle (0x2000
 * angle units) it lies off the craft's yaw, counting from the yaw: 0 for the
 * first and last eighth, 2 for the two in the middle, 1 for the rest. The
 * under-attack, on-tail and avoid-hit orders pick maneuvers by it. Nothing
 * writes it. */
// GLOBAL: XVT 0x5243D8
uint8_t g_aiThreatBearingClassByOctant[8] = {0, 1, 1, 2, 2, 1, 1, 0};
/* Rough distance in world units, by skill tier 0 to 2, within which
 * paiorder_underattackorder and paiorder_avoidhitorder look for an enemy
 * starfighter pointed at the craft. Entry 3 is 0. Nothing writes it. */
// GLOBAL: XVT 0x5243B8
int g_aiAttackerSearchRangeBySkill[4] = {0x2000, 0x3000, 0x4000, 0};
/* Rough distance in world units, by skill tier 0 to 2, within which a homing
 * warhead aimed at the craft counts as a threat; the under-attack and avoid-hit
 * orders triple it for a concussion missile, and avoid-hit for type 149 too.
 * paifight_gunnerselfdefenseorder reads it as well. Entry 3 is 0. Nothing
 * writes it. */
// GLOBAL: XVT 0x5243C8
int g_aiWarheadThreatRangeBySkill[4] = {0x800, 0x1000, 0x1800, 0};
/* Four maneuvers paiorder_underattackorder picks from by the low two bits of a
 * GameRand draw, for an attacker in bearing class 0 or 1; paiman_attackmaneuver
 * reads it too. Nothing writes it. */
// GLOBAL: XVT 0x5243E0
uint8_t g_aiUnderAttackFrontSideManeuverChoices[4] = {
	AI_MANEUVER_MODE_ZOOM, AI_MANEUVER_MODE_DIVE,
	AI_MANEUVER_MODE_SPLITS_DIVE, AI_MANEUVER_MODE_IMMELMANN};
/* Eight maneuvers paiorder_underattackorder picks from by the low three bits of
 * a GameRand draw, for an attacker in bearing class 2; paiman_attackmaneuver
 * reads it too. Nothing writes it. */
// GLOBAL: XVT 0x5243E8
uint8_t g_aiUnderAttackRearManeuverChoices[8] = {
	AI_MANEUVER_MODE_TURN_INSIDE,	 AI_MANEUVER_MODE_SPLITS_DIVE,
	AI_MANEUVER_MODE_TURN_INSIDE,	 AI_MANEUVER_MODE_TURN_INSIDE,
	AI_MANEUVER_MODE_AVOID_ATTACKER, AI_MANEUVER_MODE_AVOID_ATTACKER,
	AI_MANEUVER_MODE_SCISSORS,	 AI_MANEUVER_MODE_AVOID_ATTACKER,
};

/* The order handlers by order id, as the plan text's order tokens number them;
 * pai_ProcessPlan calls them. A handler returns nonzero when its order fires,
 * which switches the craft to the plan paired with the order. Nothing writes
 * it. */
// GLOBAL: XVT 0x5243F0
PaiOrderFunc g_orderTable[48] = {
	paiorder_nullhandler,
	paiorder_updatecourseorder,
	paiorder_underattackorder,
	paiorder_stillattackorder,
	paiorder_flyhomeorder,
	paifight_fightershootorder,
	paifight_gunnerselfdefenseorder,
	paifight_gunneroffenseorder,
	paifight_missiledefenseorder,
	paifight_scanfortargetorder,
	paiorder_waitrunorder,
	paiorder_breakofforder,
	paiorder_leaderdeadorder,
	paifight_coverleaderorder,
	paifight_followleadatkorder,
	paiorder_abortmissionorder,
	paiorder_ontailorder,
	paiorder_alwaysorder,
	paifight_checkescortorder,
	paiorder_leadergohomeorder,
	paiorder_hyperspaceorder,
	paiorder_enterhangarorder,
	paiorder_mothershiporder,
	paifight_escorttargetorder,
	paiorder_lookforcrafttoboardorder,
	paiorder_abortboardorder,
	paiorder_returnboardorder,
	paiorder_awaitboardorder,
	paiorder_makedisabledorder,
	paiorder_neartargetorder,
	paiorder_rocketsonboardorder,
	paiorder_avoidhitorder,
	paiorder_waitforallreturnorder,
	paiorder_waitforallcreateorder,
	paiorder_evasiveorder,
	paiorder_targetfromplayerorder,
	paiorder_avoidstarshiporder,
	paiorder_checkhyperorder,
	paiorder_stopgohomeorder,
	paiorder_completegohomeorder,
	paiorder_completegootherorder,
	paiorder_completefolloworder,
	paiorder_waitgootherorder,
	paiorder_orderswitchorder,
	paiorder_killselforder,
	paiorder_dropoffdestorder,
	paiorder_abortmotherwaitorder,
	paiorder_playerinputorder,
};

/* Order 47: does nothing and returns 0. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4661E0
int16_t paiorder_playerinputorder(void) { return 0; }

/* Order 0: does nothing and returns 0. */
// FUNCTION: XVT 0x4661F0
int16_t paiorder_nullhandler(void) { return 0; }

/* Order 1: runs the step function of the craft's maneuverMode from
 * g_aiCourseOrderManeuverTable, through g_aiCurrentManeuverProc, which it sets,
 * and returns what that returns. Does not check maneuverMode against the
 * table's 34 entries. */
// FUNCTION: XVT 0x466200
int16_t paiorder_updatecourseorder(void)
{
	g_aiCurrentManeuverProc =
		g_aiCourseOrderManeuverTable[g_paiContext.controller
						     ->maneuverMode];
	return g_aiCurrentManeuverProc();
}

/* Order 2: for a starfighter or transport still on its plan's own maneuver,
 * picks a maneuver against an attacker; returns 0 on every path. With no
 * attacker known it looks first for a homing warhead aimed at the craft within
 * g_aiWarheadThreatRangeBySkill (three times that for a concussion missile).
 * For one it records the warhead in lastAttackerObjIdx, turns away from it in
 * bearing class 0 or turns inside otherwise, then, carrying countermeasures,
 * adds 10 to chaffActiveSeconds and uses a round (none when the flight group's
 * status1 or status2 is 21) for chaff with a working countermeasure system, or
 * else fires a countermeasure when cmFireCooldownTimer is 0, and returns. Next
 * it looks for an AI-flown enemy starfighter within
 * g_aiAttackerSearchRangeBySkill whose nose points within 0x2000 angle units of
 * the craft in yaw and pitch, only while the craft is active, and records the
 * first. For a known attacker it then picks by bearing class with GameRand: in
 * class 1, turns inside when closer than 0x2000 on a quarter of the draws, else
 * one of the front and side choices; in class 0, one of those choices on just
 * over half the draws, else a head-on attack; in class 2, one of the rear
 * choices when the attacker is faster or within 0x8000, else speeds away. A
 * non-craft attacker counts as speed 900. Each pick runs
 * paiman_initmaneuver. */
// FUNCTION: XVT 0x466220
int16_t paiorder_underattackorder(void)
{
	unsigned int selfObjIdx;
	uint16_t objectIdx;
	int maxRangeScore;

	selfObjIdx = g_paiContext.objectIndex;

	if (g_objectTable[selfObjIdx].genusId != CRAFT_GENUS_STARFIGHTER &&
	    g_objectTable[selfObjIdx].genusId != CRAFT_GENUS_TRANSPORT) {
		return 0;
	}
	if (g_paiContext.controller->maneuverMode ==
	    g_paiContext.initialManeuverId) {
		if (g_curCraft->lastAttackerObjIdx == UINT16_MAX) {
			maxRangeScore = g_aiWarheadThreatRangeBySkill
				[g_paiContext.skillTier];
			for (objectIdx = (uint16_t)g_projectileObjectSlotStart;
			     objectIdx < g_projectileObjectSlotEnd;
			     ++objectIdx) {
				uint8_t objectType =
					g_objectTable[objectIdx].objectType;
				if (objectType != 0 &&
				    g_objectTable[objectIdx]
						    .mobj->pWarheadGuidance
						    ->homingTier != 0 &&
				    g_objectTable[objectIdx]
						    .mobj->pWarheadGuidance
						    ->targetObjIdx ==
					    g_paiContext.objectIndex) {
					int threatRange = maxRangeScore * 3;
					if (objectType !=
					    WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE) {
						threatRange = maxRangeScore;
					}
					if (pai_IsObjectWithinRangeOfCraft(
						    objectIdx,
						    (unsigned int)
							    threatRange) == 1) {
						g_curCraft->lastAttackerObjIdx =
							objectIdx;
						pai_ObjectRefDirectionToObjectRef(
							g_paiContext
								.objectIndex,
							objectIdx);
						if (g_aiThreatBearingClassByOctant
							    [(uint16_t)(trig2_xyangle -
									g_objectTable[g_paiContext
											      .objectIndex]
										.yaw) >>
							     13] == 0) {
							g_paiContext.controller
								->maneuverMode =
								AI_MANEUVER_MODE_TURN_AWAY;
						} else {
							g_paiContext.controller
								->maneuverMode =
								AI_MANEUVER_MODE_TURN_INSIDE;
						}
						paiman_initmaneuver();
						if (g_curCraft->cmTypeId !=
							    COUNTERMEASURE_TYPE_NONE &&
						    g_curCraft->cmAmmoCount !=
							    0) {
							if (g_curCraft->cmTypeId ==
								    COUNTERMEASURE_TYPE_CHAFF &&
							    (g_curCraft
								     ->workingSubsystems &
							     CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES) !=
								    0) {
								g_curCraft
									->chaffActiveSeconds +=
									10;
								if (g_missionFlightGroups[g_paiContext
												  .craftFlightGroupIndex]
										    .fg
										    .status1 !=
									    21 &&
								    g_missionFlightGroups[g_paiContext
												  .craftFlightGroupIndex]
										    .fg
										    .status2 !=
									    21) {
									--g_curCraft
										  ->cmAmmoCount;
								}
							} else if (
								g_curCraft
									->cmFireCooldownTimer ==
								0) {
								laser_createcountermeasureprojectile(
									g_paiContext
										.objectIndex,
									COUNTERMEASURE_PROJECTILE_OBJECT_TYPE);
							}
						}
						return 0;
					}
				}
			}

			maxRangeScore = g_aiAttackerSearchRangeBySkill
				[g_paiContext.skillTier];
			if (g_curCraft->lastAttackerObjIdx == UINT16_MAX) {
				for (objectIdx = (uint16_t)
					     g_activeRegionObjectSlotStart;
				     objectIdx <
				     g_activeRegionCraftObjectSlotEnd;
				     ++objectIdx) {
					if (g_objectTable[objectIdx]
							    .playerOwnerIdx ==
						    -1 &&
					    g_objectTable[objectIdx]
							    .objectType != 0) {
						int objectTeam =
							g_missionFlightGroups
								[g_objectTable[objectIdx]
									 .flightGroupIdx]
									.fg
									.team;
						int sourceTeam =
							g_objectTable
								[g_paiContext
									 .objectIndex]
									.mobj
									->team;
						int isEnemy =
							objectTeam == sourceTeam
								? 0
								: g_missionTeams[sourceTeam]
										  .allies[objectTeam] ==
									  0;
						if (isEnemy &&
						    g_curCraft->objectKind ==
							    CRAFT_OBJECT_KIND_ACTIVE &&
						    g_objectTable[objectIdx]
								    .genusId ==
							    CRAFT_GENUS_STARFIGHTER &&
						    pai_IsObjectWithinRangeOfCraft(
							    objectIdx,
							    (unsigned int)
								    maxRangeScore) ==
							    1) {
							uint16_t
								horizontalAngle;
							uint16_t verticalAngle;
							pai_ObjectRefDirectionToObjectRef(
								objectIdx,
								selfObjIdx);
							horizontalAngle =
								(uint16_t)(trig2_xyangle -
									   g_objectTable[objectIdx]
										   .yaw);
							if (horizontalAngle >=
							    0x8000) {
								horizontalAngle =
									(uint16_t)-horizontalAngle;
							}
							verticalAngle =
								(uint16_t)(trig2_pitch -
									   g_objectTable[objectIdx]
										   .pitch);
							if (verticalAngle >=
							    0x8000) {
								verticalAngle =
									(uint16_t)-verticalAngle;
							}
							if (horizontalAngle <
								    0x2000 &&
							    verticalAngle <
								    0x2000) {
								g_curCraft
									->lastAttackerObjIdx =
									objectIdx;
								break;
							}
						}
					}
				}
			}
		}

		if (g_curCraft->lastAttackerObjIdx != UINT16_MAX) {
			uint8_t threatBearing;
			uint16_t attackerMaxSpeed;
			struct MobileObject *attacker;
			uint16_t ownMaxSpeed;
			uint16_t randomValue;
			uint8_t maneuverMode;

			pai_ObjectRefDirectionToObjectRef(
				selfObjIdx, g_curCraft->lastAttackerObjIdx);
			threatBearing = g_aiThreatBearingClassByOctant
				[(uint16_t)(trig2_xyangle -
					    g_objectTable[selfObjIdx].yaw) >>
				 13];
			ownMaxSpeed =
				g_modelDefs[g_curCraft->modelIndex].maxSpeed;
			attacker = g_objectTable[g_curCraft->lastAttackerObjIdx]
					   .mobj;
			if (attacker->family == 0) {
				attackerMaxSpeed =
					g_modelDefs[attacker->pCraft
							    ->modelIndex]
						.maxSpeed;
			} else {
				attackerMaxSpeed = 900;
			}
			randomValue = (uint16_t)GameRand();
			if (threatBearing == 1) {
				if (trig2_polardistance >= 0x2000 ||
				    randomValue >= 0x4000) {
					maneuverMode =
						g_aiUnderAttackFrontSideManeuverChoices
							[randomValue & 3];
				} else {
					maneuverMode =
						AI_MANEUVER_MODE_TURN_INSIDE;
				}
			} else if (threatBearing == 0) {
				maneuverMode =
					randomValue <= 0x8000
						? g_aiUnderAttackFrontSideManeuverChoices
							  [randomValue & 3]
						: AI_MANEUVER_MODE_HEAD_ON_ATTACK;
			} else {
				if (ownMaxSpeed < attackerMaxSpeed ||
				    trig2_polardistance <= 0x8000) {
					maneuverMode =
						g_aiUnderAttackRearManeuverChoices
							[randomValue & 7];
				} else {
					maneuverMode =
						AI_MANEUVER_MODE_SPEED_AWAY;
				}
			}
			g_paiContext.controller->maneuverMode = maneuverMode;
			paiman_initmaneuver();
		}
	}
	return 0;
}

/* Order 3: returns 1, after setting lastAttackerObjIdx to 0xFFFF, when the
 * craft is off its plan's maneuver and its recorded attacker is done: an object
 * in the projectile slots that is gone or no longer targets the craft, or
 * another object not within g_aiStillAttackLastAttackerRangeBySkill. Else 0.
 * Sets g_lastRoughDistance for an attacker that is not a warhead. */
// FUNCTION: XVT 0x4666E0
int16_t paiorder_stillattackorder(void)
{
	uint16_t lastAttackerObjIdx;
	unsigned int attackerIndex;
	uint16_t *lastAttackerObjIdxPtr;
	struct ObjectRecord *attacker;
	struct WarheadGuidanceState *guidance;

	if (g_paiContext.controller->maneuverMode !=
	    g_paiContext.initialManeuverId) {
		lastAttackerObjIdxPtr = &g_curCraft->lastAttackerObjIdx;
		lastAttackerObjIdx = *lastAttackerObjIdxPtr;
		if (lastAttackerObjIdx != UINT16_MAX) {
			attackerIndex = lastAttackerObjIdx;
			if (g_projectileObjectSlotStart <= (int)attackerIndex &&
			    g_projectileObjectSlotEnd > (int)attackerIndex) {
				attacker = &g_objectTable[attackerIndex];
				guidance = attacker->mobj->pWarheadGuidance;
				if (attacker->objectType == 0 ||
				    guidance->targetObjIdx !=
					    g_paiContext.objectIndex) {
					*lastAttackerObjIdxPtr = UINT16_MAX;
					return 1;
				}
			} else if (
				lastAttackerObjIdx != UINT16_MAX &&
				!pai_IsObjectWithinRangeOfCraft(
					attackerIndex,
					(unsigned int)
						g_aiStillAttackLastAttackerRangeBySkill
							[g_paiContext
								 .skillTier])) {
				g_curCraft->lastAttackerObjIdx = UINT16_MAX;
				return 1;
			}
		}
	}
	return 0;
}

/* Order 4: steers the craft home and returns 1 once it is within 2,048 world
 * units of the outside hangar point of its mothership, else 0. Sets its
 * separation to 1. The mothership is the leader of the captured departure
 * mothership group for a captured craft whose group leaves by one, else of the
 * departure mothership group when departureMethod is set, else of the alternate
 * one when used; one in a player's flight group does not count. With one, it
 * targets it, aims at the outside hangar point of its model and, through
 * paiman_setspeed, slows to speed 150 within 0x10000, 100 within 0x8000 and 75
 * within 0x4000. With none, it targets mission point 13 when enabled, else the
 * group's current point, and returns 0. Sets the trig2_ globals with the
 * mothership. */
// FUNCTION: XVT 0x4667B0
int16_t paiorder_flyhomeorder(void)
{
	uint16_t mothershipObject;
	uint8_t mothershipFlightGroup;
	int modelIndex;

	g_curCraft->aiFlight.separation = 1;
	mothershipObject = UINT16_MAX;
	if (g_curCraft->capturedByFlightGroup != 0) {
		if (g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.capturedDepartViaMothership != 0) {
			mothershipFlightGroup =
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg.capturedDepartureMothership;
			mothershipObject =
				pai_FindMothershipObject(mothershipFlightGroup);
		}
	} else {
		if (g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.departureMethod != 0) {
			mothershipObject = pai_FindMothershipObject(
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg.departureMothership);
		}
		if (mothershipObject == UINT16_MAX &&
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
				    .fg.alternateMothershipUsed != 0) {
			mothershipObject = pai_FindMothershipObject(
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg.alternateMothership);
		}
	}
	if (mothershipObject != UINT16_MAX &&
	    g_missionFlightGroups[g_objectTable[mothershipObject]
					  .flightGroupIdx]
			    .playerOwnerIdx != -1) {
		mothershipObject = UINT16_MAX;
	}
	if (mothershipObject != UINT16_MAX) {
		g_paiContext.controller->targetObjIdx = mothershipObject;
		g_paiContext.controller->targetSignature =
			g_objectTable[mothershipObject].objectSignature;
		g_paiContext.controller->hasLiveTarget = 0;
		modelIndex = g_objectTable[mothershipObject]
				     .mobj->pCraft->modelIndex;
		pai_RotateLocalVectorToWorldScratch(
			&g_objectTable[mothershipObject],
			g_modelDefs[modelIndex].hangarPoints.outside.side,
			g_modelDefs[modelIndex].hangarPoints.outside.up,
			g_modelDefs[modelIndex].hangarPoints.outside.forward);
		g_paiContext.controller->aimPointX =
			g_rotatedX + g_objectTable[mothershipObject].world_x;
		g_paiContext.controller->aimPointY =
			g_rotatedY + g_objectTable[mothershipObject].world_y;
		g_paiContext.controller->aimPointZ =
			g_rotatedZ + g_objectTable[mothershipObject].world_z;
		pai_CalcAnglesToAimPoint();
		if (trig2_polardistance < 0x10000) {
			paiman_setspeed(g_paiContext.objectIndex, 0x96);
		}
		if (trig2_polardistance < 0x8000) {
			paiman_setspeed(g_paiContext.objectIndex, 0x64);
		}
		if (trig2_polardistance < 0x4000) {
			paiman_setspeed(g_paiContext.objectIndex, 0x4B);
		}
		return trig2_polardistance < 2048;
	}

	if (g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
		    .fg.missionPointEnabled[13] != 0) {
		g_paiContext.controller->targetObjIdx = 0x800D;
	} else {
		g_paiContext.controller->targetObjIdx = 0x8000;
	}
	g_paiContext.controller->targetSignature = 0;
	g_paiContext.controller->hasLiveTarget = 0;
	pai_UpdateAimPointFromOrderTarget();
	return 0;
}

/* Order 45: aims the craft at formation slot 0 of flight group variable2 minus
 * 1, as Mission_ResolveFormationSlotWorldLoc places it, plus 932 in Z, and
 * returns 1 once within 2,048 world units, setting waypointIndex to 0; else 0.
 * Returns 0 at once when that group has any arrived outcome. Sets the throttle
 * to 0xC000 within 0x4000 and to 0x6000 within 4,096. Sets the g_worldLoc and
 * trig2_ globals. */
// FUNCTION: XVT 0x46A140
int16_t paiorder_dropoffdestorder(void)
{
	struct MissionOrder *order =
		&g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			 .fg.orders[g_paiContext.orderSlot];
	uint16_t destinationFlightGroup = (uint16_t)(order->variable2 - 1);

	if (g_missionFgStats[destinationFlightGroup]
		    .outcomeCount[FLIGHT_GROUP_OUTCOME_ARRIVED] != 0) {
		return 0;
	}

	Mission_ResolveFormationSlotWorldLoc(destinationFlightGroup, 0,
					     UINT16_MAX);
	g_paiContext.controller->aimPointX = g_worldLocX;
	g_paiContext.controller->aimPointY = g_worldLocY;
	g_paiContext.controller->aimPointZ = g_worldLocZ + 932;
	pai_CalcAnglesToAimPoint();
	if (trig2_polardistance < 0x4000) {
		paiman_setpower(g_paiContext.objectIndex, 0xC000);
	}
	if (trig2_polardistance < 4096) {
		paiman_setpower(g_paiContext.objectIndex, 0x6000);
	}
	if (trig2_polardistance >= 2048) {
		return 0;
	}

	g_paiContext.controller->waypointIndex = 0;
	return 1;
}

/* Order 21: flies into the mothership's hangar and removes the craft there.
 * Sets its separation to 1 and thinkInterval to 29 ticks. The mothership is the
 * leader of the captured departure mothership group for a captured craft, else
 * of the departure mothership group, else of the alternate one when used;
 * departureMethod, capturedDepartViaMothership and a player's ownership are not
 * checked here. With one it targets it, aims at the inside hangar point of its
 * model and sets the speed to the mothership's plus 25, or 40 when the
 * mothership is below 25. Within 1,024 world units of that point (512 for a
 * craft not a starship) it removes every AI-flown craft of its flight group
 * that follows a leader in the follow-leader maneuver, then itself and the
 * object it carries: for each it adds to g_missionFgStats the departure
 * outcomes and team scores its flags call for, records the outcome by
 * mothership kind and frees the object, emitting message 141 for each craft.
 * Returns 0 with a mothership; with none, sets the speed to 35 and returns 1.
 * Sets the trig2_ globals. */
// FUNCTION: XVT 0x466A70
int16_t paiorder_enterhangarorder(void)
{
	uint16_t objectIndex;
	uint16_t mothershipObject;
	uint16_t outcomeId;
	unsigned int team;
	unsigned int otherTeam;
	uint16_t carriedObjectIndex;
	uint16_t carriedGroupIndex;
	struct CraftData *carriedCraft;
	int specialCargo;

	g_curCraft->aiFlight.separation = 1;
	g_paiContext.controller->thinkInterval = 29;
	if (g_curCraft->capturedByFlightGroup != 0) {
		mothershipObject = pai_FindMothershipObject(
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.capturedDepartureMothership);
		outcomeId = FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT;
	} else {
		mothershipObject = pai_FindMothershipObject(
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.departureMothership);
		outcomeId = FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT;
		if (mothershipObject == UINT16_MAX &&
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
				    .fg.alternateMothershipUsed != 0) {
			mothershipObject = pai_FindMothershipObject(
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg.alternateMothership);
			outcomeId =
				FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT;
		}
	}
	if (mothershipObject != UINT16_MAX) {
		uint16_t modelIndex;

		g_paiContext.controller->targetObjIdx = mothershipObject;
		g_paiContext.controller->targetSignature =
			g_objectTable[mothershipObject].objectSignature;
		g_paiContext.controller->hasLiveTarget = 0;
		modelIndex = g_objectTable[mothershipObject]
				     .mobj->pCraft->modelIndex;
		pai_RotateLocalVectorToWorldScratch(
			&g_objectTable[mothershipObject],
			g_modelDefs[modelIndex].hangarPoints.inside.side,
			g_modelDefs[modelIndex].hangarPoints.inside.up,
			g_modelDefs[modelIndex].hangarPoints.inside.forward);
		g_paiContext.controller->aimPointX =
			g_rotatedX + g_objectTable[mothershipObject].world_x;
		g_paiContext.controller->aimPointY =
			g_rotatedY + g_objectTable[mothershipObject].world_y;
		g_paiContext.controller->aimPointZ =
			g_rotatedZ + g_objectTable[mothershipObject].world_z;
	}
	pai_CalcAnglesToAimPoint();
	if (mothershipObject != UINT16_MAX) {
		if (g_objectTable[mothershipObject].mobj->speed >= 25) {
			paiman_setspeed(
				g_paiContext.objectIndex,
				g_objectTable[mothershipObject].mobj->speed +
					25);
		} else {
			paiman_setspeed(g_paiContext.objectIndex, 40);
		}

		if ((g_objectTable[g_paiContext.objectIndex].genusId ==
				     CRAFT_GENUS_STARSHIP
			     ? 1024
			     : 512) > trig2_polardistance) {
			for (objectIndex =
				     (uint16_t)g_activeRegionObjectSlotStart;
			     objectIndex < g_activeRegionCraftObjectSlotEnd;
			     ++objectIndex) {
				int tableIndex;
				struct ObjectRecord *object;
				struct CraftData *otherCraft;
				struct AiController *otherController;

				tableIndex = objectIndex;
				object = &g_objectTable[tableIndex];
				if (object->objectType == 0 ||
				    object->flightGroupIdx !=
					    g_paiContext
						    .craftFlightGroupIndex) {
					continue;
				}
				otherCraft = object->mobj->pCraft;
				otherController = &otherCraft->aiController;
				if (otherCraft->leader_obj_idx == UINT8_MAX ||
				    otherController->maneuverMode !=
					    AI_MANEUVER_MODE_FOLLOW_LEADER ||
				    object->playerOwnerIdx != -1) {
					continue;
				}
				if (otherCraft->capturedByFlightGroup == 0) {
					if (otherController->skippedToOrder4 ==
						    0 &&
					    (otherCraft->aiFlight.goHomeFlag !=
						     0 ||
					     (otherCraft->aiFlight
							      .missionAbortedFlag ==
						      0 &&
					      otherCraft->aiFlight
							      .departTimerFlag ==
						      0))) {
						specialCargo = 0;
						++g_missionFgStats[g_paiContext
									   .craftFlightGroupIndex]
							  .outcomeCount
								  [FLIGHT_GROUP_OUTCOME_DEPARTED];
						if (g_missionFlightGroups
							    [g_paiContext
								     .craftFlightGroupIndex]
								    .fg
								    .specialCargoCraft ==
						    otherCraft->craftOrdinal) {
							g_missionFgStats[g_paiContext
										 .craftFlightGroupIndex]
								.specialCargoOutcome
									[FLIGHT_GROUP_OUTCOME_DEPARTED] =
								1;
							specialCargo = 1;
						}
						Mission_ApplyTeamGoalScoreAllEnabledTeams(
							12,
							g_paiContext
								.craftFlightGroupIndex,
							specialCargo);
					}
					if (otherCraft->capturedByFlightGroup ==
						    0 &&
					    otherController->skippedToOrder4 ==
						    1 &&
					    otherCraft->aiFlight
							    .missionAbortedFlag ==
						    0 &&
					    otherCraft->aiFlight
							    .departTimerFlag ==
						    0) {
						++g_missionFgStats[g_paiContext
									   .craftFlightGroupIndex]
							  .outcomeCount
								  [FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE];
						if (g_missionFlightGroups
							    [g_paiContext
								     .craftFlightGroupIndex]
								    .fg
								    .specialCargoCraft ==
						    otherCraft->craftOrdinal) {
							g_missionFgStats[g_paiContext
										 .craftFlightGroupIndex]
								.specialCargoOutcome
									[FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE] =
								1;
						}
					}
				}
				msg_emitCraftMessage(objectIndex, otherCraft,
						     141);
				Mission_RecordCraftOutcome(
					objectIndex,
					g_paiContext.craftFlightGroupIndex,
					outcomeId);
				g_objectTable[tableIndex].objectType = 0;
				Craft_FreeLinkedObjects(otherCraft);
			}

			if (g_curCraft->capturedByFlightGroup == 0 &&
			    g_paiContext.controller->skippedToOrder4 == 0 &&
			    (g_curCraft->aiFlight.goHomeFlag != 0 ||
			     (g_curCraft->aiFlight.missionAbortedFlag == 0 &&
			      g_curCraft->aiFlight.departTimerFlag == 0))) {
				++g_missionFgStats[g_paiContext
							   .craftFlightGroupIndex]
					  .outcomeCount
						  [FLIGHT_GROUP_OUTCOME_DEPARTED];
				specialCargo = 0;
				if (g_missionFlightGroups
					    [g_paiContext.craftFlightGroupIndex]
						    .fg.specialCargoCraft ==
				    g_curCraft->craftOrdinal) {
					g_missionFgStats[g_paiContext
								 .craftFlightGroupIndex]
						.specialCargoOutcome
							[FLIGHT_GROUP_OUTCOME_DEPARTED] =
						1;
					specialCargo = 1;
				}
				Mission_ApplyTeamGoalScoreAllEnabledTeams(
					12, g_paiContext.craftFlightGroupIndex,
					specialCargo);
			}
			if (g_curCraft->capturedByFlightGroup == 0 &&
			    g_paiContext.controller->skippedToOrder4 == 1 &&
			    g_curCraft->aiFlight.missionAbortedFlag == 0 &&
			    g_curCraft->aiFlight.departTimerFlag == 0) {
				++g_missionFgStats[g_paiContext
							   .craftFlightGroupIndex]
					  .outcomeCount
						  [FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE];
				if (g_missionFlightGroups
					    [g_paiContext.craftFlightGroupIndex]
						    .fg.specialCargoCraft ==
				    g_curCraft->craftOrdinal) {
					g_missionFgStats[g_paiContext
								 .craftFlightGroupIndex]
						.specialCargoOutcome
							[FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE] =
						1;
				}
			}
			if (g_curCraft->capturedByFlightGroup != 0) {
				team = g_objectTable[g_paiContext.objectIndex]
					       .mobj->team;
				++g_missionFgStats
					  [g_paiContext.craftFlightGroupIndex]
						  .teamCapturedDepartedCount
							  [team];
				specialCargo = 0;
				if (g_missionFlightGroups
					    [g_paiContext.craftFlightGroupIndex]
						    .fg.specialCargoCraft ==
				    g_curCraft->craftOrdinal) {
					++g_missionFgStats
						  [g_paiContext
							   .craftFlightGroupIndex]
							  .teamSpecialCargoCapturedDeparted
								  [team];
					specialCargo = 1;
				}
				Mission_ApplyTeamGoalScoreForTeam(
					44, g_paiContext.craftFlightGroupIndex,
					specialCargo, (uint8_t)team);
				for (otherTeam = 0; otherTeam < 10;
				     ++otherTeam) {
					if (otherTeam != team &&
					    g_missionFlightGroups
							    [g_paiContext
								     .craftFlightGroupIndex]
								    .fg.team !=
						    otherTeam) {
						++g_missionFgStats[g_paiContext
									   .craftFlightGroupIndex]
							  .teamUncapturedLost
								  [otherTeam];
						if (g_missionFlightGroups
							    [g_paiContext
								     .craftFlightGroupIndex]
								    .fg
								    .specialCargoCraft ==
						    g_curCraft->craftOrdinal) {
							g_missionFgStats[g_paiContext
										 .craftFlightGroupIndex]
								.teamSpecialCargoUncapturedLost
									[otherTeam] =
								1;
						}
					}
				}
			}
			msg_emitCraftMessage(g_paiContext.objectIndex,
					     g_curCraft, 141);
			Mission_RecordCraftOutcome(
				g_paiContext.objectIndex,
				g_paiContext.craftFlightGroupIndex, outcomeId);
			g_objectTable[g_paiContext.objectIndex].objectType = 0;
			Craft_FreeLinkedObjects(g_curCraft);

			carriedObjectIndex = g_curCraft->carriedObjectIndex;
			if (carriedObjectIndex != UINT16_MAX &&
			    g_objectTable[carriedObjectIndex].mobj != NULL) {
				carriedGroupIndex =
					g_objectTable[carriedObjectIndex]
						.flightGroupIdx;
				Mission_RecordCraftOutcome(carriedObjectIndex,
							   carriedGroupIndex,
							   outcomeId);
				carriedCraft = g_objectTable[carriedObjectIndex]
						       .mobj->pCraft;
				if (carriedCraft->capturedByFlightGroup != 0) {
					team = g_objectTable[carriedObjectIndex]
						       .mobj->team;
					++g_missionFgStats[carriedGroupIndex]
						  .teamCapturedDepartedCount
							  [team];
					specialCargo = 0;
					if (g_missionFlightGroups
						    [carriedGroupIndex]
							    .fg
							    .specialCargoCraft ==
					    carriedCraft->craftOrdinal) {
						++g_missionFgStats[carriedGroupIndex]
							  .teamSpecialCargoCapturedDeparted
								  [team];
						specialCargo = 1;
					}
					Mission_ApplyTeamGoalScoreForTeam(
						44, carriedGroupIndex,
						specialCargo, (uint8_t)team);
					for (otherTeam = 0; otherTeam < 10;
					     ++otherTeam) {
						if (otherTeam != team &&
						    g_missionFlightGroups
								    [carriedGroupIndex]
									    .fg
									    .team !=
							    otherTeam) {
							++g_missionFgStats[carriedGroupIndex]
								  .teamUncapturedLost
									  [otherTeam];
							if (g_missionFlightGroups
								    [carriedGroupIndex]
									    .fg
									    .specialCargoCraft ==
							    carriedCraft
								    ->craftOrdinal) {
								g_missionFgStats[carriedGroupIndex]
									.teamSpecialCargoUncapturedLost
										[otherTeam] =
									1;
							}
						}
					}
					++g_missionFgStats[carriedGroupIndex].outcomeCount
						  [FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION];
					if (g_missionFlightGroups
						    [carriedGroupIndex]
							    .fg
							    .specialCargoCraft ==
					    carriedCraft->craftOrdinal) {
						g_missionFgStats[carriedGroupIndex]
							.specialCargoOutcome
								[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION] =
							1;
					}
				} else {
					++g_missionFgStats[carriedGroupIndex].outcomeCount
						  [FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION];
					specialCargo = 0;
					if (g_missionFlightGroups
						    [carriedGroupIndex]
							    .fg
							    .specialCargoCraft ==
					    carriedCraft->craftOrdinal) {
						g_missionFgStats[carriedGroupIndex]
							.specialCargoOutcome
								[FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION] =
							1;
						specialCargo = 1;
					}
					Mission_ApplyTeamGoalScoreAllEnabledTeams(
						46, carriedGroupIndex,
						specialCargo);
				}
				g_objectTable[carriedObjectIndex].objectType =
					0;
				Craft_FreeLinkedObjects(carriedCraft);
			}
		}
		return 0;
	}
	paiman_setspeed(g_paiContext.objectIndex, 35);
	return 1;
}

/* Order 10: returns 1 when the craft is on its plan's maneuver and its target
 * is closer, by rough distance, than its effective skill value plus 0x20000
 * world units; else 0. Does not check that targetObjIdx names an object. Sets
 * g_lastRoughDistance. */
// FUNCTION: XVT 0x467320
int16_t paiorder_waitrunorder(void)
{
	uint16_t effectiveSkill;

	if (g_paiContext.controller->maneuverMode ==
	    g_paiContext.initialManeuverId) {
		effectiveSkill = pai_GetEffectiveSkillValue(g_curCraft);
		if (pai_IsObjectWithinRangeOfCraft(
			    g_paiContext.controller->targetObjIdx,
			    (unsigned int)effectiveSkill + 0x20000) == 1) {
			return 1;
		}
	}
	return 0;
}

/* Order 11: returns 1 and drops the target (target 0xFFFF, signature 0, no live
 * target, candidate 0xFFFF) when the player told the craft to avoid it, it
 * cannot be targeted or its slot holds another object now. Next, on
 * disableldr1pln, it returns 1 when the target craft has no working subsystems,
 * clearing only the candidate. Then it drops the target and returns 1 when it
 * is a player's craft with its decoy beam on farther than 0x4000, or, in a
 * version 14 mission, has no working subsystems and is neither the candidate
 * nor a target of the current order. Else 0. Sets g_lastRoughDistance on the
 * decoy test. */
// FUNCTION: XVT 0x467380
int16_t paiorder_breakofforder(void)
{
	uint16_t targetObjIdx = g_paiContext.controller->targetObjIdx;
	int targetValid;
	uint16_t targetWorkingSubsystems;

	if (g_curCraft->playerCommandAvoidTargetObjIdx == targetObjIdx) {
		g_paiContext.controller->targetObjIdx = UINT16_MAX;
		g_paiContext.controller->targetSignature = 0;
		g_paiContext.controller->hasLiveTarget = 0;
		g_paiContext.controller->candidateTargetIdx = UINT16_MAX;
		return 1;
	}

	targetValid = pai_IsObjectTargetable(targetObjIdx);

	if (targetValid == 0) {
		g_paiContext.controller->targetObjIdx = UINT16_MAX;
		g_paiContext.controller->targetSignature = 0;
		g_paiContext.controller->hasLiveTarget = 0;
		g_paiContext.controller->candidateTargetIdx = UINT16_MAX;
		return 1;
	}

	if (g_paiContext.controller->targetSignature !=
	    g_objectTable[targetObjIdx].objectSignature) {
		g_paiContext.controller->targetObjIdx = UINT16_MAX;
		g_paiContext.controller->targetSignature = 0;
		g_paiContext.controller->hasLiveTarget = 0;
		g_paiContext.controller->candidateTargetIdx = UINT16_MAX;
		return 1;
	}

	if (strcmp(g_planTable[g_paiContext.controller->currentPlanId].name,
		   "disableldr1pln") == 0) {
		if (g_activeRegionCraftObjectSlotEnd <= (int)targetObjIdx) {
		} else if (g_objectTable[targetObjIdx]
				   .mobj->pCraft->workingSubsystems == 0) {
			g_paiContext.controller->candidateTargetIdx =
				UINT16_MAX;
			return 1;
		}
	}

	if (g_objectTable[targetObjIdx].playerOwnerIdx != -1) {
		if (g_activeRegionCraftObjectSlotEnd <= (int)targetObjIdx) {
		} else if (Object_HasActiveDecoyBeam(targetObjIdx) == 1) {
			pai_ObjectRefUpdateRoughDistance(
				g_paiContext.objectIndex, targetObjIdx);
			if (g_lastRoughDistance > 0x4000) {
				g_paiContext.controller->targetObjIdx =
					UINT16_MAX;
				g_paiContext.controller->targetSignature = 0;
				g_paiContext.controller->hasLiveTarget = 0;
				g_paiContext.controller->candidateTargetIdx =
					UINT16_MAX;
				return 1;
			}
		}
	}

	if (g_missionFileVersion == 14) {
		targetWorkingSubsystems =
			g_objectTable[targetObjIdx].typeSpecificWord;
		if (g_objectTable[targetObjIdx].mobj != NULL &&
		    g_objectTable[targetObjIdx].mobj->pCraft != NULL) {
			targetWorkingSubsystems =
				g_objectTable[targetObjIdx]
					.mobj->pCraft->workingSubsystems;
		}
		if (targetWorkingSubsystems == 0 &&
		    g_paiContext.controller->candidateTargetIdx !=
			    targetObjIdx &&
		    pai_CurrentOrderTargetsMatchObject(targetObjIdx) == 0) {
			g_paiContext.controller->targetObjIdx = UINT16_MAX;
			g_paiContext.controller->targetSignature = 0;
			g_paiContext.controller->hasLiveTarget = 0;
			g_paiContext.controller->candidateTargetIdx =
				UINT16_MAX;
			return 1;
		}
	}
	return 0;
}

/* Order 15: returns 1 when the flight group's abort trigger holds for the
 * craft, else 0; 0 at once when maxSpeedCache is 0. Triggers 1 to 9: shields
 * out (shield system down, or both banks empty on a freighter or starship),
 * cannons down, warheads out (launcher down or no rounds), hull damage reaching
 * half of hullMax, attacked by any team, shields down to half or to a quarter
 * of the maximum, and hull damage reaching a quarter or three quarters. Trigger
 * 2 tests the cannons but reports IFMSG_394_WARHEADS_OUT. The first time it
 * aborts it counts the aborted outcome, undoes a not-departed count and clears
 * departTimerFlag, has the tactical officer announce the withdrawal, sends
 * message 387 to the player who owns the flight group, and restores working
 * subsystems when the current order's leader plan is waitforboardpln and
 * subsystemDamage is 0. Sets missionAbortedFlag each time it aborts. */
// FUNCTION: XVT 0x467710
int16_t paiorder_abortmissionorder(void)
{
	int abortReasonMessage;
	int abortMission;
	uint16_t launcherIndex;
	uint8_t launcherCount;
	uint16_t weaponSlotIndex;
	uint16_t lastWeaponSlot;
	int16_t hasWarheads;

	if (g_curCraft->aiFlight.maxSpeedCache == 0) {
		return 0;
	}

	abortMission = 0;
	switch (g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.abortTrigger) {
	case 1:
		if ((g_curCraft->workingSubsystems &
		     CRAFT_SUBSYSTEM_FLAG_SHIELDS) == 0) {
			abortMission = 1;
		}
		if ((g_objectTable[g_paiContext.objectIndex].genusId == 3 ||
		     g_objectTable[g_paiContext.objectIndex].genusId == 4) &&
		    g_curCraft->shieldEnergy[0] + g_curCraft->shieldEnergy[1] ==
			    0) {
			abortMission = 1;
		}
		abortReasonMessage = IFMSG_390_SHIELDS_OUT;
		break;

	case 2:
		if ((g_curCraft->workingSubsystems &
		     CRAFT_SUBSYSTEM_FLAG_CANNONS) == 0) {
			abortMission = 1;
		}
		abortReasonMessage = IFMSG_394_WARHEADS_OUT;
		break;

	case 3:
		if ((g_curCraft->workingSubsystems &
		     CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER) == 0) {
			abortMission = 1;
		}
		hasWarheads = 0;
		launcherIndex = 0;
		launcherCount = g_curCraft->warheadLauncherCount;
		while (launcherIndex < launcherCount) {
			if (g_curCraft->warheadSlotTypeIds[launcherIndex] !=
			    0) {
				lastWeaponSlot =
					g_modelDefs[g_curCraft->modelIndex]
						.warheadLauncherLastSlot
							[launcherIndex];
				weaponSlotIndex =
					g_modelDefs[g_curCraft->modelIndex]
						.warheadLauncherFirstSlot
							[launcherIndex];
				while (!(lastWeaponSlot < weaponSlotIndex)) {
					if (g_curCraft
						    ->weaponSlots
							    [weaponSlotIndex]
						    .ammoCount != 0) {
						hasWarheads = 1;
					}
					++weaponSlotIndex;
				}
			}
			++launcherIndex;
		}
		if (hasWarheads == 0) {
			abortMission = 1;
		}
		abortReasonMessage = IFMSG_394_WARHEADS_OUT;
		break;

	case 4:
		if (MATH2_longfraction(g_curCraft->hullMax, 0x8000) <=
		    g_curCraft->hullDamage) {
			abortMission = 1;
		}
		abortReasonMessage = IFMSG_392_HULL_AT_50;
		break;

	case 5:
		/* The launcher counter is reused here as a team index over the ten attackedByTeam entries. */
		for (launcherIndex = 0; launcherIndex < 10; ++launcherIndex) {
			if (g_curCraft->attackedByTeam[launcherIndex] != 0) {
				abortMission = 1;
			}
		}
		abortReasonMessage = IFMSG_395_UNDER_ATTACK;
		break;

	case 6:
		if ((unsigned int)(g_curCraft->shieldEnergy[0] +
				   g_curCraft->shieldEnergy[1]) <=
		    (uint16_t)MATH2_fraction((uint16_t)Craft_GetObjectMaxShield(
						     g_paiContext.objectIndex),
					     0x8000)) {
			abortMission = 1;
		}
		abortReasonMessage = IFMSG_388_SHIELDS_AT_50;
		break;

	case 7:
		if ((unsigned int)(g_curCraft->shieldEnergy[0] +
				   g_curCraft->shieldEnergy[1]) <=
		    (uint16_t)MATH2_fraction((uint16_t)Craft_GetObjectMaxShield(
						     g_paiContext.objectIndex),
					     0x4000)) {
			abortMission = 1;
		}
		abortReasonMessage = IFMSG_389_SHIELDS_AT_25;
		break;

	case 8:
		if (MATH2_longfraction(g_curCraft->hullMax, 0x4000) <=
		    g_curCraft->hullDamage) {
			abortMission = 1;
		}
		abortReasonMessage = IFMSG_391_HULL_AT_75;
		break;

	case 9:
		if (MATH2_longfraction(g_curCraft->hullMax, 0xC000) <=
		    g_curCraft->hullDamage) {
			abortMission = 1;
		}
		abortReasonMessage = IFMSG_393_HULL_AT_25;
		break;
	}

	if (abortMission != 0) {
		if (g_curCraft->aiFlight.missionAbortedFlag == 0) {
			++g_missionFgStats[g_paiContext.craftFlightGroupIndex]
				  .outcomeCount[FLIGHT_GROUP_OUTCOME_ABORTED];
			if (g_curCraft->craftOrdinal ==
			    g_missionFlightGroups
				    [g_paiContext.craftFlightGroupIndex]
					    .fg.specialCargoCraft) {
				g_missionFgStats[g_paiContext
							 .craftFlightGroupIndex]
					.specialCargoOutcome
						[FLIGHT_GROUP_OUTCOME_ABORTED] =
					1;
			}
			if (g_curCraft->aiFlight.departTimerFlag != 0) {
				--g_missionFgStats[g_paiContext
							   .craftFlightGroupIndex]
					  .outcomeCount
						  [FLIGHT_GROUP_OUTCOME_NOT_DEPARTED];
				if (g_curCraft->craftOrdinal ==
				    g_missionFlightGroups
					    [g_paiContext.craftFlightGroupIndex]
						    .fg.specialCargoCraft) {
					g_missionFgStats[g_paiContext
								 .craftFlightGroupIndex]
						.specialCargoOutcome
							[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] =
						0;
				}
				g_curCraft->aiFlight.departTimerFlag = 0;
			}

			fsfx_SpeakTacticalOfficerEvent(
				TACTICAL_VOICE_STATUS, TACTICAL_MSG_WITHDRAWING,
				g_paiContext.objectIndex, UINT16_MAX);
			if (g_missionFlightGroups
				    [g_paiContext.craftFlightGroupIndex]
					    .playerOwnerIdx != -1) {
				g_msgSenderIff =
					(uint8_t)g_objectTable
						[g_paiContext.objectIndex]
							.mobj->iff;
				msg_addMessagePtr(
					0,
					g_missionFlightGroups
						[g_paiContext
							 .craftFlightGroupIndex]
							.fg.name);
				g_msgArgTable[1] = (uint16_t)
					Hud_MissionFG_GetCraftNumberIfShown(
						g_paiContext
							.craftFlightGroupIndex,
						g_curCraft);
				g_msgArgTable[2] = (uint16_t)abortReasonMessage;
				msg_emitInFlightMessage(
					IFMSG_387_WINGMAN_ARG_ARG_ABORTING_MISSION_ARG,
					g_missionFlightGroups
						[g_paiContext
							 .craftFlightGroupIndex]
							.playerOwnerIdx);
			}

			if (strcmp(g_planTable
					   [g_builtinPlanIdByNameIndex
						    [g_orderLeaderBuiltinPlanNameIndex
							     [g_missionFlightGroups[g_paiContext
											    .craftFlightGroupIndex]
								      .fg
								      .orders[g_paiContext
										      .orderSlot]
								      .order]]]
						   .name,
				   "waitforboardpln") == 0 &&
			    g_curCraft->subsystemDamage == 0) {
				g_curCraft->workingSubsystems =
					g_curCraft->systemFlags;
			}
		}
		g_curCraft->aiFlight.missionAbortedFlag = 1;
	}

	return (int16_t)abortMission;
}

/* Order 12: returns 1 and makes this craft the flight group's leader when its
 * leader is gone: an empty slot, in another flight group, breaking up or
 * exploding, aborted, or flown by a player. This craft then has no leader and
 * takes the old leader's separation, waypointIndex and target, with its aim
 * point (in the modern build only when there is a target); every other craft of
 * the group in the active region's craft slots gets this craft as leader.
 * Returns 0 for a craft with no leader or a leader index at or past the end of
 * those slots. With the leader in place it returns 0, first setting
 * thinkInterval to 59 ticks when the leader runs enterhangarpln. */
// FUNCTION: XVT 0x467C50
int16_t paiorder_leaderdeadorder(void)
{
	uint8_t leaderObjectIndex;
	struct ObjectRecord *leaderObject;
	struct CraftData *leaderCraft;
	struct AiController *leaderController;
	uint8_t leaderInvalid;
	uint16_t objectIndex;
	struct ObjectRecord *object;
	struct CraftData *craft;
	struct AiController *controller;

	leaderObjectIndex = g_curCraft->leader_obj_idx;
	if (leaderObjectIndex == UINT8_MAX) {
		return 0;
	}
	if (g_activeRegionCraftObjectSlotEnd <= (int)leaderObjectIndex) {
		return 0;
	}
	leaderObject = &g_objectTable[leaderObjectIndex];
	leaderCraft = leaderObject->mobj->pCraft;
	leaderController = &leaderCraft->aiController;
	leaderInvalid = 0;
	if (leaderObject->objectType == 0) {
		leaderInvalid = 1;
	}
	if (g_objectTable[g_paiContext.objectIndex].flightGroupIdx !=
	    leaderObject->flightGroupIdx) {
		leaderInvalid = 1;
	}
	if (leaderCraft->objectKind == CRAFT_OBJECT_KIND_BREAKING_UP ||
	    leaderCraft->objectKind == CRAFT_OBJECT_KIND_EXPLODING) {
		leaderInvalid = 1;
	}
	if (leaderCraft->aiFlight.missionAbortedFlag != 0) {
		leaderInvalid = 1;
	}
	if (leaderObject->playerOwnerIdx != -1) {
		leaderInvalid = 1;
	}
	if (leaderInvalid != 0) {
		for (objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
		     objectIndex < (int)g_activeRegionCraftObjectSlotEnd;
		     ++objectIndex) {
			object = &g_objectTable[objectIndex];
			craft = object->mobj->pCraft;
			if (object->objectType != 0 &&
			    object->flightGroupIdx ==
				    g_paiContext.craftFlightGroupIndex) {
				controller = &craft->aiController;
				if (g_paiContext.objectIndex == objectIndex) {
					craft->leader_obj_idx = UINT8_MAX;
					craft->aiFlight.separation =
						leaderCraft->aiFlight
							.separation;
					controller->waypointIndex =
						leaderController->waypointIndex;
					controller->targetObjIdx =
						leaderController->targetObjIdx;
					controller->targetSignature =
						leaderController
							->targetSignature;
					controller->hasLiveTarget =
						leaderController->hasLiveTarget;
#ifdef XVT_MODERN
					/* A leader without a target has no aim point to resolve. */
					if (controller->targetObjIdx !=
					    UINT16_MAX)
#endif
						pai_UpdateAimPointFromOrderTarget();
				} else {
					craft->leader_obj_idx =
						(uint8_t)g_paiContext
							.objectIndex;
				}
			}
		}
	} else if (strcmp(g_planTable[g_paiContext.leaderOrSelfCraft
					      ->aiController.runningPlanId]
				  .name,
			  "enterhangarpln") == 0) {
		g_paiContext.controller->thinkInterval = 59;
	}
	return leaderInvalid;
}

/* Order 16: when the craft is on its plan's maneuver and its recorded attacker
 * is in bearing class 2, switches at random to turn inside, zoom, scissors or
 * dive and runs paiman_initmaneuver. Returns 0 on every path. */
// FUNCTION: XVT 0x467E20
int16_t paiorder_ontailorder(void)
{
	uint16_t objectIndex;
	int16_t randomManeuver;
	uint8_t maneuverMode;

	objectIndex = g_paiContext.objectIndex;
	if (g_paiContext.controller->maneuverMode ==
		    g_paiContext.initialManeuverId &&
	    g_curCraft->lastAttackerObjIdx != UINT16_MAX) {
		pai_ObjectRefDirectionToObjectRef(
			objectIndex, g_curCraft->lastAttackerObjIdx);
		if (g_aiThreatBearingClassByOctant
			    [(uint16_t)(trig2_xyangle -
					g_objectTable[objectIndex].yaw) >>
			     13] == 2) {
			randomManeuver = GameRand() & 3;
			if (randomManeuver == 0) {
				maneuverMode = AI_MANEUVER_MODE_TURN_INSIDE;
			} else if (randomManeuver == 1) {
				maneuverMode = AI_MANEUVER_MODE_ZOOM;
			} else if (randomManeuver == 2) {
				maneuverMode = AI_MANEUVER_MODE_SCISSORS;
			} else {
				maneuverMode = AI_MANEUVER_MODE_DIVE;
			}
			g_paiContext.controller->maneuverMode = maneuverMode;
			paiman_initmaneuver();
		}
	}
	return 0;
}

/* Order 17: returns 1. */
// FUNCTION: XVT 0x467EE0
int16_t paiorder_alwaysorder(void) { return 1; }

/* Order 19: returns 1 when the leader, or the craft itself when it has none,
 * runs flyhomepln or flyhomeevadepln; else 0. */
// FUNCTION: XVT 0x467EF0
int16_t paiorder_leadergohomeorder(void)
{
	struct AiController *leaderController =
		&g_paiContext.leaderOrSelfCraft->aiController;

	return strcmp(g_planTable[leaderController->runningPlanId].name,
		      "flyhomepln") == 0 ||
	       strcmp(g_planTable[leaderController->runningPlanId].name,
		      "flyhomeevadepln") == 0;
}

/* Order 20. A follower that has not aborted and is not on flyhomeevadepln jumps
 * with its leader: when its model has a hyperdrive, its group leaves by
 * hyperspace (departureMethod 0, or for a captured craft no departure by
 * mothership) and its leader is entering hyperspace, it switches straight to
 * intohyperspacepln with the into-hyperspace maneuver at maneuverPhase 1, a
 * maneuver timer of 2,360 ticks and a second one of 944, clears its roll, pitch
 * and turn states and push, sets its objectKind to entering hyperspace and sets
 * full power. Such a follower returns 0 either way. Any other craft returns 1
 * when its model has a hyperdrive and its group leaves by hyperspace; else
 * 0. */
// FUNCTION: XVT 0x467F60
int16_t paiorder_hyperspaceorder(void)
{
	int16_t canEnterHyperspace;

	if (g_curCraft->leader_obj_idx != UINT8_MAX &&
	    g_curCraft->aiFlight.missionAbortedFlag == 0 &&
	    strcmp(g_planTable[g_paiContext.controller->runningPlanId].name,
		   "flyhomeevadepln") != 0) {
		canEnterHyperspace = 0;
		if (g_modelDefs[g_curCraft->modelIndex].hasHyperdrive != 0) {
			if (g_curCraft->capturedByFlightGroup == 0) {
				if (g_missionFlightGroups
					    [g_paiContext.craftFlightGroupIndex]
						    .fg.departureMethod == 0) {
					canEnterHyperspace = 1;
				}
			} else if (
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.capturedDepartViaMothership ==
				0) {
				canEnterHyperspace = 1;
			}
		}
		if (canEnterHyperspace != 0 &&
		    g_paiContext.leaderOrSelfCraft->objectKind ==
			    CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE) {
			g_paiContext.controller->runningPlanId =
				(uint8_t)pai_FindPlanIdByNameOrZero(
					"intohyperspacepln");
			g_curCraft->objectKind =
				CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE;
			g_curCraft->aiFlight.rollState = 0;
			g_curCraft->aiFlight.pitchState = 0;
			g_curCraft->aiFlight.turnState = 0;
			g_paiContext.controller->maneuverMode =
				AI_MANEUVER_MODE_INTO_HYPERSPACE;
			g_curCraft->pushAccumX = 0;
			g_curCraft->pushAccumY = g_curCraft->pushAccumX;
			g_curCraft->pushAccumZ = g_curCraft->pushAccumY;
			g_paiContext.controller->maneuverPhase = 1;
			g_paiContext.controller->secondaryManeuverTimer = 944;
			g_paiContext.controller->maneuverTimer = 2360;
			paiman_setpower(g_paiContext.objectIndex, UINT16_MAX);
		}
	} else if (g_modelDefs[g_curCraft->modelIndex].hasHyperdrive != 0) {
		if (g_curCraft->capturedByFlightGroup == 0) {
			if (g_missionFlightGroups
				    [g_paiContext.craftFlightGroupIndex]
					    .fg.departureMethod == 0) {
				return 1;
			}
		} else if (g_missionFlightGroups[g_paiContext
							 .craftFlightGroupIndex]
				   .fg.capturedDepartViaMothership == 0) {
			return 1;
		}
	}
	return 0;
}

/* Order 22: returns 1 when the craft's target, or its leader's when it has one,
 * is mission point 13 (0x800D); else 0, and always 0 for a platform. */
// FUNCTION: XVT 0x468180
int16_t paiorder_mothershiporder(void)
{
	if (g_objectTable[g_paiContext.objectIndex].genusId ==
	    CRAFT_GENUS_PLATFORM) {
		return 0;
	}
	if (g_curCraft->leader_obj_idx == UINT8_MAX) {
		return g_paiContext.controller->targetObjIdx == 0x800Du;
	}
	return g_paiContext.leaderOrSelfCraft->aiController.targetObjIdx ==
	       0x800Du;
}

/* Order 24: returns 1 after setting a boarding target, with its signature and
 * hasLiveTarget 1: the candidate target when there is one (not 0xFFFF or
 * AI_TARGET_ABORT) and it can be targeted, else what
 * pai_FindBoardingTargetFromOrder finds for the current order slot. Clears a
 * candidate that cannot be targeted. Returns 0 when there is none. */
// FUNCTION: XVT 0x4681E0
int16_t paiorder_lookforcrafttoboardorder(void)
{
	uint16_t candidateTargetIdx;

	candidateTargetIdx = g_paiContext.controller->candidateTargetIdx;
	if (candidateTargetIdx != UINT16_MAX &&
	    candidateTargetIdx != AI_TARGET_ABORT) {
		if (pai_IsObjectTargetable(candidateTargetIdx) != 0) {
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
	candidateTargetIdx = (uint16_t)pai_FindBoardingTargetFromOrder(
		g_paiContext.orderSlot);
	if (candidateTargetIdx != UINT16_MAX) {
		g_paiContext.controller->targetObjIdx = candidateTargetIdx;
		g_paiContext.controller->targetSignature =
			g_objectTable[candidateTargetIdx].objectSignature;
		g_paiContext.controller->hasLiveTarget = 1;
		return 1;
	}
	return 0;
}

/* Order 25: returns 1 and gives up boarding when, with maneuverPhase below 3,
 * the target's slot is empty, its mobile object family is 5, its signature
 * changed or the craft has no working subsystems, or, at maneuverPhase 1 or 2,
 * a player flies the target and it moves. Giving up clears the push, targets
 * the group's current mission point and sets the aim point there. Else 0. Does
 * not check targetObjIdx for 0xFFFF. */
// FUNCTION: XVT 0x4683F0
int16_t paiorder_abortboardorder(void)
{
	int16_t shouldAbort;
	uint8_t maneuverPhase;
	uint16_t targetObjIdx;
	struct ObjectRecord *target;

	shouldAbort = 0;
	maneuverPhase = g_paiContext.controller->maneuverPhase;
	if (maneuverPhase < 3) {
		targetObjIdx = g_paiContext.controller->targetObjIdx;
		target = &g_objectTable[targetObjIdx];
		if (target->mobj != NULL) {
			if (target->objectType == 0) {
				shouldAbort = 1;
			}
			if (target->mobj->family == 5) {
				shouldAbort = 1;
			}
		} else if (target->objectType == 0) {
			shouldAbort = 1;
		}
		if (g_curCraft->workingSubsystems == 0) {
			shouldAbort = 1;
		}
		if (g_paiContext.controller->targetSignature !=
		    target->objectSignature) {
			shouldAbort = 1;
		}
	}

	if (maneuverPhase == 1 || maneuverPhase == 2) {
		target = &g_objectTable[targetObjIdx];
		if (target->playerOwnerIdx != -1 && target->mobj->speed != 0) {
			shouldAbort = 1;
		}
	}

	if (shouldAbort) {
		g_curCraft->pushAccumZ = 0;
		g_curCraft->pushAccumY = g_curCraft->pushAccumZ;
		g_curCraft->pushAccumX = g_curCraft->pushAccumY;
		g_paiContext.controller->targetObjIdx = 0x8000;
		g_paiContext.controller->targetSignature = 0;
		g_paiContext.controller->hasLiveTarget = 0;
		pai_UpdateAimPointFromOrderTarget();
		return 1;
	}
	return 0;
}

/* Order 26: returns 1 when the craft's live position is within 0x4000 world
 * units of its aim point, else 0. Sets the trig2_ globals. */
// FUNCTION: XVT 0x468520
int16_t paiorder_returnboardorder(void)
{
	pai_CalcAnglesToAimPoint();
	return trig2_polardistance < 0x4000;
}

/* Order 27: counts a boarding. When boardingState is 2 or 3 it adds 1 to the
 * current order slot's goalProgress; once timesBoarded reaches the order's
 * variable1 it restores the working subsystems and clears subsystemDamage,
 * emitting "has been repaired" when they are equal on disabledpln; while
 * timesBoarded is below variable1 it sets boardingState to 0 instead. Returns 0
 * on every path. */
// FUNCTION: XVT 0x468540
int16_t paiorder_awaitboardorder(void)
{
	uint8_t boardingState;

	boardingState = g_curCraft->boardingState;
	if (boardingState == 2 || boardingState == 3) {
		++g_paiContext.controller->orderProgress
			  .goalProgress[g_paiContext.orderSlot];
		if (g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[g_paiContext.orderSlot]
			    .variable1 <= g_curCraft->aiFlight.timesBoarded) {
			g_curCraft->workingSubsystems = g_curCraft->systemFlags;
			g_curCraft->subsystemDamage = 0;
			if (g_missionFlightGroups
					    [g_paiContext.craftFlightGroupIndex]
						    .fg
						    .orders[g_paiContext
								    .orderSlot]
						    .variable1 ==
				    g_curCraft->aiFlight.timesBoarded &&
			    strcmp(g_planTable[g_paiContext.controller
						       ->currentPlanId]
					   .name,
				   "disabledpln") == 0) {
				msg_emitCraftMessage(
					g_paiContext.objectIndex, g_curCraft,
					IFMSG_138_HAS_BEEN_REPAIRED);
				return 0;
			}
		} else {
			g_curCraft->boardingState = 0;
		}
	}
	return 0;
}

/* Order 28: disables the craft, setting workingSubsystems to 0, and returns
 * 0. */
// FUNCTION: XVT 0x468670
int16_t paiorder_makedisabledorder(void)
{
	g_curCraft->workingSubsystems = 0;
	return 0;
}

/* Order 29: returns 1 when the craft's live position is within 0x4000 world
 * units of its aim point, else 0. Sets the trig2_ globals. */
// FUNCTION: XVT 0x468690
int16_t paiorder_neartargetorder(void)
{
	pai_CalcAnglesToAimPoint();
	return trig2_polardistance < 0x4000;
}

/* Order 30: returns 1 when a warhead launcher of the craft holds a warhead of
 * the class its target calls for and has a round left, else 0. A freighter,
 * starship or platform target in the craft slots calls for class 2, and so does
 * a transport in a version 14 mission; any other target calls for class 1. In a
 * version 14 mission the magnetic pulse and type 153 also count as class 2.
 * Does not check for an empty launcher's type 0. */
// FUNCTION: XVT 0x4686B0
int16_t paiorder_rocketsonboardorder(void)
{
	unsigned int targetObjIdx;
	int16_t targetGenus;
	uint16_t requiredWarheadClass;
	uint16_t launcherIndex;
	uint8_t projectileType;
	uint16_t matchesRequiredClass;
	uint16_t weaponSlot;
	uint16_t lastWeaponSlot;

	targetObjIdx = g_paiContext.controller->targetObjIdx;
	if (g_activeRegionCraftObjectSlotEnd > (int)targetObjIdx) {
		targetGenus = g_objectTable[targetObjIdx].genusId;
		if (targetGenus == 3 || targetGenus == 5 || targetGenus == 4 ||
		    (targetGenus == 1 && g_missionFileVersion == 14)) {
			requiredWarheadClass = 2;
		} else {
			requiredWarheadClass = 1;
		}
	} else {
		requiredWarheadClass = 1;
	}

	for (launcherIndex = 0;
	     launcherIndex < g_curCraft->warheadLauncherCount;
	     ++launcherIndex) {
		projectileType = g_curCraft->warheadSlotTypeIds[launcherIndex];
		if (g_projectileTypeData
			    .warheadClass[projectileType -
					  PROJECTILE_OBJECT_TYPE_FIRST] !=
		    requiredWarheadClass) {
			if (g_missionFileVersion == 14 &&
			    (projectileType ==
				     WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE ||
			     projectileType == 153)) {
				matchesRequiredClass = 1;
				if (requiredWarheadClass != 2) {
					matchesRequiredClass = 0;
				}
			} else {
				matchesRequiredClass = 0;
			}
			if (!matchesRequiredClass) {
				continue;
			}
		}
		lastWeaponSlot =
			g_modelDefs[g_curCraft->modelIndex]
				.warheadLauncherLastSlot[launcherIndex];
		weaponSlot = g_modelDefs[g_curCraft->modelIndex]
				     .warheadLauncherFirstSlot[launcherIndex];
		while (weaponSlot <= lastWeaponSlot) {
			if (g_curCraft->weaponSlots[weaponSlot].ammoCount !=
			    0) {
				return 1;
			}
			++weaponSlot;
		}
	}

	return 0;
}

/* Order 31: for a craft not a freighter or starship, still on its plan's
 * maneuver, dodges threats; returns 0 on every path. With no attacker known it
 * looks for a homing warhead aimed at it within g_aiWarheadThreatRangeBySkill
 * (three times that for a concussion missile or type 149). For one it records
 * it as lastAttackerObjIdx, turns inside in bearing class 0 or avoids the
 * attacker otherwise, then with countermeasure rounds adds 10 to an empty
 * chaffActiveSeconds and uses a round (none when the flight group's status1 or
 * status2 is 21) for chaff with a working countermeasure system, or fires a
 * flare when cmFireCooldownTimer is 0, and returns. Next it looks, while the
 * craft is active, for an enemy starfighter within
 * g_aiAttackerSearchRangeBySkill pointed within 0x2000 angle units of it. With
 * an attacker known, a craft not a utility vehicle whose front shield is below
 * 500 with shields working, or whose hullDamage has reached
 * systemDamageHullThreshold, fires a flare when the attacker is within 0x8000,
 * and when a player flies the attacker switches to avoiding it at full
 * power. */
// FUNCTION: XVT 0x468820
int16_t paiorder_avoidhitorder(void)
{
	uint16_t objectIdx;
	int maxRangeScore;
	uint8_t genusId = g_objectTable[g_paiContext.objectIndex].genusId;

	if (genusId == CRAFT_GENUS_FREIGHTER ||
	    genusId == CRAFT_GENUS_STARSHIP) {
		return 0;
	}

	if (g_paiContext.controller->maneuverMode ==
	    g_paiContext.initialManeuverId) {
		int objectTeam;
		int sourceTeam;
		int isHostile;

		if (g_curCraft->lastAttackerObjIdx == UINT16_MAX) {
			maxRangeScore = g_aiWarheadThreatRangeBySkill
				[g_paiContext.skillTier];
			for (objectIdx = (uint16_t)g_projectileObjectSlotStart;
			     objectIdx < g_projectileObjectSlotEnd;
			     ++objectIdx) {
				if (g_objectTable[objectIdx].objectType != 0 &&
				    g_objectTable[objectIdx]
						    .mobj->pWarheadGuidance
						    ->homingTier != 0 &&
				    g_objectTable[objectIdx]
						    .mobj->pWarheadGuidance
						    ->targetObjIdx ==
					    g_paiContext.objectIndex) {
					if (pai_IsObjectWithinRangeOfCraft(
						    objectIdx,
						    (unsigned int)((g_objectTable[objectIdx]
										    .objectType ==
									    WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE ||
								    g_objectTable[objectIdx]
										    .objectType ==
									    149)
									   ? maxRangeScore *
										     3
									   : maxRangeScore)) ==
					    1) {
						g_curCraft->lastAttackerObjIdx =
							objectIdx;
						pai_ObjectRefDirectionToObjectRef(
							g_paiContext
								.objectIndex,
							objectIdx);
						if (g_aiThreatBearingClassByOctant
							    [(uint16_t)(trig2_xyangle -
									g_objectTable[g_paiContext
											      .objectIndex]
										.yaw) >>
							     13] == 0) {
							g_paiContext.controller
								->maneuverMode =
								AI_MANEUVER_MODE_TURN_INSIDE;
						} else {
							g_paiContext.controller
								->maneuverMode =
								AI_MANEUVER_MODE_AVOID_ATTACKER;
						}
						paiman_initmaneuver();
						if (g_curCraft->cmAmmoCount !=
						    0) {
							if (g_curCraft->cmTypeId ==
								    COUNTERMEASURE_TYPE_CHAFF &&
							    (g_curCraft
								     ->workingSubsystems &
							     CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES) !=
								    0) {
								if (g_curCraft
									    ->chaffActiveSeconds ==
								    0) {
									g_curCraft
										->chaffActiveSeconds +=
										10;
									if (g_missionFlightGroups[g_paiContext
													  .craftFlightGroupIndex]
											    .fg
											    .status1 !=
										    21 &&
									    g_missionFlightGroups[g_paiContext
													  .craftFlightGroupIndex]
											    .fg
											    .status2 !=
										    21) {
										--g_curCraft
											  ->cmAmmoCount;
									}
								}
							} else if (
								g_curCraft->cmTypeId ==
									COUNTERMEASURE_TYPE_FLARE &&
								g_curCraft->cmFireCooldownTimer ==
									0) {
								laser_createcountermeasureprojectile(
									g_paiContext
										.objectIndex,
									COUNTERMEASURE_PROJECTILE_OBJECT_TYPE);
							}
						}
						return 0;
					}
				}
			}
			maxRangeScore = g_aiAttackerSearchRangeBySkill
				[g_paiContext.skillTier];
			if (g_curCraft->lastAttackerObjIdx == UINT16_MAX) {
				for (objectIdx = (uint16_t)
					     g_activeRegionObjectSlotStart;
				     objectIdx <
				     g_activeRegionCraftObjectSlotEnd;
				     ++objectIdx) {
					if (g_objectTable[objectIdx]
						    .objectType == 0) {
						continue;
					}
					objectTeam =
						g_missionFlightGroups
							[g_objectTable[objectIdx]
								 .flightGroupIdx]
								.fg.team;
					sourceTeam =
						g_objectTable
							[g_paiContext
								 .objectIndex]
								.mobj->team;
					isHostile =
						sourceTeam == objectTeam
							? 0
							: g_missionTeams[sourceTeam]
									  .allies[objectTeam] ==
								  0;
					if (isHostile &&
					    g_curCraft->objectKind ==
						    CRAFT_OBJECT_KIND_ACTIVE &&
					    g_objectTable[objectIdx].genusId ==
						    CRAFT_GENUS_STARFIGHTER &&
					    pai_IsObjectWithinRangeOfCraft(
						    objectIdx,
						    (unsigned int)
							    maxRangeScore) ==
						    1) {
						uint16_t horizontalAngle;
						uint16_t verticalAngle;

						pai_ObjectRefDirectionToObjectRef(
							objectIdx,
							g_paiContext
								.objectIndex);
						horizontalAngle =
							(uint16_t)(trig2_xyangle -
								   g_objectTable[objectIdx]
									   .yaw);
						if (horizontalAngle >= 0x8000) {
							horizontalAngle =
								(uint16_t)-horizontalAngle;
						}
						verticalAngle =
							(uint16_t)(trig2_pitch -
								   g_objectTable[objectIdx]
									   .pitch);
						if (verticalAngle >= 0x8000) {
							verticalAngle =
								(uint16_t)-verticalAngle;
						}
						if (horizontalAngle < 0x2000 &&
						    verticalAngle < 0x2000) {
							g_curCraft
								->lastAttackerObjIdx =
								objectIdx;
							break;
						}
					}
				}
			}
		}
		if (g_curCraft->lastAttackerObjIdx != UINT16_MAX &&
		    g_objectTable[g_paiContext.objectIndex].genusId !=
			    CRAFT_GENUS_UTILITY_VEHICLE &&
		    (((g_curCraft->workingSubsystems &
		       CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0 &&
		      g_curCraft->shieldEnergy[0] < 500) ||
		     g_curCraft->hullDamage >=
			     g_curCraft->systemDamageHullThreshold)) {
			pai_ObjectRefDirectionToObjectRef(
				g_curCraft->lastAttackerObjIdx,
				g_paiContext.objectIndex);
			if (trig2_polardistance < 0x8000 &&
			    g_curCraft->cmTypeId == COUNTERMEASURE_TYPE_FLARE &&
			    g_curCraft->cmAmmoCount != 0 &&
			    g_curCraft->cmFireCooldownTimer == 0) {
				laser_createcountermeasureprojectile(
					g_paiContext.objectIndex,
					COUNTERMEASURE_PROJECTILE_OBJECT_TYPE);
			}
			if (g_objectTable[g_curCraft->lastAttackerObjIdx]
				    .playerOwnerIdx != -1) {
				g_paiContext.controller->maneuverMode =
					AI_MANEUVER_MODE_AVOID_ATTACKER;
				paiman_initmaneuver();
				paiman_setpower(g_paiContext.objectIndex,
						UINT16_MAX);
			}
		}
	}

	return 0;
}

/* Order 32: returns 1 when every other flight group that departs to this
 * craft's group as its mothership has arrived, has wavesRemaining 0 and has no
 * craft left in the active region's craft slots; else 0. A group whose
 * arrivalEnabled is 0 and that no player owns is left out. */
// FUNCTION: XVT 0x468CF0
int16_t paiorder_waitforallreturnorder(void)
{
	uint16_t flightGroupIndex;
	uint16_t objectIndex;
	struct ObjectRecord *object;

	flightGroupIndex = 0;
	if ((int16_t)g_missionHeader.numFlightGroups > 0) {
		do {
			if (!((g_missionFgStats[flightGroupIndex]
					       .arrivalEnabled == 0 &&
			       g_missionFlightGroups[flightGroupIndex]
					       .playerOwnerIdx == -1) ||
			      g_paiContext.craftFlightGroupIndex ==
				      flightGroupIndex ||
			      g_missionFlightGroups[flightGroupIndex]
					      .fg.departureMethod == 0 ||
			      g_missionFlightGroups[flightGroupIndex]
					      .fg.departureMothership !=
				      g_paiContext.craftFlightGroupIndex)) {
				if (g_missionFgStats[flightGroupIndex]
						    .hasArrived == 0 ||
				    g_missionFgStats[flightGroupIndex]
						    .wavesRemaining != 0) {
					return 0;
				}
				for (objectIndex =
					     g_activeRegionObjectSlotStart;
				     objectIndex <
				     g_activeRegionCraftObjectSlotEnd;
				     ++objectIndex) {
					object = &g_objectTable[objectIndex];
					if (object->objectType != 0 &&
					    object->flightGroupIdx ==
						    flightGroupIndex) {
						return 0;
					}
				}
			}
			++flightGroupIndex;
		} while ((int16_t)g_missionHeader.numFlightGroups >
			 (int)flightGroupIndex);
	}
	return 1;
}

/* Order 33: returns 1 when every other flight group that arrives from this
 * craft's group as its mothership has arrived with wavesRemaining 0; else 0. A
 * group whose arrivalEnabled is 0 and that no player owns is left out. */
// FUNCTION: XVT 0x468E40
int16_t paiorder_waitforallcreateorder(void)
{
	uint16_t flightGroupIndex;

	flightGroupIndex = 0;
	if ((int16_t)g_missionHeader.numFlightGroups > 0) {
		do {
			if (!((g_missionFgStats[flightGroupIndex]
					       .arrivalEnabled == 0 &&
			       g_missionFlightGroups[flightGroupIndex]
					       .playerOwnerIdx == -1) ||
			      g_paiContext.craftFlightGroupIndex ==
				      flightGroupIndex ||
			      g_missionFlightGroups[flightGroupIndex]
					      .fg.arrivalMethod == 0 ||
			      g_missionFlightGroups[flightGroupIndex]
					      .fg.arrivalMothership !=
				      g_paiContext.craftFlightGroupIndex ||
			      (g_missionFgStats[flightGroupIndex].hasArrived !=
				       0 &&
			       g_missionFgStats[flightGroupIndex]
					       .wavesRemaining == 0))) {
				return 0;
			}
			++flightGroupIndex;
		} while ((int16_t)g_missionHeader.numFlightGroups >
			 (int)flightGroupIndex);
	}

	return 1;
}

/* Order 34: returns 1 and drops both target and candidate (0xFFFF) when the
 * craft is on its plan's maneuver and its candidate target is AI_TARGET_ABORT;
 * else 0. */
// FUNCTION: XVT 0x468F20
int16_t paiorder_evasiveorder(void)
{
	if (g_paiContext.controller->maneuverMode !=
		    g_paiContext.initialManeuverId ||
	    g_paiContext.controller->candidateTargetIdx != AI_TARGET_ABORT) {
		return 0;
	}

	g_paiContext.controller->targetObjIdx = 0xffff;
	g_paiContext.controller->targetSignature = 0;
	g_paiContext.controller->hasLiveTarget = 0;
	g_paiContext.controller->candidateTargetIdx = 0xffff;

	return 1;
}

/* Order 35: makes the candidate target the craft's target, with its signature
 * and hasLiveTarget 1, when it names an object (not 0xFFFF or AI_TARGET_ABORT)
 * that can be targeted and is not the target already; clears a candidate that
 * cannot be targeted. Returns 0 on every path. */
// FUNCTION: XVT 0x468F80
int16_t paiorder_targetfromplayerorder(void)
{
	uint16_t candidateTargetIdx;
	unsigned int objectIndex;
	int validTarget;

	candidateTargetIdx = g_paiContext.controller->candidateTargetIdx;
	if (candidateTargetIdx == UINT16_MAX ||
	    candidateTargetIdx == AI_TARGET_ABORT) {
		return 0;
	}
	objectIndex = g_paiContext.controller->candidateTargetIdx;
	validTarget = pai_IsObjectTargetable(objectIndex);
	if (validTarget != 0) {
		candidateTargetIdx =
			g_paiContext.controller->candidateTargetIdx;
		if (g_paiContext.controller->targetObjIdx ==
		    candidateTargetIdx) {
			return 0;
		}
		g_paiContext.controller->targetObjIdx = candidateTargetIdx;
		g_paiContext.controller->targetSignature =
			g_objectTable[g_paiContext.controller->targetObjIdx]
				.objectSignature;
		g_paiContext.controller->hasLiveTarget = 1;
		return 0;
	}
	g_paiContext.controller->candidateTargetIdx = UINT16_MAX;
	return 0;
}

/* Order 36. Off the avoid-starship maneuver, it asks
 * collide_craftstarshipcollision whether the craft will hit something within 6
 * simulated seconds, restoring g_curCraft after. When it will, and the object
 * is neither the craft nor what it carries, nor the target it is attacking or
 * rocket attacking (a Calamari cruiser or Imperial Star Destroyer target still
 * counts), it sets targetXYAngle a quarter turn off its yaw (plus for an odd
 * craftOrdinal, minus for an even one) and targetZAngle a quarter turn off its
 * pitch (minus when the old targetZAngle is above 0x4000, else plus), and
 * switches to the avoid-starship maneuver; for an object at or past the end of
 * the region's main slots it sets secondaryManeuverTimer to 3 to 6 times
 * SIMULATION_TICKS_PER_SECOND at random. Returns 0 there. On the avoid-starship
 * maneuver it returns 1 once secondaryManeuverTimer has run out, else 0. */
// FUNCTION: XVT 0x469150
int16_t paiorder_avoidstarshiporder(void)
{
	enum {
		COLLISION_LOOKAHEAD_SECONDS = 6,
		QUARTER_TURN = 0x4000,
		MIN_AVOIDANCE_SECONDS = 3,
		AVOIDANCE_DURATION_MASK = 3,
	};

	int16_t maneuverMode;
	struct CraftData *savedCraft;
	uint16_t collisionObjectIndex;

	maneuverMode = g_paiContext.controller->maneuverMode;
	if (maneuverMode != AI_MANEUVER_MODE_AVOID_STARSHIP) {
		int16_t sourceObjectIndex = g_paiContext.objectIndex;

		savedCraft = g_curCraft;
		collisionObjectIndex = collide_craftstarshipcollision(
			sourceObjectIndex, COLLISION_LOOKAHEAD_SECONDS);
		g_curCraft = savedCraft;
		if (collisionObjectIndex != UINT16_MAX) {
			if (g_paiContext.controller->targetObjIdx ==
				    collisionObjectIndex &&
			    (maneuverMode == AI_MANEUVER_MODE_ATTACK ||
			     maneuverMode == AI_MANEUVER_MODE_ROCKET_ATTACK)) {
				uint8_t objectType =
					g_objectTable[collisionObjectIndex]
						.objectType;
				if (objectType !=
					    CRAFT_SPECIES_CALAMARI_CRUISER &&
				    objectType !=
					    CRAFT_SPECIES_IMPERIAL_STAR_DESTROYER) {
					return 0;
				}
			}
			if (collisionObjectIndex != g_paiContext.objectIndex &&
			    savedCraft->carriedObjectIndex !=
				    collisionObjectIndex) {
				if ((savedCraft->craftOrdinal & 1) != 0) {
					g_paiContext.controller->targetXYAngle =
						(uint16_t)(g_objectTable
								   [g_paiContext
									    .objectIndex]
									   .yaw +
							   QUARTER_TURN);
				} else {
					g_paiContext.controller->targetXYAngle =
						(uint16_t)(g_objectTable
								   [g_paiContext
									    .objectIndex]
									   .yaw -
							   QUARTER_TURN);
				}

				if (g_paiContext.controller->targetZAngle >
				    QUARTER_TURN) {
					g_paiContext.controller->targetZAngle =
						(uint16_t)(g_objectTable
								   [g_paiContext
									    .objectIndex]
									   .pitch -
							   QUARTER_TURN);
				} else {
					g_paiContext.controller->targetZAngle =
						(uint16_t)(g_objectTable
								   [g_paiContext
									    .objectIndex]
									   .pitch +
							   QUARTER_TURN);
				}

				g_paiContext.controller->maneuverMode =
					AI_MANEUVER_MODE_AVOID_STARSHIP;
				paiman_initmaneuver();
				if (collisionObjectIndex >=
				    g_regionMainObjectSlotEnd) {
					g_paiContext.controller
						->secondaryManeuverTimer =
						SIMULATION_TICKS_PER_SECOND *
						((GameRand() &
						  AVOIDANCE_DURATION_MASK) +
						 MIN_AVOIDANCE_SECONDS);
				}
			} else {
				return 0;
			}
		}
	} else if (g_paiContext.controller->secondaryManeuverTimer == 0) {
		return 1;
	}
	return 0;
}

/* Order 37: returns 1 when the flight group's departureMethod is nonzero, which
 * paiorder_flyhomeorder takes as a departure by mothership, else 0. */
// FUNCTION: XVT 0x469310
int16_t paiorder_checkhyperorder(void)
{
	return g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
		       .fg.departureMethod != 0;
}

/* Order 38: returns 1 when the flight group's departure is due for the craft,
 * else 0; 0 at once when maxSpeedCache is 0. The departure starts when the
 * mission clock's minutes and seconds reach the group's departure time, or when
 * its departure trigger pair has a condition and holds: the craft then records
 * the clock in departClockHours, departClockMinutes and departClockSeconds,
 * counts the not-departed outcome and sets departTimerFlag. Once the group's
 * departure delay has passed since then, it has the tactical officer announce
 * the withdrawal, sends message 385 or 386 to the local player, restores
 * working subsystems when the current order's leader plan is waitforboardpln
 * and subsystemDamage is 0, and returns 1. */
// FUNCTION: XVT 0x469340
int16_t paiorder_stopgohomeorder(void)
{
	enum {
		DEPART_TIMER_ACTIVE = 1,
		SECONDS_PER_MINUTE = 60,
		MINUTES_PER_HOUR = 60,
		MESSAGE_MODEL_SLOT = 0,
		MESSAGE_FLIGHT_GROUP_SLOT = 1,
	};

	int departNow;
	uint8_t departureClockSeconds;
	uint8_t departureClockMinutes;

	if (g_curCraft->aiFlight.maxSpeedCache == 0) {
		return 0;
	}

	departNow = 0;
	departureClockSeconds =
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.departureClockSec;
	departureClockMinutes =
		g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.departureClockMin;
	if (departureClockSeconds + departureClockMinutes != 0) {
		if (g_missionElapsedClock.minutes > departureClockMinutes) {
			departNow = 1;
		} else if (g_missionElapsedClock.minutes ==
				   departureClockMinutes &&
			   g_missionElapsedClock.seconds >=
				   departureClockSeconds) {
			departNow = 1;
		}
	}

	if (g_curCraft->aiFlight.departTimerFlag == 0) {
		if ((g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
				     .fg.departureTrigger.triggers[0]
				     .condition != 0 ||
		     g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
				     .fg.departureTrigger.triggers[1]
				     .condition != 0) &&
		    (Mission_EvaluateTriggerPair(
			     &g_missionFlightGroups
				      [g_paiContext.craftFlightGroupIndex]
					      .fg.departureTrigger,
			     0) &
		     1) != 0) {
			departNow = 1;
		}
		if (departNow == 1) {
			uint8_t *departTimerFlag;
			uint16_t flightGroupIndex;

			g_curCraft->aiFlight.departClockHours =
				g_missionElapsedClock.hours;
			g_curCraft->aiFlight.departClockMinutes =
				g_missionElapsedClock.minutes;
			g_curCraft->aiFlight.departClockSeconds =
				g_missionElapsedClock.seconds;
			departTimerFlag = &g_curCraft->aiFlight.departTimerFlag;
			if (*departTimerFlag == 0) {
				flightGroupIndex =
					g_paiContext.craftFlightGroupIndex;
				++g_missionFgStats[flightGroupIndex].outcomeCount
					  [FLIGHT_GROUP_OUTCOME_NOT_DEPARTED];
				if (g_missionFlightGroups[flightGroupIndex]
					    .fg.specialCargoCraft ==
				    g_curCraft->craftOrdinal) {
					g_missionFgStats[flightGroupIndex].specialCargoOutcome
						[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] =
						1;
				}
			}
			*departTimerFlag = DEPART_TIMER_ACTIVE;
		}
	}

	if (g_curCraft->aiFlight.departTimerFlag == DEPART_TIMER_ACTIVE) {
		unsigned int departureDelaySeconds;
		unsigned int elapsedSeconds;
		uint16_t order;
		uint8_t planId;

		departureDelaySeconds =
			SECONDS_PER_MINUTE *
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg.departureDelayMinutes +
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.departureDelaySeconds;
		elapsedSeconds =
			SECONDS_PER_MINUTE *
				(g_missionElapsedClock.minutes +
				 MINUTES_PER_HOUR *
					 (g_missionElapsedClock.hours -
					  g_curCraft->aiFlight
						  .departClockHours) -
				 g_curCraft->aiFlight.departClockMinutes) -
			g_curCraft->aiFlight.departClockSeconds +
			g_missionElapsedClock.seconds;
		if (departureDelaySeconds == 0 ||
		    elapsedSeconds >= departureDelaySeconds) {
			fsfx_SpeakTacticalOfficerEvent(
				TACTICAL_VOICE_STATUS, TACTICAL_MSG_WITHDRAWING,
				g_paiContext.objectIndex, UINT16_MAX);
			g_msgSenderIff =
				(uint8_t)g_objectTable[g_paiContext.objectIndex]
					.mobj->iff;
			msg_addMessagePtr(MESSAGE_MODEL_SLOT,
					  &g_modelDefs[g_curCraft->modelIndex]);
			msg_addMessagePtr(
				MESSAGE_FLIGHT_GROUP_SLOT,
				&g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]);
			if (Hud_MissionFG_GetCraftNumberIfShown(
				    g_paiContext.craftFlightGroupIndex,
				    g_curCraft) == 0) {
				msg_emitInFlightMessage(
					IFMSG_385_ARG_ARG_WITHDRAWING_FROM_COMBAT_AREA,
					g_localPlayer);
			} else {
				msg_emitInFlightMessage(
					IFMSG_386_FLIGHT_GROUP_ARG_ARG_WITHDRAWING_FROM_COMBAT_AREA,
					g_localPlayer);
			}

			order = g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg
						.orders[g_paiContext.orderSlot]
						.order;
			planId = g_builtinPlanIdByNameIndex
				[g_orderLeaderBuiltinPlanNameIndex[order]];
			if (strcmp(g_planTable[planId].name,
				   "waitforboardpln") == 0 &&
			    g_curCraft->subsystemDamage == 0) {
				g_curCraft->workingSubsystems =
					g_curCraft->systemFlags;
			}
			return 1;
		}
	}

	return 0;
}

/* Order 39: marks the current order slot done and returns 1 once all orders are
 * done; else 0. A slot whose completionState is not yet 2 or 3 becomes 2 when
 * pai_IsPlanCompleteForOrderSlot says its plan is complete, else 3 when
 * pai_IsBoardingPlanCompleteForOrderSlot does. With the slot done it returns 0
 * when maxSpeedCache is 0, 1 after skipping to order 4, else 1 when every order
 * the group has in slots 0 to 2 is 2 or 3. Sets goHomeFlag when they are all
 * 2. */
// FUNCTION: XVT 0x469690
int16_t paiorder_completegohomeorder(void)
{
	uint8_t completionState;
	uint16_t orderSlot;
	unsigned int activeOrderCount;
	uint16_t flightGroupIdx;
	int completedOrderCount;
	int completedBoardingOrderCount;
	int16_t allOrdersComplete;

	completionState = g_paiContext.controller->orderProgress
				  .completionState[g_paiContext.orderSlot];
	if (completionState != 2 && completionState != 3) {
		if (pai_IsPlanCompleteForOrderSlot(
			    g_paiContext.controller->currentPlanId,
			    g_paiContext.orderSlot) != 0) {
			g_paiContext.controller->orderProgress
				.completionState[g_paiContext.orderSlot] = 2;
		} else if (pai_IsBoardingPlanCompleteForOrderSlot(
				   g_paiContext.controller->currentPlanId,
				   g_paiContext.orderSlot) != 0) {
			g_paiContext.controller->orderProgress
				.completionState[g_paiContext.orderSlot] = 3;
		}
	}

	completionState = g_paiContext.controller->orderProgress
				  .completionState[g_paiContext.orderSlot];
	if (completionState != 2 && completionState != 3) {
		return 0;
	}
	if (g_curCraft->aiFlight.maxSpeedCache == 0) {
		return 0;
	}
	if (g_paiContext.controller->skippedToOrder4 == 1) {
		return 1;
	}

	orderSlot = 0;
	activeOrderCount = 0;
	flightGroupIdx = g_paiContext.craftFlightGroupIndex;
	completedOrderCount = 0;
	completedBoardingOrderCount = 0;
	do {
		if (g_missionFlightGroups[flightGroupIdx]
			    .fg.orders[orderSlot]
			    .order != 0) {
			++activeOrderCount;
			completionState = g_paiContext.controller->orderProgress
						  .completionState[orderSlot];
			if (completionState == 2) {
				++completedOrderCount;
			}
			if (completionState == 3) {
				++completedBoardingOrderCount;
			}
		}
		++orderSlot;
	} while (orderSlot < 3);
	if (activeOrderCount != 0 &&
	    (unsigned int)completedOrderCount == activeOrderCount &&
	    g_curCraft->aiFlight.goHomeFlag == 0) {
		g_curCraft->aiFlight.goHomeFlag = 1;
	}
	allOrdersComplete =
		(unsigned int)(completedBoardingOrderCount +
			       completedOrderCount) >= activeOrderCount;
	return allOrdersComplete;
}

/* Order 40: moves the craft to its next order and returns 1, else returns 0; 0
 * at once after skipping to order 4. First, unless g_paiSkipToOrder4Checked is
 * set, it tests the flight group's skip-to-order-4 trigger pair when either
 * condition is not MISSION_COND_ALWAYS_TRUE: when it holds, the craft skips to
 * slot 3 (skippedToOrder4 1) and it returns 1; when not, it sets
 * g_paiSkipToOrder4Checked. Then, when the current slot's completionState is 2,
 * the slot is below 2 and the next slot has an order, it moves to that slot. A
 * move sets the slot in g_paiContext and the controller, currentPlanId to the
 * order's leader plan, and g_paiContext.variablePlanId to the leader or
 * follower plan, for the "variablepln" switch. */
// FUNCTION: XVT 0x4697F0
int16_t paiorder_completegootherorder(void)
{
	enum {
		ORDER_NONE = 0,
		ORDER_STATE_SKIPPED_TO_ORDER4 = 1,
		ORDER_COMPLETION_COMPLETE = 2,
		THIRD_ORDER_SLOT = 2,
		FOURTH_ORDER_SLOT = 3,
	};

	int order;

	if (g_paiContext.controller->skippedToOrder4 ==
	    ORDER_STATE_SKIPPED_TO_ORDER4) {
		return 0;
	}
	if (g_paiSkipToOrder4Checked == 0) {
		if (g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
				    .fg.skipToOrder4.triggers[0]
				    .condition != MISSION_COND_ALWAYS_TRUE ||
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
				    .fg.skipToOrder4.triggers[1]
				    .condition != MISSION_COND_ALWAYS_TRUE) {
			if ((Mission_EvaluateTriggerPair(
				     &g_missionFlightGroups
					      [g_paiContext
						       .craftFlightGroupIndex]
						      .fg.skipToOrder4,
				     0) &
			     1) != 0) {
				g_paiContext.controller->skippedToOrder4 =
					ORDER_STATE_SKIPPED_TO_ORDER4;
				g_paiContext.orderSlot = FOURTH_ORDER_SLOT;
				g_paiContext.controller->currentOrderSlot =
					FOURTH_ORDER_SLOT;
				order = g_missionFlightGroups
						[g_paiContext
							 .craftFlightGroupIndex]
							.fg
							.orders[g_paiContext
									.orderSlot]
							.order;
				g_paiContext.controller->currentPlanId =
					g_builtinPlanIdByNameIndex
						[g_orderLeaderBuiltinPlanNameIndex
							 [order]];
				if (g_curCraft->leader_obj_idx == UINT8_MAX) {
					g_paiContext.variablePlanId =
						g_builtinPlanIdByNameIndex
							[g_orderLeaderBuiltinPlanNameIndex
								 [order]];
				} else {
					g_paiContext.variablePlanId =
						g_builtinPlanIdByNameIndex
							[g_orderFollowerBuiltinPlanNameIndex
								 [order]];
				}
				return 1;
			}
			g_paiSkipToOrder4Checked = 1;
		}
	}

	if (g_paiContext.controller->orderProgress
			    .completionState[g_paiContext.orderSlot] !=
		    ORDER_COMPLETION_COMPLETE ||
	    g_paiContext.orderSlot == THIRD_ORDER_SLOT ||
	    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.orders[g_paiContext.orderSlot + 1]
			    .order == ORDER_NONE) {
		return 0;
	}

	++g_paiContext.orderSlot;
	g_paiContext.controller->currentOrderSlot =
		(uint8_t)g_paiContext.orderSlot;
	order = g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			.fg.orders[g_paiContext.orderSlot]
			.order;
	g_paiContext.controller->currentPlanId = g_builtinPlanIdByNameIndex
		[g_orderLeaderBuiltinPlanNameIndex[order]];
	if (g_curCraft->leader_obj_idx == UINT8_MAX) {
		g_paiContext.variablePlanId = g_builtinPlanIdByNameIndex
			[g_orderLeaderBuiltinPlanNameIndex[order]];
	} else {
		g_paiContext.variablePlanId = g_builtinPlanIdByNameIndex
			[g_orderFollowerBuiltinPlanNameIndex[order]];
	}
	return 1;
}

/* Order 42: returns 1 after moving the craft to the first later order slot,
 * below 3, whose plan name is capldr1pln to capldr5pln, capescortersldr1pln,
 * caprespondldr1pln or capflw1pln to capflw4pln; else 0, and 0 at once after
 * skipping to order 4. It reads that name as g_planTable[order], by the mission
 * order number itself, not by the order's plan id as the other order functions
 * do. A move sets the controller's currentOrderSlot, not
 * g_paiContext.orderSlot, currentPlanId to the order's leader plan and
 * g_paiContext.variablePlanId to the leader or follower plan. */
// FUNCTION: XVT 0x469A10
int16_t paiorder_waitgootherorder(void)
{
	uint16_t order;
	uint16_t orderSlot;
	const char *planName;

	if (g_paiContext.controller->skippedToOrder4 == 1) {
		return 0;
	}
	orderSlot = g_paiContext.orderSlot + 1;
	while (orderSlot < 3) {
		order = g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderSlot]
				.order;
		planName = g_planTable[order].name;
		if (strcmp(planName, "capldr1pln") == 0 ||
		    strcmp(planName, "capescortersldr1pln") == 0 ||
		    strcmp(planName, "caprespondldr1pln") == 0 ||
		    strcmp(planName, "capldr2pln") == 0 ||
		    strcmp(planName, "capldr3pln") == 0 ||
		    strcmp(planName, "capldr4pln") == 0 ||
		    strcmp(planName, "capldr5pln") == 0 ||
		    strcmp(planName, "capflw1pln") == 0 ||
		    strcmp(planName, "capflw2pln") == 0 ||
		    strcmp(planName, "capflw3pln") == 0 ||
		    strcmp(planName, "capflw4pln") == 0) {
			g_paiContext.controller->currentOrderSlot =
				(uint8_t)orderSlot;
			g_paiContext.controller
				->currentPlanId = g_builtinPlanIdByNameIndex
				[g_orderLeaderBuiltinPlanNameIndex[order]];
			if (g_curCraft->leader_obj_idx == UINT8_MAX) {
				g_paiContext.variablePlanId =
					g_builtinPlanIdByNameIndex
						[g_orderLeaderBuiltinPlanNameIndex
							 [order]];
			} else {
				g_paiContext.variablePlanId =
					g_builtinPlanIdByNameIndex
						[g_orderFollowerBuiltinPlanNameIndex
							 [order]];
			}
			return 1;
		}
		++orderSlot;
	}
	return 0;
}

/* Order 43: returns 1 after taking the craft back to an earlier order it can
 * work on again, else 0; 0 at once after skipping to order 4. It first runs the
 * same skip-to-order-4 test as paiorder_completegootherorder, returning 1 on a
 * skip. Then, past slot 0, it takes the first earlier slot not marked complete
 * whose leader plan is capfreeldr1pln, caprespondldr1pln, capescortersldr1pln
 * or disableldr1pln with a target found now, or a boardto plan other than
 * boardtopickuppln with a target it can board. The switch back sets the
 * controller's currentOrderSlot, not g_paiContext.orderSlot, currentPlanId and
 * g_paiContext.variablePlanId. */
// FUNCTION: XVT 0x469BD0
int16_t paiorder_orderswitchorder(void)
{
	enum {
		ORDER_STATE_SKIPPED_TO_ORDER4 = 1,
		ORDER_COMPLETION_COMPLETE = 2,
		FOURTH_ORDER_SLOT = 3,
	};

	int order;
	int16_t foundOrder;
	uint16_t orderSlot;

	if (g_paiContext.controller->skippedToOrder4 ==
	    ORDER_STATE_SKIPPED_TO_ORDER4) {
		return 0;
	}

	if (g_paiSkipToOrder4Checked == 0) {
		int flightGroupIndex;

		flightGroupIndex = g_paiContext.craftFlightGroupIndex;
		if (g_missionFlightGroups[flightGroupIndex]
				    .fg.skipToOrder4.triggers[0]
				    .condition != MISSION_COND_ALWAYS_TRUE ||
		    g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
				    .fg.skipToOrder4.triggers[1]
				    .condition != MISSION_COND_ALWAYS_TRUE) {
			if ((Mission_EvaluateTriggerPair(
				     &g_missionFlightGroups[flightGroupIndex]
					      .fg.skipToOrder4,
				     0) &
			     1) != 0) {
				g_paiContext.controller->skippedToOrder4 =
					ORDER_STATE_SKIPPED_TO_ORDER4;
				g_paiContext.orderSlot = FOURTH_ORDER_SLOT;
				g_paiContext.controller->currentOrderSlot =
					FOURTH_ORDER_SLOT;
				order = g_missionFlightGroups
						[g_paiContext
							 .craftFlightGroupIndex]
							.fg
							.orders[g_paiContext
									.orderSlot]
							.order;
				g_paiContext.controller->currentPlanId =
					g_builtinPlanIdByNameIndex
						[g_orderLeaderBuiltinPlanNameIndex
							 [order]];
				if (g_curCraft->leader_obj_idx == UINT8_MAX) {
					g_paiContext.variablePlanId =
						g_builtinPlanIdByNameIndex
							[g_orderLeaderBuiltinPlanNameIndex
								 [order]];
				} else {
					g_paiContext.variablePlanId =
						g_builtinPlanIdByNameIndex
							[g_orderFollowerBuiltinPlanNameIndex
								 [order]];
				}
				return 1;
			}
			g_paiSkipToOrder4Checked = 1;
		}
	}

	if (g_paiContext.orderSlot == 0) {
		return 0;
	}

	foundOrder = 0;
	orderSlot = 0;
	while (orderSlot < g_paiContext.orderSlot) {
		const char *planName;

		if (foundOrder != 0) {
			break;
		}
		if (g_paiContext.controller->orderProgress
			    .completionState[orderSlot] !=
		    ORDER_COMPLETION_COMPLETE) {
			order = g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg.orders[orderSlot]
						.order;
			planName =
				g_planTable
					[g_builtinPlanIdByNameIndex
						 [g_orderLeaderBuiltinPlanNameIndex
							  [order]]]
						.name;
			if (strcmp(planName, "capfreeldr1pln") == 0 ||
			    strcmp(planName, "caprespondldr1pln") == 0 ||
			    strcmp(planName, "capescortersldr1pln") == 0 ||
			    strcmp(planName, "disableldr1pln") == 0) {
				if (paifight_SearchOrderSlotTarget(orderSlot) !=
				    0) {
					foundOrder = 1;
				}
			} else if ((strcmp(planName, "boardtogivepln") == 0 ||
				    strcmp(planName, "boardtotakepln") == 0 ||
				    strcmp(planName, "boardtoexchangepln") ==
					    0 ||
				    strcmp(planName, "boardtocapturepln") ==
					    0 ||
				    strcmp(planName, "boardtodestroypln") ==
					    0 ||
				    strcmp(planName, "boardtocontactpln") ==
					    0 ||
				    strcmp(planName, "boardtorepairpln") ==
					    0) &&
				   pai_OrderSlotCanBoardTarget(orderSlot) !=
					   0) {
				foundOrder = 1;
			}
		}
		++orderSlot;
	}

	if (foundOrder != 0) {
		--orderSlot;
		g_paiContext.controller->currentOrderSlot = (uint8_t)orderSlot;
		order = g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[orderSlot]
				.order;
		g_paiContext.controller->currentPlanId =
			g_builtinPlanIdByNameIndex
				[g_orderLeaderBuiltinPlanNameIndex[order]];
		if (g_curCraft->leader_obj_idx == UINT8_MAX) {
			g_paiContext.variablePlanId = g_builtinPlanIdByNameIndex
				[g_orderLeaderBuiltinPlanNameIndex[order]];
		} else {
			g_paiContext.variablePlanId = g_builtinPlanIdByNameIndex
				[g_orderFollowerBuiltinPlanNameIndex[order]];
		}
		return 1;
	}

	return 0;
}

/* Order 41: keeps a follower on its leader's order. A follower takes on its
 * leader's goHomeFlag and departTimerFlag when they are set, counting the
 * not-departed outcome when it takes the second. When the leader's
 * currentOrderSlot differs from the craft's order slot, the craft moves to that
 * slot, with currentPlanId and g_paiContext.variablePlanId set as on any order
 * move, and it returns 1; else 0. */
// FUNCTION: XVT 0x469F40
int16_t paiorder_completefolloworder(void)
{
	struct AiController *leaderController;
	uint16_t flightGroupIndex;
	int runtimeFlightGroupIndex;
	uint16_t currentOrderSlot;

	leaderController = &g_paiContext.leaderOrSelfCraft->aiController;
	if (g_paiContext.leaderObjectIndex != UINT8_MAX) {
		if (g_paiContext.leaderOrSelfCraft->aiFlight.goHomeFlag == 1 &&
		    g_curCraft->aiFlight.goHomeFlag == 0) {
			g_curCraft->aiFlight.goHomeFlag = 1;
		}
		if (g_paiContext.leaderOrSelfCraft->aiFlight.departTimerFlag ==
			    1 &&
		    g_curCraft->aiFlight.departTimerFlag == 0) {
			g_curCraft->aiFlight.departTimerFlag = 1;
			flightGroupIndex = g_paiContext.craftFlightGroupIndex;
			runtimeFlightGroupIndex =
				g_paiContext.craftFlightGroupIndex;
			++g_missionFgStats[runtimeFlightGroupIndex].outcomeCount
				  [FLIGHT_GROUP_OUTCOME_NOT_DEPARTED];
			if (g_missionFlightGroups[flightGroupIndex]
				    .fg.specialCargoCraft ==
			    g_curCraft->craftOrdinal) {
				g_missionFgStats[runtimeFlightGroupIndex]
					.specialCargoOutcome
						[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] =
					1;
			}
		}
	}

	currentOrderSlot = leaderController->currentOrderSlot;
	if (g_paiContext.orderSlot != currentOrderSlot) {
		int order;

		g_paiContext.orderSlot = currentOrderSlot;
		g_paiContext.controller->currentOrderSlot =
			(uint8_t)currentOrderSlot;
		order = g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[g_paiContext.orderSlot]
				.order;
		{
			struct AiController *currentController;
			int planNameIndex;

			currentController = g_paiContext.controller;
			planNameIndex =
				g_orderLeaderBuiltinPlanNameIndex[order];
			currentController->currentPlanId =
				g_builtinPlanIdByNameIndex[planNameIndex];
		}
		if (g_paiContext.leaderObjectIndex == UINT8_MAX) {
			g_paiContext.variablePlanId = g_builtinPlanIdByNameIndex
				[g_orderLeaderBuiltinPlanNameIndex[order]];
			return 1;
		}
		g_paiContext.variablePlanId = g_builtinPlanIdByNameIndex
			[g_orderFollowerBuiltinPlanNameIndex[order]];
		return 1;
	}
	return 0;
}

/* Order 44: starts the craft's self-destruct countdown when none runs: sets its
 * lifetimeTimer to variable1 of the current order times 1,180 ticks (five times
 * SIMULATION_TICKS_PER_SECOND), or, for variable1 0, 2 to 5 such units at
 * random. Object_UpdateLifetimeAndMovement destroys the craft when it runs out.
 * Does not check that the product fits the 16-bit timer. Returns 0 on every
 * path. */
// FUNCTION: XVT 0x46A0A0
int16_t paiorder_killselforder(void)
{
	uint16_t delayFiveSecondUnits;

	if (g_objectTable[g_paiContext.objectIndex].mobj->lifetimeTimer == 0) {
		delayFiveSecondUnits =
			g_missionFlightGroups[g_paiContext
						      .craftFlightGroupIndex]
				.fg.orders[g_paiContext.orderSlot]
				.variable1;
		if (delayFiveSecondUnits == 0) {
			delayFiveSecondUnits = ((uint16_t)GameRand() & 3) + 2;
		}

		g_objectTable[g_paiContext.objectIndex].mobj->lifetimeTimer =
			(uint16_t)(delayFiveSecondUnits * 1180u);
	}
	return 0;
}

/* Order 46: returns 1 when the mothership groups the craft would depart to have
 * all arrived, comparing each group's FLIGHT_GROUP_OUTCOME_TOTAL and
 * FLIGHT_GROUP_OUTCOME_ARRIVED counts: for a captured craft, the captured
 * departure mothership group, and only when it departs by one; else the
 * departure mothership group when departureMethod is set and the alternate one
 * when used, which with neither gives 1. Returns 0 when the target lies in the
 * active region's craft slots or below them, or the model has no hyperdrive. */
// FUNCTION: XVT 0x46A250
int16_t paiorder_abortmotherwaitorder(void)
{
	int16_t result;

	if ((int)g_paiContext.controller->targetObjIdx <
	    g_activeRegionCraftObjectSlotEnd) {
		return 0;
	}
	if (g_modelDefs[g_curCraft->modelIndex].hasHyperdrive == 0) {
		return 0;
	}

	result = 0;
	if (g_curCraft->capturedByFlightGroup != 0) {
		if (g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.capturedDepartViaMothership != 0) {
			uint16_t mothershipFlightGroup;

			mothershipFlightGroup =
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg.capturedDepartureMothership;
			if (g_missionFgStats[mothershipFlightGroup]
				    .outcomeCount[FLIGHT_GROUP_OUTCOME_TOTAL] ==
			    g_missionFgStats[mothershipFlightGroup].outcomeCount
				    [FLIGHT_GROUP_OUTCOME_ARRIVED]) {
				result = 1;
			}
		}
	} else {
		int16_t departureMothershipReady;
		int16_t alternateMothershipReady;

		departureMothershipReady = 1;
		alternateMothershipReady = 1;
		if (g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.departureMethod != 0) {
			uint16_t mothershipFlightGroup;

			mothershipFlightGroup =
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg.departureMothership;
			if (g_missionFgStats[mothershipFlightGroup]
				    .outcomeCount[FLIGHT_GROUP_OUTCOME_TOTAL] !=
			    g_missionFgStats[mothershipFlightGroup].outcomeCount
				    [FLIGHT_GROUP_OUTCOME_ARRIVED]) {
				departureMothershipReady = 0;
			}
		}
		if (g_missionFlightGroups[g_paiContext.craftFlightGroupIndex]
			    .fg.alternateMothershipUsed != 0) {
			uint16_t mothershipFlightGroup;

			mothershipFlightGroup =
				g_missionFlightGroups
					[g_paiContext.craftFlightGroupIndex]
						.fg.alternateMothership;
			if (g_missionFgStats[mothershipFlightGroup]
				    .outcomeCount[FLIGHT_GROUP_OUTCOME_TOTAL] !=
			    g_missionFgStats[mothershipFlightGroup].outcomeCount
				    [FLIGHT_GROUP_OUTCOME_ARRIVED]) {
				alternateMothershipReady = 0;
			}
		}
		result = (int16_t)(departureMothershipReady &
				   alternateMothershipReady);
	}
	return result;
}
