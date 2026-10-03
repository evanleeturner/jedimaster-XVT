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

/* Nonzero while flight text wraps at g_flightClipRight: a glyph that does not
 * fit moves to the next line, and FlightText_DrawString breaks before a word
 * that does not fit. Set by FlightText_SetWordWrap;
 * FlightLoading_PulseAndDrawProgressScreen, FeDiskIo_ShowRetryFailPrompt
 * and FeDiskIo_ShowFatalErrorMessageAndWaitKey save it and put it back. */
// GLOBAL: XVT 0x9CD270
int16_t g_flightWordWrapEnabled;
/* Nonzero while each wrap and newline first fills the rest of the line with
 * g_flightTextBgColor. Set by FlightText_SetClearLineBackground;
 * FlightLoading_PulseAndDrawProgressScreen, FeDiskIo_ShowRetryFailPrompt
 * and FeDiskIo_ShowFatalErrorMessageAndWaitKey save it and put it back. */
// GLOBAL: XVT 0x9FE7E4
int16_t g_flightClearLineBgEnabled;
/* Never set to anything but its starting 0, and never used:
 * FlightLoading_PulseAndDrawProgressScreen, FeDiskIo_ShowRetryFailPrompt
 * and FeDiskIo_ShowFatalErrorMessageAndWaitKey only save it and put it
 * back. */
// GLOBAL: XVT 0xA07C64
int16_t g_flightTextReservedState = 0;
/* Text cursor row in pixels on the drawing surface: the top of the next
 * glyph. Written by FlightText_SetCursor and by the four glyph drawers on
 * each newline and wrap; FlightLoading_PulseAndDrawProgressScreen,
 * FeDiskIo_ShowRetryFailPrompt and FeDiskIo_ShowFatalErrorMessageAndWaitKey
 * save it and put it back. */
// GLOBAL: XVT 0xA08102
int16_t g_flightCursorY = 0;
/* Text cursor column in pixels on the drawing surface: the left edge of the
 * next glyph. Written by FlightText_SetCursor and by the four glyph drawers,
 * which advance it by each glyph's width and reset it on newline and wrap;
 * FlightLoading_PulseAndDrawProgressScreen, FeDiskIo_ShowRetryFailPrompt
 * and FeDiskIo_ShowFatalErrorMessageAndWaitKey save it and put it back. */
// GLOBAL: XVT 0xA08108
int16_t g_flightCursorX = 0;
/* Shared buffer where text is built before it is drawn or handed to
 * msg_addMessagePtr. Many functions write it, chiefly FlightText_SetScratch,
 * FlightText_AppendScratchString, FlightText_AppendScratchChar,
 * FlightText_FormatScratchInt and sprintf calls in hud.c. None of the
 * FlightText functions checks its 256-byte size. */
// GLOBAL: XVT 0x9A1ED0
char g_flightTextScratchBuffer[256];
/* Palette index of the set pixels of the next glyphs. Many functions write
 * it, chiefly FlightText_SetColor; Hud_ShowFlightMessagePane and
 * Mfd_DrawMessageLogPage step it by one. */
// GLOBAL: XVT 0x9A807A
uint8_t g_flightTextColorIndex;
/* Palette index of the unset pixels of each glyph cell, and the color
 * FlightSw_FillRectOrBorder8bpp and FlightSw_FillRectOrBorder16bpp fill
 * with, as for the rest of a line or the clip rectangle. Many functions
 * write it, chiefly FlightText_SetBackgroundColor. */
// GLOBAL: XVT 0xA08100
uint8_t g_flightTextBgColor;
/* Palette index of the drop shadow drawn while g_flightTextShadowEnabled is
 * set. Written by FlightText_SetShadowColor; FeDiskIo_InitGlobalBuffers,
 * FeDiskIo_ShowRetryFailPrompt and FeDiskIo_ShowFatalErrorMessageAndWaitKey
 * set it to 0, and those two prompts and
 * FlightLoading_PulseAndDrawProgressScreen put back what they saved. */
// GLOBAL: XVT 0x9E8F52
uint8_t g_flightTextShadowColor;
/* Font size class last given to FlightText_SetFontTier, its only writer,
 * which has the table of fonts; every caller passes 0, 1 or 2.
 * FeDiskIo_LockGlobalBuffers reads it to choose g_flightFontGlyphTableSw
 * again after relocking the fonts. */
// GLOBAL: XVT 0x9D80C8
uint8_t g_flightFontTier = 0;
/* 1 when the current font has lowercase glyphs. When it is 0 and
 * g_flightFontTier is not 0, lowercase letters draw and measure as capitals.
 * Only FlightText_SetFontTier writes it. */
// GLOBAL: XVT 0x9A1FF4
uint8_t g_flightFontHasLowercase = 0;
/* Line height of the current font in pixels: 5 for the micro font, 8 for the
 * small and 10 for the medium. Only FlightText_SetFontTier writes it. */
// GLOBAL: XVT 0x9A20AE
uint8_t g_flightFontLineHeight = 0;
/* Digit width in pixels that the HUD lays numbers out with: 3 for the micro
 * font, 4 for the small and 5 for the medium. Only FlightText_SetFontTier
 * writes it; nothing derives it from the glyphs. */
// GLOBAL: XVT 0x9ED232
uint8_t g_flightFontDigitWidth = 0;
/* Glyph records of the current font, one every g_flightFontGlyphStrideSw
 * bytes from code 0x20: an advance width byte, a height byte, then the rows.
 * Written by FlightText_SetFontTier, and by FeDiskIo_LockGlobalBuffers,
 * which picks by g_flightFontTier with another mapping (0 medium, 1 small,
 * 2 micro) and leaves the stride and line height alone. */
// GLOBAL: XVT 0x9D7674
uint8_t *g_flightFontGlyphTableSw = 0;
/* Bytes per glyph record in g_flightFontGlyphTableSw: 42 for the micro font,
 * 66 for the small and 82 for the medium. Only FlightText_SetFontTier writes
 * it. */
// GLOBAL: XVT 0x9D8C02
uint16_t g_flightFontGlyphStrideSw = 0;
/* The small font, MICRO48.FNT, in the memory of g_flightSmallFontHandle.
 * FeDiskIo_InitGlobalBuffers sets it to NULL, then locks the handle and
 * loads the file; FeDiskIo_LockGlobalBuffers locks it again. */
// GLOBAL: XVT 0x9A7800
uint8_t *g_flightFontSmallSw = 0;
/* The medium font, MICRO64.FNT, in the memory of g_flightMediumFontHandle,
 * locked by FeDiskIo_InitGlobalBuffers and FeDiskIo_LockGlobalBuffers. At
 * 320x240 FeDiskIo_InitGlobalBuffers loads no file into it. */
