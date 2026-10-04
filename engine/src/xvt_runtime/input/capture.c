#include "xvt_runtime/input/capture.h"

#include <string.h>

#include "aeron/aeron.h"
#include "aeron/compat/dinput.h"
#include "aeron/compat/host.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/player/player.h"
#include "xvt/input/dinput.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/input/keyboard_mapping.h"
#include "xvt_runtime/input/mouse_flight.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/flight_sim.h"
#include "xvt_runtime/runtime/flight_task.h"
static bool g_captured;
static bool g_renderer_tab_suppressed;
static uint8_t g_blocked_keys[AERON_KEY_COUNT];
static uint32_t g_blocked_mouse;
static uint64_t g_mouse_ignored_frame = UINT64_MAX;
static bool g_mouse_released;
static bool g_mouse_capture_failed;
static bool g_mouse_session;
static int g_mouse_context = -1;
static struct xvt_mouse_options g_mouse_options;

enum { MOUSE_CAPTURE_KEY = AERON_KEY_A + ('m' - 'a') };

enum { MOUSE_CONTROL_SHIP, MOUSE_CONTROL_EXTERNAL, MOUSE_CONTROL_MAP };

void xvt_input_flush_raw_keyboard(void)
{
	if (g_dinput_keyboard_device) {
		uint32_t count = UINT32_MAX;
		g_dinput_keyboard_device->lpVtbl->GetDeviceData(
			g_dinput_keyboard_device, sizeof(DIDEVICEOBJECTDATA),
			NULL, &count, 0);
	}
	g_dinput_shift_down = 0;
	g_dinput_ctrl_down = 0;
	g_dinput_alt_down = 0;
	g_key_ready = 0;
	g_last_key_code = 0;
	keyboard_flush_char_buffer();
}

static void xvt_input_apply_key_suppression(void)
{
	for (int key = 0; key < AERON_KEY_COUNT; ++key) {
		AeronCompat_SetKeySuppressed(
			key, g_captured || g_blocked_keys[key] ||
				     (key == AERON_KEY_TAB &&
				      g_renderer_tab_suppressed));
	}
}

void xvt_input_block_key_until_released(int key)
{
	if ((unsigned)key >= AERON_KEY_COUNT) {
		return;
	}
	g_blocked_keys[key] = 1;
	xvt_input_apply_key_suppression();
}

void xvt_input_block_held_keys(void)
{
	const AeronInputSnapshot *input = Aeron_InputSnapshot();
	if (input) {
		for (int key = 0; key < AERON_KEY_COUNT; ++key) {
			g_blocked_keys[key] |= input->key_down[key];
		}
	}
	xvt_input_apply_key_suppression();
}

void xvt_input_flush_keyboard(void)
{
	xvt_keyboard_mapping_suspend();
	xvt_input_block_held_keys();
	xvt_input_flush_raw_keyboard();
}

void xvt_input_set_captured(bool capture)
{
	if (capture == g_captured) {
		return;
	}
	g_captured = capture;
	const AeronInputSnapshot *input = Aeron_InputSnapshot();
	if (input) {
		for (int key = 0; key < AERON_KEY_COUNT; ++key) {
			g_blocked_keys[key] = input->key_down[key];
		}
		g_blocked_mouse = input->mouse.buttons;
		g_mouse_ignored_frame = input->frame_id;
	}
	xvt_input_flush_keyboard();
	g_action_key = 0;
	g_ctrl_axis_x = 0;
	g_ctrl_axis_y = 0;
	g_key_mods = 0;
	g_mouse_buttons = 0;
	g_flight_mouse_delta_x = 0;
	g_flight_mouse_delta_y = 0;
	xvt_flight_controls_reset();
	xvt_mouse_flight_reset();
	if (capture) {
		Aeron_SetRelativeMouseMode(0);
	}
	if (capture) {
		xvt_controller_mapping_suspend();
	}
	flight_input_reset_control_state();
	xvt_input_apply_key_suppression();
}

void xvt_input_begin_capture_frame(const AeronInputSnapshot *input,
				   bool capture)
{
	if (input) {
		for (int key = 0; key < AERON_KEY_COUNT; ++key) {
			if (!input->key_down[key] &&
			    !input->key_released[key]) {
				g_blocked_keys[key] = 0;
			}
		}
		g_blocked_mouse &=
			input->mouse.buttons | input->mouse.released_buttons;
	}
	xvt_input_set_captured(capture);
	xvt_input_apply_key_suppression();
	if (capture) {
		xvt_input_flush_keyboard();
	}
}

bool xvt_input_is_captured(void) { return g_captured; }

bool xvt_input_mouse_motion_allowed(void)
{
	const AeronInputSnapshot *input = Aeron_InputSnapshot();
	return !g_captured && input && input->has_focus &&
	       input->frame_id != g_mouse_ignored_frame;
}

