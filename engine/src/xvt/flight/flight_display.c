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
__declspec(dllimport) int __cdecl wsprintfA(char* buffer, const char* format, ...);
__declspec(dllimport) void __stdcall OutputDebugStringA(const char* outputString);
__declspec(dllimport) int __stdcall MessageBoxA(void* hWnd, const char* text, const char* caption,
												unsigned int type);
int __cdecl inp(unsigned short port);
int __cdecl outp(unsigned short port, int value);
#endif

// GLOBAL: XVT 0x66E700
IDirectDrawSurface* g_flightPrimarySurface;
// GLOBAL: XVT 0x66E710
uint8_t g_flightHudStagingBuffer[640 * 480 * 2];
// GLOBAL: XVT 0x803B70
int g_flightPrimaryPitch[2];
// GLOBAL: XVT 0x803F80
IDirectDrawSurface* g_flightBackBuffer;
// GLOBAL: XVT 0x803F90
uint8_t g_flightSoftwareFramebuffer[640 * 480 * 2];
// GLOBAL: XVT 0x527EA8
int g_flightFullscreen = 1;
// GLOBAL: XVT 0x527EA4
int g_flightConfFlicker = 0;
// GLOBAL: XVT 0x527EBC
int g_renderTargetWidth = 640;
// GLOBAL: XVT 0x527EC0
int g_unusedFlightDisplayBytesPerPixelMirror = 1;
// GLOBAL: XVT 0x527EC4
int g_unusedFlightDisplayHardware3DMirror = 1;
// GLOBAL: XVT 0x527F7C
uint32_t g_flightFlickerLastSyncTimeMs = 0;
// GLOBAL: XVT 0x527F80
int g_flightFlickerPhaseWindow = 20000;
// GLOBAL: XVT 0x622CB0
int g_flightFlickerRefreshRateScale = 0;
// GLOBAL: XVT 0x66DDD8
IDirectDrawPalette* g_flightPalette = NULL;
// GLOBAL: XVT 0x66DDEC
IDirectDrawSurface* g_flightRenderSurface;
// GLOBAL: XVT 0x66E200
static char g_flightDisplayDebugMessage[1280] = { 0 };
// GLOBAL: XVT 0x5233CC
unsigned int g_swFramebufferClearChunkSize = 0xF000;
// GLOBAL: XVT 0x5233D0
unsigned int g_vesaGrainsPerPage = 15;
// GLOBAL: XVT 0x52155C
char g_hudCockpitResolutionDirectory[7] = "CP640\\";
// GLOBAL: XVT 0x9A7B4C
int g_flightResolutionLegacyExtent = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x447D90
void FlightDisplay_ConfigureResolutionState(void) {
	int primarySurfacePitch;
	int resolutionMode;

	primarySurfacePitch = FlightDisplay_GetPrimarySurfacePitch();
	resolutionMode = g_flightResolutionMode;
	g_swFramebufferClearChunkSize = 480 * primarySurfacePitch;
	g_vesaGrainsPerPage = 1;
	switch (resolutionMode) {
		case FLIGHT_RESOLUTION_320X240:
			g_swFramebufferClearChunkSize = 0x10000;
			g_vesaGrainsPerPage = 1;
			g_screenWidth = 320;
			g_screenHeight = 240;
			g_flightResolutionLegacyExtent = 240;
			g_surfacePitch = FlightDisplay_GetPrimarySurfacePitch();
			g_projScaleInt = 256;
			g_projScaleHalfInt = 128;
			perspShift = 8;
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
			perspShift = 9;
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
			perspShift = 9;
			g_hudCockpitResolutionDirectory[2] = '4';
			g_hudCockpitResolutionDirectory[3] = '8';
			g_projAspectY = 0;
			break;

		default:
			g_swFramebufferClearChunkSize = 0x10000;
			g_screenWidth = 320;
			g_screenHeight = 240;
			g_flightResolutionLegacyExtent = 240;
			g_surfacePitch = FlightDisplay_GetPrimarySurfacePitch();
			g_projScaleInt = 256;
			g_projScaleHalfInt = 128;
			perspShift = 8;
			g_hudCockpitResolutionDirectory[2] = '3';
			g_hudCockpitResolutionDirectory[3] = '2';
			g_projAspectY = 0;
			break;
	}
}