// GLOBAL: XVT 0x9E965C
uint8_t *g_flightFontMediumSw = 0;
/* The micro font, MICRO32.FNT, in the memory of g_flightMicroFontHandle,
 * locked and loaded by FeDiskIo_InitGlobalBuffers and locked again by
 * FeDiskIo_LockGlobalBuffers. */
// GLOBAL: XVT 0xA07CC0
uint8_t *g_flightFontMicroSw = 0;
/* Left edge in pixels of the clip rectangle for flight text,
 * fills and lines. Written by FlightText_SetClipRect;
 * FlightLoading_PulseAndDrawProgressScreen, FeDiskIo_ShowRetryFailPrompt
 * and FeDiskIo_ShowFatalErrorMessageAndWaitKey save it and put it back. */
// GLOBAL: XVT 0x9A6FE0
int16_t g_flightClipLeft = 0;
/* Right edge, exclusive, in pixels of the clip rectangle for flight text,
 * fills and lines. Written by FlightText_SetClipRect;
 * FlightLoading_PulseAndDrawProgressScreen, FeDiskIo_ShowRetryFailPrompt
 * and FeDiskIo_ShowFatalErrorMessageAndWaitKey save it and put it back. */
// GLOBAL: XVT 0x9D8C00
int16_t g_flightClipRight = 0;
/* Bottom edge, exclusive, in pixels of the clip rectangle for flight text,
 * fills and lines. Written by FlightText_SetClipRect;
 * FlightLoading_PulseAndDrawProgressScreen, FeDiskIo_ShowRetryFailPrompt
 * and FeDiskIo_ShowFatalErrorMessageAndWaitKey save it and put it back. */
// GLOBAL: XVT 0xA07CE4
int16_t g_flightClipBottom = 0;
/* Top edge in pixels of the clip rectangle for flight text,
 * fills and lines. Written by FlightText_SetClipRect;
 * FlightLoading_PulseAndDrawProgressScreen, FeDiskIo_ShowRetryFailPrompt
 * and FeDiskIo_ShowFatalErrorMessageAndWaitKey save it and put it back. */
// GLOBAL: XVT 0xA0813C
int16_t g_flightClipTop = 0;
/* Palette indices for the color codes 0x40 to 0x5F that
 * FlightText_SetColor, FlightText_SetBackgroundColor and
 * FlightText_SetShadowColor accept: 0x40 to 0x53 give 0x2C to 0x3F in order;
 * 0x54 to 0x57 give 0xD5, 0xD5, 0xD4 and 0xD3; 0x58 to 0x5F give 0x2C to
 * 0x2F twice. */
// GLOBAL: XVT 0x524080
const uint8_t g_flightCharToColorLut[32] = {
	0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
	0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0xd5, 0xd5,
	0xd4, 0xd3, 0x2c, 0x2d, 0x2e, 0x2f, 0x2c, 0x2d, 0x2e, 0x2f,
};
/* Place value by digit position, counted from the right starting at 1: 1, 10,
 * 100, 1,000 and 10,000 for positions 1 to 5. Entry 0 is 1; entries 6 and 7
 * are 0. Read by FlightText_DrawDecimalNumber and msg_emitInFlightMessage. */
// GLOBAL: XVT 0x520EB0
const uint16_t g_flightTextDecimalDivisors[8] = {1,    1,     10, 100,
						 1000, 10000, 0,  0};
/* Nonzero while glyphs get a drop shadow in g_flightTextShadowColor: each
 * row's set pixels repeated one pixel right on the row below, the drawn cell
 * one pixel wider. No setter; many functions write it directly, chiefly
 * hud.c drawing functions, which set 0 before their text, and
 * FlightAlert_DrawBox and Hud_SetupReadyMessagePaneText, which set 1. */
// GLOBAL: XVT 0x9EC464
uint8_t g_flightTextShadowEnabled = 0;

/* Draws one character of the current font at the text cursor into an 8-bit
 * surface, for fonts whose rows are one byte, stored 2 bytes apart, top bit
 * leftmost. Set pixels take g_flightTextColorIndex, shadow pixels
 * g_flightTextShadowColor and the rest of the cell g_flightTextBgColor; all
 * clip to the g_flightClip rectangle. Advances g_flightCursorX by the glyph's
 * advance width. A newline moves the cursor to g_flightClipLeft and
 * g_flightFontLineHeight down; other codes below 0x20 draw nothing. With word
 * wrap on, a glyph that would reach g_flightClipRight first moves to the next
 * line, the glyph's height plus 2 down, and the cursor does the same after a
 * glyph that reaches it; each newline and wrap first clears the rest of the
 * line when g_flightClearLineBgEnabled is set. Nothing calls this:
 * FlightRender_InstallCallbacks never puts it in g_flightDrawCharFn. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40F050
void FlightText_DrawNarrowGlyph8bpp(uint8_t ch)
{
	uint16_t glyphAdvance;
	int glyphRowCount;
	int glyphWidth;
	uint8_t glyphHeight;
	uint16_t wrapLineHeight;
	const uint8_t *rowData;
	int lineOffset;
	uint8_t *destination;
	int line;
	uint8_t normalizedChar;
	int drawX;
	int pixelCount;
	int addressEachRowSeparately;
	uint8_t glyphBits;
	uint8_t shadowBits;
	uint8_t paletteIndex;
	uint8_t *rowStart;
#ifndef XVT_MODERN
	unsigned int pixelOffset;
	unsigned int page;
	int clippedBottom;
#endif

	if (ch == '\n') {
		if (g_flightClearLineBgEnabled != 0) {
			FlightText_ClearRemainingLineBackground8bpp();
		}
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += g_flightFontLineHeight;
		return;
	}
	if (ch < ' ') {
		return;
	}

	normalizedChar = ch;
	if (g_flightFontTier != 0 && g_flightFontHasLowercase == 0 &&
	    ch >= 'a' && ch <= 'z') {
		normalizedChar = ch - ('a' - 'A');
	}
	rowData = &g_flightFontGlyphTableSw[g_flightFontGlyphStrideSw *
					    (uint8_t)(normalizedChar - ' ')];
	glyphAdvance = *rowData++;
	glyphWidth = glyphAdvance;
	glyphHeight = *rowData++;
	if (glyphWidth + g_flightCursorX >= g_flightClipRight &&
	    g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0) {
			FlightText_ClearRemainingLineBackground8bpp();
		}
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += glyphHeight + 2;
	}

#ifdef XVT_MODERN
	XvtCockpitPages_RecordGlyph(normalizedChar, glyphAdvance, glyphHeight,
				    1);
	XvtCockpitMessages_RecordGlyph(normalizedChar, glyphAdvance,
				       glyphHeight, 1);
#endif
	shadowBits = 0;
	addressEachRowSeparately = 0;
	line = g_flightCursorY;
	if (line < g_flightClipTop) {
		line = g_flightClipTop;
	}
#ifdef XVT_MODERN
	/* Fully clipped glyphs still advance the cursor, without looking up a row. */
	lineOffset =
		line < g_flightClipBottom ? FlightSw_GetLineOffset(line) : 0;
