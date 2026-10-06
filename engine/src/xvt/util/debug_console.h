#ifndef XVT_UTIL_DEBUG_CONSOLE_H
#define XVT_UTIL_DEBUG_CONSOLE_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

void debug_printf(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif
