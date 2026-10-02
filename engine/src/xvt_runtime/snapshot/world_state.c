#include "xvt_runtime/snapshot/world_state.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/net/flight_sync.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/snapshot/records.h"
#include "xvt_runtime/snapshot/world_checksum.h"
#include "xvt_runtime/timing/flight_timing.h"
#include <string.h>

/* These trailer records already have their original fixed-width layout. */
typedef char
	xvt_snapshot_fg_size[(sizeof(MissionFgRuntimeStats) == 294) ? 1 : -1];
typedef char xvt_snapshot_guidance_size[(sizeof(WarheadGuidanceState) == 10)
						? 1
						: -1];

static uint8_t *XvtSnapshot_SaveObjects(uint8_t *cursor)
{
	for (int i = 0;
	     i < g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
	     ++i) {
		if (i >= g_localTransientSlotStart &&
		    i < g_localDebrisSlotEnd) {
			continue;
		}
		const ObjectRecord *object = &g_objectTable[i];
		*cursor++ = object->objectType;
		if (!object->objectType) {
			continue;
		}
		XvtSnapshot_EncodeObjectRecord(
			(XvtSnapshotObjectRecord *)cursor, object);
		cursor += sizeof(XvtSnapshotObjectRecord);
		const MobileObject *mobile = object->mobj;
		if (!mobile) {
			continue;
		}
		XvtSnapshot_EncodeMobileObject(
			(XvtSnapshotMobileObject *)cursor, mobile);
		cursor += sizeof(XvtSnapshotMobileObject);
		if (mobile->pCraft) {
			XvtSnapshot_EncodeCraftData(
				(XvtSnapshotCraftData *)cursor, mobile->pCraft);
			cursor += sizeof(XvtSnapshotCraftData);
		}
		if (mobile->pWarheadGuidance) {
			memcpy(cursor, mobile->pWarheadGuidance,
			       sizeof(WarheadGuidanceState));
			cursor += sizeof(WarheadGuidanceState);
		}
		if (mobile->pCharData) {
			XvtSnapshot_EncodeMobileObjectCharData(
				(XvtSnapshotMobileObjectCharData *)cursor,
				mobile->pCharData);
			cursor += sizeof(XvtSnapshotMobileObjectCharData);
		}
	}
	return cursor;
}

static uint8_t *XvtSnapshot_RestoreObjects(uint8_t *cursor)
{
	for (int i = 0;
	     i < g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
	     ++i) {
		if (i >= g_localTransientSlotStart &&
		    i < g_localDebrisSlotEnd) {
			continue;
		}
		ObjectRecord *object = &g_objectTable[i];
		object->objectType = *cursor++;
		if (!object->objectType) {
			/* The original clears only the scalar prefix, preserving pool ownership. */
			MobileObject *mobile = object->mobj;
			memset(object, 0, sizeof(*object));
			object->mobj = mobile;
			object->playerOwnerIdx = -1;
			if (mobile) {
				memset(mobile, 0,
				       offsetof(MobileObject, moveVectorDirty));
				mobile->iff = UINT8_MAX;
				if (mobile->pCraft) {
					XvtSnapshotCraftData craft;
					XvtSnapshot_EncodeCraftData(
						&craft, mobile->pCraft);
					memset(&craft, 0, 0x412);
					XvtSnapshot_DecodeCraftData(
						mobile->pCraft, &craft);
				}
				if (mobile->pWarheadGuidance) {
					memset(mobile->pWarheadGuidance, 0,
					       sizeof(WarheadGuidanceState));
				}
				if (mobile->pCharData) {
					memset(mobile->pCharData, 0,
					       sizeof(MobileObjectCharData));
				}
			}
			continue;
		}
		XvtSnapshot_DecodeObjectRecord(
			object, (const XvtSnapshotObjectRecord *)cursor);
		cursor += sizeof(XvtSnapshotObjectRecord);
		MobileObject *mobile = object->mobj;
		if (!mobile) {
			continue;
		}
		XvtSnapshot_DecodeMobileObject(
			mobile, (const XvtSnapshotMobileObject *)cursor);
		cursor += sizeof(XvtSnapshotMobileObject);
		if (mobile->pCraft) {
			XvtSnapshot_DecodeCraftData(
				mobile->pCraft,
				(const XvtSnapshotCraftData *)cursor);
			cursor += sizeof(XvtSnapshotCraftData);
		}
		if (mobile->pWarheadGuidance) {
			memcpy(mobile->pWarheadGuidance, cursor,
			       sizeof(WarheadGuidanceState));
			cursor += sizeof(WarheadGuidanceState);
		}
		if (mobile->pCharData) {
			XvtSnapshot_DecodeMobileObjectCharData(
				mobile->pCharData,
				(const XvtSnapshotMobileObjectCharData *)
					cursor);
			cursor += sizeof(XvtSnapshotMobileObjectCharData);
		}
	}
	return cursor;
}

