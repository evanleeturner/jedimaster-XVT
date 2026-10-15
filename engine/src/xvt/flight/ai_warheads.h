#ifndef XVT_FLIGHT_AI_WARHEADS_H
#define XVT_FLIGHT_AI_WARHEADS_H

/* A computer craft's warheads: the lock it counts by a player's rule, and
 * what the question tells the pilot program about that lock, the craft's
 * skill level and the homing warheads in flight at each contact. The 1997 AI
 * counted its own lock inside its fire order; the program now decides when
 * to fire, and the engine counts the lock as it does for a player's craft. */

#include <stdint.h>

#include "xvt_runtime/ai/ai_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Counts one step of the lock of the craft in object_index, which no player
 * flies, by the rule laser_weaponsfire keeps for a player's craft: against
 * its AI target, while the target lies inside the lock range (244,332 at a
 * freighter, starship or platform, else 101,805) and inside the aim cone
 * (targeting_test_aim_cone_from), the count gains the ticks that passed,
 * less half of them while chaff works on the target; outside, it falls by
 * half the ticks plus the ticks, to 0 at least. It is declared at 708 ticks,
 * 354 on a Missile Boat. A craft with no target, a target in no craft slot,
 * or no warheads left in any launcher holds 0, and a new target starts the
 * count over, as a player's new target does. The count stops at 32,767
 * rather than wrap. Logs ai.lock_gained when the lock is declared and
 * ai.lock_lost when a declared lock ends. Object slots past the game's 32
 * craft slots are left alone. */
void ai_warheads_count_lock(uint16_t object_index);

/* Writes into view the lock of the craft in object_index: its count in
 * ticks, the ticks it must reach, and its state as the last step counted it
 * (an AI_WIRE_LOCK_ value, AI_WIRE_LOCK_NONE once the craft or its target
 * changed since); and its skill level, its AI rank (ai_rank: a Sith Lord
 * is 7 in a group of 6). */
void ai_warheads_view_craft(uint16_t object_index,
			    struct ai_wire_craft_view *view);

/* Writes into contact the homing warheads in flight at the craft in
 * object_index: their count, at most 255, and their damage summed. A
 * countermeasure and a shot that does not home are not counted. */
void ai_warheads_view_contact(uint16_t object_index,
			      struct ai_wire_contact *contact);

#ifdef __cplusplus
}
#endif

#endif /* XVT_FLIGHT_AI_WARHEADS_H */