// FUNCTION: XVT 0x4AAFE0
int FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub(void) { return 1; }

typedef struct FlightDisplayDriverCaps {
	uint32_t dwSize;
	uint32_t dwCaps;
	uint32_t dwCaps2;
	uint32_t dwCKeyCaps;
	uint32_t reserved[87];
} FlightDisplayDriverCaps;

typedef struct FlightDisplayPaletteEntry {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t flags;
} FlightDisplayPaletteEntry;

enum { FLIGHT_DDPCAPS_INITIALIZE = 0x8 };

// FUNCTION: XVT 0x4AAFF0
int FlightDisplay_Init(void) {
	FlightDisplayPaletteEntry paletteEntries[256];
	FlightDisplayDriverCaps driverCaps;
	DDSURFACEDESC surfaceDesc;
	DDSCAPS attachedCaps;
	HRESULT result;
	int paletteIndex;

	g_flightDirectDraw = FrontendDisplay_GetDirectDraw();
	if (g_flightFullscreen != 0) {
		result = g_flightDirectDraw->lpVtbl->SetCooperativeLevel(g_flightDirectDraw, g_flightMainWindowHandle,
																 DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE |
																	 DDSCL_ALLOWMODEX);
		if (result != DX_DD_OK)
			return FlightDisplay_CleanupAndReportError(2);
	} else {
		result = g_flightDirectDraw->lpVtbl->SetCooperativeLevel(g_flightDirectDraw, g_flightMainWindowHandle,
																 DDSCL_NORMAL);
		if (result != DX_DD_OK)
			return FlightDisplay_CleanupAndReportError(2);
	}
	if (g_useHardware3D != 0)
		g_flight16bppBytesPerPixel = 2;
	if (g_flightFullscreen != 0) {
		result = g_flightDirectDraw->lpVtbl->SetDisplayMode(g_flightDirectDraw, width, height,
															8 * g_flight16bppBytesPerPixel);
		if (result != DX_DD_OK) {
			if (width == 320) {
				width = 512;
				height = 384;
				result = g_flightDirectDraw->lpVtbl->SetDisplayMode(g_flightDirectDraw, width, height,
																	8 * g_flight16bppBytesPerPixel);
				if (result != DX_DD_OK) {
					width = 640;
					height = 480;
					result = g_flightDirectDraw->lpVtbl->SetDisplayMode(g_flightDirectDraw, width, height,
																		8 * g_flight16bppBytesPerPixel);
					if (result != DX_DD_OK) {
						width = 320;
						height = 240;
					}
				}
			} else if (width == 512) {
				width = 640;
				height = 480;
				result = g_flightDirectDraw->lpVtbl->SetDisplayMode(g_flightDirectDraw, width, height,
																	8 * g_flight16bppBytesPerPixel);
				if (result != DX_DD_OK) {
					width = 512;
					height = 384;
				}
			}
			if (result != DX_DD_OK && g_flight16bppBytesPerPixel == 2) {
				g_flight16bppBytesPerPixel = 1;
				result = g_flightDirectDraw->lpVtbl->SetDisplayMode(g_flightDirectDraw, width, height,
																	8 * g_flight16bppBytesPerPixel);
				if (result != DX_DD_OK) {
					if (width == 320) {
						width = 512;
						height = 384;
						result = g_flightDirectDraw->lpVtbl->SetDisplayMode(g_flightDirectDraw, width, height,
																			8 * g_flight16bppBytesPerPixel);
						if (result != DX_DD_OK) {
							width = 640;
							height = 480;
							result = g_flightDirectDraw->lpVtbl->SetDisplayMode(
								g_flightDirectDraw, width, height, 8 * g_flight16bppBytesPerPixel);
							if (result != DX_DD_OK) {
								width = 320;
								height = 240;
							}
						}
					} else if (width == 512) {
						width = 640;
						height = 480;
						result = g_flightDirectDraw->lpVtbl->SetDisplayMode(g_flightDirectDraw, width, height,
																			8 * g_flight16bppBytesPerPixel);
						if (result != DX_DD_OK) {
							width = 512;
							height = 384;
						}
					}
				}
			} else if (result != DX_DD_OK && g_flight16bppBytesPerPixel == 1) {
				g_flight16bppBytesPerPixel = 2;
				result = g_flightDirectDraw->lpVtbl->SetDisplayMode(g_flightDirectDraw, width, height,
																	8 * g_flight16bppBytesPerPixel);
				if (result != DX_DD_OK) {
					if (width == 320) {
						width = 512;
						height = 384;
						result = g_flightDirectDraw->lpVtbl->SetDisplayMode(g_flightDirectDraw, width, height,
																			8 * g_flight16bppBytesPerPixel);
						if (result != DX_DD_OK) {
							width = 640;
							height = 480;
							result = g_flightDirectDraw->lpVtbl->SetDisplayMode(
								g_flightDirectDraw, width, height, 8 * g_flight16bppBytesPerPixel);
							if (result != DX_DD_OK) {
								width = 320;
								height = 240;
							}
						}
					} else if (width == 512) {
						width = 640;
						height = 480;
						result = g_flightDirectDraw->lpVtbl->SetDisplayMode(g_flightDirectDraw, width, height,
																			8 * g_flight16bppBytesPerPixel);
						if (result != DX_DD_OK) {
							width = 512;
							height = 384;
						}
					}
				}
			}
			if (result != DX_DD_OK)
				return FlightDisplay_CleanupAndReportError(3);
		}
	}
	if (g_flight16bppBytesPerPixel != 2)
		g_useHardware3D = 0;

	g_flightPrimaryPitch[1] = 1;
	memset(&driverCaps, 0, sizeof(driverCaps));
	driverCaps.dwSize = sizeof(driverCaps);
	result = g_flightDirectDraw->lpVtbl->GetCaps(g_flightDirectDraw, &driverCaps, NULL);
	if (result == DX_DD_OK && (driverCaps.dwCaps & 0x400000) != 0 && (driverCaps.dwCKeyCaps & 1) != 0 &&
		(driverCaps.dwCKeyCaps & 0x200) == 0) {
		g_flightPrimaryPitch[1] = 2;
	}

	if (g_flightFullscreen != 0) {
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		if (g_flightPageFlip != 0) {
			surfaceDesc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
			surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
			surfaceDesc.dwBackBufferCount = 1;
			if (g_useHardware3D != 0)
				surfaceDesc.ddsCaps.dwCaps |= DDSCAPS_3DDEVICE;
		} else {
			surfaceDesc.dwFlags = DDSD_CAPS;
			surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
			if (g_useHardware3D != 0)
				surfaceDesc.ddsCaps.dwCaps |= DDSCAPS_3DDEVICE;
		}
		result = g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &surfaceDesc,
														   &g_flightPrimarySurface, NULL);
		if (result != DX_DD_OK)
			return FlightDisplay_CleanupAndReportError(4);
		g_pixelFormatCode = 565;
		if (g_flight16bppBytesPerPixel != 2)
			g_pixelFormatCode = 8;
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		g_flightPrimarySurface->lpVtbl->GetSurfaceDesc(g_flightPrimarySurface, &surfaceDesc);
		g_flightPrimaryPitch[0] = surfaceDesc.lPitch;
		g_surfacePitch = surfaceDesc.lPitch;
		if ((surfaceDesc.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8) != 0) {
			g_flight16bppBytesPerPixel = 1;
			g_pixelFormatCode = 8;
		} else if ((surfaceDesc.ddpfPixelFormat.dwFlags & DDPF_RGB) != 0) {
			g_flight16bppBytesPerPixel = 2;
			g_pixelFormatCode = 565;
			if ((surfaceDesc.ddpfPixelFormat.dwGBitMask & 0x400) == 0)
				g_pixelFormatCode = 555;
		}
		FlightDisplay_ClearSurface(g_flightPrimarySurface);

		if (g_flightPageFlip != 0) {
			attachedCaps.dwCaps = DDSCAPS_BACKBUFFER;
			result = g_flightPrimarySurface->lpVtbl->GetAttachedSurface(g_flightPrimarySurface, &attachedCaps,
																		&g_flightBackBuffer);
			if (result != DX_DD_OK)
				return FlightDisplay_CleanupAndReportError(5);
			surfaceDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
			surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
			if (g_useHardware3D != 0)
				surfaceDesc.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
			surfaceDesc.dwWidth = g_surfaceWidth;
			surfaceDesc.dwHeight = g_surfaceHeight;
			result = g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &surfaceDesc,
															   &g_flightOffscreenSurface, NULL);
			if (result != DX_DD_OK)
				return FlightDisplay_CleanupAndReportError(6);
			FlightDisplay_ClearSurface(g_flightBackBuffer);
			g_flightRenderSurface = g_flightBackBuffer;
			FlightDisplay_ClearSurface(g_flightOffscreenSurface);
		} else {
			g_flightRenderSurface = g_flightPrimarySurface;
			g_flightBackBuffer = g_flightPrimarySurface;
		}
		if (FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub() == 0)
			return FlightDisplay_CleanupAndReportError(12);
	} else {
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		surfaceDesc.dwFlags = DDSD_CAPS;
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
		result = g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &surfaceDesc,
														   &g_flightPrimarySurface, NULL);
		if (result != DX_DD_OK)
			return FlightDisplay_CleanupAndReportError(4);
		if (FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub() == 0)
			return FlightDisplay_CleanupAndReportError(12);
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		g_flightPrimarySurface->lpVtbl->GetSurfaceDesc(g_flightPrimarySurface, &surfaceDesc);
		g_flightPrimaryPitch[0] = surfaceDesc.lPitch;
		if ((surfaceDesc.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8) != 0) {
			g_flight16bppBytesPerPixel = 1;
			g_pixelFormatCode = 8;
		} else if ((surfaceDesc.ddpfPixelFormat.dwFlags & DDPF_RGB) != 0) {
			g_flight16bppBytesPerPixel = 2;
			if ((surfaceDesc.ddpfPixelFormat.dwGBitMask & 0x400) != 0)
				g_pixelFormatCode = 565;
			else
				g_pixelFormatCode = 555;
		} else {
			g_flight16bppBytesPerPixel = 1;
			g_pixelFormatCode = 8;
		}
		if (g_flightPrimarySurface != NULL) {
			g_flightPrimarySurface->lpVtbl->Release(g_flightPrimarySurface);
			g_flightPrimarySurface = NULL;
		}
	}

	if (g_flightFullscreen != 0) {
		if (g_flight16bppBytesPerPixel == 1) {
			for (paletteIndex = 0; paletteIndex < 256; ++paletteIndex) {
				paletteEntries[paletteIndex].red = (uint8_t)paletteIndex;
				paletteEntries[paletteIndex].green = (uint8_t)paletteIndex;
				paletteEntries[paletteIndex].blue = (uint8_t)paletteIndex;
			}
			g_flightDirectDraw->lpVtbl->CreatePalette(
				g_flightDirectDraw, DDPCAPS_8BIT | FLIGHT_DDPCAPS_INITIALIZE | DDPCAPS_ALLOW256,
				paletteEntries, &g_flightPalette, NULL);
			if (g_flightPalette != NULL)
				g_flightPrimarySurface->lpVtbl->SetPalette(g_flightPrimarySurface, g_flightPalette);
		} else {
			g_flightPalette = NULL;
		}
	}
	if (g_useHardware3D != 0)
		Renderer_InitD3DDevice();
	return 1;
}

