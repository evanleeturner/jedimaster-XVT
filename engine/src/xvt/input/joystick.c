#include "xvt/input/joystick.h"
#include "aeron/compat/mmsystem.h"
#include "xvt/frontend/frontend_joystick.h"

#include <string.h>

// GLOBAL: XVT 0x527F48
int g_joystickCalibrationInitialized[2] = { 0, 0 };
// GLOBAL: XVT 0x527F50
JoystickCalibration g_joystickCalibration = { 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
// GLOBAL: XVT 0x622CA0
int g_joyAxisCenterZ = 0;
// GLOBAL: XVT 0x622CA8
unsigned int g_joyDeviceId[2] = { 0, 0 };
// GLOBAL: XVT 0x622CB4
int g_joyAxisCenterX = 0;
// GLOBAL: XVT 0x622CB8
int g_joyAxisCenterY = 0;
// GLOBAL: XVT 0x5280F8
int g_joystickActive = 0;
// GLOBAL: XVT 0x5280FC
int g_joyDeviceIndex = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AABA0
void Joystick_PollRawAxes(int deviceIndex, int* pAxisX, int* pAxisY, int* pAxisZ, int* pButtons) {
	JOYINFOEX joystickInfo;
	JOYCAPSA joystickCaps;
	int absDelta;

	deviceIndex = deviceIndex != 0;
	if (g_joystickCalibrationInitialized[deviceIndex] == 0) {
		g_joystickCalibrationInitialized[deviceIndex] = 1;
		memset(&joystickCaps, 0, sizeof(joystickCaps));
		if (joyGetDevCapsA(Joystick_GetDeviceId(0), &joystickCaps, sizeof(joystickCaps)) == JOYERR_NOERROR) {
			g_joyDeviceId[deviceIndex] = Joystick_GetDeviceId(0);
			g_joystickCalibration.hasPov = (joystickCaps.wCaps & JOYCAPS_HASPOV) != 0;
			g_joystickCalibration.axisRangeX = (int)(joystickCaps.wXmax - joystickCaps.wXmin);
			g_joystickCalibration.axisRangeY = (int)(joystickCaps.wYmax - joystickCaps.wYmin);
			g_joystickCalibration.axisRangeZ = (int)(joystickCaps.wZmax - joystickCaps.wZmin);
			g_joystickCalibration.axisNormalizeOffsetX =
				(g_joystickCalibration.axisRangeX >> 1) - (int)joystickCaps.wXmax;
			g_joystickCalibration.axisNormalizeOffsetY =
				(g_joystickCalibration.axisRangeY >> 1) - (int)joystickCaps.wYmax;
			g_joystickCalibration.axisNormalizeOffsetZ =
				(g_joystickCalibration.axisRangeZ >> 1) - (int)joystickCaps.wZmax;
			g_joystickCalibration.axisDeadzoneX = g_joystickCalibration.axisRangeX / 20;
			g_joystickCalibration.axisDeadzoneY = g_joystickCalibration.axisRangeY / 20;
			g_joystickCalibration.axisDeadzoneZ = g_joystickCalibration.axisRangeZ / 20;
			g_joyAxisCenterX = (g_joystickCalibration.axisRangeX >> 1) + (int)joystickCaps.wXmin;
			g_joyAxisCenterY = (g_joystickCalibration.axisRangeY >> 1) + (int)joystickCaps.wYmin;
			g_joyAxisCenterZ = (g_joystickCalibration.axisRangeZ >> 1) + (int)joystickCaps.wZmin;
		} else if (joyGetDevCapsA(Joystick_GetDeviceId(1), &joystickCaps, sizeof(joystickCaps)) ==
				   JOYERR_NOERROR) {
			g_joyDeviceId[deviceIndex] = Joystick_GetDeviceId(1);
			g_joystickCalibration.hasPov = (joystickCaps.wCaps & JOYCAPS_HASPOV) != 0;
			g_joystickCalibration.axisRangeX = (int)(joystickCaps.wXmax - joystickCaps.wXmin);
			g_joystickCalibration.axisRangeY = (int)(joystickCaps.wYmax - joystickCaps.wYmin);
			g_joystickCalibration.axisRangeZ = (int)(joystickCaps.wZmax - joystickCaps.wZmin);
			g_joystickCalibration.axisNormalizeOffsetX =
				(g_joystickCalibration.axisRangeX >> 1) - (int)joystickCaps.wXmax;
			g_joystickCalibration.axisNormalizeOffsetY =
				(g_joystickCalibration.axisRangeY >> 1) - (int)joystickCaps.wYmax;
			g_joystickCalibration.axisNormalizeOffsetZ =
				(g_joystickCalibration.axisRangeZ >> 1) - (int)joystickCaps.wZmax;
			g_joystickCalibration.axisDeadzoneX = g_joystickCalibration.axisRangeX / 20;
			g_joystickCalibration.axisDeadzoneY = g_joystickCalibration.axisRangeY / 20;
			g_joystickCalibration.axisDeadzoneZ = g_joystickCalibration.axisRangeZ / 20;
			g_joyAxisCenterX = (g_joystickCalibration.axisRangeX >> 1) + (int)joystickCaps.wXmin;
			g_joyAxisCenterY = (g_joystickCalibration.axisRangeY >> 1) + (int)joystickCaps.wYmin;
			g_joyAxisCenterZ = (g_joystickCalibration.axisRangeZ >> 1) + (int)joystickCaps.wZmin;
		} else {
			g_joystickCalibration.hasPov = 0;
			g_joystickCalibration.axisRangeX = 1;
			g_joystickCalibration.axisRangeY = 1;
			g_joystickCalibration.axisRangeZ = 1;
			g_joystickCalibration.axisNormalizeOffsetX = 0;
			g_joystickCalibration.axisNormalizeOffsetY = 0;
			g_joystickCalibration.axisNormalizeOffsetZ = 0;
			g_joystickCalibration.axisDeadzoneX = 0;
			g_joystickCalibration.axisDeadzoneY = 0;
			g_joystickCalibration.axisDeadzoneZ = 0;
		}
	}

	*pButtons = 0;
	memset(&joystickInfo, 0, sizeof(joystickInfo));
	joystickInfo.dwSize = sizeof(joystickInfo);
	joystickInfo.dwFlags =
		JOY_RETURNX | JOY_RETURNY | JOY_RETURNZ | JOY_RETURNBUTTONS | JOY_RETURNPOV | JOY_RETURNCENTERED;
	if (joyGetPosEx(g_joyDeviceId[deviceIndex], &joystickInfo) == JOYERR_NOERROR) {
		absDelta = (int)joystickInfo.dwXpos - g_joyAxisCenterX;
		if (absDelta < 0) {
			absDelta = -absDelta;
		}
		if (absDelta > g_joystickCalibration.axisDeadzoneX) {
			*pAxisX = (int)(255u * (g_joystickCalibration.axisNormalizeOffsetX + joystickInfo.dwXpos) /
							g_joystickCalibration.axisRangeX);
		} else {
			*pAxisX = 0;
		}

		absDelta = (int)joystickInfo.dwYpos - g_joyAxisCenterY;
		if (absDelta < 0) {
			absDelta = -absDelta;
		}
		if (absDelta > g_joystickCalibration.axisDeadzoneY) {
			*pAxisY = (int)(255u * (g_joystickCalibration.axisNormalizeOffsetY + joystickInfo.dwYpos) /
							g_joystickCalibration.axisRangeY);
		} else {
			*pAxisY = 0;
		}

		absDelta = (int)joystickInfo.dwZpos - g_joyAxisCenterZ;
		if (absDelta < 0) {
			absDelta = -absDelta;
		}
		if (absDelta > g_joystickCalibration.axisDeadzoneZ && g_joystickCalibration.axisRangeZ > 0) {
			*pAxisZ = (int)(255u * (g_joystickCalibration.axisNormalizeOffsetZ + joystickInfo.dwZpos) /
							g_joystickCalibration.axisRangeZ);
		} else {
			*pAxisZ = 0;
		}

		*pButtons = (int)(joystickInfo.dwButtons & 0xffff);
		if (g_joystickCalibration.hasPov != 0 && joystickInfo.dwPOV != JOY_POVCENTERED) {
			*pButtons |= 0x10000 << (joystickInfo.dwPOV / 0x2328u);
		}
	} else {
		*pAxisX = 32000;
		*pAxisY = 32000;
		*pAxisZ = 32000;
	}
}

// FUNCTION: XVT 0x4AC830
int16_t Joystick_InitializeBackendStub(void) { return 1; }

// FUNCTION: XVT 0x4ACB90
int Joystick_PollRawAxesIfEnabled(int* pAxisX, int* pAxisY, int* pAxisZ, int* pAxisR) {
	int buttons;

	(void)pAxisR;

	if (g_joystickActive == 0) {
		*pAxisX = 0;
		*pAxisY = 0;
		*pAxisZ = 0;
		return 0;
	}

	Joystick_PollRawAxes(g_joyDeviceIndex, pAxisX, pAxisY, pAxisZ, &buttons);
	return buttons;
}
