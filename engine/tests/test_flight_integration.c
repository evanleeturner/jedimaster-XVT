/* Checks the per-object step remainders (xvt_runtime/timing/flight_integration.h) against the promises in
 * its header, on an object table this file builds itself: six live objects, each with a mobile record,
 * and no game data. Each case starts from that table and a fresh remainder table.
 *
 * Most checks watch one channel's carried remainder through Rate itself: Seed leaves a carry of 3/4 on a
 * channel, and Probe adds 1/4 more, so Probe returns 1 exactly when the carry survived. */
#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/math/trig2.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/reference_motion.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

/* An allocation that cannot be met must come back as NULL, so Init's refusal can be seen; by default
 * AddressSanitizer stops the program instead. */
const char* __asan_default_options(void) { return "allocator_may_return_null=1"; }

enum { kSlots = 6 };

static ObjectRecord g_testObjects[kSlots];
static MobileObject g_testMobiles[kSlots];
static CraftData g_testCraft;
static WarheadGuidanceState g_testGuidance;

/* Six live objects (type 1, signature 0x100 + slot) at the world origin, each with a still mobile
 * record; one simulation tick per step; no local slots; empty remainder and reference motion tables. */
static void FreshWorld(void) {
	memset(g_testObjects, 0, sizeof g_testObjects);
	memset(g_testMobiles, 0, sizeof g_testMobiles);
	memset(&g_testCraft, 0, sizeof g_testCraft);
	memset(&g_testGuidance, 0, sizeof g_testGuidance);
	for (int i = 0; i < kSlots; ++i) {
		g_testObjects[i].objectType = 1;
		g_testObjects[i].objectSignature = (uint16_t)(0x100 + i);
		g_testObjects[i].mobj = &g_testMobiles[i];
	}
	g_objectTable = g_testObjects;
	g_elapsedTicks = 1;
	g_localTransientSlotStart = 0;
	g_localDebrisSlotEnd = 0;
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Init(kSlots), 1);
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Init(kSlots), 1);
}

static void Seed(unsigned slot, unsigned channel) {
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(slot, channel, 3, 1, 4), 0);
}

static int Probe(unsigned slot, unsigned channel) {
	return XvtFlightIntegration_Rate(slot, channel, 1, 1, 4);
}

/* Reads slot's entry without leaving a carry: a zero rate moves nothing. */
static void Touch(unsigned slot) {
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(slot, XVT_INTEGRATE_TURN, 0, 1, 4), 0);
}

static void CheckSeedAndProbe(void) {
	FreshWorld();
	Seed(0, XVT_INTEGRATE_PITCH);
	/* Channels and slots carry separately. */
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_TURN), 0);
	XVT_ASSERT_INT_EQ(Probe(1, XVT_INTEGRATE_PITCH), 0);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_PITCH), 1);
}

static void CheckNoTable(void) {
	FreshWorld();
	XvtFlightIntegration_Shutdown();
	/* Without the table every call is the plain truncated result, with nothing carried. */
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_PITCH, 3, 1, 4), 0);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_PITCH, 3, 1, 4), 0);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_PITCH, 7, 3, 4), 5);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_PITCH, -7, 3, 4), -5);
	for (int i = 0; i < 300; ++i)
		XVT_ASSERT_INT_EQ(XvtFlightIntegration_Steer(0, XVT_INTEGRATE_ROLL, 1, 0xFFFF, 0xFFFF, 1), 0);

	/* An allocation that fails leaves no table either. */
	FreshWorld();
	Seed(0, XVT_INTEGRATE_PITCH);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Init(SIZE_MAX / 2), 0);
	Seed(0, XVT_INTEGRATE_PITCH);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_PITCH), 0);

	/* Init again starts from empty entries. */
	FreshWorld();
	Seed(0, XVT_INTEGRATE_PITCH);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Init(kSlots), 1);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_PITCH), 0);
}

