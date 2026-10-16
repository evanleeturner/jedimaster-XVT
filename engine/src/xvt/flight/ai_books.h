#ifndef XVT_FLIGHT_AI_BOOKS_H
#define XVT_FLIGHT_AI_BOOKS_H

/* The mission's books for the computer craft: when a flight group's
 * departure, abort and skip-to-order-4 triggers hold, which of a craft's
 * order slots are done, and counting a craft that leaves the battle by
 * hyperspace or into its mothership's hangar before freeing it. The 1997 AI
 * kept them in its plans; the pilot program now decides what a craft does,
 * and the engine keeps the counts by the 1997 rules, so a mission's goals are
 * met as they were. */

#include <stdint.h>

#include "xvt_runtime/ai/ai_wire.h"

struct mission_order;

#ifdef __cplusplus
extern "C" {
#endif

/* Keeps the books of the computer craft in object_index, as the 1997 plans
 * did on each think while it flew its orders, those its order's plan kept,
 * then writes into view what the question tells the pilot program about
 * them: each order slot's code and completion state, the current order's
 * throttle, variables and speed, the craft's full speed, the AI_WIRE_BOOKS_
 * bits, its mothership with that craft's two hangar points, its home
 * point, its group's start point once the group has arrived, and which of
 * its three orders have a target in the battle now. Call it before the view's order fields are read: a follower takes
 * its leader's order slot here. */
void ai_books_view_craft(uint16_t object_index,
			 struct ai_wire_craft_view *view);

/* Whether flight group group matches the order's target pair, 0 for targets 1
 * and 2 and 1 for targets 3 and 4, as the 1997 count joined them: both, or
 * either when the pair is joined by "or". */
int ai_books_pair_matches(const struct mission_order *order, int pair,
			  uint16_t group);

/* Whether the object in object_index, a static slot, counts as an order
 * target by the 1997 rule the books use: the slot holds an object, its type
 * carries behavior flag 2 and it may count as a target left. Whether it
 * matches a particular order is a separate question. */
int ai_books_static_is_target(uint16_t object_index);

/* The order action: the craft works on slot from now, and the slot it worked
 * on takes completion state state when state is not 0; a move to slot 3 marks
 * it skipped to order 4, and its orders are marked done when every order of
 * its group in slots 0 to 2 is. The reader let through only what the craft's
 * view allowed. */
void ai_books_apply_order(uint16_t object_index, uint8_t slot, uint8_t state);

/* The depart action: marks the craft entering hyperspace at full throttle,
 * its turns stopped, with the 1997 jump's two maneuver timers. flight.c then
 * speeds it up, and ai_books_finish_jumps counts and frees it at the jump
 * speed. */
void ai_books_depart(uint16_t object_index);

/* The dock action: counts the craft's departure by the 1997 rules, records
 * the outcome of the mothership it docked in, and frees it and the object it
 * carries. Does nothing but log when that mothership has left the battle
 * since the question. */
void ai_books_dock(uint16_t object_index);

/* Counts and frees each computer craft in the active region entering
 * hyperspace whose speed has reached 3,600, and the object it carries, as the
 * 1997 jump did. Called once per reference step. */
void ai_books_finish_jumps(void);

#ifdef __cplusplus
}
#endif

#endif
