#include "xvt_runtime/timing/flight_timing.h"
#include "aeron/aeron.h"
#include "xvt/flight/flight.h"
#include "xvt/net/flight_net.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include <string.h>

static struct {
	XvtFlightTimingProfile profile;
	int unlocked, active, due, animation_time;
	unsigned phase;
	uint64_t serial, animation_serial, dropped, reported;
} g_timing;

void XvtFlightTiming_BeginSession(XvtFlightTimingProfile profile) {
	memset(&g_timing, 0, sizeof g_timing);
	g_timing.profile = profile;
	g_timing.unlocked = profile != XVT_FLIGHT_TIMING_NATIVE;
	Aeron_LogInfo("xvt.flight.timing", "flight timing: %s",
				  profile == XVT_FLIGHT_TIMING_NETWORK_125 ? "network 125 Hz"
				  : g_timing.unlocked                      ? "unlocked"
														   : "native");
}

void XvtFlightTiming_EndSession(void) { memset(&g_timing, 0, sizeof g_timing); }

int XvtFlightTiming_IsUnlocked(void) { return g_timing.unlocked; }

unsigned XvtFlightTiming_StepTicks(void) {
	return XvtFlightTiming_IsNetwork125() ? XVT_NETWORK_STEP_TICKS
		   : g_timing.unlocked            ? XVT_OFFLINE_STEP_TICKS
										  : XVT_NATIVE_STEP_TICKS;
}

void XvtFlightTiming_BeginAdvance(uint16_t elapsed) {
	g_timing.active = elapsed != 0;
	g_timing.due = 0;
	if (!elapsed)
		return;
	++g_timing.serial;
	if (!g_timing.unlocked)
		return;
	unsigned total = g_timing.phase + elapsed;
	g_timing.phase = total % XVT_REFERENCE_TICKS;
	g_timing.due = total >= XVT_REFERENCE_TICKS;
	if (g_timing.due)
		g_timing.dropped += total / XVT_REFERENCE_TICKS - 1;
	/* Report sustained overload without logging every host frame. */
	if (g_timing.dropped - g_timing.reported >= 256) {
		Aeron_LogWarn("xvt.flight.timing", "dropped %llu overdue reference periods",
					  (unsigned long long)g_timing.dropped);
		g_timing.reported = g_timing.dropped;
	}
}

void XvtFlightTiming_EndAdvance(void) { g_timing.active = g_timing.due = 0; }

int XvtFlightTiming_ReferenceDue(void) { return !g_timing.unlocked || (g_timing.active && g_timing.due); }

uint16_t XvtFlightTiming_ReferenceElapsed(void) {
	return !g_timing.unlocked ? g_elapsedTicks : XvtFlightTiming_ReferenceDue() ? XVT_REFERENCE_TICKS : 0;
}

uint64_t XvtFlightTiming_AdvanceSerial(void) { return g_timing.serial; }

XvtFlightClock XvtFlightTiming_EnterReference(void) {
	XvtFlightClock saved = { g_elapsedTicks, g_simStepScale };
	if (g_timing.unlocked) {
		g_elapsedTicks = XVT_REFERENCE_TICKS;
		g_simStepScale = SIMULATION_TICKS_PER_SECOND / XVT_REFERENCE_TICKS;
	}
	return saved;
}

void XvtFlightTiming_RestoreClock(XvtFlightClock saved) {
	g_elapsedTicks = saved.elapsed;
	g_simStepScale = saved.scale;
}

void XvtFlightTiming_AnimationEvent(void) {
	g_timing.animation_time = g_gameTime + g_elapsedTicks;
	if (XvtFlightTiming_IsNetwork125())
		g_timing.animation_serial = (unsigned)g_timing.animation_time / XVT_COMPONENT_EVENT_TICKS + 1;
	else
		++g_timing.animation_serial;
}

uint64_t XvtFlightTiming_AnimationSerial(void) { return g_timing.animation_serial; }

int XvtFlightTiming_AnimationTime(void) { return g_timing.animation_time; }

XvtFlightTimingProfile XvtFlightTiming_Profile(void) { return g_timing.profile; }

int XvtFlightTiming_IsNetwork125(void) { return g_timing.profile == XVT_FLIGHT_TIMING_NETWORK_125; }

int XvtFlightTiming_SimulationMaximum(void) {
	return XvtFlightTiming_IsNetwork125() ? XVT_NETWORK_STEP_TICKS : dtMs;
}

void XvtFlightTiming_RestoreNetworkTick(int tick) {
	if (!XvtFlightTiming_IsNetwork125() || tick < 0 || (tick % XVT_NETWORK_STEP_TICKS))
		return;
	g_timing.serial = (unsigned)tick / XVT_NETWORK_STEP_TICKS;
	g_timing.phase = (unsigned)tick % XVT_REFERENCE_TICKS;
	g_timing.active = g_timing.due = 0;
	int timer = g_flightGlobalCountdownTimers.crewMeshRotationUpdateTimer;
	int event = (tick - tick % XVT_REFERENCE_TICKS) - (XVT_COMPONENT_TIMER_TICKS - timer);
	g_timing.animation_time = event > 0 ? event : 0;
	g_timing.animation_serial = event > 0 ? (unsigned)event / XVT_COMPONENT_EVENT_TICKS + 1 : 0;
}
