/* Checks the per-player step remainders (xvt_runtime/timing/player_timing.h) against the promises in its
 * header, on a world this file builds itself: six live objects with mobile records, slots 0 to 3 in the
 * main region and slot 4 in the static region; player 0 (the local player) flies slot 0, whose craft has
 * working flight controls, and player 1 flies slot 1. No game data is read. Each case starts from that
 * world, cleared player timing and a new flight timing session.
 *
 * Most checks watch one channel's carried remainder through Scale itself: Seed clears a channel and
 * leaves a carry of 3/4 on it, and Probe adds 1/4 more, so Probe returns 1 exactly when the carry
 * survived. */
#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/player_timing.h"
#include "xvt_runtime/timing/reference_motion.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

enum { kSlots = 6, kMainSlots = 4 };

static ObjectRecord g_testObjects[kSlots];
static MobileObject g_testMobiles[kSlots];
static CraftData g_testCraft;

static void FreshWorld(XvtFlightTimingProfile profile) {
	memset(g_testObjects, 0, sizeof g_testObjects);
	memset(g_testMobiles, 0, sizeof g_testMobiles);
	memset(&g_testCraft, 0, sizeof g_testCraft);
	for (int i = 0; i < kSlots; ++i) {
		g_testObjects[i].objectType = 1;
		g_testObjects[i].objectSignature = (uint16_t)(0x200 + i);
		g_testObjects[i].world_x = 1000 * i;
		g_testObjects[i].world_y = -100 * i;
		g_testObjects[i].world_z = 10 * i;
		g_testObjects[i].mobj = &g_testMobiles[i];
		g_testMobiles[i].prevWorldX = -7 - i;
		g_testMobiles[i].prevWorldY = -8 - i;
		g_testMobiles[i].prevWorldZ = -9 - i;
	}
	g_testMobiles[0].pCraft = &g_testCraft;
	g_testCraft.workingSubsystems = CRAFT_SUBSYSTEM_FLAG_FLIGHT_CONTROLS;
	g_objectTable = g_testObjects;
	g_regionMainObjectSlotEnd = kMainSlots;
	g_regionStaticObjectSlotCount = 1;
	memset(g_players, 0, sizeof g_players);
	for (int i = 0; i < 8; ++i) {
		g_players[i].objectIndex = -1;
		g_players[i].currentTargetObjectIdx = -1;
	}
	g_players[0].objectIndex = 0;
	g_players[0].currentTargetObjectIdx = 2;
	g_players[1].objectIndex = 1;
	g_localPlayer = 0;
	g_flightKeyMods = 0;
	g_elapsedTicks = 1;
	g_localTransientSlotStart = 0;
	g_localDebrisSlotEnd = 0;
	XvtFlightTiming_BeginSession(profile);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Init(kSlots), 1);
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Init(kSlots), 1);
	XvtPlayerTiming_Reset();
}

static void Seed(unsigned player, unsigned channel) {
	XvtPlayerTiming_Clear(player, channel);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(player, channel, 3, 1, 4), 0);
}

static int Probe(unsigned player, unsigned channel) {
	return XvtPlayerTiming_Scale(player, channel, 1, 1, 4);
}

/* Opens the next simulation step of `ticks` ticks. */
static void Step(uint16_t ticks) {
	XvtFlightTiming_EndAdvance();
	XvtFlightTiming_BeginAdvance(ticks);
}

