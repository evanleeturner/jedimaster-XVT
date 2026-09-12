#ifndef XVT_RENDER_FLIGHT_SW_H
#define XVT_RENDER_FLIGHT_SW_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { FLIGHT_SW_16BPP_BYTES_PER_PIXEL = 2 };

typedef enum FlightResolutionMode {
	FLIGHT_RESOLUTION_320X240 = 0x13,
	FLIGHT_RESOLUTION_640X480 = 0x101,
	FLIGHT_RESOLUTION_640X480_16BPP = 0x111,
	FLIGHT_RESOLUTION_480X360 = 0x112,
} FlightResolutionMode;

extern int16_t g_flightSwRotSpriteOutputOffsetY;
extern int16_t g_flightSwRotSpriteOutputOffsetX;
extern int16_t g_flightSwRotSpriteInputCornerX;
extern int16_t g_flightSwRotSpriteInputCornerY;
extern int16_t g_flightSwRotSpriteEdgeCursorX;
extern int16_t g_flightSwRotSpriteEdgeCursorY;
extern int g_flightResolutionMode;
extern unsigned int g_swFramebufferClearChunkSize;
extern unsigned int g_vesaGrainsPerPage;
extern uint8_t* g_flightSwFramebufferBase;
extern int g_flightRenderModeId;
extern int g_starfieldGridDimension;
extern int g_starfieldColors8Initialized;
extern int g_starfieldColors16Initialized;
extern int g_starfieldRandomVectorIndicesInitialized;
extern uint16_t g_starfieldColors8Handle;
extern uint16_t g_starfieldColors16Handle;
extern uint16_t g_starfieldRandomVectorIndicesHandle;
extern int32_t g_starfieldJitterX[125];
extern int32_t g_starfieldJitterY[125];
extern int32_t g_starfieldJitterZ[125];
extern uint8_t g_unusedFlightRenderColorByte;
#ifndef XVT_MODERN
extern unsigned int g_vesaWindow;
#endif
extern uint8_t g_flightSwRleRunLengthMaskByPackingMode[9];
extern uint8_t g_flightSwRlePaletteShiftByPackingMode[9];
extern int g_flightSwRotSpriteSpanRunsEnabled;
extern uint16_t g_flightSwRotSpriteSavedClipMinX;
extern uint16_t g_flightSwRotSpriteSavedClipMaxX;
extern int16_t g_flightSwRotSpriteDestYMode;
extern int g_flightSwRotSpriteDestPitchBytes;
extern uint16_t g_flightSwRotSpriteSavedPrimaryEdgeY;
extern uint16_t g_flightSwRotSpriteSavedPrimaryEdgeX;
extern uint16_t g_flightFillRectBottom;
extern uint16_t g_flightFillRectRight;
extern uint16_t g_flightFillRectLeft;
extern uint16_t g_flightFillRectTop;
extern int g_flightFillRectCurrentY;
extern int g_flightFillRectRemainingRows;
extern int32_t g_flightFillRectCurrentY8bpp;
extern uint16_t g_flightFillRectTop8bpp;
extern uint16_t g_flightFillRectBottom8bpp;
extern uint16_t g_flightFillRectRight8bpp;
extern uint16_t g_flightFillRectLeft8bpp;
extern unsigned int g_flightFillRectRemainingRows8bpp;

struct FlightSwRotSpriteEdgePoint {
	int16_t x;
	int16_t y;
};

