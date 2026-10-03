#include "xvt/flight/object/object.h"
#ifdef XVT_MODERN
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/reference_motion.h"
#endif

#include "xvt/assets/model_mesh.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt/util/game_rand.h"
#include <string.h>

/* Per object type counted from WARHEAD_OBJECT_TYPE_PROTON_TORPEDO (143),
 * the first entry of the shot's row of 7 in the two homing tables below;
 * Object_UpdateLifetimeAndMovement adds the shot's homingTier, 0 to 6. */
// GLOBAL: XVT 0x521CB6
const uint8_t g_projectileHomingProfileBaseByObjectType[18] = {
	7, 0, 0, 0, 0, 0, 14, 21, 28, 14, 14, 14, 35, 0, 0, 0, 0, 0};
/* Homing turn rate in angle units per simulated second, by row plus homing
 * tier; each row of 7 starts with 0, so tier 0 never turns. */
// GLOBAL: XVT 0x521CC8
const uint16_t g_projectileHomingTurnRateByProfile[44] = {
	0,    1024, 2048, 3072, 5120, 7168,  9216,  0,	   512,	  1024,	 2048,
	3072, 4608, 6144, 0,	2048, 4096,  5120,  10240, 14336, 18432, 0,
	32,   64,   80,	  96,	112,  128,   0,	    512,   1024,  1280,	 1536,
	1792, 2048, 0,	  4096, 8192, 12288, 16384, 20480, 24576, 0,	 0,
};
/* Speed change per simulated second while homing, by the same rows: added
 * when the shot's yaw has reached the target's bearing and it is below its
 * cruiseSpeed, taken away (to no less than HOMING_MIN_TURN_SPEED, 200) while
 * its yaw still turns. */
// GLOBAL: XVT 0x521D20
const uint16_t g_projectileHomingSpeedAdjustRateByProfile[44] = {
	0,   50,  100, 200, 300, 400, 500, 0,	25,   50,   100,
	150, 200, 250, 0,   100, 200, 400, 600, 800,  1000, 0,
	0,   0,	  0,   0,   0,	 0,   0,   10,	20,   30,   40,
	50,  60,  0,   100, 200, 400, 600, 800, 1000, 0,    0,
};

/* The slot numbers below describe the layout of g_objectTable that
 * Mission_Init sets when a mission loads: craft 0 to 31, shots 32 to 191,
 * debris 192 to 207, explosions 208 to 223, character slots 224 to 479,
 * local slots 480 to 487, then 64 static slots, 488 to 551. Where a comment
 * says "restored", a world-state load copies the value back in:
 * Flight_RestoreWorldState in the original build, XvtSnapshot_DecodePrefix
 * in the modern one. */

/* One past the last craft slot, 32 (CRAFT_SLOT_COUNT); set by Mission_Init
 * and restored. Craft loops across the game run from
 * g_activeRegionObjectSlotStart up to here. */
// GLOBAL: XVT 0x9A1FE0
int g_activeRegionCraftObjectSlotEnd = 0;
/* The object table: g_regionMainObjectSlotEnd slots with a MobileObject,
 * then g_regionStaticObjectSlotCount static slots. Mission_Init allocates
 * g_objectTableHandle; FeDiskIo_LockGlobalBuffers points this at its locked
 * memory. In the modern build XvtFlightLoading_Reset sets NULL. */
// GLOBAL: XVT 0x9A1FE8
ObjectRecord *g_objectTable = 0;
/* Per mobile slot, the pool entries Object_RelinkMobileObjectPointers
 * would link. Mission_Init sets every index to -1 and nothing else writes
 * them, so the relink links none. */
// GLOBAL: XVT 0x99F9A0
MobileObjectLinkIndices g_mobileObjectLinkIndices[488] = {{0}};
/* Object type each slot was last spawned with, written by
 * Mission_InitFlightGroupObjectSlot; Mission_Init fills it with -1. Read
 * only by Object_RelinkMobileObjectPointers, in a branch that never runs. */
// GLOBAL: XVT 0x9A1090
int g_spawnObjectTypeByObjectSlot[488] = {0};
/* Entries in g_mobileObjectCharDataPool, 256 (CHAR_DATA_SLOT_COUNT); set by
 * Mission_Init and restored. */
// GLOBAL: XVT 0x9A7BA4
int g_mobileObjectCharDataCount = 0;
/* First debris slot, 192 (g_projectileObjectSlotEnd); set by Mission_Init
 * and restored. */
// GLOBAL: XVT 0x9A7B58
int g_debrisObjectSlotStart = 0;
/* The character records, g_mobileObjectCharDataCount of them, in the
 * locked memory of g_mobileObjectCharDataHandle; FeDiskIo_LockGlobalBuffers
 * sets it. In the modern build XvtFlightLoading_Reset sets NULL. */
// GLOBAL: XVT 0x9A8DA0
MobileObjectCharData *g_mobileObjectCharDataPool = 0;
/* One guidance record per shot slot, indexed by slot minus
 * g_projectileObjectSlotStart (g_projectileObjectSlotsTotal + 1 entries),
 * in the locked memory of g_warheadGuidancePoolHandle;
 * FeDiskIo_LockGlobalBuffers sets it. In the modern build
 * XvtFlightLoading_Reset sets NULL. */
// GLOBAL: XVT 0x9A8E18
WarheadGuidanceState *g_projectileGuidanceStates = 0;
/* Static slots after g_regionMainObjectSlotEnd, 64
 * (STATIC_OBJECT_SLOT_COUNT), set by Mission_Init and restored; -1 before
 * the first mission. */
// GLOBAL: XVT 0x9D1308
int g_regionStaticObjectSlotCount = -1;
/* One past the last debris slot, 208; set by Mission_Init and restored. */
// GLOBAL: XVT 0x9D1140
int g_debrisObjectSlotEnd = 0;
/* First of the 256 character slots, 224 (g_explosionObjectSlotEnd); set by
 * Mission_Init and restored. They are genus 16's range in
 * g_objectSlotRangeByGenus, and Flight_UpdateTimers counts down the AI
 * timers of any object in them. */
// GLOBAL: XVT 0x9D1144
int g_mobileObjectCharDataSlotStart = 0;
/* First explosion slot, 208 (g_debrisObjectSlotEnd); set by Mission_Init
 * and restored. */
// GLOBAL: XVT 0x9D1310
int g_explosionObjectSlotStart = 0;
/* One MobileObject per slot below g_regionMainObjectSlotEnd, in the locked
 * memory of g_mobileObjectPoolHandle; FeDiskIo_LockGlobalBuffers sets it.
 * In the modern build XvtFlightLoading_Reset sets NULL. */
// GLOBAL: XVT 0x9D6820
MobileObject *g_mobileObjectPoolBase = 0;
/* First shot slot, 32; set by Mission_Init and restored. Slots 32 to 159
 * hold player shots (12 per player, then 32 shared from slot 128), 160 to
 * 191 everyone else's. */
// GLOBAL: XVT 0x9E9640
int g_projectileObjectSlotStart = 0;
/* Number of local slots at the end of the main region: Mission_Init sets 8
 * (LOCAL_DEBRIS_SLOT_COUNT); it is restored. Only the world-state checksums
 * read it, and the modern build's check that a loaded world matches the live
 * slot ranges: the checksums cover slots 0 to g_regionMainObjectSlotEnd minus
 * this, which leaves out the 8 local slots, and mix in the value. */
// GLOBAL: XVT 0x9D767C
int g_localDebrisSlotCount = 0;
/* Shot slots in all, 160: 128 for players and 32 for the rest; set by
 * Mission_Init and restored. */
// GLOBAL: XVT 0x9FD430
unsigned int g_projectileObjectSlotsTotal = 0;
/* One past the last shot slot, 192; set by Mission_Init and restored. */
// GLOBAL: XVT 0xA00854
int g_projectileObjectSlotEnd = 0;
/* One past the last explosion slot, 224; set by Mission_Init and
 * restored. The map view walks slots 0 up to here. */
// GLOBAL: XVT 0x9EC47C
unsigned int g_explosionObjectSlotEnd = 0;
/* One past the last slot with a MobileObject, 488, set by Mission_Init and
 * restored; -1 before the first mission. The static slots follow it. */
// GLOBAL: XVT 0xA07CE8
int g_regionMainObjectSlotEnd = -1;
/* First craft slot, 0; set by Mission_Init and restored. */
// GLOBAL: XVT 0xA07CEC
int g_activeRegionObjectSlotStart = 0;
/* Debris slots in all, 16; set by Mission_Init and restored. Only the
 * world-state checksums and the modern build's range check read it. */
// GLOBAL: XVT 0xA0814C
unsigned int g_debrisObjectSlotsTotal = 0;
/* One past the last character slot, 480, where the local slots begin; set
 * by Mission_Init and restored. */
