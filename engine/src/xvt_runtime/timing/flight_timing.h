#ifndef XVT_RUNTIME_FLIGHT_TIMING_H
#define XVT_RUNTIME_FLIGHT_TIMING_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flight timing for one flight: the profile, and when logic written for the original step runs. The
 * native profile is locked: every simulation step is a reference step. The unlocked profiles, offline
 * and network 125, allow steps shorter than a reference period; a step is then a reference step only
 * when it crosses a boundary of XVT_REFERENCE_TICKS. Reference logic runs between EnterReference and
 * RestoreClock, which, when unlocked, make the step globals describe one reference period. */

/* g_elapsedTicks and g_simStepScale, as EnterReference saves them. */
typedef struct XvtFlightClock {
	uint16_t elapsed, scale;
} XvtFlightClock;

typedef enum XvtFlightTimingProfile {
	XVT_FLIGHT_TIMING_NATIVE,
	XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED,
	XVT_FLIGHT_TIMING_NETWORK_125
} XvtFlightTimingProfile;

/* Clears all timing state and selects profile; every profile but NATIVE is unlocked. Logs the profile. */
void XvtFlightTiming_BeginSession(XvtFlightTimingProfile profile);
/* The session's profile; NATIVE after EndSession. */
XvtFlightTimingProfile XvtFlightTiming_Profile(void);
/* 1 in a NETWORK_125 session. */
int XvtFlightTiming_IsNetwork125(void);
/* The longest simulation step, in ticks: XVT_NETWORK_STEP_TICKS in NETWORK_125, otherwise the current
 * g_netUpdateIntervalTicks. */
int XvtFlightTiming_SimulationMaximum(void);
/* After a NETWORK_125 state restore at tick: sets the advance serial to tick / XVT_NETWORK_STEP_TICKS
 * and the reference phase to tick % XVT_REFERENCE_TICKS, closes any open step, and rebuilds the last
 * animation update's time and serial from tick and the crew mesh rotation timer, as 0 when that time
 * is not positive. Does nothing outside NETWORK_125, or for a negative tick or one that is not a
 * multiple of XVT_NETWORK_STEP_TICKS. */
void XvtFlightTiming_RestoreNetworkTick(int tick);
/* Clears all timing state, which reads as the NATIVE profile: locked, so ReferenceDue returns 1 until
 * the next session. */
void XvtFlightTiming_EndSession(void);
/* 1 in an OFFLINE_UNLOCKED or NETWORK_125 session. */
int XvtFlightTiming_IsUnlocked(void);
/* The profile's step size in ticks: XVT_NETWORK_STEP_TICKS in NETWORK_125, XVT_OFFLINE_STEP_TICKS when
 * offline unlocked, XVT_NATIVE_STEP_TICKS when native. The non-network frame loop gathers this many
 * ticks before it advances the simulation. */
unsigned XvtFlightTiming_StepTicks(void);
/* Opens a simulation step of elapsed ticks and counts it in the advance serial; 0 opens none. When
 * unlocked, adds elapsed to the reference phase: crossing a boundary makes this a reference step, and
 * whole periods past the first are counted as dropped, with a warning logged once 256 more have dropped
 * since the last one. */
void XvtFlightTiming_BeginAdvance(uint16_t elapsed);
/* Closes the current step; while unlocked, ReferenceDue returns 0 until the next reference step. */
void XvtFlightTiming_EndAdvance(void);
/* 1 when reference logic should run: always when locked; when unlocked, only between BeginAdvance and
 * EndAdvance of a reference step. */
int XvtFlightTiming_ReferenceDue(void);
/* The step length reference logic should use: g_elapsedTicks when locked; when unlocked,
 * XVT_REFERENCE_TICKS in a reference step and 0 otherwise. */
uint16_t XvtFlightTiming_ReferenceElapsed(void);
/* The number of nonzero steps opened this session, or the count RestoreNetworkTick set. */
uint64_t XvtFlightTiming_AdvanceSerial(void);
/* Returns g_elapsedTicks and g_simStepScale for RestoreClock. When unlocked, sets them to one reference
 * period, XVT_REFERENCE_TICKS and SIMULATION_TICKS_PER_SECOND / XVT_REFERENCE_TICKS; when locked,
 * changes nothing. It does not check ReferenceDue; callers do. */
XvtFlightClock XvtFlightTiming_EnterReference(void);
/* Puts back the step globals EnterReference returned. */
void XvtFlightTiming_RestoreClock(XvtFlightClock clock);
/* Records an animation update at the end of the current step, g_gameTime + g_elapsedTicks. In
 * NETWORK_125 its serial is that time / XVT_COMPONENT_EVENT_TICKS + 1, the formula RestoreNetworkTick
 * uses; otherwise the serial counts up by one. */
void XvtFlightTiming_AnimationEvent(void);
/* The latest animation update's serial; 0 before the first. */
uint64_t XvtFlightTiming_AnimationSerial(void);
/* The latest animation update's time; 0 before the first. */
int XvtFlightTiming_AnimationTime(void);
#ifdef __cplusplus
}
#endif

#endif
