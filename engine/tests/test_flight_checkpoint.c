/* Checks the timing extension of a network125 world checkpoint (xvt_runtime/runtime/flight_checkpoint.h)
 * against the promises in its header: the membership masks and the abort flags SetMask raises, an extension
 * appended after world bytes and read back, what Read refuses, what Restore puts
 * back, and the per-player paired motion. No game data is read: the test builds a world of five object slots,
 * slots 0 to 3 in the main region and slot 4 in the static region, with slot 2 the local transient range, so
 * slots 0, 1, 3 and 4 are shared. Player 0 flies slot 0, whose craft can carry another object, and player 1
 * flies slot 1. Each check starts from that world in a fresh network125 timing session at game time TICK,
 * with the integration and reference motion tables made for it and a flight begun with players 0 and 1.
 *
 * Most checks watch one integration channel through XvtFlightIntegration_Rate (flight_integration.h): Seed
 * leaves a carried remainder of 3/4 on a slot, and Probe adds 1/4 more, so Probe returns 1 exactly when that
 * remainder is there. */
#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/net/flight_net.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_state.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/player_timing.h"
#include "xvt_runtime/timing/reference_motion.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
	kSlots = 5,
	kMainSlots = 4,
	kLocalSlot = 2,
	kSharedSlots = 4,
	PREFIX = 100,
	TICK = 40,
	CHANNEL = XVT_INTEGRATE_PUSH_X,
};

static ObjectRecord g_testObjects[kSlots];
static MobileObject g_testMobiles[kSlots];
static CraftData g_testCraft;
static uint8_t *g_image, *g_saved, *g_again;
static size_t g_capacity;

static void World(void) {
	memset(g_testObjects, 0, sizeof g_testObjects);
	memset(g_testMobiles, 0, sizeof g_testMobiles);
	memset(&g_testCraft, 0, sizeof g_testCraft);
	for (int i = 0; i < kSlots; ++i) {
		g_testObjects[i].objectType = 1;
		g_testObjects[i].objectSignature = (uint16_t)(0x300 + i);
		g_testObjects[i].world_x = 1000 * i;
		g_testObjects[i].mobj = &g_testMobiles[i];
	}
	g_testMobiles[0].pCraft = &g_testCraft;
	g_testCraft.carriedObjectIndex = UINT16_MAX;
	g_objectTable = g_testObjects;
	g_regionMainObjectSlotEnd = kMainSlots;
	g_regionStaticObjectSlotCount = kSlots - kMainSlots;
	g_localTransientSlotStart = kLocalSlot;
	g_localDebrisSlotEnd = kLocalSlot + 1;
	memset(g_players, 0, sizeof g_players);
	for (int i = 0; i < 8; ++i) {
		g_players[i].objectIndex = -1;
		g_players[i].currentTargetObjectIdx = -1;
		g_playerAbortFlags[i] = 0;
	}
	g_players[0].objectIndex = 0;
	g_players[0].connectedFlag = 1;
	g_players[0].lockstepTimestamp = TICK;
	g_players[1].objectIndex = 1;
	g_players[1].connectedFlag = 1;
	g_localPlayer = 0;
	g_gameTime = TICK;
	g_elapsedTicks = XVT_NETWORK_STEP_TICKS;
	XvtFlightTiming_BeginSession(XVT_FLIGHT_TIMING_NETWORK_125);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Init(kSlots), 1);
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Init(kSlots), 1);
	XvtPlayerTiming_Reset();
	XvtFlightCheckpoint_Begin(0x03);

	free(g_image);
	free(g_saved);
	free(g_again);
	g_capacity = PREFIX + XvtFlightCheckpoint_Maximum();
	g_image = calloc(1, g_capacity);
	g_saved = calloc(1, g_capacity);
	g_again = calloc(1, g_capacity);
	XVT_ASSERT_TRUE(g_image && g_saved && g_again);
	for (int i = 0; i < PREFIX; ++i)
		g_image[i] = (uint8_t)(i * 7 + 1);
}

static void Seed(unsigned slot) {
	XvtFlightIntegration_Clear(slot, CHANNEL);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(slot, CHANNEL, 3, 1, 4), 0);
}

static int Probe(unsigned slot) { return XvtFlightIntegration_Rate(slot, CHANNEL, 1, 1, 4); }

static size_t Append(uint8_t* image) { return XvtFlightCheckpoint_Append(image, PREFIX); }

/* The player's paired record as an extension appended now carries it. */
static XvtPairedMotionWire Paired(unsigned player) {
	size_t size = Append(g_image);
	XvtFlightCheckpointView view;
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_Read(g_image, size, &view), 1);
	XvtPairedMotionWire record;
	memcpy(&record, view.paired + player * sizeof record, sizeof record);
	return record;
}

