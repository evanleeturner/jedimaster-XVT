#include "xvt/flight/hud/flight_text.h"
#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/cockpit_pages.h"
#endif

#include "xvt/flight/flight_surface.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"

#include <string.h>

// GLOBAL: XVT 0x9CD270
int16_t g_flightWordWrapEnabled;
// GLOBAL: XVT 0x9FE7E4
int16_t g_flightClearLineBgEnabled;
// GLOBAL: XVT 0xA07C64
int16_t g_flightTextReservedState91079E = 0;
// GLOBAL: XVT 0xA08102
int16_t g_flightCursorY = 0;
// GLOBAL: XVT 0xA08108
int16_t g_flightCursorX = 0;
// GLOBAL: XVT 0x9A1ED0
char g_flightTextScratchBuffer[256];
// GLOBAL: XVT 0x9A807A
uint8_t g_flightTextColorIndex;
// GLOBAL: XVT 0xA08100
uint8_t g_flightTextBgColor;
// GLOBAL: XVT 0x9E8F52
uint8_t g_flightTextShadowColor;
// GLOBAL: XVT 0x9D80C8
uint8_t g_flightFontTier = 0;
// GLOBAL: XVT 0x9A1FF4
uint8_t g_flightFontHasLowercase = 0;
// GLOBAL: XVT 0x9A20AE
uint8_t g_flightFontLineHeight = 0;
// GLOBAL: XVT 0x9ED232
uint8_t g_flightFontHalfHeight = 0;
// GLOBAL: XVT 0x9D7674
uint8_t* g_flightFontGlyphTableSw = 0;
// GLOBAL: XVT 0x9D8C02
uint16_t g_flightFontGlyphStrideSw = 0;
// GLOBAL: XVT 0x9A7800
uint8_t* g_flightFontSmallSw = 0;
// GLOBAL: XVT 0x9E965C
uint8_t* g_flightFontMediumSw = 0;
// GLOBAL: XVT 0xA07CC0
uint8_t* g_flightFontMicroSw = 0;
// GLOBAL: XVT 0x9A6FE0
int16_t g_flightClipLeft = 0;
// GLOBAL: XVT 0x9D8C00
int16_t g_flightClipRight = 0;
// GLOBAL: XVT 0xA07CE4
int16_t g_flightClipBottom = 0;
// GLOBAL: XVT 0xA0813C
int16_t g_flightClipTop = 0;
// GLOBAL: XVT 0x524080
const uint8_t g_flightCharToColorLut[32] = {
	0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
	0x3c, 0x3d, 0x3e, 0x3f, 0xd5, 0xd5, 0xd4, 0xd3, 0x2c, 0x2d, 0x2e, 0x2f, 0x2c, 0x2d, 0x2e, 0x2f,
};
// GLOBAL: XVT 0x520EB0
const uint16_t g_flightTextDecimalDivisors[8] = { 1, 1, 10, 100, 1000, 10000, 0, 0 };
// GLOBAL: XVT 0x9EC464
uint8_t g_flightTextShadowEnabled = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40F050
void FlightText_DrawNarrowGlyph8bpp(uint8_t ch) {
	uint16_t glyphAdvance;
	int lineHeight;
	int glyphWidth;
	uint8_t glyphHeight;
	uint16_t wrapLineHeight;
	const uint8_t* rowData;
	int lineOffset;
	uint8_t* destination;
	int line;
	uint8_t normalizedChar;
	int drawX;
	int pixelCount;
	int directFramebuffer;
	uint8_t glyphBits;
	uint8_t shadowBits;
	uint8_t paletteIndex;
	uint8_t* rowStart;
#ifndef XVT_MODERN
	unsigned int pixelOffset;
	unsigned int page;
	int clippedBottom;
#endif

	if (ch == '\n') {
		if (g_flightClearLineBgEnabled != 0)
			FlightText_ClearRemainingLineBackground8bpp();
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += g_flightFontLineHeight;
		return;
	}
	if (ch < ' ')
		return;

	normalizedChar = ch;
	if (g_flightFontTier != 0 && g_flightFontHasLowercase == 0 && ch >= 'a' && ch <= 'z')
		normalizedChar = ch - ('a' - 'A');
	rowData = &g_flightFontGlyphTableSw[g_flightFontGlyphStrideSw * (uint8_t)(normalizedChar - ' ')];
	glyphAdvance = *rowData++;
	glyphWidth = glyphAdvance;
	glyphHeight = *rowData++;
	if (glyphWidth + g_flightCursorX >= g_flightClipRight && g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0)
			FlightText_ClearRemainingLineBackground8bpp();
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += glyphHeight + 2;
	}

#ifdef XVT_MODERN
	XvtCockpitPages_RecordGlyph(normalizedChar, glyphAdvance, glyphHeight, 1);
	XvtCockpitMessages_RecordGlyph(normalizedChar, glyphAdvance, glyphHeight, 1);
#endif
	shadowBits = 0;
	directFramebuffer = 0;
	line = g_flightCursorY;
	if (line < g_flightClipTop)
		line = g_flightClipTop;
#ifdef XVT_MODERN
	/* Fully clipped glyphs still advance the cursor, without looking up a row. */
	lineOffset = line < g_flightClipBottom ? FlightSw_GetLineBufferAddr(line) : 0;
#else
	lineOffset = FlightSw_GetLineBufferAddr(line);
