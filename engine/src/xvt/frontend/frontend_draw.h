#ifndef XVT_FRONTEND_FRONTEND_DRAW_H
#define XVT_FRONTEND_FRONTEND_DRAW_H

#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Active frontend draw surface and clip bounds. */
extern uint8_t* g_drawSurfacePtr;

void FrontendDraw_RectAssign(RECT* rect, int32_t left, int32_t top, int32_t right, int32_t bottom);
void FrontendDraw_RectCopy(RECT* dst, const RECT* src);
void FrontendDraw_RectOffsetXY(RECT* rect, int dx, int dy);
void FrontendDraw_RectInsetXY(RECT* rect, int dx, int dy);
int FrontendDraw_RectClipToBounds(RECT* rect);
void FrontendDraw_FillRectTranslucent(const RECT* src, int dx, int dy, unsigned int color);
void FrontendDraw_Rect(RECT* rect, int dx, int dy, int color, int filled);
void FrontendDraw_RectOutline(RECT* rect, int dx, int dy, int color);
int FrontendDraw_PointInRect(const RECT* rect, int x, int y);
void FrontendDraw_Line(int x0, int y0, int x1, int y1, int color);
void FrontendDraw_HorizontalLineClipped(int x0, int x1, int y, int color);
void FrontendDraw_VerticalLineClipped(int y0, int y1, int x, int color);

#ifdef __cplusplus
}
#endif

#endif