static XvtStateFooter Footer(const uint8_t* image, size_t size) {
	XvtStateFooter footer;
	memcpy(&footer, image + size - sizeof footer, sizeof footer);
	return footer;
}

static void SetFooter(uint8_t* image, size_t size, const XvtStateFooter* footer) {
	memcpy(image + size - sizeof *footer, footer, sizeof *footer);
}

/* After a change inside the extension, writes its CRC-32C into the footer again, so that Read judges the
 * change itself and not the CRC. */
static void Reseal(uint8_t* image, size_t size) {
	XvtStateFooter footer = Footer(image, size);
	XvtWire_Set32(footer.timing_crc, XvtFlightWire_Crc32c(image + XvtWire_Get32(footer.world_bytes),
														  XvtWire_Get32(footer.timing_bytes)));
	SetFooter(image, size, &footer);
}

static int ReadImage(const uint8_t* image, size_t size) {
	XvtFlightCheckpointView view;
	return XvtFlightCheckpoint_Read(image, size, &view);
}

static void CheckBeginAndMasks(void) {
	World();
	XvtFlightCheckpoint_Begin(0x07);
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_InitialMask(), 0x07);
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_ConfirmedMask(), 0x07);

	/* Dropping an initial player raises its abort flag; every other player's flag is cleared. */
	g_playerAbortFlags[6] = 1;
	XvtFlightCheckpoint_ApplyConfirmedMask(0x05);
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_ConfirmedMask(), 0x05);
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_InitialMask(), 0x07);
	for (int i = 0; i < 8; ++i)
		XVT_ASSERT_INT_EQ(g_playerAbortFlags[i], i == 1);

	/* The confirmed mask stays within the initial one. */
	XvtFlightCheckpoint_ApplyConfirmedMask(0xFF);
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_ConfirmedMask(), 0x07);
	for (int i = 0; i < 8; ++i)
		XVT_ASSERT_INT_EQ(g_playerAbortFlags[i], 0);

	XvtFlightCheckpoint_ApplyConfirmedMask(0);
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_ConfirmedMask(), 0);
	for (int i = 0; i < 8; ++i)
		XVT_ASSERT_INT_EQ(g_playerAbortFlags[i], i < 3);

	/* Begin sets both masks again and clears every paired record. */
	Seed(0);
	XvtFlightCheckpoint_SavePlayer(0, TICK);
	XVT_ASSERT_TRUE(Paired(0).validity & XVT_PAIRED_OWNER_VALID);
	XvtFlightCheckpoint_Begin(0x03);
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_InitialMask(), 0x03);
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_ConfirmedMask(), 0x03);
	XVT_ASSERT_INT_EQ(Paired(0).validity, 0);
}

static void CheckAppendRead(void) {
	World();
	Seed(0);
	Seed(1);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(0, XVT_PLAYER_YAW, 3, 1, 4), 0);
	size_t size = Append(g_image);
	XVT_ASSERT_TRUE(size > PREFIX + sizeof(XvtStateFooter));
	XVT_ASSERT_TRUE(size - PREFIX <= XvtFlightCheckpoint_Maximum());

	XvtFlightCheckpointView view;
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_Read(g_image, size, &view), 1);
	XVT_ASSERT_INT_EQ(view.prefix, PREFIX);
	XVT_ASSERT_INT_EQ(view.tick, TICK);
	XVT_ASSERT_INT_EQ(view.objects, kSharedSlots);
	XVT_ASSERT_INT_EQ(view.membership.initial, 0x03);
	XVT_ASSERT_INT_EQ(view.membership.confirmed, 0x03);
	/* The view points into the image, after the world bytes. */
	const uint8_t* parts[] = { view.reference, view.integration, view.players, view.paired };
	for (int i = 0; i < 4; ++i)
		XVT_ASSERT_TRUE(parts[i] >= g_image + PREFIX && parts[i] < g_image + size);

	size_t prefix = 0;
	int tick = -1;
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_Validate(g_image, size, &prefix, &tick), 1);
	XVT_ASSERT_INT_EQ(prefix, PREFIX);
	XVT_ASSERT_INT_EQ(tick, TICK);

	/* The world bytes are not checked. */
	g_image[0] ^= 0xFF;
	g_image[PREFIX - 1] ^= 0xFF;
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 1);

	/* Read changes no state: an extension appended afterwards is the same. */
	XVT_ASSERT_INT_EQ(Append(g_again), size);
	XVT_ASSERT_INT_EQ(memcmp(g_again + PREFIX, g_image + PREFIX, size - PREFIX), 0);
	XVT_ASSERT_INT_EQ(Probe(0), 1);
	XVT_ASSERT_INT_EQ(Probe(1), 1);

	/* The footer records the game time: a later time reads back as the tick. */
	g_gameTime = TICK + 2 * XVT_NETWORK_STEP_TICKS;
	size = Append(g_image);
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_Validate(g_image, size, &prefix, &tick), 1);
	XVT_ASSERT_INT_EQ(tick, g_gameTime);
}