static void CheckRateCarry(void) {
	FreshWorld();
	/* Truncated toward zero. */
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_TURN, -7, 1, 4), -1);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(1, XVT_INTEGRATE_TURN, 7, 1, 4), 1);

	/* Ten short steps add up to what one step of ten ticks gives: nothing is lost to truncation. */
	FreshWorld();
	int sum = 0, negative = 0;
	for (int i = 0; i < 10; ++i) {
		sum += XvtFlightIntegration_Rate(2, XVT_INTEGRATE_BANK, 7, 1, 4);
		negative += XvtFlightIntegration_Rate(3, XVT_INTEGRATE_BANK, -7, 1, 4);
	}
	XVT_ASSERT_INT_EQ(sum, XvtFlightIntegration_Rate(4, XVT_INTEGRATE_BANK, 7, 10, 4));
	XVT_ASSERT_INT_EQ(sum, 70 / 4);
	XVT_ASSERT_INT_EQ(negative, -70 / 4);
}

static void CheckRateSignChange(void) {
	FreshWorld();
	/* A carry of 3/4 is dropped when the rate turns negative: -1/4 then -3/4 make exactly -1. */
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_ROLL, 3, 1, 4), 0);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_ROLL, -1, 1, 4), 0);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_ROLL, -3, 1, 4), -1);

	/* Zero counts as a sign of its own: a zero rate drops the carry too. */
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(1, XVT_INTEGRATE_ROLL, 3, 1, 4), 0);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(1, XVT_INTEGRATE_ROLL, 0, 1, 4), 0);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(1, XVT_INTEGRATE_ROLL, 1, 1, 4), 0);
}

static void CheckRateLimits(void) {
	FreshWorld();
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_COUNT, 1000, 1, 1), 0);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_PITCH, 1000, 1, 0), 0);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_PITCH, 1000, 1, -4), 0);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_PITCH, INT_MAX, 4, 1), INT_MAX);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_PITCH, INT_MIN, 4, 1), INT_MIN);
}

static void CheckClear(void) {
	FreshWorld();
	Seed(0, XVT_INTEGRATE_PITCH);
	Seed(0, XVT_INTEGRATE_TURN);
	XvtFlightIntegration_Clear(0, XVT_INTEGRATE_PITCH);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_PITCH), 0);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_TURN), 1);
}

static void CheckEntryFollowsObject(void) {
	FreshWorld();
	Seed(0, XVT_INTEGRATE_PITCH);
	g_testObjects[0].objectSignature = 0x999;
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_PITCH), 0);

	Seed(1, XVT_INTEGRATE_PITCH);
	g_testObjects[1].objectType = 2;
	XVT_ASSERT_INT_EQ(Probe(1, XVT_INTEGRATE_PITCH), 0);

	Seed(2, XVT_INTEGRATE_PITCH);
	g_testMobiles[2].state = 3;
	XVT_ASSERT_INT_EQ(Probe(2, XVT_INTEGRATE_PITCH), 0);

	/* The same object keeps its carry. */
	Seed(3, XVT_INTEGRATE_PITCH);
	XVT_ASSERT_INT_EQ(Probe(3, XVT_INTEGRATE_PITCH), 1);
}

static void CheckSpinChannels(void) {
	FreshWorld();
	/* While the roll impulse rate is 0, the spin channels carry nothing; other channels still do. */
	g_testMobiles[0].rollImpulseRate = 0;
	Seed(0, XVT_INTEGRATE_SPIN_DECAY);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_SPIN_DECAY), 0);
	Seed(0, XVT_INTEGRATE_SPIN_ANGLE);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_SPIN_ANGLE), 0);
	Seed(0, XVT_INTEGRATE_PUSH_X);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_PUSH_X), 1);

	g_testMobiles[1].rollImpulseRate = 5;
	Seed(1, XVT_INTEGRATE_SPIN_DECAY);
	XVT_ASSERT_INT_EQ(Probe(1, XVT_INTEGRATE_SPIN_DECAY), 1);
	Seed(1, XVT_INTEGRATE_SPIN_ANGLE);
	XVT_ASSERT_INT_EQ(Probe(1, XVT_INTEGRATE_SPIN_ANGLE), 1);
}

