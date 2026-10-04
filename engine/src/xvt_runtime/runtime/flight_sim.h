#ifndef XVT_RUNTIME_FLIGHT_SIM_H
#define XVT_RUNTIME_FLIGHT_SIM_H
#include "xvt/net/flight_sync.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The flight simulation step and the per-player input history it replays.
 * StepToTime advances in sub-steps of at most
 * xvt_flight_timing_maximum_step_ticks ticks; each replays the connected
 * players' due input, then runs AI, weapons, collisions, movement, mission
 * logic, HUD and sound. A step can suspend when a player's update pauses the
 * game; the next call resumes it. */

typedef enum xvt_input_insert_status {
	XVT_INPUT_INSERTED,
	XVT_INPUT_DUPLICATE,
	XVT_INPUT_FULL,
	XVT_INPUT_INVALID,
	XVT_INPUT_CONFLICT
} xvt_input_insert_status;

/* Inserts input at tick into player's history, kept in tick order, as a real unapplied frame and
 * points out at it; out is NULL unless INSERTED. A real or predicted unapplied frame already at
 * tick is overwritten. Returns DUPLICATE when the frame at tick is authoritative or applied, FULL
 * when the history is at capacity, and INVALID for a bad player, a tick at or below 0, NULL input,
 * unknown flags, a throttle without its flag, or a corrupt count. */
xvt_input_insert_status
xvt_flight_history_insert(unsigned player, int tick,
			  const struct flight_input_frame_record *input,
			  struct input_frame **out);
/* Insert for received input: a full history first drops its predicted frames and retries. A new
 * frame is marked authoritative and unapplied, or real, and applied on the host. An authoritative
 * duplicate must match the frames already at tick, which then become authoritative and unapplied;
 * a mismatch logs an error and returns CONFLICT. Otherwise returns Insert's status. */
xvt_input_insert_status
xvt_flight_history_insert_real(unsigned player, int tick,
			       const struct flight_input_frame_record *input,
			       int authoritative);

/* Clears the step, pause and replay state and the prediction fallback. */
void xvt_flight_sim_reset(void);

typedef enum xvt_flight_step_result {
	XVT_STEP_PENDING,
	XVT_STEP_COMPLETE,
	XVT_STEP_TERMINAL
} xvt_flight_step_result;

/* Steps the game time to target_game_time, which must not be behind it: the
 * difference is taken as a 16-bit unsigned tick count, so a past target runs a
 * full-length step forward. Returns COMPLETE once there, TERMINAL when the
 * mission ends with side effects on, and PENDING when a player's update paused;
 * call again to resume, and target_game_time is ignored until the step
 * finishes. A target equal to the game time only replays input due now. */
xvt_flight_step_result xvt_flight_sim_step_to_time(int target_game_time);
/* After a world restore: drops each player's predicted frames and frames at or before the player's
 * lockstep tick, and every frame of a disconnected player. */
void xvt_flight_history_restore_checkpoint(void);
/* For state recovery: resets the prediction fallback and keeps only the connected local
 * player's non-predicted frames after the current game time. */
void xvt_flight_history_recover(void);
/* Replays each connected player's history frames due by target_game_time:
 * brings the player's craft from its last lockstep save to the frame's tick,
 * saves it, and runs UpdatePlayerStep with the frame's input. With side effects
 * suppressed, frames at or before the starting game time replay without keys,
 * modifiers or throttle; with them on (network125 confirmation), only
 * authoritative frames replay, each confirming the prediction fallback.
 * Unapplied frames at or before a player's lockstep tick are dropped, except
 * while network125 predicts. Returns 0 when UpdatePlayerStep paused, resuming
 * at that frame on the next call; otherwise 1. */
int xvt_flight_sim_advance(int target_game_time);
/* One player's input step: with side effects on, repairs a camera or map aim left on a destroyed
 * object and, for the local player, handles the in-flight hotkeys. A player awaiting a new craft
 * then only rebinds a destroyed craft (side effects on) and returns 1. Otherwise fires, taps or
 * holds the target modifier, runs actions or chat, updates controls and camera, and applies the
 * recorded throttle unless the craft changed. Alt-P in a single-player flight without network
 * progress pauses and returns 0; the next call skips the input read. Returns 1 otherwise, at once
 * when the mission end is pending. */
int xvt_flight_sim_update_player_step(int player_idx);
/* 1 while the flight is paused by Alt-P. */
int xvt_flight_sim_is_paused(void);
/* Returns 1 when not paused. While paused, returns 0 until the local key read is Alt-P (although
 * the pause message says any key), then recovers the controls, announces the resume and returns
 * 1. */
int xvt_flight_sim_resume(void);

#ifdef __cplusplus
}
#endif
#endif
