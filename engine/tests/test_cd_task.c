/* Checks the CD audio timing (xvt_runtime/runtime/cd_task.h) against the
 * promises in its header: when a fade starts, the size and spacing of its
 * steps, the steps applied together when several are due, the overshoot and
 * clamping of the last step, Cancel, and outside flight the resume delay and
 * the end of a track. The host clock moves only when the test advances it. The
 * volume a step sets is read back from the CD audio state, where
 * cd_audio_set_aux_volume records the level it sets. Every case starts from a
 * cleared frontend state, the music CD device marked open, no flight, no fade
 * and the clock at 0.
 *
 * Not checked here: replaying a looping track, and anything during a flight;
 * both need a CD device or a running flight. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/music_cd.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/runtime/cd_task.h"
#include "xvt_runtime/timing/host_clock.h"

/* A volume no step can produce: steps move the level from its start in multiples of 256. */
enum { UNSET = 12345 };

static void fresh(void)
{
	xvt_cd_task_cancel_fade();
	memset(&g_front_state, 0, sizeof g_front_state);
	g_music_cd_mci_device_id = 1;
	g_front_state.cd_audio_track_cache.current_aux_volume = UNSET;
	xvt_time_reset();
}

static unsigned volume(void)
{
	return g_front_state.cd_audio_track_cache.current_aux_volume;
}

static void advance_ms(int ms) { xvt_time_advance_host_clock(ms * 1000); }

static void check_begin_needs_a_device(void)
{
	fresh();
	g_music_cd_mci_device_id = 0;
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(0, 2560, 1000), 0);
	XVT_ASSERT_INT_EQ(xvt_cd_task_is_fading(), 0);

	/* Either device will do. */
	g_front_state.cd_audio_mci_device_id = 1;
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(0, 2560, 1000), 1);
	XVT_ASSERT_INT_EQ(xvt_cd_task_is_fading(), 1);
	g_front_state.cd_audio_mci_device_id = 0;
	g_music_cd_mci_device_id = 1;
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(0, 2560, 1000), 1);
	XVT_ASSERT_INT_EQ(xvt_cd_task_is_fading(), 1);
	xvt_cd_task_cancel_fade();
}

static void check_equal_levels(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(1000, 1000, 1000), 1);
	XVT_ASSERT_INT_EQ(xvt_cd_task_is_fading(), 0);
	/* Levels are clamped to 65535 before they are compared. */
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(70000, 65535, 1000), 1);
	XVT_ASSERT_INT_EQ(xvt_cd_task_is_fading(), 0);
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), UINT64_MAX);
	advance_ms(5000);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), UNSET);
}

static void check_step_timing(void)
{
	fresh();
	/* 2560 apart over 1000 ms: a step of 256 every 1000 * 256 / 2560 + 1 = 101 ms, 10 steps. */
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(0, 2560, 1000), 1);
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), 101000);
	/* The volume is not set to from at the start. */
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), UNSET);

	advance_ms(100);
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), 1000);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), UNSET);

	advance_ms(1);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), 256);
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), 101000);

	/* Three steps due at once are applied together. */
	advance_ms(303);
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), 0);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), 4 * 256);
	XVT_ASSERT_INT_EQ(xvt_cd_task_is_fading(), 1);
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), 101000);

	/* Long overdue: the remaining six steps, then the fade is over. */
	advance_ms(10000);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), 2560);
	XVT_ASSERT_INT_EQ(xvt_cd_task_is_fading(), 0);
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), UINT64_MAX);
}

static void check_non_positive_duration(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(0, 512, 0), 1);
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), 1000);
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(0, 512, -50), 1);
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), 1000);
	advance_ms(1);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), 256);
	advance_ms(1);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), 512);
	XVT_ASSERT_INT_EQ(xvt_cd_task_is_fading(), 0);
}

static void check_last_step_overshoots_and_clamps(void)
{
	/* 300 apart is two steps, rounded up: the fade ends 212 past to. */
	fresh();
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(0, 300, 0), 1);
	advance_ms(10);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), 512);
	XVT_ASSERT_TRUE(volume() - 300 <= 255);
	XVT_ASSERT_INT_EQ(xvt_cd_task_is_fading(), 0);

	/* Downward, the overshoot is clamped to 0. */
	fresh();
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(300, 0, 0), 1);
	advance_ms(10);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), 0);

	/* Upward, to 65535. */
	fresh();
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(65400, 65535, 0), 1);
	advance_ms(10);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), 65535);

	/* A start above 65535 is clamped first: 256 steps down from 65535 end at 0. */
	fresh();
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(70000, 0, 0), 1);
	advance_ms(255);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), 65535 - 255 * 256);
	advance_ms(1);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), 0);
	XVT_ASSERT_INT_EQ(xvt_cd_task_is_fading(), 0);
}

static void check_begin_replaces_fade(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(0, 2560, 1000), 1);
	advance_ms(101);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), 256);
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(5000, 0, 0), 1);
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), 1000);
	advance_ms(1);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), 5000 - 256);
	xvt_cd_task_cancel_fade();
}

static void check_cancel(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(0, 2560, 1000), 1);
	advance_ms(202);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), 512);
	xvt_cd_task_cancel_fade();
	XVT_ASSERT_INT_EQ(xvt_cd_task_is_fading(), 0);
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), UINT64_MAX);
	advance_ms(5000);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(volume(), 512);
}

static void check_resume_delay(void)
{
	fresh();
	advance_ms(1000);
	g_front_state.cd_audio_suspend_state = CD_AUDIO_RESUME_PENDING;
	g_front_state.cd_audio_resume_due_ms = 1500;

	/* Due 500 ms out, woken 1 ms late to match the strict comparison. */
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), 501000);
	advance_ms(500);
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), 1000);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(g_front_state.cd_audio_suspend_state,
			  CD_AUDIO_RESUME_PENDING);
	advance_ms(1);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(g_front_state.cd_audio_suspend_state,
			  CD_AUDIO_NOT_SUSPENDED);

	/* Overdue reads as 0. */
	g_front_state.cd_audio_suspend_state = CD_AUDIO_RESUME_PENDING;
	g_front_state.cd_audio_resume_due_ms = 100;
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), 0);
}

static void check_track_end(void)
{
	fresh();
	advance_ms(1000);
	g_front_state.cd_audio_current_track = 3;
	g_front_state.cd_audio_track_end_ms = 1200;
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), 201000);
	advance_ms(200);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(g_front_state.cd_audio_playback_complete, 0);

	/* Past the end of a track that does not loop, playback is marked
	 * complete; nothing more is due. */
	advance_ms(1);
	xvt_cd_task_update();
	XVT_ASSERT_INT_EQ(g_front_state.cd_audio_playback_complete, 1);
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), UINT64_MAX);
}

static void check_soonest_wake(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(0, 2560, 1000), 1);
	g_front_state.cd_audio_current_track = 1;
	g_front_state.cd_audio_track_end_ms = 49;
	/* The track end, 49 ms out and woken 1 ms late, comes before the first
	 * fade step at 101 ms. */
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), 50000);
	g_front_state.cd_audio_track_end_ms = 200;
	XVT_ASSERT_INT_EQ(xvt_cd_task_next_wake_delay_us(), 101000);
	xvt_cd_task_cancel_fade();
}

int main(void)
{
	check_begin_needs_a_device();
	check_equal_levels();
	check_step_timing();
	check_non_positive_duration();
	check_last_step_overshoots_and_clamps();
	check_begin_replaces_fade();
	check_cancel();
	check_resume_delay();
	check_track_end();
	check_soonest_wake();
	return 0;
}
