#ifndef XVT_FLIGHT_AI_PAI_TARGETABILITY_H
#define XVT_FLIGHT_AI_PAI_TARGETABILITY_H

#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paifight.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/object/object.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Balance of Power expands this XvT helper at its call sites. Do not copy it into callers. */
/* Returns 1 when the AI may target the object, else 0. Not 0xFFFF, an empty
 * slot, a craft going into hyperspace (that maneuver with maneuver_phase not
 * 0), or a craft of kind CRAFT_OBJECT_KIND_UNKNOWN_1, breaking up or
 * exploding. Nor a craft with its decoy beam on that lies farther from the
 * craft in g_pai_context than AI_DECOY_RANGE_SMALL, when a starfighter,
 * transport or utility vehicle looks, or AI_DECOY_RANGE_LARGE for others; that
 * test sets g_last_rough_distance. Past those, a player's craft can be targeted,
 * and an AI craft while its hull_damage is at most hull_max. A mobile object
 * with no craft can be. */
static __inline int pai_is_object_targetable(unsigned int obj_idx)
{
	struct object_record *object;
	struct craft_data *craft;
	struct ai_controller *controller;
	uint16_t object_kind;

	if (obj_idx == UINT16_MAX) {
		return 0;
	}
	object = &g_object_table[obj_idx];
	if (object->mobj != NULL) {
		if (object->object_type == 0) {
			return 0;
		}
		craft = object->mobj->p_craft;
		if (craft != NULL) {
			controller = &craft->ai_controller;
			if (controller->maneuver_mode ==
				    AI_MANEUVER_MODE_INTO_HYPERSPACE &&
			    controller->maneuver_phase != 0) {
				return 0;
			}
			object_kind = craft->object_kind;
			if (object_kind == CRAFT_OBJECT_KIND_UNKNOWN_1 ||
			    object_kind == CRAFT_OBJECT_KIND_BREAKING_UP ||
			    object_kind == CRAFT_OBJECT_KIND_EXPLODING) {
				return 0;
			}
			if (craft->beam_type_id == BEAM_TYPE_DECOY &&
			    craft->beam_active != 0) {
				uint8_t source_genus;

				pai_object_ref_update_rough_distance(
					obj_idx, g_pai_context.object_index);
				source_genus =
					g_object_table[g_pai_context
							       .object_index]
						.genus_id;
				if (source_genus == CRAFT_GENUS_STARFIGHTER ||
				    source_genus == CRAFT_GENUS_TRANSPORT ||
				    source_genus ==
					    CRAFT_GENUS_UTILITY_VEHICLE) {
					if (g_last_rough_distance >
					    AI_DECOY_RANGE_SMALL) {
						return 0;
					}
				} else if (g_last_rough_distance >
					   AI_DECOY_RANGE_LARGE) {
					return 0;
				}
			}
			if (g_object_table[obj_idx].player_owner_idx != -1) {
				return 1;
			}
			return craft->hull_damage <=
			       g_object_table[obj_idx].mobj->p_craft->hull_max;
		} else {
			return 1;
		}
	} else {
		return object->object_type != 0;
	}
}

#ifdef __cplusplus
}
#endif

#endif
