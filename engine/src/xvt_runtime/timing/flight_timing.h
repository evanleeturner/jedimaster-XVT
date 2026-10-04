#ifndef XVT_RUNTIME_FLIGHT_TIMING_H
#define XVT_RUNTIME_FLIGHT_TIMING_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flight timing for one flight: the profile, and when logic written for the
 * original step runs. The native profile is locked: every simulation step is a
 * reference step. The unlocked profiles, offline and network 125, allow steps
 * shorter than a reference period; a step is then a reference step only when it
 * crosses a boundary of XVT_REFERENCE_TICKS. Reference logic runs between
 * EnterReference and RestoreClock, which, when unlocked, make the step globals
 * describe one reference period. */

/* g_elapsed_ticks and g_sim_steps_per_second, as EnterReference saves them. */
struct xvt_flight_clock {
	uint16_t elapsed;
	uint16_t steps_per_second;
};

typedef enum xvt_flight_timing_profile {
	XVT_FLIGHT_TIMING_NATIVE,
	XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED,
	XVT_FLIGHT_TIMING_NETWORK_125
} xvt_flight_timing_profile;

/* Clears all timing state and selects profile; every profile but NATIVE is
 * unlocked. Logs the profile. */
void xvt_flight_timing_begin_session(xvt_flight_timing_profile profile);
/* The session's profile; NATIVE after EndSession. */
xvt_flight_timing_profile xvt_flight_timing_session_profile(void);
/* 1 in a NETWORK_125 session. */
int xvt_flight_timing_is_network125(void);
/* The longest simulation step, in ticks: XVT_NETWORK_STEP_TICKS in NETWORK_125,
 * otherwise the current g_net_update_interval_ticks. */
int xvt_flight_timing_maximum_step_ticks(void);
/* After a NETWORK_125 state restore at tick: sets the advance serial to tick /
 * XVT_NETWORK_STEP_TICKS and the reference phase to tick % XVT_REFERENCE_TICKS,
 * closes any open step, and rebuilds the last animation update's time and
 * serial from tick and the special-behavior update timer, as 0 when that time
 * is not positive. Does nothing outside NETWORK_125, or for a negative tick or
 * one that is not a multiple of XVT_NETWORK_STEP_TICKS. */
void xvt_flight_timing_restore_network_tick(int tick);
/* Clears all timing state, which reads as the NATIVE profile: locked, so
 * ReferenceDue returns 1 until the next session. */
void xvt_flight_timing_end_session(void);
/* 1 in an OFFLINE_UNLOCKED or NETWORK_125 session. */
int xvt_flight_timing_is_unlocked(void);
/* The profile's step size in ticks: XVT_NETWORK_STEP_TICKS in NETWORK_125,
 * XVT_OFFLINE_STEP_TICKS when offline unlocked, XVT_NATIVE_STEP_TICKS when
 * native. The non-network frame loop gathers this many ticks before it advances
 * the simulation. */
unsigned xvt_flight_timing_step_ticks(void);
/* Opens a simulation step of elapsed ticks and counts it in the advance serial;
 * 0 opens none. When unlocked, adds elapsed to the reference phase: crossing a
 * boundary makes this a reference step, and whole periods past the first are
 * counted as dropped, with a warning logged once 256 more have dropped since
 * the last one. */
void xvt_flight_timing_begin_advance(uint16_t elapsed);
/* Closes the current step; while unlocked, ReferenceDue returns 0 until the next reference step. */
void xvt_flight_timing_end_advance(void);
/* 1 when reference logic should run: always when locked; when unlocked, only
 * between BeginAdvance and EndAdvance of a reference step. */
int xvt_flight_timing_reference_due(void);
/* The step length reference logic should use: g_elapsed_ticks when locked; when unlocked,
 * XVT_REFERENCE_TICKS in a reference step and 0 otherwise. */
uint16_t xvt_flight_timing_reference_elapsed(void);
/* The number of nonzero steps opened this session, or the count RestoreNetworkTick set. */
uint64_t xvt_flight_timing_advance_serial(void);
/* Returns g_elapsed_ticks and g_sim_steps_per_second for RestoreClock. When
 * unlocked, sets them to one reference period, XVT_REFERENCE_TICKS and
 * SIMULATION_TICKS_PER_SECOND / XVT_REFERENCE_TICKS; when locked, changes
 * nothing. It does not check ReferenceDue; callers do. */
struct xvt_flight_clock xvt_flight_timing_enter_reference(void);
/* Puts back the step globals EnterReference returned. */
void xvt_flight_timing_restore_clock(struct xvt_flight_clock clock);
/* Records an animation update at the end of the current step, g_game_time +
 * g_elapsed_ticks. In NETWORK_125 its serial is that time /
 * XVT_COMPONENT_EVENT_TICKS + 1, the formula RestoreNetworkTick uses; otherwise
 * the serial counts up by one. */
void xvt_flight_timing_animation_event(void);
/* The latest animation update's serial; 0 before the first. */
uint64_t xvt_flight_timing_animation_serial(void);
/* The latest animation update's time; 0 before the first. */
int xvt_flight_timing_animation_time(void);
#ifdef __cplusplus
}
#endif

#endif
