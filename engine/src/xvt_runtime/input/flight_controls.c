#include "xvt_runtime/input/flight_controls.h"

#include <string.h>

#include "aeron/aeron.h"
#include "aeron/debug.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/input/dinput.h"
#include "xvt/input/mouse.h"
#include "xvt/math/math2.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/input/input_bridge.h"
#include "xvt_runtime/input/keyboard_mapping.h"
#include "xvt_runtime/input/mouse_flight.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/flight_sim.h"
#include "xvt_runtime/runtime/flight_task.h"
#include "xvt_runtime/runtime/movie_task.h"
#include "xvt_runtime/runtime/port.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/player_timing.h"
int16_t g_xvt_control_roll;

static struct {
	bool valid;
	uint16_t position;
	uint32_t generation;
	uint32_t signature;
	int object;
} g_throttle_baseline;

static void xvt_flight_controls_reset_throttle(void)
{
	g_throttle_baseline.valid = false;
}

void xvt_flight_controls_reset(void)
{
	xvt_player_timing_reset_controls();
	g_xvt_control_roll = 0;
	xvt_flight_controls_reset_throttle();
}

bool xvt_flight_controls_throttle_eligible(unsigned player)
{
	if (player >= 8 || !g_players[player].participation_state ||
	    g_flight_mission_state.mission_end_pending ||
	    (g_flight_runtime_state_initialized > 1 &&
	     g_dormant_flight_region_session_early_return_flag) ||
	    g_players[player].awaiting_new_craft ||
	    g_players[player].hyperspace_phase ||
	    g_players[player].map_camera_state ||
	    g_players[player].view_state.player_input_blocked ||
	    g_players[player].chat_recipient_mode !=
		    FLIGHT_CHAT_RECIPIENT_INACTIVE) {
		return false;
	}
	int index = g_players[player].object_index;
	if (!g_object_table || index < 0 ||
	    index >= g_region_main_object_slot_end) {
		return false;
	}
	const struct object_record *object = &g_object_table[index];
	return object->object_type &&
	       object->object_signature ==
		       g_players[player].bound_object_signature &&
	       object->mobj && object->mobj->p_craft;
}

static bool xvt_flight_controls_local_throttle_eligible(void)
{
	const AeronInputSnapshot *input = Aeron_InputSnapshot();
	return input && input->has_focus && !xvt_input_is_captured() &&
	       !Aeron_DebugUiVisible() &&
	       xvt_input_reconcile_keyboard() != XVT_KEYBOARD_BLOCKED &&
	       xvt_flight_task_is_active() && !xvt_flight_task_is_loading() &&
	       !xvt_flight_sim_is_paused() && !xvt_dialog_is_active() &&
	       !xvt_movie_task_is_active() && !xvt_resync_is_active() &&
	       xvt_flight_controls_throttle_eligible((unsigned)g_local_player);
}

void xvt_flight_controls_update_throttle_context(void)
{
	if (!xvt_flight_controls_local_throttle_eligible()) {
		xvt_flight_controls_reset_throttle();
	}
}

void xvt_flight_controls_sample_throttle(
	struct flight_input_frame_record *record)
{
	uint16_t position;
	uint32_t generation;
	record->flags = 0;
	record->throttle = 0;
	bool pause = record->key == FLIGHT_KEY_ALT_P &&
		     g_flight_player_count == 1 &&
		     !xvt_port_network_requires_progress();
	if (pause || !xvt_flight_controls_local_throttle_eligible() ||
	    !xvt_controller_mapping_throttle_sample(&position, &generation)) {
		xvt_flight_controls_reset_throttle();
		return;
	}
	int object = g_players[g_local_player].object_index;
	uint32_t signature = g_players[g_local_player].bound_object_signature;
	if (!g_throttle_baseline.valid ||
	    g_throttle_baseline.generation != generation ||
	    g_throttle_baseline.object != object ||
	    g_throttle_baseline.signature != signature) {
		g_throttle_baseline.valid = true;
		g_throttle_baseline.position = position;
		g_throttle_baseline.generation = generation;
		g_throttle_baseline.object = object;
		g_throttle_baseline.signature = signature;
		return;
	}
	int delta = (int)position - g_throttle_baseline.position;
	if (delta < 0) {
		delta = -delta;
	}
	/* 0.1% jitter threshold; small movements accumulate against the last accepted position. */
	if (delta && (delta >= 66 || position == 0 || position == UINT16_MAX)) {
		g_throttle_baseline.position = position;
		record->flags = XVT_INPUT_THROTTLE_PRESENT;
		record->throttle = position;
	}
}

