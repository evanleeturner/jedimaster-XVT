#include "xvt_runtime/input/mouse_flight.h"

#include "aeron/aeron.h"
#include "aeron/input.h"
#include "aeron/time.h"

#include "xvt/flight/flight_input.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/input/actions.h"
#include "xvt_runtime/input/capture.h"
#include <math.h>
#include <stdint.h>

#define MOUSE_FLIGHT_MAX_FRAME_US 100000
/* Virtual stick: axis units per pixel of mouse travel at the default
 * sensitivity notch (full deflection after ~256 px). */
#define MOUSE_FLIGHT_STICK_GAIN (127.0f / 256.0f)
/* Right-button tap window: release inside it emits the target-in-sight tap
 * action, roll-lock engages only after it. Matches the recovered 59-sim-tick
 * window Flight_UpdateEntity uses for joystick button 2. */
#define MOUSE_FLIGHT_TAP_US 250000u

static struct {
	XvtMouseOptions options;
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
	unsigned read_key, write_key;
} g_mouseFlight;

/* Doubling steps (1/16x..16x): raw relative deltas vary by more than an
 * order of magnitude between touchpads and high-DPI mice. */
static const float k_mouseFlightSensitivityScale[XVT_MOUSE_SENSITIVITY_MAX] = {
	0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f,
};

void XvtMouseFlight_Reset(void) {
	g_mouseFlight.pending_x = 0.0f;
	g_mouseFlight.pending_y = 0.0f;
	g_mouseFlight.stick_x = 0.0f;
	g_mouseFlight.stick_y = 0.0f;
	g_mouseFlight.stick_r = 0.0f;
	g_mouseFlight.axis_x = 0;
	g_mouseFlight.axis_y = 0;
	g_mouseFlight.axis_r = 0;
	g_mouseFlight.buttons = 0;
	g_mouseFlight.roll_lock = 0;
	g_mouseFlight.drained_roll_lock = 0;
	g_mouseFlight.rmb_down = 0;
	g_mouseFlight.target_tap = 0;
	g_mouseFlight.drain_time_us = 0;
	g_mouseFlight.active = 0;
	g_mouseFlight.read_key = g_mouseFlight.write_key = 0;
}

static void XvtMouseFlight_QueueKey(uint16_t key) {
	unsigned next = (g_mouseFlight.write_key + 1) % 16;
	if (next == g_mouseFlight.read_key) {
		Aeron_LogWarn("xvt.input", "Mouse command queue is full");
		return;
	}
	g_mouseFlight.keys[g_mouseFlight.write_key] = key;
	g_mouseFlight.write_key = next;
}

uint16_t XvtMouseFlight_ReadKey(void) {
	if (!XvtInput_MouseFlightAllowed() || (unsigned)g_localPlayer >= 8 ||
		g_players[g_localPlayer].msgTypeId != FLIGHT_CHAT_RECIPIENT_INACTIVE) {
		g_mouseFlight.read_key = g_mouseFlight.write_key = 0;
		return 0;
	}
	if (g_mouseFlight.read_key == g_mouseFlight.write_key)
		return 0;
	uint16_t key = g_mouseFlight.keys[g_mouseFlight.read_key];
	g_mouseFlight.read_key = (g_mouseFlight.read_key + 1) % 16;
	return key;
}

void XvtMouseFlight_SetOptions(const XvtMouseOptions* options) {

	if (!options) {
		return;
	}

	g_mouseFlight.options = *options;
	XvtMouseFlight_Reset();
}

void XvtMouseFlight_Pump(void) {
	const AeronInputSnapshot* in = Aeron_InputSnapshot();

	if (!g_mouseFlight.options.mouse_flight_enabled || !in) {
		XvtMouseFlight_Reset();
		return;
	}
	if (in->frame_id == g_mouseFlight.pumped_frame) {
		return;
	}
	g_mouseFlight.pumped_frame = in->frame_id;
	/* Only a captured pointer belongs to flight controls: while the capture is
	 * released to the OS, motion over the window must not steer the ship. */
	if (!(XvtInput_MouseFlightAllowed() && Aeron_RelativeMouseMode())) {
		XvtMouseFlight_Reset();
		return;
	}
	g_mouseFlight.active = 1;
	g_mouseFlight.pending_x += in->mouse.relative_x;
	g_mouseFlight.pending_y += in->mouse.relative_y;
	g_mouseFlight.buttons = (XvtInput_FilterMouseButtons(in->mouse.buttons) & AERON_MOUSE_BUTTON_LEFT) != 0;
	{
		const int rmb = (XvtInput_FilterMouseButtons(in->mouse.buttons) & AERON_MOUSE_BUTTON_RIGHT) != 0;
		const uint64_t now = Aeron_NowUs();

		if (rmb && !g_mouseFlight.rmb_down) {
			g_mouseFlight.rmb_press_time_us = now;
		} else if (!rmb && g_mouseFlight.rmb_down &&
				   now - g_mouseFlight.rmb_press_time_us < MOUSE_FLIGHT_TAP_US) {
			g_mouseFlight.target_tap = 1;
		}
		g_mouseFlight.rmb_down = rmb;
		g_mouseFlight.roll_lock = rmb && now - g_mouseFlight.rmb_press_time_us >= MOUSE_FLIGHT_TAP_US;
	}
	if ((unsigned)g_localPlayer < 8 && g_players[g_localPlayer].msgTypeId == FLIGHT_CHAT_RECIPIENT_INACTIVE) {
		if (g_mouseFlight.target_tap)
			XvtMouseFlight_QueueKey(FLIGHT_KEY_ALT_1);
		uint32_t pressed = XvtInput_FilterMouseButtons(in->mouse.pressed_buttons);
		if (pressed & AERON_MOUSE_BUTTON_MIDDLE)
			XvtMouseFlight_QueueKey(XvtInputActions_Key(XVT_INPUT_ACTION_TARGET_NEAREST_FIGHTER_OR_MINE));
		if (pressed & AERON_MOUSE_BUTTON_X1)
			XvtMouseFlight_QueueKey(XvtInputActions_Key(XVT_INPUT_ACTION_VIEW_TOGGLE_COCKPIT));
	} else {
		g_mouseFlight.read_key = g_mouseFlight.write_key = 0;
	}
	g_mouseFlight.target_tap = 0;
}

