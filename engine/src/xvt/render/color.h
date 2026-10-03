#ifndef XVT_RENDER_COLOR_H
#define XVT_RENDER_COLOR_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct RgbTriplet {
	uint8_t r; /* Red; 0 to 63 in the flight palette g_swPalette. */
	uint8_t g; /* Green, on the same scale. */
	uint8_t b; /* Blue, on the same scale. */
};

typedef enum StdColorMode {
	STDCOLOR_PAL = 0x0,
	STDCOLOR_RGB = 0x1,
	STDCOLOR_RGBA = 0x2,
} StdColorMode;

struct ColorInfo {
	StdColorMode colorMode; /* Paletted, RGB, or RGB with alpha. */
	int bpp;		/* Bits per pixel. */
	int redBPP;		/* Bits of red. */
	int greenBPP;		/* Bits of green. */
	int blueBPP;		/* Bits of blue. */
	int redPosShift;	/* Bit position of red's lowest bit. */
	int greenPosShift;	/* Bit position of green's lowest bit. */
	int bluePosShift;	/* Bit position of blue's lowest bit. */
	/* Right shift taking an 8-bit red down to redBPP bits. */
	int redPosShiftRight;
	/* Right shift taking an 8-bit green down to greenBPP bits. */
	int greenPosShiftRight;
	/* Right shift taking an 8-bit blue down to blueBPP bits. */
	int bluePosShiftRight;
	int alphaBPP;	   /* Bits of alpha, 0 for none. */
	int alphaPosShift; /* Bit position of alpha's lowest bit. */
	/* Right shift taking an 8-bit alpha down to alphaBPP bits. */
	int alphaPosShiftRight;
};

unsigned int Color_FindNearestRgbTripletIndex(const uint8_t *targetRgb,
					      const uint8_t *palette,
					      unsigned int startIndex,
					      unsigned int endIndex);
void Color_BuildRgb565ToPaletteIndexLut(uint8_t *outTable,
					unsigned int startIndex,
					unsigned int endIndex);
uint8_t Color_FindNearestRgb565Index(const uint16_t *palette, int targetRed,
				     int targetGreen, int targetBlue,
				     int startIndex, int endIndex);

#ifdef __cplusplus
}
#endif

#endif
