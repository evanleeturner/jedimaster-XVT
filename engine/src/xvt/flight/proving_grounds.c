#include "xvt/flight/proving_grounds.h"
#ifdef XVT_MODERN
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/player_timing.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/cockpit_readouts.h"
#include "xvt_runtime/snapshot/cockpit_text.h"
#endif
#include "xvt/assets/model_bounds.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math.h"
#include "xvt/net/flight_net.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt/util/time.h"

/* Place values for ProvingGrounds_DrawScoreDecimal, indexed by the digit places
 * left to draw: entry n is 10 to the power n - 1; entry 0 is never read. Only
 * that function reads it. */
// GLOBAL: XVT 0x520EF0
int g_provingGroundsScoreDecimalDivisors[9] = {
	1, 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000};
/* Ticks left before the course animation frame steps. Only
 * ProvingGrounds_UpdateCourse writes it: it counts down by g_elapsedTicks and,
 * below 0, adds 29. */
// GLOBAL: XVT 0x520EE8
static int g_provingGroundsCourseAnimTimer = 0;
/* Course animation frame, 0 to 3, stepped every 29 ticks and shown as each
 * course object's nodeSwitchIndex. Only ProvingGrounds_UpdateCourse writes
 * it. */
// GLOBAL: XVT 0x520EEC
static int g_provingGroundsCourseAnimFrame = 0;
/* Ticks per obstacle animation step, by proving grounds level 0 to 19, shorter
 * at higher levels. */
// GLOBAL: XVT 0x5234C8
static const uint16_t g_provingGroundsObstacleAnimPeriodTicksByLevel[20] = {
	24, 24, 24, 24, 20, 16, 14, 14, 14, 12, 12, 12, 10, 8, 6, 6, 6, 6, 6, 6,
};
/* The status panel's five labels, in ProvingGroundsStatusLabelId order;
 * StringTable_LoadGameStrings fills them. */
// GLOBAL: XVT 0xA606F0
const char *g_provingGroundsStatusLabels[5] = {0};
/* The local player's roll over the last four pose records in the proving
 * grounds, newest first. ProvingGrounds_RecordLocalPlayerPoseHistory writes it;
 * collide_collisions puts a colliding craft back to entry 3. */
// GLOBAL: XVT 0x9A8D58
int16_t g_provingGroundsLocalPlayerRollHistory[4] = {0};
/* The local player's pitch over the last four pose records, newest first; kept
 * like g_provingGroundsLocalPlayerRollHistory. */
// GLOBAL: XVT 0x9ED220
int16_t g_provingGroundsLocalPlayerPitchHistory[4] = {0};
/* The local player's yaw over the last four pose records, newest first; kept
 * like g_provingGroundsLocalPlayerRollHistory. */
// GLOBAL: XVT 0xA004C0
int16_t g_provingGroundsLocalPlayerYawHistory[4] = {0};
/* The local player's world Z over the last four pose records, newest first,
 * taken from prevWorldZ; kept like g_provingGroundsLocalPlayerRollHistory. */
// GLOBAL: XVT 0xA08200
int g_provingGroundsLocalPlayerWorldZHistory[4] = {0};
/* The local player's world X over the last four pose records, newest first,
 * taken from prevWorldX; kept like g_provingGroundsLocalPlayerRollHistory. */
// GLOBAL: XVT 0xA08220
int g_provingGroundsLocalPlayerWorldXHistory[4] = {0};
/* The local player's world Y over the last four pose records, newest first,
 * taken from prevWorldY; kept like g_provingGroundsLocalPlayerRollHistory. */
// GLOBAL: XVT 0xA08230
int g_provingGroundsLocalPlayerWorldYHistory[4] = {0};
/* Ticks left before each obstacle animation steps: 0 turns cargo meshes, 1
 * antennas, 2 misc hull meshes. Only ProvingGrounds_UpdateCourse writes it. */
// GLOBAL: XVT 0xA00738
static int16_t g_provingGroundsObstacleAnimTimers[3] = {0};
/* Object slot, 1 to 12, of the course gate the local player passed last; gate 1
 * is the start and finish. Two functions write it: ProvingGrounds_StartLevel
 * sets 1 and ProvingGrounds_UpdateCourse moves it on as gates are crossed. */
// GLOBAL: XVT 0x9A8078
uint16_t g_provingGroundsCurrentCheckpointObjIdx = 0;

/* Shifts the local player's pose history one place older, dropping entry 3, and
 * records the craft's prevWorldX, prevWorldY and prevWorldZ and current roll,
 * pitch and yaw as entry 0. In the modern build with unlocked timing it records
 * only when XvtPlayerTiming_RecordRecovery gives a reference position, which it
 * records in place of the previous position. Does not check that the local
 * player has a craft. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x42B290
void ProvingGrounds_RecordLocalPlayerPoseHistory(void)
{
	uint16_t historyIndex;
	struct ObjectRecord *object;
#ifdef XVT_MODERN
	int32_t referencePosition[3];
	if (XvtFlightTiming_IsUnlocked() &&
	    !XvtPlayerTiming_RecordRecovery(g_localPlayer, referencePosition)) {
		return;
	}
#endif

	historyIndex = 2;
	do {
		g_provingGroundsLocalPlayerWorldXHistory[historyIndex + 1] =
			g_provingGroundsLocalPlayerWorldXHistory[historyIndex];
		g_provingGroundsLocalPlayerWorldYHistory[historyIndex + 1] =
			g_provingGroundsLocalPlayerWorldYHistory[historyIndex];
		g_provingGroundsLocalPlayerWorldZHistory[historyIndex + 1] =
			g_provingGroundsLocalPlayerWorldZHistory[historyIndex];
		g_provingGroundsLocalPlayerRollHistory[historyIndex + 1] =
			g_provingGroundsLocalPlayerRollHistory[historyIndex];
		g_provingGroundsLocalPlayerPitchHistory[historyIndex + 1] =
			g_provingGroundsLocalPlayerPitchHistory[historyIndex];
		g_provingGroundsLocalPlayerYawHistory[historyIndex + 1] =
			g_provingGroundsLocalPlayerYawHistory[historyIndex];
	} while (historyIndex-- != 0);

	object = &g_objectTable[g_players[g_localPlayer].objectIndex];

#ifdef XVT_MODERN
	g_provingGroundsLocalPlayerWorldXHistory[0] =
		XvtFlightTiming_IsUnlocked() ? referencePosition[0]
					     : object->mobj->prevWorldX;
#else
	g_provingGroundsLocalPlayerWorldXHistory[0] = object->mobj->prevWorldX;
#endif

#ifdef XVT_MODERN
	g_provingGroundsLocalPlayerWorldYHistory[0] =
		XvtFlightTiming_IsUnlocked() ? referencePosition[1]
					     : object->mobj->prevWorldY;
#else
	g_provingGroundsLocalPlayerWorldYHistory[0] = object->mobj->prevWorldY;
#endif

#ifdef XVT_MODERN
	g_provingGroundsLocalPlayerWorldZHistory[0] =
		XvtFlightTiming_IsUnlocked() ? referencePosition[2]
					     : object->mobj->prevWorldZ;
#else
	g_provingGroundsLocalPlayerWorldZHistory[0] = object->mobj->prevWorldZ;
#endif

	g_provingGroundsLocalPlayerRollHistory[0] = object->roll;
	g_provingGroundsLocalPlayerPitchHistory[0] = object->pitch;
	g_provingGroundsLocalPlayerYawHistory[0] = object->yaw;
}

/* Draws one course object. The gate at g_provingGroundsCurrentCheckpointObjIdx
 * and the one after it are drawn whole, with their billboards queued; every
 * other one gets g_billboardObjectOrTypeIndex 0x7000 plus its slot. Then each
 * draws its first main hull node (its last node when it has none) with
 * nodeSwitchIndex 1; a gate before the current one does so with
 * g_billboardTargetSelectionState 1 and g_renderObjectRef set to
 * g_billboardObjectOrTypeIndex. Restores nodeSwitchIndex and g_renderObjectRef;
 * writes g_billboardModelNodeSwitchIndex. The branch that sets
 * g_localBeamTargetObjIdx never runs: it wants a slot below the current gate
 * where only the current gate and the next arrive. */
