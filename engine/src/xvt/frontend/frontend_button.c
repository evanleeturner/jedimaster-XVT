#include "xvt/frontend/frontend_button.h"

#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_text.h"

#include <stdio.h>
#include <string.h>

/* 1 once FrontendButton_DrawSpriteAndTooltip has computed
 * g_frontButtonRectGrayColor. Only that function writes it, and nothing sets it
 * back to 0. */
// GLOBAL: XVT 0x52C01C
static int g_frontButtonRectGrayColorInitialized = 0;
/* The tooltip box's outline color, RGB 0x60, 0x60, 0x60 as a display pixel
 * value from FrontendDisplay_PackRGB. Set once, by
 * FrontendButton_DrawSpriteAndTooltip. */
// GLOBAL: XVT 0x52C020
static int g_frontButtonRectGrayColor = 0;

/* RGB 5, 0x63, 0x6D as a display pixel value from FrontendDisplay_PackRGB: a
 * text button's outer fill and text when not pressed, its inner fill when
 * pressed. Set once, by FrontendButton_DrawTextButtonState. */
// GLOBAL: XVT 0x665460
static int g_frontButtonDarkColor = 0;

/* Nonzero while FrontendButton_DrawSpriteAndTooltip also draws
 * g_buttonOverlayText over each sprite button's rect. Set to 1 by
 * FrontendButton_EnableOverlayText and to 0 by
 * FrontendButton_DisableOverlayText; 0 until the first enable. */
// GLOBAL: XVT 0x665468
static int g_buttonOverlayTextEnabled;
/* The label drawn over sprite buttons while overlay text is enabled: the
 * caller's pointer, not a copy, chiefly to frontend string table entries. Only
 * FrontendButton_SetOverlayText writes it. */
// GLOBAL: XVT 0x66546C
const char *g_buttonOverlayText;
/* 1 once FrontendButton_DrawTextButtonState has computed g_frontButtonDarkColor
 * and g_frontButtonLightColor. Only that function writes it, and nothing sets
 * it back to 0. */
// GLOBAL: XVT 0x665570
static int g_frontButtonColorsInitialized = 0;
/* Nonzero to make the next FrontendButton_DrawOverlayText draw the pressed
 * colors. Set to 1 by FrontendButton_UsePressedOverlayStyle; set to 0 by every
 * FrontendButton_DrawOverlayText and by FrontendButton_EnableOverlayText. */
// GLOBAL: XVT 0x665574
static int g_buttonOverlayPressedStyle;
/* RGB 0x63, 0xE7, 0xF7 as a display pixel value from FrontendDisplay_PackRGB: a
 * text button's inner fill when not pressed, its outer fill and text when
 * pressed. Set once, by FrontendButton_DrawTextButtonState. */
// GLOBAL: XVT 0x665578
static int g_frontButtonLightColor = 0;
/* Per button slot, 1 when a mouse button was held over that button at its last
 * update, so the press sound plays once per press. Indexed by the callers'
 * heldStateSlot; only FrontendButton_HandleTextButton and
 * FrontendButton_HandleSpriteButton write it. */
// GLOBAL: XVT 0x665580
static uint8_t g_buttonHeldState[256] = {0};

/* Draws and runs a text button for one frame. While the cursor is over rect and
 * a mouse button is held it draws the button pressed and, at the start of the
 * hold, plays clickSoundName when g_gameConfig.sfxDatapadEnabled is set, at
 * volume 12 * g_gameConfig.sfxDatapadVolume; otherwise it draws it unpressed.
 * Returns 1 when the left button was released over it this frame, else 2 when
 * the right one was, else 0. Writes g_buttonHeldState[heldStateSlot], not
 * checking that the slot is under 256. unusedColor is passed on and never
 * used. */
