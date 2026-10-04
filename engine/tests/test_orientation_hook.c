/* Checks the float orientation turn, xvt_orientation_apply_pitch_yaw
 * (xvt_runtime/hooks/orientation_hook.h), against the promises in its header:
 * in a NETWORK_125 session it returns exactly what the integer path returns;
 * otherwise its result agrees with the integer path within rounding, a call
 * with zero deltas keeps the orientation, a turn and its inverse cancel, yaw is
 * applied before pitch, and a whole turn on a delta changes nothing beyond
 * rounding. The header does not say which angle values mean level or straight
 * up, so the checks are relationships that hold whatever the convention.
 * Starting orientations are ones this function returned, and every orientation
 * compared stays well away from straight up and down, where the header allows
 * the angles to differ. */
#include <stdint.h>
#include <stdio.h>

#include "test_assert.h"
#include "xvt_runtime/hooks/orientation_hook.h"
#include "xvt_runtime/timing/flight_timing.h"

/* Each call rounds every angle to a whole unit, 1/65536 of a turn, and works in
 * single-precision radians, so the header allows drift; a few chained calls
 * measured within 4 units of each other. 8 units (0.044 degrees) leaves room
 * for that, while a turn about the wrong axis, in the wrong order or the wrong
 * way is off by hundreds of units. */
enum { ROUNDING_UNITS = 8 };

static int gap(uint16_t a, uint16_t b)
{
	int gap = (int16_t)(uint16_t)(a - b);
	return gap < 0 ? -gap : gap;
}

static void expect_same_orientation(struct xvt_orientation_angles actual,
				    struct xvt_orientation_angles expected,
				    int line)
{
	int ok = gap(actual.yaw, expected.yaw) <= ROUNDING_UNITS &&
		 gap(actual.pitch, expected.pitch) <= ROUNDING_UNITS &&
		 gap(actual.roll, expected.roll) <= ROUNDING_UNITS;
	if (!ok) {
		fprintf(stderr,
			"got yaw %04x pitch %04x roll %04x, expected %04x %04x %04x\n",
			actual.yaw, actual.pitch, actual.roll, expected.yaw,
			expected.pitch, expected.roll);
	}
	xvt_test_check(ok, __FILE__, line,
		       "the same orientation within rounding");
}

#define EXPECT_SAME_ORIENTATION(actual, expected)                              \
	expect_same_orientation((actual), (expected), __LINE__)

static struct xvt_orientation_angles
apply_float(struct xvt_orientation_angles current, int pitch, int neg_yaw)
{
	return xvt_orientation_apply_pitch_yaw(current, pitch, neg_yaw);
}

static struct xvt_orientation_angles
apply_fixed(struct xvt_orientation_angles current, int pitch, int neg_yaw)
{
	return xvt_orientation_apply_pitch_yaw_fixed(current, pitch, neg_yaw);
}

static const struct xvt_orientation_angles k_seeds[] = {
	{0x1234, 0x3000, 0x0400}, {0x9000, 0x5000, 0xF000},
	{0xE000, 0x4800, 0x7000}, {0x4321, 0xC800, 0x2222},
	{0x0000, 0xC000, 0x0000},
};

enum { SEED_COUNT = sizeof k_seeds / sizeof k_seeds[0] };

/* Pitch and negated-yaw deltas, up to an eighth of a turn. */
static const int k_deltas[][2] = {
	{0x0400, 0},	   {0, 0x0400},	      {-0x0900, 0x0700},
	{0x1000, -0x1800}, {0x0013, -0x0011},
};

enum { DELTA_COUNT = sizeof k_deltas / sizeof k_deltas[0] };

/* A starting orientation in the form this function returns. */
static struct xvt_orientation_angles orientation_hook_start(unsigned seed)
{
	return apply_float(k_seeds[seed], 0x0100, 0x0100);
}

static void check_network_uses_fixed(void)
{
	xvt_flight_timing_begin_session(XVT_FLIGHT_TIMING_NETWORK_125);
	static const int extra[][2] = {
		{0, 0}, {0x10000, 0}, {0x4000, 0}, {0, 0x8000}};
	for (unsigned s = 0; s < SEED_COUNT; ++s) {
		for (unsigned d = 0; d < DELTA_COUNT + 4; ++d) {
			int pitch = d < DELTA_COUNT ? k_deltas[d][0]
						    : extra[d - DELTA_COUNT][0];
			int neg_yaw = d < DELTA_COUNT
					      ? k_deltas[d][1]
					      : extra[d - DELTA_COUNT][1];
			struct xvt_orientation_angles network =
				apply_float(k_seeds[s], pitch, neg_yaw);
			struct xvt_orientation_angles fixed =
				apply_fixed(k_seeds[s], pitch, neg_yaw);
			XVT_ASSERT_INT_EQ(network.yaw, fixed.yaw);
			XVT_ASSERT_INT_EQ(network.pitch, fixed.pitch);
			XVT_ASSERT_INT_EQ(network.roll, fixed.roll);
		}
	}
	xvt_flight_timing_end_session();
}

