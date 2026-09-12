#ifndef XVT_RUNTIME_FLIGHT_TIMING_H
#define XVT_RUNTIME_FLIGHT_TIMING_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XvtFlightClock {
	uint16_t elapsed, scale;
} XvtFlightClock;

typedef enum XvtFlightTimingProfile {
	XVT_FLIGHT_TIMING_NATIVE,
	XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED,
	XVT_FLIGHT_TIMING_NETWORK_125
} XvtFlightTimingProfile;

void XvtFlightTiming_BeginSession(XvtFlightTimingProfile profile);
XvtFlightTimingProfile XvtFlightTiming_Profile(void);
int XvtFlightTiming_IsNetwork125(void);
int XvtFlightTiming_SimulationMaximum(void);
void XvtFlightTiming_RestoreNetworkTick(int tick);
void XvtFlightTiming_EndSession(void);
int XvtFlightTiming_IsUnlocked(void);
unsigned XvtFlightTiming_StepTicks(void);
void XvtFlightTiming_BeginAdvance(uint16_t elapsed);
void XvtFlightTiming_EndAdvance(void);
int XvtFlightTiming_ReferenceDue(void);
uint16_t XvtFlightTiming_ReferenceElapsed(void);
uint64_t XvtFlightTiming_AdvanceSerial(void);
XvtFlightClock XvtFlightTiming_EnterReference(void);
void XvtFlightTiming_RestoreClock(XvtFlightClock clock);
void XvtFlightTiming_AnimationEvent(void);
uint64_t XvtFlightTiming_AnimationSerial(void);
int XvtFlightTiming_AnimationTime(void);
#ifdef __cplusplus
}
#endif

#endif
