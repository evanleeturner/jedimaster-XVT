/* Checks the reference-boundary positions (xvt_runtime/timing/reference_motion.h) against the promises in
 * its header, on an object table this file builds itself: slots 0, 2, 3 and 4 hold live objects and slot
 * 1 is empty; no game data is read. Each case starts from that table at game time 100, a fresh table of
 * samples taken by Init, and a new flight timing session. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/reference_motion.h"

/* An allocation that cannot be met must come back as NULL, so Init's refusal can be seen; by default
 * AddressSanitizer stops the program instead. */
const char *__asan_default_options(void)
{
	return "allocator_may_return_null=1";
}

enum { SLOTS = 5 };

static struct object_record g_test_objects[SLOTS];

static void place(unsigned slot, int x, int y, int z)
{
	g_test_objects[slot].world_x = x;
	g_test_objects[slot].world_y = y;
	g_test_objects[slot].world_z = z;
}

static void move(unsigned slot, int dx, int dy, int dz)
{
	g_test_objects[slot].world_x += dx;
	g_test_objects[slot].world_y += dy;
	g_test_objects[slot].world_z += dz;
}

static void fresh_world(xvt_flight_timing_profile profile)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	g_test_objects[0].object_type = 1;
	g_test_objects[0].object_signature = 0x100;
	place(0, 1000, 2000, 3000);
	g_test_objects[2].object_type = 2;
	g_test_objects[2].object_signature = 0x102;
	place(2, -500, 0, 40);
	g_test_objects[3].object_type = 1;
	g_test_objects[3].object_signature = 0x103;
	g_test_objects[4].object_type = 1;
	g_test_objects[4].object_signature = 0x104;
	g_object_table = g_test_objects;
	g_game_time = 100;
	g_local_transient_slot_start = 0;
	g_local_debris_slot_end = 0;
	xvt_flight_timing_begin_session(profile);
	XVT_ASSERT_INT_EQ(xvt_reference_motion_init(SLOTS), 1);
}

static void expect_displacement(unsigned slot, int32_t x, int32_t y, int32_t z)
{
	int32_t delta[3] = {7, 7, 7};
	xvt_reference_motion_displacement(slot, delta);
	XVT_ASSERT_INT_EQ(delta[0], x);
	XVT_ASSERT_INT_EQ(delta[1], y);
	XVT_ASSERT_INT_EQ(delta[2], z);
}

static void check_init_samples(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	/* Over exactly one reference period, the displacement is the movement itself. */
	move(0, 8, -16, 24);
	g_game_time = 100 + XVT_REFERENCE_TICKS;
	expect_displacement(0, 8, -16, 24);
	expect_displacement(2, 0, 0, 0);
	/* The empty slot holds no sample. */
	expect_displacement(1, 0, 0, 0);
}

static void check_displacement_scale(void)
{
	/* The change times 8 over the ticks between: twice the period halves it, half the period doubles it. */
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	move(0, 40, 80, -120);
	g_game_time = 116;
	expect_displacement(0, 20, 40, -60);
	g_game_time = 104;
	expect_displacement(0, 80, 160, -240);

	/* No time passed, or time ran backwards: all zero. */
	g_game_time = 100;
	expect_displacement(0, 0, 0, 0);
	g_game_time = 90;
	expect_displacement(0, 0, 0, 0);

	/* A slot out of range: all zero. */
	g_game_time = 108;
	expect_displacement(SLOTS, 0, 0, 0);
	expect_displacement(1000, 0, 0, 0);
}

static void check_clamp(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	place(0, -2000000000, 2000000000, 0);
	XVT_ASSERT_INT_EQ(xvt_reference_motion_init(SLOTS), 1);
	place(0, 2000000000, -2000000000, 0);
	g_game_time = 101;
	expect_displacement(0, INT32_MAX, INT32_MIN, 0);
}

static void check_axis(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	move(0, 16, -32, 48);
	g_game_time = 116;
	int32_t delta[3];
	xvt_reference_motion_displacement(0, delta);
	for (unsigned axis = 0; axis < 3; ++axis) {
		XVT_ASSERT_INT_EQ(
			xvt_reference_motion_axis_displacement(0, axis),
			delta[axis]);
	}
	XVT_ASSERT_INT_EQ(xvt_reference_motion_axis_displacement(0, 3), 0);
	XVT_ASSERT_INT_EQ(xvt_reference_motion_axis_displacement(0, 1000), 0);
}

