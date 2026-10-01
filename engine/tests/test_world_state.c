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
	free(g_image);
	return 0;
}
