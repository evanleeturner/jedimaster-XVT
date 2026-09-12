#include "xvt/render/render_texture.h"

#include "xvt/render/flight_palette.h"
#include "xvt/render/renderer.h"
#include "xvt/render/std3d.h"
#include "xvt/util/debug_console.h"
#include "xvt_runtime/compat/pointer_key.h"

#include <stdint.h>
#include <string.h>

// GLOBAL: XVT 0xA68750
const void* g_renderTextureCacheKeys[1024] = { 0 };
// GLOBAL: XVT 0xA69750
Std3DTexCacheNode g_renderTextureCache[1024] = { 0 };
// GLOBAL: XVT 0x52F864
int g_renderTextureCacheCursor = 0;
// GLOBAL: XVT 0x52F8F0
uint8_t g_renderTextureDecodeScratch[65536] = { 0 };
// GLOBAL: XVT 0x53F978
uint8_t g_renderTextureColorKeyScratch[65536] = { 0 };
// GLOBAL: XVT 0x51A530
const uint8_t g_bitmapRleRunLengthMaskByFormat[9] = { 0, 1, 3, 7, 15, 31, 63, 127, 255 };
// GLOBAL: XVT 0x51A540
const uint8_t g_bitmapRleColorIndexShiftByFormat[9] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4079F0
Std3DTexCacheNode* RenderTexture_FindOrAllocateCacheEntry(const void* cacheKey) {
	int probeCount;
	int hashSlot;

	if (g_renderTextureCacheCursor == -1) {
		memset(g_renderTextureCacheKeys, 0, sizeof(g_renderTextureCacheKeys));
		for (probeCount = 0; probeCount < 1024; ++probeCount)
			g_renderTextureCache[probeCount].bCached = 0;
	}

	hashSlot = XvtPointerKey_LowBits(cacheKey) & 1023;
	probeCount = 0;
	g_renderTextureCacheCursor = hashSlot;
	do {
		if (g_renderTextureCacheKeys[g_renderTextureCacheCursor] == cacheKey)
			break;
		++g_renderTextureCacheCursor;
		if (g_renderTextureCacheCursor == 1024)
			g_renderTextureCacheCursor = 0;
		++probeCount;
	} while (probeCount < 1024);
	if (probeCount == 1024) {
		g_renderTextureCacheCursor = hashSlot;
		for (probeCount = 0; probeCount < 1024; ++probeCount) {
			if (g_renderTextureCache[g_renderTextureCacheCursor].bCached == 0)
				break;
			++g_renderTextureCacheCursor;
			if (g_renderTextureCacheCursor == 1024)
				g_renderTextureCacheCursor = 0;
		}
		if (probeCount == 1024) {
			DebugConsole_WriteText("\n\n\n\n\n\nRAN OUT OF MYCACHETEXTURES!!!\n");
			return &g_renderTextureCache[g_renderTextureCacheCursor];
		}
	}

	g_renderTextureCacheKeys[g_renderTextureCacheCursor] = cacheKey;
	return &g_renderTextureCache[g_renderTextureCacheCursor];
}

