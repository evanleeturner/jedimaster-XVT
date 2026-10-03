/* Checks flight timing (xvt_runtime/timing/flight_timing.h) against the promises in its header: what
 * each profile reports, when reference logic is due, the step clock EnterReference installs and
 * RestoreClock puts back, the animation update serials, and the state a network restore rebuilds. The
 * step globals it reads (g_elapsed_ticks, g_sim_steps_per_second, g_game_time, g_net_update_interval_ticks and the
 * crew mesh timer) are set by each case; every case starts a fresh session. The dropped-period count is not
 * readable through the header, so it is not checked. */
#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/net/flight_net.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/timing/flight_timing.h"

#include <stdint.h>

static void begin(xvt_flight_timing_profile profile)
{
	g_elapsed_ticks = 0;
	g_sim_steps_per_second = 0;
	g_game_time = 0;
	g_net_update_interval_ticks = 0;
	g_flight_global_countdown_timers.special_behavior_update_timer = 0;
	xvt_flight_timing_begin_session(profile);
}

static void check_profiles(void)
{
	begin(XVT_FLIGHT_TIMING_NATIVE);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_session_profile(),
			  XVT_FLIGHT_TIMING_NATIVE);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_is_unlocked(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_is_network125(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_step_ticks(),
			  XVT_NATIVE_STEP_TICKS);

	begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_session_profile(),
			  XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_is_unlocked(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_is_network125(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_step_ticks(),
			  XVT_OFFLINE_STEP_TICKS);

	begin(XVT_FLIGHT_TIMING_NETWORK_125);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_session_profile(),
			  XVT_FLIGHT_TIMING_NETWORK_125);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_is_unlocked(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_is_network125(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_step_ticks(),
			  XVT_NETWORK_STEP_TICKS);

	/* EndSession reads as the native profile: locked, and reference logic always due. */
	xvt_flight_timing_end_session();
	XVT_ASSERT_INT_EQ(xvt_flight_timing_session_profile(),
			  XVT_FLIGHT_TIMING_NATIVE);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_is_unlocked(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_is_network125(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 1);
}

static void check_simulation_maximum(void)
{
	begin(XVT_FLIGHT_TIMING_NETWORK_125);
	g_net_update_interval_ticks = 40;
	XVT_ASSERT_INT_EQ(xvt_flight_timing_maximum_step_ticks(),
			  XVT_NETWORK_STEP_TICKS);
	begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_net_update_interval_ticks = 40;
	XVT_ASSERT_INT_EQ(xvt_flight_timing_maximum_step_ticks(), 40);
	begin(XVT_FLIGHT_TIMING_NATIVE);
	g_net_update_interval_ticks = 17;
	XVT_ASSERT_INT_EQ(xvt_flight_timing_maximum_step_ticks(), 17);
}

static void check_locked_always_due(void)
{
	begin(XVT_FLIGHT_TIMING_NATIVE);
	g_elapsed_ticks = 5;
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 1);
	xvt_flight_timing_begin_advance(1);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_elapsed(), 5);
	xvt_flight_timing_end_advance();
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 1);
	xvt_flight_timing_begin_advance(0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 1);
	g_elapsed_ticks = 9;
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_elapsed(), 9);
}

static void check_unlocked_reference_steps(void)
{
	begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsed_ticks = 3;
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 0);

	/* A step that ends just short of a reference boundary is not a reference step. */
	xvt_flight_timing_begin_advance(XVT_REFERENCE_TICKS - 1);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_elapsed(), 0);
	xvt_flight_timing_end_advance();

	/* The next tick reaches the boundary: a reference step, which lasts until EndAdvance. */
	xvt_flight_timing_begin_advance(1);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_elapsed(),
			  XVT_REFERENCE_TICKS);
	xvt_flight_timing_end_advance();
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_elapsed(), 0);

	/* Steps of 3 ticks: over 40 steps, 120 ticks cross 15 boundaries, one reference step each, since no
	 * step is long enough to cross two. */
	begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	int due = 0;
	for (int i = 0; i < 40; ++i) {
		xvt_flight_timing_begin_advance(3);
		due += xvt_flight_timing_reference_due();
		xvt_flight_timing_end_advance();
	}
	XVT_ASSERT_INT_EQ(due, 120 / XVT_REFERENCE_TICKS);

	/* A step of several periods is one reference step. */
	begin(XVT_FLIGHT_TIMING_NETWORK_125);
	xvt_flight_timing_begin_advance(5 * XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_elapsed(),
			  XVT_REFERENCE_TICKS);
}