#endif
#ifndef XVT_MODERN
	if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		g_flightSwFramebufferBase == g_swFramebufferBase) {
		page = lineOffset / g_swFramebufferClearChunkSize;
		lineOffset %= g_swFramebufferClearChunkSize;
		RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow, (uint16_t)page);
		clippedBottom = g_flightCursorY + glyphHeight + 1;
		if (clippedBottom > g_flightClipBottom)
			clippedBottom = g_flightClipBottom;
		if (lineOffset + g_surfacePitch * (clippedBottom - line) > 0xFFFF)
			directFramebuffer = 1;
	}
#endif
	if (g_flightSwFramebufferBase != g_swFramebufferBase)
		directFramebuffer = 1;

	if (directFramebuffer == 0) {
		destination = g_flightSwFramebufferBase + lineOffset;
		wrapLineHeight = glyphHeight;
		line = g_flightCursorY;
		lineHeight = wrapLineHeight;
		if (g_flightCursorY + lineHeight > line) {
			do {
				pixelCount = glyphWidth;
				glyphBits = *rowData;
				if (g_flightTextShadowEnabled != 0)
					++pixelCount;
				drawX = g_flightCursorX;
				if (drawX < g_flightClipLeft) {
					if (g_flightClipLeft - drawX >= pixelCount)
						break;
					pixelCount = drawX + pixelCount - g_flightClipLeft;
					glyphBits <<= g_flightClipLeft - drawX;
					drawX = g_flightClipLeft;
				}
				if (g_flightClipBottom <= line)
					break;
				if (g_flightClipTop <= line) {
					if (drawX + pixelCount > g_flightClipRight) {
						pixelCount = g_flightClipRight - drawX;
						if (pixelCount <= 0)
							break;
					}
					rowStart = &destination[drawX];
					while (pixelCount-- != 0) {
						if ((glyphBits & 0x80u) != 0) {
							paletteIndex = g_flightTextColorIndex;
						} else if (g_flightTextShadowEnabled != 0 && (shadowBits & 0x80u) != 0) {
							paletteIndex = g_flightTextShadowColor;
						} else {
							paletteIndex = g_flightTextBgColor;
						}
						*rowStart++ = paletteIndex;
						glyphBits <<= 1;
						shadowBits <<= 1;
					}
					destination += FlightSw_GetLinePitch();
				}
				shadowBits = *rowData;
				rowData += 2;
				++line;
				shadowBits >>= 1;
			} while (lineHeight + g_flightCursorY > line);
		}
	} else {
		line = g_flightCursorY;
		wrapLineHeight = glyphHeight;
		lineHeight = wrapLineHeight;
		if (g_flightCursorY + lineHeight > line) {
			do {
				pixelCount = glyphWidth;
				glyphBits = *rowData;
				if (g_flightTextShadowEnabled != 0)
					++pixelCount;
				drawX = g_flightCursorX;
				if (drawX < g_flightClipLeft) {
					if (g_flightClipLeft - drawX >= pixelCount)
						break;
					pixelCount = drawX + pixelCount - g_flightClipLeft;
					glyphBits <<= g_flightClipLeft - drawX;
					drawX = g_flightClipLeft;
				}
				if (g_flightClipBottom <= line)
					break;
				if (g_flightClipTop <= line) {
					if (drawX + pixelCount > g_flightClipRight) {
						pixelCount = g_flightClipRight - drawX;
						if (pixelCount <= 0)
							break;
					}
#ifdef XVT_MODERN
					rowStart = &g_flightSwFramebufferBase[FlightSw_GetLineBufferAddr(line) + drawX];
#else
					pixelOffset = drawX + FlightSw_GetLineBufferAddr(line);
					if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
						g_flightSwFramebufferBase == g_swFramebufferBase) {
						page = pixelOffset / g_swFramebufferClearChunkSize;
						pixelOffset %= g_swFramebufferClearChunkSize;
						RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow, (uint16_t)page);
					}
					rowStart = &g_flightSwFramebufferBase[pixelOffset];
#endif
					while (pixelCount-- != 0) {
						if ((glyphBits & 0x80u) != 0) {
							paletteIndex = g_flightTextColorIndex;
						} else if (g_flightTextShadowEnabled != 0 && (shadowBits & 0x80u) != 0) {
							paletteIndex = g_flightTextShadowColor;
						} else {
							paletteIndex = g_flightTextBgColor;
						}
						*rowStart++ = paletteIndex;
						glyphBits <<= 1;
						shadowBits <<= 1;
					}
				}
				shadowBits = *rowData;
				rowData += 2;
				++line;
				shadowBits >>= 1;
			} while (lineHeight + g_flightCursorY > line);
		}
	}

	g_flightCursorX += glyphAdvance;
	if (g_flightClipRight <= g_flightCursorX && g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0)
			FlightText_ClearRemainingLineBackground8bpp();
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += wrapLineHeight + 2;
	}
}

