#ifndef XVT_MATH_MATH_H
#define XVT_MATH_MATH_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a * b >> 15, taken from the full 64-bit product and rounded toward
 * minus infinity; only the low 32 bits of the shifted product are kept. */
static __inline int Math_MulQ15(int a, int b)
{
#ifdef XVT_MODERN
	return (int)(((int64_t)a * b) >> 15);
#else
	__asm {
		push edx
		mov eax, a
		imul b
		shrd eax, edx, 15
		mov a, eax
		pop edx
	}
	return a;
#endif
}

/* Returns the sum of the three products, each shifted down 15 bits as in
 * Math_MulQ15; the sum is 32-bit and may wrap. */
static __inline int Math_Dot3Q15(int leftX, int leftY, int leftZ, int rightX,
				 int rightY, int rightZ)
{
#ifdef XVT_MODERN
	return (int)(((int64_t)leftX * rightX) >> 15) +
	       (int)(((int64_t)leftY * rightY) >> 15) +
	       (int)(((int64_t)leftZ * rightZ) >> 15);
#else
	__asm {
		mov eax, leftX
		imul rightX
		shrd eax, edx, 15
		mov ebx, eax
		mov eax, leftY
		imul rightY
		shrd eax, edx, 15
		add ebx, eax
		mov eax, leftZ
		imul rightZ
		shrd eax, edx, 15
		add eax, ebx
		mov leftX, eax
	}
	return leftX;
#endif
}

/* Unlike Math_Dot3Q15, the products and their sum are 32-bit and may wrap; the sum is then clamped to
 * [-0x3FFF0000, 0x3FFFFFFF] before the shift, so the result stays within -32766..32767. */
static __inline int Math_Dot3Q15Wrapped(int leftX, int leftY, int leftZ,
					int rightX, int rightY, int rightZ)
{
#ifdef XVT_MODERN
	int32_t value = (int32_t)((uint32_t)leftX * (uint32_t)rightX +
				  (uint32_t)leftY * (uint32_t)rightY +
				  (uint32_t)leftZ * (uint32_t)rightZ);

	if (value >= 0x40000000) {
		value = 0x3FFFFFFF;
	}
	if (value <= -0x40000000) {
		value = -0x3FFF0000;
	}
	return value >> 15;
#else
	__asm {
		mov eax, leftX
		mov ebx, leftY
		mov ecx, leftZ
		imul eax, rightX
		imul ebx, rightY
		imul ecx, rightZ
		add eax, ebx
		add eax, ecx
		cmp eax, 40000000h
		jl dot3WrappedPositiveOk
		mov eax, 3fffffffh
	dot3WrappedPositiveOk:
		cmp eax, 0c0000000h
		jg dot3WrappedNegativeOk
		mov eax, 0c0010000h
	dot3WrappedNegativeOk:
		sar eax, 15
		mov leftX, eax
	}
	return leftX;
#endif
}

/* Two-term form of Math_Dot3Q15Wrapped: 32-bit products and sum, clamped the same way before the shift. */
static __inline int Math_Dot2Q15Wrapped(int leftX, int leftY, int rightX,
					int rightY)
{
#ifdef XVT_MODERN
	int32_t value = (int32_t)((uint32_t)leftX * (uint32_t)rightX +
				  (uint32_t)leftY * (uint32_t)rightY);

	if (value >= 0x40000000) {
		value = 0x3FFFFFFF;
	}
	if (value <= -0x40000000) {
		value = -0x3FFF0000;
	}
	return value >> 15;
#else
	__asm {
		mov eax, leftX
		mov ebx, leftY
		imul eax, rightX
		imul ebx, rightY
		add eax, ebx
		cmp eax, 40000000h
		jl dot2WrappedPositiveOk
		mov eax, 3fffffffh
	dot2WrappedPositiveOk:
		cmp eax, 0c0000000h
		jg dot2WrappedNegativeOk
		mov eax, 0c0010000h
	dot2WrappedNegativeOk:
		sar eax, 15
		mov leftX, eax
	}
	return leftX;
#endif
}

