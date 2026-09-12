#include "xvt/frontend/frontend_joystick.h"
#include "aeron/compat/mmsystem.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/input/keyboard.h"
#include <stdio.h>
#include <string.h>

// GLOBAL: XVT 0x665430
int g_frontendJoystickCenteringSlot = 0;
// GLOBAL: XVT 0x665434
uint8_t g_frontendJoystickCenteringFillColor = 0;

// FUNCTION: XVT 0x4D5E70
int Joystick_InitDevices(void) {
	JOYINFOEX joystickInfo;
	JOYCAPSA joystickCaps;
	int deviceCount;
	int device;
	int slot;
	int initializedCount;

	deviceCount = (int)joyGetNumDevs();
	if (deviceCount == 0) {
		return 0;
	}
	slot = 0;
	g_frontState.joystickPresent[1] = 0;
	g_frontState.joystickPresent[0] = 0;
	initializedCount = 0;
	g_frontState.joystickInitFlags[1] = 1;
	g_frontState.joystickInitFlags[0] = 1;
	for (device = 0; device < deviceCount; ++device) {
		if (joyGetDevCapsA((uint32_t)device, &joystickCaps, sizeof(joystickCaps)) == JOYERR_NOERROR) {
			g_frontState.joystickButtonCount[slot] = (uint8_t)joystickCaps.wNumButtons;
			joystickInfo.dwSize = sizeof(joystickInfo);
			joystickInfo.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNBUTTONS | JOY_RETURNCENTERED;
			g_frontState.joystickHasPov[slot] = (joystickCaps.wCaps & JOYCAPS_HASPOV) != 0;
			if (joyGetPosEx((uint32_t)device, &joystickInfo) == JOYERR_NOERROR) {
				g_frontState.joystickXMin[slot] = joystickCaps.wXmin;
				g_frontState.joystickXMax[slot] = joystickCaps.wXmax;
				g_frontState.joystickYMin[slot] = joystickCaps.wYmin;
				g_frontState.joystickYMax[slot] = joystickCaps.wYmax;
				g_frontState.joystickXCenter[slot] = joystickInfo.dwXpos;
				g_frontState.joystickYCenter[slot] = joystickInfo.dwYpos;
				g_frontState.joystickXNegativeScale[slot] = (joystickInfo.dwXpos - joystickCaps.wXmin) / 255;
				g_frontState.joystickXPositiveScale[slot] = (joystickCaps.wXmax - joystickInfo.dwXpos) / 255;
				g_frontState.joystickYNegativeScale[slot] = (joystickInfo.dwYpos - joystickCaps.wYmin) / 255;
				g_frontState.joystickYPositiveScale[slot] = (joystickCaps.wYmax - joystickInfo.dwYpos) / 255;
#ifdef XVT_MODERN
				if (g_frontState.joystickXNegativeScale[slot] < 1)
					g_frontState.joystickXNegativeScale[slot] = 1;
				if (g_frontState.joystickXPositiveScale[slot] < 1)
					g_frontState.joystickXPositiveScale[slot] = 1;
				if (g_frontState.joystickYNegativeScale[slot] < 1)
					g_frontState.joystickYNegativeScale[slot] = 1;
				if (g_frontState.joystickYPositiveScale[slot] < 1)
					g_frontState.joystickYPositiveScale[slot] = 1;
#endif
				g_frontState.joyDeviceIds[slot] = (unsigned int)device;
				g_frontState.joystickPresent[slot] = 1;
				++initializedCount;
				++slot;
				if (slot >= 2) {
					break;
				}
			}
		}
	}
	return initializedCount != 0;
}

// FUNCTION: XVT 0x4D5FC0
int Joystick_GetCount(void) {
	int joystickCount = 0;
	if (g_frontState.joystickPresent[0] != 0)
		joystickCount = 1;
	if (g_frontState.joystickPresent[1] != 0)
		++joystickCount;
	return joystickCount;
}

