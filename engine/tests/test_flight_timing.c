/* Checks flight timing (xvt_runtime/timing/flight_timing.h) against the promises in its header: what
 * each profile reports, when reference logic is due, the step clock EnterReference installs and
 * RestoreClock puts back, the animation update serials, and the state a network restore rebuilds. The
 * step globals it reads (g_elapsedTicks, g_simStepScale, g_gameTime, g_netUpdateIntervalTicks and the crew
 * mesh timer) are set by each case; every case starts a fresh session. The dropped-period count is not
 * readable through the header, so it is not checked. */
#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/net/flight_net.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/timing/flight_timing.h"

#include <stdint.h>

static void Begin(XvtFlightTimingProfile profile) {
	g_elapsedTicks = 0;
	g_simStepScale = 0;
	g_gameTime = 0;
	g_netUpdateIntervalTicks = 0;
	g_flightGlobalCountdownTimers.crewMeshRotationUpdateTimer = 0;
	XvtFlightTiming_BeginSession(profile);
}

static void CheckProfiles(void) {
	Begin(XVT_FLIGHT_TIMING_NATIVE);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_Profile(), XVT_FLIGHT_TIMING_NATIVE);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_IsUnlocked(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_IsNetwork125(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_StepTicks(), XVT_NATIVE_STEP_TICKS);

	Begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_Profile(), XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_IsUnlocked(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_IsNetwork125(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_StepTicks(), XVT_OFFLINE_STEP_TICKS);

	Begin(XVT_FLIGHT_TIMING_NETWORK_125);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_Profile(), XVT_FLIGHT_TIMING_NETWORK_125);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_IsUnlocked(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_IsNetwork125(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_StepTicks(), XVT_NETWORK_STEP_TICKS);

	/* EndSession reads as the native profile: locked, and reference logic always due. */
	XvtFlightTiming_EndSession();
	XVT_ASSERT_INT_EQ(XvtFlightTiming_Profile(), XVT_FLIGHT_TIMING_NATIVE);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_IsUnlocked(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_IsNetwork125(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 1);
}

static void CheckSimulationMaximum(void) {
	Begin(XVT_FLIGHT_TIMING_NETWORK_125);
	g_netUpdateIntervalTicks = 40;
	XVT_ASSERT_INT_EQ(XvtFlightTiming_SimulationMaximum(), XVT_NETWORK_STEP_TICKS);
	Begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_netUpdateIntervalTicks = 40;
	XVT_ASSERT_INT_EQ(XvtFlightTiming_SimulationMaximum(), 40);
	Begin(XVT_FLIGHT_TIMING_NATIVE);
	g_netUpdateIntervalTicks = 17;
	XVT_ASSERT_INT_EQ(XvtFlightTiming_SimulationMaximum(), 17);
}

static void CheckLockedAlwaysDue(void) {
	Begin(XVT_FLIGHT_TIMING_NATIVE);
	g_elapsedTicks = 5;
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 1);
	XvtFlightTiming_BeginAdvance(1);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceElapsed(), 5);
	XvtFlightTiming_EndAdvance();
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 1);
	XvtFlightTiming_BeginAdvance(0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 1);
	g_elapsedTicks = 9;
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceElapsed(), 9);
}

static void CheckUnlockedReferenceSteps(void) {
	Begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsedTicks = 3;
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 0);

	/* A step that ends just short of a reference boundary is not a reference step. */
	XvtFlightTiming_BeginAdvance(XVT_REFERENCE_TICKS - 1);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceElapsed(), 0);
	XvtFlightTiming_EndAdvance();

	/* The next tick reaches the boundary: a reference step, which lasts until EndAdvance. */
	XvtFlightTiming_BeginAdvance(1);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceElapsed(), XVT_REFERENCE_TICKS);
	XvtFlightTiming_EndAdvance();
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceElapsed(), 0);

	/* Steps of 3 ticks: over 40 steps, 120 ticks cross 15 boundaries, one reference step each, since no
	 * step is long enough to cross two. */
	Begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	int due = 0;
	for (int i = 0; i < 40; ++i) {
		XvtFlightTiming_BeginAdvance(3);
		due += XvtFlightTiming_ReferenceDue();
		XvtFlightTiming_EndAdvance();
	}
	XVT_ASSERT_INT_EQ(due, 120 / XVT_REFERENCE_TICKS);

	/* A step of several periods is one reference step. */
	Begin(XVT_FLIGHT_TIMING_NETWORK_125);
	XvtFlightTiming_BeginAdvance(5 * XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceElapsed(), XVT_REFERENCE_TICKS);
}

