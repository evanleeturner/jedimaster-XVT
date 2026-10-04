/* Checks the per-object step remainders (xvt_runtime/timing/flight_integration.h) against the promises in
 * its header, on an object table this file builds itself: six live objects, each with a mobile record,
 * and no game data. Each case starts from that table and a fresh remainder table.
 *
 * Most checks watch one channel's carried remainder through Rate itself: Seed leaves a carry of 3/4 on a
 * channel, and Probe adds 1/4 more, so Probe returns 1 exactly when the carry survived. */
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/math/trig2.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/reference_motion.h"

/* An allocation that cannot be met must come back as NULL, so Init's refusal can be seen; by default
 * AddressSanitizer stops the program instead. */
const char *__asan_default_options(void)
{
	return "allocator_may_return_null=1";
}

enum { SLOTS = 6 };

static struct object_record g_test_objects[SLOTS];
static struct mobile_object g_test_mobiles[SLOTS];
static struct craft_data g_test_craft;
static struct warhead_guidance_state g_test_guidance;

/* Six live objects (type 1, signature 0x100 + slot) at the world origin, each with a still mobile
 * record; one simulation tick per step; no local slots; empty remainder and reference motion tables. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(&g_test_craft, 0, sizeof g_test_craft);
	memset(&g_test_guidance, 0, sizeof g_test_guidance);
	for (int i = 0; i < SLOTS; ++i) {
		g_test_objects[i].object_type = 1;
		g_test_objects[i].object_signature = (uint16_t)(0x100 + i);
		g_test_objects[i].mobj = &g_test_mobiles[i];
	}
	g_object_table = g_test_objects;
	g_elapsed_ticks = 1;
	g_local_transient_slot_start = 0;
	g_local_debris_slot_end = 0;
	XVT_ASSERT_INT_EQ(xvt_flight_integration_init(SLOTS), 1);
	XVT_ASSERT_INT_EQ(xvt_reference_motion_init(SLOTS), 1);
}

static void seed(unsigned slot, unsigned channel)
{
	XVT_ASSERT_INT_EQ(xvt_flight_integration_rate(slot, channel, 3, 1, 4),
			  0);
}

static int probe(unsigned slot, unsigned channel)
{
	return xvt_flight_integration_rate(slot, channel, 1, 1, 4);
}

/* Reads slot's entry without leaving a carry: a zero rate moves nothing. */
static void touch(unsigned slot)
{
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(slot, XVT_INTEGRATE_TURN, 0, 1, 4),
		0);
}

static void check_seed_and_probe(void)
{
	fresh_world();
	seed(0, XVT_INTEGRATE_PITCH);
	/* Channels and slots carry separately. */
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_TURN), 0);
	XVT_ASSERT_INT_EQ(probe(1, XVT_INTEGRATE_PITCH), 0);
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_PITCH), 1);
}

static void check_no_table(void)
{
	fresh_world();
	xvt_flight_integration_shutdown();
	/* Without the table every call is the plain truncated result, with nothing carried. */
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(0, XVT_INTEGRATE_PITCH, 3, 1, 4),
		0);
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(0, XVT_INTEGRATE_PITCH, 3, 1, 4),
		0);
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(0, XVT_INTEGRATE_PITCH, 7, 3, 4),
		5);
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(0, XVT_INTEGRATE_PITCH, -7, 3, 4),
		-5);
	for (int i = 0; i < 300; ++i) {
		XVT_ASSERT_INT_EQ(
			xvt_flight_integration_steer(0, XVT_INTEGRATE_ROLL, 1,
						     0xFFFF, 0xFFFF, 1),
			0);
	}

	/* An allocation that fails leaves no table either. */
	fresh_world();
	seed(0, XVT_INTEGRATE_PITCH);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_init(SIZE_MAX / 2), 0);
	seed(0, XVT_INTEGRATE_PITCH);
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_PITCH), 0);

	/* Init again starts from empty entries. */
	fresh_world();
	seed(0, XVT_INTEGRATE_PITCH);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_init(SLOTS), 1);
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_PITCH), 0);
}

