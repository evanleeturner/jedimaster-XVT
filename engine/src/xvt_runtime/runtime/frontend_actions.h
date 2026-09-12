#ifndef XVT_RUNTIME_FRONTEND_ACTIONS_H
#define XVT_RUNTIME_FRONTEND_ACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

enum { XVT_ACTION_COMMON = 1, XVT_ACTION_PILOT, XVT_ACTION_CONFIG, XVT_ACTION_CONCOURSE };

int XvtFrontendAction_Trigger(int owner, int action, int pressed);
int XvtFrontendAction_Pending(int owner);
void XvtFrontendAction_Finish(int owner);
void XvtFrontendAction_Reset(void);

#ifdef __cplusplus
}
#endif

#endif