size_t XvtSnapshot_Encode(uint8_t *image, size_t capacity)
{
	if (!image || !XvtSnapshot_CalculateSize() ||
	    capacity < XvtSnapshot_CalculateSize()) {
		return 0;
	}
	uint8_t *cursor = XvtSnapshot_SaveObjects(image);
	memcpy(cursor, &g_missionElapsedClock, sizeof(g_missionElapsedClock));
	cursor += sizeof(g_missionElapsedClock);
	memcpy(cursor, &g_missionCountdownClock,
	       sizeof(g_missionCountdownClock));
	cursor += sizeof(g_missionCountdownClock);
	memcpy(cursor, &g_missionHeader, 162);
	cursor += 162;
	memcpy(cursor, g_missionFgStats,
	       294 * (int16_t)g_missionHeader.numFlightGroups);
	cursor += 294 * (int16_t)g_missionHeader.numFlightGroups;
	memcpy(cursor, g_missionFlightGroups,
	       1382 * (int16_t)g_missionHeader.numFlightGroups);
	cursor += 1382 * (int16_t)g_missionHeader.numFlightGroups;
	XvtSnapshot_EncodeFlightMissionState(
		(XvtSnapshotFlightMissionState *)cursor, &g_flightMissionState);
	cursor += 3376;
	memcpy(cursor, &g_flightGlobalCountdownTimers, 22);
	cursor += 22;
	memcpy(cursor, &g_missionFileVersion, sizeof(g_missionFileVersion));
	cursor += sizeof(g_missionFileVersion);
	memcpy(cursor, &g_flightPlayerCount, sizeof(g_flightPlayerCount));
	cursor += sizeof(g_flightPlayerCount);
	*cursor++ = g_worldStateReservedByte;

	memcpy(cursor, &g_craftDataPoolCapacity,
	       sizeof(g_craftDataPoolCapacity));
	cursor += sizeof(g_craftDataPoolCapacity);
	memcpy(cursor, &g_mobileObjectCharDataCount,
	       sizeof(g_mobileObjectCharDataCount));
	cursor += sizeof(g_mobileObjectCharDataCount);
	memcpy(cursor, &g_projectileObjectSlotsTotal,
	       sizeof(g_projectileObjectSlotsTotal));
	cursor += sizeof(g_projectileObjectSlotsTotal);
	memcpy(cursor, &g_debrisObjectSlotsTotal,
	       sizeof(g_debrisObjectSlotsTotal));
	cursor += sizeof(g_debrisObjectSlotsTotal);
	memcpy(cursor, &g_worldStateDebrisSlotCount,
	       sizeof(g_worldStateDebrisSlotCount));
	cursor += sizeof(g_worldStateDebrisSlotCount);
	memcpy(cursor, &g_regionMainObjectSlotStart,
	       sizeof(g_regionMainObjectSlotStart));
	cursor += sizeof(g_regionMainObjectSlotStart);
	memcpy(cursor, &g_activeRegionObjectSlotStart,
	       sizeof(g_activeRegionObjectSlotStart));
	cursor += sizeof(g_activeRegionObjectSlotStart);
	memcpy(cursor, &g_activeRegionCraftObjectSlotEnd,
	       sizeof(g_activeRegionCraftObjectSlotEnd));
	cursor += sizeof(g_activeRegionCraftObjectSlotEnd);
	memcpy(cursor, &g_mobileObjectCharDataSlotStart,
	       sizeof(g_mobileObjectCharDataSlotStart));
	cursor += sizeof(g_mobileObjectCharDataSlotStart);
	memcpy(cursor, &g_mobileObjectCharDataSlotEnd,
	       sizeof(g_mobileObjectCharDataSlotEnd));
	cursor += sizeof(g_mobileObjectCharDataSlotEnd);
	memcpy(cursor, &g_projectileObjectSlotStart,
	       sizeof(g_projectileObjectSlotStart));
	cursor += sizeof(g_projectileObjectSlotStart);
	memcpy(cursor, &g_projectileObjectSlotEnd,
	       sizeof(g_projectileObjectSlotEnd));
	cursor += sizeof(g_projectileObjectSlotEnd);
	memcpy(cursor, &g_debrisObjectSlotStart,
	       sizeof(g_debrisObjectSlotStart));
	cursor += sizeof(g_debrisObjectSlotStart);
	memcpy(cursor, &g_debrisObjectSlotEnd, sizeof(g_debrisObjectSlotEnd));
	cursor += sizeof(g_debrisObjectSlotEnd);
	memcpy(cursor, &g_explosionObjectSlotStart,
	       sizeof(g_explosionObjectSlotStart));
	cursor += sizeof(g_explosionObjectSlotStart);
	memcpy(cursor, &g_explosionObjectSlotEnd,
	       sizeof(g_explosionObjectSlotEnd));
	cursor += sizeof(g_explosionObjectSlotEnd);
	memcpy(cursor, &g_localTransientSlotStart,
	       sizeof(g_localTransientSlotStart));
	cursor += sizeof(g_localTransientSlotStart);
	memcpy(cursor, &g_localDebrisSlotEnd, sizeof(g_localDebrisSlotEnd));
	cursor += sizeof(g_localDebrisSlotEnd);
	memcpy(cursor, &g_regionMainObjectSlotEnd,
	       sizeof(g_regionMainObjectSlotEnd));
	cursor += sizeof(g_regionMainObjectSlotEnd);
	memcpy(cursor, &g_regionStaticObjectSlotCount,
	       sizeof(g_regionStaticObjectSlotCount));
	cursor += sizeof(g_regionStaticObjectSlotCount);
	memcpy(cursor, g_planTable, 21760);
	cursor += 21760;
	memcpy(cursor, &g_planCount, sizeof(g_planCount));
	cursor += sizeof(g_planCount);
	memcpy(cursor, &g_unusedWorldStateSerializedDword,
	       sizeof(g_unusedWorldStateSerializedDword));
	cursor += sizeof(g_unusedWorldStateSerializedDword);
	memcpy(cursor, g_builtinPlanIdByNameIndex, 256);
	cursor += 256;
	memcpy(cursor, &g_gameRandFeedbackState,
	       sizeof(g_gameRandFeedbackState));
	cursor += sizeof(g_gameRandFeedbackState);
	memcpy(cursor, &g_nextObjectSignature, sizeof(g_nextObjectSignature));
	cursor += sizeof(g_nextObjectSignature);
	memcpy(cursor, &g_laserFireTimestampTrackingEnabled,
	       sizeof(g_laserFireTimestampTrackingEnabled));
	cursor += sizeof(g_laserFireTimestampTrackingEnabled);
	for (int i = 0; i < 8; ++i) {
		XvtSnapshot_EncodePlayerData((XvtSnapshotPlayerData *)cursor,
					     &g_players[i]);
		cursor += sizeof(XvtSnapshotPlayerData);
	}
	return XvtFlightTiming_IsNetwork125()
		       ? XvtFlightCheckpoint_Append(image, cursor - image)
		       : (size_t)(cursor - image);
}