#else
	lineOffset = FlightSw_GetLineOffset(line);
#endif
#ifndef XVT_MODERN
	if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
	    g_flightSwFramebufferBase == g_swFramebufferBase) {
		page = lineOffset / g_vesaPageSizeBytes;
		lineOffset %= g_vesaPageSizeBytes;
		RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow, (uint16_t)page);
		clippedBottom = g_flightCursorY + glyphHeight + 1;
		if (clippedBottom > g_flightClipBottom) {
			clippedBottom = g_flightClipBottom;
		}
		if (lineOffset + g_surfacePitch * (clippedBottom - line) >
		    0xFFFF) {
			addressEachRowSeparately = 1;
		}
	}
#endif
	if (g_flightSwFramebufferBase != g_swFramebufferBase) {
		addressEachRowSeparately = 1;
	}

	if (addressEachRowSeparately == 0) {
		destination = g_flightSwFramebufferBase + lineOffset;
		wrapLineHeight = glyphHeight;
		line = g_flightCursorY;
		glyphRowCount = wrapLineHeight;
		if (g_flightCursorY + glyphRowCount > line) {
			do {
				pixelCount = glyphWidth;
				glyphBits = *rowData;
				if (g_flightTextShadowEnabled != 0) {
					++pixelCount;
				}
				drawX = g_flightCursorX;
				if (drawX < g_flightClipLeft) {
					if (g_flightClipLeft - drawX >=
					    pixelCount) {
						break;
					}
					pixelCount = drawX + pixelCount -
						     g_flightClipLeft;
					glyphBits <<= g_flightClipLeft - drawX;
					drawX = g_flightClipLeft;
				}
				if (g_flightClipBottom <= line) {
					break;
				}
				if (g_flightClipTop <= line) {
					if (drawX + pixelCount >
					    g_flightClipRight) {
						pixelCount = g_flightClipRight -
							     drawX;
						if (pixelCount <= 0) {
							break;
						}
					}
					rowStart = &destination[drawX];
					while (pixelCount-- != 0) {
						if ((glyphBits & 0x80u) != 0) {
							paletteIndex =
								g_flightTextColorIndex;
						} else if (
							g_flightTextShadowEnabled !=
								0 &&
							(shadowBits & 0x80u) !=
								0) {
							paletteIndex =
								g_flightTextShadowColor;
						} else {
							paletteIndex =
								g_flightTextBgColor;
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
			} while (glyphRowCount + g_flightCursorY > line);
		}
	} else {
		line = g_flightCursorY;
		wrapLineHeight = glyphHeight;
		glyphRowCount = wrapLineHeight;
		if (g_flightCursorY + glyphRowCount > line) {
			do {
				pixelCount = glyphWidth;
				glyphBits = *rowData;
				if (g_flightTextShadowEnabled != 0) {
					++pixelCount;
				}
				drawX = g_flightCursorX;
				if (drawX < g_flightClipLeft) {
					if (g_flightClipLeft - drawX >=
					    pixelCount) {
						break;
					}
					pixelCount = drawX + pixelCount -
						     g_flightClipLeft;
					glyphBits <<= g_flightClipLeft - drawX;
					drawX = g_flightClipLeft;
				}
				if (g_flightClipBottom <= line) {
					break;
				}
				if (g_flightClipTop <= line) {
					if (drawX + pixelCount >
					    g_flightClipRight) {
						pixelCount = g_flightClipRight -
							     drawX;
						if (pixelCount <= 0) {
							break;
						}
					}
#ifdef XVT_MODERN
					rowStart =
						&g_flightSwFramebufferBase
							[FlightSw_GetLineOffset(
								 line) +
							 drawX];
#else
					pixelOffset =
						drawX +
						FlightSw_GetLineOffset(line);
					if (g_flightResolutionMode !=
						    FLIGHT_RESOLUTION_320X240 &&
					    g_flightSwFramebufferBase ==
						    g_swFramebufferBase) {
						page = pixelOffset /
						       g_vesaPageSizeBytes;
						pixelOffset %=
							g_vesaPageSizeBytes;
						RtsVga2_SetCurrentPage(
							(uint8_t)g_vesaWindow,
							(uint16_t)page);
					}
					rowStart = &g_flightSwFramebufferBase
							   [pixelOffset];
#endif
					while (pixelCount-- != 0) {
						if ((glyphBits & 0x80u) != 0) {
							paletteIndex =
								g_flightTextColorIndex;
						} else if (
							g_flightTextShadowEnabled !=
								0 &&
							(shadowBits & 0x80u) !=
								0) {
							paletteIndex =
								g_flightTextShadowColor;
						} else {
							paletteIndex =
								g_flightTextBgColor;
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
			} while (glyphRowCount + g_flightCursorY > line);
		}
	}

	g_flightCursorX += glyphAdvance;
	if (g_flightClipRight <= g_flightCursorX &&
	    g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0) {
			FlightText_ClearRemainingLineBackground8bpp();
		}
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += wrapLineHeight + 2;
	}
}

/* Draws one character at the text cursor into an 8-bit surface, as
 * FlightText_DrawNarrowGlyph8bpp does, for fonts whose rows are a 32-bit
 * little-endian word, stored 8 bytes apart, top bit leftmost; a newline moves
 * down g_flightFontLineHeight plus 1. FlightRender_InstallCallbacks installs
 * it as g_flightDrawCharFn for pixel modes 0 and 1. In the modern build it
 * also records the glyph for the modern renderer. */
// FUNCTION: XVT 0x40F520
void FlightText_DrawWideGlyph8bpp(uint8_t ch)
{
	uint16_t glyphAdvance;
	int glyphRowCount;
	int glyphWidth;
	uint8_t glyphHeight;
	uint16_t wrapLineHeight;
	const uint8_t *rowData;
	int lineOffset;
	uint8_t *destination;
	int line;
	uint8_t normalizedChar;
	int drawX;
	int pixelCount;
	int addressEachRowSeparately;
	uint32_t glyphBits;
	uint32_t shadowBits;
	uint8_t paletteIndex;
	uint8_t *rowStart;
#ifndef XVT_MODERN
	unsigned int pixelOffset;
	unsigned int page;
	int clippedBottom;
#endif

	if (ch == '\n') {
		if (g_flightClearLineBgEnabled != 0) {
			FlightText_ClearRemainingLineBackground8bpp();
		}
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += g_flightFontLineHeight + 1;
		return;
	}
	if (ch < ' ') {
		return;
	}

	normalizedChar = ch;
	if (g_flightFontTier != 0 && g_flightFontHasLowercase == 0 &&
	    ch >= 'a' && ch <= 'z') {
		normalizedChar = ch - ('a' - 'A');
	}
	rowData = &g_flightFontGlyphTableSw[g_flightFontGlyphStrideSw *
					    (uint8_t)(normalizedChar - ' ')];
	glyphAdvance = *rowData++;
	glyphWidth = glyphAdvance;
	glyphHeight = *rowData++;
	if (glyphWidth + g_flightCursorX >= g_flightClipRight &&
	    g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0) {
			FlightText_ClearRemainingLineBackground8bpp();
		}
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += glyphHeight + 2;
	}

	addressEachRowSeparately = 0;
#ifdef XVT_MODERN
	XvtCockpitPages_RecordGlyph(normalizedChar, glyphAdvance, glyphHeight,
				    0);
	XvtCockpitMessages_RecordGlyph(normalizedChar, glyphAdvance,
				       glyphHeight, 0);
#endif
	shadowBits = 0;
	line = g_flightCursorY;
	if (line < g_flightClipTop) {
		line = g_flightClipTop;
	}
#ifdef XVT_MODERN
	/* Fully clipped glyphs still advance the cursor, without looking up a row. */
	lineOffset =
		line < g_flightClipBottom ? FlightSw_GetLineOffset(line) : 0;
#else
	lineOffset = FlightSw_GetLineOffset(line);
#endif
#ifndef XVT_MODERN
	if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
	    g_flightSwFramebufferBase == g_swFramebufferBase) {
		page = lineOffset / g_vesaPageSizeBytes;
		lineOffset %= g_vesaPageSizeBytes;
		RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow, (uint16_t)page);
		clippedBottom = g_flightCursorY + glyphHeight + 1;
		if (clippedBottom > g_flightClipBottom) {
			clippedBottom = g_flightClipBottom;
		}
		if (lineOffset + g_surfacePitch * (clippedBottom - line) >
		    0xFFFF) {
			addressEachRowSeparately = 1;
		}
	}
#endif
	if (g_flightSwFramebufferBase != g_swFramebufferBase) {
		addressEachRowSeparately = 1;
	}

	if (addressEachRowSeparately == 0) {
		destination = g_flightSwFramebufferBase + lineOffset;
		wrapLineHeight = glyphHeight;
		line = g_flightCursorY;
		glyphRowCount = wrapLineHeight;
		if (g_flightCursorY + glyphRowCount > line) {
			do {
				pixelCount = glyphWidth;
#ifdef XVT_MODERN
				memcpy(&glyphBits, rowData, sizeof(glyphBits));
#else
				glyphBits = *(const uint32_t *)rowData;
#endif
				if (g_flightTextShadowEnabled != 0) {
					++pixelCount;
				}
				drawX = g_flightCursorX;
				if (drawX < g_flightClipLeft) {
					if (g_flightClipLeft - drawX >=
					    pixelCount) {
						break;
					}
					pixelCount = drawX + pixelCount -
						     g_flightClipLeft;
					glyphBits <<= g_flightClipLeft - drawX;
					drawX = g_flightClipLeft;
				}
				if (g_flightClipBottom <= line) {
					break;
				}
				if (g_flightClipTop <= line) {
					if (drawX + pixelCount >
					    g_flightClipRight) {
						pixelCount = g_flightClipRight -
							     drawX;
						if (pixelCount <= 0) {
							break;
						}
					}
					rowStart = &destination[drawX];
					while (pixelCount-- != 0) {
						if ((glyphBits & 0x80000000u) !=
						    0) {
							paletteIndex =
								g_flightTextColorIndex;
						} else if (
							g_flightTextShadowEnabled !=
								0 &&
							(shadowBits &
							 0x80000000u) != 0) {
							paletteIndex =
								g_flightTextShadowColor;
						} else {
							paletteIndex =
								g_flightTextBgColor;
						}
						*rowStart++ = paletteIndex;
						glyphBits <<= 1;
						shadowBits <<= 1;
					}
					destination += FlightSw_GetLinePitch();
				}
#ifdef XVT_MODERN
				memcpy(&shadowBits, rowData,
				       sizeof(shadowBits));
#else
				shadowBits = *(const uint32_t *)rowData;
#endif
				rowData += 8;
				++line;
				shadowBits >>= 1;
			} while (glyphRowCount + g_flightCursorY > line);
		}
	} else {
		line = g_flightCursorY;
		wrapLineHeight = glyphHeight;
		glyphRowCount = wrapLineHeight;
		if (g_flightCursorY + glyphRowCount > line) {
			do {
				pixelCount = glyphWidth;
#ifdef XVT_MODERN
				memcpy(&glyphBits, rowData, sizeof(glyphBits));
#else
				glyphBits = *(const uint32_t *)rowData;
#endif
				if (g_flightTextShadowEnabled != 0) {
					++pixelCount;
				}
				drawX = g_flightCursorX;
				if (drawX < g_flightClipLeft) {
					if (g_flightClipLeft - drawX >=
					    pixelCount) {
						break;
					}
					pixelCount = drawX + pixelCount -
						     g_flightClipLeft;
					glyphBits <<= g_flightClipLeft - drawX;
					drawX = g_flightClipLeft;
				}
				if (g_flightClipBottom <= line) {
					break;
				}
				if (g_flightClipTop <= line) {
					if (drawX + pixelCount >
					    g_flightClipRight) {
						pixelCount = g_flightClipRight -
							     drawX;
						if (pixelCount <= 0) {
							break;
						}
					}
#ifdef XVT_MODERN
					rowStart =
						&g_flightSwFramebufferBase
							[FlightSw_GetLineOffset(
								 line) +
							 drawX];
#else
					pixelOffset =
						drawX +
						FlightSw_GetLineOffset(line);
					if (g_flightResolutionMode !=
						    FLIGHT_RESOLUTION_320X240 &&
					    g_flightSwFramebufferBase ==
						    g_swFramebufferBase) {
						page = pixelOffset /
						       g_vesaPageSizeBytes;
						pixelOffset %=
							g_vesaPageSizeBytes;
						RtsVga2_SetCurrentPage(
							(uint8_t)g_vesaWindow,
							(uint16_t)page);
					}
					rowStart = &g_flightSwFramebufferBase
							   [pixelOffset];
#endif
					while (pixelCount-- != 0) {
						if ((glyphBits & 0x80000000u) !=
						    0) {
							paletteIndex =
								g_flightTextColorIndex;
						} else if (
							g_flightTextShadowEnabled !=
								0 &&
							(shadowBits &
							 0x80000000u) != 0) {
							paletteIndex =
								g_flightTextShadowColor;
						} else {
							paletteIndex =
								g_flightTextBgColor;
						}
						*rowStart++ = paletteIndex;
						glyphBits <<= 1;
						shadowBits <<= 1;
					}
				}
#ifdef XVT_MODERN
				memcpy(&shadowBits, rowData,
				       sizeof(shadowBits));
#else
				shadowBits = *(const uint32_t *)rowData;
#endif
				rowData += 8;
				++line;
				shadowBits >>= 1;
			} while (glyphRowCount + g_flightCursorY > line);
		}
	}

	g_flightCursorX += glyphAdvance;
	if (g_flightClipRight <= g_flightCursorX &&
	    g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0) {
			FlightText_ClearRemainingLineBackground8bpp();
		}
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += wrapLineHeight + 2;
	}
}

