#include "xvt_runtime/snapshot/records.h"

#include <stddef.h>
#include <string.h>

typedef struct XvtSnapshotField {
	size_t nativeOffset;
	size_t recordOffset;
	size_t size;
} XvtSnapshotField;

/* One table entry: where a field sits in the live struct, where it sits in the
 * packed snapshot record, and its size in the record. Both structs name the
 * field the same way. Each table below defines a shorter macro for its own
 * pair of structs and undefines it after the table. */
#define SNAPSHOT_FIELD(native, record, field)                                  \
	{offsetof(native, field), offsetof(record, field),                     \
	 sizeof(((record *)0)->field)}

/* Copy declared fields only: native padding never enters a snapshot or checksum. */
static void XvtSnapshot_CopyFields(void *record, void *live,
				   const XvtSnapshotField *fields, size_t count,
				   int restore)
{
	for (size_t i = 0; i < count; ++i) {
		void *disk = (uint8_t *)record + fields[i].recordOffset;
		void *native = (uint8_t *)live + fields[i].nativeOffset;
		if (restore) {
			memcpy(native, disk, fields[i].size);
		} else {
			memcpy(disk, native, fields[i].size);
		}
	}
}

#define OBJECT_RECORD_FIELD(field)                                             \
	SNAPSHOT_FIELD(ObjectRecord, XvtSnapshotObjectRecord, field)
static const XvtSnapshotField g_ObjectRecordFields[] = {
	OBJECT_RECORD_FIELD(objectSignature),
	OBJECT_RECORD_FIELD(genusId),
	OBJECT_RECORD_FIELD(objectType),
	OBJECT_RECORD_FIELD(world_x),
	OBJECT_RECORD_FIELD(world_y),
	OBJECT_RECORD_FIELD(world_z),
	OBJECT_RECORD_FIELD(yaw),
	OBJECT_RECORD_FIELD(pitch),
	OBJECT_RECORD_FIELD(roll),
	OBJECT_RECORD_FIELD(flightGroupIdx),
	OBJECT_RECORD_FIELD(typeSpecificWord),
	OBJECT_RECORD_FIELD(typeSpecificByte),
	OBJECT_RECORD_FIELD(playerOwnerIdx),
};
#undef OBJECT_RECORD_FIELD

void XvtSnapshot_EncodeObjectRecord(XvtSnapshotObjectRecord *record,
				    const ObjectRecord *live)
{
	XvtSnapshot_CopyFields(
		(void *)record, (void *)live, g_ObjectRecordFields,
		sizeof(g_ObjectRecordFields) / sizeof(g_ObjectRecordFields[0]),
		0);
	record->mobj =
		live->mobj
			? (uint32_t)((live->mobj - g_mobileObjectPoolBase) *
					     sizeof(XvtSnapshotMobileObject) +
				     1)
			: 0;
}

void XvtSnapshot_DecodeObjectRecord(ObjectRecord *live,
				    const XvtSnapshotObjectRecord *record)
{
	XvtSnapshot_CopyFields(
		(void *)record, (void *)live, g_ObjectRecordFields,
		sizeof(g_ObjectRecordFields) / sizeof(g_ObjectRecordFields[0]),
		1);
	live->mobj = record->mobj ? &g_mobileObjectPoolBase
					    [(record->mobj - 1) /
					     sizeof(XvtSnapshotMobileObject)]
				  : NULL;
}

#define MOBILE_OBJECT_FIELD(field)                                             \
	SNAPSHOT_FIELD(MobileObject, XvtSnapshotMobileObject, field)