struct FlightSwRotSpriteCoeffState {
	uint16_t rotationAngle;
	int16_t sinQ15;
	uint16_t field04;
	int16_t cosQ15;
	uint16_t field08;
	uint16_t primaryCosQ15;
	uint16_t primaryStepReciprocal;
	uint16_t scanCount;
	uint16_t field10;
	uint16_t field12;
	uint16_t field14;
	uint16_t octant;
	uint16_t primaryAxisSwap;
	uint16_t secondaryAxisSwap;
	uint16_t secondaryScaleLow;
	uint16_t secondaryScaleHigh;
	int16_t firstEdgeX;
	int16_t lastEdgeX;
	int16_t firstEdgeScreenY;
	int16_t lastEdgeScreenY;
	int16_t firstEdgeY;
	int16_t lastEdgeY;
	FlightSwRotSpriteEdgePoint edgePointsWithPredecessor[1601];
	uint16_t secondaryStepByte;
	uint16_t runLengthCount;
	uint16_t runLengths[1600];
	void* destLinePtr;
	int spanOffsets[1600];
	int destPitchDelta;
};

struct FlightCursorShapeOffset {
	int8_t x;
	int8_t y;
};

struct FlightSwMarkerOffset {
	int8_t dx;
	int8_t dy;
};

struct FlightSwRotSpriteScaleState {
	uint16_t screenScale;
	uint8_t cachedStepLowByte;
	uint8_t cachedStepHighByte;
	uint8_t horizontalStepLowByte;
	uint8_t horizontalStepHighByte;
	uint8_t verticalStepLowByte;
	uint8_t verticalStepHighByte;
	uint16_t lowWordStepTable[256];
	uint16_t highWordStepTable[256];
	uint16_t textureScaleX;
	uint16_t textureScaleY;
};

struct FlightSwRotSpriteSpanRun {
	int startX;
	int pixelLowByte;
	int length;
};

extern FlightSwRotSpriteSpanRun g_flightSwRotSpriteSpanRuns[512];
extern FlightSwRotSpriteScaleState g_flightSwRotSpriteScaleState;
extern uint8_t* g_flightSwRotSpriteDestLinePtr;
extern int16_t g_flightSwRotSpriteSkipSecondaryScaleStep;
extern uint16_t g_flightSwRotSpriteSecondaryScaleAccum;
extern int16_t g_flightSwRotSpriteClipMinX;
extern int16_t g_flightSwRotSpriteSpanBaseX;
extern int16_t g_flightSwRotSpritePrimaryEdgeX;
extern int16_t g_flightSwRotSpritePrimaryEdgeY;
extern int16_t g_flightSwRotSpriteViewportMaxY;
extern int16_t g_flightSwRotSpriteClipMaxX;
extern FlightSwRotSpriteCoeffState* g_flightSwRotSpriteCoeffs;
extern int g_flightSwRotSpriteSpanRunCountdown;

typedef struct SpritePayload {
	uint32_t payloadSize;
	uint32_t colorTable24Offset;
	uint32_t rowDataOffset;
	uint32_t palette16Offset;
	uint32_t field10;
	uint32_t field14;
	int32_t anchorX;
	int32_t anchorY;
	int32_t field20;
	int32_t field24;
	int32_t colorCount;
} SpritePayload;

void FlightSw_InitLineBuffer(void);
void FlightSw_SetRenderTarget(void* surface, int width, unsigned int height, int pitchBytes);
int FlightSw_GetLineBufferAddr(int line);
int FlightSw_GetLinePitch(void);
int FlightSw_ComputePixelOffset8bpp(int x, int y);
void FlightSw_BlitSpriteRle8bpp(uint8_t* rleData, int x, int y, int endMarker, int mirror);
void FlightSw_BlitSpriteRleFaded8bpp(uint8_t* rleData, int x, int y, int endMarker, int8_t paletteShift,
									 int16_t fadeAmount);
void FlightSw_BlitSpriteRleImpl8bpp(uint8_t* rleData, int x, int y, int endMarker, int mirror, char mode,
									int16_t fadeAmount);
void FlightSw_BlitMapIconRle(uint8_t* rleData, int x, int y, int transparentIndex, int mirror);
void FlightSw_DrawPixel8bpp(uint16_t x, uint16_t y, int8_t colorIndex);
void FlightSw_FillClipRect8bpp(void);
void FlightSw_FillRectOrBorder8bpp(uint16_t borderThickness);
void FlightSw_FillRectClipped8bpp(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
								  uint16_t borderThickness);