/* Fills from the text cursor to g_flightClipRight, g_flightFontLineHeight
 * rows down, clipped to the g_flightClip rectangle, with g_flightTextBgColor
 * on an 8-bit surface. Writes the g_flightFillRect 8bpp edges; does nothing
 * when the cursor is at or past g_flightClipRight. */
// FUNCTION: XVT 0x410110
void FlightText_ClearRemainingLineBackground8bpp(void)
{

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

/* Returns g_flightFontLineHeight plus 1 when str, drawn from g_flightCursorX,
 * would end beyond g_flightClipRight - 11; else 0, and 0 for NULL. */
// FUNCTION: XVT 0x415C40
int16_t FlightText_GetWrapHeightForString(const char *str)
{
	if (str == 0) {
		return 0;
	}
	if (g_flightCursorX + FlightText_MeasureStringWidth(str) >
	    g_flightClipRight - 11) {
		return g_flightFontLineHeight + 1;
	}

	return 0;
}

/* Draws value right-aligned in digitCount places through g_flightDrawCharFn,
 * with zeros in front shown as spaces except in the last minDigits places. When
 * value needs more places, the first place shows 9 and the rest are right. The
 * value 0xFFFF draws digitCount zeros in color code '@' without shadow, then
 * puts back g_flightTextShadowEnabled and g_flightTextColorIndex. Does not
 * check digitCount: g_flightTextDecimalDivisors serves 1 to 5 places, holds 0
 * for 6 and 7, and ends there. */
// FUNCTION: XVT 0x4277F0
void FlightText_DrawDecimalNumber(uint16_t value, unsigned int digitCount,
				  unsigned int minDigits)
{
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
		for (digitIndex = digitCount; digitIndex != 0; --digitIndex) {
			g_flightDrawCharFn('0');
		}
		g_flightTextShadowEnabled = savedShadow;
		g_flightTextColorIndex = savedColor;
		return;
	}

	started = 0;
	for (digitIndex = digitCount; digitIndex != 0; --digitIndex) {
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

/* Returns the width in pixels of str in the current font, up to its end or
 * first newline: the sum of each glyph's advance byte. Skips codes below
 * 0x20, and a 0xFE color escape with the byte after it; lowercase letters
 * count as capitals as they draw. Does not check str for NULL. */
// FUNCTION: XVT 0x4278C0
uint16_t FlightText_MeasureStringWidth(const char *str)
{
	uint16_t totalWidth;
	uint8_t ch;
	const char *cursor;

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
				if (g_flightFontTier != 0 &&
				    g_flightFontHasLowercase == 0 &&
				    ch >= 'a' && ch <= 'z') {
					ch -= 32;
				}
				totalWidth += g_flightFontGlyphTableSw
					[g_flightFontGlyphStrideSw *
					 (uint8_t)(ch - 32)];
			}
		}
		ch = (uint8_t)*cursor++;
	}

	return totalWidth;
}