// GLOBAL: XVT 0x9FE7DC
int g_mobileObjectCharDataSlotEnd = 0;
/* The slot range Object_AllocSlotForGenus searches, per genus; only
 * Mission_Init writes it. It sets genera 0 to 5 to the craft slots, 6 to
 * the player shot slots (32 to 159), 7 to the other shot slots (160 to
 * 191), 11 to debris, 13 to explosions, 16 to the character slots, and 8
 * to 10, 12, 14 and 15 to empty; 17 to 19 stay 0. */
// GLOBAL: XVT 0xA082C0
ObjectSlotRange g_objectSlotRangeByGenus[20] = {{0}};

/* Advances every object in the mobile slots by one simulation step. First,
 * for each player flying a starfighter (remote players, then the local
 * one), moves hardpointWorld* to prevHardpointWorld* in g_players and stores
 * the craft's primary hardpoint world position in hardpointWorld*. Then,
 * for each slot below g_regionMainObjectSlotEnd, or only
 * g_singleObjectUpdateOverrideIdx when that is not -1: an object with its
 * own simStateTimestamp is stepped by its own elapsed ticks, which changes
 * g_elapsedTicks and g_simStepsPerSecond for that object (both are put back
 * on return), and is skipped when that comes to 0 (a player's craft still
 * gets prevWorld* updated); lifetimeTimer counts down, and at 0 the object
 * explodes or is removed by genus (a destroyed craft is recorded through
 * Mission_RecordCraftOutcome); prevWorld* takes the current position,
 * except while a single object is overridden; a nonzero rollImpulseRate
 * turns roll (and on a craft after an impact decays); then craft (genus 0
 * to 4), shots, small debris and explosions move along their move vector.
 * A craft with working systems adds its push accumulators, at most
 * maxPushRate per simulated second (250 while boarding, 750 while dropping
 * off, else the model's), and drags a carried object to its docking point.
 * A homing shot explodes when its target, in a mobile slot, is gone or its
 * slot reused; holds course while the target craft runs a decoy beam; and
 * otherwise turns toward the target (a mesh center on a craft) and changes
 * speed by the homing tables. In the modern build, timing-unlocked play
 * hands the integration to XvtFlightIntegration_*, and each object stepped
 * is reported to XvtReferenceMotion_Committed. Also writes
 * trig2_*movedist, g_rotated* and g_worldLoc*. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x445570
void Object_UpdateLifetimeAndMovement(void)
{
	enum {
		PLAYER_COUNT = 8,
		SPEED_TO_WORLD_SCALE = 4660,
		MOVE_VECTOR_SHIFT = 15,
		ROLL_IMPULSE_DECAY_SCALE = 1 << 12,
		ROLL_IMPULSE_ANGLE_SCALE = 4,
		BOARDING_PUSH_RATE = 250,
		DROPOFF_PUSH_RATE = 750,
		EXPLOSION_OBJECT_TYPE_FIRST = 127,
		EXPLOSION_OBJECT_TYPE_PROJECTILE = 129,
		EXPLOSION_OBJECT_TYPE_LARGE = 130,
		MODEL_LIGHT_SCALE_SHIFT = 9,
		HOMING_OBJECT_TYPE_FIRST = WARHEAD_OBJECT_TYPE_PROTON_TORPEDO,
		HOMING_MIN_TURN_SPEED = 200,
	};

	uint16_t playerIndex;
	uint16_t objectIndex;
	int savedSimStepsPerSecond;
	int savedElapsedTicks;
	int overrideProcessed;
	int objectOffsetIndex;

	for (playerIndex = 0; playerIndex < PLAYER_COUNT; ++playerIndex) {
		int playerObjectIndex;
		ObjectRecord *playerObject;
		ModelIndex playerModelIndex;

		if (g_players[playerIndex].participationState == 0 ||
		    playerIndex == g_localPlayer) {
			continue;
		}
		playerObjectIndex = g_players[playerIndex].objectIndex;
		if (playerObjectIndex == -1) {
			continue;
		}
		playerObject = &g_objectTable[playerObjectIndex];
		if (playerObject->genusId == CRAFT_GENUS_STARFIGHTER &&
		    (g_singleObjectUpdateOverrideIdx == -1 ||
		     g_singleObjectUpdateOverrideIdx == playerObjectIndex)) {
			playerModelIndex =
				GetModelIndexFromType(playerObject->objectType);
			pai_calcrotatedpoint(
				&g_objectTable[g_players[playerIndex]
						       .objectIndex],
				0,
				g_modelDefs[playerModelIndex].primaryHardpointZ,
				g_modelDefs[playerModelIndex]
					.primaryHardpointY);
			g_players[playerIndex].prevHardpointWorldX =
				g_players[playerIndex].hardpointWorldX;
			g_players[playerIndex].prevHardpointWorldY =
				g_players[playerIndex].hardpointWorldY;
			g_players[playerIndex].prevHardpointWorldZ =
				g_players[playerIndex].hardpointWorldZ;
			g_players[playerIndex].hardpointWorldX = g_rotatedX;
			g_players[playerIndex].hardpointWorldY = g_rotatedY;
			g_players[playerIndex].hardpointWorldZ = g_rotatedZ;
		}
	}

	playerIndex = (uint16_t)g_localPlayer;
	if (g_players[playerIndex].objectIndex != -1) {
		int playerObjectIndex = g_players[playerIndex].objectIndex;
		ObjectRecord *playerObject = &g_objectTable[playerObjectIndex];

		if (playerObject->genusId == CRAFT_GENUS_STARFIGHTER &&
		    (g_singleObjectUpdateOverrideIdx == -1 ||
		     g_singleObjectUpdateOverrideIdx == playerObjectIndex)) {
			ModelIndex playerModelIndex =
				GetModelIndexFromType(playerObject->objectType);

			pai_calcrotatedpoint(
				&g_objectTable[g_players[playerIndex]
						       .objectIndex],
				0,
				g_modelDefs[playerModelIndex].primaryHardpointZ,
				g_modelDefs[playerModelIndex]
					.primaryHardpointY);
			g_players[playerIndex].prevHardpointWorldX =
				g_players[playerIndex].hardpointWorldX;
			g_players[playerIndex].prevHardpointWorldY =
				g_players[playerIndex].hardpointWorldY;
			g_players[playerIndex].prevHardpointWorldZ =
				g_players[playerIndex].hardpointWorldZ;
			g_players[playerIndex].hardpointWorldX = g_rotatedX;
			g_players[playerIndex].hardpointWorldY = g_rotatedY;
			g_players[playerIndex].hardpointWorldZ = g_rotatedZ;
		}
	}

	savedSimStepsPerSecond = g_simStepsPerSecond;
	savedElapsedTicks = g_elapsedTicks;
	objectIndex = 0;
	overrideProcessed = 0;
	for (; objectIndex < g_regionMainObjectSlotEnd; ++objectIndex) {
		ObjectRecord *object;
		MobileObject *mobileObject;
		uint16_t genusId;
		uint16_t movementDistance;

		if (g_singleObjectUpdateOverrideIdx != -1) {
			if (overrideProcessed != 0) {
				break;
			}
			overrideProcessed = 1;
			objectIndex = (uint16_t)g_singleObjectUpdateOverrideIdx;
		}

		objectOffsetIndex = objectIndex;
		g_simStepsPerSecond = (uint16_t)savedSimStepsPerSecond;
		g_elapsedTicks = (uint16_t)savedElapsedTicks;
		object = &g_objectTable[objectOffsetIndex];
		mobileObject = object->mobj;
		if (mobileObject != NULL &&
		    mobileObject->simStateTimestamp != 0) {
			g_elapsedTicks =
				(uint16_t)(g_elapsedTicks + g_gameTime -
					   mobileObject->simStateTimestamp);
			if (g_elapsedTicks == 0) {
				if (g_singleObjectUpdateOverrideIdx == -1 &&
				    object->playerOwnerIdx != -1) {
					if (g_flightMissionState
							    .provingGroundsModeActive !=
						    0 &&
					    object->playerOwnerIdx ==
						    g_localPlayer) {
						ProvingGrounds_RecordLocalPlayerPoseHistory();
					}
					object->mobj->prevWorldX =
						object->world_x;
					object->mobj->prevWorldY =
						object->world_y;
					object->mobj->prevWorldZ =
						object->world_z;
				}
				continue;
			} else {
				g_simStepsPerSecond =
					(uint16_t)(SIMULATION_TICKS_PER_SECOND /
						   g_elapsedTicks);
				if (g_simStepsPerSecond == 0) {
					g_simStepsPerSecond = 1;
				}
				mobileObject->simStateTimestamp +=
					g_elapsedTicks;
			}
		}

		if (object->objectType == 0) {
			continue;
		}

		genusId = object->genusId;
		mobileObject = object->mobj;
		if (mobileObject->lifetimeTimer != 0) {
			uint16_t oldLifetime = mobileObject->lifetimeTimer;
			uint16_t newLifetime =
				(uint16_t)(oldLifetime - g_elapsedTicks);

			if (newLifetime > oldLifetime) {
				newLifetime = 0;
			}
			mobileObject->lifetimeTimer = newLifetime;
			if (newLifetime == 0) {
				switch (genusId) {
				case CRAFT_GENUS_STARFIGHTER:
					Craft_DetachDamageableComponent(
						objectIndex, 1);
					collide_ConvertObjectToExplosion(
						objectIndex,
						(GameRand() &
						 1) + EXPLOSION_OBJECT_TYPE_FIRST);
					Mission_RecordCraftOutcome(
						objectIndex,
						g_objectTable[objectOffsetIndex]
							.flightGroupIdx,
						FLIGHT_GROUP_OUTCOME_DESTROYED);
					break;
				case CRAFT_GENUS_TRANSPORT:
				case CRAFT_GENUS_UTILITY_VEHICLE:
				case CRAFT_GENUS_FREIGHTER:
				case CRAFT_GENUS_STARSHIP:
				case CRAFT_GENUS_PLATFORM:
					if (ModelMesh_HasFuselage(
						    object->objectType) == 0) {
						Craft_SpawnMainHullExplosionEffects(
							objectIndex, 1);
						collide_ConvertObjectToExplosion(
							objectIndex,
							EXPLOSION_OBJECT_TYPE_LARGE);
						g_objectTable[objectOffsetIndex]
							.mobj->effectSize =
							(uint8_t)(g_objectTypeTable
									  [object->objectType]
										  .maxBoundsExtent >>
								  MODEL_LIGHT_SCALE_SHIFT);
					} else {
						collide_ConvertObjectToExplosion(
							objectIndex,
							EXPLOSION_OBJECT_TYPE_LARGE);
					}
					Mission_RecordCraftOutcome(
						objectIndex,
						g_objectTable[objectOffsetIndex]
							.flightGroupIdx,
						FLIGHT_GROUP_OUTCOME_DESTROYED);
					break;
				case CRAFT_GENUS_PLAYER_PROJECTILE:
				case CRAFT_GENUS_OTHER_PROJECTILE:
					if (g_projectileTypeData.warheadClass
						    [object->objectType -
						     PROJECTILE_OBJECT_TYPE_FIRST] !=
					    0) {
						collide_ConvertObjectToExplosion(
							objectIndex,
							EXPLOSION_OBJECT_TYPE_PROJECTILE);
					} else {
						object->objectType = 0;
						continue;
					}
					break;
				case CRAFT_GENUS_SMALL_DEBRIS:
					collide_ConvertObjectToExplosion(
						objectIndex,
						(GameRand() &
						 1) + EXPLOSION_OBJECT_TYPE_FIRST);
					break;
				default:
					object->objectType = 0;
					continue;
				}
			}
		}

		mobileObject = object->mobj;
		if (g_singleObjectUpdateOverrideIdx == -1) {
			if (g_flightMissionState.provingGroundsModeActive !=
				    0 &&
			    g_objectTable[objectOffsetIndex].playerOwnerIdx ==
				    g_localPlayer) {
				ProvingGrounds_RecordLocalPlayerPoseHistory();
			}
			object->mobj->prevWorldX = object->world_x;
			object->mobj->prevWorldY = object->world_y;
			object->mobj->prevWorldZ = object->world_z;
		}

		mobileObject = object->mobj;
		{
			int16_t *rollImpulseRateField =
				&mobileObject->rollImpulseRate;
			int16_t rollImpulseRate = *rollImpulseRateField;

			if (rollImpulseRate != 0) {
				if (objectIndex <
				    g_activeRegionCraftObjectSlotEnd) {
					CraftData *craft = mobileObject->pCraft;

					if (craft->aiFlight.impactObjIdx !=
					    UINT16_MAX) {
						int16_t *
							updatedRollImpulseRateField;

						if (rollImpulseRate < 0) {
							*rollImpulseRateField =
								(int16_t)(rollImpulseRate +
#ifdef XVT_MODERN
									  (XvtFlightTiming_IsUnlocked()
										   ? XvtFlightIntegration_Rate(
											     objectIndex,
											     XVT_INTEGRATE_SPIN_DECAY,
											     ROLL_IMPULSE_DECAY_SCALE,
											     g_elapsedTicks,
											     236)
										   : ((g_elapsedTicks *
										       ROLL_IMPULSE_DECAY_SCALE) /
										      SIMULATION_TICKS_PER_SECOND))
#else
									  (g_elapsedTicks *
									   ROLL_IMPULSE_DECAY_SCALE) /
										  SIMULATION_TICKS_PER_SECOND
#endif
								);
							updatedRollImpulseRateField =
								&object->mobj
									 ->rollImpulseRate;
							if (*updatedRollImpulseRateField >=
							    0) {
								*updatedRollImpulseRateField =
									0;
								rollImpulseRate =
									object->mobj
										->rollImpulseRate;
								craft->aiFlight
									.impactObjIdx =
									UINT16_MAX;
							}
						} else {
							*rollImpulseRateField =
								(int16_t)(rollImpulseRate +
#ifdef XVT_MODERN
									  (XvtFlightTiming_IsUnlocked()
										   ? XvtFlightIntegration_Rate(
											     objectIndex,
											     XVT_INTEGRATE_SPIN_DECAY,
											     -ROLL_IMPULSE_DECAY_SCALE,
											     g_elapsedTicks,
											     236)
										   : ((g_elapsedTicks *
										       ROLL_IMPULSE_DECAY_SCALE) /
										      -SIMULATION_TICKS_PER_SECOND))
#else
									  (g_elapsedTicks *
									   ROLL_IMPULSE_DECAY_SCALE) /
										  -SIMULATION_TICKS_PER_SECOND
#endif
								);
							updatedRollImpulseRateField =
								&object->mobj
									 ->rollImpulseRate;
							if (*updatedRollImpulseRateField <=
							    0) {
								*updatedRollImpulseRateField =
									0;
								rollImpulseRate =
									object->mobj
										->rollImpulseRate;
								craft->aiFlight
									.impactObjIdx =
									UINT16_MAX;
							}
						}
					}
				}
				object->roll +=
					(int16_t)(ROLL_IMPULSE_ANGLE_SCALE *
						  (
#ifdef XVT_MODERN
							  (XvtFlightTiming_IsUnlocked()
								   ? XvtFlightIntegration_Rate(
									     objectIndex,
									     XVT_INTEGRATE_SPIN_ANGLE,
									     rollImpulseRate,
									     g_elapsedTicks,
									     236)
								   : g_elapsedTicks *
									     rollImpulseRate /
									     SIMULATION_TICKS_PER_SECOND)
#else
							  g_elapsedTicks *
							  rollImpulseRate /
							  SIMULATION_TICKS_PER_SECOND
#endif
								  ));
				mobileObject->orientMatrixDirty = 1;
			}
		}

		movementDistance = 0;
		mobileObject = object->mobj;
		if (mobileObject->speed != 0) {
			movementDistance =
				(uint16_t)(g_elapsedTicks *
					   ((SPEED_TO_WORLD_SCALE *
						     mobileObject->speed +
					     128) >>
					    8) /
					   SIMULATION_TICKS_PER_SECOND);
		}

		switch (genusId) {
		case CRAFT_GENUS_STARFIGHTER:
		case CRAFT_GENUS_TRANSPORT:
		case CRAFT_GENUS_UTILITY_VEHICLE:
		case CRAFT_GENUS_FREIGHTER:
		case CRAFT_GENUS_STARSHIP: {
			CraftData *craft = mobileObject->pCraft;

			if (mobileObject->moveVectorDirty != 0) {
				FVIEW_calcrotatemove(object->pitch, object->yaw,
						     object);
				mobileObject = object->mobj;
			}
#ifdef XVT_MODERN
			if (XvtFlightTiming_IsUnlocked()) {
				XvtFlightIntegration_Move(objectIndex);
			} else {
#endif

				trig2_xmovedist = Math_MulQ15(
					mobileObject->moveX, movementDistance);
				trig2_ymovedist = Math_MulQ15(
					mobileObject->moveY, movementDistance);
				trig2_zmovedist = Math_MulQ15(
					mobileObject->moveZ, movementDistance);

#ifdef XVT_MODERN
			}
#endif

			if (craft->workingSubsystems != 0) {
				int maxPushRate;
				int pushAccumulator;
				int16_t clampedPushRate;
				int pushStep;

				if (craft->aiController.maneuverMode ==
				    AI_MANEUVER_MODE_BOARD) {
					maxPushRate = BOARDING_PUSH_RATE;
				} else if (craft->aiController.maneuverMode ==
					   AI_MANEUVER_MODE_DROPOFF) {
					maxPushRate = DROPOFF_PUSH_RATE;
				} else {
					maxPushRate =
						g_modelDefs[craft->modelIndex]
							.maxPushRate;
				}

				pushAccumulator = craft->pushAccumX;
				if (pushAccumulator != 0) {
					clampedPushRate = (int16_t)maxPushRate;
					if (pushAccumulator < -maxPushRate) {
						clampedPushRate =
							(int16_t)-maxPushRate;
					} else if (pushAccumulator <=
						   maxPushRate) {
						clampedPushRate = (int16_t)
							pushAccumulator;
					}
#ifdef XVT_MODERN
					if (XvtFlightTiming_IsUnlocked()) {
						XvtFlightIntegration_Push(
							objectIndex, 0,
							&craft->pushAccumX,
							maxPushRate,
							&trig2_xmovedist);
					} else {
#endif

						pushStep =
							g_elapsedTicks *
							clampedPushRate /
							SIMULATION_TICKS_PER_SECOND;
						if (pushStep == 0) {
							pushStep =
								craft->pushAccumX;
						}
						craft->pushAccumX =
							pushAccumulator -
							pushStep;
						trig2_xmovedist += pushStep;

#ifdef XVT_MODERN
					}
#endif
				}

				pushAccumulator = craft->pushAccumY;
				if (pushAccumulator != 0) {
					clampedPushRate = (int16_t)maxPushRate;
					if (pushAccumulator < -maxPushRate) {
						clampedPushRate =
							(int16_t)-maxPushRate;
					} else if (pushAccumulator <=
						   maxPushRate) {
						clampedPushRate = (int16_t)
							pushAccumulator;
					}
#ifdef XVT_MODERN
					if (XvtFlightTiming_IsUnlocked()) {
						XvtFlightIntegration_Push(
							objectIndex, 1,
							&craft->pushAccumY,
							maxPushRate,
							&trig2_ymovedist);
					} else {
#endif

						pushStep =
							g_elapsedTicks *
							clampedPushRate /
							SIMULATION_TICKS_PER_SECOND;
						if (pushStep == 0) {
							pushStep =
								craft->pushAccumY;
						}
						craft->pushAccumY =
							pushAccumulator -
							pushStep;
						trig2_ymovedist += pushStep;

#ifdef XVT_MODERN
					}
#endif
				}

				pushAccumulator = craft->pushAccumZ;
				if (pushAccumulator != 0) {
					clampedPushRate = (int16_t)maxPushRate;
					if (pushAccumulator < -maxPushRate) {
						clampedPushRate =
							(int16_t)-maxPushRate;
					} else if (pushAccumulator <=
						   maxPushRate) {
						clampedPushRate = (int16_t)
							pushAccumulator;
					}
#ifdef XVT_MODERN
					if (XvtFlightTiming_IsUnlocked()) {
						XvtFlightIntegration_Push(
							objectIndex, 2,
							&craft->pushAccumZ,
							maxPushRate,
							&trig2_zmovedist);
					} else {
#endif

						pushStep =
							g_elapsedTicks *
							clampedPushRate /
							SIMULATION_TICKS_PER_SECOND;
						if (pushStep == 0) {
							pushStep =
								craft->pushAccumZ;
						}
						craft->pushAccumZ =
							pushAccumulator -
							pushStep;
						trig2_zmovedist += pushStep;

#ifdef XVT_MODERN
					}
#endif
				}
			}

			Object_AddTrigMoveDeltaAndClampWorldPosition(
				(uint32_t *)object);
			if (craft->carriedObjectIndex != UINT16_MAX) {
				uint16_t carriedObjectIndex =
					craft->carriedObjectIndex;
				ObjectRecord *carriedObject =
					&g_objectTable[carriedObjectIndex];

				if (carriedObject->mobj != NULL) {
					ModelIndex carriedModelIndex =
						carriedObject->mobj->pCraft
							->modelIndex;
					int16_t dockingForward =
						g_modelDefs[carriedModelIndex]
							.dockForward;
					int16_t dockingUpOffset;

					if (carriedObject->genusId <=
					    CRAFT_GENUS_UTILITY_VEHICLE) {
						dockingUpOffset =
							(int16_t)(g_modelDefs[craft->modelIndex]
									  .dockToUp
										  [0] -
								  g_modelDefs[carriedModelIndex]
									  .dockFromUp
										  [0]);
					} else if (
						object->genusId <=
						CRAFT_GENUS_UTILITY_VEHICLE) {
						dockingUpOffset =
							(int16_t)(g_modelDefs[craft->modelIndex]
									  .dockToUp
										  [1] -
								  g_modelDefs[carriedModelIndex]
									  .dockFromUp
										  [0]);
					} else {
						dockingUpOffset =
							(int16_t)(g_modelDefs[craft->modelIndex]
									  .dockToUp
										  [1] -
								  g_modelDefs[carriedModelIndex]
									  .dockFromUp
										  [1]);
					}
					pai_calcrotatedpoint(object, 0,
							     dockingUpOffset,
							     dockingForward);
					carriedObject =
						&g_objectTable
							[carriedObjectIndex];
					carriedObject->mobj->prevWorldX =
						carriedObject->world_x;
					carriedObject->mobj->prevWorldY =
						carriedObject->world_y;
					carriedObject->mobj->prevWorldZ =
						carriedObject->world_z;
					carriedObject->world_x =
						g_rotatedX + object->world_x;
					carriedObject->world_y =
						g_rotatedY + object->world_y;
					carriedObject->world_z =
						g_rotatedZ + object->world_z;
				}
			}
			break;
		}
		case CRAFT_GENUS_PLAYER_PROJECTILE:
		case CRAFT_GENUS_OTHER_PROJECTILE: {
			WarheadGuidanceState *guidance =
				mobileObject->pWarheadGuidance;

			if (
#ifdef XVT_MODERN
				/* Expiry can convert the object after genusId was cached for movement. */
				(object->genusId ==
					 CRAFT_GENUS_PLAYER_PROJECTILE ||
				 object->genusId ==
					 CRAFT_GENUS_OTHER_PROJECTILE) &&
#endif
				guidance->homingTier != 0 &&
				guidance->targetObjIdx != UINT16_MAX) {
				int targetObjectIndex = guidance->targetObjIdx;

				if (targetObjectIndex <
				    g_regionMainObjectSlotEnd) {
					ObjectRecord *targetObject =
						&g_objectTable
							[targetObjectIndex];

					if (targetObject->objectType == 0 ||
					    guidance->targetSignature !=
						    targetObject
							    ->objectSignature) {
						collide_ConvertObjectToExplosion(
							objectIndex,
							EXPLOSION_OBJECT_TYPE_PROJECTILE);
						break;
					}
				}
				{
					uint16_t targetComponentIndex =
						guidance->targetComponentIdx;
					uint16_t profileIndex =
						(uint16_t)(g_projectileHomingProfileBaseByObjectType
								   [object->objectType -
								    HOMING_OBJECT_TYPE_FIRST] +
							   guidance->homingTier);
					int decoyActive = 0;

					if (targetObjectIndex <
					    g_activeRegionCraftObjectSlotEnd) {
						CraftData *targetCraft =
							g_objectTable
								[targetObjectIndex]
									.mobj
									->pCraft;

						if (targetCraft->beamTypeId ==
							    BEAM_TYPE_DECOY &&
						    targetCraft->beamActive !=
							    0) {
							decoyActive = 1;
						}
					}
					if (decoyActive == 0) {
						ObjectRecord *targetObject;
						int targetObjectType;
						int centerX;
						int centerY;
						int centerZ;
						uint16_t oldYaw;
						uint16_t oldPitch;
						int16_t yawDelta;
						int16_t pitchDelta;
						int16_t absoluteDelta;
						int turnStep;

						Mission_ResolveObjectOrMissionPointWorldLoc(
							targetObjectIndex, 0);
						if (targetObjectIndex >=
							    g_activeRegionCraftObjectSlotEnd ||
						    g_objectTable[targetObjectIndex]
								    .mobj
								    ->family !=
							    0) {
							g_rotatedX =
								g_worldLocX;
							g_rotatedY =
								g_worldLocY;
							g_rotatedZ =
								g_worldLocZ;
						} else {
							targetObject =
								&g_objectTable
									[targetObjectIndex];
							if (targetComponentIndex ==
							    UINT16_MAX) {
								targetObjectType =
									targetObject
										->objectType;
								centerY = -ModelMesh_GetCenterY(
									targetObjectType,
									0);
								centerZ = ModelMesh_GetCenterZ(
									targetObjectType,
									0);
								centerX = ModelMesh_GetCenterX(
									targetObjectType,
									0);
							} else {
								targetObjectType =
									targetObject
										->objectType;
								centerY = -ModelMesh_GetCenterY(
									targetObjectType,
									targetComponentIndex);
								centerZ = ModelMesh_GetCenterZ(
									targetObjectType,
									targetComponentIndex);
								centerX = ModelMesh_GetCenterX(
									targetObjectType,
									targetComponentIndex);
							}
							pai_RotateLocalVectorToWorldScratch(
								targetObject,
								centerX,
								centerZ,
								centerY);
							g_rotatedX +=
								g_worldLocX;
							g_rotatedY +=
								g_worldLocY;
							g_rotatedZ +=
								g_worldLocZ;
						}
						Mission_ResolveObjectOrMissionPointWorldLoc(
							objectIndex, 0);
						g_rotatedX -= g_worldLocX;
						g_rotatedY -= g_worldLocY;
						g_rotatedZ -= g_worldLocZ;
						trig2_ctop(g_rotatedX,
							   g_rotatedY,
							   g_rotatedZ);
						oldYaw = object->yaw;

						yawDelta =
							(int16_t)(trig2_xyangle -
								  oldYaw);
						absoluteDelta = yawDelta;
#ifdef XVT_MODERN
						if (XvtFlightTiming_IsUnlocked()) {
							turnStep = XvtFlightIntegration_Rate(
								objectIndex,
								XVT_INTEGRATE_HOME_YAW,
								g_projectileHomingTurnRateByProfile
									[profileIndex],
								g_elapsedTicks,
								236);
						} else
#endif
							turnStep =
								g_elapsedTicks *
								g_projectileHomingTurnRateByProfile
									[profileIndex] /
								SIMULATION_TICKS_PER_SECOND;
						if (yawDelta < 0) {
							absoluteDelta =
								(int16_t)(oldYaw -
									  trig2_xyangle);
						}
						if ((uint16_t)turnStep >=
						    absoluteDelta) {
							MobileObject *
								homingMobileObject;
							uint16_t speed;

							object->yaw =
								trig2_xyangle;
#ifdef XVT_MODERN
							if (XvtFlightTiming_IsUnlocked()) {
								XvtFlightIntegration_Clear(
									objectIndex,
									XVT_INTEGRATE_HOME_YAW);
							}
#endif
							homingMobileObject =
								object->mobj;
							speed = homingMobileObject
									->speed;
							if (guidance->cruiseSpeed >
							    speed) {
								homingMobileObject
									->speed =
									(uint16_t)(speed +
#ifdef XVT_MODERN
										   (XvtFlightTiming_IsUnlocked()
											    ? XvtFlightIntegration_Rate(
												      objectIndex,
												      XVT_INTEGRATE_HOME_SPEED,
												      g_projectileHomingSpeedAdjustRateByProfile
													      [profileIndex],
												      g_elapsedTicks,
												      236)
											    : (g_elapsedTicks *
											       g_projectileHomingSpeedAdjustRateByProfile
												       [profileIndex] /
											       SIMULATION_TICKS_PER_SECOND))
#else
										   g_elapsedTicks *
											   g_projectileHomingSpeedAdjustRateByProfile
												   [profileIndex] /
											   SIMULATION_TICKS_PER_SECOND
#endif
									);
							}
						} else {
							if (yawDelta < 0) {
								turnStep =
									-turnStep;
							}
							object->yaw =
								(int16_t)(oldYaw +
									  turnStep);
							{
								uint16_t *speed =
									&object->mobj
										 ->speed;

								if (*speed >
								    HOMING_MIN_TURN_SPEED) {
									*speed =
										(uint16_t)(*speed +
#ifdef XVT_MODERN
											   (XvtFlightTiming_IsUnlocked()
												    ? XvtFlightIntegration_Rate(
													      objectIndex,
													      XVT_INTEGRATE_HOME_SPEED,
													      -g_projectileHomingSpeedAdjustRateByProfile
														      [profileIndex],
													      g_elapsedTicks,
													      236)
												    : (g_elapsedTicks *
												       g_projectileHomingSpeedAdjustRateByProfile
													       [profileIndex] /
												       -SIMULATION_TICKS_PER_SECOND))
#else
											   g_elapsedTicks *
												   g_projectileHomingSpeedAdjustRateByProfile
													   [profileIndex] /
												   -SIMULATION_TICKS_PER_SECOND
#endif
										);
									speed = &object->mobj
											 ->speed;
									if (*speed <
									    HOMING_MIN_TURN_SPEED) {
										*speed =
											HOMING_MIN_TURN_SPEED;
									}
								}
							}
						}

						oldPitch = object->pitch;
						pitchDelta =
							(int16_t)(trig2_pitch -
								  oldPitch);
						absoluteDelta = pitchDelta;
#ifdef XVT_MODERN
						if (XvtFlightTiming_IsUnlocked()) {
							turnStep = XvtFlightIntegration_Rate(
								objectIndex,
								XVT_INTEGRATE_HOME_PITCH,
								g_projectileHomingTurnRateByProfile
									[profileIndex],
								g_elapsedTicks,
								236);
						} else
#endif
							turnStep =
								g_elapsedTicks *
								g_projectileHomingTurnRateByProfile
									[profileIndex] /
								SIMULATION_TICKS_PER_SECOND;
						if (pitchDelta < 0) {
							absoluteDelta =
								(int16_t)(oldPitch -
									  trig2_pitch);
						}
						if ((uint16_t)turnStep >=
						    absoluteDelta) {
							object->pitch =
								trig2_pitch;
#ifdef XVT_MODERN
							if (XvtFlightTiming_IsUnlocked()) {
								XvtFlightIntegration_Clear(
									objectIndex,
									XVT_INTEGRATE_HOME_PITCH);
							}
#endif
						} else {
							if (pitchDelta < 0) {
								turnStep =
									-turnStep;
							}
							object->pitch =
								(int16_t)(oldPitch +
									  turnStep);
						}

						mobileObject
							->orientMatrixDirty = 1;
						mobileObject->moveVectorDirty =
							mobileObject
								->orientMatrixDirty;
						FVIEW_calcrotatemove(
							object->pitch,
							object->yaw, object);
						object->mobj->moveX = (int16_t)
							g_fviewMoveX_Q15;
						object->mobj->moveY = (int16_t)
							g_fviewMoveY_Q15;
						object->mobj->moveZ = (int16_t)
							g_fviewMoveZ_Q15;
					}
				}
			}

			mobileObject = object->mobj;
			if (mobileObject->moveVectorDirty != 0) {
				FVIEW_calcrotatemove(object->pitch, object->yaw,
						     object);
				mobileObject = object->mobj;
			}
#ifdef XVT_MODERN
			if (XvtFlightTiming_IsUnlocked()) {
				XvtFlightIntegration_Move(objectIndex);
			} else {
#endif

				trig2_xmovedist = Math_MulQ15(
					mobileObject->moveX, movementDistance);
				trig2_ymovedist = Math_MulQ15(
					mobileObject->moveY, movementDistance);
				trig2_zmovedist = Math_MulQ15(
					mobileObject->moveZ, movementDistance);

#ifdef XVT_MODERN
			}
#endif

			Object_AddTrigMoveDeltaAndClampWorldPosition(
				(uint32_t *)object);
			break;
		}
		case CRAFT_GENUS_SMALL_DEBRIS:
		case CRAFT_GENUS_EXPLOSION: {
			if (mobileObject->moveVectorDirty != 0) {
				FVIEW_calcrotatemove(object->pitch, object->yaw,
						     object);
				mobileObject = object->mobj;
			}
#ifdef XVT_MODERN
			if (XvtFlightTiming_IsUnlocked()) {
				XvtFlightIntegration_Move(objectIndex);
			} else {
#endif

				trig2_xmovedist = Math_MulQ15(
					mobileObject->moveX, movementDistance);
				trig2_ymovedist = Math_MulQ15(
					mobileObject->moveY, movementDistance);
				trig2_zmovedist = Math_MulQ15(
					mobileObject->moveZ, movementDistance);

#ifdef XVT_MODERN
			}
#endif

			Object_AddTrigMoveDeltaAndClampWorldPosition(
				(uint32_t *)object);
			break;
		}
		default:
			break;
		}
#ifdef XVT_MODERN
		XvtReferenceMotion_Committed(objectIndex,
					     g_gameTime + g_elapsedTicks);
#endif
	}

	g_simStepsPerSecond = (uint16_t)savedSimStepsPerSecond;
	g_elapsedTicks = (uint16_t)savedElapsedTicks;
}

/* Adds trig2_xmovedist, trig2_ymovedist and trig2_zmovedist to an object's
 * world_x, world_y and world_z (objectWords points at its ObjectRecord, read
 * as 32-bit words 1 to 3) and clamps each to 0x1000000 either way of 0.
 * Returns world_z after the lower clamp only, so a value above 0x1000000
 * comes back unclamped although the stored one is clamped; no caller uses
 * the result. */
// FUNCTION: XVT 0x4464C0
int Object_AddTrigMoveDeltaAndClampWorldPosition(uint32_t *objectWords)
{
	int32_t result;

	objectWords[1] += (uint32_t)trig2_xmovedist;
	if ((int32_t)objectWords[1] < -0x01000000) {
		objectWords[1] = (uint32_t)-0x01000000;
	}
	if ((int32_t)objectWords[1] > 0x01000000) {
		objectWords[1] = 0x01000000;
	}

	objectWords[2] += (uint32_t)trig2_ymovedist;
	if ((int32_t)objectWords[2] < -0x01000000) {
		objectWords[2] = (uint32_t)-0x01000000;
	}
	if ((int32_t)objectWords[2] > 0x01000000) {
		objectWords[2] = 0x01000000;
	}

	objectWords[3] += (uint32_t)trig2_zmovedist;
	if ((int32_t)objectWords[3] < -0x01000000) {
		objectWords[3] = (uint32_t)-0x01000000;
	}
	result = (int32_t)objectWords[3];
	if (result > 0x01000000) {
		objectWords[3] = 0x01000000;
	}
	return result;
}

