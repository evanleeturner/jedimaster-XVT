/* Checks the world-state image (xvt_runtime/snapshot/world_state.h) against the promises in its header,
 * on worlds this file builds itself: no game data is read. Each case starts from an empty world (no
 * object slots, no flight groups, the offline timing profile) and sets only what it needs. */
#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/snapshot/records.h"
#include "xvt_runtime/snapshot/world_state.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint8_t* g_image;
static size_t g_capacity;

/* The rich world's tables: 2 main slots and 1 static slot, 1 entry in each of the other pools. */
static ObjectRecord g_testObjects[3];
static MobileObject g_testMobiles[2];
static CraftData g_testCraft[1];
static MobileObjectCharData g_testCharData[1];
static WarheadGuidanceState g_testGuidance[1];

/* Gives the world-state buffer room for the world as it is now. */
static void SizeImage(void) {
	free(g_image);
	g_capacity = XvtSnapshot_CalculateSize();
	g_image = calloc(1, g_capacity + 64);
	XVT_ASSERT_TRUE(g_image != NULL);
}

/* Clears the counts and tables the image depends on, then sizes a fresh image buffer for that world. */
static void EmptyWorld(void) {
	g_regionMainObjectSlotEnd = 0;
	g_regionStaticObjectSlotCount = 0;
	g_localTransientSlotStart = 0;
	g_localDebrisSlotEnd = 0;
	g_objectTable = NULL;
	memset(&g_missionHeader, 0, sizeof g_missionHeader);
	g_craftDataPoolCapacity = 0;
	g_mobileObjectCharDataCount = 0;
	g_projectileObjectSlotsTotal = 0;
	memset(&g_flightMissionState, 0, sizeof g_flightMissionState);
	/* A player's slot must be a main slot or -1; with no slots, every player has none. */
	memset(g_players, 0, sizeof g_players);
	for (int i = 0; i < 8; ++i)
		g_players[i].objectIndex = -1;
	SizeImage();
}

/* Slot 0 is a flying object with a craft, a character record and warhead guidance; slot 1 is empty but
 * owns the second mobile record; slot 2 is a static object. Player 0 sits in slot 0. */
static void RichWorld(void) {
	EmptyWorld();
	memset(g_testObjects, 0, sizeof g_testObjects);
	memset(g_testMobiles, 0, sizeof g_testMobiles);
	memset(g_testCraft, 0, sizeof g_testCraft);
	memset(g_testCharData, 0, sizeof g_testCharData);
	memset(g_testGuidance, 0, sizeof g_testGuidance);
	g_objectTable = g_testObjects;
	g_mobileObjectPoolBase = g_testMobiles;
	g_craftDataPoolBase = g_testCraft;
	g_mobileObjectCharDataPool = g_testCharData;
	g_projectileGuidanceStates = g_testGuidance;
	g_regionMainObjectSlotEnd = 2;
	g_regionStaticObjectSlotCount = 1;
	g_craftDataPoolCapacity = 1;
	g_mobileObjectCharDataCount = 1;
	g_projectileObjectSlotsTotal = 1;

	g_testObjects[0].objectType = 1;
	g_testObjects[0].objectSignature = 0x0101;
	g_testObjects[0].mobj = &g_testMobiles[0];
	g_testMobiles[0].pCraft = &g_testCraft[0];
	g_testMobiles[0].pCharData = &g_testCharData[0];
	g_testMobiles[0].pWarheadGuidance = &g_testGuidance[0];
	g_testMobiles[0].cachedFwdX = 5;
	g_testObjects[1].mobj = &g_testMobiles[1];
	g_testObjects[2].objectType = 2;
	g_testObjects[2].objectSignature = 0x0202;
	g_players[0].objectIndex = 0;
	SizeImage();
}

/* Offset in the rich world's image of slot 0's object record and of its mobile record. */
static const size_t kSlot0Object = 1;
static const size_t kSlot0Mobile = 1 + sizeof(XvtSnapshotObjectRecord);

