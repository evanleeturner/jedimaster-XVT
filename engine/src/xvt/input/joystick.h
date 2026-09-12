#ifndef XVT_INPUT_JOYSTICK_H
#define XVT_INPUT_JOYSTICK_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct JoystickCalibration {
	int axisRangeX;
	int axisRangeY;
	int axisRangeZ;
	int axisNormalizeOffsetX;
	int axisNormalizeOffsetY;
	int axisNormalizeOffsetZ;
	int axisDeadzoneX;
	int axisDeadzoneY;
	int axisDeadzoneZ;
	int hasPov;
};

extern int g_joystickCalibrationInitialized[2];
extern JoystickCalibration g_joystickCalibration;
extern int g_joyAxisCenterZ;
extern unsigned int g_joyDeviceId[2];
extern int g_joyAxisCenterX;
extern int g_joyAxisCenterY;
extern int g_joystickActive;
extern int g_joyDeviceIndex;

void Joystick_PollRawAxes(int deviceIndex, int* pAxisX, int* pAxisY, int* pAxisZ, int* pButtons);
int16_t Joystick_InitializeBackendStub(void);
int Joystick_PollRawAxesIfEnabled(int* pAxisX, int* pAxisY, int* pAxisZ, int* pAxisR);

#ifdef __cplusplus
}
#endif

#endif
