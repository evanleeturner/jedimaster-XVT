/* Checks the reference-boundary positions (xvt_runtime/timing/reference_motion.h) against the promises in
 * its header, on an object table this file builds itself: slots 0, 2, 3 and 4 hold live objects and slot
 * 1 is empty; no game data is read. Each case starts from that table at game time 100, a fresh table of
 * samples taken by Init, and a new flight timing session. */
#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/reference_motion.h"

#include <stdint.h>
#include <string.h>

/* An allocation that cannot be met must come back as NULL, so Init's refusal can be seen; by default
 * AddressSanitizer stops the program instead. */
const char* __asan_default_options(void) { return "allocator_may_return_null=1"; }

enum { kSlots = 5 };

static ObjectRecord g_testObjects[kSlots];

static void Place(unsigned slot, int x, int y, int z) {
	g_testObjects[slot].world_x = x;
	g_testObjects[slot].world_y = y;
	g_testObjects[slot].world_z = z;
}

static void Move(unsigned slot, int dx, int dy, int dz) {
	g_testObjects[slot].world_x += dx;
	g_testObjects[slot].world_y += dy;
	g_testObjects[slot].world_z += dz;
}

static void FreshWorld(XvtFlightTimingProfile profile) {
	memset(g_testObjects, 0, sizeof g_testObjects);
	g_testObjects[0].objectType = 1;
	g_testObjects[0].objectSignature = 0x100;
	Place(0, 1000, 2000, 3000);
	g_testObjects[2].objectType = 2;
	g_testObjects[2].objectSignature = 0x102;
	Place(2, -500, 0, 40);
	g_testObjects[3].objectType = 1;
	g_testObjects[3].objectSignature = 0x103;
	g_testObjects[4].objectType = 1;
	g_testObjects[4].objectSignature = 0x104;
	g_objectTable = g_testObjects;
	g_gameTime = 100;
	g_localTransientSlotStart = 0;
	g_localDebrisSlotEnd = 0;
	XvtFlightTiming_BeginSession(profile);
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Init(kSlots), 1);
}

static void ExpectDisplacement(unsigned slot, int32_t x, int32_t y, int32_t z) {
	int32_t delta[3] = { 7, 7, 7 };
	XvtReferenceMotion_Displacement(slot, delta);
	XVT_ASSERT_INT_EQ(delta[0], x);
	XVT_ASSERT_INT_EQ(delta[1], y);
	XVT_ASSERT_INT_EQ(delta[2], z);
}

static void CheckInitSamples(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	/* Over exactly one reference period, the displacement is the movement itself. */
	Move(0, 8, -16, 24);
	g_gameTime = 100 + XVT_REFERENCE_TICKS;
	ExpectDisplacement(0, 8, -16, 24);
	ExpectDisplacement(2, 0, 0, 0);
	/* The empty slot holds no sample. */
	ExpectDisplacement(1, 0, 0, 0);
}

static void CheckDisplacementScale(void) {
	/* The change times 8 over the ticks between: twice the period halves it, half the period doubles it. */
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	Move(0, 40, 80, -120);
	g_gameTime = 116;
	ExpectDisplacement(0, 20, 40, -60);
	g_gameTime = 104;
	ExpectDisplacement(0, 80, 160, -240);

	/* No time passed, or time ran backwards: all zero. */
	g_gameTime = 100;
	ExpectDisplacement(0, 0, 0, 0);
	g_gameTime = 90;
	ExpectDisplacement(0, 0, 0, 0);

	/* A slot out of range: all zero. */
	g_gameTime = 108;
	ExpectDisplacement(kSlots, 0, 0, 0);
	ExpectDisplacement(1000, 0, 0, 0);
}

static void CheckClamp(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	Place(0, -2000000000, 2000000000, 0);
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Init(kSlots), 1);
	Place(0, 2000000000, -2000000000, 0);
	g_gameTime = 101;
	ExpectDisplacement(0, INT32_MAX, INT32_MIN, 0);
}

static void CheckAxis(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	Move(0, 16, -32, 48);
	g_gameTime = 116;
	int32_t delta[3];
	XvtReferenceMotion_Displacement(0, delta);
	for (unsigned axis = 0; axis < 3; ++axis)
		XVT_ASSERT_INT_EQ(XvtReferenceMotion_Axis(0, axis), delta[axis]);
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Axis(0, 3), 0);
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Axis(0, 1000), 0);
}