// FUNCTION: XVT 0x42B380
void ProvingGrounds_DrawCourseObject(uint16_t objectIndex)
{
	enum {
		PROVING_GROUNDS_BILLBOARD_REF_BASE = 0x7000,
		PROVING_GROUNDS_SELECTED_NODE_STATE = 1,
	};

	uint16_t meshIndex;
	uint16_t selectedNodeIndex;
	uint16_t savedRenderObjectRef;
	int objectType;
	int meshCount;
	int meshTypeIndex;
	MeshComponentType meshType;
	uint8_t savedNodeSwitchIndex;

	if (objectIndex == g_provingGroundsCurrentCheckpointObjIdx ||
	    (int)g_provingGroundsCurrentCheckpointObjIdx - (int)objectIndex ==
		    -1) {
		if (objectIndex < g_provingGroundsCurrentCheckpointObjIdx) {
			g_localBeamTargetObjIdx = objectIndex;
		}
		Damage_QueueCraftBillboards(objectIndex);
		RenderScene_DrawObjectModel(&g_objectTable[objectIndex]);
	} else {
		g_billboardObjectOrTypeIndex =
			objectIndex + PROVING_GROUNDS_BILLBOARD_REF_BASE;
	}

	g_billboardModelNodeSwitchIndex = 0;
	objectType = g_objectTable[objectIndex].objectType;
	if (objectType < (int)(sizeof(g_objectTypeMeshCache) /
			       sizeof(g_objectTypeMeshCache[0]))) {
		meshCount = g_objectTypeMeshCache[objectType].meshCount;
	} else {
		meshCount = ModelMesh_GetObjectTypeMeshCount(objectType);
	}
	for (meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
		++g_billboardModelNodeSwitchIndex;
		meshTypeIndex = meshIndex;
		objectType = g_objectTable[objectIndex].objectType;
		if (objectType < (int)(sizeof(g_objectTypeMeshCache) /
				       sizeof(g_objectTypeMeshCache[0]))) {
			meshType = ModelMesh_GetCachedObjectTypeMeshType(
				objectType, meshTypeIndex);
		} else {
			meshType = ModelMesh_GetObjectTypeMeshType(
				objectType, meshTypeIndex);
		}
		if (meshType == MESH_COMPONENT_01_MAIN_HULL) {
			break;
		}
	}

	selectedNodeIndex = g_billboardModelNodeSwitchIndex - 1;
	savedRenderObjectRef = g_renderObjectRef;
	g_billboardModelNodeSwitchIndex = selectedNodeIndex;
	if (objectIndex < g_provingGroundsCurrentCheckpointObjIdx) {
		g_billboardTargetSelectionState = 1;
		g_renderObjectRef = g_billboardObjectOrTypeIndex;
	}
	savedNodeSwitchIndex = g_objectTable[objectIndex].mobj->nodeSwitchIndex;
	g_objectTable[objectIndex].mobj->nodeSwitchIndex =
		PROVING_GROUNDS_SELECTED_NODE_STATE;
	RenderScene_DrawSelectedRootNode(&g_objectTable[objectIndex],
					 selectedNodeIndex);
	g_objectTable[objectIndex].mobj->nodeSwitchIndex = savedNodeSwitchIndex;
	g_renderObjectRef = savedRenderObjectRef;
}

/* Builds the course in object slots 1 to 12: gates of object types 98 and 99 in
 * turn, each turned by a fixed table of angles and chained to the one before
 * through its model bounds. Each gets genus 14, family 6, flight group 1,
 * signature 1, no motion, damageAmount 0x7FFF, the craft record of the same
 * pool index with every component intact (componentHp 255), hullMax and
 * systemDamageHullThreshold 0x7FFF, only shields working, 0x7FFF forward
 * shield. Sets flight group 1's status1 to 5 and zeroes the proving grounds
 * targets destroyed and score. Writes g_curCraft. */
