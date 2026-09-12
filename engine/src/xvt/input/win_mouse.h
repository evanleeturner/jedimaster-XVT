#ifndef XVT_INPUT_WIN_MOUSE_H
#define XVT_INPUT_WIN_MOUSE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void WinMouse_PollState(int* positionX, int* positionY, int* deltaX, int* deltaY, int* buttonDown,
						int* buttonPressed, int* buttonReleased);
int WinMouse_SetPosition(int x, int y);
void WinMouse_SetHorizontalBounds(int minX, int maxX);
void WinMouse_SetVerticalBounds(int minY, int maxY);
void WinMouse_SetScaleFactors(int scaleX, int scaleY);
void WinMouse_GetPositionAndButtons(int16_t* buttons, int16_t* x, int16_t* y);
void WinMouse_GetButtonPress(int16_t buttonIndex, int16_t* isDown, int16_t* pressed, int16_t* x, int16_t* y);
void WinMouse_GetButtonRelease(int16_t buttonIndex, int16_t* isDown, int16_t* released, int16_t* x,
							   int16_t* y);
void WinMouse_GetMovementDelta(int16_t* deltaX, int16_t* deltaY);

#ifdef __cplusplus
}
#endif

#endif