static void CheckCommitted(void) {
	/* Unlocked: the committed time stands for the current time. */
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	Move(0, 8, 0, 0);
	XvtReferenceMotion_Committed(0, 104);
	g_gameTime = 500;
	ExpectDisplacement(0, 16, 0, 0);

	/* Locked: Committed does nothing, and game time is the current time. */
	FreshWorld(XVT_FLIGHT_TIMING_NATIVE);
	Move(0, 8, 0, 0);
	XvtReferenceMotion_Committed(0, 104);
	g_gameTime = 108;
	ExpectDisplacement(0, 8, 0, 0);
}

static void CheckCommitBoundary(void) {
	/* In a reference step, the boundary takes a new sample at game time: later movement is measured from
	 * it, so 8 more over 8 more ticks is a displacement of 8, not (16 + 8) * 8 / 16. */
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtFlightTiming_BeginAdvance(XVT_REFERENCE_TICKS);
	Move(0, 16, 0, 0);
	g_gameTime = 108;
	XvtReferenceMotion_CommitBoundary();
	XvtFlightTiming_EndAdvance();
	ExpectDisplacement(0, 0, 0, 0);
	Move(0, 8, 0, 0);
	g_gameTime = 116;
	ExpectDisplacement(0, 8, 0, 0);

	/* A committed time dates the sample instead of game time. */
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtReferenceMotion_Committed(0, 150);
	XvtFlightTiming_BeginAdvance(XVT_REFERENCE_TICKS);
	XvtReferenceMotion_CommitBoundary();
	XvtFlightTiming_EndAdvance();
	Move(0, 8, 0, 0);
	XvtReferenceMotion_Committed(0, 158);
	g_gameTime = 1000;
	ExpectDisplacement(0, 8, 0, 0);

	/* Outside a reference step it does nothing: the movement is still measured from Init's sample. */
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtFlightTiming_BeginAdvance(1);
	Move(0, 16, 0, 0);
	g_gameTime = 108;
	XvtReferenceMotion_CommitBoundary();
	XvtFlightTiming_EndAdvance();
	ExpectDisplacement(0, 16, 0, 0);

	/* In a locked flight it does nothing either. */
	FreshWorld(XVT_FLIGHT_TIMING_NATIVE);
	XvtFlightTiming_BeginAdvance(XVT_REFERENCE_TICKS);
	Move(0, 16, 0, 0);
	g_gameTime = 108;
	XvtReferenceMotion_CommitBoundary();
	ExpectDisplacement(0, 16, 0, 0);
}

static void CheckObjectChangeClears(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	Move(0, 8, 0, 0);
	Move(2, 8, 0, 0);
	g_gameTime = 108;
	ExpectDisplacement(0, 8, 0, 0);

	/* Another signature: the sample is gone, and stays gone when the old signature returns. */
	g_testObjects[0].objectSignature = 0x999;
	ExpectDisplacement(0, 0, 0, 0);
	g_testObjects[0].objectSignature = 0x100;
	ExpectDisplacement(0, 0, 0, 0);

	/* Another type. */
	g_testObjects[2].objectType = 3;
	ExpectDisplacement(2, 0, 0, 0);
}

static int RecordsEqual(const XvtReferenceMotionWire* a, const XvtReferenceMotionWire* b) {
	return memcmp(a, b, sizeof *a) == 0;
}

static XvtReferenceMotionWire EmptyRecord(unsigned slot) {
	XvtReferenceMotionWire record;
	memset(&record, 0, sizeof record);
	XvtWire_Set16(record.slot, (uint16_t)slot);
	return record;
}

static void CheckEncodeDecode(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtReferenceMotion_Committed(0, 104);
	Move(0, 8, 8, 8);
	XvtReferenceMotionWire record, out;
	XvtReferenceMotion_Encode(0, &record);
	XVT_ASSERT_INT_EQ(record.type, 1);
	XVT_ASSERT_INT_EQ(XvtWire_Get16(record.slot), 0);

	/* Installed into a fresh table sampled later, the record brings the old sample and committed time
	 * back: the same displacement, and the same record out. */
	g_gameTime = 300;
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Init(kSlots), 1);
	ExpectDisplacement(0, 0, 0, 0);
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Decode(&record, 1), 1);
	ExpectDisplacement(0, 16, 16, 16);
	XvtReferenceMotion_Encode(0, &out);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &record));

	/* Without apply it is judged, not installed. */
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Init(kSlots), 1);
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Decode(&record, 0), 1);
	ExpectDisplacement(0, 0, 0, 0);

	/* Not checked against the object: accepted, encoded as another object's entry, and cleared by the
	 * next read. */
	XvtReferenceMotionWire other = record;
	XvtWire_Set16(other.signature, 0x777);
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Decode(&other, 1), 1);
	XvtReferenceMotion_Encode(0, &out);
	XvtReferenceMotionWire empty = EmptyRecord(0);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));
	ExpectDisplacement(0, 0, 0, 0);
}