void xvt_flight_controls_apply_throttle(
	unsigned player, const struct flight_input_frame_record *input)
{
	if ((input->flags & XVT_INPUT_THROTTLE_PRESENT) &&
	    xvt_flight_controls_throttle_eligible(player)) {
		g_object_table[g_players[player].object_index]
			.mobj->p_craft->throttle_speed = input->throttle;
	}
}

uint16_t xvt_flight_controls_read_local(void)
{
	xvt_keyboard_route keyboard = xvt_input_reconcile_keyboard();
	g_ctrl_axis_x = g_ctrl_axis_y = g_xvt_control_roll = 0;
	g_key_mods = g_mouse_buttons = g_action_key = 0;
	g_flight_mouse_delta_x = g_flight_mouse_delta_y = 0;
	if (keyboard == XVT_KEYBOARD_BLOCKED) {
		xvt_flight_controls_reset();
		return 0;
	}
	g_ctrl_axis_x =
		(int16_t)xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW);
	g_ctrl_axis_y =
		(int16_t)xvt_controller_mapping_axis(XVT_INPUT_AXIS_PITCH);
	g_xvt_control_roll =
		(int16_t)xvt_controller_mapping_axis(XVT_INPUT_AXIS_ROLL);
	g_key_mods = xvt_controller_mapping_modifiers();
	if (keyboard == XVT_KEYBOARD_GAMEPLAY) {
		g_key_mods |= xvt_keyboard_mapping_read_buttons();
	}
	if (xvt_config_settings()->mouse.mouse_flight_enabled) {
		if (xvt_mouse_flight_sample()) {
			int yaw;
			int pitch;
			int roll;
			xvt_mouse_flight_get_axes(&yaw, &pitch, &roll);
			if (yaw) {
				g_ctrl_axis_x = (int16_t)yaw;
			}
			if (pitch) {
				g_ctrl_axis_y = (int16_t)pitch;
			}
			if (roll) {
				g_xvt_control_roll = (int16_t)roll;
			}
			g_key_mods |= xvt_mouse_flight_buttons_mask();
		}
	} else if (g_flight_mouse_enabled) {
		g_mouse_buttons = (uint16_t)mouse_read_position_and_buttons(
			&g_flight_mouse_x, &g_flight_mouse_y);
		mouse_read_delta(&g_flight_mouse_delta_x,
				 &g_flight_mouse_delta_y);
		if (g_flight_mouse_delta_x < -191) {
			g_flight_mouse_delta_x = -191;
		}
		if (g_flight_mouse_delta_x > 191) {
			g_flight_mouse_delta_x = 191;
		}
		if (g_flight_mouse_delta_y < -127) {
			g_flight_mouse_delta_y = -127;
		}
		if (g_flight_mouse_delta_y > 127) {
			g_flight_mouse_delta_y = 127;
		}
	}
	uint16_t key =
		keyboard == XVT_KEYBOARD_GAMEPLAY
			? xvt_keyboard_mapping_read_key()
			: (dinput_skip_to_pending_key_press() ? dinput_get_key()
							      : 0);
	if (!key) {
		key = xvt_controller_mapping_read_key();
	}
	if (!key) {
		key = xvt_mouse_flight_read_key();
	}
	g_action_key = key;
	return g_action_key;
}

void xvt_flight_controls_encode_axes(
	uint8_t *bytes, const struct flight_input_frame_record *input)
{
	bytes[0] = ((uint8_t)input->axis_x & 0xfeu) | (input->key_mods & 1u);
	bytes[1] = ((uint8_t)input->axis_y & 0xfeu) |
		   ((input->key_mods >> 1) & 1u);
	bytes[2] = (uint8_t)input->axis_r & 0xfeu;
}

