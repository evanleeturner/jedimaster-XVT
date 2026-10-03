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

/* The joystick slot the centering prompt is waiting on, 0 to 2; 2 ends it.
 * Written only by FrontendJoystick_BeginCenteringPrompt, which sets it to 0 and
 * which nothing calls, and FrontendJoystick_UpdateCenteringPrompt. */
// GLOBAL: XVT 0x665430
int g_frontendJoystickCenteringSlot = 0;
/* The centering prompt's background color, the low byte of
 * FrontendDisplay_PackRGB(0, 0, 255). Written only by
 * FrontendJoystick_BeginCenteringPrompt, which nothing calls. */
// GLOBAL: XVT 0x665434
uint8_t g_frontendJoystickCenteringFillColor = 0;

/* Finds up to two joysticks through the system's joystick API and records them
 * in g_frontState, in slot order: for each device from 0 that reports its
 * capabilities and then its position, the button count, whether it has a
 * point-of-view hat, its axis ranges, its current x and y as the center, the
 * scales (center - min) / 255 and (max - center) / 255 for each axis, its
 * device id, and joystickPresent 1. First clears joystickPresent and sets both
 * joystickInitFlags to 1, which nothing reads, unless the system reports no
 * device at all. Returns 1 when it found one or two, else 0. The modern build
 * raises each scale under 1 to 1. */
