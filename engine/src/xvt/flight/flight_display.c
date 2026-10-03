#include "xvt/flight/flight_display.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/cockpit_capture.h"
#include "xvt_runtime/snapshot/render_capture.h"
#endif
#include "xvt/assets/model_texture.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/transfm2.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/net/net_session.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/util/debug_console.h"
#include "xvt/util/time.h"

#include <stdio.h>
#include <string.h>

#ifndef XVT_MODERN
__declspec(dllimport) int __cdecl wsprintfA(char *buffer, const char *format,
					    ...);
__declspec(dllimport) void __stdcall
OutputDebugStringA(const char *outputString);
__declspec(dllimport) int __stdcall MessageBoxA(void *hWnd, const char *text,
						const char *caption,
						unsigned int type);
int __cdecl inp(unsigned short port);
int __cdecl outp(unsigned short port, int value);
#endif

/* The DirectDraw primary surface: the screen. FlightDisplay_Init creates it; in
 * a window it is created only while needed (FlightDisplay_Init reads its
 * format, FlightDisplay_Flip copies the frame) and released after.
 * FlightDisplay_CleanupAndReportError and the flight shutdown (Flight_Main in
 * the original build, XvtFlightEntry_Cleanup in the modern one) release it and
 * set NULL. */
// GLOBAL: XVT 0x66E700
IDirectDrawSurface *g_flightPrimarySurface;
/* Without page flipping, the memory the cockpit and HUD layer is drawn in
 * (FlightSurface_Lock hands it out while g_flightDrawToHudLayer is set);
 * FlightDisplay_BlitRenderSurface copies 480 rows of it into
 * g_flightSoftwareFramebuffer. */
// GLOBAL: XVT 0x66E710
uint8_t g_flightHudStagingBuffer[640 * 480 * 2];
/* Element 0 is the primary surface pitch in bytes. Element 1, which nothing reads, is 2 when the driver
 * can color key with a destination key but not a source key, else 1. */
// GLOBAL: XVT 0x803B70
int g_flightPrimaryPitch[2];
/* The surface the 3D view is drawn on and the next flip shows: the primary
 * surface's attached back buffer with page flipping, else the primary itself.
 * Set by FlightDisplay_Init in fullscreen only; XvtFlightEntry_Cleanup releases
 * it and sets NULL in the modern build. */
// GLOBAL: XVT 0x803F80
IDirectDrawSurface *g_flightBackBuffer;
/* Without page flipping, the frame the 3D view is drawn in; FlightDisplay_Flip
 * copies 480 rows of the primary pitch from it to the screen. */
// GLOBAL: XVT 0x803F90
uint8_t g_flightSoftwareFramebuffer[640 * 480 * 2];
/* 1 for exclusive fullscreen with a display mode change, 0 for a window. Starts
 * at 1; the launch options "nofullscreen" and "fullscreen" set it, read by
 * Flight_Main in the original build and XvtFlightEntry_ReadLaunchSwitches in
 * the modern one. */
// GLOBAL: XVT 0x527EA8
int g_flightFullscreen = 1;
/* 1 when FlightDisplay_Flip times its flips to the display's refresh. Starts at
 * 0; at flight start it becomes 0 when a file named flicker.txt can be opened,
 * else 1 (Flight_Main in the original build, XvtFlightEntry_ReadLaunchSwitches
 * in the modern one). */
// GLOBAL: XVT 0x527EA4
int g_flightConfFlicker = 0;
/* Display mode width copied before the flight's display is set up (Flight_Main
 * in the original build, XvtFlightEntry_ConfigureDisplaySize in the modern
 * one); FeDiskIo_InitGlobalBuffers reads it. Starts at 640. */
// GLOBAL: XVT 0x527EBC
int g_renderTargetWidth = 640;
/* g_flightBytesPerPixel as it stood before FlightDisplay_Init, which may change
 * it; copied by Flight_Main in the original build and XvtFlightEntry_Configure
 * in the modern one, and read by FeDiskIo_InitGlobalBuffers. Starts at 1. */
// GLOBAL: XVT 0x527EC0
int g_requestedFlightBytesPerPixel = 1;
/* g_useHardware3D as it stood before FlightDisplay_Init, which may clear it;
 * copied and read by the same functions as g_requestedFlightBytesPerPixel.
 * Starts at 1. */
// GLOBAL: XVT 0x527EC4
int g_requestedFlightHardware3D = 1;
/* timeGetTime, in ms, of the last vertical blank FlightDisplay_Flip waited for
 * when timing flips; 0 until the first. Only FlightDisplay_Flip writes it. */
// GLOBAL: XVT 0x527F7C
uint32_t g_flightFlickerLastSyncTimeMs = 0;
/* Width, out of 100,000 parts of a refresh, of the window at either end of the
 * refresh in which FlightDisplay_Flip waits for a vertical blank. Nothing
 * changes it from 20,000. */
// GLOBAL: XVT 0x527F80
int g_flightFlickerPhaseWindow = 20000;
/* Rate by which FlightDisplay_Flip turns elapsed ms into a position within the
 * refresh: the display frequency from GetMonitorFrequency, or, when that fails,
 * 10,000,000 divided by the ms 100 refreshes took. Only FlightDisplay_Flip
 * writes it. */
// GLOBAL: XVT 0x622CB0
int g_flightFlickerRefreshRateScale = 0;
/* The 256-entry DirectDraw palette attached to the primary surface in 8-bit
 * fullscreen; NULL otherwise. Created by FlightDisplay_Init;
 * FlightDisplay_CleanupAndReportError and the flight shutdown release it and
 * set NULL. */
// GLOBAL: XVT 0x66DDD8
IDirectDrawPalette *g_flightPalette = NULL;
/* g_flightBackBuffer with page flipping, else g_flightPrimarySurface; set by
 * FlightDisplay_Init in fullscreen only and cleared by XvtFlightEntry_Cleanup.
 * Nothing reads it. */