static void check_zero_step_opens_none(void)
{
	begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_flight_timing_begin_advance(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_advance_serial(), 1);
	/* A zero step opens nothing and is not counted. */
	xvt_flight_timing_begin_advance(0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_advance_serial(), 1);
}

static void check_advance_serial(void)
{
	begin(XVT_FLIGHT_TIMING_NATIVE);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_advance_serial(), 0);
	for (int i = 0; i < 5; ++i) {
		xvt_flight_timing_begin_advance((uint16_t)(i + 1));
		xvt_flight_timing_end_advance();
	}
	XVT_ASSERT_INT_EQ(xvt_flight_timing_advance_serial(), 5);

	/* A new session starts the count again. */
	begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_advance_serial(), 0);
	xvt_flight_timing_begin_advance(2);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_advance_serial(), 1);
	xvt_flight_timing_end_session();
	XVT_ASSERT_INT_EQ(xvt_flight_timing_advance_serial(), 0);
}

static void check_enter_reference(void)
{
	/* Unlocked: the step globals describe one reference period until RestoreClock. */
	begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_elapsed_ticks = 3;
	g_sim_steps_per_second = 77;
	struct xvt_flight_clock saved = xvt_flight_timing_enter_reference();
	XVT_ASSERT_INT_EQ(saved.elapsed, 3);
	XVT_ASSERT_INT_EQ(saved.steps_per_second, 77);
	XVT_ASSERT_INT_EQ(g_elapsed_ticks, XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(g_sim_steps_per_second,
			  SIMULATION_TICKS_PER_SECOND / XVT_REFERENCE_TICKS);
	xvt_flight_timing_restore_clock(saved);
	XVT_ASSERT_INT_EQ(g_elapsed_ticks, 3);
	XVT_ASSERT_INT_EQ(g_sim_steps_per_second, 77);

	/* It does not check ReferenceDue: outside a reference step it still installs the period. */
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 0);
	saved = xvt_flight_timing_enter_reference();
	XVT_ASSERT_INT_EQ(g_elapsed_ticks, XVT_REFERENCE_TICKS);
	xvt_flight_timing_restore_clock(saved);

	/* Locked: nothing changes. */
	begin(XVT_FLIGHT_TIMING_NATIVE);
	g_elapsed_ticks = 11;
	g_sim_steps_per_second = 21;
	saved = xvt_flight_timing_enter_reference();
	XVT_ASSERT_INT_EQ(saved.elapsed, 11);
	XVT_ASSERT_INT_EQ(saved.steps_per_second, 21);
	XVT_ASSERT_INT_EQ(g_elapsed_ticks, 11);
	XVT_ASSERT_INT_EQ(g_sim_steps_per_second, 21);
}

static void check_animation_event(void)
{
	begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_animation_serial(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_animation_time(), 0);
	g_game_time = 100;
	g_elapsed_ticks = 5;
	xvt_flight_timing_animation_event();
	XVT_ASSERT_INT_EQ(xvt_flight_timing_animation_time(), 105);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_animation_serial(), 1);
	g_game_time = 500;
	xvt_flight_timing_animation_event();
	XVT_ASSERT_INT_EQ(xvt_flight_timing_animation_time(), 505);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_animation_serial(), 2);

	/* In NETWORK_125 the serial follows the time. */
	begin(XVT_FLIGHT_TIMING_NETWORK_125);
	g_game_time = 3 * XVT_COMPONENT_EVENT_TICKS + 6;
	g_elapsed_ticks = XVT_NETWORK_STEP_TICKS;
	xvt_flight_timing_animation_event();
	XVT_ASSERT_INT_EQ(xvt_flight_timing_animation_time(),
			  3 * XVT_COMPONENT_EVENT_TICKS + 6 +
				  XVT_NETWORK_STEP_TICKS);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_animation_serial(), 3 + 1);
	xvt_flight_timing_animation_event();
	XVT_ASSERT_INT_EQ(xvt_flight_timing_animation_serial(), 3 + 1);
}

/* Restores at tick, then reports whether a step of `step` ticks is a reference step. */
static int due_after_restore(int tick, uint16_t step)
{
	begin(XVT_FLIGHT_TIMING_NETWORK_125);
	xvt_flight_timing_restore_network_tick(tick);
	xvt_flight_timing_begin_advance(step);
	return xvt_flight_timing_reference_due();
}

