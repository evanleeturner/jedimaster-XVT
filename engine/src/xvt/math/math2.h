#ifndef XVT_MATH_MATH2_H
#define XVT_MATH_MATH2_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int math2_ab_over_c32(int a, int b, int c);
unsigned int math2_fraction(uint16_t value, uint16_t frac_q16);
unsigned int math2_longfraction(unsigned int value, uint16_t frac_q16);
uint16_t math2_ratio_q16(uint16_t numerator, uint16_t denominator);
unsigned int math2_longratio_q16(unsigned int numerator,
				 unsigned int denominator);
unsigned int math2_mphconvert(int16_t speed, uint16_t divisor);
int16_t math2_getradarcoord(int side, int up, int forward);

#ifdef __cplusplus
}
#endif

#endif
