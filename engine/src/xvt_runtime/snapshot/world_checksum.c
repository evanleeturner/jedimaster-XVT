/* World-state checksums: the regional byte sums of a world image and the
 * live checksum of the running world, both of which peers compare to find a
 * world that has drifted. world_state.c writes, reads and checks the image
 * these sums are taken over. */
#include "xvt_runtime/snapshot/world_checksum.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/net/flight_sync.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/snapshot/records.h"
#include "xvt_runtime/snapshot/world_state.h"
#include "xvt_runtime/timing/flight_timing.h"
#include <string.h>

static unsigned int XvtSnapshot_SumBytes(uint8_t **cursor, int count)
{
	unsigned int sum = 0;
	for (int i = 0; i < count; ++i) {
		sum += *(*cursor)++;
	}
	return sum;
}

/* Sums the bytes of one present slot's records at *cursor: the object, then its mobile, craft,
 * warhead-guidance and character records when present, leaving out the links and cached motion that
 * XvtSnapshot_ChecksumImage's comment lists. Moves *cursor past all of them; returns the sum. */
static unsigned int XvtSnapshot_SumSlotRecords(uint8_t **cursor)
{
	unsigned int sum = 0;
	int bytesRemaining;

	XvtSnapshotObjectRecord *objectState;
	int objectDataBytes;
	uint32_t mobilePresent;

	objectState = (XvtSnapshotObjectRecord *)*cursor;
	objectDataBytes = sizeof(*objectState) - sizeof(objectState->mobj);
	do {
		sum += *(*cursor)++;
	} while (--objectDataBytes != 0);
	*cursor = (uint8_t *)(objectState + 1);
	memcpy(&mobilePresent, &objectState->mobj, sizeof(mobilePresent));
	if (mobilePresent != 0) {
		XvtSnapshotMobileObject *mobileState;
		int mobileDataBytes;
		uint32_t craftPresent;
		uint32_t guidancePresent;
		uint32_t charDataPresent;

		mobileState = (XvtSnapshotMobileObject *)*cursor;
		mobileDataBytes = sizeof(*mobileState) -
				  sizeof(mobileState->moveVectorDirty) -
				  sizeof(mobileState->moveX) -
				  sizeof(mobileState->moveY) -
				  sizeof(mobileState->moveZ) -
				  sizeof(mobileState->orientMatrixDirty) -
				  sizeof(mobileState->cachedFwdX) -
				  sizeof(mobileState->cachedFwdY) -
				  sizeof(mobileState->cachedFwdZ) -
				  sizeof(mobileState->cachedSideX) -
				  sizeof(mobileState->cachedSideY) -
				  sizeof(mobileState->cachedSideZ) -
				  sizeof(mobileState->cachedUpX) -
				  sizeof(mobileState->cachedUpY) -
				  sizeof(mobileState->cachedUpZ) -
				  sizeof(mobileState->pWarheadGuidance) -
				  sizeof(mobileState->pCraft) -
				  sizeof(mobileState->pCharData);
		do {
			sum += *(*cursor)++;
		} while (--mobileDataBytes != 0);
		*cursor = (uint8_t *)(mobileState + 1);
		memcpy(&craftPresent, &mobileState->pCraft,
		       sizeof(craftPresent));
		if (craftPresent != 0) {
			XvtSnapshotCraftData *craftState;
			int craftDataBytes;

			craftState = (XvtSnapshotCraftData *)*cursor;
			craftDataBytes =
				sizeof(*craftState) -
				sizeof(craftState->field_3F2) -
				sizeof(craftState->turretObjectLinks) -
				sizeof(craftState->effectiveAiObjectLink) + 32;
			do {
				sum += *(*cursor)++;
			} while (--craftDataBytes != 0);
			*cursor = (uint8_t *)(craftState + 1);
		}
		memcpy(&guidancePresent, &mobileState->pWarheadGuidance,
		       sizeof(guidancePresent));
		if (guidancePresent != 0) {
			bytesRemaining = sizeof(WarheadGuidanceState);
			do {
				sum += *(*cursor)++;
			} while (--bytesRemaining != 0);
		}
		memcpy(&charDataPresent, &mobileState->pCharData,
		       sizeof(charDataPresent));
		if (charDataPresent != 0) {
			bytesRemaining =
				sizeof(XvtSnapshotMobileObjectCharData);
			do {
				sum += *(*cursor)++;
			} while (--bytesRemaining != 0);
		}
	}
	return sum;
}

