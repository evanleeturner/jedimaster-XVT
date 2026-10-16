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
 * its three orders have a target in the battle now, and what a boarder
 * sees: its board point and the boardings it has done and asked for. Call it before the view's order fields are read: a follower takes
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

/* Whether the mission order code boards a craft: Give, Take, Exchange,
 * Capture, Destroy and Pick Up cargo (12 to 17), SS Board (31) and Repair
 * (32). */
int ai_books_order_boards(int order_code);

/* What the craft's board point is: the board point and the books' BOARD_POINT
 * and AT_BOARD_POINT bits for the craft's AI target (its last target action)
 * when its current order boards and the target is a craft in the active
 * region (point 0, 0, 0 and no bits otherwise). The craft is at the point when
 * its rough polar distance to it is at most 2,048, the 1997 test, unless the
 * target is a moving player's craft, which is asked to stop instead: then
 * *ask_target is that craft, else UINT16_MAX. Changes nothing. */
uint32_t ai_books_board_bits(uint16_t object_index,
			     int32_t point[AI_WIRE_POINT_AXES],
			     uint16_t *ask_target);

/* What the craft's drop point is: the books' DROP_POINT, AT_DROP_POINT and
 * ALL_DROPPED bits for a craft whose current order is Drop Off, and the drop
 * point (slot 0 of the delivered group, 932 above it; 0, 0, 0 without
 * DROP_POINT). drop_phase is the craft's drop phase (ai_dropoff_phase). The
 * group is variable2 minus 1; its delivered count is the order slot's goal
 * progress, its total the group's outcome_count[TOTAL]. DROP_POINT: the group
 * is one of the mission's, delivered is below the total (at most 255, the
 * counters are 8 bits) and the craft has no leader. AT_DROP_POINT: DROP_POINT,
 * rough polar distance to the point below 2,048 and no ARRIVED outcome for the
 * group. ALL_DROPPED: delivered is at least the total; or, with no drop
 * running, the group is not one of the mission's, has an ARRIVED outcome, or
 * has a total past 255 (a WARN, once for each group). Changes nothing else. */
uint32_t ai_books_drop_bits(uint16_t object_index, uint8_t drop_phase,
			    int32_t point[AI_WIRE_POINT_AXES]);

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
