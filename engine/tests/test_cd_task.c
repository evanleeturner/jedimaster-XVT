/* Checks the CD audio timing (xvt_runtime/runtime/cd_task.h) against the promises in its header: when a
 * fade starts, the size and spacing of its steps, the steps applied together when several are due, the
 * overshoot and clamping of the last step, Cancel, and outside flight the resume delay and the end of a
 * track. The host clock moves only when the test advances it. The volume a step sets is read back from
 * the CD audio state, where CDAudio_SetAuxVolume records the level it sets. Every case starts from a
 * cleared frontend state, the music CD device marked open, no flight, no fade and the clock at 0.
 *
 * Not checked here: replaying a looping track, and anything during a flight; both need a CD device or a
 * running flight. */
#include "test_assert.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/music_cd.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/runtime/cd_task.h"
#include "xvt_runtime/timing/host_clock.h"

#include <stdint.h>
#include <string.h>

/* A volume no step can produce: steps move the level from its start in multiples of 256. */
enum { UNSET = 12345 };

static void Fresh(void)
{
	XvtCdTask_CancelFade();
	memset(&g_frontState, 0, sizeof g_frontState);
	g_musicCdMciDeviceId = 1;
	g_frontState.cdAudioTrackCache.currentAuxVolume = UNSET;
	XvtTime_Reset();
}

static unsigned Volume(void)
{
	return g_frontState.cdAudioTrackCache.currentAuxVolume;
}

static void AdvanceMs(int ms) { XvtTime_AdvanceHostClock(ms * 1000); }

static void CheckBeginNeedsADevice(void)
{
	Fresh();
	g_musicCdMciDeviceId = 0;
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(0, 2560, 1000), 0);
	XVT_ASSERT_INT_EQ(XvtCdTask_IsFading(), 0);

	/* Either device will do. */
	g_frontState.cdAudioMciDeviceId = 1;
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(0, 2560, 1000), 1);
	XVT_ASSERT_INT_EQ(XvtCdTask_IsFading(), 1);
	g_frontState.cdAudioMciDeviceId = 0;
	g_musicCdMciDeviceId = 1;
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(0, 2560, 1000), 1);
	XVT_ASSERT_INT_EQ(XvtCdTask_IsFading(), 1);
	XvtCdTask_CancelFade();
}

static void CheckEqualLevels(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(1000, 1000, 1000), 1);
	XVT_ASSERT_INT_EQ(XvtCdTask_IsFading(), 0);
	/* Levels are clamped to 65535 before they are compared. */
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(70000, 65535, 1000), 1);
	XVT_ASSERT_INT_EQ(XvtCdTask_IsFading(), 0);
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), UINT64_MAX);
	AdvanceMs(5000);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), UNSET);
}

static void CheckStepTiming(void)
{
	Fresh();
	/* 2560 apart over 1000 ms: a step of 256 every 1000 * 256 / 2560 + 1 = 101 ms, 10 steps. */
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(0, 2560, 1000), 1);
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), 101000);
	/* The volume is not set to from at the start. */
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), UNSET);

	AdvanceMs(100);
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), 1000);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), UNSET);

	AdvanceMs(1);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), 256);
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), 101000);

	/* Three steps due at once are applied together. */
	AdvanceMs(303);
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), 0);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), 4 * 256);
	XVT_ASSERT_INT_EQ(XvtCdTask_IsFading(), 1);
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), 101000);

	/* Long overdue: the remaining six steps, then the fade is over. */
	AdvanceMs(10000);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), 2560);
	XVT_ASSERT_INT_EQ(XvtCdTask_IsFading(), 0);
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), UINT64_MAX);
}

static void CheckNonPositiveDuration(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(0, 512, 0), 1);
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), 1000);
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(0, 512, -50), 1);
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), 1000);
	AdvanceMs(1);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), 256);
	AdvanceMs(1);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), 512);
	XVT_ASSERT_INT_EQ(XvtCdTask_IsFading(), 0);
}

