#ifndef XVT_FLIGHT_AI_MINES_H
#define XVT_FLIGHT_AI_MINES_H

/* A mine's target search: the nearest targetable object that matches the
 * targets of its flight group's first order. The 1997 AI searched for the
 * mine; the engine keeps the rule, since the mine decides nothing. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the object index of the nearest targetable object that matches
 * the two targets, measured from the mine in mine_obj_idx, or UINT16_MAX
 * when none matches within AI_TARGET_RANGE_MAX. The targets match as in
 * mission_object_matches_trigger_variable, joined by OR when joined_by_or
 * is 1, else by AND. A craft counts, and a static object with behavior flag
 * 2; a Mine Type B takes a craft only while it has working subsystems. A
 * tie keeps the lower slot, craft before static objects. Sets
 * g_last_rough_distance. */
uint16_t ai_mines_find_target(uint16_t mine_obj_idx, int16_t type_a,
			      uint16_t value_a, int16_t joined_by_or,
			      int16_t type_b, uint16_t value_b);

#ifdef __cplusplus
}
#endif

#endif
