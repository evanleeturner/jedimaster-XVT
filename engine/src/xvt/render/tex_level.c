#include "xvt/render/tex_level.h"

#include "xvt/flight/flight.h"
#include "xvt/render/color.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/image_quantizer.h"

/* Builds the 16-bit palettes of a loaded texture block (a TexLevelHeader, its
 * images after it) in the space after its data, at header->dataSize, with
 * FlightPalette_Build16BppRange. Each 24-bit palette, 4 bytes per color of
 * which the first three are red, green and blue, each >> 2 to the 0 to 63
 * scale, is converted when it holds under 1024 colors. When the block's own
 * palette is 24-bit it sets header->convertedPaletteOffset to the output first.
 * For every image, found through the offset table, it sets
 * convertedPaletteOffset, counted from the image header, to the next output
 * position and converts a 24-bit image's palette there. Returns
 * header->imageCount. FeDiskIo_LoadResources calls it when
 * g_flightBytesPerPixel is 2. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40E6C0
unsigned int TexLevel_Convert24BppPalettesTo16Bpp(unsigned int *texLevel)
{
	struct TexLevelHeader *header;
	uint16_t *outputPalette16;
	uint8_t *sourcePaletteRgba;
	unsigned int paletteColorCount;
	struct RgbTriplet *rgbCursor;
	unsigned int entriesRemaining;
	unsigned int paletteIndex;
	unsigned int imageIndex;
	unsigned int result;
	struct TexLevelImageHeader *image;
	struct RgbTriplet srcRgb[1024];

	header = (struct TexLevelHeader *)texLevel;
	outputPalette16 = (uint16_t *)((uint8_t *)header + header->dataSize);
	if (header->bitsPerPixel == 24) {
		sourcePaletteRgba = (uint8_t *)header + header->paletteOffset;
		header->convertedPaletteOffset =
			(uint32_t)((uint8_t *)outputPalette16 -
				   (uint8_t *)header);
		paletteColorCount = header->paletteColorCount;
		if (paletteColorCount < 1024) {
			if (paletteColorCount != 0) {
				rgbCursor = srcRgb;
				entriesRemaining = paletteColorCount;
				do {
					rgbCursor->r =
						*sourcePaletteRgba++ >> 2;
					rgbCursor->g =
						*sourcePaletteRgba++ >> 2;
					rgbCursor->b =
						*sourcePaletteRgba++ >> 2;
					++sourcePaletteRgba;
					++rgbCursor;
				} while (--entriesRemaining != 0);
			}
			FlightPalette_Build16BppRange(srcRgb, outputPalette16,
						      0, paletteColorCount);
			outputPalette16 += header->paletteColorCount;
		}
	}

	imageIndex = 0;
	result = header->imageCount;
	if (result != 0) {
		do {
			image = (struct TexLevelImageHeader
					 *)((uint8_t *)header +
					    *(uint32_t
						      *)((uint8_t *)header +
							 header->imageOffsetTableOffset +
							 imageIndex *
								 sizeof(uint32_t)));
			image->convertedPaletteOffset =
				(uint32_t)((uint8_t *)outputPalette16 -
					   (uint8_t *)image);
			if (image->bitsPerPixel == 24) {
				paletteColorCount = image->paletteColorCount;
				sourcePaletteRgba =
					(uint8_t *)image + image->paletteOffset;
				if (paletteColorCount < 1024) {
					paletteIndex = 0;
					if (paletteColorCount != 0) {
						rgbCursor = srcRgb;
						do {
							rgbCursor->r =
								*sourcePaletteRgba++ >>
								2;
							rgbCursor->g =
								*sourcePaletteRgba++ >>
								2;
							rgbCursor->b =
								*sourcePaletteRgba++ >>
								2;
							++sourcePaletteRgba;
							++rgbCursor;
							++paletteIndex;
						} while (
							image->paletteColorCount >
							paletteIndex);
					}
					FlightPalette_Build16BppRange(
						srcRgb, outputPalette16, 0,
						image->paletteColorCount);
					outputPalette16 +=
						image->paletteColorCount;
				}
			}
			result = imageIndex + 1;
			imageIndex = result;
		} while (header->imageCount > result);
	}
	return result;
}

/* While a mission palette is being collected (g_generateMissionPalette), this also hands each 24-bit image
 * to ImageQuantizer_ClassifyEncodedTexLevelImage, which counts its colors into the palette being built. */