static void CheckHomingChannels(void) {
	FreshWorld();
	g_testMobiles[0].pWarheadGuidance = &g_testGuidance;
	g_testGuidance.homingTier = 1;
	g_testGuidance.targetObjIdx = 3;
	g_testGuidance.targetSignature = 0x103;

	/* A steady target keeps the homing carry. */
	Seed(0, XVT_INTEGRATE_HOME_YAW);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_HOME_YAW), 1);

	/* A new target index or signature drops it, on the homing channels only. */
	Seed(0, XVT_INTEGRATE_HOME_PITCH);
	Seed(0, XVT_INTEGRATE_TURN);
	g_testGuidance.targetObjIdx = 4;
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_HOME_PITCH), 0);
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_TURN), 1);
	Seed(0, XVT_INTEGRATE_HOME_SPEED);
	g_testGuidance.targetSignature = 0x104;
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_HOME_SPEED), 0);

	/* Homing stopped drops it. */
	Seed(0, XVT_INTEGRATE_HOME_YAW);
	g_testGuidance.homingTier = 0;
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_HOME_YAW), 0);
}

static void CheckCarriedObjectReset(void) {
	FreshWorld();
	g_testMobiles[0].pCraft = &g_testCraft;
	g_testCraft.carriedObjectIndex = 1;
	/* The craft's first read records what it carries. */
	Touch(0);

	Seed(1, XVT_INTEGRATE_PITCH);
	Seed(2, XVT_INTEGRATE_PITCH);
	Seed(3, XVT_INTEGRATE_PITCH);
	/* Unchanged: nothing is reset. */
	Touch(0);
	XVT_ASSERT_INT_EQ(Probe(1, XVT_INTEGRATE_PITCH), 1);
	Seed(1, XVT_INTEGRATE_PITCH);

	/* Carrying slot 2 instead of slot 1 resets both; slot 3 is untouched. */
	g_testCraft.carriedObjectIndex = 2;
	Touch(0);
	XVT_ASSERT_INT_EQ(Probe(1, XVT_INTEGRATE_PITCH), 0);
	XVT_ASSERT_INT_EQ(Probe(2, XVT_INTEGRATE_PITCH), 0);
	XVT_ASSERT_INT_EQ(Probe(3, XVT_INTEGRATE_PITCH), 1);
}

static void CheckSteer(void) {
	FreshWorld();
	/* 0xFFFF counts as a whole: 236 * 1 * 1 * 1 over 236 is exactly 1. */
	XVT_ASSERT_INT_EQ(
		XvtFlightIntegration_Steer(0, XVT_INTEGRATE_ROLL, SIMULATION_TICKS_PER_SECOND, 0xFFFF, 0xFFFF, 1), 1);
	/* 0x8000 is a half. */
	XVT_ASSERT_INT_EQ(
		XvtFlightIntegration_Steer(1, XVT_INTEGRATE_ROLL, 2 * SIMULATION_TICKS_PER_SECOND, 0x8000, 0xFFFF, 1),
		1);
	XVT_ASSERT_INT_EQ(
		XvtFlightIntegration_Steer(2, XVT_INTEGRATE_ROLL, 2 * SIMULATION_TICKS_PER_SECOND, 0xFFFF, 0x8000, 1),
		1);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Steer(0, XVT_INTEGRATE_COUNT, 1000, 0xFFFF, 0xFFFF, 1), 0);

	/* Clamped to 0xFFFF. */
	g_elapsedTicks = 1000;
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Steer(3, XVT_INTEGRATE_ROLL, 0xFFFF, 0xFFFF, 0xFFFF, 1), 0xFFFF);

	/* 472 steps of one tick add up to one step of 472 ticks. */
	FreshWorld();
	unsigned sum = 0;
	for (int i = 0; i < 2 * SIMULATION_TICKS_PER_SECOND; ++i)
		sum += XvtFlightIntegration_Steer(0, XVT_INTEGRATE_TURN, 100, 0x8000, 0xFFFF, 1);
	g_elapsedTicks = 2 * SIMULATION_TICKS_PER_SECOND;
	XVT_ASSERT_INT_EQ(sum, XvtFlightIntegration_Steer(1, XVT_INTEGRATE_TURN, 100, 0x8000, 0xFFFF, 1));
	XVT_ASSERT_INT_EQ(sum, 100);

	/* A new direction drops the carry: 235/236 of a unit carried, then 1/236 the other way is 0. */
	FreshWorld();
	for (int i = 0; i < SIMULATION_TICKS_PER_SECOND - 1; ++i)
		XVT_ASSERT_INT_EQ(XvtFlightIntegration_Steer(0, XVT_INTEGRATE_BANK, 1, 0xFFFF, 0xFFFF, 1), 0);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Steer(1, XVT_INTEGRATE_BANK, 1, 0xFFFF, 0xFFFF, 1), 0);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Steer(0, XVT_INTEGRATE_BANK, 1, 0xFFFF, 0xFFFF, -1), 0);
	/* The same direction keeps it: the 236th step completes a unit. */
	for (int i = 0; i < SIMULATION_TICKS_PER_SECOND - 2; ++i)
		XVT_ASSERT_INT_EQ(XvtFlightIntegration_Steer(1, XVT_INTEGRATE_BANK, 1, 0xFFFF, 0xFFFF, 1), 0);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Steer(1, XVT_INTEGRATE_BANK, 1, 0xFFFF, 0xFFFF, 1), 1);
}