// FUNCTION: XVT 0x42B550
void ProvingGrounds_InitCourseObjects(void)
{
	uint16_t objectTypes[13];
	uint16_t rolls[13];
	uint16_t yaws[13];
	uint16_t pitches[13];
	uint16_t objectIndex;
	uint16_t componentIndex;
	uint16_t beamIndex;
	uint16_t objectType;
	int16_t localX;
	int16_t localY;
	int16_t sizeY;
	int16_t localZ;
	int offsetX;
	int offsetY;
	int offsetZ;
	int deltaX;
	int deltaY;
	int deltaZ;

	g_flightMissionState.provingGroundsTargetsDestroyed = 0;
	g_flightMissionState.provingGroundsScore = 0;
	offsetX = 0;
	offsetY = 0;
	offsetZ = 0;

	objectTypes[1] = 98;
	pitches[1] = 0x4000;
	yaws[1] = 0;
	rolls[1] = 0;
	objectTypes[2] = 99;
	pitches[2] = 0x4000;
	yaws[2] = 0;
	rolls[2] = 0;
	objectTypes[3] = 98;
	pitches[3] = 0;
	yaws[3] = 0;
	rolls[3] = 0;
	objectTypes[4] = 99;
	pitches[4] = 0;
	yaws[4] = 0x4000;
	rolls[4] = 0;
	objectTypes[5] = 98;
	pitches[5] = 0x4000;
	yaws[5] = 0xC000;
	rolls[5] = 0;
	objectTypes[6] = 99;
	pitches[6] = 0x4000;
	yaws[6] = 0xC000;
	rolls[6] = 0x8000;
	objectTypes[7] = 98;
	pitches[7] = 0x8000;
	yaws[7] = 0;
	rolls[7] = 0;
	objectTypes[8] = 99;
	pitches[8] = 0x8000;
	yaws[8] = 0x8000;
	rolls[8] = 0;
	objectTypes[9] = 98;
	pitches[9] = 0x4000;
	yaws[9] = 0x8000;
	rolls[9] = 0;
	objectTypes[10] = 99;
	pitches[10] = 0x4000;
	yaws[10] = 0x8000;
	rolls[10] = 0x4000;
	objectTypes[11] = 98;
	pitches[11] = 0x4000;
	yaws[11] = 0x4000;
	rolls[11] = 0;
	objectIndex = 1;
	objectTypes[12] = 99;
	pitches[12] = 0x4000;
	yaws[12] = 0x4000;
	rolls[12] = 0x4000;

	do {
		objectType = objectTypes[objectIndex];
		g_objectTable[objectIndex].objectType = (uint8_t)objectType;
		g_objectTable[objectIndex].objectSignature = 1;
		g_objectTable[objectIndex].mobj->family = 6;
		g_objectTable[objectIndex].genusId = 14;
		g_objectTable[objectIndex].mobj->rollImpulseRate = 0;
		g_objectTable[objectIndex].mobj->speed = 0;
		g_objectTable[objectIndex].mobj->speedRemainder = 0;
		g_objectTable[objectIndex].mobj->damageAmount = 0x7FFF;
		g_objectTable[objectIndex].mobj->lifetimeTimer = 0;
		g_objectTable[objectIndex].mobj->secondsAlive = 0;
		g_objectTable[objectIndex].mobj->sourceObjIdx = 0;
		g_objectTable[objectIndex].mobj->sourceObjectType = 0;
		g_objectTable[objectIndex].mobj->iff = 0;
		g_objectTable[objectIndex].mobj->orientMatrixDirty = 1;
		g_objectTable[objectIndex].mobj->moveVectorDirty =
			g_objectTable[objectIndex].mobj->orientMatrixDirty;
		g_objectTable[objectIndex].flightGroupIdx = 1;
		g_missionFlightGroups[1].fg.status1 = 5;
		g_curCraft = &g_craftDataPoolBase[objectIndex];
		g_objectTable[objectIndex].mobj->pCraft = g_curCraft;

		for (componentIndex = 0; componentIndex < 2; ++componentIndex) {
			g_objectTable[objectIndex]
				.typeSpecificByte[componentIndex] = 0;
		}
		componentIndex = 0;
		do {
			g_curCraft->componentState[componentIndex] = 0;
			g_curCraft->meshRotation[componentIndex] = 0;
			g_curCraft->componentHp[componentIndex] = (uint8_t)-1;
		} while (++componentIndex < 50);
		g_curCraft->hullMax = 0x7FFF;
		g_curCraft->systemDamageHullThreshold = 0x7FFF;
		g_curCraft->hullDamage = 0;
		g_curCraft->damageStats.lastSystemHitTime = 0;
		g_curCraft->unusedMissionFlag = 0;
		g_curCraft->attackedByTeam[0] = 0;
		g_curCraft->notDisabledAccountingSuppress = 0;
		g_curCraft->capturedByFlightGroup = 0;
		g_curCraft->sFoilState = 0;
		for (beamIndex = 0; beamIndex < 5; ++beamIndex) {
			g_curCraft->beamEffectAccum[beamIndex] = 0;
		}
		g_curCraft->systemFlags = CRAFT_SUBSYSTEM_FLAG_SHIELDS;
		g_curCraft->workingSubsystems = g_curCraft->systemFlags;
		g_curCraft->shieldDistribMode =
			SHIELD_DISTRIBUTION_FULLY_FORWARD;
		g_curCraft->shieldEnergy[0] = 0x7FFF;
		g_curCraft->shieldEnergy[1] = 0;
		g_objectTable[objectIndex].roll = rolls[objectIndex];
		g_objectTable[objectIndex].yaw = yaws[objectIndex];
		g_objectTable[objectIndex].pitch = pitches[objectIndex];
		FVIEW_calcrotatemove(g_objectTable[objectIndex].pitch,
				     g_objectTable[objectIndex].yaw,
				     &g_objectTable[objectIndex]);
		FVIEW_calcrotateorient(g_objectTable[objectIndex].roll, 0,
				       &g_objectTable[objectIndex]);

		localX = (int16_t)-ModelBounds_GetMaxY(objectType);
		if (objectType == 98) {
			localY = 0;
			sizeY = (int16_t)ModelBounds_GetSizeY(objectType);
			localZ = 0;
		} else if (objectType == 99) {
			localY = 0;
			deltaX = ModelBounds_GetMinY(objectType);
			sizeY = (int16_t)(deltaX +
					  ModelBounds_GetSizeY(objectType));
			deltaX = ModelBounds_GetMinZ(objectType);
			localZ = (int16_t)(deltaX +
					   ModelBounds_GetSizeZ(objectType));
		}

		deltaX = Math_MulQ15(0, g_fviewSideX_Q15);
		deltaY = Math_MulQ15(0, g_fviewSideY_Q15);
		deltaZ = Math_MulQ15(0, g_fviewSideZ_Q15);
		deltaX += Math_MulQ15(localX, g_fviewForwardX_Q15);
		deltaY += Math_MulQ15(localX, g_fviewForwardY_Q15);
		deltaZ += Math_MulQ15(localX, g_fviewForwardZ_Q15);
		deltaX += Math_MulQ15(0, g_fviewUpX_Q15);
		deltaY += Math_MulQ15(0, g_fviewUpY_Q15);
		deltaZ += Math_MulQ15(0, g_fviewUpZ_Q15);
		g_objectTable[objectIndex].world_x = offsetX - 2 * deltaX;
		g_objectTable[objectIndex].world_y = offsetY - 2 * deltaY;
		g_objectTable[objectIndex].world_z = offsetZ - 2 * deltaZ;

		deltaX = Math_MulQ15(localY, g_fviewSideX_Q15);
		deltaY = Math_MulQ15(localY, g_fviewSideY_Q15);
		deltaZ = Math_MulQ15(localY, g_fviewSideZ_Q15);
		deltaX += Math_MulQ15(sizeY, g_fviewForwardX_Q15);
		deltaY += Math_MulQ15(sizeY, g_fviewForwardY_Q15);
		deltaZ += Math_MulQ15(sizeY, g_fviewForwardZ_Q15);
		deltaX += Math_MulQ15(localZ, g_fviewUpX_Q15);
		deltaY += Math_MulQ15(localZ, g_fviewUpY_Q15);
		deltaZ += Math_MulQ15(localZ, g_fviewUpZ_Q15);
		offsetX += 2 * deltaX;
		offsetY += 2 * deltaY;
		offsetZ += 2 * deltaZ;
		g_objectTable[objectIndex].mobj->prevWorldX =
			g_objectTable[objectIndex].world_x;
		g_objectTable[objectIndex].mobj->prevWorldY =
			g_objectTable[objectIndex].world_y;
		g_objectTable[objectIndex].mobj->prevWorldZ =
			g_objectTable[objectIndex].world_z;
	} while (++objectIndex < 13);
}

