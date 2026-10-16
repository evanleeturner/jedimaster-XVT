#ifndef XVT_FLIGHT_AI_FORMATION_H
#define XVT_FLIGHT_AI_FORMATION_H

/* A computer craft's place in its flight's formation, for the pilot program:
 * where a follower belongs beside its leader. The 1997 AI pushed a follower
 * into its place at each think (paiman_calcformation in paiman.c, deleted
 * with the 1997 AI); the program now flies it there, and the engine only
 * says where the place is. The engine also keeps the formation and spacing
 * the place is drawn from, as the 1997 AI's orders changed them. */

#include <stdint.h>

#include "xvt_runtime/ai/ai_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Writes into view the formation place of the craft in object_index. A
 * craft that follows a leader gets AI_WIRE_BOOKS_PLACE in its books and the
 * place's world position: the leader's position plus the offset
 * g_form_pos_x, g_form_pos_y and g_form_pos_z give the craft's craft_ordinal
 * in its formation_type, less those of place 0, times its leader's
 * separation plus 1 and the craft model's bound sizes (half a bound more at
 * separation 0), divided by g_formation_divisor, turned to the leader's axes
 * and shifted left by the model's bound_size_shift, in 1997's arithmetic,
 * its 16-bit steps included. A leader or a craft alone gets 0 and the bit
 * clear. Changes no game state but the g_rotated_ globals and, when the
 * leader's axes are stale, its cached axes
 * (flight_geometry_local_to_world_i16). */
void ai_formation_view_craft(uint16_t object_index,
			     struct ai_wire_craft_view *view);

/* Writes into view the craft escorted and its station, for a craft whose
 * current order is Escort and that leads or flies alone (a follower flies its
 * formation place). As the 1997 escort order did on each think, the escorted
 * craft is the first craft with no leader, in the active region's craft
 * slots, of the flight group of the nearest object, craft or static, that
 * matches the order's first target pair, else its second. The station is one
 * of 28 offsets of plus or minus 3,072 along the escorted craft's axes, the
 * one the order's first variable picks, 16 times as far for a craft whose
 * type has a max_bounds_extent of 3,000 or more, added to its position; it
 * is given with AI_WIRE_BOOKS_STATION, with the escorted craft. A variable of
 * 28 or more, or no craft to escort, gives neither: 0xFFFF, 0, 0, 0 and the
 * bit clear. Changes no game state but the g_rotated_ globals, the rough
 * distance globals and, when the escorted craft's axes are stale, its cached
 * axes. */
void ai_formation_view_escort(uint16_t object_index,
			      struct ai_wire_craft_view *view);

/* Keeps the formation books as the pilot program's activity action moves
 * the craft in object_index from activity was to activity now. A craft
 * whose leaving-hangar mark ends gives every craft of its flight group in
 * the active region the mission's formation and spacing, which a round out
 * of a hangar starts without (the 1997 AI did this when its out-of-hangar
 * maneuver ended, 2,360 ticks out; the program ends the mark at the craft's
 * first think). A craft that goes home, evading or not, or enters a hangar
 * takes separation 1, so its followers close up behind it, as the 1997
 * fly-home and enter-hangar orders set it. */
void ai_formation_activity_changed(uint16_t object_index, uint8_t was,
				   uint8_t now);

#ifdef __cplusplus
}
#endif

#endif
