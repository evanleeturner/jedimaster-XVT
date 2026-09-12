#include "xvt/math/math.h"

#ifndef XVT_MODERN
#include <float.h>
#endif

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x408110
void Math_SetFpuSinglePrecisionMode(void) {
#ifdef XVT_MODERN
	// PORT: supported 64-bit hosts use SSE arithmetic rather than x87 precision control.
#else
	enum {
		FPU_PRECISION_CONTROL_MASK = 0x00030000,
		FPU_SINGLE_PRECISION = 0x00020000,
	};

	_control87(FPU_SINGLE_PRECISION, FPU_PRECISION_CONTROL_MASK);
#endif
}

// FUNCTION: XVT 0x408140
void Math_SetFpuExtendedPrecisionMode(void) {
#ifdef XVT_MODERN
	// PORT: supported 64-bit hosts use SSE arithmetic rather than x87 precision control.
#else
	enum {
		FPU_PRECISION_CONTROL_MASK = 0x00030000,
		FPU_EXTENDED_PRECISION = 0x00000000,
	};

	_control87(FPU_EXTENDED_PRECISION, FPU_PRECISION_CONTROL_MASK);
#endif
}

// FUNCTION: XVT 0x425BE0
uint16_t Math_DivU16WithFractionQ16(uint16_t dividend, uint16_t divisor) {
	(void)dividend;
	(void)divisor;

	/* TODO: Reimplement Math_DivU16WithFractionQ16 @ 0x425BE0. */
	return 0;
}

// FUNCTION: XVT 0x425E00
unsigned int Math_U16ToQ16(uint16_t value) { return value << 16; }