// FUNCTION: XVT 0x4AB890
uint8_t FlightDisplay_SetPaletteEntries(const uint8_t* rgbData, int firstEntry, int entryCount) {
	uint32_t paletteEntries[256];
	uint8_t* destination;
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
				destination = (uint8_t*)&paletteEntries[first];
				do {
					destination[0] = rgbData[0] << 2;
					destination[1] = rgbData[1] << 2;
					destination[2] = rgbData[2] << 2;
					rgbData += 3;
					destination += 4;
				} while (--remaining != 0);
			}

			return (uint8_t)g_flightPalette->lpVtbl->SetEntries(g_flightPalette, 0, first, count,
																paletteEntries);
		}

		return FlightDisplay_WriteVgaPaletteEntries(rgbData, firstEntry, count);
	}

	return FlightDisplay_WriteVgaPaletteEntries(rgbData, firstEntry, entryCount);
}

// FUNCTION: XVT 0x4AB970
int FlightDisplay_CleanupAndReportError(int errorCode) {
#ifndef XVT_MODERN
	void(__stdcall * outputDebugString)(const char* outputString);
#endif
	IDirectDrawSurface* surface;
	IDirectDrawPalette* palette;
	int pageFlip;

#ifndef XVT_MODERN
	outputDebugString = OutputDebugStringA;
	wsprintfA(g_flightDisplayDebugMessage, "___CleanupAndExit  err = %d\n", errorCode);
	outputDebugString(g_flightDisplayDebugMessage);
#else
	snprintf(g_flightDisplayDebugMessage, sizeof(g_flightDisplayDebugMessage),
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

// FUNCTION: XVT 0x4ABA10
int FlightDisplay_GetPrimarySurfacePitch(void) { return g_flightPrimaryPitch[0]; }

// FUNCTION: XVT 0x4ABEE0
HRESULT FlightDisplay_Flip(void) {
	DDSURFACEDESC surfaceDesc;
	HRESULT result;
	HRESULT flipResult;
	int verticalBlankStatus;
	int phase;
	int i;

	if (g_flightPageFlip != 0) {
		if (g_flightConfFlicker != 0) {
			if (g_flightFlickerLastSyncTimeMs == 0) {
				if (g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(g_flightDirectDraw,
																	   &verticalBlankStatus) == DX_DD_OK) {
					while (verticalBlankStatus != 0 &&
						   g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(
							   g_flightDirectDraw, &verticalBlankStatus) == DX_DD_OK) {
					}
					while (verticalBlankStatus == 0 &&
						   g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(
							   g_flightDirectDraw, &verticalBlankStatus) == DX_DD_OK) {
					}
				}

				g_flightFlickerLastSyncTimeMs = timeGetTime();
				if (g_flightDirectDraw->lpVtbl->GetMonitorFrequency(
						g_flightDirectDraw, (uint32_t*)&g_flightFlickerRefreshRateScale) != DX_DD_OK) {
					for (i = 0; i < 100; ++i) {
						while (verticalBlankStatus != 0 &&
							   g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(
								   g_flightDirectDraw, &verticalBlankStatus) == DX_DD_OK) {
						}
						while (verticalBlankStatus == 0 &&
							   g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(
								   g_flightDirectDraw, &verticalBlankStatus) == DX_DD_OK) {
						}
					}
					g_flightFlickerRefreshRateScale =
						10000000 / (int)(timeGetTime() - g_flightFlickerLastSyncTimeMs);
				}
			} else {
				phase =
					(int)(g_flightFlickerRefreshRateScale * (timeGetTime() - g_flightFlickerLastSyncTimeMs)) %
					100000;
				if ((g_flightFlickerPhaseWindow > phase || 100000 - g_flightFlickerPhaseWindow < phase) &&
					g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(g_flightDirectDraw,
																	   &verticalBlankStatus) == DX_DD_OK &&
					verticalBlankStatus == 0) {
					while (verticalBlankStatus == 0 &&
						   g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(
							   g_flightDirectDraw, &verticalBlankStatus) == DX_DD_OK) {
					}
					g_flightFlickerLastSyncTimeMs = timeGetTime();
				}
			}
		}

		flipResult = g_flightPrimarySurface->lpVtbl->Flip(g_flightPrimarySurface, NULL, DDFLIP_WAIT);
#ifdef XVT_MODERN
		XvtRenderCapture_Presented(flipResult == DX_DD_OK);
#endif
		if (flipResult == DX_DDERR_NOEXCLUSIVEMODE) {
			result = g_flightDirectDraw->lpVtbl->SetCooperativeLevel(
				g_flightDirectDraw, g_flightMainWindowHandle,
				DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE | DDSCL_ALLOWMODEX);
			if (result != DX_DD_OK) {
#ifndef XVT_MODERN
				__debugbreak();
#else
				DebugPrintf("SetCooperativeLevel failed: %d", result);
#endif
			}

			result = g_flightPrimarySurface->lpVtbl->Flip(g_flightPrimarySurface, NULL, DDFLIP_WAIT);
#ifdef XVT_MODERN
			XvtRenderCapture_Presented(result == DX_DD_OK);
#endif
			if (result == DX_DDERR_SURFACELOST) {
				g_flightPrimarySurface->lpVtbl->Restore(g_flightPrimarySurface);
				g_flightBackBuffer->lpVtbl->Restore(g_flightBackBuffer);
				g_std3DZBufferSurface->lpVtbl->Restore(g_std3DZBufferSurface);
				result = g_flightPrimarySurface->lpVtbl->Flip(g_flightPrimarySurface, NULL, DDFLIP_WAIT);
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

			flipResult = g_flightDirectDraw->lpVtbl->SetCooperativeLevel(
				g_flightDirectDraw, g_flightMainWindowHandle, DDSCL_NORMAL);
			if (flipResult != DX_DD_OK) {
#ifndef XVT_MODERN
				__debugbreak();
#else
				DebugPrintf("SetCooperativeLevel failed: %d", flipResult);
#endif
			}
		}

		result = flipResult;
		if (flipResult == DX_DDERR_SURFACELOST) {
			result = FlightDisplay_RestorePrimarySurface();
			if (result != DX_DD_OK)
				return FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub();
		}
	} else {
		if (g_flightFullscreen == 0) {
			memset(&surfaceDesc, 0, sizeof(surfaceDesc));
			surfaceDesc.dwSize = sizeof(surfaceDesc);
			surfaceDesc.dwFlags = DDSD_CAPS;
			surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
			result = g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &surfaceDesc,
															   &g_flightPrimarySurface, NULL);
			if (result != DX_DD_OK)
				return FlightDisplay_CleanupAndReportError(4);
			if (FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub() == 0)
				return FlightDisplay_CleanupAndReportError(12);
		}

		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		g_flightPrimarySurface->lpVtbl->GetSurfaceDesc(g_flightPrimarySurface, &surfaceDesc);
		g_flightPrimaryPitch[0] = surfaceDesc.lPitch;
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		for (;;) {
			result =
				g_flightPrimarySurface->lpVtbl->Lock(g_flightPrimarySurface, NULL, &surfaceDesc, 0, NULL);
			if (result == DX_DD_OK)
				break;
			if (result != DX_DDERR_WASSTILLDRAWING)
				return result;
		}

		memcpy(surfaceDesc.lpSurface, g_flightSoftwareFramebuffer,
			   480 * FlightDisplay_GetPrimarySurfacePitch());
		result = g_flightPrimarySurface->lpVtbl->Unlock(g_flightPrimarySurface, surfaceDesc.lpSurface);
#ifdef XVT_MODERN
		XvtRenderCapture_Presented(result == DX_DD_OK);
#endif
		if (g_flightFullscreen == 0 && g_flightPrimarySurface != NULL) {
			result = g_flightPrimarySurface->lpVtbl->Release(g_flightPrimarySurface);
			g_flightPrimarySurface = NULL;
		}
	}
	return result;
}

// FUNCTION: XVT 0x4AC250
void nullsub_11(void) {}

// FUNCTION: XVT 0x4AC260
int FlightDisplay_BlitRenderSurface(void) {
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
			destinationRect[0] = (unsigned int)(width - g_surfaceWidth) >> 1;
			destinationRect[1] = (unsigned int)(height - g_surfaceHeight) >> 1;
			destinationRect[2] = g_surfaceWidth + destinationRect[0];
			destinationRect[3] = g_surfaceHeight + destinationRect[1];
			sourceRect[0] = 0;
			sourceRect[1] = 0;
			sourceRect[2] = g_surfaceWidth;
			sourceRect[3] = g_surfaceHeight;
			bltResult =
				g_flightBackBuffer->lpVtbl->Blt(g_flightBackBuffer, destinationRect, g_flightOffscreenSurface,
												sourceRect, DDBLT_ROP, &effects);
			result = bltResult;
			if (bltResult == DX_DD_OK) {
				break;
			}
			if (bltResult == DX_DDERR_SURFACELOST) {
				result = FlightDisplay_RestorePrimarySurface();
				if (result == 0) {
					return result;
				}
				result = FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub();
			}
		} while (bltResult == DX_DDERR_WASSTILLDRAWING);
	} else {
		result = FlightDisplay_GetPrimarySurfacePitch();
		memcpy(g_flightSoftwareFramebuffer, g_flightHudStagingBuffer, 480 * result);
	}
#ifdef XVT_MODERN
	if (g_flightPageFlip == 0 || result == DX_DD_OK)
		XvtCockpit_LatchComposition();
#endif
	return result;
}