// FUNCTION: XVT 0x4D5E70
int Joystick_InitDevices(void)
{
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
		if (joyGetDevCapsA((uint32_t)device, &joystickCaps,
				   sizeof(joystickCaps)) == JOYERR_NOERROR) {
			g_frontState.joystickButtonCount[slot] =
				(uint8_t)joystickCaps.wNumButtons;
			joystickInfo.dwSize = sizeof(joystickInfo);
			joystickInfo.dwFlags = JOY_RETURNX | JOY_RETURNY |
					       JOY_RETURNBUTTONS |
					       JOY_RETURNCENTERED;
			g_frontState.joystickHasPov[slot] =
				(joystickCaps.wCaps & JOYCAPS_HASPOV) != 0;
			if (joyGetPosEx((uint32_t)device, &joystickInfo) ==
			    JOYERR_NOERROR) {
				g_frontState.joystickXMin[slot] =
					joystickCaps.wXmin;
				g_frontState.joystickXMax[slot] =
					joystickCaps.wXmax;
				g_frontState.joystickYMin[slot] =
					joystickCaps.wYmin;
				g_frontState.joystickYMax[slot] =
					joystickCaps.wYmax;
				g_frontState.joystickXCenter[slot] =
					joystickInfo.dwXpos;
				g_frontState.joystickYCenter[slot] =
					joystickInfo.dwYpos;
				g_frontState.joystickXNegativeScale[slot] =
					(joystickInfo.dwXpos -
					 joystickCaps.wXmin) /
					255;
				g_frontState.joystickXPositiveScale[slot] =
					(joystickCaps.wXmax -
					 joystickInfo.dwXpos) /
					255;
				g_frontState.joystickYNegativeScale[slot] =
					(joystickInfo.dwYpos -
					 joystickCaps.wYmin) /
					255;
				g_frontState.joystickYPositiveScale[slot] =
					(joystickCaps.wYmax -
					 joystickInfo.dwYpos) /
					255;
#ifdef XVT_MODERN
				if (g_frontState.joystickXNegativeScale[slot] <
				    1) {
					g_frontState
						.joystickXNegativeScale[slot] =
						1;
				}
				if (g_frontState.joystickXPositiveScale[slot] <
				    1) {
					g_frontState
						.joystickXPositiveScale[slot] =
						1;
				}
				if (g_frontState.joystickYNegativeScale[slot] <
				    1) {
					g_frontState
						.joystickYNegativeScale[slot] =
						1;
				}
				if (g_frontState.joystickYPositiveScale[slot] <
				    1) {
					g_frontState
						.joystickYPositiveScale[slot] =
						1;
				}
#endif
				g_frontState.joyDeviceIds[slot] =
					(unsigned int)device;
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

/* Only the original build calls this. Returns how many of the two slots hold a
 * joystick, 0 to 2. */
// FUNCTION: XVT 0x4D5FC0
int Joystick_GetCount(void)
{
	int joystickCount = 0;
	if (g_frontState.joystickPresent[0] != 0) {
		joystickCount = 1;
	}
	if (g_frontState.joystickPresent[1] != 0) {
		++joystickCount;
	}
	return joystickCount;
}

/* Reads joystick slot joySlot's position, buttons and hat into g_frontState;
 * the frame loops call it for both slots every 100 ms. Does nothing for a slot
 * over 1 or without a joystick. Sets joystickAxisX and joystickAxisY to the
 * offset from the center divided by that side's scale, or 0 while the offset is
 * from -1000 to 1000. For each of 32 buttons sets joystickButtonHeld to 1 when
 * it is down and joystickButtonReleased to 1 when it is up now but was held at
 * the last read. With a hat it sets joystickPovDirection to 0 when centered,
 * else to dwPOV / 9000 + 1, dwPOV being hundredths of a degree clockwise from
 * forward. The modern build also refuses negative slots, and a failed read
 * clears the slot's buttons and axes and marks it absent; the original build
 * does not check the read. */
// FUNCTION: XVT 0x4D5FE0
void Joystick_UpdateState(int joySlot)
{
	JOYINFOEX joystickInfo;
	int axisDeltaX;
	int axisDeltaY;
	unsigned int buttonMask;
	int buttonIndex;

#ifdef XVT_MODERN
	if ((unsigned int)joySlot >= 2 ||
	    g_frontState.joystickPresent[joySlot] == 0) {
#else
	if (joySlot > 1 || g_frontState.joystickPresent[joySlot] == 0) {
#endif
		return;
	}
	joystickInfo.dwSize = sizeof(joystickInfo);
	joystickInfo.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNBUTTONS |
			       JOY_RETURNPOV | JOY_RETURNCENTERED;
#ifdef XVT_MODERN
	if (joyGetPosEx(g_frontState.joyDeviceIds[joySlot], &joystickInfo) !=
	    JOYERR_NOERROR) {
		memset(g_frontState.joystickButtonHeld[joySlot], 0,
		       sizeof(g_frontState.joystickButtonHeld[joySlot]));
		memset(g_frontState.joystickButtonReleased[joySlot], 0,
		       sizeof(g_frontState.joystickButtonReleased[joySlot]));
		g_frontState.joystickAxisX[joySlot] =
			g_frontState.joystickAxisY[joySlot] = 0;
		g_frontState.joystickPresent[joySlot] = 0;
		return;
	}
#else
	joyGetPosEx(g_frontState.joyDeviceIds[joySlot], &joystickInfo);
#endif

	axisDeltaX = (int)joystickInfo.dwXpos -
		     g_frontState.joystickXCenter[joySlot];
	axisDeltaY = (int)joystickInfo.dwYpos -
		     g_frontState.joystickYCenter[joySlot];
	if (axisDeltaX > 1000 || axisDeltaX < -1000) {
		if (axisDeltaX < 0) {
			g_frontState.joystickAxisX[joySlot] =
				axisDeltaX /
				g_frontState.joystickXNegativeScale[joySlot];
		} else {
			g_frontState.joystickAxisX[joySlot] =
				axisDeltaX /
				g_frontState.joystickXPositiveScale[joySlot];
		}
	} else {
		g_frontState.joystickAxisX[joySlot] = 0;
	}
	if (axisDeltaY > 1000 || axisDeltaY < -1000) {
		if (axisDeltaY < 0) {
			g_frontState.joystickAxisY[joySlot] =
				axisDeltaY /
				g_frontState.joystickYNegativeScale[joySlot];
		} else {
			g_frontState.joystickAxisY[joySlot] =
				axisDeltaY /
				g_frontState.joystickYPositiveScale[joySlot];
		}
	} else {
		g_frontState.joystickAxisY[joySlot] = 0;
	}

	buttonMask = 1;
	for (buttonIndex = 0; buttonIndex < 32; ++buttonIndex) {
		g_frontState.joystickButtonReleased[joySlot][buttonIndex] =
			(joystickInfo.dwButtons & buttonMask) == 0 &&
			g_frontState.joystickButtonHeld[joySlot][buttonIndex] ==
				1;
		g_frontState.joystickButtonHeld[joySlot][buttonIndex] =
			(joystickInfo.dwButtons & buttonMask) != 0;
		buttonMask <<= 1;
	}

	if (g_frontState.joystickHasPov[joySlot] != 0) {
		if (joystickInfo.dwPOV == JOY_POVCENTERED) {
			g_frontState.joystickPovDirection[joySlot] = 0;
		} else {
			g_frontState.joystickPovDirection[joySlot] =
				(uint8_t)(joystickInfo.dwPOV / 0x2328u) + 1;
		}
	}
}

/* Only FrontendJoystick_UpdateCenteringPrompt calls this, and nothing reaches
 * that. Returns 1 when button 0 of the slot's joystick came up at the last read
 * and no frame has cleared the flag since; 0 for a slot over 1 or without a
 * joystick. */
// FUNCTION: XVT 0x4D61E0
int Joystick_IsButton0Released(int joystickSlot)
{
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

/* Only FrontendJoystick_UpdateCenteringPrompt calls this, and nothing reaches
 * that. Returns 1 when button 1 of the slot's joystick came up at the last read
 * and no frame has cleared the flag since; 0 for a slot over 1 or without a
 * joystick. */
// FUNCTION: XVT 0x4D6210
int Joystick_IsButton1Released(int joystickSlot)
{
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

/* Returns the lowest-numbered button, 0 to 31, held at the slot's last read, or
 * -1 when none is, the slot is over 1 or it has no joystick. */
// FUNCTION: XVT 0x4D6240
int Joystick_GetFirstPressedButton(int joySlot)
{
	int buttonIndex;

#ifdef XVT_MODERN
	if ((unsigned int)joySlot >= 2)
#else
	if (joySlot > 1)
#endif
		return -1;
	if (g_frontState.joystickPresent[joySlot] == 0) {
		return -1;
	}

	for (buttonIndex = 0; buttonIndex < 32; ++buttonIndex) {
		if (g_frontState.joystickButtonHeld[joySlot][buttonIndex] !=
		    0) {
			return buttonIndex;
		}
	}
	return -1;
}

/* Nothing calls this. Returns the lowest-numbered button, 0 to 31, whose
 * released flag is set, or -1 when none is, the slot is over 1 or it has no
 * joystick. */
// FUNCTION: XVT 0x4D6280
int Joystick_GetFirstReleasedButton(int joystickSlot)
{
	int buttonIndex;

#ifdef XVT_MODERN
	if ((unsigned int)joystickSlot >= 2)
#else
	if (joystickSlot > 1)
#endif
		return -1;
	if (g_frontState.joystickPresent[joystickSlot] == 0) {
		return -1;
	}

	for (buttonIndex = 0; buttonIndex < 32; ++buttonIndex) {
		if (g_frontState.joystickButtonReleased[joystickSlot]
						       [buttonIndex] != 0) {
			return buttonIndex;
		}
	}
	return -1;
}

/* Returns the slot's joystickPovDirection, 0 when the hat is centered or
 * absent, else dwPOV / 9000 + 1 from the last read; 0 for a slot over 1. Does
 * not check that a joystick is present. */
// FUNCTION: XVT 0x4D62C0
int Joystick_GetPovDirection(int joySlot)
{
#ifdef XVT_MODERN
	if ((unsigned int)joySlot >= 2)
#else
	if (joySlot > 1)
#endif
		return 0;
	return g_frontState.joystickPovDirection[joySlot];
}

/* Returns 1 when the slot's joystick reported a point-of-view hat, else 0; 0
 * for a slot over 1. */
// FUNCTION: XVT 0x4D62E0
int Joystick_HasPov(int joySlot)
{
#ifdef XVT_MODERN
	if ((unsigned int)joySlot >= 2)
#else
	if (joySlot > 1)
#endif
		return 0;
	return g_frontState.joystickHasPov[joySlot];
}

/* Returns the button count the slot's joystick reported, or 0 for a slot over
 * 1. */
// FUNCTION: XVT 0x4D6300
int Joystick_GetButtonCount(int joySlot)
{
#ifdef XVT_MODERN
	if ((unsigned int)joySlot >= 2)
#else
	if (joySlot > 1)
#endif
		return 0;
	return g_frontState.joystickButtonCount[joySlot];
}

/* Nothing calls this. Sets g_frontendJoystickCenteringSlot to 0 and
 * g_frontendJoystickCenteringFillColor to the low byte of
 * FrontendDisplay_PackRGB(0, 0, 255), then queues
 * FrontendJoystick_UpdateCenteringPrompt as a screen over the rect from (120,
 * 190) to (520, 290) and returns FrontendScreen_QueuePush's 1. */
// FUNCTION: XVT 0x4D6320
int FrontendJoystick_BeginCenteringPrompt(void)
{
	struct RECT screenRect;
	int fillColor;

	screenRect.left = 120;
	screenRect.right = 520;
	screenRect.top = 190;
	screenRect.bottom = 290;
	fillColor = FrontendDisplay_PackRGB(0, 0, 255);
	g_frontendJoystickCenteringFillColor = (uint8_t)fillColor;
	g_frontendJoystickCenteringSlot = 0;
	return FrontendScreen_QueuePush(FrontendJoystick_UpdateCenteringPrompt,
					&screenRect);
}

/* The centering prompt's frame function, reached only through
 * FrontendJoystick_BeginCenteringPrompt, which nothing calls. For each slot
 * with a joystick in turn it fills the screen's rect in
 * g_frontendJoystickCenteringFillColor, draws "Center joystick <n> and press a
 * button." centered in the size-20 font in color 255, and waits for button 0 or
 * 1 to come up; empty slots are skipped. After slot 1 it flushes the typed
 * characters and pops the screen. It records no center: the centers stay what
 * Joystick_InitDevices read. Returns 0. Ignores frameCounter. */
// FUNCTION: XVT 0x4D6380
int FrontendJoystick_UpdateCenteringPrompt(int frameCounter)
{
	int joystickSlot;
	struct RECT *screenRect;
	char promptText[100];

	(void)frameCounter;
	joystickSlot = g_frontendJoystickCenteringSlot;
	if (g_frontState.joystickPresent[g_frontendJoystickCenteringSlot] ==
	    0) {
		++joystickSlot;
	} else {
		screenRect =
			&g_frontState
				 .screenStates[g_frontState.screenStackTop - 1]
				 .savedRect;
		FrontendDraw_Rect(screenRect, 0, 0,
				  g_frontendJoystickCenteringFillColor, 1);
		sprintf(promptText, "Center joystick %d and press a button.",
			g_frontendJoystickCenteringSlot + 1);
		FrontendText_DrawCentered(20, promptText, screenRect, 255);
		if (Joystick_IsButton0Released(
			    g_frontendJoystickCenteringSlot) != 0 ||
		    Joystick_IsButton1Released(
			    g_frontendJoystickCenteringSlot) != 0) {
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

/* Returns the system device id of the joystick in slot joySlot, or 0 for a slot
 * out of range: over 1 in the modern build, over 2 in the original build, which
 * for slot 2 reads joyDeviceIds[2], past the array's end. Does not check that a
 * joystick is present. */
// FUNCTION: XVT 0x4D6440
unsigned int Joystick_GetDeviceId(int joySlot)
{
#ifdef XVT_MODERN
	if ((unsigned int)joySlot >= 2)
#else
	if (joySlot > 2)
#endif
		return 0;
	return g_frontState.joyDeviceIds[joySlot];
}