/* One term of an axis-angle rotation matrix when the cosine is 0 or more:
 * ((axisA_Q15 * axisB_Q15) >> 15) * oneMinusCos_Q15 + (crossTerm_Q15 << 15),
 * in 32-bit arithmetic that may wrap, clamped as in Math_Dot3Q15Wrapped and
 * shifted down 15 bits. */
static __inline int Math_RodriguesTermNonnegativeCos(int axisA_Q15,
						     int axisB_Q15,
						     int oneMinusCos_Q15,
						     int crossTerm_Q15)
{
#ifdef XVT_MODERN
	int32_t product = (int32_t)((uint32_t)axisA_Q15 * (uint32_t)axisB_Q15);
	int32_t value = (int32_t)((uint32_t)(product >> 15) *
					  (uint32_t)oneMinusCos_Q15 +
				  ((uint32_t)crossTerm_Q15 << 15));

	if (value >= 0x40000000) {
		value = 0x3FFFFFFF;
	}
	if (value <= -0x40000000) {
		value = -0x3FFF0000;
	}
	return value >> 15;
#else
	__asm {
		mov eax, axisA_Q15
		imul eax, axisB_Q15
		sar eax, 15
		imul eax, oneMinusCos_Q15
		mov edx, crossTerm_Q15
		shl edx, 15
		add eax, edx
		cmp eax, 40000000h
		jl rodriguesNonnegativePositiveOk
		mov eax, 3fffffffh
	rodriguesNonnegativePositiveOk:
		cmp eax, 0c0000000h
		jg rodriguesNonnegativeNegativeOk
		mov eax, 0c0010000h
	rodriguesNonnegativeNegativeOk:
		sar eax, 15
		mov axisA_Q15, eax
	}
	return axisA_Q15;
#endif
}

/* The same term when the cosine is negative:
 * ((axisA_Q15 * axisB_Q15) >> 15) * absCos_Q15 + axisA_Q15 * axisB_Q15
 * + (crossTerm_Q15 << 15), in 32-bit arithmetic that may wrap, clamped as in
 * Math_Dot3Q15Wrapped and shifted down 15 bits. */
static __inline int Math_RodriguesTermNegativeCos(int axisA_Q15, int axisB_Q15,
						  int absCos_Q15,
						  int crossTerm_Q15)
{
#ifdef XVT_MODERN
	int32_t product = (int32_t)((uint32_t)axisA_Q15 * (uint32_t)axisB_Q15);
	int32_t value =
		(int32_t)((uint32_t)(product >> 15) * (uint32_t)absCos_Q15 +
			  (uint32_t)product + ((uint32_t)crossTerm_Q15 << 15));

	if (value >= 0x40000000) {
		value = 0x3FFFFFFF;
	}
	if (value <= -0x40000000) {
		value = -0x3FFF0000;
	}
	return value >> 15;
#else
	__asm {
		mov eax, axisA_Q15
		imul eax, axisB_Q15
		mov ebx, eax
		sar eax, 15
		imul eax, absCos_Q15
		mov edx, crossTerm_Q15
		shl edx, 15
		add eax, ebx
		add eax, edx
		cmp eax, 40000000h
		jl rodriguesNegativePositiveOk
		mov eax, 3fffffffh
	rodriguesNegativePositiveOk:
		cmp eax, 0c0000000h
		jg rodriguesNegativeNegativeOk
		mov eax, 0c0010000h
	rodriguesNegativeNegativeOk:
		sar eax, 15
		mov axisA_Q15, eax
	}
	return axisA_Q15;
#endif
}

void Math_SetFpuSinglePrecisionMode(void);
void Math_SetFpuExtendedPrecisionMode(void);
uint16_t Math_DivU16WithFractionQ16(uint16_t dividend, uint16_t divisor);
unsigned int Math_U16ToQ16(uint16_t value);

#ifdef __cplusplus
}
#endif

#endif
