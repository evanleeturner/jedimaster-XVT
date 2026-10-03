#include "xvt/flight/transfm2.h"

#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/render/renderer.h"

/* Camera matrix, row 2, Y term. The nine g_camMatR globals rotate a world
 * offset from the camera into view space, in Q15 fixed point (32,768 is
 * 1.0): row 0 gives view X (rightward on screen), row 1 view Y (downward on
 * screen), row 2 depth. Two functions write them, all nine together:
 * FVIEW_BuildCameraOrient, and PopFlightViewport, which puts back the copy
 * PushFlightViewport saved. */
// GLOBAL: XVT 0x9FE7F4
int g_camMatR2_Y = 0;
/* Camera matrix, row 1, Z term; see g_camMatR2_Y. */
// GLOBAL: XVT 0xA00480
int g_camMatR1_Z = 0;
/* Camera matrix, row 0, Z term; see g_camMatR2_Y. */
// GLOBAL: XVT 0xA00488
int g_camMatR0_Z = 0;
/* Camera matrix, row 2, Z term; see g_camMatR2_Y. */
// GLOBAL: XVT 0xA00490
int g_camMatR2_Z = 0;
/* Camera matrix, row 1, X term; see g_camMatR2_Y. */
// GLOBAL: XVT 0xA004A4
int g_camMatR1_X = 0;
/* Camera matrix, row 0, X term; see g_camMatR2_Y. */
// GLOBAL: XVT 0xA004A8
int g_camMatR0_X = 0;
/* Camera matrix, row 2, X term; see g_camMatR2_Y. */
// GLOBAL: XVT 0xA004B0
int g_camMatR2_X = 0;
/* Camera matrix, row 1, Y term; see g_camMatR2_Y. */
// GLOBAL: XVT 0xA004B8
int g_camMatR1_Y = 0;
/* Camera matrix, row 0, Y term; see g_camMatR2_Y. */
// GLOBAL: XVT 0xA004CC
int g_camMatR0_Y = 0;
/* View-space X of the point being drawn or tested: the camera-relative
 * world offset dotted with camera row 0. Many functions write it, chiefly
 * FlightView_ComputeObjectViewPosition, the flight map and HUD drawing
 * code, and TRANSFM2_clipobjecteyez. */
// GLOBAL: XVT 0x9A8E2C
int g_viewSpaceX = 0;
/* View-space Y of the point being drawn or tested, written beside
 * g_viewSpaceX by the same functions. */
// GLOBAL: XVT 0x9A8E30
int g_viewSpaceY = 0;
/* Depth of the point being drawn or tested, along camera row 2; 0 or less
 * means at or behind the eye. Many functions write it, chiefly the ones that
 * write g_viewSpaceX; TRANSFM2_clipobjecteyez sets it to 1 and
 * Backdrop_DrawModelTexQuadAtScreen to 0x7FFFFFFF. */
// GLOBAL: XVT 0x9A8E34
int g_viewSpaceDepth = 0;
/* Half of g_projScaleInt, added to the shifted numerator before the divide
 * in the screen projections. FlightDisplay_ConfigureResolutionState sets it
 * to 128 at 320x240 and in an unknown mode, 256 at 640x480 and 480x360;
 * ModelPreview_RenderViewport sets MODEL_PREVIEW_PROJECTION_HALF_SCALE. */
// GLOBAL: XVT 0x9A1FF0
int32_t g_projScaleHalfInt = 0;
/* Left shift applied to a view-space coordinate before it is divided by
 * depth, so the projection scale is 2 to this power. The same two
 * functions that write g_projScaleHalfInt write it: 8 at 320x240, 9 at
 * 640x480 and 480x360, MODEL_PREVIEW_PERSPECTIVE_SHIFT in the model
 * preview. */
// GLOBAL: XVT 0x9D8C04
uint8_t g_perspectiveShift = 0;
/* Column of the viewport's center, counted from the viewport's left edge:
 * half the viewport width. Four functions write it: SetFlightViewport,
 * PushFlightViewport, PopFlightViewport and ModelPreview_RenderViewport. */
