#ifndef XVT_RUNTIME_FLIGHT_INTEGRATION_H
#define XVT_RUNTIME_FLIGHT_INTEGRATION_H
#include "xvt_runtime/timing/flight_state.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per object slot, the fractions the original whole-number step arithmetic drops, carried into the next
 * step so short unlocked steps add up instead of losing them: one remainder per channel below and one per
 * position axis. The table exists only in unlocked flights; without it, every call returns the plain
 * truncated result. An entry is cleared when next read if its slot's object changed (signature, type, or
 * mobile state). The spin channels clear while the roll impulse rate is 0, the homing channels when a
 * warhead's target changes or homing stops, and a craft whose carried object changes resets the old and
 * the new carried slot. Each channel also clears when the sign or direction of its input changes. */
enum {
	XVT_INTEGRATE_SPIN_DECAY,
	XVT_INTEGRATE_SPIN_ANGLE,
	XVT_INTEGRATE_PUSH_X,
	XVT_INTEGRATE_PUSH_Y,
	XVT_INTEGRATE_PUSH_Z,
	XVT_INTEGRATE_HOME_YAW,
	XVT_INTEGRATE_HOME_PITCH,
	XVT_INTEGRATE_HOME_SPEED,
	XVT_INTEGRATE_ROLL,
	XVT_INTEGRATE_PITCH,
	XVT_INTEGRATE_TURN,
	XVT_INTEGRATE_BANK,
	XVT_INTEGRATE_COUNT
};

/* Frees any old table and allocates count empty entries. Returns 0 when allocation fails, leaving none. */
int xvt_flight_integration_init(size_t count);
/* Frees the table. */
void xvt_flight_integration_shutdown(void);
/* Clears slot's entry and its reference motion entry; a slot out of range is ignored. */
void xvt_flight_integration_reset_slot_and_motion(unsigned slot);
/* Drops one channel's carried remainder. */
void xvt_flight_integration_clear(unsigned slot, unsigned channel);
/* (rate * elapsed + the channel's carried remainder) / divisor, truncated toward zero; the channel
 * keeps the new remainder. The carry clears first when rate's sign (zero included) differs from the
 * last call's. Clamped to int; 0 for a channel past the enum or a divisor that is not positive. */
int xvt_flight_integration_rate(unsigned slot, unsigned channel, int rate,
				unsigned elapsed, int divisor);
/* rate * g_elapsed_ticks * accel * factor / (SIMULATION_TICKS_PER_SECOND * 65536 * 65536), with the
 * channel's remainder carried as in Rate. accel and factor are fractions of 65536, and 0xFFFF counts as
 * a whole 65536. The carry clears when direction differs from the last call's. Clamped to 0xFFFF; 0 for
 * a channel past the enum. */
unsigned xvt_flight_integration_steer(unsigned slot, unsigned channel,
				      uint16_t rate, uint16_t accel,
				      uint16_t factor, int direction);
/* The unlocked replacement for the original's per-step movement: sets trig2_xmovedist, trig2_ymovedist
 * and trig2_zmovedist from the mobile's speed, scaled by the original's speed factor ((4660 * speed +
 * 128) >> 8), times g_elapsed_ticks and each move axis, over SIMULATION_TICKS_PER_SECOND * 32768, carrying
 * each axis's remainder. An axis's carry clears while the object's world coordinate on it is at or past
 * +-0x01000000. The slot's object must have a mobile record. */
void xvt_flight_integration_move(unsigned slot);
/* Moves one step's share of a pending push from *accum to *output: *accum, clamped to +-cap, times
 * g_elapsed_ticks over SIMULATION_TICKS_PER_SECOND through Rate's carry, never more than *accum holds.
 * Clears the axis's carry once *accum reaches 0. axis must be 0, 1 or 2; it is not checked. */
void xvt_flight_integration_push(unsigned slot, unsigned axis, int *accum,
				 int cap, int *output);
/* Canonical schema-1 record, 144 bytes. Decode validates before installation. */
/* Writes slot's entry as a record. A slot out of range or empty, or an entry that belongs to another
 * object (signature, type or mobile state), gives the empty record: zero but for the slot. */
void xvt_flight_integration_encode(unsigned slot,
				   struct xvt_integration_wire *out);
/* Returns 1 when record is well formed: slot in range; for type 0, exactly the empty record; carried
 * and target slots in range or 0xFFFF; directions -1, 0 or 1; each position remainder smaller than
 * SIMULATION_TICKS_PER_SECOND * 32768, each steering remainder (ROLL to BANK) smaller than
 * SIMULATION_TICKS_PER_SECOND * 65536 * 65536, and every other remainder smaller than 2^31, in
 * magnitude. With apply, it then replaces the slot's entry. Returns 0 otherwise and changes nothing.
 * The record is not checked against the object in the slot. */
int xvt_flight_integration_decode(const struct xvt_integration_wire *record,
				  int apply);
/* Clears every entry except the local slots from g_local_transient_slot_start up to g_local_debris_slot_end,
 * leaving reference motion alone. Does nothing before Init. */
void xvt_flight_integration_reset_shared(void);

#ifdef __cplusplus
}
#endif

#endif
