#ifndef XVT_FRONTEND_DDUTIL_H
#define XVT_FRONTEND_DDUTIL_H

#include "aeron/compat/ddraw.h"
#include "aeron/compat/win_types.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

IDirectDrawSurface* DDUtil_LoadBitmapSurface(IDirectDraw* directDraw, const char* bitmapName, int width,
											 int height);
HRESULT DDUtil_ReloadBitmapSurface(IDirectDrawSurface* surface, const char* bitmapName);
HRESULT DDUtil_CopyBitmapToSurface(IDirectDrawSurface* surface, void* bitmap, int xSrc, int ySrc, int width,
								   int height);

#ifdef __cplusplus
}
#endif

#endif
