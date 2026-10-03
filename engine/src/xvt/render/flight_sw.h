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
extern unsigned int g_vesaPageSizeBytes;
extern unsigned int g_vesaGrainsPerPage;
extern uint8_t *g_flightSwFramebufferBase;
extern int g_flightViewportInsetX;
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
extern uint8_t g_flightBackgroundColorIndex;
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
extern uint16_t g_flightFillRectBottom16bpp;
extern uint16_t g_flightFillRectRight16bpp;
extern uint16_t g_flightFillRectLeft16bpp;
extern uint16_t g_flightFillRectTop16bpp;
extern int g_flightFillRectCurrentY16bpp;
extern int g_flightFillRectRemainingRows16bpp;
extern int32_t g_flightFillRectCurrentY8bpp;
extern uint16_t g_flightFillRectTop8bpp;
extern uint16_t g_flightFillRectBottom8bpp;
extern uint16_t g_flightFillRectRight8bpp;
extern uint16_t g_flightFillRectLeft8bpp;
extern unsigned int g_flightFillRectRemainingRows8bpp;

/* One point of a rotated sprite's edge, in pixels from the edge's start. */
struct FlightSwRotSpriteEdgePoint {
	int16_t x; /* Steps along x. */
	int16_t y; /* Steps along y. */
};

/* The tables FlightSw_BuildSpriteRotationCoeffs makes for one rotation angle,
 * with which the software renderer draws rotated sprites. The first five
 * fields are also read as an array of five 16-bit values by
 * FlightSw_RotateSpritePoint. */
struct FlightSwRotSpriteCoeffState {
	/* The angle, 65,536 units to the circle. */
	uint16_t rotationAngle;
	/* The sine's magnitude for the angle folded into a quarter turn, as
	 * FlightSw_LookupSpriteSineMagnitudeQ16 returns it (65536 for 1). */
	int16_t sinMagnitudeQ16;
	/* The 0x8000 bit of the angle: set when the sine is negative. */
	uint16_t sinSignMask;
	/* The cosine's magnitude, in the same form as sinMagnitudeQ16. */
	int16_t cosMagnitudeQ16;
	/* The 0x8000 bit of the angle plus 0x4000: set when the cosine is
	 * negative. */
	uint16_t cosSignMask;
	/* The cosine's magnitude for the edge's angle (the folded angle, or a
	 * quarter turn less it with primaryAxisSwap), in the same form. */
	uint16_t primaryCosMagnitudeQ16;
	/* 0x80000000 / primaryCosMagnitudeQ16, rescaled by g_projAspectY /
	 * 65536 with primaryAxisSwap when pixels are not square; nothing reads
	 * it. */
	uint16_t primaryStepReciprocal;
	/* Edge points: g_flightSwRotSpriteViewportWidth without
	 * primaryAxisSwap, g_flightSwRotSpriteViewportHeight with it. */
	uint16_t scanCount;
	uint16_t flipY; /* 1 when the angle is 0x8000 or more, else 0. */
	/* 2 when the angle is from 0x4000 up to 0xC000, else 0. */
	uint16_t flipX;
	uint16_t flipCount; /* (flipX >> 1) + flipY: 0, 1 or 2. */
	/* primaryAxisSwap | flipY | flipX, 0 to 7: picks the octant functions
	 * that walk the sprite. */
	uint16_t octant;
	/* 4 when the folded angle is g_flightSwRotSpriteAxisSwapThresholdAngle
	 * or more, so the edge steps along y; else 0. */
	uint16_t primaryAxisSwap;
	/* The same test for the angle a quarter turn on. */
	uint16_t secondaryAxisSwap;
	/* What FlightSw_AdvanceRotSpriteSecondaryScale adds to
	 * g_flightSwRotSpriteSecondaryScaleAccum each step. */
	uint16_t secondaryScaleLow;
	/* Nonzero: each step moves the span base by 1, or 2 on a carry from
	 * that sum. 0: only a carry moves it, by 1. */
	uint16_t secondaryScaleHigh;
	int16_t firstEdgeX; /* x of the edge's first point. */
	int16_t lastEdgeX;  /* x of the edge's last point. */
	/* g_flightSwRotSpriteViewportMaxY less the first point's y. */
	int16_t firstEdgeScreenY;
	/* g_flightSwRotSpriteViewportMaxY less the last point's y. */
	int16_t lastEdgeScreenY;
	int16_t firstEdgeY; /* y of the edge's first point. */
	int16_t lastEdgeY;  /* y of the edge's last point. */
	/* Entry 0: how far the last point lies from the first on x and on y,
	 * as magnitudes. Entries 1 to scanCount: the edge's points from (0,
	 * 0), one step along the main axis each and the cross axis following
	 * the edge's slope. */
	FlightSwRotSpriteEdgePoint edgePointsWithPredecessor[1601];
	/* A step factor in 256ths that FlightSw_PrepareRotatedSpriteScaleState
	 * folds into the vertical step. */
	uint16_t secondaryStepByte;
	uint16_t runLengthCount; /* Entries in runLengths. */
	/* Lengths of the runs of edge points that share a cross-axis
	 * coordinate. */
	uint16_t runLengths[1600];
	/* Where the walk's first line starts in the destination buffer. */
	void *destLinePtr;
	/* Byte offset of each edge point from a line's start, flips applied:
	 * x times bytes per pixel, plus y times
	 * g_flightSwRotSpriteDestPitchBytes when g_flightSwRotSpriteDestYMode
	 * is positive, else minus it. */
	int spanOffsets[1600];
	/* g_flightSwRotSpriteDestPitchBytes, negated when
	 * g_flightSwRotSpriteDestYMode is positive. */
	int destPitchDelta;
};

