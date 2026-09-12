#include "xvt/util/time.h"

// GLOBAL: XVT 0x5280E4
uint32_t g_lastTickTime;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AC750
void Time_ResetFrameDeltaClocks(void) { g_lastTickTime = 0; }

// FUNCTION: XVT 0x4AC760
uint32_t Time_GetFrameDelta(void) {
	uint32_t time;
	uint32_t lastTickTime;
	uint32_t deltaTicks;

	time = timeGetTime();
	lastTickTime = g_lastTickTime;
	if (lastTickTime == 0) {
		lastTickTime = time;
	}
	deltaTicks = (time - lastTickTime) >> 2;
	g_lastTickTime = lastTickTime + 4 * deltaTicks;
	return deltaTicks;
}
