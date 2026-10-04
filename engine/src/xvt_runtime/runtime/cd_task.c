#include "xvt_runtime/runtime/cd_task.h"

#include "xvt/audio/cd_audio.h"
#include "xvt/audio/music_cd.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/runtime/flight_task.h"
#include "xvt_runtime/timing/host_clock.h"

static struct {
	unsigned int current;
	unsigned int target;
	uint64_t interval;
	uint64_t next;
	int active;
} g_fade;

int xvt_cd_task_begin_fade(unsigned int from, unsigned int to, int duration_ms)
{
	xvt_cd_task_cancel_fade();
	if (!g_front_state.cd_audio_mci_device_id &&
	    !g_music_cd_mci_device_id) {
		return 0;
	}
	if (from > 65535) {
		from = 65535;
	}
	if (to > 65535) {
		to = 65535;
	}
	if (from == to) {
		return 1;
	}
	unsigned int distance = from > to ? from - to : to - from;
	g_fade.current = from;
	g_fade.target = to;
	/* Original comparison is strict: a zero step delay still waits one millisecond. */
	g_fade.interval =
		((duration_ms > 0 ? (uint64_t)duration_ms * 256 / distance
				  : 0) +
		 1) *
		1000;
	g_fade.next = xvt_time_get_elapsed_us() + g_fade.interval;
	g_fade.active = 1;
	return 1;
}

int xvt_cd_task_is_fading(void) { return g_fade.active; }

void xvt_cd_task_cancel_fade(void) { g_fade.active = 0; }

void xvt_cd_task_update(void)
{
	uint64_t now = xvt_time_get_elapsed_us();
	uint32_t now_ms = xvt_time_get_elapsed_ms();
	if (g_fade.active && now >= g_fade.next) {
		unsigned int steps =
			(unsigned int)((now - g_fade.next) / g_fade.interval +
				       1);
		unsigned int distance =
			g_fade.current > g_fade.target
				? g_fade.current - g_fade.target
				: g_fade.target - g_fade.current;
		if (steps >= (distance + 255) / 256) {
			steps = (distance + 255) / 256;
			g_fade.active = 0;
		}
		if (g_fade.current > g_fade.target) {
			g_fade.current = steps * 256 > g_fade.current
						 ? 0
						 : g_fade.current - steps * 256;
		} else {
			g_fade.current += steps * 256;
			if (g_fade.current > 65535) {
				g_fade.current = 65535;
			}
		}
		cd_audio_set_aux_volume(g_fade.current);
		g_fade.next += steps * g_fade.interval;
	}
	if (xvt_flight_task_is_active()) {
		return;
	}
	if (g_front_state.cd_audio_suspend_state == CD_AUDIO_RESUME_PENDING &&
	    (int32_t)(now_ms - g_front_state.cd_audio_resume_due_ms) > 0) {
		cd_audio_resume_suspended_playback();
	}
	if (g_front_state.cd_audio_current_track &&
	    g_front_state.cd_audio_suspend_state == 0 &&
	    !g_front_state.cd_audio_playback_complete &&
	    (int32_t)(now_ms - g_front_state.cd_audio_track_end_ms) > 0) {
		if (g_front_state.cd_audio_loop_current_track) {
			cd_audio_play_track_from_time(
				g_front_state.cd_audio_current_track, 0, 0);
		} else {
			g_front_state.cd_audio_playback_complete = 1;
		}
	}
}

uint64_t xvt_cd_task_next_wake_delay_us(void)
{
	uint64_t now = xvt_time_get_elapsed_us();
	uint64_t delay = g_fade.active
				 ? (now < g_fade.next ? g_fade.next - now : 0)
				 : UINT64_MAX;
	uint32_t now_ms = xvt_time_get_elapsed_ms();
	if (xvt_flight_task_is_active()) {
		return delay;
	}
	int32_t remaining;
	uint64_t candidate;
	if (g_front_state.cd_audio_suspend_state == CD_AUDIO_RESUME_PENDING) {
		remaining = (int32_t)(g_front_state.cd_audio_resume_due_ms -
				      now_ms);
		candidate =
			remaining < 0 ? 0 : ((uint64_t)remaining + 1) * 1000;
		if (candidate < delay) {
			delay = candidate;
		}
	}
	if (g_front_state.cd_audio_current_track &&
	    !g_front_state.cd_audio_playback_complete &&
	    g_front_state.cd_audio_suspend_state == CD_AUDIO_NOT_SUSPENDED) {
		remaining =
			(int32_t)(g_front_state.cd_audio_track_end_ms - now_ms);
		candidate =
			remaining < 0 ? 0 : ((uint64_t)remaining + 1) * 1000;
		if (candidate < delay) {
			delay = candidate;
		}
	}
	return delay;
}
