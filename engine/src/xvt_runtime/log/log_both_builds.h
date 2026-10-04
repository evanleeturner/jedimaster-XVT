#ifndef XVT_RUNTIME_LOG_LOG_BOTH_BUILDS_H
#define XVT_RUNTIME_LOG_LOG_BOTH_BUILDS_H

/* The log macros for the 1997 game's code that both builds read (src/xvt). The
 * modern build, which defines XVT_MODERN, gets the real macros from log.h. The
 * original build is the record of what the 1997 game did, and it had no log:
 * there each macro stands for nothing, so a log line added since leaves that
 * build's code as it was. As in the modern build at a level that is off, the
 * arguments are never evaluated. */

#ifdef XVT_MODERN
#include "xvt_runtime/log/log.h"
#else
#define XVT_LOG_DEBUG(...) ((void)0)
#define XVT_LOG_INFO(...) ((void)0)
#define XVT_LOG_WARN(...) ((void)0)
#define XVT_LOG_ERROR(...) ((void)0)
#endif

#endif