/* Draws one object that is not a craft (debris, a mine, an effect, a
 * static object) for the current view, using the g_viewSpace* position and
 * object view matrix the caller set. With no texture frame sequence for its
 * type, it queues fuselage billboards (Damage_QueueCraftBillboards) and
 * draws the model, but only while typeSpecificByte[0] is 0. Otherwise the
 * frame at typeSpecificByte[0] in the sequence picks nothing (0xFF00 and
 * up), the model (below 0x8000), or a textured billboard of size 256 queued
 * at the projected point and turned to the object's roll on screen; the
 * billboard is skipped when the view depth is negative or the point falls
 * outside -65,536 to 65,535 on either axis. Writes
 * g_billboardObjectOrTypeIndex. */
// FUNCTION: XVT 0x446550
void RenderNonCraftSceneObject(uint16_t objectIndex)
{
	enum {
		NONCRAFT_MODEL_FRAME_LIMIT = 0x8000,
		NONCRAFT_INVALID_FRAME_START = 0xFF00,
		NONCRAFT_SCREEN_COORD_HIGH_MASK = -65536,
		NONCRAFT_DEFAULT_SCREEN_SIZE = 256,
	};

	ObjectRecord *object;
	ObjectTypeInfo *modelType;
	uint16_t rotationAngle;
	int16_t *textureFrameSequence;
	uint16_t frame;
	int absR0Z;
	int absR1Z;
	int axisX;
	int axisY;
	int projectedX;
	int projectedXHigh;
	int projectedY;
	int projectedYHigh;
	int viewportCenter;
	int screenY;

	object = &g_objectTable[objectIndex];
	modelType = &g_objectTypeTable[object->objectType];
	g_billboardObjectOrTypeIndex = objectIndex;
	textureFrameSequence = modelType->textureFrameSequence;
	if (textureFrameSequence == NULL) {
		if (object->typeSpecificByte[0] != 0) {
			return;
		}
		Damage_QueueCraftBillboards(objectIndex);
		RenderScene_DrawObjectModel(
			&g_objectTable[g_billboardObjectOrTypeIndex]);
		return;
	}

	frame = textureFrameSequence[object->typeSpecificByte[0]];
	if (frame >= NONCRAFT_INVALID_FRAME_START) {
		return;
	}
	if (frame < NONCRAFT_MODEL_FRAME_LIMIT) {
		RenderScene_DrawObjectModel(object);
		return;
	}
	if (g_viewSpaceDepth < 0) {
		return;
	}

	absR0Z = g_objViewMat_R0_Z;
	absR1Z = g_objViewMat_R1_Z;
	if (absR0Z < 0) {
		absR0Z = -absR0Z;
	}
	if (absR1Z < 0) {
		absR1Z = -absR1Z;
	}
	if (absR1Z > absR0Z) {
		axisX = g_objViewMat_R0_X;
		axisY = g_objViewMat_R0_Y;
	} else {
		axisX = g_objViewMat_R1_X;
		axisY = g_objViewMat_R1_Y;
	}
	if (axisX < 0) {
		rotationAngle = (uint16_t)trig2_arctan(axisY, -axisX);
	} else {
		rotationAngle = (uint16_t)-trig2_arctan(axisY, axisX);
	}

	projectedX = TRANSFM2_ProjectScreenX(g_viewSpaceX, g_viewSpaceDepth);
	projectedXHigh = projectedX & NONCRAFT_SCREEN_COORD_HIGH_MASK;
	if (projectedXHigh > 0 ||
	    projectedXHigh < NONCRAFT_SCREEN_COORD_HIGH_MASK) {
		return;
	}
	projectedY = TRANSFM2_ProjectScreenY(g_viewSpaceY, g_viewSpaceDepth);
	projectedYHigh = projectedY & NONCRAFT_SCREEN_COORD_HIGH_MASK;
	if (projectedYHigh > 0 ||
	    projectedYHigh < NONCRAFT_SCREEN_COORD_HIGH_MASK) {
		return;
	}
	viewportCenter = g_flightVpHeight >> 1;
	projectedY -= viewportCenter;
	screenY = viewportCenter - projectedY;
	SceneBillboard_QueueProjectedTextured(
		g_billboardObjectOrTypeIndex, frame,
		NONCRAFT_DEFAULT_SCREEN_SIZE, (int16_t)projectedX,
		(int16_t)screenY, g_viewSpaceDepth, rotationAngle);
}

/* Breaks a piece off the craft in sourceObjectIndex: a small-debris slot
 * takes the craft's position, angles and motion
 * (Object_CopyStatePreservingStorage) and becomes object type
 * CRAFT_SPECIES_COMPONENT, family 3, with no player owner, the craft's type
 * as sourceObjectType, twice meshIndex in typeSpecificByte[0] (the mesh to
 * draw) and a life of 4 to 11 simulated seconds. Returns the new slot, or
 * UINT16_MAX when no debris slot is free. Does not check meshIndex. */
// FUNCTION: XVT 0x4591C0
uint16_t Object_SpawnDetachedComponent(uint16_t sourceObjectIndex,
				       int16_t meshIndex)
{
	uint16_t objectIndex;
	int objectOffsetIndex;

	objectIndex = Object_AllocSlotForGenus(CRAFT_GENUS_SMALL_DEBRIS);
	if (objectIndex == UINT16_MAX) {
		return UINT16_MAX;
	}

	Object_CopyStatePreservingStorage(objectIndex, sourceObjectIndex);
	objectOffsetIndex = objectIndex;
	g_objectTable[objectOffsetIndex].mobj->family = 3;
	g_objectTable[objectOffsetIndex].genusId = CRAFT_GENUS_SMALL_DEBRIS;
	g_objectTable[objectOffsetIndex].mobj->effectSize = 0;
	g_objectTable[objectOffsetIndex].objectType = CRAFT_SPECIES_COMPONENT;
	g_objectTable[objectOffsetIndex].mobj->sourceObjectType =
		g_objectTable[sourceObjectIndex].objectType;
	g_objectTable[objectOffsetIndex].playerOwnerIdx = -1;
	g_objectTable[objectOffsetIndex].mobj->secondsAlive = 0;
	g_objectTable[objectOffsetIndex].mobj->lifetimeTimer =
		SIMULATION_TICKS_PER_SECOND * ((GameRand() & 7) + 4);
	g_objectTable[objectOffsetIndex].typeSpecificByte[0] =
		(uint8_t)(meshIndex * 2);
	g_objectTable[objectOffsetIndex].typeSpecificByte[1] = 0;

	return objectIndex;
}

/* Throws a fragment effect off sourceObjIdx: an explosion slot takes the
 * source's state and becomes an explosion (family 5) of object type 133 or
 * 134 at random, with no player owner, its yaw and pitch each turned by a
 * random 0x100 to 0x8FF either way, its speed raised by 50 to 305, and a
 * life of 1 to 4 simulated seconds. Returns the new slot, or UINT16_MAX
 * when no explosion slot is free. */
