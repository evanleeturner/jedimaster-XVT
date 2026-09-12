#ifndef XVT_RUNTIME_FRONTEND_TASK_H
#define XVT_RUNTIME_FRONTEND_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int XvtFrontendTask_Init(int skip_intro);
void XvtFrontendTask_Tick(void);
int XvtFrontendTask_RunFrame(void);
void XvtFrontendTask_ServiceFrameSystems(void);
int XvtFrontendTask_ShouldQuit(void);
uint64_t XvtFrontendTask_NextWakeDelayUs(void);
void XvtFrontendTask_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
