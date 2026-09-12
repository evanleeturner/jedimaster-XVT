#ifndef XVT_FRONTEND_FRONTEND_JOYSTICK_H
#define XVT_FRONTEND_FRONTEND_JOYSTICK_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct JoystickEntry {
	uint8_t actionCode;
	char name[20];
	char description[128];
};

extern JoystickEntry g_joystickEntries[128];
extern int g_joystickEntryCount;

extern int g_frontendJoystickCenteringSlot;
extern uint8_t g_frontendJoystickCenteringFillColor;

int Joystick_InitDevices(void);
int Joystick_GetCount(void);
void Joystick_UpdateState(int joySlot);
int Joystick_IsButton0Released(int joystickSlot);
int Joystick_IsButton1Released(int joystickSlot);
int Joystick_GetFirstPressedButton(int joySlot);
int Joystick_GetFirstReleasedButton(int joystickSlot);
int Joystick_GetPovDirection(int joySlot);
int Joystick_HasPov(int joySlot);
int Joystick_GetButtonCount(int joySlot);
int FrontendJoystick_BeginCenteringPrompt(void);
int FrontendJoystick_UpdateCenteringPrompt(int inputCode);
unsigned int Joystick_GetDeviceId(int joySlot);

#ifdef __cplusplus
}
#endif

#endif