/* Runs Move on slot `steps` times and returns the summed movement on each axis. */
static void MoveSum(unsigned slot, int steps, int64_t sum[3]) {
	sum[0] = sum[1] = sum[2] = 0;
	for (int i = 0; i < steps; ++i) {
		XvtFlightIntegration_Move(slot);
		sum[0] += trig2_xmovedist;
		sum[1] += trig2_ymovedist;
		sum[2] += trig2_zmovedist;
	}
}

static void SetMotion(unsigned slot, uint16_t speed, int16_t x, int16_t y, int16_t z) {
	g_testMobiles[slot].speed = speed;
	g_testMobiles[slot].moveX = x;
	g_testMobiles[slot].moveY = y;
	g_testMobiles[slot].moveZ = z;
}

static void CheckMove(void) {
	/* Speed 256 scales to (4660 * 256 + 128) >> 8 = 4660; over 236 * 32768, an axis of 32767 moves 4660 *
	 * 32767 / 7733248 = 19.74 per tick and an axis of 1000 moves 0.60. */
	FreshWorld();
	XvtFlightIntegration_Shutdown();
	SetMotion(0, 256, 32767, 1000, 0);
	g_elapsedTicks = 1;
	XvtFlightIntegration_Move(0);
	XVT_ASSERT_INT_EQ(trig2_xmovedist, 19);
	XVT_ASSERT_INT_EQ(trig2_ymovedist, 0);
	XVT_ASSERT_INT_EQ(trig2_zmovedist, 0);
	g_elapsedTicks = 10;
	XvtFlightIntegration_Move(0);
	XVT_ASSERT_INT_EQ(trig2_xmovedist, 197);
	XVT_ASSERT_INT_EQ(trig2_ymovedist, 6);

	/* With the table, ten one-tick steps add up to one ten-tick step, on every axis and either sign. */
	FreshWorld();
	SetMotion(0, 300, 1000, -1000, 32767);
	SetMotion(1, 300, 1000, -1000, 32767);
	int64_t steps[3];
	g_elapsedTicks = 1;
	MoveSum(0, 10, steps);
	g_elapsedTicks = 10;
	XvtFlightIntegration_Move(1);
	XVT_ASSERT_INT_EQ(steps[0], trig2_xmovedist);
	XVT_ASSERT_INT_EQ(steps[1], trig2_ymovedist);
	XVT_ASSERT_INT_EQ(steps[2], trig2_zmovedist);
	XVT_ASSERT_TRUE(steps[0] > 0 && steps[1] < 0);

	/* At or past +-0x01000000 on an axis, that axis carries nothing; the others still do. */
	FreshWorld();
	SetMotion(0, 256, 1000, 1000, 1000);
	g_testObjects[0].world_x = 0x01000000;
	g_testObjects[0].world_y = 0x00FFFFFF;
	g_testObjects[0].world_z = -0x01000000;
	g_elapsedTicks = 1;
	MoveSum(0, 10, steps);
	XVT_ASSERT_INT_EQ(steps[0], 0);
	XVT_ASSERT_INT_EQ(steps[1], 6);
	XVT_ASSERT_INT_EQ(steps[2], 0);
}

