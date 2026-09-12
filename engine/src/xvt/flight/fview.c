#include "xvt/flight/fview.h"
#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_camera.h"
#endif

#include "xvt/assets/model_preview.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/render/renderer.h"
#include <stdint.h>

// GLOBAL: XVT 0x9D12E8
int g_fviewForwardX_Q15 = 0;
// GLOBAL: XVT 0x9D1264
int g_fviewForwardY_Q15 = 0;
// GLOBAL: XVT 0x9D1154
int g_fviewForwardZ_Q15 = 0;
// GLOBAL: XVT 0x9A8D80
int g_fviewSideX_Q15 = 0;
// GLOBAL: XVT 0x9A8D60
int g_fviewSideY_Q15 = 0;
// GLOBAL: XVT 0x9A8D8C
int g_fviewSideZ_Q15 = 0;
// GLOBAL: XVT 0x9A8E20
int g_fviewUpX_Q15 = 0;
// GLOBAL: XVT 0x9A8E1C
int g_fviewUpY_Q15 = 0;
// GLOBAL: XVT 0x9A8E28
int g_fviewUpZ_Q15 = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x427940
void FVIEW_BuildCameraOrient(int16_t viewRoll, int16_t viewPitch, int16_t viewYaw, int16_t viewAngleD,
							 int16_t hudAimX, int16_t hudAimY, ObjectRecord* objRecord) {
	/* Build the camera basis from the current orientation and HUD aim offsets. */
	int axisX;
	int axisY;
	int axisZ;

	FVIEW_calcrotatemove(viewPitch, viewYaw, objRecord);
	FVIEW_calcrotateorient(viewRoll, viewAngleD, objRecord);

	g_curMatR2_X = -g_curMatR2_X;
	g_curMatR2_Y = -g_curMatR2_Y;
	g_curMatR2_Z = -g_curMatR2_Z;
	g_curMatR1_X = -g_curMatR1_X;
	axisX = g_curMatR1_X;
	g_curMatR1_Y = -g_curMatR1_Y;
	axisY = g_curMatR1_Y;
	g_curMatR1_Z = -g_curMatR1_Z;
	axisZ = g_curMatR1_Z;

	FVIEW_transformaxes(g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z, hudAimX);
	FVIEW_transformaxes(axisX, axisY, axisZ, hudAimY);

	g_camMatR0_X = g_curMatR0_X;
	g_camMatR0_Y = g_curMatR0_Y;
	g_camMatR0_Z = g_curMatR0_Z;
	g_camMatR1_X = g_curMatR1_X;
	g_camMatR1_Y = g_curMatR1_Y;
	g_camMatR1_Z = g_curMatR1_Z;
	g_camMatR2_X = g_curMatR2_X;
	g_camMatR2_Y = g_curMatR2_Y;
	g_camMatR2_Z = g_curMatR2_Z;
#ifdef XVT_MODERN
	XvtRenderCamera_Build(viewRoll, viewPitch, viewYaw, viewAngleD, hudAimX, hudAimY);
#endif
}

// FUNCTION: XVT 0x427A60
int FVIEW_SetObjectTransform(int16_t roll, int16_t pitch, int16_t yaw, int16_t rollOffset,
							 ObjectRecord* objRecord) {
	if (objRecord == NULL) {
		FVIEW_calcrotatemove(pitch, yaw, objRecord);
		FVIEW_calcrotateorient(roll, rollOffset, objRecord);
		return FVIEW_ComputeObjectViewMatrix();
	}

	if (objRecord->mobj->orientMatrixDirty != 0) {
		FVIEW_calcrotatemove(pitch, yaw, objRecord);
		FVIEW_calcrotateorient(roll, rollOffset, objRecord);
		return FVIEW_ComputeObjectViewMatrix();
	}

	g_fviewForwardX_Q15 = objRecord->mobj->cachedFwdX;
	g_fviewForwardY_Q15 = objRecord->mobj->cachedFwdY;
	g_fviewForwardZ_Q15 = objRecord->mobj->cachedFwdZ;
	g_fviewSideX_Q15 = objRecord->mobj->cachedSideX;
	g_fviewSideY_Q15 = objRecord->mobj->cachedSideY;
	g_fviewSideZ_Q15 = objRecord->mobj->cachedSideZ;
	g_fviewUpX_Q15 = objRecord->mobj->cachedUpX;
	g_fviewUpY_Q15 = objRecord->mobj->cachedUpY;
	g_fviewUpZ_Q15 = objRecord->mobj->cachedUpZ;

	g_curMatR2_X = -g_fviewForwardX_Q15;
	g_curMatR2_Y = -g_fviewForwardY_Q15;
	g_curMatR2_Z = -g_fviewForwardZ_Q15;
	g_curMatR0_X = g_fviewSideX_Q15;
	g_curMatR0_Y = g_fviewSideY_Q15;
	g_curMatR0_Z = g_fviewSideZ_Q15;
	g_curMatR1_X = g_fviewUpX_Q15;
	g_curMatR1_Y = g_fviewUpY_Q15;
	g_curMatR1_Z = g_fviewUpZ_Q15;

	return FVIEW_ComputeObjectViewMatrix();
}

