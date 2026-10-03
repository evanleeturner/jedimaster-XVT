#include "xvt/flight/hud/flight_alert.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/render_capture.h"
#endif

#include "xvt/flight/flight_display.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"

#include <stdlib.h>

/* Pixels added to g_surfaceHeight before halving it to place the alert box's
 * vertical center. Nothing writes it, so it stays 0 and the box sits at the
 * surface's vertical middle. */
// GLOBAL: XVT 0x5233E4
int g_flightAlertBoxVerticalOffset = 0;
/* Heap copy of the screen under the alert box. Only
 * FlightAlert_SaveBoxBackground writes it: it allocates it on first use and
 * frees and reallocates it when a box needs more bytes; nothing else frees
 * it. NULL until then, or after a failed allocation; while it is NULL,
 * FlightAlert_DrawBox and FlightAlert_RestoreBoxBackground draw nothing. */
// GLOBAL: XVT 0x5236A0
void *g_flightAlertBoxSavedPixels = 0;
/* Bytes allocated for g_flightAlertBoxSavedPixels; only
 * FlightAlert_SaveBoxBackground writes it. A failed reallocation leaves the
 * old size here while the pointer is NULL. */
// GLOBAL: XVT 0x5236A4
int g_flightAlertBoxSavedBytes = 0;

/* Saves the screen under the alert box that network waits draw over the
 * flight view, so FlightAlert_RestoreBoxBackground can put it back. The box
 * is half as wide as the span from g_flightViewportInsetX to g_surfaceWidth
 * and centered on it, five lines of font tier 0 tall, centered at half of
 * g_surfaceHeight plus g_flightAlertBoxVerticalOffset, and saved with a
 * one-pixel border. Sets font tier 0. Allocates g_flightAlertBoxSavedPixels,
 * or a larger one when the box needs more than g_flightAlertBoxSavedBytes,
 * and returns without saving when the allocation fails. Then calls
 * FlightDisplay_Flip, copies the box out of the frame buffer with
 * g_flightDrawToHudLayer at 0, sets g_flightDrawToHudLayer to 1 and calls
 * FlightDisplay_Flip again. The modern build first clears the alert it
 * records for its renderer. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x448DA0
void FlightAlert_SaveBoxBackground(void)
{
	int boxX;
	int boxY;
	int boxWidth;
	int boxHeight;
	int pixelCount;

	FlightText_SetFontTier(0);
	boxWidth = (unsigned int)(g_surfaceWidth - g_flightViewportInsetX) >> 1;
	boxHeight = 5 * g_flightFontLineHeight;
	boxX = ((unsigned int)(g_flightViewportInsetX + g_surfaceWidth) >> 1) -
	       boxWidth / 2 - 1;
	boxY = ((unsigned int)(g_flightAlertBoxVerticalOffset +
			       g_surfaceHeight) >>
		1) -
	       boxHeight / 2 - 1;
	boxWidth += 2;
	boxHeight += 2;

	if (g_flightAlertBoxSavedPixels == 0) {
		pixelCount = boxWidth * boxHeight;
		g_flightAlertBoxSavedPixels =
			malloc(pixelCount * g_flightBytesPerPixel);
		if (g_flightAlertBoxSavedPixels == 0) {
			return;
		}
		g_flightAlertBoxSavedBytes = pixelCount * g_flightBytesPerPixel;
	} else {
		pixelCount = boxWidth * boxHeight;
		if ((unsigned int)(pixelCount * g_flightBytesPerPixel) >
		    (unsigned int)g_flightAlertBoxSavedBytes) {
			free(g_flightAlertBoxSavedPixels);
			g_flightAlertBoxSavedPixels =
				malloc(pixelCount * g_flightBytesPerPixel);
			if (g_flightAlertBoxSavedPixels == 0) {
				return;
			}
			g_flightAlertBoxSavedBytes =
				pixelCount * g_flightBytesPerPixel;
		}
	}

#ifdef XVT_MODERN
	XvtCockpitMessages_BeginAlert();
	XvtRenderCapture_BeginOverlay();
#endif
	FlightDisplay_Flip();
	g_flightDrawToHudLayer = 0;
	FlightSurface_Lock();
	g_flightSaveScreenRectFn(g_flightAlertBoxSavedPixels, (uint16_t)boxX,
				 (uint16_t)boxY, (uint16_t)boxWidth,
				 (uint16_t)boxHeight);
	FlightSurface_Unlock();
	g_flightDrawToHudLayer = 1;
	FlightDisplay_Flip();

#ifdef XVT_MODERN
	XvtRenderCapture_EndOverlay();
#endif
}

/* Puts back the screen FlightAlert_SaveBoxBackground saved under the alert
 * box, which removes the alert. Sets font tier 0 and computes the box again
 * from the same globals; when nothing was saved it stops there. Otherwise it
 * calls FlightDisplay_Flip, writes the saved pixels into the frame buffer with
 * g_flightDrawToHudLayer at 0, sets g_flightDrawToHudLayer to 1 and calls
 * FlightDisplay_Flip again; the modern build also ends its recorded alert.
 * Keeps the saved copy. Does not check that the box is still the size it
 * saved. */