static void check_rate_carry(void)
{
	fresh_world();
	/* Truncated toward zero. */
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(0, XVT_INTEGRATE_TURN, -7, 1, 4),
		-1);
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(1, XVT_INTEGRATE_TURN, 7, 1, 4), 1);

	/* Ten short steps add up to what one step of ten ticks gives: nothing is lost to truncation. */
	fresh_world();
	int sum = 0;
	int negative = 0;
	for (int i = 0; i < 10; ++i) {
		sum += xvt_flight_integration_rate(2, XVT_INTEGRATE_BANK, 7, 1,
						   4);
		negative += xvt_flight_integration_rate(3, XVT_INTEGRATE_BANK,
							-7, 1, 4);
	}
	XVT_ASSERT_INT_EQ(sum, xvt_flight_integration_rate(
				       4, XVT_INTEGRATE_BANK, 7, 10, 4));
	XVT_ASSERT_INT_EQ(sum, 70 / 4);
	XVT_ASSERT_INT_EQ(negative, -70 / 4);
}

static void check_rate_sign_change(void)
{
	fresh_world();
	/* A carry of 3/4 is dropped when the rate turns negative: -1/4 then -3/4 make exactly -1. */
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(0, XVT_INTEGRATE_ROLL, 3, 1, 4), 0);
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(0, XVT_INTEGRATE_ROLL, -1, 1, 4),
		0);
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(0, XVT_INTEGRATE_ROLL, -3, 1, 4),
		-1);

	/* Zero counts as a sign of its own: a zero rate drops the carry too. */
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(1, XVT_INTEGRATE_ROLL, 3, 1, 4), 0);
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(1, XVT_INTEGRATE_ROLL, 0, 1, 4), 0);
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(1, XVT_INTEGRATE_ROLL, 1, 1, 4), 0);
}

static void check_rate_limits(void)
{
	fresh_world();
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(0, XVT_INTEGRATE_COUNT, 1000, 1, 1),
		0);
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(0, XVT_INTEGRATE_PITCH, 1000, 1, 0),
		0);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_rate(0, XVT_INTEGRATE_PITCH,
						      1000, 1, -4),
			  0);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_rate(0, XVT_INTEGRATE_PITCH,
						      INT_MAX, 4, 1),
			  INT_MAX);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_rate(0, XVT_INTEGRATE_PITCH,
						      INT_MIN, 4, 1),
			  INT_MIN);
}

static void check_clear(void)
{
	fresh_world();
	seed(0, XVT_INTEGRATE_PITCH);
	seed(0, XVT_INTEGRATE_TURN);
	xvt_flight_integration_clear(0, XVT_INTEGRATE_PITCH);
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_PITCH), 0);
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_TURN), 1);
}

static void check_entry_follows_object(void)
{
	fresh_world();
	seed(0, XVT_INTEGRATE_PITCH);
	g_test_objects[0].object_signature = 0x999;
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_PITCH), 0);

	seed(1, XVT_INTEGRATE_PITCH);
	g_test_objects[1].object_type = 2;
	XVT_ASSERT_INT_EQ(probe(1, XVT_INTEGRATE_PITCH), 0);

	seed(2, XVT_INTEGRATE_PITCH);
	g_test_mobiles[2].family = 3;
	XVT_ASSERT_INT_EQ(probe(2, XVT_INTEGRATE_PITCH), 0);

	/* The same object keeps its carry. */
	seed(3, XVT_INTEGRATE_PITCH);
	XVT_ASSERT_INT_EQ(probe(3, XVT_INTEGRATE_PITCH), 1);
}

static void check_spin_channels(void)
{
	fresh_world();
	/* While the roll impulse rate is 0, the spin channels carry nothing; other channels still do. */
	g_test_mobiles[0].roll_impulse_rate = 0;
	seed(0, XVT_INTEGRATE_SPIN_DECAY);
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_SPIN_DECAY), 0);
	seed(0, XVT_INTEGRATE_SPIN_ANGLE);
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_SPIN_ANGLE), 0);
	seed(0, XVT_INTEGRATE_PUSH_X);
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_PUSH_X), 1);

	g_test_mobiles[1].roll_impulse_rate = 5;
	seed(1, XVT_INTEGRATE_SPIN_DECAY);
	XVT_ASSERT_INT_EQ(probe(1, XVT_INTEGRATE_SPIN_DECAY), 1);
	seed(1, XVT_INTEGRATE_SPIN_ANGLE);
	XVT_ASSERT_INT_EQ(probe(1, XVT_INTEGRATE_SPIN_ANGLE), 1);
}