// FUNCTION: XVT 0x4DA650
int FrontendButton_HandleTextButton(RECT *rect, const char *text, int fontSize,
				    int unusedColor, int heldStateSlot,
				    const char *clickSoundName)
{
	int cursorX;
	int cursorY;

	FrontendCursor_GetPos(&cursorX, &cursorY);
	if (FrontendDraw_PointInRect(rect, cursorX, cursorY)) {
		if (FrontendMouse_GetLeftDown() != 0 ||
		    FrontendMouse_GetRightDown() != 0) {
			FrontendButton_DrawTextButtonState(rect, text, fontSize,
							   unusedColor, 1);
			if (g_buttonHeldState[heldStateSlot] == 0 &&
			    g_gameConfig.sfxDatapadEnabled != 0) {
				FrontendSound_PlayUISound(
					clickSoundName, 1, 0, 255,
					12 * g_gameConfig.sfxDatapadVolume, 63);
			}
			g_buttonHeldState[heldStateSlot] = 1;
		} else {
			FrontendButton_DrawTextButtonState(rect, text, fontSize,
							   unusedColor, 0);
			g_buttonHeldState[heldStateSlot] = 0;
		}
		if (FrontendMouse_GetLeftClick() != 0) {
			return 1;
		}
		if (FrontendMouse_GetRightClick() != 0) {
			return 2;
		}
	} else {
		g_buttonHeldState[heldStateSlot] = 0;
		FrontendButton_DrawTextButtonState(rect, text, fontSize,
						   unusedColor, 0);
	}
	return 0;
}

/* Draws and runs a sprite button for one frame through
 * FrontendButton_DrawSpriteAndTooltip. While the cursor is over rect: when
 * either mouse button was released this frame it draws pressedSprite in the
 * pressed overlay style and returns 1; while a button is held it draws it the
 * same way and, at the start of the hold, plays pressSoundName when
 * g_gameConfig.sfxDatapadEnabled is set, at volume 12 *
 * g_gameConfig.sfxDatapadVolume. Otherwise it draws normalSprite. Returns 0 on
 * every other path. Writes g_buttonHeldState[heldStateSlot], except on the
 * release frame, not checking that the slot is under 256. unusedColor is passed
 * on and never used. */
// FUNCTION: XVT 0x4DA780
int FrontendButton_HandleSpriteButton(RECT *rect, const char *normalSprite,
				      const char *pressedSprite,
				      const char *tooltipText, int fontSize,
				      int unusedColor, int heldStateSlot,
				      const char *pressSoundName)
{
	int cursorX;
	int cursorY;

	FrontendCursor_GetPos(&cursorX, &cursorY);
	if (FrontendDraw_PointInRect(rect, cursorX, cursorY)) {
		if (FrontendMouse_GetLeftClick() != 0 ||
		    FrontendMouse_GetRightClick() != 0) {
			FrontendButton_UsePressedOverlayStyle();
			FrontendButton_DrawSpriteAndTooltip(
				rect, pressedSprite, tooltipText, fontSize,
				unusedColor);
			return 1;
		}
		if (FrontendMouse_GetLeftDown() != 0 ||
		    FrontendMouse_GetRightDown() != 0) {
			FrontendButton_UsePressedOverlayStyle();
			FrontendButton_DrawSpriteAndTooltip(
				rect, pressedSprite, tooltipText, fontSize,
				unusedColor);
			if (g_buttonHeldState[heldStateSlot] == 0 &&
			    g_gameConfig.sfxDatapadEnabled != 0) {
				FrontendSound_PlayUISound(
					pressSoundName, 1, 0, 255,
					12 * g_gameConfig.sfxDatapadVolume, 63);
			}
			g_buttonHeldState[heldStateSlot] = 1;
			return 0;
		}
		FrontendButton_DrawSpriteAndTooltip(
			rect, normalSprite, tooltipText, fontSize, unusedColor);
		g_buttonHeldState[heldStateSlot] = 0;
		return 0;
	}

	g_buttonHeldState[heldStateSlot] = 0;
	FrontendButton_DrawSpriteAndTooltip(rect, normalSprite, tooltipText,
					    fontSize, unusedColor);
	return 0;
}

/* Draws a text button: rect filled in one button color, an inner rect inset 3
 * pixels (1 when rect's bottom - top is 14 or less) filled in the other, and
 * text centered in rect in the outer color. With isPressed nonzero the outer
 * color is g_frontButtonLightColor, else g_frontButtonDarkColor; the first call
 * computes both. Returns FrontendText_DrawCentered's result. */
