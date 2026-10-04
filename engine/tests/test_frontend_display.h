/* A frontend display with no window, for tests that run frontend frames: a DirectDraw device from Aeron's
 * compatibility layer with a 640x480, 16-bit back buffer in memory and a primary surface to present to. The
 * recovered frontend can then lock, draw into, clear and present its back buffer. No offscreen surface is
 * made, so the frontend's offscreen restore must stay off while frames run, and no fonts or images are
 * loaded, so text and sprites draw nothing.
 *
 * Open after clearing g_front_state; Close releases the surfaces and the device and drops what Aeron kept of
 * the last presented frame. */
#ifndef XVT_TESTS_TEST_FRONTEND_DISPLAY_H
#define XVT_TESTS_TEST_FRONTEND_DISPLAY_H

#include <string.h>

#include "aeron/compat/ddraw.h"
#include "aeron/compat/host.h"
#include "test_assert.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"

/* Creates one surface of the device with the given caps; size flags are set for an offscreen surface. */
static inline IDirectDrawSurface *
xvt_test_create_surface(IDirectDraw *device, unsigned caps, int sized)
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
static inline void xvt_test_open_display(void)
{
	IDirectDraw *device = NULL;
	DDSURFACEDESC desc;
	XVT_ASSERT_INT_EQ(DirectDrawCreate(NULL, &device, NULL), DX_DD_OK);
	XVT_ASSERT_INT_EQ(device->lpVtbl->SetDisplayMode(device, 640, 480, 16),
			  DX_DD_OK);
	g_front_state.direct_draw = device;
	g_front_state.primary_surface =
		xvt_test_create_surface(device, DDSCAPS_PRIMARYSURFACE, 0);
	g_front_state.back_buffer_surface = xvt_test_create_surface(
		device, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY, 1);
	memset(&desc, 0, sizeof desc);
	desc.dwSize = sizeof desc;
	XVT_ASSERT_INT_EQ(g_front_state.back_buffer_surface->lpVtbl->Lock(
				  g_front_state.back_buffer_surface, NULL,
				  &desc, 0, NULL),
			  DX_DD_OK);
	g_front_state.back_buffer_pitch = desc.lPitch;
	g_front_state.back_buffer_surface->lpVtbl->Unlock(
		g_front_state.back_buffer_surface, NULL);
	g_front_state.back_buffer_locked = 0;
	g_front_state.display_bpp = 16;
	g_no_page_flip = 1;
}

/* Releases what Open made and clears the frontend's pointers to it. */
static inline void xvt_test_close_display(void)
{
	if (!g_front_state.direct_draw) {
		return;
	}
	frontend_display_unlock_back_buffer();
	g_front_state.back_buffer_surface->lpVtbl->Release(
		g_front_state.back_buffer_surface);
	g_front_state.primary_surface->lpVtbl->Release(
		g_front_state.primary_surface);
	g_front_state.direct_draw->lpVtbl->Release(g_front_state.direct_draw);
	g_front_state.back_buffer_surface = NULL;
	g_front_state.primary_surface = NULL;
	g_front_state.direct_draw = NULL;
	g_draw_surface_ptr = NULL;
	AeronDx5_Shutdown();
}

#endif