static void check_homing_channels(void)
{
	fresh_world();
	g_test_mobiles[0].p_warhead_guidance = &g_test_guidance;
	g_test_guidance.homing_tier = 1;
	g_test_guidance.target_obj_idx = 3;
	g_test_guidance.target_signature = 0x103;

	/* A steady target keeps the homing carry. */
	seed(0, XVT_INTEGRATE_HOME_YAW);
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_HOME_YAW), 1);

	/* A new target index or signature drops it, on the homing channels only. */
	seed(0, XVT_INTEGRATE_HOME_PITCH);
	seed(0, XVT_INTEGRATE_TURN);
	g_test_guidance.target_obj_idx = 4;
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_HOME_PITCH), 0);
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_TURN), 1);
	seed(0, XVT_INTEGRATE_HOME_SPEED);
	g_test_guidance.target_signature = 0x104;
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_HOME_SPEED), 0);

	/* Homing stopped drops it. */
	seed(0, XVT_INTEGRATE_HOME_YAW);
	g_test_guidance.homing_tier = 0;
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_HOME_YAW), 0);
}

static void check_carried_object_reset(void)
{
	fresh_world();
	g_test_mobiles[0].p_craft = &g_test_craft;
	g_test_craft.carried_object_index = 1;
	/* The craft's first read records what it carries. */
	touch(0);

	seed(1, XVT_INTEGRATE_PITCH);
	seed(2, XVT_INTEGRATE_PITCH);
	seed(3, XVT_INTEGRATE_PITCH);
	/* Unchanged: nothing is reset. */
	touch(0);
	XVT_ASSERT_INT_EQ(probe(1, XVT_INTEGRATE_PITCH), 1);
	seed(1, XVT_INTEGRATE_PITCH);

	/* Carrying slot 2 instead of slot 1 resets both; slot 3 is untouched. */
	g_test_craft.carried_object_index = 2;
	touch(0);
	XVT_ASSERT_INT_EQ(probe(1, XVT_INTEGRATE_PITCH), 0);
	XVT_ASSERT_INT_EQ(probe(2, XVT_INTEGRATE_PITCH), 0);
	XVT_ASSERT_INT_EQ(probe(3, XVT_INTEGRATE_PITCH), 1);
}

static void check_steer(void)
{
	fresh_world();
	/* 0xFFFF counts as a whole: 236 * 1 * 1 * 1 over 236 is exactly 1. */
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_steer(0, XVT_INTEGRATE_ROLL,
					     SIMULATION_TICKS_PER_SECOND,
					     0xFFFF, 0xFFFF, 1),
		1);
	/* 0x8000 is a half. */
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_steer(1, XVT_INTEGRATE_ROLL,
					     2 * SIMULATION_TICKS_PER_SECOND,
					     0x8000, 0xFFFF, 1),
		1);
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_steer(2, XVT_INTEGRATE_ROLL,
					     2 * SIMULATION_TICKS_PER_SECOND,
					     0xFFFF, 0x8000, 1),
		1);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_steer(0, XVT_INTEGRATE_COUNT,
						       1000, 0xFFFF, 0xFFFF, 1),
			  0);

	/* Clamped to 0xFFFF. */
	g_elapsed_ticks = 1000;
	XVT_ASSERT_INT_EQ(xvt_flight_integration_steer(3, XVT_INTEGRATE_ROLL,
						       0xFFFF, 0xFFFF, 0xFFFF,
						       1),
			  0xFFFF);

	/* 472 steps of one tick add up to one step of 472 ticks. */
	fresh_world();
	unsigned sum = 0;
	for (int i = 0; i < 2 * SIMULATION_TICKS_PER_SECOND; ++i) {
		sum += xvt_flight_integration_steer(0, XVT_INTEGRATE_TURN, 100,
						    0x8000, 0xFFFF, 1);
	}
	g_elapsed_ticks = 2 * SIMULATION_TICKS_PER_SECOND;
	XVT_ASSERT_INT_EQ(sum,
			  xvt_flight_integration_steer(1, XVT_INTEGRATE_TURN,
						       100, 0x8000, 0xFFFF, 1));
	XVT_ASSERT_INT_EQ(sum, 100);

	/* A new direction drops the carry: 235/236 of a unit carried, then 1/236 the other way is 0. */
	fresh_world();
	for (int i = 0; i < SIMULATION_TICKS_PER_SECOND - 1; ++i) {
		XVT_ASSERT_INT_EQ(
			xvt_flight_integration_steer(0, XVT_INTEGRATE_BANK, 1,
						     0xFFFF, 0xFFFF, 1),
			0);
	}
	XVT_ASSERT_INT_EQ(xvt_flight_integration_steer(1, XVT_INTEGRATE_BANK, 1,
						       0xFFFF, 0xFFFF, 1),
			  0);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_steer(0, XVT_INTEGRATE_BANK, 1,
						       0xFFFF, 0xFFFF, -1),
			  0);
	/* The same direction keeps it: the 236th step completes a unit. */
	for (int i = 0; i < SIMULATION_TICKS_PER_SECOND - 2; ++i) {
		XVT_ASSERT_INT_EQ(
			xvt_flight_integration_steer(1, XVT_INTEGRATE_BANK, 1,
						     0xFFFF, 0xFFFF, 1),
			0);
	}
	XVT_ASSERT_INT_EQ(xvt_flight_integration_steer(1, XVT_INTEGRATE_BANK, 1,
						       0xFFFF, 0xFFFF, 1),
			  1);
}

