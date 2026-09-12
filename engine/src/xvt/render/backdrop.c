#include "xvt/render/backdrop.h"

#include "xvt/assets/object_type.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_quad.h"
#include "xvt/render/renderer.h"
#include "xvt/render/tex_level.h"
#include "xvt/util/game_rand.h"
#include "xvt/util/memory.h"

// GLOBAL: XVT 0x5234F0
uint8_t g_backdropModelTypes[64] = { 0 };
// GLOBAL: XVT 0x523530
uint8_t g_backdropPackedDirections[64] = { 0 };
// GLOBAL: XVT 0x523570
uint16_t g_backdropPositiveYCount = 0;
// GLOBAL: XVT 0x523574
uint16_t g_backdropNegativeYCount = 0;
// GLOBAL: XVT 0x523578
uint16_t g_backdropPositiveZCount = 0;
// GLOBAL: XVT 0x52357C
uint16_t g_backdropNegativeZCount = 0;
// GLOBAL: XVT 0x523580
uint16_t g_backdropPositiveXCount = 0;
// GLOBAL: XVT 0x523584
uint16_t g_backdropNegativeXCount = 0;
// GLOBAL: XVT 0x9A8DD0
int32_t g_backdropCamR1XSteps[16] = { 0 };
// GLOBAL: XVT 0x9D1270
int32_t g_backdropCamR2XSteps[16] = { 0 };
// GLOBAL: XVT 0x9D80D0
int32_t g_backdropCamR0XSteps[16] = { 0 };
// GLOBAL: XVT 0x9E9600
int32_t g_backdropCamR1YSteps[16] = { 0 };
// GLOBAL: XVT 0x9EC480
int32_t g_backdropCamR2YSteps[16] = { 0 };
// GLOBAL: XVT 0x9FE740
int32_t g_backdropCamR0YSteps[16] = { 0 };
// GLOBAL: XVT 0xA004E0
int32_t g_backdropCamR1ZSteps[16] = { 0 };
// GLOBAL: XVT 0xA07C80
int32_t g_backdropCamR2ZSteps[16] = { 0 };
// GLOBAL: XVT 0xA08250
int32_t g_backdropCamR0ZSteps[16] = { 0 };

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x420110
void Backdrop_DrawModelTexQuadAtScreen(int modelType, int screenX, int screenY, int angle) {
	const uint8_t* modelData;
	const TexLevelHeader* textureHeader;
	SpritePayload* sprite;
	uint16_t softwareAngle;

	g_flightSwRotSpriteSpanRunsEnabled = 1;
	g_camRelWorldZ = 0x100000;
	depthZ = 0x7FFFFFFF;
	modelData = (const uint8_t*)Memory_LockHandle(g_modelTypeTable[modelType].curTexLevel);
	Memory_UnlockHandle(g_modelTypeTable[modelType].curTexLevel);
	textureHeader = (const TexLevelHeader*)modelData;
	sprite =
		(SpritePayload*)(modelData + *(const uint32_t*)(modelData + textureHeader->imageOffsetTableOffset));
	if (g_useHardware3D != 0)
		RenderQuad_DrawRotatedSprite(angle, screenX, screenY, 256, sprite);
	else {
		softwareAngle = (uint16_t)angle;
		FlightSw_PrepareSpriteRotationTables(softwareAngle, FLIGHT_SW_16BPP_BYTES_PER_PIXEL);
		FlightSw_BuildSpriteTintRemapTables(sprite);
		FlightSw_DrawRotatedSpriteQuad((int16_t)screenX, (int16_t)screenY, 256, sprite);
	}
}

