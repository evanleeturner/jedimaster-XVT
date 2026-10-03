#include "xvt/render/flight_palette.h"

#include "xvt/flight/flight_display.h"
#include "xvt/render/color.h"
#include "xvt/render/renderer.h"

/* Bytes per pixel of the flight frame buffer: 1 in the 8-bit paletted modes, 2
 * in 16-bit color; 1 at start. Four functions write it: Flight_Main in the
 * original build, XvtFlightEntry_Configure in the modern one,
 * FlightDisplay_Init and ModelPreview_LoadModel. */
// GLOBAL: XVT 0x5233D8
int g_flightBytesPerPixel = 1;
/* Flight palette brightness, 256 for 1.0 (eight fraction bits).
 * FlightPalette_BuildRgbRange and FlightPalette_Build16BppRange scale each
 * color's brightest channel by it, and copy colors unchanged at exactly 256. At
 * flight start Flight_Main (original build) or XvtFlightEntry_Configure
 * (modern) sets it to (setting + 4) << 6 from the solo or multiplayer
 * brightness setting, clamped to 256 to 704; in the 8-bit modes the Alt+B key
 * raises it by 0x40, going from 0x300 back to 0x100 (Flight_UpdatePlayerStep,
 * XvtFlightSim_UpdatePlayerStep). */
// GLOBAL: XVT 0x523400
int g_flightBrightnessScaleQ8 = 0x100;
/* The flight palette before the brightness adjustment: 256 colors with channels
 * 0 to 63. FlightPalette_SetRange writes it, FlightPalette_ApplyToDisplay sends
 * an adjusted copy to the display, and the color matching code reads it. */
// GLOBAL: XVT 0x9A7BC0
struct RgbTriplet g_swPalette[256] = {{0}};
/* The flight palette as 16-bit pixels, one per palette index, for the 16-bit
 * drawing code; FlightPalette_SetRange rebuilds the entries it sets while
 * g_flightPixelMode is 2, brightness included. */
// GLOBAL: XVT 0xA00530
uint16_t g_flightPalette16Bpp[256] = {0};
/* FlightPalette_ApplyToDisplay clears its 0x1 bit; nothing else reads or writes
 * it. */
// GLOBAL: XVT 0xA081F4
uint8_t g_paletteDirtyFlags = 0;

/* Writes entries startIndex to startIndex + count - 1 of srcRgb, channels 0 to
 * 63, into the same entries of dstRgb, adjusted for g_flightBrightnessScaleQ8.
 * At 256 it copies them unchanged. Otherwise it splits each color into a
 * saturation, 63 * (max - min) / max, a hue sector and offset, and a value,
 * g_flightBrightnessScaleQ8 * max >> 8 capped at 63, and rebuilds the three
 * channels from them; a gray gets the value in all three. Does nothing for
 * count 0. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40E1B0
void FlightPalette_BuildRgbRange(const struct RgbTriplet *srcRgb,
				 struct RgbTriplet *dstRgb, int startIndex,
				 int count)
{
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
		const struct RgbTriplet *src;
		struct RgbTriplet *dst;

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
		const struct RgbTriplet *src;
		struct RgbTriplet *dst;

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
				saturation6 =
					(uint8_t)(63 *
						  (maxChannel - minChannel) /
						  maxChannel);
			} else {
				saturation6 = 0;
			}

			if (saturation6 != 0) {
				if (r == maxChannel) {
					if (g >= b) {
						hueOffset6 =
							(uint8_t)(63 * (g - b) /
								  (maxChannel -
								   minChannel));
						hueSector = 0;
					} else {
						hueOffset6 =
							(uint8_t)(63 * (g - b) /
									  (maxChannel -
									   minChannel) +
								  63);
						hueSector = 5;
					}
				} else if (g == maxChannel) {
					if (b >= r) {
						hueOffset6 =
							(uint8_t)(63 * (b - r) /
								  (maxChannel -
								   minChannel));
						hueSector = 2;
					} else {
						hueOffset6 =
							(uint8_t)(63 * (b - r) /
									  (maxChannel -
									   minChannel) +
								  63);
						hueSector = 1;
					}
				} else if (r >= g) {
					hueOffset6 = (uint8_t)(63 * (r - g) /
							       (maxChannel -
								minChannel));
					hueSector = 4;
				} else {
					hueOffset6 =
						(uint8_t)(63 * (r - g) /
								  (maxChannel -
								   minChannel) +
							  63);
					hueSector = 3;
				}
			}

			value6 = (uint8_t)(((unsigned int)
						    g_flightBrightnessScaleQ8 *
					    maxChannel) >>
					   8);
			if (value6 > 63) {
				value6 = 63;
			}

			if (saturation6 != 0) {
				lowChannel = (uint8_t)(value6 *
						       (63 - saturation6) / 63);
				offsetChannel =
					(uint8_t)(value6 *
						  (63 - saturation6 *
								hueOffset6 /
								63) /
						  63);
				inverseOffsetChannel =
					(uint8_t)(value6 *
						  (63 -
						   saturation6 *
							   (63 - hueOffset6) /
							   63) /
						  63);

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

/* Sends g_swPalette, adjusted by FlightPalette_BuildRgbRange, to the display
 * with FlightDisplay_SetPaletteEntries when g_flightBytesPerPixel is 1, and
 * clears the 0x1 bit of g_paletteDirtyFlags. FlightRender_InstallCallbacks
 * installs it as g_flightResetPaletteFn; Flight_MainLoop in the original build
 * and XvtFlightLoading_Palette in the modern one also call it. */
