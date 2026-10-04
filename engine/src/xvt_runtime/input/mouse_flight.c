#include "xvt_runtime/input/mouse_flight.h"

#include <math.h>
#include <stdint.h>

#include "aeron/aeron.h"
#include "aeron/input.h"
#include "aeron/time.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/input/actions.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/log/log.h"

#define MOUSE_FLIGHT_MAX_SAMPLE_GAP_US 100000
/* Virtual stick: axis units per pixel of mouse travel at the default
 * sensitivity notch (full deflection after ~256 px). */
#define MOUSE_FLIGHT_STICK_GAIN (127.0f / 256.0f)
/* Right-button tap window: release inside it emits the target-in-sight tap
 * action, roll-lock engages only after it. Close to the recovered 59-tick
 * window flight_update_player_step uses for joystick button 2: 236 ms of real
 * time, 250 ms at the original's assumed 236 ticks a second. */
#define MOUSE_FLIGHT_TAP_US 250000u

static struct {
	struct xvt_mouse_options options;
	uint64_t pumped_frame;
	/* Time of the last drain; 0 = no drain since (re)activation. */
	uint64_t drain_time_us;
	/* Motion accumulated by the per-host-frame pump, awaiting a game read. */
	float pending_x;
	float pending_y;
	/* Virtual stick: the held virtual-stick deflection, in axis units. */
	float stick_x;
	float stick_y;
	float stick_r;
	int axis_x;
	int axis_y;
	int axis_r;
	int buttons;
	/* After the tap window, X moves roll while yaw/pitch retain their deflection. */
	int roll_lock;
	int drained_roll_lock;
	uint64_t rmb_press_time_us;
	int rmb_down;
	/* A right-button tap was released inside the tap window and awaits its
	 * target-in-sight action key. */
	int target_tap;
	int active;
	uint16_t keys[16];
	unsigned read_key;
	unsigned write_key;
} g_mouse_flight;

/* Doubling steps (1/16x..16x): raw relative deltas vary by more than an
 * order of magnitude between touchpads and high-DPI mice. */
static const float k_mouse_flight_sensitivity_scale[XVT_MOUSE_SENSITIVITY_MAX] =
	{
		0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f,
};

void xvt_mouse_flight_reset(void)
{
	g_mouse_flight.pending_x = 0.0f;
	g_mouse_flight.pending_y = 0.0f;
	g_mouse_flight.stick_x = 0.0f;
	g_mouse_flight.stick_y = 0.0f;
	g_mouse_flight.stick_r = 0.0f;
	g_mouse_flight.axis_x = 0;
	g_mouse_flight.axis_y = 0;
	g_mouse_flight.axis_r = 0;
	g_mouse_flight.buttons = 0;
	g_mouse_flight.roll_lock = 0;
	g_mouse_flight.drained_roll_lock = 0;
	g_mouse_flight.rmb_down = 0;
	g_mouse_flight.target_tap = 0;
	g_mouse_flight.drain_time_us = 0;
	g_mouse_flight.active = 0;
	g_mouse_flight.read_key = 0;
	g_mouse_flight.write_key = 0;
}

static void xvt_mouse_flight_queue_key(uint16_t key)
{
	unsigned next = (g_mouse_flight.write_key + 1) % 16;
	if (next == g_mouse_flight.read_key) {
		XVT_LOG_WARN("input.queue_full queue=mouse");
		return;
	}
	g_mouse_flight.keys[g_mouse_flight.write_key] = key;
	g_mouse_flight.write_key = next;
}

uint16_t xvt_mouse_flight_read_key(void)
{
	if (!xvt_input_mouse_flight_allowed() ||
	    (unsigned)g_local_player >= 8 ||
	    g_players[g_local_player].chat_recipient_mode !=
		    FLIGHT_CHAT_RECIPIENT_INACTIVE) {
		g_mouse_flight.read_key = 0;
		g_mouse_flight.write_key = 0;
		return 0;
	}
	if (g_mouse_flight.read_key == g_mouse_flight.write_key) {
		return 0;
	}
	uint16_t key = g_mouse_flight.keys[g_mouse_flight.read_key];
	g_mouse_flight.read_key = (g_mouse_flight.read_key + 1) % 16;
	return key;
}

