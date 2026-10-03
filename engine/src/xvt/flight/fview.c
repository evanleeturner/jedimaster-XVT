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

/* Forward axis of the object last oriented, X term, Q15 (32,768 is 1.0);
 * the negated row 2 of the current object matrix (g_curMatR2_X). The nine
 * g_fview axis globals have two writers, each setting all nine:
 * FVIEW_calcrotateorient, from the matrix it has just turned, and
 * FVIEW_SetObjectTransform, from an object's cached axes. */
// GLOBAL: XVT 0x9D12E8
int g_fviewForwardX_Q15 = 0;
/* Forward axis, Y term; see g_fviewForwardX_Q15. */
// GLOBAL: XVT 0x9D1264
int g_fviewForwardY_Q15 = 0;
/* Forward axis, Z term; see g_fviewForwardX_Q15. */
// GLOBAL: XVT 0x9D1154
int g_fviewForwardZ_Q15 = 0;
/* Side axis of the object last oriented, X term: row 0 of the current
 * object matrix; see g_fviewForwardX_Q15. */
// GLOBAL: XVT 0x9A8D80
int g_fviewSideX_Q15 = 0;
/* Side axis, Y term; see g_fviewSideX_Q15. */
// GLOBAL: XVT 0x9A8D60
int g_fviewSideY_Q15 = 0;
/* Side axis, Z term; see g_fviewSideX_Q15. */
// GLOBAL: XVT 0x9A8D8C
int g_fviewSideZ_Q15 = 0;
/* Up axis of the object last oriented, X term: row 1 of the current object
 * matrix; see g_fviewForwardX_Q15. */
// GLOBAL: XVT 0x9A8E20
int g_fviewUpX_Q15 = 0;
/* Up axis, Y term; see g_fviewUpX_Q15. */
// GLOBAL: XVT 0x9A8E1C
int g_fviewUpY_Q15 = 0;
/* Up axis, Z term; see g_fviewUpX_Q15. */
// GLOBAL: XVT 0x9A8E28
int g_fviewUpZ_Q15 = 0;

/* Builds the camera matrix g_camMatR0_X to g_camMatR2_Z from view angles
 * (a full circle is 65,536): FVIEW_calcrotatemove for viewPitch and viewYaw,
 * FVIEW_calcrotateorient for viewUpAxisAngle and viewRoll, rows 1 and 2
 * negated, then a turn by hudAimX about the side axis and by hudAimY about
 * row 1 as it stood before that turn. Those calls also write g_curMatR0_X to
 * g_curMatR2_Z, the g_fviewMove globals and the g_fview axis globals, and,
 * when objRecord is not NULL, store that move vector and those axes, as they
 * were before the negation, in objRecord's mobj. The modern build also calls
 * XvtRenderCamera_Build with the same angles. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x427940
void FVIEW_BuildCameraOrient(int16_t viewRoll, int16_t viewPitch,
			     int16_t viewYaw, int16_t viewUpAxisAngle,
			     int16_t hudAimX, int16_t hudAimY,
			     struct ObjectRecord *objRecord)
{
	/* Build the camera basis from the current orientation and HUD aim offsets. */
	int axisX;
	int axisY;
	int axisZ;

	FVIEW_calcrotatemove(viewPitch, viewYaw, objRecord);
	FVIEW_calcrotateorient(viewRoll, viewUpAxisAngle, objRecord);

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
	XvtRenderCamera_Build(viewRoll, viewPitch, viewYaw, viewUpAxisAngle,
			      hudAimX, hudAimY);
#endif
}

/* Sets the current object matrix (g_curMatR0_X to g_curMatR2_Z) and the
 * g_fview axis globals for an object, then builds its object-to-view matrix
 * with FVIEW_ComputeObjectViewMatrix and returns what that returns. With
 * objRecord NULL, or its mobj's orientMatrixDirty set, it works from the
 * angles through FVIEW_calcrotatemove and FVIEW_calcrotateorient, which
 * refresh a record's cached axes; otherwise it takes the cached axes from
 * the record's mobj. Does not check that mobj is set. */