// FUNCTION: XVT 0x426080
void Backdrop_RenderCurrentRegion(void) {
	int accumR0X;
	int accumR0Y;
	int accumR0Z;
	int accumR1X;
	int accumR1Y;
	int accumR1Z;
	int accumR2X;
	int accumR2Y;
	int accumR2Z;
	int gridBaseR0;
	int gridBaseR1;
	int gridBaseR2;
	int gridRowR0;
	int gridRowR1;
	int gridRowR2;
	int gridValueR0;
	int gridValueR1;
	int gridValueR2;
	int stepIndex;
	int gridX;
	int gridY;
	int gridZ;
	int jitterIndex;
	int16_t angle;
	unsigned int directionIndex;
	int directionCount;
	int lowStepIndex;
	unsigned int highStepIndex;
	int basisX;
	int basisY;
	int basisZ;
	int viewX;
	int viewY;
	int viewZ;
	uint8_t packedDirection;

	enum {
		CAMERA_STEP_COUNT = 16,
		CAMERA_STEP_SHIFT = 5,
		STAR_GRID_SIZE = 5,
		STAR_GRID_RADIUS = 2,
		STAR_JITTER_SHIFT = 7,
		CAMERA_QUARTER_SHIFT = 2,
		DIRECTION_LOW_INDEX_MASK = 0x07,
		DIRECTION_NEGATE_LOW_BIT = 0x08,
		DIRECTION_HIGH_INDEX_MASK = 0x70,
		DIRECTION_NEGATE_HIGH_BIT = 0x80
	};

	accumR0X = 0;
	accumR0Y = 0;
	accumR0Z = 0;
	accumR1X = 0;
	accumR1Y = 0;
	accumR1Z = 0;
	accumR2X = 0;
	accumR2Y = 0;
	accumR2Z = 0;
	for (stepIndex = 0; stepIndex < CAMERA_STEP_COUNT; ++stepIndex) {
		g_backdropCamR0XSteps[stepIndex] = accumR0X >> CAMERA_STEP_SHIFT;
		g_backdropCamR1XSteps[stepIndex] = accumR1X >> CAMERA_STEP_SHIFT;
		g_backdropCamR2XSteps[stepIndex] = accumR2X >> CAMERA_STEP_SHIFT;
		g_backdropCamR0YSteps[stepIndex] = accumR0Y >> CAMERA_STEP_SHIFT;
		g_backdropCamR1YSteps[stepIndex] = accumR1Y >> CAMERA_STEP_SHIFT;
		g_backdropCamR2YSteps[stepIndex] = accumR2Y >> CAMERA_STEP_SHIFT;
		g_backdropCamR0ZSteps[stepIndex] = accumR0Z >> CAMERA_STEP_SHIFT;
		g_backdropCamR1ZSteps[stepIndex] = accumR1Z >> CAMERA_STEP_SHIFT;
		g_backdropCamR2ZSteps[stepIndex] = accumR2Z >> CAMERA_STEP_SHIFT;
		accumR0X += g_camMatR0_X;
		accumR1X += g_camMatR1_X;
		accumR2X += g_camMatR2_X;
		accumR0Y += g_camMatR0_Y;
		accumR1Y += g_camMatR1_Y;
		accumR2Y += g_camMatR2_Y;
		accumR0Z += g_camMatR0_Z;
		accumR1Z += g_camMatR1_Z;
		accumR2Z += g_camMatR2_Z;
	}

	gridBaseR2 = -STAR_GRID_RADIUS * (g_camMatR2_X + g_camMatR2_Y + g_camMatR2_Z);
	gridBaseR1 = -STAR_GRID_RADIUS * (g_camMatR1_X + g_camMatR1_Y + g_camMatR1_Z);
	gridBaseR0 = -STAR_GRID_RADIUS * (g_camMatR0_X + g_camMatR0_Y + g_camMatR0_Z);
	jitterIndex = 0;
	for (gridX = 0; gridX < STAR_GRID_SIZE; ++gridX) {
		gridRowR0 = gridBaseR0;
		gridRowR1 = gridBaseR1;
		gridRowR2 = gridBaseR2;
		for (gridY = 0; gridY < STAR_GRID_SIZE; ++gridY) {
			gridValueR0 = gridRowR0;
			gridValueR1 = gridRowR1;
			gridValueR2 = gridRowR2;
			for (gridZ = 0; gridZ < STAR_GRID_SIZE; ++gridZ) {
				g_starfieldJitterX[jitterIndex] = gridValueR0 >> STAR_JITTER_SHIFT;
				g_starfieldJitterY[jitterIndex] = gridValueR1 >> STAR_JITTER_SHIFT;
				g_starfieldJitterZ[jitterIndex] = gridValueR2 >> STAR_JITTER_SHIFT;
				++jitterIndex;
				gridValueR0 += g_camMatR0_Z;
				gridValueR2 += g_camMatR2_Z;
				gridValueR1 += g_camMatR1_Z;
			}
			gridRowR0 += g_camMatR0_Y;
			gridRowR1 += g_camMatR1_Y;
			gridRowR2 += g_camMatR2_Y;
		}
		gridBaseR2 += g_camMatR2_X;
		gridBaseR1 += g_camMatR1_X;
		gridBaseR0 += g_camMatR0_X;
	}

	if (g_backdropsEnabled == 0)
		return;

	directionIndex = 0;
	angle = (int16_t)-trig2_arctan(g_camMatR1_X, g_camMatR0_X);
	if (g_camMatR2_Y >= 0) {
		for (directionCount = g_backdropPositiveYCount; directionCount-- != 0;) {
			packedDirection = g_backdropPackedDirections[directionIndex++];
			lowStepIndex = (uint8_t)packedDirection & DIRECTION_LOW_INDEX_MASK;
			basisY = g_backdropCamR1XSteps[lowStepIndex];
			basisZ = g_backdropCamR2XSteps[lowStepIndex];
			basisX = g_backdropCamR0XSteps[lowStepIndex];
			if (((uint8_t)packedDirection & DIRECTION_NEGATE_LOW_BIT) != 0) {
				basisX = -basisX;
				basisY = -basisY;
				basisZ = -basisZ;
			}
			if ((packedDirection & DIRECTION_NEGATE_HIGH_BIT) != 0) {
				highStepIndex = (packedDirection & DIRECTION_HIGH_INDEX_MASK) >> 4;
				viewX = basisX - g_backdropCamR0ZSteps[highStepIndex];
				viewY = basisY - g_backdropCamR1ZSteps[highStepIndex];
				viewZ = basisZ - g_backdropCamR2ZSteps[highStepIndex];
			} else {
				highStepIndex = (packedDirection & DIRECTION_HIGH_INDEX_MASK) >> 4;
				viewX = basisX + g_backdropCamR0ZSteps[highStepIndex];
				viewY = basisY + g_backdropCamR1ZSteps[highStepIndex];
				viewZ = basisZ + g_backdropCamR2ZSteps[highStepIndex];
			}
			viewX += g_camMatR0_Y >> CAMERA_QUARTER_SHIFT;
			viewY += g_camMatR1_Y >> CAMERA_QUARTER_SHIFT;
			viewZ += g_camMatR2_Y >> CAMERA_QUARTER_SHIFT;
			if (viewZ >= 0)
				Backdrop_ProjectAndDrawScreenQuad(viewX, viewY, viewZ, angle, directionIndex);
		}
		directionIndex += g_backdropNegativeYCount;
	} else {
		directionIndex += g_backdropPositiveYCount;
		for (directionCount = g_backdropNegativeYCount; directionCount-- != 0;) {
			packedDirection = g_backdropPackedDirections[directionIndex++];
			lowStepIndex = (uint8_t)packedDirection & DIRECTION_LOW_INDEX_MASK;
			basisX = g_backdropCamR0XSteps[lowStepIndex];
			basisY = g_backdropCamR1XSteps[lowStepIndex];
			basisZ = g_backdropCamR2XSteps[lowStepIndex];
			if (((uint8_t)packedDirection & DIRECTION_NEGATE_LOW_BIT) != 0) {
				basisX = -basisX;
				basisY = -basisY;
				basisZ = -basisZ;
			}
			if ((packedDirection & DIRECTION_NEGATE_HIGH_BIT) != 0) {
				highStepIndex = (packedDirection & DIRECTION_HIGH_INDEX_MASK) >> 4;
				viewX = basisX - g_backdropCamR0ZSteps[highStepIndex];
				viewY = basisY - g_backdropCamR1ZSteps[highStepIndex];
				viewZ = basisZ - g_backdropCamR2ZSteps[highStepIndex];
			} else {
				highStepIndex = (packedDirection & DIRECTION_HIGH_INDEX_MASK) >> 4;
				viewX = basisX + g_backdropCamR0ZSteps[highStepIndex];
				viewY = basisY + g_backdropCamR1ZSteps[highStepIndex];
				viewZ = basisZ + g_backdropCamR2ZSteps[highStepIndex];
			}
			viewX -= g_camMatR0_Y >> CAMERA_QUARTER_SHIFT;
			viewY -= g_camMatR1_Y >> CAMERA_QUARTER_SHIFT;
			viewZ -= g_camMatR2_Y >> CAMERA_QUARTER_SHIFT;
			if (viewZ >= 0)
				Backdrop_ProjectAndDrawScreenQuad(viewX, viewY, viewZ, angle, directionIndex);
		}
	}

	angle = (int16_t)-trig2_arctan(g_camMatR1_Y, g_camMatR0_Y);
	if (g_camMatR2_X >= 0) {
		for (directionCount = g_backdropPositiveXCount; directionCount-- != 0;) {
			packedDirection = g_backdropPackedDirections[directionIndex++];
			lowStepIndex = (uint8_t)packedDirection & DIRECTION_LOW_INDEX_MASK;
			basisX = g_backdropCamR0YSteps[lowStepIndex];
			basisY = g_backdropCamR1YSteps[lowStepIndex];
			basisZ = g_backdropCamR2YSteps[lowStepIndex];
			if (((uint8_t)packedDirection & DIRECTION_NEGATE_LOW_BIT) != 0) {
				basisX = -basisX;
				basisY = -basisY;
				basisZ = -basisZ;
			}
			if ((packedDirection & DIRECTION_NEGATE_HIGH_BIT) != 0) {
				highStepIndex = (packedDirection & DIRECTION_HIGH_INDEX_MASK) >> 4;
				viewX = basisX - g_backdropCamR0ZSteps[highStepIndex];
				viewY = basisY - g_backdropCamR1ZSteps[highStepIndex];
				viewZ = basisZ - g_backdropCamR2ZSteps[highStepIndex];
			} else {
				highStepIndex = (packedDirection & DIRECTION_HIGH_INDEX_MASK) >> 4;
				viewX = basisX + g_backdropCamR0ZSteps[highStepIndex];
				viewY = basisY + g_backdropCamR1ZSteps[highStepIndex];
				viewZ = basisZ + g_backdropCamR2ZSteps[highStepIndex];
			}
			viewX += g_camMatR0_X >> CAMERA_QUARTER_SHIFT;
			viewY += g_camMatR1_X >> CAMERA_QUARTER_SHIFT;
			viewZ += g_camMatR2_X >> CAMERA_QUARTER_SHIFT;
			if (viewZ >= 0)
				Backdrop_ProjectAndDrawScreenQuad(viewX, viewY, viewZ, angle, directionIndex);
		}
		directionIndex += g_backdropNegativeXCount;
	} else {
		directionIndex += g_backdropPositiveXCount;
		for (directionCount = g_backdropNegativeXCount; directionCount-- != 0;) {
			packedDirection = g_backdropPackedDirections[directionIndex++];
			lowStepIndex = (uint8_t)packedDirection & DIRECTION_LOW_INDEX_MASK;
			basisX = g_backdropCamR0YSteps[lowStepIndex];
			basisY = g_backdropCamR1YSteps[lowStepIndex];
			basisZ = g_backdropCamR2YSteps[lowStepIndex];
			if (((uint8_t)packedDirection & DIRECTION_NEGATE_LOW_BIT) != 0) {
				basisX = -basisX;
				basisY = -basisY;
				basisZ = -basisZ;
			}
			if ((packedDirection & DIRECTION_NEGATE_HIGH_BIT) != 0) {
				highStepIndex = (packedDirection & DIRECTION_HIGH_INDEX_MASK) >> 4;
				viewX = basisX - g_backdropCamR0ZSteps[highStepIndex];
				viewY = basisY - g_backdropCamR1ZSteps[highStepIndex];
				viewZ = basisZ - g_backdropCamR2ZSteps[highStepIndex];
			} else {
				highStepIndex = (packedDirection & DIRECTION_HIGH_INDEX_MASK) >> 4;
				viewX = basisX + g_backdropCamR0ZSteps[highStepIndex];
				viewY = basisY + g_backdropCamR1ZSteps[highStepIndex];
				viewZ = basisZ + g_backdropCamR2ZSteps[highStepIndex];
			}
			viewX -= g_camMatR0_X >> CAMERA_QUARTER_SHIFT;
			viewY -= g_camMatR1_X >> CAMERA_QUARTER_SHIFT;
			viewZ -= g_camMatR2_X >> CAMERA_QUARTER_SHIFT;
			if (viewZ >= 0)
				Backdrop_ProjectAndDrawScreenQuad(viewX, viewY, viewZ, angle, directionIndex);
		}
	}

	angle = (int16_t)-trig2_arctan(g_camMatR1_X, g_camMatR0_X);
	if (g_camMatR2_Z >= 0) {
		for (directionCount = g_backdropPositiveZCount; directionCount-- != 0;) {
			packedDirection = g_backdropPackedDirections[directionIndex++];
			lowStepIndex = (uint8_t)packedDirection & DIRECTION_LOW_INDEX_MASK;
			basisX = g_backdropCamR0YSteps[lowStepIndex];
			basisY = g_backdropCamR1YSteps[lowStepIndex];
			basisZ = g_backdropCamR2YSteps[lowStepIndex];
			if (((uint8_t)packedDirection & DIRECTION_NEGATE_LOW_BIT) != 0) {
				basisX = -basisX;
				basisY = -basisY;
				basisZ = -basisZ;
			}
			if ((packedDirection & DIRECTION_NEGATE_HIGH_BIT) != 0) {
				highStepIndex = (packedDirection & DIRECTION_HIGH_INDEX_MASK) >> 4;
				viewX = basisX - g_backdropCamR0XSteps[highStepIndex];
				viewY = basisY - g_backdropCamR1XSteps[highStepIndex];
				viewZ = basisZ - g_backdropCamR2XSteps[highStepIndex];
			} else {
				highStepIndex = (packedDirection & DIRECTION_HIGH_INDEX_MASK) >> 4;
				viewX = basisX + g_backdropCamR0XSteps[highStepIndex];
				viewY = basisY + g_backdropCamR1XSteps[highStepIndex];
				viewZ = basisZ + g_backdropCamR2XSteps[highStepIndex];
			}
			viewX += g_camMatR0_Z >> CAMERA_QUARTER_SHIFT;
			viewY += g_camMatR1_Z >> CAMERA_QUARTER_SHIFT;
			viewZ += g_camMatR2_Z >> CAMERA_QUARTER_SHIFT;
			if (viewZ >= 0)
				Backdrop_ProjectAndDrawScreenQuad(viewX, viewY, viewZ, angle, directionIndex);
		}
	} else {
		directionIndex += g_backdropPositiveZCount;
		for (directionCount = g_backdropNegativeZCount; directionCount-- != 0;) {
			packedDirection = g_backdropPackedDirections[directionIndex++];
			lowStepIndex = (uint8_t)packedDirection & DIRECTION_LOW_INDEX_MASK;
			basisX = g_backdropCamR0YSteps[lowStepIndex];
			basisY = g_backdropCamR1YSteps[lowStepIndex];
			basisZ = g_backdropCamR2YSteps[lowStepIndex];
			if (((uint8_t)packedDirection & DIRECTION_NEGATE_LOW_BIT) != 0) {
				basisX = -basisX;
				basisY = -basisY;
				basisZ = -basisZ;
			}
			if ((packedDirection & DIRECTION_NEGATE_HIGH_BIT) != 0) {
				highStepIndex = (packedDirection & DIRECTION_HIGH_INDEX_MASK) >> 4;
				viewX = basisX - g_backdropCamR0XSteps[highStepIndex];
				viewY = basisY - g_backdropCamR1XSteps[highStepIndex];
				viewZ = basisZ - g_backdropCamR2XSteps[highStepIndex];
			} else {
				highStepIndex = (packedDirection & DIRECTION_HIGH_INDEX_MASK) >> 4;
				viewX = basisX + g_backdropCamR0XSteps[highStepIndex];
				viewY = basisY + g_backdropCamR1XSteps[highStepIndex];
				viewZ = basisZ + g_backdropCamR2XSteps[highStepIndex];
			}
			viewX -= g_camMatR0_Z >> CAMERA_QUARTER_SHIFT;
			viewY -= g_camMatR1_Z >> CAMERA_QUARTER_SHIFT;
			viewZ -= g_camMatR2_Z >> CAMERA_QUARTER_SHIFT;
			if (viewZ >= 0)
				Backdrop_ProjectAndDrawScreenQuad(viewX, viewY, viewZ, angle, directionIndex);
		}
	}
}

