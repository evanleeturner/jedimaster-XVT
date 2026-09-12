#include "xvt/render/flight_palette.h"

#include "xvt/flight/flight_display.h"
#include "xvt/render/color.h"
#include "xvt/render/renderer.h"

// GLOBAL: XVT 0x5233D8
int g_flight16bppBytesPerPixel = 1;
// GLOBAL: XVT 0x523400
int g_flightBrightnessScaleQ8 = 0x100;
// GLOBAL: XVT 0x9A7BC0
RgbTriplet g_swPalette[256] = { { 0 } };
// GLOBAL: XVT 0xA00530
uint16_t g_flightTextPalette[256] = { 0 };
// GLOBAL: XVT 0xA081F4
uint8_t g_paletteDirtyFlags = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40E1B0
void FlightPalette_BuildRgbRange(const RgbTriplet* srcRgb, RgbTriplet* dstRgb, int startIndex, int count) {
	uint8_t maxChannel;
	uint8_t minChannel;
	uint8_t r;
	uint8_t b;
	uint8_t g;
	uint8_t saturation6;
	uint8_t hueSector;
	uint8_t hueOffset6;
	uint8_t value6;
	uint8_t lowChannel;
	uint8_t offsetChannel;
	uint8_t inverseOffsetChannel;

	if (g_flightBrightnessScaleQ8 == 256) {
		const RgbTriplet* src;
		RgbTriplet* dst;

		if (count-- == 0) {
			return;
		}
		src = &srcRgb[startIndex];
		dst = &dstRgb[startIndex];
		do {
			dst->r = src->r;
			dst->g = src->g;
			dst->b = src->b;
			++src;
			++dst;
		} while (count-- != 0);
		return;
	}

	{
		const RgbTriplet* src;
		RgbTriplet* dst;

		if (count-- == 0) {
			return;
		}

		dst = &dstRgb[startIndex];
		src = &srcRgb[startIndex];
		do {
			r = src->r;
			g = src->g;
			b = src->b;

			if (r >= g && r >= b) {
				maxChannel = r;
			} else {
				maxChannel = (g >= r && g >= b) ? g : b;
			}

			if (r <= g && r <= b) {
				minChannel = r;
			} else {
				minChannel = (g <= r && g <= b) ? g : b;
			}

			if (maxChannel != 0) {
				saturation6 = (uint8_t)(63 * (maxChannel - minChannel) / maxChannel);
			} else {
				saturation6 = 0;
			}

			if (saturation6 != 0) {
				if (r == maxChannel) {
					if (g >= b) {
						hueOffset6 = (uint8_t)(63 * (g - b) / (maxChannel - minChannel));
						hueSector = 0;
					} else {
						hueOffset6 = (uint8_t)(63 * (g - b) / (maxChannel - minChannel) + 63);
						hueSector = 5;
					}
				} else if (g == maxChannel) {
					if (b >= r) {
						hueOffset6 = (uint8_t)(63 * (b - r) / (maxChannel - minChannel));
						hueSector = 2;
					} else {
						hueOffset6 = (uint8_t)(63 * (b - r) / (maxChannel - minChannel) + 63);
						hueSector = 1;
					}
				} else if (r >= g) {
					hueOffset6 = (uint8_t)(63 * (r - g) / (maxChannel - minChannel));
					hueSector = 4;
				} else {
					hueOffset6 = (uint8_t)(63 * (r - g) / (maxChannel - minChannel) + 63);
					hueSector = 3;
				}
			}

			value6 = (uint8_t)(((unsigned int)g_flightBrightnessScaleQ8 * maxChannel) >> 8);
			if (value6 > 63) {
				value6 = 63;
			}

			if (saturation6 != 0) {
				lowChannel = (uint8_t)(value6 * (63 - saturation6) / 63);
				offsetChannel = (uint8_t)(value6 * (63 - saturation6 * hueOffset6 / 63) / 63);
				inverseOffsetChannel = (uint8_t)(value6 * (63 - saturation6 * (63 - hueOffset6) / 63) / 63);

				switch (hueSector) {
					case 0:
						r = value6;
						g = inverseOffsetChannel;
						b = lowChannel;
						break;
					case 1:
						r = offsetChannel;
						g = value6;
						b = lowChannel;
						break;
					case 2:
						r = lowChannel;
						g = value6;
						b = inverseOffsetChannel;
						break;
					case 3:
						r = lowChannel;
						g = offsetChannel;
						b = value6;
						break;
					case 4:
						r = inverseOffsetChannel;
						g = lowChannel;
						b = value6;
						break;
					case 5:
						r = value6;
						g = lowChannel;
						b = offsetChannel;
						break;
					default:
						break;
				}
			} else {
				r = value6;
				g = value6;
				b = value6;
			}

			dst->r = r;
			dst->g = g;
			dst->b = b;
			++dst;
			++src;
		} while (count-- != 0);
	}
}