static const XvtSnapshotField g_MobileObjectFields[] = {
	MOBILE_OBJECT_FIELD(family),
	MOBILE_OBJECT_FIELD(effectSize),
	MOBILE_OBJECT_FIELD(simStateTimestamp),
	MOBILE_OBJECT_FIELD(prevWorldX),
	MOBILE_OBJECT_FIELD(prevWorldY),
	MOBILE_OBJECT_FIELD(prevWorldZ),
	MOBILE_OBJECT_FIELD(proximityList.count),
	MOBILE_OBJECT_FIELD(proximityList.contactTicks),
	MOBILE_OBJECT_FIELD(proximityList.objIdx),
	MOBILE_OBJECT_FIELD(proximityList.rebuildTicks),
	MOBILE_OBJECT_FIELD(rollImpulseRate),
	MOBILE_OBJECT_FIELD(speed),
	MOBILE_OBJECT_FIELD(speedRemainder),
	MOBILE_OBJECT_FIELD(damageAmount),
	MOBILE_OBJECT_FIELD(lifetimeTimer),
	MOBILE_OBJECT_FIELD(secondsAlive),
	MOBILE_OBJECT_FIELD(sourceObjIdx),
	MOBILE_OBJECT_FIELD(sourceObjectType),
	MOBILE_OBJECT_FIELD(iff),
	MOBILE_OBJECT_FIELD(team),
	MOBILE_OBJECT_FIELD(nodeSwitchIndex),
	MOBILE_OBJECT_FIELD(moveVectorDirty),
	MOBILE_OBJECT_FIELD(moveX),
	MOBILE_OBJECT_FIELD(moveY),
	MOBILE_OBJECT_FIELD(moveZ),
	MOBILE_OBJECT_FIELD(orientMatrixDirty),
	MOBILE_OBJECT_FIELD(cachedFwdX),
	MOBILE_OBJECT_FIELD(cachedFwdY),
	MOBILE_OBJECT_FIELD(cachedFwdZ),
	MOBILE_OBJECT_FIELD(cachedSideX),
	MOBILE_OBJECT_FIELD(cachedSideY),
	MOBILE_OBJECT_FIELD(cachedSideZ),
	MOBILE_OBJECT_FIELD(cachedUpX),
	MOBILE_OBJECT_FIELD(cachedUpY),
	MOBILE_OBJECT_FIELD(cachedUpZ),
};
#undef MOBILE_OBJECT_FIELD

void XvtSnapshot_EncodeMobileObject(XvtSnapshotMobileObject *record,
				    const MobileObject *live)
{
	XvtSnapshot_CopyFields(
		(void *)record, (void *)live, g_MobileObjectFields,
		sizeof(g_MobileObjectFields) / sizeof(g_MobileObjectFields[0]),
		0);
	record->pWarheadGuidance =
		live->pWarheadGuidance
			? (uint32_t)((live->pWarheadGuidance -
				      g_projectileGuidanceStates) *
					     sizeof(WarheadGuidanceState) +
				     1)
			: 0;
	record->pCraft =
		live->pCraft ? (uint32_t)((live->pCraft - g_craftDataPoolBase) *
						  sizeof(XvtSnapshotCraftData) +
					  1)
			     : 0;
	record->pCharData =
		live->pCharData
			? (uint32_t)((live->pCharData -
				      g_mobileObjectCharDataPool) *
					     sizeof(XvtSnapshotMobileObjectCharData) +
				     1)
			: 0;
}

void XvtSnapshot_DecodeMobileObject(MobileObject *live,
				    const XvtSnapshotMobileObject *record)
{
	XvtSnapshot_CopyFields(
		(void *)record, (void *)live, g_MobileObjectFields,
		sizeof(g_MobileObjectFields) / sizeof(g_MobileObjectFields[0]),
		1);
	live->pWarheadGuidance =
		record->pWarheadGuidance
			? &g_projectileGuidanceStates
				  [(record->pWarheadGuidance - 1) /
				   sizeof(WarheadGuidanceState)]
			: NULL;
	live->pCraft =
		record->pCraft
			? &g_craftDataPoolBase[(record->pCraft - 1) /
					       sizeof(XvtSnapshotCraftData)]
			: NULL;
	live->pCharData =
		record->pCharData
			? &g_mobileObjectCharDataPool
				  [(record->pCharData - 1) /
				   sizeof(XvtSnapshotMobileObjectCharData)]
			: NULL;
}

#define CRAFT_DATA_FIELD(field)                                                \
	SNAPSHOT_FIELD(CraftData, XvtSnapshotCraftData, field)
