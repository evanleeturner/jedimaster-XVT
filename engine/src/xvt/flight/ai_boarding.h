#ifndef XVT_FLIGHT_AI_BOARDING_H
#define XVT_FLIGHT_AI_BOARDING_H

/* The dock of a computer craft on a boarding order, ported from the 1997
 * AI's board maneuver (paiman.c) and its abort order (paiorder.c). The pilot
 * program flies the approach to the board point; at the board action the
 * engine takes the craft over until the dock ends: it aligns with the
 * target's docking hatch, transfers by the craft's order and separates. Its
 * state is the craft's maneuver mode (BOARD while it docks), maneuver phase
 * (1 align, 2 transfer, 3 separate), the two maneuver timers and the push
 * accumulators, all of which ride the world save. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ai_boarding_phase {
	/* No dock: the pilot program flies, approach included. */
	AI_BOARDING_PHASE_NONE = 0,
	AI_BOARDING_PHASE_ALIGN = 1,
	AI_BOARDING_PHASE_TRANSFER = 2,
	AI_BOARDING_PHASE_SEPARATE = 3,
};

/* The craft's dock phase, an ai_boarding_phase value: its maneuver phase
 * while its maneuver mode is BOARD, else 0. */
uint8_t ai_boarding_phase(uint16_t object_index);

/* The board action naming target: starts the dock when the craft is at its
 * board point for its AI target, which must be target, on a boarding order,
 * and not already docking: throttle 0, phase align. Anything else is declined
 * with a log line and changes nothing, as the world may have changed since the
 * question. */
void ai_boarding_start(uint16_t object_index, uint16_t target);

/* The board action naming no object: stops a dock in its align or transfer
 * phase, as an abort does but keeping the craft's target. Does nothing in any
 * other phase. */
void ai_boarding_stop(uint16_t object_index);

/* One step of the craft's dock, run on its think before its view is built:
 * the abort tests in the align and transfer phases, then the phase's work. A
 * craft with no dock is left alone. */
void ai_boarding_step(uint16_t object_index);

/* Keeps the craft's maneuver mode true to its dock: BOARD while it docks, or
 * while its activity is BOARDING on a boarding order (the approach, so the
 * push rate and the collision rules of a boarder act); NULL when it was BOARD
 * and neither holds. Called after each answer. */
void ai_boarding_sync_mode(uint16_t object_index);

#ifdef __cplusplus
}
#endif

#endif