static void CheckCalculateSize(void) {
	EmptyWorld();
	XVT_ASSERT_TRUE(g_capacity > 0);

	g_regionMainObjectSlotEnd = -1;
	XVT_ASSERT_INT_EQ(XvtSnapshot_CalculateSize(), 0);
	g_regionMainObjectSlotEnd = 0;
	g_regionStaticObjectSlotCount = 65536;
	XVT_ASSERT_INT_EQ(XvtSnapshot_CalculateSize(), 0);
	g_regionStaticObjectSlotCount = 0;
	g_missionHeader.numFlightGroups = -1;
	XVT_ASSERT_INT_EQ(XvtSnapshot_CalculateSize(), 0);
	g_missionHeader.numFlightGroups = 0;
	g_craftDataPoolCapacity = 65536;
	XVT_ASSERT_INT_EQ(XvtSnapshot_CalculateSize(), 0);
	g_craftDataPoolCapacity = 0;
	XVT_ASSERT_INT_EQ(XvtSnapshot_CalculateSize(), g_capacity);
}

static void CheckEncodeRefusals(void) {
	EmptyWorld();
	XVT_ASSERT_INT_EQ(XvtSnapshot_Encode(NULL, g_capacity), 0);
	XVT_ASSERT_INT_EQ(XvtSnapshot_Encode(g_image, g_capacity - 1), 0);
	g_regionMainObjectSlotEnd = -1;
	XVT_ASSERT_INT_EQ(XvtSnapshot_Encode(g_image, g_capacity), 0);
}

static void CheckValidate(void) {
	EmptyWorld();
	size_t written = XvtSnapshot_Encode(g_image, g_capacity);
	XVT_ASSERT_TRUE(written > 0 && written <= g_capacity);
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(g_image, written), 1);

	/* The length must be exact, and a NULL image is refused. */
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(NULL, written), 0);
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(g_image, written - 1), 0);
	if (written < g_capacity)
		XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(g_image, written + 1), 0);

	/* An image is only accepted by a world with the pool sizes it was written with. */
	g_mobileObjectCharDataCount = 1;
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(g_image, written), 0);
}

static void CheckDecodeRoundTrip(void) {
	EmptyWorld();
	g_nextObjectSignature = 0x1234;
	g_gameRandStateB = 77;
	size_t written = XvtSnapshot_Encode(g_image, g_capacity);
	XVT_ASSERT_TRUE(written > 0);

	g_nextObjectSignature = 0;
	g_gameRandStateB = 0;
	XVT_ASSERT_INT_EQ(XvtSnapshot_Decode(g_image, written), 1);
	XVT_ASSERT_INT_EQ(g_nextObjectSignature, 0x1234);
	XVT_ASSERT_INT_EQ(g_gameRandStateB, 77);

	/* Encoding the restored world gives the same bytes back. */
	uint8_t* again = calloc(1, g_capacity);
	XVT_ASSERT_TRUE(again != NULL);
	XVT_ASSERT_INT_EQ(XvtSnapshot_Encode(again, g_capacity), written);
	XVT_ASSERT_INT_EQ(memcmp(again, g_image, written), 0);
	free(again);
}

static void CheckDecodeRefusalLeavesWorld(void) {
	EmptyWorld();
	g_nextObjectSignature = 0x1234;
	size_t written = XvtSnapshot_Encode(g_image, g_capacity);
	g_nextObjectSignature = 0x4321;
	XVT_ASSERT_INT_EQ(XvtSnapshot_Decode(g_image, written - 1), 0);
	XVT_ASSERT_INT_EQ(g_nextObjectSignature, 0x4321);
}

