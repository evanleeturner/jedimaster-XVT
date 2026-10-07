/* Tests for xvt/flight/flight_input.c: how the input for a player step is
 * applied from a recorded input, latched into the step's controls and cut by
 * the dead zone, and what the resets clear. Each check sets the input globals
 * it needs; no device is read and no game data either. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/flight_input.h"
#include "xvt_runtime/input/flight_controls.h"

/* The module defines these two; its header does not declare them. */
extern int g_held_joystick_buttons;
extern int g_throttle_smoothed;

enum {
	TEST_PLAYER = 3,
	TEST_KEY = 0x41,
};

/* Every input global the latch reads or writes, at values no check expects. */
static void fresh_input(void)
{
	g_action_key = 0;
	g_current_action_key = 0x77;
	g_flight_mouse_enabled = 0;
	g_flight_mouse_delta_x = 0;
	g_flight_mouse_delta_y = 0;
	g_mouse_buttons = 0;
	g_key_mods = 0;
	g_ctrl_axis_x = 0;
	g_ctrl_axis_y = 0;
	g_scaled_input_yaw = 0x123;
	g_scaled_input_pitch = 0x456;
	g_flight_key_mods = 0x80;
	memset(g_replay_inputs, 0, sizeof g_replay_inputs);
}

/* With the mouse off the latch copies the action key, turns the stick's axes
 * into yaw (times 120) and pitch (times 50), and takes the step's buttons from
 * g_key_mods alone: the mouse's buttons and movement are not read. */
static void check_latch_stick(void)
{
	fresh_input();
	g_action_key = TEST_KEY;
	g_ctrl_axis_x = 7;
	g_ctrl_axis_y = -9;
	g_key_mods = 2;
	g_mouse_buttons = 1;
	g_flight_mouse_delta_x = 5;
	g_flight_mouse_delta_y = 6;
	flight_input_latch_flight_controls();
	XVT_ASSERT_INT_EQ(g_current_action_key, TEST_KEY);
	XVT_ASSERT_INT_EQ(g_scaled_input_yaw, 7 * 120);
	XVT_ASSERT_INT_EQ(g_scaled_input_pitch, -9 * 50);
	XVT_ASSERT_INT_EQ(g_flight_key_mods, 2);

	g_ctrl_axis_x = 0;
	g_ctrl_axis_y = 0;
	flight_input_latch_flight_controls();
	XVT_ASSERT_INT_EQ(g_scaled_input_yaw, 0);
	XVT_ASSERT_INT_EQ(g_scaled_input_pitch, 0);
}

/* With the mouse on, its movement sets yaw (shifted left 7) and pitch
 * (shifted left 6), and its buttons join g_key_mods; an axis the mouse leaves
 * at 0 comes from the stick. */
static void check_latch_mouse(void)
{
	fresh_input();
	g_flight_mouse_enabled = 1;
	g_flight_mouse_delta_x = -3;
	g_flight_mouse_delta_y = 4;
	g_mouse_buttons = 1;
	g_key_mods = 2;
	g_ctrl_axis_x = 7;
	g_ctrl_axis_y = -9;
	flight_input_latch_flight_controls();
	XVT_ASSERT_INT_EQ(g_scaled_input_yaw, -3 * 128);
	XVT_ASSERT_INT_EQ(g_scaled_input_pitch, 4 * 64);
	XVT_ASSERT_INT_EQ(g_flight_key_mods, 3);

	g_flight_mouse_delta_x = 0;
	flight_input_latch_flight_controls();
	XVT_ASSERT_INT_EQ(g_scaled_input_yaw, 7 * 120);
	XVT_ASSERT_INT_EQ(g_scaled_input_pitch, 4 * 64);
	g_flight_mouse_delta_x = -3;
	g_flight_mouse_delta_y = 0;
	flight_input_latch_flight_controls();
	XVT_ASSERT_INT_EQ(g_scaled_input_yaw, -3 * 128);
	XVT_ASSERT_INT_EQ(g_scaled_input_pitch, -9 * 50);
}

/* Yaw of size 64 or less and pitch of size 24 or less become 0; larger
 * inputs, of either sign, are kept. */