/* Starts the given proving grounds level: gate 1 current, 0 checkpoints passed,
 * 12 remaining; the countdown clock gets (10 - level) / 2 minutes plus 30
 * seconds for an odd level up to 8, or 5 * (20 - level) seconds above 8. Then
 * sets up every obstacle craft's components: gate 1 hides all of them
 * (componentState 2), with componentHp 255 on main hulls and 0 elsewhere; other
 * gates hide the main hull, give laser guns 2 * level, show cargo from level 2
 * with 3 * level, misc hull from level 5 with 255 and antennas from level 3
 * with 24 * level, else hide them with 0. Rebuilds the HUD unless in replay
 * view. Writes g_curCraft. Does not check the level. */
// FUNCTION: XVT 0x42BD80
void ProvingGrounds_StartLevel(uint16_t level)
{
	enum {
		FIRST_CHECKPOINT_OBJECT = 1,
		CHECKPOINT_COUNT = 12,
		LONG_TIMER_LAST_LEVEL = 8,
		LONG_TIMER_BASE_MINUTES = 10,
		SHORT_TIMER_BASE_LEVEL = 20,
		SECONDS_PER_HALF_MINUTE = 30,
		SECONDS_PER_SHORT_LEVEL = 5,
		COMPONENT_DISABLED = 2,
		LASER_HP_PER_LEVEL = 2,
		CARGO_FIRST_ACTIVE_LEVEL = 2,
		CARGO_HP_PER_LEVEL = 3,
		HULL_FIRST_ACTIVE_LEVEL = 5,
		ANTENNA_FIRST_ACTIVE_LEVEL = 3,
		ANTENNA_HP_PER_LEVEL = 24,
	};

	uint8_t countdownSeconds;
	uint16_t objectIndex;
	uint16_t meshIndex;
	int modelNodeIndex;
	int objectType;
	int meshTypeIndex;
	int meshCount;
	MeshComponentType meshType;

	g_provingGroundsCurrentCheckpointObjIdx = FIRST_CHECKPOINT_OBJECT;
	g_flightMissionState.provingGroundsCheckpointsPassed = 0;
	g_flightMissionState.provingGroundsCheckpointsRemaining =
		CHECKPOINT_COUNT;
	if (level > LONG_TIMER_LAST_LEVEL) {
		g_missionCountdownClock.minutes = 0;
		countdownSeconds = SECONDS_PER_SHORT_LEVEL *
				   (SHORT_TIMER_BASE_LEVEL - level);
	} else {
		g_missionCountdownClock.minutes =
			(LONG_TIMER_BASE_MINUTES - level) / 2;
		countdownSeconds = SECONDS_PER_HALF_MINUTE * (level & 1);
	}
	g_missionCountdownClock.seconds = countdownSeconds;

	for (objectIndex = 0;
	     objectIndex < (int)g_activeRegionCraftObjectSlotEnd;
	     ++objectIndex) {
		if (g_objectTable[objectIndex].objectType == 0 ||
		    g_objectTable[objectIndex].genusId !=
			    CRAFT_GENUS_OBSTACLE) {
			continue;
		}

		g_curCraft = g_objectTable[objectIndex].mobj->pCraft;
		objectType = g_objectTable[objectIndex].objectType;
		if (objectType < (int)(sizeof(g_objectTypeMeshCache) /
				       sizeof(g_objectTypeMeshCache[0]))) {
			meshCount = g_objectTypeMeshCache[objectType].meshCount;
		} else {
			meshCount =
				ModelMesh_GetObjectTypeMeshCount(objectType);
		}

		modelNodeIndex = 0;
		for (meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
			++modelNodeIndex;
			meshTypeIndex = meshIndex;
			objectType = g_objectTable[objectIndex].objectType;
			if (objectType <
			    (int)(sizeof(g_objectTypeMeshCache) /
				  sizeof(g_objectTypeMeshCache[0]))) {
				meshType =
					ModelMesh_GetCachedObjectTypeMeshType(
						objectType, meshTypeIndex);
			} else {
				meshType = ModelMesh_GetObjectTypeMeshType(
					objectType, meshTypeIndex);
			}

			if (objectIndex == FIRST_CHECKPOINT_OBJECT) {
				if (meshType == MESH_COMPONENT_01_MAIN_HULL) {
					g_curCraft->componentHp[modelNodeIndex -
								1] = UINT8_MAX;
				} else {
					g_curCraft->componentHp[modelNodeIndex -
								1] = 0;
				}
				g_curCraft->componentState[modelNodeIndex - 1] =
					COMPONENT_DISABLED;
				continue;
			}

			switch (meshType) {
			case MESH_COMPONENT_01_MAIN_HULL:
				g_curCraft->componentState[modelNodeIndex - 1] =
					COMPONENT_DISABLED;
				break;
			case MESH_COMPONENT_05_LASR_GUN:
				g_curCraft->componentHp[modelNodeIndex - 1] =
					LASER_HP_PER_LEVEL * level;
				g_curCraft->componentState[modelNodeIndex - 1] =
					0;
				break;
			case MESH_COMPONENT_17_CARGO:
				if (level < CARGO_FIRST_ACTIVE_LEVEL) {
					g_curCraft->componentState
						[modelNodeIndex - 1] =
						COMPONENT_DISABLED;
					g_curCraft->componentHp[modelNodeIndex -
								1] = 0;
				} else {
					g_curCraft->componentHp[modelNodeIndex -
								1] =
						CARGO_HP_PER_LEVEL * level;
					g_curCraft->componentState
						[modelNodeIndex - 1] = 0;
					g_curCraft
						->meshRotation[modelNodeIndex -
							       1] = 0;
				}
				break;
			case MESH_COMPONENT_18_MISC_HULL:
				if (level < HULL_FIRST_ACTIVE_LEVEL) {
					g_curCraft->componentState
						[modelNodeIndex - 1] =
						COMPONENT_DISABLED;
					g_curCraft->componentHp[modelNodeIndex -
								1] = 0;
				} else {
					g_curCraft->componentState
						[modelNodeIndex - 1] = 0;
					g_curCraft
						->meshRotation[modelNodeIndex -
							       1] = 0;
					g_curCraft->componentHp[modelNodeIndex -
								1] = UINT8_MAX;
				}
				break;
			case MESH_COMPONENT_19_ANTENNA:
				if (level < ANTENNA_FIRST_ACTIVE_LEVEL) {
					g_curCraft->componentState
						[modelNodeIndex - 1] =
						COMPONENT_DISABLED;
					g_curCraft->componentHp[modelNodeIndex -
								1] = 0;
				} else {
					g_curCraft->componentHp[modelNodeIndex -
								1] =
						ANTENNA_HP_PER_LEVEL * level;
					g_curCraft->componentState
						[modelNodeIndex - 1] = 0;
					g_curCraft
						->meshRotation[modelNodeIndex -
							       1] = 0;
				}
				break;
			default:
				break;
			}
		}
	}

	if (g_replayViewMode == 0) {
		Hud_InitHUD(g_localPlayer);
	}
}