static void CheckChecksumImage(void) {
	EmptyWorld();
	size_t written = XvtSnapshot_Encode(g_image, g_capacity);
	unsigned sums[16];
	unsigned lengths[16];

	/* A refused image leaves both arrays as they were. */
	memset(sums, 0xAB, sizeof sums);
	memset(lengths, 0xAB, sizeof lengths);
	XVT_ASSERT_INT_EQ(XvtSnapshot_ChecksumImage(g_image, written - 1, sums, lengths), 0);
	for (int i = 0; i < 16; ++i) {
		XVT_ASSERT_INT_EQ(sums[i], 0xABABABABu);
		XVT_ASSERT_INT_EQ(lengths[i], 0xABABABABu);
	}

	XVT_ASSERT_INT_EQ(XvtSnapshot_ChecksumImage(g_image, written, sums, lengths), 1);
	size_t covered = 0;
	for (int i = 0; i < 16; ++i)
		covered += lengths[i];
	XVT_ASSERT_TRUE(covered > 0 && covered <= written);

	/* The sums are byte sums: one more in the image's first byte (the mission clock, which the
	 * validator does not judge) is one more in the first region, and no other region moves. */
	unsigned before[16];
	memcpy(before, sums, sizeof before);
	int delta = g_image[0] == 0xFF ? -1 : 1;
	g_image[0] = (uint8_t)(g_image[0] + delta);
	XVT_ASSERT_INT_EQ(XvtSnapshot_ChecksumImage(g_image, written, sums, lengths), 1);
	XVT_ASSERT_INT_EQ(sums[0], before[0] + (unsigned)delta);
	for (int i = 1; i < 16; ++i)
		XVT_ASSERT_INT_EQ(sums[i], before[i]);
}

static void CheckRichRoundTrip(void) {
	RichWorld();
	size_t written = XvtSnapshot_Encode(g_image, g_capacity);
	XVT_ASSERT_TRUE(written > 0 && written <= g_capacity);
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(g_image, written), 1);

	g_testObjects[0].objectSignature = 0;
	g_testObjects[2].objectSignature = 0;
	g_testMobiles[0].pCraft = NULL;
	XVT_ASSERT_INT_EQ(XvtSnapshot_Decode(g_image, written), 1);
	XVT_ASSERT_INT_EQ(g_testObjects[0].objectSignature, 0x0101);
	XVT_ASSERT_INT_EQ(g_testObjects[2].objectSignature, 0x0202);
	XVT_ASSERT_TRUE(g_testObjects[0].mobj == &g_testMobiles[0]);
	XVT_ASSERT_TRUE(g_testMobiles[0].pCraft == &g_testCraft[0]);
	XVT_ASSERT_TRUE(g_testMobiles[0].pCharData == &g_testCharData[0]);
	XVT_ASSERT_TRUE(g_testMobiles[0].pWarheadGuidance == &g_testGuidance[0]);
	XVT_ASSERT_TRUE(g_testObjects[2].mobj == NULL);

	uint8_t* again = calloc(1, g_capacity);
	XVT_ASSERT_TRUE(again != NULL);
	XVT_ASSERT_INT_EQ(XvtSnapshot_Encode(again, g_capacity), written);
	XVT_ASSERT_INT_EQ(memcmp(again, g_image, written), 0);
	free(again);
}

static void CheckEmptySlotKeepsPoolLink(void) {
	RichWorld();
	size_t written = XvtSnapshot_Encode(g_image, g_capacity);
	g_testObjects[1].objectSignature = 0x7777;
	XVT_ASSERT_INT_EQ(XvtSnapshot_Decode(g_image, written), 1);
	XVT_ASSERT_INT_EQ(g_testObjects[1].objectType, 0);
	XVT_ASSERT_INT_EQ(g_testObjects[1].objectSignature, 0);
	XVT_ASSERT_TRUE(g_testObjects[1].mobj == &g_testMobiles[1]);
}

