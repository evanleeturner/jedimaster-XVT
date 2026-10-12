/* The game's tick, the unit every flight clock counts in.
 *
 * One tick is 4 ms: 250 ticks to a real second, at any frame rate.
 * time_consume_elapsed_ticks below makes them by cutting timeGetTime's
 * milliseconds into whole 4 ms pieces and carrying the rest into the next
 * call, so no time is lost between frames.
 *
 * Two different "seconds" are counted in ticks. Do not mix them up:
 *
 *   250 ticks  one real second (this file).
 *   236 ticks  one simulated second (SIMULATION_TICKS_PER_SECOND,
 *              xvt/flight/flight.h): the second in speeds, acceleration,
 *              shot lifetimes and the flight's timers. It lasts 944 ms,
 *              so the flight runs about 6% faster than its per-second
 *              figures say.
 *
 * Both numbers come from the 1997 game, and this port keeps them.
 *
 * Where the time comes from in this port: the frame timer
 * (Aeron_BeginFrame, on SDL's clock) measures each frame and clamps the gap
 * to 250 ms; xvt_port_update adds it to the host clock
 * (xvt_runtime/timing/host_clock.c), whose whole milliseconds timeGetTime
 * returns. A frame that takes longer than 250 ms loses the rest. Outside
 * network play the host clock does not move while the settings menu is open
 * or the window is out of focus, so ticks stop too.
 *
 * Each simulation step covers some ticks: 2 (8 ms, 125 steps a second) in
 * network play; 1 in a solo flight with the unlocked setting on; in a solo
 * flight with it off, at least 8 (32 ms), following the recent frame length.
 * Logic written for the 1997 step runs once per 8-tick reference period.
 *
 * Measured without a game: the host clock and this file, driven with frames
 * from 1 microsecond to 1 second long, gave 15,000 ticks in 60 seconds for
 * every frame length up to 250 ms. The full account, with every tick length
 * the engine names: TICK.md at the top of the repository. */

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
