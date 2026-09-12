#include "xvt/render/tex_level.h"

#include "xvt/flight/flight.h"
#include "xvt/render/color.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/image_quantizer.h"

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40E6C0
unsigned int TexLevel_Convert24BppPalettesTo16Bpp(unsigned int* texLevel) {
	TexLevelHeader* header;
	uint16_t* outputPalette16;
	uint8_t* sourcePaletteRgba;
	unsigned int paletteCount;
	RgbTriplet* rgbCursor;
	unsigned int entriesRemaining;
	unsigned int paletteIndex;
	unsigned int imageIndex;
	unsigned int result;
	TexLevelImageHeader* image;
	RgbTriplet srcRgb[1024];

	header = (TexLevelHeader*)texLevel;
	outputPalette16 = (uint16_t*)((uint8_t*)header + header->dataSize);
	if (header->bitsPerPixel == 24) {
		sourcePaletteRgba = (uint8_t*)header + header->paletteOffset;
		header->palette8Offset = (uint32_t)((uint8_t*)outputPalette16 - (uint8_t*)header);
		paletteCount = header->paletteColorCount;
		if (paletteCount < 1024) {
			if (paletteCount != 0) {
				rgbCursor = srcRgb;
				entriesRemaining = paletteCount;
				do {
					rgbCursor->r = *sourcePaletteRgba++ >> 2;
					rgbCursor->g = *sourcePaletteRgba++ >> 2;
					rgbCursor->b = *sourcePaletteRgba++ >> 2;
					++sourcePaletteRgba;
					++rgbCursor;
				} while (--entriesRemaining != 0);
			}
			FlightPalette_Build16BppRange(srcRgb, outputPalette16, 0, paletteCount);
			outputPalette16 += header->paletteColorCount;
		}
	}

	imageIndex = 0;
	result = header->imageCount;
	if (result != 0) {
		do {
			image = (TexLevelImageHeader*)((uint8_t*)header +
										   *(uint32_t*)((uint8_t*)header + header->imageOffsetTableOffset +
														imageIndex * sizeof(uint32_t)));
			image->palette8Offset = (uint32_t)((uint8_t*)outputPalette16 - (uint8_t*)image);
			if (image->bitsPerPixel == 24) {
				paletteCount = image->paletteColorCount;
				sourcePaletteRgba = (uint8_t*)image + image->paletteOffset;
				if (paletteCount < 1024) {
					paletteIndex = 0;
					if (paletteCount != 0) {
						rgbCursor = srcRgb;
						do {
							rgbCursor->r = *sourcePaletteRgba++ >> 2;
							rgbCursor->g = *sourcePaletteRgba++ >> 2;
							rgbCursor->b = *sourcePaletteRgba++ >> 2;
							++sourcePaletteRgba;
							++rgbCursor;
							++paletteIndex;
						} while (image->paletteColorCount > paletteIndex);
					}
					FlightPalette_Build16BppRange(srcRgb, outputPalette16, 0, image->paletteColorCount);
					outputPalette16 += image->paletteColorCount;
				}
			}
			result = imageIndex + 1;
			imageIndex = result;
		} while (header->imageCount > result);
	}
	return result;
}

// FUNCTION: XVT 0x40E7F0
unsigned int TexLevel_Convert24BppPalettesTo8Bpp(unsigned int* texLevel) {
	TexLevelHeader* header;
	uint8_t* outputPalette8;
	const uint8_t* sourcePaletteRgba;
	unsigned int paletteIndex;
	unsigned int imageIndex;
	TexLevelImageHeader* image;
	RgbTriplet targetRgb;

	header = (TexLevelHeader*)texLevel;
	outputPalette8 = (uint8_t*)header + header->dataSize;
	if (header->bitsPerPixel == 24) {
		sourcePaletteRgba = (const uint8_t*)header + header->paletteOffset;
		header->palette8Offset = (uint32_t)(outputPalette8 - (uint8_t*)header);
		paletteIndex = 0;
		while (paletteIndex < header->paletteColorCount) {
			targetRgb.r = sourcePaletteRgba[0] >> 2;
			targetRgb.g = sourcePaletteRgba[1] >> 2;
			targetRgb.b = sourcePaletteRgba[2] >> 2;
			*outputPalette8 = (uint8_t)Color_FindNearestRgbTripletIndex(
				(const uint8_t*)&targetRgb, (const uint8_t*)g_swPalette, 0x40, 0x100);
			sourcePaletteRgba += 4;
			++outputPalette8;
			++paletteIndex;
		}
	}

	imageIndex = 0;
	while (imageIndex < header->imageCount) {
		image = (TexLevelImageHeader*)((uint8_t*)header + *(uint32_t*)((uint8_t*)&texLevel[imageIndex] +
																	   header->imageOffsetTableOffset));
		image->palette8Offset = (uint32_t)(outputPalette8 - (uint8_t*)image);
		if (image->bitsPerPixel == 24) {
			sourcePaletteRgba = (const uint8_t*)image + image->paletteOffset;
			if (g_generateMissionPalette != 0) {
				ImageQuantizer_ClassifyEncodedTexLevelImage((const uint8_t*)image + image->encodedImageOffset,
															sourcePaletteRgba, image->width, image->height,
															image->packingMode);
			}
			paletteIndex = 0;
			while (paletteIndex < image->paletteColorCount) {
				targetRgb.r = sourcePaletteRgba[0] >> 2;
				targetRgb.g = sourcePaletteRgba[1] >> 2;
				targetRgb.b = sourcePaletteRgba[2] >> 2;
				*outputPalette8 = (uint8_t)Color_FindNearestRgbTripletIndex(
					(const uint8_t*)&targetRgb, (const uint8_t*)g_swPalette, 0x40, 0x100);
				sourcePaletteRgba += 4;
				++outputPalette8;
				++paletteIndex;
			}
		}
		++imageIndex;
	}
	return imageIndex;
}
