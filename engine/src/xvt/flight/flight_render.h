#ifndef XVT_FLIGHT_FLIGHT_RENDER_H
#define XVT_FLIGHT_FLIGHT_RENDER_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void FlightRender_TransitionHookStub(void);
void FlightRender_InvokeTransitionHook(int transitionFlags);
void FlightRender_ResetPalette(int transitionFlags);
void FlightRender_ConfigureCallbacksForResolution(uint8_t initialGraphicsDetailPreset);
void FlightRender_InstallCallbacks(int pixelMode);
void FlightRender_SetPixelModeStub(int pixelMode);

#ifdef __cplusplus
}
#endif

#endif
