#include "xvt/input/mouse.h"
#include "xvt/input/win_mouse.h"

/* Polls the mouse through WinMouse_PollPositionAndButtons: stores the scaled
 * position, cut to 16 bits, in *x and *y and returns the held buttons, 0x1
 * left, 0x2 right, 0x4 middle. Its two callers, FlightInput_Read and
 * XvtFlightControls_ReadLocal, call it only while g_flightMouseEnabled is
 * nonzero, and only FlightInput_ResetRuntimeState writes that, setting 0. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4A4E70
int Mouse_ReadPositionAndButtons(int16_t *x, int16_t *y)
{
	int16_t buttons;

	WinMouse_PollPositionAndButtons(&buttons, x, y);
	return buttons;
}

/* Polls the mouse through WinMouse_PollMovementDelta and stores the scaled
 * movement since the last poll, cut to 16 bits. Same two callers as
 * Mouse_ReadPositionAndButtons, under the same g_flightMouseEnabled test. */
// FUNCTION: XVT 0x4A4EA0
void Mouse_ReadDelta(int16_t *deltaX, int16_t *deltaY)
{
	WinMouse_PollMovementDelta(deltaX, deltaY);
}

/* Passes to WinMouse_SetPosition and returns its result. Nothing calls this. */
// FUNCTION: XVT 0x4AC8D0
int Mouse_SetPosition(int16_t x, int16_t y)
{
	return WinMouse_SetPosition(x, y);
}

/* Passes to WinMouse_SetHorizontalBounds. Nothing calls this. */
// FUNCTION: XVT 0x4AC9D0
void Mouse_SetHorizontalBounds(int16_t minX, int16_t maxX)
{
	WinMouse_SetHorizontalBounds(minX, maxX);
}

/* Passes to WinMouse_SetVerticalBounds. Nothing calls this. */
// FUNCTION: XVT 0x4AC9F0
void Mouse_SetVerticalBounds(int16_t minY, int16_t maxY)
{
	WinMouse_SetVerticalBounds(minY, maxY);
}

/* Passes to WinMouse_SetScaleFactors. Nothing calls this. */
// FUNCTION: XVT 0x4ACA70
void Mouse_SetScaleFactors(int16_t scaleX, int16_t scaleY)
{
	WinMouse_SetScaleFactors(scaleX, scaleY);
}
