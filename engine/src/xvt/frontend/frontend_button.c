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

// GLOBAL: XVT 0x52C01C
static int g_frontButtonRectGrayColorInitialized = 0;
// GLOBAL: XVT 0x52C020
static int g_frontButtonRectGrayColor = 0;

// GLOBAL: XVT 0x665460
static int g_frontButtonDarkColor = 0;

// GLOBAL: XVT 0x665468
static int g_buttonOverlayTextEnabled;
// GLOBAL: XVT 0x66546C
const char* g_buttonOverlayText;
// GLOBAL: XVT 0x665570
static int g_frontButtonColorsInitialized = 0;
// GLOBAL: XVT 0x665574
static int g_buttonOverlayPressedStyle;
// GLOBAL: XVT 0x665578
static int g_frontButtonLightColor = 0;
// GLOBAL: XVT 0x665580
static uint8_t g_buttonHoverState[256] = { 0 };

// FUNCTION: XVT 0x4DA650
int FrontendButton_HandleTextButton(RECT* rect, const char* text, int fontSize, int normalColor,
									int hoverSlot, const char* clickSoundName) {
	int cursorX;
	int cursorY;

	FrontendCursor_GetPos(&cursorX, &cursorY);
	if (FrontendDraw_PointInRect(rect, cursorX, cursorY)) {
		if (FrontendMouse_GetLeftDown() != 0 || FrontendMouse_GetRightDown() != 0) {
			FrontendButton_DrawTextButtonState(rect, text, fontSize, normalColor, 1);
			if (g_buttonHoverState[hoverSlot] == 0 && g_gameConfig.sfxDatapadEnabled != 0) {
				FrontendSound_PlayUISound(clickSoundName, 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
			}
			g_buttonHoverState[hoverSlot] = 1;
		} else {
			FrontendButton_DrawTextButtonState(rect, text, fontSize, normalColor, 0);
			g_buttonHoverState[hoverSlot] = 0;
		}
		if (FrontendMouse_GetLeftClick() != 0) {
			return 1;
		}
		if (FrontendMouse_GetRightClick() != 0) {
			return 2;
		}
	} else {
		g_buttonHoverState[hoverSlot] = 0;
		FrontendButton_DrawTextButtonState(rect, text, fontSize, normalColor, 0);
	}
	return 0;
}

// FUNCTION: XVT 0x4DA780
int FrontendButton_DrawSpriteHitTest(RECT* rect, const char* normalSprite, const char* pressedSprite,
									 const char* tooltipText, int fontSize, int textColor, int hoverSlot,
									 const char* hoverSound) {
	int cursorX;
	int cursorY;

	FrontendCursor_GetPos(&cursorX, &cursorY);
	if (FrontendDraw_PointInRect(rect, cursorX, cursorY)) {
		if (FrontendMouse_GetLeftClick() != 0 || FrontendMouse_GetRightClick() != 0) {
			FrontendButton_UsePressedOverlayStyle();
			FrontendButton_DrawSpriteAndTooltip(rect, pressedSprite, tooltipText, fontSize, textColor);
			return 1;
		}
		if (FrontendMouse_GetLeftDown() != 0 || FrontendMouse_GetRightDown() != 0) {
			FrontendButton_UsePressedOverlayStyle();
			FrontendButton_DrawSpriteAndTooltip(rect, pressedSprite, tooltipText, fontSize, textColor);
			if (g_buttonHoverState[hoverSlot] == 0 && g_gameConfig.sfxDatapadEnabled != 0) {
				FrontendSound_PlayUISound(hoverSound, 1, 0, 255, 12 * g_gameConfig.sfxDatapadVolume, 63);
			}
			g_buttonHoverState[hoverSlot] = 1;
			return 0;
		}
		FrontendButton_DrawSpriteAndTooltip(rect, normalSprite, tooltipText, fontSize, textColor);
		g_buttonHoverState[hoverSlot] = 0;
		return 0;
	}

	g_buttonHoverState[hoverSlot] = 0;
	FrontendButton_DrawSpriteAndTooltip(rect, normalSprite, tooltipText, fontSize, textColor);
	return 0;
}

// FUNCTION: XVT 0x4DA8E0
int FrontendButton_DrawTextButtonState(RECT* rect, const char* text, int fontSize, int normalColor,
									   char state) {
	RECT innerRect;
	int textColor;

	(void)normalColor;

	if (!g_frontButtonColorsInitialized) {
		g_frontButtonColorsInitialized = 1;
		g_frontButtonDarkColor = FrontendDisplay_PackRGB(5, 0x63, 0x6D);
		g_frontButtonLightColor = FrontendDisplay_PackRGB(0x63, 0xE7, 0xF7);
	}

	if (state) {
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

// FUNCTION: XVT 0x4DAA20
void FrontendButton_DrawSpriteAndTooltip(RECT* rect, const char* spriteName, const char* tooltipText,
										 int fontSize, int textColor) {
	int cursorX;
	int cursorY;
	int cursorWidth;
	int cursorHeight;
	int textWidth;
	RECT tooltipRect;

	(void)textColor;

	FrontImage_DrawSprite(spriteName, 0, 0);
	if (g_frontButtonRectGrayColorInitialized == 0) {
		g_frontButtonRectGrayColorInitialized = 1;
		g_frontButtonRectGrayColor = FrontendDisplay_PackRGB(0x60, 0x60, 0x60);
	}

	FrontendCursor_GetPos(&cursorX, &cursorY);
	if (g_buttonOverlayTextEnabled != 0)
		FrontendButton_DrawOverlayText(rect, g_buttonOverlayText);
	if (tooltipText == NULL || !FrontendDraw_PointInRect(rect, cursorX, cursorY))
		return;

	textWidth = FrontendText_MeasureWidth(tooltipText, fontSize);
	FrontendCursor_GetDimensions(&cursorWidth, &cursorHeight);
	cursorX += cursorWidth;
	cursorY += cursorHeight;
	if (textWidth + cursorX + 5 >= 640)
		cursorX = 634 - textWidth;
	if ((uint32_t)(fontSize + cursorY + 5) >= 480)
		cursorY = 474 - fontSize;

	FrontendDraw_RectAssign(&tooltipRect, cursorX, cursorY, textWidth + cursorX + 5, fontSize + cursorY + 3);
	FrontendDraw_Rect(&tooltipRect, 0, 0, 0xFFFF, 1);
	FrontendDraw_RectOutline(&tooltipRect, 0, 0, g_frontButtonRectGrayColor);
	FrontendText_DrawCentered(fontSize, tooltipText, &tooltipRect, 0);
}

// FUNCTION: XVT 0x4DABA0
void FrontendButton_DrawEightSlotNavigationState(const FrontendNavigationSlotState* slotStates) {
	FrontendNavigationSlotState states[8];
	int index;
	FrontendNavigationSlotState savedState;
	FrontendNavigationSlotState* state;

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
				sprintf(g_frontendScratchBuffer, "%ulita%u", index + 1, states[index - 1]);
				FrontImage_DrawSprite(g_frontendScratchBuffer, 0, 0);
			}
			if (state != &states[7] && state != &states[5]) {
				sprintf(g_frontendScratchBuffer, "%ulitb%u", index + 1, state[1]);
				FrontImage_DrawSprite(g_frontendScratchBuffer, 0, 0);
			}
		}
		++state;
		++index;
	} while (state < states + 8);
}

// FUNCTION: XVT 0x4DACA0
int FrontendButton_DrawOverlayText(RECT* rect, const char* str) {
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
		result = FrontendText_DrawCentered(10, str, rect, g_colorPaleCyan);
	}

	g_buttonOverlayPressedStyle = 0;
	return result;
}

// FUNCTION: XVT 0x4DAD40
void FrontendButton_EnableOverlayText(void) {
	g_buttonOverlayTextEnabled = 1;
	g_buttonOverlayPressedStyle = 0;
}

// FUNCTION: XVT 0x4DAD60
void FrontendButton_DisableOverlayText(void) { g_buttonOverlayTextEnabled = 0; }

// FUNCTION: XVT 0x4DAD70
const char* FrontendButton_SetOverlayText(const char* text) { return g_buttonOverlayText = text; }

// FUNCTION: XVT 0x4DAD80
void FrontendButton_UsePressedOverlayStyle(void) { g_buttonOverlayPressedStyle = 1; }

// FUNCTION: XVT 0x4DAD90
int FrontendButton_IsOverlayTextEnabled(void) { return g_buttonOverlayTextEnabled; }