// FUNCTION: XVT 0x4DA8E0
int FrontendButton_DrawTextButtonState(RECT *rect, const char *text,
				       int fontSize, int unusedColor,
				       char isPressed)
{
	RECT innerRect;
	int textColor;

	(void)unusedColor;

	if (!g_frontButtonColorsInitialized) {
		g_frontButtonColorsInitialized = 1;
		g_frontButtonDarkColor = FrontendDisplay_PackRGB(5, 0x63, 0x6D);
		g_frontButtonLightColor =
			FrontendDisplay_PackRGB(0x63, 0xE7, 0xF7);
	}

	if (isPressed) {
		FrontendDraw_Rect(rect, 0, 0, g_frontButtonLightColor, 1);
		FrontendDraw_RectCopy(&innerRect, rect);
		if (rect->bottom - rect->top > 14) {
			FrontendDraw_RectInsetXY(&innerRect, 3, 3);
		} else {
			FrontendDraw_RectInsetXY(&innerRect, 1, 1);
		}
		FrontendDraw_Rect(&innerRect, 0, 0, g_frontButtonDarkColor, 1);
		textColor = g_frontButtonLightColor;
	} else {
		FrontendDraw_Rect(rect, 0, 0, g_frontButtonDarkColor, 1);
		FrontendDraw_RectCopy(&innerRect, rect);
		if (rect->bottom - rect->top > 14) {
			FrontendDraw_RectInsetXY(&innerRect, 3, 3);
		} else {
			FrontendDraw_RectInsetXY(&innerRect, 1, 1);
		}
		FrontendDraw_Rect(&innerRect, 0, 0, g_frontButtonLightColor, 1);
		textColor = g_frontButtonDarkColor;
	}

	return FrontendText_DrawCentered(fontSize, text, rect, textColor);
}

/* Draws spriteName with its top-left corner at (0, 0), then, while overlay text
 * is enabled, g_buttonOverlayText centered in rect through
 * FrontendButton_DrawOverlayText. When tooltipText is not NULL and the cursor
 * is in rect, it also draws a tooltip box whose top-left corner is the cursor
 * moved right and down by the cursor's size, with right = left + textWidth + 5
 * and bottom = top + fontSize + 3: filled 0xFFFF, outlined in
 * g_frontButtonRectGrayColor, the text centered in color 0. When left +
 * textWidth + 5 reaches 640 the left becomes 634 - textWidth, and when top +
 * fontSize + 5 reaches 480 the top becomes 474 - fontSize. The first call
 * computes the gray. unusedColor is never used. */
// FUNCTION: XVT 0x4DAA20
void FrontendButton_DrawSpriteAndTooltip(RECT *rect, const char *spriteName,
					 const char *tooltipText, int fontSize,
					 int unusedColor)
{
	int tooltipLeft;
	int tooltipTop;
	int cursorWidth;
	int cursorHeight;
	int textWidth;
	RECT tooltipRect;

	(void)unusedColor;

	FrontImage_DrawSprite(spriteName, 0, 0);
	if (g_frontButtonRectGrayColorInitialized == 0) {
		g_frontButtonRectGrayColorInitialized = 1;
		g_frontButtonRectGrayColor =
			FrontendDisplay_PackRGB(0x60, 0x60, 0x60);
	}

	FrontendCursor_GetPos(&tooltipLeft, &tooltipTop);
	if (g_buttonOverlayTextEnabled != 0) {
		FrontendButton_DrawOverlayText(rect, g_buttonOverlayText);
	}
	if (tooltipText == NULL ||
	    !FrontendDraw_PointInRect(rect, tooltipLeft, tooltipTop)) {
		return;
	}

	textWidth = FrontendText_MeasureWidth(tooltipText, fontSize);
	FrontendCursor_GetDimensions(&cursorWidth, &cursorHeight);
	tooltipLeft += cursorWidth;
	tooltipTop += cursorHeight;
	if (textWidth + tooltipLeft + 5 >= 640) {
		tooltipLeft = 634 - textWidth;
	}
	if ((uint32_t)(fontSize + tooltipTop + 5) >= 480) {
		tooltipTop = 474 - fontSize;
	}

	FrontendDraw_RectAssign(&tooltipRect, tooltipLeft, tooltipTop,
				textWidth + tooltipLeft + 5,
				fontSize + tooltipTop + 3);
	FrontendDraw_Rect(&tooltipRect, 0, 0, 0xFFFF, 1);
	FrontendDraw_RectOutline(&tooltipRect, 0, 0,
				 g_frontButtonRectGrayColor);
	FrontendText_DrawCentered(fontSize, tooltipText, &tooltipRect, 0);
}

/* Draws the state sprites of an eight-slot navigation bar from slotStates: the
 * sprite "active<n>" for each FRONTEND_NAVIGATION_SLOT_ACTIVE slot n, 1 to 8,
 * then, for each FRONTEND_NAVIGATION_SLOT_SELECTED slot, the sprite
 * "<n>lita<s>", s being the state value of the slot before it, and
 * "<n>litb<s>", s being that of the slot after it. For these two the slots are
 * taken in the order 1 to 5, 8, 6, 7, n counts positions in that order, the
 * first and seventh positions get no "lita" and the sixth and eighth no "litb".
 * Every sprite is drawn at (0, 0); the names are built in
 * g_frontendScratchBuffer. */