static const XvtSnapshotField g_CraftDataFields[] = {
	CRAFT_DATA_FIELD(craftIndexInGroup),
	CRAFT_DATA_FIELD(modelIndex),
	CRAFT_DATA_FIELD(leader_obj_idx),
	CRAFT_DATA_FIELD(field_006),
	CRAFT_DATA_FIELD(objectKind),
	CRAFT_DATA_FIELD(missionAccountingDone),
	CRAFT_DATA_FIELD(aiSkill),
	CRAFT_DATA_FIELD(field_00B),
	CRAFT_DATA_FIELD(pitch),
	CRAFT_DATA_FIELD(yaw),
	CRAFT_DATA_FIELD(breakupPitchRate),
	CRAFT_DATA_FIELD(breakupYawRate),
	CRAFT_DATA_FIELD(beamEffectAccum),
	CRAFT_DATA_FIELD(sFoilState),
	CRAFT_DATA_FIELD(aiController.currentOrderSlot),
	CRAFT_DATA_FIELD(aiController.orderProgress),
	CRAFT_DATA_FIELD(aiController.skippedToOrder4),
	CRAFT_DATA_FIELD(aiController.pendingPlanId),
	CRAFT_DATA_FIELD(aiController.currentPlanId),
	CRAFT_DATA_FIELD(aiController.waypointIndex),
	CRAFT_DATA_FIELD(aiController.savedPlanId),
	CRAFT_DATA_FIELD(aiController.thinkInterval),
	CRAFT_DATA_FIELD(aiController.thinkTimer),
	CRAFT_DATA_FIELD(aiController.savedRandSeed),
	CRAFT_DATA_FIELD(aiController.targetObjIdx),
	CRAFT_DATA_FIELD(aiController.targetSignature),
	CRAFT_DATA_FIELD(aiController.targetComponent),
	CRAFT_DATA_FIELD(aiController.hasLiveTarget),
	CRAFT_DATA_FIELD(aiController.aimPointX),
	CRAFT_DATA_FIELD(aiController.aimPointY),
	CRAFT_DATA_FIELD(aiController.aimPointZ),
	CRAFT_DATA_FIELD(aiController.candidateTargetIdx),
	CRAFT_DATA_FIELD(aiController.escortTargetFG),
	CRAFT_DATA_FIELD(aiController.targetZAngle),
	CRAFT_DATA_FIELD(aiController.targetRoll),
	CRAFT_DATA_FIELD(aiController.targetXYAngle),
	CRAFT_DATA_FIELD(aiController.maneuverMode),
	CRAFT_DATA_FIELD(aiController.maneuverPhase),
	CRAFT_DATA_FIELD(aiController.maneuverTimer),
	CRAFT_DATA_FIELD(aiController.secondaryManeuverTimer),
	CRAFT_DATA_FIELD(carriedObjectIndex),
	CRAFT_DATA_FIELD(carrierObjIdx),
	CRAFT_DATA_FIELD(lastAttackerObjIdx),
	CRAFT_DATA_FIELD(lastHitMissionSecond),
	CRAFT_DATA_FIELD(aiFlight.threatObjIdx),
	CRAFT_DATA_FIELD(aiFlight.impactObjIdx),
	CRAFT_DATA_FIELD(aiFlight.goHomeFlag),
	CRAFT_DATA_FIELD(aiFlight.missionAbortedFlag),
	CRAFT_DATA_FIELD(aiFlight.departTimerFlag),
	CRAFT_DATA_FIELD(aiFlight.departClockHours),
	CRAFT_DATA_FIELD(aiFlight.departClockMinutes),
	CRAFT_DATA_FIELD(aiFlight.departClockSeconds),
	CRAFT_DATA_FIELD(aiFlight.warheadsFiredThisManeuver),
	CRAFT_DATA_FIELD(aiFlight.hitsThisManeuver),
	CRAFT_DATA_FIELD(aiFlight.boardedAccountingDone),
	CRAFT_DATA_FIELD(aiFlight.timesBoarded),
	CRAFT_DATA_FIELD(aiFlight.dockingAccountingDone),
	CRAFT_DATA_FIELD(aiFlight.dockedTargetCount),
	CRAFT_DATA_FIELD(aiFlight.dockedTargetSignatures),
	CRAFT_DATA_FIELD(aiFlight.maxSpeedCache),
	CRAFT_DATA_FIELD(aiFlight.motionScale),
	CRAFT_DATA_FIELD(aiFlight.climbState),
	CRAFT_DATA_FIELD(aiFlight.diveState),
	CRAFT_DATA_FIELD(aiFlight.pitchRate),
	CRAFT_DATA_FIELD(aiFlight.pitchAccel),
	CRAFT_DATA_FIELD(aiFlight.pitchState),
	CRAFT_DATA_FIELD(aiFlight.pitchThroughLoop),
	CRAFT_DATA_FIELD(aiFlight.pitchStepScale),
	CRAFT_DATA_FIELD(aiFlight.rollRate),
	CRAFT_DATA_FIELD(aiFlight.rollAccel),
	CRAFT_DATA_FIELD(aiFlight.rollState),
	CRAFT_DATA_FIELD(aiFlight.rollStep),
	CRAFT_DATA_FIELD(aiFlight.turnRate),
	CRAFT_DATA_FIELD(aiFlight.turnAccel),
	CRAFT_DATA_FIELD(aiFlight.turnState),
	CRAFT_DATA_FIELD(aiFlight.turnStep),
	CRAFT_DATA_FIELD(aiFlight.formationType),
	CRAFT_DATA_FIELD(aiFlight.separation),
	CRAFT_DATA_FIELD(craftOrdinal),
	CRAFT_DATA_FIELD(pushAccumX),
	CRAFT_DATA_FIELD(pushAccumY),
	CRAFT_DATA_FIELD(pushAccumZ),
	CRAFT_DATA_FIELD(throttleSpeed),
	CRAFT_DATA_FIELD(engineOverdriveOff),
	CRAFT_DATA_FIELD(commandedSpeed),
	CRAFT_DATA_FIELD(hullDamage),
	CRAFT_DATA_FIELD(systemDamageHullThreshold),
	CRAFT_DATA_FIELD(hullMax),
	CRAFT_DATA_FIELD(subsystemDamage),
	CRAFT_DATA_FIELD(damageStats.lastSystemHitTime),
	CRAFT_DATA_FIELD(damageStats.damageReceivedTotal),
	CRAFT_DATA_FIELD(damageStats.damageReceivedByPlayerOwnedCraft),
	CRAFT_DATA_FIELD(damageStats.damageFromCollision),
	CRAFT_DATA_FIELD(damageStats.damageFromStarship),
	CRAFT_DATA_FIELD(damageStats.damageFromMine),
	CRAFT_DATA_FIELD(damageStats.damageFromFlightGroupAmount),
	CRAFT_DATA_FIELD(damageStats.damageFromPlayer),
	CRAFT_DATA_FIELD(damageStats.damageFromAiSkill),
	CRAFT_DATA_FIELD(damageStats.installedHudFeatureMask),
	CRAFT_DATA_FIELD(damageStats.activeHudFeatureMask),
	CRAFT_DATA_FIELD(systemFlags),
	CRAFT_DATA_FIELD(workingSubsystems),
	CRAFT_DATA_FIELD(weaponFireInhibitTimer),
	CRAFT_DATA_FIELD(unusedMissionFlag),
	CRAFT_DATA_FIELD(notDisabledAccountingSuppress),
	CRAFT_DATA_FIELD(capturedByFlightGroup),
	CRAFT_DATA_FIELD(attackedByTeam),
	CRAFT_DATA_FIELD(identifiedOrderByTeam),
	CRAFT_DATA_FIELD(boardingState),
	CRAFT_DATA_FIELD(specialCargoName),
	CRAFT_DATA_FIELD(shieldEnergy),
	CRAFT_DATA_FIELD(shieldRechargeLevel),
	CRAFT_DATA_FIELD(shieldDistribMode),
	CRAFT_DATA_FIELD(cannonGroupCount),
	CRAFT_DATA_FIELD(laserRechargeLevel),
	CRAFT_DATA_FIELD(laserSlotCount),
	CRAFT_DATA_FIELD(laserState),
	CRAFT_DATA_FIELD(warheadLauncherCount),
	CRAFT_DATA_FIELD(warheadSlotTypeIds),
	CRAFT_DATA_FIELD(warheadLauncherFlags),
	CRAFT_DATA_FIELD(warheadLauncherCooldownTicks),
	CRAFT_DATA_FIELD(warheadLockTicks),
	CRAFT_DATA_FIELD(beamTypeId),
	CRAFT_DATA_FIELD(beamRechargeLevel),
	CRAFT_DATA_FIELD(beamCharge),
	CRAFT_DATA_FIELD(beamActive),
	CRAFT_DATA_FIELD(beamOutput),
	CRAFT_DATA_FIELD(beamTargetObjIdx),
	CRAFT_DATA_FIELD(cmTypeId),
	CRAFT_DATA_FIELD(cmAmmoCount),
	CRAFT_DATA_FIELD(chaffActiveSeconds),
	CRAFT_DATA_FIELD(cmFireCooldownTimer),
	CRAFT_DATA_FIELD(weaponStats),
	CRAFT_DATA_FIELD(field_256),
	CRAFT_DATA_FIELD(field_29F),
	CRAFT_DATA_FIELD(systemDisplaySlotBySystem),
	CRAFT_DATA_FIELD(systemHealth),
	CRAFT_DATA_FIELD(systemRepairSeconds),
	CRAFT_DATA_FIELD(componentState),
	CRAFT_DATA_FIELD(meshRotation),
	CRAFT_DATA_FIELD(componentHp),
	CRAFT_DATA_FIELD(playerCommandAvoidTargetObjIdx),
	CRAFT_DATA_FIELD(weaponSlots),
	CRAFT_DATA_FIELD(effectiveAiObjectSignature),
	CRAFT_DATA_FIELD(turretTargetStates),
	CRAFT_DATA_FIELD(field_3F2),
};
#undef CRAFT_DATA_FIELD