// GLOBAL: XVT 0x66DDEC
IDirectDrawSurface *g_flightRenderSurface;
/* Text FlightDisplay_CleanupAndReportError formats its error line into before
 * sending it to the debugger output; nothing else uses it. */
// GLOBAL: XVT 0x66E200
static char g_flightDisplayDebugMessage[1280] = {0};
/* Bytes in one bank of the screen memory: the original build's software drawing
 * functions split an offset into bank and offset by it when they draw at the
 * software framebuffer base outside 320x240.
 * FlightDisplay_ConfigureResolutionState sets it to 480 rows of the primary
 * pitch, or 0x10000 at 320x240 and in an unknown mode. Starts at 0xF000. */
// GLOBAL: XVT 0x5233CC
unsigned int g_vesaPageSizeBytes = 0xF000;
/* Set to 1 by FlightDisplay_ConfigureResolutionState; starts at 15. Nothing
 * reads it. */
// GLOBAL: XVT 0x5233D0
unsigned int g_vesaGrainsPerPage = 15;
/* Folder the cockpit art is loaded from: "CP640\" at start, its digits set by
 * FlightDisplay_ConfigureResolutionState to "CP320\", "CP640\" or "CP480\" for
 * the resolution. Read by the HUD's cockpit loaders. */
// GLOBAL: XVT 0x52155C
char g_hudCockpitResolutionDirectory[7] = "CP640\\";
/* 240 or 480 for the resolution, set by FlightDisplay_ConfigureResolutionState.
 * Nothing reads it. */
// GLOBAL: XVT 0x9A7B4C
int g_flightResolutionLegacyExtent = 0;

/* Sets the drawing state for g_flightResolutionMode: g_screenWidth and
 * g_screenHeight (320x240, 640x480 or 480x360, and 320x240 for any other mode),
 * g_surfacePitch from FlightDisplay_GetPrimarySurfacePitch, g_projScaleInt (256
 * or 512), g_projScaleHalfInt, g_perspectiveShift, g_projAspectY (0),
 * g_flightResolutionLegacyExtent, the digits of
 * g_hudCockpitResolutionDirectory, g_vesaPageSizeBytes and
 * g_vesaGrainsPerPage. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x447D90
void FlightDisplay_ConfigureResolutionState(void)
{
	int primarySurfacePitch;
	int resolutionMode;

	primarySurfacePitch = FlightDisplay_GetPrimarySurfacePitch();
	resolutionMode = g_flightResolutionMode;
	g_vesaPageSizeBytes = 480 * primarySurfacePitch;
	g_vesaGrainsPerPage = 1;
	switch (resolutionMode) {
	case FLIGHT_RESOLUTION_320X240:
		g_vesaPageSizeBytes = 0x10000;
		g_vesaGrainsPerPage = 1;
		g_screenWidth = 320;
		g_screenHeight = 240;
		g_flightResolutionLegacyExtent = 240;
		g_surfacePitch = FlightDisplay_GetPrimarySurfacePitch();
		g_projScaleInt = 256;
		g_projScaleHalfInt = 128;
		g_perspectiveShift = 8;
		g_hudCockpitResolutionDirectory[2] = '3';
		g_hudCockpitResolutionDirectory[3] = '2';
		g_projAspectY = 0;
		break;

	case FLIGHT_RESOLUTION_640X480:
		g_screenWidth = 640;
		g_screenHeight = 480;
		g_flightResolutionLegacyExtent = 480;
		g_surfacePitch = FlightDisplay_GetPrimarySurfacePitch();
		g_projScaleInt = 512;
		g_projScaleHalfInt = 256;
		g_perspectiveShift = 9;
		g_hudCockpitResolutionDirectory[2] = '6';
		g_hudCockpitResolutionDirectory[3] = '4';
		g_projAspectY = 0;
		break;

	case FLIGHT_RESOLUTION_480X360:
		g_screenHeight = 360;
		g_screenWidth = 480;
		g_flightResolutionLegacyExtent = 480;
		g_surfacePitch = FlightDisplay_GetPrimarySurfacePitch();
		g_projScaleInt = 512;
		g_projScaleHalfInt = 256;
		g_perspectiveShift = 9;
		g_hudCockpitResolutionDirectory[2] = '4';
		g_hudCockpitResolutionDirectory[3] = '8';
		g_projAspectY = 0;
		break;

	default:
		g_vesaPageSizeBytes = 0x10000;
		g_screenWidth = 320;
		g_screenHeight = 240;
		g_flightResolutionLegacyExtent = 240;
		g_surfacePitch = FlightDisplay_GetPrimarySurfacePitch();
		g_projScaleInt = 256;
		g_projScaleHalfInt = 128;
		g_perspectiveShift = 8;
		g_hudCockpitResolutionDirectory[2] = '3';
		g_hudCockpitResolutionDirectory[3] = '2';
		g_projAspectY = 0;
		break;
	}
}

/* Returns 1 and does nothing else. Its callers treat 0 as a failure (error 12
 * in FlightDisplay_Init), which never comes. */
// FUNCTION: XVT 0x4AAFE0
int FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub(void) { return 1; }

/* The start of the driver capability block FlightDisplay_Init asks DirectDraw
 * for with GetCaps. */
typedef struct FlightDisplayDriverCaps {
	uint32_t dwSize; /* Size of this block, set before GetCaps. */
	/* Capability bits; FlightDisplay_Init tests 0x400000. */
	uint32_t dwCaps;
	uint32_t dwCaps2; /* Filled by GetCaps; nothing reads it. */
	/* Color key bits; FlightDisplay_Init tests 0x1 and 0x200. */
	uint32_t dwCKeyCaps;
	uint32_t reserved[87]; /* The rest of the driver's block, unread. */
} FlightDisplayDriverCaps;

/* One palette entry as FlightDisplay_Init hands it to CreatePalette. */
typedef struct FlightDisplayPaletteEntry {
	/* Red, 0 to 255; FlightDisplay_Init sets the entry's index. */
	uint8_t red;
	uint8_t green; /* Green, set like red. */
	uint8_t blue;  /* Blue, set like red. */
	uint8_t flags; /* Never set: CreatePalette gets what the stack held. */
} FlightDisplayPaletteEntry;

