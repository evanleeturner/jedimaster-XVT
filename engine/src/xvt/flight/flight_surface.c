#include "xvt/flight/flight_surface.h"

#include "xvt/flight/flight_display.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"

#include <string.h>

/* 1 when flight draws through DirectDraw surfaces and flips pages; 0 when it
 * draws into memory and FlightDisplay_Flip copies g_flightSoftwareFramebuffer
 * to the screen. Starts at 1; the launch options "nopageflip" and
 * "pageflip" set it, read by Flight_Main in the original build and
 * XvtFlightEntry_ReadLaunchSwitches in the modern one. */
// GLOBAL: XVT 0x527EAC
int g_flightPageFlip = 1;
/* 1 when FlightSurface_Lock is to hand out the cockpit and HUD layer
 * (g_flightOffscreenSurface, or g_flightHudStagingBuffer without page
 * flipping); 0 for the frame the 3D view is drawn in (g_flightBackBuffer or
 * g_flightSoftwareFramebuffer). Starts at 1. Many functions write it,
 * chiefly FlightView_Render, the alert box code and the pause key, which set
 * it to 0 around their own drawing and then to 1. */
// GLOBAL: XVT 0x527ED0
int g_flightDrawToHudLayer = 1;
/* With page flipping, the offscreen surface, g_surfaceWidth by
 * g_surfaceHeight, that holds the cockpit and HUD layer;
 * FlightDisplay_BlitRenderSurface and FlightView_CompositeMaskedSoftwareSurface
 * copy it onto the back buffer. Four functions write it: FlightDisplay_Init
 * creates it; FlightDisplay_CleanupAndReportError and the flight shutdown
 * (Flight_Main in the original build, XvtFlightEntry_Cleanup in the modern
 * one) release it and set NULL. */
// GLOBAL: XVT 0x66DDCC
IDirectDrawSurface *g_flightOffscreenSurface = 0;
/* 1 while the flight's back buffer and offscreen surface are in use, so
 * FlightSurface_Lock locks one of them instead of the primary surface. Set
 * to 1 at flight start and 0 at its end (Flight_MainLoop in the original
 * build; XvtFlightLoading_Globals and XvtFlightTask_ReleaseMission in the
 * modern one); FeDiskIo_ShowFatalErrorMessageAndWaitKey sets 1 while it
 * shows its message and then puts the old value back. */
// GLOBAL: XVT 0x9ED23B
uint8_t g_flightDisplaySurfacesActive = 0;
/* Ticks the local input clock (g_inputTimestamp) aims to run ahead of
 * g_serverTickTime: 30, or 130 in internet play, at mission start; then the
 * host's measured start delay, at least 35, and the clock probes move it.
 * Six functions write it: FlightNet_WaitForMissionStart and
 * FlightNet_ProcessIncomingPackets in the original build;
 * XvtFlightNetwork_WaitForMissionStart, XvtFlightNetwork_AnswerClockProbe,
 * XvtFlightNetwork_ApplyClockProbeReply and XvtFlightNetwork_Control in the
 * modern one. FlightSurface_Lock and FlightSurface_Unlock also read its low
 * byte: see FlightSurface_Lock. */
// GLOBAL: XVT 0x9ED23C
int32_t g_flightNetClockLeadTicks = 0;
/* Nothing sets it to anything but 0 (FlightView_Render does, three times),
 * so the tests of it in the HUD box drawing, the rotated sprite blitter and
 * the software face drawer always find 0. */
// GLOBAL: XVT 0xA0813F
uint8_t g_flightSurfaceAlreadyLocked = 0;

/* Nesting depth of FlightSurface_Lock calls not yet undone by
 * FlightSurface_Unlock; only those two functions write it. */
// GLOBAL: XVT 0x527F78
int g_surfaceLockCount = 0;
/* Pixel base the software renderer starts from when its render target is
 * reset (FlightSw_SetRenderTarget with NULL, FlightSw_InitFramebuffer).
 * Starts at 0xA0000; only FlightSurface_SetSoftwareFramebufferBase writes
 * it, as FlightSurface_Lock picks a surface. */