/* Runs Move on slot `steps` times and returns the summed movement on each axis. */
static void move_sum(unsigned slot, int steps, int64_t sum[3])
{
	sum[0] = 0;
	sum[1] = 0;
	sum[2] = 0;
	for (int i = 0; i < steps; ++i) {
		xvt_flight_integration_move(slot);
		sum[0] += trig2_xmovedist;
		sum[1] += trig2_ymovedist;
		sum[2] += trig2_zmovedist;
	}
}

static void set_motion(unsigned slot, uint16_t speed, int16_t x, int16_t y,
		       int16_t z)
{
	g_test_mobiles[slot].speed = speed;
	g_test_mobiles[slot].move_x = x;
	g_test_mobiles[slot].move_y = y;
	g_test_mobiles[slot].move_z = z;
}

static void check_move(void)
{
	/* Speed 256 scales to (4660 * 256 + 128) >> 8 = 4660; over 236 * 32768, an axis of 32767 moves 4660 *
	 * 32767 / 7733248 = 19.74 per tick and an axis of 1000 moves 0.60. */
	fresh_world();
	xvt_flight_integration_shutdown();
	set_motion(0, 256, 32767, 1000, 0);
	g_elapsed_ticks = 1;
	xvt_flight_integration_move(0);
	XVT_ASSERT_INT_EQ(trig2_xmovedist, 19);
	XVT_ASSERT_INT_EQ(trig2_ymovedist, 0);
	XVT_ASSERT_INT_EQ(trig2_zmovedist, 0);
	g_elapsed_ticks = 10;
	xvt_flight_integration_move(0);
	XVT_ASSERT_INT_EQ(trig2_xmovedist, 197);
	XVT_ASSERT_INT_EQ(trig2_ymovedist, 6);

	/* With the table, ten one-tick steps add up to one ten-tick step, on every axis and either sign. */
	fresh_world();
	set_motion(0, 300, 1000, -1000, 32767);
	set_motion(1, 300, 1000, -1000, 32767);
	int64_t steps[3];
	g_elapsed_ticks = 1;
	move_sum(0, 10, steps);
	g_elapsed_ticks = 10;
	xvt_flight_integration_move(1);
	XVT_ASSERT_INT_EQ(steps[0], trig2_xmovedist);
	XVT_ASSERT_INT_EQ(steps[1], trig2_ymovedist);
	XVT_ASSERT_INT_EQ(steps[2], trig2_zmovedist);
	XVT_ASSERT_TRUE(steps[0] > 0 && steps[1] < 0);

	/* At or past +-0x01000000 on an axis, that axis carries nothing; the others still do. */
	fresh_world();
	set_motion(0, 256, 1000, 1000, 1000);
	g_test_objects[0].world_x = 0x01000000;
	g_test_objects[0].world_y = 0x00FFFFFF;
	g_test_objects[0].world_z = -0x01000000;
	g_elapsed_ticks = 1;
	move_sum(0, 10, steps);
	XVT_ASSERT_INT_EQ(steps[0], 0);
	XVT_ASSERT_INT_EQ(steps[1], 6);
	XVT_ASSERT_INT_EQ(steps[2], 0);
}

