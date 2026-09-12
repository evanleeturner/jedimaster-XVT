#include "xvt/input/input.h"
#include "xvt/input/joystick.h"

// GLOBAL: XVT 0x5280F0
int g_joystickDetectionCached = 0;
// GLOBAL: XVT 0x5280F4
int g_joystickBackendInitialized = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4ACA90
int Input_InitializeJoystickBackend(void) {
	int active;

	if (g_joystickDetectionCached == 0) {
		g_joystickBackendInitialized = Joystick_InitializeBackendStub();
		active = Input_ProbeActiveJoystickDevices();
		g_joystickDetectionCached = 1;
		g_joystickActive = active;
	}
	return g_joystickBackendInitialized;
}

// FUNCTION: XVT 0x4ACAC0
int Input_DetectActiveJoystick(void) {
	int active;

	if (g_joystickDetectionCached == 0) {
		g_joystickBackendInitialized = Joystick_InitializeBackendStub();
		active = Input_ProbeActiveJoystickDevices();
		g_joystickDetectionCached = 1;
		g_joystickActive = active;
	}
	return g_joystickActive;
}

// FUNCTION: XVT 0x4ACAF0
int Input_ProbeActiveJoystickDevices(void) {
	int axisX;
	int axisY;
	int buttons;
	int axisZ;

	Joystick_PollRawAxes(0, &axisX, &axisY, &axisZ, &buttons);
	if (axisX == 32000 && axisY == 32000) {
		Joystick_PollRawAxes(1, &axisX, &axisY, &axisZ, &buttons);
		if (axisX == 32000 && axisY == 32000) {
			return 0;
		}
		g_joyDeviceIndex = 1;
		return 1;
	}

	g_joyDeviceIndex = 0;
	return 1;
}
