#include "xvt/util/time.h"

/* Real time in ms, from timeGetTime, of the last whole 4 ms tick
 * Time_ConsumeElapsedTicks has handed out; 0 at program start and after a
 * reset, until the next call. Time_ConsumeElapsedTicks advances it by 4 ms per
 * tick it returns; Time_ResetElapsedTicks sets it to 0 at flight start
 * (Flight_MainLoop in the original build, XvtFlightLoading_Globals in the
 * modern one). The modern build's XvtFlightTime_DelayForTicks also reads it. */
// GLOBAL: XVT 0x5280E4
uint32_t g_lastTickTimeMs;

/* Sets g_lastTickTimeMs to 0, so the next Time_ConsumeElapsedTicks counts from
 * its own call and returns 0. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AC750
void Time_ResetElapsedTicks(void) { g_lastTickTimeMs = 0; }

/* Returns the whole 4 ms ticks of real time since g_lastTickTimeMs:
 * (now - g_lastTickTimeMs) >> 2, with now from timeGetTime, and advances
 * g_lastTickTimeMs by 4 times that, so the remainder under 4 ms carries into
 * the next call. When g_lastTickTimeMs is 0 it sets it to now and returns 0.
 * The subtraction is unsigned, so a wrap of timeGetTime between calls still
 * gives the right count. */
// FUNCTION: XVT 0x4AC760
uint32_t Time_ConsumeElapsedTicks(void)
{
	uint32_t time;
	uint32_t lastTickTimeMs;
	uint32_t deltaTicks;

	time = timeGetTime();
	lastTickTimeMs = g_lastTickTimeMs;
	if (lastTickTimeMs == 0) {
		lastTickTimeMs = time;
	}
	deltaTicks = (time - lastTickTimeMs) >> 2;
	g_lastTickTimeMs = lastTickTimeMs + 4 * deltaTicks;
	return deltaTicks;
}