/* Runs the course for one simulation step. Counts down the obstacle animation
 * timers by g_elapsedTicks, turning cargo meshes from level 3, antennas from
 * level 4 and misc hull meshes from level 6 one step per period (the misc hull
 * uses the previous level's period); steps the course animation frame every 29
 * ticks into each object's nodeSwitchIndex (frame 3 shows as 1). When the local
 * player crosses the next gate (gate 1 after 12) it becomes current and is
 * counted; crossing gate 1 ends the level: IFMSG_198, then the clock runs down
 * to 0 one second at a time, each adding 10 to provingGroundsTimeBonus and the
 * score, beeping every 100 points, drawn with a wait of 4 ticks that holds the
 * game until it ends; then IFMSG_199 and the next level starts. Writes
 * g_curCraft and g_inputTimestamp. Does not check the level against the
 * 20-entry period table. */
// FUNCTION: XVT 0x42C0A0
void ProvingGrounds_UpdateCourse(void)
{
	enum {
		OBSTACLE_ANIM_COUNT = 3,
		SPECIAL_ANIM_INDEX = 2,
		COURSE_ANIM_PERIOD_TICKS = 29,
		COURSE_ANIM_FRAME_COUNT = 4,
		COURSE_OBJECT_FIRST = 1,
		COURSE_OBJECT_END = 13,
		COURSE_ANIM_PING_PONG_FRAME = 3,
		COURSE_ANIM_PING_PONG_DISPLAY_FRAME = 1,
		CARGO_FIRST_ANIM_LEVEL = 3,
		HULL_FIRST_ANIM_LEVEL = 6,
		ANTENNA_FIRST_ANIM_LEVEL = 4,
		LAST_CHECKPOINT_OBJECT = 12,
		SCORE_INCREMENT = 10,
		SCORE_SOUND_INTERVAL = 100,
		FRAME_DELAY_TICKS = 4,
	};

	uint16_t animIndex;
	uint16_t animPeriod;
	int16_t animTimer;
	int16_t animStepCount;
	int16_t obstacleAnimSteps[OBSTACLE_ANIM_COUNT];
	uint16_t objectIndex;
	int objectType;
	int meshCount;
	int modelNodeIndex;
	int meshIndex;
	int meshTypeIndex;
	MeshComponentType meshType;
	uint16_t nextCheckpointObject;

	for (animIndex = 0; animIndex < OBSTACLE_ANIM_COUNT; ++animIndex) {
		animTimer = g_provingGroundsObstacleAnimTimers[animIndex] -
			    g_elapsedTicks;
		g_provingGroundsObstacleAnimTimers[animIndex] = animTimer;
		if (animTimer < 0) {
			animPeriod =
				g_provingGroundsObstacleAnimPeriodTicksByLevel
					[g_flightMissionState
						 .provingGroundsLevel];
			if (animIndex == SPECIAL_ANIM_INDEX) {
				animPeriod =
					g_provingGroundsObstacleAnimPeriodTicksByLevel
						[g_flightMissionState
							 .provingGroundsLevel -
						 1];
			}
			animStepCount = 1 - animTimer / (int)animPeriod;
			obstacleAnimSteps[animIndex] = animStepCount;
			g_provingGroundsObstacleAnimTimers[animIndex] =
				animTimer + animStepCount * animPeriod;
		} else {
			obstacleAnimSteps[animIndex] = 0;
		}
	}

	g_provingGroundsCourseAnimTimer -= g_elapsedTicks;
	if (g_provingGroundsCourseAnimTimer < 0) {
		g_provingGroundsCourseAnimTimer += COURSE_ANIM_PERIOD_TICKS;
		++g_provingGroundsCourseAnimFrame;
	}

	for (objectIndex = COURSE_OBJECT_FIRST; objectIndex < COURSE_OBJECT_END;
	     ++objectIndex) {
		g_curCraft = g_objectTable[objectIndex].mobj->pCraft;
		if (g_provingGroundsCourseAnimFrame ==
		    COURSE_ANIM_FRAME_COUNT) {
			g_provingGroundsCourseAnimFrame = 0;
		}
		g_objectTable[objectIndex].mobj->nodeSwitchIndex =
			(uint8_t)g_provingGroundsCourseAnimFrame;
		if (g_provingGroundsCourseAnimFrame ==
		    COURSE_ANIM_PING_PONG_FRAME) {
			g_objectTable[objectIndex].mobj->nodeSwitchIndex =
				COURSE_ANIM_PING_PONG_DISPLAY_FRAME;
		}

		objectType = g_objectTable[objectIndex].objectType;
		if (objectType < (int)(sizeof(g_objectTypeMeshCache) /
				       sizeof(g_objectTypeMeshCache[0]))) {
			meshCount = g_objectTypeMeshCache[objectType].meshCount;
		} else {
			meshCount =
				ModelMesh_GetObjectTypeMeshCount(objectType);
		}

		modelNodeIndex = 0;
		for (meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
			++modelNodeIndex;
			meshTypeIndex = meshIndex;
			objectType = g_objectTable[objectIndex].objectType;
			if (objectType <
			    (int)(sizeof(g_objectTypeMeshCache) /
				  sizeof(g_objectTypeMeshCache[0]))) {
				meshType =
					ModelMesh_GetCachedObjectTypeMeshType(
						objectType, meshTypeIndex);
			} else {
				meshType = ModelMesh_GetObjectTypeMeshType(
					objectType, meshTypeIndex);
			}

			switch (meshType) {
			case MESH_COMPONENT_17_CARGO:
				if (g_flightMissionState.provingGroundsLevel >=
				    CARGO_FIRST_ANIM_LEVEL) {
					g_curCraft
						->meshRotation[modelNodeIndex -
							       1] +=
						obstacleAnimSteps[0];
				}
				break;
			case MESH_COMPONENT_18_MISC_HULL:
				if (g_flightMissionState.provingGroundsLevel >=
				    HULL_FIRST_ANIM_LEVEL) {
					g_curCraft
						->meshRotation[modelNodeIndex -
							       1] +=
						obstacleAnimSteps[2];
				}
				break;
			case MESH_COMPONENT_19_ANTENNA:
				if (g_flightMissionState.provingGroundsLevel >=
				    ANTENNA_FIRST_ANIM_LEVEL) {
					g_curCraft
						->meshRotation[modelNodeIndex -
							       1] +=
						obstacleAnimSteps[1];
				}
				break;
			default:
				break;
			}
		}
	}

	nextCheckpointObject = COURSE_OBJECT_FIRST;
	if (g_provingGroundsCurrentCheckpointObjIdx != LAST_CHECKPOINT_OBJECT) {
		nextCheckpointObject =
			g_provingGroundsCurrentCheckpointObjIdx + 1;
	}
	if (ProvingGrounds_HasPlayerCrossedCheckpoint(nextCheckpointObject)) {
		g_provingGroundsCurrentCheckpointObjIdx = nextCheckpointObject;
		++g_flightMissionState.provingGroundsCheckpointsPassed;
		--g_flightMissionState.provingGroundsCheckpointsRemaining;
		if (nextCheckpointObject == COURSE_OBJECT_FIRST) {
			msg_emitInFlightMessage(
				IFMSG_198_CONGRATULATIONS_LEVEL_COMPLETED_TIME_LEFT_BONUS,
				g_localPlayer);
			g_flightMissionState.provingGroundsTimeBonus = 0;
			ProvingGrounds_RenderTimeBonusFrame();
			while (g_missionCountdownClock.minutes != 0 ||
			       g_missionCountdownClock.seconds != 0) {
				if (g_missionCountdownClock.seconds != 0) {
					--g_missionCountdownClock.seconds;
				} else {
					g_missionCountdownClock.seconds = 59;
					--g_missionCountdownClock.minutes;
				}
				g_flightMissionState.provingGroundsTimeBonus +=
					SCORE_INCREMENT;
				g_flightMissionState.provingGroundsScore +=
					SCORE_INCREMENT;
				if ((int32_t)g_flightMissionState
						    .provingGroundsScore %
					    SCORE_SOUND_INTERVAL ==
				    0) {
					fsfx_PlaySound(
						FLIGHT_SOUND_CONFIRM_BEEP, -1,
						g_localPlayer);
				}
				ProvingGrounds_RenderTimeBonusFrame();
				do {
					g_inputTimestamp +=
						Time_ConsumeElapsedTicks();
				} while ((unsigned int)g_inputTimestamp <
					 FRAME_DELAY_TICKS);
				g_inputTimestamp = 0;
			}
			g_msgArgTable[0] =
				g_flightMissionState.provingGroundsTimeBonus;
			msg_emitInFlightMessage(
				IFMSG_199_BONUS_POINTS_AWARDED_ARG,
				g_localPlayer);
			++g_flightMissionState.provingGroundsLevel;
			ProvingGrounds_StartLevel(
				g_flightMissionState.provingGroundsLevel);
		}
	}
}