// GLOBAL: XVT 0x5280E8
void *g_swFramebufferBase = (void *)0xA0000;
/* Bytes in 480 rows of the primary pitch, recorded by
 * FlightSurface_SetViewport480ByteSpan on every first lock. Nothing reads
 * it. */
// GLOBAL: XVT 0x5280EC
static int g_flightSurfaceViewport480ByteSpan;

/* Fills the screen-sized buffer g_flightOffscreenBuffer with the color
 * index g_flightTransparentColorIndex: by memset at 1 byte per pixel, else
 * through g_flightFillClipRectFn with the buffer as a temporary render
 * target, which it then resets (FlightSw_SetRenderTarget with NULL). Changes
 * the text clip rectangle and background color on that path. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x49CAE0
void FlightSurface_ClearToBlack(void)
{
	if (g_flightBytesPerPixel == 1) {
		memset(g_flightOffscreenBuffer, g_flightTransparentColorIndex,
		       g_screenWidth * g_screenHeight);
	} else {
		FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth,
					 g_screenHeight,
					 g_screenWidth * g_flightBytesPerPixel);
		FlightText_SetBackgroundColor(g_flightTransparentColorIndex);
		FlightText_SetClipRect(0, 0, g_screenWidth, g_screenHeight);
		g_flightFillClipRectFn();
		FlightSw_SetRenderTarget(NULL, 320, 240, 0);
	}
}

/* Returns g_surfaceLockCount. */
// FUNCTION: XVT 0x4ABA40
int FlightSurface_GetLockCount(void) { return g_surfaceLockCount; }

/* Locks the surface the software renderer draws to and points
 * g_flightSwFramebufferBase, g_surfacePixels and g_swFramebufferBase at its
 * pixels. Only the first of nested calls does the work; the rest add to
 * g_surfaceLockCount. With g_flightRenderToFrontend 1 it takes the
 * frontend's draw surface and pitch instead, leaving the count alone.
 * Without page flipping it hands out memory: g_flightHudStagingBuffer when
 * g_flightDrawToHudLayer is set, else g_flightSoftwareFramebuffer. With page
 * flipping and the surfaces active, it locks g_flightOffscreenSurface when
 * g_flightDrawToHudLayer is set, else g_flightBackBuffer; with them not
 * active, the primary surface. The surfaces count as active when
 * g_flightDisplaySurfacesActive or the low byte of
 * g_flightNetClockLeadTicks is nonzero. On the back buffer and the primary
 * the pointers are moved to center g_surfaceWidth by g_surfaceHeight in the
 * display mode. Sets g_flightPrimaryPitch[0] to the locked surface's pitch
 * and, on the offscreen surface and back buffer, g_surfacePitch and the
 * render target when the pitch changed. Retries while DirectDraw is still
 * drawing; on any other lock failure it returns with the count already
 * raised. */