/* The 16-bit surface version of FlightText_DrawNarrowGlyph8bpp: the same
 * glyph rows, colors, shadow, clipping, cursor moves and wrap, with each
 * palette index turned into a pixel through g_flightPalette16Bpp. Nothing
 * calls this: FlightRender_InstallCallbacks never puts it in
 * g_flightDrawCharFn. */
// FUNCTION: XVT 0x449F70
void FlightText_DrawNarrowGlyph(uint8_t ch)
{
	uint8_t normalizedChar;
	uint8_t glyphHeight;
	uint8_t glyphBits;
	uint8_t shadowBits;
	const uint8_t *glyphData;
	const uint8_t *rowData;
	int16_t glyphAdvance;
	int16_t wrapLineHeight;
	int glyphRowCount;
	int glyphWidth;
	int line;
	int drawX;
	int pixelCount;
	int pixelsRemaining;
	unsigned int pixelOffset;
	unsigned int paletteIndex;
	uint16_t *destination;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	if (ch == '\n') {
		if (g_flightClearLineBgEnabled != 0) {
			FlightText_ClearRemainingLineBackground();
		}
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += g_flightFontLineHeight;
		return;
	}
	if (ch < ' ') {
		return;
	}

	normalizedChar = ch;
	if (g_flightFontTier != 0 && g_flightFontHasLowercase == 0 &&
	    ch >= 'a' && ch <= 'z') {
		normalizedChar = ch - ('a' - 'A');
	}
	glyphData = &g_flightFontGlyphTableSw[g_flightFontGlyphStrideSw *
					      (uint8_t)(normalizedChar - ' ')];
	glyphAdvance = *glyphData++;
	glyphHeight = *glyphData++;
	rowData = glyphData;
	glyphWidth = glyphAdvance;
	if (glyphWidth + g_flightCursorX >= g_flightClipRight &&
	    g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0) {
			FlightText_ClearRemainingLineBackground();
		}
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += glyphHeight + 2;
	}

#ifdef XVT_MODERN
	XvtCockpitPages_RecordGlyph(normalizedChar, glyphAdvance, glyphHeight,
				    1);
	XvtCockpitMessages_RecordGlyph(normalizedChar, glyphAdvance,
				       glyphHeight, 1);