static void CheckScale(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	/* Truncated toward zero. */
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(0, XVT_PLAYER_YAW, -7, 1, 4), -1);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(0, XVT_PLAYER_PITCH, 7, 1, 4), 1);

	/* Ten short steps add up to one long one. */
	int sum = 0, negative = 0;
	for (int i = 0; i < 10; ++i) {
		sum += XvtPlayerTiming_Scale(0, XVT_PLAYER_ROLL, 7, 1, 4);
		negative += XvtPlayerTiming_Scale(1, XVT_PLAYER_ROLL, -7, 1, 4);
	}
	XVT_ASSERT_INT_EQ(sum, XvtPlayerTiming_Scale(0, XVT_PLAYER_ZOOM, 7, 10, 4));
	XVT_ASSERT_INT_EQ(sum, 70 / 4);
	XVT_ASSERT_INT_EQ(negative, -70 / 4);

	/* A new sign drops the carry; zero is a sign of its own. */
	Seed(0, XVT_PLAYER_DISTANCE);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(0, XVT_PLAYER_DISTANCE, -1, 1, 4), 0);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(0, XVT_PLAYER_DISTANCE, -3, 1, 4), -1);
	Seed(0, XVT_PLAYER_SLEW_YAW);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(0, XVT_PLAYER_SLEW_YAW, 0, 1, 4), 0);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(0, XVT_PLAYER_SLEW_YAW, 1, 1, 4), 0);

	/* Clamped to int; 0 for a zero divisor or a channel past the enum. */
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(0, XVT_PLAYER_CAMERA_YAW, INT_MAX, 4, 1), INT_MAX);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(0, XVT_PLAYER_CAMERA_YAW, INT_MIN, 4, 1), INT_MIN);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(0, XVT_PLAYER_CAMERA_PITCH, 1000, 1, 0), 0);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(0, XVT_PLAYER_CHANNELS, 1000, 1, 1), 0);

	/* A player out of range gets the plain result with no carry. */
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(XVT_FLIGHT_PLAYERS, XVT_PLAYER_YAW, 3, 1, 4), 0);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(XVT_FLIGHT_PLAYERS, XVT_PLAYER_YAW, 3, 1, 4), 0);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(XVT_FLIGHT_PLAYERS, XVT_PLAYER_YAW, 7, 3, 4), 5);
}

static void CheckClearAndReset(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	Seed(0, XVT_PLAYER_YAW);
	Seed(0, XVT_PLAYER_PITCH);
	XvtPlayerTiming_Clear(0, XVT_PLAYER_YAW);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_YAW), 0);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_PITCH), 1);

	Seed(0, XVT_PLAYER_YAW);
	Seed(1, XVT_PLAYER_YAW);
	Seed(5, XVT_PLAYER_YAW);
	XvtPlayerTiming_Reset();
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_YAW), 0);
	XVT_ASSERT_INT_EQ(Probe(1, XVT_PLAYER_YAW), 0);
	XVT_ASSERT_INT_EQ(Probe(5, XVT_PLAYER_YAW), 0);
}

static void CheckEntryFollowsObject(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	/* The object in the main region changes signature: the entry is cleared. */
	Seed(0, XVT_PLAYER_YAW);
	g_testObjects[0].objectSignature = 0x999;
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_YAW), 0);

	/* The player moves to another main slot: cleared. */
	Seed(1, XVT_PLAYER_YAW);
	g_players[1].objectIndex = 2;
	XVT_ASSERT_INT_EQ(Probe(1, XVT_PLAYER_YAW), 0);

	/* An object outside the main region, or none: the entry is kept. */
	g_players[2].objectIndex = kMainSlots;
	Seed(2, XVT_PLAYER_YAW);
	g_testObjects[kMainSlots].objectSignature = 0x999;
	XVT_ASSERT_INT_EQ(Probe(2, XVT_PLAYER_YAW), 1);
	Seed(3, XVT_PLAYER_YAW);
	XVT_ASSERT_INT_EQ(Probe(3, XVT_PLAYER_YAW), 1);
}

