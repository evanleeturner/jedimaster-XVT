/* Checks the integer orientation turn, XvtOrientation_ApplyPitchYawFixed (xvt_runtime/hooks/
 * orientation_hook.h), against the promises in its header. The header does not say which angle values
 * mean level or straight up, so most checks are relationships that hold whatever the convention: a turn
 * and its inverse, two turns about one axis and their sum, yaw applied before pitch, a whole turn added
 * to a delta, and agreement with the float path, ApplyPitchYaw, which the header promises "within
 * rounding". Starting orientations are ones this function returned, and every orientation compared
 * stays well away from straight up and down, where the header allows the angles to differ.
 *
 * The float path runs outside a NETWORK_125 session; the flight timing session is ended first so it
 * reads as native. */
#include "test_assert.h"
#include "xvt_runtime/hooks/orientation_hook.h"
#include "xvt_runtime/timing/flight_timing.h"

#include <stdint.h>
#include <stdio.h>

typedef XvtOrientationAngles Angles;

/* Each call rounds every angle to a whole unit, 1/65536 of a turn, and the float path works in
 * single-precision radians; a few chained calls measured within 4 units of each other. 8 units (0.044
 * degrees) leaves room for that, while a turn about the wrong axis, in the wrong order or the wrong way is
 * off by hundreds of units. */
enum { kRoundingUnits = 8 };

static int Gap(uint16_t a, uint16_t b) {
	int gap = (int16_t)(uint16_t)(a - b);
	return gap < 0 ? -gap : gap;
}

static void ExpectSameOrientation(Angles actual, Angles expected, int line) {
	int ok = Gap(actual.yaw, expected.yaw) <= kRoundingUnits &&
			 Gap(actual.pitch, expected.pitch) <= kRoundingUnits &&
			 Gap(actual.roll, expected.roll) <= kRoundingUnits;
	if (!ok)
		fprintf(stderr, "got yaw %04x pitch %04x roll %04x, expected %04x %04x %04x\n", actual.yaw,
				actual.pitch, actual.roll, expected.yaw, expected.pitch, expected.roll);
	xvt_test_check(ok, __FILE__, line, "the same orientation within rounding");
}

#define EXPECT_SAME_ORIENTATION(actual, expected) ExpectSameOrientation((actual), (expected), __LINE__)

#define EXPECT_EQUAL_ANGLES(actual, expected)                                                                \
	do {                                                                                                     \
		Angles actual_ = (actual), expected_ = (expected);                                                   \
		XVT_ASSERT_INT_EQ(actual_.yaw, expected_.yaw);                                                       \
		XVT_ASSERT_INT_EQ(actual_.pitch, expected_.pitch);                                                   \
		XVT_ASSERT_INT_EQ(actual_.roll, expected_.roll);                                                     \
	} while (0)

static Angles Fixed(Angles current, int pitch, int neg_yaw) {
	return XvtOrientation_ApplyPitchYawFixed(current, pitch, neg_yaw);
}

static const Angles kSeeds[] = {
	{ 0x1234, 0x3000, 0x0400 }, { 0x9000, 0x5000, 0xF000 }, { 0xE000, 0x4800, 0x7000 },
	{ 0x4321, 0xC800, 0x2222 }, { 0x0000, 0xC000, 0x0000 },
};

enum { kSeedCount = sizeof kSeeds / sizeof kSeeds[0] };

/* Pitch and negated-yaw deltas, up to an eighth of a turn. */
static const int kDeltas[][2] = {
	{ 0x0400, 0 }, { 0, 0x0400 }, { -0x0900, 0x0700 }, { 0x1000, -0x1800 }, { 0x0013, -0x0011 },
};

enum { kDeltaCount = sizeof kDeltas / sizeof kDeltas[0] };

/* A starting orientation in the form this function returns. */
static Angles Start(unsigned seed) { return Fixed(kSeeds[seed], 0x0100, 0x0100); }

static void CheckZeroTurnsReturnInput(void) {
	static const Angles inputs[] = { { 0x1234, 0x3000, 0x0400 },
									 { 0x0000, 0xC000, 0x0000 },
									 { 0xFFFF, 0x8000, 0x8001 },
									 { 0x4000, 0x0000, 0x2000 } };
	static const int zero[][2] = { { 0, 0 }, { 0x10000, 0 }, { 0, -0x10000 }, { -0x20000, 0x30000 } };
	for (unsigned i = 0; i < sizeof inputs / sizeof inputs[0]; ++i)
		for (unsigned j = 0; j < sizeof zero / sizeof zero[0]; ++j)
			EXPECT_EQUAL_ANGLES(Fixed(inputs[i], zero[j][0], zero[j][1]), inputs[i]);
}