/* Closes the current checksum region once it has grown past the target size
 * while regions remain: records its length and sum, then starts the next
 * region at the cursor with a zero sum. */
static void XvtSnapshot_CloseChecksumRegion(
	unsigned checksums[16], unsigned lengths[16], int lastRegion,
	int regionTargetSize, const uint8_t *cursor, uint8_t **regionStart,
	int *checksumRegionIndex, unsigned int *checksum)
{
	if (*checksumRegionIndex < lastRegion &&
	    cursor - *regionStart > regionTargetSize) {
		lengths[*checksumRegionIndex] =
			(unsigned int)(cursor - *regionStart);
		checksums[(*checksumRegionIndex)++] = *checksum;
		*checksum = 0;
		*regionStart = (uint8_t *)cursor;
	}
}

void XvtSnapshot_ChecksumPrefix(const uint8_t *image, size_t prefix,
				unsigned checksums[16], unsigned lengths[16])
{
	uint8_t *cursor;
	uint8_t *regionStart;
	unsigned int checksum;
	int regionTargetSize;
	int checksumRegionIndex;
	int objectIndex;
	int objectCount;
	int bytesRemaining;
	int flightGroupCount;

	int network = XvtFlightTiming_IsNetwork125();
	int lastRegion = network ? 14 : 15;
	memset(checksums, 0, 16 * sizeof *checksums);
	memset(lengths, 0, 16 * sizeof *lengths);

	regionTargetSize = (int)prefix / (network ? 15 : 16);
	cursor = (uint8_t *)image;
	regionStart = (uint8_t *)image;
	checksum = 0;
	checksumRegionIndex = 0;
	objectIndex = 0;
	objectCount = g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
	if (objectCount > 0) {
		do {
			if (g_localTransientSlotStart > objectIndex ||
			    g_localDebrisSlotEnd <= objectIndex) {
				uint8_t objectPresent;

				objectPresent = *cursor++;
				if (objectPresent != 0) {
					checksum += XvtSnapshot_SumSlotRecords(
						&cursor);
				}
				XvtSnapshot_CloseChecksumRegion(
					checksums, lengths, lastRegion,
					regionTargetSize, cursor, &regionStart,
					&checksumRegionIndex, &checksum);
			}
			++objectIndex;
		} while (objectCount > objectIndex);
	}

	checksum += XvtSnapshot_SumBytes(&cursor, 8);
	checksum += XvtSnapshot_SumBytes(&cursor, 8);
	checksum += XvtSnapshot_SumBytes(&cursor, sizeof(MissionHeader));
	flightGroupCount = (int16_t)g_missionHeader.numFlightGroups;
	bytesRemaining = 294 * flightGroupCount;
	if (bytesRemaining > 0) {
		do {
			checksum += *cursor++;
		} while (--bytesRemaining != 0);
	}
	XvtSnapshot_CloseChecksumRegion(checksums, lengths, lastRegion,
					regionTargetSize, cursor, &regionStart,
					&checksumRegionIndex, &checksum);

	bytesRemaining = 1382 * flightGroupCount;
	if (bytesRemaining > 0) {
		do {
			checksum += *cursor++;
		} while (--bytesRemaining != 0);
	}
	XvtSnapshot_CloseChecksumRegion(checksums, lengths, lastRegion,
					regionTargetSize, cursor, &regionStart,
					&checksumRegionIndex, &checksum);

	checksum += XvtSnapshot_SumBytes(&cursor, 3376);
	XvtSnapshot_CloseChecksumRegion(checksums, lengths, lastRegion,
					regionTargetSize, cursor, &regionStart,
					&checksumRegionIndex, &checksum);

	checksum += XvtSnapshot_SumBytes(&cursor, 22);
	checksum += XvtSnapshot_SumBytes(&cursor, 2);
	XvtSnapshot_CloseChecksumRegion(checksums, lengths, lastRegion,
					regionTargetSize, cursor, &regionStart,
					&checksumRegionIndex, &checksum);

	checksum += XvtSnapshot_SumBytes(&cursor, 4);
	checksum += *cursor++;
	/* The 20 range dwords: 4 pool sizes, the world-state debris slot count and 15 slot-range bounds. */
	checksum += XvtSnapshot_SumBytes(&cursor, 20 * 4);
	checksum += XvtSnapshot_SumBytes(&cursor, 21760);
	checksum += XvtSnapshot_SumBytes(&cursor, 4);
	checksum += XvtSnapshot_SumBytes(&cursor, 4);
	checksum += XvtSnapshot_SumBytes(&cursor, 256);
	XvtSnapshot_CloseChecksumRegion(checksums, lengths, lastRegion,
					regionTargetSize, cursor, &regionStart,
					&checksumRegionIndex, &checksum);

	checksum += XvtSnapshot_SumBytes(&cursor, 2);
	checksum += XvtSnapshot_SumBytes(&cursor, 2);
	checksum += XvtSnapshot_SumBytes(&cursor, 4);
	checksum += XvtSnapshot_SumBytes(&cursor, 11752);
	if (network || cursor - regionStart > regionTargetSize) {
		if (network) {
			checksumRegionIndex = 14;
		}
		lengths[checksumRegionIndex] = (unsigned)(cursor - regionStart);
		checksums[checksumRegionIndex] = checksum;
	}
}