static void CheckPush(void) {
	FreshWorld();
	g_elapsedTicks = 1;
	/* Clamped to the cap: 236 per second at one tick moves 1. Nothing is created or lost. */
	int accum = 1000, output = 0;
	XvtFlightIntegration_Push(0, 0, &accum, SIMULATION_TICKS_PER_SECOND, &output);
	XVT_ASSERT_INT_EQ(output, 1);
	XVT_ASSERT_INT_EQ(accum, 999);
	for (int i = 0; i < 50; ++i) {
		XvtFlightIntegration_Push(0, 0, &accum, 100, &output);
		XVT_ASSERT_INT_EQ(accum + output, 1000);
	}
	/* 50 ticks at 100 a second, carried: 5000 / 236 = 21. */
	XVT_ASSERT_INT_EQ(output, 1 + 21);

	int negative = -1000, out_negative = 0;
	XvtFlightIntegration_Push(1, 1, &negative, SIMULATION_TICKS_PER_SECOND, &out_negative);
	XVT_ASSERT_INT_EQ(out_negative, -1);
	XVT_ASSERT_INT_EQ(negative, -999);

	/* Never more than the accumulator holds. */
	g_elapsedTicks = 1000;
	int small = 5, out_small = 7;
	XvtFlightIntegration_Push(2, 2, &small, 10000, &out_small);
	XVT_ASSERT_INT_EQ(small, 0);
	XVT_ASSERT_INT_EQ(out_small, 12);
	int small_negative = -5, out_small_negative = 0;
	XvtFlightIntegration_Push(3, 0, &small_negative, 10000, &out_small_negative);
	XVT_ASSERT_INT_EQ(small_negative, 0);
	XVT_ASSERT_INT_EQ(out_small_negative, -5);

	/* Emptying the accumulator drops the axis's carry: 300/236 leaves 64/236 behind until then. */
	FreshWorld();
	g_elapsedTicks = 300;
	int last = 1, out_last = 0;
	XvtFlightIntegration_Push(4, 1, &last, 1, &out_last);
	XVT_ASSERT_INT_EQ(last, 0);
	XvtIntegrationWire record;
	XvtFlightIntegration_Encode(4, &record);
	XVT_ASSERT_INT_EQ(record.type, 1);
	XVT_ASSERT_INT_EQ(XvtWire_Get64(record.remainder[XVT_INTEGRATE_PUSH_Y]), 0);
	XVT_ASSERT_INT_EQ(record.direction[XVT_INTEGRATE_PUSH_Y], 0);
}

static int RecordsEqual(const XvtIntegrationWire* a, const XvtIntegrationWire* b) {
	return memcmp(a, b, sizeof *a) == 0;
}

/* A well-formed record for slot 2's object: every channel and axis carries something. */
static XvtIntegrationWire GoodRecord(void) {
	XvtIntegrationWire record;
	memset(&record, 0, sizeof record);
	XvtWire_Set16(record.slot, 2);
	XvtWire_Set16(record.signature, 0x102);
	record.type = 1;
	record.state = 0;
	XvtWire_Set16(record.carried_slot, 0xFFFF);
	XvtWire_Set16(record.target_slot, 4);
	XvtWire_Set16(record.target_signature, 0x104);
	for (unsigned axis = 0; axis < XVT_STATE_POSITION_AXES; ++axis)
		XvtWire_Set64(record.position[axis], (uint64_t)(int64_t)(axis == 1 ? -1000 : 1000 + (int)axis));
	for (unsigned channel = 0; channel < XVT_INTEGRATE_COUNT; ++channel) {
		XvtWire_Set64(record.remainder[channel],
					  (uint64_t)(int64_t)(channel % 2 ? -(int)channel : (int)channel));
		record.direction[channel] = (int8_t)(channel % 3) - 1;
	}
	return record;
}

static void CheckEncodeDecode(void) {
	FreshWorld();
	XvtIntegrationWire record = GoodRecord(), out;
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Decode(&record, 1), 1);
	XvtFlightIntegration_Encode(2, &out);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &record));

	/* A carry built by Rate survives a round trip through a record into a fresh table. */
	FreshWorld();
	Seed(3, XVT_INTEGRATE_TURN);
	XvtFlightIntegration_Encode(3, &out);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Init(kSlots), 1);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Decode(&out, 1), 1);
	XVT_ASSERT_INT_EQ(Probe(3, XVT_INTEGRATE_TURN), 1);

	/* Without apply the record is judged but not installed. */
	FreshWorld();
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Decode(&record, 0), 1);
	XvtFlightIntegration_Encode(2, &out);
	XVT_ASSERT_INT_EQ(out.type, 0);

	/* The record is not checked against the object in the slot. */
	FreshWorld();
	XvtWire_Set16(record.signature, 0x777);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Decode(&record, 1), 1);
}

