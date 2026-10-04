/* Checks keyboard and mouse ownership (xvt_runtime/input/capture.h) against the
 * promises in its header: what capturing clears, which keys and buttons stay
 * blocked across a handover and when they unblock, when mouse motion counts,
 * Tab hiding, and the keyboard flushes. The test plays the host: it writes each
 * frame into Aeron's input snapshot, the one the module reads, and sets the
 * game's input globals itself. A key the game cannot see shows as suppressed in
 * Aeron's compatibility layer, which is what the game reads keys through. Every
 * case starts from ResetCapture and an empty frame with focus.
 *
 * Not checked here: UpdateMouseCapture and the true side of MouseFlightAllowed
 * need loaded settings and a running flight, and relative mouse mode needs a
 * window; DirectInput's buffer drain needs its device. */
#include <string.h>

#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "aeron/input.h"
#include "test_assert.h"
#include "xvt/flight/flight_input.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/input/dinput.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/input/keyboard_mapping.h"

enum { ALL_BUTTONS = 0x1F };

static const int k_a = AERON_KEY_A;
static const int k_c = AERON_KEY_A + 2;
static const int k_d = AERON_KEY_A + 3;
static const int k_t = AERON_KEY_A + ('t' - 'a');

/* Aeron's snapshot is the host's frame; the test writes it as the host would. */
static AeronInputSnapshot *capture_host(void)
{
	return (AeronInputSnapshot *)Aeron_InputSnapshot();
}

/* An empty frame with focus, numbered after the last one. */
static void new_frame(void)
{
	AeronInputSnapshot *host = capture_host();
	uint64_t frame = host->frame_id;
	memset(host, 0, sizeof *host);
	host->frame_id = frame + 1;
	host->has_focus = 1;
}

/* The next frame with the same keys and buttons held, and nothing just pressed or released. */
static void next_frame(void)
{
	AeronInputSnapshot *host = capture_host();
	++host->frame_id;
	memset(host->key_released, 0, sizeof host->key_released);
	host->mouse.released_buttons = 0;
}

static void capture_start(void)
{
	xvt_input_reset_capture();
	new_frame();
}

static int suppressed_count(void)
{
	int count = 0;
	for (int key = 0; key < AERON_KEY_COUNT; ++key) {
		count += AeronCompat_IsKeySuppressed(key) != 0;
	}
	return count;
}

static void set_game_input(void)
{
	g_action_key = 0x41;
	g_ctrl_axis_x = 12;
	g_ctrl_axis_y = -12;
	g_key_mods = 3;
	g_mouse_buttons = 1;
	g_flight_mouse_delta_x = 4;
	g_flight_mouse_delta_y = -4;
	g_xvt_control_roll = 9;
}

/* A keyboard mapping that queues next target for T, enabled with no key held. */
static void keyboard_mapping_with_t(void)
{
	static struct xvt_keyboard_bindings profile;
	memset(&profile, 0, sizeof profile);
	profile.bindings[0].source.key = (uint16_t)k_t;
	profile.bindings[0].action = XVT_INPUT_ACTION_TARGET_NEXT;
	profile.count = 1;
	xvt_keyboard_mapping_install(&profile);
	xvt_keyboard_mapping_enable(true, capture_host());
}

static void press_t(void)
{
	AeronKeyEvent event;
	memset(&event, 0, sizeof event);
	event.chord.key = (uint16_t)k_t;
	event.down = 1;
	xvt_keyboard_mapping_event(&event, false);
}

static void check_suppress_key(void)
{
	capture_start();
	XVT_ASSERT_INT_EQ(suppressed_count(), 0);
	xvt_input_block_key_until_released(k_a);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(k_a), 1);
	XVT_ASSERT_INT_EQ(suppressed_count(), 1);

	/* A key out of range is ignored. */
	xvt_input_block_key_until_released(-1);
	xvt_input_block_key_until_released(AERON_KEY_COUNT);
	XVT_ASSERT_INT_EQ(suppressed_count(), 1);

	/* Blocked until released: held, then just released, then up. */
	next_frame();
	capture_host()->key_down[k_a] = 1;
	xvt_input_begin_capture_frame(capture_host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(k_a), 1);
	next_frame();
	capture_host()->key_down[k_a] = 0;
	capture_host()->key_released[k_a] = 1;
	xvt_input_begin_capture_frame(capture_host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(k_a), 1);
	next_frame();
	xvt_input_begin_capture_frame(capture_host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(k_a), 0);
}

