#ifndef XVT_RUNTIME_FLIGHT_SIM_H
#define XVT_RUNTIME_FLIGHT_SIM_H
#include "xvt/net/flight_sync.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum XvtInputInsertStatus {
	XVT_INPUT_INSERTED,
	XVT_INPUT_DUPLICATE,
	XVT_INPUT_FULL,
	XVT_INPUT_INVALID,
	XVT_INPUT_CONFLICT
} XvtInputInsertStatus;

XvtInputInsertStatus XvtFlightHistory_Insert(unsigned player, int tick, const FlightInputFrameRecord* input,
											 InputFrame** out);
XvtInputInsertStatus XvtFlightHistory_InsertReal(unsigned player, int tick,
												 const FlightInputFrameRecord* input, int authoritative);

void XvtFlightSim_Reset(void);

typedef enum XvtFlightStepResult {
	XVT_STEP_PENDING,
	XVT_STEP_COMPLETE,
	XVT_STEP_TERMINAL
} XvtFlightStepResult;

XvtFlightStepResult XvtFlightSim_StepToTime(int targetGameTime);
void XvtFlightHistory_RestoreCheckpoint(void);
void XvtFlightHistory_Recover(void);
int XvtFlightSim_Advance(int targetGameTime);
int XvtFlightSim_UpdateEntity(int playerIdx);
int XvtFlightSim_IsPaused(void);
int XvtFlightSim_Resume(void);

#ifdef __cplusplus
}
#endif
#endif