static void check_push(void)
{
	fresh_world();
	g_elapsed_ticks = 1;
	/* Clamped to the cap: 236 per second at one tick moves 1. Nothing is created or lost. */
	int accum = 1000;
	int output = 0;
	xvt_flight_integration_push(0, 0, &accum, SIMULATION_TICKS_PER_SECOND,
				    &output);
	XVT_ASSERT_INT_EQ(output, 1);
	XVT_ASSERT_INT_EQ(accum, 999);
	for (int i = 0; i < 50; ++i) {
		xvt_flight_integration_push(0, 0, &accum, 100, &output);
		XVT_ASSERT_INT_EQ(accum + output, 1000);
	}
	/* 50 ticks at 100 a second, carried: 5000 / 236 = 21. */
	XVT_ASSERT_INT_EQ(output, 1 + 21);

	int negative = -1000;
	int out_negative = 0;
	xvt_flight_integration_push(1, 1, &negative,
				    SIMULATION_TICKS_PER_SECOND, &out_negative);
	XVT_ASSERT_INT_EQ(out_negative, -1);
	XVT_ASSERT_INT_EQ(negative, -999);

	/* Never more than the accumulator holds. */
	g_elapsed_ticks = 1000;
	int small = 5;
	int out_small = 7;
	xvt_flight_integration_push(2, 2, &small, 10000, &out_small);
	XVT_ASSERT_INT_EQ(small, 0);
	XVT_ASSERT_INT_EQ(out_small, 12);
	int small_negative = -5;
	int out_small_negative = 0;
	xvt_flight_integration_push(3, 0, &small_negative, 10000,
				    &out_small_negative);
	XVT_ASSERT_INT_EQ(small_negative, 0);
	XVT_ASSERT_INT_EQ(out_small_negative, -5);

	/* Emptying the accumulator drops the axis's carry: 300/236 leaves 64/236 behind until then. */
	fresh_world();
	g_elapsed_ticks = 300;
	int last = 1;
	int out_last = 0;
	xvt_flight_integration_push(4, 1, &last, 1, &out_last);
	XVT_ASSERT_INT_EQ(last, 0);
	struct xvt_integration_wire record;
	xvt_flight_integration_encode(4, &record);
	XVT_ASSERT_INT_EQ(record.type, 1);
	XVT_ASSERT_INT_EQ(
		xvt_wire_get64(record.remainder[XVT_INTEGRATE_PUSH_Y]), 0);
	XVT_ASSERT_INT_EQ(record.direction[XVT_INTEGRATE_PUSH_Y], 0);
}

static int records_equal(const struct xvt_integration_wire *a,
			 const struct xvt_integration_wire *b)
{
	return memcmp(a, b, sizeof *a) == 0;
}

/* A well-formed record for slot 2's object: every channel and axis carries something. */
static struct xvt_integration_wire good_record(void)
{
	struct xvt_integration_wire record;
	memset(&record, 0, sizeof record);
	xvt_wire_set16(record.slot, 2);
	xvt_wire_set16(record.signature, 0x102);
	record.type = 1;
	record.family = 0;
	xvt_wire_set16(record.carried_slot, 0xFFFF);
	xvt_wire_set16(record.target_slot, 4);
	xvt_wire_set16(record.target_signature, 0x104);
	for (unsigned axis = 0; axis < XVT_STATE_POSITION_AXES; ++axis) {
		xvt_wire_set64(record.position_remainder[axis],
			       (uint64_t)(int64_t)(axis == 1
							   ? -1000
							   : 1000 + (int)axis));
	}
	for (unsigned channel = 0; channel < XVT_INTEGRATE_COUNT; ++channel) {
		xvt_wire_set64(record.remainder[channel],
			       (uint64_t)(int64_t)(channel % 2 ? -(int)channel
							       : (int)channel));
		record.direction[channel] = (int8_t)(channel % 3) - 1;
	}
	return record;
}

static void check_encode_decode(void)
{
	fresh_world();
	struct xvt_integration_wire record = good_record();
	struct xvt_integration_wire out;
	XVT_ASSERT_INT_EQ(xvt_flight_integration_decode(&record, 1), 1);
	xvt_flight_integration_encode(2, &out);
	XVT_ASSERT_TRUE(records_equal(&out, &record));

	/* A carry built by Rate survives a round trip through a record into a fresh table. */
	fresh_world();
	seed(3, XVT_INTEGRATE_TURN);
	xvt_flight_integration_encode(3, &out);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_init(SLOTS), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_decode(&out, 1), 1);
	XVT_ASSERT_INT_EQ(probe(3, XVT_INTEGRATE_TURN), 1);

	/* Without apply the record is judged but not installed. */
	fresh_world();
	XVT_ASSERT_INT_EQ(xvt_flight_integration_decode(&record, 0), 1);
	xvt_flight_integration_encode(2, &out);
	XVT_ASSERT_INT_EQ(out.type, 0);

	/* The record is not checked against the object in the slot. */
	fresh_world();
	xvt_wire_set16(record.signature, 0x777);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_decode(&record, 1), 1);
}

