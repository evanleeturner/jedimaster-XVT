#ifndef XVT_FRONTEND_DDUTIL_H
#define XVT_FRONTEND_DDUTIL_H

#include "aeron/compat/ddraw.h"
#include "aeron/compat/win_types.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

IDirectDrawSurface *dd_util_load_bitmap_surface(IDirectDraw *direct_draw,
						const char *bitmap_name,
						int width, int height);
HRESULT dd_util_reload_bitmap_surface(IDirectDrawSurface *surface,
				      const char *bitmap_name);
HRESULT dd_util_copy_bitmap_to_surface(IDirectDrawSurface *surface,
				       void *bitmap, int x_src, int y_src,
				       int width, int height);

#ifdef __cplusplus
}
#endif

#endif
