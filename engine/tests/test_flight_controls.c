/* Checks local flight input (xvt_runtime/input/flight_controls.h) against the
 * promises in its header: packing recorded axes, which player may use the
 * throttle lever and what applying a recorded lever does, the roll step's
 * limits, and what Reset, Recover and a blocked read clear. The test builds its
 * own world (an object table with one craft) and sets the game's input globals
 * and Aeron's input snapshot itself; every case starts from that world with the
 * local player eligible and the window focused.
 *
 * Not checked here: ReadLocal on an open keyboard route and SampleRecorded need
 * loaded settings and the game's DirectInput keyboard device, and a lever
 * position is only sent during a running flight. */
#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/input/keyboard_mapping.h"
#include "xvt_runtime/timing/flight_timing.h"

enum { PLAYER = 3, SLOT = 1, SIGNATURE = 0x0155 };

static struct object_record g_test_objects[2];
static struct mobile_object g_test_mobiles[2];
static struct craft_data g_test_craft[1];

static AeronInputSnapshot *flight_controls_host(void)
{
	return (AeronInputSnapshot *)Aeron_InputSnapshot();
}

/* Player PLAYER flies the craft in main slot SLOT, bound to it by signature;
 * nothing is in the way. */
static void flight_controls_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_players, 0, sizeof g_players);
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	g_flight_runtime_state_initialized = 0;
	g_dormant_flight_region_session_early_return_flag = 0;
	g_object_table = g_test_objects;
	g_region_main_object_slot_end = 2;
	g_test_objects[SLOT].object_type = 1;
	g_test_objects[SLOT].object_signature = SIGNATURE;
	g_test_objects[SLOT].mobj = &g_test_mobiles[SLOT];
	g_test_mobiles[SLOT].p_craft = &g_test_craft[0];
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
	}
	g_players[PLAYER].participation_state = 1;
	g_players[PLAYER].object_index = SLOT;
	g_players[PLAYER].bound_object_signature = SIGNATURE;
	g_players[PLAYER].chat_recipient_mode = FLIGHT_CHAT_RECIPIENT_INACTIVE;
	g_local_player = PLAYER;
	g_flight_player_count = 1;

	xvt_input_reset_capture();
	AeronInputSnapshot *host = flight_controls_host();
	uint64_t frame = host->frame_id;
	memset(host, 0, sizeof *host);
	host->frame_id = frame + 1;
	host->has_focus = 1;
}

static void set_game_input(void)
{
	g_action_key = 0x41;
	g_ctrl_axis_x = 12;
	g_ctrl_axis_y = -12;
	g_xvt_control_roll = 9;
	g_key_mods = 3;
	g_mouse_buttons = 1;
	g_flight_mouse_delta_x = 4;
	g_flight_mouse_delta_y = -4;
}

/* A gamepad model with yaw on the left stick and fire on the west button, and
 * that gamepad connected in Aeron's snapshot with the stick pushed and fire
 * held after a first frame with it released. */
static void controller_firing(void)
{
	static struct xvt_controller_options options;
	memset(&options, 0, sizeof options);
	struct xvt_controller_model *model = &options.models[0];
	memcpy(model->guid, "0123456789abcdef0123456789abcdea",
	       sizeof model->guid);
	model->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	xvt_controller_options_clear_profile(&model->profile,
					     AERON_CONTROLLER_KIND_GAMEPAD);
	model->profile.mapping.axes[XVT_INPUT_AXIS_YAW].source =
		AERON_GAMEPAD_AXIS_LEFTX;
	model->profile.bindings[0].source.kind =
		AERON_CONTROLLER_DIGITAL_BUTTON;
	model->profile.bindings[0].source.index = AERON_GAMEPAD_BUTTON_WEST;
	model->profile.bindings[0].source.threshold = 0.5f;
	model->profile.bindings[0].action = XVT_INPUT_ACTION_FIRE_WEAPON;
	model->profile.binding_count = 1;
	options.count = 1;
	xvt_controller_mapping_init(&options);

	AeronControllerSnapshot *pad = &flight_controls_host()->controllers[0];
	pad->connected = 1;
	pad->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	pad->instance_id = 5;
	memcpy(pad->guid, model->guid, sizeof pad->guid);
	pad->gamepad_available_axes = 1u << AERON_GAMEPAD_AXIS_LEFTX;
	pad->gamepad_available_buttons = 1u << AERON_GAMEPAD_BUTTON_WEST;
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = 16384;
	xvt_controller_mapping_update(flight_controls_host());
	++flight_controls_host()->frame_id;
	pad->gamepad_buttons = 1u << AERON_GAMEPAD_BUTTON_WEST;
	xvt_controller_mapping_update(flight_controls_host());
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 1);
	XVT_ASSERT_TRUE(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW) != 0);
}

