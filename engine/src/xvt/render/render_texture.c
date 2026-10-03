#include "xvt/render/render_texture.h"

#include "xvt/render/flight_palette.h"
#include "xvt/render/renderer.h"
#include "xvt/render/std3d.h"
#include "xvt/util/debug_console.h"
#include "xvt_runtime/compat/pointer_key.h"

#include <stdint.h>
#include <string.h>

/* Key of each entry of g_renderTextureCache, the address of the image data it
 * was made from; NULL for an unclaimed entry.
 * RenderTexture_FindOrAllocateCacheEntry writes it and clears it all when
 * g_renderTextureCacheCursor is -1. */
// GLOBAL: XVT 0xA68750
const void *g_renderTextureCacheKeys[1024] = {0};
/* The hardware texture cache, 1024 entries found through
 * g_renderTextureCacheKeys by open addressing; std3D fills an entry when it
 * uploads a texture. */
// GLOBAL: XVT 0xA69750
struct Std3DTexCacheNode g_renderTextureCache[1024] = {0};
/* Index of the cache entry RenderTexture_FindOrAllocateCacheEntry last
 * returned, 0 to 1023; -1, set by Renderer_InitD3DDevice, makes the next lookup
 * clear the cache. */
// GLOBAL: XVT 0x52F864
int g_renderTextureCacheCursor = 0;
/* Buffer RenderTexture_GetOrCreateBitmap decodes a run-length image into, 8
 * bits per pixel, up to 65536 pixels, before the upload. */
// GLOBAL: XVT 0x52F8F0
uint8_t g_renderTextureDecodeScratch[65536] = {0};
/* Buffer RenderTexture_GetOrCreateColorKey copies an image into with its
 * transparent pixels set to index 0, up to 65536 pixels, before the upload. */
// GLOBAL: XVT 0x53F978
uint8_t g_renderTextureColorKeyScratch[65536] = {0};
/* Mask of the run-length bits in a run byte, by run-length format 0 to 8:
 * (1 << format) - 1. */
// GLOBAL: XVT 0x51A530
const uint8_t g_bitmapRleRunLengthMaskByFormat[9] = {0,	 1,  3,	  7,  15,
						     31, 63, 127, 255};
/* Shift that takes the color offset out of a run byte, by run-length format 0
 * to 8: the format itself. */
// GLOBAL: XVT 0x51A540
const uint8_t g_bitmapRleColorIndexShiftByFormat[9] = {0, 1, 2, 3, 4,
						       5, 6, 7, 8};

