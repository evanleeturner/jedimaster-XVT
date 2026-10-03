#include "xvt/input/joystick.h"
#include "aeron/compat/mmsystem.h"
#include "xvt/frontend/frontend_joystick.h"

#include <string.h>

/* Per joystick index, 0 and 1: 1 once Joystick_PollScaledAxes has calibrated
 * that index. Only that function writes it, and nothing sets it back to 0. */
// GLOBAL: XVT 0x527F48
int g_joystickCalibrationInitialized[2] = {0, 0};
/* Axis calibration Joystick_PollScaledAxes takes from the device's capabilities
 * on each index's first poll: ranges 1 and the rest 0 until then, and again
 * when no device answers. One copy serves both indexes, so calibrating index 1
 * rewrites what index 0 stored. */
// GLOBAL: XVT 0x527F50
struct JoystickCalibration g_joystickCalibration = {1, 1, 1, 0, 0,
						    0, 0, 0, 0, 0};
/* Raw Z position that counts as centered: (range >> 1) + wZmin from the
 * device's capabilities. Written only by Joystick_PollScaledAxes when it
 * calibrates and a device answers; 0 before. */
// GLOBAL: XVT 0x622CA0
int g_joyAxisCenterZ = 0;
/* WinMM joystick id each index, 0 and 1, reads: what Joystick_GetDeviceId(0)
 * returned, or Joystick_GetDeviceId(1) when the first had no capabilities.
 * Written only by Joystick_PollScaledAxes when it calibrates; stays 0 when
 * neither answers. */
// GLOBAL: XVT 0x622CA8
unsigned int g_joyDeviceId[2] = {0, 0};
/* Raw X position that counts as centered: (range >> 1) + wXmin. Written like
 * g_joyAxisCenterZ. */
// GLOBAL: XVT 0x622CB4
int g_joyAxisCenterX = 0;
/* Raw Y position that counts as centered: (range >> 1) + wYmin. Written like
 * g_joyAxisCenterZ. */
// GLOBAL: XVT 0x622CB8
int g_joyAxisCenterY = 0;
/* 1 when the joystick detection found a joystick, else 0. Written only by
 * Input_InitializeJoystickBackend and Input_DetectActiveJoystick, the first
 * time either runs; Joystick_PollScaledAxesIfActive polls only while it is
 * set. */
// GLOBAL: XVT 0x5280F8
int g_joystickActive = 0;
/* Joystick index, 0 or 1, that Joystick_PollScaledAxesIfActive polls: the first
 * index Input_ProbeActiveJoystickDevices found answering. Only that function
 * writes it. */
// GLOBAL: XVT 0x5280FC
int g_joyDeviceIndex = 0;