static void XvtSnapshot_DecodePrefix(const uint8_t *image)
{
	uint8_t *cursor = XvtSnapshot_RestoreObjects((uint8_t *)image);
	memcpy(&g_missionElapsedClock, cursor, sizeof(g_missionElapsedClock));
	cursor += sizeof(g_missionElapsedClock);
	memcpy(&g_missionCountdownClock, cursor,
	       sizeof(g_missionCountdownClock));
	cursor += sizeof(g_missionCountdownClock);
	memcpy(&g_missionHeader, cursor, sizeof(g_missionHeader));
	cursor += sizeof(g_missionHeader);
	memcpy(g_missionFgStats, cursor,
	       sizeof(*g_missionFgStats) * g_missionHeader.numFlightGroups);
	cursor += sizeof(*g_missionFgStats) * g_missionHeader.numFlightGroups;
	memcpy(g_missionFlightGroups, cursor,
	       sizeof(*g_missionFlightGroups) *
		       g_missionHeader.numFlightGroups);
	cursor += sizeof(*g_missionFlightGroups) *
		  g_missionHeader.numFlightGroups;
	XvtSnapshot_DecodeFlightMissionState(
		&g_flightMissionState,
		(const XvtSnapshotFlightMissionState *)cursor);
	cursor += sizeof(XvtSnapshotFlightMissionState);
	memcpy(&g_flightGlobalCountdownTimers, cursor,
	       sizeof(g_flightGlobalCountdownTimers));
	cursor += sizeof(g_flightGlobalCountdownTimers);
	memcpy(&g_missionFileVersion, cursor, sizeof(g_missionFileVersion));
	cursor += sizeof(g_missionFileVersion);
	memcpy(&g_flightPlayerCount, cursor, sizeof(g_flightPlayerCount));
	cursor += sizeof(g_flightPlayerCount);
	g_worldStateReservedByte = *cursor++;
	memcpy(&g_craftDataPoolCapacity, cursor,
	       sizeof(g_craftDataPoolCapacity));
	cursor += sizeof(g_craftDataPoolCapacity);
	memcpy(&g_mobileObjectCharDataCount, cursor,
	       sizeof(g_mobileObjectCharDataCount));
	cursor += sizeof(g_mobileObjectCharDataCount);
	memcpy(&g_projectileObjectSlotsTotal, cursor,
	       sizeof(g_projectileObjectSlotsTotal));
	cursor += sizeof(g_projectileObjectSlotsTotal);
	memcpy(&g_debrisObjectSlotsTotal, cursor,
	       sizeof(g_debrisObjectSlotsTotal));
	cursor += sizeof(g_debrisObjectSlotsTotal);
	memcpy(&g_worldStateDebrisSlotCount, cursor,
	       sizeof(g_worldStateDebrisSlotCount));
	cursor += sizeof(g_worldStateDebrisSlotCount);
	memcpy(&g_regionMainObjectSlotStart, cursor,
	       sizeof(g_regionMainObjectSlotStart));
	cursor += sizeof(g_regionMainObjectSlotStart);
	memcpy(&g_activeRegionObjectSlotStart, cursor,
	       sizeof(g_activeRegionObjectSlotStart));
	cursor += sizeof(g_activeRegionObjectSlotStart);
	memcpy(&g_activeRegionCraftObjectSlotEnd, cursor,
	       sizeof(g_activeRegionCraftObjectSlotEnd));
	cursor += sizeof(g_activeRegionCraftObjectSlotEnd);
	memcpy(&g_mobileObjectCharDataSlotStart, cursor,
	       sizeof(g_mobileObjectCharDataSlotStart));
	cursor += sizeof(g_mobileObjectCharDataSlotStart);
	memcpy(&g_mobileObjectCharDataSlotEnd, cursor,
	       sizeof(g_mobileObjectCharDataSlotEnd));
	cursor += sizeof(g_mobileObjectCharDataSlotEnd);
	memcpy(&g_projectileObjectSlotStart, cursor,
	       sizeof(g_projectileObjectSlotStart));
	cursor += sizeof(g_projectileObjectSlotStart);
	memcpy(&g_projectileObjectSlotEnd, cursor,
	       sizeof(g_projectileObjectSlotEnd));
	cursor += sizeof(g_projectileObjectSlotEnd);
	memcpy(&g_debrisObjectSlotStart, cursor,
	       sizeof(g_debrisObjectSlotStart));
	cursor += sizeof(g_debrisObjectSlotStart);
	memcpy(&g_debrisObjectSlotEnd, cursor, sizeof(g_debrisObjectSlotEnd));
	cursor += sizeof(g_debrisObjectSlotEnd);
	memcpy(&g_explosionObjectSlotStart, cursor,
	       sizeof(g_explosionObjectSlotStart));
	cursor += sizeof(g_explosionObjectSlotStart);
	memcpy(&g_explosionObjectSlotEnd, cursor,
	       sizeof(g_explosionObjectSlotEnd));
	cursor += sizeof(g_explosionObjectSlotEnd);
	memcpy(&g_localTransientSlotStart, cursor,
	       sizeof(g_localTransientSlotStart));
	cursor += sizeof(g_localTransientSlotStart);
	memcpy(&g_localDebrisSlotEnd, cursor, sizeof(g_localDebrisSlotEnd));
	cursor += sizeof(g_localDebrisSlotEnd);
	memcpy(&g_regionMainObjectSlotEnd, cursor,
	       sizeof(g_regionMainObjectSlotEnd));
	cursor += sizeof(g_regionMainObjectSlotEnd);
	memcpy(&g_regionStaticObjectSlotCount, cursor,
	       sizeof(g_regionStaticObjectSlotCount));
	cursor += sizeof(g_regionStaticObjectSlotCount);
	memcpy(g_planTable, cursor, sizeof(g_planTable));
	cursor += sizeof(g_planTable);
	memcpy(&g_planCount, cursor, sizeof(g_planCount));
	cursor += sizeof(g_planCount);
	memcpy(&g_unusedWorldStateSerializedDword, cursor,
	       sizeof(g_unusedWorldStateSerializedDword));
	cursor += sizeof(g_unusedWorldStateSerializedDword);
	memcpy(g_builtinPlanIdByNameIndex, cursor,
	       sizeof(g_builtinPlanIdByNameIndex));
	cursor += sizeof(g_builtinPlanIdByNameIndex);
	memcpy(&g_gameRandFeedbackState, cursor,
	       sizeof(g_gameRandFeedbackState));
	cursor += sizeof(g_gameRandFeedbackState);
	memcpy(&g_nextObjectSignature, cursor, sizeof(g_nextObjectSignature));
	cursor += sizeof(g_nextObjectSignature);
	memcpy(&g_laserFireTimestampTrackingEnabled, cursor,
	       sizeof(g_laserFireTimestampTrackingEnabled));
	cursor += sizeof(g_laserFireTimestampTrackingEnabled);
	for (int i = 0; i < 8; ++i) {
		XvtSnapshot_DecodePlayerData(
			&g_players[i], (const XvtSnapshotPlayerData *)cursor);
		cursor += sizeof(XvtSnapshotPlayerData);
	}
}