// FUNCTION: XVT 0x427BD0
void FVIEW_calcrotatemove(int16_t angleA, int16_t angleB, ObjectRecord* objRecord) {
	int16_t cosNegB;
	int16_t cosC000MinusA;
	int16_t sinNegB;
	int16_t sinC000MinusA;

	cosNegB = trig2_getsignedcos(-angleB);
	cosC000MinusA = trig2_getsignedcos((int16_t)(0xc000 - angleA));
	sinNegB = trig2_getsignedsin(-angleB);
	sinC000MinusA = trig2_getsignedsin((int16_t)(0xc000 - angleA));

	g_curMatR0_X = cosNegB;
	g_curMatR0_Y = sinNegB;
	g_curMatR0_Z = 0;
	g_curMatR2_X = Math_MulQ15(-sinNegB, cosC000MinusA);
	g_curMatR2_Y = Math_MulQ15(cosNegB, cosC000MinusA);
	g_curMatR2_Z = sinC000MinusA;
	g_curMatR1_X = -Math_MulQ15(sinNegB, sinC000MinusA);
	g_curMatR1_Z = -cosC000MinusA;
	g_curMatR1_Y = -Math_MulQ15(-cosNegB, sinC000MinusA);

	g_fviewMoveX_Q15 = -g_curMatR2_X;
	g_fviewMoveZ_Q15 = -g_curMatR2_Z;
	g_fviewMoveY_Q15 = -g_curMatR2_Y;
	if (objRecord != NULL) {
		objRecord->mobj->moveX = (int16_t)g_fviewMoveX_Q15;
		objRecord->mobj->moveY = (int16_t)g_fviewMoveY_Q15;
		objRecord->mobj->moveZ = (int16_t)g_fviewMoveZ_Q15;
		objRecord->mobj->moveVectorDirty = 0;
	}
}

// FUNCTION: XVT 0x427D30
void FVIEW_calcrotateorient(int16_t angleA, int16_t angleQ16, ObjectRecord* objRecord) {
	FVIEW_transformaxes(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z, angleQ16);
	FVIEW_transformaxes(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z, angleA);

	g_fviewForwardX_Q15 = -g_curMatR2_X;
	g_fviewForwardY_Q15 = -g_curMatR2_Y;
	g_fviewSideX_Q15 = g_curMatR0_X;
	g_fviewSideY_Q15 = g_curMatR0_Y;
	g_fviewUpX_Q15 = g_curMatR1_X;
	g_fviewForwardZ_Q15 = -g_curMatR2_Z;
	g_fviewSideZ_Q15 = g_curMatR0_Z;
	g_fviewUpY_Q15 = g_curMatR1_Y;
	g_fviewUpZ_Q15 = g_curMatR1_Z;

	if (objRecord != NULL) {
		objRecord->mobj->cachedFwdX = (int16_t)g_fviewForwardX_Q15;
		objRecord->mobj->cachedFwdY = (int16_t)g_fviewForwardY_Q15;
		objRecord->mobj->cachedFwdZ = (int16_t)g_fviewForwardZ_Q15;
		objRecord->mobj->cachedSideX = (int16_t)g_fviewSideX_Q15;
		objRecord->mobj->cachedSideY = (int16_t)g_fviewSideY_Q15;
		objRecord->mobj->cachedSideZ = (int16_t)g_fviewSideZ_Q15;
		objRecord->mobj->cachedUpX = (int16_t)g_fviewUpX_Q15;
		objRecord->mobj->cachedUpY = (int16_t)g_fviewUpY_Q15;
		objRecord->mobj->cachedUpZ = (int16_t)g_fviewUpZ_Q15;
		objRecord->mobj->orientMatrixDirty = 0;
	}
}