// FUNCTION: XVT 0x426860
void Backdrop_ProjectAndDrawScreenQuad(int viewX, int viewY, int viewZ, int angle, int backdropIndex) {
	int projectedX;
	int projectedY;
	uint32_t projectionScale;

	enum { PROJECTION_WORD_BITS = 32, PROJECTION_SATURATION = 0x7FFFFF00 };

#ifdef XVT_MODERN
	projectionScale = 1u << (perspShift & (PROJECTION_WORD_BITS - 1));
#else
	projectionScale = 1u << perspShift;
#endif

	if (viewX < 0) {
		int projectionDepth;
		int magnitude;
		uint64_t numerator;
		uint32_t quotient;

		projectionDepth = viewZ;
#ifdef XVT_MODERN
		magnitude = (int)(0u - (unsigned int)viewX);
#else
		magnitude = -viewX;
#endif
		if (projectionDepth < magnitude)
			return;
		numerator = (uint64_t)(uint32_t)magnitude * projectionScale + (uint32_t)g_projScaleHalfInt;
#ifdef XVT_MODERN
		if ((uint32_t)(numerator >> PROJECTION_WORD_BITS) < (uint32_t)projectionDepth)
#else
		if (((const uint32_t*)&numerator)[1] < (uint32_t)projectionDepth)
#endif
			quotient = (uint32_t)(numerator / (uint32_t)projectionDepth);
		else
			quotient = PROJECTION_SATURATION;
		projectedX = -(int)quotient;
	} else {
		int projectionDepth;
		int magnitude;
		uint64_t numerator;

		projectionDepth = viewZ;
		magnitude = viewX;
		if (projectionDepth < magnitude)
			return;
		numerator = (uint64_t)(uint32_t)magnitude * projectionScale + (uint32_t)g_projScaleHalfInt;
#ifdef XVT_MODERN
		if ((uint32_t)(numerator >> PROJECTION_WORD_BITS) < (uint32_t)projectionDepth)
#else
		if (((const uint32_t*)&numerator)[1] < (uint32_t)projectionDepth)
#endif
			projectedX = (int)(numerator / (uint32_t)projectionDepth);
		else
			projectedX = PROJECTION_SATURATION;
	}

	if (viewY < 0) {
		int projectionDepth;
		int magnitude;
		uint64_t numerator;
		uint32_t quotient;

		projectionDepth = viewZ;
#ifdef XVT_MODERN
		magnitude = (int)(0u - (unsigned int)viewY);
#else
		magnitude = -viewY;
#endif
		if (projectionDepth < magnitude)
			return;
		numerator = (uint64_t)(uint32_t)magnitude * projectionScale + (uint32_t)g_projScaleHalfInt;
#ifdef XVT_MODERN
		if ((uint32_t)(numerator >> PROJECTION_WORD_BITS) < (uint32_t)projectionDepth)
#else
		if (((const uint32_t*)&numerator)[1] < (uint32_t)projectionDepth)
#endif
			quotient = (uint32_t)(numerator / (uint32_t)projectionDepth);
		else
			quotient = PROJECTION_SATURATION;
		projectedY = -(int)quotient;
	} else {
		int projectionDepth;
		int magnitude;
		uint64_t numerator;

		projectionDepth = viewZ;
		magnitude = viewY;
		if (projectionDepth < magnitude)
			return;
		numerator = (uint64_t)(uint32_t)magnitude * projectionScale + (uint32_t)g_projScaleHalfInt;
#ifdef XVT_MODERN
		if ((uint32_t)(numerator >> PROJECTION_WORD_BITS) < (uint32_t)projectionDepth)
#else
		if (((const uint32_t*)&numerator)[1] < (uint32_t)projectionDepth)
#endif
			projectedY = (int)(numerator / (uint32_t)projectionDepth);
		else
			projectedY = PROJECTION_SATURATION;
	}
	projectedX += (int)g_flightVpCenterX;
	projectedY += (int)g_flightVpCenterY;
	projectedY += g_projOffsetY;
	Backdrop_DrawModelTexQuadAtScreen(g_backdropModelTypes[backdropIndex - 1], projectedX,
									  (int)g_flightVpHeight - projectedY, angle);
}

