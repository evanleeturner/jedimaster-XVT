#include "xvt/flight/object/static.h"

#include "xvt/assets/object_type.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/util/game_rand.h"

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x446700
uint16_t static_laserstaticcollide(uint16_t sourceObjIdx, uint16_t staticObjIdx) {
	enum { MAX_DISTANCE = 0x20000, LARGE_MODEL_EXTENT = 1095 };

	int sourceSourceObjIdx;
	unsigned int staticGenusId;
	int staticObjectType;
	int hitRadius;
	int dx;
	int dy;
	int dz;
	unsigned int sourceToStaticDistance;
	int staticSweepAbs;

	sourceSourceObjIdx = (uint16_t)g_objectTable[sourceObjIdx].mobj->sourceObjIdx;
	if (sourceSourceObjIdx >= staticObjIdx)
		return 0;

	staticGenusId = g_objectTable[staticObjIdx].genusId;
	if (staticGenusId == CRAFT_GENUS_OBSTACLE)
		return 0;
	if (staticGenusId == CRAFT_GENUS_SMALL_DEBRIS)
		return 0;
	if (staticGenusId != CRAFT_GENUS_NORMAL_DEBRIS &&
		g_activeRegionCraftObjectSlotEnd > (int)sourceSourceObjIdx) {
		if (g_objectTable[sourceSourceObjIdx].playerOwnerIdx == -1 &&
			g_objectTable[sourceSourceObjIdx].mobj->pCraft->aiController.targetObjIdx != staticObjIdx)
			return 0;
	}

	staticObjectType = g_objectTable[staticObjIdx].objectType;
	Mission_ResolveObjectOrMissionPointWorldLoc(staticObjIdx, 0);
	g_collisionSweepStartX = worldlocx;
	g_collisionSweepEndX = worldlocx;
	g_collisionSweepStartY = worldlocy;
	g_collisionSweepEndY = worldlocy;
	g_collisionSweepStartZ = worldlocz;
	g_collisionSweepEndZ = worldlocz;

	hitRadius = g_modelTypeTable[staticObjectType].maxBoundsExtent;
	dx = g_collisionProbeWorldX - worldlocx;
	if (dx < 0)
		dx = -dx;
	if (dx > MAX_DISTANCE)
		return 0;
	dy = g_collisionProbeWorldY - worldlocy;
	if (dy < 0)
		dy = -dy;
	if (dy > MAX_DISTANCE)
		return 0;
	dz = g_collisionProbeWorldZ - worldlocz;
	if (dz < 0)
		dz = -dz;
	if (dz > MAX_DISTANCE)
		return 0;

	sourceToStaticDistance = collide_roughdistance3du((unsigned int)dx, (unsigned int)dy, (unsigned int)dz);
	if ((int)sourceToStaticDistance > MAX_DISTANCE)
		return 0;

	dx = g_collisionProbeWorldX - g_collisionSegmentStartWorldX;
	if (dx < 0)
		dx = -dx;
	dy = g_collisionProbeWorldY - g_collisionSegmentStartWorldY;
	if (dy < 0)
		dy = -dy;
	dz = g_collisionProbeWorldZ - g_collisionSegmentStartWorldZ;
	if (dz < 0)
		dz = -dz;

	staticSweepAbs = g_collisionSweepEndX - g_collisionSweepStartX;
	if (staticSweepAbs < 0)
		staticSweepAbs = -staticSweepAbs;
	dx += staticSweepAbs;
	staticSweepAbs = g_collisionSweepEndY - g_collisionSweepStartY;
	if (staticSweepAbs < 0)
		staticSweepAbs = -staticSweepAbs;
	dy += staticSweepAbs;
	staticSweepAbs = g_collisionSweepEndZ - g_collisionSweepStartZ;
	if (staticSweepAbs < 0)
		staticSweepAbs = -staticSweepAbs;
	dz += staticSweepAbs;

	dx += hitRadius;
	dy += hitRadius;
	dz += hitRadius;
	if (collide_roughdistance3du((unsigned int)dx, (unsigned int)dy, (unsigned int)dz) <
		sourceToStaticDistance)
		return 0;

	if (hitRadius >= LARGE_MODEL_EXTENT || g_objectTable[staticObjIdx].objectType == MODEL_058_CONTAINER_I)
		return (uint16_t)collide_CheckSweptModelCollision(sourceObjIdx, staticObjIdx);
	hitRadius >>= 2;
	return (uint16_t)collide_checkboxcollision(hitRadius + (hitRadius >> 1));
}

