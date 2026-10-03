#include "xvt/input/input.h"
#include "xvt/input/joystick.h"

/* 1 once the joystick detection in input_detect_active_joystick or
 * input_initialize_joystick_backend has run; only those two write it, and
 * nothing sets it back to 0, so the detection runs once per run of the
 * program. */
// GLOBAL: XVT 0x5280F0
int g_joystick_detection_cached = 0;
/* What joystick_initialize_backend_stub returned when the detection ran, which
 * is always 1; 0 before. Read only by input_initialize_joystick_backend, which
 * nothing calls. */
// GLOBAL: XVT 0x5280F4
int g_joystick_backend_initialized = 0;

/* Runs the joystick detection the first time it or input_detect_active_joystick
 * is called: sets g_joystick_backend_initialized from
 * joystick_initialize_backend_stub, g_joystick_active from
 * input_probe_active_joystick_devices and g_joystick_detection_cached to 1. Returns
 * g_joystick_backend_initialized, which is 1 once the detection ran. Nothing
 * calls this. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4ACA90
int input_initialize_joystick_backend(void)
{
	int active;

	if (g_joystick_detection_cached == 0) {
		g_joystick_backend_initialized =
			joystick_initialize_backend_stub();
		active = input_probe_active_joystick_devices();
		g_joystick_detection_cached = 1;
		g_joystick_active = active;
	}
	return g_joystick_backend_initialized;
}

/* Runs the same once-only detection as input_initialize_joystick_backend and
 * returns g_joystick_active: 1 when one of the two joystick slots answered,
 * else 0. Later calls return the first answer without polling again.
 * flight_input_reset_runtime_state calls it at flight start. */
// FUNCTION: XVT 0x4ACAC0
int input_detect_active_joystick(void)
{
	int active;

	if (g_joystick_detection_cached == 0) {
		g_joystick_backend_initialized =
			joystick_initialize_backend_stub();
		active = input_probe_active_joystick_devices();
		g_joystick_detection_cached = 1;
		g_joystick_active = active;
	}
	return g_joystick_active;
}

/* Polls joystick index 0 with joystick_poll_scaled_axes and, when its X and Y
 * both read 32000 (what that function stores when the read fails), index 1.
 * Sets g_joy_device_index to the first index that does not read 32000 on both
 * axes and returns 1; returns 0 when both do, leaving g_joy_device_index as it
 * was. The polls also calibrate each index on its first use. */
// FUNCTION: XVT 0x4ACAF0
int input_probe_active_joystick_devices(void)
{
	int axis_x;
	int axis_y;
	int buttons;
	int axis_z;

	joystick_poll_scaled_axes(0, &axis_x, &axis_y, &axis_z, &buttons);
	if (axis_x == 32000 && axis_y == 32000) {
		joystick_poll_scaled_axes(1, &axis_x, &axis_y, &axis_z,
					  &buttons);
		if (axis_x == 32000 && axis_y == 32000) {
			return 0;
		}
		g_joy_device_index = 1;
		return 1;
	}

	g_joy_device_index = 0;
	return 1;
}
