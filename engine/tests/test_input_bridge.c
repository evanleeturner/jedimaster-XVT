/* Checks the host-to-game input bridge (xvt_runtime/input/input_bridge.h)
 * against the promises in its header: how the keyboard route is picked and what
 * a new route does, the frontend's key states, typed keys and Windows-1252
 * text, the frontend mouse, what a suppressed frame clears, keyboard
 * reacquisition, and Init and Shutdown. The test plays the host by writing each
 * frame into Aeron's input snapshot, and reads the frontend's state where the
 * original game reads it. No flight runs and no settings are loaded.
 *
 * Not checked here: the GAMEPLAY route needs a loaded flight;
 * RendererShortcutAllowed's true side needs a committed render snapshot; the
 * frontend joystick needs Init, which needs loaded settings. */
#include <string.h>

#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "test_assert.h"
#include "xvt/flight/flight_input.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/input/input_bridge.h"
#include "xvt_runtime/input/keyboard_mapping.h"
#include "xvt_runtime/runtime/flight_task.h"
#include "xvt_runtime/snapshot/render_snapshot.h"

/* Windows virtual-key codes. */
enum {
	VK_BACK = 0x08,
	VK_TAB = 0x09,
	VK_RETURN = 0x0D,
	VK_SHIFT = 0x10,
	VK_ESCAPE = 0x1B,
	VK_F1 = 0x70,
	VK_LSHIFT = 0xA0,
};

static AeronInputSnapshot *input_bridge_host(void)
{
	return (AeronInputSnapshot *)Aeron_InputSnapshot();
}

/* An empty frame with focus, numbered after the last one. */
static void new_frame(void)
{
	AeronInputSnapshot *host = input_bridge_host();
	uint64_t frame = host->frame_id;
	memset(host, 0, sizeof *host);
	host->frame_id = frame + 1;
	host->has_focus = 1;
}

/* Nothing captured, blocked or mapped, an empty frontend, and an empty frame with focus. */
static void input_bridge_start(void)
{
	xvt_input_reset_capture();
	xvt_keyboard_mapping_suspend();
	xvt_controller_mapping_shutdown();
	keyboard_flush_char_buffer();
	memset(g_front_state.key_state, 0, sizeof g_front_state.key_state);
	g_front_state.mouse_x = 0;
	g_front_state.mouse_y = 0;
	g_front_state.mouse_left_down = 0;
	g_front_state.mouse_right_down = 0;
	g_front_state.mouse_left_click_latch = 0;
	g_front_state.mouse_right_click_latch = 0;
	new_frame();
}

static void set_text(const char *text)
{
	size_t length = strlen(text);
	XVT_ASSERT_TRUE(length <= AERON_TEXT_INPUT_CAPACITY);
	memcpy(input_bridge_host()->text, text, length);
	input_bridge_host()->text_length = (uint32_t)length;
}

/* Reads the frontend's typed characters into out; returns how many there were. */
static size_t read_typed(unsigned char *out, size_t capacity)
{
	size_t count = 0;
	while (g_front_state.char_read_idx != g_front_state.char_write_idx) {
		unsigned char c = (unsigned char)keyboard_dequeue_char();
		if (count < capacity) {
			out[count] = c;
		}
		++count;
	}
	return count;
}

static void check_route(void)
{
	input_bridge_start();
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_input_reconcile_keyboard(), XVT_KEYBOARD_RAW);

	input_bridge_host()->has_focus = 0;
	XVT_ASSERT_INT_EQ(xvt_input_reconcile_keyboard(), XVT_KEYBOARD_BLOCKED);
	input_bridge_host()->has_focus = 1;
	XVT_ASSERT_INT_EQ(xvt_input_reconcile_keyboard(), XVT_KEYBOARD_RAW);

	xvt_input_set_captured(true);
	XVT_ASSERT_INT_EQ(xvt_input_reconcile_keyboard(), XVT_KEYBOARD_BLOCKED);
	xvt_input_set_captured(false);
	XVT_ASSERT_INT_EQ(xvt_input_reconcile_keyboard(), XVT_KEYBOARD_RAW);

	/* A suppressed Update blocks the route until an Update that is not. */
	xvt_input_update(1);
	XVT_ASSERT_INT_EQ(xvt_input_reconcile_keyboard(), XVT_KEYBOARD_BLOCKED);
	new_frame();
	xvt_input_update(0);
	XVT_ASSERT_INT_EQ(xvt_input_reconcile_keyboard(), XVT_KEYBOARD_RAW);
}

