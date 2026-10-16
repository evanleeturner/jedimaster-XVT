/* A computer craft's place in its flight's formation, for the pilot program,
 * and the formation books, ported from the 1997 AI: the place from
 * paiman_calcformation (paiman.c), the group's formation out of a hangar
 * from pai_set_flight_group_formation (pai.c), and separation 1 from the
 * fly-home and enter-hangar orders (paiorder.c), all deleted with it. The
 * escorted craft and the escort station come from the 1997 escort order's
 * search (paifight.c) and escort maneuver (paiman.c), also deleted. Two
 * faults of the maneuver are not kept: it took slot 255 for no craft (any
 * slot may be escorted now), and read past its station tables when the
 * order's first variable was 28 or more (such an order has no station). */
#include "xvt/flight/ai_formation.h"

#include <stdint.h>

#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai_books.h"
#include "xvt/flight/ai_state.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_geometry.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt_runtime/log/log.h"

enum {
	/* The formations g_form_pos_x, g_form_pos_y and g_form_pos_z hold. */
	AI_FORMATION_TYPES = 34,
	/* The places each formation holds, by craft_ordinal. */
	AI_FORMATION_PLACES = 6,
	/* The escort stations an escort order's first variable names. */
	AI_FORMATION_STATIONS = 28,
	/* An escorted craft whose type has this max_bounds_extent or more is
	 * big: its stations lie 16 times as far out. */
	AI_FORMATION_BIG_EXTENT = 3000,
	AI_FORMATION_BIG_FACTOR = 16,
};

/* Each escort station's offset in world units along the escorted craft's
 * side, up and forward axes, by the escort order's first variable: a three by
 * three by three grid 3,072 apart, entries 0 to 26, and entry 27 on the
 * craft. The 1997 AI's tables. */
static const int16_t g_ai_formation_station_side[AI_FORMATION_STATIONS] = {
	-3072, 0,     3072,  -3072, 0,	   3072,  -3072, 0,    3072,  -3072,
	0,     3072,  -3072, 0,	    3072,  -3072, 0,	 3072, -3072, 0,
	3072,  -3072, 0,     3072,  -3072, 0,	  3072,	 0};
static const int16_t g_ai_formation_station_up[AI_FORMATION_STATIONS] = {
	3072,  3072,  3072,  3072,  3072,  3072,  3072,	 3072, 3072,  0,
	0,     0,     0,     0,	    0,	   0,	  0,	 0,    -3072, -3072,
	-3072, -3072, -3072, -3072, -3072, -3072, -3072, 0};
static const int16_t g_ai_formation_station_forward[AI_FORMATION_STATIONS] = {
	3072, 3072, 3072, 0, 0,	    0,	   -3072, -3072, -3072, 3072,
	3072, 3072, 0,	  0, 0,	    -3072, -3072, -3072, 3072,	3072,
	3072, 0,    0,	  0, -3072, -3072, -3072, 0};

/* Puts in place the world position of craft's place beside leader, by
 * 1997's arithmetic: g_form_pos_y is the forward offset and g_form_pos_z the
 * up one, and each step keeps 1997's 16-bit width. */
static void ai_formation_place(const struct craft_data *craft,
			       const struct object_record *leader,
			       int32_t place[AI_WIRE_POINT_AXES])
{
	const struct model_def *model = &g_model_defs[craft->model_index];
	int formation = craft->ai_flight.formation_type;
	int ordinal = craft->craft_ordinal;
	int16_t separation =
		(int16_t)(leader->mobj->p_craft->ai_flight.separation + 1);
	int16_t offset_side = (int16_t)(g_form_pos_x[formation][ordinal] -
					g_form_pos_x[formation][0]);
	int16_t offset_forward = (int16_t)(g_form_pos_y[formation][ordinal] -
					   g_form_pos_y[formation][0]);
	int16_t offset_up = (int16_t)(g_form_pos_z[formation][ordinal] -
				      g_form_pos_z[formation][0]);
	int16_t side = (int16_t)((int16_t)(separation * offset_side) *
				 model->bound_size_x);
	int16_t forward = (int16_t)((int16_t)(separation * offset_forward) *
				    model->bound_size_y);
	int16_t up = (int16_t)((int16_t)(separation * offset_up) *
			       model->bound_size_z);
	if (separation == 1) {
		side = (int16_t)(side +
				 offset_side * (model->bound_size_x / 2));
		up = (int16_t)(up + offset_up * (model->bound_size_z / 2));
		forward = (int16_t)(forward +
				    offset_forward * (model->bound_size_y / 2));
	}
	int16_t divisor = g_formation_divisor[formation];
	if (divisor != 1) {
		side = (int16_t)(side / divisor);
		forward = (int16_t)(forward / divisor);
		up = (int16_t)(up / divisor);
	}
	flight_geometry_local_to_world_i16(leader, side, up, forward);
	/* 1997 shifted the signed sums left; through unsigned, the same bits. */
	unsigned int shift = model->bound_size_shift;
	place[0] = (int32_t)((uint32_t)g_rotated_x << shift) + leader->world_x;
	place[1] = (int32_t)((uint32_t)g_rotated_y << shift) + leader->world_y;
	place[2] = (int32_t)((uint32_t)g_rotated_z << shift) + leader->world_z;
}