// GLOBAL: XVT 0xA080F4
uint16_t g_flightVpCenterX = 0;
/* Vertical scale for TRANSFM2_ProjectScreenYFixedPoint, as a fraction of
 * 65,536; 0 means none. Both writers, FlightDisplay_ConfigureResolutionState
 * and ModelPreview_RenderViewport, set it to 0, so the scaling never runs. */
// GLOBAL: XVT 0x9ECC44
uint16_t g_projAspectY = 0;

/* Moves the point in g_viewSpaceX and g_viewSpaceY, which lies at or behind
 * the eye, along the segment toward the view-space point (x, y, z) to where
 * that segment crosses depth 0, then sets g_viewSpaceDepth to 1 so the point
 * can be projected. Returns the size of the move made in Y; the callers
 * ignore it. Does not check that g_viewSpaceDepth is 0 or less; the flight
 * map code calls it only then. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x427390
int TRANSFM2_clipobjecteyez(int x, int y, int z)
{
	int negativeDepth;
	int interpolationDelta;
	int currentX;
	int currentY;

	negativeDepth = (int)(0u - (uint32_t)g_viewSpaceDepth);
	currentX = g_viewSpaceX;
	if (currentX < x) {
		interpolationDelta = MATH2_ABoverC32(
			negativeDepth, (int)((uint32_t)x - (uint32_t)currentX),
			(int)((uint32_t)z + (uint32_t)negativeDepth));
		g_viewSpaceX = (int)((uint32_t)g_viewSpaceX +
				     (uint32_t)interpolationDelta);
	} else {
		interpolationDelta = MATH2_ABoverC32(
			negativeDepth, (int)((uint32_t)currentX - (uint32_t)x),
			(int)((uint32_t)z + (uint32_t)negativeDepth));
		g_viewSpaceX = (int)((uint32_t)g_viewSpaceX -
				     (uint32_t)interpolationDelta);
	}

	currentY = g_viewSpaceY;
	if (currentY < y) {
		interpolationDelta = MATH2_ABoverC32(
			negativeDepth, (int)((uint32_t)y - (uint32_t)currentY),
			(int)((uint32_t)z + (uint32_t)negativeDepth));
		g_viewSpaceY = (int)((uint32_t)g_viewSpaceY +
				     (uint32_t)interpolationDelta);
	} else {
		interpolationDelta = MATH2_ABoverC32(
			negativeDepth, (int)((uint32_t)currentY - (uint32_t)y),
			(int)((uint32_t)z + (uint32_t)negativeDepth));
		g_viewSpaceY = (int)((uint32_t)g_viewSpaceY -
				     (uint32_t)interpolationDelta);
	}

	g_viewSpaceDepth = 1;
	return interpolationDelta;
}

/* Returns (x, y, z) dotted with camera row 0, Q15: view-space X of a
 * camera-relative offset. */
// FUNCTION: XVT 0x427420
int TRANSFM2_CamMatDotRow0(int x, int y, int z)
{
	return Math_Dot3Q15(g_camMatR0_X, g_camMatR0_Y, g_camMatR0_Z, x, y, z);
}

/* Returns (x, y, z) dotted with camera row 1, Q15: view-space Y. */
// FUNCTION: XVT 0x427490
int TRANSFM2_CamMatDotRow1(int x, int y, int z)
{
	return Math_Dot3Q15(g_camMatR1_X, g_camMatR1_Y, g_camMatR1_Z, x, y, z);
}

/* Returns (x, y, z) dotted with camera row 2, Q15: depth. */
// FUNCTION: XVT 0x427500
int TRANSFM2_CamMatDotRow2(int x, int y, int z)
{
	return Math_Dot3Q15(g_camMatR2_X, g_camMatR2_Y, g_camMatR2_Z, x, y, z);
}

/* Returns TRANSFM2_ProjectScreenXFixedPoint for viewZ read as unsigned. */
// FUNCTION: XVT 0x427570
int TRANSFM2_ProjectScreenX(int viewX, int viewZ)
{
	return TRANSFM2_ProjectScreenXFixedPoint(viewX, (unsigned int)viewZ);
}