static void check_committed(void)
{
	/* Unlocked: the committed time stands for the current time. */
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	move(0, 8, 0, 0);
	xvt_reference_motion_committed(0, 104);
	g_game_time = 500;
	expect_displacement(0, 16, 0, 0);

	/* Locked: Committed does nothing, and game time is the current time. */
	fresh_world(XVT_FLIGHT_TIMING_NATIVE);
	move(0, 8, 0, 0);
	xvt_reference_motion_committed(0, 104);
	g_game_time = 108;
	expect_displacement(0, 8, 0, 0);
}

static void check_commit_boundary(void)
{
	/* In a reference step, the boundary takes a new sample at game time: later movement is measured from
	 * it, so 8 more over 8 more ticks is a displacement of 8, not (16 + 8) * 8 / 16. */
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_flight_timing_begin_advance(XVT_REFERENCE_TICKS);
	move(0, 16, 0, 0);
	g_game_time = 108;
	xvt_reference_motion_commit_boundary();
	xvt_flight_timing_end_advance();
	expect_displacement(0, 0, 0, 0);
	move(0, 8, 0, 0);
	g_game_time = 116;
	expect_displacement(0, 8, 0, 0);

	/* A committed time dates the sample instead of game time. */
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_reference_motion_committed(0, 150);
	xvt_flight_timing_begin_advance(XVT_REFERENCE_TICKS);
	xvt_reference_motion_commit_boundary();
	xvt_flight_timing_end_advance();
	move(0, 8, 0, 0);
	xvt_reference_motion_committed(0, 158);
	g_game_time = 1000;
	expect_displacement(0, 8, 0, 0);

	/* Outside a reference step it does nothing: the movement is still measured from Init's sample. */
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_flight_timing_begin_advance(1);
	move(0, 16, 0, 0);
	g_game_time = 108;
	xvt_reference_motion_commit_boundary();
	xvt_flight_timing_end_advance();
	expect_displacement(0, 16, 0, 0);

	/* In a locked flight it does nothing either. */
	fresh_world(XVT_FLIGHT_TIMING_NATIVE);
	xvt_flight_timing_begin_advance(XVT_REFERENCE_TICKS);
	move(0, 16, 0, 0);
	g_game_time = 108;
	xvt_reference_motion_commit_boundary();
	expect_displacement(0, 16, 0, 0);
}

static void check_object_change_clears(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	move(0, 8, 0, 0);
	move(2, 8, 0, 0);
	g_game_time = 108;
	expect_displacement(0, 8, 0, 0);

	/* Another signature: the sample is gone, and stays gone when the old signature returns. */
	g_test_objects[0].object_signature = 0x999;
	expect_displacement(0, 0, 0, 0);
	g_test_objects[0].object_signature = 0x100;
	expect_displacement(0, 0, 0, 0);

	/* Another type. */
	g_test_objects[2].object_type = 3;
	expect_displacement(2, 0, 0, 0);
}

static int records_equal(const struct xvt_reference_motion_wire *a,
			 const struct xvt_reference_motion_wire *b)
{
	return memcmp(a, b, sizeof *a) == 0;
}

static struct xvt_reference_motion_wire empty_record(unsigned slot)
{
	struct xvt_reference_motion_wire record;
	memset(&record, 0, sizeof record);
	xvt_wire_set16(record.slot, (uint16_t)slot);
	return record;
}

static void check_encode_decode(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_reference_motion_committed(0, 104);
	move(0, 8, 8, 8);
	struct xvt_reference_motion_wire record, out;
	xvt_reference_motion_encode(0, &record);
	XVT_ASSERT_INT_EQ(record.type, 1);
	XVT_ASSERT_INT_EQ(xvt_wire_get16(record.slot), 0);

	/* Installed into a fresh table sampled later, the record brings the old sample and committed time
	 * back: the same displacement, and the same record out. */
	g_game_time = 300;
	XVT_ASSERT_INT_EQ(xvt_reference_motion_init(SLOTS), 1);
	expect_displacement(0, 0, 0, 0);
	XVT_ASSERT_INT_EQ(xvt_reference_motion_decode(&record, 1), 1);
	expect_displacement(0, 16, 16, 16);
	xvt_reference_motion_encode(0, &out);
	XVT_ASSERT_TRUE(records_equal(&out, &record));

	/* Without apply it is judged, not installed. */
	XVT_ASSERT_INT_EQ(xvt_reference_motion_init(SLOTS), 1);
	XVT_ASSERT_INT_EQ(xvt_reference_motion_decode(&record, 0), 1);
	expect_displacement(0, 0, 0, 0);

	/* Not checked against the object: accepted, encoded as another object's entry, and cleared by the
	 * next read. */
	struct xvt_reference_motion_wire other = record;
	xvt_wire_set16(other.signature, 0x777);
	XVT_ASSERT_INT_EQ(xvt_reference_motion_decode(&other, 1), 1);
	xvt_reference_motion_encode(0, &out);
	struct xvt_reference_motion_wire empty = empty_record(0);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));
	expect_displacement(0, 0, 0, 0);
}

