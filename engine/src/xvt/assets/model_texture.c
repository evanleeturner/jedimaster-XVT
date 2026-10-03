#include "xvt/assets/model_texture.h"
#include "xvt/assets/file.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/fediskio.h"
#include "xvt/render/color.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/std3d.h"
#include "xvt/util/debug_console.h"
#include <stdlib.h>
#include <string.h>

/* Returns 1 when the 3D device's opaque texture format has 5 green bits
 * (g_pFmtOpaqueTexture), else 0. Its one caller, Display_IsPixelFormat555,
 * returns the same. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40DD40
int ModelTexture_IsHardwareFormat555(void)
{
	return g_pFmtOpaqueTexture->colorInfo.greenBPP == 5;
}

/* Marks a texture palette's see-through colors for the 3D card; the palette is
 * 16 sub-palettes of 256 RGB565 colors. Below g_textureResolutionLevel 2 it
 * only sets palette[2304] to 0. At level 2 it sets to 0 each of the first 256
 * colors that is near black (its three 5-bit components' squares adding to
 * under 32) or that lies more than 16, in squared distance, from the same entry
 * of any of sub-palettes 1 to 6; each other color takes sub-palette 10's entry.
 * Then palette[256] gets the first index set to 0 (0xFFFF when none) and
 * palette[2304] the count set to 0, or 0 when all 256 were. The renderer reads
 * palette[2304] as the opaque half's transparent-index slot. */
// FUNCTION: XVT 0x40DD50
void ModelTexture_FilterHardwarePalette(uint16_t *palette)
{
	uint16_t *entry;
	int red;
	int planeIndex;
	uint16_t *comparisonEntry;
	int blueDelta;
	int greenDelta;
	int redDelta;
	int colorMagnitude;
	int colorDistance;
	int clearedCount;
	int paletteIndex;
	int firstClearedIndex;
	int blue;
	int green;
	uint16_t color;
	uint16_t comparisonColor;

	if (g_textureResolutionLevel != 2) {
		palette[2304] = 0;
		return;
	}
	{
		clearedCount = 0;
		paletteIndex = 0;
		firstClearedIndex = -1;
		entry = palette;
		do {
			color = *entry;
			blue = color & 0x1F;
			color >>= 6;
			green = color & 0x1F;
			color >>= 5;
			red = color & 0x1F;
			colorMagnitude = blue * blue;
			colorMagnitude += green * green;
			colorMagnitude += red * red;
			if (colorMagnitude < 32) {
				*entry = 0;
				++clearedCount;
				if (firstClearedIndex == -1) {
					firstClearedIndex = paletteIndex;
				}
			} else {
				planeIndex = 1;
				comparisonEntry = entry + 256;
				for (;;) {
					comparisonColor = *comparisonEntry;
					blueDelta =
						(comparisonColor & 0x1F) - blue;
					comparisonColor >>= 6;
					greenDelta = (comparisonColor & 0x1F) -
						     green;
					comparisonColor >>= 5;
					redDelta =
						(comparisonColor & 0x1F) - red;
					colorDistance = blueDelta * blueDelta;
					colorDistance +=
						greenDelta * greenDelta;
					colorDistance += redDelta * redDelta;
					if (colorDistance > 16) {
						*entry = 0;
						++clearedCount;
						if (firstClearedIndex == -1) {
							firstClearedIndex =
								paletteIndex;
						}
						break;
					}
					comparisonEntry += 256;
					++planeIndex;
					if (planeIndex >= 7) {
						break;
					}
				}
				if (planeIndex == 7) {
					*entry = entry[2560];
				}
			}
			++entry;
			++paletteIndex;
		} while (paletteIndex < 256);

		if (clearedCount < 256) {
			DebugPrintf("%x:AlphaTex!(%d)\n", palette,
				    clearedCount);
		} else {
			DebugPrintf("%x:No Alpha\n", palette);
			clearedCount = 0;
		}
		palette[256] = (uint16_t)firstClearedIndex;
		palette[2304] = (uint16_t)clearedCount;
	}
}

