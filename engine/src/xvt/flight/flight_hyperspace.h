#ifndef XVT_FLIGHT_FLIGHT_HYPERSPACE_H
#define XVT_FLIGHT_FLIGHT_HYPERSPACE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void FlightHyperspace_DrawTransitionEffectObject(void);
void FlightHyperspace_RequestTransitionEffectInitialization(void);
void FlightHyperspace_RenderTransitionEffect(void);

#ifdef __cplusplus
}
#endif

#endif
