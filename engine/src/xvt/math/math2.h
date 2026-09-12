#ifndef XVT_MATH_MATH2_H
#define XVT_MATH_MATH2_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int MATH2_ABoverC32(int a, int b, int c);
unsigned int MATH2_fraction(uint16_t value, uint16_t fracQ16);
unsigned int MATH2_longfraction(unsigned int value, uint16_t fracQ16);
uint16_t MATH2_divide(uint16_t numerator, uint16_t denominator);
unsigned int MATH2_percentage(unsigned int numerator, unsigned int denominator);
unsigned int MATH2_mphconvert(int16_t speed, uint16_t divisor);
int16_t MATH2_getradarcoord(int a1, int a2, int a3);

#ifdef __cplusplus
}
#endif

#endif