static void CheckReadRefusals(void) {
	World();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Cookie(), 0);
	Seed(0);
	size_t size = Append(g_saved);
	memcpy(g_image, g_saved, size);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 1);
	XvtFlightCheckpointView view;
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_Read(g_saved, size, &view), 1);
	size_t reference = (size_t)(view.reference - g_saved);
	size_t players = (size_t)(view.players - g_saved);
	size_t membership = (size_t)(view.paired - g_saved) + XVT_FLIGHT_PLAYERS * sizeof(XvtPairedMotionWire);
	XvtStateFooter good = Footer(g_saved, size), footer;

	XVT_ASSERT_INT_EQ(ReadImage(g_image, sizeof(XvtStateFooter) - 1), 0);

	/* The footer's magic, schema, profile and cookie. */
	footer = good;
	XvtWire_Set32(footer.magic, XvtWire_Get32(good.magic) + 1);
	SetFooter(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 0);
	footer = good;
	XvtWire_Set16(footer.schema, XvtWire_Get16(good.schema) + 1);
	SetFooter(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 0);
	footer = good;
	XvtWire_Set16(footer.profile, XvtWire_Get16(good.profile) + 1);
	SetFooter(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 0);
	footer = good;
	XvtWire_Set32(footer.cookie, XvtFlightNetwork_Cookie() + 1);
	SetFooter(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 0);

	/* A tick off the step, or negative. */
	footer = good;
	XvtWire_Set32(footer.completed_tick, TICK + 1);
	SetFooter(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 0);
	XvtWire_Set32(footer.completed_tick, (uint32_t)-XVT_NETWORK_STEP_TICKS);
	SetFooter(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 0);
	XvtWire_Set32(footer.completed_tick, TICK + XVT_NETWORK_STEP_TICKS);
	SetFooter(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 1);

	/* Lengths that do not add up to the image. */
	footer = good;
	XvtWire_Set32(footer.world_bytes, PREFIX + 1);
	SetFooter(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 0);
	footer = good;
	XvtWire_Set32(footer.timing_bytes, XvtWire_Get32(good.timing_bytes) - 1);
	SetFooter(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 0);
	SetFooter(g_image, size, &good);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 1);
	XVT_ASSERT_INT_EQ(ReadImage(g_image + 1, size - 1), 0);

	/* A changed byte in the extension fails the CRC. */
	g_image[reference] ^= 0x01;
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 0);
	memcpy(g_image, g_saved, size);

	/* The shared-slot count must be this world's. */
	g_localDebrisSlotEnd = kLocalSlot;
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 0);
	g_localDebrisSlotEnd = kLocalSlot + 1;
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 1);

	/* Every record must decode: a reference record with an unknown flag, a player record whose valid
	 * byte is neither 0 nor 1 (reference_motion.h and player_timing.h say which records are well formed). */
	g_image[reference + offsetof(XvtReferenceMotionWire, flags)] |= 0x80;
	Reseal(g_image, size);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 0);
	memcpy(g_image, g_saved, size);
	g_image[players + offsetof(XvtPlayerTimingWire, valid)] = 2;
	Reseal(g_image, size);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 0);
	memcpy(g_image, g_saved, size);

	/* Membership: the confirmed mask must stay within the initial one... */
	g_image[membership + offsetof(XvtMembershipWire, confirmed)] = 0x83;
	Reseal(g_image, size);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 0);
	g_image[membership + offsetof(XvtMembershipWire, confirmed)] = 0x01;
	Reseal(g_image, size);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 1);
	/* ...and the initial mask must be this flight's. */
	memcpy(g_image, g_saved, size);
	XvtFlightCheckpoint_Begin(0x07);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 0);
	XvtFlightCheckpoint_Begin(0x03);
	XVT_ASSERT_INT_EQ(ReadImage(g_image, size), 1);
}