// FUNCTION: XVT 0x4ABA50
void FlightSurface_Lock(void)
{
	DDSURFACEDESC surfaceDesc;
	HRESULT lockResult;
	uint8_t displaySurfaceState;
	unsigned int horizontalOffset;
	unsigned int verticalOffset;

	if (g_flightRenderToFrontend == 1) {
		g_flightSwFramebufferBase =
			FrontendDisplay_GetDrawSurfaceForFlight();
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
		displaySurfaceState |= (uint8_t)g_flightNetClockLeadTicks;
		if (displaySurfaceState != 0) {
			if (g_flightDrawToHudLayer != 0) {
				memset(&surfaceDesc, 0, sizeof(surfaceDesc));
				surfaceDesc.dwSize = sizeof(surfaceDesc);
				for (;;) {
					lockResult =
						g_flightOffscreenSurface->lpVtbl
							->Lock(g_flightOffscreenSurface,
							       NULL,
							       &surfaceDesc, 0,
							       NULL);
					if (lockResult == DX_DD_OK) {
						break;
					}
					if (lockResult !=
					    DX_DDERR_WASSTILLDRAWING) {
						return;
					}
				}

				FlightSurface_SetSoftwareFramebufferBase(
					surfaceDesc.lpSurface);
				g_flightSwFramebufferBase =
					surfaceDesc.lpSurface;
				g_surfacePixels = surfaceDesc.lpSurface;
				memset(&surfaceDesc, 0, sizeof(surfaceDesc));
				surfaceDesc.dwSize = sizeof(surfaceDesc);
				g_flightOffscreenSurface->lpVtbl
					->GetSurfaceDesc(
						g_flightOffscreenSurface,
						&surfaceDesc);
				g_flightPrimaryPitch[0] = surfaceDesc.lPitch;
				FlightSurface_SetViewport480ByteSpan(
					480 *
					FlightDisplay_GetPrimarySurfacePitch());
				if (g_surfacePitch != g_flightPrimaryPitch[0]) {
					g_surfacePitch =
						g_flightPrimaryPitch[0];
					FlightSw_SetRenderTarget(
						g_flightSwFramebufferBase,
						g_flightPrimaryPitch[0], 480,
						-1);
				}
			} else {
				memset(&surfaceDesc, 0, sizeof(surfaceDesc));
				surfaceDesc.dwSize = sizeof(surfaceDesc);
				for (;;) {
					lockResult =
						g_flightBackBuffer->lpVtbl->Lock(
							g_flightBackBuffer,
							NULL, &surfaceDesc, 0,
							NULL);
					if (lockResult == DX_DD_OK) {
						break;
					}
					if (lockResult !=
					    DX_DDERR_WASSTILLDRAWING) {
						return;
					}
				}

				FlightSurface_SetSoftwareFramebufferBase(
					surfaceDesc.lpSurface);
				g_flightSwFramebufferBase =
					surfaceDesc.lpSurface;
				g_surfacePixels = surfaceDesc.lpSurface;
				memset(&surfaceDesc, 0, sizeof(surfaceDesc));
				surfaceDesc.dwSize = sizeof(surfaceDesc);
				g_flightBackBuffer->lpVtbl->GetSurfaceDesc(
					g_flightBackBuffer, &surfaceDesc);
				g_flightPrimaryPitch[0] = surfaceDesc.lPitch;
				horizontalOffset =
					g_flightBytesPerPixel *
					((unsigned int)(g_displayModeWidth -
							g_surfaceWidth) >>
					 1);
				verticalOffset =
					surfaceDesc.lPitch *
					((unsigned int)(g_displayModeHeight -
							g_surfaceHeight) >>
					 1);
				g_flightSwFramebufferBase += horizontalOffset;
				g_flightSwFramebufferBase += verticalOffset;
				g_surfacePixels = (uint8_t *)g_surfacePixels +
						  horizontalOffset;
				g_surfacePixels = (uint8_t *)g_surfacePixels +
						  verticalOffset;
				FlightSurface_SetViewport480ByteSpan(
					480 *
					FlightDisplay_GetPrimarySurfacePitch());
				if (g_surfacePitch != g_flightPrimaryPitch[0]) {
					g_surfacePitch =
						g_flightPrimaryPitch[0];
					FlightSw_SetRenderTarget(
						g_flightSwFramebufferBase,
						g_flightPrimaryPitch[0], 480,
						-1);
				}
			}
		} else {
			memset(&surfaceDesc, 0, sizeof(surfaceDesc));
			surfaceDesc.dwSize = sizeof(surfaceDesc);
			for (;;) {
				lockResult =
					g_flightPrimarySurface->lpVtbl->Lock(
						g_flightPrimarySurface, NULL,
						&surfaceDesc, 0, NULL);
				if (lockResult == DX_DD_OK) {
					break;
				}
				if (lockResult != DX_DDERR_WASSTILLDRAWING) {
					return;
				}
			}

			FlightSurface_SetSoftwareFramebufferBase(
				surfaceDesc.lpSurface);
			g_flightSwFramebufferBase = surfaceDesc.lpSurface;
			g_surfacePixels = surfaceDesc.lpSurface;
			memset(&surfaceDesc, 0, sizeof(surfaceDesc));
			surfaceDesc.dwSize = sizeof(surfaceDesc);
			g_flightPrimarySurface->lpVtbl->GetSurfaceDesc(
				g_flightPrimarySurface, &surfaceDesc);
			g_flightPrimaryPitch[0] = surfaceDesc.lPitch;
			horizontalOffset = g_flightBytesPerPixel *
					   ((unsigned int)(g_displayModeWidth -
							   g_surfaceWidth) >>
					    1);
			verticalOffset = surfaceDesc.lPitch *
					 ((unsigned int)(g_displayModeHeight -
							 g_surfaceHeight) >>
					  1);
			g_flightSwFramebufferBase += horizontalOffset;
			g_flightSwFramebufferBase += verticalOffset;
			g_surfacePixels =
				(uint8_t *)g_surfacePixels + horizontalOffset;
			g_surfacePixels =
				(uint8_t *)g_surfacePixels + verticalOffset;
			FlightSurface_SetViewport480ByteSpan(
				480 * FlightDisplay_GetPrimarySurfacePitch());
		}
	} else if (g_flightDrawToHudLayer != 0) {
		FlightSurface_SetSoftwareFramebufferBase(
			g_flightHudStagingBuffer);
		FlightSurface_SetViewport480ByteSpan(
			480 * FlightDisplay_GetPrimarySurfacePitch());
		g_flightSwFramebufferBase = g_flightHudStagingBuffer;
		g_surfacePixels = g_flightHudStagingBuffer;
	} else {
		FlightSurface_SetSoftwareFramebufferBase(
			g_flightSoftwareFramebuffer);
		FlightSurface_SetViewport480ByteSpan(
			480 * FlightDisplay_GetPrimarySurfacePitch());
		g_flightSwFramebufferBase = g_flightSoftwareFramebuffer;
		g_surfacePixels = g_flightSoftwareFramebuffer;
	}
}

