#ifndef XVT_UTIL_TIME_H
#define XVT_UTIL_TIME_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t timeGetTime(void);
uint32_t GetTickCount(void);
void Time_ResetFrameDeltaClocks(void);
uint32_t Time_GetFrameDelta(void);

#ifdef __cplusplus
}
#endif

#endif
