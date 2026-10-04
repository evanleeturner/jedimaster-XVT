#include "xvt_runtime/input/input_bridge.h"

#include <string.h>

#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_joystick.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/input/keyboard_mapping.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/flight_task.h"
#include "xvt_runtime/runtime/movie_task.h"
#include "xvt_runtime/runtime/presentation.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/snapshot/render_snapshot.h"

static AeronWinmmJoystickState g_joystick;
static int g_connected;
static uint64_t g_reacquire_frame = UINT64_MAX;

static xvt_keyboard_route g_keyboard_route;
static bool g_keyboard_suppressed;
static uint64_t g_keyboard_frame = UINT64_MAX;

xvt_keyboard_route xvt_input_reconcile_keyboard(void)
{
	const AeronInputSnapshot *input = Aeron_InputSnapshot();
	xvt_keyboard_route route = XVT_KEYBOARD_RAW;
	if (g_keyboard_suppressed || !input || !input->has_focus ||
	    xvt_input_is_captured() || Aeron_DebugUiVisible()) {
		route = XVT_KEYBOARD_BLOCKED;
	} else if (xvt_flight_task_is_active() &&
		   !xvt_flight_task_is_loading() && !xvt_dialog_is_active() &&
		   !xvt_movie_task_is_active() && !xvt_resync_is_active() &&
		   (unsigned)g_local_player < 8 &&
		   g_players[g_local_player].chat_recipient_mode ==
			   FLIGHT_CHAT_RECIPIENT_INACTIVE) {
		route = XVT_KEYBOARD_GAMEPLAY;
	}
	if (route != g_keyboard_route) {
		/* Commands and text never cross a routing transition. Held keys must be released. */
		xvt_input_flush_keyboard();
		g_keyboard_route = route;
	}
	xvt_keyboard_mapping_enable(route == XVT_KEYBOARD_GAMEPLAY, input);
	return route;
}

static void xvt_input_update_keyboard(bool suppress)
{
	const AeronInputSnapshot *input = Aeron_InputSnapshot();
	g_keyboard_suppressed = suppress;
	xvt_keyboard_route route = xvt_input_reconcile_keyboard();
	if (!input || input->frame_id == g_keyboard_frame) {
		return;
	}
	g_keyboard_frame = input->frame_id;
	xvt_keyboard_mapping_begin_frame(input);
	if (route == XVT_KEYBOARD_GAMEPLAY) {
		for (uint16_t i = 0;
		     !input->key_events_overflow && i < input->key_event_count;
		     ++i) {
			const AeronKeyEvent *event = &input->key_events[i];
			xvt_keyboard_mapping_event(
				event, AeronCompat_IsKeySuppressed(
					       event->chord.key) != 0);
		}
		/* DirectInput still serves raw consumers, but owns no gameplay backlog. */
		xvt_input_flush_raw_keyboard();
	} else if (route == XVT_KEYBOARD_BLOCKED) {
		xvt_input_flush_raw_keyboard();
	}
}

void xvt_input_frontend_cursor_position(int *x, int *y)
{
	*x = g_front_state.mouse_x;
	*y = g_front_state.mouse_y;
}

static int xvt_input_joystick(AeronWinmmJoystickState *state, void *user)
{
	(void)user;
	*state = g_joystick;
	return g_connected;
}

static void xvt_input_append(unsigned int ch)
{
	int next = (g_front_state.char_write_idx + 1) % 1024;
	if (next == g_front_state.char_read_idx) {
		g_front_state.char_read_idx =
			(g_front_state.char_read_idx + 1) % 1024;
	}
	g_front_state.char_ring_buffer[g_front_state.char_write_idx] = (char)ch;
	g_front_state.char_write_idx = next;
}

static unsigned int xvt_input_virtual_key(int key)
{
	static const unsigned char controls[] = {
		13,   27, 8,	9,    32,   0xbd, 0xbb, 0xdb, 0xdd,
		0xdc, 0,  0xba, 0xde, 0xc0, 0xbc, 0xbe, 0xbf, 0x14};
	static const unsigned char navigation[] = {0x2c, 0x91, 0x13, 0x2d, 0x24,
						   0x21, 0x2e, 0x23, 0x22, 0x27,
						   0x25, 0x28, 0x26};
	if (key >= AERON_KEY_A && key < AERON_KEY_A + 26) {
		return 'A' + key - AERON_KEY_A;
	}
	if (key >= AERON_KEY_1 && key < AERON_KEY_1 + 10) {
		return key == AERON_KEY_1 + 9 ? '0' : '1' + key - AERON_KEY_1;
	}
	if (key >= AERON_KEY_RETURN && key <= AERON_KEY_CAPSLOCK) {
		return controls[key - AERON_KEY_RETURN];
	}
	if (key >= AERON_KEY_F1 && key < AERON_KEY_F1 + 12) {
		return 0x70 + key - AERON_KEY_F1;
	}
	if (key >= AERON_KEY_PRINTSCREEN && key <= AERON_KEY_UP) {
		return navigation[key - AERON_KEY_PRINTSCREEN];
	}
	if (key >= AERON_KEY_LCTRL && key <= AERON_KEY_RGUI) {
		static const unsigned char modifiers[] = {
			0xa2, 0xa0, 0xa4, 0x5b, 0xa3, 0xa1, 0xa5, 0x5c};
		return modifiers[key - AERON_KEY_LCTRL];
	}
	if (key >= AERON_KEY_KP_1 && key <= AERON_KEY_KP_9) {
		return 0x61 + key - AERON_KEY_KP_1;
	}
	if (key == AERON_KEY_KP_0) {
		return 0x60;
	}
	if (key == AERON_KEY_KP_ENTER) {
		return 13;
	}
	return 0;
}