uint32_t xvt_input_filter_mouse_buttons(uint32_t buttons)
{
	return g_captured ? 0 : buttons & ~g_blocked_mouse;
}

void xvt_input_suppress_renderer_tab(bool suppress)
{
	g_renderer_tab_suppressed = suppress;
	AeronCompat_SetKeySuppressed(
		AERON_KEY_TAB, g_captured || g_blocked_keys[AERON_KEY_TAB] ||
				       g_renderer_tab_suppressed);
}

void xvt_input_reset_capture(void)
{
	g_captured = false;
	g_renderer_tab_suppressed = false;
	g_blocked_mouse = 0;
	g_mouse_ignored_frame = UINT64_MAX;
	memset(g_blocked_keys, 0, sizeof g_blocked_keys);
	xvt_input_apply_key_suppression();
	g_mouse_released = false;
	g_mouse_capture_failed = false;
	g_mouse_session = false;
	g_mouse_context = -1;
	memset(&g_mouse_options, 0, sizeof g_mouse_options);
	xvt_mouse_flight_reset();
	Aeron_SetRelativeMouseMode(0);
}

bool xvt_input_mouse_flight_allowed(void)
{
	return g_mouse_options.mouse_flight_enabled &&
	       xvt_flight_task_is_active() && !xvt_flight_task_is_loading() &&
	       !xvt_flight_sim_is_paused() && !xvt_dialog_is_active() &&
	       !Aeron_DebugUiVisible() && xvt_input_mouse_motion_allowed() &&
	       !g_mouse_released && !g_mouse_capture_failed;
}

void xvt_input_update_mouse_capture(const AeronInputSnapshot *input)
{
	const struct xvt_settings *settings = xvt_config_settings();
	if (!settings) {
		return;
	}
	if (memcmp(&g_mouse_options, &settings->mouse,
		   sizeof g_mouse_options)) {
		g_mouse_options = settings->mouse;
		xvt_mouse_flight_set_options(&g_mouse_options);
		g_mouse_capture_failed = false;
		g_blocked_mouse |= input ? input->mouse.buttons : 0;
		g_mouse_ignored_frame = input ? input->frame_id : UINT64_MAX;
	}
	bool session =
		xvt_flight_task_is_active() && !xvt_flight_task_is_loading();
	if (session != g_mouse_session) {
		g_mouse_session = session;
		g_mouse_released = false;
		g_mouse_capture_failed = false;
		g_mouse_context = -1;
		xvt_mouse_flight_reset();
	}
	if (session && (unsigned)g_local_player < 8) {
		int context = g_players[g_local_player].map_camera_state
				      ? MOUSE_CONTROL_MAP
			      : g_players[g_local_player]
					      .view_state.external_camera_active
				      ? MOUSE_CONTROL_EXTERNAL
				      : MOUSE_CONTROL_SHIP;
		if (context != g_mouse_context) {
			g_mouse_context = context;
			xvt_mouse_flight_reset();
		}
	}
	if (session && g_mouse_options.mouse_flight_enabled && input &&
	    input->has_focus && !g_captured && !xvt_dialog_is_active() &&
	    !Aeron_DebugUiVisible()) {
		bool chord = !g_blocked_keys[MOUSE_CAPTURE_KEY] &&
			     xvt_keyboard_mapping_find_shortcut_press(
				     input, XVT_KEYBOARD_SHORTCUT_MOUSE) >= 0;
		bool click = g_mouse_released && input->mouse.inside_content &&
			     input->mouse.pressed_buttons;
		if (chord || click) {
			g_mouse_released = chord ? !g_mouse_released : false;
			g_mouse_capture_failed = false;
			g_blocked_keys[MOUSE_CAPTURE_KEY] |= chord;
			g_blocked_mouse |= input->mouse.buttons |
					   input->mouse.pressed_buttons;
			g_mouse_ignored_frame = input->frame_id;
			xvt_mouse_flight_reset();
			xvt_input_apply_key_suppression();
		}
	}
	bool want_relative_mouse = xvt_input_mouse_flight_allowed();
	bool was_relative = Aeron_RelativeMouseMode() != 0;
	if (want_relative_mouse != was_relative) {
		if (!Aeron_SetRelativeMouseMode(want_relative_mouse) &&
		    want_relative_mouse) {
			g_mouse_capture_failed = true;
			g_mouse_released = true;
			XVT_LOG_ERROR("input.capture_failed device=mouse");
		}
		xvt_mouse_flight_reset();
		g_blocked_mouse |= input ? input->mouse.buttons : 0;
	}
	if (g_mouse_options.mouse_flight_enabled && session) {
		Aeron_SetHostCursorVisible(!Aeron_RelativeMouseMode() &&
					   !xvt_dialog_is_active());
	} else if (was_relative) {
		Aeron_SetHostCursorVisible(0);
	}
	xvt_mouse_flight_pump();
}
