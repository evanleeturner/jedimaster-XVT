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

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40DD40
int ModelTexture_IsHardwareFormat555(void) { return g_pFmtRGB565->colorInfo.greenBPP == 5; }

// FUNCTION: XVT 0x40DD50
void ModelTexture_FilterHardwarePalette(uint16_t* palette) {
	uint16_t* entry;
	int red;
	int planeIndex;
	uint16_t* comparisonEntry;
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

	if (g_keepFullResTextures != 2) {
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
				if (firstClearedIndex == -1)
					firstClearedIndex = paletteIndex;
			} else {
				planeIndex = 1;
				comparisonEntry = entry + 256;
				for (;;) {
					comparisonColor = *comparisonEntry;
					blueDelta = (comparisonColor & 0x1F) - blue;
					comparisonColor >>= 6;
					greenDelta = (comparisonColor & 0x1F) - green;
					comparisonColor >>= 5;
					redDelta = (comparisonColor & 0x1F) - red;
					colorDistance = blueDelta * blueDelta;
					colorDistance += greenDelta * greenDelta;
					colorDistance += redDelta * redDelta;
					if (colorDistance > 16) {
						*entry = 0;
						++clearedCount;
						if (firstClearedIndex == -1)
							firstClearedIndex = paletteIndex;
						break;
					}
					comparisonEntry += 256;
					++planeIndex;
					if (planeIndex >= 7)
						break;
				}
				if (planeIndex == 7)
					*entry = entry[2560];
			}
			++entry;
			++paletteIndex;
		} while (paletteIndex < 256);

		if (clearedCount < 256) {
			DebugPrintf("%x:AlphaTex!(%d)\n", palette, clearedCount);
		} else {
			DebugPrintf("%x:No Alpha\n", palette);
			clearedCount = 0;
		}
		palette[256] = (uint16_t)firstClearedIndex;
		palette[2304] = (uint16_t)clearedCount;
	}
}