void xvt_mouse_flight_set_options(const struct xvt_mouse_options *options)
{

	if (!options) {
		return;
	}

	g_mouse_flight.options = *options;
	xvt_mouse_flight_reset();
}

void xvt_mouse_flight_pump(void)
{
	const AeronInputSnapshot *in = Aeron_InputSnapshot();

	if (!g_mouse_flight.options.mouse_flight_enabled || !in) {
		xvt_mouse_flight_reset();
		return;
	}
	if (in->frame_id == g_mouse_flight.pumped_frame) {
		return;
	}
	g_mouse_flight.pumped_frame = in->frame_id;
	/* Only a captured pointer belongs to flight controls: while the capture is
	 * released to the OS, motion over the window must not steer the ship. */
	if (!(xvt_input_mouse_flight_allowed() && Aeron_RelativeMouseMode())) {
		xvt_mouse_flight_reset();
		return;
	}
	g_mouse_flight.active = 1;
	g_mouse_flight.pending_x += in->mouse.relative_x;
	g_mouse_flight.pending_y += in->mouse.relative_y;
	g_mouse_flight.buttons =
		(xvt_input_filter_mouse_buttons(in->mouse.buttons) &
		 AERON_MOUSE_BUTTON_LEFT) != 0;
	{
		const int rmb =
			(xvt_input_filter_mouse_buttons(in->mouse.buttons) &
			 AERON_MOUSE_BUTTON_RIGHT) != 0;
		const uint64_t now = Aeron_NowUs();

		if (rmb && !g_mouse_flight.rmb_down) {
			g_mouse_flight.rmb_press_time_us = now;
		} else if (!rmb && g_mouse_flight.rmb_down &&
			   now - g_mouse_flight.rmb_press_time_us <
				   MOUSE_FLIGHT_TAP_US) {
			g_mouse_flight.target_tap = 1;
		}
		g_mouse_flight.rmb_down = rmb;
		g_mouse_flight.roll_lock =
			rmb && now - g_mouse_flight.rmb_press_time_us >=
				       MOUSE_FLIGHT_TAP_US;
	}
	if ((unsigned)g_local_player < 8 &&
	    g_players[g_local_player].chat_recipient_mode ==
		    FLIGHT_CHAT_RECIPIENT_INACTIVE) {
		if (g_mouse_flight.target_tap) {
			xvt_mouse_flight_queue_key(FLIGHT_KEY_ALT_1);
		}
		uint32_t pressed = xvt_input_filter_mouse_buttons(
			in->mouse.pressed_buttons);
		if (pressed & AERON_MOUSE_BUTTON_MIDDLE) {
			xvt_mouse_flight_queue_key(xvt_input_actions_key(
				XVT_INPUT_ACTION_TARGET_NEAREST_FIGHTER_OR_MINE));
		}
		if (pressed & AERON_MOUSE_BUTTON_X1) {
			xvt_mouse_flight_queue_key(xvt_input_actions_key(
				XVT_INPUT_ACTION_VIEW_TOGGLE_COCKPIT));
		}
	} else {
		g_mouse_flight.read_key = 0;
		g_mouse_flight.write_key = 0;
	}
	g_mouse_flight.target_tap = 0;
}

static float xvt_mouse_flight_clamp_stick(float value)
{
	if (value > 127.0f) {
		return 127.0f;
	}
	if (value < -127.0f) {
		return -127.0f;
	}
	return value;
}

static int xvt_mouse_flight_stick_axis(float value)
{
	return (int)floorf(value + 0.5f);
}

/* Virtual stick: mouse displacement moves a held virtual-stick deflection.
 * While roll-lock is held, X motion moves a transient roll deflection and the
 * yaw/pitch stick is frozen; roll recenters when the button is released. */