static unsigned int
XvtSnapshot_ChecksumMobileObjectCharData(const MobileObjectCharData *live)
{
	XvtSnapshotMobileObjectCharData record;
	XvtSnapshot_EncodeMobileObjectCharData(&record, live);
	return Flight_ChecksumBufferRotateXor(&record, 0x4C);
}

static unsigned int XvtSnapshot_ChecksumMobileObject(const MobileObject *live)
{
	XvtSnapshotMobileObject record;
	XvtSnapshot_EncodeMobileObject(&record, live);
	return Flight_ChecksumBufferRotateXor(&record, 0x8B);
}

static unsigned int XvtSnapshot_ChecksumObjectRecord(const ObjectRecord *live)
{
	XvtSnapshotObjectRecord record;
	XvtSnapshot_EncodeObjectRecord(&record, live);
	return Flight_ChecksumBufferRotateXor(&record, 0x1F);
}

static unsigned int XvtSnapshot_ChecksumCraftData(const CraftData *live)
{
	XvtSnapshotCraftData record;
	XvtSnapshot_EncodeCraftData(&record, live);
	return Flight_ChecksumBufferRotateXor(&record, 0x412);
}

static unsigned int XvtSnapshot_ChecksumPlayerData(const PlayerData *live)
{
	XvtSnapshotPlayerData record;
	XvtSnapshot_EncodePlayerData(&record, live);
	return Flight_ChecksumBufferRotateXor(&record, 0x5BD);
}

/* Folds one value into the live checksum: exclusive-or, then rotate left by one bit. */
static uint32_t XvtSnapshot_MixChecksum(uint32_t checksum, uint32_t value)
{
	return Flight_RotateChecksumLeft(checksum ^ value);
}

/* Folds every occupied pool record into checksum, pool by pool: character data, mobile records,
 * main-slot objects, static objects, craft, then warhead guidance. Returns the new checksum. */
