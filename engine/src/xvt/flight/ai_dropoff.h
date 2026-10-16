#ifndef XVT_FLIGHT_AI_DROPOFF_H
#define XVT_FLIGHT_AI_DROPOFF_H

/* The drop of a computer craft on a Drop Off order, ported from the 1997 AI's
 * drop-off maneuver (paiman.c). The pilot program flies the approach to the
 * drop point; at the drop off action the engine takes the craft over until
 * the drop ends: it pushes the carrier to each place of the delivered group in
 * turn, brings the group in there one craft (or one mine) at a time, and
 * waits after each delivery. Its state is the craft's maneuver mode (DROPOFF
 * while it drops), maneuver phase (1 push to the place, 2 wait after a
 * delivery), maneuver timer, push accumulators, its waypoint index (the place:
 * the places delivered) and its unused0b (the order slot the drop started
 * under), all of which ride the world save. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ai_dropoff_phase {
	/* No drop: the pilot program flies, the approach included. */
	AI_DROPOFF_PHASE_NONE = 0,
	AI_DROPOFF_PHASE_PUSH = 1,
	AI_DROPOFF_PHASE_WAIT = 2,
};

/* The craft's drop phase, an ai_dropoff_phase value: its maneuver phase while
 * its maneuver mode is DROPOFF, else 0. */
uint8_t ai_dropoff_phase(uint16_t object_index);

/* The drop off action with 1: starts the drop when the craft is at its drop
 * point (the books' AT_DROP_POINT) and neither a drop nor a dock runs: roll,
 * pitch and turn stopped, throttle 0, place 0, phase push. Anything else is
 * declined with a log line and changes nothing, as the world may have
 * changed since the question. */
void ai_dropoff_start(uint16_t object_index);

/* The drop off action with 0: ends a running drop at once, keeping the places
 * delivered. Does nothing but log when none runs. */
void ai_dropoff_stop(uint16_t object_index);

/* One step of the craft's drop, run on its think before its view is built: the
 * drop ends when the craft's current order is no longer the Drop Off it
 * started under (the new slot's progress is left alone); then the phase's
 * work, and the places delivered written to the order slot's goal progress. A
 * craft with no drop is left alone. */
void ai_dropoff_step(uint16_t object_index);

/* Keeps the craft's maneuver mode true to its drop: DROPOFF while it drops,
 * NULL when it was DROPOFF and no drop runs. Called after each answer. */
void ai_dropoff_sync_mode(uint16_t object_index);

#ifdef __cplusplus
}
#endif

#endif
