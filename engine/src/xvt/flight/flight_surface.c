#include "xvt/flight/flight_surface.h"

#include "xvt/flight/flight_display.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"

#include <string.h>

// GLOBAL: XVT 0x527EAC
int g_flightPageFlip = 1;
// GLOBAL: XVT 0x527ED0
int g_flightLockBackBufferForHudDraw = 1;
// GLOBAL: XVT 0x66DDCC
IDirectDrawSurface* g_flightOffscreenSurface = 0;
// GLOBAL: XVT 0x9ED23B
uint8_t g_flightDisplaySurfacesActive = 0;
// GLOBAL: XVT 0x9ED23C
int32_t g_flightNetClockLeadAllowanceMs = 0;
// GLOBAL: XVT 0xA0813F
uint8_t g_flightSurfaceAlreadyLocked = 0;

// GLOBAL: XVT 0x527F78
int g_surfaceLockCount = 0;
// GLOBAL: XVT 0x5280E8
void* g_swFramebufferBase = (void*)0xA0000;
// GLOBAL: XVT 0x5280EC
static int g_flightSurfaceViewport480ByteSpan;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x49CAE0
void FlightSurface_ClearToBlack(void) {
	if (g_flight16bppBytesPerPixel == 1) {
		memset(g_flightOffscreenBuffer, g_flightColorEscapeBypassChar, g_screenWidth * g_screenHeight);
	} else {
		FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth, g_screenHeight,
								 g_screenWidth * g_flight16bppBytesPerPixel);
		FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
		FlightText_SetClipRect(0, 0, g_screenWidth, g_screenHeight);
		g_flightFillClipRectFn();
		FlightSw_SetRenderTarget(NULL, 320, 240, 0);
	}
}

// FUNCTION: XVT 0x4ABA40
int FlightSurface_GetLockCount(void) { return g_surfaceLockCount; }