static void check_block_held_keys(void)
{
	capture_start();
	capture_host()->key_down[k_a] = 1;
	xvt_input_block_held_keys();
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(k_a), 1);
	XVT_ASSERT_INT_EQ(suppressed_count(), 1);
	next_frame();
	capture_host()->key_down[k_a] = 0;
	xvt_input_begin_capture_frame(capture_host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(k_a), 0);
}

static void check_capture_clears_game_input(void)
{
	capture_start();
	set_game_input();
	xvt_input_set_captured(true);
	XVT_ASSERT_INT_EQ(xvt_input_is_captured(), 1);
	XVT_ASSERT_INT_EQ(g_action_key, 0);
	XVT_ASSERT_INT_EQ(g_ctrl_axis_x, 0);
	XVT_ASSERT_INT_EQ(g_ctrl_axis_y, 0);
	XVT_ASSERT_INT_EQ(g_key_mods, 0);
	XVT_ASSERT_INT_EQ(g_mouse_buttons, 0);
	XVT_ASSERT_INT_EQ(g_flight_mouse_delta_x, 0);
	XVT_ASSERT_INT_EQ(g_flight_mouse_delta_y, 0);
	XVT_ASSERT_INT_EQ(g_xvt_control_roll, 0);

	/* Releasing is a change too, and clears again. */
	set_game_input();
	xvt_input_set_captured(false);
	XVT_ASSERT_INT_EQ(xvt_input_is_captured(), 0);
	XVT_ASSERT_INT_EQ(g_action_key, 0);
	XVT_ASSERT_INT_EQ(g_ctrl_axis_x, 0);
	XVT_ASSERT_INT_EQ(g_key_mods, 0);

	/* No change does nothing: the game's input and the held key stay as they are. */
	set_game_input();
	capture_host()->key_down[k_d] = 1;
	xvt_input_set_captured(false);
	XVT_ASSERT_INT_EQ(g_action_key, 0x41);
	XVT_ASSERT_INT_EQ(g_xvt_control_roll, 9);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(k_d), 0);
}

static void check_handover_blocks_held_input(void)
{
	capture_start();
	/* While captured the host owns the keyboard and mouse: the game sees no
	 * key and no button. */
	xvt_input_begin_capture_frame(capture_host(), true);
	for (int key = 1; key < AERON_KEY_COUNT; ++key) {
		if (AeronKey_Name((AeronKey)key)[0] != '\0') {
			XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(key), 1);
		}
	}
	XVT_ASSERT_INT_EQ(xvt_input_filter_mouse_buttons(ALL_BUTTONS), 0);
	XVT_ASSERT_INT_EQ(xvt_input_mouse_motion_allowed(), 0);

	/* Handing back with C and the left button held blocks both, and this frame's motion. */
	next_frame();
	capture_host()->key_down[k_c] = 1;
	capture_host()->mouse.buttons = AERON_MOUSE_BUTTON_LEFT;
	xvt_input_begin_capture_frame(capture_host(), false);
	XVT_ASSERT_INT_EQ(xvt_input_is_captured(), 0);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(k_c), 1);
	XVT_ASSERT_INT_EQ(suppressed_count(), 1);
	XVT_ASSERT_INT_EQ(xvt_input_filter_mouse_buttons(ALL_BUTTONS),
			  ALL_BUTTONS & ~AERON_MOUSE_BUTTON_LEFT);
	XVT_ASSERT_INT_EQ(xvt_input_mouse_motion_allowed(), 0);

	/* Still held next frame: still blocked, but motion counts again. */
	next_frame();
	xvt_input_begin_capture_frame(capture_host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(k_c), 1);
	XVT_ASSERT_INT_EQ(xvt_input_filter_mouse_buttons(ALL_BUTTONS),
			  ALL_BUTTONS & ~AERON_MOUSE_BUTTON_LEFT);
	XVT_ASSERT_INT_EQ(xvt_input_mouse_motion_allowed(), 1);

	/* Just released: still blocked. Up a frame later: both reach the game again. */
	next_frame();
	capture_host()->key_down[k_c] = 0;
	capture_host()->key_released[k_c] = 1;
	capture_host()->mouse.buttons = 0;
	capture_host()->mouse.released_buttons = AERON_MOUSE_BUTTON_LEFT;
	xvt_input_begin_capture_frame(capture_host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(k_c), 1);
	XVT_ASSERT_INT_EQ(xvt_input_filter_mouse_buttons(ALL_BUTTONS),
			  ALL_BUTTONS & ~AERON_MOUSE_BUTTON_LEFT);
	next_frame();
	xvt_input_begin_capture_frame(capture_host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(k_c), 0);
	XVT_ASSERT_INT_EQ(xvt_input_filter_mouse_buttons(ALL_BUTTONS),
			  ALL_BUTTONS);
}

