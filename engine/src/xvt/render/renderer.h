#ifndef XVT_RENDER_RENDERER_H
#define XVT_RENDER_RENDERER_H

#include "aeron/compat/ddraw.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*FlightBlitSpriteFn)(uint8_t* rleData, int x, int y, int endMarker, int mirror);
typedef void (*FlightBlitSpriteFadedFn)(uint8_t* rleData, int x, int y, int endMarker, int8_t paletteShift,
										int16_t fade);
typedef void (*FlightDrawCharFn)(uint8_t ch);
typedef void (*FlightSetPaletteRangeFn)(RgbTriplet* palette, int16_t startIdx, uint16_t count);
typedef void (*FlightPaletteFn)(RgbTriplet* palette);
typedef int (*FlightComputePixelOffsetFn)(int x, int y);
typedef void (*FlightFillRectClippedFn)(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
										uint16_t borderThickness);
typedef void (*FlightScreenRectFn)(void* buffer, int x, int y, int16_t width, int height);
typedef void (*FlightDrawPointArrayFn)(uint16_t* points, int16_t count);
typedef void (*FlightDrawPixelFn)(uint16_t x, uint16_t y, int8_t color);
typedef void (*FlightDrawLineFn)(int x1, int y1, int x2, int y2, uint8_t color);

extern IDirectDraw* g_flightDirectDraw;
extern int g_forcedLodLevel;
extern float g_lodDistanceScale;
extern int g_mipmappingEnabled;
extern float g_mipLodScale;
extern int g_ditheringEnabled;
extern int g_localLightsLevel;
extern int g_specularEnabled;
extern int g_dirLightingEnabled;
extern int g_keepFullResTextures;
extern uint8_t g_flightColorEscapeBypassChar;
extern uint8_t g_palettePackedMode;
extern uint8_t g_flightGraphicsDetailPreset;
extern uint16_t g_flightViewportMode;
extern int g_useHardware3D;
extern int g_bilinearEnabled;
extern int g_loadingModel;
extern int g_surfaceWidth;
extern int g_surfaceHeight;
extern void* g_surfacePixels;
extern int width;
extern int height;
extern unsigned int g_screenWidth;
extern unsigned int g_screenHeight;
extern void* g_flightOffscreenBuffer;
extern void (*g_flightFillClipRectFn)(void);
extern int g_flightVpX;
extern int g_flightVpY;
extern uint16_t g_flightVpWidth;
extern uint16_t g_flightVpHeight;
extern uint16_t g_renderTargetComponentIdx;
extern uint16_t g_renderObjectRef;
extern uint16_t g_renderObjectRefFlags;
extern uint16_t g_flightVpCenterX;
extern uint16_t g_flightVpMaxX;
extern uint16_t g_flightVpMaxY;
extern uint16_t g_flightVpCenterY;
extern unsigned int g_flightVpBaseOffset;
extern int g_surfacePitch;
extern int g_projOffsetY;
extern uint32_t g_projScaleInt;
extern FlightBlitSpriteFn g_flightBlitSpriteFn;
extern FlightBlitSpriteFadedFn g_flightBlitSpriteFadedFn;
extern FlightDrawCharFn g_flightDrawCharFn;
extern void (*g_flightInitLineBufferFn)(void);
extern void (*g_flightRenderTransitionHook)(void);
extern void (*g_flightResetPaletteFn)(void);
extern FlightSetPaletteRangeFn g_flightSetPaletteRangeFn;
extern FlightPaletteFn g_flightGetPaletteFn;
extern FlightPaletteFn g_flightSetPaletteFn;
extern FlightComputePixelOffsetFn g_flightComputePixelOffsetFn;
extern FlightFillRectClippedFn g_flightFillRectClippedFn;
extern FlightScreenRectFn g_flightSaveScreenRectFn;
extern FlightScreenRectFn g_flightRestoreScreenRectFn;
extern FlightDrawPointArrayFn g_flightDrawPointArrayFn;
extern FlightDrawPointArrayFn g_flightDrawPointArrayMaskedFn;
extern FlightDrawPixelFn g_flightDrawPixelFn;
extern void (*g_flightSaveDrawCursorFn)(void);
extern void (*g_flightRestoreCursorFn)(void);
extern FlightDrawLineFn g_flightDrawLineFn;
extern int g_curMatR0_X;
extern int g_curMatR0_Y;
extern int g_curMatR0_Z;
extern int g_curMatR1_X;
extern int g_curMatR1_Y;
extern int g_curMatR1_Z;
extern int g_curMatR2_X;
extern int g_curMatR2_Y;
extern int g_curMatR2_Z;
extern int g_objViewMat_R0_X;
extern int g_objViewMat_R0_Y;
extern int g_objViewMat_R0_Z;
extern int g_objViewMat_R1_X;
extern int g_objViewMat_R1_Y;
extern int g_objViewMat_R1_Z;
extern int g_objViewMat_R2_X;
extern int g_objViewMat_R2_Y;
extern int g_objViewMat_R2_Z;
extern int g_objectLightDirectionX;
extern int g_objectLightDirectionY;
extern int g_objectLightDirectionZ;
extern int g_fviewMoveX_Q15;
extern int g_fviewMoveY_Q15;
extern int g_fviewMoveZ_Q15;

IDirectDraw* Renderer_GetDirectDraw(void);
void Renderer_InitD3DDevice(void);

#ifdef __cplusplus
}
#endif

#endif
