#include "xvt/util/debug_console.h"

#include <stdio.h>
#include <string.h>

#include "xvt/assets/file.h"

/* Does nothing in either build: its calls print nothing, and it reads none of
 * its arguments. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4079E0
void debug_printf(const char *format, ...) { (void)format; }

#if defined(_MSC_VER) && _MSC_VER <= 1100
#pragma function(memcpy)
#endif
#if defined(_MSC_VER) && _MSC_VER <= 1100
#pragma intrinsic(memcpy)
#endif