/* Undoes one FlightSurface_Lock: above 1 it only lowers g_surfaceLockCount;
 * below 1 it sets it to 0; at 1 it lowers it to 0 and, with page flipping,
 * unlocks the surface FlightSurface_Lock picks by the same test. Does nothing
 * when g_flightRenderToFrontend is 1. */
// FUNCTION: XVT 0x4ABE50
void FlightSurface_Unlock(void)
{
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
	displaySurfaceState |= (uint8_t)g_flightNetClockLeadTicks;
	if (displaySurfaceState != 0) {
		if (g_flightDrawToHudLayer != 0) {
			g_flightOffscreenSurface->lpVtbl->Unlock(
				g_flightOffscreenSurface, g_surfacePixels);
		} else {
			g_flightBackBuffer->lpVtbl->Unlock(g_flightBackBuffer,
							   g_surfacePixels);
		}
	} else {
		g_flightPrimarySurface->lpVtbl->Unlock(g_flightPrimarySurface,
						       g_surfacePixels);
	}
}

/* Sets g_flightSurfaceViewport480ByteSpan, which nothing reads, and returns
 * it. */
// FUNCTION: XVT 0x4AC780
int FlightSurface_SetViewport480ByteSpan(int byteSpan)
{
	return g_flightSurfaceViewport480ByteSpan = byteSpan;
}

/* Sets g_swFramebufferBase and returns it. */
// FUNCTION: XVT 0x4AC790
void *FlightSurface_SetSoftwareFramebufferBase(void *framebufferBase)
{
	return g_swFramebufferBase = framebufferBase;
}

/* Returns g_swFramebufferBase. */
// FUNCTION: XVT 0x4AC7A0
void *FlightSurface_GetSoftwareFramebufferBase(void)
{
	return g_swFramebufferBase;
}
