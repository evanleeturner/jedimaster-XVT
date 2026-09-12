#include "xvt/render/color.h"

#include "xvt/render/flight_palette.h"

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40E940
unsigned int Color_FindNearestRgbTripletIndex(const uint8_t* targetRgb, const uint8_t* palette,
											  unsigned int startIndex, unsigned int endIndex) {
	unsigned int nearestIndex;
	unsigned int paletteIndex;
	int nearestDistance;
	const uint8_t* paletteEntry;
	int targetComponents[3];

	nearestIndex = startIndex;
	paletteIndex = startIndex;
	nearestDistance = INT32_MAX;
	if (endIndex > startIndex) {
		int redDistance;
		int blueDistance;
		int distance;

		paletteEntry = &palette[3 * startIndex];
		targetComponents[0] = targetRgb[0];
		targetComponents[1] = targetRgb[1];
		targetComponents[2] = targetRgb[2];
		do {
			redDistance = targetComponents[0] - paletteEntry[0];
			blueDistance = targetComponents[2] - paletteEntry[2];
			distance = redDistance * redDistance +
					   (targetComponents[1] - paletteEntry[1]) * (targetComponents[1] - paletteEntry[1]) +
					   blueDistance * blueDistance;
			if (nearestDistance > distance) {
				nearestIndex = paletteIndex;
				nearestDistance = distance;
			}
			paletteEntry += 3;
			++paletteIndex;
		} while (endIndex > paletteIndex);
	}
	return nearestIndex;
}

// FUNCTION: XVT 0x40E9E0
void Color_BuildRgb565ToPaletteIndexLut(uint8_t* outTable, unsigned int startIndex, unsigned int endIndex) {
	int rgb565Value;
	int shiftedValue;
	uint8_t targetRgb[3];
	uint8_t* output;
	unsigned int firstPaletteIndex;
	unsigned int lastPaletteIndex;

	firstPaletteIndex = startIndex;
	lastPaletteIndex = endIndex;
	output = outTable;
	rgb565Value = 0;
	do {
		shiftedValue = rgb565Value >> 5;
		targetRgb[1] = shiftedValue & 0x3F;
		targetRgb[0] = 2 * (((unsigned int)shiftedValue >> 6) & 0x1F);
		shiftedValue = rgb565Value++;
		targetRgb[2] = 2 * (shiftedValue & 0x1F);
		output[rgb565Value - 1] = (uint8_t)Color_FindNearestRgbTripletIndex(
			targetRgb, (const uint8_t*)g_swPalette, firstPaletteIndex, lastPaletteIndex);
	} while (rgb565Value < 0x10000);
}

// FUNCTION: XVT 0x476B00
uint8_t Color_FindNearestRgb565Index(const uint16_t* palette, int targetRed, int targetGreen, int targetBlue,
									 int startIndex, int endIndex) {
	int nearestDistance;
	int nearestIndex;
	int paletteIndex;

	nearestDistance = INT32_MAX;
	for (paletteIndex = startIndex; paletteIndex < endIndex; ++paletteIndex) {
		uint16_t color;
		int blue;
		int green;
		int red;
		int distance;

		color = palette[paletteIndex];
		blue = color & 0x1F;
		color >>= 5;
		green = color & 0x3F;
		color >>= 6;
		red = color & 0x1F;
		distance = (blue - targetBlue) * (blue - targetBlue) + (green - targetGreen) * (green - targetGreen) +
				   (red - targetRed) * (red - targetRed);
		if (distance == 0)
			return (uint8_t)paletteIndex;
		if (nearestDistance > distance) {
			nearestDistance = distance;
			nearestIndex = paletteIndex;
		}
	}
	return (uint8_t)nearestIndex;
}