enum { FLIGHT_DDPCAPS_INITIALIZE = 0x8 };

/* Sets up DirectDraw for flight on the frontend's DirectDraw object
 * (g_flightDirectDraw): exclusive fullscreen or a normal window by
 * g_flightFullscreen. In fullscreen it sets the display mode g_displayModeWidth
 * by g_displayModeHeight at 8 times g_flightBytesPerPixel bits (2 bytes with
 * g_useHardware3D). When refused, 320 wide tries 512x384 and then 640x480, 512
 * wide tries 640x480, and then the other pixel size is tried the same way. It
 * creates the primary surface with one back buffer and an offscreen surface of
 * g_surfaceWidth by g_surfaceHeight (g_flightBackBuffer,
 * g_flightOffscreenSurface) when page flipping, else the primary alone serving
 * as both, clears them, and at 1 byte per pixel attaches a gray ramp palette
 * (g_flightPalette, else set to NULL). In a window it creates the primary only
 * to read its format and releases it. Sets g_flightPrimaryPitch,
 * g_flightBytesPerPixel and g_pixelFormatCode (8, 555 or 565) from the surface,
 * and, in fullscreen, g_surfacePitch and g_flightRenderSurface. g_useHardware3D
 * is cleared unless pixels are 2 bytes, and when still set
 * Renderer_InitD3DDevice runs. Returns 1, or the 0 of
 * FlightDisplay_CleanupAndReportError with code 2 (cooperative level), 3
 * (display mode), 4 (primary), 5 (back buffer) or 6 (offscreen surface). */
