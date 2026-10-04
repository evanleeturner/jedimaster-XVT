#include "xvt_runtime/timing/flight_timing.h"

#include <string.h>

#include "aeron/aeron.h"
#include "xvt/flight/flight.h"
#include "xvt/net/flight_net.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_protocol.h"

static struct {
	xvt_flight_timing_profile profile;
	int unlocked, active, due, animation_time;
	unsigned phase;
	uint64_t serial, animation_serial, dropped, reported;
} g_timing;

void xvt_flight_timing_begin_session(xvt_flight_timing_profile profile)
{
	memset(&g_timing, 0, sizeof g_timing);
	g_timing.profile = profile;
	g_timing.unlocked = profile != XVT_FLIGHT_TIMING_NATIVE;
	XVT_LOG_INFO("timing.profile mode=\"%s\"",
		     profile == XVT_FLIGHT_TIMING_NETWORK_125 ? "network125"
		     : g_timing.unlocked		      ? "unlocked"
							      : "native");
}

void xvt_flight_timing_end_session(void)
{
	memset(&g_timing, 0, sizeof g_timing);
}

int xvt_flight_timing_is_unlocked(void) { return g_timing.unlocked; }

unsigned xvt_flight_timing_step_ticks(void)
{
	return xvt_flight_timing_is_network125() ? XVT_NETWORK_STEP_TICKS
	       : g_timing.unlocked		 ? XVT_OFFLINE_STEP_TICKS
						 : XVT_NATIVE_STEP_TICKS;
}

void xvt_flight_timing_begin_advance(uint16_t elapsed)
{
	g_timing.active = elapsed != 0;
	g_timing.due = 0;
	if (!elapsed) {
		return;
	}
	++g_timing.serial;
	if (!g_timing.unlocked) {
		return;
	}
	unsigned total = g_timing.phase + elapsed;
	g_timing.phase = total % XVT_REFERENCE_TICKS;
	g_timing.due = total >= XVT_REFERENCE_TICKS;
	if (g_timing.due) {
		g_timing.dropped += total / XVT_REFERENCE_TICKS - 1;
	}
	/* Report sustained overload without logging every host frame. */
	if (g_timing.dropped - g_timing.reported >= 256) {
		XVT_LOG_WARN("timing.periods_dropped count=%llu",
			     (unsigned long long)g_timing.dropped);
		g_timing.reported = g_timing.dropped;
	}
}

void xvt_flight_timing_end_advance(void) { g_timing.active = g_timing.due = 0; }

int xvt_flight_timing_reference_due(void)
{
	return !g_timing.unlocked || (g_timing.active && g_timing.due);
}

uint16_t xvt_flight_timing_reference_elapsed(void)
{
	return !g_timing.unlocked		   ? g_elapsed_ticks
	       : xvt_flight_timing_reference_due() ? XVT_REFERENCE_TICKS
						   : 0;
}

uint64_t xvt_flight_timing_advance_serial(void) { return g_timing.serial; }

struct xvt_flight_clock xvt_flight_timing_enter_reference(void)
{
	struct xvt_flight_clock saved = {g_elapsed_ticks,
					 g_sim_steps_per_second};
	if (g_timing.unlocked) {
		g_elapsed_ticks = XVT_REFERENCE_TICKS;
		g_sim_steps_per_second =
			SIMULATION_TICKS_PER_SECOND / XVT_REFERENCE_TICKS;
	}
	return saved;
}

void xvt_flight_timing_restore_clock(struct xvt_flight_clock saved)
{
	g_elapsed_ticks = saved.elapsed;
	g_sim_steps_per_second = saved.steps_per_second;
}

void xvt_flight_timing_animation_event(void)
{
	g_timing.animation_time = g_game_time + g_elapsed_ticks;
	if (xvt_flight_timing_is_network125()) {
		g_timing.animation_serial = (unsigned)g_timing.animation_time /
						    XVT_COMPONENT_EVENT_TICKS +
					    1;
	} else {
		++g_timing.animation_serial;
	}
}

uint64_t xvt_flight_timing_animation_serial(void)
{
	return g_timing.animation_serial;
}

int xvt_flight_timing_animation_time(void) { return g_timing.animation_time; }

xvt_flight_timing_profile xvt_flight_timing_session_profile(void)
{
	return g_timing.profile;
}

int xvt_flight_timing_is_network125(void)
{
	return g_timing.profile == XVT_FLIGHT_TIMING_NETWORK_125;
}

int xvt_flight_timing_maximum_step_ticks(void)
{
	return xvt_flight_timing_is_network125() ? XVT_NETWORK_STEP_TICKS
						 : g_net_update_interval_ticks;
}

void xvt_flight_timing_restore_network_tick(int tick)
{
	if (!xvt_flight_timing_is_network125() || tick < 0 ||
	    (tick % XVT_NETWORK_STEP_TICKS)) {
		return;
	}
	g_timing.serial = (unsigned)tick / XVT_NETWORK_STEP_TICKS;
	g_timing.phase = (unsigned)tick % XVT_REFERENCE_TICKS;
	g_timing.active = g_timing.due = 0;
	int timer =
		g_flight_global_countdown_timers.special_behavior_update_timer;
	int event = (tick - tick % XVT_REFERENCE_TICKS) -
		    (XVT_COMPONENT_TIMER_TICKS - timer);
	g_timing.animation_time = event > 0 ? event : 0;
	g_timing.animation_serial =
		event > 0 ? (unsigned)event / XVT_COMPONENT_EVENT_TICKS + 1 : 0;
}