static void CheckResetControls(void) {
	static const unsigned camera[] = { XVT_PLAYER_CAMERA_YAW, XVT_PLAYER_CAMERA_PITCH, XVT_PLAYER_DISTANCE,
									   XVT_PLAYER_ZOOM };
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	for (unsigned i = 0; i < 4; ++i)
		Seed(0, camera[i]);
	Seed(0, XVT_PLAYER_YAW);
	Seed(0, XVT_PLAYER_SLEW_YAW);
	Seed(1, XVT_PLAYER_CAMERA_YAW);
	XvtPlayerTiming_ResetControls();
	for (unsigned i = 0; i < 4; ++i)
		XVT_ASSERT_INT_EQ(Probe(0, camera[i]), 0);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_YAW), 1);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_SLEW_YAW), 1);
	/* Only the local player's. */
	XVT_ASSERT_INT_EQ(Probe(1, XVT_PLAYER_CAMERA_YAW), 1);

	FreshWorld(XVT_FLIGHT_TIMING_NATIVE);
	Seed(0, XVT_PLAYER_ZOOM);
	XvtPlayerTiming_ResetControls();
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_ZOOM), 0);

	/* In NETWORK_125 it does nothing. */
	FreshWorld(XVT_FLIGHT_TIMING_NETWORK_125);
	Seed(0, XVT_PLAYER_CAMERA_YAW);
	XvtPlayerTiming_ResetControls();
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_CAMERA_YAW), 1);
}

/* Seeds player 0's yaw, pitch, roll and slew channels, calls BeginControls, and checks whether the carry
 * survived on each. */
static void ExpectControlsKeep(int kept) {
	static const unsigned flight[] = { XVT_PLAYER_YAW, XVT_PLAYER_PITCH, XVT_PLAYER_ROLL, XVT_PLAYER_SLEW_YAW,
									   XVT_PLAYER_SLEW_PITCH };
	for (unsigned i = 0; i < 5; ++i)
		Seed(0, flight[i]);
	XvtPlayerTiming_BeginControls(0);
	for (unsigned i = 0; i < 5; ++i)
		XVT_ASSERT_INT_EQ(Probe(0, flight[i]), kept);
}

static void CheckBeginControlsMode(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	/* The first call clears; the same mode again keeps; another player is not touched. */
	Seed(1, XVT_PLAYER_YAW);
	ExpectControlsKeep(0);
	ExpectControlsKeep(1);
	XVT_ASSERT_INT_EQ(Probe(1, XVT_PLAYER_YAW), 1);

	/* Each part of the mode is a change: roll modifier held, ... */
	g_flightKeyMods = XVT_ROLL_MODIFIER;
	ExpectControlsKeep(0);
	ExpectControlsKeep(1);
	g_flightKeyMods = 0;
	ExpectControlsKeep(0);

	/* ...flight controls lost, ... */
	g_testCraft.workingSubsystems = 0;
	ExpectControlsKeep(0);
	ExpectControlsKeep(1);
	g_testCraft.workingSubsystems = CRAFT_SUBSYSTEM_FLAG_FLIGHT_CONTROLS;
	ExpectControlsKeep(0);

	/* ...beamEffectAccum[1] set, which disables only while no chaff is active, ... */
	g_testCraft.beamEffectAccum[1] = 1;
	g_testCraft.chaffActiveTimer = 5;
	ExpectControlsKeep(1);
	g_testCraft.chaffActiveTimer = 0;
	ExpectControlsKeep(0);
	g_testCraft.beamEffectAccum[1] = 0;
	ExpectControlsKeep(0);

	/* ...input blocked, map view and hyperspace. */
	g_players[0].viewState.playerInputBlocked = 1;
	ExpectControlsKeep(0);
	g_players[0].viewState.playerInputBlocked = 0;
	ExpectControlsKeep(0);
	g_players[0].mapCameraState = 1;
	ExpectControlsKeep(0);
	g_players[0].mapCameraState = 0;
	ExpectControlsKeep(0);
	g_players[0].hyperspacePhase = 2;
	ExpectControlsKeep(0);
	g_players[0].hyperspacePhase = 0;
	ExpectControlsKeep(0);
	ExpectControlsKeep(1);
}

