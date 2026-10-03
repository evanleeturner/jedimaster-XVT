#include "xvt/flight/flight_render.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"

/* Does nothing. FlightRender_InstallCallbacks makes it the transition hook
 * (g_flightRenderTransitionHook) in every mode. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40E580
void FlightRender_TransitionHookStub(void) {}

/* Calls g_flightRenderTransitionHook; the argument is ignored. */
// FUNCTION: XVT 0x4A9B60
void FlightRender_InvokeTransitionHook(int transitionFlags)
{
	(void)transitionFlags;
	g_flightRenderTransitionHook();
}

/* Calls g_flightResetPaletteFn; the argument is ignored. */
// FUNCTION: XVT 0x4A9B70
void FlightRender_ResetPalette(int transitionFlags)
{
	(void)transitionFlags;
	g_flightResetPaletteFn();
}

/* Picks the software drawing mode for the resolution and installs its
 * drawing functions: g_flightPixelMode is 0 at 320x240 or an unknown mode,
 * 1 at 640x480 and 480x360, and 2 at 640x480 in 16 bits or whenever
 * g_flightBytesPerPixel is 2. Also sets g_flightViewportMode to 1 and
 * g_flightGraphicsDetailPreset to initialGraphicsDetailPreset; both callers,
 * at flight start, pass 3. */
// FUNCTION: XVT 0x411AF0
void FlightRender_ConfigureCallbacksForResolution(
	uint8_t initialGraphicsDetailPreset)
{
	uint8_t pixelMode;

	switch (g_flightResolutionMode) {
	case FLIGHT_RESOLUTION_320X240:
		pixelMode = 0;
		break;
	case FLIGHT_RESOLUTION_640X480:
	case FLIGHT_RESOLUTION_480X360:
		pixelMode = 1;
		break;
	case FLIGHT_RESOLUTION_640X480_16BPP:
		pixelMode = 2;
		break;
	default:
		pixelMode = 0;
		break;
	}
	g_flightPixelMode = pixelMode;
	if (g_flightBytesPerPixel == 2) {
		pixelMode = 2;
	}
	g_flightPixelMode = pixelMode;
	g_flightViewportMode = 1;
	FlightRender_InstallCallbacks(pixelMode);
	g_flightGraphicsDetailPreset = initialGraphicsDetailPreset;
}

/* Sets the 20 software drawing function pointers, g_flightInitLineBufferFn
 * to g_flightDrawLineFn: the 8-bit drawing functions for pixelMode 0 or 1,
 * the same set for both, or the 16-bit ones for 2. The palette functions,
 * the transition hook and g_flightInitLineBufferFn are the same in all
 * three. Any other pixelMode changes nothing. */
