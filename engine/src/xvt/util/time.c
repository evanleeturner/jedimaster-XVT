#include "xvt/util/time.h"

/* Real time in ms, from timeGetTime, of the last whole 4 ms tick
 * time_consume_elapsed_ticks has handed out; 0 at program start and after a
 * reset, until the next call. time_consume_elapsed_ticks advances it by 4 ms
 * per tick it returns; time_reset_elapsed_ticks sets it to 0 at flight start
 * (xvt_flight_loading_globals). xvt_flight_time_delay_for_ticks also reads
 * it. */
// GLOBAL: XVT 0x5280E4
uint32_t g_last_tick_time_ms;

/* Sets g_last_tick_time_ms to 0, so the next time_consume_elapsed_ticks counts from
 * its own call and returns 0. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AC750
void time_reset_elapsed_ticks(void) { g_last_tick_time_ms = 0; }

/* Returns the whole 4 ms ticks of real time since g_last_tick_time_ms:
 * (now - g_last_tick_time_ms) >> 2, with now from timeGetTime, and advances
 * g_last_tick_time_ms by 4 times that, so the remainder under 4 ms carries into
 * the next call. When g_last_tick_time_ms is 0 it sets it to now and returns 0.
 * The subtraction is unsigned, so a wrap of timeGetTime between calls still
 * gives the right count. */
// FUNCTION: XVT 0x4AC760
uint32_t time_consume_elapsed_ticks(void)
{
	uint32_t time = timeGetTime();
	uint32_t last_tick_time_ms = g_last_tick_time_ms;
	if (last_tick_time_ms == 0) {
		last_tick_time_ms = time;
	}
	uint32_t delta_ticks = (time - last_tick_time_ms) >> 2;
	g_last_tick_time_ms = last_tick_time_ms + 4 * delta_ticks;
	return delta_ticks;
}