static void CheckZeroStepOpensNone(void) {
	Begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtFlightTiming_BeginAdvance(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AdvanceSerial(), 1);
	/* A zero step opens nothing and is not counted. */
	XvtFlightTiming_BeginAdvance(0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AdvanceSerial(), 1);
}

static void CheckAdvanceSerial(void) {
	Begin(XVT_FLIGHT_TIMING_NATIVE);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AdvanceSerial(), 0);
	for (int i = 0; i < 5; ++i) {
		XvtFlightTiming_BeginAdvance((uint16_t)(i + 1));
		XvtFlightTiming_EndAdvance();
	}
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AdvanceSerial(), 5);

	/* A new session starts the count again. */
	Begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AdvanceSerial(), 0);
	XvtFlightTiming_BeginAdvance(2);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AdvanceSerial(), 1);
	XvtFlightTiming_EndSession();
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AdvanceSerial(), 0);
}

static void CheckEnterReference(void) {
	/* Unlocked: the step globals describe one reference period until RestoreClock. */
	Begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsedTicks = 3;
	g_simStepScale = 77;
	XvtFlightClock saved = XvtFlightTiming_EnterReference();
	XVT_ASSERT_INT_EQ(saved.elapsed, 3);
	XVT_ASSERT_INT_EQ(saved.scale, 77);
	XVT_ASSERT_INT_EQ(g_elapsedTicks, XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(g_simStepScale, SIMULATION_TICKS_PER_SECOND / XVT_REFERENCE_TICKS);
	XvtFlightTiming_RestoreClock(saved);
	XVT_ASSERT_INT_EQ(g_elapsedTicks, 3);
	XVT_ASSERT_INT_EQ(g_simStepScale, 77);

	/* It does not check ReferenceDue: outside a reference step it still installs the period. */
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 0);
	saved = XvtFlightTiming_EnterReference();
	XVT_ASSERT_INT_EQ(g_elapsedTicks, XVT_REFERENCE_TICKS);
	XvtFlightTiming_RestoreClock(saved);

	/* Locked: nothing changes. */
	Begin(XVT_FLIGHT_TIMING_NATIVE);
	g_elapsedTicks = 11;
	g_simStepScale = 21;
	saved = XvtFlightTiming_EnterReference();
	XVT_ASSERT_INT_EQ(saved.elapsed, 11);
	XVT_ASSERT_INT_EQ(saved.scale, 21);
	XVT_ASSERT_INT_EQ(g_elapsedTicks, 11);
	XVT_ASSERT_INT_EQ(g_simStepScale, 21);
}

static void CheckAnimationEvent(void) {
	Begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationSerial(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationTime(), 0);
	g_gameTime = 100;
	g_elapsedTicks = 5;
	XvtFlightTiming_AnimationEvent();
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationTime(), 105);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationSerial(), 1);
	g_gameTime = 500;
	XvtFlightTiming_AnimationEvent();
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationTime(), 505);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationSerial(), 2);

	/* In NETWORK_125 the serial follows the time. */
	Begin(XVT_FLIGHT_TIMING_NETWORK_125);
	g_gameTime = 3 * XVT_COMPONENT_EVENT_TICKS + 6;
	g_elapsedTicks = XVT_NETWORK_STEP_TICKS;
	XvtFlightTiming_AnimationEvent();
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationTime(),
					  3 * XVT_COMPONENT_EVENT_TICKS + 6 + XVT_NETWORK_STEP_TICKS);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationSerial(), 3 + 1);
	XvtFlightTiming_AnimationEvent();
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationSerial(), 3 + 1);
}

/* Restores at tick, then reports whether a step of `step` ticks is a reference step. */
static int DueAfterRestore(int tick, uint16_t step) {
	Begin(XVT_FLIGHT_TIMING_NETWORK_125);
	XvtFlightTiming_RestoreNetworkTick(tick);
	XvtFlightTiming_BeginAdvance(step);
	return XvtFlightTiming_ReferenceDue();
}