static void check_agrees_with_fixed(void)
{
	xvt_flight_timing_begin_session(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	for (unsigned s = 0; s < SEED_COUNT; ++s) {
		for (unsigned d = 0; d < DELTA_COUNT; ++d) {
			int pitch = k_deltas[d][0];
			int neg_yaw = k_deltas[d][1];
			EXPECT_SAME_ORIENTATION(
				apply_float(k_seeds[s], pitch, neg_yaw),
				apply_fixed(k_seeds[s], pitch, neg_yaw));
			EXPECT_SAME_ORIENTATION(
				apply_float(orientation_hook_start(s), pitch,
					    neg_yaw),
				apply_fixed(orientation_hook_start(s), pitch,
					    neg_yaw));
		}
	}
	xvt_flight_timing_end_session();
}

static void check_zero_deltas_keep_orientation(void)
{
	xvt_flight_timing_end_session();
	for (unsigned s = 0; s < SEED_COUNT; ++s) {
		/* An orientation in the returned form comes back within rounding. */
		EXPECT_SAME_ORIENTATION(
			apply_float(orientation_hook_start(s), 0, 0),
			orientation_hook_start(s));
		/* Any other form may be rewritten, but it is the same
		 * orientation: the same turn of either gives the same
		 * result. */
		struct xvt_orientation_angles rewritten =
			apply_float(k_seeds[s], 0, 0);
		EXPECT_SAME_ORIENTATION(
			apply_fixed(rewritten, 0x0300, 0x0200),
			apply_fixed(k_seeds[s], 0x0300, 0x0200));
	}
}

static void check_inverse(void)
{
	xvt_flight_timing_end_session();
	for (unsigned s = 0; s < SEED_COUNT; ++s) {
		struct xvt_orientation_angles start = orientation_hook_start(s);
		for (unsigned d = 0; d < DELTA_COUNT; ++d) {
			int pitch = k_deltas[d][0];
			int neg_yaw = k_deltas[d][1];
			EXPECT_SAME_ORIENTATION(
				apply_float(apply_float(start, pitch, 0),
					    -pitch, 0),
				start);
			EXPECT_SAME_ORIENTATION(
				apply_float(apply_float(start, 0, neg_yaw), 0,
					    -neg_yaw),
				start);
			EXPECT_SAME_ORIENTATION(
				apply_float(
					apply_float(apply_float(start, pitch,
								neg_yaw),
						    -pitch, 0),
					0, -neg_yaw),
				start);
		}
	}
}

static void check_yaw_then_pitch(void)
{
	xvt_flight_timing_end_session();
	for (unsigned s = 0; s < SEED_COUNT; ++s) {
		struct xvt_orientation_angles start = orientation_hook_start(s);
		for (unsigned d = 0; d < DELTA_COUNT; ++d) {
			int pitch = k_deltas[d][0];
			int neg_yaw = k_deltas[d][1];
			EXPECT_SAME_ORIENTATION(
				apply_float(start, pitch, neg_yaw),
				apply_float(apply_float(start, 0, neg_yaw),
					    pitch, 0));
		}
	}
}

static void check_whole_turns(void)
{
	xvt_flight_timing_end_session();
	for (unsigned s = 0; s < SEED_COUNT; ++s) {
		struct xvt_orientation_angles start = orientation_hook_start(s);
		EXPECT_SAME_ORIENTATION(apply_float(start, 0x10000, 0), start);
		EXPECT_SAME_ORIENTATION(apply_float(start, 0, -0x10000), start);
		struct xvt_orientation_angles yawed = start;
		struct xvt_orientation_angles pitched = start;
		for (int i = 0; i < 4; ++i) {
			yawed = apply_float(yawed, 0, 0x4000);
			pitched = apply_float(pitched, 0x4000, 0);
		}
		EXPECT_SAME_ORIENTATION(yawed, start);
		EXPECT_SAME_ORIENTATION(pitched, start);
		for (unsigned d = 0; d < DELTA_COUNT; ++d) {
			int pitch = k_deltas[d][0];
			int neg_yaw = k_deltas[d][1];
			EXPECT_SAME_ORIENTATION(
				apply_float(start, pitch + 0x10000,
					    neg_yaw - 0x10000),
				apply_float(start, pitch, neg_yaw));
		}
	}
}

int main(void)
{
	check_network_uses_fixed();
	check_agrees_with_fixed();
	check_zero_deltas_keep_orientation();
	check_inverse();
	check_yaw_then_pitch();
	check_whole_turns();
	xvt_flight_timing_end_session();
	return 0;
}
