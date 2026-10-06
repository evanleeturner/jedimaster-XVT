#include "xvt/frontend/ddutil.h"

#include <stddef.h>
#include <string.h>

#include "xvt/util/win32.h"

typedef HRESULT(AERON_DXAPI *dd_util_surface_get_dc_func)(
	IDirectDrawSurface *surface, void **dc);
typedef HRESULT(AERON_DXAPI *dd_util_surface_release_dc_func)(
	IDirectDrawSurface *surface, void *dc);


/* Nothing calls this. The original build loads the bitmap file bitmapName as a
 * DIB section (LoadImageA flags 0x2010, LR_CREATEDIBSECTION | LR_LOADFROMFILE)
 * at width by height, 0 meaning its own size, creates an offscreen surface of
 * the loaded size, copies the bitmap into it with dd_util_copy_bitmap_to_surface,
 * frees the bitmap and returns the surface. It returns NULL when the load or
 * the surface creation fails, the latter without freeing the bitmap. The modern
 * build returns NULL. */
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

/* Nothing calls this. The original build loads bitmapName as a bitmap resource
 * of the executable, else as a bitmap file, copies it over the whole surface
 * with dd_util_copy_bitmap_to_surface and returns that result, or DX_E_FAIL when
 * both loads fail. The modern build returns DX_E_NOTIMPL. */
// FUNCTION: XVT 0x4F0CC0
HRESULT dd_util_reload_bitmap_surface(IDirectDrawSurface *surface,
				      const char *bitmap_name)
{
	(void)surface;
	(void)bitmap_name;
	return DX_E_NOTIMPL;
}

/* Only dd_util_load_bitmap_surface and dd_util_reload_bitmap_surface call this, and
 * nothing calls them. The original build restores the surface, then stretches
 * the bitmap's width by height pixels from (xSrc, ySrc), 0 meaning the bitmap's
 * own width or height, over the whole surface through GDI. Returns the
 * surface's GetDC result, DX_DD_OK when it copied, or DX_E_FAIL when surface or
 * bitmap is NULL. The modern build returns DX_E_NOTIMPL. */
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