#endif
	shadowBits = 0;
	line = g_flightCursorY;
	wrapLineHeight = glyphHeight;
	glyphRowCount = glyphHeight;
	if (glyphRowCount + g_flightCursorY > line) {
		do {
			pixelCount = glyphWidth;
			glyphBits = *rowData;
			if (g_flightTextShadowEnabled != 0) {
				++pixelCount;
			}
			drawX = g_flightCursorX;
			if (g_flightCursorX < g_flightClipLeft) {
				if (g_flightClipLeft - g_flightCursorX >=
				    pixelCount) {
					break;
				}
				pixelCount = g_flightCursorX + pixelCount -
					     g_flightClipLeft;
				drawX = g_flightClipLeft;
				glyphBits <<=
					g_flightClipLeft - g_flightCursorX;
			}
			if (g_flightClipBottom <= line) {
				break;
			}
			if (g_flightClipTop > line) {
				pixelCount = 0;
			}
			if (pixelCount + drawX > g_flightClipRight) {
				pixelCount = g_flightClipRight - drawX;
				if (pixelCount <= 0) {
					break;
				}
			}

#ifndef XVT_MODERN
			pixelOffset = FlightSw_GetLineOffset(line) + 2 * drawX;
			if (g_flightResolutionMode !=
				    FLIGHT_RESOLUTION_320X240 &&
			    g_flightSwFramebufferBase == g_swFramebufferBase) {
				page = pixelOffset / g_vesaPageSizeBytes;
				pixelOffset %= g_vesaPageSizeBytes;
				RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
						       (uint16_t)page);
			}
			destination = &(
				(uint16_t *)
					g_flightSwFramebufferBase)[pixelOffset /
								   2];
#endif
			pixelsRemaining = pixelCount;
			if (pixelsRemaining != 0) {
#ifdef XVT_MODERN
				/* The original looks up negative rows even when top clipping leaves
				 * no pixels. Keep row/shadow progression outside this drawing block. */
				pixelOffset = FlightSw_GetLineOffset(line) +
					      2 * drawX;
				destination =
					&((uint16_t *)g_flightSwFramebufferBase)
						[pixelOffset / 2];
#endif
				--pixelsRemaining;
				do {
					if ((glyphBits & 0x80u) != 0) {
						paletteIndex =
							g_flightTextColorIndex;
					} else if (g_flightTextShadowEnabled !=
							   0 &&
						   (shadowBits & 0x80u) != 0) {
						paletteIndex =
							g_flightTextShadowColor;
					} else {
						paletteIndex =
							g_flightTextBgColor;
					}
					*destination++ = g_flightPalette16Bpp
						[paletteIndex];
					shadowBits <<= 1;
					glyphBits <<= 1;
				} while (pixelsRemaining-- != 0);
			}
			shadowBits = *rowData >> 1;
			rowData += 2;
			++line;
		} while (glyphRowCount + g_flightCursorY > line);
	}

	g_flightCursorX += glyphAdvance;
	if (g_flightClipRight <= g_flightCursorX &&
	    g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0) {
			FlightText_ClearRemainingLineBackground();
		}
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += wrapLineHeight + 2;
	}
}

/* The 16-bit surface version of FlightText_DrawWideGlyph8bpp, with each
 * palette index turned into a pixel through g_flightPalette16Bpp.
 * FlightRender_InstallCallbacks installs it as g_flightDrawCharFn for pixel
 * mode 2. In the modern build it also records the glyph for the modern
 * renderer. */
// FUNCTION: XVT 0x44A270
void FlightText_DrawWideGlyph(uint8_t ch)
{
	uint8_t normalizedChar;
	uint8_t glyphHeight;
	const uint8_t *glyphData;
#ifdef XVT_MODERN
	const uint8_t *rowData;
#else
	const uint32_t *rowData;
#endif
	int16_t glyphAdvance;
	int16_t wrapLineHeight;
	int glyphWidth;
	int glyphRowCount;
	int line;
	int drawX;
	int pixelCount;
	int pixelsRemaining;
	uint32_t glyphBits;
	uint32_t shadowBits;
	unsigned int pixelOffset;
	unsigned int paletteIndex;
	uint16_t *destination;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	if (ch == '\n') {
		if (g_flightClearLineBgEnabled != 0) {
			FlightText_ClearRemainingLineBackground();
		}
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += g_flightFontLineHeight + 1;
		return;
	}
	if (ch < ' ') {
		return;
	}

	normalizedChar = ch;
	if (g_flightFontTier != 0 && g_flightFontHasLowercase == 0 &&
	    normalizedChar >= 'a' && normalizedChar <= 'z') {
		normalizedChar -= 'a' - 'A';
	}
	glyphData = &g_flightFontGlyphTableSw[g_flightFontGlyphStrideSw *
					      (uint8_t)(normalizedChar - ' ')];
	glyphAdvance = *glyphData++;
	glyphHeight = *glyphData++;
#ifdef XVT_MODERN
	rowData = glyphData;
#else
	rowData = (const uint32_t *)glyphData;
#endif
	glyphWidth = glyphAdvance;
	if (glyphWidth + g_flightCursorX >= g_flightClipRight &&
	    g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0) {
			FlightText_ClearRemainingLineBackground();
		}
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += glyphHeight + 2;
	}

#ifdef XVT_MODERN
	XvtCockpitPages_RecordGlyph(normalizedChar, glyphAdvance, glyphHeight,
				    0);
	XvtCockpitMessages_RecordGlyph(normalizedChar, glyphAdvance,
				       glyphHeight, 0);
#endif
	shadowBits = 0;
	wrapLineHeight = glyphHeight;
	line = g_flightCursorY;
	glyphRowCount = glyphHeight;
	if (g_flightCursorY + glyphRowCount > g_flightCursorY) {
		do {
			pixelCount = glyphWidth;
#ifdef XVT_MODERN
			memcpy(&glyphBits, rowData, sizeof(glyphBits));
#else
			glyphBits = *rowData;
#endif
			if (g_flightTextShadowEnabled != 0) {
				++pixelCount;
			}
			drawX = g_flightCursorX;
			if (g_flightCursorX < g_flightClipLeft) {
				if (g_flightClipLeft - g_flightCursorX >=
				    pixelCount) {
					break;
				}
				pixelCount = g_flightCursorX + pixelCount -
					     g_flightClipLeft;
				drawX = g_flightClipLeft;
				glyphBits <<=
					g_flightClipLeft - g_flightCursorX;
			}
			if (g_flightClipBottom <= line) {
				break;
			}
			if (g_flightClipTop > line) {
				pixelCount = 0;
			}
			if (pixelCount + drawX > g_flightClipRight) {
				pixelCount = g_flightClipRight - drawX;
				if (pixelCount <= 0) {
					break;
				}
			}

#ifndef XVT_MODERN
			pixelOffset = FlightSw_GetLineOffset(line) + 2 * drawX;
			if (g_flightResolutionMode !=
				    FLIGHT_RESOLUTION_320X240 &&
			    g_flightSwFramebufferBase == g_swFramebufferBase) {
				page = pixelOffset / g_vesaPageSizeBytes;
				pixelOffset %= g_vesaPageSizeBytes;
				RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
						       (uint16_t)page);
			}
			destination = &(
				(uint16_t *)
					g_flightSwFramebufferBase)[pixelOffset /
								   2];
#endif
			pixelsRemaining = pixelCount;
			if (pixelsRemaining != 0) {
#ifdef XVT_MODERN
				/* The original looks up negative rows even when top clipping leaves
				 * no pixels. Keep row/shadow progression outside this drawing block. */
				pixelOffset = FlightSw_GetLineOffset(line) +
					      2 * drawX;
				destination =
					&((uint16_t *)g_flightSwFramebufferBase)
						[pixelOffset / 2];
#endif
				--pixelsRemaining;
				do {
					if ((glyphBits & 0x80000000u) != 0) {
						paletteIndex =
							g_flightTextColorIndex;
					} else if (g_flightTextShadowEnabled !=
							   0 &&
						   (shadowBits & 0x80000000u) !=
							   0) {
						paletteIndex =
							g_flightTextShadowColor;
					} else {
						paletteIndex =
							g_flightTextBgColor;
					}
					*destination++ = g_flightPalette16Bpp
						[paletteIndex];
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
		} while (glyphRowCount + g_flightCursorY > line);
	}

	g_flightCursorX += glyphAdvance;
	if (g_flightClipRight <= g_flightCursorX &&
	    g_flightWordWrapEnabled != 0) {
		if (g_flightClearLineBgEnabled != 0) {
			FlightText_ClearRemainingLineBackground();
		}
		g_flightCursorX = g_flightClipLeft;
		g_flightCursorY += wrapLineHeight + 2;
	}
}

