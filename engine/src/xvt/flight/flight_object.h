#ifndef XVT_FLIGHT_FLIGHT_OBJECT_H
#define XVT_FLIGHT_FLIGHT_OBJECT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint16_t g_billboardTextureSequenceIndex;
extern int16_t* g_billboardTextureFrameSequence;
extern uint16_t g_localDebrisRecycleSlotCursor;

void FlightObject_UpdateSpecialBehavior(void);
void FlightObject_AdvanceTextureFrameSequence(unsigned int objectIdx);
void FlightObject_UpdatePlayerHyperspaceTransition(int playerIdx);
void FlightObject_UpdateDebrisAndTransientAnimations(void);

#ifdef __cplusplus
}
#endif

#endif