// FUNCTION: XVT 0x40F520
void FlightText_DrawSoftwareGlyph8bpp(uint8_t ch) {
	uint16_t glyphAdvance;
	int lineHeight;
	int glyphWidth;
	uint8_t glyphHeight;
	uint16_t wrapLineHeight;
	const uint8_t* rowData;
	int lineOffset;
	uint8_t* destination;
	int line;
	uint8_t normalizedChar;
	int drawX;
	int pixelCount;
	int directFramebuffer;
	uint32_t glyphBits;
	uint32_t shadowBits;
	uint8_t paletteIndex;
	uint8_t* rowStart;
#ifndef XVT_MODERN
	unsigned int pixelOffset;
	unsigned int page;
	int clippedBottom;
#endif

	if (ch == '\n') {
		if (g_flightClearLineBgEnabled != 0)
			FlightText_ClearRemainingLineBackground8bpp();
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += g_flightFontLineHeight + 1;
		return;
	}
	if (ch < ' ')
		return;

	normalizedChar = ch;
	if (g_flightFontTier != 0 && g_flightFontHasLowercase == 0 && ch >= 'a' && ch <= 'z')
		normalizedChar = ch - ('a' - 'A');
	rowData = &g_flightFontGlyphTableSw[g_flightFontGlyphStrideSw * (uint8_t)(normalizedChar - ' ')];
	glyphAdvance = *rowData++;
	glyphWidth = glyphAdvance;
	glyphHeight = *rowData++;
	if (glyphWidth + g_flightCursorX >= g_flightClipRight && g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0)
			FlightText_ClearRemainingLineBackground8bpp();
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += glyphHeight + 2;
	}

	directFramebuffer = 0;
#ifdef XVT_MODERN
	XvtCockpitPages_RecordGlyph(normalizedChar, glyphAdvance, glyphHeight, 0);
	XvtCockpitMessages_RecordGlyph(normalizedChar, glyphAdvance, glyphHeight, 0);
#endif
	shadowBits = 0;
	line = g_flightCursorY;
	if (line < g_flightClipTop)
		line = g_flightClipTop;
#ifdef XVT_MODERN
	/* Fully clipped glyphs still advance the cursor, without looking up a row. */
	lineOffset = line < g_flightClipBottom ? FlightSw_GetLineBufferAddr(line) : 0;
#else
	lineOffset = FlightSw_GetLineBufferAddr(line);
#endif
#ifndef XVT_MODERN
	if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		g_flightSwFramebufferBase == g_swFramebufferBase) {
		page = lineOffset / g_swFramebufferClearChunkSize;
		lineOffset %= g_swFramebufferClearChunkSize;
		RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow, (uint16_t)page);
		clippedBottom = g_flightCursorY + glyphHeight + 1;
		if (clippedBottom > g_flightClipBottom)
			clippedBottom = g_flightClipBottom;
		if (lineOffset + g_surfacePitch * (clippedBottom - line) > 0xFFFF)
			directFramebuffer = 1;
	}
#endif
	if (g_flightSwFramebufferBase != g_swFramebufferBase)
		directFramebuffer = 1;

	if (directFramebuffer == 0) {
		destination = g_flightSwFramebufferBase + lineOffset;
		wrapLineHeight = glyphHeight;
		line = g_flightCursorY;
		lineHeight = wrapLineHeight;
		if (g_flightCursorY + lineHeight > line) {
			do {
				pixelCount = glyphWidth;
#ifdef XVT_MODERN
				memcpy(&glyphBits, rowData, sizeof(glyphBits));
#else
				glyphBits = *(const uint32_t*)rowData;
#endif
				if (g_flightTextShadowEnabled != 0)
					++pixelCount;
				drawX = g_flightCursorX;
				if (drawX < g_flightClipLeft) {
					if (g_flightClipLeft - drawX >= pixelCount)
						break;
					pixelCount = drawX + pixelCount - g_flightClipLeft;
					glyphBits <<= g_flightClipLeft - drawX;
					drawX = g_flightClipLeft;
				}
				if (g_flightClipBottom <= line)
					break;
				if (g_flightClipTop <= line) {
					if (drawX + pixelCount > g_flightClipRight) {
						pixelCount = g_flightClipRight - drawX;
						if (pixelCount <= 0)
							break;
					}
					rowStart = &destination[drawX];
					while (pixelCount-- != 0) {
						if ((glyphBits & 0x80000000u) != 0) {
							paletteIndex = g_flightTextColorIndex;
						} else if (g_flightTextShadowEnabled != 0 && (shadowBits & 0x80000000u) != 0) {
							paletteIndex = g_flightTextShadowColor;
						} else {
							paletteIndex = g_flightTextBgColor;
						}
						*rowStart++ = paletteIndex;
						glyphBits <<= 1;
						shadowBits <<= 1;
					}
					destination += FlightSw_GetLinePitch();
				}
#ifdef XVT_MODERN
				memcpy(&shadowBits, rowData, sizeof(shadowBits));
#else
				shadowBits = *(const uint32_t*)rowData;
#endif
				rowData += 8;
				++line;
				shadowBits >>= 1;
			} while (lineHeight + g_flightCursorY > line);
		}
	} else {
		line = g_flightCursorY;
		wrapLineHeight = glyphHeight;
		lineHeight = wrapLineHeight;
		if (g_flightCursorY + lineHeight > line) {
			do {
				pixelCount = glyphWidth;
#ifdef XVT_MODERN
				memcpy(&glyphBits, rowData, sizeof(glyphBits));
#else
				glyphBits = *(const uint32_t*)rowData;
#endif
				if (g_flightTextShadowEnabled != 0)
					++pixelCount;
				drawX = g_flightCursorX;
				if (drawX < g_flightClipLeft) {
					if (g_flightClipLeft - drawX >= pixelCount)
						break;
					pixelCount = drawX + pixelCount - g_flightClipLeft;
					glyphBits <<= g_flightClipLeft - drawX;
					drawX = g_flightClipLeft;
				}
				if (g_flightClipBottom <= line)
					break;
				if (g_flightClipTop <= line) {
					if (drawX + pixelCount > g_flightClipRight) {
						pixelCount = g_flightClipRight - drawX;
						if (pixelCount <= 0)
							break;
					}
#ifdef XVT_MODERN
					rowStart = &g_flightSwFramebufferBase[FlightSw_GetLineBufferAddr(line) + drawX];
#else
					pixelOffset = drawX + FlightSw_GetLineBufferAddr(line);
					if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
						g_flightSwFramebufferBase == g_swFramebufferBase) {
						page = pixelOffset / g_swFramebufferClearChunkSize;
						pixelOffset %= g_swFramebufferClearChunkSize;
						RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow, (uint16_t)page);
					}
					rowStart = &g_flightSwFramebufferBase[pixelOffset];
#endif
					while (pixelCount-- != 0) {
						if ((glyphBits & 0x80000000u) != 0) {
							paletteIndex = g_flightTextColorIndex;
						} else if (g_flightTextShadowEnabled != 0 && (shadowBits & 0x80000000u) != 0) {
							paletteIndex = g_flightTextShadowColor;
						} else {
							paletteIndex = g_flightTextBgColor;
						}
						*rowStart++ = paletteIndex;
						glyphBits <<= 1;
						shadowBits <<= 1;
					}
				}
#ifdef XVT_MODERN
				memcpy(&shadowBits, rowData, sizeof(shadowBits));
#else
				shadowBits = *(const uint32_t*)rowData;
#endif
				rowData += 8;
				++line;
				shadowBits >>= 1;
			} while (lineHeight + g_flightCursorY > line);
		}
	}

	g_flightCursorX += glyphAdvance;
	if (g_flightClipRight <= g_flightCursorX && g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0)
			FlightText_ClearRemainingLineBackground8bpp();
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += wrapLineHeight + 2;
	}
}

