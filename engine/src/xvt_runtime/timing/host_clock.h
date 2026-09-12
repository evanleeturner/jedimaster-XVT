#ifndef XVT_RUNTIME_HOST_CLOCK_H
#define XVT_RUNTIME_HOST_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void XvtTime_Reset(void);
void XvtTime_AdvanceHostClock(int32_t delta_us);
uint64_t XvtTime_GetElapsedUs(void);
uint32_t XvtTime_GetElapsedTicks(void);

#ifdef __cplusplus
}
#endif

#endif
