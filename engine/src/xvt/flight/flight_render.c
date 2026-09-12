#include "xvt/flight/flight_render.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40E580
void FlightRender_TransitionHookStub(void) {}

// FUNCTION: XVT 0x4A9B60
void FlightRender_InvokeTransitionHook(int transitionFlags) {
	(void)transitionFlags;
	g_flightRenderTransitionHook();
}

// FUNCTION: XVT 0x4A9B70
void FlightRender_ResetPalette(int transitionFlags) {
	(void)transitionFlags;
	g_flightResetPaletteFn();
}

// FUNCTION: XVT 0x411AF0
void FlightRender_ConfigureCallbacksForResolution(uint8_t initialGraphicsDetailPreset) {
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
	g_palettePackedMode = pixelMode;
	if (g_flight16bppBytesPerPixel == 2)
		pixelMode = 2;
	g_palettePackedMode = pixelMode;
	g_flightViewportMode = 1;
	FlightRender_InstallCallbacks(pixelMode);
	g_flightGraphicsDetailPreset = initialGraphicsDetailPreset;
}

// FUNCTION: XVT 0x411B60
void FlightRender_InstallCallbacks(int pixelMode) {
	switch (pixelMode) {
		case 1: {
			g_flightInitLineBufferFn = FlightSw_InitLineBuffer;
			g_flightRenderTransitionHook = FlightRender_TransitionHookStub;
			g_flightResetPaletteFn = FlightPalette_Reset;
			g_flightSetPaletteRangeFn = FlightPalette_SetRange;
			g_flightGetPaletteFn = FlightPalette_GetFull;
			g_flightSetPaletteFn = FlightPalette_SetFull;
			g_flightComputePixelOffsetFn = FlightSw_ComputePixelOffset8bpp;
			g_flightBlitSpriteFn = FlightSw_BlitSpriteRle8bpp;
			g_flightBlitSpriteFadedFn = FlightSw_BlitSpriteRleFaded8bpp;
			g_flightDrawCharFn = FlightText_DrawSoftwareGlyph8bpp;
			g_flightFillClipRectFn = FlightSw_FillClipRect8bpp;
			g_flightFillRectClippedFn = FlightSw_FillRectClipped8bpp;
			g_flightSaveScreenRectFn = (FlightScreenRectFn)FlightSw_SaveScreenRect8bpp;
			g_flightRestoreScreenRectFn = (FlightScreenRectFn)FlightSw_RestoreScreenRect8bpp;
			g_flightDrawPointArrayFn = FlightSw_DrawPointArray8bpp;
			g_flightDrawPointArrayMaskedFn = FlightSw_DrawPointArrayMasked8bpp;
			g_flightDrawPixelFn = FlightSw_DrawPixel8bpp;
			g_flightSaveDrawCursorFn = FlightSw_DrawRadarTargetMarker8bpp;
			g_flightRestoreCursorFn = FlightSw_RestoreRadarTargetMarker8bpp;
			g_flightDrawLineFn = FlightSw_DrawLine8bpp;
			FlightRender_SetPixelModeStub(pixelMode);
			break;
		}
		case 2: {
			g_flightInitLineBufferFn = FlightSw_InitLineBuffer;
			g_flightRenderTransitionHook = FlightRender_TransitionHookStub;
			g_flightResetPaletteFn = FlightPalette_Reset;
			g_flightSetPaletteRangeFn = FlightPalette_SetRange;
			g_flightGetPaletteFn = FlightPalette_GetFull;
			g_flightSetPaletteFn = FlightPalette_SetFull;
			g_flightComputePixelOffsetFn = FlightSw_ComputePixelOffset;
			g_flightBlitSpriteFn = FlightSw_BlitSpriteRle;
			g_flightBlitSpriteFadedFn = FlightSw_BlitSpriteRleFaded;
			g_flightDrawCharFn = FlightText_DrawSoftwareGlyph;
			g_flightFillClipRectFn = FlightSw_FillClipRect;
			g_flightFillRectClippedFn = FlightSw_FillRectClipped;
			g_flightSaveScreenRectFn = (FlightScreenRectFn)FlightSw_SaveScreenRect;
			g_flightRestoreScreenRectFn = (FlightScreenRectFn)FlightSw_RestoreScreenRect;
			g_flightDrawPointArrayFn = FlightSw_DrawPointArray;
			g_flightDrawPointArrayMaskedFn = FlightSw_DrawPointArrayMasked;
			g_flightDrawPixelFn = FlightSw_DrawPixel;
			g_flightSaveDrawCursorFn = FlightSw_DrawRadarTargetMarker;
			g_flightRestoreCursorFn = FlightSw_RestoreRadarTargetMarker;
			g_flightDrawLineFn = FlightSw_DrawLine;
			FlightRender_SetPixelModeStub(pixelMode);
			break;
		}
		default:
			FlightRender_SetPixelModeStub(pixelMode);
			break;
		case 0: {
			g_flightInitLineBufferFn = FlightSw_InitLineBuffer;
			g_flightRenderTransitionHook = FlightRender_TransitionHookStub;
			g_flightResetPaletteFn = FlightPalette_Reset;
			g_flightSetPaletteRangeFn = FlightPalette_SetRange;
			g_flightGetPaletteFn = FlightPalette_GetFull;
			g_flightSetPaletteFn = FlightPalette_SetFull;
			g_flightComputePixelOffsetFn = FlightSw_ComputePixelOffset8bpp;
			g_flightBlitSpriteFn = FlightSw_BlitSpriteRle8bpp;
			g_flightBlitSpriteFadedFn = FlightSw_BlitSpriteRleFaded8bpp;
			g_flightDrawCharFn = FlightText_DrawSoftwareGlyph8bpp;
			g_flightFillClipRectFn = FlightSw_FillClipRect8bpp;
			g_flightFillRectClippedFn = FlightSw_FillRectClipped8bpp;
			g_flightSaveScreenRectFn = (FlightScreenRectFn)FlightSw_SaveScreenRect8bpp;
			g_flightRestoreScreenRectFn = (FlightScreenRectFn)FlightSw_RestoreScreenRect8bpp;
			g_flightDrawPointArrayFn = FlightSw_DrawPointArray8bpp;
			g_flightDrawPointArrayMaskedFn = FlightSw_DrawPointArrayMasked8bpp;
			g_flightDrawPixelFn = FlightSw_DrawPixel8bpp;
			g_flightSaveDrawCursorFn = FlightSw_DrawRadarTargetMarker8bpp;
			g_flightRestoreCursorFn = FlightSw_RestoreRadarTargetMarker8bpp;
			g_flightDrawLineFn = FlightSw_DrawLine8bpp;
			FlightRender_SetPixelModeStub(pixelMode);
			break;
		}
	}
}

// FUNCTION: XVT 0x426C40
void FlightRender_SetPixelModeStub(int pixelMode) { (void)pixelMode; }