/* Builds the 8-bit palettes of a loaded texture block in the space after its
 * data, at header->dataSize: each color of a 24-bit palette, read as in
 * TexLevel_Convert24BppPalettesTo16Bpp, becomes the index of the nearest
 * g_swPalette color from 0x40 to 0xFF (Color_FindNearestRgbTripletIndex). Sets
 * header->convertedPaletteOffset when the block's palette is 24-bit and every
 * image's convertedPaletteOffset to the next output position, converting each
 * 24-bit image's palette; there is no limit on the count. Returns the number of
 * images. FeDiskIo_LoadResources calls it when g_flightBytesPerPixel is not
 * 2. */
// FUNCTION: XVT 0x40E7F0
unsigned int TexLevel_Convert24BppPalettesTo8Bpp(unsigned int *texLevel)
{
	struct TexLevelHeader *header;
	uint8_t *outputPalette8;
	const uint8_t *sourcePaletteRgba;
	unsigned int paletteIndex;
	unsigned int imageIndex;
	struct TexLevelImageHeader *image;
	struct RgbTriplet targetRgb;

	header = (struct TexLevelHeader *)texLevel;
	outputPalette8 = (uint8_t *)header + header->dataSize;
	if (header->bitsPerPixel == 24) {
		sourcePaletteRgba =
			(const uint8_t *)header + header->paletteOffset;
		header->convertedPaletteOffset =
			(uint32_t)(outputPalette8 - (uint8_t *)header);
		paletteIndex = 0;
		while (paletteIndex < header->paletteColorCount) {
			targetRgb.r = sourcePaletteRgba[0] >> 2;
			targetRgb.g = sourcePaletteRgba[1] >> 2;
			targetRgb.b = sourcePaletteRgba[2] >> 2;
			*outputPalette8 =
				(uint8_t)Color_FindNearestRgbTripletIndex(
					(const uint8_t *)&targetRgb,
					(const uint8_t *)g_swPalette, 0x40,
					0x100);
			sourcePaletteRgba += 4;
			++outputPalette8;
			++paletteIndex;
		}
	}

	imageIndex = 0;
	while (imageIndex < header->imageCount) {
		image = (struct TexLevelImageHeader
				 *)((uint8_t *)header +
				    *(uint32_t
					      *)((uint8_t *)&texLevel
							 [imageIndex] +
						 header->imageOffsetTableOffset));
		image->convertedPaletteOffset =
			(uint32_t)(outputPalette8 - (uint8_t *)image);
		if (image->bitsPerPixel == 24) {
			sourcePaletteRgba =
				(const uint8_t *)image + image->paletteOffset;
			if (g_generateMissionPalette != 0) {
				ImageQuantizer_ClassifyEncodedTexLevelImage(
					(const uint8_t *)image +
						image->encodedImageOffset,
					sourcePaletteRgba, image->width,
					image->height, image->packingMode);
			}
			paletteIndex = 0;
			while (paletteIndex < image->paletteColorCount) {
				targetRgb.r = sourcePaletteRgba[0] >> 2;
				targetRgb.g = sourcePaletteRgba[1] >> 2;
				targetRgb.b = sourcePaletteRgba[2] >> 2;
				*outputPalette8 = (uint8_t)
					Color_FindNearestRgbTripletIndex(
						(const uint8_t *)&targetRgb,
						(const uint8_t *)g_swPalette,
						0x40, 0x100);
				sourcePaletteRgba += 4;
				++outputPalette8;
				++paletteIndex;
			}
		}
		++imageIndex;
	}
	return imageIndex;
}