static uint32_t XvtSnapshot_MixPools(uint32_t checksum)
{
	int firstSlot;
	int charDataIndex;
	int mobileObjectIndex;
	int objectIndex;
	int staticObjectIndex;
	int craftIndex;
	int projectileIndex;

	firstSlot = g_objectSlotRangeByGenus[16].start;
	for (charDataIndex = 0;
	     charDataIndex < (int)g_mobileObjectCharDataCount;
	     charDataIndex++) {
		if (g_objectTable[firstSlot + charDataIndex].objectType != 0) {
			checksum = XvtSnapshot_MixChecksum(
				checksum,
				XvtSnapshot_ChecksumMobileObjectCharData(
					&g_mobileObjectCharDataPool
						[charDataIndex]));
		}
	}

	for (mobileObjectIndex = 0;
	     mobileObjectIndex <
	     g_regionMainObjectSlotEnd - g_regionMainObjectSlotStart;
	     mobileObjectIndex++) {
		if (g_objectTable[mobileObjectIndex].objectType != 0) {
			checksum = XvtSnapshot_MixChecksum(
				checksum, XvtSnapshot_ChecksumMobileObject(
						  &g_mobileObjectPoolBase
							  [mobileObjectIndex]));
		}
	}
	for (objectIndex = 0; objectIndex < g_regionMainObjectSlotEnd -
						    g_regionMainObjectSlotStart;
	     objectIndex++) {
		if (g_objectTable[objectIndex].objectType != 0) {
			checksum = XvtSnapshot_MixChecksum(
				checksum, XvtSnapshot_ChecksumObjectRecord(
						  &g_objectTable[objectIndex]));
		}
	}
	for (staticObjectIndex = g_regionMainObjectSlotEnd;
	     staticObjectIndex <
	     g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
	     staticObjectIndex++) {
		if (g_objectTable[staticObjectIndex].objectType != 0) {
			checksum = XvtSnapshot_MixChecksum(
				checksum,
				XvtSnapshot_ChecksumObjectRecord(
					&g_objectTable[staticObjectIndex]));
		}
	}

	firstSlot = g_objectSlotRangeByGenus[0].start;
	for (craftIndex = 0; craftIndex < g_craftDataPoolCapacity;
	     craftIndex++) {
		if (g_objectTable[firstSlot + craftIndex].objectType != 0) {
			checksum = XvtSnapshot_MixChecksum(
				checksum,
				XvtSnapshot_ChecksumCraftData(
					&g_craftDataPoolBase[craftIndex]));
		}
	}
	firstSlot = g_objectSlotRangeByGenus[6].start;
	for (projectileIndex = 0;
	     projectileIndex < (int)g_projectileObjectSlotsTotal;
	     projectileIndex++) {
		if (g_objectTable[firstSlot + projectileIndex].objectType !=
		    0) {
			checksum = XvtSnapshot_MixChecksum(
				checksum, Flight_ChecksumBufferRotateXor(
						  &g_projectileGuidanceStates
							  [projectileIndex],
						  0xA));
		}
	}
	return checksum;
}