// FUNCTION: XVT 0x410110
void FlightText_ClearRemainingLineBackground8bpp(void) {

	uint16_t bottom;

	if (g_flightClipRight <= g_flightCursorX) {
		return;
	}
	g_flightFillRectRight8bpp = g_flightClipRight;
	g_flightFillRectLeft8bpp = g_flightCursorX;
	g_flightFillRectTop8bpp = g_flightCursorY;
	bottom = g_flightFontLineHeight + g_flightCursorY;
	if (g_flightFillRectLeft8bpp < g_flightClipLeft) {
		g_flightFillRectLeft8bpp = g_flightClipLeft;
	}
	if (g_flightClipTop > g_flightFillRectTop8bpp) {
		g_flightFillRectTop8bpp = g_flightClipTop;
	}
	g_flightFillRectBottom8bpp = bottom;
	if (bottom > g_flightClipBottom) {
		bottom = g_flightClipBottom;
		g_flightFillRectBottom8bpp = bottom;
	}
	if (g_flightFillRectTop8bpp < g_flightFillRectBottom8bpp) {
		FlightSw_FillRectOrBorder8bpp(0);
	}
}

// FUNCTION: XVT 0x415C40
int16_t FlightText_GetWrapHeightForString(const char* str) {
	if (str == 0) {
		return 0;
	}
	if (g_flightCursorX + FlightText_MeasureStringWidth(str) > g_flightClipRight - 11) {
		return g_flightFontLineHeight + 1;
	}

	return 0;
}

// FUNCTION: XVT 0x4277F0
void FlightText_DrawDecimalNumber(uint16_t value, unsigned int width, unsigned int minDigits) {
	uint16_t savedShadow;
	uint16_t savedColor;
	unsigned int digitIndex;
	int16_t started;
	uint16_t divisor;
	uint16_t digit;

	if (value == UINT16_MAX) {
		savedShadow = g_flightTextShadowEnabled;
		savedColor = g_flightTextColorIndex;
		g_flightTextShadowEnabled = 0;
		FlightText_SetColor('@');
		for (digitIndex = width; digitIndex != 0; --digitIndex) {
			g_flightDrawCharFn('0');
		}
		g_flightTextShadowEnabled = savedShadow;
		g_flightTextColorIndex = savedColor;
		return;
	}

	started = 0;
	for (digitIndex = width; digitIndex != 0; --digitIndex) {
		divisor = g_flightTextDecimalDivisors[digitIndex];
		digit = value / divisor;
		divisor *= digit;
		value -= divisor;
		if (started != 0 || digitIndex <= minDigits || digit != 0) {
			started = 1;
			if (digit > 9) {
				digit = 9;
			}
			digit += '0';
		} else {
			digit = ' ';
		}
		g_flightDrawCharFn((uint8_t)digit);
	}
}

// FUNCTION: XVT 0x4278C0
uint16_t FlightText_MeasureStringWidth(const char* str) {
	uint16_t totalWidth;
	uint8_t ch;
	const char* cursor;

	totalWidth = 0;
	ch = (uint8_t)*str;
	cursor = str + 1;
	while (ch != '\0') {
		if (ch == '\n') {
			break;
		}
		if (ch >= 0x20u) {
			if (ch == 0xfeu) {
				++cursor;
			} else {
				if (g_flightFontTier != 0 && g_flightFontHasLowercase == 0 && ch >= 'a' && ch <= 'z') {
					ch -= 32;
				}
				totalWidth += g_flightFontGlyphTableSw[g_flightFontGlyphStrideSw * (uint8_t)(ch - 32)];
			}
		}
		ch = (uint8_t)*cursor++;
	}

	return totalWidth;
}