static unsigned int xvt_input_windows1252(unsigned int cp)
{
	static const unsigned short extended[32] = {
		0x20ac, 0,	0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
		0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0,	0x017d, 0,
		0,	0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
		0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0,	0x017e, 0x0178};
	unsigned int i;
	if ((cp >= 32 && cp < 127) || (cp >= 160 && cp <= 255)) {
		return cp;
	}
	for (i = 0; i < 32; ++i) {
		if (cp && cp == extended[i]) {
			return 128 + i;
		}
	}
	return 0;
}

static void xvt_input_text(const AeronInputSnapshot *input)
{
	uint32_t i = 0;
	while (i < input->text_length) {
		unsigned int cp = (unsigned char)input->text[i++];
		unsigned int count = 0;
		unsigned int minimum = 0;
		if (cp >= 0xc2 && cp <= 0xdf) {
			cp &= 31;
			count = 1;
			minimum = 128;
		} else if (cp >= 0xe0 && cp <= 0xef) {
			cp &= 15;
			count = 2;
			minimum = 2048;
		} else if (cp >= 128) {
			continue;
		}
		if (count > input->text_length - i) {
			break;
		}
		while (count--) {
			unsigned int tail = (unsigned char)input->text[i++];
			if ((tail & 0xc0) != 0x80) {
				cp = 0;
				break;
			}
			cp = (cp << 6) | (tail & 63);
		}
		if (cp < minimum) {
			continue;
		}
		cp = xvt_input_windows1252(cp);
		if (cp) {
			xvt_input_append(cp);
		}
	}
}

static void xvt_input_update_joystick(int suppress)
{
	int connected = xvt_controller_mapping_is_model_connected();
	int changed = g_connected != connected;
	g_connected = connected;
	memset(&g_joystick, 0, sizeof g_joystick);
	g_joystick.name = "OpenXvT Controllers";
	g_joystick.button_count = 2;
	g_joystick.has_pov = 1;
	g_joystick.pov_direction = -1;
	for (int axis = 0; axis < 4; ++axis) {
		g_joystick.axes[axis] = 32768;
	}
	if (changed) {
		memset(g_front_state.joystick_present, 0,
		       sizeof g_front_state.joystick_present);
		memset(g_front_state.joystick_button_held, 0,
		       sizeof g_front_state.joystick_button_held);
		memset(g_front_state.joystick_button_released, 0,
		       sizeof g_front_state.joystick_button_released);
		memset(g_front_state.joystick_axis_x, 0,
		       sizeof g_front_state.joystick_axis_x);
		memset(g_front_state.joystick_axis_y, 0,
		       sizeof g_front_state.joystick_axis_y);
		memset(g_front_state.joystick_pov_direction, 0,
		       sizeof g_front_state.joystick_pov_direction);
		joystick_init_devices();
	}
	if (connected && !suppress) {
		static const int channels[] = {XVT_INPUT_AXIS_YAW,
					       XVT_INPUT_AXIS_PITCH, -1,
					       XVT_INPUT_AXIS_ROLL};
		for (int axis = 0; axis < 4; ++axis) {
			if (channels[axis] >= 0) {
				g_joystick.axes[axis] =
					(uint32_t)(32768 +
						   256 * xvt_controller_mapping_menu_axis(
								 (xvt_input_axis)channels
									 [axis]));
			}
		}
		g_joystick.buttons = xvt_controller_mapping_menu_buttons();
		unsigned hat = xvt_controller_mapping_menu_hat();
		g_joystick.pov_direction = hat & 1   ? 0
					   : hat & 2 ? 1
					   : hat & 4 ? 2
					   : hat & 8 ? 3
						     : -1;
	} else {
		/* Suppression is a route transition, not a frontend button release. */
		memset(g_front_state.joystick_button_held, 0,
		       sizeof g_front_state.joystick_button_held);
		memset(g_front_state.joystick_button_released, 0,
		       sizeof g_front_state.joystick_button_released);
	}
}