// FUNCTION: XVT 0x448ED0
void FlightAlert_RestoreBoxBackground(void)
{
	int boxX;
	int boxY;
	int boxWidth;
	int boxHeight;
	int viewportInsetX;
	int surfaceWidth;
	FlightScreenRectFn restoreScreenRect;

	FlightText_SetFontTier(0);
	surfaceWidth = g_surfaceWidth;
	viewportInsetX = g_flightViewportInsetX;
	boxWidth = (unsigned int)(surfaceWidth - viewportInsetX) >> 1;
	boxHeight = 5 * g_flightFontLineHeight;
	boxX = ((unsigned int)(viewportInsetX + surfaceWidth) >> 1) -
	       boxWidth / 2 - 1;
	boxY = ((unsigned int)(g_flightAlertBoxVerticalOffset +
			       g_surfaceHeight) >>
		1) -
	       boxHeight / 2 - 1;
	boxWidth += 2;
	boxHeight += 2;
	if (g_flightAlertBoxSavedPixels != 0) {

#ifdef XVT_MODERN
		XvtRenderCapture_BeginOverlay();
#endif
		FlightDisplay_Flip();
		g_flightDrawToHudLayer = 0;
		FlightSurface_Lock();
		restoreScreenRect = g_flightRestoreScreenRectFn;
		restoreScreenRect(g_flightAlertBoxSavedPixels, (uint16_t)boxX,
				  (uint16_t)boxY, (uint16_t)boxWidth,
				  (uint16_t)boxHeight);
#ifdef XVT_MODERN
		XvtCockpitMessages_EndAlert();
#endif
		FlightSurface_Unlock();
		g_flightDrawToHudLayer = 1;
		FlightDisplay_Flip();
#ifdef XVT_MODERN
		XvtRenderCapture_EndOverlay();
#endif
	}
}

/* Writes one line of text, centered, into the alert box on the frame buffer,
 * between two calls to FlightDisplay_Flip, with g_flightDrawToHudLayer at 0
 * while drawing and 1 after. Line N sits N lines of font tier 0 below the
 * box's top; textRow 1 starts a new alert: it fills the box and a one-pixel
 * border with color bgColor + 2, then the box with bgColor, and writes on
 * line 1. Row 0 does the same without the border. Any other row fills the box
 * from that line down with bgColor, which clears later lines, and writes
 * there. Colors pass through FlightText_SetBackgroundColor; the text is
 * palette index 0x2F with a 0x2C shadow. Leaves font tier 0,
 * g_flightTextShadowEnabled at 1 and the text clip on the box. Returns
 * without drawing when FlightAlert_SaveBoxBackground has saved nothing. The
 * modern build also records the line for its renderer. */
// FUNCTION: XVT 0x448F90
void FlightAlert_DrawBox(int textRow, char *text, uint8_t bgColor)
{
	int boxX;
	int boxY;
	int boxWidth;
	int boxHeight;
	unsigned int backgroundColor;

	FlightText_SetFontTier(0);
	boxWidth = (unsigned int)(g_surfaceWidth - g_flightViewportInsetX) >> 1;
	boxX = ((unsigned int)(g_flightViewportInsetX + g_surfaceWidth) >> 1) -
	       boxWidth / 2;
	boxHeight = 5 * g_flightFontLineHeight;
	boxY = ((unsigned int)(g_flightAlertBoxVerticalOffset +
			       g_surfaceHeight) >>
		1) -
	       boxHeight / 2;
	if (g_flightAlertBoxSavedPixels == 0) {
		return;
	}

	FlightText_SetColor('/');
	backgroundColor = bgColor;
	FlightText_SetBackgroundColor(backgroundColor + 2);
	g_flightTextShadowEnabled = 1;
	FlightText_SetShadowColor(',');

#ifdef XVT_MODERN
	XvtRenderCapture_BeginOverlay();
#endif
	FlightDisplay_Flip();
	g_flightDrawToHudLayer = 0;
	FlightSurface_Lock();

#ifdef XVT_MODERN
	XvtCockpitMessages_BeginAlertLine(textRow, boxX, boxY, boxWidth,
					  boxHeight);
#endif
	if (textRow == 1) {
		FlightText_SetClipRect(boxX - 1, boxY - 1, boxX + boxWidth + 1,
				       boxY + boxHeight + 1);
		g_flightFillClipRectFn();
		textRow = 0;
	}
	FlightText_SetBackgroundColor(backgroundColor);
	FlightText_SetClipRect(boxX, boxY + textRow * g_flightFontLineHeight,
			       boxX + boxWidth, boxY + boxHeight);
	g_flightFillClipRectFn();
	if (textRow == 0) {
		textRow = 1;
	}
	FlightText_SetCursor(boxX + g_flightFontLineHeight,
			     boxY + textRow * g_flightFontLineHeight);
	FlightText_DrawStringCentered(text);
#ifdef XVT_MODERN
	XvtCockpitMessages_EndAlertLine();
#endif
	FlightSurface_Unlock();
	g_flightDrawToHudLayer = 1;
	FlightDisplay_Flip();

#ifdef XVT_MODERN
	XvtRenderCapture_EndOverlay();
#endif
}
