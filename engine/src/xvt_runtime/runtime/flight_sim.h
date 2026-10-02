#ifndef XVT_RUNTIME_FLIGHT_SIM_H
#define XVT_RUNTIME_FLIGHT_SIM_H
#include "xvt/net/flight_sync.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The flight simulation step and the per-player input history it replays. StepToTime advances in
 * sub-steps of at most XvtFlightTiming_MaximumStepTicks ticks; each replays the connected players'
 * due input, then runs AI, weapons, collisions, movement, mission logic, HUD and sound. A step can
 * suspend when a player's update pauses the game; the next call resumes it. */

typedef enum XvtInputInsertStatus {
	XVT_INPUT_INSERTED,
	XVT_INPUT_DUPLICATE,
	XVT_INPUT_FULL,
	XVT_INPUT_INVALID,
	XVT_INPUT_CONFLICT
} XvtInputInsertStatus;

/* Inserts input at tick into player's history, kept in tick order, as a real unapplied frame and
 * points out at it; out is NULL unless INSERTED. A real or predicted unapplied frame already at
 * tick is overwritten. Returns DUPLICATE when the frame at tick is authoritative or applied, FULL
 * when the history is at capacity, and INVALID for a bad player, a tick at or below 0, NULL input,
 * unknown flags, a throttle without its flag, or a corrupt count. */
XvtInputInsertStatus XvtFlightHistory_Insert(unsigned player, int tick, const FlightInputFrameRecord* input,
											 InputFrame** out);
/* Insert for received input: a full history first drops its predicted frames and retries. A new
 * frame is marked authoritative and unapplied, or real, and applied on the host. An authoritative
 * duplicate must match the frames already at tick, which then become authoritative and unapplied;
 * a mismatch logs an error and returns CONFLICT. Otherwise returns Insert's status. */
XvtInputInsertStatus XvtFlightHistory_InsertReal(unsigned player, int tick,
												 const FlightInputFrameRecord* input, int authoritative);

/* Clears the step, pause and replay state and the prediction fallback. */
void XvtFlightSim_Reset(void);

typedef enum XvtFlightStepResult {
	XVT_STEP_PENDING,
	XVT_STEP_COMPLETE,
	XVT_STEP_TERMINAL
} XvtFlightStepResult;

/* Steps the game time to targetGameTime, which must not be behind it: the difference is taken as a
 * 16-bit unsigned tick count, so a past target runs a full-length step forward. Returns COMPLETE
 * once there, TERMINAL when the mission ends with side effects on, and PENDING when a player's
 * update paused; call again to resume, and targetGameTime is ignored until the step finishes. A
 * target equal to the game time only replays input due now. */
XvtFlightStepResult XvtFlightSim_StepToTime(int targetGameTime);
/* After a world restore: drops each player's predicted frames and frames at or before the player's
 * lockstep tick, and every frame of a disconnected player. */
void XvtFlightHistory_RestoreCheckpoint(void);
/* For state recovery: resets the prediction fallback and keeps only the connected local
 * player's non-predicted frames after the current game time. */
void XvtFlightHistory_Recover(void);
/* Replays each connected player's history frames due by targetGameTime: brings the player's craft
 * from its last lockstep save to the frame's tick, saves it, and runs UpdateEntity with the frame's
 * input. With side effects suppressed, frames at or before the starting game time replay without
 * keys, modifiers or throttle; with them on (network125 confirmation), only authoritative frames
 * replay, each confirming the prediction fallback. Unapplied frames at or before a player's lockstep
 * tick are dropped, except while network125 predicts. Returns 0 when UpdateEntity paused, resuming
 * at that frame on the next call; otherwise 1. */
int XvtFlightSim_Advance(int targetGameTime);
/* One player's input step: with side effects on, repairs a camera or map aim left on a destroyed
 * object and, for the local player, handles the in-flight hotkeys. A player with a region session
 * then only rebinds a destroyed craft (side effects on) and returns 1. Otherwise fires, taps or
 * holds the target modifier, runs actions or chat, updates controls and camera, and applies the
 * recorded throttle unless the craft changed. Alt-P in a single-player flight without network
 * progress pauses and returns 0; the next call skips the input read. Returns 1 otherwise, at once
 * when the mission end is pending. */
int XvtFlightSim_UpdatePlayerStep(int playerIdx);
/* 1 while the flight is paused by Alt-P. */
int XvtFlightSim_IsPaused(void);
/* Returns 1 when not paused. While paused, returns 0 until the local key read is Alt-P (although
 * the pause message says any key), then recovers the controls, announces the resume and returns
 * 1. */
int XvtFlightSim_Resume(void);

#ifdef __cplusplus
}
#endif
#endif
