#include "xvt/input/win_mouse.h"

#include "xvt/flight/flight_input.h"
#include "xvt/util/win32.h"

#ifdef XVT_MODERN
#include "aeron/aeron.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/runtime/presentation.h"
#else
struct WinMouseWin32Message {
	/* Target window, as MSG.hwnd; nothing reads it by name. */
	void *window;
	uint32_t message; /* Message number; nothing reads it by name. */
	/* First message parameter; nothing reads it by name. */
	uint32_t wParam;
	/* Second message parameter; nothing reads it by name. */
	int32_t lParam;
	/* Time the message was posted; nothing reads it by name. */
	uint32_t time;
	/* Cursor X when it was posted; nothing reads it by name. */
	int32_t pointX;
	/* Cursor Y when it was posted; nothing reads it by name. */
	int32_t pointY;
};

__declspec(dllimport) int __stdcall
PeekMessageA(struct WinMouseWin32Message *message, void *hWnd,
	     unsigned int filterMin, unsigned int filterMax,
	     unsigned int removeMessage);
__declspec(dllimport) int __stdcall
GetMessageA(struct WinMouseWin32Message *message, void *hWnd,
	    unsigned int filterMin, unsigned int filterMax);
__declspec(dllimport) int __stdcall
TranslateMessage(const struct WinMouseWin32Message *message);
__declspec(dllimport) int32_t __stdcall
DispatchMessageA(const struct WinMouseWin32Message *message);
__declspec(dllimport) int __stdcall GetCursorPos(POINT *point);
__declspec(dllimport) int __stdcall SetCursorPos(int x, int y);
#endif

/* Cursor point the next WinMouse_PollState measures movement from. Each poll
 * sets it to g_winMouseCenterPos after warping the cursor there, and
 * WinMouse_SetPosition sets it; in the modern build a poll also sets it to the
 * polled point while XvtInput_MouseMotionAllowed is false, so that poll finds
 * no movement. */
// GLOBAL: XVT 0x527ED8
POINT g_winMousePrevPos;
/* Cursor point WinMouse_PollState last read: from GetCursorPos in the original
 * build; in the modern one from XvtPresentation_MouseToClassic, or the old
 * point kept when that call returns 0. WinMouse_SetPosition also sets it. */
// GLOBAL: XVT 0x527EE0
POINT g_winMouseCursorPos;
/* The game's mouse position: each poll adds the movement and clamps it to
 * g_winMouseMinX to g_winMouseMaxX and g_winMouseMinY to g_winMouseMaxY;
 * WinMouse_SetPosition sets it. Polls return it times g_winMouseScaleX and
 * g_winMouseScaleY. */
// GLOBAL: XVT 0x527EE8
POINT g_winMousePos;
/* Left, right and middle button held, 1 or 0. Only the modern build's
 * WinMouse_PollState writes it, from Aeron's input snapshot; in the original
 * build nothing in the engine writes it, so it stays 0. */
// GLOBAL: XVT 0x527EF0
int g_winMouseButtonDown[3] = {0, 0, 0};
/* Left, right and middle button pressed since the last poll, 1 or 0. The modern
 * build's WinMouse_PollState sets it from Aeron's input snapshot; every poll
 * clears it after copying it out. In the original build nothing else writes
 * it. */
// GLOBAL: XVT 0x527EFC
int g_winMouseButtonPressed[3] = {0, 0, 0};
/* Left, right and middle button released since the last poll, written like
 * g_winMouseButtonPressed. */
// GLOBAL: XVT 0x527F08
int g_winMouseButtonReleased[3] = {0, 0, 0};
/* Lowest X g_winMousePos may take. Only WinMouse_SetHorizontalBounds writes it,
 * and only the uncalled Mouse_SetHorizontalBounds calls that, so it stays 0. */
// GLOBAL: XVT 0x527F14
int g_winMouseMinX;
/* Highest X g_winMousePos may take; written like g_winMouseMinX, so it stays
 * 0. */
// GLOBAL: XVT 0x527F18
int g_winMouseMaxX;
/* Lowest Y g_winMousePos may take. Only WinMouse_SetVerticalBounds writes it,
 * and only the uncalled Mouse_SetVerticalBounds calls that, so it stays 0. */
// GLOBAL: XVT 0x527F1C
int g_winMouseMinY;
/* Highest Y g_winMousePos may take; written like g_winMouseMinY, so it stays
 * 0. */
