#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/input/keyboard.h"
#include <string.h>

/* Frames left before a held scrollbar arrow steps the value again; the step
 * comes on a frame that finds it 0. One shared by every scrollbar. Only
 * FrontendScrollbar_Draw writes it: it loads g_scrollbarRepeatInterval after
 * each step, lowers it by one on each other frame an arrow is held, and sets it
 * to 0 on a click and during a thumb drag. */
// GLOBAL: XVT 0x52C00C
int g_scrollbarRepeatCountdown = 0;
/* The value g_scrollbarRepeatCountdown is loaded with after an arrow step: 0
 * until the first step, then 12, then the previous value shifted right 2 bits
 * but at least 1 (3, then 1). Only FrontendScrollbar_Draw writes it, and sets
 * it to 0 on a click and during a thumb drag. */
// GLOBAL: XVT 0x52C010
int g_scrollbarRepeatInterval = 0;

/* Copies the scrollable-control focus list, g_scrollableControlIds and
 * g_scrollableControlCount, into g_scrollableControlIdsSaved and
 * g_scrollableControlCountSaved. Returns 1. */
// FUNCTION: XVT 0x4D9D10
int FrontendScrollbar_SaveState(void)
{
	memcpy(g_scrollableControlIdsSaved, g_scrollableControlIds,
	       sizeof(g_scrollableControlIdsSaved));
	g_scrollableControlCountSaved = g_scrollableControlCount;
	return 1;
}

/* Copies the focus list saved by FrontendScrollbar_SaveState back into
 * g_scrollableControlIds and g_scrollableControlCount. Returns 1. */
// FUNCTION: XVT 0x4D9D40
int FrontendScrollbar_RestoreState(void)
{
	memcpy(g_scrollableControlIds, g_scrollableControlIdsSaved,
	       sizeof(g_scrollableControlIds));
	g_scrollableControlCount = g_scrollableControlCountSaved;
	return 1;
}

/* Draws a vertical scrollbar in barRect and returns the value the player's
 * input this frame gives it, currentValue when none. The arrow buttons are
 * squares as tall as the bar is wide at each end; travel is the bar's height
 * minus two widths, and the thumb is travel / (maximumExclusive - minimum)
 * pixels tall, at least 1, placed as if minimum were 0 (every caller passes 0).
 * Registers controlId in the Tab focus list and, when a Tab is next in the
 * keyboard buffer, takes it and moves the focus on. A click in the track above
 * or below the thumb moves the value by pageStep; holding an arrow moves it by
 * 1 at once, again 13 frames later, then 4 frames later, then every 2 frames.
 * While the bar has the focus, held Page Up and Page Down keys move it by
 * pageStep and held Up and Down arrow keys by 1. Each of these starts from
 * currentValue, so they do not add up; the last one checked wins. Pressing on
 * the thumb claims the mouse input gate as controlId + 1000; while it holds the
 * gate the value is (cursor y - bar top - width) * (maximumExclusive - minimum)
 * / travel, and the release opens the gate and clears the clicks. Each change
 * stays from minimum to maximumExclusive - 1. Fills the track translucent in
 * color (the whole bar during a drag) and outlines the thumb in 0xFFFF, filling
 * it inside, inset 2 pixels, when focused. Draws nothing and returns
 * currentValue when the bar is not taller than wide. The original build divides
 * by zero when maximumExclusive equals minimum; the modern build returns
 * currentValue there, drawing nothing, as it does whenever their difference is
 * under 1. */