static void CheckBeginControlsCamera(void) {
	static const unsigned camera[] = { XVT_PLAYER_CAMERA_YAW, XVT_PLAYER_CAMERA_PITCH, XVT_PLAYER_DISTANCE,
									   XVT_PLAYER_ZOOM };
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtPlayerTiming_BeginControls(0);

	/* A new camera focus clears the camera channels and keeps the others. */
	for (unsigned i = 0; i < 4; ++i)
		Seed(0, camera[i]);
	Seed(0, XVT_PLAYER_YAW);
	g_players[0].viewState.cameraFocusObjIdx = 3;
	XvtPlayerTiming_BeginControls(0);
	for (unsigned i = 0; i < 4; ++i)
		XVT_ASSERT_INT_EQ(Probe(0, camera[i]), 0);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_YAW), 1);

	/* The same focus keeps them, and so does a change of mode. */
	for (unsigned i = 0; i < 4; ++i)
		Seed(0, camera[i]);
	XvtPlayerTiming_BeginControls(0);
	g_flightKeyMods = XVT_ROLL_MODIFIER;
	XvtPlayerTiming_BeginControls(0);
	for (unsigned i = 0; i < 4; ++i)
		XVT_ASSERT_INT_EQ(Probe(0, camera[i]), 1);
}

static void CheckSlew(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	/* Under 8: all of it, and the carry is dropped. */
	Seed(0, XVT_PLAYER_SLEW_YAW);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Slew(0, XVT_PLAYER_SLEW_YAW, 5), 5);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_SLEW_YAW), 0);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Slew(0, XVT_PLAYER_SLEW_PITCH, -7), -7);

	/* Otherwise 4 * max(1, |difference| / 29) per 8 ticks, with the difference's sign. */
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsedTicks = 8;
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Slew(0, XVT_PLAYER_SLEW_YAW, 100), 4 * (100 / 29));
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Slew(1, XVT_PLAYER_SLEW_YAW, -100), -4 * (100 / 29));
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Slew(1, XVT_PLAYER_SLEW_PITCH, 20), 4);
	g_elapsedTicks = 16;
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Slew(0, XVT_PLAYER_CAMERA_YAW, 1000), 2 * 4 * (1000 / 29));

	/* One tick at a time it is carried: eight one-tick steps move what one eight-tick step does. */
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsedTicks = 1;
	int sum = 0;
	for (int i = 0; i < 8; ++i)
		sum += XvtPlayerTiming_Slew(0, XVT_PLAYER_SLEW_PITCH, 100);
	XVT_ASSERT_INT_EQ(sum, 4 * (100 / 29));

	/* A step that would reach the difference returns all of it and drops the carry: 3 ticks of 4 per 8
	 * leave 4/8 carried; 64 more ticks would pass 10; afterwards 4/8 alone makes nothing. */
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsedTicks = 3;
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Slew(0, XVT_PLAYER_SLEW_YAW, 10), 1);
	g_elapsedTicks = 64;
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Slew(0, XVT_PLAYER_SLEW_YAW, 10), 10);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Scale(0, XVT_PLAYER_SLEW_YAW, 4, 1, 8), 0);
}

/* Known failure slew_int_min: the header promises a step for any difference, and INT_MIN's magnitude
 * is 2^31, so with 8 ticks the step is -4 * (2^31 / 29). The code takes abs(INT_MIN), which is undefined
 * behavior, and the sanitizer stops the program there. */
static void CheckSlewMostNegative(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsedTicks = 8;
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Slew(0, XVT_PLAYER_SLEW_YAW, INT_MIN), -4 * ((INT64_C(1) << 31) / 29));
}

static void CheckLockHalfLocked(void) {
	FreshWorld(XVT_FLIGHT_TIMING_NATIVE);
	g_elapsedTicks = 7;
	for (int i = 0; i < 3; ++i) {
		Step(7);
		XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_NONE), 3);
		XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_CHAFF), 3);
		XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(XVT_FLIGHT_PLAYERS, XVT_LOCK_HALF_TARGET_LOSS), 3);
	}
}