static void CheckLastStepOvershootsAndClamps(void)
{
	/* 300 apart is two steps, rounded up: the fade ends 212 past to. */
	Fresh();
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(0, 300, 0), 1);
	AdvanceMs(10);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), 512);
	XVT_ASSERT_TRUE(Volume() - 300 <= 255);
	XVT_ASSERT_INT_EQ(XvtCdTask_IsFading(), 0);

	/* Downward, the overshoot is clamped to 0. */
	Fresh();
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(300, 0, 0), 1);
	AdvanceMs(10);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), 0);

	/* Upward, to 65535. */
	Fresh();
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(65400, 65535, 0), 1);
	AdvanceMs(10);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), 65535);

	/* A start above 65535 is clamped first: 256 steps down from 65535 end at 0. */
	Fresh();
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(70000, 0, 0), 1);
	AdvanceMs(255);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), 65535 - 255 * 256);
	AdvanceMs(1);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), 0);
	XVT_ASSERT_INT_EQ(XvtCdTask_IsFading(), 0);
}

static void CheckBeginReplacesFade(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(0, 2560, 1000), 1);
	AdvanceMs(101);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), 256);
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(5000, 0, 0), 1);
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), 1000);
	AdvanceMs(1);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), 5000 - 256);
	XvtCdTask_CancelFade();
}

static void CheckCancel(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(0, 2560, 1000), 1);
	AdvanceMs(202);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), 512);
	XvtCdTask_CancelFade();
	XVT_ASSERT_INT_EQ(XvtCdTask_IsFading(), 0);
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), UINT64_MAX);
	AdvanceMs(5000);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(Volume(), 512);
}

static void CheckResumeDelay(void)
{
	Fresh();
	AdvanceMs(1000);
	g_frontState.cdAudioSuspendState = CDAudio_ResumePending;
	g_frontState.cdAudioResumeDueMs = 1500;

	/* Due 500 ms out, woken 1 ms late to match the strict comparison. */
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), 501000);
	AdvanceMs(500);
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), 1000);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(g_frontState.cdAudioSuspendState,
			  CDAudio_ResumePending);
	AdvanceMs(1);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(g_frontState.cdAudioSuspendState,
			  CDAudio_NotSuspended);

	/* Overdue reads as 0. */
	g_frontState.cdAudioSuspendState = CDAudio_ResumePending;
	g_frontState.cdAudioResumeDueMs = 100;
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), 0);
}

static void CheckTrackEnd(void)
{
	Fresh();
	AdvanceMs(1000);
	g_frontState.cdAudioCurrentTrack = 3;
	g_frontState.cdAudioTrackEndMs = 1200;
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), 201000);
	AdvanceMs(200);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(g_frontState.cdAudioPlaybackComplete, 0);

	/* Past the end of a track that does not loop, playback is marked complete; nothing more is due. */
	AdvanceMs(1);
	XvtCdTask_Update();
	XVT_ASSERT_INT_EQ(g_frontState.cdAudioPlaybackComplete, 1);
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), UINT64_MAX);
}

static void CheckSoonestWake(void)
{
	Fresh();
	XVT_ASSERT_INT_EQ(XvtCdTask_BeginFade(0, 2560, 1000), 1);
	g_frontState.cdAudioCurrentTrack = 1;
	g_frontState.cdAudioTrackEndMs = 49;
	/* The track end, 49 ms out and woken 1 ms late, comes before the first fade step at 101 ms. */
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), 50000);
	g_frontState.cdAudioTrackEndMs = 200;
	XVT_ASSERT_INT_EQ(XvtCdTask_NextWakeDelayUs(), 101000);
	XvtCdTask_CancelFade();
}

int main(void)
{
	CheckBeginNeedsADevice();
	CheckEqualLevels();
	CheckStepTiming();
	CheckNonPositiveDuration();
	CheckLastStepOvershootsAndClamps();
	CheckBeginReplacesFade();
	CheckCancel();
	CheckResumeDelay();
	CheckTrackEnd();
	CheckSoonestWake();
	return 0;
}