static void CheckRestoreNetworkTick(void) {
	Begin(XVT_FLIGHT_TIMING_NETWORK_125);
	XvtFlightTiming_RestoreNetworkTick(1000);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AdvanceSerial(), 1000 / XVT_NETWORK_STEP_TICKS);

	/* The reference phase becomes tick % XVT_REFERENCE_TICKS: from tick 1002 the boundary is 6 ticks on. */
	const int tick = 1000 + XVT_NETWORK_STEP_TICKS;
	const int to_boundary = XVT_REFERENCE_TICKS - tick % XVT_REFERENCE_TICKS;
	XVT_ASSERT_INT_EQ(DueAfterRestore(tick, (uint16_t)(to_boundary - 1)), 0);
	XVT_ASSERT_INT_EQ(DueAfterRestore(tick, (uint16_t)to_boundary), 1);

	/* It closes an open step. */
	Begin(XVT_FLIGHT_TIMING_NETWORK_125);
	XvtFlightTiming_BeginAdvance(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 1);
	XvtFlightTiming_RestoreNetworkTick(2000);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 0);

	/* Refused: a negative tick, a tick that is not a multiple of the network step, any other profile. */
	Begin(XVT_FLIGHT_TIMING_NETWORK_125);
	XvtFlightTiming_BeginAdvance(2);
	XvtFlightTiming_RestoreNetworkTick(-XVT_NETWORK_STEP_TICKS);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AdvanceSerial(), 1);
	XvtFlightTiming_RestoreNetworkTick(1001);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AdvanceSerial(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 0);
	XvtFlightTiming_BeginAdvance(XVT_REFERENCE_TICKS);
	XvtFlightTiming_RestoreNetworkTick(1001);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 1);
	Begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XvtFlightTiming_RestoreNetworkTick(1000);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AdvanceSerial(), 0);
	Begin(XVT_FLIGHT_TIMING_NATIVE);
	XvtFlightTiming_RestoreNetworkTick(1000);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AdvanceSerial(), 0);
}

static void CheckRestoredAnimation(void) {
	/* The rebuilt animation update uses AnimationEvent's network formula, and is all zero when its time is
	 * not positive. The pairs cover early ticks, where it is not, and later ones, where it is. */
	static const int ticks[] = { 0, 2, 8, 30, 64, 1000, 123456 };
	static const uint16_t timers[] = { 0, 1, 28, 29, 200 };
	for (unsigned i = 0; i < sizeof ticks / sizeof ticks[0]; ++i) {
		for (unsigned j = 0; j < sizeof timers / sizeof timers[0]; ++j) {
			Begin(XVT_FLIGHT_TIMING_NETWORK_125);
			g_flightGlobalCountdownTimers.crewMeshRotationUpdateTimer = timers[j];
			XvtFlightTiming_RestoreNetworkTick(ticks[i]);
			int time = XvtFlightTiming_AnimationTime();
			XVT_ASSERT_TRUE(time >= 0);
			if (time > 0)
				XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationSerial(), time / XVT_COMPONENT_EVENT_TICKS + 1);
			else
				XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationSerial(), 0);
		}
	}
}

static void CheckSessionClearsState(void) {
	Begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_gameTime = 40;
	g_elapsedTicks = 1;
	XvtFlightTiming_BeginAdvance(XVT_REFERENCE_TICKS - 1);
	XvtFlightTiming_AnimationEvent();
	XvtFlightTiming_EndAdvance();

	/* A new session forgets the reference phase, the serials and the animation update. */
	XvtFlightTiming_BeginSession(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AdvanceSerial(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationSerial(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationTime(), 0);
	XvtFlightTiming_BeginAdvance(1);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_ReferenceDue(), 0);
	XvtFlightTiming_EndAdvance();

	XvtFlightTiming_AnimationEvent();
	XvtFlightTiming_EndSession();
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationSerial(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_AnimationTime(), 0);
}

int main(void) {
	CheckProfiles();
	CheckSimulationMaximum();
	CheckLockedAlwaysDue();
	CheckUnlockedReferenceSteps();
	CheckZeroStepOpensNone();
	CheckAdvanceSerial();
	CheckEnterReference();
	CheckAnimationEvent();
	CheckRestoreNetworkTick();
	CheckRestoredAnimation();
	CheckSessionClearsState();
	XvtFlightTiming_EndSession();
	return 0;
}