// FUNCTION: XVT 0x407AF0
Std3DTexCacheNode* RenderTexture_GetOrCreateBitmap(int width, int height, uint16_t* palette,
												   const uint8_t* pixels, int rleFormat) {
	enum {
		BITMAP_RLE_SET_COLOR_BASE = 0xFB,
		BITMAP_RLE_TRANSPARENT_RUN = 0xFC,
		BITMAP_RLE_SOLID_RUN = 0xFD,
		BITMAP_RLE_END_ROW = 0xFE,
		BITMAP_RLE_END_IMAGE = 0xFF,
		INDEXED_TEXTURE_BITS_PER_PIXEL = 8,
		MAX_BITMAP_PIXELS = 65536
	};

	Std3DTexCacheNode* node;
	int padCount;
	Std3DVBuffer source;
	uint8_t* output;
	const uint8_t* input;
	uint8_t* rowEnd;
	uint8_t color;
	uint8_t runLength;
	int colorBase;
	unsigned int maxColor = 0;
	int row;
	int column;
	int oldAlphaTexture;

	if (width * height > MAX_BITMAP_PIXELS) {
		DebugPrintf("Error: Bitmap too large! (%d,%d)\n", width, height);
		return NULL;
	}
	input = pixels;
	node = RenderTexture_FindOrAllocateCacheEntry(pixels);
	if (node->bCached != 0) {
		std3D_CacheTextureSurface(node);
		return node;
	}
	output = g_renderTextureDecodeScratch;
	colorBase = 0;
	for (row = 0; row < height; ++row) {
		if (*input == BITMAP_RLE_END_IMAGE)
			break;
		column = 0;
		rowEnd = output + width;
		while (*input != BITMAP_RLE_END_ROW) {
			if (*input == BITMAP_RLE_SET_COLOR_BASE) {
				colorBase = input[1] + (input[2] << 8);
				input += 3;
			} else if (*input == BITMAP_RLE_TRANSPARENT_RUN) {
				runLength = input[1] + 1;
				input += 2;
				if (column < width) {
					column += runLength;
					if (width < column) {
						column -= runLength;
						runLength = (uint8_t)(width - column);
						column = width;
					}
					while (runLength-- != 0)
						*output++ = 0;
				}
			} else {
				if (*input == BITMAP_RLE_SOLID_RUN) {
					runLength = input[1] + 1;
					color = input[2];
					input += 3;
				} else {
					color = (uint8_t)(colorBase + (*input >> g_bitmapRleColorIndexShiftByFormat[rleFormat]));
					runLength = (*input & g_bitmapRleRunLengthMaskByFormat[rleFormat]) + 1;
					++input;
				}
				if (maxColor < color)
					maxColor = color;
				if (column < width) {
					column += runLength;
					if (width < column) {
						column -= runLength;
						runLength = (uint8_t)(width - column);
						column = width;
					}
					while (runLength-- != 0)
						*output++ = color;
				}
			}
		}
		++input;
		if (output < rowEnd) {
			padCount = rowEnd - output;
			memset(output, 0, (size_t)padCount);
			output += padCount;
		}
	}
	if (row < height)
		memset(output, 0, (size_t)width * (size_t)(height - row));
	memset(&source, 0, sizeof(source));
	source.storageType = 0;
	source.raster.width = (unsigned int)width;
	source.raster.height = (unsigned int)height;
	source.raster.rowPitch = (unsigned int)width;
	source.pixels = g_renderTextureDecodeScratch;
	source.raster.bpp = 8;
	source.raster.colorMode = STDCOLOR_PAL;
	oldAlphaTexture = g_pStd3DCurDevice->caps.bAlphaTexture;
	if (g_pStd3DCurDevice->caps.bColorKeyTexture != 0)
		g_pStd3DCurDevice->caps.bAlphaTexture = 0;
	if (g_pStd3DCurDevice->caps.bAlphaTexture != 0) {
		palette[0] = g_flightTextPalette[g_flightColorEscapeBypassChar];
		std3D_ConvertTexTo1555(palette, (int)maxColor + 1);
	} else {
		palette[0] = g_flightTextPalette[g_flightColorEscapeBypassChar];
		std3D_CopyPaletteToScratch16(palette, (int)maxColor + 1);
	}
	if (std3D_CreateMipSurface(&source, node, 1, 0) == 0) {
		DebugPrintf("AddToTextureCache returned NULL! (colorkey, (%d,%d))\n", width, height);
		if (g_pStd3DCurDevice->caps.bColorKeyTexture != 0)
			g_pStd3DCurDevice->caps.bAlphaTexture = oldAlphaTexture;
		return NULL;
	}
	if (g_pStd3DCurDevice->caps.bColorKeyTexture != 0)
		g_pStd3DCurDevice->caps.bAlphaTexture = oldAlphaTexture;
	return node;
}

