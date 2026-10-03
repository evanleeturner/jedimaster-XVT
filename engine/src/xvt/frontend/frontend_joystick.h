#ifndef XVT_FRONTEND_FRONTEND_JOYSTICK_H
#define XVT_FRONTEND_FRONTEND_JOYSTICK_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct JoystickEntry {
	/* The action's code, the first field of its line in joystick.txt read
	 * with atoi; compared with the codes in g_gameConfig.joyButtons. */
	uint8_t actionCode;
	/* The action's short name, the line's second space-separated field,
	 * copied without a length check. */
	char name[20];
	/* The rest of the line after the name and the space that ends it, the
	 * action's description; copied without a length check. */
	char description[128];
};

extern struct JoystickEntry g_joystickEntries[128];
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
int FrontendJoystick_UpdateCenteringPrompt(int frameCounter);
unsigned int Joystick_GetDeviceId(int joySlot);

#ifdef __cplusplus
}
#endif

#endif