/* Tells whether the local player's craft crossed a gate's plane: returns 1 when
 * its current and previous positions, both within 0x4000 units of the plane's
 * point on every axis, lie on opposite sides of the plane or on it; else 0. The
 * plane faces along the gate's forward axis through a point on it: -maxY of the
 * model bounds for type 98, else 0, moved 1024 back for the current gate and 32
 * forward for any other. Only this file calls it. */
// FUNCTION: XVT 0x42C410
int ProvingGrounds_HasPlayerCrossedCheckpoint(uint16_t checkpointObjIdx)
{
	uint16_t checkpointIndex;
	uint16_t modelType;
	int16_t checkpointOffset;
	struct ObjectRecord *checkpoint;
	struct ObjectRecord *playerObject;
	int checkpointX;
	int checkpointY;
	int checkpointZ;
	int currentDeltaX;
	int currentDeltaY;
	int currentDeltaZ;
	int previousDeltaX;
	int previousDeltaY;
	int previousDeltaZ;
	int16_t currentSide;
	int16_t previousSide;

	checkpointIndex = checkpointObjIdx;
	modelType = g_objectTable[checkpointIndex].objectType;
	if (modelType == 98) {
		checkpointOffset = (int16_t)-ModelBounds_GetMaxY(modelType);
	} else {
		checkpointOffset = 0;
	}
	if (checkpointObjIdx == g_provingGroundsCurrentCheckpointObjIdx) {
		checkpointOffset = (int16_t)(checkpointOffset - 1024);
	} else {
		checkpointOffset = (int16_t)(checkpointOffset + 32);
	}

	checkpoint = &g_objectTable[checkpointIndex];
	checkpointX =
		Math_MulQ15(checkpointOffset, checkpoint->mobj->cachedFwdX);
	checkpointY =
		Math_MulQ15(checkpointOffset, checkpoint->mobj->cachedFwdY);
	checkpointZ =
		Math_MulQ15(checkpointOffset, checkpoint->mobj->cachedFwdZ);

	checkpointX = checkpoint->world_x + 2 * checkpointX;
	checkpointY = checkpoint->world_y + 2 * checkpointY;
	checkpointZ = checkpoint->world_z + 2 * checkpointZ;

	playerObject = &g_objectTable[g_players[g_localPlayer].objectIndex];
	currentDeltaX = playerObject->world_x - checkpointX;
	currentDeltaY = playerObject->world_y - checkpointY;
	currentDeltaZ = playerObject->world_z - checkpointZ;
	if (currentDeltaX > 0x4000) {
		return 0;
	}
	if (currentDeltaX < -0x4000) {
		return 0;
	}
	if (currentDeltaY > 0x4000) {
		return 0;
	}
	if (currentDeltaY < -0x4000) {
		return 0;
	}
	if (currentDeltaZ > 0x4000) {
		return 0;
	}
	if (currentDeltaZ < -0x4000) {
		return 0;
	}

	previousDeltaX = playerObject->mobj->prevWorldX - checkpointX;
	previousDeltaY = playerObject->mobj->prevWorldY - checkpointY;
	previousDeltaZ = playerObject->mobj->prevWorldZ - checkpointZ;
	if (previousDeltaX > 0x4000) {
		return 0;
	}
	if (previousDeltaX < -0x4000) {
		return 0;
	}
	if (previousDeltaY > 0x4000) {
		return 0;
	}
	if (previousDeltaY < -0x4000) {
		return 0;
	}
	if (previousDeltaZ > 0x4000) {
		return 0;
	}
	if (previousDeltaZ < -0x4000) {
		return 0;
	}

	currentSide = (int16_t)Math_Dot3Q15Wrapped(
		(int16_t)currentDeltaX, (int16_t)currentDeltaY,
		(int16_t)currentDeltaZ, checkpoint->mobj->cachedFwdX,
		checkpoint->mobj->cachedFwdY, checkpoint->mobj->cachedFwdZ);
	previousSide = (int16_t)Math_Dot3Q15Wrapped(
		(int16_t)previousDeltaX, (int16_t)previousDeltaY,
		(int16_t)previousDeltaZ, checkpoint->mobj->cachedFwdX,
		checkpoint->mobj->cachedFwdY, checkpoint->mobj->cachedFwdZ);

	return (currentSide >= 0 && previousSide <= 0) ||
	       (currentSide <= 0 && previousSide >= 0);
}