static void check_new_route_flushes(void)
{
	input_bridge_start();
	XVT_ASSERT_INT_EQ(xvt_input_reconcile_keyboard(), XVT_KEYBOARD_RAW);
	input_bridge_host()->key_down[AERON_KEY_A] = 1;
	g_key_ready = 1;
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_A), 0);

	/* Losing focus is a new route: the held key is blocked and the raw keyboard flushed. */
	input_bridge_host()->has_focus = 0;
	XVT_ASSERT_INT_EQ(xvt_input_reconcile_keyboard(), XVT_KEYBOARD_BLOCKED);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_A), 1);
	XVT_ASSERT_INT_EQ(g_key_ready, 0);
}

static void check_mapping_only_for_gameplay(void)
{
	/* The route settles on RAW first, so the next call is no new route and flushes nothing. */
	input_bridge_start();
	XVT_ASSERT_INT_EQ(xvt_input_reconcile_keyboard(), XVT_KEYBOARD_RAW);
	static struct xvt_keyboard_bindings profile;
	memset(&profile, 0, sizeof profile);
	profile.bindings[0].source.key = AERON_KEY_A + ('t' - 'a');
	profile.bindings[0].action = XVT_INPUT_ACTION_TARGET_NEXT;
	profile.count = 1;
	xvt_keyboard_mapping_install(&profile);
	xvt_keyboard_mapping_enable(true, input_bridge_host());

	XVT_ASSERT_INT_EQ(xvt_input_reconcile_keyboard(), XVT_KEYBOARD_RAW);
	AeronKeyEvent press;
	memset(&press, 0, sizeof press);
	press.chord.key = AERON_KEY_A + ('t' - 'a');
	press.down = 1;
	xvt_keyboard_mapping_event(&press, false);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(), 0);
}

static void check_can_reacquire_keyboard(void)
{
	input_bridge_start();
	XVT_ASSERT_INT_EQ(xvt_input_consume_keyboard_reacquire(), 1);
	XVT_ASSERT_INT_EQ(xvt_input_consume_keyboard_reacquire(), 0);
	new_frame();
	input_bridge_host()->has_focus = 0;
	XVT_ASSERT_INT_EQ(xvt_input_consume_keyboard_reacquire(), 0);
	input_bridge_host()->has_focus = 1;
	XVT_ASSERT_INT_EQ(xvt_input_consume_keyboard_reacquire(), 1);
	XVT_ASSERT_INT_EQ(xvt_input_consume_keyboard_reacquire(), 0);
}

static void check_frontend_key_states(void)
{
	input_bridge_start();
	input_bridge_host()->key_down[AERON_KEY_A] = 1;
	input_bridge_host()->key_down[AERON_KEY_1] = 1;
	input_bridge_host()->key_down[AERON_KEY_1 + 9] = 1;
	input_bridge_host()->key_down[AERON_KEY_F1] = 1;
	input_bridge_host()->key_down[AERON_KEY_RETURN] = 1;
	input_bridge_host()->key_down[AERON_KEY_LSHIFT] = 1;
	xvt_input_update(0);
	XVT_ASSERT_TRUE(g_front_state.key_state['A'] & 0x80);
	XVT_ASSERT_TRUE(g_front_state.key_state['1'] & 0x80);
	XVT_ASSERT_TRUE(g_front_state.key_state['0'] & 0x80);
	XVT_ASSERT_TRUE(g_front_state.key_state[VK_F1] & 0x80);
	XVT_ASSERT_TRUE(g_front_state.key_state[VK_RETURN] & 0x80);
	XVT_ASSERT_TRUE(g_front_state.key_state[VK_LSHIFT] & 0x80);
	XVT_ASSERT_TRUE(g_front_state.key_state[VK_SHIFT] & 0x80);
	XVT_ASSERT_INT_EQ(g_front_state.key_state['B'], 0);
	XVT_ASSERT_INT_EQ(g_front_state.key_state['2'], 0);

	/* A key let go is up in the next frame's states. */
	new_frame();
	xvt_input_update(0);
	XVT_ASSERT_INT_EQ(g_front_state.key_state['A'], 0);
	XVT_ASSERT_INT_EQ(g_front_state.key_state[VK_SHIFT], 0);
}

static void check_typed_control_keys(void)
{
	input_bridge_start();
	input_bridge_host()->key_typed[AERON_KEY_BACKSPACE] = 2;
	input_bridge_host()->key_typed[AERON_KEY_TAB] = 1;
	input_bridge_host()->key_typed[AERON_KEY_RETURN] = 1;
	input_bridge_host()->key_typed[AERON_KEY_ESCAPE] = 1;
	xvt_input_update(0);
	unsigned char typed[16];
	size_t count = read_typed(typed, sizeof typed);
	XVT_ASSERT_INT_EQ(count, 5);
	int seen[256] = {0};
	for (size_t i = 0; i < count; ++i) {
		++seen[typed[i]];
	}
	XVT_ASSERT_INT_EQ(seen[VK_BACK], 2);
	XVT_ASSERT_INT_EQ(seen[VK_TAB], 1);
	XVT_ASSERT_INT_EQ(seen[VK_RETURN], 1);
	XVT_ASSERT_INT_EQ(seen[VK_ESCAPE], 1);
}