// FUNCTION: XVT 0x427E90
int FVIEW_ComputeObjectViewMatrix(void) {
	int result;

	g_objViewMat_R0_X = Math_Dot3Q15Wrapped(g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z, g_camMatR0_X,
											g_camMatR0_Y, g_camMatR0_Z);
	g_objViewMat_R0_Y = Math_Dot3Q15Wrapped(g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z, g_camMatR1_X,
											g_camMatR1_Y, g_camMatR1_Z);
	g_objViewMat_R0_Z = Math_Dot3Q15Wrapped(g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z, g_camMatR2_X,
											g_camMatR2_Y, g_camMatR2_Z);
	g_objViewMat_R1_X = Math_Dot3Q15Wrapped(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z, g_camMatR0_X,
											g_camMatR0_Y, g_camMatR0_Z);
	g_objViewMat_R1_Y = Math_Dot3Q15Wrapped(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z, g_camMatR1_X,
											g_camMatR1_Y, g_camMatR1_Z);
	g_objViewMat_R1_Z = Math_Dot3Q15Wrapped(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z, g_camMatR2_X,
											g_camMatR2_Y, g_camMatR2_Z);
	g_objViewMat_R2_X = Math_Dot3Q15Wrapped(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z, g_camMatR0_X,
											g_camMatR0_Y, g_camMatR0_Z);
	g_objViewMat_R2_Y = Math_Dot3Q15Wrapped(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z, g_camMatR1_X,
											g_camMatR1_Y, g_camMatR1_Z);
	result = Math_Dot3Q15Wrapped(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z, g_camMatR2_X, g_camMatR2_Y,
								 g_camMatR2_Z);
	g_objViewMat_R2_Z = result;
	if (g_transformLightDirectionToObjectSpace != 0) {
		g_objectLightDirectionX =
			Math_Dot3Q15Wrapped(g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z, g_modelPreviewLightDirectionX,
								g_modelPreviewLightDirectionY, g_modelPreviewLightDirectionZ);
		g_objectLightDirectionY =
			Math_Dot3Q15Wrapped(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z, g_modelPreviewLightDirectionX,
								g_modelPreviewLightDirectionY, g_modelPreviewLightDirectionZ);
		result = Math_Dot3Q15Wrapped(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z, g_modelPreviewLightDirectionX,
									 g_modelPreviewLightDirectionY, g_modelPreviewLightDirectionZ);
		g_objectLightDirectionZ = result;
	} else {
		g_objectLightDirectionX = g_modelPreviewLightDirectionX;
		g_objectLightDirectionY = g_modelPreviewLightDirectionY;
		g_objectLightDirectionZ = g_modelPreviewLightDirectionZ;
	}
	return result;
}