static void CheckLockHalfUnlocked(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsedTicks = 3;
	Step(3);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_NONE), 0);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(XVT_FLIGHT_PLAYERS, XVT_LOCK_HALF_CHAFF), 0);

	/* Consecutive steps carry the odd tick: four steps of 3 ticks give 12 / 2. */
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsedTicks = 3;
	unsigned sum = 0;
	for (int i = 0; i < 4; ++i) {
		Step(3);
		sum += XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_CHAFF);
	}
	XVT_ASSERT_INT_EQ(sum, 12 / 2);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(XVT_FLIGHT_PLAYERS, XVT_LOCK_HALF_CHAFF), 0);
}

/* Starts a fresh unlocked world in which player 0's lock timing carries an odd tick out of this step. */
static void OddTickCarried(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsedTicks = 3;
	Step(3);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_CHAFF), 1);
}

static void CheckLockHalfCarryDropped(void) {
	/* Kept: the next step with everything the same returns (3 + 1) / 2. */
	OddTickCarried();
	Step(3);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_CHAFF), 2);

	/* Dropped: a step skipped, ... */
	OddTickCarried();
	Step(3);
	Step(3);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_CHAFF), 1);
	/* ...a second call in the same step, ... */
	OddTickCarried();
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_CHAFF), 1);
	/* ...another mode, ... */
	OddTickCarried();
	Step(3);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_TARGET_LOSS), 1);
	/* ...another target, ... */
	OddTickCarried();
	g_players[0].currentTargetObjectIdx = 3;
	Step(3);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_CHAFF), 1);
	/* ...another selected warhead, ... */
	OddTickCarried();
	g_players[0].selectedWarhead = 1;
	Step(3);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_CHAFF), 1);
	/* ...another player object, ... */
	OddTickCarried();
	g_testObjects[0].objectSignature = 0x999;
	Step(3);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_CHAFF), 1);
	/* ...or mode 0 in between. */
	OddTickCarried();
	Step(3);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_NONE), 0);
	Step(3);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_CHAFF), 1);
}

static void ExpectPosition(const int32_t position[3], int32_t x, int32_t y, int32_t z) {
	XVT_ASSERT_INT_EQ(position[0], x);
	XVT_ASSERT_INT_EQ(position[1], y);
	XVT_ASSERT_INT_EQ(position[2], z);
}

static void MoveObject(unsigned slot, int dx) { g_testObjects[slot].world_x += dx; }

static void CheckRecordRecovery(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtPlayerTiming_BeginWorld();
	MoveObject(0, 50);
	int32_t position[3] = { 1, 2, 3 };

	/* Outside a reference step nothing is written. */
	Step(1);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(0, position), 0);
	ExpectPosition(position, 1, 2, 3);

	/* In one: the position BeginWorld recorded, then the object's position now; once per step. */
	Step(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(0, position), 1);
	ExpectPosition(position, 0, 0, 0);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(0, position), 0);
	MoveObject(0, 25);
	Step(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(0, position), 1);
	ExpectPosition(position, 50, 0, 0);
	Step(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(0, position), 1);
	ExpectPosition(position, 75, 0, 0);

	/* A player out of range. */
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(XVT_FLIGHT_PLAYERS, position), 0);

	/* Locked flights: every step is a reference step. */
	FreshWorld(XVT_FLIGHT_TIMING_NATIVE);
	XvtPlayerTiming_BeginWorld();
	Step(1);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(1, position), 1);
	ExpectPosition(position, 1000, -100, 10);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(1, position), 0);
	Step(1);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(1, position), 1);
}