// GLOBAL: XVT 0x527F20
int g_winMouseMaxY;
/* Factor each poll multiplies the X position and movement by. Only
 * WinMouse_SetScaleFactors writes it, and only the uncalled
 * Mouse_SetScaleFactors calls that, so it stays 0 and every X a poll returns is
 * 0. */
// GLOBAL: XVT 0x527F24
int g_winMouseScaleX;
/* Factor for the Y position and movement, written like g_winMouseScaleX, so it
 * stays 0. */
// GLOBAL: XVT 0x527F28
int g_winMouseScaleY;
/* Point each poll warps the cursor to: the middle of the bounds on each axis,
 * (min + max) / 2. Only the two bounds setters write it, so it stays 0, 0. */
// GLOBAL: XVT 0x527F2C
POINT g_winMouseCenterPos;

/* Reads the mouse once and puts the cursor back at the center. The original
 * build first handles one window message, waiting for it in GetMessageA unless
 * g_flightInputNonBlockingMsgPump is set and PeekMessageA finds none, and
 * returns writing nothing when GetMessageA returns 0; then it reads the cursor
 * with GetCursorPos. The modern build reads Aeron's input snapshot: while input
 * is captured, the window lacks focus or there is no snapshot, it returns
 * g_winMousePos times the scale factors, no movement and no buttons, and writes
 * no global; otherwise it maps the cursor with XvtPresentation_MouseToClassic
 * and sets the three button arrays from the filtered buttons. Both then store
 * the point in g_winMouseCursorPos, return its offset from g_winMousePrevPos as
 * the movement, add that to g_winMousePos and clamp it to the bounds, move the
 * cursor to g_winMouseCenterPos (SetCursorPos, or XvtPresentation_WarpClassic
 * in the modern build) and set g_winMousePrevPos there, copy the button arrays
 * out and clear the pressed and released ones. Position and movement come back
 * multiplied by g_winMouseScaleX and g_winMouseScaleY. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AA910
void WinMouse_PollState(int *positionX, int *positionY, int *deltaX,
			int *deltaY, int *buttonDown, int *buttonPressed,
			int *buttonReleased)
{
	POINT point;

#ifdef XVT_MODERN
	const AeronInputSnapshot *input;

	input = Aeron_InputSnapshot();
	if (XvtInput_IsCaptured() || !input || !input->has_focus) {
		*positionX = g_winMousePos.x * g_winMouseScaleX;
		*positionY = g_winMousePos.y * g_winMouseScaleY;
		*deltaX = *deltaY = 0;
		buttonDown[0] = buttonDown[1] = buttonDown[2] = 0;
		buttonPressed[0] = buttonPressed[1] = buttonPressed[2] = 0;
		buttonReleased[0] = buttonReleased[1] = buttonReleased[2] = 0;
		return;
	}
	if (!XvtPresentation_MouseToClassic(input, &point.x, &point.y)) {
		point = g_winMouseCursorPos;
	}
	g_winMouseButtonDown[0] =
		(XvtInput_FilterMouseButtons(input->mouse.buttons) &
		 AERON_MOUSE_BUTTON_LEFT) != 0;
	g_winMouseButtonDown[1] =
		(XvtInput_FilterMouseButtons(input->mouse.buttons) &
		 AERON_MOUSE_BUTTON_RIGHT) != 0;
	g_winMouseButtonDown[2] =
		(XvtInput_FilterMouseButtons(input->mouse.buttons) &
		 AERON_MOUSE_BUTTON_MIDDLE) != 0;
	g_winMouseButtonPressed[0] =
		(XvtInput_FilterMouseButtons(input->mouse.pressed_buttons) &
		 AERON_MOUSE_BUTTON_LEFT) != 0;
	g_winMouseButtonPressed[1] =
		(XvtInput_FilterMouseButtons(input->mouse.pressed_buttons) &
		 AERON_MOUSE_BUTTON_RIGHT) != 0;
	g_winMouseButtonPressed[2] =
		(XvtInput_FilterMouseButtons(input->mouse.pressed_buttons) &
		 AERON_MOUSE_BUTTON_MIDDLE) != 0;
	g_winMouseButtonReleased[0] =
		(XvtInput_FilterMouseButtons(input->mouse.released_buttons) &
		 AERON_MOUSE_BUTTON_LEFT) != 0;
	g_winMouseButtonReleased[1] =
		(XvtInput_FilterMouseButtons(input->mouse.released_buttons) &
		 AERON_MOUSE_BUTTON_RIGHT) != 0;
	g_winMouseButtonReleased[2] =
		(XvtInput_FilterMouseButtons(input->mouse.released_buttons) &
		 AERON_MOUSE_BUTTON_MIDDLE) != 0;
#else
	struct WinMouseWin32Message message;

	if (g_flightInputNonBlockingMsgPump == 0 ||
	    PeekMessageA(&message, 0, 0, 0, 0) != 0) {
		if (GetMessageA(&message, 0, 0, 0) == 0) {
			return;
		}
		TranslateMessage(&message);
		DispatchMessageA(&message);
	}
	GetCursorPos(&point);
#endif

	g_winMouseCursorPos = point;
#ifdef XVT_MODERN
	if (!XvtInput_MouseMotionAllowed()) {
		g_winMousePrevPos = point;
	}
#endif
	*deltaX = point.x - g_winMousePrevPos.x;
	*deltaY = g_winMouseCursorPos.y - g_winMousePrevPos.y;
	g_winMousePos.x += *deltaX;
	g_winMousePos.y += *deltaY;
	if (g_winMousePos.x < g_winMouseMinX) {
		g_winMousePos.x = g_winMouseMinX;
	}
	if (g_winMousePos.x > g_winMouseMaxX) {
		g_winMousePos.x = g_winMouseMaxX;
	}
	if (g_winMousePos.y < g_winMouseMinY) {
		g_winMousePos.y = g_winMouseMinY;
	}
	if (g_winMousePos.y > g_winMouseMaxY) {
		g_winMousePos.y = g_winMouseMaxY;
	}
	*positionX = g_winMousePos.x;
	*positionY = g_winMousePos.y;

#ifdef XVT_MODERN
	XvtPresentation_WarpClassic(g_winMouseCenterPos.x,
				    g_winMouseCenterPos.y);
#else
	SetCursorPos(g_winMouseCenterPos.x, g_winMouseCenterPos.y);
#endif
	g_winMousePrevPos = g_winMouseCenterPos;
	buttonDown[0] = g_winMouseButtonDown[0];
	buttonDown[1] = g_winMouseButtonDown[1];
	buttonDown[2] = g_winMouseButtonDown[2];
	buttonPressed[0] = g_winMouseButtonPressed[0];
	buttonPressed[1] = g_winMouseButtonPressed[1];
	buttonPressed[2] = g_winMouseButtonPressed[2];
	buttonReleased[0] = g_winMouseButtonReleased[0];
	buttonReleased[1] = g_winMouseButtonReleased[1];
	buttonReleased[2] = g_winMouseButtonReleased[2];
	g_winMouseButtonPressed[0] = 0;
	g_winMouseButtonPressed[1] = 0;
	g_winMouseButtonPressed[2] = 0;
	g_winMouseButtonReleased[0] = 0;
	g_winMouseButtonReleased[1] = 0;
	g_winMouseButtonReleased[2] = 0;
	*deltaX *= g_winMouseScaleX;
	*deltaY *= g_winMouseScaleY;
	*positionX *= g_winMouseScaleX;
	*positionY *= g_winMouseScaleY;
}

/* Sets g_winMousePrevPos, g_winMouseCursorPos and g_winMousePos to x, y and
 * moves the cursor there, returning the result of SetCursorPos, or of
 * XvtPresentation_WarpClassic in the modern build. Only Mouse_SetPosition calls
 * it, and nothing calls that. */
