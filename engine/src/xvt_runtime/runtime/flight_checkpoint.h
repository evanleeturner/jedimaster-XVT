#ifndef XVT_RUNTIME_FLIGHT_CHECKPOINT_H
#define XVT_RUNTIME_FLIGHT_CHECKPOINT_H
#include "xvt_runtime/timing/flight_state.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void XvtFlightCheckpoint_Begin(uint8_t mask);
size_t XvtFlightCheckpoint_Maximum(void);
size_t XvtFlightCheckpoint_Append(uint8_t* image, size_t prefix);
int XvtFlightCheckpoint_Validate(const uint8_t* image, size_t size, size_t* prefix, int* tick);

typedef struct XvtFlightCheckpointView {
	size_t prefix;
	int tick;
	unsigned objects;
	const uint8_t *reference, *integration, *players, *paired;
	XvtMembershipWire membership;
} XvtFlightCheckpointView;

int XvtFlightCheckpoint_Read(const uint8_t* image, size_t size, XvtFlightCheckpointView* view);
/* Install only a view validated with Read and the recovered world prefix. */
void XvtFlightCheckpoint_Restore(const XvtFlightCheckpointView* view);
void XvtFlightCheckpoint_SavePlayer(unsigned player, int tick);
void XvtFlightCheckpoint_RestorePlayer(unsigned player);
void XvtFlightCheckpoint_InvalidatePlayer(unsigned player);
uint8_t XvtFlightCheckpoint_InitialMask(void);
uint8_t XvtFlightCheckpoint_ConfirmedMask(void);
void XvtFlightCheckpoint_SetMask(uint8_t mask);
#ifdef __cplusplus
}
#endif
#endif