/* The 16-bit surface version of
 * FlightText_ClearRemainingLineBackground8bpp: fills the rest of the line
 * with g_flightTextBgColor and writes the g_flightFillRect 16bpp edges. */
// FUNCTION: XVT 0x44AA50
void FlightText_ClearRemainingLineBackground(void)
{

	uint16_t clippedTop;
	uint16_t clippedBottom;
	int16_t cursorX;
	int16_t cursorY;

	cursorX = g_flightCursorX;
	if (g_flightClipRight <= cursorX) {
		return;
	}

	cursorY = g_flightCursorY;
	clippedTop = cursorY;
	g_flightFillRectRight16bpp = g_flightClipRight;
	g_flightFillRectLeft16bpp = cursorX;
	clippedBottom = g_flightFontLineHeight + cursorY;
	if (g_flightClipLeft > (int)(uint16_t)cursorX) {
		g_flightFillRectLeft16bpp = g_flightClipLeft;
	}

	g_flightFillRectTop16bpp = cursorY;
	if (g_flightClipTop > (int)g_flightFillRectTop16bpp) {
		clippedTop = g_flightClipTop;
	}

	g_flightFillRectBottom16bpp = g_flightFontLineHeight + cursorY;
	if (g_flightClipBottom < (int)g_flightFillRectBottom16bpp) {
		clippedBottom = g_flightClipBottom;
	}
	g_flightFillRectBottom16bpp = clippedBottom;
	g_flightFillRectTop16bpp = clippedTop;
	if (clippedBottom > clippedTop) {
		FlightSw_FillRectOrBorder16bpp(0);
	}
}

/* Moves the text cursor to x, y; no checks. */
// FUNCTION: XVT 0x4A9500
void FlightText_SetCursor(int x, int y)
{
	g_flightCursorY = y;
	g_flightCursorX = x;
}

/* Sets the g_flightClip rectangle; right and bottom are exclusive. Raises a
 * negative left or top to 0 and lowers right and bottom to g_screenWidth and
 * g_screenHeight; a negative right or bottom, compared unsigned, also becomes
 * the screen's width or height. Does not check that left is below right or
 * top below bottom. */