/* Draws the proving grounds panel of the HUD near x, y: on a full HUD redraw
 * the labels and level, every call the gates remaining and passed, targets
 * destroyed and score. 640x480 doubles the column width and offsets; the panel
 * sits one column right for craft models 7, 8, 11 and 15, else one left. The
 * modern build also records each field for its cockpit readouts. */
// FUNCTION: XVT 0x42C750
void ProvingGrounds_DrawStatusPanel(int16_t x, int16_t y)
{
	int16_t columnWidth;
	int16_t panelY;
	int16_t panelAnchorX;
	int16_t panelX;
	int panelWidth;
	int levelLabelXOffset;
	int levelValueXOffset;
	int scoreLabelXOffset;
	int valueXOffset;
	int scoreValueXOffset;

	switch (g_flightResolutionMode) {
	case FLIGHT_RESOLUTION_320X240:
		panelY = y;
		columnWidth = 8;
		levelLabelXOffset = 26;
		levelValueXOffset = 53;
		panelWidth = 90;
		scoreLabelXOffset = 20;
		valueXOffset = 75;
		scoreValueXOffset = 45;
		panelY -= 6;
		panelAnchorX = x;
		break;
	case FLIGHT_RESOLUTION_640X480:
		panelAnchorX = x;
		columnWidth = 16;
		levelLabelXOffset = 52;
		levelValueXOffset = 106;
		panelWidth = 180;
		scoreLabelXOffset = 40;
		valueXOffset = 150;
		scoreValueXOffset = 90;
		panelAnchorX += 10;
		panelY = y;
		panelY -= 10;
		break;
	default:
		panelY = y;
		columnWidth = 8;
		levelLabelXOffset = 26;
		levelValueXOffset = 53;
		panelWidth = 90;
		scoreLabelXOffset = 20;
		valueXOffset = 75;
		scoreValueXOffset = 45;
		panelY -= 6;
		panelAnchorX = x;
		break;
	}

	switch (g_objectTable[g_players[g_localPlayer].objectIndex]
			.mobj->pCraft->modelIndex) {
	case 7:
	case 8:
	case 11:
	case 15:
		panelX = panelAnchorX + columnWidth;
		break;
	default:
		panelX = panelAnchorX - columnWidth;
		break;
	}

	if (g_hudFullRedrawInProgress != 0) {
		FlightText_SetFontTier(1);
		FlightText_SetClipRect(panelX + columnWidth, panelY,
				       panelX + 10 * columnWidth,
				       panelY + g_flightFontLineHeight);
		FlightText_SetClearLineBackground(1);
		FlightText_SetBackgroundColor(0x30);
		g_flightFillClipRectFn();
		FlightText_SetColor(0x49);
		FlightText_SetCursor(panelX + levelLabelXOffset, panelY);
#ifdef XVT_MODERN
		XvtCockpitText_RecordField(
			(XvtCockpitTextFieldId)(XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST +
						0),
			g_provingGroundsStatusLabels[PROVING_STATUS_LEVEL],
			XVT_COCKPIT_ALIGN_LEFT);
#endif
		FlightText_DrawString(
			g_provingGroundsStatusLabels[PROVING_STATUS_LEVEL]);
		FlightText_SetColor(0x4A);
		FlightText_SetCursor(panelX + levelValueXOffset, panelY);
#ifdef XVT_MODERN
		XvtCockpitReadouts_RecordNumber(
			XVT_COCKPIT_NUMBER_COURSE_LEVEL,
			g_flightMissionState.provingGroundsLevel, 2, 2);
#endif
		FlightText_DrawDecimalNumber(
			g_flightMissionState.provingGroundsLevel, 2, 2);
		FlightText_SetClipRect(panelX,
				       panelY + g_flightFontLineHeight + 1,
				       panelX + panelWidth,
				       panelY + 5 * g_flightFontLineHeight + 1);
		FlightText_SetColor(0x45);
		FlightText_SetCursor(panelX,
				     panelY + g_flightFontLineHeight + 1);
#ifdef XVT_MODERN
		XvtCockpitText_RecordField(
			(XvtCockpitTextFieldId)(XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST +
						1),
			g_provingGroundsStatusLabels
				[PROVING_STATUS_SEGMENTS_LEFT],
			XVT_COCKPIT_ALIGN_LEFT);
#endif
		FlightText_DrawString(g_provingGroundsStatusLabels
					      [PROVING_STATUS_SEGMENTS_LEFT]);
		FlightText_SetCursor(panelX,
				     panelY + 2 * g_flightFontLineHeight + 1);
#ifdef XVT_MODERN
		XvtCockpitText_RecordField(
			(XvtCockpitTextFieldId)(XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST +
						2),
			g_provingGroundsStatusLabels
				[PROVING_STATUS_SEGMENTS_DONE],
			XVT_COCKPIT_ALIGN_LEFT);
#endif
		FlightText_DrawString(g_provingGroundsStatusLabels
					      [PROVING_STATUS_SEGMENTS_DONE]);
		FlightText_SetColor(0x4D);
		FlightText_SetCursor(panelX,
				     panelY + 3 * g_flightFontLineHeight + 1);
#ifdef XVT_MODERN
		XvtCockpitText_RecordField(
			(XvtCockpitTextFieldId)(XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST +
						3),
			g_provingGroundsStatusLabels
				[PROVING_STATUS_TARGETS_HIT],
			XVT_COCKPIT_ALIGN_LEFT);
#endif
		FlightText_DrawString(g_provingGroundsStatusLabels
					      [PROVING_STATUS_TARGETS_HIT]);
		FlightText_SetColor(0x51);
		FlightText_SetCursor(panelX + scoreLabelXOffset,
				     panelY + 4 * g_flightFontLineHeight + 1);
#ifdef XVT_MODERN
		XvtCockpitText_RecordField(
			(XvtCockpitTextFieldId)(XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST +
						4),
			g_provingGroundsStatusLabels[PROVING_STATUS_SCORE],
			XVT_COCKPIT_ALIGN_LEFT);
#endif
		FlightText_DrawString(
			g_provingGroundsStatusLabels[PROVING_STATUS_SCORE]);
	}

	FlightText_SetFontTier(1);
#ifdef XVT_MODERN
	XvtCockpitReadouts_RecordCourse(panelX, panelY, panelWidth,
					5 * g_flightFontLineHeight + 1);
#endif
	FlightText_SetClipRect(panelX, panelY + g_flightFontLineHeight + 1,
			       panelX + panelWidth,
			       panelY + 5 * g_flightFontLineHeight + 1);
	FlightText_SetClearLineBackground(1);
	FlightText_SetBackgroundColor(0x30);
	FlightText_SetColor(0x46);
	FlightText_SetCursor(panelX + valueXOffset,
			     panelY + g_flightFontLineHeight + 1);
#ifdef XVT_MODERN
	XvtCockpitReadouts_RecordNumber(
		XVT_COCKPIT_NUMBER_COURSE_REMAINING,
		g_flightMissionState.provingGroundsCheckpointsRemaining, 3, 1);
#endif
	FlightText_DrawDecimalNumber(
		g_flightMissionState.provingGroundsCheckpointsRemaining, 3, 1);
	g_flightDrawCharFn(' ');
	FlightText_SetCursor(panelX + valueXOffset,
			     panelY + 2 * g_flightFontLineHeight + 1);
#ifdef XVT_MODERN
	XvtCockpitReadouts_RecordNumber(
		XVT_COCKPIT_NUMBER_COURSE_PASSED,
		g_flightMissionState.provingGroundsCheckpointsPassed, 3, 1);
#endif
	FlightText_DrawDecimalNumber(
		g_flightMissionState.provingGroundsCheckpointsPassed, 3, 1);
	g_flightDrawCharFn(' ');
	FlightText_SetColor(0x4E);
	FlightText_SetCursor(panelX + valueXOffset,
			     panelY + 3 * g_flightFontLineHeight + 1);
#ifdef XVT_MODERN
	XvtCockpitReadouts_RecordNumber(
		XVT_COCKPIT_NUMBER_COURSE_TARGETS,
		g_flightMissionState.provingGroundsTargetsDestroyed, 3, 1);
#endif
	FlightText_DrawDecimalNumber(
		g_flightMissionState.provingGroundsTargetsDestroyed, 3, 1);
	g_flightDrawCharFn(' ');
	FlightText_SetColor(0x52);
	FlightText_SetCursor(panelX + scoreValueXOffset,
			     panelY + 4 * g_flightFontLineHeight + 1);
#ifdef XVT_MODERN
	XvtCockpitReadouts_RecordNumber(
		XVT_COCKPIT_NUMBER_COURSE_SCORE,
		g_flightMissionState.provingGroundsScore, 6, 1);
#endif
	ProvingGrounds_DrawScoreDecimal(
		g_flightMissionState.provingGroundsScore, 6, 1);
	g_flightDrawCharFn(' ');
	FlightText_SetFontTier(2);
}