// FUNCTION: XVT 0x40E590
void FlightPalette_ApplyToDisplay(void)
{
	struct RgbTriplet adjustedPalette[256];

	FlightPalette_BuildRgbRange(g_swPalette, adjustedPalette, 0, 256);
	if (g_flightBytesPerPixel == 1) {
		FlightDisplay_SetPaletteEntries((uint8_t *)adjustedPalette, 0,
						256);
	}
	g_paletteDirtyFlags &= ~1;
}

/* Copies count colors from rgbTriples into g_swPalette from entry startIdx,
 * read as unsigned 16 bits; while g_flightPixelMode is 2 it also rebuilds those
 * entries of g_flightPalette16Bpp with FlightPalette_Build16BppRange. Does not
 * send the colors to the display or check that they stay under 256. Installed
 * as g_flightSetPaletteRangeFn. */
// FUNCTION: XVT 0x40E5E0
void FlightPalette_SetRange(struct RgbTriplet *rgbTriples, int16_t startIdx,
			    uint16_t count)
{
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

	if (g_flightPixelMode == 2) {
		FlightPalette_Build16BppRange(g_swPalette, g_flightPalette16Bpp,
					      (uint16_t)startIdx, count);
	}
}

/* Copies the 256 colors of g_swPalette to dstPalette. Installed as
 * g_flightGetPaletteFn. */