void FlightSw_SaveScreenRect8bpp(uint8_t* buffer, int x, int y, int16_t width, int height);
void FlightSw_RestoreScreenRect8bpp(uint8_t* buffer, int x, int y, int16_t width, int height);
void FlightSw_DrawPointArray8bpp(uint16_t* points, int16_t count);
void FlightSw_DrawPointArrayMasked8bpp(uint16_t* points, int16_t count);
void FlightSw_DrawRadarTargetMarker8bpp(void);
void FlightSw_RestoreRadarTargetMarker8bpp(void);
uint8_t FlightSw_DrawCrossMarker8bpp(uint16_t x, uint16_t y, uint8_t color);
uint8_t FlightSw_RestoreCrossMarker8bpp(uint16_t x, uint16_t y);
void FlightStarfield_Render(void);
void RtsVga2_SetCurrentPage(uint8_t window, uint16_t page);
void FlightScreenshot_Capture(void);
void FlightSw_DrawLine8bpp(int x1, int y1, int x2, int y2, uint8_t colorIdx);
void FlightSw_DrawRotSpriteSpanRuns8(const FlightSwRotSpriteSpanRun* runs, uint8_t* destBase,
									 const int* spanOffsets);
void FlightSw_DrawClippedRotSpriteSpanRuns8(const FlightSwRotSpriteSpanRun* runs, uint8_t* destBase,
											const int* spanOffsets);
void FlightSw_DrawRotSpriteSpanRuns16(const FlightSwRotSpriteSpanRun* runs, uint8_t* destBase,
									  const int* spanOffsets);
void FlightSw_DrawClippedRotSpriteSpanRuns16(const FlightSwRotSpriteSpanRun* runs, uint8_t* destBase,
											 const int* spanOffsets);
void FlightSw_DrawRotatedSpriteQuad(int16_t screenX, int16_t screenY, uint16_t screenSize,
									SpritePayload* sprite);
void FlightSw_ClipAndBlitPreparedRotatedSprite(int* cornerCoords);
void FlightSw_PrepareSpriteRotationTables(int16_t rotationAngle, int bytesPerPixel);
int FlightSw_BuildSpriteTintRemapTables(SpritePayload* sprite);
uint16_t FlightSw_LookupScaledTangent(uint16_t angle, int16_t scalePercent);
void FlightSw_PrepareRotatedSpriteScaleState(uint16_t screenSize, FlightSwRotSpriteCoeffState* rotationCoeffs,
											 FlightSwRotSpriteScaleState* scaleState);
void FlightSw_RotateSpritePoint(uint16_t* rotationCoeffs, FlightSwRotSpriteScaleState* scaleState);
void FlightSw_BuildSpriteRotationCoeffs(uint16_t rotationAngle, uint16_t* outCoeffs);
void FlightSw_RasterizePreparedRotatedSprite(uint8_t* spriteData, int formatIndex);
void FlightSw_AdvanceRotSpriteSecondaryScale(void);
int FlightSw_InitRotSpriteForCurrentOctant(void);
int FlightSw_StepRotSpriteForCurrentOctant(void);
int FlightSw_InitRotSpriteOctant0(void);
int FlightSw_StepRotSpriteOctant0(void);
int FlightSw_InitRotSpriteOctant1(void);
int FlightSw_StepRotSpriteOctant1(void);
int FlightSw_InitRotSpriteOctant2(void);
int FlightSw_StepRotSpriteOctant2(void);
int FlightSw_InitRotSpriteOctant3(void);
int FlightSw_StepRotSpriteOctant3(void);
int FlightSw_InitRotSpriteOctant4(void);
int FlightSw_StepRotSpriteOctant4(void);
int FlightSw_InitRotSpriteOctant5(void);
int FlightSw_StepRotSpriteOctant5(void);
int FlightSw_InitRotSpriteOctant6(void);
int FlightSw_StepRotSpriteOctant6(void);
int FlightSw_InitRotSpriteOctant7(void);
int FlightSw_StepRotSpriteOctant7(void);
uint8_t* FlightSw_SetRotatedSpriteDestBuffer(uint8_t* bufferAddress);
unsigned int SetFlightViewport(unsigned int arg1, unsigned int arg2, int arg3, unsigned int arg4);
void FlightSw_CopyLegacy8BitViewportToFramebuffer(const uint8_t* srcPixels);
unsigned int PushFlightViewport(uint16_t arg1, uint16_t arg2, int16_t arg3, unsigned int arg4);
int PopFlightViewport(void);
void Blit16ToFlightSurface(uint8_t* sourceBase, uint16_t transparentColorIndex, uint16_t sourceX,
						   uint16_t sourceY, uint16_t destinationX, uint16_t destinationY,
						   uint16_t widthPixels, uint16_t heightPixels, uint16_t sourcePitch);