// FUNCTION: XVT 0x4290B0
void FVIEW_transformaxes(int axisX_Q15, int axisY_Q15, int axisZ_Q15, int16_t angleQ16) {
	enum { Q15_ONE = 0x7FFF };

	int16_t cosine;
	int16_t sine;
	int coefficient00;
	int coefficient01;
	int coefficient02;
	int coefficient10;
	int coefficient11;
	int coefficient12;
	int coefficient20;
	int coefficient21;
	int coefficient22;
	int newX;
	int newY;
	int newZ;

	if (angleQ16 == 0)
		return;

	cosine = trig2_getsignedcos(angleQ16);
	sine = trig2_getsignedsin(angleQ16);
	if (cosine >= 0) {
		const int cosineComplement = Q15_ONE - cosine;

		coefficient00 = Math_RodriguesTermNonnegativeCos(axisX_Q15, axisX_Q15, cosineComplement, cosine);
		coefficient01 = Math_RodriguesTermNonnegativeCos(axisX_Q15, axisY_Q15, cosineComplement,
														 Math_MulQ15(sine, axisZ_Q15));
		coefficient02 = Math_RodriguesTermNonnegativeCos(axisX_Q15, axisZ_Q15, cosineComplement,
														 -Math_MulQ15(sine, axisY_Q15));
		coefficient10 = Math_RodriguesTermNonnegativeCos(axisX_Q15, axisY_Q15, cosineComplement,
														 -Math_MulQ15(sine, axisZ_Q15));
		coefficient11 = Math_RodriguesTermNonnegativeCos(axisY_Q15, axisY_Q15, cosineComplement, cosine);
		coefficient12 = Math_RodriguesTermNonnegativeCos(axisY_Q15, axisZ_Q15, cosineComplement,
														 Math_MulQ15(sine, axisX_Q15));
		coefficient20 = Math_RodriguesTermNonnegativeCos(axisX_Q15, axisZ_Q15, cosineComplement,
														 Math_MulQ15(sine, axisY_Q15));
		coefficient21 = Math_RodriguesTermNonnegativeCos(axisY_Q15, axisZ_Q15, cosineComplement,
														 -Math_MulQ15(sine, axisX_Q15));
		coefficient22 = Math_RodriguesTermNonnegativeCos(axisZ_Q15, axisZ_Q15, cosineComplement, cosine);
	} else {
		const int cosineMagnitude = -cosine;

		coefficient00 = Math_RodriguesTermNegativeCos(axisX_Q15, axisX_Q15, cosineMagnitude, cosine);
		coefficient01 = Math_RodriguesTermNegativeCos(axisX_Q15, axisY_Q15, cosineMagnitude,
													  Math_MulQ15(sine, axisZ_Q15));
		coefficient02 = Math_RodriguesTermNegativeCos(axisX_Q15, axisZ_Q15, cosineMagnitude,
													  -Math_MulQ15(sine, axisY_Q15));
		coefficient10 = Math_RodriguesTermNegativeCos(axisX_Q15, axisY_Q15, cosineMagnitude,
													  -Math_MulQ15(sine, axisZ_Q15));
		coefficient11 = Math_RodriguesTermNegativeCos(axisY_Q15, axisY_Q15, cosineMagnitude, cosine);
		coefficient12 = Math_RodriguesTermNegativeCos(axisY_Q15, axisZ_Q15, cosineMagnitude,
													  Math_MulQ15(sine, axisX_Q15));
		coefficient20 = Math_RodriguesTermNegativeCos(axisX_Q15, axisZ_Q15, cosineMagnitude,
													  Math_MulQ15(sine, axisY_Q15));
		coefficient21 = Math_RodriguesTermNegativeCos(axisY_Q15, axisZ_Q15, cosineMagnitude,
													  -Math_MulQ15(sine, axisX_Q15));
		coefficient22 = Math_RodriguesTermNegativeCos(axisZ_Q15, axisZ_Q15, cosineMagnitude, cosine);
	}

	newX = Math_Dot3Q15Wrapped(g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z, coefficient00, coefficient10,
							   coefficient20);
	newY = Math_Dot3Q15Wrapped(g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z, coefficient01, coefficient11,
							   coefficient21);
	newZ = Math_Dot3Q15Wrapped(g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z, coefficient02, coefficient12,
							   coefficient22);
	g_curMatR0_X = newX;
	g_curMatR0_Y = newY;
	g_curMatR0_Z = newZ;

	newX = Math_Dot3Q15Wrapped(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z, coefficient00, coefficient10,
							   coefficient20);
	newY = Math_Dot3Q15Wrapped(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z, coefficient01, coefficient11,
							   coefficient21);
	newZ = Math_Dot3Q15Wrapped(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z, coefficient02, coefficient12,
							   coefficient22);
	g_curMatR1_X = newX;
	g_curMatR1_Y = newY;
	g_curMatR1_Z = newZ;

	newX = Math_Dot3Q15Wrapped(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z, coefficient00, coefficient10,
							   coefficient20);
	newY = Math_Dot3Q15Wrapped(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z, coefficient01, coefficient11,
							   coefficient21);
	newZ = Math_Dot3Q15Wrapped(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z, coefficient02, coefficient12,
							   coefficient22);
	g_curMatR2_X = newX;
	g_curMatR2_Y = newY;
	g_curMatR2_Z = newZ;
}