void ai_formation_view_craft(uint16_t object_index,
			     struct ai_wire_craft_view *view)
{
	const struct craft_data *craft =
		g_object_table[object_index].mobj->p_craft;
	for (int axis = 0; axis < AI_WIRE_POINT_AXES; ++axis) {
		view->place[axis] = 0;
	}
	view->books &= (uint16_t)~AI_WIRE_BOOKS_PLACE;
	if (craft->leader_obj_idx == UINT8_MAX) {
		return;
	}
	const struct object_record *leader =
		&g_object_table[craft->leader_obj_idx];
	/* 1997 read past the tables here; a place they do not hold is none. */
	if (leader->object_type == 0 ||
	    craft->ai_flight.formation_type >= AI_FORMATION_TYPES ||
	    craft->craft_ordinal >= AI_FORMATION_PLACES) {
		return;
	}
	ai_formation_place(craft, leader, view->place);
	view->books |= AI_WIRE_BOOKS_PLACE;
}

/* Gives every craft of flight group in the active region the mission's
 * formation and spacing. */
static void ai_formation_set_group(int flight_group)
{
	const struct mission_flight_group *group =
		&g_mission_flight_groups[flight_group];
	for (int slot = g_active_region_object_slot_start;
	     slot < g_active_region_craft_object_slot_end; ++slot) {
		struct object_record *object = &g_object_table[slot];
		if (object->object_type == 0 ||
		    object->flight_group_idx != flight_group) {
			continue;
		}
		struct craft_data *craft = object->mobj->p_craft;
		craft->ai_flight.formation_type = group->fg.formation;
		craft->ai_flight.separation = group->fg.formation_spacing;
	}
	XVT_LOG_DEBUG("ai.formation_set fg=%d formation=%u spacing=%u "
		      "predicted=%d",
		      flight_group, (unsigned)group->fg.formation,
		      (unsigned)group->fg.formation_spacing,
		      g_flight_sim_side_effects_suppressed);
}

void ai_formation_activity_changed(uint16_t object_index, uint8_t was,
				   uint8_t now)
{
	struct object_record *object = &g_object_table[object_index];
	if (was == AI_ACTIVITY_LEAVING_HANGAR &&
	    now != AI_ACTIVITY_LEAVING_HANGAR) {
		ai_formation_set_group(object->flight_group_idx);
	}
	struct ai_flight_state *flight = &object->mobj->p_craft->ai_flight;
	if ((now == AI_ACTIVITY_GOING_HOME ||
	     now == AI_ACTIVITY_GOING_HOME_EVADING ||
	     now == AI_ACTIVITY_ENTERING_HANGAR) &&
	    flight->separation != 1) {
		flight->separation = 1;
		XVT_LOG_DEBUG("ai.formation_closed object=%d activity=%d "
			      "predicted=%d",
			      (int)object_index, (int)now,
			      g_flight_sim_side_effects_suppressed);
	}
}

/* The nearest object of flight group group to the craft in object_index among
 * the slots from first up to end, by rough distance, the first of equals:
 * updates *best and *best_group when one is nearer than *best. */
static void ai_formation_nearest_in(uint16_t object_index, uint16_t group,
				    int first, int end, unsigned int *best,
				    uint16_t *best_group)
{
	for (int slot = first; slot < end; ++slot) {
		const struct object_record *object = &g_object_table[slot];
		if (object->object_type == 0 ||
		    object->flight_group_idx != group) {
			continue;
		}
		flight_geometry_rough_distance_between_refs(object_index,
							    (unsigned int)slot);
		if (*best > (unsigned int)g_last_rough_distance) {
			*best = (unsigned int)g_last_rough_distance;
			*best_group = group;
		}
	}
}

