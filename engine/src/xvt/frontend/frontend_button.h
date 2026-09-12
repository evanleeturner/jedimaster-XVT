#ifndef XVT_FRONTEND_FRONTEND_BUTTON_H
#define XVT_FRONTEND_FRONTEND_BUTTON_H

#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FrontendNavigationSlotState {
	FRONTEND_NAVIGATION_SLOT_INACTIVE = 0,
	FRONTEND_NAVIGATION_SLOT_ACTIVE = 1,
	FRONTEND_NAVIGATION_SLOT_SELECTED = 2,
} FrontendNavigationSlotState;

extern const char* g_buttonOverlayText;

int FrontendButton_HandleTextButton(RECT* rect, const char* text, int fontSize, int normalColor,
									int hoverSlot, const char* clickSoundName);
int FrontendButton_DrawSpriteHitTest(RECT* rect, const char* normalSprite, const char* pressedSprite,
									 const char* tooltipText, int fontSize, int textColor, int hoverSlot,
									 const char* hoverSound);
int FrontendButton_DrawTextButtonState(RECT* rect, const char* text, int fontSize, int normalColor,
									   char state);
void FrontendButton_DrawSpriteAndTooltip(RECT* rect, const char* spriteName, const char* tooltipText,
										 int fontSize, int textColor);
void FrontendButton_DrawEightSlotNavigationState(const FrontendNavigationSlotState* slotStates);
int FrontendButton_DrawOverlayText(RECT* rect, const char* str);
void FrontendButton_EnableOverlayText(void);
void FrontendButton_DisableOverlayText(void);
const char* FrontendButton_SetOverlayText(const char* text);
void FrontendButton_UsePressedOverlayStyle(void);
int FrontendButton_IsOverlayTextEnabled(void);

#ifdef __cplusplus
}
#endif

#endif