void XvtSnapshot_EncodeCraftData(XvtSnapshotCraftData *record,
				 const CraftData *live)
{
	XvtSnapshot_CopyFields(
		(void *)record, (void *)live, g_CraftDataFields,
		sizeof(g_CraftDataFields) / sizeof(g_CraftDataFields[0]), 0);
	for (size_t i = 0; i < sizeof(record->turretObjectLinks) /
				       sizeof(record->turretObjectLinks[0]);
	     ++i) {
		record->turretObjectLinks[i] =
			live->turretObjectLinks[i]
				? (uint32_t)((live->turretObjectLinks[i] -
					      g_objectTable) *
						     sizeof(XvtSnapshotObjectRecord) +
					     1)
				: 0;
	}
	record->effectiveAiObjectLink =
		live->effectiveAiObjectLink
			? (uint32_t)((live->effectiveAiObjectLink -
				      g_objectTable) *
					     sizeof(XvtSnapshotObjectRecord) +
				     1)
			: 0;
}

void XvtSnapshot_DecodeCraftData(CraftData *live,
				 const XvtSnapshotCraftData *record)
{
	XvtSnapshot_CopyFields(
		(void *)record, (void *)live, g_CraftDataFields,
		sizeof(g_CraftDataFields) / sizeof(g_CraftDataFields[0]), 1);
	for (size_t i = 0; i < sizeof(record->turretObjectLinks) /
				       sizeof(record->turretObjectLinks[0]);
	     ++i) {
		live->turretObjectLinks[i] =
			record->turretObjectLinks[i]
				? &g_objectTable
					  [(record->turretObjectLinks[i] - 1) /
					   sizeof(XvtSnapshotObjectRecord)]
				: NULL;
	}
	live->effectiveAiObjectLink =
		record->effectiveAiObjectLink
			? &g_objectTable[(record->effectiveAiObjectLink - 1) /
					 sizeof(XvtSnapshotObjectRecord)]
			: NULL;
}