static void check_text_windows1252(void)
{
	input_bridge_start();
	/* a, e acute (U+00E9), the euro sign (U+20AC, 0x80 in Windows-1252), a
	 * four-byte emoji, A with macron (U+0100, not in Windows-1252), then
	 * z. */
	set_text("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xC4\x80z");
	xvt_input_update(0);
	unsigned char typed[16];
	size_t count = read_typed(typed, sizeof typed);
	XVT_ASSERT_INT_EQ(count, 4);
	XVT_ASSERT_INT_EQ(typed[0], 'a');
	XVT_ASSERT_INT_EQ(typed[1], 0xE9);
	XVT_ASSERT_INT_EQ(typed[2], 0x80);
	XVT_ASSERT_INT_EQ(typed[3], 'z');
}

static void check_text_ring_drops_oldest(void)
{
	input_bridge_start();

	enum { TYPED = 1500, PER_FRAME = 750 };

	char text[PER_FRAME + 1];
	for (int frame = 0; frame < TYPED / PER_FRAME; ++frame) {
		for (int i = 0; i < PER_FRAME; ++i) {
			text[i] = (char)('A' + (frame * PER_FRAME + i) % 26);
		}
		text[PER_FRAME] = '\0';
		new_frame();
		set_text(text);
		xvt_input_update(0);
	}
	static unsigned char typed[TYPED];
	size_t count = read_typed(typed, sizeof typed);
	XVT_ASSERT_TRUE(count >= 1023 && count <= 1024);
	/* What is left is the newest characters, in order. */
	for (size_t i = 0; i < count; ++i) {
		XVT_ASSERT_INT_EQ(typed[i], 'A' + (TYPED - count + i) % 26);
	}
}

static void check_frontend_mouse(void)
{
	input_bridge_start();
	input_bridge_host()->window_width = 640;
	input_bridge_host()->window_height = 480;
	input_bridge_host()->mouse.raw_x = 100;
	input_bridge_host()->mouse.raw_y = 50;
	input_bridge_host()->mouse.inside_content = 1;
	xvt_input_update(0);
	int x = -1;
	int y = -1;
	xvt_input_frontend_cursor_position(&x, &y);
	XVT_ASSERT_INT_EQ(x, 100);
	XVT_ASSERT_INT_EQ(y, 50);

	/* A window twice the classic size: the same spot in classic coordinates. */
	new_frame();
	input_bridge_host()->window_width = 1280;
	input_bridge_host()->window_height = 960;
	input_bridge_host()->mouse.raw_x = 300;
	input_bridge_host()->mouse.raw_y = 140;
	input_bridge_host()->mouse.inside_content = 1;
	xvt_input_update(0);
	xvt_input_frontend_cursor_position(&x, &y);
	XVT_ASSERT_INT_EQ(x, 150);
	XVT_ASSERT_INT_EQ(y, 70);

	/* Outside the classic view the frontend keeps the last position. */
	new_frame();
	input_bridge_host()->window_width = 640;
	input_bridge_host()->window_height = 480;
	input_bridge_host()->mouse.raw_x = 10;
	input_bridge_host()->mouse.raw_y = 10;
	input_bridge_host()->mouse.inside_content = 0;
	xvt_input_update(0);
	xvt_input_frontend_cursor_position(&x, &y);
	XVT_ASSERT_INT_EQ(x, 150);
	XVT_ASSERT_INT_EQ(y, 70);
}

/* Held A, typed text, and pending clicks in the frontend. An empty frame first
 * settles the route, since a new route blocks keys already held. */
static void fill_frontend(void)
{
	xvt_input_update(0);
	new_frame();
	input_bridge_host()->key_down[AERON_KEY_A] = 1;
	set_text("hi");
	xvt_input_update(0);
	XVT_ASSERT_TRUE(g_front_state.key_state['A'] & 0x80);
	XVT_ASSERT_TRUE(keyboard_peek_char() == 'h');
	g_front_state.mouse_left_down = 1;
	g_front_state.mouse_right_down = 1;
	g_front_state.mouse_left_click_latch = 1;
	g_front_state.mouse_right_click_latch = 1;
}

