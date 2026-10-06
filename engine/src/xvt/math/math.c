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