void xvt_input_init(void)
{
	const struct xvt_settings *settings = xvt_config_settings();
	if (!settings) {
		return;
	}
	xvt_controller_mapping_init(&settings->controller);
	xvt_keyboard_mapping_install(&settings->keyboard);
	g_keyboard_route = XVT_KEYBOARD_RAW;
	g_keyboard_suppressed = true;
	g_keyboard_frame = UINT64_MAX;
	xvt_flight_controls_reset();
	g_reacquire_frame = UINT64_MAX;
	AeronCompat_SetJoystickSource(xvt_input_joystick, NULL);
}

int xvt_input_consume_keyboard_reacquire(void)
{
	const AeronInputSnapshot *input = Aeron_InputSnapshot();
	if (!input || !input->has_focus ||
	    input->frame_id == g_reacquire_frame) {
		return 0;
	}
	g_reacquire_frame = input->frame_id;
	return 1;
}

void xvt_input_update(int suppress)
{
	xvt_input_update_keyboard(suppress != 0);
	const AeronInputSnapshot *input = Aeron_InputSnapshot();
	int key;
	if (!input) {
		return;
	}
	suppress |= g_keyboard_route == XVT_KEYBOARD_BLOCKED;
	xvt_controller_mapping_update(input);
	xvt_flight_controls_update_throttle_context();
	xvt_input_update_joystick(suppress);
	memset(g_front_state.key_state, 0, sizeof(g_front_state.key_state));
	if (suppress) {
		keyboard_flush_char_buffer();
		g_front_state.mouse_left_down = g_front_state.mouse_right_down =
			0;
		g_front_state.mouse_left_click_latch =
			g_front_state.mouse_right_click_latch = 0;
		return;
	}
	for (key = 0; key < AERON_KEY_COUNT; ++key) {
		if (AeronCompat_IsKeySuppressed(key)) {
			continue;
		}
		unsigned int vk = xvt_input_virtual_key(key);
		if (vk && input->key_down[key]) {
			g_front_state.key_state[vk] = 0x80;
		}
		if (vk == 8 || vk == 9 || vk == 13 || vk == 27) {
			unsigned int repeat;
			for (repeat = 0; repeat < input->key_typed[key];
			     ++repeat) {
				xvt_input_append(vk);
			}
		}
	}
	g_front_state.key_state[0x10] =
		g_front_state.key_state[0xa0] | g_front_state.key_state[0xa1];
	g_front_state.key_state[0x11] =
		g_front_state.key_state[0xa2] | g_front_state.key_state[0xa3];
	g_front_state.key_state[0x12] =
		g_front_state.key_state[0xa4] | g_front_state.key_state[0xa5];
	xvt_input_text(input);
	int x, y;
	int inside = xvt_presentation_mouse_to_classic(input, &x, &y);
	if (inside) {
		g_front_state.mouse_x = x;
		g_front_state.mouse_y = y;
		g_front_state.mouse_left_click_latch |=
			!!(xvt_input_filter_mouse_buttons(
				   input->mouse.released_buttons) &
			   AERON_MOUSE_BUTTON_LEFT);
		g_front_state.mouse_right_click_latch |=
			!!(xvt_input_filter_mouse_buttons(
				   input->mouse.released_buttons) &
			   AERON_MOUSE_BUTTON_RIGHT);
	}
	g_front_state.mouse_left_down =
		inside &&
		(xvt_input_filter_mouse_buttons(input->mouse.buttons) &
		 AERON_MOUSE_BUTTON_LEFT);
	g_front_state.mouse_right_down =
		inside &&
		(xvt_input_filter_mouse_buttons(input->mouse.buttons) &
		 AERON_MOUSE_BUTTON_RIGHT);
}

int xvt_input_renderer_shortcut_allowed(void)
{
	if (xvt_input_is_captured()) {
		return 0;
	}
	const struct xvt_render_snapshot *s = xvt_render_snapshot_current();
	if (!s || s->text_entry_active || xvt_dialog_is_text_prompt()) {
		return 0;
	}
	return !xvt_flight_task_is_active() || (unsigned)g_local_player >= 8 ||
	       g_players[g_local_player].chat_recipient_mode ==
		       FLIGHT_CHAT_RECIPIENT_INACTIVE;
}

void xvt_input_update_flight(int suppress)
{
	xvt_input_update_keyboard(suppress != 0);
	const AeronInputSnapshot *input = Aeron_InputSnapshot();
	if (input) {
		xvt_controller_mapping_update(input);
		xvt_flight_controls_update_throttle_context();
		xvt_input_update_joystick(suppress || !input->has_focus ||
					  xvt_input_is_captured());
	}
	keyboard_flush_char_buffer();
}

void xvt_input_shutdown(void)
{
	xvt_keyboard_mapping_suspend();
	xvt_input_reset_capture();
	xvt_controller_mapping_shutdown();
	xvt_flight_controls_reset();
	AeronCompat_SetKeySuppressed(AERON_KEY_TAB, 0);
	AeronCompat_SetJoystickSource(NULL, NULL);
	g_connected = 0;
}