/* Draws score in width right-aligned decimal places with g_flightDrawCharFn:
 * leading zeros show as spaces until minDigits places remain, and a place whose
 * digit comes out above 9 shows 9. Draws nothing for width 0. Does not check
 * that width is at most 8. */
// FUNCTION: XVT 0x42CBD0
void ProvingGrounds_DrawScoreDecimal(int score, unsigned int width,
				     unsigned int minDigits)
{
	int16_t sawDigit;
	unsigned int remainingWidth;
	int divisor;
	int digit;
	uint16_t drawChar;

	sawDigit = 0;
	remainingWidth = width;
	if (remainingWidth == 0) {
		return;
	}

	do {
		divisor = g_provingGroundsScoreDecimalDivisors[remainingWidth];
		digit = score / divisor;
		score -= divisor * (uint16_t)digit;
		if (sawDigit != 0 || minDigits >= remainingWidth ||
		    (uint16_t)digit != 0) {
			sawDigit = 1;
			drawChar = (uint16_t)digit;
			if (drawChar > 9) {
				drawChar = 9;
			}
			drawChar += '0';
		} else {
			drawChar = ' ';
		}
		g_flightDrawCharFn((uint8_t)drawChar);
		--remainingWidth;
	} while (remainingWidth != 0);
}

/* Draws the countdown clock as minutes:seconds and the time bonus near the
 * bottom of the screen in shadowed text, at y 456 in 640x480 and y 190
 * otherwise, then redraws the HUD with the surface locked. Leaves
 * g_flightTextShadowEnabled at 1. */
// FUNCTION: XVT 0x42CC40
void ProvingGrounds_RenderTimeBonusFrame(void)
{
	enum {
		LOW_TEXT_Y = 190,
		LOW_CLOCK_X = 200,
		LOW_BONUS_X = 255,
		HIGH_TEXT_Y = 456,
		HIGH_CLOCK_X = 360,
		HIGH_BONUS_X = 465,
		CLOCK_DIGITS = 2,
		BONUS_DIGITS = 5,
		COLOR_BACKGROUND = 0x2C,
		COLOR_TEXT = 0x43,
	};

	int textY;
	int clockX;
	int bonusX;

	switch (g_flightResolutionMode) {
	case FLIGHT_RESOLUTION_320X240:
		textY = LOW_TEXT_Y;
		clockX = LOW_CLOCK_X;
		bonusX = LOW_BONUS_X;
		break;
	case FLIGHT_RESOLUTION_640X480:
		textY = HIGH_TEXT_Y;
		clockX = HIGH_CLOCK_X;
		bonusX = HIGH_BONUS_X;
		break;
	default:
		textY = LOW_TEXT_Y;
		clockX = LOW_CLOCK_X;
		bonusX = LOW_BONUS_X;
		break;
	}

	g_flightTextShadowEnabled = 1;
	FlightText_SetBackgroundColor(COLOR_BACKGROUND);
	FlightText_SetColor(COLOR_TEXT);
	FlightText_SetClearLineBackground(0);
	FlightText_SetFontTier(1);
	FlightText_SetClipRect(0, textY, g_screenWidth, g_screenHeight);
	FlightText_SetCursor(clockX, textY);
	FlightText_DrawDecimalNumber(g_missionCountdownClock.minutes,
				     CLOCK_DIGITS, CLOCK_DIGITS);
	g_flightDrawCharFn(':');
	FlightText_DrawDecimalNumber(g_missionCountdownClock.seconds,
				     CLOCK_DIGITS, CLOCK_DIGITS);
	FlightText_SetCursor(bonusX, textY);
	FlightText_DrawDecimalNumber(
		g_flightMissionState.provingGroundsTimeBonus, BONUS_DIGITS,
		BONUS_DIGITS);
	FlightSurface_Lock();
	Hud_RenderHud(g_localPlayer);
	FlightSurface_Unlock();
}