// FUNCTION: XVT 0x411B60
void FlightRender_InstallCallbacks(int pixelMode)
{
	switch (pixelMode) {
	case 1: {
		g_flightInitLineBufferFn = FlightSw_InitFramebuffer;
		g_flightRenderTransitionHook = FlightRender_TransitionHookStub;
		g_flightResetPaletteFn = FlightPalette_ApplyToDisplay;
		g_flightSetPaletteRangeFn = FlightPalette_SetRange;
		g_flightGetPaletteFn = FlightPalette_GetFull;
		g_flightSetPaletteFn = FlightPalette_SetFull;
		g_flightComputePixelOffsetFn = FlightSw_ComputePixelOffset8bpp;
		g_flightBlitSpriteFn = FlightSw_BlitSpriteRle8bpp;
		g_flightBlitSpriteFadedFn = FlightSw_BlitSpriteRleFaded8bpp;
		g_flightDrawCharFn = FlightText_DrawWideGlyph8bpp;
		g_flightFillClipRectFn = FlightSw_FillClipRect8bpp;
		g_flightFillRectClippedFn = FlightSw_FillRectClipped8bpp;
		g_flightSaveScreenRectFn =
			(FlightScreenRectFn)FlightSw_SaveScreenRect8bpp;
		g_flightRestoreScreenRectFn =
			(FlightScreenRectFn)FlightSw_RestoreScreenRect8bpp;
		g_flightDrawPointArrayFn = FlightSw_DrawPointArray8bpp;
		g_flightDrawPointArrayMaskedFn = FlightSw_ErasePointArray8bpp;
		g_flightDrawPixelFn = FlightSw_DrawPixel8bpp;
		g_flightDrawRadarTargetMarkerFn =
			FlightSw_DrawRadarTargetMarker8bpp;
		g_flightRestoreRadarTargetMarkerFn =
			FlightSw_RestoreRadarTargetMarker8bpp;
		g_flightDrawLineFn = FlightSw_DrawLine8bpp;
		FlightRender_SetPixelModeStub(pixelMode);
		break;
	}
	case 2: {
		g_flightInitLineBufferFn = FlightSw_InitFramebuffer;
		g_flightRenderTransitionHook = FlightRender_TransitionHookStub;
		g_flightResetPaletteFn = FlightPalette_ApplyToDisplay;
		g_flightSetPaletteRangeFn = FlightPalette_SetRange;
		g_flightGetPaletteFn = FlightPalette_GetFull;
		g_flightSetPaletteFn = FlightPalette_SetFull;
		g_flightComputePixelOffsetFn = FlightSw_ComputePixelOffset;
		g_flightBlitSpriteFn = FlightSw_BlitSpriteRle16bpp;
		g_flightBlitSpriteFadedFn = FlightSw_BlitSpriteRleFaded16bpp;
		g_flightDrawCharFn = FlightText_DrawWideGlyph;
		g_flightFillClipRectFn = FlightSw_FillClipRect16bpp;
		g_flightFillRectClippedFn = FlightSw_FillRectClipped16bpp;
		g_flightSaveScreenRectFn =
			(FlightScreenRectFn)FlightSw_SaveScreenRect16bpp;
		g_flightRestoreScreenRectFn =
			(FlightScreenRectFn)FlightSw_RestoreScreenRect16bpp;
		g_flightDrawPointArrayFn = FlightSw_DrawPointArray16bpp;
		g_flightDrawPointArrayMaskedFn = FlightSw_ErasePointArray16bpp;
		g_flightDrawPixelFn = FlightSw_DrawPixel16bpp;
		g_flightDrawRadarTargetMarkerFn =
			FlightSw_DrawRadarTargetMarker16bpp;
		g_flightRestoreRadarTargetMarkerFn =
			FlightSw_RestoreRadarTargetMarker16bpp;
		g_flightDrawLineFn = FlightSw_DrawLine16bpp;
		FlightRender_SetPixelModeStub(pixelMode);
		break;
	}
	default:
		FlightRender_SetPixelModeStub(pixelMode);
		break;
	case 0: {
		g_flightInitLineBufferFn = FlightSw_InitFramebuffer;
		g_flightRenderTransitionHook = FlightRender_TransitionHookStub;
		g_flightResetPaletteFn = FlightPalette_ApplyToDisplay;
		g_flightSetPaletteRangeFn = FlightPalette_SetRange;
		g_flightGetPaletteFn = FlightPalette_GetFull;
		g_flightSetPaletteFn = FlightPalette_SetFull;
		g_flightComputePixelOffsetFn = FlightSw_ComputePixelOffset8bpp;
		g_flightBlitSpriteFn = FlightSw_BlitSpriteRle8bpp;
		g_flightBlitSpriteFadedFn = FlightSw_BlitSpriteRleFaded8bpp;
		g_flightDrawCharFn = FlightText_DrawWideGlyph8bpp;
		g_flightFillClipRectFn = FlightSw_FillClipRect8bpp;
		g_flightFillRectClippedFn = FlightSw_FillRectClipped8bpp;
		g_flightSaveScreenRectFn =
			(FlightScreenRectFn)FlightSw_SaveScreenRect8bpp;
		g_flightRestoreScreenRectFn =
			(FlightScreenRectFn)FlightSw_RestoreScreenRect8bpp;
		g_flightDrawPointArrayFn = FlightSw_DrawPointArray8bpp;
		g_flightDrawPointArrayMaskedFn = FlightSw_ErasePointArray8bpp;
		g_flightDrawPixelFn = FlightSw_DrawPixel8bpp;
		g_flightDrawRadarTargetMarkerFn =
			FlightSw_DrawRadarTargetMarker8bpp;
		g_flightRestoreRadarTargetMarkerFn =
			FlightSw_RestoreRadarTargetMarker8bpp;
		g_flightDrawLineFn = FlightSw_DrawLine8bpp;
		FlightRender_SetPixelModeStub(pixelMode);
		break;
	}
	}
}

/* Does nothing; FlightRender_InstallCallbacks calls it last in every
 * mode. */
// FUNCTION: XVT 0x426C40
void FlightRender_SetPixelModeStub(int pixelMode) { (void)pixelMode; }
