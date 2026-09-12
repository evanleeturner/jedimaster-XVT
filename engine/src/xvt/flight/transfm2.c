#include "xvt/flight/transfm2.h"

#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/render/renderer.h"

// GLOBAL: XVT 0x9FE7F4
int g_camMatR2_Y = 0;
// GLOBAL: XVT 0xA00480
int g_camMatR1_Z = 0;
// GLOBAL: XVT 0xA00488
int g_camMatR0_Z = 0;
// GLOBAL: XVT 0xA00490
int g_camMatR2_Z = 0;
// GLOBAL: XVT 0xA004A4
int g_camMatR1_X = 0;
// GLOBAL: XVT 0xA004A8
int g_camMatR0_X = 0;
// GLOBAL: XVT 0xA004B0
int g_camMatR2_X = 0;
// GLOBAL: XVT 0xA004B8
int g_camMatR1_Y = 0;
// GLOBAL: XVT 0xA004CC
int g_camMatR0_Y = 0;
// GLOBAL: XVT 0x9A8E2C
int viewX = 0;
// GLOBAL: XVT 0x9A8E30
int viewY = 0;
// GLOBAL: XVT 0x9A8E34
int depthZ = 0;
// GLOBAL: XVT 0x9A1FF0
int32_t g_projScaleHalfInt = 0;
// GLOBAL: XVT 0x9D8C04
uint8_t perspShift = 0;
// GLOBAL: XVT 0xA080F4
uint16_t g_flightVpCenterX = 0;
// GLOBAL: XVT 0x9ECC44
uint16_t g_projAspectY = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x427390
int TRANSFM2_clipobjecteyez(int x, int y, int z) {
	int negativeDepth;
	int interpolationDelta;
	int currentX;
	int currentY;

	negativeDepth = (int)(0u - (uint32_t)depthZ);
	currentX = viewX;
	if (currentX < x) {
		interpolationDelta = MATH2_ABoverC32(negativeDepth, (int)((uint32_t)x - (uint32_t)currentX),
											 (int)((uint32_t)z + (uint32_t)negativeDepth));
		viewX = (int)((uint32_t)viewX + (uint32_t)interpolationDelta);
	} else {
		interpolationDelta = MATH2_ABoverC32(negativeDepth, (int)((uint32_t)currentX - (uint32_t)x),
											 (int)((uint32_t)z + (uint32_t)negativeDepth));
		viewX = (int)((uint32_t)viewX - (uint32_t)interpolationDelta);
	}

	currentY = viewY;
	if (currentY < y) {
		interpolationDelta = MATH2_ABoverC32(negativeDepth, (int)((uint32_t)y - (uint32_t)currentY),
											 (int)((uint32_t)z + (uint32_t)negativeDepth));
		viewY = (int)((uint32_t)viewY + (uint32_t)interpolationDelta);
	} else {
		interpolationDelta = MATH2_ABoverC32(negativeDepth, (int)((uint32_t)currentY - (uint32_t)y),
											 (int)((uint32_t)z + (uint32_t)negativeDepth));
		viewY = (int)((uint32_t)viewY - (uint32_t)interpolationDelta);
	}

	depthZ = 1;
	return interpolationDelta;
}

// FUNCTION: XVT 0x427420
int TRANSFM2_CamMatDotRow0(int x, int y, int z) {
	return Math_Dot3Q15(g_camMatR0_X, g_camMatR0_Y, g_camMatR0_Z, x, y, z);
}

// FUNCTION: XVT 0x427490
int TRANSFM2_CamMatDotRow1(int x, int y, int z) {
	return Math_Dot3Q15(g_camMatR1_X, g_camMatR1_Y, g_camMatR1_Z, x, y, z);
}

// FUNCTION: XVT 0x427500
int TRANSFM2_CamMatDotRow2(int x, int y, int z) {
	return Math_Dot3Q15(g_camMatR2_X, g_camMatR2_Y, g_camMatR2_Z, x, y, z);
}

// FUNCTION: XVT 0x427570
int TRANSFM2_ProjectScreenX(int viewX, int viewZ) {
	return TRANSFM2_ProjectScreenXFixedPoint(viewX, (unsigned int)viewZ);
}

// FUNCTION: XVT 0x427590
int TRANSFM2_ProjectScreenY(int viewY, int viewZ) {
	return TRANSFM2_ProjectScreenYFixedPoint(viewY, (unsigned int)viewZ);
}

// FUNCTION: XVT 0x4275B0
int TRANSFM2_ProjectScreenXFixedPoint(int viewX, unsigned int depth) {
	if (viewX < 0) {
		uint64_t numerator;

		numerator = (uint64_t)(0u - (uint32_t)viewX) * (1u << (perspShift & 31)) + (uint32_t)g_projScaleHalfInt;
		if ((numerator & ~(uint64_t)UINT32_MAX) >= ((uint64_t)depth << 32))
			viewX = 0x7FFFFF00;
		else
			viewX = (int)(uint32_t)(numerator / depth);
		viewX = (int)(0u - (uint32_t)viewX);
	} else {
		uint64_t numerator;

		numerator = (uint64_t)(uint32_t)viewX * (1u << (perspShift & 31)) + (uint32_t)g_projScaleHalfInt;
		if ((numerator & ~(uint64_t)UINT32_MAX) >= ((uint64_t)depth << 32))
			viewX = 0x7FFFFF00;
		else
			viewX = (int)(uint32_t)(numerator / depth);
	}

	return (int)((uint32_t)g_flightVpCenterX + (uint32_t)viewX);
}

// FUNCTION: XVT 0x427650
int TRANSFM2_ProjectScreenYFixedPoint(int viewY, unsigned int depth) {
	uint64_t numerator;
	uint32_t quotient;
	int projectedOffset;

	if (viewY < 0) {
		numerator = (uint64_t)(0u - (uint32_t)viewY) << perspShift;
		numerator += (uint32_t)g_projScaleHalfInt;
		if (((uint32_t*)&numerator)[1] < depth) {
			quotient = (uint32_t)(numerator / depth);
		} else {
			quotient = 0x7FFFFF00u;
		}
		projectedOffset = (int)(0u - quotient);
	} else {
		numerator = (uint64_t)(uint32_t)viewY << perspShift;
		numerator += (uint32_t)g_projScaleHalfInt;
		if (((uint32_t*)&numerator)[1] < depth) {
			projectedOffset = (int)(uint32_t)(numerator / depth);
		} else {
			projectedOffset = 0x7FFFFF00;
		}
	}

	if (g_projAspectY != 0) {
		if (projectedOffset < 0) {
			projectedOffset = -(int)MATH2_longfraction(0u - (uint32_t)projectedOffset, g_projAspectY);
		} else {
			projectedOffset = (int)MATH2_longfraction((unsigned int)projectedOffset, g_projAspectY);
		}
	}

	projectedOffset = (int)((uint32_t)projectedOffset + g_flightVpCenterY);
	projectedOffset = (int)((uint32_t)projectedOffset + (uint32_t)g_projOffsetY);
	return projectedOffset;
}