// FUNCTION: XVT 0x4DABA0
void FrontendButton_DrawEightSlotNavigationState(
	const FrontendNavigationSlotState *slotStates)
{
	FrontendNavigationSlotState states[8];
	int index;
	FrontendNavigationSlotState savedState;
	FrontendNavigationSlotState *state;

	memcpy(states, slotStates, sizeof(states));
	for (index = 0; index < 8; ++index) {
		if (states[index] == FRONTEND_NAVIGATION_SLOT_ACTIVE) {
			sprintf(g_frontendScratchBuffer, "active%u", index + 1);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 0, 0);
		}
	}

	savedState = states[7];
	states[7] = states[6];
	states[6] = states[5];
	states[5] = savedState;
	index = 0;
	state = states;
	do {
		if (*state == FRONTEND_NAVIGATION_SLOT_SELECTED) {
			if (state != states && state != &states[6]) {
				sprintf(g_frontendScratchBuffer, "%ulita%u",
					index + 1, states[index - 1]);
				FrontImage_DrawSprite(g_frontendScratchBuffer,
						      0, 0);
			}
			if (state != &states[7] && state != &states[5]) {
				sprintf(g_frontendScratchBuffer, "%ulitb%u",
					index + 1, state[1]);
				FrontImage_DrawSprite(g_frontendScratchBuffer,
						      0, 0);
			}
		}
		++state;
		++index;
	} while (state < states + 8);
}

/* Draws str centered in rect in the size-10 font twice: a shadow 2 pixels right
 * and down, then the text. The text is g_colorPaleCyan on a g_colorTeal shadow,
 * or the reverse after FrontendButton_UsePressedOverlayStyle; the call then
 * sets g_buttonOverlayPressedStyle to 0. Moves *rect and moves it back. Returns
 * the second FrontendText_DrawCentered result. */
// FUNCTION: XVT 0x4DACA0
int FrontendButton_DrawOverlayText(RECT *rect, const char *str)
{
	int result;

	if (g_buttonOverlayPressedStyle) {
		FrontendDraw_RectOffsetXY(rect, 2, 2);
		FrontendText_DrawCentered(10, str, rect, g_colorPaleCyan);
		FrontendDraw_RectOffsetXY(rect, -2, -2);
		result = FrontendText_DrawCentered(10, str, rect, g_colorTeal);
	} else {
		FrontendDraw_RectOffsetXY(rect, 2, 2);
		FrontendText_DrawCentered(10, str, rect, g_colorTeal);
		FrontendDraw_RectOffsetXY(rect, -2, -2);
		result = FrontendText_DrawCentered(10, str, rect,
						   g_colorPaleCyan);
	}

	g_buttonOverlayPressedStyle = 0;
	return result;
}

/* Turns overlay text on and drops the pressed style: sets
 * g_buttonOverlayTextEnabled to 1 and g_buttonOverlayPressedStyle to 0. */
// FUNCTION: XVT 0x4DAD40
void FrontendButton_EnableOverlayText(void)
{
	g_buttonOverlayTextEnabled = 1;
	g_buttonOverlayPressedStyle = 0;
}

/* Turns overlay text off: sets g_buttonOverlayTextEnabled to 0. */
// FUNCTION: XVT 0x4DAD60
void FrontendButton_DisableOverlayText(void) { g_buttonOverlayTextEnabled = 0; }

/* Stores text in g_buttonOverlayText, the pointer and not a copy, and returns
 * it. */
// FUNCTION: XVT 0x4DAD70
const char *FrontendButton_SetOverlayText(const char *text)
{
	return g_buttonOverlayText = text;
}

/* Sets g_buttonOverlayPressedStyle to 1, so the next
 * FrontendButton_DrawOverlayText swaps its colors. */
// FUNCTION: XVT 0x4DAD80
void FrontendButton_UsePressedOverlayStyle(void)
{
	g_buttonOverlayPressedStyle = 1;
}

/* Returns g_buttonOverlayTextEnabled. */
// FUNCTION: XVT 0x4DAD90
int FrontendButton_IsOverlayTextEnabled(void)
{
	return g_buttonOverlayTextEnabled;
}