static void check_encode_empty(void)
{
	fresh_world();
	struct xvt_integration_wire record = good_record();
	struct xvt_integration_wire out;
	struct xvt_integration_wire empty;
	XVT_ASSERT_INT_EQ(xvt_flight_integration_decode(&record, 1), 1);
	memset(&empty, 0, sizeof empty);
	xvt_wire_set16(empty.slot, 2);

	g_test_objects[2].object_signature = 0x999;
	xvt_flight_integration_encode(2, &out);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));
	g_test_objects[2].object_signature = 0x102;
	g_test_objects[2].object_type = 7;
	xvt_flight_integration_encode(2, &out);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));
	g_test_objects[2].object_type = 1;
	g_test_mobiles[2].family = 9;
	xvt_flight_integration_encode(2, &out);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));
	g_test_mobiles[2].family = 0;
	xvt_flight_integration_encode(2, &out);
	XVT_ASSERT_TRUE(records_equal(&out, &record));

	/* An empty slot. */
	g_test_objects[2].object_type = 0;
	xvt_flight_integration_encode(2, &out);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));

	/* A slot out of range. */
	xvt_flight_integration_encode(SLOTS + 3, &out);
	memset(&empty, 0, sizeof empty);
	xvt_wire_set16(empty.slot, SLOTS + 3);
	XVT_ASSERT_TRUE(records_equal(&out, &empty));
}

/* Decodes record with apply and checks it is refused and slot 2's entry is as before. */
static void expect_refused(const struct xvt_integration_wire *record)
{
	struct xvt_integration_wire before;
	struct xvt_integration_wire after;
	xvt_flight_integration_encode(2, &before);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_decode(record, 1), 0);
	xvt_flight_integration_encode(2, &after);
	XVT_ASSERT_TRUE(records_equal(&before, &after));
}

static void check_decode_refusals(void)
{
	fresh_world();
	seed(2, XVT_INTEGRATE_PITCH);
	const struct xvt_integration_wire good = good_record();
	struct xvt_integration_wire bad;
	const int64_t position_limit =
		(int64_t)SIMULATION_TICKS_PER_SECOND * 32768;
	const int64_t steering_limit =
		(int64_t)SIMULATION_TICKS_PER_SECOND * 65536 * 65536;
	const int64_t other_limit = INT64_C(1) << 31;

	bad = good;
	xvt_wire_set16(bad.slot, SLOTS);
	expect_refused(&bad);

	/* Type 0 must be exactly the empty record. */
	memset(&bad, 0, sizeof bad);
	xvt_wire_set16(bad.slot, 2);
	xvt_wire_set16(bad.signature, 1);
	expect_refused(&bad);
	xvt_wire_set16(bad.signature, 0);
	bad.direction[XVT_INTEGRATE_BANK] = 1;
	expect_refused(&bad);

	bad = good;
	xvt_wire_set16(bad.carried_slot, SLOTS);
	expect_refused(&bad);
	bad = good;
	xvt_wire_set16(bad.target_slot, SLOTS);
	expect_refused(&bad);

	bad = good;
	bad.direction[XVT_INTEGRATE_PUSH_Z] = 2;
	expect_refused(&bad);
	bad.direction[XVT_INTEGRATE_PUSH_Z] = -2;
	expect_refused(&bad);

	bad = good;
	xvt_wire_set64(bad.position_remainder[2], (uint64_t)position_limit);
	expect_refused(&bad);
	xvt_wire_set64(bad.position_remainder[2], (uint64_t)-position_limit);
	expect_refused(&bad);

	bad = good;
	xvt_wire_set64(bad.remainder[XVT_INTEGRATE_ROLL],
		       (uint64_t)steering_limit);
	expect_refused(&bad);
	bad = good;
	xvt_wire_set64(bad.remainder[XVT_INTEGRATE_BANK],
		       (uint64_t)-steering_limit);
	expect_refused(&bad);

	bad = good;
	xvt_wire_set64(bad.remainder[XVT_INTEGRATE_HOME_SPEED],
		       (uint64_t)other_limit);
	expect_refused(&bad);
	bad = good;
	xvt_wire_set64(bad.remainder[XVT_INTEGRATE_SPIN_DECAY],
		       (uint64_t)-other_limit);
	expect_refused(&bad);

	/* One short of each bound is accepted, as are 0xFFFF slots and the empty record. */
	bad = good;
	xvt_wire_set64(bad.position_remainder[0],
		       (uint64_t)(position_limit - 1));
	xvt_wire_set64(bad.position_remainder[1],
		       (uint64_t)(1 - position_limit));
	xvt_wire_set64(bad.remainder[XVT_INTEGRATE_ROLL],
		       (uint64_t)(steering_limit - 1));
	xvt_wire_set64(bad.remainder[XVT_INTEGRATE_TURN],
		       (uint64_t)(1 - steering_limit));
	xvt_wire_set64(bad.remainder[XVT_INTEGRATE_PUSH_X],
		       (uint64_t)(other_limit - 1));
	xvt_wire_set64(bad.remainder[XVT_INTEGRATE_HOME_YAW],
		       (uint64_t)(1 - other_limit));
	xvt_wire_set16(bad.target_slot, 0xFFFF);
	xvt_wire_set16(bad.carried_slot, SLOTS - 1);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_decode(&bad, 0), 1);
	memset(&bad, 0, sizeof bad);
	xvt_wire_set16(bad.slot, 2);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_decode(&bad, 0), 1);
}