// FUNCTION: XVT 0x4592D0
uint16_t Object_SpawnEffectFragment(uint16_t sourceObjIdx)
{
	uint16_t objectIndex;
	int objectOffsetIndex;
	int16_t yawOffset;
	int16_t pitchOffset;
	uint16_t *pitch;
	MobileObject *mobileObject;

	objectIndex = Object_AllocSlotForGenus(CRAFT_GENUS_EXPLOSION);
	if (objectIndex == UINT16_MAX) {
		return UINT16_MAX;
	}

	Object_CopyStatePreservingStorage(objectIndex, sourceObjIdx);
	objectOffsetIndex = objectIndex;
	g_objectTable[objectOffsetIndex].mobj->family = 5;
	g_objectTable[objectOffsetIndex].genusId = CRAFT_GENUS_EXPLOSION;
	g_objectTable[objectOffsetIndex].mobj->effectSize = 0;
	g_objectTable[objectOffsetIndex].objectType =
		(uint8_t)((GameRand() & 1) - 123);
	g_objectTable[objectOffsetIndex].mobj->sourceObjectType =
		g_objectTable[sourceObjIdx].objectType;
	g_objectTable[objectOffsetIndex].playerOwnerIdx = -1;

	yawOffset = (GameRand() & 0x7FF) + 0x100;
	pitchOffset = (GameRand() & 0x7FF) + 0x100;
	if ((GameRand() & 1) != 0) {
		yawOffset = -yawOffset;
	}
	if ((GameRand() & 1) != 0) {
		pitchOffset = -pitchOffset;
	}
	g_objectTable[objectOffsetIndex].yaw += yawOffset;
	g_objectTable[objectOffsetIndex].pitch += pitchOffset;
	pitch = &g_objectTable[objectOffsetIndex].pitch;
	if (*pitch >= 0x8000) {
		*pitch = -*pitch;
		g_objectTable[objectOffsetIndex].yaw += 0x8000;
	}

	g_objectTable[objectOffsetIndex].mobj->orientMatrixDirty = 1;
	g_objectTable[objectOffsetIndex].mobj->moveVectorDirty =
		g_objectTable[objectOffsetIndex].mobj->orientMatrixDirty;
	mobileObject = g_objectTable[objectOffsetIndex].mobj;
	mobileObject->speed += (GameRand() & 0xFF) + 50;
	g_objectTable[objectOffsetIndex].mobj->secondsAlive = 0;
	g_objectTable[objectOffsetIndex].mobj->lifetimeTimer =
		SIMULATION_TICKS_PER_SECOND * ((GameRand() & 3) + 1);
	g_objectTable[objectOffsetIndex].typeSpecificByte[0] = 0;

	return objectIndex;
}

/* Like Object_SpawnEffectFragment, but the fragment is object type 157
 * ((uint8_t)-99) with effectSize 2 and typeSpecificByte[0] 2; it faces back
 * along the source (a half turn of yaw, pitch mirrored), turned by a random
 * 0x100 to 0x20FF either way on each angle, flies at speed 35 to 50 for 39 to
 * 42 ticks, and is moved at once by four times the distance one step at
 * g_simStepsPerSecond covers. Returns the new slot, or UINT16_MAX when no
 * explosion slot is free. Its only caller, FlightObject_UpdateSpecialBehavior,
 * makes three at a time for an active craft whose chaff is active. */
// FUNCTION: XVT 0x459480
uint16_t Object_SpawnLocalEffectFragment(uint16_t sourceObjIdx)
{
	uint16_t objectIndex;
	int objectOffsetIndex;
	int16_t yawOffset;
	int16_t pitchOffset;
	ObjectRecord *object;
	uint16_t speedPerFrame;

	objectIndex = Object_AllocSlotForGenus(CRAFT_GENUS_EXPLOSION);
	if (objectIndex == UINT16_MAX) {
		return UINT16_MAX;
	}

	Object_CopyStatePreservingStorage(objectIndex, sourceObjIdx);
	objectOffsetIndex = objectIndex;
	g_objectTable[objectOffsetIndex].mobj->family = 5;
	g_objectTable[objectOffsetIndex].genusId = CRAFT_GENUS_EXPLOSION;
	g_objectTable[objectOffsetIndex].mobj->effectSize = 2;
	g_objectTable[objectOffsetIndex].objectType = (uint8_t)-99;
	g_objectTable[objectOffsetIndex].mobj->sourceObjectType =
		g_objectTable[sourceObjIdx].objectType;
	g_objectTable[objectOffsetIndex].playerOwnerIdx = -1;

	yawOffset = (GameRand() & 0x1FFF) + 0x100;
	pitchOffset = (GameRand() & 0x1FFF) + 0x100;
	if ((GameRand() & 1) != 0) {
		yawOffset = -yawOffset;
	}
	if ((GameRand() & 1) != 0) {
		pitchOffset = -pitchOffset;
	}
	g_objectTable[objectOffsetIndex].yaw += 0x8000;
	g_objectTable[objectOffsetIndex].yaw += yawOffset;
	g_objectTable[objectOffsetIndex].pitch =
		(int16_t)((uint16_t)0x8000 -
			  g_objectTable[objectOffsetIndex].pitch);
	g_objectTable[objectOffsetIndex].pitch += pitchOffset;
	if (g_objectTable[objectOffsetIndex].pitch >= 0x8000) {
		g_objectTable[objectOffsetIndex].pitch =
			-g_objectTable[objectOffsetIndex].pitch;
		g_objectTable[objectOffsetIndex].yaw += 0x8000;
	}

	g_objectTable[objectOffsetIndex].mobj->orientMatrixDirty = 1;
	g_objectTable[objectOffsetIndex].mobj->moveVectorDirty =
		g_objectTable[objectOffsetIndex].mobj->orientMatrixDirty;
	g_objectTable[objectOffsetIndex].mobj->speed = (GameRand() & 0xF) + 35;
	g_objectTable[objectOffsetIndex].mobj->secondsAlive = 0;
	g_objectTable[objectOffsetIndex].mobj->lifetimeTimer =
		(GameRand() & 3) + 39;
	g_objectTable[objectOffsetIndex].typeSpecificByte[0] = 2;

	object = &g_objectTable[objectOffsetIndex];
	object->mobj->prevWorldX = object->world_x;
	object->mobj->prevWorldY = object->world_y;
	object->mobj->prevWorldZ = object->world_z;
	speedPerFrame =
		MATH2_mphconvert(object->mobj->speed, g_simStepsPerSecond);
	if (speedPerFrame != 0) {
		int movementSpeed;
		int zMove;

		if (object->mobj->moveVectorDirty != 0) {
			FVIEW_calcrotatemove(object->pitch, object->yaw,
					     object);
		}
		movementSpeed = speedPerFrame;
		trig2_xmovedist =
			Math_MulQ15(object->mobj->moveX, movementSpeed);
		trig2_ymovedist =
			Math_MulQ15(object->mobj->moveY, movementSpeed);
		zMove = Math_MulQ15(object->mobj->moveZ, movementSpeed);
		trig2_xmovedist *= 4;
		trig2_ymovedist *= 4;
		trig2_zmovedist = 4 * zMove;
		Object_AddTrigMoveDeltaAndClampWorldPosition(
			(uint32_t *)object);
	}

	return objectIndex;
}

/* Finds the first free slot (objectType 0) in genusId's range in
 * g_objectSlotRangeByGenus, sets its mobj->sourceObjIdx and effectSize to 0,
 * and resets its proximity lists (collide_ResetObjectProximityForSlot; the
 * modern build also calls XvtFlightIntegration_ResetSlotAndMotion). Returns
 * the slot, or UINT16_MAX when the range is full or empty. Does not check
 * genusId. */
// FUNCTION: XVT 0x459750
uint16_t Object_AllocSlotForGenus(uint16_t genusId)
{
	uint16_t end;
	uint16_t objectIndex;

	end = g_objectSlotRangeByGenus[genusId].end;
	objectIndex = g_objectSlotRangeByGenus[genusId].start;
	if (end > objectIndex) {
		for (;;) {
			if (g_objectTable[objectIndex].objectType == 0) {
				g_objectTable[objectIndex].mobj->sourceObjIdx =
					0;
				g_objectTable[objectIndex].mobj->effectSize = 0;
				break;
			}
			++objectIndex;
			if (end <= objectIndex) {
				break;
			}
		}
	}

	if (end > objectIndex) {
		collide_ResetObjectProximityForSlot(objectIndex);
#ifdef XVT_MODERN
		XvtFlightIntegration_ResetSlotAndMotion(objectIndex);
#endif
		return objectIndex;
	}

	return UINT16_MAX;
}

/* Returns the first free static slot (objectType 0) of the
 * g_regionStaticObjectSlotCount after g_regionMainObjectSlotEnd, or
 * UINT16_MAX when all are used. */
// FUNCTION: XVT 0x4597F0
uint16_t Object_FindFreeMissionSlot(void)
{
	uint16_t objectIndex;
	int mainObjectSlotEnd;
	int missionSlotEnd;

	objectIndex = (uint16_t)g_regionMainObjectSlotEnd;
	mainObjectSlotEnd = g_regionMainObjectSlotEnd;
	missionSlotEnd = g_regionStaticObjectSlotCount;
	missionSlotEnd += mainObjectSlotEnd;
	while (objectIndex < missionSlotEnd) {
		if (g_objectTable[objectIndex].objectType == 0) {
			return objectIndex;
		}
		++objectIndex;
	}
	return UINT16_MAX;
}

