/* Checks the runtime clock (xvt_runtime/timing/host_clock.h) against the promises in its header: it
 * starts from 0 after Reset, moves only by positive deltas, reports whole milliseconds cut to 32 bits,
 * and answers the game's timeGetTime and GetTickCount from that value. Each case starts from Reset. */
#include "test_assert.h"
#include "xvt/util/time.h"
#include "xvt_runtime/timing/host_clock.h"

#include <stdint.h>

static void CheckReset(void) {
	XvtTime_Reset();
	XvtTime_AdvanceHostClock(123456);
	XVT_ASSERT_TRUE(XvtTime_GetElapsedUs() != 0);
	XvtTime_Reset();
	XVT_ASSERT_INT_EQ(XvtTime_GetElapsedUs(), 0);
	XVT_ASSERT_INT_EQ(XvtTime_GetElapsedMs(), 0);
}

static void CheckAdvance(void) {
	XvtTime_Reset();
	XvtTime_AdvanceHostClock(250);
	XvtTime_AdvanceHostClock(750);
	XVT_ASSERT_INT_EQ(XvtTime_GetElapsedUs(), 1000);

	/* Zero and negative deltas are ignored. */
	XvtTime_AdvanceHostClock(0);
	XvtTime_AdvanceHostClock(-1);
	XvtTime_AdvanceHostClock(INT32_MIN);
	XVT_ASSERT_INT_EQ(XvtTime_GetElapsedUs(), 1000);

	XvtTime_AdvanceHostClock(INT32_MAX);
	XVT_ASSERT_INT_EQ(XvtTime_GetElapsedUs(), 1000 + (uint64_t)INT32_MAX);
}

static void CheckWholeMilliseconds(void) {
	XvtTime_Reset();
	XvtTime_AdvanceHostClock(999);
	XVT_ASSERT_INT_EQ(XvtTime_GetElapsedMs(), 0);
	XvtTime_AdvanceHostClock(1);
	XVT_ASSERT_INT_EQ(XvtTime_GetElapsedMs(), 1);
	XvtTime_AdvanceHostClock(1999);
	XVT_ASSERT_INT_EQ(XvtTime_GetElapsedMs(), 2);
}

static void CheckTicksWrap(void) {
	XvtTime_Reset();
	/* 2^32 milliseconds plus 5, in steps of 2^31 - 1 microseconds and a last partial step. */
	const uint64_t target = (UINT64_C(1) << 32) * 1000 + 5000;
	uint64_t added = 0;
	while (target - added > (uint64_t)INT32_MAX) {
		XvtTime_AdvanceHostClock(INT32_MAX);
		added += (uint64_t)INT32_MAX;
	}
	XvtTime_AdvanceHostClock((int32_t)(target - added));
	/* The microsecond count does not wrap; the millisecond count does. */
	XVT_ASSERT_INT_EQ(XvtTime_GetElapsedUs(), target);
	XVT_ASSERT_INT_EQ(XvtTime_GetElapsedMs(), 5);
}

static void CheckWindowsClock(void) {
	XvtTime_Reset();
	XvtTime_AdvanceHostClock(42 * 1000 + 300);
	XVT_ASSERT_INT_EQ(timeGetTime(), XvtTime_GetElapsedMs());
	XVT_ASSERT_INT_EQ(GetTickCount(), XvtTime_GetElapsedMs());
	XVT_ASSERT_INT_EQ(timeGetTime(), 42);
}

int main(void) {
	CheckReset();
	CheckAdvance();
	CheckWholeMilliseconds();
	CheckTicksWrap();
	CheckWindowsClock();
	return 0;
}