/* One pixel of the radar target marker, relative to the marker's point. */
struct FlightRadarMarkerOffset {
	int8_t x; /* Columns right. */
	int8_t y; /* Rows down. */
};

/* One pixel of the cross marker, relative to its center. */
struct FlightSwMarkerOffset {
	int8_t dx; /* Columns right. */
	int8_t dy; /* Rows down. */
};

/* The scale of a rotated sprite on screen, from
 * FlightSw_PrepareRotatedSpriteScaleState. Steps are in 256ths of a pixel
 * per texel, split into bytes. */
struct FlightSwRotSpriteScaleState {
	/* The sprite's screen size, 256 for one pixel per texel. */
	uint16_t screenScale;
	/* Horizontal step the step tables were last built for, low byte. */
	uint8_t cachedStepLowByte;
	uint8_t cachedStepHighByte; /* Its high byte. */
	/* (screenScale * primaryCosMagnitudeQ16) >> 16, times aspectScaleY >> 8
	 * with primaryAxisSwap: low byte. */
	uint8_t horizontalStepLowByte;
	uint8_t horizontalStepHighByte; /* Its high byte. */
	/* Destination lines per sprite row: the horizontal base step plus its
	 * secondaryStepByte 256ths, times inverseAspectScaleY >> 8 without
	 * secondaryAxisSwap; low byte. */
	uint8_t verticalStepLowByte;
	uint8_t verticalStepHighByte; /* Its high byte. */
	/* Entry n: n + 1 horizontal steps, fraction part, in 65536ths of a
	 * pixel. */
	uint16_t lowWordStepTable[256];
	/* Entry n: n + 1 horizontal steps, whole pixels. */
	uint16_t highWordStepTable[256];
	/* 256 with square pixels, else 233: y scale in 256ths. */
	uint16_t aspectScaleY;
	/* 256 with square pixels, else 282. */
	uint16_t inverseAspectScaleY;
};

/* One run of a rotated sprite's row, scaled, ready to draw along the edge. */
struct FlightSwRotSpriteSpanRun {
	/* Edge position of its first pixel, before g_flightSwRotSpriteSpanBaseX
	 * is added. */
	int startX;
	/* Sprite color index, drawn through the sprite palette. */
	int colorIndex;
	int length; /* Pixels. */
};

extern FlightSwRotSpriteSpanRun g_flightSwRotSpriteSpanRuns[512];
extern FlightSwRotSpriteScaleState g_flightSwRotSpriteScaleState;
extern uint8_t *g_flightSwRotSpriteDestLinePtr;
extern int16_t g_flightSwRotSpriteSkipSecondaryScaleStep;
extern uint16_t g_flightSwRotSpriteSecondaryScaleAccum;
extern int16_t g_flightSwRotSpriteClipMinX;
extern int16_t g_flightSwRotSpriteSpanBaseX;
extern int16_t g_flightSwRotSpritePrimaryEdgeX;
extern int16_t g_flightSwRotSpritePrimaryEdgeY;
extern int16_t g_flightSwRotSpriteViewportMaxY;
extern int16_t g_flightSwRotSpriteClipMaxX;
extern FlightSwRotSpriteCoeffState *g_flightSwRotSpriteCoeffs;
extern int g_flightSwRotSpriteSpanRunCountdown;

/* SpritePayload describes the same 44-byte image header as TexLevelImageHeader, under different field
 * names; some code reads one image through both. */