static void check_restore_network_tick(void)
{
	begin(XVT_FLIGHT_TIMING_NETWORK_125);
	xvt_flight_timing_restore_network_tick(1000);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_advance_serial(),
			  1000 / XVT_NETWORK_STEP_TICKS);

	/* The reference phase becomes tick % XVT_REFERENCE_TICKS: from tick 1002 the boundary is 6 ticks on. */
	const int tick = 1000 + XVT_NETWORK_STEP_TICKS;
	const int to_boundary =
		XVT_REFERENCE_TICKS - tick % XVT_REFERENCE_TICKS;
	XVT_ASSERT_INT_EQ(due_after_restore(tick, (uint16_t)(to_boundary - 1)),
			  0);
	XVT_ASSERT_INT_EQ(due_after_restore(tick, (uint16_t)to_boundary), 1);

	/* It closes an open step. */
	begin(XVT_FLIGHT_TIMING_NETWORK_125);
	xvt_flight_timing_begin_advance(XVT_REFERENCE_TICKS);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 1);
	xvt_flight_timing_restore_network_tick(2000);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 0);

	/* Refused: a negative tick, a tick that is not a multiple of the network step, any other profile. */
	begin(XVT_FLIGHT_TIMING_NETWORK_125);
	xvt_flight_timing_begin_advance(2);
	xvt_flight_timing_restore_network_tick(-XVT_NETWORK_STEP_TICKS);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_advance_serial(), 1);
	xvt_flight_timing_restore_network_tick(1001);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_advance_serial(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 0);
	xvt_flight_timing_begin_advance(XVT_REFERENCE_TICKS);
	xvt_flight_timing_restore_network_tick(1001);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 1);
	begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_flight_timing_restore_network_tick(1000);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_advance_serial(), 0);
	begin(XVT_FLIGHT_TIMING_NATIVE);
	xvt_flight_timing_restore_network_tick(1000);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_advance_serial(), 0);
}

static void check_restored_animation(void)
{
	/* The rebuilt animation update uses AnimationEvent's network formula, and is all zero when its time is
	 * not positive. The pairs cover early ticks, where it is not, and later ones, where it is. */
	static const int ticks[] = {0, 2, 8, 30, 64, 1000, 123456};
	static const uint16_t timers[] = {0, 1, 28, 29, 200};
	for (unsigned i = 0; i < sizeof ticks / sizeof ticks[0]; ++i) {
		for (unsigned j = 0; j < sizeof timers / sizeof timers[0];
		     ++j) {
			begin(XVT_FLIGHT_TIMING_NETWORK_125);
			g_flight_global_countdown_timers
				.special_behavior_update_timer = timers[j];
			xvt_flight_timing_restore_network_tick(ticks[i]);
			int time = xvt_flight_timing_animation_time();
			XVT_ASSERT_TRUE(time >= 0);
			if (time > 0) {
				XVT_ASSERT_INT_EQ(
					xvt_flight_timing_animation_serial(),
					time / XVT_COMPONENT_EVENT_TICKS + 1);
			} else {
				XVT_ASSERT_INT_EQ(
					xvt_flight_timing_animation_serial(),
					0);
			}
		}
	}
}

static void check_session_clears_state(void)
{
	begin(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	g_game_time = 40;
	g_elapsed_ticks = 1;
	xvt_flight_timing_begin_advance(XVT_REFERENCE_TICKS - 1);
	xvt_flight_timing_animation_event();
	xvt_flight_timing_end_advance();

	/* A new session forgets the reference phase, the serials and the animation update. */
	xvt_flight_timing_begin_session(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_advance_serial(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_animation_serial(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_animation_time(), 0);
	xvt_flight_timing_begin_advance(1);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_reference_due(), 0);
	xvt_flight_timing_end_advance();

	xvt_flight_timing_animation_event();
	xvt_flight_timing_end_session();
	XVT_ASSERT_INT_EQ(xvt_flight_timing_animation_serial(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_animation_time(), 0);
}

int main(void)
{
	check_profiles();
	check_simulation_maximum();
	check_locked_always_due();
	check_unlocked_reference_steps();
	check_zero_step_opens_none();
	check_advance_serial();
	check_enter_reference();
	check_animation_event();
	check_restore_network_tick();
	check_restored_animation();
	check_session_clears_state();
	xvt_flight_timing_end_session();
	return 0;
}