static void CheckAgreesWithFloat(void) {
	XvtFlightTiming_EndSession();
	XVT_ASSERT_INT_EQ(XvtFlightTiming_IsNetwork125(), 0);
	for (unsigned s = 0; s < kSeedCount; ++s) {
		for (unsigned d = 0; d < kDeltaCount; ++d) {
			int pitch = kDeltas[d][0], neg_yaw = kDeltas[d][1];
			EXPECT_SAME_ORIENTATION(Fixed(kSeeds[s], pitch, neg_yaw),
									XvtOrientation_ApplyPitchYaw(kSeeds[s], pitch, neg_yaw));
			EXPECT_SAME_ORIENTATION(Fixed(Start(s), pitch, neg_yaw),
									XvtOrientation_ApplyPitchYaw(Start(s), pitch, neg_yaw));
		}
	}
}

static void CheckInverse(void) {
	for (unsigned s = 0; s < kSeedCount; ++s) {
		Angles start = Start(s);
		for (unsigned d = 0; d < kDeltaCount; ++d) {
			int pitch = kDeltas[d][0], neg_yaw = kDeltas[d][1];
			EXPECT_SAME_ORIENTATION(Fixed(Fixed(start, pitch, 0), -pitch, 0), start);
			EXPECT_SAME_ORIENTATION(Fixed(Fixed(start, 0, neg_yaw), 0, -neg_yaw), start);
			/* Yaw then pitch is undone by pitch back, then yaw back. */
			EXPECT_SAME_ORIENTATION(Fixed(Fixed(Fixed(start, pitch, neg_yaw), -pitch, 0), 0, -neg_yaw),
									start);
		}
	}
}

static void CheckSameAxisAdds(void) {
	for (unsigned s = 0; s < kSeedCount; ++s) {
		Angles start = Start(s);
		EXPECT_SAME_ORIENTATION(Fixed(Fixed(start, 0, 0x0700), 0, 0x0900), Fixed(start, 0, 0x1000));
		EXPECT_SAME_ORIENTATION(Fixed(Fixed(start, 0x0700, 0), 0x0900, 0), Fixed(start, 0x1000, 0));
		EXPECT_SAME_ORIENTATION(Fixed(Fixed(start, -0x0300, 0), 0x0100, 0), Fixed(start, -0x0200, 0));

		/* Four quarter turns about one axis are a whole turn. */
		Angles yawed = start, pitched = start;
		for (int i = 0; i < 4; ++i) {
			yawed = Fixed(yawed, 0, 0x4000);
			pitched = Fixed(pitched, 0x4000, 0);
		}
		EXPECT_SAME_ORIENTATION(yawed, start);
		EXPECT_SAME_ORIENTATION(pitched, start);
	}
}

static void CheckYawThenPitch(void) {
	for (unsigned s = 0; s < kSeedCount; ++s) {
		Angles start = Start(s);
		for (unsigned d = 0; d < kDeltaCount; ++d) {
			int pitch = kDeltas[d][0], neg_yaw = kDeltas[d][1];
			EXPECT_SAME_ORIENTATION(Fixed(start, pitch, neg_yaw), Fixed(Fixed(start, 0, neg_yaw), pitch, 0));
		}
	}
}

static void CheckDeltaWrap(void) {
	/* 65536 units are a turn: a whole turn more or less on either delta is the same turn. */
	for (unsigned s = 0; s < kSeedCount; ++s) {
		Angles start = Start(s);
		for (unsigned d = 0; d < kDeltaCount; ++d) {
			int pitch = kDeltas[d][0], neg_yaw = kDeltas[d][1];
			Angles turned = Fixed(start, pitch, neg_yaw);
			EXPECT_SAME_ORIENTATION(Fixed(start, pitch + 0x10000, neg_yaw - 0x10000), turned);
			EXPECT_SAME_ORIENTATION(Fixed(start, pitch - 0x20000, neg_yaw + 0x10000), turned);
		}
	}
}

int main(void) {
	CheckZeroTurnsReturnInput();
	CheckAgreesWithFloat();
	CheckInverse();
	CheckSameAxisAdds();
	CheckYawThenPitch();
	CheckDeltaWrap();
	return 0;
}