static void CheckEncodeEmpty(void) {
	FreshWorld();
	XvtIntegrationWire record = GoodRecord(), out, empty;
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Decode(&record, 1), 1);
	memset(&empty, 0, sizeof empty);
	XvtWire_Set16(empty.slot, 2);

	g_testObjects[2].objectSignature = 0x999;
	XvtFlightIntegration_Encode(2, &out);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));
	g_testObjects[2].objectSignature = 0x102;
	g_testObjects[2].objectType = 7;
	XvtFlightIntegration_Encode(2, &out);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));
	g_testObjects[2].objectType = 1;
	g_testMobiles[2].state = 9;
	XvtFlightIntegration_Encode(2, &out);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));
	g_testMobiles[2].state = 0;
	XvtFlightIntegration_Encode(2, &out);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &record));

	/* An empty slot. */
	g_testObjects[2].objectType = 0;
	XvtFlightIntegration_Encode(2, &out);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));

	/* A slot out of range. */
	XvtFlightIntegration_Encode(kSlots + 3, &out);
	memset(&empty, 0, sizeof empty);
	XvtWire_Set16(empty.slot, kSlots + 3);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));
}

/* Decodes record with apply and checks it is refused and slot 2's entry is as before. */
static void ExpectRefused(const XvtIntegrationWire* record) {
	XvtIntegrationWire before, after;
	XvtFlightIntegration_Encode(2, &before);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Decode(record, 1), 0);
	XvtFlightIntegration_Encode(2, &after);
	XVT_ASSERT_TRUE(RecordsEqual(&before, &after));
}

static void CheckDecodeRefusals(void) {
	FreshWorld();
	Seed(2, XVT_INTEGRATE_PITCH);
	const XvtIntegrationWire good = GoodRecord();
	XvtIntegrationWire bad;
	const int64_t position_limit = (int64_t)SIMULATION_TICKS_PER_SECOND * 32768;
	const int64_t steering_limit = (int64_t)SIMULATION_TICKS_PER_SECOND * 65536 * 65536;
	const int64_t other_limit = INT64_C(1) << 31;

	bad = good;
	XvtWire_Set16(bad.slot, kSlots);
	ExpectRefused(&bad);

	/* Type 0 must be exactly the empty record. */
	memset(&bad, 0, sizeof bad);
	XvtWire_Set16(bad.slot, 2);
	XvtWire_Set16(bad.signature, 1);
	ExpectRefused(&bad);
	XvtWire_Set16(bad.signature, 0);
	bad.direction[XVT_INTEGRATE_BANK] = 1;
	ExpectRefused(&bad);

	bad = good;
	XvtWire_Set16(bad.carried_slot, kSlots);
	ExpectRefused(&bad);
	bad = good;
	XvtWire_Set16(bad.target_slot, kSlots);
	ExpectRefused(&bad);

	bad = good;
	bad.direction[XVT_INTEGRATE_PUSH_Z] = 2;
	ExpectRefused(&bad);
	bad.direction[XVT_INTEGRATE_PUSH_Z] = -2;
	ExpectRefused(&bad);

	bad = good;
	XvtWire_Set64(bad.position[2], (uint64_t)position_limit);
	ExpectRefused(&bad);
	XvtWire_Set64(bad.position[2], (uint64_t)-position_limit);
	ExpectRefused(&bad);

	bad = good;
	XvtWire_Set64(bad.remainder[XVT_INTEGRATE_ROLL], (uint64_t)steering_limit);
	ExpectRefused(&bad);
	bad = good;
	XvtWire_Set64(bad.remainder[XVT_INTEGRATE_BANK], (uint64_t)-steering_limit);
	ExpectRefused(&bad);

	bad = good;
	XvtWire_Set64(bad.remainder[XVT_INTEGRATE_HOME_SPEED], (uint64_t)other_limit);
	ExpectRefused(&bad);
	bad = good;
	XvtWire_Set64(bad.remainder[XVT_INTEGRATE_SPIN_DECAY], (uint64_t)-other_limit);
	ExpectRefused(&bad);

	/* One short of each bound is accepted, as are 0xFFFF slots and the empty record. */
	bad = good;
	XvtWire_Set64(bad.position[0], (uint64_t)(position_limit - 1));
	XvtWire_Set64(bad.position[1], (uint64_t)(1 - position_limit));
	XvtWire_Set64(bad.remainder[XVT_INTEGRATE_ROLL], (uint64_t)(steering_limit - 1));
	XvtWire_Set64(bad.remainder[XVT_INTEGRATE_TURN], (uint64_t)(1 - steering_limit));
	XvtWire_Set64(bad.remainder[XVT_INTEGRATE_PUSH_X], (uint64_t)(other_limit - 1));
	XvtWire_Set64(bad.remainder[XVT_INTEGRATE_HOME_YAW], (uint64_t)(1 - other_limit));
	XvtWire_Set16(bad.target_slot, 0xFFFF);
	XvtWire_Set16(bad.carried_slot, kSlots - 1);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Decode(&bad, 0), 1);
	memset(&bad, 0, sizeof bad);
	XvtWire_Set16(bad.slot, 2);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Decode(&bad, 0), 1);
}