typedef struct SpritePayload {
	uint32_t payloadSize; /* Never read or written through this name. */
	/* Never read or written through this name; TexLevelImageHeader calls
	 * it paletteOffset. */
	uint32_t colorTable24Offset;
	/* Bytes from the header to the encoded image: a
	 * FlightSwRotSpriteDataHeader, then the rows. */
	uint32_t rowDataOffset;
	/* Bytes from the header to the drawing palette: one byte per color for
	 * 8-bit drawing, a low and a high byte for 16-bit. */
	uint32_t displayPaletteOffset;
	uint32_t width;	 /* Texels per row. */
	uint32_t height; /* Rows. */
	int32_t anchorX; /* Never read or written through this name. */
	int32_t anchorY; /* Never read or written through this name. */
	/* Bits of run length in a run byte; the bits above them hold the color
	 * index. */
	int32_t packingMode;
	int32_t bitsPerPixel; /* Never read or written through this name. */
	int32_t colorCount;   /* Colors in the drawing palette. */
} SpritePayload;

void FlightSw_InitFramebuffer(void);
void FlightSw_SetRenderTarget(void *surface, int width, unsigned int height,
			      int pitchBytes);
int FlightSw_GetLineOffset(int line);
int FlightSw_GetLinePitch(void);
int FlightSw_ComputePixelOffset8bpp(int x, int y);
void FlightSw_BlitSpriteRle8bpp(uint8_t *rleData, int x, int y,
				int transparentColorIndex, int mirror);
void FlightSw_BlitSpriteRleFaded8bpp(uint8_t *rleData, int x, int y,
				     int transparentColorIndex,
				     int8_t paletteShift, int16_t fadeAmount);
void FlightSw_BlitSpriteRleImpl8bpp(uint8_t *rleData, int x, int y,
				    int transparentColorIndex, int mirror,
				    char isFaded, int16_t fadeAmount);
void FlightSw_BlitMapIconRle(uint8_t *rleData, int x, int y,
			     int transparentIndex, int mirror);
void FlightSw_DrawPixel8bpp(uint16_t x, uint16_t y, int8_t colorIndex);
void FlightSw_FillClipRect8bpp(void);
void FlightSw_FillRectOrBorder8bpp(uint16_t borderThickness);
void FlightSw_FillRectClipped8bpp(uint16_t x1, uint16_t y1, uint16_t x2,
				  uint16_t y2, uint16_t borderThickness);
void FlightSw_SaveScreenRect8bpp(uint8_t *buffer, int x, int y, int16_t width,
				 int height);
void FlightSw_RestoreScreenRect8bpp(uint8_t *buffer, int x, int y,
				    int16_t width, int height);
void FlightSw_DrawPointArray8bpp(uint16_t *points, int16_t count);
void FlightSw_ErasePointArray8bpp(uint16_t *points, int16_t count);
void FlightSw_DrawRadarTargetMarker8bpp(void);
void FlightSw_RestoreRadarTargetMarker8bpp(void);
uint8_t FlightSw_DrawCrossMarker8bpp(uint16_t x, uint16_t y, uint8_t color);
uint8_t FlightSw_RestoreCrossMarker8bpp(uint16_t x, uint16_t y);
void FlightStarfield_Render(void);
void RtsVga2_SetCurrentPage(uint8_t window, uint16_t page);
void FlightScreenshot_Capture(void);
void FlightSw_DrawLine8bpp(int x1, int y1, int x2, int y2, uint8_t colorIdx);
void FlightSw_DrawRotSpriteSpanRuns8(const FlightSwRotSpriteSpanRun *runs,
				     uint8_t *destBase, const int *spanOffsets);
void FlightSw_DrawClippedRotSpriteSpanRuns8(
	const FlightSwRotSpriteSpanRun *runs, uint8_t *destBase,
	const int *spanOffsets);
void FlightSw_DrawRotSpriteSpanRuns16(const FlightSwRotSpriteSpanRun *runs,
				      uint8_t *destBase,
				      const int *spanOffsets);
void FlightSw_DrawClippedRotSpriteSpanRuns16(
	const FlightSwRotSpriteSpanRun *runs, uint8_t *destBase,
	const int *spanOffsets);
void FlightSw_DrawRotatedSpriteQuad(int16_t screenX, int16_t screenY,
				    uint16_t screenSize, SpritePayload *sprite);
void FlightSw_ClipAndBlitPreparedRotatedSprite(int *cornerCoords);
void FlightSw_PrepareSpriteRotationTables(int16_t rotationAngle,
					  int bytesPerPixel);
int FlightSw_LoadSpritePaletteTables(SpritePayload *sprite);
uint16_t FlightSw_LookupScaledTangent(uint16_t angle, int16_t scalePercent);
void FlightSw_PrepareRotatedSpriteScaleState(
	uint16_t screenSize, FlightSwRotSpriteCoeffState *rotationCoeffs,
	FlightSwRotSpriteScaleState *scaleState);
