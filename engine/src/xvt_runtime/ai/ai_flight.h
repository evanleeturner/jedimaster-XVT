#ifndef XVT_RUNTIME_AI_AI_FLIGHT_H
#define XVT_RUNTIME_AI_AI_FLIGHT_H

/* The AI program for every flight the launch door did not start: a battle
 * begun from the game's own screens, solo or in a network game, runs the
 * program the settings name (ai.python and ai.program) from its start to its
 * end. In a network game each machine runs its own copy, as each runs the
 * whole battle. A flight the launch door started keeps the door's program,
 * or none. With no program set, computer craft sit, and a warning says so
 * once a run. A program that fails during a flight ends that flight, as a
 * network fault does; one that cannot be started leaves its craft sitting. */

#include <stdint.h>

#include "xvt_runtime/config/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What ai_flight_begin did. */
enum ai_flight_start {
	/* The launch door started the flight; its program, if any, serves. */
	AI_FLIGHT_DOOR = 0,
	/* No program is set: computer craft sit. */
	AI_FLIGHT_UNSET = 1,
	/* The program set could not be started: computer craft sit. */
	AI_FLIGHT_UNSTARTED = 2,
	/* The program set runs and is asked about computer craft. */
	AI_FLIGHT_STARTED = 3,
};

/* Keeps the run's id and the file the program's log goes to ("" for
 * standard error only), from xvt_log_sink_ai_log, for every later start. */
void ai_flight_set_log(uint32_t run_id, const char *ai_log_file);

/* Called as a flight starts. Unless door_active, starts the program settings
 * names (settings may be NULL: nothing is loaded) and has the flight ask it.
 * Returns an ai_flight_start value. */
int ai_flight_begin(const struct xvt_settings *settings, int door_active);

/* Called as a flight ends: stops the program ai_flight_begin started, and the
 * flight asks no program. Does nothing when it started none. */
void ai_flight_end(void);

#ifdef __cplusplus
}
#endif

#endif