void xvt_flight_controls_decode_axes(const uint8_t *bytes,
				     struct flight_input_frame_record *input)
{
	input->axis_x = (int8_t)(bytes[0] & 0xfeu);
	input->axis_y = (int8_t)(bytes[1] & 0xfeu);
	input->axis_r = (int8_t)(bytes[2] & 0xfeu);
	input->key_mods = (bytes[0] & 1u) | ((bytes[1] & 1u) << 1);
}

int16_t xvt_flight_controls_roll_step(unsigned player, uint16_t roll_rate,
				      int16_t modifier_step)
{
	if (!g_xvt_control_roll) {
		xvt_player_timing_clear(player, XVT_PLAYER_ROLL);
		return modifier_step;
	}
	int raw = g_xvt_control_roll * 120;
	unsigned magnitude = (unsigned)(raw < 0 ? -raw : raw);
	unsigned whole = roll_rate / 0x3800;
	uint16_t fraction =
		(uint16_t)math2_ratio_q16(roll_rate % 0x3800, 0x3800);
	int target =
		(int)(magnitude * whole + math2_fraction(magnitude, fraction));
	if (raw < 0) {
		target = -target;
	}
	int step = xvt_flight_timing_is_unlocked()
			   ? xvt_player_timing_scale(player, XVT_PLAYER_ROLL,
						     (int16_t)target,
						     g_elapsed_ticks, 236)
			   : player_scale_control_step_by_elapsed_ticks(
				     (int16_t)target);
	int limit = player_scale_control_step_by_elapsed_ticks(
		(int16_t)(127 * 120 * whole +
			  math2_fraction(127 * 120, fraction)));
	step += modifier_step;
	if (step > limit) {
		step = limit;
	}
	if (step < -limit) {
		step = -limit;
	}
	return (int16_t)step;
}

void xvt_flight_controls_sample_recorded(
	struct flight_input_frame_record *input)
{
	enum {
		AXIS_QUANTIZATION_MASK = 0xfe,
		RECORDED_MODIFIERS = 3,
		YAW_AXIS_SCALE = 120,
		PITCH_AXIS_SCALE = 50,
		MOUSE_YAW_SCALE = 128,
		MOUSE_PITCH_SCALE = 64,
		MAX_AXIS = INT8_MAX & AXIS_QUANTIZATION_MASK
	};

	flight_input_read(-2);
	memset(input, 0, sizeof *input);
	input->key = (uint8_t)g_action_key;
	input->axis_x = (int8_t)(g_ctrl_axis_x & AXIS_QUANTIZATION_MASK);
	input->axis_y = (int8_t)(g_ctrl_axis_y & AXIS_QUANTIZATION_MASK);
	input->axis_r = (int8_t)(g_xvt_control_roll & AXIS_QUANTIZATION_MASK);
	input->key_mods = (g_key_mods | g_mouse_buttons) & RECORDED_MODIFIERS;
	xvt_flight_controls_sample_throttle(input);
	if (g_flight_mouse_enabled) {
		int yaw = g_flight_mouse_delta_x * MOUSE_YAW_SCALE /
			  YAW_AXIS_SCALE;
		int pitch = g_flight_mouse_delta_y * MOUSE_PITCH_SCALE /
			    PITCH_AXIS_SCALE;
		if (yaw) {
			input->axis_x = (int8_t)((yaw < INT8_MIN   ? INT8_MIN
						  : yaw > MAX_AXIS ? MAX_AXIS
								   : yaw) &
						 AXIS_QUANTIZATION_MASK);
		}
		if (pitch) {
			input->axis_y = (int8_t)((pitch < INT8_MIN   ? INT8_MIN
						  : pitch > MAX_AXIS ? MAX_AXIS
								     : pitch) &
						 AXIS_QUANTIZATION_MASK);
		}
	}
}

void xvt_flight_controls_recover(void)
{
	xvt_input_flush_keyboard();
	xvt_controller_mapping_drop_commands();
	xvt_mouse_flight_discard_pending();
	g_action_key = 0;
	xvt_flight_controls_reset_throttle();
}
