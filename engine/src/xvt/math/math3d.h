#ifndef XVT_MATH_MATH3D_H
#define XVT_MATH_MATH3D_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

float Math3D_Dot3(const float* lhs, const float* rhs);
void Math3D_RotateVec3(float* vecInOut, const float* matrix3x3);
float Math3D_RotateVec3X(const float* vec, const float* matrix3x3);
float Math3D_RotateVec3Y(const float* vec, const float* matrix3x3);
float Math3D_RotateVec3Z(const float* vec, const float* matrix3x3);
void Math3D_MulMatrix3x3(float* lhsInOut, const float* rhs);
void Math3D_MulMatrix3x3T(float* lhsInOut, const float* rhs);
void Math3D_BuildAxisAngleMatrix(float* matrix3x3Out, const float* axisAngle);

#ifdef __cplusplus
}
#endif

#endif
