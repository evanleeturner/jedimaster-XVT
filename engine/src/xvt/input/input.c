#include "xvt/input/input.h"
#include "xvt/input/joystick.h"

/* 1 once the joystick detection in Input_DetectActiveJoystick or
 * Input_InitializeJoystickBackend has run; only those two write it, and
 * nothing sets it back to 0, so the detection runs once per run of the
 * program. */
// GLOBAL: XVT 0x5280F0
int g_joystickDetectionCached = 0;
/* What Joystick_InitializeBackendStub returned when the detection ran, which
 * is always 1; 0 before. Read only by Input_InitializeJoystickBackend, which
 * nothing calls. */
// GLOBAL: XVT 0x5280F4
int g_joystickBackendInitialized = 0;

/* Runs the joystick detection the first time it or Input_DetectActiveJoystick
 * is called: sets g_joystickBackendInitialized from
 * Joystick_InitializeBackendStub, g_joystickActive from
 * Input_ProbeActiveJoystickDevices and g_joystickDetectionCached to 1. Returns
 * g_joystickBackendInitialized, which is 1 once the detection ran. Nothing
 * calls this. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4ACA90
int Input_InitializeJoystickBackend(void)
{
	int active;

	if (g_joystickDetectionCached == 0) {
		g_joystickBackendInitialized = Joystick_InitializeBackendStub();
		active = Input_ProbeActiveJoystickDevices();
		g_joystickDetectionCached = 1;
		g_joystickActive = active;
	}
	return g_joystickBackendInitialized;
}

/* Runs the same once-only detection as Input_InitializeJoystickBackend and
 * returns g_joystickActive: 1 when one of the two joystick slots answered,
 * else 0. Later calls return the first answer without polling again.
 * FlightInput_ResetRuntimeState calls it at flight start. */
// FUNCTION: XVT 0x4ACAC0
int Input_DetectActiveJoystick(void)
{
	int active;

	if (g_joystickDetectionCached == 0) {
		g_joystickBackendInitialized = Joystick_InitializeBackendStub();
		active = Input_ProbeActiveJoystickDevices();
		g_joystickDetectionCached = 1;
		g_joystickActive = active;
	}
	return g_joystickActive;
}

/* Polls joystick index 0 with Joystick_PollScaledAxes and, when its X and Y
 * both read 32000 (what that function stores when the read fails), index 1.
 * Sets g_joyDeviceIndex to the first index that does not read 32000 on both
 * axes and returns 1; returns 0 when both do, leaving g_joyDeviceIndex as it
 * was. The polls also calibrate each index on its first use. */
// FUNCTION: XVT 0x4ACAF0
int Input_ProbeActiveJoystickDevices(void)
{
	int axisX;
	int axisY;
	int buttons;
	int axisZ;

	Joystick_PollScaledAxes(0, &axisX, &axisY, &axisZ, &buttons);
	if (axisX == 32000 && axisY == 32000) {
		Joystick_PollScaledAxes(1, &axisX, &axisY, &axisZ, &buttons);
		if (axisX == 32000 && axisY == 32000) {
			return 0;
		}
		g_joyDeviceIndex = 1;
		return 1;
	}

	g_joyDeviceIndex = 0;
	return 1;
}