// FUNCTION: XVT 0x4AAFF0
int FlightDisplay_Init(void)
{
	FlightDisplayPaletteEntry paletteEntries[256];
	FlightDisplayDriverCaps driverCaps;
	DDSURFACEDESC surfaceDesc;
	DDSCAPS attachedCaps;
	HRESULT result;
	int paletteIndex;

	g_flightDirectDraw = FrontendDisplay_GetDirectDraw();
	if (g_flightFullscreen != 0) {
		result = g_flightDirectDraw->lpVtbl->SetCooperativeLevel(
			g_flightDirectDraw, g_flightMainWindowHandle,
			DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE | DDSCL_ALLOWMODEX);
		if (result != DX_DD_OK) {
			return FlightDisplay_CleanupAndReportError(2);
		}
	} else {
		result = g_flightDirectDraw->lpVtbl->SetCooperativeLevel(
			g_flightDirectDraw, g_flightMainWindowHandle,
			DDSCL_NORMAL);
		if (result != DX_DD_OK) {
			return FlightDisplay_CleanupAndReportError(2);
		}
	}
	if (g_useHardware3D != 0) {
		g_flightBytesPerPixel = 2;
	}
	if (g_flightFullscreen != 0) {
		result = g_flightDirectDraw->lpVtbl->SetDisplayMode(
			g_flightDirectDraw, g_displayModeWidth,
			g_displayModeHeight, 8 * g_flightBytesPerPixel);
		if (result != DX_DD_OK) {
			if (g_displayModeWidth == 320) {
				g_displayModeWidth = 512;
				g_displayModeHeight = 384;
				result =
					g_flightDirectDraw->lpVtbl->SetDisplayMode(
						g_flightDirectDraw,
						g_displayModeWidth,
						g_displayModeHeight,
						8 * g_flightBytesPerPixel);
				if (result != DX_DD_OK) {
					g_displayModeWidth = 640;
					g_displayModeHeight = 480;
					result = g_flightDirectDraw->lpVtbl->SetDisplayMode(
						g_flightDirectDraw,
						g_displayModeWidth,
						g_displayModeHeight,
						8 * g_flightBytesPerPixel);
					if (result != DX_DD_OK) {
						g_displayModeWidth = 320;
						g_displayModeHeight = 240;
					}
				}
			} else if (g_displayModeWidth == 512) {
				g_displayModeWidth = 640;
				g_displayModeHeight = 480;
				result =
					g_flightDirectDraw->lpVtbl->SetDisplayMode(
						g_flightDirectDraw,
						g_displayModeWidth,
						g_displayModeHeight,
						8 * g_flightBytesPerPixel);
				if (result != DX_DD_OK) {
					g_displayModeWidth = 512;
					g_displayModeHeight = 384;
				}
			}
			if (result != DX_DD_OK && g_flightBytesPerPixel == 2) {
				g_flightBytesPerPixel = 1;
				result =
					g_flightDirectDraw->lpVtbl->SetDisplayMode(
						g_flightDirectDraw,
						g_displayModeWidth,
						g_displayModeHeight,
						8 * g_flightBytesPerPixel);
				if (result != DX_DD_OK) {
					if (g_displayModeWidth == 320) {
						g_displayModeWidth = 512;
						g_displayModeHeight = 384;
						result =
							g_flightDirectDraw
								->lpVtbl
								->SetDisplayMode(
									g_flightDirectDraw,
									g_displayModeWidth,
									g_displayModeHeight,
									8 * g_flightBytesPerPixel);
						if (result != DX_DD_OK) {
							g_displayModeWidth =
								640;
							g_displayModeHeight =
								480;
							result =
								g_flightDirectDraw
									->lpVtbl
									->SetDisplayMode(
										g_flightDirectDraw,
										g_displayModeWidth,
										g_displayModeHeight,
										8 * g_flightBytesPerPixel);
							if (result !=
							    DX_DD_OK) {
								g_displayModeWidth =
									320;
								g_displayModeHeight =
									240;
							}
						}
					} else if (g_displayModeWidth == 512) {
						g_displayModeWidth = 640;
						g_displayModeHeight = 480;
						result =
							g_flightDirectDraw
								->lpVtbl
								->SetDisplayMode(
									g_flightDirectDraw,
									g_displayModeWidth,
									g_displayModeHeight,
									8 * g_flightBytesPerPixel);
						if (result != DX_DD_OK) {
							g_displayModeWidth =
								512;
							g_displayModeHeight =
								384;
						}
					}
				}
			} else if (result != DX_DD_OK &&
				   g_flightBytesPerPixel == 1) {
				g_flightBytesPerPixel = 2;
				result =
					g_flightDirectDraw->lpVtbl->SetDisplayMode(
						g_flightDirectDraw,
						g_displayModeWidth,
						g_displayModeHeight,
						8 * g_flightBytesPerPixel);
				if (result != DX_DD_OK) {
					if (g_displayModeWidth == 320) {
						g_displayModeWidth = 512;
						g_displayModeHeight = 384;
						result =
							g_flightDirectDraw
								->lpVtbl
								->SetDisplayMode(
									g_flightDirectDraw,
									g_displayModeWidth,
									g_displayModeHeight,
									8 * g_flightBytesPerPixel);
						if (result != DX_DD_OK) {
							g_displayModeWidth =
								640;
							g_displayModeHeight =
								480;
							result =
								g_flightDirectDraw
									->lpVtbl
									->SetDisplayMode(
										g_flightDirectDraw,
										g_displayModeWidth,
										g_displayModeHeight,
										8 * g_flightBytesPerPixel);
							if (result !=
							    DX_DD_OK) {
								g_displayModeWidth =
									320;
								g_displayModeHeight =
									240;
							}
						}
					} else if (g_displayModeWidth == 512) {
						g_displayModeWidth = 640;
						g_displayModeHeight = 480;
						result =
							g_flightDirectDraw
								->lpVtbl
								->SetDisplayMode(
									g_flightDirectDraw,
									g_displayModeWidth,
									g_displayModeHeight,
									8 * g_flightBytesPerPixel);
						if (result != DX_DD_OK) {
							g_displayModeWidth =
								512;
							g_displayModeHeight =
								384;
						}
					}
				}
			}
			if (result != DX_DD_OK) {
				return FlightDisplay_CleanupAndReportError(3);
			}
		}
	}
	if (g_flightBytesPerPixel != 2) {
		g_useHardware3D = 0;
	}

	g_flightPrimaryPitch[1] = 1;
	memset(&driverCaps, 0, sizeof(driverCaps));
	driverCaps.dwSize = sizeof(driverCaps);
	result = g_flightDirectDraw->lpVtbl->GetCaps(g_flightDirectDraw,
						     &driverCaps, NULL);
	if (result == DX_DD_OK && (driverCaps.dwCaps & 0x400000) != 0 &&
	    (driverCaps.dwCKeyCaps & 1) != 0 &&
	    (driverCaps.dwCKeyCaps & 0x200) == 0) {
		g_flightPrimaryPitch[1] = 2;
	}

	if (g_flightFullscreen != 0) {
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		if (g_flightPageFlip != 0) {
			surfaceDesc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
			surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE |
						     DDSCAPS_FLIP |
						     DDSCAPS_COMPLEX;
			surfaceDesc.dwBackBufferCount = 1;
			if (g_useHardware3D != 0) {
				surfaceDesc.ddsCaps.dwCaps |= DDSCAPS_3DDEVICE;
			}
		} else {
			surfaceDesc.dwFlags = DDSD_CAPS;
			surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
			if (g_useHardware3D != 0) {
				surfaceDesc.ddsCaps.dwCaps |= DDSCAPS_3DDEVICE;
			}
		}
		result = g_flightDirectDraw->lpVtbl->CreateSurface(
			g_flightDirectDraw, &surfaceDesc,
			&g_flightPrimarySurface, NULL);
		if (result != DX_DD_OK) {
			return FlightDisplay_CleanupAndReportError(4);
		}
		g_pixelFormatCode = 565;
		if (g_flightBytesPerPixel != 2) {
			g_pixelFormatCode = 8;
		}
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		g_flightPrimarySurface->lpVtbl->GetSurfaceDesc(
			g_flightPrimarySurface, &surfaceDesc);
		g_flightPrimaryPitch[0] = surfaceDesc.lPitch;
		g_surfacePitch = surfaceDesc.lPitch;
		if ((surfaceDesc.ddpfPixelFormat.dwFlags &
		     DDPF_PALETTEINDEXED8) != 0) {
			g_flightBytesPerPixel = 1;
			g_pixelFormatCode = 8;
		} else if ((surfaceDesc.ddpfPixelFormat.dwFlags & DDPF_RGB) !=
			   0) {
			g_flightBytesPerPixel = 2;
			g_pixelFormatCode = 565;
			if ((surfaceDesc.ddpfPixelFormat.dwGBitMask & 0x400) ==
			    0) {
				g_pixelFormatCode = 555;
			}
		}
		FlightDisplay_ClearSurface(g_flightPrimarySurface);

		if (g_flightPageFlip != 0) {
			attachedCaps.dwCaps = DDSCAPS_BACKBUFFER;
			result = g_flightPrimarySurface->lpVtbl
					 ->GetAttachedSurface(
						 g_flightPrimarySurface,
						 &attachedCaps,
						 &g_flightBackBuffer);
			if (result != DX_DD_OK) {
				return FlightDisplay_CleanupAndReportError(5);
			}
			surfaceDesc.dwFlags =
				DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
			surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
			if (g_useHardware3D != 0) {
				surfaceDesc.ddsCaps.dwCaps |=
					DDSCAPS_SYSTEMMEMORY;
			}
			surfaceDesc.dwWidth = g_surfaceWidth;
			surfaceDesc.dwHeight = g_surfaceHeight;
			result = g_flightDirectDraw->lpVtbl->CreateSurface(
				g_flightDirectDraw, &surfaceDesc,
				&g_flightOffscreenSurface, NULL);
			if (result != DX_DD_OK) {
				return FlightDisplay_CleanupAndReportError(6);
			}
			FlightDisplay_ClearSurface(g_flightBackBuffer);
			g_flightRenderSurface = g_flightBackBuffer;
			FlightDisplay_ClearSurface(g_flightOffscreenSurface);
		} else {
			g_flightRenderSurface = g_flightPrimarySurface;
			g_flightBackBuffer = g_flightPrimarySurface;
		}
		if (FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub() ==
		    0) {
			return FlightDisplay_CleanupAndReportError(12);
		}
	} else {
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		surfaceDesc.dwFlags = DDSD_CAPS;
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
		result = g_flightDirectDraw->lpVtbl->CreateSurface(
			g_flightDirectDraw, &surfaceDesc,
			&g_flightPrimarySurface, NULL);
		if (result != DX_DD_OK) {
			return FlightDisplay_CleanupAndReportError(4);
		}
		if (FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub() ==
		    0) {
			return FlightDisplay_CleanupAndReportError(12);
		}
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		g_flightPrimarySurface->lpVtbl->GetSurfaceDesc(
			g_flightPrimarySurface, &surfaceDesc);
		g_flightPrimaryPitch[0] = surfaceDesc.lPitch;
		if ((surfaceDesc.ddpfPixelFormat.dwFlags &
		     DDPF_PALETTEINDEXED8) != 0) {
			g_flightBytesPerPixel = 1;
			g_pixelFormatCode = 8;
		} else if ((surfaceDesc.ddpfPixelFormat.dwFlags & DDPF_RGB) !=
			   0) {
			g_flightBytesPerPixel = 2;
			if ((surfaceDesc.ddpfPixelFormat.dwGBitMask & 0x400) !=
			    0) {
				g_pixelFormatCode = 565;
			} else {
				g_pixelFormatCode = 555;
			}
		} else {
			g_flightBytesPerPixel = 1;
			g_pixelFormatCode = 8;
		}
		if (g_flightPrimarySurface != NULL) {
			g_flightPrimarySurface->lpVtbl->Release(
				g_flightPrimarySurface);
			g_flightPrimarySurface = NULL;
		}
	}

	if (g_flightFullscreen != 0) {
		if (g_flightBytesPerPixel == 1) {
			for (paletteIndex = 0; paletteIndex < 256;
			     ++paletteIndex) {
				paletteEntries[paletteIndex].red =
					(uint8_t)paletteIndex;
				paletteEntries[paletteIndex].green =
					(uint8_t)paletteIndex;
				paletteEntries[paletteIndex].blue =
					(uint8_t)paletteIndex;
			}
			g_flightDirectDraw->lpVtbl->CreatePalette(
				g_flightDirectDraw,
				DDPCAPS_8BIT | FLIGHT_DDPCAPS_INITIALIZE |
					DDPCAPS_ALLOW256,
				paletteEntries, &g_flightPalette, NULL);
			if (g_flightPalette != NULL) {
				g_flightPrimarySurface->lpVtbl->SetPalette(
					g_flightPrimarySurface,
					g_flightPalette);
			}
		} else {
			g_flightPalette = NULL;
		}
	}
	if (g_useHardware3D != 0) {
		Renderer_InitD3DDevice();
	}
	return 1;
}

