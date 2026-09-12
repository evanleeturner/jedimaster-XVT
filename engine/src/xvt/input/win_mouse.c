#include "xvt/input/win_mouse.h"

#include "xvt/flight/flight_input.h"
#include "xvt/util/win32.h"

#ifdef XVT_MODERN
#include "aeron/aeron.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/runtime/presentation.h"
#else
struct WinMouseWin32Message {
	void* window;
	uint32_t message;
	uint32_t wParam;
	int32_t lParam;
	uint32_t time;
	int32_t pointX;
	int32_t pointY;
};

__declspec(dllimport) int __stdcall PeekMessageA(struct WinMouseWin32Message* message, void* hWnd,
												 unsigned int filterMin, unsigned int filterMax,
												 unsigned int removeMessage);
__declspec(dllimport) int __stdcall GetMessageA(struct WinMouseWin32Message* message, void* hWnd,
												unsigned int filterMin, unsigned int filterMax);
__declspec(dllimport) int __stdcall TranslateMessage(const struct WinMouseWin32Message* message);
__declspec(dllimport) int32_t __stdcall DispatchMessageA(const struct WinMouseWin32Message* message);
__declspec(dllimport) int __stdcall GetCursorPos(POINT* point);
__declspec(dllimport) int __stdcall SetCursorPos(int x, int y);
#endif