static void CheckValidateRefusesBadRecords(void) {
	RichWorld();
	size_t written = XvtSnapshot_Encode(g_image, g_capacity);
	uint8_t* bad = malloc(written);
	XVT_ASSERT_TRUE(bad != NULL);
	size_t mobj = kSlot0Object + offsetof(XvtSnapshotObjectRecord, mobj);
	uint32_t link;

	/* The type byte must equal the record's type. */
	memcpy(bad, g_image, written);
	bad[0] = 3;
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(bad, written), 0);

	/* A pool link must land on a whole record... */
	memcpy(bad, g_image, written);
	link = 2;
	memcpy(bad + mobj, &link, sizeof link);
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(bad, written), 0);

	/* ...inside the pool: the mobile pool has one record per main slot. */
	memcpy(bad, g_image, written);
	link = 2 * sizeof(XvtSnapshotMobileObject) + 1;
	memcpy(bad + mobj, &link, sizeof link);
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(bad, written), 0);

	/* The second mobile record is inside the pool; a link to it is accepted. */
	memcpy(bad, g_image, written);
	link = sizeof(XvtSnapshotMobileObject) + 1;
	memcpy(bad + mobj, &link, sizeof link);
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(bad, written), 1);

	/* A player's slot must be a main slot. */
	memcpy(bad, g_image, written);
	int32_t slot = 2;
	memcpy(bad + written - 8 * sizeof(XvtSnapshotPlayerData) + offsetof(XvtSnapshotPlayerData, objectIndex),
		   &slot, sizeof slot);
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(bad, written), 0);
	free(bad);
}

static void CheckChecksumSkipsLinks(void) {
	RichWorld();
	size_t written = XvtSnapshot_Encode(g_image, g_capacity);
	unsigned sums[16];
	unsigned lengths[16];
	unsigned before[16];
	XVT_ASSERT_INT_EQ(XvtSnapshot_ChecksumImage(g_image, written, before, lengths), 1);

	/* The mobile record's cached motion is not summed: changing it leaves every sum as it was. */
	int16_t fwd = 1234;
	memcpy(g_image + kSlot0Mobile + offsetof(XvtSnapshotMobileObject, cachedFwdX), &fwd, sizeof fwd);
	XVT_ASSERT_INT_EQ(XvtSnapshot_ChecksumImage(g_image, written, sums, lengths), 1);
	XVT_ASSERT_INT_EQ(memcmp(sums, before, sizeof sums), 0);

	/* The object's signature is summed: one more in its low byte is one more in the first region. */
	size_t signature = kSlot0Object + offsetof(XvtSnapshotObjectRecord, objectSignature);
	XVT_ASSERT_INT_EQ(g_image[signature], 0x01);
	g_image[signature] = 0x02;
	XVT_ASSERT_INT_EQ(XvtSnapshot_ChecksumImage(g_image, written, sums, lengths), 1);
	XVT_ASSERT_INT_EQ(sums[0], before[0] + 1);
}

static void CheckPresenceMap(void) {
	RichWorld();
	XVT_ASSERT_TRUE(XvtSnapshot_Encode(g_image, g_capacity) > 0);
	uint8_t map[16];
	memset(map, 0xEE, sizeof map);
	/* A slot count, then slot 0's five components, a run of one empty slot, and slot 2's object alone. */
	XVT_ASSERT_INT_EQ(XvtSnapshot_BuildPresenceMap(map, g_image), (int)sizeof(int) + 3);
	int count;
	memcpy(&count, map, sizeof count);
	XVT_ASSERT_INT_EQ(count, 3);
	XVT_ASSERT_INT_EQ(map[sizeof(int)], 0x1F);
	XVT_ASSERT_INT_EQ(map[sizeof(int) + 1], 0x81);
	XVT_ASSERT_INT_EQ(map[sizeof(int) + 2], 0x01);
	XVT_ASSERT_INT_EQ(map[sizeof(int) + 3], 0xEE);
}

static void CheckLiveChecksum(void) {
	EmptyWorld();
	g_nextObjectSignature = 1;
	int base = XvtSnapshot_LiveChecksum();

	/* Left out by the header: the laser-fire flag and the player count. */
	g_laserFireTimestampTrackingEnabled = !g_laserFireTimestampTrackingEnabled;
	g_flightPlayerCount += 3;
	XVT_ASSERT_INT_EQ(XvtSnapshot_LiveChecksum(), base);

	/* Covered: the next object signature, which the image also carries. */
	g_nextObjectSignature = 2;
	XVT_ASSERT_TRUE(XvtSnapshot_LiveChecksum() != base);
}

