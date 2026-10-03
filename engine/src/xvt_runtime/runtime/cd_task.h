#ifndef XVT_RUNTIME_CD_TASK_H
#define XVT_RUNTIME_CD_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CD audio timing on the host clock: a stepped volume fade, and outside flight, the resume delay
 * and end-of-track handling of CD playback. */

/* Replaces any fade with one from from to to (each clamped to 65535). Returns 0 when neither the
 * CD audio nor the music CD device is open, and 1 otherwise; equal levels start no fade and set no
 * volume. The volume is not set to from at the start; each step moves it 256, one step every
 * duration_ms * 256 / distance + 1 milliseconds (1 ms when duration_ms is not positive). The step
 * count is rounded up, so the fade can end up to 255 past to, clamped to 0 through 65535. */
int xvt_cd_task_begin_fade(unsigned int from, unsigned int to, int duration_ms);
/* 1 while a fade has steps left. */
int xvt_cd_task_is_fading(void);
/* Stops the fade, leaving the volume at its last step. */
void xvt_cd_task_cancel_fade(void);
/* Applies every fade step now due in one volume change. Outside flight, also resumes suspended CD
 * playback once its resume time has passed, and at the end of the current track replays it when
 * looping or marks playback complete. */
void xvt_cd_task_update(void);
/* Microseconds until the next fade step, and outside flight the resume or track end (1 ms late,
 * matching Update's strict comparison); 0 when overdue, UINT64_MAX when nothing is due. */
uint64_t xvt_cd_task_next_wake_delay_us(void);

#ifdef __cplusplus
}
#endif

#endif
