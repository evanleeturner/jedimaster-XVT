#ifndef XVT_RUNTIME_INPUT_FLIGHT_CONTROLS_H
#define XVT_RUNTIME_INPUT_FLIGHT_CONTROLS_H
#include <stdbool.h>

#include "xvt/flight/flight_input.h"

/* Local flight input for each game read: yaw, pitch and roll, held buttons and
 * the action key, merged from the controller mapping, the keyboard mapping and
 * mouse flight into the game's input globals; the recorded form of that input
 * that network flights send; and an absolute throttle from a controller
 * lever. */

enum { XVT_FLIGHT_AXIS_BYTES = 3 };

/* The local roll axis for this read, -127 to 127, set by ReadLocal. */
extern int16_t g_xvt_control_roll;
/* Fills g_ctrl_axis_x, g_ctrl_axis_y, g_xvt_control_roll, g_key_mods,
 * g_action_key and the mouse globals, and returns the action key. A blocked
 * keyboard route resets and returns 0. Axes and held buttons come from the
 * controllers, plus the keyboard's held buttons on the gameplay route; mouse
 * flight, when enabled, replaces each nonzero axis and adds its buttons;
 * otherwise, with g_flight_mouse_enabled, the classic mouse is read with its
 * delta clamped to +-191 and +-127. The key is the first nonzero of the
 * keyboard (gameplay mapping or DirectInput), the controllers, then mouse
 * flight. Needs loaded settings. */
uint16_t xvt_flight_controls_read_local(void);
/* Clears the local camera carries (outside network 125), the roll axis and the
 * throttle baseline. */
void xvt_flight_controls_reset(void);
/* Drops the throttle baseline whenever the local player cannot use the lever now. */
void xvt_flight_controls_update_throttle_context(void);
/* true when the player is connected and flying their bound live craft, and
 * neither the mission end, a region session, hyperspace, the map, blocked input
 * nor an open chat is in the way. */
bool xvt_flight_controls_throttle_eligible(unsigned player);
/* Sets input->flags and input->throttle; input->key must already be set. The
 * lever position is sent only when it moved at least 66 (0.1%) from the last
 * one sent, or reached either end. The first sample after a change of
 * controller, binding, suspension or craft only sets the baseline, and nothing
 * is sent while Alt-P pauses a one-player flight, the local player is not
 * eligible, or no lever was read. */
void xvt_flight_controls_sample_throttle(
	struct flight_input_frame_record *input);
/* Sets the player's craft throttle to a recorded lever position when one is
 * present and eligible. */
void xvt_flight_controls_apply_throttle(
	unsigned player, const struct flight_input_frame_record *input);
/* Flushes the keyboard, releases controller commands and pending mouse flight input without sending
 * releases, clears the action key and drops the throttle baseline. */
void xvt_flight_controls_recover(void);
/* Reads local input through flight_input_read, which runs ReadLocal in the
 * modern build, and records it: the key's low byte, each axis made even, fire
 * and target/roll as values 1 and 2 (bits 0 and 1) of key_mods, and the
 * throttle. With g_flight_mouse_enabled, a nonzero classic mouse delta replaces
 * yaw (times 128/120) and pitch (times 64/50), clamped to -128..126. */
void xvt_flight_controls_sample_recorded(
	struct flight_input_frame_record *input);
/* Packs three axes into XVT_FLIGHT_AXIS_BYTES bytes: the low bit of the yaw
 * byte carries fire and of the pitch byte the target/roll modifier. */
void xvt_flight_controls_encode_axes(
	uint8_t *bytes, const struct flight_input_frame_record *input);
/* The inverse of EncodeAxes; axes come back even. */
void xvt_flight_controls_decode_axes(const uint8_t *bytes,
				     struct flight_input_frame_record *input);
/* This step's roll: the roll axis times 120, scaled by roll_rate / 0x3800 and
 * by the step's ticks (carrying the remainder when unlocked), plus
 * modifier_step, limited to what full deflection gives. With the roll axis at
 * 0, clears the carry and returns modifier_step. */
int16_t xvt_flight_controls_roll_step(unsigned player, uint16_t roll_rate,
				      int16_t modifier_step);
#endif