// FUNCTION: XVT 0x40E590
void FlightPalette_Reset(void) {
	RgbTriplet adjustedPalette[256];

	FlightPalette_BuildRgbRange(g_swPalette, adjustedPalette, 0, 256);
	if (g_flight16bppBytesPerPixel == 1)
		FlightDisplay_SetPaletteEntries((uint8_t*)adjustedPalette, 0, 256);
	g_paletteDirtyFlags &= ~1;
}

// FUNCTION: XVT 0x40E5E0
void FlightPalette_SetRange(RgbTriplet* rgbTriples, int16_t startIdx, uint16_t count) {
	uint16_t paletteIndex;
	int endIndex;

	paletteIndex = (uint16_t)startIdx;
	endIndex = paletteIndex + count;
	while (paletteIndex < endIndex) {
		g_swPalette[paletteIndex].r = rgbTriples->r;
		g_swPalette[paletteIndex].g = rgbTriples->g;
		g_swPalette[paletteIndex].b = rgbTriples->b;
		++paletteIndex;
		++rgbTriples;
	}

	if (g_palettePackedMode == 2)
		FlightPalette_Build16BppRange(g_swPalette, g_flightTextPalette, (uint16_t)startIdx, count);
}

// FUNCTION: XVT 0x40E660
void FlightPalette_GetFull(RgbTriplet* dstPalette) {
	uint16_t index;

	index = 0;
	do {
		dstPalette->r = g_swPalette[index].r;
		dstPalette->g = g_swPalette[index].g;
		dstPalette->b = g_swPalette[index].b;
		++dstPalette;
		++index;
	} while (index < 256);
}

// FUNCTION: XVT 0x40E6A0
void FlightPalette_SetFull(RgbTriplet* rgbTriples) { FlightPalette_SetRange(rgbTriples, 0, 256); }

// FUNCTION: XVT 0x449100
void FlightPalette_ResetIf8Bit(void) {
	if (g_flight16bppBytesPerPixel == 1)
		FlightPalette_Reset();
}

