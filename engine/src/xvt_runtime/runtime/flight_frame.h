#ifndef XVT_RUNTIME_FLIGHT_FRAME_H
#define XVT_RUNTIME_FLIGHT_FRAME_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The flight frame loop. The native and offline profiles advance the simulation to an input-clock
 * target and render. Network125 confirms world messages (restore the saved world, insert the
 * message, step to its tick with side effects on), then predicts ahead of the last confirmed tick
 * with side effects suppressed, and renders once caught up. Confirmation and prediction share a
 * budget of XVT_SIM_STEPS_PER_ITERATION steps or XVT_SIM_BUDGET_US per host iteration. */

/* Clears the frame and confirmation state, the step target, the ping score and the update
 * histogram; network125 starts with a predicted frame delta of XVT_NETWORK_STEP_TICKS. */
void XvtFlightFrame_Begin(void);

typedef enum XvtFlightReplayResult {
	XVT_REPLAY_PENDING,
	XVT_REPLAY_IDLE,
	XVT_REPLAY_ADVANCED,
	XVT_REPLAY_TERMINAL
} XvtFlightReplayResult;

/* Confirms messages from the replay queue, one after another within the budget. A message at or
 * before the last confirmed tick is dropped; one that is not exactly XVT_WORLD_MESSAGE_TICKS past
 * it requests recovery. Returns IDLE when the queue is empty, ADVANCED when the budget ran out
 * between messages, PENDING when a message is unfinished, requested recovery or could not be
 * applied, and TERMINAL when the mission ended. */
XvtFlightReplayResult XvtFlightFrame_ReplayBuffered(void);
/* Clears the confirmation state and resets the flight simulation. */
void XvtFlightFrame_ResetReplay(void);
/* Runs the frame loop once; returns 1 when the flight should end, otherwise 0. Native: resumes a
 * paused simulation or returns 0, finishes a suspended step, and once a step's worth of input time
 * has passed ends on mission end or steps to the new target and renders. Network125: advances the
 * input clock, flushes input and world messages and reads packets; holds while a resync is active;
 * requests a resync on recovery; confirms pending messages, then predicts at most
 * XVT_PREDICTION_LEAD_TICKS past the last confirmed tick. It returns 1 on mission end, a lost
 * local player, a host abort, a client whose host timeout (reset by the network code) passes
 * 7080 input-clock ticks, or an input clock near INT32_MAX (announcing that the local player
 * left). */
int XvtFlightFrame_Update(void);
/* Microseconds until Update has work. Network125: 0 while confirmation or prediction work remains,
 * else the sooner of the network's next event and the next simulation step. Native: UINT64_MAX
 * while paused, else the time until a step's worth of input time. */
uint64_t XvtFlightFrame_NextWakeDelayUs(void);
/* Microseconds until ticks 4 ms ticks have passed since the frame-delta clock's last tick (now,
 * when the clock is reset); 0 once they have. */
uint64_t XvtFlightTime_DelayForTicks(unsigned int ticks);
#ifdef __cplusplus
}
#endif
#endif
