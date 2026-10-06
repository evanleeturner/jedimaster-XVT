#ifndef XVT_MATH_MATH_H
#define XVT_MATH_MATH_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a * b >> 15, taken from the full 64-bit product and rounded toward
 * minus infinity; only the low 32 bits of the shifted product are kept. */
static __inline int math_mul_q15(int a, int b)
{
	return (int)(((int64_t)a * b) >> 15);
}

/* Returns the sum of the three products, each shifted down 15 bits as in
 * math_mul_q15; the sum is 32-bit and may wrap. */
static __inline int math_dot3q15(int left_x, int left_y, int left_z,
				 int right_x, int right_y, int right_z)
{
	return (int)(((int64_t)left_x * right_x) >> 15) +
	       (int)(((int64_t)left_y * right_y) >> 15) +
	       (int)(((int64_t)left_z * right_z) >> 15);
}

/* Unlike math_dot3q15, the products and their sum are 32-bit and may wrap; the
 * sum is then clamped to [-0x3FFF0000, 0x3FFFFFFF] before the shift, so the
 * result stays within -32766..32767. */
static __inline int math_dot3q15_wrapped(int left_x, int left_y, int left_z,
					 int right_x, int right_y, int right_z)
{
	int32_t value = (int32_t)((uint32_t)left_x * (uint32_t)right_x +
				  (uint32_t)left_y * (uint32_t)right_y +
				  (uint32_t)left_z * (uint32_t)right_z);

	if (value >= 0x40000000) {
		value = 0x3FFFFFFF;
	}
	if (value <= -0x40000000) {
		value = -0x3FFF0000;
	}
	return value >> 15;
}

/* Two-term form of math_dot3q15_wrapped: 32-bit products and sum, clamped the
 * same way before the shift. */
static __inline int math_dot2q15_wrapped(int left_x, int left_y, int right_x,
					 int right_y)
{
	int32_t value = (int32_t)((uint32_t)left_x * (uint32_t)right_x +
				  (uint32_t)left_y * (uint32_t)right_y);

	if (value >= 0x40000000) {
		value = 0x3FFFFFFF;
	}
	if (value <= -0x40000000) {
		value = -0x3FFF0000;
	}
	return value >> 15;
}

/* One term of an axis-angle rotation matrix when the cosine is 0 or more:
 * ((axis_a_q15 * axis_b_q15) >> 15) * one_minus_cos_q15 + (cross_term_q15 << 15),
 * in 32-bit arithmetic that may wrap, clamped as in math_dot3q15_wrapped and
 * shifted down 15 bits. */
static __inline int math_rodrigues_term_nonnegative_cos(int axis_a_q15,
							int axis_b_q15,
							int one_minus_cos_q15,
							int cross_term_q15)
{
	int32_t product =
		(int32_t)((uint32_t)axis_a_q15 * (uint32_t)axis_b_q15);
	int32_t value = (int32_t)((uint32_t)(product >> 15) *
					  (uint32_t)one_minus_cos_q15 +
				  ((uint32_t)cross_term_q15 << 15));

	if (value >= 0x40000000) {
		value = 0x3FFFFFFF;
	}
	if (value <= -0x40000000) {
		value = -0x3FFF0000;
	}
	return value >> 15;
}

/* The same term when the cosine is negative:
 * ((axis_a_q15 * axis_b_q15) >> 15) * abs_cos_q15 + axis_a_q15 * axis_b_q15
 * + (cross_term_q15 << 15), in 32-bit arithmetic that may wrap, clamped as in
 * math_dot3q15_wrapped and shifted down 15 bits. */
static __inline int math_rodrigues_term_negative_cos(int axis_a_q15,
						     int axis_b_q15,
						     int abs_cos_q15,
						     int cross_term_q15)
{
	int32_t product =
		(int32_t)((uint32_t)axis_a_q15 * (uint32_t)axis_b_q15);
	int32_t value =
		(int32_t)((uint32_t)(product >> 15) * (uint32_t)abs_cos_q15 +
			  (uint32_t)product + ((uint32_t)cross_term_q15 << 15));

	if (value >= 0x40000000) {
		value = 0x3FFFFFFF;
	}
	if (value <= -0x40000000) {
		value = -0x3FFF0000;
	}
	return value >> 15;
}

void math_set_fpu_single_precision_mode(void);
void math_set_fpu_extended_precision_mode(void);
uint16_t math_div_u16_with_fraction_q16(uint16_t dividend, uint16_t divisor);
unsigned int math_u16_to_q16(uint16_t value);

#ifdef __cplusplus
}
#endif

#endif
