#ifndef XVT_FRONTEND_FRONTEND_FLIGHT_H
#define XVT_FRONTEND_FRONTEND_FLIGHT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_flightLoadingReadyScreenStartMs;
extern int g_flightLoadingReadyScreenNowMs;
extern int g_unusedFlightLoadingReadyScreenFlag;
extern int g_frontendLaunchHumanPlayerCount;
extern char g_frontendFlightCommandLine[256];

int FlightLoading_UpdateReadyScreen(int frameCounter);
int FrontendFlight_NoOpExit(int frameCounter);
int FrontendFlight_LaunchSession(int frameCounter);

#ifdef __cplusplus
}
#endif

#endif