// FUNCTION: XVT 0x427A60
int FVIEW_SetObjectTransform(int16_t roll, int16_t pitch, int16_t yaw,
			     int16_t upAxisAngle,
			     struct ObjectRecord *objRecord)
{
	if (objRecord == NULL) {
		FVIEW_calcrotatemove(pitch, yaw, objRecord);
		FVIEW_calcrotateorient(roll, upAxisAngle, objRecord);
		return FVIEW_ComputeObjectViewMatrix();
	}

	if (objRecord->mobj->orientMatrixDirty != 0) {
		FVIEW_calcrotatemove(pitch, yaw, objRecord);
		FVIEW_calcrotateorient(roll, upAxisAngle, objRecord);
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

/* Starts the current object matrix from a pitch and a yaw (a full circle is
 * 65,536), with no roll: writes all nine g_curMatR0_X to g_curMatR2_Z, and
 * g_fviewMoveX_Q15, g_fviewMoveY_Q15 and g_fviewMoveZ_Q15, the forward
 * direction (negated row 2), Q15. When objRecord is not NULL it also stores
 * that direction as its mobj's moveX, moveY and moveZ and clears
 * moveVectorDirty. */
// FUNCTION: XVT 0x427BD0
void FVIEW_calcrotatemove(int16_t pitch, int16_t yaw,
			  struct ObjectRecord *objRecord)
{
	int16_t cosNegB;
	int16_t cosC000MinusA;
	int16_t sinNegB;
	int16_t sinC000MinusA;

	cosNegB = trig2_getsignedcos(-yaw);
	cosC000MinusA = trig2_getsignedcos((int16_t)(0xc000 - pitch));
	sinNegB = trig2_getsignedsin(-yaw);
	sinC000MinusA = trig2_getsignedsin((int16_t)(0xc000 - pitch));

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

/* Finishes the current object matrix begun by FVIEW_calcrotatemove: turns it
 * by upAxisAngle about its row 1, then by roll about its row 2
 * (FVIEW_transformaxes), and copies the axes into the g_fview axis globals.
 * When objRecord is not NULL it also caches them in its mobj (cachedFwdX to
 * cachedUpZ) and clears orientMatrixDirty. */
// FUNCTION: XVT 0x427D30
void FVIEW_calcrotateorient(int16_t roll, int16_t upAxisAngle,
			    struct ObjectRecord *objRecord)
{
	FVIEW_transformaxes(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z,
			    upAxisAngle);
	FVIEW_transformaxes(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z, roll);

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

/* Builds g_objViewMat_R0_X to g_objViewMat_R2_Z, the rotation from the
 * current object matrix into view space: each entry is one row of the
 * object matrix (rows 0, 2 and 1, in that order) dotted with one row of the
 * camera matrix, by Math_Dot3Q15Wrapped. With
 * g_transformLightDirectionToObjectSpace set it also turns the world light
 * direction into object space the same way, into g_objectLightDirectionX to
 * Z, and returns the Z term; otherwise it copies the world direction
 * unchanged and returns g_objViewMat_R2_Z. */
// FUNCTION: XVT 0x427E90
int FVIEW_ComputeObjectViewMatrix(void)
{
	int result;

	g_objViewMat_R0_X =
		Math_Dot3Q15Wrapped(g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z,
				    g_camMatR0_X, g_camMatR0_Y, g_camMatR0_Z);
	g_objViewMat_R0_Y =
		Math_Dot3Q15Wrapped(g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z,
				    g_camMatR1_X, g_camMatR1_Y, g_camMatR1_Z);
	g_objViewMat_R0_Z =
		Math_Dot3Q15Wrapped(g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z,
				    g_camMatR2_X, g_camMatR2_Y, g_camMatR2_Z);
	g_objViewMat_R1_X =
		Math_Dot3Q15Wrapped(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z,
				    g_camMatR0_X, g_camMatR0_Y, g_camMatR0_Z);
	g_objViewMat_R1_Y =
		Math_Dot3Q15Wrapped(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z,
				    g_camMatR1_X, g_camMatR1_Y, g_camMatR1_Z);
	g_objViewMat_R1_Z =
		Math_Dot3Q15Wrapped(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z,
				    g_camMatR2_X, g_camMatR2_Y, g_camMatR2_Z);
	g_objViewMat_R2_X =
		Math_Dot3Q15Wrapped(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z,
				    g_camMatR0_X, g_camMatR0_Y, g_camMatR0_Z);
	g_objViewMat_R2_Y =
		Math_Dot3Q15Wrapped(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z,
				    g_camMatR1_X, g_camMatR1_Y, g_camMatR1_Z);
	result = Math_Dot3Q15Wrapped(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z,
				     g_camMatR2_X, g_camMatR2_Y, g_camMatR2_Z);
	g_objViewMat_R2_Z = result;
	if (g_transformLightDirectionToObjectSpace != 0) {
		g_objectLightDirectionX = Math_Dot3Q15Wrapped(
			g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z,
			g_worldLightDirectionX, g_worldLightDirectionY,
			g_worldLightDirectionZ);
		g_objectLightDirectionY = Math_Dot3Q15Wrapped(
			g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z,
			g_worldLightDirectionX, g_worldLightDirectionY,
			g_worldLightDirectionZ);
		result = Math_Dot3Q15Wrapped(
			g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z,
			g_worldLightDirectionX, g_worldLightDirectionY,
			g_worldLightDirectionZ);
		g_objectLightDirectionZ = result;
	} else {
		g_objectLightDirectionX = g_worldLightDirectionX;
		g_objectLightDirectionY = g_worldLightDirectionY;
		g_objectLightDirectionZ = g_worldLightDirectionZ;
	}
	return result;
}

/* Turns all three rows of the current object matrix (g_curMatR0_X to
 * g_curMatR2_Z) by angleQ16 (a full circle is 65,536) about the axis
 * (axisX_Q15, axisY_Q15, axisZ_Q15), building the rotation from the axis
 * and the angle's cosine and sine. Does nothing when the angle is 0. Does
 * not check that the axis has length 1. */
// FUNCTION: XVT 0x4290B0
void FVIEW_transformaxes(int axisX_Q15, int axisY_Q15, int axisZ_Q15,
			 int16_t angleQ16)
{
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

	if (angleQ16 == 0) {
		return;
	}

	cosine = trig2_getsignedcos(angleQ16);
	sine = trig2_getsignedsin(angleQ16);
	if (cosine >= 0) {
		const int cosineComplement = Q15_ONE - cosine;

		coefficient00 = Math_RodriguesTermNonnegativeCos(
			axisX_Q15, axisX_Q15, cosineComplement, cosine);
		coefficient01 = Math_RodriguesTermNonnegativeCos(
			axisX_Q15, axisY_Q15, cosineComplement,
			Math_MulQ15(sine, axisZ_Q15));
		coefficient02 = Math_RodriguesTermNonnegativeCos(
			axisX_Q15, axisZ_Q15, cosineComplement,
			-Math_MulQ15(sine, axisY_Q15));
		coefficient10 = Math_RodriguesTermNonnegativeCos(
			axisX_Q15, axisY_Q15, cosineComplement,
			-Math_MulQ15(sine, axisZ_Q15));
		coefficient11 = Math_RodriguesTermNonnegativeCos(
			axisY_Q15, axisY_Q15, cosineComplement, cosine);
		coefficient12 = Math_RodriguesTermNonnegativeCos(
			axisY_Q15, axisZ_Q15, cosineComplement,
			Math_MulQ15(sine, axisX_Q15));
		coefficient20 = Math_RodriguesTermNonnegativeCos(
			axisX_Q15, axisZ_Q15, cosineComplement,
			Math_MulQ15(sine, axisY_Q15));
		coefficient21 = Math_RodriguesTermNonnegativeCos(
			axisY_Q15, axisZ_Q15, cosineComplement,
			-Math_MulQ15(sine, axisX_Q15));
		coefficient22 = Math_RodriguesTermNonnegativeCos(
			axisZ_Q15, axisZ_Q15, cosineComplement, cosine);
	} else {
		const int cosineMagnitude = -cosine;

		coefficient00 = Math_RodriguesTermNegativeCos(
			axisX_Q15, axisX_Q15, cosineMagnitude, cosine);
		coefficient01 = Math_RodriguesTermNegativeCos(
			axisX_Q15, axisY_Q15, cosineMagnitude,
			Math_MulQ15(sine, axisZ_Q15));
		coefficient02 = Math_RodriguesTermNegativeCos(
			axisX_Q15, axisZ_Q15, cosineMagnitude,
			-Math_MulQ15(sine, axisY_Q15));
		coefficient10 = Math_RodriguesTermNegativeCos(
			axisX_Q15, axisY_Q15, cosineMagnitude,
			-Math_MulQ15(sine, axisZ_Q15));
		coefficient11 = Math_RodriguesTermNegativeCos(
			axisY_Q15, axisY_Q15, cosineMagnitude, cosine);
		coefficient12 = Math_RodriguesTermNegativeCos(
			axisY_Q15, axisZ_Q15, cosineMagnitude,
			Math_MulQ15(sine, axisX_Q15));
		coefficient20 = Math_RodriguesTermNegativeCos(
			axisX_Q15, axisZ_Q15, cosineMagnitude,
			Math_MulQ15(sine, axisY_Q15));
		coefficient21 = Math_RodriguesTermNegativeCos(
			axisY_Q15, axisZ_Q15, cosineMagnitude,
			-Math_MulQ15(sine, axisX_Q15));
		coefficient22 = Math_RodriguesTermNegativeCos(
			axisZ_Q15, axisZ_Q15, cosineMagnitude, cosine);
	}

	newX = Math_Dot3Q15Wrapped(g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z,
				   coefficient00, coefficient10, coefficient20);
	newY = Math_Dot3Q15Wrapped(g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z,
				   coefficient01, coefficient11, coefficient21);
	newZ = Math_Dot3Q15Wrapped(g_curMatR0_X, g_curMatR0_Y, g_curMatR0_Z,
				   coefficient02, coefficient12, coefficient22);
	g_curMatR0_X = newX;
	g_curMatR0_Y = newY;
	g_curMatR0_Z = newZ;

	newX = Math_Dot3Q15Wrapped(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z,
				   coefficient00, coefficient10, coefficient20);
	newY = Math_Dot3Q15Wrapped(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z,
				   coefficient01, coefficient11, coefficient21);
	newZ = Math_Dot3Q15Wrapped(g_curMatR1_X, g_curMatR1_Y, g_curMatR1_Z,
				   coefficient02, coefficient12, coefficient22);
	g_curMatR1_X = newX;
	g_curMatR1_Y = newY;
	g_curMatR1_Z = newZ;

	newX = Math_Dot3Q15Wrapped(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z,
				   coefficient00, coefficient10, coefficient20);
	newY = Math_Dot3Q15Wrapped(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z,
				   coefficient01, coefficient11, coefficient21);
	newZ = Math_Dot3Q15Wrapped(g_curMatR2_X, g_curMatR2_Y, g_curMatR2_Z,
				   coefficient02, coefficient12, coefficient22);
	g_curMatR2_X = newX;
	g_curMatR2_Y = newY;
	g_curMatR2_Z = newZ;
}