#define MOBILE_OBJECT_CHAR_DATA_FIELD(field)                                   \
	SNAPSHOT_FIELD(MobileObjectCharData, XvtSnapshotMobileObjectCharData,  \
		       field)
static const XvtSnapshotField g_MobileObjectCharDataFields[] = {
	MOBILE_OBJECT_CHAR_DATA_FIELD(skillValue),
	MOBILE_OBJECT_CHAR_DATA_FIELD(reserved02),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.currentOrderSlot),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.orderProgress),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.skippedToOrder4),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.pendingPlanId),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.currentPlanId),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.waypointIndex),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.savedPlanId),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.thinkInterval),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.thinkTimer),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.savedRandSeed),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.targetObjIdx),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.targetSignature),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.targetComponent),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.hasLiveTarget),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.aimPointX),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.aimPointY),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.aimPointZ),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.candidateTargetIdx),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.escortTargetFG),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.targetZAngle),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.targetRoll),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.targetXYAngle),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.maneuverMode),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.maneuverPhase),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.maneuverTimer),
	MOBILE_OBJECT_CHAR_DATA_FIELD(aiController.secondaryManeuverTimer),
	MOBILE_OBJECT_CHAR_DATA_FIELD(reserved40),
};
#undef MOBILE_OBJECT_CHAR_DATA_FIELD