static void check_capture_blocks_held_input(void)
{
	/* D and the right button, held through capture and its release, stay
	 * blocked from the game. */
	capture_start();
	capture_host()->key_down[k_d] = 1;
	capture_host()->mouse.buttons = AERON_MOUSE_BUTTON_RIGHT;
	xvt_input_set_captured(true);
	xvt_input_set_captured(false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(k_d), 1);
	XVT_ASSERT_INT_EQ(xvt_input_filter_mouse_buttons(ALL_BUTTONS),
			  ALL_BUTTONS & ~AERON_MOUSE_BUTTON_RIGHT);
}

static void check_mouse_motion_allowed(void)
{
	capture_start();
	XVT_ASSERT_INT_EQ(xvt_input_mouse_motion_allowed(), 1);
	capture_host()->has_focus = 0;
	XVT_ASSERT_INT_EQ(xvt_input_mouse_motion_allowed(), 0);
	capture_host()->has_focus = 1;
	xvt_input_set_captured(true);
	next_frame();
	XVT_ASSERT_INT_EQ(xvt_input_mouse_motion_allowed(), 0);
	xvt_input_set_captured(false);
	XVT_ASSERT_INT_EQ(xvt_input_mouse_motion_allowed(), 0);
	next_frame();
	XVT_ASSERT_INT_EQ(xvt_input_mouse_motion_allowed(), 1);
}

static void check_capture_suspends_controllers(void)
{
	capture_start();
	struct xvt_controller_options options;
	memset(&options, 0, sizeof options);
	struct xvt_controller_model *model = &options.models[0];
	memcpy(model->guid, "0123456789abcdef0123456789abcdea",
	       sizeof model->guid);
	model->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	xvt_controller_options_clear_profile(&model->profile,
					     AERON_CONTROLLER_KIND_GAMEPAD);
	model->profile.mapping.axes[XVT_INPUT_AXIS_YAW].source =
		AERON_GAMEPAD_AXIS_LEFTX;
	options.count = 1;
	xvt_controller_mapping_init(&options);

	AeronControllerSnapshot *pad = &capture_host()->controllers[0];
	pad->connected = 1;
	pad->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	pad->instance_id = 5;
	memcpy(pad->guid, model->guid, sizeof pad->guid);
	pad->gamepad_available_axes = 1u << AERON_GAMEPAD_AXIS_LEFTX;
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = 16384;
	xvt_controller_mapping_update(capture_host());
	XVT_ASSERT_TRUE(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW) != 0);

	xvt_input_set_captured(true);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW), 0);
	xvt_controller_mapping_shutdown();
}

static void check_renderer_tab(void)
{
	capture_start();
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_TAB), 0);
	xvt_input_suppress_renderer_tab(true);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_TAB), 1);
	XVT_ASSERT_INT_EQ(suppressed_count(), 1);

	/* It stays hidden across frames while suppress is true. */
	next_frame();
	xvt_input_begin_capture_frame(capture_host(), false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_TAB), 1);
	xvt_input_suppress_renderer_tab(false);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_TAB), 0);
}