enum FlightWorldStatePresenceFlags {
	FLIGHT_WORLDSTATE_HAS_OBJECT = 0x01,
	FLIGHT_WORLDSTATE_HAS_MOBILE = 0x02,
	FLIGHT_WORLDSTATE_HAS_CRAFT = 0x04,
	FLIGHT_WORLDSTATE_HAS_WARHEAD_GUIDANCE = 0x08,
	FLIGHT_WORLDSTATE_HAS_CHAR_DATA = 0x10,
	FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG = 0x80,
	FLIGHT_WORLDSTATE_EMPTY_RUN_LENGTH_MASK = 0x7F,
	FLIGHT_WORLDSTATE_MAX_EMPTY_RUN = 0x7E
};

size_t XvtSnapshot_CalculateSize(void)
{
	size_t size;
	if (g_regionMainObjectSlotEnd < 0 ||
	    g_regionStaticObjectSlotCount < 0 ||
	    (size_t)g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount >
		    UINT16_MAX ||
	    (unsigned)g_missionHeader.numFlightGroups > INT16_MAX ||
	    (unsigned)g_craftDataPoolCapacity > UINT16_MAX ||
	    (unsigned)g_mobileObjectCharDataCount > UINT16_MAX ||
	    (unsigned)g_projectileObjectSlotsTotal > UINT16_MAX) {
		return 0;
	}

	/* The fixed trailer contains 24 dwords, three words, and one byte around the fixed arrays. */
	size = (int)(2 * sizeof(MissionClock) + sizeof(MissionHeader) +
		     sizeof(XvtSnapshotFlightMissionState) +
		     sizeof(FlightGlobalCountdownTimers) + sizeof(g_planTable) +
		     sizeof(g_builtinPlanIdByNameIndex) +
		     8 * sizeof(XvtSnapshotPlayerData) + 24 * sizeof(uint32_t) +
		     3 * sizeof(uint16_t) + sizeof(uint8_t) +
		     sizeof(WarheadGuidanceState));
	size += (int)(sizeof(MissionFgRuntimeStats) +
		      sizeof(MissionFlightGroup)) *
		g_missionHeader.numFlightGroups;
	size += (int)(sizeof(uint8_t) + sizeof(XvtSnapshotObjectRecord)) *
		g_regionStaticObjectSlotCount;
	size += (int)(sizeof(uint8_t) + sizeof(XvtSnapshotObjectRecord) +
		      sizeof(XvtSnapshotMobileObject)) *
		g_regionMainObjectSlotEnd;
	size += (int)sizeof(XvtSnapshotMobileObjectCharData) *
		(int)g_mobileObjectCharDataCount;
	size += (int)sizeof(WarheadGuidanceState) *
		(int)g_projectileObjectSlotsTotal;
	size += (int)sizeof(XvtSnapshotCraftData) * g_craftDataPoolCapacity;
	return (size_t)size + (XvtFlightTiming_IsNetwork125()
				       ? XvtFlightCheckpoint_Maximum()
				       : 0);
}