/* Copies the object in srcObjIdx onto dstObjIdx: the contents of its craft,
 * guidance and character records where both slots have one, its
 * MobileObject where both have one, and its ObjectRecord; the destination
 * keeps its own mobj, pCraft, pWarheadGuidance and pCharData pointers. Then
 * resets the destination's proximity lists; the modern build first calls
 * XvtFlightIntegration_ResetSlotAndMotion. Does not check that the
 * destination has a MobileObject. */
// FUNCTION: XVT 0x459F30
void Object_CopyStatePreservingStorage(unsigned int dstObjIdx,
				       unsigned int srcObjIdx)
{
	CraftData *destinationCraft;
	CraftData *sourceCraft;
	WarheadGuidanceState *destinationGuidance;
	WarheadGuidanceState *sourceGuidance;
	MobileObjectCharData *destinationCharData;
	MobileObjectCharData *sourceCharData;
	MobileObject *destinationMobileObject;
	MobileObject *sourceMobileObject;
	CraftData *preservedCraft;
	WarheadGuidanceState *preservedGuidance;
	MobileObjectCharData *preservedCharData;
	ObjectRecord *destinationObject;
	ObjectRecord *sourceObject;

#ifdef XVT_MODERN
	XvtFlightIntegration_ResetSlotAndMotion(dstObjIdx);
#endif

	destinationCraft = g_objectTable[dstObjIdx].mobj->pCraft;
	if (destinationCraft != NULL) {
		sourceCraft = g_objectTable[srcObjIdx].mobj->pCraft;
		if (sourceCraft != NULL) {
			memcpy(destinationCraft, sourceCraft,
			       sizeof(*destinationCraft));
		}
	}

	destinationGuidance = g_objectTable[dstObjIdx].mobj->pWarheadGuidance;
	if (destinationGuidance != NULL) {
		sourceGuidance =
			g_objectTable[srcObjIdx].mobj->pWarheadGuidance;
		if (sourceGuidance != NULL) {
			*destinationGuidance = *sourceGuidance;
		}
	}

	destinationCharData = g_objectTable[dstObjIdx].mobj->pCharData;
	if (destinationCharData != NULL) {
		sourceCharData = g_objectTable[srcObjIdx].mobj->pCharData;
		if (sourceCharData != NULL) {
			memcpy(destinationCharData, sourceCharData,
			       sizeof(*destinationCharData));
		}
	}

	destinationMobileObject = g_objectTable[dstObjIdx].mobj;
	if (destinationMobileObject != NULL) {
		sourceMobileObject = g_objectTable[srcObjIdx].mobj;
		if (sourceMobileObject != NULL) {
			preservedCraft = destinationMobileObject->pCraft;
			preservedGuidance =
				destinationMobileObject->pWarheadGuidance;
			preservedCharData = destinationMobileObject->pCharData;
			memcpy(destinationMobileObject, sourceMobileObject,
			       sizeof(*destinationMobileObject));
			g_objectTable[dstObjIdx].mobj->pCraft = preservedCraft;
			g_objectTable[dstObjIdx].mobj->pWarheadGuidance =
				preservedGuidance;
			g_objectTable[dstObjIdx].mobj->pCharData =
				preservedCharData;
		}
	}

	destinationObject = &g_objectTable[dstObjIdx];
	destinationMobileObject = destinationObject->mobj;
	sourceObject = &g_objectTable[srcObjIdx];
	memcpy(destinationObject, sourceObject, sizeof(*destinationObject));
	g_objectTable[dstObjIdx].mobj = destinationMobileObject;
	collide_ResetObjectProximityForSlot((uint16_t)dstObjIdx);
}

/* Points mobj of each slot below g_regionMainObjectSlotEnd at its entry in
 * g_mobileObjectPoolBase. A second pass would point pWarheadGuidance,
 * pCraft or pCharData at the pool entries named in
 * g_mobileObjectLinkIndices, but every index there is -1, so it links
 * nothing. */
// FUNCTION: XVT 0x45A0C0
void Object_RelinkMobileObjectPointers(void)
{
	int objectIndex;
	MobileObjectLinkIndices *linkIndices;
	int linkedObjectIndex;

	objectIndex = 0;
	if (g_regionMainObjectSlotEnd > 0) {
		do {
			g_objectTable[objectIndex].mobj =
				&g_mobileObjectPoolBase[objectIndex];
			++objectIndex;
		} while (g_regionMainObjectSlotEnd > objectIndex);
	}

	objectIndex = 0;
	if (g_regionMainObjectSlotEnd > 0) {
		linkIndices = g_mobileObjectLinkIndices;
		do {
			if (linkIndices->warheadGuidanceIdx != -1) {
				g_mobileObjectPoolBase[objectIndex]
					.pWarheadGuidance =
					&g_projectileGuidanceStates
						[linkIndices
							 ->warheadGuidanceIdx];
			} else if (linkIndices->craftDataIdx != -1) {
				g_mobileObjectPoolBase[objectIndex].pCraft =
					&g_craftDataPoolBase
						[linkIndices->craftDataIdx];
				/* Nothing in this build sets craftDataIdx, so this branch never runs. The table it reads
				 * holds the object type each slot was spawned with, not an object index. */
				linkedObjectIndex =
					g_spawnObjectTypeByObjectSlot
						[linkIndices->craftDataIdx];
				if (linkedObjectIndex != -1) {
					g_mobileObjectPoolBase[objectIndex]
						.pCraft->effectiveAiObjectLink =
						&g_objectTable
							[linkedObjectIndex];
				}
			} else if (linkIndices->charDataIdx != -1) {
				g_mobileObjectPoolBase[objectIndex].pCharData =
					&g_mobileObjectCharDataPool
						[linkIndices->charDataIdx];
			}
			++linkIndices;
			++objectIndex;
		} while (g_regionMainObjectSlotEnd > objectIndex);
	}
}

/* Measures from the object in fromObjIdx to the center of mesh meshIdx of
 * targetObjIdx, turned by the target's orientation: leaves the direction
 * and distance in trig2_ctop's outputs (trig2_xyangle, trig2_pitch,
 * trig2_polardistance) and returns collide_roughdistance3d of the offset.
 * Writes g_worldLoc* and g_rotated*. Does not check meshIdx. */
// FUNCTION: XVT 0x4836E0
unsigned int Object_DirectionAndDistanceToMeshCenter(uint16_t fromObjIdx,
						     uint16_t targetObjIdx,
						     unsigned int meshIdx)
{
	/* Resolve, rotate, and measure the target mesh center. */
	ObjectRecord *fromObject = &g_objectTable[fromObjIdx];
	int originX = fromObject->world_x;
	int originY = fromObject->world_y;
	int originZ = fromObject->world_z;
	int objectType;
	int deltaX;
	int deltaY;
	int deltaZ;
	int targetWorldX;
	int targetWorldY;
	int targetWorldZ;

	Mission_ResolveObjectOrMissionPointWorldLoc(targetObjIdx, 0);
	targetWorldX = g_worldLocX;
	targetWorldY = g_worldLocY;
	targetWorldZ = g_worldLocZ;
	objectType = g_objectTable[targetObjIdx].objectType;
	g_rotatedX = ModelMesh_GetCenterX(objectType, meshIdx);
	g_rotatedY = ModelMesh_GetCenterZ(objectType, meshIdx);
	g_rotatedZ = -ModelMesh_GetCenterY(objectType, meshIdx);
	pai_RotateLocalVectorToWorldScratch(&g_objectTable[targetObjIdx],
					    g_rotatedX, g_rotatedY, g_rotatedZ);

	deltaX = g_rotatedX + targetWorldX - originX;
	deltaY = g_rotatedY + targetWorldY - originY;
	deltaZ = g_rotatedZ + targetWorldZ - originZ;
	trig2_ctop(deltaX, deltaY, deltaZ);
	return (unsigned int)collide_roughdistance3d(deltaX, deltaY, deltaZ);
}

/* Returns 1 when objIdx is a player's craft whose beam system works and is
 * on, is the decoy beam, and has output; else 0, including for UINT16_MAX,
 * slots past the craft slots, AI craft and objects with no craft record. */
// FUNCTION: XVT 0x484F80
uint8_t Object_HasActiveDecoyBeam(uint16_t objIdx)
{
	CraftData *craft;

	if (objIdx == UINT16_MAX) {
		return 0;
	}
	if (objIdx >= g_activeRegionCraftObjectSlotEnd) {
		return 0;
	}

	if (g_objectTable[objIdx].playerOwnerIdx == -1) {
		return 0;
	}

	craft = g_objectTable[objIdx].mobj->pCraft;
	if (craft == NULL) {
		return 0;
	}

	if ((craft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) ==
		    0 ||
	    craft->beamActive == 0 || craft->beamTypeId != BEAM_TYPE_DECOY ||
	    craft->beamOutput == 0) {
		return 0;
	}

	return 1;
}
