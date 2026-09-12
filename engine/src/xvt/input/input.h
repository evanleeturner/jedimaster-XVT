#ifndef XVT_INPUT_INPUT_H
#define XVT_INPUT_INPUT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_joystickDetectionCached;
extern int g_joystickBackendInitialized;

int Input_InitializeJoystickBackend(void);
int Input_DetectActiveJoystick(void);
int Input_ProbeActiveJoystickDevices(void);

#ifdef __cplusplus
}
#endif

#endif