static void CheckRestore(void) {
	World();
	Seed(0);
	Seed(1);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(0, XVT_PLAYER_YAW, 3, 1, 4), 0);
	XvtFlightCheckpoint_ApplyConfirmedMask(0x01);
	size_t size = Append(g_saved);
	memcpy(g_image, g_saved, size);
	XvtFlightCheckpointView view;
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_Read(g_image, size, &view), 1);

	/* Move on: spend slot 0's remainder, seed one on slot 3, confirm both players again, and let time
	 * pass. */
	XVT_ASSERT_INT_EQ(Probe(0), 1);
	Seed(3);
	XvtFlightCheckpoint_ApplyConfirmedMask(0x03);
	XVT_ASSERT_INT_EQ(g_playerAbortFlags[1], 0);
	g_gameTime = TICK + 10 * XVT_NETWORK_STEP_TICKS;

	XvtFlightCheckpoint_Restore(&view);
	XVT_ASSERT_INT_EQ(g_gameTime, TICK);
	/* The network tick is the view's (flight_timing.h: the advance serial is tick / step). */
	XVT_ASSERT_TRUE(XvtFlightTiming_AdvanceSerial() == TICK / XVT_NETWORK_STEP_TICKS);
	XVT_ASSERT_INT_EQ(XvtFlightCheckpoint_ConfirmedMask(), 0x01);
	XVT_ASSERT_INT_EQ(g_playerAbortFlags[1], 1);

	/* Every record is back: the extension appended now is the one restored from. */
	XVT_ASSERT_INT_EQ(Append(g_again), size);
	XVT_ASSERT_INT_EQ(memcmp(g_again + PREFIX, g_saved + PREFIX, size - PREFIX), 0);
	XVT_ASSERT_INT_EQ(Probe(0), 1);
	XVT_ASSERT_INT_EQ(Probe(3), 0);
}

static void CheckSavePlayer(void) {
	World();
	Seed(0);
	XvtFlightCheckpoint_SavePlayer(0, TICK);
	XvtPairedMotionWire record = Paired(0);
	XVT_ASSERT_INT_EQ(record.validity, XVT_PAIRED_OWNER_VALID);
	XVT_ASSERT_INT_EQ(record.player, 0);
	XVT_ASSERT_INT_EQ(XvtWire_Get32(record.saved_tick), TICK);
	XVT_ASSERT_INT_EQ(XvtWire_Get16(record.owner_id.slot), 0);
	XVT_ASSERT_INT_EQ(XvtWire_Get16(record.owner_id.signature), 0x300);
	/* Only player 0's record changed. */
	XVT_ASSERT_INT_EQ(Paired(1).validity, 0);

	/* The record stays invalid when the player's object is not a live shared slot: a local slot, an
	 * empty slot, or none. */
	g_players[0].objectIndex = kLocalSlot;
	XvtFlightCheckpoint_SavePlayer(0, TICK);
	XVT_ASSERT_INT_EQ(Paired(0).validity, 0);
	g_players[0].objectIndex = 1;
	g_testObjects[1].objectType = 0;
	XvtFlightCheckpoint_SavePlayer(0, TICK);
	XVT_ASSERT_INT_EQ(Paired(0).validity, 0);
	g_testObjects[1].objectType = 1;
	g_players[0].objectIndex = -1;
	XvtFlightCheckpoint_SavePlayer(0, TICK);
	XVT_ASSERT_INT_EQ(Paired(0).validity, 0);
	g_players[0].objectIndex = 0;

	/* A player out of range is ignored. */
	XvtFlightCheckpoint_SavePlayer(XVT_FLIGHT_PLAYERS, TICK);
	XvtFlightCheckpoint_RestorePlayer(XVT_FLIGHT_PLAYERS);

	/* InvalidatePlayer clears the record; out of range it is ignored. */
	XvtFlightCheckpoint_SavePlayer(0, TICK);
	XvtFlightCheckpoint_SavePlayer(1, TICK);
	XvtFlightCheckpoint_InvalidatePlayer(0);
	XvtFlightCheckpoint_InvalidatePlayer(XVT_FLIGHT_PLAYERS);
	XVT_ASSERT_INT_EQ(Paired(0).validity, 0);
	XVT_ASSERT_INT_EQ(Paired(1).validity, XVT_PAIRED_OWNER_VALID);
}