void FlightSw_CopyFramebufferRectToBuffer(uint8_t* dstPixels, uint16_t srcX, uint16_t srcY, uint16_t dstX,
										  uint16_t dstY, uint16_t widthPixels, uint16_t heightPixels,
										  uint16_t dstPitchBytes);
void FlightSw_DrawHorizontalColorSpan(int xStart, int xEnd, int y, uint8_t colorIndex);
void FlightSw_CopyViewportSpanMaskRle(const uint8_t* encodedMask, uint16_t width, uint16_t height,
									  int16_t mirrorHorizontal);
extern uint8_t* g_flightAuxBuffer;
extern uint16_t g_viewportSpanMaskOffset;
extern int g_flightSwRotSpriteCoeffCacheValid;

void FlightSw_BuildFullViewportSpanMaskRle(uint16_t width, unsigned int height);
int32_t FlightSw_ComputePixelOffset(int x, int y);
void FlightSw_BlitSpriteRle(uint8_t* rleData, int x, int y, int endMarker, int mirror);
void FlightSw_BlitSpriteRleFaded(uint8_t* rleData, int x, int y, int endMarker, int8_t paletteShift,
								 int16_t fadeAmount);
void FlightSw_BlitSpriteRleImpl(uint8_t* rleData, int16_t x, int16_t y, int endMarker, int mirror, char mode,
								int16_t fadeAmount);
void FlightSw_BlitMapIconRle16bpp(uint8_t* rleData, int x, int y, int transparentIndex, int mirror);
void FlightSw_DrawPixel(uint16_t x, uint16_t y, int8_t colorIndex);
void FlightSw_FillClipRect(void);
void FlightSw_FillRectOrBorder(uint16_t borderThickness);
void FlightSw_FillRectClipped(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t borderThickness);
void FlightSw_SaveScreenRect(uint16_t* buffer, int x, int y, int16_t width, int height);
void FlightSw_RestoreScreenRect(uint16_t* buffer, int x, int y, int16_t width, int height);
void FlightSw_DrawPointArray(uint16_t* points, int16_t count);
void FlightSw_DrawPointArrayMasked(uint16_t* points, int16_t count);
void FlightSw_DrawRadarTargetMarker(void);
void FlightSw_RestoreRadarTargetMarker(void);
uint16_t FlightSw_DrawCrossMarker(uint16_t x, uint16_t y, uint8_t colorIndex);
uint16_t FlightSw_RestoreCrossMarker(uint16_t x, uint16_t y);
void FlightSw_DrawLine(int x1, int y1, int x2, int y2, uint8_t colorIdx);
int16_t FlightSw_LookupSpriteSineQ15(int16_t angle);
void FlightSw_BlitPreparedRotatedSpriteSpans(uint8_t* pDst, int rowSkipBytes, int startX, int startY,
											 int endX, int endY);

#ifdef __cplusplus
}
#endif

#endif