/* The flight group of the nearest object, a craft in the active region or a
 * static, of a flight group that matches the order's target pair; 0xFFFF when
 * none does. The 1997 escort search, which did not ask whether the object may
 * be targeted. */
static uint16_t ai_formation_nearest_group(uint16_t object_index,
					   const struct mission_order *order,
					   int pair)
{
	unsigned int best = UINT32_MAX;
	uint16_t best_group = UINT16_MAX;
	for (uint16_t group = 0; group < g_mission_header.num_flight_groups;
	     ++group) {
		if (!ai_books_pair_matches(order, pair, group)) {
			continue;
		}
		ai_formation_nearest_in(object_index, group,
					g_active_region_object_slot_start,
					g_active_region_craft_object_slot_end,
					&best, &best_group);
		ai_formation_nearest_in(
			object_index, group, g_region_main_object_slot_end,
			g_region_main_object_slot_end +
				g_region_static_object_slot_count,
			&best, &best_group);
	}
	return best_group;
}

/* The first craft of the flight group in the active region's craft slots
 * that follows no leader; 0xFFFF when there is none. */
static uint16_t ai_formation_group_leader(uint16_t group)
{
	for (uint16_t slot = (uint16_t)g_active_region_object_slot_start;
	     slot < g_active_region_craft_object_slot_end; ++slot) {
		const struct object_record *object = &g_object_table[slot];
		if (object->object_type != 0 && object->mobj->family == 0 &&
		    object->flight_group_idx == group &&
		    object->mobj->p_craft->leader_obj_idx == UINT8_MAX) {
			return slot;
		}
	}
	return UINT16_MAX;
}

/* Puts in station the world position of the escort station the order's first
 * variable names beside the escorted craft: the offset along its axes, 16
 * times as far for a big craft, added to its position. */
static void ai_formation_station(const struct object_record *escorted,
				 int variable,
				 int32_t station[AI_WIRE_POINT_AXES])
{
	int16_t side = g_ai_formation_station_side[variable];
	int16_t up = g_ai_formation_station_up[variable];
	int16_t forward = g_ai_formation_station_forward[variable];
	flight_geometry_local_to_world_i16(escorted, side, up, forward);
	int extent =
		g_object_type_table[escorted->object_type].max_bounds_extent;
	int factor = 1;
	if (extent >= AI_FORMATION_BIG_EXTENT) {
		factor = AI_FORMATION_BIG_FACTOR;
	}
	station[0] = escorted->world_x + g_rotated_x * factor;
	station[1] = escorted->world_y + g_rotated_y * factor;
	station[2] = escorted->world_z + g_rotated_z * factor;
}

void ai_formation_view_escort(uint16_t object_index,
			      struct ai_wire_craft_view *view)
{
	const struct object_record *object = &g_object_table[object_index];
	const struct craft_data *craft = object->mobj->p_craft;
	const struct mission_order *order =
		&g_mission_flight_groups[object->flight_group_idx]
			 .fg.orders[craft->ai_controller.current_order_slot];
	view->escorted = UINT16_MAX;
	for (int axis = 0; axis < AI_WIRE_POINT_AXES; ++axis) {
		view->station[axis] = 0;
	}
	view->books &= (uint16_t)~AI_WIRE_BOOKS_STATION;
	if (order->order != MISSION_ORDER_ESCORT ||
	    craft->leader_obj_idx != UINT8_MAX) {
		return;
	}
	uint16_t group = ai_formation_nearest_group(object_index, order, 0);
	if (group == UINT16_MAX) {
		group = ai_formation_nearest_group(object_index, order, 1);
	}
	uint16_t escorted = group == UINT16_MAX
				    ? UINT16_MAX
				    : ai_formation_group_leader(group);
	/* 1997 read past the station tables here; a station they do not hold
	 * is none, and so is a craft escorted by no one. */
	if (escorted == UINT16_MAX ||
	    order->variable1 >= AI_FORMATION_STATIONS) {
		return;
	}
	view->escorted = escorted;
	ai_formation_station(&g_object_table[escorted], order->variable1,
			     view->station);
	view->books |= AI_WIRE_BOOKS_STATION;
}