// FUNCTION: XVT 0x407E40
Std3DTexCacheNode* RenderTexture_GetOrCreateOpaque(int width, int height, const uint16_t* palette,
												   const uint8_t* pixels) {
	enum { INDEXED_TEXTURE_BITS_PER_PIXEL = 8, PALETTE_COLOR_COUNT = 256 };

	Std3DTexCacheNode* node;
	Std3DVBuffer source;

	node = RenderTexture_FindOrAllocateCacheEntry(pixels);
	if (node->bCached != 0) {
		std3D_CacheTextureSurface(node);
		return node;
	}
	memset(&source, 0, sizeof(source));
	source.pixels = (void*)pixels;
	source.storageType = 0;
	source.raster.width = (unsigned int)width;
	source.raster.height = (unsigned int)height;
	source.raster.rowPitch = (unsigned int)width;
	source.raster.bpp = INDEXED_TEXTURE_BITS_PER_PIXEL;
	source.raster.colorMode = STDCOLOR_PAL;
	std3D_CopyPaletteToScratch16(palette, PALETTE_COLOR_COUNT);
	if (std3D_CreateMipSurface(&source, node, 0, 0) == 0) {
		DebugPrintf("AddToTextureCache returned NULL! (nokey (%d,%d))\n", width, height);
		return NULL;
	}
	return node;
}

// FUNCTION: XVT 0x407F10
Std3DTexCacheNode* RenderTexture_GetOrCreateColorKey(int width, int height, uint16_t* palette,
													 const uint8_t* pixels) {
	enum { INDEXED_TEXTURE_BITS_PER_PIXEL = 8, PALETTE_COLOR_COUNT = 256 };

	Std3DTexCacheNode* node;
	int padCount;
	Std3DVBuffer source;
	int transparentIndex;
	int hasVisiblePixels;
	int oldAlphaTexture;
	int pixelIndex;

	node = RenderTexture_FindOrAllocateCacheEntry(pixels + 1);
	if (node->bCached != 0) {
		std3D_CacheTextureSurface(node);
		return node;
	}
	memset(&source, 0, sizeof(source));
	transparentIndex = palette[PALETTE_COLOR_COUNT];
	hasVisiblePixels = 0;
	padCount = width * height;
	for (pixelIndex = 0; pixelIndex < padCount; ++pixelIndex) {
		uint8_t colorIndex = *pixels++;
		if (palette[colorIndex] == 0) {
			g_renderTextureColorKeyScratch[pixelIndex] = 0;
		} else {
			hasVisiblePixels = 1;
			if (colorIndex != 0)
				g_renderTextureColorKeyScratch[pixelIndex] = colorIndex;
			else
				g_renderTextureColorKeyScratch[pixelIndex] = (uint8_t)transparentIndex;
		}
	}
	if (hasVisiblePixels == 0)
		return NULL;
	source.storageType = 0;
	source.raster.width = (unsigned int)width;
	source.raster.height = (unsigned int)height;
	source.raster.rowPitch = (unsigned int)width;
	source.raster.colorMode = STDCOLOR_PAL;
	source.pixels = g_renderTextureColorKeyScratch;
	source.raster.bpp = INDEXED_TEXTURE_BITS_PER_PIXEL;
	oldAlphaTexture = g_pStd3DCurDevice->caps.bAlphaTexture;
	if (g_pStd3DCurDevice->caps.bColorKeyTexture != 0)
		g_pStd3DCurDevice->caps.bAlphaTexture = 0;
	if (g_pStd3DCurDevice->caps.bAlphaTexture != 0) {
		palette[transparentIndex] = palette[0];
		palette[0] = g_flightTextPalette[g_flightColorEscapeBypassChar];
		std3D_ConvertTexTo1555(palette, PALETTE_COLOR_COUNT);
		palette[0] = palette[transparentIndex];
		palette[transparentIndex] = 0;
	} else {
		uint16_t* transparentColor = &palette[transparentIndex];
		*transparentColor = palette[0];
		palette[0] = g_flightTextPalette[g_flightColorEscapeBypassChar];
		std3D_CopyPaletteToScratch16(palette, PALETTE_COLOR_COUNT);
		palette[0] = *transparentColor;
		*transparentColor = 0;
	}
	if (std3D_CreateMipSurface(&source, node, 1, 0) == 0) {
		DebugPrintf("AlphaTex:AddToTextureCache returned NULL! (colorkey, (%d,%d))\n", width, height);
		if (g_pStd3DCurDevice->caps.bColorKeyTexture != 0)
			g_pStd3DCurDevice->caps.bAlphaTexture = oldAlphaTexture;
		return NULL;
	}
	if (g_pStd3DCurDevice->caps.bColorKeyTexture != 0)
		g_pStd3DCurDevice->caps.bAlphaTexture = oldAlphaTexture;
	return node;
}
