#ifndef XVT_INPUT_MOUSE_H
#define XVT_INPUT_MOUSE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int Mouse_ReadPositionAndButtons(int16_t* x, int16_t* y);
void Mouse_ReadDelta(int16_t* deltaX, int16_t* deltaY);
int Mouse_SetPosition(int16_t x, int16_t y);
void Mouse_SetHorizontalBounds(int16_t minX, int16_t maxX);
void Mouse_SetVerticalBounds(int16_t minY, int16_t maxY);
void Mouse_SetScaleFactors(int16_t scaleX, int16_t scaleY);

#ifdef __cplusplus
}
#endif

#endif