/* Turns width by height pixels of 24-bit color at rgb24 into a paletted texture
 * at dst. Each pixel, its bytes taken in reverse order and cut to 5 bits,
 * becomes the index of an equal color in a palette built as it goes, the first
 * pixel's color at 0; once 256 colors are taken, a new color gets the nearest
 * one. After the texels it writes 16 shades of the 256 colors, shade by shade:
 * 4096 bytes of the nearest g_swPalette index from 0x40 to 0xFF, then 4096
 * RGB565 colors with the green's low bit 0. Shades 0 to 7 turn a component c
 * into ((c << 7) + (((c * shade) & 0xFFFFF8) << 4)) >> 8, shades 8 to 15 into
 * ((c << 8) + ((((31 - c) * (shade - 8)) & 0xFFFFF8) << 5)) >> 8. Palette
 * entries past the colors taken come from uninitialized stack bytes. */
// FUNCTION: XVT 0x4720D0
void ModelTexture_BuildPalettedShadeTable(uint8_t *dst, const uint8_t *rgb24,
					  int width, int height)
{
	uint8_t targetRgb[3];
	uint8_t localPalette[256 * 3];
	unsigned int paletteSize;
	const uint8_t *sourcePixel;
	int pixelCount;
	uint8_t *dstTexel;

	sourcePixel = rgb24;
	localPalette[0] = sourcePixel[2] >> 3;
	paletteSize = 1;
	localPalette[1] = sourcePixel[1] >> 3;
	pixelCount = width * height;
	localPalette[2] = sourcePixel[0] >> 3;
	sourcePixel += 3;
	dstTexel = dst + 1;
	dst[0] = 0;
	if ((unsigned int)pixelCount > 1) {
		int remainingPixels;

		remainingPixels = pixelCount - 1;
		do {
			uint8_t paletteIndex;

			targetRgb[0] = sourcePixel[2] >> 3;
			targetRgb[1] = sourcePixel[1] >> 3;
			targetRgb[2] = sourcePixel[0] >> 3;
			paletteIndex =
				(uint8_t)Color_FindNearestRgbTripletIndex(
					targetRgb, localPalette, 0,
					paletteSize);
			if ((localPalette[3 * paletteIndex] != targetRgb[0] ||
			     localPalette[3 * paletteIndex + 1] !=
				     targetRgb[1] ||
			     localPalette[3 * paletteIndex + 2] !=
				     targetRgb[2]) &&
			    paletteSize < 256) {
				paletteIndex = (uint8_t)paletteSize++;
				localPalette[3 * paletteIndex] = targetRgb[0];
				localPalette[3 * paletteIndex + 1] =
					targetRgb[1];
				localPalette[3 * paletteIndex + 2] =
					targetRgb[2];
			}
			*dstTexel++ = paletteIndex;
			sourcePixel += 3;
			--remainingPixels;
		} while (remainingPixels != 0);
	}

	{
		const uint8_t *paletteEntry;
		uint16_t *packedShadeEntry;
		uint8_t *indexedShadeEntry;

		paletteEntry = localPalette;
		indexedShadeEntry = &dst[pixelCount];
		packedShadeEntry = (uint16_t *)&dst[pixelCount + 4096];
		do {
			unsigned int shade;
			uint16_t *packedEntry;
			uint8_t *indexedEntry;

			shade = 0;
			packedEntry = packedShadeEntry;
			indexedEntry = indexedShadeEntry;
			do {
				int paletteComponent;
				int scaledComponent;

				if (shade < 8) {
					scaledComponent = shade;
					paletteComponent = paletteEntry[0];
					scaledComponent *= paletteComponent;
					scaledComponent =
						(paletteComponent << 7) +
						((scaledComponent & 0xFFFFF8)
						 << 4);
					paletteComponent = paletteEntry[1];
					targetRgb[0] =
						(uint8_t)(scaledComponent >> 8);
					scaledComponent =
						paletteComponent * shade;
					scaledComponent =
						(paletteComponent << 7) +
						((scaledComponent & 0xFFFFF8)
						 << 4);
					paletteComponent = paletteEntry[2];
					targetRgb[1] =
						(uint8_t)(scaledComponent >> 8);
					scaledComponent =
						paletteComponent * shade;
					scaledComponent =
						(paletteComponent << 7) +
						((scaledComponent & 0xFFFFF8)
						 << 4);
				} else {
					unsigned int lightShade;

					paletteComponent = paletteEntry[0];
					lightShade = shade - 8;
					scaledComponent =
						lightShade *
						(31 - paletteComponent);
					scaledComponent =
						(paletteComponent << 8) +
						((scaledComponent & 0xFFFFF8)
						 << 5);
					paletteComponent = paletteEntry[1];
					targetRgb[0] =
						(uint8_t)(scaledComponent >> 8);
					scaledComponent =
						lightShade *
						(31 - paletteComponent);
					scaledComponent =
						(paletteComponent << 8) +
						((scaledComponent & 0xFFFFF8)
						 << 5);
					paletteComponent = paletteEntry[2];
					targetRgb[1] =
						(uint8_t)(scaledComponent >> 8);
					scaledComponent =
						lightShade *
						(31 - paletteComponent);
					scaledComponent =
						(paletteComponent << 8) +
						((scaledComponent & 0xFFFFF8)
						 << 5);
				}
				targetRgb[2] = (uint8_t)(scaledComponent >> 8);
				*packedEntry = (uint16_t)(targetRgb[2] +
							  ((targetRgb[1] +
							    32 * targetRgb[0])
							   << 6));
				targetRgb[0] *= 2;
				targetRgb[1] *= 2;
				targetRgb[2] *= 2;
				*indexedEntry = (uint8_t)
					Color_FindNearestRgbTripletIndex(
						targetRgb,
						(const uint8_t *)g_swPalette,
						0x40, 0x100);
				indexedEntry += 256;
				packedEntry += 256;
				++shade;
			} while (shade < 16);
			++packedShadeEntry;
			++indexedShadeEntry;
			paletteEntry += 3;
		} while (paletteEntry < localPalette + sizeof(localPalette));
	}
}