// FUNCTION: XVT 0x4AAB00
int WinMouse_SetPosition(int x, int y)
{
	g_winMousePrevPos.x = x;
	g_winMouseCursorPos.x = x;
	g_winMousePos.x = x;
	g_winMousePrevPos.y = y;
	g_winMouseCursorPos.y = y;
	g_winMousePos.y = y;

#ifdef XVT_MODERN
	return XvtPresentation_WarpClassic(x, y);
#else
	return SetCursorPos(x, y);
#endif
}

/* Sets g_winMouseMinY and g_winMouseMaxY and g_winMouseCenterPos.y to
 * (minY + maxY) / 2. Only Mouse_SetVerticalBounds calls it, and nothing calls
 * that. */
// FUNCTION: XVT 0x4AAB60
void WinMouse_SetVerticalBounds(int minY, int maxY)
{
	g_winMouseMinY = minY;
	g_winMouseMaxY = maxY;
	g_winMouseCenterPos.y = (minY + maxY) / 2;
}

/* Sets g_winMouseMinX and g_winMouseMaxX and g_winMouseCenterPos.x to
 * (minX + maxX) / 2. Only Mouse_SetHorizontalBounds calls it, and nothing
 * calls that. */
// FUNCTION: XVT 0x4AAB40
void WinMouse_SetHorizontalBounds(int minX, int maxX)
{
	g_winMouseMinX = minX;
	g_winMouseMaxX = maxX;
	g_winMouseCenterPos.x = (minX + maxX) / 2;
}