/* The big world: 8 main slots holding flying objects, the first `crafts` of them with a craft record, then
 * `statics` static slots, so the object section is longer than one checksum region; and `flightGroups`
 * flight groups. */
static ObjectRecord g_bigObjects[24];
static MobileObject g_bigMobiles[8];
static CraftData g_bigCraft[8];

static void BigWorld(int flightGroups, int crafts, int statics) {
	EmptyWorld();
	memset(g_bigObjects, 0, sizeof g_bigObjects);
	memset(g_bigMobiles, 0, sizeof g_bigMobiles);
	memset(g_bigCraft, 0, sizeof g_bigCraft);
	g_objectTable = g_bigObjects;
	g_mobileObjectPoolBase = g_bigMobiles;
	g_craftDataPoolBase = g_bigCraft;
	g_regionMainObjectSlotEnd = 8;
	g_regionStaticObjectSlotCount = statics;
	g_craftDataPoolCapacity = 8;
	for (int i = 0; i < 8 + statics; ++i) {
		g_bigObjects[i].objectType = (uint8_t)(1 + i % 3);
		g_bigObjects[i].objectSignature = (uint16_t)(0x100 + i);
		g_bigObjects[i].world_x = 1000 * i;
	}
	for (int i = 0; i < 8; ++i) {
		g_bigObjects[i].mobj = &g_bigMobiles[i];
		g_bigMobiles[i].pCraft = i < crafts ? &g_bigCraft[i] : NULL;
		g_bigMobiles[i].speed = (int16_t)(10 + i);
	}
	g_missionHeader.numFlightGroups = (int16_t)flightGroups;
	for (int i = 0; i < flightGroups; ++i) {
		memset(&g_missionFgStats[i], 0x11 + i, sizeof g_missionFgStats[i]);
		memset(&g_missionFlightGroups[i], 0x31 + i, sizeof g_missionFlightGroups[i]);
	}
	g_players[0].objectIndex = 0;
	SizeImage();
}

/* Region sums and lengths of three big worlds, recorded from the code at fork commit 03e9d80. They pin
 * where today's regions close: inside the object section, after the flight-group tables, after the fixed
 * trailer tables, and (in the third world, whose 3,400-byte region shows it) right after the two short
 * tables that follow the 3,376-byte one. A change to the image layout or to the region rule must update
 * them. */
static const unsigned kBig4Sums[16] = {
	0x000001ef, 0x0000024c, 0x000056ee, 0x0004427c, 0x00000015, 0x00001c34
};
static const unsigned kBig4Lengths[16] = { 4005, 4005, 4096, 5528, 25509, 11760 };
static const unsigned kBig12Sums[16] = { 0x000002c4, 0x000002a4, 0x000f0175, 0x00000015, 0x00001c34 };
static const unsigned kBigSmallLengths[16] = { 4005, 4005, 8790, 3400, 22109, 11760 };
static const unsigned kBigSmallSums[16] = { 0x000001ef, 0x0000024c, 0x0004a070,
											0x00000000, 0x0000001d, 0x00001c34 };
static const unsigned kBig12Lengths[16] = { 5340, 5340, 20362, 25509, 11760 };

static void CheckChecksumRegionsInBigWorlds(void) {
	static const struct {
		int flightGroups, crafts, statics;
		const unsigned* sums;
		const unsigned* lengths;
	} cases[] = { { 4, 8, 2, kBig4Sums, kBig4Lengths },
				  { 12, 8, 2, kBig12Sums, kBig12Lengths },
				  { 4, 7, 10, kBigSmallSums, kBigSmallLengths } };

	for (size_t c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
		BigWorld(cases[c].flightGroups, cases[c].crafts, cases[c].statics);
		size_t written = XvtSnapshot_Encode(g_image, g_capacity);
		XVT_ASSERT_TRUE(written > 0);
		unsigned sums[16];
		unsigned lengths[16];
		XVT_ASSERT_INT_EQ(XvtSnapshot_ChecksumImage(g_image, written, sums, lengths), 1);
		/* More than one region closes, and the regions tile the world part from its start. */
		size_t covered = 0;
		int used = 0;
		for (int i = 0; i < 16; ++i) {
			covered += lengths[i];
			used += lengths[i] != 0;
		}
		XVT_ASSERT_TRUE(used > 2 && covered <= written);
		for (int i = 0; i < 16; ++i) {
			XVT_ASSERT_INT_EQ(sums[i], cases[c].sums[i]);
			XVT_ASSERT_INT_EQ(lengths[i], cases[c].lengths[i]);
		}
	}
	g_missionHeader.numFlightGroups = 0;
}

