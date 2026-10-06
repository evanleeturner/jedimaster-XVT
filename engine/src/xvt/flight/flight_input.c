#include "xvt/flight/flight_input.h"

#include "xvt/frontend/config.h"
#include "xvt/input/dinput.h"
#include "xvt/input/input.h"
#include "xvt/input/joystick.h"
#include "xvt/input/mouse.h"
#include "xvt/util/time.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/log/log_both_builds.h"
#include "xvt_runtime/timing/flight_timing.h"

/* 1 when keys are read through DirectInput, 0 when through window messages; the
 * modern build reads keys through DirectInput either way. Starts at 1; at
 * flight start the launch option "nodinput" sets 0 and "dinput" or neither sets
 * 1 (flight_main in the original build, xvt_flight_entry_read_launch_switches in
 * the modern one), and it falls to 0 when dinput_init fails (flight_main,
 * xvt_flight_entry_create_devices). */
// GLOBAL: XVT 0x527EB4
int g_flight_conf_direct_input = 1;
/* Key code flight_input_get_next_key returns on the window-message path. Nothing
 * in the engine stores a key in it; only xvt_input_flush_raw_keyboard writes it,
 * setting 0, in the modern build. */
// GLOBAL: XVT 0x66E1F4
uint8_t g_last_key_code = 0;
/* 1 when a key waits for flight_input_get_next_key on the window-message path.
 * Nothing in the engine sets it to 1: flight_input_get_next_key and, in the modern
 * build, xvt_input_flush_raw_keyboard set it to 0, so that path never sees a
 * key. */
// GLOBAL: XVT 0x66E708
int g_key_ready = 0;

/* Bit mask of the 20 joystick buttons flight_input_read last counted as held, so
 * a button gives its key once per press. Written only by flight_input_read and
 * flight_input_reset_control_state, which sets 0. */
// GLOBAL: XVT 0x5505EC
int g_held_joystick_buttons;
/* The joystick throttle, 0 to 255, smoothed by flight_input_read, which moves it
 * a quarter of the way to each new reading; -1 until the first reading. Written
 * only by flight_input_read and flight_input_reset_control_state, which sets -1. */
// GLOBAL: XVT 0x5505F0
int g_throttle_smoothed;
/* Keys a joystick throttle sends, indexed by 16 minus the smoothed throttle in
 * sixteenths (g_throttle_smoothed plus 8, divided by 16, clamped 0 to 16):
 * FLIGHT_KEY_BACKSLASH at index 0, the FLIGHT_KEY_THROTTLE keys and
 * FLIGHT_KEY_LEFT_BRACKET and FLIGHT_KEY_RIGHT_BRACKET between,
 * FLIGHT_KEY_BACKSPACE at index 16. flight_input_read sends one when the
 * sixteenth changes. Never written. */
// GLOBAL: XVT 0x51A878
uint8_t g_throttle_key_table[17] = {0x5C, 0xDB, 0xDC, 0xDD, 0xDE, 0x5B,
				    0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0x5D,
				    0xE4, 0xE5, 0xE6, 0xE7, 0x08};
/* Per player, the input record flight_input_read applies when called with that
 * player's index: the input from that player's history for the step being
 * simulated, its key and button bits cleared when a step with side effects
 * suppressed replays a tick already simulated. Five functions write it:
 * flight_advance_one_step, and at flight start flight_main_loop, which clears it,
 * in the original build; xvt_flight_sim_advance, xvt_flight_loading_globals, and
 * xvt_flight_sim_update_player_step, which clears its flags and throttle on a
 * pause, in the modern one. */
// GLOBAL: XVT 0x9A7B70
struct flight_input_frame_record g_replay_inputs[8] = {{0}};
/* Stick X axis of the input being applied: the local joystick's on a local
 * read, else the replayed record's. flight_input_latch_flight_controls turns it
 * into g_scaled_input_yaw (times 120) when the mouse gives no yaw. Written by
 * flight_input_read and flight_input_clear_axes_and_modifiers, and in the modern
 * build by xvt_flight_controls_read_local and xvt_input_set_captured. */
// GLOBAL: XVT 0xA00498
int16_t g_ctrl_axis_x;
/* Stick Y axis, as g_ctrl_axis_x; it becomes g_scaled_input_pitch (times 50) when
 * the mouse gives no pitch. Same writers as g_ctrl_axis_x. */