void XvtSnapshot_EncodeMobileObjectCharData(
	XvtSnapshotMobileObjectCharData *record,
	const MobileObjectCharData *live)
{
	XvtSnapshot_CopyFields((void *)record, (void *)live,
			       g_MobileObjectCharDataFields,
			       sizeof(g_MobileObjectCharDataFields) /
				       sizeof(g_MobileObjectCharDataFields[0]),
			       0);
}

void XvtSnapshot_DecodeMobileObjectCharData(
	MobileObjectCharData *live,
	const XvtSnapshotMobileObjectCharData *record)
{
	XvtSnapshot_CopyFields((void *)record, (void *)live,
			       g_MobileObjectCharDataFields,
			       sizeof(g_MobileObjectCharDataFields) /
				       sizeof(g_MobileObjectCharDataFields[0]),
			       1);
}

#define PLAYER_DATA_FIELD(field)                                               \
	SNAPSHOT_FIELD(PlayerData, XvtSnapshotPlayerData, field)
static const XvtSnapshotField g_PlayerDataFields[] = {
	PLAYER_DATA_FIELD(objectIndex),
	PLAYER_DATA_FIELD(boundObjectSignature),
	PLAYER_DATA_FIELD(pilotRating),
	PLAYER_DATA_FIELD(iff),
	PLAYER_DATA_FIELD(team),
	PLAYER_DATA_FIELD(boundFlightGroupIdx),
	PLAYER_DATA_FIELD(participationState),
	PLAYER_DATA_FIELD(awaitingNewCraft),
	PLAYER_DATA_FIELD(boundCraftEngineGlowCount),
	PLAYER_DATA_FIELD(mapCameraState),
	PLAYER_DATA_FIELD(hyperspacePhase),
	PLAYER_DATA_FIELD(hyperspaceRuntime),
	PLAYER_DATA_FIELD(targetBoxEnabled),
	PLAYER_DATA_FIELD(currentTargetObjectIdx),
	PLAYER_DATA_FIELD(targetCycleStart),
	PLAYER_DATA_FIELD(targetPresetSlot),
	PLAYER_DATA_FIELD(missileLockState),
	PLAYER_DATA_FIELD(selectedWeaponBank),
	PLAYER_DATA_FIELD(selectedWeaponMode),
	PLAYER_DATA_FIELD(selectedTargetComponent),
	PLAYER_DATA_FIELD(targetingState),
	PLAYER_DATA_FIELD(engineWashSourceObjIdx),
	PLAYER_DATA_FIELD(engineWashStrength),
	PLAYER_DATA_FIELD(throttlePreset),
	PLAYER_DATA_FIELD(laserPreset),
	PLAYER_DATA_FIELD(shieldPreset),
	PLAYER_DATA_FIELD(beamPreset),
	PLAYER_DATA_FIELD(savedCraftSettings),
	PLAYER_DATA_FIELD(savedHudViewState),
	PLAYER_DATA_FIELD(pendingActionId),
	PLAYER_DATA_FIELD(pendingActionParam),
	PLAYER_DATA_FIELD(pendingActionIssuerPlayerIdx),
	PLAYER_DATA_FIELD(yawRollSwap),
	PLAYER_DATA_FIELD(smoothedInputYaw),
	PLAYER_DATA_FIELD(smoothedInputPitch),
	PLAYER_DATA_FIELD(savedKeyMods),
	PLAYER_DATA_FIELD(keyModsHoldTimer),
	PLAYER_DATA_FIELD(hardpointWorldX),
	PLAYER_DATA_FIELD(hardpointWorldY),
	PLAYER_DATA_FIELD(hardpointWorldZ),
	PLAYER_DATA_FIELD(prevHardpointWorldX),
	PLAYER_DATA_FIELD(prevHardpointWorldY),
	PLAYER_DATA_FIELD(prevHardpointWorldZ),
	PLAYER_DATA_FIELD(missionStats),
	PLAYER_DATA_FIELD(warheadsFired),
	PLAYER_DATA_FIELD(perMissionKills),
	PLAYER_DATA_FIELD(msgText),
	PLAYER_DATA_FIELD(msgLength),
	PLAYER_DATA_FIELD(chatRecipientMode),
	PLAYER_DATA_FIELD(viewState.cameraWorldX),
	PLAYER_DATA_FIELD(viewState.cameraWorldY),
	PLAYER_DATA_FIELD(viewState.cameraWorldZ),
	PLAYER_DATA_FIELD(viewState.cameraFocusObjIdx),
	PLAYER_DATA_FIELD(viewState.aimTargetIdx),
	PLAYER_DATA_FIELD(viewState.viewPitch),
	PLAYER_DATA_FIELD(viewState.viewYaw),
	PLAYER_DATA_FIELD(viewState.viewRoll),
	PLAYER_DATA_FIELD(viewState.viewAngleD),
	PLAYER_DATA_FIELD(viewState.hudAimX),
	PLAYER_DATA_FIELD(viewState.hudAimY),
	PLAYER_DATA_FIELD(viewState.hudStateLive),
	PLAYER_DATA_FIELD(viewState.hudStateMirror),
	PLAYER_DATA_FIELD(viewState.hudAimXSnapState),
	PLAYER_DATA_FIELD(viewState.savedHudStateByte),
	PLAYER_DATA_FIELD(viewState.field_20),
	PLAYER_DATA_FIELD(viewState.savedHudAimX),
	PLAYER_DATA_FIELD(viewState.savedHudAimY),
	PLAYER_DATA_FIELD(viewState.playerInputBlocked),
	PLAYER_DATA_FIELD(viewState.cameraDistanceStep),
	PLAYER_DATA_FIELD(viewState.externalCameraActive),
	PLAYER_DATA_FIELD(viewState.cameraDistance),
	PLAYER_DATA_FIELD(viewState.transitionTimer),
	PLAYER_DATA_FIELD(viewState.cameraRollHistory),
	PLAYER_DATA_FIELD(viewState.cameraPitchHistory),
	PLAYER_DATA_FIELD(viewState.cameraYawHistory),
	PLAYER_DATA_FIELD(viewState.field_199),
	PLAYER_DATA_FIELD(network.flightResolutionMode),
	PLAYER_DATA_FIELD(network.directPlayId),
	PLAYER_DATA_FIELD(lockstepTimestamp),
	PLAYER_DATA_FIELD(savedX),
	PLAYER_DATA_FIELD(savedY),
	PLAYER_DATA_FIELD(savedZ),
	PLAYER_DATA_FIELD(savedRoll),
	PLAYER_DATA_FIELD(savedPitch),
	PLAYER_DATA_FIELD(savedYaw),
	PLAYER_DATA_FIELD(savedLifetimeTimer),
	PLAYER_DATA_FIELD(savedSpeed),
	PLAYER_DATA_FIELD(savedSpeedRemainder),
	PLAYER_DATA_FIELD(savedRollImpulseRate),
	PLAYER_DATA_FIELD(savedObjectSignature),
	PLAYER_DATA_FIELD(savedAwaitingNewCraft),
	PLAYER_DATA_FIELD(pendingActionTimer),
	PLAYER_DATA_FIELD(beamFireCooldownTimer),
	PLAYER_DATA_FIELD(field_5B5),
	PLAYER_DATA_FIELD(nextEngineWashCheckTime),
};
#undef PLAYER_DATA_FIELD

