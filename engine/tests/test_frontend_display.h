/* A frontend display with no window, for tests that run frontend frames: a DirectDraw device from Aeron's
 * compatibility layer with a 640x480, 16-bit back buffer in memory and a primary surface to present to. The
 * recovered frontend can then lock, draw into, clear and present its back buffer. No offscreen surface is
 * made, so the frontend's offscreen restore must stay off while frames run, and no fonts or images are
 * loaded, so text and sprites draw nothing.
 *
 * Open after clearing g_frontState; Close releases the surfaces and the device and drops what Aeron kept of
 * the last presented frame. */
#ifndef XVT_TESTS_TEST_FRONTEND_DISPLAY_H
#define XVT_TESTS_TEST_FRONTEND_DISPLAY_H

#include "aeron/compat/ddraw.h"
#include "aeron/compat/host.h"
#include "test_assert.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"

#include <string.h>

/* Creates one surface of the device with the given caps; size flags are set for an offscreen surface. */
static inline IDirectDrawSurface *
XvtTest_CreateSurface(IDirectDraw *device, unsigned caps, int sized)
{
	IDirectDrawSurface *surface = NULL;
	DDSURFACEDESC desc;
	memset(&desc, 0, sizeof desc);
	desc.dwSize = sizeof desc;
	desc.dwFlags = DDSD_CAPS | (sized ? DDSD_WIDTH | DDSD_HEIGHT : 0);
	desc.dwWidth = 640;
	desc.dwHeight = 480;
	desc.ddsCaps.dwCaps = caps;
	XVT_ASSERT_INT_EQ(
		device->lpVtbl->CreateSurface(device, &desc, &surface, NULL),
		DX_DD_OK);
	XVT_ASSERT_TRUE(surface != NULL);
	return surface;
}

/* Gives the frontend its display: the device, the back buffer and its pitch, the primary surface, 16 bits
 * per pixel, and presenting by copy rather than by page flip. */
static inline void XvtTest_OpenDisplay(void)
{
	IDirectDraw *device = NULL;
	DDSURFACEDESC desc;
	XVT_ASSERT_INT_EQ(DirectDrawCreate(NULL, &device, NULL), DX_DD_OK);
	XVT_ASSERT_INT_EQ(device->lpVtbl->SetDisplayMode(device, 640, 480, 16),
			  DX_DD_OK);
	g_frontState.directDraw = device;
	g_frontState.primarySurface =
		XvtTest_CreateSurface(device, DDSCAPS_PRIMARYSURFACE, 0);
	g_frontState.backBufferSurface = XvtTest_CreateSurface(
		device, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY, 1);
	memset(&desc, 0, sizeof desc);
	desc.dwSize = sizeof desc;
	XVT_ASSERT_INT_EQ(
		g_frontState.backBufferSurface->lpVtbl->Lock(
			g_frontState.backBufferSurface, NULL, &desc, 0, NULL),
		DX_DD_OK);
	g_frontState.backBufferPitch = desc.lPitch;
	g_frontState.backBufferSurface->lpVtbl->Unlock(
		g_frontState.backBufferSurface, NULL);
	g_frontState.backBufferLocked = 0;
	g_frontState.displayBpp = 16;
	g_noPageFlip = 1;
}

/* Releases what Open made and clears the frontend's pointers to it. */
static inline void XvtTest_CloseDisplay(void)
{
	if (!g_frontState.directDraw) {
		return;
	}
	FrontendDisplay_UnlockBackBuffer();
	g_frontState.backBufferSurface->lpVtbl->Release(
		g_frontState.backBufferSurface);
	g_frontState.primarySurface->lpVtbl->Release(
		g_frontState.primarySurface);
	g_frontState.directDraw->lpVtbl->Release(g_frontState.directDraw);
	g_frontState.backBufferSurface = NULL;
	g_frontState.primarySurface = NULL;
	g_frontState.directDraw = NULL;
	g_drawSurfacePtr = NULL;
	AeronDx5_Shutdown();
}

#endif
