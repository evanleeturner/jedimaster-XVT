#ifndef XVT_FLIGHT_AI_TRACER_H
#define XVT_FLIGHT_AI_TRACER_H

#include <stdint.h>

#include "xvt/flight/craft.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt_runtime/runtime/sith.h"

/* Which shots are tracers, shared by the pilot program's question
 * (ai_brain_view_tracers) and the world log (xvt_flight_record_tracers) so
 * both pick the same bolts.
 *
 * A tracer is a cannon bolt (a shot type with warhead_class 0 among the
 * cannon types 137 to 142 and 145 to 147) in an even shot slot. A slot's
 * parity is world state, so every machine marks the same bolts and nothing
 * is counted or stored. */

/* Whether the shot type is a cannon type. */
static inline int ai_tracer_type_is_cannon(int object_type)
{
	switch (object_type) {
	case 137:
	case 138:
	case 139:
	case 140:
	case 141:
	case 142:
	case 145:
	case 146:
	case 147:
		return 1;
	default:
		return 0;
	}
}

/* Whether the object in the shot slot is a tracer. */
static inline int ai_tracer_slot_is_tracer(int slot)
{
	if (slot < g_projectile_object_slot_start ||
	    slot >= g_projectile_object_slot_end || (slot & 1) != 0) {
		return 0;
	}
	const struct object_record *object = &g_object_table[slot];
	int type_index =
		(int)object->object_type - PROJECTILE_OBJECT_TYPE_FIRST;
	return object->mobj != NULL &&
	       ai_tracer_type_is_cannon(object->object_type) &&
	       type_index >= 0 && type_index < PROJECTILE_OBJECT_TYPE_COUNT &&
	       g_projectile_type_data.warhead_class[type_index] == 0;
}

/* Whether the slot holds a craft of the active region's craft slots, with
 * a craft record, at the given AI rank. */
static inline int ai_tracer_craft_has_rank(int slot, int rank)
{
	if (slot < g_active_region_object_slot_start ||
	    slot >= g_active_region_craft_object_slot_end) {
		return 0;
	}
	const struct object_record *object = &g_object_table[slot];
	return object->object_type != 0 && object->mobj != NULL &&
	       object->mobj->family == 0 && object->mobj->p_craft != NULL &&
	       object->mobj->p_craft->ai_rank == rank;
}

/* Whether a craft at XVT_SITH_LORD_LEVEL flies in the active region: one
 * that is not breaking up or exploding. */
static inline int ai_tracer_lord_alive(void)
{
	for (int slot = g_active_region_object_slot_start;
	     slot < g_active_region_craft_object_slot_end; ++slot) {
		if (!ai_tracer_craft_has_rank(slot, XVT_SITH_LORD_LEVEL)) {
			continue;
		}
		craft_object_kind kind =
			g_object_table[slot].mobj->p_craft->object_kind;
		if (kind != CRAFT_OBJECT_KIND_BREAKING_UP &&
		    kind != CRAFT_OBJECT_KIND_EXPLODING) {
			return 1;
		}
	}
	return 0;
}

#endif