// FUNCTION: XVT 0x4720D0
void ModelTexture_BuildPalettedShadeTable(uint8_t* dst, const uint8_t* rgb24, int width, int height) {
	uint8_t targetRgb[3];
	uint8_t localPalette[256 * 3];
	unsigned int paletteSize;
	const uint8_t* sourcePixel;
	int pixelCount;
	uint8_t* dstTexel;

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
			paletteIndex = (uint8_t)Color_FindNearestRgbTripletIndex(targetRgb, localPalette, 0, paletteSize);
			if ((localPalette[3 * paletteIndex] != targetRgb[0] ||
				 localPalette[3 * paletteIndex + 1] != targetRgb[1] ||
				 localPalette[3 * paletteIndex + 2] != targetRgb[2]) &&
				paletteSize < 256) {
				paletteIndex = (uint8_t)paletteSize++;
				localPalette[3 * paletteIndex] = targetRgb[0];
				localPalette[3 * paletteIndex + 1] = targetRgb[1];
				localPalette[3 * paletteIndex + 2] = targetRgb[2];
			}
			*dstTexel++ = paletteIndex;
			sourcePixel += 3;
			--remainingPixels;
		} while (remainingPixels != 0);
	}

	{
		const uint8_t* paletteEntry;
		uint16_t* packedShadeEntry;
		uint8_t* indexedShadeEntry;

		paletteEntry = localPalette;
		indexedShadeEntry = &dst[pixelCount];
		packedShadeEntry = (uint16_t*)&dst[pixelCount + 4096];
		do {
			unsigned int shade;
			uint16_t* packedEntry;
			uint8_t* indexedEntry;

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
					scaledComponent = (paletteComponent << 7) + ((scaledComponent & 0xFFFFF8) << 4);
					paletteComponent = paletteEntry[1];
					targetRgb[0] = (uint8_t)(scaledComponent >> 8);
					scaledComponent = paletteComponent * shade;
					scaledComponent = (paletteComponent << 7) + ((scaledComponent & 0xFFFFF8) << 4);
					paletteComponent = paletteEntry[2];
					targetRgb[1] = (uint8_t)(scaledComponent >> 8);
					scaledComponent = paletteComponent * shade;
					scaledComponent = (paletteComponent << 7) + ((scaledComponent & 0xFFFFF8) << 4);
				} else {
					unsigned int lightShade;

					paletteComponent = paletteEntry[0];
					lightShade = shade - 8;
					scaledComponent = lightShade * (31 - paletteComponent);
					scaledComponent = (paletteComponent << 8) + ((scaledComponent & 0xFFFFF8) << 5);
					paletteComponent = paletteEntry[1];
					targetRgb[0] = (uint8_t)(scaledComponent >> 8);
					scaledComponent = lightShade * (31 - paletteComponent);
					scaledComponent = (paletteComponent << 8) + ((scaledComponent & 0xFFFFF8) << 5);
					paletteComponent = paletteEntry[2];
					targetRgb[1] = (uint8_t)(scaledComponent >> 8);
					scaledComponent = lightShade * (31 - paletteComponent);
					scaledComponent = (paletteComponent << 8) + ((scaledComponent & 0xFFFFF8) << 5);
				}
				targetRgb[2] = (uint8_t)(scaledComponent >> 8);
				*packedEntry = (uint16_t)(targetRgb[2] + ((targetRgb[1] + 32 * targetRgb[0]) << 6));
				targetRgb[0] *= 2;
				targetRgb[1] *= 2;
				targetRgb[2] *= 2;
				*indexedEntry = (uint8_t)Color_FindNearestRgbTripletIndex(
					targetRgb, (const uint8_t*)g_swPalette, 0x40, 0x100);
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
// FUNCTION: XVT 0x479E80
size_t ModelTexture_LoadRgbOrTexFile(uint8_t* dst, const char* fileName) {
	uint8_t targetRgb[3];
	char path[256];
	uint8_t localPalette[256 * 3];
	char* extension;
	XvtFile* stream;
	uint8_t* texels;
	int pixelCount;

	strcpy(path, fileName);
	extension = path + strlen(path) - 3;
	if (_strcmpi(extension, g_extRgb) == 0) {
		extension[0] = 't';
		extension[1] = 'e';
		extension[2] = 'x';
		File_OpenGlobalStream(path, g_fileModeReadBinary, 0, 0);
		stream = (XvtFile*)g_stream;
		extension[0] = 'r';
		extension[1] = 'g';
		extension[2] = 'b';
		if (stream == NULL) {
			File_OpenGlobalStream(path, g_fileModeReadBinary, 0, 0);
			stream = (XvtFile*)g_stream;
			if (stream == NULL) {
				uint8_t* whiteTexels = dst + 24;

				((unsigned int*)dst)[4] = 8;
				((unsigned int*)dst)[5] = 8;
				((unsigned int*)dst)[0] = 256;
				((unsigned int*)dst)[1] = 16;
				ModelTexture_BuildPalettedShadeTable(whiteTexels, g_defaultWhiteTextureRgb24, 8, 8);
				return 12376;
			}
			{
				const uint8_t* paletteEntry;
				uint16_t* packedShadeEntry;
				uint8_t* indexedShadeEntry;
				uint8_t* serializedEnd;
				uint8_t* dstTexel;
				uint8_t* sourcePixel;
				int remainingPixels;
				int paletteSize;
				int height;
				int width;

				File_RawRead(dst, 512, 1, stream);
				width = ((unsigned int)dst[6] << 8) + dst[7];
				height = ((unsigned int)dst[8] << 8) + dst[9];
				pixelCount = width * height;
				sourcePixel = dst + 24;
				((unsigned int*)dst)[4] = width;
				((unsigned int*)dst)[5] = height;
				File_RawRead(sourcePixel, pixelCount, 3, stream);
				File_RawClose(stream);

				/* The three colour planes are converted to palette indices in place. */
				paletteSize = 1;
				localPalette[0] = sourcePixel[0] >> 3;
				localPalette[1] = sourcePixel[pixelCount] >> 3;
				localPalette[2] = sourcePixel[pixelCount * 2] >> 3;
				serializedEnd = sourcePixel + pixelCount;
				dst[24] = 0;
				++sourcePixel;
				dstTexel = sourcePixel;
				if (pixelCount > 1) {
					remainingPixels = pixelCount - 1;
					do {
						uint8_t paletteIndex;

						targetRgb[0] = sourcePixel[0] >> 3;
						targetRgb[1] = sourcePixel[pixelCount] >> 3;
						targetRgb[2] = sourcePixel[pixelCount * 2] >> 3;
						paletteIndex = (uint8_t)Color_FindNearestRgbTripletIndex(targetRgb, localPalette, 0,
																				 paletteSize);
						if ((localPalette[3 * paletteIndex] != targetRgb[0] ||
							 localPalette[3 * paletteIndex + 1] != targetRgb[1] ||
							 localPalette[3 * paletteIndex + 2] != targetRgb[2]) &&
							paletteSize < 256) {
							paletteIndex = (uint8_t)paletteSize++;
							localPalette[3 * paletteIndex] = targetRgb[0];
							localPalette[3 * paletteIndex + 1] = targetRgb[1];
							localPalette[3 * paletteIndex + 2] = targetRgb[2];
						}
						*dstTexel++ = paletteIndex;
						++sourcePixel;
						--remainingPixels;
					} while (remainingPixels != 0);
				}

				indexedShadeEntry = serializedEnd;
				serializedEnd += 4096;
				packedShadeEntry = (uint16_t*)serializedEnd;
				serializedEnd += 8192;
				((unsigned int*)dst)[0] = 256;
				((unsigned int*)dst)[1] = 16;
				paletteEntry = localPalette;
				do {
					int shade;
					uint16_t* packedEntry;
					uint8_t* indexedEntry;

					shade = 0;
					indexedEntry = indexedShadeEntry;
					packedEntry = packedShadeEntry;
					do {
						int paletteComponent;
						int scaledComponent;

						if (shade < 8) {
							paletteComponent = paletteEntry[0];
							scaledComponent = paletteComponent * shade;
							scaledComponent = (paletteComponent << 7) + (scaledComponent << 8) / 16;
							paletteComponent = paletteEntry[1];
							targetRgb[0] = (uint8_t)(scaledComponent >> 8);
							scaledComponent = paletteComponent * shade;
							scaledComponent = (paletteComponent << 7) + (scaledComponent << 8) / 16;
							paletteComponent = paletteEntry[2];
							targetRgb[1] = (uint8_t)(scaledComponent >> 8);
							scaledComponent = paletteComponent * shade;
							scaledComponent = (paletteComponent << 7) + (scaledComponent << 8) / 16;
						} else {
							paletteComponent = paletteEntry[0];
							scaledComponent = (31 - paletteComponent) * (shade - 8);
							scaledComponent = (paletteComponent << 8) + (scaledComponent << 8) / 8;
							paletteComponent = paletteEntry[1];
							targetRgb[0] = (uint8_t)(scaledComponent >> 8);
							scaledComponent = (31 - paletteComponent) * (shade - 8);
							scaledComponent = (paletteComponent << 8) + (scaledComponent << 8) / 8;
							paletteComponent = paletteEntry[2];
							targetRgb[1] = (uint8_t)(scaledComponent >> 8);
							scaledComponent = (31 - paletteComponent) * (shade - 8);
							scaledComponent = (paletteComponent << 8) + (scaledComponent << 8) / 8;
						}
						targetRgb[2] = (uint8_t)(scaledComponent >> 8);
						*packedEntry = (uint16_t)(((32 * targetRgb[0] + targetRgb[1]) << 6) + targetRgb[2]);
						targetRgb[0] *= 2;
						targetRgb[1] *= 2;
						targetRgb[2] *= 2;
						*indexedEntry = (uint8_t)Color_FindNearestRgbTripletIndex(
							targetRgb, (const uint8_t*)g_swPalette, 0x40, 0x100);
						indexedEntry += 256;
						packedEntry += 256;
						++shade;
					} while (shade < 16);
					++packedShadeEntry;
					++indexedShadeEntry;
					paletteEntry += 3;
				} while (paletteEntry < localPalette + sizeof(localPalette));

				return serializedEnd - dst;
			}
		}
	} else {
		if (_strcmpi(extension, g_extTex) != 0) {
			uint8_t* whiteTexels = dst + 24;

			((unsigned int*)dst)[4] = 8;
			((unsigned int*)dst)[5] = 8;
			((unsigned int*)dst)[0] = 256;
			((unsigned int*)dst)[1] = 16;
			ModelTexture_BuildPalettedShadeTable(whiteTexels, g_defaultWhiteTextureRgb24, 8, 8);
			return 12376;
		}
		File_OpenGlobalStream(path, g_fileModeReadBinary, 0, 0);
		stream = (XvtFile*)g_stream;
		if (stream == NULL) {
			uint8_t* whiteTexels = dst + 24;

			((unsigned int*)dst)[4] = 8;
			((unsigned int*)dst)[5] = 8;
			((unsigned int*)dst)[0] = 256;
			((unsigned int*)dst)[1] = 16;
			ModelTexture_BuildPalettedShadeTable(whiteTexels, g_defaultWhiteTextureRgb24, 8, 8);
			return 12376;
		}
	}

	texels = dst + 24;
	File_RawRead(dst, 24, 1, stream);
	pixelCount = ((unsigned int*)dst)[4] * ((unsigned int*)dst)[5];
	if (((unsigned int*)dst)[2] == (unsigned int)pixelCount)
		pixelCount = ((unsigned int*)dst)[3];
	((unsigned int*)dst)[0] = 256;
	((unsigned int*)dst)[1] = 16;
	File_RawRead(texels, pixelCount, 1, stream);
	texels += pixelCount + 4096;
	File_RawRead(texels, 8192, 1, stream);
	File_RawClose(stream);
	return pixelCount + 12312;
}
#endif
