#ifndef XVT_RUNTIME_PLAYER_TIMING_H
#define XVT_RUNTIME_PLAYER_TIMING_H
#include "xvt_runtime/timing/flight_state.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per player, what flight_integration.h keeps per object: the fractions of player-side step arithmetic
 * carried between short unlocked steps (the channels below), the odd tick of missile lock timing, and a
 * recovery position the proving grounds' pose history reads. While a player has an object in the main
 * region, the player's entry is cleared when read if that object's slot or signature changed. */
enum {
	XVT_PLAYER_YAW,
	XVT_PLAYER_PITCH,
	XVT_PLAYER_ROLL,
	XVT_PLAYER_CAMERA_YAW,
	XVT_PLAYER_CAMERA_PITCH,
	XVT_PLAYER_DISTANCE,
	XVT_PLAYER_ZOOM,
	XVT_PLAYER_SLEW_YAW,
	XVT_PLAYER_SLEW_PITCH,
	XVT_PLAYER_CHANNELS
};

/* Clears every player's entry. */
void XvtPlayerTiming_Reset(void);
/* Clears every entry, then makes the current position of each player's object the recovery position,
 * for players whose object is live in the main region. */
void XvtPlayerTiming_BeginWorld(void);
/* Outside NETWORK_125, clears the local player's camera channels, CAMERA_YAW to ZOOM. */
void XvtPlayerTiming_ResetControls(void);
/* Records a player's control mode and camera focus. The mode combines: roll modifier held, flight
 * controls disabled (the subsystem is lost, or beamEffectAccum[1] is set with no chaff active), input
 * blocked, map view, hyperspace. A changed mode, or the first call, clears the yaw, pitch, roll and slew
 * channels; a changed camera focus object clears the camera channels. */
void XvtPlayerTiming_BeginControls(unsigned player);
/* Drops one channel's carried remainder. */
void XvtPlayerTiming_Clear(unsigned player, unsigned channel);
/* (value * elapsed + the channel's carried remainder) / divisor, truncated toward zero; the channel
 * keeps the new remainder. The carry clears first when value's sign (zero included) differs from the
 * last call's. Clamped to int; 0 for a zero divisor or a channel past the enum. A player out of range
 * gets the plain result, with no carry. */
int XvtPlayerTiming_Scale(unsigned player, unsigned channel, int value, unsigned elapsed, unsigned divisor);
/* How far to move this step to close difference: all of it, clearing the carry, when its magnitude is
 * under 8 or the step would reach it; otherwise 4 * max(1, |difference| / 29) per 8 ticks, scaled by
 * g_elapsedTicks through Scale, with difference's sign. */
int XvtPlayerTiming_Slew(unsigned player, unsigned channel, int difference);
/* Replaces the original's g_elapsedTicks / 2 in missile lock timing; mode is one of the XVT_LOCK_HALF_
 * values. Locked flights get exactly g_elapsedTicks / 2. Unlocked, mode 0 returns 0; any other mode
 * returns (g_elapsedTicks + a carried odd tick) / 2 and carries the new odd tick. The carry is dropped
 * unless the previous call came in the step just before this one with the same mode, player object,
 * target and selected warhead. Unlocked, 0 for a player out of range. */
unsigned XvtPlayerTiming_LockHalf(unsigned player, unsigned mode);
/* At most once per step, and only in a reference step: writes the player's recovery position to
 * position, then makes the object's current world position the new one. With no recovery position yet,
 * it writes the object's previous-step position (prevWorld). Returns 1 when it wrote; 0 for a player
 * out of range, outside a reference step, or on a second call in the same step. The player's object must
 * exist and have a mobile record. */
int XvtPlayerTiming_RecordRecovery(unsigned player, int32_t position[3]);
/* Resets the integration and reference motion entries of the player's object, clears all the
 * player's channels, and makes the object's current world position the recovery position. Lock and
 * control state are kept. */
void XvtPlayerTiming_Recover(unsigned player);
/* Canonical schema-2 record, 116 bytes. Decode validates before installation. */
/* slot is a player number, 0 to XVT_FLIGHT_PLAYERS - 1, not an object slot. Writes the player's
 * shared state as a record: channels, lock and control state, camera focus; not the recovery position.
 * A player out of range, without a live object in the main region, or whose entry belongs to another
 * object gives the empty record: zero but for the player. */
void XvtPlayerTiming_Encode(unsigned slot, XvtPlayerTimingWire* out);
/* Returns 1 when record is well formed: player in range; valid, lock_half and control_valid 0 or 1;
 * lock_mode at most XVT_LOCK_HALF_TARGET_LOSS; no unknown control bit; zero reserved fields; if not
 * valid, exactly the empty record; if valid, a slot in the main region; directions -1, 0 or 1; and
 * remainders smaller than SIMULATION_TICKS_PER_SECOND in magnitude. With apply, it then replaces the
 * player's shared state, leaving the recovery position. Returns 0 otherwise and changes nothing. */
int XvtPlayerTiming_Decode(const XvtPlayerTimingWire* record, int apply);
/* Clears every player's shared state: all channels, lock and control state, camera focus and the
 * entry's object. Recovery positions stay. */
void XvtPlayerTiming_ResetShared(void);

#ifdef __cplusplus
}
#endif

#endif
