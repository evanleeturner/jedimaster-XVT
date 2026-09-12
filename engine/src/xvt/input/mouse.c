#include "xvt/input/mouse.h"
#include "xvt/input/win_mouse.h"

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4A4E70
int Mouse_ReadPositionAndButtons(int16_t* x, int16_t* y) {
	int16_t buttons;

	WinMouse_GetPositionAndButtons(&buttons, x, y);
	return buttons;
}

// FUNCTION: XVT 0x4A4EA0
void Mouse_ReadDelta(int16_t* deltaX, int16_t* deltaY) { WinMouse_GetMovementDelta(deltaX, deltaY); }

// FUNCTION: XVT 0x4AC8D0
int Mouse_SetPosition(int16_t x, int16_t y) { return WinMouse_SetPosition(x, y); }

// FUNCTION: XVT 0x4AC9D0
void Mouse_SetHorizontalBounds(int16_t minX, int16_t maxX) { WinMouse_SetHorizontalBounds(minX, maxX); }

// FUNCTION: XVT 0x4AC9F0
void Mouse_SetVerticalBounds(int16_t minY, int16_t maxY) { WinMouse_SetVerticalBounds(minY, maxY); }

// FUNCTION: XVT 0x4ACA70
void Mouse_SetScaleFactors(int16_t scaleX, int16_t scaleY) { WinMouse_SetScaleFactors(scaleX, scaleY); }