/* Returns TRANSFM2_ProjectScreenYFixedPoint for viewZ read as unsigned. */
// FUNCTION: XVT 0x427590
int TRANSFM2_ProjectScreenY(int viewY, int viewZ)
{
	return TRANSFM2_ProjectScreenYFixedPoint(viewY, (unsigned int)viewZ);
}

/* Returns the viewport column of a view-space X at a depth:
 * g_flightVpCenterX plus the size of viewX shifted left by
 * g_perspectiveShift, plus g_projScaleHalfInt, divided by depth, with
 * viewX's sign. When the quotient would not fit in 32 bits, a depth of 0
 * included, the offset is 0x7FFFFF00 with that sign. A negative depth is read
 * as a large unsigned one; callers test depth first. */
// FUNCTION: XVT 0x4275B0
int TRANSFM2_ProjectScreenXFixedPoint(int viewX, unsigned int depth)
{
	if (viewX < 0) {
		uint64_t numerator;

		numerator = (uint64_t)(0u - (uint32_t)viewX) *
				    (1u << (g_perspectiveShift & 31)) +
			    (uint32_t)g_projScaleHalfInt;
		if ((numerator & ~(uint64_t)UINT32_MAX) >=
		    ((uint64_t)depth << 32)) {
			viewX = 0x7FFFFF00;
		} else {
			viewX = (int)(uint32_t)(numerator / depth);
		}
		viewX = (int)(0u - (uint32_t)viewX);
	} else {
		uint64_t numerator;

		numerator = (uint64_t)(uint32_t)viewX *
				    (1u << (g_perspectiveShift & 31)) +
			    (uint32_t)g_projScaleHalfInt;
		if ((numerator & ~(uint64_t)UINT32_MAX) >=
		    ((uint64_t)depth << 32)) {
			viewX = 0x7FFFFF00;
		} else {
			viewX = (int)(uint32_t)(numerator / depth);
		}
	}

	return (int)((uint32_t)g_flightVpCenterX + (uint32_t)viewX);
}

/* Returns the viewport row of a view-space Y at a depth, worked as in
 * TRANSFM2_ProjectScreenXFixedPoint, the offset scaled by g_projAspectY when
 * that is nonzero, plus g_flightVpCenterY and g_projOffsetY. */
// FUNCTION: XVT 0x427650
int TRANSFM2_ProjectScreenYFixedPoint(int viewY, unsigned int depth)
{
	uint64_t numerator;
	uint32_t quotient;
	int projectedOffset;

	if (viewY < 0) {
		numerator = (uint64_t)(0u - (uint32_t)viewY)
			    << g_perspectiveShift;
		numerator += (uint32_t)g_projScaleHalfInt;
		if (((uint32_t *)&numerator)[1] < depth) {
			quotient = (uint32_t)(numerator / depth);
		} else {
			quotient = 0x7FFFFF00u;
		}
		projectedOffset = (int)(0u - quotient);
	} else {
		numerator = (uint64_t)(uint32_t)viewY << g_perspectiveShift;
		numerator += (uint32_t)g_projScaleHalfInt;
		if (((uint32_t *)&numerator)[1] < depth) {
			projectedOffset = (int)(uint32_t)(numerator / depth);
		} else {
			projectedOffset = 0x7FFFFF00;
		}
	}

	if (g_projAspectY != 0) {
		if (projectedOffset < 0) {
			projectedOffset = -(int)MATH2_longfraction(
				0u - (uint32_t)projectedOffset, g_projAspectY);
		} else {
			projectedOffset = (int)MATH2_longfraction(
				(unsigned int)projectedOffset, g_projAspectY);
		}
	}

	projectedOffset = (int)((uint32_t)projectedOffset + g_flightVpCenterY);
	projectedOffset =
		(int)((uint32_t)projectedOffset + (uint32_t)g_projOffsetY);
	return projectedOffset;
}
