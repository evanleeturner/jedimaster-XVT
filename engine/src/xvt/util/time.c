#include "xvt/util/time.h"

// GLOBAL: XVT 0x5280E4
uint32_t g_lastTickTimeMs;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AC750
void Time_ResetElapsedTicks(void) { g_lastTickTimeMs = 0; }

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