// FUNCTION: XVT 0x449F70
void FlightText_DrawNarrowGlyph(uint8_t ch) {
	uint8_t normalizedChar;
	uint8_t glyphHeight;
	uint8_t glyphBits;
	uint8_t shadowBits;
	const uint8_t* glyphData;
	const uint8_t* rowData;
	int16_t glyphAdvance;
	int16_t wrapLineHeight;
	int lineHeight;
	int glyphWidth;
	int line;
	int drawX;
	int pixelCount;
	int pixelsRemaining;
	unsigned int pixelOffset;
	unsigned int paletteIndex;
	uint16_t* destination;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	if (ch == '\n') {
		if (g_flightClearLineBgEnabled != 0)
			FlightText_ClearLineRemainder();
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += g_flightFontLineHeight;
		return;
	}
	if (ch < ' ')
		return;

	normalizedChar = ch;
	if (g_flightFontTier != 0 && g_flightFontHasLowercase == 0 && ch >= 'a' && ch <= 'z')
		normalizedChar = ch - ('a' - 'A');
	glyphData = &g_flightFontGlyphTableSw[g_flightFontGlyphStrideSw * (uint8_t)(normalizedChar - ' ')];
	glyphAdvance = *glyphData++;
	glyphHeight = *glyphData++;
	rowData = glyphData;
	glyphWidth = glyphAdvance;
	if (glyphWidth + g_flightCursorX >= g_flightClipRight && g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0)
			FlightText_ClearLineRemainder();
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += glyphHeight + 2;
	}

#ifdef XVT_MODERN
	XvtCockpitPages_RecordGlyph(normalizedChar, glyphAdvance, glyphHeight, 1);
	XvtCockpitMessages_RecordGlyph(normalizedChar, glyphAdvance, glyphHeight, 1);
#endif
	shadowBits = 0;
	line = g_flightCursorY;
	wrapLineHeight = glyphHeight;
	lineHeight = glyphHeight;
	if (lineHeight + g_flightCursorY > line) {
		do {
			pixelCount = glyphWidth;
			glyphBits = *rowData;
			if (g_flightTextShadowEnabled != 0)
				++pixelCount;
			drawX = g_flightCursorX;
			if (g_flightCursorX < g_flightClipLeft) {
				if (g_flightClipLeft - g_flightCursorX >= pixelCount)
					break;
				pixelCount = g_flightCursorX + pixelCount - g_flightClipLeft;
				drawX = g_flightClipLeft;
				glyphBits <<= g_flightClipLeft - g_flightCursorX;
			}
			if (g_flightClipBottom <= line)
				break;
			if (g_flightClipTop > line)
				pixelCount = 0;
			if (pixelCount + drawX > g_flightClipRight) {
				pixelCount = g_flightClipRight - drawX;
				if (pixelCount <= 0)
					break;
			}

#ifndef XVT_MODERN
			pixelOffset = FlightSw_GetLineBufferAddr(line) + 2 * drawX;
			if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
				g_flightSwFramebufferBase == g_swFramebufferBase) {
				page = pixelOffset / g_swFramebufferClearChunkSize;
				pixelOffset %= g_swFramebufferClearChunkSize;
				RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow, (uint16_t)page);
			}
			destination = &((uint16_t*)g_flightSwFramebufferBase)[pixelOffset / 2];
#endif
			pixelsRemaining = pixelCount;
			if (pixelsRemaining != 0) {
#ifdef XVT_MODERN
				/* The original looks up negative rows even when top clipping leaves
				 * no pixels. Keep row/shadow progression outside this drawing block. */
				pixelOffset = FlightSw_GetLineBufferAddr(line) + 2 * drawX;
				destination = &((uint16_t*)g_flightSwFramebufferBase)[pixelOffset / 2];
#endif
				--pixelsRemaining;
				do {
					if ((glyphBits & 0x80u) != 0) {
						paletteIndex = g_flightTextColorIndex;
					} else if (g_flightTextShadowEnabled != 0 && (shadowBits & 0x80u) != 0) {
						paletteIndex = g_flightTextShadowColor;
					} else {
						paletteIndex = g_flightTextBgColor;
					}
					*destination++ = g_flightTextPalette[paletteIndex];
					shadowBits <<= 1;
					glyphBits <<= 1;
				} while (pixelsRemaining-- != 0);
			}
			shadowBits = *rowData >> 1;
			rowData += 2;
			++line;
		} while (lineHeight + g_flightCursorY > line);
	}

	g_flightCursorX += glyphAdvance;
	if (g_flightClipRight <= g_flightCursorX && g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0)
			FlightText_ClearLineRemainder();
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += wrapLineHeight + 2;
	}
}

