#ifndef XVT_FLIGHT_HUD_FLIGHT_TEXT_H
#define XVT_FLIGHT_HUD_FLIGHT_TEXT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int16_t g_flightWordWrapEnabled;
extern int16_t g_flightClearLineBgEnabled;
extern int16_t g_flightTextReservedState91079E;
extern int16_t g_flightCursorY;
extern int16_t g_flightCursorX;
extern uint8_t g_flightTextColorIndex;
extern uint8_t g_flightTextBgColor;
extern uint8_t g_flightTextShadowColor;
extern uint8_t g_flightTextShadowEnabled;
extern uint8_t g_flightFontTier;
extern uint8_t g_flightFontHasLowercase;
extern uint8_t g_flightFontLineHeight;
extern uint8_t g_flightFontHalfHeight;
extern uint8_t* g_flightFontGlyphTableSw;
extern uint16_t g_flightFontGlyphStrideSw;
extern uint8_t* g_flightFontSmallSw;
extern uint8_t* g_flightFontMediumSw;
extern uint8_t* g_flightFontMicroSw;
extern const uint16_t g_flightTextDecimalDivisors[8];
extern const uint8_t g_flightCharToColorLut[32];
extern int16_t g_flightClipTop;
extern int16_t g_flightClipBottom;
extern int16_t g_flightClipLeft;
extern int16_t g_flightClipRight;
extern char g_flightTextScratchBuffer[256];

void FlightText_DrawNarrowGlyph8bpp(uint8_t ch);
void FlightText_DrawSoftwareGlyph8bpp(uint8_t ch);
void FlightText_ClearRemainingLineBackground8bpp(void);
int16_t FlightText_GetWrapHeightForString(const char* str);
void FlightText_DrawDecimalNumber(uint16_t value, unsigned int width, unsigned int minDigits);
uint16_t FlightText_MeasureStringWidth(const char* str);
void FlightText_DrawNarrowGlyph(uint8_t ch);
void FlightText_DrawSoftwareGlyph(uint8_t ch);
void FlightText_ClearLineRemainder(void);
void FlightText_SetCursor(int x, int y);
void FlightText_SetClipRect(int16_t left, int16_t top, int16_t right, int16_t bottom);
void FlightText_SetColor(unsigned int charOrIndex);
void FlightText_SetBackgroundColor(unsigned int charOrIndex);
void FlightText_SetShadowColor(unsigned int charOrIndex);
void FlightText_SetWordWrap(int16_t enabled);
void FlightText_SetClearLineBackground(int16_t enabled);
void FlightText_SetFontTier(uint8_t tier);
void FlightText_SetScratch(const char* text);
void FlightText_AppendScratchString(const char* text);
void FlightText_AppendScratchChar(uint8_t ch);
uint16_t FlightText_FormatScratchInt(int value);
void FlightText_DrawString(const char* str);
void FlightText_DrawStringCentered(const char* str);
void FlightText_DrawStringRightAligned(const char* str);

#ifdef __cplusplus
}
#endif

#endif