// GLOBAL: XVT 0xA004A0
int16_t g_ctrl_axis_y;
/* Button bits of the input being applied: bit 0 fire, bit 1 target, set while a
 * joystick button mapped to code 156 or 157 is held, or from a replayed record
 * (its key_mods, low 2 bits). flight_input_latch_flight_controls merges it into
 * g_flight_key_mods. Written by flight_input_read, flight_input_reset_runtime_state
 * and flight_input_clear_axes_and_modifiers, and in the modern build by
 * xvt_flight_controls_read_local and xvt_input_set_captured. */
// GLOBAL: XVT 0xA08244
uint16_t g_key_mods;
/* What input_detect_active_joystick returned, cut to 16 bits;
 * flight_input_reset_runtime_state sets it and sets g_joystick_available from it,
 * and nothing else reads it. */
// GLOBAL: XVT 0xA08130
uint16_t g_joystick_detect_result_word = 0;
/* Action key of the last flight_input_read (a flight_action_key, 0 for none);
 * flight_input_latch_flight_controls copies it to g_current_action_key. Seven
 * functions write it: flight_input_read, mission_init_flight_runtime_state (0),
 * flight_update_player_step in the original build, and
 * xvt_flight_controls_read_local, xvt_flight_controls_recover, xvt_flight_sim_resume
 * and xvt_input_set_captured in the modern one. */
// GLOBAL: XVT 0x9A8C12
uint16_t g_action_key;
/* Action key the current player step acts on, latched from g_action_key by
 * flight_input_latch_flight_controls; the only other writer,
 * flight_process_player_actions, sets it to FLIGHT_KEY_NONE after some keys. Read
 * by the action handling, the chat input and the MFD pages. */
// GLOBAL: XVT 0x9D6930
uint16_t g_current_action_key;
/* Button bits for the current player step: g_key_mods and the mouse buttons,
 * merged by flight_input_latch_flight_controls. Bit 0, with bits 2 and 3 clear,
 * fires the weapon. Bit 1, with bits 2 and 3 clear, picks a target when
 * released within 59 ticks and, held longer, turns yaw input into roll; the
 * player step clears it here during those first 59 ticks. Three functions write
 * it: flight_input_latch_flight_controls and the player step,
 * flight_update_player_step in the original build and
 * xvt_flight_sim_update_player_step in the modern one. */
// GLOBAL: XVT 0x9EC474
uint16_t g_flight_key_mods;
/* 1 when the mouse steers. Only flight_input_reset_runtime_state writes it,
 * setting 0, so the mouse paths in flight_input_read,
 * flight_input_latch_flight_controls and the modern controls never run. */
// GLOBAL: XVT 0x9E95F0
uint16_t g_flight_mouse_enabled;
/* 1 when flight_input_reset_runtime_state found a joystick
 * (input_detect_active_joystick nonzero), else 0; only that function writes it.
 * flight_input_read polls the joystick only when it is set. */
// GLOBAL: XVT 0xA08242
uint16_t g_joystick_available;
/* Mouse movement in X since the last local read, clamped to -191 to 191;
 * flight_input_latch_flight_controls turns it into yaw (shifted left 7). Written
 * by flight_input_read, and in the modern build by xvt_flight_controls_read_local
 * and xvt_input_set_captured. */
// GLOBAL: XVT 0x9A73E8
int16_t g_flight_mouse_delta_x;
/* Mouse movement in Y, clamped to -127 to 127, turned into pitch (shifted left
 * 6); written as g_flight_mouse_delta_x. */
// GLOBAL: XVT 0x9A73F2
int16_t g_flight_mouse_delta_y;
/* Mouse X position from the last local read. Written by flight_input_read and,
 * through its address, xvt_flight_controls_read_local; nothing reads it. */
// GLOBAL: XVT 0x9A739C
int16_t g_flight_mouse_x = 0;
/* Mouse Y position, as g_flight_mouse_x; nothing reads it. */
// GLOBAL: XVT 0x9A739E
int16_t g_flight_mouse_y = 0;
/* Mouse button bits from the last local read; flight_input_latch_flight_controls
 * merges them into g_flight_key_mods. Written by flight_input_read and
 * flight_input_reset_runtime_state, and in the modern build by
 * xvt_flight_controls_read_local and xvt_input_set_captured. */
