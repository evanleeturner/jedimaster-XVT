#ifndef XVT_RUNTIME_LAUNCH_TASK_H
#define XVT_RUNTIME_LAUNCH_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

int XvtLaunchTask_Queue(void);
void XvtLaunchTask_Tick(void);
int XvtLaunchTask_IsActive(void);
int XvtLaunchTask_HasPendingLaunch(void);
const char* XvtLaunchTask_BeginPendingLaunch(void);
void XvtLaunchTask_Complete(int succeeded);
void XvtLaunchTask_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
