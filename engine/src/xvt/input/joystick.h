#ifndef XVT_INPUT_JOYSTICK_H
#define XVT_INPUT_JOYSTICK_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct JoystickCalibration {
	int axisRangeX; /* wXmax - wXmin; 1 with no device. */
	int axisRangeY; /* wYmax - wYmin; 1 with no device. */
	int axisRangeZ; /* wZmax - wZmin; 1 with no device. */
	/* (axisRangeX >> 1) - wXmax, added to a raw X before scaling; 0 with no
	 * device. */
	int axisNormalizeOffsetX;
	/* (axisRangeY >> 1) - wYmax; 0 with no device. */
	int axisNormalizeOffsetY;
	/* (axisRangeZ >> 1) - wZmax; 0 with no device. */
	int axisNormalizeOffsetZ;
	/* axisRangeX / 20: a raw X no further than this from g_joyAxisCenterX
	 * reads 0. */
	int axisDeadzoneX;
	int axisDeadzoneY; /* axisRangeY / 20, the same for Y. */
	int axisDeadzoneZ; /* axisRangeZ / 20, the same for Z. */
	int hasPov; /* 1 when the capabilities carry JOYCAPS_HASPOV, else 0. */
};

extern int g_joystickCalibrationInitialized[2];
extern struct JoystickCalibration g_joystickCalibration;
extern int g_joyAxisCenterZ;
extern unsigned int g_joyDeviceId[2];
extern int g_joyAxisCenterX;
extern int g_joyAxisCenterY;
extern int g_joystickActive;
extern int g_joyDeviceIndex;

void Joystick_PollScaledAxes(int deviceIndex, int *pAxisX, int *pAxisY,
			     int *pAxisZ, int *pButtons);
int16_t Joystick_InitializeBackendStub(void);
int Joystick_PollScaledAxesIfActive(int *pAxisX, int *pAxisY, int *pAxisZ,
				    int *pAxisR);

#ifdef __cplusplus
}
#endif

#endif
