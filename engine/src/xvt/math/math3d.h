#ifndef XVT_MATH_MATH3D_H
#define XVT_MATH_MATH3D_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

float math3d_dot3(const float *lhs, const float *rhs);
void math3d_rotate_vec3(float *vec_in_out, const float *matrix3x3);
float math3d_rotate_vec3x(const float *vec, const float *matrix3x3);
float math3d_rotate_vec3y(const float *vec, const float *matrix3x3);
float math3d_rotate_vec3z(const float *vec, const float *matrix3x3);
void math3d_mul_matrix3x3(float *lhs_in_out, const float *rhs);
void math3d_pre_mul_transposed_matrix3x3(float *lhs_in_out, const float *rhs);
void math3d_build_axis_angle_matrix(float *matrix3x3_out,
				    const float *axis_angle);

#ifdef __cplusplus
}
#endif

#endif
