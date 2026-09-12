#ifndef XVT_RUNTIME_FLIGHT_PREDICTION_H
#define XVT_RUNTIME_FLIGHT_PREDICTION_H

#include "xvt/flight/flight_input.h"

void XvtFlightPrediction_Reset(void);
/* Only successfully replayed authoritative controls establish the fallback. */
void XvtFlightPrediction_Confirm(unsigned player, int tick, const FlightInputFrameRecord* input);
int XvtFlightPrediction_Queue(int tick);

#endif
