#ifndef XVT_RENDER_RENDER_TEXTURE_H
#define XVT_RENDER_RENDER_TEXTURE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t RenderTextureDecodeScratch[65536];

extern int g_renderTextureCacheCursor;

Std3DTexCacheNode* RenderTexture_FindOrAllocateCacheEntry(const void* cacheKey);
Std3DTexCacheNode* RenderTexture_GetOrCreateBitmap(int width, int height, uint16_t* palette,
												   const uint8_t* pixels, int rleFormat);
Std3DTexCacheNode* RenderTexture_GetOrCreateOpaque(int width, int height, const uint16_t* palette,
												   const uint8_t* pixels);
Std3DTexCacheNode* RenderTexture_GetOrCreateColorKey(int width, int height, uint16_t* palette,
													 const uint8_t* pixels);

#ifdef __cplusplus
}
#endif

#endif
