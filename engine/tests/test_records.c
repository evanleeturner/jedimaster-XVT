/* Checks the packed world records (xvt_runtime/snapshot/records.h) against the promises in its header, on
 * live structs, records and pools this file fills itself: no game data is read.
 *
 * Live structs and records are filled with a byte pattern that changes from one offset to the next, so a
 * field copied to or from the wrong place shows up. Every pointer in a live struct, and every link in a
 * record, is set to none or into one of this file's pools before it is translated. The field lists below
 * are the record fields the header declares, with the nested packed records spelled out member by member;
 * the links have checks of their own. */
#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/snapshot/records.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One listed field: where it sits and how big it is in the record and in the live struct. */
typedef struct Field {
	size_t record_offset, record_size, live_offset, live_size;
	const char* name;
} Field;

#define FIELD(RecordType, LiveType, member)                                                                  \
	{ offsetof(RecordType, member), sizeof(((RecordType*)0)->member), offsetof(LiveType, member),            \
	  sizeof(((LiveType*)0)->member), #member }
#define OBJECT_FIELD(member) FIELD(XvtSnapshotObjectRecord, ObjectRecord, member)
#define MOBILE_FIELD(member) FIELD(XvtSnapshotMobileObject, MobileObject, member)
#define CRAFT_FIELD(member) FIELD(XvtSnapshotCraftData, CraftData, member)
#define CHAR_DATA_FIELD(member) FIELD(XvtSnapshotMobileObjectCharData, MobileObjectCharData, member)
#define PLAYER_FIELD(member) FIELD(XvtSnapshotPlayerData, PlayerData, member)
#define MISSION_FIELD(member) FIELD(XvtSnapshotFlightMissionState, FlightMissionState, member)

static const Field kObjectFields[] = {
	OBJECT_FIELD(objectSignature),
	OBJECT_FIELD(genusId),
	OBJECT_FIELD(objectType),
	OBJECT_FIELD(world_x),
	OBJECT_FIELD(world_y),
	OBJECT_FIELD(world_z),
	OBJECT_FIELD(yaw),
	OBJECT_FIELD(pitch),
	OBJECT_FIELD(roll),
	OBJECT_FIELD(flightGroupIdx),
	OBJECT_FIELD(typeSpecificWord),
	OBJECT_FIELD(typeSpecificByte),
	OBJECT_FIELD(playerOwnerIdx),
};

static const Field kMobileFields[] = {
	MOBILE_FIELD(family),
	MOBILE_FIELD(lightIntensityScale),
	MOBILE_FIELD(simStateTimestamp),
	MOBILE_FIELD(prevWorldX),
	MOBILE_FIELD(prevWorldY),
	MOBILE_FIELD(prevWorldZ),
	MOBILE_FIELD(proximityList.count),
	MOBILE_FIELD(proximityList.score),
	MOBILE_FIELD(proximityList.objIdx),
	MOBILE_FIELD(proximityList.overflowScore),
	MOBILE_FIELD(rollImpulseRate),
	MOBILE_FIELD(speed),
	MOBILE_FIELD(speedRemainder),
	MOBILE_FIELD(damageAmount),
	MOBILE_FIELD(lifetimeTimer),
	MOBILE_FIELD(secondsAlive),
	MOBILE_FIELD(sourceObjIdx),
	MOBILE_FIELD(sourceObjectType),
	MOBILE_FIELD(iff),
	MOBILE_FIELD(team),
	MOBILE_FIELD(nodeSwitchIndex),
	MOBILE_FIELD(moveVectorDirty),
	MOBILE_FIELD(moveX),
	MOBILE_FIELD(moveY),
	MOBILE_FIELD(moveZ),
	MOBILE_FIELD(orientMatrixDirty),
	MOBILE_FIELD(cachedFwdX),
	MOBILE_FIELD(cachedFwdY),
	MOBILE_FIELD(cachedFwdZ),
	MOBILE_FIELD(cachedSideX),
	MOBILE_FIELD(cachedSideY),
	MOBILE_FIELD(cachedSideZ),
	MOBILE_FIELD(cachedUpX),
	MOBILE_FIELD(cachedUpY),
	MOBILE_FIELD(cachedUpZ),
};

static const Field kCraftFields[] = {
	CRAFT_FIELD(craftIndexInGroup),
	CRAFT_FIELD(modelIndex),
	CRAFT_FIELD(leader_obj_idx),
	CRAFT_FIELD(field_006),
	CRAFT_FIELD(objectKind),
	CRAFT_FIELD(missionAccountingDone),
	CRAFT_FIELD(aiSkill),
	CRAFT_FIELD(field_00B),
	CRAFT_FIELD(pitch),
	CRAFT_FIELD(yaw),
	CRAFT_FIELD(breakupPitchRate),
	CRAFT_FIELD(breakupYawRate),
	CRAFT_FIELD(beamEffectAccum),
	CRAFT_FIELD(sFoilState),
	CRAFT_FIELD(aiController.currentOrderSlot),
	CRAFT_FIELD(aiController.orderProgress),
	CRAFT_FIELD(aiController.skippedToOrder4),
	CRAFT_FIELD(aiController.pendingPlanId),
	CRAFT_FIELD(aiController.currentPlanId),
	CRAFT_FIELD(aiController.waypointIndex),
	CRAFT_FIELD(aiController.savedPlanId),
	CRAFT_FIELD(aiController.thinkInterval),
	CRAFT_FIELD(aiController.thinkTimer),
	CRAFT_FIELD(aiController.savedRandSeed),
	CRAFT_FIELD(aiController.targetObjIdx),
	CRAFT_FIELD(aiController.targetSignature),
	CRAFT_FIELD(aiController.targetComponent),
	CRAFT_FIELD(aiController.hasLiveTarget),
	CRAFT_FIELD(aiController.aimPointX),
	CRAFT_FIELD(aiController.aimPointY),
	CRAFT_FIELD(aiController.aimPointZ),
	CRAFT_FIELD(aiController.candidateTargetIdx),
	CRAFT_FIELD(aiController.escortTargetFG),
	CRAFT_FIELD(aiController.targetZAngle),
	CRAFT_FIELD(aiController.targetRoll),
	CRAFT_FIELD(aiController.targetXYAngle),
	CRAFT_FIELD(aiController.maneuverMode),
	CRAFT_FIELD(aiController.maneuverPhase),
	CRAFT_FIELD(aiController.maneuverTimer),
	CRAFT_FIELD(aiController.secondaryManeuverTimer),
	CRAFT_FIELD(carriedObjectIndex),
	CRAFT_FIELD(carrierObjIdx),
	CRAFT_FIELD(lastAttackerObjIdx),
	CRAFT_FIELD(lastHitMissionSecond),
	CRAFT_FIELD(aiFlight.threatObjIdx),
	CRAFT_FIELD(aiFlight.impactObjIdx),
	CRAFT_FIELD(aiFlight.goHomeFlag),
	CRAFT_FIELD(aiFlight.missionAbortedFlag),
	CRAFT_FIELD(aiFlight.departTimerFlag),
	CRAFT_FIELD(aiFlight.departClockHours),
	CRAFT_FIELD(aiFlight.departClockMinutes),
	CRAFT_FIELD(aiFlight.departClockSeconds),
	CRAFT_FIELD(aiFlight.maneuverCounter),
	CRAFT_FIELD(aiFlight.reactionTimer),
	CRAFT_FIELD(aiFlight.boardedAccountingDone),
	CRAFT_FIELD(aiFlight.orderActionCounter),
	CRAFT_FIELD(aiFlight.orderActionFlag),
	CRAFT_FIELD(aiFlight.objSignatureCount),
	CRAFT_FIELD(aiFlight.objSignatures),
	CRAFT_FIELD(aiFlight.maxSpeedCache),
	CRAFT_FIELD(aiFlight.motionScale),
	CRAFT_FIELD(aiFlight.climbState),
	CRAFT_FIELD(aiFlight.diveState),
	CRAFT_FIELD(aiFlight.pitchRate),
	CRAFT_FIELD(aiFlight.pitchAccel),
	CRAFT_FIELD(aiFlight.pitchState),
	CRAFT_FIELD(aiFlight.pitchThroughLoop),
	CRAFT_FIELD(aiFlight.pitchStepScale),
	CRAFT_FIELD(aiFlight.rollRate),
	CRAFT_FIELD(aiFlight.rollAccel),
	CRAFT_FIELD(aiFlight.rollState),
	CRAFT_FIELD(aiFlight.rollStep),
	CRAFT_FIELD(aiFlight.turnRate),
	CRAFT_FIELD(aiFlight.turnAccel),
	CRAFT_FIELD(aiFlight.turnState),
	CRAFT_FIELD(aiFlight.turnStep),
	CRAFT_FIELD(aiFlight.formationType),
	CRAFT_FIELD(aiFlight.separation),
	CRAFT_FIELD(waveNumber),
	CRAFT_FIELD(pushAccumX),
	CRAFT_FIELD(pushAccumY),
	CRAFT_FIELD(pushAccumZ),
	CRAFT_FIELD(throttleSpeed),
	CRAFT_FIELD(engineOutputScale),
	CRAFT_FIELD(commandedSpeed),
	CRAFT_FIELD(hullDamage),
	CRAFT_FIELD(systemDamageHullThreshold),
	CRAFT_FIELD(hullMax),
	CRAFT_FIELD(subsystemDamage),
	CRAFT_FIELD(damageStats.lastSystemHitTime),
	CRAFT_FIELD(damageStats.damageReceivedTotal),
	CRAFT_FIELD(damageStats.damageReceivedByPlayerOwnedCraft),
	CRAFT_FIELD(damageStats.damageFromCollision),
	CRAFT_FIELD(damageStats.damageFromStarship),
	CRAFT_FIELD(damageStats.damageFromMine),
	CRAFT_FIELD(damageStats.damageFromFlightGroupAmount),
	CRAFT_FIELD(damageStats.damageFromPlayer),
	CRAFT_FIELD(damageStats.damageFromAiSkill),
	CRAFT_FIELD(damageStats.installedHudFeatureMask),
	CRAFT_FIELD(damageStats.activeHudFeatureMask),
	CRAFT_FIELD(systemFlags),
	CRAFT_FIELD(workingSubsystems),
	CRAFT_FIELD(weaponFireInhibitTimer),
	CRAFT_FIELD(unusedMissionFlag),
	CRAFT_FIELD(notDisabledAccountingSuppress),
	CRAFT_FIELD(capturedByFlightGroup),
	CRAFT_FIELD(attackedByTeam),
	CRAFT_FIELD(identifiedOrderByTeam),
	CRAFT_FIELD(boardingState),
	CRAFT_FIELD(specialCargoName),
	CRAFT_FIELD(shieldEnergy),
	CRAFT_FIELD(shieldRechargeLevel),
	CRAFT_FIELD(shieldDistribMode),
	CRAFT_FIELD(cannonClassCount),
	CRAFT_FIELD(laserRechargeLevel),
	CRAFT_FIELD(laserSlotCount),
	CRAFT_FIELD(laserState),
	CRAFT_FIELD(warheadLauncherCount),
	CRAFT_FIELD(warheadSlotTypeIds),
	CRAFT_FIELD(warheadLauncherFlags),
	CRAFT_FIELD(warheadLauncherCooldownTicks),
	CRAFT_FIELD(warheadLockTicks),
	CRAFT_FIELD(beamTypeId),
	CRAFT_FIELD(beamLevel),
	CRAFT_FIELD(beamPresent),
	CRAFT_FIELD(beamActive),
	CRAFT_FIELD(beamTimer),
	CRAFT_FIELD(beamTargetObjIdx),
	CRAFT_FIELD(cmTypeId),
	CRAFT_FIELD(cmAmmoCount),
	CRAFT_FIELD(chaffActiveTimer),
	CRAFT_FIELD(cmFireCooldownTimer),
	CRAFT_FIELD(weaponStats),
	CRAFT_FIELD(field_256),
	CRAFT_FIELD(field_29F),
	CRAFT_FIELD(systemDisplaySlotBySystem),
	CRAFT_FIELD(systemHealth),
	CRAFT_FIELD(systemTimer),
	CRAFT_FIELD(componentState),
	CRAFT_FIELD(meshRotation),
	CRAFT_FIELD(componentHp),
	CRAFT_FIELD(playerCommandAvoidTargetObjIdx),
	CRAFT_FIELD(weaponSlots),
	CRAFT_FIELD(effectiveAiObjectSignature),
	CRAFT_FIELD(turretTargetStates),
	CRAFT_FIELD(field_3F2),
};

static const Field kCharFields[] = {
	CHAR_DATA_FIELD(skillValue),
	CHAR_DATA_FIELD(reserved02),
	CHAR_DATA_FIELD(aiController.currentOrderSlot),
	CHAR_DATA_FIELD(aiController.orderProgress),
	CHAR_DATA_FIELD(aiController.skippedToOrder4),
	CHAR_DATA_FIELD(aiController.pendingPlanId),
	CHAR_DATA_FIELD(aiController.currentPlanId),
	CHAR_DATA_FIELD(aiController.waypointIndex),
	CHAR_DATA_FIELD(aiController.savedPlanId),
	CHAR_DATA_FIELD(aiController.thinkInterval),
	CHAR_DATA_FIELD(aiController.thinkTimer),
	CHAR_DATA_FIELD(aiController.savedRandSeed),
	CHAR_DATA_FIELD(aiController.targetObjIdx),
	CHAR_DATA_FIELD(aiController.targetSignature),
	CHAR_DATA_FIELD(aiController.targetComponent),
	CHAR_DATA_FIELD(aiController.hasLiveTarget),
	CHAR_DATA_FIELD(aiController.aimPointX),
	CHAR_DATA_FIELD(aiController.aimPointY),
	CHAR_DATA_FIELD(aiController.aimPointZ),
	CHAR_DATA_FIELD(aiController.candidateTargetIdx),
	CHAR_DATA_FIELD(aiController.escortTargetFG),
	CHAR_DATA_FIELD(aiController.targetZAngle),
	CHAR_DATA_FIELD(aiController.targetRoll),
	CHAR_DATA_FIELD(aiController.targetXYAngle),
	CHAR_DATA_FIELD(aiController.maneuverMode),
	CHAR_DATA_FIELD(aiController.maneuverPhase),
	CHAR_DATA_FIELD(aiController.maneuverTimer),
	CHAR_DATA_FIELD(aiController.secondaryManeuverTimer),
	CHAR_DATA_FIELD(reserved40),
};

static const Field kPlayerFields[] = {
	PLAYER_FIELD(objectIndex),
	PLAYER_FIELD(boundObjectSignature),
	PLAYER_FIELD(pilotRating),
	PLAYER_FIELD(iff),
	PLAYER_FIELD(team),
	PLAYER_FIELD(boundFlightGroupIdx),
	PLAYER_FIELD(connectedFlag),
	PLAYER_FIELD(awaitingNewCraft),
	PLAYER_FIELD(boundCraftEngineGlowCount),
	PLAYER_FIELD(mapCameraState),
	PLAYER_FIELD(hyperspacePhase),
	PLAYER_FIELD(hyperspaceRuntime),
	PLAYER_FIELD(targetBoxEnabled),
	PLAYER_FIELD(currentTargetObjectIdx),
	PLAYER_FIELD(targetCycleStart),
	PLAYER_FIELD(targetPresetSlot),
	PLAYER_FIELD(missileLockState),
	PLAYER_FIELD(selectedWarhead),
	PLAYER_FIELD(selectedWeaponMode),
	PLAYER_FIELD(selectedTargetComponent),
	PLAYER_FIELD(targetingState),
	PLAYER_FIELD(engineWashSourceObjIdx),
	PLAYER_FIELD(engineWashStrength),
	PLAYER_FIELD(throttlePreset),
	PLAYER_FIELD(laserPreset),
	PLAYER_FIELD(shieldPreset),
	PLAYER_FIELD(beamPreset),
	PLAYER_FIELD(savedCraftSettings),
	PLAYER_FIELD(savedHudViewState),
	PLAYER_FIELD(pendingActionId),
	PLAYER_FIELD(pendingActionParam),
	PLAYER_FIELD(pendingActionIssuerPlayerIdx),
	PLAYER_FIELD(yawRollSwap),
	PLAYER_FIELD(smoothedInputYaw),
	PLAYER_FIELD(smoothedInputPitch),
	PLAYER_FIELD(savedKeyMods),
	PLAYER_FIELD(keyModsHoldTimer),
	PLAYER_FIELD(hardpointWorldX),
	PLAYER_FIELD(hardpointWorldY),
	PLAYER_FIELD(hardpointWorldZ),
	PLAYER_FIELD(hardpointLocalX),
	PLAYER_FIELD(hardpointLocalY),
	PLAYER_FIELD(hardpointLocalZ),
	PLAYER_FIELD(missionStats),
	PLAYER_FIELD(warheadsFired),
	PLAYER_FIELD(perMissionKills),
	PLAYER_FIELD(msgText),
	PLAYER_FIELD(msgLength),
	PLAYER_FIELD(msgTypeId),
	PLAYER_FIELD(viewState.savedTargetX),
	PLAYER_FIELD(viewState.savedTargetY),
	PLAYER_FIELD(viewState.savedTargetZ),
	PLAYER_FIELD(viewState.cameraFocusObjIdx),
	PLAYER_FIELD(viewState.aimTargetIdx),
	PLAYER_FIELD(viewState.viewPitch),
	PLAYER_FIELD(viewState.viewYaw),
	PLAYER_FIELD(viewState.viewRoll),
	PLAYER_FIELD(viewState.viewAngleD),
	PLAYER_FIELD(viewState.hudAimX),
	PLAYER_FIELD(viewState.hudAimY),
	PLAYER_FIELD(viewState.hudStateLive),
	PLAYER_FIELD(viewState.hudStateMirror),
	PLAYER_FIELD(viewState.hudAimXSnapState),
	PLAYER_FIELD(viewState.savedHudStateByte),
	PLAYER_FIELD(viewState.field_20),
	PLAYER_FIELD(viewState.savedHudAimX),
	PLAYER_FIELD(viewState.savedHudAimY),
	PLAYER_FIELD(viewState.playerInputBlocked),
	PLAYER_FIELD(viewState.cameraDistanceStep),
	PLAYER_FIELD(viewState.externalCameraActive),
	PLAYER_FIELD(viewState.cameraDistance),
	PLAYER_FIELD(viewState.transitionTimer),
	PLAYER_FIELD(viewState.cameraRollHistory),
	PLAYER_FIELD(viewState.cameraPitchHistory),
	PLAYER_FIELD(viewState.cameraYawHistory),
	PLAYER_FIELD(viewState.field_199),
	PLAYER_FIELD(network.flightResolutionMode),
	PLAYER_FIELD(network.directPlayId),
	PLAYER_FIELD(lockstepTimestamp),
	PLAYER_FIELD(savedX),
	PLAYER_FIELD(savedY),
	PLAYER_FIELD(savedZ),
	PLAYER_FIELD(savedRoll),
	PLAYER_FIELD(savedPitch),
	PLAYER_FIELD(savedYaw),
	PLAYER_FIELD(savedLifetimeTimer),
	PLAYER_FIELD(savedSpeed),
	PLAYER_FIELD(savedSpeedRemainder),
	PLAYER_FIELD(savedRollImpulseRate),
	PLAYER_FIELD(savedObjectSignature),
	PLAYER_FIELD(savedRegion),
	PLAYER_FIELD(pendingActionTimer),
	PLAYER_FIELD(beamFireCooldownTimer),
	PLAYER_FIELD(field_5B5),
	PLAYER_FIELD(impactDamageCooldownTime),
};

static const Field kMissionFields[] = {
	MISSION_FIELD(missionEndPending),
	MISSION_FIELD(provingGroundsModeActive),
	MISSION_FIELD(provingGroundsCraftType),
	MISSION_FIELD(provingGroundsLevel),
	MISSION_FIELD(provingGroundsScore),
	MISSION_FIELD(reserved08),
	MISSION_FIELD(provingGroundsCheckpointsPassed),
	MISSION_FIELD(reserved0C),
	MISSION_FIELD(provingGroundsCheckpointsRemaining),
	MISSION_FIELD(provingGroundsTargetsDestroyed),
	MISSION_FIELD(provingGroundsTimeBonus),
	MISSION_FIELD(difficulty),
	MISSION_FIELD(collisionsEnabled),
	MISSION_FIELD(craftJumpingEnabled),
	MISSION_FIELD(randomVariationEnabled),
	MISSION_FIELD(reserved18),
	MISSION_FIELD(locatePlayersEnabled),
	MISSION_FIELD(aiOpponentsEnabled),
	MISSION_FIELD(playerFlightGroupWaveMode),
	MISSION_FIELD(missionTimeLimitMinutes),
	MISSION_FIELD(teamVictoryTimeLimitMinutes),
	MISSION_FIELD(teamVictoryTimeLimitStarted),
	MISSION_FIELD(craftImpactBounceEnabled),
	MISSION_FIELD(connectedPlayerCount),
	MISSION_FIELD(maxConnectedPlayerCountThisMission),
	MISSION_FIELD(runtime.teamScores),
	MISSION_FIELD(runtime.teamKillStats),
	MISSION_FIELD(runtime.teamFgInspectedCapturedCounts),
	MISSION_FIELD(runtime.teamFgDesignationCode),
	MISSION_FIELD(runtime.globalPrimaryGoalStatus),
	MISSION_FIELD(runtime.globalGoalStatusUnused),
	MISSION_FIELD(runtime.globalBonusGoalStatus),
	MISSION_FIELD(runtime.teamGlobalGoalState),
	MISSION_FIELD(runtime.teamGoalStatus),
	MISSION_FIELD(runtime.globalGoalTriggerCounts),
	MISSION_FIELD(runtime.teamMissionCompletionTimeSeconds),
	MISSION_FIELD(runtime.teamHasCountableCraft),
	MISSION_FIELD(runtime.teamActiveGoalSequence),
	MISSION_FIELD(messageTriggered),
	MISSION_FIELD(messageDelayCountdown),
	MISSION_FIELD(globalUnitCraftCount),
};

/* The pools the links point into. */
static ObjectRecord g_testObjects[4];
static MobileObject g_testMobiles[3];
static CraftData g_testCraft[3];
static MobileObjectCharData g_testCharData[3];
static WarheadGuidanceState g_testGuidance[3];

static void UseTestPools(void) {
	g_objectTable = g_testObjects;
	g_mobileObjectPoolBase = g_testMobiles;
	g_craftDataPoolBase = g_testCraft;
	g_mobileObjectCharDataPool = g_testCharData;
	g_projectileGuidanceStates = g_testGuidance;
}

/* Fills size bytes from a seeded generator: neighbouring bytes differ in all but rare cases, and two seeds
 * give unrelated patterns. */
static void Fill(void* data, size_t size, uint32_t seed) {
	uint8_t* bytes = data;
	uint32_t state = seed * 2654435761u + 1u;
	for (size_t i = 0; i < size; ++i) {
		state = state * 1103515245u + 12345u;
		bytes[i] = (uint8_t)(state >> 16);
	}
}

/* The check fails, naming the field, when a listed field differs in size or in any byte between the
 * record and the live struct. */
static void CheckFields(const Field* fields, size_t count, const void* record, const void* live) {
	for (size_t i = 0; i < count; ++i) {
		const Field* f = &fields[i];
		int same = f->record_size == f->live_size &&
				   memcmp((const uint8_t*)record + f->record_offset, (const uint8_t*)live + f->live_offset,
						  f->record_size) == 0;
		if (!same)
			fprintf(stderr, "field %s differs between the record and the live struct\n", f->name);
		XVT_ASSERT_TRUE(same);
	}
}

/* One record type: its two sizes, its Encode/Decode pair, how to clear its links on either side, and
 * its listed fields. */
typedef struct RecordKind {
	const char* name;
	size_t record_size, live_size;
	void (*encode)(void* record, const void* live);
	void (*decode)(void* live, const void* record);
	void (*clear_live_links)(void* live);
	void (*clear_record_links)(void* record);
	const Field* fields;
	size_t field_count;
} RecordKind;

static void EncodeObject(void* record, const void* live) { XvtSnapshot_EncodeObjectRecord(record, live); }

static void DecodeObject(void* live, const void* record) { XvtSnapshot_DecodeObjectRecord(live, record); }

static void ClearObjectLive(void* live) { ((ObjectRecord*)live)->mobj = NULL; }

static void ClearObjectRecord(void* record) { ((XvtSnapshotObjectRecord*)record)->mobj = 0; }

static void EncodeMobile(void* record, const void* live) { XvtSnapshot_EncodeMobileObject(record, live); }

static void DecodeMobile(void* live, const void* record) { XvtSnapshot_DecodeMobileObject(live, record); }

static void ClearMobileLive(void* live) {
	MobileObject* mobile = live;
	mobile->pWarheadGuidance = NULL;
	mobile->pCraft = NULL;
	mobile->pCharData = NULL;
}

static void ClearMobileRecord(void* record) {
	XvtSnapshotMobileObject* mobile = record;
	mobile->pWarheadGuidance = 0;
	mobile->pCraft = 0;
	mobile->pCharData = 0;
}

static void EncodeCraft(void* record, const void* live) { XvtSnapshot_EncodeCraftData(record, live); }

static void DecodeCraft(void* live, const void* record) { XvtSnapshot_DecodeCraftData(live, record); }

static void ClearCraftLive(void* live) {
	CraftData* craft = live;
	for (int i = 0; i < 16; ++i)
		craft->turretObjectLinks[i] = NULL;
	craft->effectiveAiObjectLink = NULL;
}

static void ClearCraftRecord(void* record) {
	XvtSnapshotCraftData* craft = record;
	for (int i = 0; i < 16; ++i)
		craft->turretObjectLinks[i] = 0;
	craft->effectiveAiObjectLink = 0;
}

static void EncodeChar(void* record, const void* live) {
	XvtSnapshot_EncodeMobileObjectCharData(record, live);
}

static void DecodeChar(void* live, const void* record) {
	XvtSnapshot_DecodeMobileObjectCharData(live, record);
}

static void EncodePlayer(void* record, const void* live) { XvtSnapshot_EncodePlayerData(record, live); }

static void DecodePlayer(void* live, const void* record) { XvtSnapshot_DecodePlayerData(live, record); }

static void EncodeMission(void* record, const void* live) {
	XvtSnapshot_EncodeFlightMissionState(record, live);
}

static void DecodeMission(void* live, const void* record) {
	XvtSnapshot_DecodeFlightMissionState(live, record);
}

/* Character, player and mission-state records carry no links. */
static void NoLinks(void* data) { (void)data; }

static const RecordKind kKinds[] = {
	{ "object", sizeof(XvtSnapshotObjectRecord), sizeof(ObjectRecord), EncodeObject, DecodeObject,
	  ClearObjectLive, ClearObjectRecord, kObjectFields, sizeof kObjectFields / sizeof kObjectFields[0] },
	{ "mobile", sizeof(XvtSnapshotMobileObject), sizeof(MobileObject), EncodeMobile, DecodeMobile,
	  ClearMobileLive, ClearMobileRecord, kMobileFields, sizeof kMobileFields / sizeof kMobileFields[0] },
	{ "craft", sizeof(XvtSnapshotCraftData), sizeof(CraftData), EncodeCraft, DecodeCraft, ClearCraftLive,
	  ClearCraftRecord, kCraftFields, sizeof kCraftFields / sizeof kCraftFields[0] },
	{ "character", sizeof(XvtSnapshotMobileObjectCharData), sizeof(MobileObjectCharData), EncodeChar,
	  DecodeChar, NoLinks, NoLinks, kCharFields, sizeof kCharFields / sizeof kCharFields[0] },
	{ "player", sizeof(XvtSnapshotPlayerData), sizeof(PlayerData), EncodePlayer, DecodePlayer, NoLinks,
	  NoLinks, kPlayerFields, sizeof kPlayerFields / sizeof kPlayerFields[0] },
	{ "mission state", sizeof(XvtSnapshotFlightMissionState), sizeof(FlightMissionState), EncodeMission,
	  DecodeMission, NoLinks, NoLinks, kMissionFields, sizeof kMissionFields / sizeof kMissionFields[0] },
};

#define KIND_COUNT (sizeof kKinds / sizeof kKinds[0])

static void* Allocate(size_t size) {
	void* data = malloc(size);
	XVT_ASSERT_TRUE(data != NULL);
	return data;
}

/* Encode fills every byte of the record whatever it held before, and each listed field is the live
 * field's bytes as they are. */
static void CheckEncodeCopiesFields(void) {
	UseTestPools();
	for (size_t k = 0; k < KIND_COUNT; ++k) {
		const RecordKind* kind = &kKinds[k];
		for (uint32_t seed = 1; seed <= 2; ++seed) {
			void* live = Allocate(kind->live_size);
			uint8_t* zeros = Allocate(kind->record_size);
			uint8_t* ones = Allocate(kind->record_size);
			Fill(live, kind->live_size, seed);
			kind->clear_live_links(live);
			memset(zeros, 0x00, kind->record_size);
			memset(ones, 0xFF, kind->record_size);
			kind->encode(zeros, live);
			kind->encode(ones, live);
			if (memcmp(zeros, ones, kind->record_size) != 0)
				fprintf(stderr, "the %s record keeps bytes Encode did not write\n", kind->name);
			XVT_ASSERT_INT_EQ(memcmp(zeros, ones, kind->record_size), 0);
			CheckFields(kind->fields, kind->field_count, zeros, live);
			free(live);
			free(zeros);
			free(ones);
		}
	}
}

/* Decode writes each listed field back as the record holds it. */
static void CheckDecodeWritesFields(void) {
	UseTestPools();
	for (size_t k = 0; k < KIND_COUNT; ++k) {
		const RecordKind* kind = &kKinds[k];
		for (uint32_t seed = 3; seed <= 4; ++seed) {
			void* record = Allocate(kind->record_size);
			void* live = Allocate(kind->live_size);
			Fill(record, kind->record_size, seed);
			kind->clear_record_links(record);
			memset(live, 0xA5, kind->live_size);
			kind->decode(live, record);
			CheckFields(kind->fields, kind->field_count, record, live);
			free(record);
			free(live);
		}
	}
}

/* A record decoded into a live struct and encoded again gives the same bytes, whatever the live struct
 * held before: native padding never reaches the record. */
static void CheckRoundTrip(void) {
	UseTestPools();
	for (size_t k = 0; k < KIND_COUNT; ++k) {
		const RecordKind* kind = &kKinds[k];
		void* live = Allocate(kind->live_size);
		void* other = Allocate(kind->live_size);
		uint8_t* record = Allocate(kind->record_size);
		uint8_t* again = Allocate(kind->record_size);
		Fill(live, kind->live_size, 5);
		kind->clear_live_links(live);
		kind->encode(record, live);

		memset(other, 0x5A, kind->live_size);
		kind->decode(other, record);
		kind->encode(again, other);
		if (memcmp(again, record, kind->record_size) != 0)
			fprintf(stderr, "the %s record does not survive a round trip\n", kind->name);
		XVT_ASSERT_INT_EQ(memcmp(again, record, kind->record_size), 0);
		free(live);
		free(other);
		free(record);
		free(again);
	}
}

/* An object's mobile record is linked by its byte offset in the mobile record array, plus 1. */
static void CheckObjectLink(void) {
	UseTestPools();
	ObjectRecord live;
	XvtSnapshotObjectRecord record;
	memset(&live, 0, sizeof live);

	live.mobj = &g_testMobiles[2];
	XvtSnapshot_EncodeObjectRecord(&record, &live);
	XVT_ASSERT_INT_EQ(record.mobj, 2 * sizeof(XvtSnapshotMobileObject) + 1);
	live.mobj = &g_testMobiles[0];
	XvtSnapshot_EncodeObjectRecord(&record, &live);
	XVT_ASSERT_INT_EQ(record.mobj, 1);
	live.mobj = NULL;
	XvtSnapshot_EncodeObjectRecord(&record, &live);
	XVT_ASSERT_INT_EQ(record.mobj, 0);

	record.mobj = (uint32_t)(1 * sizeof(XvtSnapshotMobileObject) + 1);
	XvtSnapshot_DecodeObjectRecord(&live, &record);
	XVT_ASSERT_TRUE(live.mobj == &g_testMobiles[1]);
	record.mobj = 0;
	XvtSnapshot_DecodeObjectRecord(&live, &record);
	XVT_ASSERT_TRUE(live.mobj == NULL);
}

/* A mobile record links its guidance, craft and character records the same way, each by its offset in
 * its own pool's record array. */
static void CheckMobileLinks(void) {
	UseTestPools();
	MobileObject live;
	XvtSnapshotMobileObject record;
	memset(&live, 0, sizeof live);

	live.pWarheadGuidance = &g_testGuidance[0];
	live.pCraft = &g_testCraft[1];
	live.pCharData = &g_testCharData[2];
	XvtSnapshot_EncodeMobileObject(&record, &live);
	XVT_ASSERT_INT_EQ(record.pWarheadGuidance, 1);
	XVT_ASSERT_INT_EQ(record.pCraft, 1 * sizeof(XvtSnapshotCraftData) + 1);
	XVT_ASSERT_INT_EQ(record.pCharData, 2 * sizeof(XvtSnapshotMobileObjectCharData) + 1);

	memset(&live, 0, sizeof live);
	XvtSnapshot_DecodeMobileObject(&live, &record);
	XVT_ASSERT_TRUE(live.pWarheadGuidance == &g_testGuidance[0]);
	XVT_ASSERT_TRUE(live.pCraft == &g_testCraft[1]);
	XVT_ASSERT_TRUE(live.pCharData == &g_testCharData[2]);

	/* A guidance link further into its pool comes back to the same entry. */
	live.pWarheadGuidance = &g_testGuidance[2];
	XvtSnapshot_EncodeMobileObject(&record, &live);
	live.pWarheadGuidance = NULL;
	XvtSnapshot_DecodeMobileObject(&live, &record);
	XVT_ASSERT_TRUE(live.pWarheadGuidance == &g_testGuidance[2]);

	live.pWarheadGuidance = NULL;
	live.pCraft = NULL;
	live.pCharData = NULL;
	XvtSnapshot_EncodeMobileObject(&record, &live);
	XVT_ASSERT_INT_EQ(record.pWarheadGuidance, 0);
	XVT_ASSERT_INT_EQ(record.pCraft, 0);
	XVT_ASSERT_INT_EQ(record.pCharData, 0);
	live.pCraft = &g_testCraft[0];
	XvtSnapshot_DecodeMobileObject(&live, &record);
	XVT_ASSERT_TRUE(live.pWarheadGuidance == NULL);
	XVT_ASSERT_TRUE(live.pCraft == NULL);
	XVT_ASSERT_TRUE(live.pCharData == NULL);
}

/* A craft record links 16 turret objects and one AI object in the object table, by their offsets in the
 * object record array. */
static void CheckCraftLinks(void) {
	UseTestPools();
	CraftData* live = Allocate(sizeof *live);
	XvtSnapshotCraftData* record = Allocate(sizeof *record);
	memset(live, 0, sizeof *live);
	for (int i = 0; i < 16; ++i)
		live->turretObjectLinks[i] = i % 5 == 4 ? NULL : &g_testObjects[i % 5];
	live->effectiveAiObjectLink = &g_testObjects[3];
	XvtSnapshot_EncodeCraftData(record, live);
	for (int i = 0; i < 16; ++i) {
		uint32_t expected = i % 5 == 4 ? 0 : (uint32_t)((i % 5) * sizeof(XvtSnapshotObjectRecord) + 1);
		XVT_ASSERT_INT_EQ(record->turretObjectLinks[i], expected);
	}
	XVT_ASSERT_INT_EQ(record->effectiveAiObjectLink, 3 * sizeof(XvtSnapshotObjectRecord) + 1);

	memset(live, 0, sizeof *live);
	XvtSnapshot_DecodeCraftData(live, record);
	for (int i = 0; i < 16; ++i)
		XVT_ASSERT_TRUE(live->turretObjectLinks[i] == (i % 5 == 4 ? NULL : &g_testObjects[i % 5]));
	XVT_ASSERT_TRUE(live->effectiveAiObjectLink == &g_testObjects[3]);

	record->effectiveAiObjectLink = 0;
	XvtSnapshot_DecodeCraftData(live, record);
	XVT_ASSERT_TRUE(live->effectiveAiObjectLink == NULL);
	free(live);
	free(record);
}

/* Decode does not range-check a link: one just past the pool gives the pointer just past the pool's last
 * entry, not none and not a clamped entry. */
static void CheckLinksNotRangeChecked(void) {
	UseTestPools();
	ObjectRecord live;
	XvtSnapshotObjectRecord record;
	memset(&record, 0, sizeof record);
	record.mobj = (uint32_t)(3 * sizeof(XvtSnapshotMobileObject) + 1);
	XvtSnapshot_DecodeObjectRecord(&live, &record);
	XVT_ASSERT_TRUE(live.mobj == g_testMobiles + 3);
}

int main(void) {
	CheckEncodeCopiesFields();
	CheckDecodeWritesFields();
	CheckRoundTrip();
	CheckObjectLink();
	CheckMobileLinks();
	CheckCraftLinks();
	CheckLinksNotRangeChecked();
	return 0;
}