static float XvtMouseFlight_ClampStick(float value) {
	if (value > 127.0f) {
		return 127.0f;
	}
	if (value < -127.0f) {
		return -127.0f;
	}
	return value;
}

static int XvtMouseFlight_StickAxis(float value) { return (int)floorf(value + 0.5f); }

/* Virtual stick: mouse displacement moves a held virtual-stick deflection.
 * While roll-lock is held, X motion moves a transient roll deflection and the
 * yaw/pitch stick is frozen; roll recenters when the button is released. */
static void XvtMouseFlight_UpdateStick(float sensitivity) {
	const float gain = MOUSE_FLIGHT_STICK_GAIN * sensitivity;
	float delta_y = g_mouseFlight.pending_y * gain;

	/* Mouse Y is positive downward; flight pitch is positive nose-up. */
	if (!g_mouseFlight.options.mouse_invert_y) {
		delta_y = -delta_y;
	}
	if (g_mouseFlight.roll_lock != g_mouseFlight.drained_roll_lock) {
		g_mouseFlight.drained_roll_lock = g_mouseFlight.roll_lock;
		g_mouseFlight.stick_r = 0.0f;
	}
	if (g_mouseFlight.roll_lock) {
		g_mouseFlight.stick_r =
			XvtMouseFlight_ClampStick(g_mouseFlight.stick_r + g_mouseFlight.pending_x * gain);
	} else {
		g_mouseFlight.stick_r = 0.0f;
		g_mouseFlight.stick_x =
			XvtMouseFlight_ClampStick(g_mouseFlight.stick_x + g_mouseFlight.pending_x * gain);
		g_mouseFlight.stick_y = XvtMouseFlight_ClampStick(g_mouseFlight.stick_y + delta_y);
	}
	g_mouseFlight.axis_x = XvtMouseFlight_StickAxis(g_mouseFlight.stick_x);
	g_mouseFlight.axis_y = XvtMouseFlight_StickAxis(g_mouseFlight.stick_y);
	g_mouseFlight.axis_r = XvtMouseFlight_StickAxis(g_mouseFlight.stick_r);
}

int XvtMouseFlight_Sample(void) {
	uint64_t now;
	uint64_t interval_us;
	float sensitivity;

	if (!XvtInput_MouseFlightAllowed()) {
		XvtMouseFlight_Reset();
		return 0;
	}
	XvtMouseFlight_Pump();
	if (!g_mouseFlight.active) {
		return 0;
	}

	now = Aeron_NowUs();
	interval_us = now - g_mouseFlight.drain_time_us;
	/* Discard transition motion on first sampling or after a stall, preserving
	 * the held stick deflection. */
	if (g_mouseFlight.drain_time_us == 0 || interval_us > MOUSE_FLIGHT_MAX_FRAME_US) {
		g_mouseFlight.drain_time_us = now;
		g_mouseFlight.pending_x = 0.0f;
		g_mouseFlight.pending_y = 0.0f;
		g_mouseFlight.axis_x = XvtMouseFlight_StickAxis(g_mouseFlight.stick_x);
		g_mouseFlight.axis_y = XvtMouseFlight_StickAxis(g_mouseFlight.stick_y);
		g_mouseFlight.axis_r = XvtMouseFlight_StickAxis(g_mouseFlight.stick_r);
		return 1;
	}
	g_mouseFlight.drain_time_us = now;

	sensitivity =
		k_mouseFlightSensitivityScale[g_mouseFlight.options.mouse_sensitivity - XVT_MOUSE_SENSITIVITY_MIN];
	XvtMouseFlight_UpdateStick(sensitivity);
	g_mouseFlight.pending_x = 0.0f;
	g_mouseFlight.pending_y = 0.0f;
	return 1;
}

void XvtMouseFlight_GetAxes(int* axisX, int* axisY, int* axisR) {
	if (axisX) {
		*axisX = g_mouseFlight.axis_x;
	}
	if (axisY) {
		*axisY = g_mouseFlight.axis_y;
	}
	if (axisR) {
		*axisR = g_mouseFlight.axis_r;
	}
}

int XvtMouseFlight_ButtonsMask(void) { return g_mouseFlight.buttons; }

int XvtMouseFlight_GetHudMarker(int* deflectionX, int* deflectionY) {
	if (!XvtInput_MouseFlightAllowed() || !g_mouseFlight.active) {
		return 0;
	}
	if (deflectionX) {
		*deflectionX = XvtMouseFlight_StickAxis(g_mouseFlight.stick_x);
	}
	if (deflectionY) {
		*deflectionY = XvtMouseFlight_StickAxis(g_mouseFlight.stick_y);
	}
	return 1;
}

void XvtMouseFlight_DiscardPending(void) {
	g_mouseFlight.pending_x = g_mouseFlight.pending_y = 0.0f;
	g_mouseFlight.read_key = g_mouseFlight.write_key = 0;
	g_mouseFlight.target_tap = 0;
	g_mouseFlight.drain_time_us = Aeron_NowUs();
}