// FUNCTION: XVT 0x4D9D70
int FrontendScrollbar_Draw(const RECT *barRect, int currentValue,
			   int maximumExclusive, int minimum, int pageStep,
			   unsigned int color, int controlId)
{
	RECT thumb;

	struct {
		/* The part of the bar being drawn or tested: the track, an
		 * arrow button or the thumb during a drag. */
		RECT partRect;
		int cursorX; /* Cursor x, from FrontendCursor_GetPos. */
		/* controlId + 1000, the id this bar holds the mouse input gate
		 * with during a thumb drag. */
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
	if (range <= 0) {
		return currentValue;
	}
#endif
	thumbSize = travel / range;
	if (thumbSize < 1) {
		thumbSize = 1;
	}
	value = currentValue;
	if (height > width) {
		if (Keyboard_PeekChar() == 9) {
			Keyboard_DiscardChar();
			Frontend_CycleScrollableFocus();
		}
		drawState.gateId = controlId + 1000;

		/* Draw and update the ordinary scrollbar while no thumb drag owns the input gate. */
		if (!FrontendMouse_IsGateOwner(drawState.gateId)) {
			if (FrontendMouse_GetLeftClick() ||
			    FrontendMouse_GetRightClick()) {
				g_scrollbarRepeatCountdown = 0;
				g_scrollbarRepeatInterval = 0;
			}
			FrontendDraw_RectCopy(&drawState.partRect, barRect);
			FrontendDraw_RectAssign(
				&thumb, drawState.partRect.left,
				width + travel * currentValue / range +
					drawState.partRect.top,
				drawState.partRect.right,
				width + travel * currentValue / range +
					drawState.partRect.top + thumbSize);
			drawState.partRect.top += width;
			drawState.partRect.bottom -= width;
			FrontendDraw_FillRectTranslucent(&drawState.partRect, 0,
							 0, color);
			if (FrontendDraw_PointInRect(&drawState.partRect,
						     drawState.cursorX,
						     cursorY) &&
			    (FrontendMouse_GetLeftClick() ||
			     FrontendMouse_GetRightClick())) {
				if (cursorY < thumb.top) {
					value = currentValue - pageStep;
					if (currentValue - pageStep < minimum) {
						value = minimum;
					}
				} else if (cursorY > thumb.bottom) {
					value = currentValue + pageStep;
					if (maximumExclusive <= value) {
						value = maximumExclusive - 1;
					}
				}
			}
			if (g_scrollableControlIds[0] == controlId) {
				if (Keyboard_IsKeyDown(0x21)) {
					value = currentValue - pageStep;
					if (currentValue - pageStep < minimum) {
						value = minimum;
					}
				} else if (Keyboard_IsKeyDown(0x22)) {
					value = currentValue + pageStep;
					if (maximumExclusive <= value) {
						value = maximumExclusive - 1;
					}
				}
			}
			FrontendDraw_RectAssign(&drawState.partRect,
						barRect->left, barRect->top,
						barRect->right,
						barRect->top + width);
			if (FrontendDraw_PointInRect(&drawState.partRect,
						     drawState.cursorX,
						     cursorY) &&
			    (FrontendMouse_GetLeftDown() ||
			     FrontendMouse_GetRightDown())) {
				FrontImage_DrawSprite("slideud", barRect->left,
						      barRect->top);
				if (g_scrollbarRepeatCountdown == 0) {
					if (currentValue > minimum) {
						value = currentValue - 1;
					}
					if (g_scrollbarRepeatInterval == 0) {
						g_scrollbarRepeatInterval = 12;
					} else {
						g_scrollbarRepeatInterval >>= 2;
						if (g_scrollbarRepeatInterval ==
						    0) {
							g_scrollbarRepeatInterval =
								1;
						}
					}
					g_scrollbarRepeatCountdown =
						g_scrollbarRepeatInterval;
				} else {
					--g_scrollbarRepeatCountdown;
				}
			} else {
				FrontImage_DrawSprite("slideuu", barRect->left,
						      barRect->top);
			}
			if (g_scrollableControlIds[0] == controlId) {
				if (Keyboard_IsKeyDown(0x26)) {
					if (currentValue > minimum) {
						value = currentValue - 1;
					}
				}
			}
			FrontendDraw_RectAssign(
				&drawState.partRect, barRect->left,
				barRect->bottom - width, barRect->right,
				barRect->bottom);
			if (FrontendDraw_PointInRect(&drawState.partRect,
						     drawState.cursorX,
						     cursorY) &&
			    (FrontendMouse_GetLeftDown() ||
			     FrontendMouse_GetRightDown())) {
				FrontImage_DrawSprite("slidedd", barRect->left,
						      barRect->bottom - width);
				if (g_scrollbarRepeatCountdown == 0) {
					if (currentValue <
					    maximumExclusive - 1) {
						value = currentValue + 1;
					}
					if (g_scrollbarRepeatInterval == 0) {
						g_scrollbarRepeatInterval = 12;
					} else {
						g_scrollbarRepeatInterval >>= 2;
						if (g_scrollbarRepeatInterval ==
						    0) {
							g_scrollbarRepeatInterval =
								1;
						}
					}
					g_scrollbarRepeatCountdown =
						g_scrollbarRepeatInterval;
				} else {
					--g_scrollbarRepeatCountdown;
				}
			} else {
				FrontImage_DrawSprite("slidedu", barRect->left,
						      barRect->bottom - width);
			}
			if (g_scrollableControlIds[0] == controlId) {
				if (Keyboard_IsKeyDown(0x28) &&
				    currentValue < maximumExclusive - 1) {
					value = currentValue + 1;
				}
			}
			FrontendDraw_Rect(&thumb, 0, 0, 0xFFFF, 0);
			if (Frontend_IsScrollableControlFocused(controlId)) {
				FrontendDraw_RectInsetXY(&thumb, 2, 2);
				FrontendDraw_Rect(&thumb, 0, 0, g_colorPaleCyan,
						  1);
				FrontendDraw_RectInsetXY(&thumb, -2, -2);
			}
			if (FrontendMouse_IsGateOpen() &&
			    FrontendDraw_PointInRect(&thumb, drawState.cursorX,
						     cursorY) &&
			    (FrontendMouse_GetLeftDown() ||
			     FrontendMouse_GetRightDown())) {
				FrontendMouse_SetInputGate(drawState.gateId);
			}
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
			FrontendDraw_FillRectTranslucent(&drawState.partRect, 0,
							 0, color);
			FrontendDraw_RectAssign(&drawState.partRect,
						barRect->left, barRect->top,
						barRect->right,
						barRect->top + width);
			FrontImage_DrawSprite("slideuu", barRect->left,
					      barRect->top);
			FrontendDraw_RectAssign(
				&drawState.partRect, barRect->left,
				barRect->bottom - width, barRect->right,
				barRect->bottom);
			FrontImage_DrawSprite("slidedu", barRect->left,
					      barRect->bottom - width);
			top = barRect->top;
			value = (cursorY - width - top) * range / travel;
			if (value < minimum) {
				value = minimum;
			}
			if (value >= maximumExclusive) {
				value = maximumExclusive - 1;
			}
			thumbY = cursorY;
			if (thumbY < top + width) {
				thumbY = top + width;
			} else if (thumbY >=
				   barRect->bottom - width - thumbSize) {
				thumbY =
					barRect->bottom - width - thumbSize - 1;
			}
			FrontendDraw_RectAssign(
				&drawState.partRect, barRect->left, thumbY,
				barRect->right, thumbY + thumbSize);
			FrontendDraw_Rect(&drawState.partRect, 0, 0, 0xFFFF, 0);
			if (Frontend_IsScrollableControlFocused(controlId)) {
				FrontendDraw_RectInsetXY(&drawState.partRect, 2,
							 2);
				FrontendDraw_Rect(&drawState.partRect, 0, 0,
						  g_colorTeal, 1);
				FrontendDraw_RectInsetXY(&drawState.partRect,
							 -2, -2);
			}
		}
	}
	return value;
}