// FUNCTION: XVT 0x44A270
void FlightText_DrawSoftwareGlyph(uint8_t ch) {
	uint8_t normalizedChar;
	uint8_t glyphHeight;
	const uint8_t* glyphData;
#ifdef XVT_MODERN
	const uint8_t* rowData;
#else
	const uint32_t* rowData;
#endif
	int16_t glyphAdvance;
	int16_t wrapLineHeight;
	int glyphWidth;
	int lineHeight;
	int line;
	int drawX;
	int pixelCount;
	int pixelsRemaining;
	uint32_t glyphBits;
	uint32_t shadowBits;
	unsigned int pixelOffset;
	unsigned int paletteIndex;
	uint16_t* destination;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	if (ch == '\n') {
		if (g_flightClearLineBgEnabled != 0)
			FlightText_ClearLineRemainder();
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += g_flightFontLineHeight + 1;
		return;
	}
	if (ch < ' ')
		return;

	normalizedChar = ch;
	if (g_flightFontTier != 0 && g_flightFontHasLowercase == 0 && normalizedChar >= 'a' &&
		normalizedChar <= 'z') {
		normalizedChar -= 'a' - 'A';
	}
	glyphData = &g_flightFontGlyphTableSw[g_flightFontGlyphStrideSw * (uint8_t)(normalizedChar - ' ')];
	glyphAdvance = *glyphData++;
	glyphHeight = *glyphData++;
#ifdef XVT_MODERN
	rowData = glyphData;
#else
	rowData = (const uint32_t*)glyphData;
#endif
	glyphWidth = glyphAdvance;
	if (glyphWidth + g_flightCursorX >= g_flightClipRight && g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0)
			FlightText_ClearLineRemainder();
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += glyphHeight + 2;
	}

#ifdef XVT_MODERN
	XvtCockpitPages_RecordGlyph(normalizedChar, glyphAdvance, glyphHeight, 0);
	XvtCockpitMessages_RecordGlyph(normalizedChar, glyphAdvance, glyphHeight, 0);
#endif
	shadowBits = 0;
	wrapLineHeight = glyphHeight;
	line = g_flightCursorY;
	lineHeight = glyphHeight;
	if (g_flightCursorY + lineHeight > g_flightCursorY) {
		do {
			pixelCount = glyphWidth;
#ifdef XVT_MODERN
			memcpy(&glyphBits, rowData, sizeof(glyphBits));
#else
			glyphBits = *rowData;
#endif
			if (g_flightTextShadowEnabled != 0)
				++pixelCount;
			drawX = g_flightCursorX;
			if (g_flightCursorX < g_flightClipLeft) {
				if (g_flightClipLeft - g_flightCursorX >= pixelCount)
					break;
				pixelCount = g_flightCursorX + pixelCount - g_flightClipLeft;
				drawX = g_flightClipLeft;
				glyphBits <<= g_flightClipLeft - g_flightCursorX;
			}
			if (g_flightClipBottom <= line)
				break;
			if (g_flightClipTop > line)
				pixelCount = 0;
			if (pixelCount + drawX > g_flightClipRight) {
				pixelCount = g_flightClipRight - drawX;
				if (pixelCount <= 0)
					break;
			}

#ifndef XVT_MODERN
			pixelOffset = FlightSw_GetLineBufferAddr(line) + 2 * drawX;
			if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
				g_flightSwFramebufferBase == g_swFramebufferBase) {
				page = pixelOffset / g_swFramebufferClearChunkSize;
				pixelOffset %= g_swFramebufferClearChunkSize;
				RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow, (uint16_t)page);
			}
			destination = &((uint16_t*)g_flightSwFramebufferBase)[pixelOffset / 2];
#endif
			pixelsRemaining = pixelCount;
			if (pixelsRemaining != 0) {
#ifdef XVT_MODERN
				/* The original looks up negative rows even when top clipping leaves
				 * no pixels. Keep row/shadow progression outside this drawing block. */
				pixelOffset = FlightSw_GetLineBufferAddr(line) + 2 * drawX;
				destination = &((uint16_t*)g_flightSwFramebufferBase)[pixelOffset / 2];
#endif
				--pixelsRemaining;
				do {
					if ((glyphBits & 0x80000000u) != 0) {
						paletteIndex = g_flightTextColorIndex;
					} else if (g_flightTextShadowEnabled != 0 && (shadowBits & 0x80000000u) != 0) {
						paletteIndex = g_flightTextShadowColor;
					} else {
						paletteIndex = g_flightTextBgColor;
					}
					*destination++ = g_flightTextPalette[paletteIndex];
					glyphBits <<= 1;
					shadowBits <<= 1;
				} while (pixelsRemaining-- != 0);
			}
#ifdef XVT_MODERN
			memcpy(&shadowBits, rowData, sizeof(shadowBits));
			rowData += 8;
#else
			shadowBits = *rowData;
			rowData += 2;
#endif
			++line;
			shadowBits >>= 1;
		} while (lineHeight + g_flightCursorY > line);
	}

	g_flightCursorX += glyphAdvance;
	if (g_flightClipRight <= g_flightCursorX && g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0)
			FlightText_ClearLineRemainder();
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += wrapLineHeight + 2;
	}
}

// FUNCTION: XVT 0x44AA50
void FlightText_ClearLineRemainder(void) {

	uint16_t clippedTop;
	uint16_t clippedBottom;
	int16_t cursorX;
	int16_t cursorY;

	cursorX = g_flightCursorX;
	if (g_flightClipRight <= cursorX)
		return;

	cursorY = g_flightCursorY;
	clippedTop = cursorY;
	g_flightFillRectRight = g_flightClipRight;
	g_flightFillRectLeft = cursorX;
	clippedBottom = g_flightFontLineHeight + cursorY;
	if (g_flightClipLeft > (int)(uint16_t)cursorX)
		g_flightFillRectLeft = g_flightClipLeft;

	g_flightFillRectTop = cursorY;
	if (g_flightClipTop > (int)g_flightFillRectTop)
		clippedTop = g_flightClipTop;

	g_flightFillRectBottom = g_flightFontLineHeight + cursorY;
	if (g_flightClipBottom < (int)g_flightFillRectBottom)
		clippedBottom = g_flightClipBottom;
	g_flightFillRectBottom = clippedBottom;
	g_flightFillRectTop = clippedTop;
	if (clippedBottom > clippedTop)
		FlightSw_FillRectOrBorder(0);
}

