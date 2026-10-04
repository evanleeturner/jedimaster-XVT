#ifndef XVT_UTIL_TIME_H
#define XVT_UTIL_TIME_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t timeGetTime(void);
uint32_t GetTickCount(void);
void time_reset_elapsed_ticks(void);
uint32_t time_consume_elapsed_ticks(void);

#ifdef __cplusplus
}
#endif

#endif
