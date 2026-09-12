#include "xvt/math/math3d.h"

#include <math.h>

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x420FC0
float Math3D_Dot3(const float* lhs, const float* rhs) {
	float y = rhs[1] * lhs[1];
	return y + rhs[2] * lhs[2] + rhs[0] * lhs[0];
}

// FUNCTION: XVT 0x420FE0
void Math3D_RotateVec3(float* vecInOut, const float* matrix3x3) {
	float x = vecInOut[0];
	float y = vecInOut[1];
	float z = vecInOut[2];
	float xProduct;
	float yProduct;
	float zProduct;

	vecInOut[0] = matrix3x3[6] * z + matrix3x3[3] * y + matrix3x3[0] * x;
	vecInOut[1] = matrix3x3[1] * x + matrix3x3[7] * z + matrix3x3[4] * y;
	zProduct = z * matrix3x3[8];
	yProduct = y * matrix3x3[5];
	xProduct = x * matrix3x3[2];
	vecInOut[2] = zProduct + yProduct + xProduct;
}

// FUNCTION: XVT 0x421040
float Math3D_RotateVec3X(const float* vec, const float* matrix3x3) {
	return matrix3x3[6] * vec[2] + vec[1] * matrix3x3[3] + matrix3x3[0] * vec[0];
}

// FUNCTION: XVT 0x421060
float Math3D_RotateVec3Y(const float* vec, const float* matrix3x3) {
	return vec[1] * matrix3x3[4] + vec[2] * matrix3x3[7] + matrix3x3[1] * vec[0];
}

// FUNCTION: XVT 0x421080
float Math3D_RotateVec3Z(const float* vec, const float* matrix3x3) {
	return matrix3x3[8] * vec[2] + vec[1] * matrix3x3[5] + matrix3x3[2] * vec[0];
}

/* Multiplies the row-major 3x3 matrix in `lhsInOut` by `rhs` in place. */
// FUNCTION: XVT 0x4210A0
void Math3D_MulMatrix3x3(float* lhsInOut, const float* rhs) {
	float r00;
	float r01;
	float r02;
	float r10;
	float r11;
	float r12;
	float r20;
	float r21;
	float r22;

	r00 = lhsInOut[0] * rhs[0] + lhsInOut[1] * rhs[3] + lhsInOut[2] * rhs[6];
	r01 = lhsInOut[0] * rhs[1] + lhsInOut[1] * rhs[4] + lhsInOut[2] * rhs[7];
	r02 = lhsInOut[0] * rhs[2] + lhsInOut[1] * rhs[5] + lhsInOut[2] * rhs[8];
	r10 = lhsInOut[3] * rhs[0] + lhsInOut[4] * rhs[3] + lhsInOut[5] * rhs[6];
	r11 = lhsInOut[3] * rhs[1] + lhsInOut[4] * rhs[4] + lhsInOut[5] * rhs[7];
	r12 = lhsInOut[3] * rhs[2] + lhsInOut[4] * rhs[5] + lhsInOut[5] * rhs[8];
	r20 = lhsInOut[6] * rhs[0] + lhsInOut[7] * rhs[3] + lhsInOut[8] * rhs[6];
	r21 = lhsInOut[6] * rhs[1] + lhsInOut[7] * rhs[4] + lhsInOut[8] * rhs[7];
	r22 = lhsInOut[6] * rhs[2] + lhsInOut[7] * rhs[5] + lhsInOut[8] * rhs[8];
	lhsInOut[0] = r00;
	lhsInOut[1] = r01;
	lhsInOut[2] = r02;
	lhsInOut[3] = r10;
	lhsInOut[4] = r11;
	lhsInOut[5] = r12;
	lhsInOut[6] = r20;
	lhsInOut[7] = r21;
	lhsInOut[8] = r22;
}

/* Multiplies the transposed `rhs` into `lhsInOut` in place:
 * new[r][c] = sum_k lhs[k][c] * rhs[k][r]. */
// FUNCTION: XVT 0x4211D0
void Math3D_MulMatrix3x3T(float* lhsInOut, const float* rhs) {
	float r00;
	float r01;
	float r02;
	float r10;
	float r11;
	float r12;
	float r20;
	float r21;
	float r22;

	r00 = lhsInOut[0] * rhs[0] + lhsInOut[3] * rhs[3] + lhsInOut[6] * rhs[6];
	r01 = lhsInOut[1] * rhs[0] + lhsInOut[4] * rhs[3] + lhsInOut[7] * rhs[6];
	r02 = lhsInOut[2] * rhs[0] + lhsInOut[5] * rhs[3] + lhsInOut[8] * rhs[6];
	r10 = lhsInOut[0] * rhs[1] + lhsInOut[3] * rhs[4] + lhsInOut[6] * rhs[7];
	r11 = lhsInOut[1] * rhs[1] + lhsInOut[4] * rhs[4] + lhsInOut[7] * rhs[7];
	r12 = lhsInOut[2] * rhs[1] + lhsInOut[5] * rhs[4] + lhsInOut[8] * rhs[7];
	r20 = lhsInOut[0] * rhs[2] + lhsInOut[3] * rhs[5] + lhsInOut[6] * rhs[8];
	r21 = lhsInOut[1] * rhs[2] + lhsInOut[4] * rhs[5] + lhsInOut[7] * rhs[8];
	r22 = lhsInOut[2] * rhs[2] + lhsInOut[5] * rhs[5] + lhsInOut[8] * rhs[8];
	lhsInOut[0] = r00;
	lhsInOut[1] = r01;
	lhsInOut[2] = r02;
	lhsInOut[3] = r10;
	lhsInOut[4] = r11;
	lhsInOut[5] = r12;
	lhsInOut[6] = r20;
	lhsInOut[7] = r21;
	lhsInOut[8] = r22;
}

// FUNCTION: XVT 0x421300
void Math3D_BuildAxisAngleMatrix(float* matrix3x3Out, const float* axisAngle) {
	float axisX;
	float axisY;
	float axisZ;
	float sinAngle;
	float cosAngle;
	float oneMinusCosTimesY;
	float oneMinusCosTimesZ;
	float xyTerm;
	float xzTerm;
	float yzTerm;
	float sinX;
	float sinY;
	float sinZ;

	axisX = axisAngle[0];
	axisY = axisAngle[1];
	axisZ = axisAngle[2];
	sinAngle = (float)sin(axisAngle[3]);
	cosAngle = (float)cos(axisAngle[3]);
	oneMinusCosTimesY = (1.0f - cosAngle) * axisY;
	xyTerm = oneMinusCosTimesY * axisX;
	oneMinusCosTimesZ = (1.0f - cosAngle) * axisZ;
	xzTerm = oneMinusCosTimesZ * axisX;
	yzTerm = oneMinusCosTimesZ * axisY;
	sinX = sinAngle * axisX;
	sinZ = sinAngle * axisZ;
	sinY = sinAngle * axisY;

	matrix3x3Out[0] = (1.0f - cosAngle) * axisX * axisX + cosAngle;
	matrix3x3Out[1] = xyTerm + sinZ;
	matrix3x3Out[3] = xyTerm - sinZ;
	matrix3x3Out[2] = xzTerm - sinY;
	matrix3x3Out[4] = axisY * oneMinusCosTimesY + cosAngle;
	matrix3x3Out[5] = yzTerm + sinX;
	matrix3x3Out[6] = xzTerm + sinY;
	matrix3x3Out[8] = cosAngle + axisZ * oneMinusCosTimesZ;
	matrix3x3Out[7] = yzTerm - sinX;
}