static void assert_frontend_cleared(void)
{
	for (int vk = 0; vk < 256; ++vk) {
		XVT_ASSERT_INT_EQ(g_front_state.key_state[vk], 0);
	}
	XVT_ASSERT_INT_EQ(keyboard_peek_char(), 0);
	XVT_ASSERT_INT_EQ(g_front_state.mouse_left_down, 0);
	XVT_ASSERT_INT_EQ(g_front_state.mouse_right_down, 0);
	XVT_ASSERT_INT_EQ(g_front_state.mouse_left_click_latch, 0);
	XVT_ASSERT_INT_EQ(g_front_state.mouse_right_click_latch, 0);
}

static void check_suppressed_frame_clears(void)
{
	input_bridge_start();
	fill_frontend();
	new_frame();
	input_bridge_host()->key_down[AERON_KEY_A] = 1;
	set_text("x");
	xvt_input_update(1);
	assert_frontend_cleared();

	/* A blocked route clears the same way. */
	input_bridge_start();
	fill_frontend();
	new_frame();
	input_bridge_host()->key_down[AERON_KEY_A] = 1;
	set_text("x");
	input_bridge_host()->has_focus = 0;
	xvt_input_update(0);
	assert_frontend_cleared();
}

/* A gamepad model and that gamepad connected in Aeron's snapshot. */
static void connect_controller(void)
{
	static struct xvt_controller_options options;
	memset(&options, 0, sizeof options);
	struct xvt_controller_model *model = &options.models[0];
	memcpy(model->guid, "0123456789abcdef0123456789abcdea",
	       sizeof model->guid);
	model->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	xvt_controller_options_clear_profile(&model->profile,
					     AERON_CONTROLLER_KIND_GAMEPAD);
	options.count = 1;
	xvt_controller_mapping_init(&options);
	AeronControllerSnapshot *pad = &input_bridge_host()->controllers[0];
	pad->connected = 1;
	pad->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	pad->instance_id = 5;
	memcpy(pad->guid, model->guid, sizeof pad->guid);
}

static void check_update_samples_controllers(void)
{
	input_bridge_start();
	connect_controller();
	xvt_input_update(0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_is_model_connected(), 1);

	input_bridge_start();
	connect_controller();
	xvt_input_update_flight(0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_is_model_connected(), 1);
}

static void check_update_flight_drops_text(void)
{
	input_bridge_start();
	set_text("abc");
	xvt_input_update(0);
	XVT_ASSERT_TRUE(keyboard_peek_char() == 'a');
	new_frame();
	xvt_input_update_flight(0);
	XVT_ASSERT_INT_EQ(keyboard_peek_char(), 0);
}

static void check_init_waits_for_settings(void)
{
	input_bridge_start();
	XVT_ASSERT_TRUE(xvt_config_settings() == NULL);
	connect_controller();
	xvt_input_init();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_options()->count, 1);
}

static void check_shutdown(void)
{
	input_bridge_start();
	connect_controller();
	xvt_input_suppress_renderer_tab(true);
	xvt_input_set_captured(true);
	static struct xvt_keyboard_bindings profile;
	memset(&profile, 0, sizeof profile);
	profile.bindings[0].source.key = AERON_KEY_A + ('t' - 'a');
	profile.bindings[0].action = XVT_INPUT_ACTION_TARGET_NEXT;
	profile.count = 1;
	xvt_keyboard_mapping_install(&profile);
	xvt_keyboard_mapping_enable(true, input_bridge_host());
	AeronKeyEvent press;
	memset(&press, 0, sizeof press);
	press.chord.key = AERON_KEY_A + ('t' - 'a');
	press.down = 1;
	xvt_keyboard_mapping_event(&press, false);
	g_xvt_control_roll = 5;

	xvt_input_shutdown();
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(), 0);
	XVT_ASSERT_INT_EQ(xvt_input_is_captured(), 0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_options()->count, 0);
	XVT_ASSERT_INT_EQ(g_xvt_control_roll, 0);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_TAB), 0);
}

static void check_renderer_shortcut_refusals(void)
{
	input_bridge_start();
	XVT_ASSERT_TRUE(xvt_render_snapshot_current() == NULL);
	XVT_ASSERT_INT_EQ(xvt_input_renderer_shortcut_allowed(), 0);
	xvt_input_set_captured(true);
	XVT_ASSERT_INT_EQ(xvt_input_renderer_shortcut_allowed(), 0);
	xvt_input_set_captured(false);
}

int main(void)
{
	check_route();
	check_new_route_flushes();
	check_mapping_only_for_gameplay();
	check_can_reacquire_keyboard();
	check_frontend_key_states();
	check_typed_control_keys();
	check_text_windows1252();
	check_text_ring_drops_oldest();
	check_frontend_mouse();
	check_suppressed_frame_clears();
	check_update_samples_controllers();
	check_update_flight_drops_text();
	check_init_waits_for_settings();
	check_shutdown();
	check_renderer_shortcut_refusals();
	xvt_input_shutdown();
	return 0;
}
