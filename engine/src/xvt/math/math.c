#include "xvt/math/math.h"


/* Sets the x87 unit's precision control to single precision (24-bit
 * mantissa) through _control87, leaving its other control bits as they are.
 * Does nothing in the modern build. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x408110
void math_set_fpu_single_precision_mode(void)
{
	// PORT: supported 64-bit hosts use SSE arithmetic rather than x87 precision control.
}

/* Sets the x87 unit's precision control to extended precision (64-bit
 * mantissa) through _control87, leaving its other control bits as they are.
 * Does nothing in the modern build. */
// FUNCTION: XVT 0x408140
void math_set_fpu_extended_precision_mode(void)
{
	// PORT: supported 64-bit hosts use SSE arithmetic rather than x87 precision control.
}

/* A placeholder for the 1997 code at this address, which is not rebuilt:
 * returns 0 for every input. Nothing calls this. */
// FUNCTION: XVT 0x425BE0
uint16_t math_div_u16_with_fraction_q16(uint16_t dividend, uint16_t divisor)
{
	(void)dividend;
	(void)divisor;

	/* TODO: Reimplement math_div_u16_with_fraction_q16 @ 0x425BE0. */
	return 0;
}

/* Returns value << 16, the value as a 16.16 fixed-point number. Nothing calls
 * this. */
// FUNCTION: XVT 0x425E00
unsigned int math_u16_to_q16(uint16_t value) { return value << 16; }