static void CheckRestorePlayer(void) {
	/* A record saved at the player's lockstep tick for its current object is restored. */
	World();
	Seed(0);
	XvtFlightCheckpoint_SavePlayer(0, TICK);
	XVT_ASSERT_INT_EQ(Probe(0), 1);
	XvtFlightCheckpoint_RestorePlayer(0);
	XVT_ASSERT_INT_EQ(Probe(0), 1);

	/* Saved at another tick: the object's integration is reset... */
	World();
	Seed(0);
	XvtFlightCheckpoint_SavePlayer(0, TICK);
	g_players[0].lockstepTimestamp = TICK + XVT_NETWORK_STEP_TICKS;
	XvtFlightCheckpoint_RestorePlayer(0);
	XVT_ASSERT_INT_EQ(Probe(0), 0);
	/* ...and the record invalidated: at the right tick again it no longer restores, it resets. */
	XVT_ASSERT_INT_EQ(Paired(0).validity, 0);
	g_players[0].lockstepTimestamp = TICK;
	Seed(0);
	XvtFlightCheckpoint_RestorePlayer(0);
	XVT_ASSERT_INT_EQ(Probe(0), 0);

	/* Saved for another slot: the integration of the player's current object is reset. */
	World();
	Seed(0);
	XvtFlightCheckpoint_SavePlayer(0, TICK);
	g_players[0].objectIndex = 1;
	Seed(1);
	XvtFlightCheckpoint_RestorePlayer(0);
	XVT_ASSERT_INT_EQ(Probe(1), 0);
	XVT_ASSERT_INT_EQ(Paired(0).validity, 0);
}

static void CheckCarriedObject(void) {
	/* Slot 0's craft carries slot 3: both motions are saved, and both restored. */
	World();
	g_testCraft.carriedObjectIndex = 3;
	Seed(0);
	Seed(3);
	XvtFlightCheckpoint_SavePlayer(0, TICK);
	XvtPairedMotionWire record = Paired(0);
	XVT_ASSERT_INT_EQ(record.validity, XVT_PAIRED_OWNER_VALID | XVT_PAIRED_CARRIED_VALID);
	XVT_ASSERT_INT_EQ(XvtWire_Get16(record.carried_id.slot), 3);
	XVT_ASSERT_INT_EQ(XvtWire_Get16(record.carried_id.signature), 0x303);
	XVT_ASSERT_INT_EQ(Probe(3), 1);
	XvtFlightCheckpoint_RestorePlayer(0);
	XVT_ASSERT_INT_EQ(Probe(3), 1);

	/* A carried object that is not another live shared slot is not saved: the local slot. */
	World();
	g_testCraft.carriedObjectIndex = kLocalSlot;
	Seed(0);
	XvtFlightCheckpoint_SavePlayer(0, TICK);
	XVT_ASSERT_INT_EQ(Paired(0).validity, XVT_PAIRED_OWNER_VALID);

	/* The craft now carries another shared object: that object's integration is reset. */
	World();
	g_testCraft.carriedObjectIndex = 3;
	Seed(0);
	Seed(3);
	XvtFlightCheckpoint_SavePlayer(0, TICK);
	g_testCraft.carriedObjectIndex = 4;
	Seed(4);
	XvtFlightCheckpoint_RestorePlayer(0);
	XVT_ASSERT_INT_EQ(Probe(4), 0);
	XVT_ASSERT_INT_EQ(Probe(0), 1);
}

static void CheckNetwork125Only(void) {
	World();
	Seed(0);
	XvtFlightCheckpoint_SavePlayer(0, TICK);

	/* Outside network125, SavePlayer leaves the record as it was and RestorePlayer does nothing, not even
	 * on a mismatch. */
	XvtFlightTiming_BeginSession(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtFlightCheckpoint_SavePlayer(0, TICK + 8);
	XvtFlightCheckpoint_SavePlayer(1, TICK);
	XvtPairedMotionWire record = Paired(0);
	XVT_ASSERT_INT_EQ(record.validity, XVT_PAIRED_OWNER_VALID);
	XVT_ASSERT_INT_EQ(XvtWire_Get32(record.saved_tick), TICK);
	XVT_ASSERT_INT_EQ(Paired(1).validity, 0);
	g_players[0].lockstepTimestamp = TICK + XVT_NETWORK_STEP_TICKS;
	Seed(0);
	XvtFlightCheckpoint_RestorePlayer(0);
	XVT_ASSERT_INT_EQ(Probe(0), 1);
	XVT_ASSERT_INT_EQ(Paired(0).validity, XVT_PAIRED_OWNER_VALID);
}

int main(void) {
	CheckBeginAndMasks();
	CheckAppendRead();
	CheckReadRefusals();
	CheckRestore();
	CheckSavePlayer();
	CheckRestorePlayer();
	CheckCarriedObject();
	CheckNetwork125Only();
	XvtFlightIntegration_Shutdown();
	XvtReferenceMotion_Shutdown();
	XvtFlightTiming_EndSession();
	free(g_image);
	free(g_saved);
	free(g_again);
	return 0;
}