// GLOBAL: XVT 0x9D8C06
uint16_t g_mouse_buttons;
/* Yaw input for the current player step, signed: mouse X movement shifted left
 * 7, or with no mouse yaw, g_ctrl_axis_x times 120. Set by
 * flight_input_latch_flight_controls; the dead zones (flight_input_apply_deadzone,
 * flight_input_read_and_apply_flight_deadzone) zero it, and
 * player_update_flight_controls_and_camera also writes it. */
// GLOBAL: XVT 0x9ECC30
int16_t g_scaled_input_yaw;
/* The size of g_scaled_input_yaw, stored by player_update_flight_controls_and_camera,
 * its only writer; nothing reads it. */
// GLOBAL: XVT 0x999414
int16_t g_abs_scaled_input_yaw = 0;
/* Pitch input for the current player step, signed: mouse Y movement shifted
 * left 6, or with no mouse pitch, g_ctrl_axis_y times 50. Written by the same
 * functions as g_scaled_input_yaw. */
// GLOBAL: XVT 0x9ECA24
int16_t g_scaled_input_pitch;

/* Latches the input flight_input_read left for the current player step:
 * g_current_action_key from g_action_key, g_scaled_input_yaw and g_scaled_input_pitch
 * from the mouse movement when the mouse is on and, where that gives 0, from
 * g_ctrl_axis_x times 120 and g_ctrl_axis_y times 50, and g_flight_key_mods from
 * g_key_mods and the mouse buttons. Its test for no joystick,
 * (g_joystick_available | 1) == 0, can never pass, so the stick is always
 * consulted. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x411540
void flight_input_latch_flight_controls(void)
{
	g_current_action_key = g_action_key;
	int16_t pitch = 0;
	uint16_t mouse_buttons = 0;
	int16_t yaw = 0;
	g_flight_key_mods = mouse_buttons;
	g_scaled_input_pitch = pitch;
	g_scaled_input_yaw = yaw;
	if (g_flight_mouse_enabled != 0) {
		yaw = (int16_t)((uint16_t)g_flight_mouse_delta_x << 7);
		pitch = (int16_t)((uint16_t)g_flight_mouse_delta_y << 6);
		mouse_buttons = g_mouse_buttons;
	}
	*(uint16_t *)&g_scaled_input_yaw = (uint16_t)yaw;
	*(uint16_t *)&g_scaled_input_pitch = (uint16_t)pitch;
	*(int16_t *)&g_flight_key_mods = (int16_t)mouse_buttons;
	if ((uint16_t)(g_joystick_available | 1u) == 0) {
		return;
	}
	if (yaw == 0) {
		g_scaled_input_yaw = (int16_t)(g_ctrl_axis_x * 120);
	}
	*(uint16_t *)&g_scaled_input_pitch = (uint16_t)pitch;
	if (pitch == 0) {
		g_scaled_input_pitch = (int16_t)(g_ctrl_axis_y * 50);
	}
	*(int16_t *)&g_flight_key_mods = (int16_t)(g_key_mods | mouse_buttons);
}

/* Zeroes g_scaled_input_yaw when its size is 64 or less and g_scaled_input_pitch
 * when 24 or less. */
// FUNCTION: XVT 0x4115F0
void flight_input_apply_deadzone(void)
{
	int16_t magnitude = g_scaled_input_yaw;
	if ((uint16_t)magnitude >= 0x8000u) {
		magnitude = -magnitude;
	}
	if (magnitude <= 64) {
		g_scaled_input_yaw = 0;
	}

	magnitude = g_scaled_input_pitch;
	if ((uint16_t)magnitude >= 0x8000u) {
		magnitude = -magnitude;
	}
	if (magnitude <= 24) {
		g_scaled_input_pitch = 0;
	}
}

/* Resets input at flight start: clears g_mouse_buttons, g_key_mods and
 * g_flight_mouse_enabled, asks input_detect_active_joystick for a joystick
 * (g_joystick_detect_result_word, g_joystick_available), and calls
 * flight_input_reset_control_state. */
// FUNCTION: XVT 0x411780
void flight_input_reset_runtime_state(void)
{
	g_joystick_available = 0;
	g_flight_mouse_enabled = 0;
	g_mouse_buttons = 0;
	g_key_mods = 0;
	g_joystick_detect_result_word =
		(uint16_t)input_detect_active_joystick();
	if (g_joystick_detect_result_word == 0) {
		g_joystick_available = 0;
	} else {
		g_joystick_available = 1;
	}
	flight_input_reset_control_state();
	g_flight_mouse_enabled = 0;
	XVT_LOG_DEBUG("input.flight_reset joystick=%d detect=%u",
		      (int)g_joystick_available,
		      (unsigned)g_joystick_detect_result_word);
}