/* Finds the texture cache entry keyed by cacheKey, an image's address, or
 * claims a free one for it. When g_renderTextureCacheCursor is -1 it first
 * clears the 1024 keys and every entry's bCached. It looks from slot
 * XvtPointerKey_LowBits(cacheKey) & 1023 onward, wrapping, for the key, then
 * from the same slot for an entry with bCached 0, records the key there and
 * returns that entry, leaving its index in g_renderTextureCacheCursor. When all
 * 1024 are cached it writes a line to the debug console with
 * DebugConsole_WriteText and returns the start slot's entry with its key
 * unchanged. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4079F0
struct Std3DTexCacheNode *
RenderTexture_FindOrAllocateCacheEntry(const void *cacheKey)
{
	int probeCount;
	int hashSlot;

	if (g_renderTextureCacheCursor == -1) {
		memset(g_renderTextureCacheKeys, 0,
		       sizeof(g_renderTextureCacheKeys));
		for (probeCount = 0; probeCount < 1024; ++probeCount) {
			g_renderTextureCache[probeCount].bCached = 0;
		}
	}

	hashSlot = XvtPointerKey_LowBits(cacheKey) & 1023;
	probeCount = 0;
	g_renderTextureCacheCursor = hashSlot;
	do {
		if (g_renderTextureCacheKeys[g_renderTextureCacheCursor] ==
		    cacheKey) {
			break;
		}
		++g_renderTextureCacheCursor;
		if (g_renderTextureCacheCursor == 1024) {
			g_renderTextureCacheCursor = 0;
		}
		++probeCount;
	} while (probeCount < 1024);
	if (probeCount == 1024) {
		g_renderTextureCacheCursor = hashSlot;
		for (probeCount = 0; probeCount < 1024; ++probeCount) {
			if (g_renderTextureCache[g_renderTextureCacheCursor]
				    .bCached == 0) {
				break;
			}
			++g_renderTextureCacheCursor;
			if (g_renderTextureCacheCursor == 1024) {
				g_renderTextureCacheCursor = 0;
			}
		}
		if (probeCount == 1024) {
			DebugConsole_WriteText(
				"\n\n\n\n\n\nRAN OUT OF MYCACHETEXTURES!!!\n");
			return &g_renderTextureCache
				[g_renderTextureCacheCursor];
		}
	}

	g_renderTextureCacheKeys[g_renderTextureCacheCursor] = cacheKey;
	return &g_renderTextureCache[g_renderTextureCacheCursor];
}

/* Returns the hardware texture for a run-length image, decoding and uploading
 * it the first time. Returns NULL when width * height is over 65536 or
 * std3D_AddToTextureCache fails. An entry already cached for pixels is
 * refreshed with std3D_CacheTextureSurface and returned. Otherwise it decodes
 * into g_renderTextureDecodeScratch, 8 bits per pixel: each row runs to a 0xFE
 * byte; 0xFB sets the color base from the next two bytes, low byte first; 0xFC
 * writes input[1] + 1 pixels of 0; 0xFD writes input[1] + 1 pixels of color
 * input[2]; any other byte writes (byte & mask) + 1 pixels of color
 * base + (byte >> shift), mask and shift taken by rleFormat from the two
 * tables. Runs are cut at the row's width, short rows are filled with 0, and a
 * 0xFF at a row's start ends the image, the rest filled with 0. It sets
 * palette[0] to the 16-bit pixel of g_flightTransparentColorIndex and converts
 * colors 0 to the highest one used (std3D_ConvertPaletteTo1555 when the device
 * takes alpha textures and not color-key ones, else
 * std3D_CopyPaletteToScratch16) and uploads with color keying. While the device
 * has color-key textures it turns its alpha-texture flag off for the call.
 * RenderQuad_DrawRotatedSprite is its only caller. */
