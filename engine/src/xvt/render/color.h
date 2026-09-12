#ifndef XVT_RENDER_COLOR_H
#define XVT_RENDER_COLOR_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct RgbTriplet {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

typedef enum StdColorMode {
	STDCOLOR_PAL = 0x0,
	STDCOLOR_RGB = 0x1,
	STDCOLOR_RGBA = 0x2,
} StdColorMode;

struct ColorInfo {
	StdColorMode colorMode;
	int bpp;
	int redBPP;
	int greenBPP;
	int blueBPP;
	int redPosShift;
	int greenPosShift;
	int bluePosShift;
	int redPosShiftRight;
	int greenPosShiftRight;
	int bluePosShiftRight;
	int alphaBPP;
	int alphaPosShift;
	int alphaPosShiftRight;
};

unsigned int Color_FindNearestRgbTripletIndex(const uint8_t* targetRgb, const uint8_t* palette,
											  unsigned int startIndex, unsigned int endIndex);
void Color_BuildRgb565ToPaletteIndexLut(uint8_t* outTable, unsigned int startIndex, unsigned int endIndex);
uint8_t Color_FindNearestRgb565Index(const uint16_t* palette, int targetRed, int targetGreen, int targetBlue,
									 int startIndex, int endIndex);

#ifdef __cplusplus
}
#endif

#endif