// FUNCTION: XVT 0x4A9500
void FlightText_SetCursor(int x, int y) {
	g_flightCursorY = y;
	g_flightCursorX = x;
}

// FUNCTION: XVT 0x4A9520
void FlightText_SetClipRect(int16_t left, int16_t top, int16_t right, int16_t bottom) {
	if (top < 0) {
		top = 0;
	}
	if (left < 0) {
		left = 0;
	}
	if ((unsigned int)right > g_screenWidth) {
		right = (int16_t)g_screenWidth;
	}
	if ((unsigned int)bottom > g_screenHeight) {
		bottom = (int16_t)g_screenHeight;
	}

	g_flightClipTop = top;
	g_flightClipBottom = bottom;
	g_flightClipLeft = left;
	g_flightClipRight = right;
}

// FUNCTION: XVT 0x4A9590
void FlightText_SetColor(unsigned int charOrIndex) {
	if (charOrIndex >= 0x40u && charOrIndex != g_flightColorEscapeBypassChar) {
		g_flightTextColorIndex = g_flightCharToColorLut[charOrIndex - 0x40u];
	} else {
		g_flightTextColorIndex = (uint8_t)charOrIndex;
	}
}

// FUNCTION: XVT 0x4A95C0
void FlightText_SetBackgroundColor(unsigned int charOrIndex) {
	if (charOrIndex >= 0x40u && charOrIndex != g_flightColorEscapeBypassChar) {
		g_flightTextBgColor = g_flightCharToColorLut[charOrIndex - 0x40u];
	} else {
		g_flightTextBgColor = (uint8_t)charOrIndex;
	}
}

// FUNCTION: XVT 0x4A95F0
void FlightText_SetShadowColor(unsigned int charOrIndex) {
	if (charOrIndex >= 0x40u && charOrIndex != g_flightColorEscapeBypassChar) {
		g_flightTextShadowColor = g_flightCharToColorLut[charOrIndex - 0x40u];
	} else {
		g_flightTextShadowColor = (uint8_t)charOrIndex;
	}
}

// FUNCTION: XVT 0x4A9620
void FlightText_SetWordWrap(int16_t enabled) { g_flightWordWrapEnabled = enabled; }

// FUNCTION: XVT 0x4A9630
void FlightText_SetClearLineBackground(int16_t enabled) { g_flightClearLineBgEnabled = enabled; }

// FUNCTION: XVT 0x4A9640
void FlightText_SetFontTier(uint8_t tier) {
	g_flightFontTier = tier;
	switch (tier) {
		case 0:
			switch (g_flightResolutionMode) {
				case FLIGHT_RESOLUTION_320X240:
				case FLIGHT_RESOLUTION_480X360:
					g_flightFontLineHeight = 5;
					g_flightFontHasLowercase = 0;
					g_flightFontHalfHeight = 3;
					g_flightFontGlyphStrideSw = 42;
					g_flightFontGlyphTableSw = g_flightFontMicroSw;
					break;
				case FLIGHT_RESOLUTION_640X480:
					g_flightFontLineHeight = 8;
					g_flightFontHasLowercase = 1;
					g_flightFontHalfHeight = 4;
					g_flightFontGlyphStrideSw = 66;
					g_flightFontGlyphTableSw = g_flightFontSmallSw;
					break;
			}
			break;
		case 1:
			switch (g_flightResolutionMode) {
				case FLIGHT_RESOLUTION_320X240:
					g_flightFontLineHeight = 5;
					g_flightFontHasLowercase = 0;
					g_flightFontHalfHeight = 3;
					g_flightFontGlyphStrideSw = 42;
					g_flightFontGlyphTableSw = g_flightFontMicroSw;
					break;
				case FLIGHT_RESOLUTION_640X480:
					g_flightFontLineHeight = 10;
					g_flightFontHasLowercase = 1;
					g_flightFontHalfHeight = 5;
					g_flightFontGlyphStrideSw = 82;
					g_flightFontGlyphTableSw = g_flightFontMediumSw;
					break;
				case FLIGHT_RESOLUTION_480X360:
					g_flightFontLineHeight = 8;
					g_flightFontHalfHeight = 4;
					g_flightFontGlyphStrideSw = 66;
					g_flightFontGlyphTableSw = g_flightFontSmallSw;
					break;
			}
			break;
		case 2:
			switch (g_flightResolutionMode) {
				case FLIGHT_RESOLUTION_320X240:
					g_flightFontLineHeight = 5;
					g_flightFontHasLowercase = 0;
					g_flightFontHalfHeight = 3;
					g_flightFontGlyphStrideSw = 42;
					g_flightFontGlyphTableSw = g_flightFontMicroSw;
					break;
				case FLIGHT_RESOLUTION_640X480:
					g_flightFontLineHeight = 10;
					g_flightFontHasLowercase = 1;
					g_flightFontHalfHeight = 5;
					g_flightFontGlyphStrideSw = 82;
					g_flightFontGlyphTableSw = g_flightFontMediumSw;
					break;
				case FLIGHT_RESOLUTION_480X360:
					g_flightFontLineHeight = 8;
					g_flightFontHalfHeight = 4;
					g_flightFontGlyphStrideSw = 66;
					g_flightFontGlyphTableSw = g_flightFontSmallSw;
					break;
			}
			break;
	}
}