int XvtSnapshot_BuildPresenceMap(uint8_t *outMap, uint8_t *worldState)
{
	int emptyRunLength;
	uint8_t *mapStart;
	int objectIndex;

	mapStart = outMap;
	{
		int objectCount = g_regionStaticObjectSlotCount +
				  g_regionMainObjectSlotEnd;
		memcpy(outMap, &objectCount, sizeof(objectCount));
	}
	outMap += sizeof(int);
	emptyRunLength = 0;
	objectIndex = 0;
	while (objectIndex <
	       g_regionStaticObjectSlotCount + g_regionMainObjectSlotEnd) {
		if (objectIndex < g_localTransientSlotStart ||
		    objectIndex >= g_localDebrisSlotEnd) {
			uint8_t componentFlags;

			componentFlags = 0;
			if (*worldState++ != 0) {
				const XvtSnapshotObjectRecord *objectState;
				uint32_t mobileObjectPresent;

				componentFlags = FLIGHT_WORLDSTATE_HAS_OBJECT;
				objectState = (const XvtSnapshotObjectRecord *)
					worldState;
				worldState += sizeof(*objectState);
				memcpy(&mobileObjectPresent, &objectState->mobj,
				       sizeof(mobileObjectPresent));
				if (mobileObjectPresent != 0) {
					const XvtSnapshotMobileObject
						*mobileObjectState;
					uint32_t craftPresent;
					uint32_t warheadGuidancePresent;
					uint32_t charDataPresent;

					componentFlags |=
						FLIGHT_WORLDSTATE_HAS_MOBILE;
					mobileObjectState =
						(const XvtSnapshotMobileObject
							 *)worldState;
					worldState +=
						sizeof(*mobileObjectState);
					memcpy(&craftPresent,
					       &mobileObjectState->pCraft,
					       sizeof(craftPresent));
					if (craftPresent != 0) {
						componentFlags |=
							FLIGHT_WORLDSTATE_HAS_CRAFT;
						worldState += sizeof(
							XvtSnapshotCraftData);
					}
					memcpy(&warheadGuidancePresent,
					       &mobileObjectState
							->pWarheadGuidance,
					       sizeof(warheadGuidancePresent));
					if (warheadGuidancePresent != 0) {
						componentFlags |=
							FLIGHT_WORLDSTATE_HAS_WARHEAD_GUIDANCE;
						worldState += sizeof(
							WarheadGuidanceState);
					}
					memcpy(&charDataPresent,
					       &mobileObjectState->pCharData,
					       sizeof(charDataPresent));
					if (charDataPresent != 0) {
						componentFlags |=
							FLIGHT_WORLDSTATE_HAS_CHAR_DATA;
						worldState += sizeof(
							XvtSnapshotMobileObjectCharData);
					}
				}
			}

			if (componentFlags == 0) {
				++emptyRunLength;
				if (emptyRunLength >=
				    FLIGHT_WORLDSTATE_MAX_EMPTY_RUN) {
					*outMap++ =
						(uint8_t)(emptyRunLength |
							  FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG);
					emptyRunLength = 0;
				}
			} else {
				if (emptyRunLength != 0) {
					*outMap++ =
						(uint8_t)(emptyRunLength |
							  FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG);
					emptyRunLength = 0;
				}
				*outMap++ = componentFlags;
			}
		}
		++objectIndex;
	}

	if (emptyRunLength != 0) {
		*outMap++ = (uint8_t)(emptyRunLength |
				      FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG);
	}
	return (int)(outMap - mapStart);
}

/* Makes one optional block at *cursor match the host's presence bit. Present on both sides: steps
 * over it and returns 1, so the caller can go on to the blocks nested under it. Present here only:
 * removes it, closing the gap, and leaves *cursor where it began. Present at the host only: inserts
 * it zero-filled and steps past it. *end, the end of the image's bytes, moves with every change.
 * Returns 0 unless both sides have the block. */