static void CheckValidateRefusesOtherRanges(void) {
	RichWorld();
	size_t written = XvtSnapshot_Encode(g_image, g_capacity);
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(g_image, written), 1);

	/* The slot-range bounds and the reserved dword must equal the live ones. */
	g_debrisObjectSlotStart += 1;
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(g_image, written), 0);
	g_debrisObjectSlotStart -= 1;
	g_worldStateReservedDword += 1;
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(g_image, written), 0);
	g_worldStateReservedDword -= 1;
	g_explosionObjectSlotEnd += 1;
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(g_image, written), 0);
	g_explosionObjectSlotEnd -= 1;
	XVT_ASSERT_INT_EQ(XvtSnapshot_Validate(g_image, written), 1);
}

/* Gives every value the live checksum mixes in without walking a pool a distinct nonzero value (seed 1),
 * or zero (seed 0), so a dropped, repeated or reordered field changes the result. */
static void SetMixedOnlyValues(int seed) {
	int v = seed ? 0x31 : 0;
	g_worldStateReservedByte = (uint8_t)v;
	g_worldStateReservedDword = v + 1;
	g_activeRegionObjectSlotStart = v + 2;
	g_activeRegionCraftObjectSlotEnd = v + 3;
	g_mobileObjectCharDataSlotStart = v + 4;
	g_mobileObjectCharDataSlotEnd = v + 5;
	g_projectileObjectSlotStart = v + 6;
	g_projectileObjectSlotEnd = v + 7;
	g_debrisObjectSlotStart = v + 8;
	g_debrisObjectSlotEnd = v + 9;
	g_explosionObjectSlotStart = v + 10;
	g_explosionObjectSlotEnd = v + 11;
	g_debrisObjectSlotsTotal = (unsigned)(v + 12);
	g_planCount = v + 13;
	g_unusedWorldStateSerializedDword = v + 14;
	g_gameRandStateB = (int16_t)(v + 15);
	g_flightConfNewNet = v + 16;
	g_activeFlightPlayerCount = v + 17;
	g_missionFileVersion = (uint16_t)(v + 18);
}

/* The live checksum of the rich world with 3 flight groups and player 0 connected, recorded from the
 * code at fork commit 03e9d80, with SetMixedOnlyValues(1). Every pool loop, both flight-group loops and
 * the player loop run. */
static const unsigned kRichLiveChecksum = 0xdb657f64u;

static void CheckLiveChecksumOfRichWorld(void) {
	RichWorld();
	g_missionHeader.numFlightGroups = 3;
	for (int i = 0; i < 3; ++i) {
		memset(&g_missionFgStats[i], 0x21 + i, sizeof g_missionFgStats[i]);
		memset(&g_missionFlightGroups[i], 0x41 + i, sizeof g_missionFlightGroups[i]);
	}
	g_players[0].connectedFlag = 1;
	g_nextObjectSignature = 9;
	/* Every pool record carries a nonzero field, so dropping any pool's loop changes the result. */
	g_testCraft[0].craftIndexInGroup = 3;
	g_testCharData[0].skillValue = 4;
	g_testGuidance[0].minSpeed = 5;
	g_testMobiles[0].speed = 6;
	g_testObjects[2].world_x = 7;
	SetMixedOnlyValues(1);
	int live = XvtSnapshot_LiveChecksum();
	XVT_ASSERT_INT_EQ((unsigned)live, kRichLiveChecksum);

	/* Covered: a pool entry's record, a flight group's stats, a connected player. */
	g_testObjects[0].world_x += 1;
	XVT_ASSERT_TRUE(XvtSnapshot_LiveChecksum() != live);
	g_testObjects[0].world_x -= 1;
	g_missionFgStats[2].spawnedCraftCount += 1;
	XVT_ASSERT_TRUE(XvtSnapshot_LiveChecksum() != live);
	g_missionFgStats[2].spawnedCraftCount -= 1;
	XVT_ASSERT_INT_EQ(XvtSnapshot_LiveChecksum(), live);
	g_players[0].connectedFlag = 0;
	XVT_ASSERT_TRUE(XvtSnapshot_LiveChecksum() != live);
	SetMixedOnlyValues(0);
	g_missionHeader.numFlightGroups = 0;
}