// FUNCTION: XVT 0x4A97F0
void FlightText_SetScratch(const char* text) {
	char* destination;

	destination = g_flightTextScratchBuffer;
	if (text != 0) {
		while (*text != '\0') {
			*destination++ = *text++;
		}
	}
	*destination = '\0';
}

// FUNCTION: XVT 0x4A9820
void FlightText_AppendScratchString(const char* text) {
	char* destination;

	destination = g_flightTextScratchBuffer;
	while (*destination != '\0') {
		destination++;
	}
	if (text != 0) {
		while (*text != '\0') {
			*destination++ = *text++;
		}
	}
	*destination = '\0';
}

// FUNCTION: XVT 0x4A9860
void FlightText_AppendScratchChar(uint8_t ch) {
	char* destination;

	destination = g_flightTextScratchBuffer;
	while (*destination != '\0') {
		destination++;
	}
	destination[0] = ch;
	destination[1] = '\0';
}

// FUNCTION: XVT 0x4A9890
uint16_t FlightText_FormatScratchInt(int value) {
	int magnitude;
	uint16_t digitCount;
	int expandedDigitCount;
	int remainingValue;
	int digitIndex;
	int16_t digit;
	int16_t isNegative;
	char* output;
	char* digitPosition;

	isNegative = 0;
	output = g_flightTextScratchBuffer;
	magnitude = value;
	if (magnitude < 0) {
		magnitude = -magnitude;
		g_flightTextScratchBuffer[0] = '-';
		output = &g_flightTextScratchBuffer[1];
		isNegative = 1;
	}
	if (magnitude != 0) {
		digitCount = 0;
		remainingValue = magnitude;
		if (magnitude > 0) {
			do {
				++digitCount;
				remainingValue /= 10;
			} while (remainingValue > 0);
		}
		digitIndex = 0;
		if (digitCount != 0) {
			expandedDigitCount = digitCount;
			do {
				digit = (int16_t)(magnitude % 10);
				magnitude /= 10;
				digitPosition = &output[-digitIndex++];
				digitPosition[expandedDigitCount - 1] = (char)(digit + '0');
			} while (digitIndex < expandedDigitCount);
		}
	} else {
		digitCount = 1;
		*output = '0';
	}
	output[digitCount] = '\0';
	if (isNegative != 0)
		++digitCount;
	return digitCount;
}

// FUNCTION: XVT 0x4A9960
void FlightText_DrawString(const char* str) {
	char wordBuffer[80];
	uint16_t wordLength;
	const char* wordScan;
	int nextWordWidth;

	if (*str == '\0') {
		return;
	}

	do {
		if ((uint8_t)*str == 0xfeu) {
			++str;
			FlightText_SetColor((uint8_t)*str);
		} else if ((uint8_t)*str < 0x10u) {
			FlightText_SetColor((uint8_t)*str);
		} else if ((uint8_t)*str == ' ' && g_flightWordWrapEnabled != 0) {
			wordScan = str + 1;
			wordLength = 0;
			while (*wordScan != ' ' && *wordScan != '\0'
#ifdef XVT_MODERN
				   && wordLength < (uint16_t)(sizeof(wordBuffer) - 1u)
#endif
			) {
				wordBuffer[wordLength] = *wordScan;
				++wordLength;
				++wordScan;
			}
			wordBuffer[wordLength] = '\0';

			nextWordWidth = FlightText_MeasureStringWidth(wordBuffer);
			if (g_flightCursorX + FlightText_MeasureStringWidth(" ") + nextWordWidth >
				g_flightClipRight - 2) {
				g_flightDrawCharFn('\n');
			} else {
				g_flightDrawCharFn((uint8_t)*str);
			}
		} else {
			g_flightDrawCharFn((uint8_t)*str);
		}
	} while (*++str != '\0');
}

// FUNCTION: XVT 0x4A9A50
void FlightText_DrawStringCentered(const char* str) {
	uint16_t width;
	uint16_t candidateX;
	int cursorY;

	width = FlightText_MeasureStringWidth(str) >> 1;
	candidateX = (uint16_t)(((int)g_flightClipRight + (int)g_flightClipLeft) / 2 - width);
	if (candidateX < (int)g_flightClipLeft) {
		candidateX = g_flightClipLeft;
	}
	cursorY = g_flightCursorY;
	FlightText_SetCursor((int16_t)candidateX, (int16_t)cursorY);
	FlightText_DrawString(str);
}

// FUNCTION: XVT 0x4A9AC0
void FlightText_DrawStringRightAligned(const char* str) {
	uint16_t width;
	uint16_t cursorX;
	int cursorY;

	width = FlightText_MeasureStringWidth(str) + 2;
	cursorX = g_flightClipRight;
	cursorX -= width;
	if (cursorX >= 0x8000u) {
		cursorX = 0;
	}
	if (g_flightClipLeft > (int)cursorX) {
		cursorX = g_flightClipLeft;
	}
	cursorY = g_flightCursorY;
	FlightText_SetCursor(cursorX, cursorY);
	FlightText_DrawString(str);
}
