#ifndef XVT_RUNTIME_REFERENCE_MOTION_H
#define XVT_RUNTIME_REFERENCE_MOTION_H
#include "xvt_runtime/timing/flight_state.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int XvtReferenceMotion_Init(size_t count);
void XvtReferenceMotion_Shutdown(void);
void XvtReferenceMotion_Reset(unsigned slot);
void XvtReferenceMotion_Committed(unsigned slot, int timestamp);
void XvtReferenceMotion_CommitBoundary(void);
void XvtReferenceMotion_Displacement(unsigned slot, int32_t delta[3]);
int32_t XvtReferenceMotion_Axis(unsigned slot, unsigned axis);
/* Canonical schema-1 record, 28 bytes. Decode validates before installation. */
void XvtReferenceMotion_Encode(unsigned slot, XvtReferenceMotionWire* out);
int XvtReferenceMotion_Decode(const XvtReferenceMotionWire* record, int apply);
void XvtReferenceMotion_ResetShared(void);

#ifdef __cplusplus
}
#endif

#endif
