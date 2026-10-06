#include "xvt/frontend/ddutil.h"

#include <stddef.h>
#include <string.h>

#include "xvt/util/win32.h"

typedef HRESULT(AERON_DXAPI *dd_util_surface_get_dc_func)(
	IDirectDrawSurface *surface, void **dc);
typedef HRESULT(AERON_DXAPI *dd_util_surface_release_dc_func)(
	IDirectDrawSurface *surface, void *dc);

/* Nothing calls this. It returns NULL, where the 1997 game loaded the bitmap
 * file bitmapName into a new offscreen surface. */
// FUNCTION: XVT 0x4F0BE0
IDirectDrawSurface *dd_util_load_bitmap_surface(IDirectDraw *direct_draw,
						const char *bitmap_name,
						int width, int height)
{
	(void)direct_draw;
	(void)bitmap_name;
	(void)width;
	(void)height;
	return NULL;
}

/* Nothing calls this. It returns DX_E_NOTIMPL, where the 1997 game loaded
 * bitmapName and copied it over the whole surface. */
// FUNCTION: XVT 0x4F0CC0
HRESULT dd_util_reload_bitmap_surface(IDirectDrawSurface *surface,
				      const char *bitmap_name)
{
	(void)surface;
	(void)bitmap_name;
	return DX_E_NOTIMPL;
}

/* Only dd_util_load_bitmap_surface and dd_util_reload_bitmap_surface call this,
 * and nothing calls them. It returns DX_E_NOTIMPL, where the 1997 game
 * stretched the bitmap over the whole surface through GDI. */
// FUNCTION: XVT 0x4F0D30
HRESULT dd_util_copy_bitmap_to_surface(IDirectDrawSurface *surface,
				       void *bitmap, int x_src, int y_src,
				       int width, int height)
{
	(void)surface;
	(void)bitmap;
	(void)x_src;
	(void)y_src;
	(void)width;
	(void)height;
	return DX_E_NOTIMPL;
}