static int HasReferenceMotion(unsigned slot) {
	XvtReferenceMotionWire record;
	XvtReferenceMotion_Encode(slot, &record);
	return record.type != 0;
}

static void CheckReset(void) {
	FreshWorld();
	Seed(1, XVT_INTEGRATE_PITCH);
	Seed(2, XVT_INTEGRATE_PITCH);
	XVT_ASSERT_INT_EQ(HasReferenceMotion(1), 1);
	XvtFlightIntegration_Reset(1);
	XVT_ASSERT_INT_EQ(Probe(1, XVT_INTEGRATE_PITCH), 0);
	XVT_ASSERT_INT_EQ(HasReferenceMotion(1), 0);

	/* A slot out of range is ignored. */
	XvtFlightIntegration_Reset(kSlots + 3);
	XVT_ASSERT_INT_EQ(Probe(2, XVT_INTEGRATE_PITCH), 1);
	XVT_ASSERT_INT_EQ(HasReferenceMotion(2), 1);
}

static void CheckResetShared(void) {
	FreshWorld();
	for (unsigned slot = 0; slot < kSlots; ++slot)
		Seed(slot, XVT_INTEGRATE_PITCH);
	g_localTransientSlotStart = 2;
	g_localDebrisSlotEnd = 4;
	XvtFlightIntegration_ResetShared();
	XVT_ASSERT_INT_EQ(Probe(0, XVT_INTEGRATE_PITCH), 0);
	XVT_ASSERT_INT_EQ(Probe(1, XVT_INTEGRATE_PITCH), 0);
	XVT_ASSERT_INT_EQ(Probe(2, XVT_INTEGRATE_PITCH), 1);
	XVT_ASSERT_INT_EQ(Probe(3, XVT_INTEGRATE_PITCH), 1);
	XVT_ASSERT_INT_EQ(Probe(5, XVT_INTEGRATE_PITCH), 0);
	/* Reference motion is left alone. */
	XVT_ASSERT_INT_EQ(HasReferenceMotion(0), 1);
	XVT_ASSERT_INT_EQ(HasReferenceMotion(5), 1);

	/* Before Init it does nothing. */
	XvtFlightIntegration_Shutdown();
	XvtFlightIntegration_ResetShared();
}

int main(void) {
	CheckSeedAndProbe();
	CheckNoTable();
	CheckRateCarry();
	CheckRateSignChange();
	CheckRateLimits();
	CheckClear();
	CheckEntryFollowsObject();
	CheckSpinChannels();
	CheckHomingChannels();
	CheckCarriedObjectReset();
	CheckSteer();
	CheckMove();
	CheckPush();
	CheckEncodeDecode();
	CheckEncodeEmpty();
	CheckDecodeRefusals();
	CheckReset();
	CheckResetShared();
	XvtFlightIntegration_Shutdown();
	XvtReferenceMotion_Shutdown();
	return 0;
}
