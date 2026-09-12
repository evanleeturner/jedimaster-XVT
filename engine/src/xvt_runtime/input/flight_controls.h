#ifndef XVT_RUNTIME_INPUT_FLIGHT_CONTROLS_H
#define XVT_RUNTIME_INPUT_FLIGHT_CONTROLS_H
#include "xvt/flight/flight_input.h"
#include <stdbool.h>

enum { XVT_FLIGHT_AXIS_BYTES = 3 };

extern int16_t g_xvtControlRoll;
uint16_t XvtFlightControls_ReadLocal(void);
void XvtFlightControls_Reset(void);
void XvtFlightControls_UpdateThrottleContext(void);
bool XvtFlightControls_ThrottleEligible(unsigned player);
void XvtFlightControls_SampleThrottle(FlightInputFrameRecord* input);
void XvtFlightControls_ApplyThrottle(unsigned player, const FlightInputFrameRecord* input);
void XvtFlightControls_Recover(void);
void XvtFlightControls_SampleRecorded(FlightInputFrameRecord* input);
void XvtFlightControls_EncodeAxes(uint8_t* bytes, const FlightInputFrameRecord* input);
void XvtFlightControls_DecodeAxes(const uint8_t* bytes, FlightInputFrameRecord* input);
int16_t XvtFlightControls_RollStep(unsigned player, uint16_t roll_rate, int16_t modifier_step);
#endif