#ifndef XVT_MODERN
/* Loads a texture file into dst as a packed OPT texture and returns its bytes:
 * a 24-byte OptTextureData header with palette 256 and inlinePaletteCount 16,
 * the texels, 4096 bytes of g_swPalette shade indices and 8192 bytes of RGB565
 * shades. A name ending in "rgb", ignoring case, is tried first as the same
 * name ending in "tex". A .tex file gives its own header and texels (dataSize
 * bytes when textureSize equals width times height, else width times height),
 * then the RGB565 shades, leaving the 4096 bytes before them unwritten. An .rgb
 * file has a 512-byte header with a big-endian width and height at bytes 6 and
 * 8, then three planes of one byte per pixel; it is turned into texels and
 * shades in place much as ModelTexture_BuildPalettedShadeTable does, without
 * its 0xFFFFF8 masks, and textureSize and dataSize keep the header's bytes 8 to
 * 15. Either file returns its texel bytes plus 12312. A missing file or another
 * extension gives the 8 by 8 texture of g_defaultWhiteTextureRgb24 and returns
 * 12376. Only the original build calls this. */
// FUNCTION: XVT 0x479E80
size_t ModelTexture_LoadRgbOrTexFile(uint8_t *dst, const char *fileName)
{
	uint8_t targetRgb[3];
	char path[256];
	uint8_t localPalette[256 * 3];
	char *extension;
	XvtFile *stream;
	uint8_t *texels;
	int pixelCount;

	strcpy(path, fileName);
	extension = path + strlen(path) - 3;
	if (_strcmpi(extension, g_extRgb) == 0) {
		extension[0] = 't';
		extension[1] = 'e';
		extension[2] = 'x';
		FeDiskIo_OpenGlobalStream(path, g_fileModeReadBinary, 0, 0);
		stream = (XvtFile *)g_stream;
		extension[0] = 'r';
		extension[1] = 'g';
		extension[2] = 'b';
		if (stream == NULL) {
			FeDiskIo_OpenGlobalStream(path, g_fileModeReadBinary, 0,
						  0);
			stream = (XvtFile *)g_stream;
			if (stream == NULL) {
				uint8_t *whiteTexels = dst + 24;

				((unsigned int *)dst)[4] = 8;
				((unsigned int *)dst)[5] = 8;
				((unsigned int *)dst)[0] = 256;
				((unsigned int *)dst)[1] = 16;
				ModelTexture_BuildPalettedShadeTable(
					whiteTexels, g_defaultWhiteTextureRgb24,
					8, 8);
				return 12376;
			}
			{
				const uint8_t *paletteEntry;
				uint16_t *packedShadeEntry;
				uint8_t *indexedShadeEntry;
				uint8_t *serializedEnd;
				uint8_t *dstTexel;
				uint8_t *sourcePixel;
				int remainingPixels;
				int paletteSize;
				int height;
				int width;

				File_RawRead(dst, 512, 1, stream);
				width = ((unsigned int)dst[6] << 8) + dst[7];
				height = ((unsigned int)dst[8] << 8) + dst[9];
				pixelCount = width * height;
				sourcePixel = dst + 24;
				((unsigned int *)dst)[4] = width;
				((unsigned int *)dst)[5] = height;
				File_RawRead(sourcePixel, pixelCount, 3,
					     stream);
				File_RawClose(stream);

				/* The three colour planes are converted to palette indices in place. */
				paletteSize = 1;
				localPalette[0] = sourcePixel[0] >> 3;
				localPalette[1] = sourcePixel[pixelCount] >> 3;
				localPalette[2] =
					sourcePixel[pixelCount * 2] >> 3;
				serializedEnd = sourcePixel + pixelCount;
				dst[24] = 0;
				++sourcePixel;
				dstTexel = sourcePixel;
				if (pixelCount > 1) {
					remainingPixels = pixelCount - 1;
					do {
						uint8_t paletteIndex;

						targetRgb[0] =
							sourcePixel[0] >> 3;
						targetRgb[1] =
							sourcePixel
								[pixelCount] >>
							3;
						targetRgb[2] =
							sourcePixel[pixelCount *
								    2] >>
							3;
						paletteIndex = (uint8_t)
							Color_FindNearestRgbTripletIndex(
								targetRgb,
								localPalette, 0,
								paletteSize);
						if ((localPalette[3 *
								  paletteIndex] !=
							     targetRgb[0] ||
						     localPalette[3 * paletteIndex +
								  1] !=
							     targetRgb[1] ||
						     localPalette[3 * paletteIndex +
								  2] !=
							     targetRgb[2]) &&
						    paletteSize < 256) {
							paletteIndex = (uint8_t)
								paletteSize++;
							localPalette[3 *
								     paletteIndex] =
								targetRgb[0];
							localPalette
								[3 * paletteIndex +
								 1] = targetRgb
									[1];
							localPalette
								[3 * paletteIndex +
								 2] = targetRgb
									[2];
						}
						*dstTexel++ = paletteIndex;
						++sourcePixel;
						--remainingPixels;
					} while (remainingPixels != 0);
				}

				indexedShadeEntry = serializedEnd;
				serializedEnd += 4096;
				packedShadeEntry = (uint16_t *)serializedEnd;
				serializedEnd += 8192;
				((unsigned int *)dst)[0] = 256;
				((unsigned int *)dst)[1] = 16;
				paletteEntry = localPalette;
				do {
					int shade;
					uint16_t *packedEntry;
					uint8_t *indexedEntry;

					shade = 0;
					indexedEntry = indexedShadeEntry;
					packedEntry = packedShadeEntry;
					do {
						int paletteComponent;
						int scaledComponent;

						if (shade < 8) {
							paletteComponent =
								paletteEntry[0];
							scaledComponent =
								paletteComponent *
								shade;
							scaledComponent =
								(paletteComponent
								 << 7) +
								(scaledComponent
								 << 8) / 16;
							paletteComponent =
								paletteEntry[1];
							targetRgb[0] =
								(uint8_t)(scaledComponent >>
									  8);
							scaledComponent =
								paletteComponent *
								shade;
							scaledComponent =
								(paletteComponent
								 << 7) +
								(scaledComponent
								 << 8) / 16;
							paletteComponent =
								paletteEntry[2];
							targetRgb[1] =
								(uint8_t)(scaledComponent >>
									  8);
							scaledComponent =
								paletteComponent *
								shade;
							scaledComponent =
								(paletteComponent
								 << 7) +
								(scaledComponent
								 << 8) / 16;
						} else {
							paletteComponent =
								paletteEntry[0];
							scaledComponent =
								(31 -
								 paletteComponent) *
								(shade - 8);
							scaledComponent =
								(paletteComponent
								 << 8) +
								(scaledComponent
								 << 8) / 8;
							paletteComponent =
								paletteEntry[1];
							targetRgb[0] =
								(uint8_t)(scaledComponent >>
									  8);
							scaledComponent =
								(31 -
								 paletteComponent) *
								(shade - 8);
							scaledComponent =
								(paletteComponent
								 << 8) +
								(scaledComponent
								 << 8) / 8;
							paletteComponent =
								paletteEntry[2];
							targetRgb[1] =
								(uint8_t)(scaledComponent >>
									  8);
							scaledComponent =
								(31 -
								 paletteComponent) *
								(shade - 8);
							scaledComponent =
								(paletteComponent
								 << 8) +
								(scaledComponent
								 << 8) / 8;
						}
						targetRgb[2] =
							(uint8_t)(scaledComponent >>
								  8);
						*packedEntry =
							(uint16_t)(((32 * targetRgb[0] +
								     targetRgb
									     [1])
								    << 6) +
								   targetRgb
									   [2]);
						targetRgb[0] *= 2;
						targetRgb[1] *= 2;
						targetRgb[2] *= 2;
						*indexedEntry = (uint8_t)
							Color_FindNearestRgbTripletIndex(
								targetRgb,
								(const uint8_t
									 *)
									g_swPalette,
								0x40, 0x100);
						indexedEntry += 256;
						packedEntry += 256;
						++shade;
					} while (shade < 16);
					++packedShadeEntry;
					++indexedShadeEntry;
					paletteEntry += 3;
				} while (paletteEntry <
					 localPalette + sizeof(localPalette));

				return serializedEnd - dst;
			}
		}
	} else {
		if (_strcmpi(extension, g_extTex) != 0) {
			uint8_t *whiteTexels = dst + 24;

			((unsigned int *)dst)[4] = 8;
			((unsigned int *)dst)[5] = 8;
			((unsigned int *)dst)[0] = 256;
			((unsigned int *)dst)[1] = 16;
			ModelTexture_BuildPalettedShadeTable(
				whiteTexels, g_defaultWhiteTextureRgb24, 8, 8);
			return 12376;
		}
		FeDiskIo_OpenGlobalStream(path, g_fileModeReadBinary, 0, 0);
		stream = (XvtFile *)g_stream;
		if (stream == NULL) {
			uint8_t *whiteTexels = dst + 24;

			((unsigned int *)dst)[4] = 8;
			((unsigned int *)dst)[5] = 8;
			((unsigned int *)dst)[0] = 256;
			((unsigned int *)dst)[1] = 16;
			ModelTexture_BuildPalettedShadeTable(
				whiteTexels, g_defaultWhiteTextureRgb24, 8, 8);
			return 12376;
		}
	}

	texels = dst + 24;
	File_RawRead(dst, 24, 1, stream);
	pixelCount = ((unsigned int *)dst)[4] * ((unsigned int *)dst)[5];
	if (((unsigned int *)dst)[2] == (unsigned int)pixelCount) {
		pixelCount = ((unsigned int *)dst)[3];
	}
	((unsigned int *)dst)[0] = 256;
	((unsigned int *)dst)[1] = 16;
	File_RawRead(texels, pixelCount, 1, stream);
	texels += pixelCount + 4096;
	File_RawRead(texels, 8192, 1, stream);
	File_RawClose(stream);
	return pixelCount + 12312;
}
#endif
