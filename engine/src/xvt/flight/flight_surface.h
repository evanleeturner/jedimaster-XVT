#ifndef XVT_FLIGHT_FLIGHT_SURFACE_H
#define XVT_FLIGHT_FLIGHT_SURFACE_H

#include "aeron/compat/ddraw.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void* g_swFramebufferBase;
extern int g_flightPageFlip;
extern int g_flightLockBackBufferForHudDraw;
extern IDirectDrawSurface* g_flightOffscreenSurface;
extern uint8_t g_flightDisplaySurfacesActive;
extern int32_t g_flightNetClockLeadAllowanceMs;
extern uint8_t g_flightSurfaceAlreadyLocked;
extern int g_surfaceLockCount;

void FlightSurface_ClearToBlack(void);
int FlightSurface_GetLockCount(void);
void FlightSurface_Lock(void);
void FlightSurface_Unlock(void);
int FlightSurface_SetViewport480ByteSpan(int byteSpan);
void* FlightSurface_SetSoftwareFramebufferBase(void* framebufferBase);
void* FlightSurface_GetSoftwareFramebufferBase(void);

#ifdef __cplusplus
}
#endif

#endif
