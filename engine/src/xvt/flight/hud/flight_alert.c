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

// GLOBAL: XVT 0x5233E4
int g_flightAlertBoxVerticalOffset = 0;
// GLOBAL: XVT 0x5236A0
void *g_flightAlertBoxSavedPixels = 0;
// GLOBAL: XVT 0x5236A4
int g_flightAlertBoxSavedBytes = 0;

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