// FUNCTION: XVT 0x446960
void static_laserhitstatic(uint16_t sourceObjIdx, int victimObjIdx) {
	enum {
		EFFECT_TYPE_DEFAULT = 0x81,
		EFFECT_TYPE_LASER_IMPACT = 0x83,
		EFFECT_TYPE_ION_IMPACT = 0x84,
		STATIC_LAUNCHER_OBJECT_TYPE = 77,
		TEMPORARY_PROJECTILE_OBJECT_TYPE = PROJECTILE_OBJECT_TYPE_REBEL_LASER,
		RANDOM_IMPACT_SOUND_COUNT = 4,
		IMPACT_STATE = 5,
		IMPACT_VARIANT = 2,
	};

	uint16_t effectType;
	uint16_t victimIndex = (uint16_t)victimObjIdx;

	if (g_objectTable[victimIndex].genusId == CRAFT_GENUS_NORMAL_DEBRIS) {
		if (g_objectTable[sourceObjIdx].objectType != PROJECTILE_OBJECT_TYPE_ION_LASER &&
			g_objectTable[sourceObjIdx].objectType != PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER) {
			effectType = EFFECT_TYPE_LASER_IMPACT;
		} else {
			effectType = EFFECT_TYPE_ION_IMPACT;
		}
	} else if (g_activeRegionCraftObjectSlotEnd > sourceObjIdx) {
		++g_missionFgStats[g_objectTable[victimIndex].flightGroupIdx]
			  .outcomeCount[FLIGHT_GROUP_OUTCOME_DESTROYED];
		effectType = EFFECT_TYPE_DEFAULT;
		g_objectTable[victimIndex].objectType = 0;
		Mission_CreditDestructionDamageContributors(sourceObjIdx, victimObjIdx);
	} else {
		if (g_objectTable[sourceObjIdx].objectType == PROJECTILE_OBJECT_TYPE_ION_LASER ||
			g_objectTable[sourceObjIdx].objectType == PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER) {
			g_objectTable[victimIndex].typeSpecificWord = 0;
			++g_missionFgStats[g_objectTable[victimIndex].flightGroupIdx]
				  .outcomeCount[FLIGHT_GROUP_OUTCOME_DISABLED];
			effectType = EFFECT_TYPE_ION_IMPACT;
		} else {
			++g_missionFgStats[g_objectTable[victimIndex].flightGroupIdx]
				  .outcomeCount[FLIGHT_GROUP_OUTCOME_DESTROYED];
			effectType = EFFECT_TYPE_DEFAULT;
			if (g_objectTable[victimIndex].objectType == STATIC_LAUNCHER_OBJECT_TYPE) {
				laser_createprojectilefromstatic(victimObjIdx,
												 g_objectTable[sourceObjIdx].mobj->sourceObjIdx);
			}
			g_objectTable[victimIndex].objectType = 0;
			Mission_CreditDestructionDamageContributors(g_objectTable[sourceObjIdx].mobj->sourceObjIdx,
														victimObjIdx);
		}
	}

	if (sourceObjIdx < g_activeRegionCraftObjectSlotEnd) {
		sourceObjIdx = Object_AllocSlotForGenus(CRAFT_GENUS_EXPLOSION);
		if (sourceObjIdx == UINT16_MAX) {
			return;
		}
		g_objectTable[sourceObjIdx].objectType = TEMPORARY_PROJECTILE_OBJECT_TYPE;
	}

	g_objectTable[sourceObjIdx].world_x = g_collisionSegmentStartWorldX + g_collisionHitOffsetX;
	g_objectTable[sourceObjIdx].world_y = g_collisionSegmentStartWorldY + g_collisionHitOffsetY;
	g_objectTable[sourceObjIdx].world_z = g_collisionSegmentStartWorldZ + g_collisionHitOffsetZ;
	if (g_projectileDamageByObjectType
			.warheadClass[g_objectTable[sourceObjIdx].objectType - PROJECTILE_OBJECT_TYPE_FIRST] != 0) {
		effectType = EFFECT_TYPE_DEFAULT;
	}
	g_objectTable[sourceObjIdx].objectType = (uint8_t)effectType;
	g_objectTable[sourceObjIdx].genusId = CRAFT_GENUS_EXPLOSION;
	g_objectTable[sourceObjIdx].mobj->state = IMPACT_STATE;
	g_objectTable[sourceObjIdx].typeSpecificByte[0] = IMPACT_VARIANT;
	g_objectTable[sourceObjIdx].mobj->speed = 0;
	g_objectTable[sourceObjIdx].mobj->lightIntensityScale = 0;
	g_objectTable[sourceObjIdx].mobj->framesAlive = 0;
	g_objectTable[sourceObjIdx].mobj->lifetimeTimer = 0;
	g_objectTable[sourceObjIdx].pitch = 0;
	g_objectTable[sourceObjIdx].yaw = 0;
	g_objectTable[sourceObjIdx].roll = 0;
	g_objectTable[sourceObjIdx].mobj->orientMatrixDirty = 1;
	g_objectTable[sourceObjIdx].mobj->moveVectorDirty = g_objectTable[sourceObjIdx].mobj->orientMatrixDirty;
	if (effectType == EFFECT_TYPE_LASER_IMPACT || effectType == EFFECT_TYPE_ION_IMPACT) {
		fsfx_PlaySound(FLIGHT_SOUND_LASER_IMPACT, sourceObjIdx, g_localPlayer);
	} else {
		fsfx_PlaySound(
			(uint16_t)((GameRand2() & (RANDOM_IMPACT_SOUND_COUNT - 1)) + FLIGHT_SOUND_SMALL_EXPLOSION_FIRST),
			sourceObjIdx, g_localPlayer);
	}
}