// FUNCTION: XVT 0x40E660
void FlightPalette_GetFull(struct RgbTriplet *dstPalette)
{
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

/* Calls FlightPalette_SetRange for all 256 colors. Installed as
 * g_flightSetPaletteFn. */
// FUNCTION: XVT 0x40E6A0
void FlightPalette_SetFull(struct RgbTriplet *rgbTriples)
{
	FlightPalette_SetRange(rgbTriples, 0, 256);
}

/* Calls FlightPalette_ApplyToDisplay when g_flightBytesPerPixel is 1.
 * Flight_WndProc calls it on message 0x311; in the original build
 * Flight_PumpWindowMessages calls it after bringing the flight window back to
 * the foreground. */
// FUNCTION: XVT 0x449100
void FlightPalette_ResetIf8Bit(void)
{
	if (g_flightBytesPerPixel == 1) {
		FlightPalette_ApplyToDisplay();
	}
}

/* Packs entries startIndex to startIndex + count - 1 of srcRgb, channels 0 to
 * 63, into 16-bit pixels in the same entries of dst16: 5-6-5 as (r >> 1, g,
 * b >> 1), or 5-5-5 with each channel >> 1 when Display_IsPixelFormat555 is
 * true. At brightness 256 the colors go in unchanged and it returns the last
 * pixel packed, or startIndex + count cut to 16 bits when it packed none.
 * Otherwise each color first gets the adjustment FlightPalette_BuildRgbRange
 * makes, and it returns 0, packing nothing for count 0. */
// FUNCTION: XVT 0x449410
int16_t FlightPalette_Build16BppRange(struct RgbTriplet *srcRgb,
				      uint16_t *dst16, int startIndex,
				      int count)
{
	struct RgbTriplet *src;
	uint16_t *dst;
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
					packedColor =
						(uint16_t)((((src->r & 0x7Eu)
							     << 9) &
							    0x7FFFu) |
							   (src->b >> 1) |
							   (16 *
							    (src->g & 0xFEu)));
				} else {
					packedColor = (uint16_t)(((src->r >> 1)
								  << 11) |
								 (src->g << 5) |
								 (src->b >> 1));
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

	if (count-- == 0) {
		return 0;
	}

	dst = &dst16[startIndex];
	src = &srcRgb[startIndex];
	do {
		r = src->r;
		g = src->g;
		b = src->b;

		if (r < g || r < b) {
			if (g < r || g < b) {
				maxChannel = b;
			} else {
				maxChannel = g;
			}
		} else {
			maxChannel = r;
		}

		if (r > g || r > b) {
			if (g > r || g > b) {
				minChannel = b;
			} else {
				minChannel = g;
			}
		} else {
			minChannel = r;
		}

		if (maxChannel != 0) {
			saturation6 = (uint8_t)(63 * (maxChannel - minChannel) /
						maxChannel);
		} else {
			saturation6 = 0;
		}

		if (saturation6 != 0) {
			if (r == maxChannel) {
				if (g >= b) {
					hueSector = 0;
					hueOffset6 = (uint8_t)(63 * (g - b) /
							       (maxChannel -
								minChannel));
				} else {
					hueSector = 5;
					hueOffset6 =
						(uint8_t)(63 * (g - b) /
								  (maxChannel -
								   minChannel) +
							  63);
				}
			} else if (g == maxChannel) {
				if (b >= r) {
					hueSector = 2;
					hueOffset6 = (uint8_t)(63 * (b - r) /
							       (maxChannel -
								minChannel));
				} else {
					hueSector = 1;
					hueOffset6 =
						(uint8_t)(63 * (b - r) /
								  (maxChannel -
								   minChannel) +
							  63);
				}
			} else if (r >= g) {
				hueSector = 4;
				hueOffset6 =
					(uint8_t)(63 * (r - g) /
						  (maxChannel - minChannel));
			} else {
				hueSector = 3;
				hueOffset6 = (uint8_t)(63 * (r - g) /
							       (maxChannel -
								minChannel) +
						       63);
			}
		}

		value6 = (uint8_t)(((unsigned int)g_flightBrightnessScaleQ8 *
				    maxChannel) >>
				   8);
		if (value6 > 63) {
			value6 = 63;
		}

		if (saturation6 != 0) {
			lowChannel =
				(uint8_t)(value6 * (63 - saturation6) / 63);
			offsetChannel =
				(uint8_t)(value6 *
					  (63 - saturation6 * hueOffset6 / 63) /
					  63);
			inverseOffsetChannel =
				(uint8_t)(value6 *
					  (63 - saturation6 *
							(63 - hueOffset6) /
							63) /
					  63);
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
			packedColor =
				(uint16_t)((((r & 0x7Eu) << 9) & 0x7FFFu) |
					   (b >> 1) | (16 * (g & 0xFEu)));
		} else {
			packedColor = (uint16_t)(((r >> 1) << 11) | (g << 5) |
						 (b >> 1));
		}
		*dst = packedColor;
		++dst;
		++src;
	} while (count-- != 0);
	return 0;
}