/* Reads joystick index 0 or 1 (any nonzero deviceIndex counts as 1) through
 * WinMM. On an index's first call it calibrates: it asks joyGetDevCapsA for the
 * device Joystick_GetDeviceId(0) names, then for Joystick_GetDeviceId(1),
 * whatever the index, and from the first that answers stores the device in
 * g_joyDeviceId and the ranges (max - min), deadzones (range / 20), normalize
 * offsets ((range >> 1) - max) and hat flag in g_joystickCalibration, and the
 * centers in g_joyAxisCenterX, Y and Z; with neither answering it sets the
 * ranges to 1 and the rest of g_joystickCalibration to 0. Then it reads
 * joyGetPosEx. An axis whose distance from its center is over its deadzone
 * gives (int)(255u * (offset + position) / range), worked in unsigned
 * arithmetic; any other gives 0, and Z also needs a range over 0. A position
 * under max - (range >> 1) wraps in that arithmetic and gives a large positive
 * value, not a negative one: for a device reporting 0 to 65535, as Aeron's
 * joyGetDevCapsA does, positions under 32768 give 65409 to 65536 and the rest
 * 0 to 127. *pButtons gets the low 16 button bits, plus
 * 0x10000 << (dwPOV / 9000) when the device has a hat that is not centered.
 * When joyGetPosEx fails, the three axes are 32000 and *pButtons is 0. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AABA0
void Joystick_PollScaledAxes(int deviceIndex, int *pAxisX, int *pAxisY,
			     int *pAxisZ, int *pButtons)
{
	JOYINFOEX joystickInfo;
	JOYCAPSA joystickCaps;
	int absDelta;

	deviceIndex = deviceIndex != 0;
	if (g_joystickCalibrationInitialized[deviceIndex] == 0) {
		g_joystickCalibrationInitialized[deviceIndex] = 1;
		memset(&joystickCaps, 0, sizeof(joystickCaps));
		if (joyGetDevCapsA(Joystick_GetDeviceId(0), &joystickCaps,
				   sizeof(joystickCaps)) == JOYERR_NOERROR) {
			g_joyDeviceId[deviceIndex] = Joystick_GetDeviceId(0);
			g_joystickCalibration.hasPov =
				(joystickCaps.wCaps & JOYCAPS_HASPOV) != 0;
			g_joystickCalibration.axisRangeX =
				(int)(joystickCaps.wXmax - joystickCaps.wXmin);
			g_joystickCalibration.axisRangeY =
				(int)(joystickCaps.wYmax - joystickCaps.wYmin);
			g_joystickCalibration.axisRangeZ =
				(int)(joystickCaps.wZmax - joystickCaps.wZmin);
			g_joystickCalibration.axisNormalizeOffsetX =
				(g_joystickCalibration.axisRangeX >> 1) -
				(int)joystickCaps.wXmax;
			g_joystickCalibration.axisNormalizeOffsetY =
				(g_joystickCalibration.axisRangeY >> 1) -
				(int)joystickCaps.wYmax;
			g_joystickCalibration.axisNormalizeOffsetZ =
				(g_joystickCalibration.axisRangeZ >> 1) -
				(int)joystickCaps.wZmax;
			g_joystickCalibration.axisDeadzoneX =
				g_joystickCalibration.axisRangeX / 20;
			g_joystickCalibration.axisDeadzoneY =
				g_joystickCalibration.axisRangeY / 20;
			g_joystickCalibration.axisDeadzoneZ =
				g_joystickCalibration.axisRangeZ / 20;
			g_joyAxisCenterX =
				(g_joystickCalibration.axisRangeX >> 1) +
				(int)joystickCaps.wXmin;
			g_joyAxisCenterY =
				(g_joystickCalibration.axisRangeY >> 1) +
				(int)joystickCaps.wYmin;
			g_joyAxisCenterZ =
				(g_joystickCalibration.axisRangeZ >> 1) +
				(int)joystickCaps.wZmin;
		} else if (joyGetDevCapsA(
				   Joystick_GetDeviceId(1), &joystickCaps,
				   sizeof(joystickCaps)) == JOYERR_NOERROR) {
			g_joyDeviceId[deviceIndex] = Joystick_GetDeviceId(1);
			g_joystickCalibration.hasPov =
				(joystickCaps.wCaps & JOYCAPS_HASPOV) != 0;
			g_joystickCalibration.axisRangeX =
				(int)(joystickCaps.wXmax - joystickCaps.wXmin);
			g_joystickCalibration.axisRangeY =
				(int)(joystickCaps.wYmax - joystickCaps.wYmin);
			g_joystickCalibration.axisRangeZ =
				(int)(joystickCaps.wZmax - joystickCaps.wZmin);
			g_joystickCalibration.axisNormalizeOffsetX =
				(g_joystickCalibration.axisRangeX >> 1) -
				(int)joystickCaps.wXmax;
			g_joystickCalibration.axisNormalizeOffsetY =
				(g_joystickCalibration.axisRangeY >> 1) -
				(int)joystickCaps.wYmax;
			g_joystickCalibration.axisNormalizeOffsetZ =
				(g_joystickCalibration.axisRangeZ >> 1) -
				(int)joystickCaps.wZmax;
			g_joystickCalibration.axisDeadzoneX =
				g_joystickCalibration.axisRangeX / 20;
			g_joystickCalibration.axisDeadzoneY =
				g_joystickCalibration.axisRangeY / 20;
			g_joystickCalibration.axisDeadzoneZ =
				g_joystickCalibration.axisRangeZ / 20;
			g_joyAxisCenterX =
				(g_joystickCalibration.axisRangeX >> 1) +
				(int)joystickCaps.wXmin;
			g_joyAxisCenterY =
				(g_joystickCalibration.axisRangeY >> 1) +
				(int)joystickCaps.wYmin;
			g_joyAxisCenterZ =
				(g_joystickCalibration.axisRangeZ >> 1) +
				(int)joystickCaps.wZmin;
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
	joystickInfo.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNZ |
			       JOY_RETURNBUTTONS | JOY_RETURNPOV |
			       JOY_RETURNCENTERED;
	if (joyGetPosEx(g_joyDeviceId[deviceIndex], &joystickInfo) ==
	    JOYERR_NOERROR) {
		absDelta = (int)joystickInfo.dwXpos - g_joyAxisCenterX;
		if (absDelta < 0) {
			absDelta = -absDelta;
		}
		if (absDelta > g_joystickCalibration.axisDeadzoneX) {
			*pAxisX = (int)(255u *
					(g_joystickCalibration
						 .axisNormalizeOffsetX +
					 joystickInfo.dwXpos) /
					g_joystickCalibration.axisRangeX);
		} else {
			*pAxisX = 0;
		}

		absDelta = (int)joystickInfo.dwYpos - g_joyAxisCenterY;
		if (absDelta < 0) {
			absDelta = -absDelta;
		}
		if (absDelta > g_joystickCalibration.axisDeadzoneY) {
			*pAxisY = (int)(255u *
					(g_joystickCalibration
						 .axisNormalizeOffsetY +
					 joystickInfo.dwYpos) /
					g_joystickCalibration.axisRangeY);
		} else {
			*pAxisY = 0;
		}

		absDelta = (int)joystickInfo.dwZpos - g_joyAxisCenterZ;
		if (absDelta < 0) {
			absDelta = -absDelta;
		}
		if (absDelta > g_joystickCalibration.axisDeadzoneZ &&
		    g_joystickCalibration.axisRangeZ > 0) {
			*pAxisZ = (int)(255u *
					(g_joystickCalibration
						 .axisNormalizeOffsetZ +
					 joystickInfo.dwZpos) /
					g_joystickCalibration.axisRangeZ);
		} else {
			*pAxisZ = 0;
		}

		*pButtons = (int)(joystickInfo.dwButtons & 0xffff);
		if (g_joystickCalibration.hasPov != 0 &&
		    joystickInfo.dwPOV != JOY_POVCENTERED) {
			*pButtons |= 0x10000 << (joystickInfo.dwPOV / 0x2328u);
		}
	} else {
		*pAxisX = 32000;
		*pAxisY = 32000;
		*pAxisZ = 32000;
	}
}

/* Returns 1 and does nothing else. */
// FUNCTION: XVT 0x4AC830
int16_t Joystick_InitializeBackendStub(void) { return 1; }

/* When g_joystickActive is 0, sets the three axes to 0 and returns 0 without
 * polling. Otherwise polls index g_joyDeviceIndex with Joystick_PollScaledAxes
 * and returns its buttons. pAxisR is ignored. Only the original build reaches
 * its one call, in FlightInput_Read; the modern build returns before it. */
// FUNCTION: XVT 0x4ACB90
int Joystick_PollScaledAxesIfActive(int *pAxisX, int *pAxisY, int *pAxisZ,
				    int *pAxisR)
{
	int buttons;

	(void)pAxisR;

	if (g_joystickActive == 0) {
		*pAxisX = 0;
		*pAxisY = 0;
		*pAxisZ = 0;
		return 0;
	}

	Joystick_PollScaledAxes(g_joyDeviceIndex, pAxisX, pAxisY, pAxisZ,
				&buttons);
	return buttons;
}