// FUNCTION: XVT 0x4ABA50
void FlightSurface_Lock(void) {
	DDSURFACEDESC surfaceDesc;
	HRESULT lockResult;
	uint8_t displaySurfaceState;
	unsigned int horizontalOffset;
	unsigned int verticalOffset;

	if (g_flightRenderToFrontend == 1) {
		g_flightSwFramebufferBase = FrontendDisplay_LockSurfaceForFlight();
		g_surfacePixels = g_flightSwFramebufferBase;
		g_surfacePitch = FrontendDisplay_GetFrontendOrFlightDrawPitch();
		return;
	}
	if (g_surfaceLockCount != 0) {
		++g_surfaceLockCount;
		return;
	}
	++g_surfaceLockCount;

	if (g_flightPageFlip != 0) {
		displaySurfaceState = g_flightDisplaySurfacesActive;
		displaySurfaceState |= (uint8_t)g_flightNetClockLeadAllowanceMs;
		if (displaySurfaceState != 0) {
			if (g_flightLockBackBufferForHudDraw != 0) {
				memset(&surfaceDesc, 0, sizeof(surfaceDesc));
				surfaceDesc.dwSize = sizeof(surfaceDesc);
				for (;;) {
					lockResult = g_flightOffscreenSurface->lpVtbl->Lock(g_flightOffscreenSurface, NULL,
																		&surfaceDesc, 0, NULL);
					if (lockResult == DX_DD_OK) {
						break;
					}
					if (lockResult != DX_DDERR_WASSTILLDRAWING) {
						return;
					}
				}

				FlightSurface_SetSoftwareFramebufferBase(surfaceDesc.lpSurface);
				g_flightSwFramebufferBase = surfaceDesc.lpSurface;
				g_surfacePixels = surfaceDesc.lpSurface;
				memset(&surfaceDesc, 0, sizeof(surfaceDesc));
				surfaceDesc.dwSize = sizeof(surfaceDesc);
				g_flightOffscreenSurface->lpVtbl->GetSurfaceDesc(g_flightOffscreenSurface, &surfaceDesc);
				g_flightPrimaryPitch[0] = surfaceDesc.lPitch;
				FlightSurface_SetViewport480ByteSpan(480 * FlightDisplay_GetPrimarySurfacePitch());
				if (g_surfacePitch != g_flightPrimaryPitch[0]) {
					g_surfacePitch = g_flightPrimaryPitch[0];
					FlightSw_SetRenderTarget(g_flightSwFramebufferBase, g_flightPrimaryPitch[0], 480, -1);
				}
			} else {
				memset(&surfaceDesc, 0, sizeof(surfaceDesc));
				surfaceDesc.dwSize = sizeof(surfaceDesc);
				for (;;) {
					lockResult =
						g_flightBackBuffer->lpVtbl->Lock(g_flightBackBuffer, NULL, &surfaceDesc, 0, NULL);
					if (lockResult == DX_DD_OK) {
						break;
					}
					if (lockResult != DX_DDERR_WASSTILLDRAWING) {
						return;
					}
				}

				FlightSurface_SetSoftwareFramebufferBase(surfaceDesc.lpSurface);
				g_flightSwFramebufferBase = surfaceDesc.lpSurface;
				g_surfacePixels = surfaceDesc.lpSurface;
				memset(&surfaceDesc, 0, sizeof(surfaceDesc));
				surfaceDesc.dwSize = sizeof(surfaceDesc);
				g_flightBackBuffer->lpVtbl->GetSurfaceDesc(g_flightBackBuffer, &surfaceDesc);
				g_flightPrimaryPitch[0] = surfaceDesc.lPitch;
				horizontalOffset = g_flight16bppBytesPerPixel * ((unsigned int)(width - g_surfaceWidth) >> 1);
				verticalOffset = surfaceDesc.lPitch * ((unsigned int)(height - g_surfaceHeight) >> 1);
				g_flightSwFramebufferBase += horizontalOffset;
				g_flightSwFramebufferBase += verticalOffset;
				g_surfacePixels = (uint8_t*)g_surfacePixels + horizontalOffset;
				g_surfacePixels = (uint8_t*)g_surfacePixels + verticalOffset;
				FlightSurface_SetViewport480ByteSpan(480 * FlightDisplay_GetPrimarySurfacePitch());
				if (g_surfacePitch != g_flightPrimaryPitch[0]) {
					g_surfacePitch = g_flightPrimaryPitch[0];
					FlightSw_SetRenderTarget(g_flightSwFramebufferBase, g_flightPrimaryPitch[0], 480, -1);
				}
			}
		} else {
			memset(&surfaceDesc, 0, sizeof(surfaceDesc));
			surfaceDesc.dwSize = sizeof(surfaceDesc);
			for (;;) {
				lockResult =
					g_flightPrimarySurface->lpVtbl->Lock(g_flightPrimarySurface, NULL, &surfaceDesc, 0, NULL);
				if (lockResult == DX_DD_OK) {
					break;
				}
				if (lockResult != DX_DDERR_WASSTILLDRAWING) {
					return;
				}
			}

			FlightSurface_SetSoftwareFramebufferBase(surfaceDesc.lpSurface);
			g_flightSwFramebufferBase = surfaceDesc.lpSurface;
			g_surfacePixels = surfaceDesc.lpSurface;
			memset(&surfaceDesc, 0, sizeof(surfaceDesc));
			surfaceDesc.dwSize = sizeof(surfaceDesc);
			g_flightPrimarySurface->lpVtbl->GetSurfaceDesc(g_flightPrimarySurface, &surfaceDesc);
			g_flightPrimaryPitch[0] = surfaceDesc.lPitch;
			horizontalOffset = g_flight16bppBytesPerPixel * ((unsigned int)(width - g_surfaceWidth) >> 1);
			verticalOffset = surfaceDesc.lPitch * ((unsigned int)(height - g_surfaceHeight) >> 1);
			g_flightSwFramebufferBase += horizontalOffset;
			g_flightSwFramebufferBase += verticalOffset;
			g_surfacePixels = (uint8_t*)g_surfacePixels + horizontalOffset;
			g_surfacePixels = (uint8_t*)g_surfacePixels + verticalOffset;
			FlightSurface_SetViewport480ByteSpan(480 * FlightDisplay_GetPrimarySurfacePitch());
		}
	} else if (g_flightLockBackBufferForHudDraw != 0) {
		FlightSurface_SetSoftwareFramebufferBase(g_flightHudStagingBuffer);
		FlightSurface_SetViewport480ByteSpan(480 * FlightDisplay_GetPrimarySurfacePitch());
		g_flightSwFramebufferBase = g_flightHudStagingBuffer;
		g_surfacePixels = g_flightHudStagingBuffer;
	} else {
		FlightSurface_SetSoftwareFramebufferBase(g_flightSoftwareFramebuffer);
		FlightSurface_SetViewport480ByteSpan(480 * FlightDisplay_GetPrimarySurfacePitch());
		g_flightSwFramebufferBase = g_flightSoftwareFramebuffer;
		g_surfacePixels = g_flightSoftwareFramebuffer;
	}
}

// FUNCTION: XVT 0x4ABE50
void FlightSurface_Unlock(void) {
	uint8_t displaySurfaceState;

	if (g_flightRenderToFrontend == 1) {
		return;
	}
	if (g_surfaceLockCount > 1) {
		--g_surfaceLockCount;
		return;
	}
	if (g_surfaceLockCount < 1) {
		g_surfaceLockCount = 0;
		return;
	}

	--g_surfaceLockCount;
	if (g_flightPageFlip == 0) {
		return;
	}
	displaySurfaceState = g_flightDisplaySurfacesActive;
	displaySurfaceState |= (uint8_t)g_flightNetClockLeadAllowanceMs;
	if (displaySurfaceState != 0) {
		if (g_flightLockBackBufferForHudDraw != 0) {
			g_flightOffscreenSurface->lpVtbl->Unlock(g_flightOffscreenSurface, g_surfacePixels);
		} else {
			g_flightBackBuffer->lpVtbl->Unlock(g_flightBackBuffer, g_surfacePixels);
		}
	} else {
		g_flightPrimarySurface->lpVtbl->Unlock(g_flightPrimarySurface, g_surfacePixels);
	}
}

// FUNCTION: XVT 0x4AC780
int FlightSurface_SetViewport480ByteSpan(int byteSpan) {
	return g_flightSurfaceViewport480ByteSpan = byteSpan;
}

// FUNCTION: XVT 0x4AC790
void* FlightSurface_SetSoftwareFramebufferBase(void* framebufferBase) {
	return g_swFramebufferBase = framebufferBase;
}

// FUNCTION: XVT 0x4AC7A0
void* FlightSurface_GetSoftwareFramebufferBase(void) { return g_swFramebufferBase; }