/* Sets g_throttle_smoothed to -1 (no reading yet) and g_held_joystick_buttons to
 * 0; the modern build also calls xvt_flight_controls_reset. */
// FUNCTION: XVT 0x4117F0
void flight_input_reset_control_state(void)
{
	xvt_flight_controls_reset();
	g_throttle_smoothed = -1;
	g_held_joystick_buttons = 0;
	XVT_LOG_DEBUG("input.controls_reset");
}

/* Reads one input and returns its action key (0 for none). With a negative
 * argument it reads the local devices: the modern build returns
 * xvt_flight_controls_read_local at once, and the rest of this path runs only in
 * the original build. That path polls the joystick when g_joystick_available and
 * the mouse when g_flight_mouse_enabled, takes one waiting key, and maps the 20
 * joystick buttons through g_game_config.joy_buttons: a newly pressed button
 * gives its key when no key came yet (else it is counted as not held, so it
 * gives its key on a later read); releasing a button mapped to keypad 0 gives
 * keypad 0, and one mapped to keypad 1 to 9 gives keypad 8, the same way;
 * buttons mapped to codes 156 and 157 set g_key_mods bits 0 and 1 while held.
 * With still no key, the smoothed throttle may give one from
 * g_throttle_key_table. It writes g_action_key, g_ctrl_axis_x, g_ctrl_axis_y,
 * g_key_mods, g_mouse_buttons, the mouse globals, g_held_joystick_buttons and
 * g_throttle_smoothed; with the mouse off it stores mouse positions it never
 * set. With a player index it applies g_replay_inputs for that player instead:
 * the axes, the key (into g_action_key) and the low 2 bits of key_mods; the
 * modern build also sets g_xvt_control_roll and, under the network timing, zeroes
 * the mouse movement and buttons. */