static void CheckBeginWorld(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	Seed(0, XVT_PLAYER_YAW);
	Seed(2, XVT_PLAYER_YAW);
	/* Player 2 flies the static slot: not in the main region, so no recovery position. */
	g_players[2].objectIndex = kMainSlots;
	XvtPlayerTiming_BeginWorld();
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_YAW), 0);
	XVT_ASSERT_INT_EQ(Probe(2, XVT_PLAYER_YAW), 0);

	/* Without a recovery position, the object's previous-step position is written. */
	int32_t position[3];
	Step(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(2, position), 1);
	ExpectPosition(position, -7 - kMainSlots, -8 - kMainSlots, -9 - kMainSlots);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(1, position), 1);
	ExpectPosition(position, 1000, -100, 10);
}

static void CheckRecover(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtPlayerTiming_BeginWorld();
	XvtPlayerTiming_BeginControls(0);
	g_elapsedTicks = 3;
	Step(3);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_CHAFF), 1);
	Seed(0, XVT_PLAYER_YAW);
	Seed(0, XVT_PLAYER_ZOOM);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_PITCH, 3, 1, 4), 0);
	XvtReferenceMotionWire motion;
	XvtReferenceMotion_Encode(0, &motion);
	XVT_ASSERT_INT_EQ(motion.type, 1);

	MoveObject(0, 40);
	XvtPlayerTiming_Recover(0);
	/* Channels, the object's step remainders and its reference motion are gone. */
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_YAW), 0);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_ZOOM), 0);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_PITCH, 1, 1, 4), 0);
	XvtReferenceMotion_Encode(0, &motion);
	XVT_ASSERT_INT_EQ(motion.type, 0);

	/* Lock timing is kept: the odd tick carries into the next step. */
	Step(3);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_CHAFF), 2);
	/* Control state is kept: the same mode is not a first call. */
	Seed(0, XVT_PLAYER_YAW);
	XvtPlayerTiming_BeginControls(0);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_YAW), 1);

	/* The recovery position is where the object was at Recover. */
	MoveObject(0, 5);
	int32_t position[3];
	Step(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(0, position), 1);
	ExpectPosition(position, 40, 0, 0);
}

static int RecordsEqual(const XvtPlayerTimingWire* a, const XvtPlayerTimingWire* b) {
	return memcmp(a, b, sizeof *a) == 0;
}

static XvtPlayerTimingWire EmptyRecord(unsigned player) {
	XvtPlayerTimingWire record;
	memset(&record, 0, sizeof record);
	record.player = (uint8_t)player;
	return record;
}

/* A well-formed record for player 0 in slot 0. */
static XvtPlayerTimingWire GoodRecord(void) {
	XvtPlayerTimingWire record = EmptyRecord(0);
	record.valid = 1;
	XvtWire_Set16(record.slot, 0);
	XvtWire_Set16(record.signature, 0x200);
	for (unsigned i = 0; i < XVT_STATE_PLAYER_CHANNELS; ++i) {
		XvtWire_Set64(record.remainder[i], (uint64_t)(int64_t)(i % 2 ? -(int)(10 * i) : (int)(10 * i)));
		record.direction[i] = (int8_t)((int)(i % 3) - 1);
	}
	record.lock_mode = XVT_LOCK_HALF_CHAFF;
	record.lock_half = 1;
	record.control_valid = 1;
	XvtWire_Set32(record.control_mode, XVT_CONTROL_ROLL | XVT_CONTROL_MAP);
	XvtWire_Set16(record.lock_signature, 0x200);
	XvtWire_Set16(record.lock_target, 2);
	XvtWire_Set16(record.lock_target_signature, 0x202);
	XvtWire_Set16(record.lock_weapon, 1);
	XvtWire_Set64(record.lock_frame, 41);
	XvtWire_Set16(record.camera_focus, 3);
	return record;
}

