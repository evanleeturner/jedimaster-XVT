/* Checks the runtime clock (xvt_runtime/timing/host_clock.h) against the promises in its header: it
 * starts from 0 after Reset, moves only by positive deltas, reports whole milliseconds cut to 32 bits,
 * and answers the game's timeGetTime and GetTickCount from that value. Each case starts from Reset. */
#include "test_assert.h"
#include "xvt/util/time.h"
#include "xvt_runtime/timing/host_clock.h"

#include <stdint.h>

static void check_reset(void)
{
	xvt_time_reset();
	xvt_time_advance_host_clock(123456);
	XVT_ASSERT_TRUE(xvt_time_get_elapsed_us() != 0);
	xvt_time_reset();
	XVT_ASSERT_INT_EQ(xvt_time_get_elapsed_us(), 0);
	XVT_ASSERT_INT_EQ(xvt_time_get_elapsed_ms(), 0);
}

static void check_advance(void)
{
	xvt_time_reset();
	xvt_time_advance_host_clock(250);
	xvt_time_advance_host_clock(750);
	XVT_ASSERT_INT_EQ(xvt_time_get_elapsed_us(), 1000);

	/* Zero and negative deltas are ignored. */
	xvt_time_advance_host_clock(0);
	xvt_time_advance_host_clock(-1);
	xvt_time_advance_host_clock(INT32_MIN);
	XVT_ASSERT_INT_EQ(xvt_time_get_elapsed_us(), 1000);

	xvt_time_advance_host_clock(INT32_MAX);
	XVT_ASSERT_INT_EQ(xvt_time_get_elapsed_us(),
			  1000 + (uint64_t)INT32_MAX);
}

static void check_whole_milliseconds(void)
{
	xvt_time_reset();
	xvt_time_advance_host_clock(999);
	XVT_ASSERT_INT_EQ(xvt_time_get_elapsed_ms(), 0);
	xvt_time_advance_host_clock(1);
	XVT_ASSERT_INT_EQ(xvt_time_get_elapsed_ms(), 1);
	xvt_time_advance_host_clock(1999);
	XVT_ASSERT_INT_EQ(xvt_time_get_elapsed_ms(), 2);
}

static void check_ticks_wrap(void)
{
	xvt_time_reset();
	/* 2^32 milliseconds plus 5, in steps of 2^31 - 1 microseconds and a last partial step. */
	const uint64_t target = (UINT64_C(1) << 32) * 1000 + 5000;
	uint64_t added = 0;
	while (target - added > (uint64_t)INT32_MAX) {
		xvt_time_advance_host_clock(INT32_MAX);
		added += (uint64_t)INT32_MAX;
	}
	xvt_time_advance_host_clock((int32_t)(target - added));
	/* The microsecond count does not wrap; the millisecond count does. */
	XVT_ASSERT_INT_EQ(xvt_time_get_elapsed_us(), target);
	XVT_ASSERT_INT_EQ(xvt_time_get_elapsed_ms(), 5);
}

static void check_windows_clock(void)
{
	xvt_time_reset();
	xvt_time_advance_host_clock(42 * 1000 + 300);
	XVT_ASSERT_INT_EQ(timeGetTime(), xvt_time_get_elapsed_ms());
	XVT_ASSERT_INT_EQ(GetTickCount(), xvt_time_get_elapsed_ms());
	XVT_ASSERT_INT_EQ(timeGetTime(), 42);
}

int main(void)
{
	check_reset();
	check_advance();
	check_whole_milliseconds();
	check_ticks_wrap();
	check_windows_clock();
	return 0;
}