int XvtSnapshot_LiveChecksum(void)
{
	XvtSnapshotFlightMissionState missionState;
	uint32_t checksum;
	int flightGroupIndex;
	int goalIndex;
	int playerIndex;

	checksum = 0;
	checksum = XvtSnapshot_MixPools(checksum);

	checksum = XvtSnapshot_MixChecksum(
		checksum,
		Flight_ChecksumBufferRotateXor(&g_missionElapsedClock,
					       sizeof(g_missionElapsedClock)));
	checksum = XvtSnapshot_MixChecksum(
		checksum, Flight_ChecksumBufferRotateXor(
				  &g_missionCountdownClock,
				  sizeof(g_missionCountdownClock)));
	for (flightGroupIndex = 0;
	     flightGroupIndex < (int16_t)g_missionHeader.numFlightGroups;
	     flightGroupIndex++) {
		checksum = XvtSnapshot_MixChecksum(
			checksum,
			Flight_ChecksumBufferRotateXor(
				&g_missionFgStats[flightGroupIndex], 0x126));
	}

	XvtSnapshot_EncodeFlightMissionState(&missionState,
					     &g_flightMissionState);
	checksum = XvtSnapshot_MixChecksum(
		checksum, Flight_ChecksumBufferRotateXor(&missionState,
							 sizeof(missionState)));
	checksum = XvtSnapshot_MixChecksum(checksum, g_nextObjectSignature);
	checksum = XvtSnapshot_MixChecksum(
		checksum, Flight_ChecksumBufferRotateXor(
				  &g_flightGlobalCountdownTimers, 0x16));
	checksum = XvtSnapshot_MixChecksum(
		checksum, (uint32_t)(int16_t)g_missionFileVersion);
	checksum = XvtSnapshot_MixChecksum(
		checksum,
		Flight_ChecksumBufferRotateXor(&g_missionHeader, 0xA2));
	for (flightGroupIndex = 0;
	     flightGroupIndex < (int16_t)g_missionHeader.numFlightGroups;
	     flightGroupIndex++) {
		checksum = XvtSnapshot_MixChecksum(
			checksum,
			Flight_ChecksumBufferRotateXor(
				&g_missionFlightGroups[flightGroupIndex],
				0x562));
	}
	checksum = XvtSnapshot_MixChecksum(
		checksum,
		Flight_ChecksumBufferRotateXor(g_missionMessages,
					       sizeof(g_missionMessages)));
	for (goalIndex = 0; goalIndex < 10; goalIndex++) {
		checksum = XvtSnapshot_MixChecksum(
			checksum, Flight_ChecksumBufferRotateXor(
					  g_missionGlobalGoals[goalIndex],
					  sizeof(g_missionGlobalGoals[0])));
	}

	checksum = XvtSnapshot_MixChecksum(checksum, g_activeFlightPlayerCount);
	checksum = XvtSnapshot_MixChecksum(checksum, g_worldStateReservedByte);
	checksum = XvtSnapshot_MixChecksum(checksum, g_craftDataPoolCapacity);
	checksum =
		XvtSnapshot_MixChecksum(checksum, g_mobileObjectCharDataCount);
	checksum =
		XvtSnapshot_MixChecksum(checksum, g_projectileObjectSlotsTotal);
	checksum = XvtSnapshot_MixChecksum(checksum, g_debrisObjectSlotsTotal);
	checksum =
		XvtSnapshot_MixChecksum(checksum, g_worldStateDebrisSlotCount);
	checksum =
		XvtSnapshot_MixChecksum(checksum, g_regionMainObjectSlotStart);
	checksum = XvtSnapshot_MixChecksum(checksum,
					   g_activeRegionObjectSlotStart);
	checksum = XvtSnapshot_MixChecksum(checksum,
					   g_activeRegionCraftObjectSlotEnd);
	checksum = XvtSnapshot_MixChecksum(checksum,
					   g_mobileObjectCharDataSlotStart);
	checksum = XvtSnapshot_MixChecksum(checksum,
					   g_mobileObjectCharDataSlotEnd);
	checksum =
		XvtSnapshot_MixChecksum(checksum, g_projectileObjectSlotStart);
	checksum = XvtSnapshot_MixChecksum(checksum, g_projectileObjectSlotEnd);
	checksum = XvtSnapshot_MixChecksum(checksum, g_debrisObjectSlotStart);
	checksum = XvtSnapshot_MixChecksum(checksum, g_debrisObjectSlotEnd);
	checksum =
		XvtSnapshot_MixChecksum(checksum, g_explosionObjectSlotStart);
	checksum = XvtSnapshot_MixChecksum(checksum, g_explosionObjectSlotEnd);
	checksum = XvtSnapshot_MixChecksum(checksum, g_localTransientSlotStart);
	checksum = XvtSnapshot_MixChecksum(checksum, g_localDebrisSlotEnd);
	checksum = XvtSnapshot_MixChecksum(checksum, g_regionMainObjectSlotEnd);
	checksum = XvtSnapshot_MixChecksum(checksum,
					   g_regionStaticObjectSlotCount);
	checksum = XvtSnapshot_MixChecksum(
		checksum, Flight_ChecksumBufferRotateXor(g_planTable, 0x5500));
	checksum = XvtSnapshot_MixChecksum(checksum, g_planCount);
	checksum = XvtSnapshot_MixChecksum(
		checksum,
		Flight_ChecksumBufferRotateXor(g_planOrderData, 0x1FFFF));
	checksum = XvtSnapshot_MixChecksum(checksum,
					   g_unusedWorldStateSerializedDword);
	checksum = XvtSnapshot_MixChecksum(checksum,
					   (uint16_t)g_gameRandFeedbackState);
	checksum = XvtSnapshot_MixChecksum(checksum, g_flightConfNewNet);

	for (playerIndex = 0; playerIndex < 8; playerIndex++) {
		if (g_players[playerIndex].participationState != 0) {
			checksum = XvtSnapshot_MixChecksum(
				checksum, XvtSnapshot_ChecksumPlayerData(
						  &g_players[playerIndex]));
		}
	}
	return (int)checksum;
}