static void check_deadzone(void)
{
	static const struct {
		int16_t yaw;
		int16_t pitch;
		int16_t yaw_after;
		int16_t pitch_after;
	} cases[] = {
		{64, 24, 0, 0},		{-64, -24, 0, 0},
		{65, 25, 65, 25},	{-65, -25, -65, -25},
		{-32767, 0, -32767, 0}, {0, 32767, 0, 32767},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		g_scaled_input_yaw = cases[i].yaw;
		g_scaled_input_pitch = cases[i].pitch;
		flight_input_apply_deadzone();
		XVT_ASSERT_INT_EQ(g_scaled_input_yaw, cases[i].yaw_after);
		XVT_ASSERT_INT_EQ(g_scaled_input_pitch, cases[i].pitch_after);
	}
}

/* Called with a player's index the read applies that player's recorded input:
 * its axes, its roll, its key (returned and put in g_action_key) and the low
 * two bits of its buttons. Another player's record is not read. */
static void check_read_recorded(void)
{
	fresh_input();
	g_replay_inputs[TEST_PLAYER].axis_x = -12;
	g_replay_inputs[TEST_PLAYER].axis_y = 34;
	g_replay_inputs[TEST_PLAYER].axis_r = -56;
	g_replay_inputs[TEST_PLAYER].key = TEST_KEY;
	g_replay_inputs[TEST_PLAYER].key_mods = 0xFE;
	g_replay_inputs[TEST_PLAYER + 1].key = 0x42;
	g_replay_inputs[TEST_PLAYER + 1].axis_x = 99;
	XVT_ASSERT_INT_EQ(flight_input_read(TEST_PLAYER), TEST_KEY);
	XVT_ASSERT_INT_EQ(g_action_key, TEST_KEY);
	XVT_ASSERT_INT_EQ(g_ctrl_axis_x, -12);
	XVT_ASSERT_INT_EQ(g_ctrl_axis_y, 34);
	XVT_ASSERT_INT_EQ(g_xvt_control_roll, -56);
	XVT_ASSERT_INT_EQ(g_key_mods, 2);

	g_replay_inputs[TEST_PLAYER].key = 0;
	g_replay_inputs[TEST_PLAYER].key_mods = 1;
	XVT_ASSERT_INT_EQ(flight_input_read(TEST_PLAYER), 0);
	XVT_ASSERT_INT_EQ(g_action_key, 0);
	XVT_ASSERT_INT_EQ(g_key_mods, 1);
}

/* The control reset sets the smoothed throttle to -1, no reading yet, and
 * the held joystick buttons to 0. */
static void check_reset_control_state(void)
{
	g_throttle_smoothed = 200;
	g_held_joystick_buttons = 0x1234;
	flight_input_reset_control_state();
	XVT_ASSERT_INT_EQ(g_throttle_smoothed, -1);
	XVT_ASSERT_INT_EQ(g_held_joystick_buttons, 0);
}

/* The flight-start reset clears the mouse buttons, g_key_mods and the mouse
 * flag, records whether a joystick was found, and resets the controls. */
static void check_reset_runtime_state(void)
{
	fresh_input();
	g_mouse_buttons = 3;
	g_key_mods = 3;
	g_flight_mouse_enabled = 1;
	g_joystick_available = 7;
	g_throttle_smoothed = 200;
	g_held_joystick_buttons = 0x1234;
	flight_input_reset_runtime_state();
	XVT_ASSERT_INT_EQ(g_mouse_buttons, 0);
	XVT_ASSERT_INT_EQ(g_key_mods, 0);
	XVT_ASSERT_INT_EQ(g_flight_mouse_enabled, 0);
	XVT_ASSERT_INT_EQ(g_joystick_available,
			  g_joystick_detect_result_word != 0);
	XVT_ASSERT_INT_EQ(g_throttle_smoothed, -1);
	XVT_ASSERT_INT_EQ(g_held_joystick_buttons, 0);
}

int main(void)
{
	check_latch_stick();
	check_latch_mouse();
	check_deadzone();
	check_read_recorded();
	check_reset_control_state();
	check_reset_runtime_state();
	return 0;
}