// FUNCTION: XVT 0x4AC380
void FlightDisplay_ApplyResolutionModeBackendStub(int resolutionMode) { (void)resolutionMode; }

// FUNCTION: XVT 0x4AC3A0
void FlightDisplay_ClearBackBuffer(void) {
	DDBLTFX effects;
	HRESULT bltResult;

	effects.dwSize = sizeof(effects);
	effects.dwFillColor = 0;
	do {
		bltResult =
			g_flightBackBuffer->lpVtbl->Blt(g_flightBackBuffer, NULL, NULL, NULL, DDBLT_COLORFILL, &effects);
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

// FUNCTION: XVT 0x4AC400
void FlightDisplay_ClearSurface(IDirectDrawSurface* surface) {
	DDBLTFX effects;
	HRESULT bltResult;

	effects.dwSize = sizeof(effects);
	effects.dwFillColor = 0;
	do {
		bltResult = surface->lpVtbl->Blt(surface, NULL, NULL, NULL, DDBLT_COLORFILL, &effects);
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

// FUNCTION: XVT 0x4AC460
int FlightDisplay_RestorePrimarySurface(void) {
	return g_flightPrimarySurface->lpVtbl->Restore(g_flightPrimarySurface) == DX_DD_OK;
}

// FUNCTION: XVT 0x4AC4B0
int Display_IsPixelFormat555(void) {
	if (g_flightRenderToFrontend != 0) {
		return FrontendDisplay_GetPixelFormat555();
	}
	if (g_loadingModel != 0 && g_useHardware3D != 0) {
		return ModelTexture_IsHardwareFormat555();
	}
	return g_pixelFormatCode == 555;
}

// FUNCTION: XVT 0x4AC7B0
int FlightDisplay_ApplyResolutionMode(int resolutionMode) {
	return FlightDisplay_ApplyResolutionModeInternalStub(resolutionMode, 0);
}

// FUNCTION: XVT 0x4AC7C0
int FlightDisplay_ApplyResolutionModeInternalStub(int resolutionMode, int flags) {
	(void)flags;
	FlightDisplay_ApplyResolutionModeBackendStub(resolutionMode);
	return 1;
}

// FUNCTION: XVT 0x4AC7E0
uint8_t FlightDisplay_WriteVgaPaletteEntries(const uint8_t* rgbEntries, int16_t firstEntry,
											 int16_t entryCount) {
#ifndef XVT_MODERN
	const uint8_t* entry;
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
