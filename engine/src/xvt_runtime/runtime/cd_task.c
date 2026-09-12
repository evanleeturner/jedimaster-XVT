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

int XvtCdTask_BeginFade(unsigned int from, unsigned int to, int duration_ms) {
	unsigned int distance;
	XvtCdTask_Cancel();
	if (!g_frontState.cdAudioMciDeviceId && !g_musicCdMciDeviceId)
		return 0;
	if (from > 65535)
		from = 65535;
	if (to > 65535)
		to = 65535;
	if (from == to)
		return 1;
	distance = from > to ? from - to : to - from;
	g_fade.current = from;
	g_fade.target = to;
	/* Original comparison is strict: a zero step delay still waits one millisecond. */
	g_fade.interval = ((duration_ms > 0 ? (uint64_t)duration_ms * 256 / distance : 0) + 1) * 1000;
	g_fade.next = XvtTime_GetElapsedUs() + g_fade.interval;
	g_fade.active = 1;
	return 1;
}

int XvtCdTask_IsFading(void) { return g_fade.active; }

void XvtCdTask_Cancel(void) { g_fade.active = 0; }

void XvtCdTask_Tick(void) {
	uint64_t now = XvtTime_GetElapsedUs();
	uint32_t ticks = XvtTime_GetElapsedTicks();
	if (g_fade.active && now >= g_fade.next) {
		unsigned int steps = (unsigned int)((now - g_fade.next) / g_fade.interval + 1);
		unsigned int distance =
			g_fade.current > g_fade.target ? g_fade.current - g_fade.target : g_fade.target - g_fade.current;
		if (steps >= (distance + 255) / 256) {
			steps = (distance + 255) / 256;
			g_fade.active = 0;
		}
		if (g_fade.current > g_fade.target) {
			g_fade.current = steps * 256 > g_fade.current ? 0 : g_fade.current - steps * 256;
		} else {
			g_fade.current += steps * 256;
			if (g_fade.current > 65535)
				g_fade.current = 65535;
		}
		CDAudio_SetAuxVolume(g_fade.current);
		g_fade.next += steps * g_fade.interval;
	}
	if (XvtFlightTask_IsActive())
		return;
	if (g_frontState.cdAudioSuspendState == CDAudio_ResumePending &&
		(int32_t)(ticks - g_frontState.cdAudioResumeDueTick) > 0)
		CDAudio_ResumeSuspendedPlayback();
	if (g_frontState.cdAudioCurrentTrack && g_frontState.cdAudioSuspendState == 0 &&
		!g_frontState.cdAudioPlaybackComplete && (int32_t)(ticks - g_frontState.cdAudioTrackEndTick) > 0) {
		if (g_frontState.cdAudioLoopCurrentTrack)
			CDAudio_PlayTrackFromTime(g_frontState.cdAudioCurrentTrack, 0, 0);
		else
			g_frontState.cdAudioPlaybackComplete = 1;
	}
}

uint64_t XvtCdTask_NextWakeDelayUs(void) {
	uint64_t now = XvtTime_GetElapsedUs();
	uint64_t delay = g_fade.active ? (now < g_fade.next ? g_fade.next - now : 0) : UINT64_MAX;
	uint32_t ticks = XvtTime_GetElapsedTicks();
	int32_t remaining;
	uint64_t candidate;
	if (XvtFlightTask_IsActive())
		return delay;
	if (g_frontState.cdAudioSuspendState == CDAudio_ResumePending) {
		remaining = (int32_t)(g_frontState.cdAudioResumeDueTick - ticks);
		candidate = remaining < 0 ? 0 : ((uint64_t)remaining + 1) * 1000;
		if (candidate < delay)
			delay = candidate;
	}
	if (g_frontState.cdAudioCurrentTrack && !g_frontState.cdAudioPlaybackComplete &&
		g_frontState.cdAudioSuspendState == CDAudio_NotSuspended) {
		remaining = (int32_t)(g_frontState.cdAudioTrackEndTick - ticks);
		candidate = remaining < 0 ? 0 : ((uint64_t)remaining + 1) * 1000;
		if (candidate < delay)
			delay = candidate;
	}
	return delay;
}
