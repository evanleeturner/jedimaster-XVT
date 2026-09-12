#ifndef XVT_RENDER_FLIGHT_PALETTE_H
#define XVT_RENDER_FLIGHT_PALETTE_H

#include "xvt/render/color.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_flight16bppBytesPerPixel;
extern int g_flightBrightnessScaleQ8;
extern uint16_t g_flightTextPalette[256];
extern RgbTriplet g_swPalette[256];
extern uint8_t g_paletteDirtyFlags;

void FlightPalette_BuildRgbRange(const RgbTriplet* srcRgb, RgbTriplet* dstRgb, int startIndex, int count);
void FlightPalette_Reset(void);
void FlightPalette_SetRange(RgbTriplet* rgbTriples, int16_t startIdx, uint16_t count);
void FlightPalette_GetFull(RgbTriplet* dstPalette);
void FlightPalette_SetFull(RgbTriplet* rgbTriples);
void FlightPalette_ResetIf8Bit(void);
int16_t FlightPalette_Build16BppRange(RgbTriplet* srcRgb, uint16_t* dst16, int startIndex, int count);

#ifdef __cplusplus
}
#endif

#endif