static void check_encode_empty(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	struct xvt_reference_motion_wire out, empty;

	xvt_reference_motion_encode(1, &out);
	empty = empty_record(1);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));

	xvt_reference_motion_encode(SLOTS + 2, &out);
	empty = empty_record(SLOTS + 2);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));

	g_test_objects[3].object_signature = 0x555;
	xvt_reference_motion_encode(3, &out);
	empty = empty_record(3);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));
	g_test_objects[2].object_type = 9;
	xvt_reference_motion_encode(2, &out);
	empty = empty_record(2);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));
}

static void expect_refused(const struct xvt_reference_motion_wire *record)
{
	struct xvt_reference_motion_wire before, after;
	xvt_reference_motion_encode(0, &before);
	XVT_ASSERT_INT_EQ(xvt_reference_motion_decode(record, 1), 0);
	xvt_reference_motion_encode(0, &after);
	XVT_ASSERT_TRUE(records_equal(&before, &after));
}

static void check_decode_refusals(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	struct xvt_reference_motion_wire good, bad;
	xvt_reference_motion_encode(0, &good);
	xvt_wire_set32(good.sample_tick, 50);
	good.flags = XVT_MOTION_VALID | XVT_MOTION_CURRENT_VALID;

	bad = good;
	xvt_wire_set16(bad.slot, SLOTS);
	expect_refused(&bad);
	bad = good;
	bad.flags |= 4;
	expect_refused(&bad);
	bad = good;
	bad.flags = 0x80;
	expect_refused(&bad);
	bad = good;
	xvt_wire_set16(bad.reserved, 1);
	expect_refused(&bad);

	/* Type 0 must be exactly the empty record. */
	bad = empty_record(0);
	xvt_wire_set32(bad.current_tick, 1);
	expect_refused(&bad);
	bad = empty_record(0);
	bad.flags = XVT_MOTION_VALID;
	expect_refused(&bad);

	bad = empty_record(0);
	XVT_ASSERT_INT_EQ(xvt_reference_motion_decode(&bad, 0), 1);
	XVT_ASSERT_INT_EQ(xvt_reference_motion_decode(&good, 0), 1);
}

static void check_reset(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	move(0, 8, 0, 0);
	move(2, 8, 0, 0);
	g_game_time = 108;
	xvt_reference_motion_reset(0);
	expect_displacement(0, 0, 0, 0);
	xvt_reference_motion_reset(SLOTS + 5);
	expect_displacement(2, 8, 0, 0);
}

static void check_reset_shared(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	move(0, 8, 0, 0);
	move(2, 8, 0, 0);
	move(4, 8, 0, 0);
	g_game_time = 108;
	/* Local slots 1 and 2; slot 0 lies below them and slot 4 past them. */
	g_local_transient_slot_start = 1;
	g_local_debris_slot_end = 3;
	xvt_reference_motion_reset_shared();
	expect_displacement(0, 0, 0, 0);
	expect_displacement(2, 8, 0, 0);
	expect_displacement(4, 0, 0, 0);

	/* Before Init it does nothing. */
	xvt_reference_motion_shutdown();
	xvt_reference_motion_reset_shared();
}

static void check_no_table(void)
{
	fresh_world(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	move(0, 8, 0, 0);
	g_game_time = 108;
	/* A failed allocation leaves no table: nothing is in range. */
	XVT_ASSERT_INT_EQ(xvt_reference_motion_init(SIZE_MAX / 2), 0);
	expect_displacement(0, 0, 0, 0);
	struct xvt_reference_motion_wire out, empty = empty_record(0);
	xvt_reference_motion_encode(0, &out);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));
	XVT_ASSERT_INT_EQ(xvt_reference_motion_decode(&empty, 0), 0);
}

int main(void)
{
	check_init_samples();
	check_displacement_scale();
	check_clamp();
	check_axis();
	check_committed();
	check_commit_boundary();
	check_object_change_clears();
	check_encode_decode();
	check_encode_empty();
	check_decode_refusals();
	check_reset();
	check_reset_shared();
	check_no_table();
	xvt_reference_motion_shutdown();
	xvt_flight_timing_end_session();
	return 0;
}