static void check_encode_decode(void)
{
	for (int axis = -128; axis <= 127; ++axis) {
		for (int mods = 0; mods < 4; ++mods) {
			struct flight_input_frame_record in;
			memset(&in, 0, sizeof in);
			in.axis_x = (int8_t)axis;
			in.axis_y = (int8_t)(-1 - axis);
			in.axis_r = (int8_t)axis;
			in.key_mods = (uint8_t)mods;
			uint8_t bytes[XVT_FLIGHT_AXIS_BYTES + 1];
			memset(bytes, 0xC3, sizeof bytes);
			xvt_flight_controls_encode_axes(bytes, &in);
			XVT_ASSERT_INT_EQ(bytes[XVT_FLIGHT_AXIS_BYTES], 0xC3);
			XVT_ASSERT_INT_EQ(bytes[0] & 1, mods & 1);
			XVT_ASSERT_INT_EQ(bytes[1] & 1, (mods >> 1) & 1);

			struct flight_input_frame_record out;
			memset(&out, 0, sizeof out);
			xvt_flight_controls_decode_axes(bytes, &out);
			XVT_ASSERT_INT_EQ(out.key_mods, mods);
			const int8_t sent[3] = {in.axis_x, in.axis_y,
						in.axis_r};
			const int8_t back[3] = {out.axis_x, out.axis_y,
						out.axis_r};
			for (int i = 0; i < 3; ++i) {
				/* Axes come back even: an even axis exactly, an
				 * odd one off by one. */
				XVT_ASSERT_INT_EQ(back[i] % 2, 0);
				XVT_ASSERT_TRUE(abs(back[i] - sent[i]) <= 1);
				if (sent[i] % 2 == 0) {
					XVT_ASSERT_INT_EQ(back[i], sent[i]);
				}
			}
		}
	}
}

static void check_throttle_eligible(void)
{
	flight_controls_world();
	XVT_ASSERT_TRUE(xvt_flight_controls_throttle_eligible(PLAYER));
	XVT_ASSERT_TRUE(!xvt_flight_controls_throttle_eligible(0));

	flight_controls_world();
	g_players[PLAYER].participation_state = 0;
	XVT_ASSERT_TRUE(!xvt_flight_controls_throttle_eligible(PLAYER));
	flight_controls_world();
	g_flight_mission_state.mission_end_pending = 1;
	XVT_ASSERT_TRUE(!xvt_flight_controls_throttle_eligible(PLAYER));
	flight_controls_world();
	g_players[PLAYER].awaiting_new_craft = 1;
	XVT_ASSERT_TRUE(!xvt_flight_controls_throttle_eligible(PLAYER));
	flight_controls_world();
	g_players[PLAYER].hyperspace_phase = 1;
	XVT_ASSERT_TRUE(!xvt_flight_controls_throttle_eligible(PLAYER));
	flight_controls_world();
	g_players[PLAYER].map_camera_state = 1;
	XVT_ASSERT_TRUE(!xvt_flight_controls_throttle_eligible(PLAYER));
	flight_controls_world();
	g_players[PLAYER].view_state.player_input_blocked = 1;
	XVT_ASSERT_TRUE(!xvt_flight_controls_throttle_eligible(PLAYER));
	flight_controls_world();
	g_players[PLAYER].chat_recipient_mode = FLIGHT_CHAT_RECIPIENT_TEAM;
	XVT_ASSERT_TRUE(!xvt_flight_controls_throttle_eligible(PLAYER));

	/* Their bound, live craft: another signature, an empty slot or no craft
	 * record will not do. */
	flight_controls_world();
	g_players[PLAYER].bound_object_signature = SIGNATURE + 1;
	XVT_ASSERT_TRUE(!xvt_flight_controls_throttle_eligible(PLAYER));
	flight_controls_world();
	g_test_objects[SLOT].object_type = 0;
	XVT_ASSERT_TRUE(!xvt_flight_controls_throttle_eligible(PLAYER));
	flight_controls_world();
	g_test_mobiles[SLOT].p_craft = NULL;
	XVT_ASSERT_TRUE(!xvt_flight_controls_throttle_eligible(PLAYER));
}