/* Loads entryCount palette entries from firstEntry, given as 6-bit RGB
 * triplets, into the display palette. In fullscreen with g_flightPalette it
 * shifts each value up 2 bits and calls DirectDraw SetEntries, returning the
 * low byte of its result; otherwise it calls
 * FlightDisplay_WriteVgaPaletteEntries and returns what that returns. On the
 * DirectDraw path rgbData is read from entry 0 and the local table is filled
 * from firstEntry but passed from its start, so it is right only for firstEntry
 * 0, which is what its one caller, FlightPalette_ApplyToDisplay, passes, with
 * 256 entries. */
// FUNCTION: XVT 0x4AB890
uint8_t FlightDisplay_SetPaletteEntries(const uint8_t *rgbData, int firstEntry,
					int entryCount)
{
	uint32_t paletteEntries[256];
	uint8_t *destination;
	unsigned int remaining;
	int count;
	int first;

	if (g_flightFullscreen != 0) {
		count = entryCount;
		if (g_flightPalette != NULL) {
			first = firstEntry;
			if (first < first + count) {
				rgbData += 3 * first;
				remaining = count;
				destination = (uint8_t *)&paletteEntries[first];
				do {
					destination[0] = rgbData[0] << 2;
					destination[1] = rgbData[1] << 2;
					destination[2] = rgbData[2] << 2;
					rgbData += 3;
					destination += 4;
				} while (--remaining != 0);
			}

			return (uint8_t)g_flightPalette->lpVtbl->SetEntries(
				g_flightPalette, 0, first, count,
				paletteEntries);
		}

		return FlightDisplay_WriteVgaPaletteEntries(rgbData, firstEntry,
							    count);
	}

	return FlightDisplay_WriteVgaPaletteEntries(rgbData, firstEntry,
						    entryCount);
}

/* Reports that flight could not start: sends a ___CleanupAndExit line with
 * errorCode to the debugger output (DebugPrintf in the modern build), releases
 * g_flightPrimarySurface, g_flightPalette and, with page flipping,
 * g_flightOffscreenSurface, setting each to NULL, shuts the network session
 * down (NetSession_Shutdown), and shows "Game could not start" (a message box
 * in the original build, a DebugPrintf line in the modern one). Returns 0. */