static int XvtSnapshot_MatchBlock(uint8_t **cursor, uint8_t **end, size_t size,
				  int here, int host)
{
	uint8_t *blockStart;

	if (here && host) {
		*cursor += size;
		return 1;
	}
	if (here) {
		blockStart = *cursor;
		*cursor += size;
		memmove(blockStart, *cursor, (size_t)(*end - *cursor));
		*cursor = blockStart;
		*end -= size;
	} else if (host) {
		blockStart = *cursor;
		*cursor += size;
		memmove(*cursor, blockStart, (size_t)(*end - blockStart));
		memset(blockStart, 0, (size_t)(*cursor - blockStart));
		*end += size;
	}
	return 0;
}

void XvtSnapshot_ApplyPresenceMap(const uint8_t *presenceMap)
{
	uint8_t *cursor;
	uint8_t *end;
	int mapSlotLimit;
	int emptyRunRemaining;
	int objectIndex;

	cursor = g_worldStateDupBuffer;
	end = &g_worldStateDupBuffer[g_worldStateDupSize];
	memcpy(&mapSlotLimit, presenceMap, sizeof(mapSlotLimit));
	presenceMap += sizeof(mapSlotLimit);
	emptyRunRemaining = 0;
	objectIndex = 0;
	while (objectIndex <
	       g_regionStaticObjectSlotCount + g_regionMainObjectSlotEnd) {
		if (g_localTransientSlotStart > objectIndex ||
		    g_localDebrisSlotEnd <= objectIndex) {
			int8_t presence;
			int8_t objectType;

			if (emptyRunRemaining != 0) {
				presence = 0;
				--emptyRunRemaining;
			} else {
				uint16_t emptyRunLength;

				if (mapSlotLimit > objectIndex) {
					presence = (int8_t)*presenceMap++;
				} else {
					break;
				}
				if (presence < 0) {
					emptyRunLength =
						presence &
						FLIGHT_WORLDSTATE_EMPTY_RUN_LENGTH_MASK;
					presence = 0;
					emptyRunRemaining = emptyRunLength - 1;
				}
			}

			objectType = *cursor++;
			if (XvtSnapshot_MatchBlock(
				    &cursor, &end,
				    sizeof(XvtSnapshotObjectRecord),
				    objectType != 0,
				    (presence & FLIGHT_WORLDSTATE_HAS_OBJECT) !=
					    0)) {
				const XvtSnapshotObjectRecord *objectState;
				uint32_t mobilePresent;

				objectState = (const XvtSnapshotObjectRecord
						       *)(cursor -
							  sizeof(*objectState));
				memcpy(&mobilePresent, &objectState->mobj,
				       sizeof(mobilePresent));
				if (XvtSnapshot_MatchBlock(
					    &cursor, &end,
					    sizeof(XvtSnapshotMobileObject),
					    mobilePresent != 0,
					    (presence &
					     FLIGHT_WORLDSTATE_HAS_MOBILE) !=
						    0)) {
					const XvtSnapshotMobileObject
						*mobileState;
					uint32_t craftPresent;
					uint32_t warheadGuidancePresent;
					uint32_t charDataPresent;

					mobileState =
						(const XvtSnapshotMobileObject
							 *)(cursor -
							    sizeof(*mobileState));
					memcpy(&craftPresent,
					       &mobileState->pCraft,
					       sizeof(craftPresent));
					XvtSnapshot_MatchBlock(
						&cursor, &end,
						sizeof(XvtSnapshotCraftData),
						craftPresent != 0,
						(presence &
						 FLIGHT_WORLDSTATE_HAS_CRAFT) !=
							0);
					memcpy(&warheadGuidancePresent,
					       &mobileState->pWarheadGuidance,
					       sizeof(warheadGuidancePresent));
					XvtSnapshot_MatchBlock(
						&cursor, &end,
						sizeof(WarheadGuidanceState),
						warheadGuidancePresent != 0,
						(presence &
						 FLIGHT_WORLDSTATE_HAS_WARHEAD_GUIDANCE) !=
							0);
					memcpy(&charDataPresent,
					       &mobileState->pCharData,
					       sizeof(charDataPresent));
					XvtSnapshot_MatchBlock(
						&cursor, &end,
						sizeof(XvtSnapshotMobileObjectCharData),
						charDataPresent != 0,
						(presence &
						 FLIGHT_WORLDSTATE_HAS_CHAR_DATA) !=
							0);
				}
			}
		}
		++objectIndex;
	}

	g_worldStateDupSize = (int)(end - g_worldStateDupBuffer);
}

static int XvtSnapshot_IsPoolLink(uint32_t value, size_t stride, unsigned count)
{
	return !value ||
	       ((value - 1) % stride == 0 && (value - 1) / stride < count);
}

typedef struct XvtSnapshotWorldRanges {
	int32_t craft_capacity, character_count, projectile_count, debris_count,
		debris_slot_count;
	int32_t main_start, active_start, craft_end, character_start,
		character_end;
	int32_t projectile_start, projectile_end, debris_start, debris_end,
		explosion_start, explosion_end;
	int32_t local_start, local_end, main_end, static_count;
} XvtSnapshotWorldRanges;

/* Returns 1 when the 20 dwords at image_ranges (pool sizes, the world-state debris slot count and the
 * slot-range bounds) equal this flight's live values, else 0. */
