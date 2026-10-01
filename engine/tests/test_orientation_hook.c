/* Checks the float orientation turn, XvtOrientation_ApplyPitchYaw (xvt_runtime/hooks/orientation_hook.h),
 * against the promises in its header: in a NETWORK_125 session it returns exactly what the integer path
 * returns; otherwise its result agrees with the integer path within rounding, a call with zero deltas
 * keeps the orientation, a turn and its inverse cancel, yaw is applied before pitch, and a whole turn on
 * a delta changes nothing beyond rounding. The header does not say which angle values mean level or
 * straight up, so the checks are relationships that hold whatever the convention. Starting orientations
 * are ones this function returned, and every orientation compared stays well away from straight up and
 * down, where the header allows the angles to differ. */
#include "test_assert.h"
#include "xvt_runtime/hooks/orientation_hook.h"
#include "xvt_runtime/timing/flight_timing.h"

#include <stdint.h>
#include <stdio.h>

typedef XvtOrientationAngles Angles;

/* Each call rounds every angle to a whole unit, 1/65536 of a turn, and works in single-precision radians,
 * so the header allows drift; a few chained calls measured within 4 units of each other. 8 units (0.044
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

static Angles Float(Angles current, int pitch, int neg_yaw) {
	return XvtOrientation_ApplyPitchYaw(current, pitch, neg_yaw);
}

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
static Angles Start(unsigned seed) { return Float(kSeeds[seed], 0x0100, 0x0100); }

static void CheckNetworkUsesFixed(void) {
	XvtFlightTiming_BeginSession(XVT_FLIGHT_TIMING_NETWORK_125);
	static const int extra[][2] = { { 0, 0 }, { 0x10000, 0 }, { 0x4000, 0 }, { 0, 0x8000 } };
	for (unsigned s = 0; s < kSeedCount; ++s) {
		for (unsigned d = 0; d < kDeltaCount + 4; ++d) {
			int pitch = d < kDeltaCount ? kDeltas[d][0] : extra[d - kDeltaCount][0];
			int neg_yaw = d < kDeltaCount ? kDeltas[d][1] : extra[d - kDeltaCount][1];
			Angles network = Float(kSeeds[s], pitch, neg_yaw), fixed = Fixed(kSeeds[s], pitch, neg_yaw);
			XVT_ASSERT_INT_EQ(network.yaw, fixed.yaw);
			XVT_ASSERT_INT_EQ(network.pitch, fixed.pitch);
			XVT_ASSERT_INT_EQ(network.roll, fixed.roll);
		}
	}
	XvtFlightTiming_EndSession();
}

static void CheckAgreesWithFixed(void) {
	XvtFlightTiming_BeginSession(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	for (unsigned s = 0; s < kSeedCount; ++s) {
		for (unsigned d = 0; d < kDeltaCount; ++d) {
			int pitch = kDeltas[d][0], neg_yaw = kDeltas[d][1];
			EXPECT_SAME_ORIENTATION(Float(kSeeds[s], pitch, neg_yaw), Fixed(kSeeds[s], pitch, neg_yaw));
			EXPECT_SAME_ORIENTATION(Float(Start(s), pitch, neg_yaw), Fixed(Start(s), pitch, neg_yaw));
		}
	}
	XvtFlightTiming_EndSession();
}

static void CheckZeroDeltasKeepOrientation(void) {
	XvtFlightTiming_EndSession();
	for (unsigned s = 0; s < kSeedCount; ++s) {
		/* An orientation in the returned form comes back within rounding. */
		EXPECT_SAME_ORIENTATION(Float(Start(s), 0, 0), Start(s));
		/* Any other form may be rewritten, but it is the same orientation: the same turn of either gives
		 * the same result. */
		Angles rewritten = Float(kSeeds[s], 0, 0);
		EXPECT_SAME_ORIENTATION(Fixed(rewritten, 0x0300, 0x0200), Fixed(kSeeds[s], 0x0300, 0x0200));
	}
}

static void CheckInverse(void) {
	XvtFlightTiming_EndSession();
	for (unsigned s = 0; s < kSeedCount; ++s) {
		Angles start = Start(s);
		for (unsigned d = 0; d < kDeltaCount; ++d) {
			int pitch = kDeltas[d][0], neg_yaw = kDeltas[d][1];
			EXPECT_SAME_ORIENTATION(Float(Float(start, pitch, 0), -pitch, 0), start);
			EXPECT_SAME_ORIENTATION(Float(Float(start, 0, neg_yaw), 0, -neg_yaw), start);
			EXPECT_SAME_ORIENTATION(Float(Float(Float(start, pitch, neg_yaw), -pitch, 0), 0, -neg_yaw),
									start);
		}
	}
}

static void CheckYawThenPitch(void) {
	XvtFlightTiming_EndSession();
	for (unsigned s = 0; s < kSeedCount; ++s) {
		Angles start = Start(s);
		for (unsigned d = 0; d < kDeltaCount; ++d) {
			int pitch = kDeltas[d][0], neg_yaw = kDeltas[d][1];
			EXPECT_SAME_ORIENTATION(Float(start, pitch, neg_yaw), Float(Float(start, 0, neg_yaw), pitch, 0));
		}
	}
}

static void CheckWholeTurns(void) {
	XvtFlightTiming_EndSession();
	for (unsigned s = 0; s < kSeedCount; ++s) {
		Angles start = Start(s);
		EXPECT_SAME_ORIENTATION(Float(start, 0x10000, 0), start);
		EXPECT_SAME_ORIENTATION(Float(start, 0, -0x10000), start);
		Angles yawed = start, pitched = start;
		for (int i = 0; i < 4; ++i) {
			yawed = Float(yawed, 0, 0x4000);
			pitched = Float(pitched, 0x4000, 0);
		}
		EXPECT_SAME_ORIENTATION(yawed, start);
		EXPECT_SAME_ORIENTATION(pitched, start);
		for (unsigned d = 0; d < kDeltaCount; ++d) {
			int pitch = kDeltas[d][0], neg_yaw = kDeltas[d][1];
			EXPECT_SAME_ORIENTATION(Float(start, pitch + 0x10000, neg_yaw - 0x10000),
									Float(start, pitch, neg_yaw));
		}
	}
}

int main(void) {
	CheckNetworkUsesFixed();
	CheckAgreesWithFixed();
	CheckZeroDeltasKeepOrientation();
	CheckInverse();
	CheckYawThenPitch();
	CheckWholeTurns();
	XvtFlightTiming_EndSession();
	return 0;
}
