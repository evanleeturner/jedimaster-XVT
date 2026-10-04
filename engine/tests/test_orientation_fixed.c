/* Checks the integer orientation turn, xvt_orientation_apply_pitch_yaw_fixed (xvt_runtime/hooks/
 * orientation_hook.h), against the promises in its header. The header does not say which angle values
 * mean level or straight up, so most checks are relationships that hold whatever the convention: a turn
 * and its inverse, two turns about one axis and their sum, yaw applied before pitch, a whole turn added
 * to a delta, and agreement with the float path, ApplyPitchYaw, which the header promises "within
 * rounding". Starting orientations are ones this function returned, and every orientation compared
 * stays well away from straight up and down, where the header allows the angles to differ.
 *
 * The float path runs outside a NETWORK_125 session; the flight timing session is ended first so it
 * reads as native. */
#include <stdint.h>
#include <stdio.h>

#include "test_assert.h"
#include "xvt_runtime/hooks/orientation_hook.h"
#include "xvt_runtime/timing/flight_timing.h"

/* Each call rounds every angle to a whole unit, 1/65536 of a turn, and the float path works in
 * single-precision radians; a few chained calls measured within 4 units of each other. 8 units (0.044
 * degrees) leaves room for that, while a turn about the wrong axis, in the wrong order or the wrong way is
 * off by hundreds of units. */
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

#define EXPECT_EQUAL_ANGLES(actual, expected)                                  \
	do {                                                                   \
		struct xvt_orientation_angles actual_ = (actual);              \
		struct xvt_orientation_angles expected_ = (expected);          \
		XVT_ASSERT_INT_EQ(actual_.yaw, expected_.yaw);                 \
		XVT_ASSERT_INT_EQ(actual_.pitch, expected_.pitch);             \
		XVT_ASSERT_INT_EQ(actual_.roll, expected_.roll);               \
	} while (0)

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
static struct xvt_orientation_angles orientation_fixed_start(unsigned seed)
{
	return apply_fixed(k_seeds[seed], 0x0100, 0x0100);
}

static void check_zero_turns_return_input(void)
{
	static const struct xvt_orientation_angles inputs[] = {
		{0x1234, 0x3000, 0x0400},
		{0x0000, 0xC000, 0x0000},
		{0xFFFF, 0x8000, 0x8001},
		{0x4000, 0x0000, 0x2000}};
	static const int zero[][2] = {
		{0, 0}, {0x10000, 0}, {0, -0x10000}, {-0x20000, 0x30000}};
	for (unsigned i = 0; i < sizeof inputs / sizeof inputs[0]; ++i) {
		for (unsigned j = 0; j < sizeof zero / sizeof zero[0]; ++j) {
			EXPECT_EQUAL_ANGLES(
				apply_fixed(inputs[i], zero[j][0], zero[j][1]),
				inputs[i]);
		}
	}
}

static void check_agrees_with_float(void)
{
	xvt_flight_timing_end_session();
	XVT_ASSERT_INT_EQ(xvt_flight_timing_is_network125(), 0);
	for (unsigned s = 0; s < SEED_COUNT; ++s) {
		for (unsigned d = 0; d < DELTA_COUNT; ++d) {
			int pitch = k_deltas[d][0];
			int neg_yaw = k_deltas[d][1];
			EXPECT_SAME_ORIENTATION(
				apply_fixed(k_seeds[s], pitch, neg_yaw),
				xvt_orientation_apply_pitch_yaw(
					k_seeds[s], pitch, neg_yaw));
			EXPECT_SAME_ORIENTATION(
				apply_fixed(orientation_fixed_start(s), pitch,
					    neg_yaw),
				xvt_orientation_apply_pitch_yaw(
					orientation_fixed_start(s), pitch,
					neg_yaw));
		}
	}
}

static void check_inverse(void)
{
	for (unsigned s = 0; s < SEED_COUNT; ++s) {
		struct xvt_orientation_angles start =
			orientation_fixed_start(s);
		for (unsigned d = 0; d < DELTA_COUNT; ++d) {
			int pitch = k_deltas[d][0];
			int neg_yaw = k_deltas[d][1];
			EXPECT_SAME_ORIENTATION(
				apply_fixed(apply_fixed(start, pitch, 0),
					    -pitch, 0),
				start);
			EXPECT_SAME_ORIENTATION(
				apply_fixed(apply_fixed(start, 0, neg_yaw), 0,
					    -neg_yaw),
				start);
			/* Yaw then pitch is undone by pitch back, then yaw back. */
			EXPECT_SAME_ORIENTATION(
				apply_fixed(
					apply_fixed(apply_fixed(start, pitch,
								neg_yaw),
						    -pitch, 0),
					0, -neg_yaw),
				start);
		}
	}
}

static void check_same_axis_adds(void)
{
	for (unsigned s = 0; s < SEED_COUNT; ++s) {
		struct xvt_orientation_angles start =
			orientation_fixed_start(s);
		EXPECT_SAME_ORIENTATION(
			apply_fixed(apply_fixed(start, 0, 0x0700), 0, 0x0900),
			apply_fixed(start, 0, 0x1000));
		EXPECT_SAME_ORIENTATION(
			apply_fixed(apply_fixed(start, 0x0700, 0), 0x0900, 0),
			apply_fixed(start, 0x1000, 0));
		EXPECT_SAME_ORIENTATION(
			apply_fixed(apply_fixed(start, -0x0300, 0), 0x0100, 0),
			apply_fixed(start, -0x0200, 0));

		/* Four quarter turns about one axis are a whole turn. */
		struct xvt_orientation_angles yawed = start;
		struct xvt_orientation_angles pitched = start;
		for (int i = 0; i < 4; ++i) {
			yawed = apply_fixed(yawed, 0, 0x4000);
			pitched = apply_fixed(pitched, 0x4000, 0);
		}
		EXPECT_SAME_ORIENTATION(yawed, start);
		EXPECT_SAME_ORIENTATION(pitched, start);
	}
}

static void check_yaw_then_pitch(void)
{
	for (unsigned s = 0; s < SEED_COUNT; ++s) {
		struct xvt_orientation_angles start =
			orientation_fixed_start(s);
		for (unsigned d = 0; d < DELTA_COUNT; ++d) {
			int pitch = k_deltas[d][0];
			int neg_yaw = k_deltas[d][1];
			EXPECT_SAME_ORIENTATION(
				apply_fixed(start, pitch, neg_yaw),
				apply_fixed(apply_fixed(start, 0, neg_yaw),
					    pitch, 0));
		}
	}
}

static void check_delta_wrap(void)
{
	/* 65536 units are a turn: a whole turn more or less on either delta is the same turn. */
	for (unsigned s = 0; s < SEED_COUNT; ++s) {
		struct xvt_orientation_angles start =
			orientation_fixed_start(s);
		for (unsigned d = 0; d < DELTA_COUNT; ++d) {
			int pitch = k_deltas[d][0];
			int neg_yaw = k_deltas[d][1];
			struct xvt_orientation_angles turned =
				apply_fixed(start, pitch, neg_yaw);
			EXPECT_SAME_ORIENTATION(apply_fixed(start,
							    pitch + 0x10000,
							    neg_yaw - 0x10000),
						turned);
			EXPECT_SAME_ORIENTATION(apply_fixed(start,
							    pitch - 0x20000,
							    neg_yaw + 0x10000),
						turned);
		}
	}
}

int main(void)
{
	check_zero_turns_return_input();
	check_agrees_with_float();
	check_inverse();
	check_same_axis_adds();
	check_yaw_then_pitch();
	check_delta_wrap();
	return 0;
}