static void check_apply_throttle(void)
{
	struct flight_input_frame_record record;
	memset(&record, 0, sizeof record);
	record.flags = XVT_INPUT_THROTTLE_PRESENT;
	record.throttle = 1234;

	flight_controls_world();
	g_test_craft[0].throttle_speed = 7;
	xvt_flight_controls_apply_throttle(PLAYER, &record);
	XVT_ASSERT_INT_EQ(g_test_craft[0].throttle_speed, 1234);

	/* No lever position recorded: the craft keeps its throttle. */
	flight_controls_world();
	g_test_craft[0].throttle_speed = 7;
	record.flags = 0;
	xvt_flight_controls_apply_throttle(PLAYER, &record);
	XVT_ASSERT_INT_EQ(g_test_craft[0].throttle_speed, 7);

	/* Not eligible: the same. */
	flight_controls_world();
	g_test_craft[0].throttle_speed = 7;
	record.flags = XVT_INPUT_THROTTLE_PRESENT;
	g_players[PLAYER].hyperspace_phase = 1;
	xvt_flight_controls_apply_throttle(PLAYER, &record);
	XVT_ASSERT_INT_EQ(g_test_craft[0].throttle_speed, 7);
}

static void check_sample_throttle_sends_nothing(void)
{
	/* No lever was read: no controller mapping is installed. */
	flight_controls_world();
	xvt_controller_mapping_shutdown();
	struct flight_input_frame_record record;
	memset(&record, 0, sizeof record);
	record.flags = 0xFF;
	xvt_flight_controls_sample_throttle(&record);
	XVT_ASSERT_INT_EQ(record.flags & XVT_INPUT_THROTTLE_PRESENT, 0);

	/* The local player is not eligible. */
	flight_controls_world();
	g_players[PLAYER].participation_state = 0;
	memset(&record, 0, sizeof record);
	record.flags = 0xFF;
	xvt_flight_controls_sample_throttle(&record);
	XVT_ASSERT_INT_EQ(record.flags & XVT_INPUT_THROTTLE_PRESENT, 0);

	/* Alt-P in a one-player flight. */
	flight_controls_world();
	memset(&record, 0, sizeof record);
	record.key = FLIGHT_KEY_ALT_P;
	record.flags = 0xFF;
	xvt_flight_controls_sample_throttle(&record);
	XVT_ASSERT_INT_EQ(record.flags & XVT_INPUT_THROTTLE_PRESENT, 0);
}

static void check_roll_step(void)
{
	/* Steps from one tick to one second of ticks, and rates up to twice the
	 * unit rate: full deflection then fits the 16-bit result. These
	 * relationships hold for each. */
	XVT_ASSERT_INT_EQ(xvt_flight_timing_is_unlocked(), 0);
	static const uint16_t k_ticks[] = {1, 4, 59,
					   SIMULATION_TICKS_PER_SECOND};
	static const uint16_t k_rates[] = {0x1C00, 0x3800, 0x5555, 0x7000};
	static const int16_t k_modifiers[] = {-30000, -500, -1,	  0,
					      1,      500,  30000};
	for (size_t t = 0; t < sizeof k_ticks / sizeof k_ticks[0]; ++t) {
		for (size_t r = 0; r < sizeof k_rates / sizeof k_rates[0];
		     ++r) {
			g_elapsed_ticks = k_ticks[t];
			uint16_t rate = k_rates[r];

			/* With the roll axis at 0 the step is modifier_step, whatever it is. */
			g_xvt_control_roll = 0;
			for (size_t m = 0;
			     m < sizeof k_modifiers / sizeof k_modifiers[0];
			     ++m) {
				XVT_ASSERT_INT_EQ(
					xvt_flight_controls_roll_step(
						0, rate, k_modifiers[m]),
					k_modifiers[m]);
			}

			/* Full deflection is the limit, in both directions. */
			g_xvt_control_roll = 127;
			int full = xvt_flight_controls_roll_step(0, rate, 0);
			XVT_ASSERT_TRUE(full >= 0);
			XVT_ASSERT_INT_EQ(
				xvt_flight_controls_roll_step(0, rate, 30000),
				full);
			g_xvt_control_roll = -127;
			XVT_ASSERT_INT_EQ(
				xvt_flight_controls_roll_step(0, rate, -30000),
				-full);

			for (int roll = -127; roll <= 127; roll += 6) {
				g_xvt_control_roll = (int16_t)roll;
				int plain = xvt_flight_controls_roll_step(
					0, rate, 0);
				/* The step follows the axis's direction and
				 * never passes full deflection. */
				XVT_ASSERT_TRUE(roll > 0 ? plain >= 0
							 : plain <= 0);
				for (size_t m = 0;
				     m <
				     sizeof k_modifiers / sizeof k_modifiers[0];
				     ++m) {
					int step =
						xvt_flight_controls_roll_step(
							0, rate,
							k_modifiers[m]);
					XVT_ASSERT_TRUE(step >= -full &&
							step <= full);
					/* modifier_step adds on, while the sum
					 * stays inside the limit. */
					if (plain + k_modifiers[m] >= -full &&
					    plain + k_modifiers[m] <= full) {
						XVT_ASSERT_INT_EQ(
							step,
							plain + k_modifiers[m]);
					}
				}
			}
		}
	}
	g_xvt_control_roll = 0;
}

