#include "xvt/frontend/ddutil.h"

#include <stddef.h>
#include <string.h>

#include "xvt/util/win32.h"

typedef HRESULT(AERON_DXAPI *dd_util_surface_get_dc_func)(
	IDirectDrawSurface *surface, void **dc);
typedef HRESULT(AERON_DXAPI *dd_util_surface_release_dc_func)(
	IDirectDrawSurface *surface, void *dc);

#ifndef XVT_MODERN
__declspec(dllimport) void *__stdcall CreateCompatibleDC(void *dc);
__declspec(dllimport) void *__stdcall SelectObject(void *dc, void *object);
__declspec(dllimport) int __stdcall GetObjectA(void *object, int buffer_size,
					       void *buffer);
__declspec(dllimport) int __stdcall
StretchBlt(void *destination_dc, int x_destination, int y_destination,
	   int destination_width, int destination_height, void *source_dc,
	   int x_source, int y_source, int source_width, int source_height,
	   unsigned long raster_operation);
__declspec(dllimport) int __stdcall DeleteDC(void *dc);
__declspec(dllimport) void *__stdcall GetModuleHandleA(const char *module_name);
__declspec(dllimport) void *__stdcall
LoadImageA(void *instance, const char *name, unsigned int type, int width,
	   int height, unsigned int load_flags);
__declspec(dllimport) int __stdcall DeleteObject(void *object);
#endif

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
#ifdef XVT_MODERN
	(void)direct_draw;
	(void)bitmap_name;
	(void)width;
	(void)height;
	return NULL;
#else
	void *bitmap = LoadImageA(NULL, bitmap_name, 0, width, height, 0x2010);
	if (bitmap == NULL) {
		return NULL;
	}
	struct BITMAP bitmap_info;
	GetObjectA(bitmap, sizeof(bitmap_info), &bitmap_info);
	DDSURFACEDESC surface_desc;
	memset(&surface_desc, 0, sizeof(surface_desc));
	surface_desc.dwWidth = bitmap_info.bmWidth;
	surface_desc.dwHeight = bitmap_info.bmHeight;
	surface_desc.dwSize = sizeof(surface_desc);
	surface_desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
	surface_desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
	IDirectDrawSurface *surface;
	if (direct_draw->lpVtbl->CreateSurface(direct_draw, &surface_desc,
					       &surface, NULL) != DX_DD_OK) {
		return NULL;
	}
	dd_util_copy_bitmap_to_surface(surface, bitmap, 0, 0, 0, 0);
	DeleteObject(bitmap);
	return surface;
#endif
}

/* Nothing calls this. The original build loads bitmapName as a bitmap resource
 * of the executable, else as a bitmap file, copies it over the whole surface
 * with dd_util_copy_bitmap_to_surface and returns that result, or DX_E_FAIL when
 * both loads fail. The modern build returns DX_E_NOTIMPL. */
// FUNCTION: XVT 0x4F0CC0
HRESULT dd_util_reload_bitmap_surface(IDirectDrawSurface *surface,
				      const char *bitmap_name)
{
#ifdef XVT_MODERN
	(void)surface;
	(void)bitmap_name;
	return DX_E_NOTIMPL;
#else
	void *bitmap = LoadImageA(GetModuleHandleA(NULL), bitmap_name, 0, 0, 0,
				  0x2000);
	if (bitmap == NULL) {
		bitmap = LoadImageA(NULL, bitmap_name, 0, 0, 0, 0x2010);
		if (bitmap == NULL) {
			return DX_E_FAIL;
		}
	}
	HRESULT result =
		dd_util_copy_bitmap_to_surface(surface, bitmap, 0, 0, 0, 0);
	DeleteObject(bitmap);
	return result;
#endif
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
#ifdef XVT_MODERN
	(void)surface;
	(void)bitmap;
	(void)x_src;
	(void)y_src;
	(void)width;
	(void)height;
	return DX_E_NOTIMPL;
#else
	if (bitmap == NULL || surface == NULL) {
		return DX_E_FAIL;
	}

	surface->lpVtbl->Restore(surface);
	void *compatible_dc = CreateCompatibleDC(NULL);
	SelectObject(compatible_dc, bitmap);
	int bitmap_object[6];
	GetObjectA(bitmap, sizeof(bitmap_object), bitmap_object);
	int actual_width = width;
	if (actual_width == 0) {
		actual_width = bitmap_object[1];
	}
	int actual_height = height;
	if (actual_height == 0) {
		actual_height = bitmap_object[2];
	}
	DDSURFACEDESC surface_desc;
	surface_desc.dwSize = sizeof(surface_desc);
	surface_desc.dwFlags = DDSD_HEIGHT | DDSD_WIDTH;
	surface->lpVtbl->GetSurfaceDesc(surface, &surface_desc);
	void *destination_dc;
	HRESULT result = ((dd_util_surface_get_dc_func)surface->lpVtbl->GetDC)(
		surface, &destination_dc);
	if (result == DX_DD_OK) {
		StretchBlt(destination_dc, 0, 0, surface_desc.dwWidth,
			   surface_desc.dwHeight, compatible_dc, x_src, y_src,
			   actual_width, actual_height, DDROP_SRCCOPY);
		((dd_util_surface_release_dc_func)surface->lpVtbl->ReleaseDC)(
			surface, destination_dc);
	}
	DeleteDC(compatible_dc);
	return result;
#endif
}
