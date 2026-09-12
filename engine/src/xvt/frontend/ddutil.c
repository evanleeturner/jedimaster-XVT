#include "xvt/frontend/ddutil.h"

#include "xvt/util/win32.h"

#include <stddef.h>
#include <string.h>

typedef HRESULT(AERON_DXAPI* DDUtilSurfaceGetDcFunc)(IDirectDrawSurface* surface, void** dc);
typedef HRESULT(AERON_DXAPI* DDUtilSurfaceReleaseDcFunc)(IDirectDrawSurface* surface, void* dc);

#ifndef XVT_MODERN
__declspec(dllimport) void* __stdcall CreateCompatibleDC(void* dc);
__declspec(dllimport) void* __stdcall SelectObject(void* dc, void* object);
__declspec(dllimport) int __stdcall GetObjectA(void* object, int bufferSize, void* buffer);
__declspec(dllimport) int __stdcall StretchBlt(void* destinationDc, int xDestination, int yDestination,
											   int destinationWidth, int destinationHeight, void* sourceDc,
											   int xSource, int ySource, int sourceWidth, int sourceHeight,
											   unsigned long rasterOperation);
__declspec(dllimport) int __stdcall DeleteDC(void* dc);
__declspec(dllimport) void* __stdcall GetModuleHandleA(const char* moduleName);
__declspec(dllimport) void* __stdcall LoadImageA(void* instance, const char* name, unsigned int type,
												 int width, int height, unsigned int loadFlags);
__declspec(dllimport) int __stdcall DeleteObject(void* object);
#endif

// FUNCTION: XVT 0x4F0BE0
IDirectDrawSurface* DDUtil_LoadBitmapSurface(IDirectDraw* directDraw, const char* bitmapName, int width,
											 int height) {
#ifdef XVT_MODERN
	(void)directDraw;
	(void)bitmapName;
	(void)width;
	(void)height;
	return NULL;
#else
	void* bitmap;
	IDirectDrawSurface* surface;
	DDSURFACEDESC surfaceDesc;
	BITMAP bitmapInfo;

	bitmap = LoadImageA(NULL, bitmapName, 0, width, height, 0x2010);
	if (bitmap == NULL) {
		return NULL;
	}
	GetObjectA(bitmap, sizeof(bitmapInfo), &bitmapInfo);
	memset(&surfaceDesc, 0, sizeof(surfaceDesc));
	surfaceDesc.dwWidth = bitmapInfo.bmWidth;
	surfaceDesc.dwHeight = bitmapInfo.bmHeight;
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	surfaceDesc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
	surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
	if (directDraw->lpVtbl->CreateSurface(directDraw, &surfaceDesc, &surface, NULL) != DX_DD_OK) {
		return NULL;
	}
	DDUtil_CopyBitmapToSurface(surface, bitmap, 0, 0, 0, 0);
	DeleteObject(bitmap);
	return surface;
#endif
}

// FUNCTION: XVT 0x4F0CC0
HRESULT DDUtil_ReloadBitmapSurface(IDirectDrawSurface* surface, const char* bitmapName) {
#ifdef XVT_MODERN
	(void)surface;
	(void)bitmapName;
	return DX_E_NOTIMPL;
#else
	void* bitmap;
	HRESULT result;

	bitmap = LoadImageA(GetModuleHandleA(NULL), bitmapName, 0, 0, 0, 0x2000);
	if (bitmap == NULL) {
		bitmap = LoadImageA(NULL, bitmapName, 0, 0, 0, 0x2010);
		if (bitmap == NULL) {
			return DX_E_FAIL;
		}
	}
	result = DDUtil_CopyBitmapToSurface(surface, bitmap, 0, 0, 0, 0);
	DeleteObject(bitmap);
	return result;
#endif
}

// FUNCTION: XVT 0x4F0D30
HRESULT DDUtil_CopyBitmapToSurface(IDirectDrawSurface* surface, void* bitmap, int xSrc, int ySrc, int width,
								   int height) {
#ifdef XVT_MODERN
	(void)surface;
	(void)bitmap;
	(void)xSrc;
	(void)ySrc;
	(void)width;
	(void)height;
	return DX_E_NOTIMPL;
#else
	void* compatibleDc;
	int actualWidth;
	int actualHeight;
	void* destinationDc;
	HRESULT result;
	int bitmapObject[6];
	DDSURFACEDESC surfaceDesc;

	if (bitmap == NULL || surface == NULL) {
		return DX_E_FAIL;
	}

	surface->lpVtbl->Restore(surface);
	compatibleDc = CreateCompatibleDC(NULL);
	SelectObject(compatibleDc, bitmap);
	GetObjectA(bitmap, sizeof(bitmapObject), bitmapObject);
	actualWidth = width;
	if (actualWidth == 0) {
		actualWidth = bitmapObject[1];
	}
	actualHeight = height;
	if (actualHeight == 0) {
		actualHeight = bitmapObject[2];
	}
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	surfaceDesc.dwFlags = DDSD_HEIGHT | DDSD_WIDTH;
	surface->lpVtbl->GetSurfaceDesc(surface, &surfaceDesc);
	result = ((DDUtilSurfaceGetDcFunc)surface->lpVtbl->GetDC)(surface, &destinationDc);
	if (result == DX_DD_OK) {
		StretchBlt(destinationDc, 0, 0, surfaceDesc.dwWidth, surfaceDesc.dwHeight, compatibleDc, xSrc, ySrc,
				   actualWidth, actualHeight, DDROP_SRCCOPY);
		((DDUtilSurfaceReleaseDcFunc)surface->lpVtbl->ReleaseDC)(surface, destinationDc);
	}
	DeleteDC(compatibleDc);
	return result;
#endif
}
