#ifndef XVT_RENDER_FLIGHT_PALETTE_H
#define XVT_RENDER_FLIGHT_PALETTE_H

#include "xvt/render/color.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_flightBytesPerPixel;
extern int g_flightBrightnessScaleQ8;
extern uint16_t g_flightPalette16Bpp[256];
extern struct RgbTriplet g_swPalette[256];
extern uint8_t g_paletteDirtyFlags;

void FlightPalette_BuildRgbRange(const struct RgbTriplet *srcRgb,
				 struct RgbTriplet *dstRgb, int startIndex,
				 int count);
void FlightPalette_ApplyToDisplay(void);
void FlightPalette_SetRange(struct RgbTriplet *rgbTriples, int16_t startIdx,
			    uint16_t count);
void FlightPalette_GetFull(struct RgbTriplet *dstPalette);
void FlightPalette_SetFull(struct RgbTriplet *rgbTriples);
void FlightPalette_ResetIf8Bit(void);
int16_t FlightPalette_Build16BppRange(struct RgbTriplet *srcRgb,
				      uint16_t *dst16, int startIndex,
				      int count);

#ifdef __cplusplus
}
#endif

#endif