static void check_reset(void)
{
	flight_controls_world();
	g_xvt_control_roll = -40;
	xvt_flight_controls_reset();
	XVT_ASSERT_INT_EQ(g_xvt_control_roll, 0);
}

static void check_blocked_read_clears(void)
{
	/* Without focus the keyboard route blocks: the read returns 0 and
	 * clears the game's input, even with a controller pushing yaw and
	 * holding fire. */
	flight_controls_world();
	controller_firing();
	set_game_input();
	flight_controls_host()->has_focus = 0;
	XVT_ASSERT_INT_EQ(xvt_flight_controls_read_local(), 0);
	XVT_ASSERT_INT_EQ(g_ctrl_axis_x, 0);
	XVT_ASSERT_INT_EQ(g_ctrl_axis_y, 0);
	XVT_ASSERT_INT_EQ(g_xvt_control_roll, 0);
	XVT_ASSERT_INT_EQ(g_key_mods, 0);
	XVT_ASSERT_INT_EQ(g_mouse_buttons, 0);
	XVT_ASSERT_INT_EQ(g_action_key, 0);
	XVT_ASSERT_INT_EQ(g_flight_mouse_delta_x, 0);
	XVT_ASSERT_INT_EQ(g_flight_mouse_delta_y, 0);

	/* Captured input blocks it the same way. */
	flight_controls_world();
	controller_firing();
	xvt_input_set_captured(true);
	set_game_input();
	XVT_ASSERT_INT_EQ(xvt_flight_controls_read_local(), 0);
	XVT_ASSERT_INT_EQ(g_ctrl_axis_x, 0);
	XVT_ASSERT_INT_EQ(g_key_mods, 0);
	XVT_ASSERT_INT_EQ(g_action_key, 0);
	xvt_input_set_captured(false);
	xvt_controller_mapping_shutdown();
}

static void check_recover(void)
{
	flight_controls_world();
	controller_firing();

	static struct xvt_keyboard_bindings profile;
	memset(&profile, 0, sizeof profile);
	profile.bindings[0].source.key = AERON_KEY_A;
	profile.bindings[0].action = XVT_INPUT_ACTION_TARGET_NEXT;
	profile.count = 1;
	xvt_keyboard_mapping_install(&profile);
	xvt_keyboard_mapping_enable(true, flight_controls_host());
	AeronKeyEvent press;
	memset(&press, 0, sizeof press);
	press.chord.key = AERON_KEY_A;
	press.down = 1;
	xvt_keyboard_mapping_event(&press, false);
	flight_controls_host()->key_down[AERON_KEY_A + 1] = 1;
	g_action_key = 0x41;

	xvt_flight_controls_recover();
	XVT_ASSERT_INT_EQ(g_action_key, 0);
	/* The keyboard is flushed: the queued key is gone and the held key is
	 * blocked from the game. */
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(), 0);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_A + 1), 1);
	/* Controller commands are released: fire is no longer held, and stays
	 * so while the button is. */
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 0);
	++flight_controls_host()->frame_id;
	xvt_controller_mapping_update(flight_controls_host());
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 0);
	xvt_controller_mapping_shutdown();
	xvt_keyboard_mapping_suspend();
}

/* Known failure: the header gives roll_rate no upper limit, but above about
 * 0x7866 full deflection no longer fits the 16-bit step, and the limit itself
 * wraps negative. At 0x8000 a left roll comes back as a right roll. The step is
 * the roll axis scaled by positive factors and limited to full deflection, so
 * it must keep the axis's sign. */
static void known_failure_roll_step_fast_rate(void)
{
	XVT_ASSERT_INT_EQ(xvt_flight_timing_is_unlocked(), 0);
	g_elapsed_ticks = 59;
	g_xvt_control_roll = -60;
	int step = xvt_flight_controls_roll_step(0, 0x8000, 0);
	g_xvt_control_roll = 0;
	XVT_ASSERT_TRUE(step <= 0);
}

/* Runs every check, or with "known-failure <name>" only that known failure; an
 * unknown name passes. */
int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		if (strcmp(argv[2], "roll_step_fast_rate") == 0) {
			known_failure_roll_step_fast_rate();
		}
		return 0;
	}
	check_encode_decode();
	check_throttle_eligible();
	check_apply_throttle();
	check_sample_throttle_sends_nothing();
	check_roll_step();
	check_reset();
	check_blocked_read_clears();
	check_recover();
	xvt_input_reset_capture();
	return 0;
}