static int XvtSnapshot_RangesMatchLive(const uint8_t *image_ranges)
{
	XvtSnapshotWorldRanges ranges;
	memcpy(&ranges, image_ranges, sizeof ranges);
	const XvtSnapshotWorldRanges expected = {
		.craft_capacity = g_craftDataPoolCapacity,
		.character_count = g_mobileObjectCharDataCount,
		.projectile_count = g_projectileObjectSlotsTotal,
		.debris_count = g_debrisObjectSlotsTotal,
		.debris_slot_count = g_worldStateDebrisSlotCount,
		.main_start = g_regionMainObjectSlotStart,
		.active_start = g_activeRegionObjectSlotStart,
		.craft_end = g_activeRegionCraftObjectSlotEnd,
		.character_start = g_mobileObjectCharDataSlotStart,
		.character_end = g_mobileObjectCharDataSlotEnd,
		.projectile_start = g_projectileObjectSlotStart,
		.projectile_end = g_projectileObjectSlotEnd,
		.debris_start = g_debrisObjectSlotStart,
		.debris_end = g_debrisObjectSlotEnd,
		.explosion_start = g_explosionObjectSlotStart,
		.explosion_end = g_explosionObjectSlotEnd,
		.local_start = g_localTransientSlotStart,
		.local_end = g_localDebrisSlotEnd,
		.main_end = g_regionMainObjectSlotEnd,
		.static_count = g_regionStaticObjectSlotCount};
	return memcmp(&ranges, &expected, sizeof ranges) == 0;
}

static int XvtSnapshot_ValidatePrefix(const uint8_t *image, size_t size,
				      const XvtFlightCheckpointView *timing)
{
	int network = timing != NULL;
	unsigned row = 0;
	XvtPlayerTimingWire player_timing[XVT_FLIGHT_PLAYERS];
	if (network) {
		memcpy(player_timing, timing->players, sizeof player_timing);
	}
	if (size < XVT_FLIGHT_PLAYERS * sizeof(XvtSnapshotPlayerData)) {
		return 0;
	}
	const uint8_t *players =
		image + size -
		XVT_FLIGHT_PLAYERS * sizeof(XvtSnapshotPlayerData);
	for (unsigned i = 0; i < XVT_FLIGHT_PLAYERS; ++i) {
		int slot = (int32_t)XvtWire_Get32(
			players + i * sizeof(XvtSnapshotPlayerData) +
			offsetof(XvtSnapshotPlayerData, objectIndex));
		if (slot < -1 || slot >= g_regionMainObjectSlotEnd) {
			return 0;
		}
		if (network) {
			const XvtPlayerTimingWire *state = &player_timing[i];
			if (state->valid &&
			    XvtWire_Get16(state->slot) != (unsigned)slot) {
				return 0;
			}
		}
	}
	const uint8_t *cursor = image;
	size_t left = size;
	for (int slot = 0;
	     slot < g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
	     ++slot) {
		if (slot >= g_localTransientSlotStart &&
		    slot < g_localDebrisSlotEnd) {
			continue;
		}
		if (!left) {
			return 0;
		}
		uint8_t type = *cursor++;
		--left;
		XvtReferenceMotionWire reference = {0};
		XvtIntegrationWire integration = {0};
		if (network) {
			memcpy(&reference,
			       timing->reference + row * sizeof reference,
			       sizeof reference);
			memcpy(&integration,
			       timing->integration + row * sizeof integration,
			       sizeof integration);
		}
		++row;
		if (!type) {
			if (network && (reference.type || integration.type)) {
				return 0;
			}
			continue;
		}
		if (left < sizeof(XvtSnapshotObjectRecord)) {
			return 0;
		}
		XvtSnapshotObjectRecord object;
		memcpy(&object, cursor, sizeof object);
		cursor += sizeof object;
		left -= sizeof object;
		if (network) {
			if ((reference.type &&
			     (reference.type != type ||
			      XvtWire_Get16(reference.signature) !=
				      object.objectSignature)) ||
			    (integration.type &&
			     (integration.type != type ||
			      XvtWire_Get16(integration.signature) !=
				      object.objectSignature))) {
				return 0;
			}
			for (unsigned i = 0; i < XVT_FLIGHT_PLAYERS; ++i) {
				const XvtPlayerTimingWire *state =
					&player_timing[i];
				if (state->valid &&
				    XvtWire_Get16(state->slot) ==
					    (unsigned)slot &&
				    XvtWire_Get16(state->signature) !=
					    object.objectSignature) {
					return 0;
				}
			}
		}
		if (object.objectType != type ||
		    !XvtSnapshot_IsPoolLink(object.mobj,
					    sizeof(XvtSnapshotMobileObject),
					    g_regionMainObjectSlotEnd)) {
			return 0;
		}
		if (!object.mobj) {
			continue;
		}
		if (left < sizeof(XvtSnapshotMobileObject)) {
			return 0;
		}
		XvtSnapshotMobileObject mobile;
		memcpy(&mobile, cursor, sizeof mobile);
		cursor += sizeof mobile;
		left -= sizeof mobile;
		if (network && integration.type &&
		    integration.family != mobile.family) {
			return 0;
		}
		if (!XvtSnapshot_IsPoolLink(mobile.pCraft,
					    sizeof(XvtSnapshotCraftData),
					    g_craftDataPoolCapacity) ||
		    !XvtSnapshot_IsPoolLink(mobile.pWarheadGuidance,
					    sizeof(WarheadGuidanceState),
					    g_projectileObjectSlotsTotal) ||
		    !XvtSnapshot_IsPoolLink(
			    mobile.pCharData,
			    sizeof(XvtSnapshotMobileObjectCharData),
			    g_mobileObjectCharDataCount)) {
			return 0;
		}
		size_t extra =
			(mobile.pCraft ? sizeof(XvtSnapshotCraftData) : 0) +
			(mobile.pWarheadGuidance ? sizeof(WarheadGuidanceState)
						 : 0) +
			(mobile.pCharData
				 ? sizeof(XvtSnapshotMobileObjectCharData)
				 : 0);
		if (left < extra) {
			return 0;
		}
		cursor += extra;
		left -= extra;
	}

	size_t clocks_and_header = sizeof(g_missionElapsedClock) +
				   sizeof(g_missionCountdownClock) +
				   sizeof(MissionHeader);
	if (left < clocks_and_header) {
		return 0;
	}
	MissionHeader header;
	memcpy(&header,
	       cursor + sizeof(g_missionElapsedClock) +
		       sizeof(g_missionCountdownClock),
	       sizeof header);
	if (header.numFlightGroups != g_missionHeader.numFlightGroups) {
		return 0;
	}
	size_t groups =
		(sizeof(MissionFgRuntimeStats) + sizeof(MissionFlightGroup)) *
		header.numFlightGroups;
	size_t before_ranges = clocks_and_header + groups +
			       sizeof(XvtSnapshotFlightMissionState) +
			       sizeof(g_flightGlobalCountdownTimers) +
			       sizeof(g_missionFileVersion) +
			       sizeof(g_flightPlayerCount) +
			       sizeof(g_worldStateReservedByte);
	size_t after_ranges =
		sizeof(g_planTable) + sizeof(g_planCount) +
		sizeof(g_unusedWorldStateSerializedDword) +
		sizeof(g_builtinPlanIdByNameIndex) +
		sizeof(g_gameRandFeedbackState) +
		sizeof(g_nextObjectSignature) +
		sizeof(g_laserFireTimestampTrackingEnabled) +
		XVT_FLIGHT_PLAYERS * sizeof(XvtSnapshotPlayerData);
	if (left !=
	    before_ranges + sizeof(XvtSnapshotWorldRanges) + after_ranges) {
		return 0;
	}
	return XvtSnapshot_RangesMatchLive(cursor + before_ranges);
}