static int has_reference_motion(unsigned slot)
{
	struct xvt_reference_motion_wire record;
	xvt_reference_motion_encode(slot, &record);
	return record.type != 0;
}

static void check_reset(void)
{
	fresh_world();
	seed(1, XVT_INTEGRATE_PITCH);
	seed(2, XVT_INTEGRATE_PITCH);
	XVT_ASSERT_INT_EQ(has_reference_motion(1), 1);
	xvt_flight_integration_reset_slot_and_motion(1);
	XVT_ASSERT_INT_EQ(probe(1, XVT_INTEGRATE_PITCH), 0);
	XVT_ASSERT_INT_EQ(has_reference_motion(1), 0);

	/* A slot out of range is ignored. */
	xvt_flight_integration_reset_slot_and_motion(SLOTS + 3);
	XVT_ASSERT_INT_EQ(probe(2, XVT_INTEGRATE_PITCH), 1);
	XVT_ASSERT_INT_EQ(has_reference_motion(2), 1);
}

static void check_reset_shared(void)
{
	fresh_world();
	for (unsigned slot = 0; slot < SLOTS; ++slot) {
		seed(slot, XVT_INTEGRATE_PITCH);
	}
	g_local_transient_slot_start = 2;
	g_local_debris_slot_end = 4;
	xvt_flight_integration_reset_shared();
	XVT_ASSERT_INT_EQ(probe(0, XVT_INTEGRATE_PITCH), 0);
	XVT_ASSERT_INT_EQ(probe(1, XVT_INTEGRATE_PITCH), 0);
	XVT_ASSERT_INT_EQ(probe(2, XVT_INTEGRATE_PITCH), 1);
	XVT_ASSERT_INT_EQ(probe(3, XVT_INTEGRATE_PITCH), 1);
	XVT_ASSERT_INT_EQ(probe(5, XVT_INTEGRATE_PITCH), 0);
	/* Reference motion is left alone. */
	XVT_ASSERT_INT_EQ(has_reference_motion(0), 1);
	XVT_ASSERT_INT_EQ(has_reference_motion(5), 1);

	/* Before Init it does nothing. */
	xvt_flight_integration_shutdown();
	xvt_flight_integration_reset_shared();
}

int main(void)
{
	check_seed_and_probe();
	check_no_table();
	check_rate_carry();
	check_rate_sign_change();
	check_rate_limits();
	check_clear();
	check_entry_follows_object();
	check_spin_channels();
	check_homing_channels();
	check_carried_object_reset();
	check_steer();
	check_move();
	check_push();
	check_encode_decode();
	check_encode_empty();
	check_decode_refusals();
	check_reset();
	check_reset_shared();
	xvt_flight_integration_shutdown();
	xvt_reference_motion_shutdown();
	return 0;
}