static void CheckEncodeDecode(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	const XvtPlayerTimingWire good = GoodRecord();
	XvtPlayerTimingWire out;
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Decode(&good, 1), 1);
	XvtPlayerTiming_Encode(0, &out);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &good));

	/* Carries built by Scale and lock timing survive a round trip into cleared state. */
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtPlayerTiming_BeginControls(0);
	g_elapsedTicks = 3;
	Step(3);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_CHAFF), 1);
	Seed(0, XVT_PLAYER_SLEW_PITCH);
	Seed(0, XVT_PLAYER_DISTANCE);
	XvtPlayerTiming_Encode(0, &out);
	XVT_ASSERT_INT_EQ(out.valid, 1);
	XvtPlayerTiming_Reset();
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Decode(&out, 1), 1);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_SLEW_PITCH), 1);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_PLAYER_DISTANCE), 1);
	Step(3);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_LockHalf(0, XVT_LOCK_HALF_CHAFF), 2);

	/* Without apply it is judged, not installed. */
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Decode(&good, 0), 1);
	XvtPlayerTiming_Encode(0, &out);
	XVT_ASSERT_INT_EQ(out.valid, 0);
}

static void CheckRecoveryNotShared(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtPlayerTiming_BeginWorld();
	Seed(0, XVT_PLAYER_YAW);
	XvtPlayerTimingWire before, after;
	XvtPlayerTiming_Encode(0, &before);

	/* The record does not carry the recovery position: moving it leaves the record as it was. */
	MoveObject(0, 30);
	int32_t position[3];
	Step(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(0, position), 1);
	XvtPlayerTiming_Encode(0, &after);
	XVT_ASSERT_TRUE(RecordsEqual(&before, &after));

	/* Decode leaves the recovery position: the next record is the one taken above. */
	const XvtPlayerTimingWire good = GoodRecord();
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Decode(&good, 1), 1);
	Step(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(0, position), 1);
	ExpectPosition(position, 30, 0, 0);
}

static void CheckEncodeEmpty(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	const XvtPlayerTimingWire good = GoodRecord();
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Decode(&good, 1), 1);
	XvtPlayerTimingWire out, empty;

	XvtPlayerTiming_Encode(XVT_FLIGHT_PLAYERS, &out);
	empty = EmptyRecord(XVT_FLIGHT_PLAYERS);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));

	/* Player 5 has no object. */
	XvtPlayerTiming_Encode(5, &out);
	empty = EmptyRecord(5);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));

	/* Player 0's entry belongs to another object, its object is dead, it is outside the main region. */
	empty = EmptyRecord(0);
	g_testObjects[0].objectSignature = 0x999;
	XvtPlayerTiming_Encode(0, &out);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));
	g_testObjects[0].objectSignature = 0x200;
	g_testObjects[0].objectType = 0;
	XvtPlayerTiming_Encode(0, &out);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));
	g_testObjects[0].objectType = 1;
	g_regionMainObjectSlotEnd = 0;
	XvtPlayerTiming_Encode(0, &out);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));
	g_regionMainObjectSlotEnd = kMainSlots;
	XvtPlayerTiming_Encode(0, &out);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &good));
}

static void ExpectRefused(const XvtPlayerTimingWire* record) {
	XvtPlayerTimingWire before, after;
	XvtPlayerTiming_Encode(0, &before);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Decode(record, 1), 0);
	XvtPlayerTiming_Encode(0, &after);
	XVT_ASSERT_TRUE(RecordsEqual(&before, &after));
}