static int XvtSnapshot_ReadImage(const uint8_t *image, size_t size,
				 XvtFlightCheckpointView *timing)
{
	if (!image || size > XvtSnapshot_CalculateSize()) {
		return 0;
	}
	memset(timing, 0, sizeof *timing);
	timing->prefix = size;
	if (XvtFlightTiming_IsNetwork125() &&
	    !XvtFlightCheckpoint_Read(image, size, timing)) {
		return 0;
	}
	return XvtSnapshot_ValidatePrefix(
		image, timing->prefix,
		XvtFlightTiming_IsNetwork125() ? timing : NULL);
}

int XvtSnapshot_Validate(const uint8_t *image, size_t size)
{
	XvtFlightCheckpointView timing;
	return XvtSnapshot_ReadImage(image, size, &timing);
}

int XvtSnapshot_Decode(const uint8_t *image, size_t size)
{
	XvtFlightCheckpointView timing;
	if (!XvtSnapshot_ReadImage(image, size, &timing)) {
		return 0;
	}
	XvtSnapshot_DecodePrefix(image);
	if (XvtFlightTiming_IsNetwork125()) {
		XvtFlightCheckpoint_Restore(&timing);
	}
	return 1;
}

void XvtSnapshot_Save(void)
{
	g_worldStateSize = (unsigned)XvtSnapshot_Encode(
		g_worldStateBuffer, XvtSnapshot_CalculateSize());
}

void XvtSnapshot_Restore(void)
{
	if (!XvtSnapshot_Decode(g_worldStateBuffer, g_worldStateSize)) {
		g_flightMissionState.missionEndPending = 1;
	}
}

int XvtSnapshot_ChecksumImage(const uint8_t *image, size_t size,
			      unsigned checksums[XVT_WORLD_CHECKSUM_REGIONS],
			      unsigned lengths[XVT_WORLD_CHECKSUM_REGIONS])
{
	XvtFlightCheckpointView timing;
	if (!XvtSnapshot_ReadImage(image, size, &timing)) {
		return 0;
	}
	XvtSnapshot_ChecksumPrefix(image, timing.prefix, checksums, lengths);
	if (XvtFlightTiming_IsNetwork125()) {
		checksums[XVT_TIMING_CHECKSUM_REGION] = XvtFlightWire_Crc32c(
			image + timing.prefix, size - timing.prefix);
		lengths[XVT_TIMING_CHECKSUM_REGION] =
			(unsigned)(size - timing.prefix);
	}
	return 1;
}

void XvtSnapshot_Checksum(int unusedArg0, int unusedArg1)
{
	(void)unusedArg0;
	(void)unusedArg1;
	if (!XvtSnapshot_ChecksumImage(g_worldStateBuffer, g_worldStateSize,
				       g_worldChecksum,
				       g_worldChecksumRegionLengths)) {
		g_flightMissionState.missionEndPending = 1;
	}
}