void XvtSnapshot_EncodePlayerData(XvtSnapshotPlayerData *record,
				  const PlayerData *live)
{
	XvtSnapshot_CopyFields(
		(void *)record, (void *)live, g_PlayerDataFields,
		sizeof(g_PlayerDataFields) / sizeof(g_PlayerDataFields[0]), 0);
}

void XvtSnapshot_DecodePlayerData(PlayerData *live,
				  const XvtSnapshotPlayerData *record)
{
	XvtSnapshot_CopyFields(
		(void *)record, (void *)live, g_PlayerDataFields,
		sizeof(g_PlayerDataFields) / sizeof(g_PlayerDataFields[0]), 1);
}

#define FLIGHT_MISSION_STATE_FIELD(field)                                      \
	SNAPSHOT_FIELD(FlightMissionState, XvtSnapshotFlightMissionState, field)
static const XvtSnapshotField g_FlightMissionStateFields[] = {
	FLIGHT_MISSION_STATE_FIELD(missionEndPending),
	FLIGHT_MISSION_STATE_FIELD(provingGroundsModeActive),
	FLIGHT_MISSION_STATE_FIELD(provingGroundsCraftType),
	FLIGHT_MISSION_STATE_FIELD(provingGroundsLevel),
	FLIGHT_MISSION_STATE_FIELD(provingGroundsScore),
	FLIGHT_MISSION_STATE_FIELD(reserved08),
	FLIGHT_MISSION_STATE_FIELD(provingGroundsCheckpointsPassed),
	FLIGHT_MISSION_STATE_FIELD(reserved0C),
	FLIGHT_MISSION_STATE_FIELD(provingGroundsCheckpointsRemaining),
	FLIGHT_MISSION_STATE_FIELD(provingGroundsTargetsDestroyed),
	FLIGHT_MISSION_STATE_FIELD(provingGroundsTimeBonus),
	FLIGHT_MISSION_STATE_FIELD(difficulty),
	FLIGHT_MISSION_STATE_FIELD(collisionsEnabled),
	FLIGHT_MISSION_STATE_FIELD(craftJumpingEnabled),
	FLIGHT_MISSION_STATE_FIELD(randomVariationEnabled),
	FLIGHT_MISSION_STATE_FIELD(battleLengthIndex),
	FLIGHT_MISSION_STATE_FIELD(locatePlayersEnabled),
	FLIGHT_MISSION_STATE_FIELD(aiOpponentsEnabled),
	FLIGHT_MISSION_STATE_FIELD(playerFlightGroupWaveMode),
	FLIGHT_MISSION_STATE_FIELD(missionTimeLimitMinutes),
	FLIGHT_MISSION_STATE_FIELD(teamVictoryTimeLimitMinutes),
	FLIGHT_MISSION_STATE_FIELD(teamVictoryTimeLimitStarted),
	FLIGHT_MISSION_STATE_FIELD(craftImpactBounceEnabled),
	FLIGHT_MISSION_STATE_FIELD(connectedPlayerCount),
	FLIGHT_MISSION_STATE_FIELD(maxConnectedPlayerCountThisMission),
	FLIGHT_MISSION_STATE_FIELD(runtime.teamScores),
	FLIGHT_MISSION_STATE_FIELD(runtime.teamKillStats),
	FLIGHT_MISSION_STATE_FIELD(runtime.teamFgInspectedCapturedCounts),
	FLIGHT_MISSION_STATE_FIELD(runtime.teamFgDesignationCode),
	FLIGHT_MISSION_STATE_FIELD(runtime.globalPrimaryGoalStatus),
	FLIGHT_MISSION_STATE_FIELD(runtime.globalGoalStatusUnused),
	FLIGHT_MISSION_STATE_FIELD(runtime.globalBonusGoalStatus),
	FLIGHT_MISSION_STATE_FIELD(runtime.teamGlobalGoalState),
	FLIGHT_MISSION_STATE_FIELD(runtime.teamGoalStatus),
	FLIGHT_MISSION_STATE_FIELD(runtime.globalGoalTriggerCounts),
	FLIGHT_MISSION_STATE_FIELD(runtime.teamMissionCompletionTimeSeconds),
	FLIGHT_MISSION_STATE_FIELD(runtime.teamHasCountableCraft),
	FLIGHT_MISSION_STATE_FIELD(runtime.teamReinforcementsCalled),
	FLIGHT_MISSION_STATE_FIELD(messageTriggered),
	FLIGHT_MISSION_STATE_FIELD(messageDelayCountdown),
	FLIGHT_MISSION_STATE_FIELD(globalUnitCraftCount),
};
#undef FLIGHT_MISSION_STATE_FIELD
#undef SNAPSHOT_FIELD

void XvtSnapshot_EncodeFlightMissionState(XvtSnapshotFlightMissionState *record,
					  const FlightMissionState *live)
{
	XvtSnapshot_CopyFields(record, (void *)live, g_FlightMissionStateFields,
			       sizeof(g_FlightMissionStateFields) /
				       sizeof(g_FlightMissionStateFields[0]),
			       0);
}

void XvtSnapshot_DecodeFlightMissionState(
	FlightMissionState *live, const XvtSnapshotFlightMissionState *record)
{
	XvtSnapshot_CopyFields((void *)record, live, g_FlightMissionStateFields,
			       sizeof(g_FlightMissionStateFields) /
				       sizeof(g_FlightMissionStateFields[0]),
			       1);
}