static void check_reset_capture(void)
{
	capture_start();
	capture_host()->key_down[k_a] = 1;
	capture_host()->mouse.buttons = AERON_MOUSE_BUTTON_LEFT;
	xvt_input_set_captured(true);
	xvt_input_suppress_renderer_tab(true);
	xvt_input_block_key_until_released(k_d);

	xvt_input_reset_capture();
	XVT_ASSERT_INT_EQ(xvt_input_is_captured(), 0);
	XVT_ASSERT_INT_EQ(suppressed_count(), 0);
	XVT_ASSERT_INT_EQ(xvt_input_filter_mouse_buttons(ALL_BUTTONS),
			  ALL_BUTTONS);
}

static void put_char(char c)
{
	g_front_state.char_ring_buffer[g_front_state.char_write_idx] = c;
	g_front_state.char_write_idx =
		(g_front_state.char_write_idx + 1) % 1024;
}

static void set_raw_keyboard(void)
{
	keyboard_flush_char_buffer();
	g_dinput_shift_down = 1;
	g_dinput_ctrl_down = 1;
	g_dinput_alt_down = 1;
	g_key_ready = 1;
	g_last_key_code = 0x1E;
	put_char('x');
}

static void check_flush_raw_keyboard(void)
{
	capture_start();
	XVT_ASSERT_TRUE(g_dinput_keyboard_device == NULL);
	set_raw_keyboard();
	XVT_ASSERT_TRUE(keyboard_peek_char() == 'x');
	xvt_input_flush_raw_keyboard();
	XVT_ASSERT_INT_EQ(g_dinput_shift_down, 0);
	XVT_ASSERT_INT_EQ(g_dinput_ctrl_down, 0);
	XVT_ASSERT_INT_EQ(g_dinput_alt_down, 0);
	XVT_ASSERT_INT_EQ(g_key_ready, 0);
	XVT_ASSERT_INT_EQ(g_last_key_code, 0);
	XVT_ASSERT_INT_EQ(keyboard_peek_char(), 0);
}

static void check_flush_keyboard(void)
{
	capture_start();
	keyboard_mapping_with_t();
	press_t();
	capture_host()->key_down[k_d] = 1;
	set_raw_keyboard();

	xvt_input_flush_keyboard();
	/* The mapping is suspended: its queue is empty and it takes no new presses. */
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(), 0);
	press_t();
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(), 0);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(k_d), 1);
	XVT_ASSERT_INT_EQ(g_key_ready, 0);
	XVT_ASSERT_INT_EQ(g_last_key_code, 0);
	XVT_ASSERT_INT_EQ(keyboard_peek_char(), 0);
	xvt_keyboard_mapping_suspend();
}

static void check_capture_flushes_keyboard(void)
{
	/* Taking capture flushes the keyboard. */
	capture_start();
	keyboard_mapping_with_t();
	press_t();
	set_raw_keyboard();
	xvt_input_set_captured(true);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(), 0);
	XVT_ASSERT_INT_EQ(g_key_ready, 0);
	XVT_ASSERT_INT_EQ(keyboard_peek_char(), 0);

	/* Each captured frame flushes it again. */
	next_frame();
	set_raw_keyboard();
	xvt_input_begin_capture_frame(capture_host(), true);
	XVT_ASSERT_INT_EQ(g_key_ready, 0);
	XVT_ASSERT_INT_EQ(g_last_key_code, 0);
	XVT_ASSERT_INT_EQ(keyboard_peek_char(), 0);
	xvt_keyboard_mapping_suspend();
}

static void check_mouse_flight_needs_flight(void)
{
	capture_start();
	XVT_ASSERT_INT_EQ(xvt_input_mouse_flight_allowed(), 0);
}

int main(void)
{
	check_suppress_key();
	check_block_held_keys();
	check_capture_clears_game_input();
	check_handover_blocks_held_input();
	check_capture_blocks_held_input();
	check_mouse_motion_allowed();
	check_capture_suspends_controllers();
	check_renderer_tab();
	check_reset_capture();
	check_flush_raw_keyboard();
	check_flush_keyboard();
	check_capture_flushes_keyboard();
	check_mouse_flight_needs_flight();
	xvt_input_reset_capture();
	return 0;
}