/* Block sizes in an image, and the rich world's layout: slot 0 holds all five blocks, slot 1 is empty,
 * slot 2 holds an object alone. */
#define OBJ_SIZE sizeof(XvtSnapshotObjectRecord)
#define MOB_SIZE sizeof(XvtSnapshotMobileObject)
#define CRAFT_SIZE sizeof(XvtSnapshotCraftData)
#define GUIDE_SIZE sizeof(WarheadGuidanceState)
#define CHAR_SIZE sizeof(XvtSnapshotMobileObjectCharData)

static uint8_t* g_dup;

/* Writes a 3-slot presence map: the slot count, then one flag byte per slot. */
static void Map3(uint8_t map[7], int count, uint8_t slot0, uint8_t slot1, uint8_t slot2) {
	memcpy(map, &count, sizeof count);
	map[4] = slot0;
	map[5] = slot1;
	map[6] = slot2;
}

/* Copies the first `written` bytes of g_image into the duplicate buffer, with room to grow, applies map,
 * and returns the new size. */
static size_t Apply(const uint8_t* map, size_t written) {
	free(g_dup);
	g_dup = calloc(1, written + 4096);
	XVT_ASSERT_TRUE(g_dup != NULL);
	memcpy(g_dup, g_image, written);
	g_worldStateDupBuffer = g_dup;
	worldStateSize = (int)written;
	XvtSnapshot_ApplyPresenceMap(map);
	return (size_t)worldStateSize;
}

/* True when the duplicate buffer is g_image with `length` bytes at `at` removed. */
static int Removed(size_t written, size_t at, size_t length) {
	return (size_t)worldStateSize == written - length && memcmp(g_dup, g_image, at) == 0 &&
		   memcmp(g_dup + at, g_image + at + length, written - at - length) == 0;
}

/* True when the duplicate buffer is g_image with `length` zero bytes inserted at `at`. */
static int Inserted(size_t written, size_t at, size_t length) {
	for (size_t i = 0; i < length; ++i)
		if (g_dup[at + i] != 0)
			return 0;
	return (size_t)worldStateSize == written + length && memcmp(g_dup, g_image, at) == 0 &&
		   memcmp(g_dup + at + length, g_image + at, written - at) == 0;
}

static void CheckApplyPresenceMapKeepsMatchingImage(void) {
	RichWorld();
	size_t written = XvtSnapshot_Encode(g_image, g_capacity);
	uint8_t map[16];
	XvtSnapshot_BuildPresenceMap(map, g_image);
	XVT_ASSERT_INT_EQ(Apply(map, written), written);
	XVT_ASSERT_INT_EQ(memcmp(g_dup, g_image, written), 0);

	/* With slot 0 emptied too, the map packs slots 0 and 1 as one run of two before slot 2's object. */
	g_testObjects[0].objectType = 0;
	g_players[0].objectIndex = -1;
	SizeImage();
	written = XvtSnapshot_Encode(g_image, g_capacity);
	XVT_ASSERT_INT_EQ(XvtSnapshot_BuildPresenceMap(map, g_image), (int)sizeof(int) + 2);
	XVT_ASSERT_INT_EQ(map[sizeof(int)], 0x82);
	XVT_ASSERT_INT_EQ(map[sizeof(int) + 1], 0x01);
	XVT_ASSERT_INT_EQ(Apply(map, written), written);
	XVT_ASSERT_INT_EQ(memcmp(g_dup, g_image, written), 0);
}