// GLOBAL: XVT 0x527ED8
POINT g_winMousePrevPos;
// GLOBAL: XVT 0x527EE0
POINT g_winMouseCursorPos;
// GLOBAL: XVT 0x527EE8
POINT g_winMousePos;
// GLOBAL: XVT 0x527EF0
int g_winMouseButtonDown[3] = { 0, 0, 0 };
// GLOBAL: XVT 0x527EFC
int g_winMouseButtonPressed[3] = { 0, 0, 0 };
// GLOBAL: XVT 0x527F08
int g_winMouseButtonReleased[3] = { 0, 0, 0 };
// GLOBAL: XVT 0x527F14
int g_winMouseMinX;
// GLOBAL: XVT 0x527F18
int g_winMouseMaxX;
// GLOBAL: XVT 0x527F1C
int g_winMouseMinY;
// GLOBAL: XVT 0x527F20
int g_winMouseMaxY;
// GLOBAL: XVT 0x527F24
int g_winMouseScaleX;
// GLOBAL: XVT 0x527F28
int g_winMouseScaleY;
// GLOBAL: XVT 0x527F2C
POINT g_winMouseCenterPos;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AA910
void WinMouse_PollState(int* positionX, int* positionY, int* deltaX, int* deltaY, int* buttonDown,
						int* buttonPressed, int* buttonReleased) {
	POINT point;

#ifdef XVT_MODERN
	const AeronInputSnapshot* input;

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
	if (!XvtPresentation_MouseToClassic(input, &point.x, &point.y))
		point = g_winMouseCursorPos;
	g_winMouseButtonDown[0] =
		(XvtInput_FilterMouseButtons(input->mouse.buttons) & AERON_MOUSE_BUTTON_LEFT) != 0;
	g_winMouseButtonDown[1] =
		(XvtInput_FilterMouseButtons(input->mouse.buttons) & AERON_MOUSE_BUTTON_RIGHT) != 0;
	g_winMouseButtonDown[2] =
		(XvtInput_FilterMouseButtons(input->mouse.buttons) & AERON_MOUSE_BUTTON_MIDDLE) != 0;
	g_winMouseButtonPressed[0] =
		(XvtInput_FilterMouseButtons(input->mouse.pressed_buttons) & AERON_MOUSE_BUTTON_LEFT) != 0;
	g_winMouseButtonPressed[1] =
		(XvtInput_FilterMouseButtons(input->mouse.pressed_buttons) & AERON_MOUSE_BUTTON_RIGHT) != 0;
	g_winMouseButtonPressed[2] =
		(XvtInput_FilterMouseButtons(input->mouse.pressed_buttons) & AERON_MOUSE_BUTTON_MIDDLE) != 0;
	g_winMouseButtonReleased[0] =
		(XvtInput_FilterMouseButtons(input->mouse.released_buttons) & AERON_MOUSE_BUTTON_LEFT) != 0;
	g_winMouseButtonReleased[1] =
		(XvtInput_FilterMouseButtons(input->mouse.released_buttons) & AERON_MOUSE_BUTTON_RIGHT) != 0;
	g_winMouseButtonReleased[2] =
		(XvtInput_FilterMouseButtons(input->mouse.released_buttons) & AERON_MOUSE_BUTTON_MIDDLE) != 0;
#else
	struct WinMouseWin32Message message;

	if (g_flightInputNonBlockingMsgPump == 0 || PeekMessageA(&message, 0, 0, 0, 0) != 0) {
		if (GetMessageA(&message, 0, 0, 0) == 0)
			return;
		TranslateMessage(&message);
		DispatchMessageA(&message);
	}
	GetCursorPos(&point);
#endif

	g_winMouseCursorPos = point;
#ifdef XVT_MODERN
	if (!XvtInput_MouseMotionAllowed())
		g_winMousePrevPos = point;
#endif
	*deltaX = point.x - g_winMousePrevPos.x;
	*deltaY = g_winMouseCursorPos.y - g_winMousePrevPos.y;
	g_winMousePos.x += *deltaX;
	g_winMousePos.y += *deltaY;
	if (g_winMousePos.x < g_winMouseMinX)
		g_winMousePos.x = g_winMouseMinX;
	if (g_winMousePos.x > g_winMouseMaxX)
		g_winMousePos.x = g_winMouseMaxX;
	if (g_winMousePos.y < g_winMouseMinY)
		g_winMousePos.y = g_winMouseMinY;
	if (g_winMousePos.y > g_winMouseMaxY)
		g_winMousePos.y = g_winMouseMaxY;
	*positionX = g_winMousePos.x;
	*positionY = g_winMousePos.y;

#ifdef XVT_MODERN
	XvtPresentation_WarpClassic(g_winMouseCenterPos.x, g_winMouseCenterPos.y);
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

// FUNCTION: XVT 0x4AAB00
int WinMouse_SetPosition(int x, int y) {
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

// FUNCTION: XVT 0x4AAB60
void WinMouse_SetVerticalBounds(int minY, int maxY) {
	g_winMouseMinY = minY;
	g_winMouseMaxY = maxY;
	g_winMouseCenterPos.y = (minY + maxY) / 2;
}

// FUNCTION: XVT 0x4AAB40
void WinMouse_SetHorizontalBounds(int minX, int maxX) {
	g_winMouseMinX = minX;
	g_winMouseMaxX = maxX;
	g_winMouseCenterPos.x = (minX + maxX) / 2;
}

// FUNCTION: XVT 0x4AAB80
void WinMouse_SetScaleFactors(int scaleX, int scaleY) {
	g_winMouseScaleX = scaleX;
	g_winMouseScaleY = scaleY;
}

// FUNCTION: XVT 0x4AC860
void WinMouse_GetPositionAndButtons(int16_t* buttons, int16_t* x, int16_t* y) {
	int positionX;
	int positionY;
	int buttonDown[3];
	int deltaX;
	int deltaY;
	int buttonPressed[3];
	int buttonReleased[3];

	WinMouse_PollState(&positionX, &positionY, &deltaX, &deltaY, buttonDown, buttonPressed, buttonReleased);
	*x = (int16_t)positionX;
	*y = (int16_t)positionY;
	*buttons = (int16_t)(buttonDown[0] | 2 * (buttonDown[1] | 2 * buttonDown[2]));
}

// FUNCTION: XVT 0x4AC8F0
void WinMouse_GetButtonPress(int16_t buttonIndex, int16_t* isDown, int16_t* pressed, int16_t* x, int16_t* y) {
	int positionX;
	int positionY;
	int deltaX;
	int deltaY;
	int buttonDown[3];
	int buttonPressed[3];
	int buttonReleased[3];

	WinMouse_PollState(&positionX, &positionY, &deltaX, &deltaY, buttonDown, buttonPressed, buttonReleased);
	*isDown = (int16_t)buttonDown[buttonIndex];
	*pressed = (int16_t)buttonPressed[buttonIndex];
	*x = (int16_t)positionX;
	*y = (int16_t)positionY;
}

// FUNCTION: XVT 0x4AC960
void WinMouse_GetButtonRelease(int16_t buttonIndex, int16_t* isDown, int16_t* released, int16_t* x,
							   int16_t* y) {
	int positionX;
	int positionY;
	int deltaX;
	int deltaY;
	int buttonDown[3];
	int buttonReleased[3];
	int buttonPressed[3];

	WinMouse_PollState(&positionX, &positionY, &deltaX, &deltaY, buttonDown, buttonPressed, buttonReleased);
	*isDown = (int16_t)buttonDown[buttonIndex];
	*released = (int16_t)buttonReleased[buttonIndex];
	*x = (int16_t)positionX;
	*y = (int16_t)positionY;
}

// FUNCTION: XVT 0x4ACA20
void WinMouse_GetMovementDelta(int16_t* deltaX, int16_t* deltaY) {
	int polledDeltaX;
	int polledDeltaY;
	int positionX;
	int positionY;
	int buttonDown[3];
	int buttonPressed[3];
	int buttonReleased[3];

	WinMouse_PollState(&positionX, &positionY, &polledDeltaX, &polledDeltaY, buttonDown, buttonPressed,
					   buttonReleased);
	*deltaX = (int16_t)polledDeltaX;
	*deltaY = (int16_t)polledDeltaY;
}
