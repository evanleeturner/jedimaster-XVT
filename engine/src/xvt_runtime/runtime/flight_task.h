#ifndef XVT_RUNTIME_FLIGHT_TASK_H
#define XVT_RUNTIME_FLIGHT_TASK_H

#include <stdint.h>

/* One flight, from launch command to cleanup, run as a sequence of phases that Update advances:
 * entry and network session, loading, world start, the frame loop, then release and a music fade.
 * The task is idle, active, or done. */

/* Starts a flight for command, copied up to 1023 characters, and resets the flight simulation.
 * Returns 1; returns 0 and does nothing when a flight is active or command is NULL. */
int XvtFlightTask_Begin(const char* command);
/* Advances the flight by one phase step; call only while the task is active, since a lost network
 * session sends any phase before cleanup, idle included, to cleanup with a result of 0. Does not
 * advance while a resync is active. A failed step goes to cleanup. Cleanup releases the mission
 * once: it tears down timing, capture and the world buffers, commits flight results once world
 * start succeeded, restores the pre-flight resolution when the mission was entered and it changed,
 * saves the pilot when the result is 1, and starts a music CD fade when CD music is on; the next
 * phase waits out the fade, closes the music CD, runs the entry cleanup and marks the task done. */
void XvtFlightTask_Update(void);
/* 1 from Begin until the task is done or shut down. */
int XvtFlightTask_IsActive(void);
/* 1 from Begin through the world-start phase, which also draws the first view. */
int XvtFlightTask_IsLoading(void);
/* 1 once cleanup and the music fade have finished, until Shutdown or the next Begin. */
int XvtFlightTask_IsComplete(void);
/* 1 once world start succeeded, unless the network session was lost before cleanup; otherwise
 * 0. */
int XvtFlightTask_GetResult(void);
/* 1 while active with more than one flight player; XvtPort_NetworkRequiresProgress reports it. */
int XvtFlightTask_ContinuesWithoutFocus(void);
/* Microseconds until the task next needs an update: the resync's delay while a resync is active or
 * holds input, the frame loop's during frames, one tick while waiting for the first time delta,
 * the CD task's during the fade, and UINT64_MAX when the task asks for no timed wake. */
uint64_t XvtFlightTask_NextWakeDelayUs(void);
/* Leaves relative mouse mode and resets the resync and flight network state. An active flight is
 * released without restoring the resolution, its CD fade is cancelled, the music CD closed and the
 * entry cleanup run. Always leaves the task idle with a result of 0. */
void XvtFlightTask_Shutdown(void);

#endif
