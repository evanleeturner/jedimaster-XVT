#include "xvt/math/math3d.h"

#include <math.h>

/* Returns the dot product of two 3-float vectors. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x420FC0
float math3d_dot3(const float *lhs, const float *rhs)
{
	float y = rhs[1] * lhs[1];
	return y + rhs[2] * lhs[2] + rhs[0] * lhs[0];
}

/* Multiplies the vector, as a row, by the row-major 3x3 matrix in place:
 * each new component is the dot product with one column, m[0], m[3] and m[6]
 * for x. */
// FUNCTION: XVT 0x420FE0
void math3d_rotate_vec3(float *vec_in_out, const float *matrix3x3)
{
	float x = vec_in_out[0];
	float y = vec_in_out[1];
	float z = vec_in_out[2];
	float x_product;
	float y_product;
	float z_product;

	vec_in_out[0] = matrix3x3[6] * z + matrix3x3[3] * y + matrix3x3[0] * x;
	vec_in_out[1] = matrix3x3[1] * x + matrix3x3[7] * z + matrix3x3[4] * y;
	z_product = z * matrix3x3[8];
	y_product = y * matrix3x3[5];
	x_product = x * matrix3x3[2];
	vec_in_out[2] = z_product + y_product + x_product;
}

/* Returns the x that math3d_rotate_vec3 would give: m[0] * x + m[3] * y
 * + m[6] * z. */
// FUNCTION: XVT 0x421040
float math3d_rotate_vec3x(const float *vec, const float *matrix3x3)
{
	return matrix3x3[6] * vec[2] + vec[1] * matrix3x3[3] +
	       matrix3x3[0] * vec[0];
}

/* Returns the y that math3d_rotate_vec3 would give: m[1] * x + m[4] * y
 * + m[7] * z. */
// FUNCTION: XVT 0x421060
float math3d_rotate_vec3y(const float *vec, const float *matrix3x3)
{
	return vec[1] * matrix3x3[4] + vec[2] * matrix3x3[7] +
	       matrix3x3[1] * vec[0];
}

/* Returns the z that math3d_rotate_vec3 would give: m[2] * x + m[5] * y
 * + m[8] * z. */
// FUNCTION: XVT 0x421080
float math3d_rotate_vec3z(const float *vec, const float *matrix3x3)
{
	return matrix3x3[8] * vec[2] + vec[1] * matrix3x3[5] +
	       matrix3x3[2] * vec[0];
}

/* Multiplies the row-major 3x3 matrix in `lhs_in_out` by `rhs` in place. */
// FUNCTION: XVT 0x4210A0
void math3d_mul_matrix3x3(float *lhs_in_out, const float *rhs)
{
	float r00;
	float r01;
	float r02;
	float r10;
	float r11;
	float r12;
	float r20;
	float r21;
	float r22;

	r00 = lhs_in_out[0] * rhs[0] + lhs_in_out[1] * rhs[3] +
	      lhs_in_out[2] * rhs[6];
	r01 = lhs_in_out[0] * rhs[1] + lhs_in_out[1] * rhs[4] +
	      lhs_in_out[2] * rhs[7];
	r02 = lhs_in_out[0] * rhs[2] + lhs_in_out[1] * rhs[5] +
	      lhs_in_out[2] * rhs[8];
	r10 = lhs_in_out[3] * rhs[0] + lhs_in_out[4] * rhs[3] +
	      lhs_in_out[5] * rhs[6];
	r11 = lhs_in_out[3] * rhs[1] + lhs_in_out[4] * rhs[4] +
	      lhs_in_out[5] * rhs[7];
	r12 = lhs_in_out[3] * rhs[2] + lhs_in_out[4] * rhs[5] +
	      lhs_in_out[5] * rhs[8];
	r20 = lhs_in_out[6] * rhs[0] + lhs_in_out[7] * rhs[3] +
	      lhs_in_out[8] * rhs[6];
	r21 = lhs_in_out[6] * rhs[1] + lhs_in_out[7] * rhs[4] +
	      lhs_in_out[8] * rhs[7];
	r22 = lhs_in_out[6] * rhs[2] + lhs_in_out[7] * rhs[5] +
	      lhs_in_out[8] * rhs[8];
	lhs_in_out[0] = r00;
	lhs_in_out[1] = r01;
	lhs_in_out[2] = r02;
	lhs_in_out[3] = r10;
	lhs_in_out[4] = r11;
	lhs_in_out[5] = r12;
	lhs_in_out[6] = r20;
	lhs_in_out[7] = r21;
	lhs_in_out[8] = r22;
}