// FUNCTION: XVT 0x4D5FE0
void Joystick_UpdateState(int joySlot) {
	JOYINFOEX joystickInfo;
	int axisDeltaX;
	int axisDeltaY;
	unsigned int buttonMask;
	int buttonIndex;

#ifdef XVT_MODERN
	if ((unsigned int)joySlot >= 2 || g_frontState.joystickPresent[joySlot] == 0) {
#else
	if (joySlot > 1 || g_frontState.joystickPresent[joySlot] == 0) {
#endif
		return;
	}
	joystickInfo.dwSize = sizeof(joystickInfo);
	joystickInfo.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNBUTTONS | JOY_RETURNPOV | JOY_RETURNCENTERED;
#ifdef XVT_MODERN
	if (joyGetPosEx(g_frontState.joyDeviceIds[joySlot], &joystickInfo) != JOYERR_NOERROR) {
		memset(g_frontState.joystickButtonHeld[joySlot], 0, sizeof(g_frontState.joystickButtonHeld[joySlot]));
		memset(g_frontState.joystickButtonReleased[joySlot], 0,
			   sizeof(g_frontState.joystickButtonReleased[joySlot]));
		g_frontState.joystickAxisX[joySlot] = g_frontState.joystickAxisY[joySlot] = 0;
		g_frontState.joystickPresent[joySlot] = 0;
		return;
	}
#else
	joyGetPosEx(g_frontState.joyDeviceIds[joySlot], &joystickInfo);
#endif

	axisDeltaX = (int)joystickInfo.dwXpos - g_frontState.joystickXCenter[joySlot];
	axisDeltaY = (int)joystickInfo.dwYpos - g_frontState.joystickYCenter[joySlot];
	if (axisDeltaX > 1000 || axisDeltaX < -1000) {
		if (axisDeltaX < 0) {
			g_frontState.joystickAxisX[joySlot] = axisDeltaX / g_frontState.joystickXNegativeScale[joySlot];
		} else {
			g_frontState.joystickAxisX[joySlot] = axisDeltaX / g_frontState.joystickXPositiveScale[joySlot];
		}
	} else {
		g_frontState.joystickAxisX[joySlot] = 0;
	}
	if (axisDeltaY > 1000 || axisDeltaY < -1000) {
		if (axisDeltaY < 0) {
			g_frontState.joystickAxisY[joySlot] = axisDeltaY / g_frontState.joystickYNegativeScale[joySlot];
		} else {
			g_frontState.joystickAxisY[joySlot] = axisDeltaY / g_frontState.joystickYPositiveScale[joySlot];
		}
	} else {
		g_frontState.joystickAxisY[joySlot] = 0;
	}

	buttonMask = 1;
	for (buttonIndex = 0; buttonIndex < 32; ++buttonIndex) {
		g_frontState.joystickButtonReleased[joySlot][buttonIndex] =
			(joystickInfo.dwButtons & buttonMask) == 0 &&
			g_frontState.joystickButtonHeld[joySlot][buttonIndex] == 1;
		g_frontState.joystickButtonHeld[joySlot][buttonIndex] = (joystickInfo.dwButtons & buttonMask) != 0;
		buttonMask <<= 1;
	}

	if (g_frontState.joystickHasPov[joySlot] != 0) {
		if (joystickInfo.dwPOV == JOY_POVCENTERED) {
			g_frontState.joystickPovDirection[joySlot] = 0;
		} else {
			g_frontState.joystickPovDirection[joySlot] = (uint8_t)(joystickInfo.dwPOV / 0x2328u) + 1;
		}
	}
}

// FUNCTION: XVT 0x4D61E0
int Joystick_IsButton0Released(int joystickSlot) {
#ifdef XVT_MODERN
	if ((unsigned int)joystickSlot >= 2) {
#else
	if (joystickSlot > 1) {
#endif
		return 0;
	}
	if (g_frontState.joystickPresent[joystickSlot] == 0) {
		return 0;
	}
	return g_frontState.joystickButtonReleased[joystickSlot][0];
}

// FUNCTION: XVT 0x4D6210
int Joystick_IsButton1Released(int joystickSlot) {
#ifdef XVT_MODERN
	if ((unsigned int)joystickSlot >= 2) {
#else
	if (joystickSlot > 1) {
#endif
		return 0;
	}
	if (g_frontState.joystickPresent[joystickSlot] == 0) {
		return 0;
	}
	return g_frontState.joystickButtonReleased[joystickSlot][1];
}

// FUNCTION: XVT 0x4D6240
int Joystick_GetFirstPressedButton(int joySlot) {
	int buttonIndex;

#ifdef XVT_MODERN
	if ((unsigned int)joySlot >= 2)
#else
	if (joySlot > 1)
#endif
		return -1;
	if (g_frontState.joystickPresent[joySlot] == 0)
		return -1;

	for (buttonIndex = 0; buttonIndex < 32; ++buttonIndex) {
		if (g_frontState.joystickButtonHeld[joySlot][buttonIndex] != 0)
			return buttonIndex;
	}
	return -1;
}

// FUNCTION: XVT 0x4D6280
int Joystick_GetFirstReleasedButton(int joystickSlot) {
	int buttonIndex;

#ifdef XVT_MODERN
	if ((unsigned int)joystickSlot >= 2)
#else
	if (joystickSlot > 1)
#endif
		return -1;
	if (g_frontState.joystickPresent[joystickSlot] == 0)
		return -1;

	for (buttonIndex = 0; buttonIndex < 32; ++buttonIndex) {
		if (g_frontState.joystickButtonReleased[joystickSlot][buttonIndex] != 0)
			return buttonIndex;
	}
	return -1;
}

// FUNCTION: XVT 0x4D62C0
int Joystick_GetPovDirection(int joySlot) {
#ifdef XVT_MODERN
	if ((unsigned int)joySlot >= 2)
#else
	if (joySlot > 1)
#endif
		return 0;
	return g_frontState.joystickPovDirection[joySlot];
}

// FUNCTION: XVT 0x4D62E0
int Joystick_HasPov(int joySlot) {
#ifdef XVT_MODERN
	if ((unsigned int)joySlot >= 2)
#else
	if (joySlot > 1)
#endif
		return 0;
	return g_frontState.joystickHasPov[joySlot];
}

// FUNCTION: XVT 0x4D6300
int Joystick_GetButtonCount(int joySlot) {
#ifdef XVT_MODERN
	if ((unsigned int)joySlot >= 2)
#else
	if (joySlot > 1)
#endif
		return 0;
	return g_frontState.joystickButtonCount[joySlot];
}

// FUNCTION: XVT 0x4D6320
int FrontendJoystick_BeginCenteringPrompt(void) {
	RECT screenRect;
	int fillColor;

	screenRect.left = 120;
	screenRect.right = 520;
	screenRect.top = 190;
	screenRect.bottom = 290;
	fillColor = FrontendDisplay_PackRGB(0, 0, 255);
	g_frontendJoystickCenteringFillColor = (uint8_t)fillColor;
	g_frontendJoystickCenteringSlot = 0;
	return FrontendScreen_QueuePush(FrontendJoystick_UpdateCenteringPrompt, &screenRect);
}

// FUNCTION: XVT 0x4D6380
int FrontendJoystick_UpdateCenteringPrompt(int inputCode) {
	int joystickSlot;
	RECT* screenRect;
	char promptText[100];

	(void)inputCode;
	joystickSlot = g_frontendJoystickCenteringSlot;
	if (g_frontState.joystickPresent[g_frontendJoystickCenteringSlot] == 0) {
		++joystickSlot;
	} else {
		screenRect = &g_frontState.screenStates[g_frontState.screenStackTop - 1].savedRect;
		FrontendDraw_Rect(screenRect, 0, 0, g_frontendJoystickCenteringFillColor, 1);
		sprintf(promptText, "Center joystick %d and press a button.", g_frontendJoystickCenteringSlot + 1);
		FrontendText_DrawCentered(20, promptText, screenRect, 255);
		if (Joystick_IsButton0Released(g_frontendJoystickCenteringSlot) != 0 ||
			Joystick_IsButton1Released(g_frontendJoystickCenteringSlot) != 0) {
			joystickSlot = g_frontendJoystickCenteringSlot + 1;
		} else {
			joystickSlot = g_frontendJoystickCenteringSlot;
		}
	}
	g_frontendJoystickCenteringSlot = joystickSlot;
	if (joystickSlot >= 2) {
		Keyboard_FlushCharBuffer();
		FrontendScreen_PopState();
	}
	return 0;
}

// FUNCTION: XVT 0x4D6440
unsigned int Joystick_GetDeviceId(int joySlot) {
#ifdef XVT_MODERN
	if ((unsigned int)joySlot >= 2)
#else
	if (joySlot > 2)
#endif
		return 0;
	return g_frontState.joyDeviceIds[joySlot];
}
