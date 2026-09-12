#ifndef XVT_FRONTEND_FRONTEND_MOUSE_H
#define XVT_FRONTEND_FRONTEND_MOUSE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int FrontendMouse_SetInputGate(int gateId);
int FrontendMouse_ClearInputGate(void);
int FrontendMouse_GetLeftDown(void);
int FrontendMouse_GetRightDown(void);
int FrontendMouse_GetLeftClick(void);
int FrontendMouse_GetRightClick(void);
int FrontendMouse_GetLeftClickFor(int gateId);
int FrontendMouse_GetRightClickFor(int gateId);
int FrontendMouse_IsGateOwner(int gateId);
int FrontendMouse_IsGateOpen(void);
int FrontendMouse_ClearClicks(void);

#ifdef __cplusplus
}
#endif

#endif