void FlightSw_RotateSpritePoint(uint16_t *rotationCoeffs,
				FlightSwRotSpriteScaleState *scaleState);
void FlightSw_BuildSpriteRotationCoeffs(uint16_t rotationAngle,
					uint16_t *outCoeffs);
void FlightSw_RasterizePreparedRotatedSprite(uint8_t *spriteData,
					     int packingMode);
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
uint8_t *FlightSw_SetRotatedSpriteDestBuffer(uint8_t *bufferAddress);
unsigned int SetFlightViewport(unsigned int requestedWidth,
			       unsigned int requestedHeight, int viewportMode,
			       unsigned int requestedBaseOffset);
void FlightSw_CopyLegacy8BitViewportToFramebuffer(const uint8_t *srcPixels);
unsigned int PushFlightViewport(uint16_t width, uint16_t height,
				int16_t refreshSpanMask,
				unsigned int baseOffset);
int PopFlightViewport(void);
void FlightSw_BlitRectToFlightSurface(
	uint8_t *sourceBase, uint16_t transparentColorIndex, uint16_t sourceX,
	uint16_t sourceY, uint16_t destinationX, uint16_t destinationY,
	uint16_t widthPixels, uint16_t heightPixels, uint16_t sourcePitch);
void FlightSw_CopyFramebufferRectToBuffer(uint8_t *dstPixels, uint16_t srcX,
					  uint16_t srcY, uint16_t dstX,
					  uint16_t dstY, uint16_t widthPixels,
					  uint16_t heightPixels,
					  uint16_t dstPitchBytes);
void FlightSw_DrawHorizontalColorSpan(int xStart, int xEnd, int y,
				      uint8_t colorIndex);
void FlightSw_CopyViewportSpanMaskRle(const uint8_t *encodedMask,
				      uint16_t width, uint16_t height,
				      int16_t mirrorHorizontal);
extern uint8_t *g_flightAuxBuffer;
extern uint16_t g_viewportSpanMaskOffset;
extern int g_flightSwRotSpriteCoeffCacheValid;

void FlightSw_BuildFullViewportSpanMaskRle(uint16_t width, unsigned int height);
int32_t FlightSw_ComputePixelOffset(int x, int y);
void FlightSw_BlitSpriteRle16bpp(uint8_t *rleData, int x, int y,
				 int transparentColorIndex, int mirror);
void FlightSw_BlitSpriteRleFaded16bpp(uint8_t *rleData, int x, int y,
				      int transparentColorIndex,
				      int8_t paletteShift, int16_t fadeAmount);
void FlightSw_BlitSpriteRleImpl16bpp(uint8_t *rleData, int16_t x, int16_t y,
				     int transparentColorIndex, int mirror,
				     char isFaded, int16_t fadeAmount);
void FlightSw_BlitMapIconRle16bpp(uint8_t *rleData, int x, int y,
				  int transparentIndex, int mirror);
void FlightSw_DrawPixel16bpp(uint16_t x, uint16_t y, int8_t colorIndex);
void FlightSw_FillClipRect16bpp(void);
void FlightSw_FillRectOrBorder16bpp(uint16_t borderThickness);
void FlightSw_FillRectClipped16bpp(uint16_t x1, uint16_t y1, uint16_t x2,
				   uint16_t y2, uint16_t borderThickness);
void FlightSw_SaveScreenRect16bpp(uint16_t *buffer, int x, int y, int16_t width,
				  int height);
void FlightSw_RestoreScreenRect16bpp(uint16_t *buffer, int x, int y,
				     int16_t width, int height);
void FlightSw_DrawPointArray16bpp(uint16_t *points, int16_t count);
void FlightSw_ErasePointArray16bpp(uint16_t *points, int16_t count);
void FlightSw_DrawRadarTargetMarker16bpp(void);
void FlightSw_RestoreRadarTargetMarker16bpp(void);
uint16_t FlightSw_DrawCrossMarker16bpp(uint16_t x, uint16_t y,
				       uint8_t colorIndex);
uint16_t FlightSw_RestoreCrossMarker16bpp(uint16_t x, uint16_t y);
void FlightSw_DrawLine16bpp(int x1, int y1, int x2, int y2, uint8_t colorIdx);
int16_t FlightSw_LookupSpriteSineMagnitudeQ16(int16_t angle);
void FlightSw_BlitPreparedRotatedSpriteSpans(uint8_t *pDst, int rowSkipBytes,
					     int startX, int startY, int endX,
					     int endY);

#ifdef __cplusplus
}
#endif

#endif