/* Multiplies the transposed `rhs` into `lhs_in_out` in place:
 * new[r][c] = sum_k lhs[k][c] * rhs[k][r]. */
// FUNCTION: XVT 0x4211D0
void math3d_pre_mul_transposed_matrix3x3(float *lhs_in_out, const float *rhs)
{
	float r00;
	float r01;
	float r02;
	float r10;
	float r11;
	float r12;
	float r20;
	float r21;
	float r22;

	r00 = lhs_in_out[0] * rhs[0] + lhs_in_out[3] * rhs[3] +
	      lhs_in_out[6] * rhs[6];
	r01 = lhs_in_out[1] * rhs[0] + lhs_in_out[4] * rhs[3] +
	      lhs_in_out[7] * rhs[6];
	r02 = lhs_in_out[2] * rhs[0] + lhs_in_out[5] * rhs[3] +
	      lhs_in_out[8] * rhs[6];
	r10 = lhs_in_out[0] * rhs[1] + lhs_in_out[3] * rhs[4] +
	      lhs_in_out[6] * rhs[7];
	r11 = lhs_in_out[1] * rhs[1] + lhs_in_out[4] * rhs[4] +
	      lhs_in_out[7] * rhs[7];
	r12 = lhs_in_out[2] * rhs[1] + lhs_in_out[5] * rhs[4] +
	      lhs_in_out[8] * rhs[7];
	r20 = lhs_in_out[0] * rhs[2] + lhs_in_out[3] * rhs[5] +
	      lhs_in_out[6] * rhs[8];
	r21 = lhs_in_out[1] * rhs[2] + lhs_in_out[4] * rhs[5] +
	      lhs_in_out[7] * rhs[8];
	r22 = lhs_in_out[2] * rhs[2] + lhs_in_out[5] * rhs[5] +
	      lhs_in_out[8] * rhs[8];
	lhs_in_out[0] = r00;
	lhs_in_out[1] = r01;
	lhs_in_out[2] = r02;
	lhs_in_out[3] = r10;
	lhs_in_out[4] = r11;
	lhs_in_out[5] = r12;
	lhs_in_out[6] = r20;
	lhs_in_out[7] = r21;
	lhs_in_out[8] = r22;
}

/* Writes the row-major rotation matrix for axis_angle[3] radians about the
 * axis in axis_angle[0] to [2], with m[0] = (1 - cos) * x * x + cos and the
 * sine terms placed so that m[1] = (1 - cos) * x * y + sin * z and
 * m[3] = (1 - cos) * x * y - sin * z. Does not normalize the axis. */
// FUNCTION: XVT 0x421300
void math3d_build_axis_angle_matrix(float *matrix3x3_out,
				    const float *axis_angle)
{
	float axis_x;
	float axis_y;
	float axis_z;
	float sin_angle;
	float cos_angle;
	float one_minus_cos_times_y;
	float one_minus_cos_times_z;
	float xy_term;
	float xz_term;
	float yz_term;
	float sin_x;
	float sin_y;
	float sin_z;

	axis_x = axis_angle[0];
	axis_y = axis_angle[1];
	axis_z = axis_angle[2];
	sin_angle = (float)sin(axis_angle[3]);
	cos_angle = (float)cos(axis_angle[3]);
	one_minus_cos_times_y = (1.0f - cos_angle) * axis_y;
	xy_term = one_minus_cos_times_y * axis_x;
	one_minus_cos_times_z = (1.0f - cos_angle) * axis_z;
	xz_term = one_minus_cos_times_z * axis_x;
	yz_term = one_minus_cos_times_z * axis_y;
	sin_x = sin_angle * axis_x;
	sin_z = sin_angle * axis_z;
	sin_y = sin_angle * axis_y;

	matrix3x3_out[0] = (1.0f - cos_angle) * axis_x * axis_x + cos_angle;
	matrix3x3_out[1] = xy_term + sin_z;
	matrix3x3_out[3] = xy_term - sin_z;
	matrix3x3_out[2] = xz_term - sin_y;
	matrix3x3_out[4] = axis_y * one_minus_cos_times_y + cos_angle;
	matrix3x3_out[5] = yz_term + sin_x;
	matrix3x3_out[6] = xz_term + sin_y;
	matrix3x3_out[8] = cos_angle + axis_z * one_minus_cos_times_z;
	matrix3x3_out[7] = yz_term - sin_x;
}
