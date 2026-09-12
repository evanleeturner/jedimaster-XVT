#ifndef XVT_RUNTIME_CD_TASK_H
#define XVT_RUNTIME_CD_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int XvtCdTask_BeginFade(unsigned int from, unsigned int to, int duration_ms);
int XvtCdTask_IsFading(void);
void XvtCdTask_Cancel(void);
void XvtCdTask_Tick(void);
uint64_t XvtCdTask_NextWakeDelayUs(void);

#ifdef __cplusplus
}
#endif

#endif
