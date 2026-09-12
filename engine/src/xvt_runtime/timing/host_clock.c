#include "xvt_runtime/timing/host_clock.h"

#include "xvt/util/time.h"

static uint64_t g_xvtElapsedUs;

void XvtTime_Reset(void) { g_xvtElapsedUs = 0; }

void XvtTime_AdvanceHostClock(int32_t delta_us) {
	if (delta_us > 0) {
		g_xvtElapsedUs += (uint32_t)delta_us;
	}
}

uint64_t XvtTime_GetElapsedUs(void) { return g_xvtElapsedUs; }

uint32_t XvtTime_GetElapsedTicks(void) { return (uint32_t)(g_xvtElapsedUs / 1000u); }

uint32_t timeGetTime(void) { return XvtTime_GetElapsedTicks(); }

uint32_t GetTickCount(void) { return XvtTime_GetElapsedTicks(); }