/* Sets g_winMouseScaleX and g_winMouseScaleY. Only Mouse_SetScaleFactors calls
 * it, and nothing calls that. */
// FUNCTION: XVT 0x4AAB80
void WinMouse_SetScaleFactors(int scaleX, int scaleY)
{
	g_winMouseScaleX = scaleX;
	g_winMouseScaleY = scaleY;
}

/* Polls with WinMouse_PollState and stores the scaled position, cut to 16 bits,
 * and the held buttons as 0x1 left, 0x2 right, 0x4 middle.
 * Mouse_ReadPositionAndButtons is its only caller. */
// FUNCTION: XVT 0x4AC860
void WinMouse_PollPositionAndButtons(int16_t *buttons, int16_t *x, int16_t *y)
{
	int positionX;
	int positionY;
	int buttonDown[3];
	int deltaX;
	int deltaY;
	int buttonPressed[3];
	int buttonReleased[3];

	WinMouse_PollState(&positionX, &positionY, &deltaX, &deltaY, buttonDown,
			   buttonPressed, buttonReleased);
	*x = (int16_t)positionX;
	*y = (int16_t)positionY;
	*buttons = (int16_t)(buttonDown[0] |
			     2 * (buttonDown[1] | 2 * buttonDown[2]));
}

/* Polls with WinMouse_PollState and reports one button, 0 left, 1 right, 2
 * middle: whether it is held, whether it was pressed since the last poll, and
 * the scaled position, each cut to 16 bits. Does not check buttonIndex. Nothing
 * calls this. */
// FUNCTION: XVT 0x4AC8F0
void WinMouse_PollButtonPress(int16_t buttonIndex, int16_t *isDown,
			      int16_t *pressed, int16_t *x, int16_t *y)
{
	int positionX;
	int positionY;
	int deltaX;
	int deltaY;
	int buttonDown[3];
	int buttonPressed[3];
	int buttonReleased[3];

	WinMouse_PollState(&positionX, &positionY, &deltaX, &deltaY, buttonDown,
			   buttonPressed, buttonReleased);
	*isDown = (int16_t)buttonDown[buttonIndex];
	*pressed = (int16_t)buttonPressed[buttonIndex];
	*x = (int16_t)positionX;
	*y = (int16_t)positionY;
}

/* Polls with WinMouse_PollState and reports one button, 0 left, 1 right, 2
 * middle: whether it is held, whether it was released since the last poll, and
 * the scaled position, each cut to 16 bits. Does not check buttonIndex. Nothing
 * calls this. */
// FUNCTION: XVT 0x4AC960
void WinMouse_PollButtonRelease(int16_t buttonIndex, int16_t *isDown,
				int16_t *released, int16_t *x, int16_t *y)
{
	int positionX;
	int positionY;
	int deltaX;
	int deltaY;
	int buttonDown[3];
	int buttonReleased[3];
	int buttonPressed[3];

	WinMouse_PollState(&positionX, &positionY, &deltaX, &deltaY, buttonDown,
			   buttonPressed, buttonReleased);
	*isDown = (int16_t)buttonDown[buttonIndex];
	*released = (int16_t)buttonReleased[buttonIndex];
	*x = (int16_t)positionX;
	*y = (int16_t)positionY;
}

/* Polls with WinMouse_PollState and stores the scaled movement, cut to 16 bits.
 * Mouse_ReadDelta is its only caller. */
// FUNCTION: XVT 0x4ACA20
void WinMouse_PollMovementDelta(int16_t *deltaX, int16_t *deltaY)
{
	int polledDeltaX;
	int polledDeltaY;
	int positionX;
	int positionY;
	int buttonDown[3];
	int buttonPressed[3];
	int buttonReleased[3];

	WinMouse_PollState(&positionX, &positionY, &polledDeltaX, &polledDeltaY,
			   buttonDown, buttonPressed, buttonReleased);
	*deltaX = (int16_t)polledDeltaX;
	*deltaY = (int16_t)polledDeltaY;
}