static void CheckDecodeRefusals(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtPlayerTiming_BeginControls(0);
	Seed(0, XVT_PLAYER_YAW);
	const XvtPlayerTimingWire good = GoodRecord();
	XvtPlayerTimingWire bad;

	bad = good;
	bad.player = XVT_FLIGHT_PLAYERS;
	ExpectRefused(&bad);
	bad = good;
	bad.valid = 2;
	ExpectRefused(&bad);
	bad = good;
	bad.lock_half = 2;
	ExpectRefused(&bad);
	bad = good;
	bad.control_valid = 2;
	ExpectRefused(&bad);
	bad = good;
	bad.lock_mode = XVT_LOCK_HALF_TARGET_LOSS + 1;
	ExpectRefused(&bad);
	bad = good;
	XvtWire_Set32(bad.control_mode, XVT_CONTROL_MASK + 1);
	ExpectRefused(&bad);
	bad = good;
	XvtWire_Set16(bad.reserved, 1);
	ExpectRefused(&bad);
	bad = good;
	XvtWire_Set16(bad.reserved_tail, 1);
	ExpectRefused(&bad);

	/* Not valid: exactly the empty record. */
	bad = EmptyRecord(0);
	XvtWire_Set16(bad.camera_focus, 1);
	ExpectRefused(&bad);

	/* Valid: a main-region slot, directions -1 to 1, remainders under one second of ticks. */
	bad = good;
	XvtWire_Set16(bad.slot, kMainSlots);
	ExpectRefused(&bad);
	bad = good;
	bad.direction[4] = 2;
	ExpectRefused(&bad);
	bad = good;
	bad.direction[8] = -2;
	ExpectRefused(&bad);
	bad = good;
	XvtWire_Set64(bad.remainder[0], SIMULATION_TICKS_PER_SECOND);
	ExpectRefused(&bad);
	bad = good;
	XvtWire_Set64(bad.remainder[8], (uint64_t)-(int64_t)SIMULATION_TICKS_PER_SECOND);
	ExpectRefused(&bad);

	/* One short of the bounds is accepted, and so is the empty record. */
	bad = good;
	XvtWire_Set64(bad.remainder[0], SIMULATION_TICKS_PER_SECOND - 1);
	XvtWire_Set64(bad.remainder[8], (uint64_t)(1 - (int64_t)SIMULATION_TICKS_PER_SECOND));
	XvtWire_Set16(bad.slot, kMainSlots - 1);
	bad.lock_mode = XVT_LOCK_HALF_TARGET_LOSS;
	XvtWire_Set32(bad.control_mode, XVT_CONTROL_MASK);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Decode(&bad, 0), 1);
	bad = EmptyRecord(7);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Decode(&bad, 0), 1);
}

static void CheckResetShared(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtPlayerTiming_BeginWorld();
	XvtPlayerTiming_BeginControls(0);
	Seed(0, XVT_PLAYER_YAW);
	MoveObject(0, 60);
	int32_t position[3];
	Step(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(0, position), 1);

	/* The shared state, the entry's object included, is gone: the record is empty. */
	XvtPlayerTiming_ResetShared();
	XvtPlayerTimingWire out, empty = EmptyRecord(0);
	XvtPlayerTiming_Encode(0, &out);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));

	/* Restoring the shared state from a record finds the recovery position still there. */
	const XvtPlayerTimingWire good = GoodRecord();
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_Decode(&good, 1), 1);
	Step(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(XvtPlayerTiming_RecordRecovery(0, position), 1);
	ExpectPosition(position, 60, 0, 0);
}

int main(int argc, char** argv) {
	/* "known-failure <check>" runs one check the code is known to fail; an unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		if (strcmp(argv[2], "slew_int_min") == 0)
			CheckSlewMostNegative();
		return 0;
	}
	CheckScale();
	CheckClearAndReset();
	CheckEntryFollowsObject();
	CheckResetControls();
	CheckBeginControlsMode();
	CheckBeginControlsCamera();
	CheckSlew();
	CheckLockHalfLocked();
	CheckLockHalfUnlocked();
	CheckLockHalfCarryDropped();
	CheckRecordRecovery();
	CheckBeginWorld();
	CheckRecover();
	CheckEncodeDecode();
	CheckRecoveryNotShared();
	CheckEncodeEmpty();
	CheckDecodeRefusals();
	CheckResetShared();
	XvtFlightIntegration_Shutdown();
	XvtReferenceMotion_Shutdown();
	XvtFlightTiming_EndSession();
	return 0;
}