// FUNCTION: XVT 0x4AB970
int FlightDisplay_CleanupAndReportError(int errorCode)
{
#ifndef XVT_MODERN
	void(__stdcall * outputDebugString)(const char *outputString);
#endif
	IDirectDrawSurface *surface;
	IDirectDrawPalette *palette;
	int pageFlip;

#ifndef XVT_MODERN
	outputDebugString = OutputDebugStringA;
	wsprintfA(g_flightDisplayDebugMessage, "___CleanupAndExit  err = %d\n",
		  errorCode);
	outputDebugString(g_flightDisplayDebugMessage);
#else
	snprintf(g_flightDisplayDebugMessage,
		 sizeof(g_flightDisplayDebugMessage),
		 "___CleanupAndExit  err = %d\n", errorCode);
	DebugPrintf("%s", g_flightDisplayDebugMessage);
#endif
	surface = g_flightPrimarySurface;
	if (surface != NULL) {
		surface->lpVtbl->Release(surface);
		g_flightPrimarySurface = NULL;
	}
	palette = g_flightPalette;
	if (palette != NULL) {
		palette->lpVtbl->Release(palette);
		g_flightPalette = NULL;
	}
	pageFlip = g_flightPageFlip;
	if (pageFlip != 0) {
		surface = g_flightOffscreenSurface;
		if (surface != NULL) {
			surface->lpVtbl->Release(surface);
			g_flightOffscreenSurface = NULL;
		}
	}
	NetSession_Shutdown();
#ifndef XVT_MODERN
	MessageBoxA(NULL, "Game could not start", "ERROR", 0);
#else
	DebugPrintf("ERROR: Game could not start");
#endif
	return 0;
}

/* Returns g_flightPrimaryPitch[0]: the pitch in bytes of the surface last
 * locked or described. */
// FUNCTION: XVT 0x4ABA10
int FlightDisplay_GetPrimarySurfacePitch(void)
{
	return g_flightPrimaryPitch[0];
}

/* Shows the finished frame. With page flipping it flips the primary surface
 * (DDFLIP_WAIT). With g_flightConfFlicker set it first lines the flip up with
 * the display's refresh: the first call waits for a vertical blank to begin,
 * records the time in g_flightFlickerLastSyncTimeMs and sets
 * g_flightFlickerRefreshRateScale; later calls take the refresh rate scale
 * times the ms since that time, modulo 100,000, and when that lies within
 * g_flightFlickerPhaseWindow of either end and no blank is under way, wait for
 * the next blank and record its time. When the flip reports no exclusive mode
 * it takes exclusive mode, flips again (restoring the primary, back buffer and
 * z-buffer when lost) and returns to the normal cooperative level, returning
 * that last call's result; the original build stops in the debugger at each
 * failure there. When the flip reports a lost surface it restores the primary
 * (FlightDisplay_RestorePrimarySurface) and returns 1 when that succeeded, 0
 * when it failed, without flipping again. Otherwise it returns the flip's
 * result. Without page flipping it copies 480 rows of the primary pitch from
 * g_flightSoftwareFramebuffer to the primary surface, which in a window it
 * creates first and releases after, and returns the failing lock's error, the
 * unlock's or release's result, or, when the window's primary cannot be
 * created, FlightDisplay_CleanupAndReportError's 0. The modern build reports
 * each flip or copy to XvtRenderCapture_Presented. */
