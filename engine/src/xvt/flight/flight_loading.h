#ifndef XVT_FLIGHT_FLIGHT_LOADING_H
#define XVT_FLIGHT_FLIGHT_LOADING_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void FlightLoading_ResetProgressState(void);
extern uint32_t g_flightLoadingProgressStep;
void FlightLoading_PulseAndDrawProgressScreen(void);
void FlightLoading_DrawProgressToCompletion(void);
int PilotData_HasNetworkPlayerDpid(int dpid);

#ifdef __cplusplus
}
#endif

#endif