// FUNCTION: XVT 0x407AF0
struct Std3DTexCacheNode *RenderTexture_GetOrCreateBitmap(int width, int height,
							  uint16_t *palette,
							  const uint8_t *pixels,
							  int rleFormat)
{
	enum {
		BITMAP_RLE_SET_COLOR_BASE = 0xFB,
		BITMAP_RLE_TRANSPARENT_RUN = 0xFC,
		BITMAP_RLE_SOLID_RUN = 0xFD,
		BITMAP_RLE_END_ROW = 0xFE,
		BITMAP_RLE_END_IMAGE = 0xFF,
		INDEXED_TEXTURE_BITS_PER_PIXEL = 8,
		MAX_BITMAP_PIXELS = 65536
	};

	struct Std3DTexCacheNode *node;
	int padCount;
	struct Std3DVBuffer source;
	uint8_t *output;
	const uint8_t *input;
	uint8_t *rowEnd;
	uint8_t color;
	uint8_t runLength;
	int colorBase;
	unsigned int maxColor = 0;
	int row;
	int column;
	int oldAlphaTexture;

	if (width * height > MAX_BITMAP_PIXELS) {
		DebugPrintf("Error: Bitmap too large! (%d,%d)\n", width,
			    height);
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
		if (*input == BITMAP_RLE_END_IMAGE) {
			break;
		}
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
						runLength = (uint8_t)(width -
								      column);
						column = width;
					}
					while (runLength-- != 0) {
						*output++ = 0;
					}
				}
			} else {
				if (*input == BITMAP_RLE_SOLID_RUN) {
					runLength = input[1] + 1;
					color = input[2];
					input += 3;
				} else {
					color = (uint8_t)(colorBase +
							  (*input >>
							   g_bitmapRleColorIndexShiftByFormat
								   [rleFormat]));
					runLength =
						(*input &
						 g_bitmapRleRunLengthMaskByFormat
							 [rleFormat]) +
						1;
					++input;
				}
				if (maxColor < color) {
					maxColor = color;
				}
				if (column < width) {
					column += runLength;
					if (width < column) {
						column -= runLength;
						runLength = (uint8_t)(width -
								      column);
						column = width;
					}
					while (runLength-- != 0) {
						*output++ = color;
					}
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
	if (row < height) {
		memset(output, 0, (size_t)width * (size_t)(height - row));
	}
	memset(&source, 0, sizeof(source));
	source.storageType = 0;
	source.raster.width = (unsigned int)width;
	source.raster.height = (unsigned int)height;
	source.raster.rowPitch = (unsigned int)width;
	source.pixels = g_renderTextureDecodeScratch;
	source.raster.bpp = 8;
	source.raster.colorMode = STDCOLOR_PAL;
	oldAlphaTexture = g_pStd3DCurDevice->caps.bAlphaTexture;
	if (g_pStd3DCurDevice->caps.bColorKeyTexture != 0) {
		g_pStd3DCurDevice->caps.bAlphaTexture = 0;
	}
	if (g_pStd3DCurDevice->caps.bAlphaTexture != 0) {
		palette[0] =
			g_flightPalette16Bpp[g_flightTransparentColorIndex];
		std3D_ConvertPaletteTo1555(palette, (int)maxColor + 1);
	} else {
		palette[0] =
			g_flightPalette16Bpp[g_flightTransparentColorIndex];
		std3D_CopyPaletteToScratch16(palette, (int)maxColor + 1);
	}
	if (std3D_AddToTextureCache(&source, node, 1, 0) == 0) {
		DebugPrintf(
			"AddToTextureCache returned NULL! (colorkey, (%d,%d))\n",
			width, height);
		if (g_pStd3DCurDevice->caps.bColorKeyTexture != 0) {
			g_pStd3DCurDevice->caps.bAlphaTexture = oldAlphaTexture;
		}
		return NULL;
	}
	if (g_pStd3DCurDevice->caps.bColorKeyTexture != 0) {
		g_pStd3DCurDevice->caps.bAlphaTexture = oldAlphaTexture;
	}
	return node;
}

/* Returns the hardware texture for an 8-bit image drawn without transparency:
 * the cached entry for pixels, refreshed with std3D_CacheTextureSurface, or a
 * new upload of pixels with its 256 palette colors copied by
 * std3D_CopyPaletteToScratch16. Returns NULL when the upload fails. */
// FUNCTION: XVT 0x407E40
struct Std3DTexCacheNode *
RenderTexture_GetOrCreateOpaque(int width, int height, const uint16_t *palette,
				const uint8_t *pixels)
{
	enum { INDEXED_TEXTURE_BITS_PER_PIXEL = 8, PALETTE_COLOR_COUNT = 256 };

	struct Std3DTexCacheNode *node;
	struct Std3DVBuffer source;

	node = RenderTexture_FindOrAllocateCacheEntry(pixels);
	if (node->bCached != 0) {
		std3D_CacheTextureSurface(node);
		return node;
	}
	memset(&source, 0, sizeof(source));
	source.pixels = (void *)pixels;
	source.storageType = 0;
	source.raster.width = (unsigned int)width;
	source.raster.height = (unsigned int)height;
	source.raster.rowPitch = (unsigned int)width;
	source.raster.bpp = INDEXED_TEXTURE_BITS_PER_PIXEL;
	source.raster.colorMode = STDCOLOR_PAL;
	std3D_CopyPaletteToScratch16(palette, PALETTE_COLOR_COUNT);
	if (std3D_AddToTextureCache(&source, node, 0, 0) == 0) {
		DebugPrintf(
			"AddToTextureCache returned NULL! (nokey (%d,%d))\n",
			width, height);
		return NULL;
	}
	return node;
}

/* Returns the color-keyed hardware texture for an 8-bit image, cached under
 * pixels + 1 so it does not share the opaque texture's entry. It copies the
 * image into g_renderTextureColorKeyScratch, every pixel whose palette color is
 * 0 becoming index 0 and every other pixel of index 0 becoming the index
 * palette[256] holds; returns NULL when no pixel is visible. For the upload
 * palette entry 0 is the 16-bit pixel of g_flightTransparentColorIndex and that
 * index holds the old color 0; the 256 colors are converted as in
 * RenderTexture_GetOrCreateBitmap. Afterwards palette[0] is put back and the
 * moved entry set to 0. Returns NULL when the upload fails. Reads palette[256],
 * past the 256 colors. */
// FUNCTION: XVT 0x407F10
struct Std3DTexCacheNode *
RenderTexture_GetOrCreateColorKey(int width, int height, uint16_t *palette,
				  const uint8_t *pixels)
{
	enum { INDEXED_TEXTURE_BITS_PER_PIXEL = 8, PALETTE_COLOR_COUNT = 256 };

	struct Std3DTexCacheNode *node;
	int pixelCount;
	struct Std3DVBuffer source;
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
	pixelCount = width * height;
	for (pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
		uint8_t colorIndex = *pixels++;
		if (palette[colorIndex] == 0) {
			g_renderTextureColorKeyScratch[pixelIndex] = 0;
		} else {
			hasVisiblePixels = 1;
			if (colorIndex != 0) {
				g_renderTextureColorKeyScratch[pixelIndex] =
					colorIndex;
			} else {
				g_renderTextureColorKeyScratch[pixelIndex] =
					(uint8_t)transparentIndex;
			}
		}
	}
	if (hasVisiblePixels == 0) {
		return NULL;
	}
	source.storageType = 0;
	source.raster.width = (unsigned int)width;
	source.raster.height = (unsigned int)height;
	source.raster.rowPitch = (unsigned int)width;
	source.raster.colorMode = STDCOLOR_PAL;
	source.pixels = g_renderTextureColorKeyScratch;
	source.raster.bpp = INDEXED_TEXTURE_BITS_PER_PIXEL;
	oldAlphaTexture = g_pStd3DCurDevice->caps.bAlphaTexture;
	if (g_pStd3DCurDevice->caps.bColorKeyTexture != 0) {
		g_pStd3DCurDevice->caps.bAlphaTexture = 0;
	}
	if (g_pStd3DCurDevice->caps.bAlphaTexture != 0) {
		palette[transparentIndex] = palette[0];
		palette[0] =
			g_flightPalette16Bpp[g_flightTransparentColorIndex];
		std3D_ConvertPaletteTo1555(palette, PALETTE_COLOR_COUNT);
		palette[0] = palette[transparentIndex];
		palette[transparentIndex] = 0;
	} else {
		uint16_t *transparentColor = &palette[transparentIndex];
		*transparentColor = palette[0];
		palette[0] =
			g_flightPalette16Bpp[g_flightTransparentColorIndex];
		std3D_CopyPaletteToScratch16(palette, PALETTE_COLOR_COUNT);
		palette[0] = *transparentColor;
		*transparentColor = 0;
	}
	if (std3D_AddToTextureCache(&source, node, 1, 0) == 0) {
		DebugPrintf(
			"AlphaTex:AddToTextureCache returned NULL! (colorkey, (%d,%d))\n",
			width, height);
		if (g_pStd3DCurDevice->caps.bColorKeyTexture != 0) {
			g_pStd3DCurDevice->caps.bAlphaTexture = oldAlphaTexture;
		}
		return NULL;
	}
	if (g_pStd3DCurDevice->caps.bColorKeyTexture != 0) {
		g_pStd3DCurDevice->caps.bAlphaTexture = oldAlphaTexture;
	}
	return node;
}