// FUNCTION: XVT 0x458ED0
void Backdrop_GenerateDefaultRecords(void) {
	int16_t lowCoordRoll;
	uint16_t lowCoord;
	uint16_t highCoord;
	uint16_t directionRecordIdx;
	uint16_t modelTypeRoll;
	uint8_t modelType;

	g_backdropPositiveYCount = 4;
	g_backdropNegativeYCount = 4;
	directionRecordIdx = 0;
	g_backdropPositiveXCount = 4;
	g_backdropNegativeXCount = 4;
	g_backdropPositiveZCount = 3;
	g_backdropNegativeZCount = 3;
	while (directionRecordIdx < 22) {
		do {
			lowCoordRoll = GameRand() & 14;
			lowCoord = (uint16_t)(lowCoordRoll + 4);
		} while (lowCoord > 12);
		do {
			highCoord = (uint16_t)((GameRand() & 14) + 4);
		} while (highCoord > 12);
		highCoord <<= 4;
		highCoord += lowCoord;
		g_backdropPackedDirections[directionRecordIdx++] = (uint8_t)highCoord;
	}

	for (lowCoord = 0; lowCoord < 22; lowCoord++) {
		modelTypeRoll = (uint16_t)(GameRand() & 31);
		if (modelTypeRoll < 3) {
			g_backdropModelTypes[lowCoord] = 117;
		} else {
			if (modelTypeRoll < 12) {
				modelType = (uint8_t)(modelTypeRoll / 3 + 117);
			} else {
				modelType = (uint8_t)((modelTypeRoll & 1) + 125);
			}
			g_backdropModelTypes[lowCoord] = modelType;
		}
	}
}
