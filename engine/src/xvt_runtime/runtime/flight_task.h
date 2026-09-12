#ifndef XVT_RUNTIME_FLIGHT_TASK_H
#define XVT_RUNTIME_FLIGHT_TASK_H

#include <stdint.h>

int XvtFlightTask_Begin(const char* command);
void XvtFlightTask_Tick(void);
int XvtFlightTask_IsActive(void);
int XvtFlightTask_IsLoading(void);
int XvtFlightTask_IsComplete(void);
int XvtFlightTask_GetResult(void);
int XvtFlightTask_ContinuesWithoutFocus(void);
uint64_t XvtFlightTask_NextWakeDelayUs(void);
void XvtFlightTask_Shutdown(void);

#endif