// FUNCTION: XVT 0x4ABEE0
HRESULT FlightDisplay_Flip(void)
{
	DDSURFACEDESC surfaceDesc;
	HRESULT result;
	HRESULT flipResult;
	int verticalBlankStatus;
	int phase;
	int i;

	if (g_flightPageFlip != 0) {
		if (g_flightConfFlicker != 0) {
			if (g_flightFlickerLastSyncTimeMs == 0) {
				if (g_flightDirectDraw->lpVtbl
					    ->GetVerticalBlankStatus(
						    g_flightDirectDraw,
						    &verticalBlankStatus) ==
				    DX_DD_OK) {
					while (verticalBlankStatus != 0 &&
					       g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(
						       g_flightDirectDraw,
						       &verticalBlankStatus) ==
						       DX_DD_OK) {
					}
					while (verticalBlankStatus == 0 &&
					       g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(
						       g_flightDirectDraw,
						       &verticalBlankStatus) ==
						       DX_DD_OK) {
					}
				}

				g_flightFlickerLastSyncTimeMs = timeGetTime();
				if (g_flightDirectDraw->lpVtbl->GetMonitorFrequency(
					    g_flightDirectDraw,
					    (uint32_t
						     *)&g_flightFlickerRefreshRateScale) !=
				    DX_DD_OK) {
					for (i = 0; i < 100; ++i) {
						while (verticalBlankStatus !=
							       0 &&
						       g_flightDirectDraw
								       ->lpVtbl
								       ->GetVerticalBlankStatus(
									       g_flightDirectDraw,
									       &verticalBlankStatus) ==
							       DX_DD_OK) {
						}
						while (verticalBlankStatus ==
							       0 &&
						       g_flightDirectDraw
								       ->lpVtbl
								       ->GetVerticalBlankStatus(
									       g_flightDirectDraw,
									       &verticalBlankStatus) ==
							       DX_DD_OK) {
						}
					}
					g_flightFlickerRefreshRateScale =
						10000000 /
						(int)(timeGetTime() -
						      g_flightFlickerLastSyncTimeMs);
				}
			} else {
				phase = (int)(g_flightFlickerRefreshRateScale *
					      (timeGetTime() -
					       g_flightFlickerLastSyncTimeMs)) %
					100000;
				if ((g_flightFlickerPhaseWindow > phase ||
				     100000 - g_flightFlickerPhaseWindow <
					     phase) &&
				    g_flightDirectDraw->lpVtbl
						    ->GetVerticalBlankStatus(
							    g_flightDirectDraw,
							    &verticalBlankStatus) ==
					    DX_DD_OK &&
				    verticalBlankStatus == 0) {
					while (verticalBlankStatus == 0 &&
					       g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(
						       g_flightDirectDraw,
						       &verticalBlankStatus) ==
						       DX_DD_OK) {
					}
					g_flightFlickerLastSyncTimeMs =
						timeGetTime();
				}
			}
		}

		flipResult = g_flightPrimarySurface->lpVtbl->Flip(
			g_flightPrimarySurface, NULL, DDFLIP_WAIT);
#ifdef XVT_MODERN
		XvtRenderCapture_Presented(flipResult == DX_DD_OK);
#endif
		if (flipResult == DX_DDERR_NOEXCLUSIVEMODE) {
			result =
				g_flightDirectDraw->lpVtbl->SetCooperativeLevel(
					g_flightDirectDraw,
					g_flightMainWindowHandle,
					DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE |
						DDSCL_ALLOWMODEX);
			if (result != DX_DD_OK) {
#ifndef XVT_MODERN
				__debugbreak();
#else
				DebugPrintf("SetCooperativeLevel failed: %d",
					    result);
#endif
			}

			result = g_flightPrimarySurface->lpVtbl->Flip(
				g_flightPrimarySurface, NULL, DDFLIP_WAIT);
#ifdef XVT_MODERN
			XvtRenderCapture_Presented(result == DX_DD_OK);
#endif
			if (result == DX_DDERR_SURFACELOST) {
				g_flightPrimarySurface->lpVtbl->Restore(
					g_flightPrimarySurface);
				g_flightBackBuffer->lpVtbl->Restore(
					g_flightBackBuffer);
				g_std3DZBufferSurface->lpVtbl->Restore(
					g_std3DZBufferSurface);
				result = g_flightPrimarySurface->lpVtbl->Flip(
					g_flightPrimarySurface, NULL,
					DDFLIP_WAIT);
#ifdef XVT_MODERN
				XvtRenderCapture_Presented(result == DX_DD_OK);
#endif
			}
			if (result != DX_DD_OK) {
#ifndef XVT_MODERN
				__debugbreak();
#else
				DebugPrintf("Flip failed: %d", result);
#endif
			}

			/* From here flipResult holds the result of returning to the normal cooperative level, not of a
			 * flip; on this path the function returns it and drops the retried flip's result. */
			flipResult =
				g_flightDirectDraw->lpVtbl->SetCooperativeLevel(
					g_flightDirectDraw,
					g_flightMainWindowHandle, DDSCL_NORMAL);
			if (flipResult != DX_DD_OK) {
#ifndef XVT_MODERN
				__debugbreak();
#else
				DebugPrintf("SetCooperativeLevel failed: %d",
					    flipResult);
#endif
			}
		}

		result = flipResult;
		if (flipResult == DX_DDERR_SURFACELOST) {
			result = FlightDisplay_RestorePrimarySurface();
			if (result != DX_DD_OK) {
				return FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub();
			}
		}
	} else {
		if (g_flightFullscreen == 0) {
			memset(&surfaceDesc, 0, sizeof(surfaceDesc));
			surfaceDesc.dwSize = sizeof(surfaceDesc);
			surfaceDesc.dwFlags = DDSD_CAPS;
			surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
			result = g_flightDirectDraw->lpVtbl->CreateSurface(
				g_flightDirectDraw, &surfaceDesc,
				&g_flightPrimarySurface, NULL);
			if (result != DX_DD_OK) {
				return FlightDisplay_CleanupAndReportError(4);
			}
			if (FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub() ==
			    0) {
				return FlightDisplay_CleanupAndReportError(12);
			}
		}

		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		g_flightPrimarySurface->lpVtbl->GetSurfaceDesc(
			g_flightPrimarySurface, &surfaceDesc);
		g_flightPrimaryPitch[0] = surfaceDesc.lPitch;
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		for (;;) {
			result = g_flightPrimarySurface->lpVtbl->Lock(
				g_flightPrimarySurface, NULL, &surfaceDesc, 0,
				NULL);
			if (result == DX_DD_OK) {
				break;
			}
			if (result != DX_DDERR_WASSTILLDRAWING) {
				return result;
			}
		}

		memcpy(surfaceDesc.lpSurface, g_flightSoftwareFramebuffer,
		       480 * FlightDisplay_GetPrimarySurfacePitch());
		result = g_flightPrimarySurface->lpVtbl->Unlock(
			g_flightPrimarySurface, surfaceDesc.lpSurface);
#ifdef XVT_MODERN
		XvtRenderCapture_Presented(result == DX_DD_OK);
#endif
		if (g_flightFullscreen == 0 && g_flightPrimarySurface != NULL) {
			result = g_flightPrimarySurface->lpVtbl->Release(
				g_flightPrimarySurface);
			g_flightPrimarySurface = NULL;
		}
	}
	return result;
}

/* Does nothing. */
// FUNCTION: XVT 0x4AC250
void nullsub_11(void) {}

/* Lays the cockpit and HUD layer over the frame. With page flipping it copies
 * g_flightOffscreenSurface onto g_flightBackBuffer, centered in the display
 * mode, retrying while DirectDraw is still drawing, and returns the copy's
 * result; after a lost surface it restores the primary and returns 1 when that
 * succeeded, 0 when it failed, without copying again. Without page flipping it
 * copies 480 rows of the primary pitch from g_flightHudStagingBuffer into
 * g_flightSoftwareFramebuffer and returns that pitch. The modern build then
 * calls XvtCockpit_LatchComposition, unless a page flipping copy failed. */
// FUNCTION: XVT 0x4AC260
int FlightDisplay_BlitRenderSurface(void)
{
	HRESULT result;
	HRESULT bltResult;
	uint32_t destinationRect[4];
	uint32_t sourceRect[4];
	DDBLTFX effects;

	if (g_flightPageFlip != 0) {
		memset(&effects, 0, sizeof(effects));
		effects.dwSize = sizeof(effects);
		effects.dwROP = 0x00CC0020;
		do {
			destinationRect[0] = (unsigned int)(g_displayModeWidth -
							    g_surfaceWidth) >>
					     1;
			destinationRect[1] =
				(unsigned int)(g_displayModeHeight -
					       g_surfaceHeight) >>
				1;
			destinationRect[2] =
				g_surfaceWidth + destinationRect[0];
			destinationRect[3] =
				g_surfaceHeight + destinationRect[1];
			sourceRect[0] = 0;
			sourceRect[1] = 0;
			sourceRect[2] = g_surfaceWidth;
			sourceRect[3] = g_surfaceHeight;
			bltResult = g_flightBackBuffer->lpVtbl->Blt(
				g_flightBackBuffer, destinationRect,
				g_flightOffscreenSurface, sourceRect, DDBLT_ROP,
				&effects);
			result = bltResult;
			if (bltResult == DX_DD_OK) {
				break;
			}
			if (bltResult == DX_DDERR_SURFACELOST) {
				result = FlightDisplay_RestorePrimarySurface();
				if (result == 0) {
					return result;
				}
				result =
					FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub();
			}
		} while (bltResult == DX_DDERR_WASSTILLDRAWING);
	} else {
		result = FlightDisplay_GetPrimarySurfacePitch();
		memcpy(g_flightSoftwareFramebuffer, g_flightHudStagingBuffer,
		       480 * result);
	}
#ifdef XVT_MODERN
	if (g_flightPageFlip == 0 || result == DX_DD_OK) {
		XvtCockpit_LatchComposition();
	}
#endif
	return result;
}

