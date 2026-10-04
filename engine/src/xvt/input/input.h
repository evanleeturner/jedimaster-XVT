#ifndef XVT_INPUT_INPUT_H
#define XVT_INPUT_INPUT_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_joystick_detection_cached;
extern int g_joystick_backend_initialized;

int input_initialize_joystick_backend(void);
int input_detect_active_joystick(void);
int input_probe_active_joystick_devices(void);

#ifdef __cplusplus
}
#endif

#endif
