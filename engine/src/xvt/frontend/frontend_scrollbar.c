#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/input/keyboard.h"
#include <string.h>

// GLOBAL: XVT 0x52C00C
int g_scrollbarRepeatCountdown = 0;
// GLOBAL: XVT 0x52C010
int g_scrollbarRepeatInterval = 0;

// FUNCTION: XVT 0x4D9D10
int FrontendScrollbar_SaveState(void) {
	memcpy(g_scrollableControlIdsSaved, g_scrollableControlIds, sizeof(g_scrollableControlIdsSaved));
	g_scrollableControlCountSaved = g_scrollableControlCount;
	return 1;
}

// FUNCTION: XVT 0x4D9D40
int FrontendScrollbar_RestoreState(void) {
	memcpy(g_scrollableControlIds, g_scrollableControlIdsSaved, sizeof(g_scrollableControlIds));
	g_scrollableControlCount = g_scrollableControlCountSaved;
	return 1;
}

// FUNCTION: XVT 0x4D9D70
int FrontendScrollbar_Draw(const RECT* barRect, int currentValue, int maximumExclusive, int minimum,
						   int pageStep, unsigned int color, int controlId) {
	RECT thumb;

	struct {
		RECT partRect;
		int cursorX;
		int gateId;
	} drawState;

	int cursorY;
	int width;
	int height;
	int travel;
	int range;
	int thumbSize;
	int top;
	int value;

	Frontend_RegisterScrollableControl(controlId);
	FrontendCursor_GetPos(&drawState.cursorX, &cursorY);
	width = barRect->right - barRect->left;
	height = barRect->bottom - barRect->top;
	travel = height - 2 * width;
	range = maximumExclusive - minimum;
#ifdef XVT_MODERN
	if (range <= 0)
		return currentValue;
#endif
	thumbSize = travel / range;
	if (thumbSize < 1)
		thumbSize = 1;
	value = currentValue;
	if (height > width) {
		if (Keyboard_PeekChar() == 9) {
			Keyboard_DiscardChar();
			Frontend_CycleScrollableFocus();
		}
		drawState.gateId = controlId + 1000;

		/* Draw and update the ordinary scrollbar while no thumb drag owns the input gate. */
		if (!FrontendMouse_IsGateOwner(drawState.gateId)) {
			if (FrontendMouse_GetLeftClick() || FrontendMouse_GetRightClick()) {
				g_scrollbarRepeatCountdown = 0;
				g_scrollbarRepeatInterval = 0;
			}
			FrontendDraw_RectCopy(&drawState.partRect, barRect);
			FrontendDraw_RectAssign(
				&thumb, drawState.partRect.left,
				width + travel * currentValue / range + drawState.partRect.top, drawState.partRect.right,
				width + travel * currentValue / range + drawState.partRect.top + thumbSize);
			drawState.partRect.top += width;
			drawState.partRect.bottom -= width;
			FrontendDraw_FillRectTranslucent(&drawState.partRect, 0, 0, color);
			if (FrontendDraw_PointInRect(&drawState.partRect, drawState.cursorX, cursorY) &&
				(FrontendMouse_GetLeftClick() || FrontendMouse_GetRightClick())) {
				if (cursorY < thumb.top) {
					value = currentValue - pageStep;
					if (currentValue - pageStep < minimum)
						value = minimum;
				} else if (cursorY > thumb.bottom) {
					value = currentValue + pageStep;
					if (maximumExclusive <= value)
						value = maximumExclusive - 1;
				}
			}
			if (g_scrollableControlIds[0] == controlId) {
				if (Keyboard_IsKeyDown(0x21)) {
					value = currentValue - pageStep;
					if (currentValue - pageStep < minimum)
						value = minimum;
				} else if (Keyboard_IsKeyDown(0x22)) {
					value = currentValue + pageStep;
					if (maximumExclusive <= value)
						value = maximumExclusive - 1;
				}
			}
			FrontendDraw_RectAssign(&drawState.partRect, barRect->left, barRect->top, barRect->right,
									barRect->top + width);
			if (FrontendDraw_PointInRect(&drawState.partRect, drawState.cursorX, cursorY) &&
				(FrontendMouse_GetLeftDown() || FrontendMouse_GetRightDown())) {
				FrontImage_DrawSprite("slideud", barRect->left, barRect->top);
				if (g_scrollbarRepeatCountdown == 0) {
					if (currentValue > minimum)
						value = currentValue - 1;
					if (g_scrollbarRepeatInterval == 0)
						g_scrollbarRepeatInterval = 12;
					else {
						g_scrollbarRepeatInterval >>= 2;
						if (g_scrollbarRepeatInterval == 0)
							g_scrollbarRepeatInterval = 1;
					}
					g_scrollbarRepeatCountdown = g_scrollbarRepeatInterval;
				} else {
					--g_scrollbarRepeatCountdown;
				}
			} else {
				FrontImage_DrawSprite("slideuu", barRect->left, barRect->top);
			}
			if (g_scrollableControlIds[0] == controlId) {
				if (Keyboard_IsKeyDown(0x26)) {
					if (currentValue > minimum)
						value = currentValue - 1;
				}
			}
			FrontendDraw_RectAssign(&drawState.partRect, barRect->left, barRect->bottom - width,
									barRect->right, barRect->bottom);
			if (FrontendDraw_PointInRect(&drawState.partRect, drawState.cursorX, cursorY) &&
				(FrontendMouse_GetLeftDown() || FrontendMouse_GetRightDown())) {
				FrontImage_DrawSprite("slidedd", barRect->left, barRect->bottom - width);
				if (g_scrollbarRepeatCountdown == 0) {
					if (currentValue < maximumExclusive - 1)
						value = currentValue + 1;
					if (g_scrollbarRepeatInterval == 0)
						g_scrollbarRepeatInterval = 12;
					else {
						g_scrollbarRepeatInterval >>= 2;
						if (g_scrollbarRepeatInterval == 0)
							g_scrollbarRepeatInterval = 1;
					}
					g_scrollbarRepeatCountdown = g_scrollbarRepeatInterval;
				} else {
					--g_scrollbarRepeatCountdown;
				}
			} else {
				FrontImage_DrawSprite("slidedu", barRect->left, barRect->bottom - width);
			}
			if (g_scrollableControlIds[0] == controlId) {
				if (Keyboard_IsKeyDown(0x28) && currentValue < maximumExclusive - 1)
					value = currentValue + 1;
			}
			FrontendDraw_Rect(&thumb, 0, 0, 0xFFFF, 0);
			if (Frontend_IsScrollableControlFocused(controlId)) {
				FrontendDraw_RectInsetXY(&thumb, 2, 2);
				FrontendDraw_Rect(&thumb, 0, 0, g_colorPaleCyan, 1);
				FrontendDraw_RectInsetXY(&thumb, -2, -2);
			}
			if (FrontendMouse_IsGateOpen() && FrontendDraw_PointInRect(&thumb, drawState.cursorX, cursorY) &&
				(FrontendMouse_GetLeftDown() || FrontendMouse_GetRightDown()))
				FrontendMouse_SetInputGate(drawState.gateId);
		} else {
			int thumbY;

			g_scrollbarRepeatCountdown = 0;
			g_scrollbarRepeatInterval = 0;
			if (FrontendMouse_GetLeftClickFor(drawState.gateId) ||
				FrontendMouse_GetRightClickFor(drawState.gateId)) {
				FrontendMouse_ClearInputGate();
				FrontendMouse_ClearClicks();
			}
			FrontendDraw_RectCopy(&drawState.partRect, barRect);
			FrontendDraw_FillRectTranslucent(&drawState.partRect, 0, 0, color);
			FrontendDraw_RectAssign(&drawState.partRect, barRect->left, barRect->top, barRect->right,
									barRect->top + width);
			FrontImage_DrawSprite("slideuu", barRect->left, barRect->top);
			FrontendDraw_RectAssign(&drawState.partRect, barRect->left, barRect->bottom - width,
									barRect->right, barRect->bottom);
			FrontImage_DrawSprite("slidedu", barRect->left, barRect->bottom - width);
			top = barRect->top;
			value = (cursorY - width - top) * range / travel;
			if (value < minimum)
				value = minimum;
			if (value >= maximumExclusive)
				value = maximumExclusive - 1;
			thumbY = cursorY;
			if (thumbY < top + width)
				thumbY = top + width;
			else if (thumbY >= barRect->bottom - width - thumbSize)
				thumbY = barRect->bottom - width - thumbSize - 1;
			FrontendDraw_RectAssign(&drawState.partRect, barRect->left, thumbY, barRect->right,
									thumbY + thumbSize);
			FrontendDraw_Rect(&drawState.partRect, 0, 0, 0xFFFF, 0);
			if (Frontend_IsScrollableControlFocused(controlId)) {
				FrontendDraw_RectInsetXY(&drawState.partRect, 2, 2);
				FrontendDraw_Rect(&drawState.partRect, 0, 0, g_colorTeal, 1);
				FrontendDraw_RectInsetXY(&drawState.partRect, -2, -2);
			}
		}
	}
	return value;
}
