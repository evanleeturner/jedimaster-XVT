#ifndef XVT_RUNTIME_FLIGHT_INTEGRATION_H
#define XVT_RUNTIME_FLIGHT_INTEGRATION_H
#include "xvt_runtime/timing/flight_state.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	XVT_INTEGRATE_SPIN_DECAY,
	XVT_INTEGRATE_SPIN_ANGLE,
	XVT_INTEGRATE_PUSH_X,
	XVT_INTEGRATE_PUSH_Y,
	XVT_INTEGRATE_PUSH_Z,
	XVT_INTEGRATE_HOME_YAW,
	XVT_INTEGRATE_HOME_PITCH,
	XVT_INTEGRATE_HOME_SPEED,
	XVT_INTEGRATE_ROLL,
	XVT_INTEGRATE_PITCH,
	XVT_INTEGRATE_TURN,
	XVT_INTEGRATE_BANK,
	XVT_INTEGRATE_COUNT
};

int XvtFlightIntegration_Init(size_t count);
void XvtFlightIntegration_Shutdown(void);
void XvtFlightIntegration_Reset(unsigned slot);
void XvtFlightIntegration_Clear(unsigned slot, unsigned channel);
int XvtFlightIntegration_Rate(unsigned slot, unsigned channel, int rate, unsigned elapsed, int divisor);
unsigned XvtFlightIntegration_Steer(unsigned slot, unsigned channel, uint16_t rate, uint16_t accel,
									uint16_t factor, int direction);
void XvtFlightIntegration_Move(unsigned slot);
void XvtFlightIntegration_Push(unsigned slot, unsigned axis, int* accum, int cap, int* output);
/* Canonical schema-1 record, 144 bytes. Decode validates before installation. */
void XvtFlightIntegration_Encode(unsigned slot, XvtIntegrationWire* out);
int XvtFlightIntegration_Decode(const XvtIntegrationWire* record, int apply);
void XvtFlightIntegration_ResetShared(void);

#ifdef __cplusplus
}
#endif

#endif