static void xvt_mouse_flight_update_stick(float sensitivity)
{
	const float gain = MOUSE_FLIGHT_STICK_GAIN * sensitivity;
	float delta_y = g_mouse_flight.pending_y * gain;

	/* Mouse Y is positive downward; flight pitch is positive nose-up. */
	if (!g_mouse_flight.options.mouse_invert_y) {
		delta_y = -delta_y;
	}
	if (g_mouse_flight.roll_lock != g_mouse_flight.drained_roll_lock) {
		g_mouse_flight.drained_roll_lock = g_mouse_flight.roll_lock;
		g_mouse_flight.stick_r = 0.0f;
	}
	if (g_mouse_flight.roll_lock) {
		g_mouse_flight.stick_r = xvt_mouse_flight_clamp_stick(
			g_mouse_flight.stick_r +
			g_mouse_flight.pending_x * gain);
	} else {
		g_mouse_flight.stick_r = 0.0f;
		g_mouse_flight.stick_x = xvt_mouse_flight_clamp_stick(
			g_mouse_flight.stick_x +
			g_mouse_flight.pending_x * gain);
		g_mouse_flight.stick_y = xvt_mouse_flight_clamp_stick(
			g_mouse_flight.stick_y + delta_y);
	}
	g_mouse_flight.axis_x =
		xvt_mouse_flight_stick_axis(g_mouse_flight.stick_x);
	g_mouse_flight.axis_y =
		xvt_mouse_flight_stick_axis(g_mouse_flight.stick_y);
	g_mouse_flight.axis_r =
		xvt_mouse_flight_stick_axis(g_mouse_flight.stick_r);
}

int xvt_mouse_flight_sample(void)
{
	if (!xvt_input_mouse_flight_allowed()) {
		xvt_mouse_flight_reset();
		return 0;
	}
	xvt_mouse_flight_pump();
	if (!g_mouse_flight.active) {
		return 0;
	}

	uint64_t now = Aeron_NowUs();
	uint64_t interval_us = now - g_mouse_flight.drain_time_us;
	/* Discard transition motion on first sampling or after a stall, preserving
	 * the held stick deflection. */
	if (g_mouse_flight.drain_time_us == 0 ||
	    interval_us > MOUSE_FLIGHT_MAX_SAMPLE_GAP_US) {
		g_mouse_flight.drain_time_us = now;
		g_mouse_flight.pending_x = 0.0f;
		g_mouse_flight.pending_y = 0.0f;
		g_mouse_flight.axis_x =
			xvt_mouse_flight_stick_axis(g_mouse_flight.stick_x);
		g_mouse_flight.axis_y =
			xvt_mouse_flight_stick_axis(g_mouse_flight.stick_y);
		g_mouse_flight.axis_r =
			xvt_mouse_flight_stick_axis(g_mouse_flight.stick_r);
		return 1;
	}
	g_mouse_flight.drain_time_us = now;

	float sensitivity =
		k_mouse_flight_sensitivity_scale[g_mouse_flight.options
							 .mouse_sensitivity -
						 XVT_MOUSE_SENSITIVITY_MIN];
	xvt_mouse_flight_update_stick(sensitivity);
	g_mouse_flight.pending_x = 0.0f;
	g_mouse_flight.pending_y = 0.0f;
	return 1;
}

void xvt_mouse_flight_get_axes(int *axis_x, int *axis_y, int *axis_r)
{
	if (axis_x) {
		*axis_x = g_mouse_flight.axis_x;
	}
	if (axis_y) {
		*axis_y = g_mouse_flight.axis_y;
	}
	if (axis_r) {
		*axis_r = g_mouse_flight.axis_r;
	}
}

int xvt_mouse_flight_buttons_mask(void) { return g_mouse_flight.buttons; }

int xvt_mouse_flight_get_hud_marker(int *deflection_x, int *deflection_y)
{
	if (!xvt_input_mouse_flight_allowed() || !g_mouse_flight.active) {
		return 0;
	}
	if (deflection_x) {
		*deflection_x =
			xvt_mouse_flight_stick_axis(g_mouse_flight.stick_x);
	}
	if (deflection_y) {
		*deflection_y =
			xvt_mouse_flight_stick_axis(g_mouse_flight.stick_y);
	}
	return 1;
}

void xvt_mouse_flight_discard_pending(void)
{
	g_mouse_flight.pending_x = 0.0f;
	g_mouse_flight.pending_y = 0.0f;
	g_mouse_flight.read_key = 0;
	g_mouse_flight.write_key = 0;
	g_mouse_flight.target_tap = 0;
	g_mouse_flight.drain_time_us = Aeron_NowUs();
}