// FUNCTION: XVT 0x411810
uint16_t flight_input_read(int player_idx_or_sentinel)
{
	int joystick_buttons = 0;
	uint16_t key;
	uint16_t mapped_key;
	unsigned int mapped_key_value;
	uint8_t button_key;
	int16_t mouse_x;
	int16_t mouse_y;
	if (player_idx_or_sentinel < 0) {
		return xvt_flight_controls_read_local();
		uint16_t mouse_buttons = 0;
		int throttle_raw = 0;
		int16_t mouse_delta_y = 0;
		int axis_y = 0;
		int16_t mouse_delta_x = 0;
		int axis_x = 0;
		mouse_x = 0;
		mouse_y = 0;
		if (g_joystick_available != 0) {
			joystick_buttons = joystick_poll_scaled_axes_if_active(
				&axis_x, &axis_y, &throttle_raw, NULL);
		}
		if (g_flight_mouse_enabled != 0) {
			mouse_buttons =
				(uint16_t)mouse_read_position_and_buttons(
					&mouse_x, &mouse_y);
			mouse_read_delta(&mouse_delta_x, &mouse_delta_y);
			if (mouse_delta_x <= -192) {
				mouse_delta_x = -191;
			} else if (mouse_delta_x >= 192) {
				mouse_delta_x = 191;
			}
			if (mouse_delta_y <= -128) {
				mouse_delta_y = -127;
			} else if (mouse_delta_y >= 128) {
				mouse_delta_y = 127;
			}
		}

		key = 0;
		if (flight_input_has_key_ready() != 0) {
			key = flight_input_get_next_key();
		}
		int target_button_held = 0;
		int button_bit = 1;
		int button_index = 0;
		int fire_button_held = 0;
		do {
			button_key = g_game_config.joy_buttons[button_index];
			if (button_key != 0) {
				if ((button_bit & joystick_buttons) != 0) {
					mapped_key = button_key;
					mapped_key_value = mapped_key;
					switch (mapped_key_value) {
					case 156:
						fire_button_held = 1;
						break;
					case 157:
						target_button_held = 1;
						break;
					}
					if ((g_held_joystick_buttons &
					     button_bit) == 0) {
						if (key == 0) {
							key = mapped_key;
						} else {
							joystick_buttons &=
								~button_bit;
						}
					}
				} else if ((g_held_joystick_buttons &
					    button_bit) != 0) {
					int released_key = button_key;
					if (released_key == 178) {
						released_key = 178;
					} else if (released_key >= 179 &&
						   released_key <= 187) {
						released_key = 186;
					} else {
						released_key = 0;
					}
					if (released_key != 0) {
						if (key == 0) {
							key = (uint16_t)
								released_key;
						} else {
							joystick_buttons |=
								button_bit;
						}
					}
				}
			}
			button_bit *= 2;
			++button_index;
		} while (button_index < 20);
		int combined_key_mods =
			fire_button_held + 2 * target_button_held;
		g_held_joystick_buttons = joystick_buttons;

		if (key == 0) {
			throttle_raw = (int)(int8_t)throttle_raw;
			throttle_raw += 128;
			if (g_throttle_smoothed == -1) {
				g_throttle_smoothed = throttle_raw;
			} else {
				int previous_throttle = g_throttle_smoothed;
				g_throttle_smoothed +=
					(throttle_raw - g_throttle_smoothed) /
					4;
				previous_throttle =
					(previous_throttle + 8) / 16;
				int throttle_bucket =
					(g_throttle_smoothed + 8) / 16;
				if (throttle_bucket != previous_throttle) {
					if (throttle_bucket < 0) {
						throttle_bucket = 0;
					}
					if (throttle_bucket > 16) {
						throttle_bucket = 16;
					}
					key = g_throttle_key_table
						[16 - throttle_bucket];
				}
			}
		}

		g_action_key = key;
		g_flight_mouse_delta_x = mouse_delta_x;
		g_flight_mouse_delta_y = mouse_delta_y;
		g_ctrl_axis_x = (int16_t)axis_x;
		g_ctrl_axis_y = (int16_t)axis_y;
		g_key_mods = (uint16_t)combined_key_mods;
		g_mouse_buttons = mouse_buttons;
		g_flight_mouse_x = mouse_x;
		g_flight_mouse_y = mouse_y;
		return key;
	} else {
		g_ctrl_axis_x = g_replay_inputs[player_idx_or_sentinel].axis_x;
		g_xvt_control_roll =
			g_replay_inputs[player_idx_or_sentinel].axis_r;
		/* Recorded axes are the complete shared control sample. */
		if (xvt_flight_timing_is_network125()) {
			g_flight_mouse_delta_x = 0;
			g_flight_mouse_delta_y = 0;
			g_mouse_buttons = 0;
		}
		g_ctrl_axis_y = g_replay_inputs[player_idx_or_sentinel].axis_y;
		key = g_replay_inputs[player_idx_or_sentinel].key;
		g_action_key = key;
		g_key_mods =
			g_replay_inputs[player_idx_or_sentinel].key_mods & 3;
		if (key != 0) {
			XVT_LOG_DEBUG(
				"input.recorded_key slot=%d key=%u x=%d y=%d mods=%d",
				player_idx_or_sentinel, (unsigned)key,
				(int)g_ctrl_axis_x, (int)g_ctrl_axis_y,
				(int)g_key_mods);
		}
		return key;
	}
}

/* Returns nonzero when a key press waits. The modern build returns 0 while the
 * input is captured (xvt_input_is_captured), else dinput_skip_to_pending_key_press.
 * The original build asks dinput_skip_to_pending_key_press when
 * g_flight_conf_direct_input is set; otherwise it handles one window message,
 * waiting for one in GetMessageA unless g_flight_input_non_blocking_msg_pump is set
 * and none is queued, and returns g_key_ready. */
// FUNCTION: XVT 0x4AA7F0
int flight_input_has_key_ready(void)
{
	return xvt_input_is_captured() ? 0 : dinput_skip_to_pending_key_press();
}

/* Returns the next key press. The modern build returns 0 while the input is
 * captured, else dinput_get_key. The original build returns dinput_get_key when
 * g_flight_conf_direct_input is set; otherwise it handles window messages until
 * g_key_ready is set, clears it and returns g_last_key_code, or returns
 * g_last_key_code when GetMessageA reports the quit message. */
// FUNCTION: XVT 0x4AA870
int flight_input_get_next_key(void)
{
	return xvt_input_is_captured() ? 0 : dinput_get_key();
}