// FUNCTION: XVT 0x449410
int16_t FlightPalette_Build16BppRange(RgbTriplet* srcRgb, uint16_t* dst16, int startIndex, int count) {
	RgbTriplet* src;
	uint16_t* dst;
	int remaining;
	int endIndex;
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t maxChannel;
	uint8_t minChannel;
	uint8_t saturation6;
	uint8_t hueSector;
	uint8_t hueOffset6;
	uint8_t value6;
	uint8_t lowChannel;
	uint8_t offsetChannel;
	uint8_t inverseOffsetChannel;
	uint16_t packedColor;
	int16_t result;

	if (g_flightBrightnessScaleQ8 == 256) {
		endIndex = startIndex + count;
		result = (int16_t)endIndex;
		if (startIndex < endIndex) {
			dst = &dst16[startIndex];
			src = &srcRgb[startIndex];
			remaining = count;
			do {
				if (Display_IsPixelFormat555()) {
					packedColor = (uint16_t)((((src->r & 0x7Eu) << 9) & 0x7FFFu) | (src->b >> 1) |
											 (16 * (src->g & 0xFEu)));
				} else {
					packedColor = (uint16_t)(((src->r >> 1) << 11) | (src->g << 5) | (src->b >> 1));
				}
				*dst = packedColor;
				result = (int16_t)packedColor;
				++dst;
				++src;
				--remaining;
			} while (remaining != 0);
		}
		return result;
	}

	if (count-- == 0)
		return 0;

	dst = &dst16[startIndex];
	src = &srcRgb[startIndex];
	do {
		r = src->r;
		g = src->g;
		b = src->b;

		if (r < g || r < b) {
			if (g < r || g < b)
				maxChannel = b;
			else
				maxChannel = g;
		} else {
			maxChannel = r;
		}

		if (r > g || r > b) {
			if (g > r || g > b)
				minChannel = b;
			else
				minChannel = g;
		} else {
			minChannel = r;
		}

		if (maxChannel != 0)
			saturation6 = (uint8_t)(63 * (maxChannel - minChannel) / maxChannel);
		else
			saturation6 = 0;

		if (saturation6 != 0) {
			if (r == maxChannel) {
				if (g >= b) {
					hueSector = 0;
					hueOffset6 = (uint8_t)(63 * (g - b) / (maxChannel - minChannel));
				} else {
					hueSector = 5;
					hueOffset6 = (uint8_t)(63 * (g - b) / (maxChannel - minChannel) + 63);
				}
			} else if (g == maxChannel) {
				if (b >= r) {
					hueSector = 2;
					hueOffset6 = (uint8_t)(63 * (b - r) / (maxChannel - minChannel));
				} else {
					hueSector = 1;
					hueOffset6 = (uint8_t)(63 * (b - r) / (maxChannel - minChannel) + 63);
				}
			} else if (r >= g) {
				hueSector = 4;
				hueOffset6 = (uint8_t)(63 * (r - g) / (maxChannel - minChannel));
			} else {
				hueSector = 3;
				hueOffset6 = (uint8_t)(63 * (r - g) / (maxChannel - minChannel) + 63);
			}
		}

		value6 = (uint8_t)(((unsigned int)g_flightBrightnessScaleQ8 * maxChannel) >> 8);
		if (value6 > 63)
			value6 = 63;

		if (saturation6 != 0) {
			lowChannel = (uint8_t)(value6 * (63 - saturation6) / 63);
			offsetChannel = (uint8_t)(value6 * (63 - saturation6 * hueOffset6 / 63) / 63);
			inverseOffsetChannel = (uint8_t)(value6 * (63 - saturation6 * (63 - hueOffset6) / 63) / 63);
			switch (hueSector) {
				case 0:
					r = value6;
					g = inverseOffsetChannel;
					b = lowChannel;
					break;
				case 1:
					r = offsetChannel;
					g = value6;
					b = lowChannel;
					break;
				case 2:
					r = lowChannel;
					g = value6;
					b = inverseOffsetChannel;
					break;
				case 3:
					r = lowChannel;
					g = offsetChannel;
					b = value6;
					break;
				case 4:
					r = inverseOffsetChannel;
					g = lowChannel;
					b = value6;
					break;
				case 5:
					r = value6;
					g = lowChannel;
					b = offsetChannel;
					break;
				default:
					break;
			}
		} else {
			r = value6;
			g = value6;
			b = value6;
		}

		if (Display_IsPixelFormat555()) {
			packedColor = (uint16_t)((((r & 0x7Eu) << 9) & 0x7FFFu) | (b >> 1) | (16 * (g & 0xFEu)));
		} else {
			packedColor = (uint16_t)(((r >> 1) << 11) | (g << 5) | (b >> 1));
		}
		*dst = packedColor;
		++dst;
		++src;
	} while (count-- != 0);
	return 0;
}