/* Does nothing. */
// FUNCTION: XVT 0x4AC380
void FlightDisplay_ApplyResolutionModeBackendStub(int resolutionMode)
{
	(void)resolutionMode;
}

/* Fills g_flightBackBuffer with color 0 by a DirectDraw color fill, retrying
 * while DirectDraw is still drawing; after a lost surface it restores the
 * primary and stops without filling. Nothing calls this. */
// FUNCTION: XVT 0x4AC3A0
void FlightDisplay_ClearBackBuffer(void)
{
	DDBLTFX effects;
	HRESULT bltResult;

	effects.dwSize = sizeof(effects);
	effects.dwFillColor = 0;
	do {
		bltResult = g_flightBackBuffer->lpVtbl->Blt(
			g_flightBackBuffer, NULL, NULL, NULL, DDBLT_COLORFILL,
			&effects);
		if (bltResult == DX_DD_OK) {
			break;
		}
		if (bltResult == DX_DDERR_SURFACELOST) {
			if (FlightDisplay_RestorePrimarySurface() == 0) {
				return;
			}
			FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub();
		}
	} while (bltResult == DX_DDERR_WASSTILLDRAWING);
}

/* Fills a DirectDraw surface with color 0, as FlightDisplay_ClearBackBuffer
 * does for the back buffer: it retries while DirectDraw is still drawing and
 * after a lost surface restores the primary surface, not the one given, and
 * stops without filling. */
// FUNCTION: XVT 0x4AC400
void FlightDisplay_ClearSurface(IDirectDrawSurface *surface)
{
	DDBLTFX effects;
	HRESULT bltResult;

	effects.dwSize = sizeof(effects);
	effects.dwFillColor = 0;
	do {
		bltResult = surface->lpVtbl->Blt(surface, NULL, NULL, NULL,
						 DDBLT_COLORFILL, &effects);
		if (bltResult == DX_DD_OK) {
			break;
		}
		if (bltResult == DX_DDERR_SURFACELOST) {
			if (FlightDisplay_RestorePrimarySurface() == 0) {
				return;
			}
			FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub();
		}
	} while (bltResult == DX_DDERR_WASSTILLDRAWING);
}

/* Restores a lost g_flightPrimarySurface; returns 1 on success, else 0. */
// FUNCTION: XVT 0x4AC460
int FlightDisplay_RestorePrimarySurface(void)
{
	return g_flightPrimarySurface->lpVtbl->Restore(
		       g_flightPrimarySurface) == DX_DD_OK;
}

/* Returns 1 when 16-bit pixels are laid out 5-5-5: the frontend's answer while
 * g_flightRenderToFrontend is set, the hardware texture format while a model
 * loads with hardware 3D (g_loadingModel, g_useHardware3D), else whether
 * g_pixelFormatCode is 555. */
// FUNCTION: XVT 0x4AC4B0
int Display_IsPixelFormat555(void)
{
	if (g_flightRenderToFrontend != 0) {
		return FrontendDisplay_GetPixelFormat555();
	}
	if (g_loadingModel != 0 && g_useHardware3D != 0) {
		return ModelTexture_IsHardwareFormat555();
	}
	return g_pixelFormatCode == 555;
}

/* Applies nothing: it returns 1 through
 * FlightDisplay_ApplyResolutionModeInternalStub, which calls
 * FlightDisplay_ApplyResolutionModeBackendStub, which does nothing. */
// FUNCTION: XVT 0x4AC7B0
int FlightDisplay_ApplyResolutionModeStub(int resolutionMode)
{
	return FlightDisplay_ApplyResolutionModeInternalStub(resolutionMode, 0);
}

/* Calls FlightDisplay_ApplyResolutionModeBackendStub, which does nothing, and
 * returns 1; flags is ignored. */
// FUNCTION: XVT 0x4AC7C0
int FlightDisplay_ApplyResolutionModeInternalStub(int resolutionMode, int flags)
{
	(void)flags;
	FlightDisplay_ApplyResolutionModeBackendStub(resolutionMode);
	return 1;
}

/* In the original build, writes entryCount RGB triplets to the VGA palette
 * registers from firstEntry (port 0x3C8 for the index, 0x3C9 for the data)
 * after waiting for a vertical retrace to begin (bit 3 of port 0x3DA), and
 * returns the last blue value written, or 0 for no entries. The modern build
 * does nothing and returns 0. */
// FUNCTION: XVT 0x4AC7E0
uint8_t FlightDisplay_WriteVgaPaletteEntries(const uint8_t *rgbEntries,
					     int16_t firstEntry,
					     int16_t entryCount)
{
#ifndef XVT_MODERN
	const uint8_t *entry;
	int first;
	int count;
	unsigned short port;
	uint8_t result;
#endif

#ifdef XVT_MODERN
	(void)rgbEntries;
	(void)firstEntry;
	(void)entryCount;
	return 0;
#else
	entry = rgbEntries;
	first = firstEntry;
	count = entryCount;
	result = 0;
	if (count != 0) {
		port = 0x3DA;
		while ((inp(port) & 8) != 0) {
		}
		while ((inp(port) & 8) == 0) {
		}
		port = 0x3C8;
		outp(port, first);
		port = 0x3C9;
		do {
			outp(port, entry[0]);
			outp(port, entry[1]);
			result = entry[2];
			outp(port, result);
			entry += 3;
			--count;
		} while (count != 0);
	}
	return result;
#endif
}