static void CheckEncodeEmpty(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtReferenceMotionWire out, empty;

	XvtReferenceMotion_Encode(1, &out);
	empty = EmptyRecord(1);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));

	XvtReferenceMotion_Encode(kSlots + 2, &out);
	empty = EmptyRecord(kSlots + 2);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));

	g_testObjects[3].objectSignature = 0x555;
	XvtReferenceMotion_Encode(3, &out);
	empty = EmptyRecord(3);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));
	g_testObjects[2].objectType = 9;
	XvtReferenceMotion_Encode(2, &out);
	empty = EmptyRecord(2);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));
}

static void ExpectRefused(const XvtReferenceMotionWire* record) {
	XvtReferenceMotionWire before, after;
	XvtReferenceMotion_Encode(0, &before);
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Decode(record, 1), 0);
	XvtReferenceMotion_Encode(0, &after);
	XVT_ASSERT_TRUE(RecordsEqual(&before, &after));
}

static void CheckDecodeRefusals(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtReferenceMotionWire good, bad;
	XvtReferenceMotion_Encode(0, &good);
	XvtWire_Set32(good.sample_tick, 50);
	good.flags = XVT_MOTION_VALID | XVT_MOTION_CURRENT_VALID;

	bad = good;
	XvtWire_Set16(bad.slot, kSlots);
	ExpectRefused(&bad);
	bad = good;
	bad.flags |= 4;
	ExpectRefused(&bad);
	bad = good;
	bad.flags = 0x80;
	ExpectRefused(&bad);
	bad = good;
	XvtWire_Set16(bad.reserved, 1);
	ExpectRefused(&bad);

	/* Type 0 must be exactly the empty record. */
	bad = EmptyRecord(0);
	XvtWire_Set32(bad.current_tick, 1);
	ExpectRefused(&bad);
	bad = EmptyRecord(0);
	bad.flags = XVT_MOTION_VALID;
	ExpectRefused(&bad);

	bad = EmptyRecord(0);
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Decode(&bad, 0), 1);
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Decode(&good, 0), 1);
}

static void CheckReset(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	Move(0, 8, 0, 0);
	Move(2, 8, 0, 0);
	g_gameTime = 108;
	XvtReferenceMotion_Reset(0);
	ExpectDisplacement(0, 0, 0, 0);
	XvtReferenceMotion_Reset(kSlots + 5);
	ExpectDisplacement(2, 8, 0, 0);
}

static void CheckResetShared(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	Move(0, 8, 0, 0);
	Move(2, 8, 0, 0);
	Move(4, 8, 0, 0);
	g_gameTime = 108;
	/* Local slots 1 and 2; slot 0 lies below them and slot 4 past them. */
	g_localTransientSlotStart = 1;
	g_localDebrisSlotEnd = 3;
	XvtReferenceMotion_ResetShared();
	ExpectDisplacement(0, 0, 0, 0);
	ExpectDisplacement(2, 8, 0, 0);
	ExpectDisplacement(4, 0, 0, 0);

	/* Before Init it does nothing. */
	XvtReferenceMotion_Shutdown();
	XvtReferenceMotion_ResetShared();
}

static void CheckNoTable(void) {
	FreshWorld(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	Move(0, 8, 0, 0);
	g_gameTime = 108;
	/* A failed allocation leaves no table: nothing is in range. */
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Init(SIZE_MAX / 2), 0);
	ExpectDisplacement(0, 0, 0, 0);
	XvtReferenceMotionWire out, empty = EmptyRecord(0);
	XvtReferenceMotion_Encode(0, &out);
	XVT_ASSERT_TRUE(RecordsEqual(&out, &empty));
	XVT_ASSERT_INT_EQ(XvtReferenceMotion_Decode(&empty, 0), 0);
}

int main(void) {
	CheckInitSamples();
	CheckDisplacementScale();
	CheckClamp();
	CheckAxis();
	CheckCommitted();
	CheckCommitBoundary();
	CheckObjectChangeClears();
	CheckEncodeDecode();
	CheckEncodeEmpty();
	CheckDecodeRefusals();
	CheckReset();
	CheckResetShared();
	CheckNoTable();
	XvtReferenceMotion_Shutdown();
	XvtFlightTiming_EndSession();
	return 0;
}