static void CheckApplyPresenceMapRemovesBlocks(void) {
	RichWorld();
	size_t written = XvtSnapshot_Encode(g_image, g_capacity);
	const size_t craft = kSlot0Mobile + MOB_SIZE;
	uint8_t map[7];

	Map3(map, 3, 0x1F & ~0x04, 0, 0x01);
	Apply(map, written);
	XVT_ASSERT_TRUE(Removed(written, craft, CRAFT_SIZE));
	Map3(map, 3, 0x1F & ~0x08, 0, 0x01);
	Apply(map, written);
	XVT_ASSERT_TRUE(Removed(written, craft + CRAFT_SIZE, GUIDE_SIZE));
	Map3(map, 3, 0x1F & ~0x10, 0, 0x01);
	Apply(map, written);
	XVT_ASSERT_TRUE(Removed(written, craft + CRAFT_SIZE + GUIDE_SIZE, CHAR_SIZE));

	/* Removing an object or mobile record leaves the blocks nested under it. The walk then reads the
	 * next slot inside those blocks, so these maps cover slot 0 alone. */
	Map3(map, 1, 0x01, 0, 0x00);
	Apply(map, written);
	XVT_ASSERT_TRUE(Removed(written, kSlot0Mobile, MOB_SIZE));
	Map3(map, 1, 0x00, 0, 0x00);
	Apply(map, written);
	XVT_ASSERT_TRUE(Removed(written, kSlot0Object, OBJ_SIZE));

	/* Slots from the map's slot count onward are left alone: slot 2 keeps its object. */
	Map3(map, 1, 0x1F & ~0x04, 0, 0x00);
	Apply(map, written);
	XVT_ASSERT_TRUE(Removed(written, craft, CRAFT_SIZE));
}

static void CheckApplyPresenceMapInsertsZeroedBlocks(void) {
	RichWorld();
	g_testMobiles[0].pCraft = NULL;
	g_testMobiles[0].pCharData = NULL;
	g_testMobiles[0].pWarheadGuidance = NULL;
	size_t written = XvtSnapshot_Encode(g_image, g_capacity);
	const size_t slot1 = kSlot0Mobile + MOB_SIZE;
	uint8_t map[7];

	/* Slot 0's mobile record gains a craft, guidance and character block, in image order. */
	Map3(map, 3, 0x1F, 0, 0x01);
	Apply(map, written);
	XVT_ASSERT_TRUE(Inserted(written, slot1, CRAFT_SIZE + GUIDE_SIZE + CHAR_SIZE));

	/* Inserting a record adds that record alone: slot 2 gains a mobile record, slot 1 an object record
	 * (after its type byte, which stays 0). */
	Map3(map, 3, 0x03, 0, 0x03);
	Apply(map, written);
	XVT_ASSERT_TRUE(Inserted(written, slot1 + 1 + 1 + OBJ_SIZE, MOB_SIZE));
	Map3(map, 3, 0x03, 0x01, 0x01);
	Apply(map, written);
	XVT_ASSERT_TRUE(Inserted(written, slot1 + 1, OBJ_SIZE));
}

int main(void) {
	CheckCalculateSize();
	CheckEncodeRefusals();
	CheckValidate();
	CheckDecodeRoundTrip();
	CheckDecodeRefusalLeavesWorld();
	CheckChecksumImage();
	CheckRichRoundTrip();
	CheckEmptySlotKeepsPoolLink();
	CheckValidateRefusesBadRecords();
	CheckChecksumSkipsLinks();
	CheckPresenceMap();
	CheckLiveChecksum();
	CheckChecksumRegionsInBigWorlds();
	CheckValidateRefusesOtherRanges();
	CheckLiveChecksumOfRichWorld();
	CheckApplyPresenceMapKeepsMatchingImage();
	CheckApplyPresenceMapRemovesBlocks();
	CheckApplyPresenceMapInsertsZeroedBlocks();
	free(g_dup);
	free(g_image);
	return 0;
}