// FUNCTION: XVT 0x4A9520
void FlightText_SetClipRect(int16_t left, int16_t top, int16_t right,
			    int16_t bottom)
{
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

/* Sets g_flightTextColorIndex: a code from 0x40 up, other than
 * g_flightTransparentColorIndex, is looked up in g_flightCharToColorLut;
 * anything else is the palette index itself. Does not check a code above
 * 0x5F against the table's 32 entries. */
// FUNCTION: XVT 0x4A9590
void FlightText_SetColor(unsigned int charOrIndex)
{
	if (charOrIndex >= 0x40u &&
	    charOrIndex != g_flightTransparentColorIndex) {
		g_flightTextColorIndex =
			g_flightCharToColorLut[charOrIndex - 0x40u];
	} else {
		g_flightTextColorIndex = (uint8_t)charOrIndex;
	}
}

/* Sets g_flightTextBgColor from a color code or palette index, as
 * FlightText_SetColor reads them, with the same missing check. */
// FUNCTION: XVT 0x4A95C0
void FlightText_SetBackgroundColor(unsigned int charOrIndex)
{
	if (charOrIndex >= 0x40u &&
	    charOrIndex != g_flightTransparentColorIndex) {
		g_flightTextBgColor =
			g_flightCharToColorLut[charOrIndex - 0x40u];
	} else {
		g_flightTextBgColor = (uint8_t)charOrIndex;
	}
}

/* Sets g_flightTextShadowColor from a color code or palette index, as
 * FlightText_SetColor reads them, with the same missing check. */
// FUNCTION: XVT 0x4A95F0
void FlightText_SetShadowColor(unsigned int charOrIndex)
{
	if (charOrIndex >= 0x40u &&
	    charOrIndex != g_flightTransparentColorIndex) {
		g_flightTextShadowColor =
			g_flightCharToColorLut[charOrIndex - 0x40u];
	} else {
		g_flightTextShadowColor = (uint8_t)charOrIndex;
	}
}

/* Sets g_flightWordWrapEnabled. */
// FUNCTION: XVT 0x4A9620
void FlightText_SetWordWrap(int16_t enabled)
{
	g_flightWordWrapEnabled = enabled;
}

/* Sets g_flightClearLineBgEnabled. */
// FUNCTION: XVT 0x4A9630
void FlightText_SetClearLineBackground(int16_t enabled)
{
	g_flightClearLineBgEnabled = enabled;
}

/* Selects the flight text font: stores tier in g_flightFontTier and, for
 * tiers 0 to 2, sets g_flightFontGlyphTableSw, g_flightFontGlyphStrideSw,
 * g_flightFontLineHeight, g_flightFontDigitWidth and, but for one case,
 * g_flightFontHasLowercase by g_flightResolutionMode. At 320x240 every tier
 * gets the micro font. Tier 0 gets the micro font at 480x360 and the small
 * one at 640x480; tiers 1 and 2 are alike: the small font at 480x360 and the
 * medium one at 640x480. The small font at 480x360 leaves
 * g_flightFontHasLowercase as it was. Any other tier or resolution mode
 * changes only g_flightFontTier. */
// FUNCTION: XVT 0x4A9640
void FlightText_SetFontTier(uint8_t tier)
{
	g_flightFontTier = tier;
	switch (tier) {
	case 0:
		switch (g_flightResolutionMode) {
		case FLIGHT_RESOLUTION_320X240:
		case FLIGHT_RESOLUTION_480X360:
			g_flightFontLineHeight = 5;
			g_flightFontHasLowercase = 0;
			g_flightFontDigitWidth = 3;
			g_flightFontGlyphStrideSw = 42;
			g_flightFontGlyphTableSw = g_flightFontMicroSw;
			break;
		case FLIGHT_RESOLUTION_640X480:
			g_flightFontLineHeight = 8;
			g_flightFontHasLowercase = 1;
			g_flightFontDigitWidth = 4;
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
			g_flightFontDigitWidth = 3;
			g_flightFontGlyphStrideSw = 42;
			g_flightFontGlyphTableSw = g_flightFontMicroSw;
			break;
		case FLIGHT_RESOLUTION_640X480:
			g_flightFontLineHeight = 10;
			g_flightFontHasLowercase = 1;
			g_flightFontDigitWidth = 5;
			g_flightFontGlyphStrideSw = 82;
			g_flightFontGlyphTableSw = g_flightFontMediumSw;
			break;
		case FLIGHT_RESOLUTION_480X360:
			g_flightFontLineHeight = 8;
			g_flightFontDigitWidth = 4;
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
			g_flightFontDigitWidth = 3;
			g_flightFontGlyphStrideSw = 42;
			g_flightFontGlyphTableSw = g_flightFontMicroSw;
			break;
		case FLIGHT_RESOLUTION_640X480:
			g_flightFontLineHeight = 10;
			g_flightFontHasLowercase = 1;
			g_flightFontDigitWidth = 5;
			g_flightFontGlyphStrideSw = 82;
			g_flightFontGlyphTableSw = g_flightFontMediumSw;
			break;
		case FLIGHT_RESOLUTION_480X360:
			g_flightFontLineHeight = 8;
			g_flightFontDigitWidth = 4;
			g_flightFontGlyphStrideSw = 66;
			g_flightFontGlyphTableSw = g_flightFontSmallSw;
			break;
		}
		break;
	}
}

/* Copies text into g_flightTextScratchBuffer, or empties it for NULL. Does
 * not check the buffer's 256-byte size. */
// FUNCTION: XVT 0x4A97F0
void FlightText_SetScratch(const char *text)
{
	char *destination;

	destination = g_flightTextScratchBuffer;
	if (text != 0) {
		while (*text != '\0') {
			*destination++ = *text++;
		}
	}
	*destination = '\0';
}

/* Appends text, or nothing for NULL, to g_flightTextScratchBuffer. Does not
 * check the buffer's 256-byte size. */
// FUNCTION: XVT 0x4A9820
void FlightText_AppendScratchString(const char *text)
{
	char *destination;

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

/* Appends one character to g_flightTextScratchBuffer. Does not check the
 * buffer's 256-byte size. */
// FUNCTION: XVT 0x4A9860
void FlightText_AppendScratchChar(uint8_t ch)
{
	char *destination;

	destination = g_flightTextScratchBuffer;
	while (*destination != '\0') {
		destination++;
	}
	destination[0] = ch;
	destination[1] = '\0';
}

/* Writes value in decimal, with a '-' in front when negative, over
 * g_flightTextScratchBuffer and returns the characters written, sign
 * included. Does not handle INT_MIN, whose negation overflows. */
// FUNCTION: XVT 0x4A9890
uint16_t FlightText_FormatScratchInt(int value)
{
	int magnitude;
	uint16_t digitCount;
	int expandedDigitCount;
	int remainingValue;
	int digitIndex;
	int16_t digit;
	int16_t isNegative;
	char *output;
	char *digitPosition;

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
				digitPosition[expandedDigitCount - 1] =
					(char)(digit + '0');
			} while (digitIndex < expandedDigitCount);
		}
	} else {
		digitCount = 1;
		*output = '0';
	}
	output[digitCount] = '\0';
	if (isNegative != 0) {
		++digitCount;
	}
	return digitCount;
}

/* Draws str at the text cursor through g_flightDrawCharFn. A 0xFE byte sets
 * the text color from the byte after it; a byte below 0x10, newline
 * included, sets the color to that value; both go through
 * FlightText_SetColor. With word wrap on, a space draws as a newline when
 * it and the word after it would end beyond g_flightClipRight - 2. Returns
 * at once for an empty string; does not check str for NULL. The modern build
 * reads at most 79 characters of that word; the original does not bound it,
 * and a word of 80 or more overruns the 80-byte buffer it is copied into. */
// FUNCTION: XVT 0x4A9960
void FlightText_DrawString(const char *str)
{
	char wordBuffer[80];
	uint16_t wordLength;
	const char *wordScan;
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
		} else if ((uint8_t)*str == ' ' &&
			   g_flightWordWrapEnabled != 0) {
			wordScan = str + 1;
			wordLength = 0;
			while (*wordScan != ' ' && *wordScan != '\0'
#ifdef XVT_MODERN
			       &&
			       wordLength < (uint16_t)(sizeof(wordBuffer) - 1u)
#endif
			) {
				wordBuffer[wordLength] = *wordScan;
				++wordLength;
				++wordScan;
			}
			wordBuffer[wordLength] = '\0';

			nextWordWidth =
				FlightText_MeasureStringWidth(wordBuffer);
			if (g_flightCursorX +
				    FlightText_MeasureStringWidth(" ") +
				    nextWordWidth >
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

/* Draws str through FlightText_DrawString on the cursor's row, centered
 * between g_flightClipLeft and g_flightClipRight. A start left of
 * g_flightClipLeft moves to it, but a start below 0 is kept. */
// FUNCTION: XVT 0x4A9A50
void FlightText_DrawStringCentered(const char *str)
{
	uint16_t halfWidth;
	uint16_t candidateX;
	int cursorY;

	halfWidth = FlightText_MeasureStringWidth(str) >> 1;
	candidateX =
		(uint16_t)(((int)g_flightClipRight + (int)g_flightClipLeft) /
				   2 -
			   halfWidth);
	if (candidateX < (int)g_flightClipLeft) {
		candidateX = g_flightClipLeft;
	}
	cursorY = g_flightCursorY;
	FlightText_SetCursor((int16_t)candidateX, (int16_t)cursorY);
	FlightText_DrawString(str);
}

/* Draws str through FlightText_DrawString on the cursor's row, ending 2
 * pixels short of g_flightClipRight. A start left of g_flightClipLeft, or
 * below 0, moves to g_flightClipLeft. */
// FUNCTION: XVT 0x4A9AC0
void FlightText_DrawStringRightAligned(const char *str)
{
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
