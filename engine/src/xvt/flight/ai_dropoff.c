/* The drop of a computer craft on a Drop Off order, ported from the 1997
 * AI's drop-off maneuver (paiman_dropoffmaneuver, deleted with the 1997 AI).
 * The pilot program flies the approach, the 1997 order's rendezvous; from
 * the drop off action the engine runs the drop: it pushes the carrier to the
 * place of the delivered group that the place counter names (turning to face
 * it), brings the group in there with mission_start_flight_group_arrival,
 * pushes the carrier up and waits 1,180 ticks, then takes the next place. A
 * mine group goes through the same call and lays one mine for each place.
 * Faults of the 1997 maneuver not kept: a group with no craft of its own that
 * leads passed 255 as the formation basis, which the place's position took
 * for a basis craft 255 (it passes 0xFFFF, no basis, now); and a drop whose
 * order changed went on delivering (the drop ends with the order). */
#include "xvt/flight/ai_dropoff.h"

#include <stdint.h>

#include "xvt/assets/model_bounds.h"
#include "xvt/flight/ai_boarding.h"
#include "xvt/flight/ai_books.h"
#include "xvt/flight/ai_state.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/math/trig2.h"
#include "xvt_runtime/log/log.h"

enum {
	/* The carrier turns to face the place when it is farther than this in
	 * X plus Y, and has arrived when X plus Y plus Z are below the second
	 * (sums, not polar distances). */
	DROPOFF_TURN_DISTANCE = 256,
	DROPOFF_ARRIVAL_DISTANCE = 32,
	/* A turn that goes on until the heading is met, at its full step. */
	DROPOFF_TURN_STATE = 2,
	/* The wait after a delivery, and the push up the carrier gets from the
	 * place it delivered at. */
	DROPOFF_WAIT_TICKS = 1180,
	DROPOFF_PUSH_UP = 1500,
};

static struct craft_data *dropoff_craft(uint16_t object_index)
{
	return g_object_table[object_index].mobj->p_craft;
}

uint8_t ai_dropoff_phase(uint16_t object_index)
{
	const struct ai_controller *controller =
		&dropoff_craft(object_index)->ai_controller;
	if (controller->maneuver_mode != AI_MANEUVER_MODE_DROPOFF) {
		return AI_DROPOFF_PHASE_NONE;
	}
	return controller->maneuver_phase <= AI_DROPOFF_PHASE_WAIT
		       ? controller->maneuver_phase
		       : AI_DROPOFF_PHASE_NONE;
}

/* Ends the drop, keeping the places delivered: phase 0, mode NULL, push and
 * timer cleared. */
static void dropoff_end(struct craft_data *craft)
{
	craft->push_accum_x = 0;
	craft->push_accum_y = 0;
	craft->push_accum_z = 0;
	craft->ai_controller.maneuver_mode = AI_MANEUVER_MODE_NULL;
	craft->ai_controller.maneuver_phase = AI_DROPOFF_PHASE_NONE;
	craft->ai_controller.maneuver_timer = 0;
}

void ai_dropoff_start(uint16_t object_index)
{
	struct craft_data *craft = dropoff_craft(object_index);
	struct ai_controller *controller = &craft->ai_controller;
	int32_t point[AI_WIRE_POINT_AXES];
	const uint32_t bits = ai_books_drop_bits(
		object_index, ai_dropoff_phase(object_index), point);
	const int started =
		ai_dropoff_phase(object_index) == AI_DROPOFF_PHASE_NONE &&
		ai_boarding_phase(object_index) == AI_BOARDING_PHASE_NONE &&
		(bits & AI_WIRE_BOOKS_AT_DROP_POINT) != 0;
	if (started) {
		craft->ai_flight.roll_state = 0;
		craft->ai_flight.pitch_state = 0;
		craft->ai_flight.turn_state = 0;
		craft->throttle_speed = 0;
		controller->waypoint_index = 0;
		controller->unused0b = controller->current_order_slot;
		dropoff_end(craft);
		controller->maneuver_mode = AI_MANEUVER_MODE_DROPOFF;
		controller->maneuver_phase = AI_DROPOFF_PHASE_PUSH;
	}
	XVT_LOG_DEBUG(
		"ai.craft_drop_off object=%d started=%d bits=%u predicted=%d",
		(int)object_index, started, (unsigned)bits,
		g_flight_sim_side_effects_suppressed);
}

void ai_dropoff_stop(uint16_t object_index)
{
	const uint8_t phase = ai_dropoff_phase(object_index);
	if (phase != AI_DROPOFF_PHASE_NONE) {
		dropoff_end(dropoff_craft(object_index));
	}
	XVT_LOG_DEBUG(
		"ai.dropoff_stopped object=%d phase=%d place=%d predicted=%d",
		(int)object_index, (int)phase,
		(int)dropoff_craft(object_index)->ai_controller.waypoint_index,
		g_flight_sim_side_effects_suppressed);
}

void ai_dropoff_sync_mode(uint16_t object_index)
{
	if (g_object_table[object_index].object_type == 0) {
		return;
	}
	struct ai_controller *controller =
		&dropoff_craft(object_index)->ai_controller;
	if (controller->maneuver_mode == AI_MANEUVER_MODE_DROPOFF &&
	    ai_dropoff_phase(object_index) == AI_DROPOFF_PHASE_NONE) {
		controller->maneuver_mode = AI_MANEUVER_MODE_NULL;
	}
}

/* The last craft of group in the active region that has no leader, the
 * basis the group's places are laid out around, or UINT16_MAX when it has
 * none (the 1997 maneuver passed 255 there, bug #48). */
static uint16_t dropoff_basis(uint16_t group)
{
	uint16_t basis = UINT16_MAX;
	for (uint16_t slot = (uint16_t)g_active_region_object_slot_start;
	     slot < g_active_region_craft_object_slot_end; ++slot) {
		const struct object_record *other = &g_object_table[slot];
		if (other->object_type != 0 && other->mobj->family == 0 &&
		    other->flight_group_idx == group &&
		    other->mobj->p_craft->leader_obj_idx == UINT8_MAX) {
			basis = slot;
		}
	}
	return basis;
}

static int dropoff_abs(int value) { return value < 0 ? -value : value; }

/* The push phase: the carrier is pushed to the place, raised by its own
 * model's lowest Z, and turned to face it when more than 256 away in X plus
 * Y. Within 32 (X plus Y plus Z) the group's arrival for the place starts,
 * and the wait begins with a push up. */
static void dropoff_push_phase(uint16_t object_index, uint16_t group)
{
	const struct object_record *object = &g_object_table[object_index];
	struct craft_data *craft = dropoff_craft(object_index);
	struct ai_controller *controller = &craft->ai_controller;
	const uint16_t place = controller->waypoint_index;
	const uint16_t basis = dropoff_basis(group);
	mission_resolve_formation_slot_world_loc(group, place, basis);
	const int min_z = -model_bounds_get_min_z(object->object_type);
	craft->push_accum_x = g_world_loc_x - object->world_x;
	craft->push_accum_y = g_world_loc_y - object->world_y;
	craft->push_accum_z = g_world_loc_z - object->world_z + min_z;
	const int distance_x = craft->push_accum_x;
	const int distance_y = craft->push_accum_y;
	const int distance_z = craft->push_accum_z;
	trig2_ctop(distance_x, distance_y, distance_z);
	const int flat = dropoff_abs(distance_x) + dropoff_abs(distance_y);
	if (flat > DROPOFF_TURN_DISTANCE &&
	    object->yaw != (uint16_t)trig2_xyangle) {
		craft->ai_flight.turn_state = DROPOFF_TURN_STATE;
		craft->ai_flight.turn_step = INT16_MIN;
		controller->target_xy_angle = (uint16_t)trig2_xyangle;
	}
	if (flat + dropoff_abs(distance_z) >= DROPOFF_ARRIVAL_DISTANCE) {
		return;
	}
	struct craft_data *const caller = g_cur_craft;
	g_current_flight_group_idx = group;
	g_spawn_leader_obj_idx = (uint8_t)basis;
	mission_start_flight_group_arrival(place);
	g_cur_craft = caller;
	controller->maneuver_phase = AI_DROPOFF_PHASE_WAIT;
	controller->maneuver_timer = DROPOFF_WAIT_TICKS;
	craft->push_accum_z = DROPOFF_PUSH_UP;
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO(
			"ai.dropoff_delivered object=%d fg=%d place=%d basis=%d tick=%d",
			(int)object_index, (int)group, (int)place,
			basis == UINT16_MAX ? -1 : (int)basis, g_game_time);
	}
}

/* The wait phase: when the timer has run out the next place is taken, or the
 * drop ends when every place of the group is delivered. */
static void dropoff_wait_phase(uint16_t object_index, uint16_t group)
{
	struct craft_data *craft = dropoff_craft(object_index);
	struct ai_controller *controller = &craft->ai_controller;
	if (controller->maneuver_timer != 0) {
		return;
	}
	++controller->waypoint_index;
	const unsigned int total =
		g_mission_fg_stats[group]
			.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL];
	XVT_LOG_DEBUG(
		"ai.dropoff_next object=%d place=%d total=%u predicted=%d",
		(int)object_index, (int)controller->waypoint_index, total,
		g_flight_sim_side_effects_suppressed);
	if (controller->waypoint_index >= total) {
		dropoff_end(craft);
	} else {
		controller->maneuver_phase = AI_DROPOFF_PHASE_PUSH;
	}
}

void ai_dropoff_step(uint16_t object_index)
{
	const uint8_t phase = ai_dropoff_phase(object_index);
	if (phase == AI_DROPOFF_PHASE_NONE) {
		return;
	}
	struct craft_data *craft = dropoff_craft(object_index);
	struct ai_controller *controller = &craft->ai_controller;
	const uint8_t slot = controller->current_order_slot;
	const struct mission_order *order =
		&g_mission_flight_groups[g_object_table[object_index]
						 .flight_group_idx]
			 .fg.orders[slot];
	const int moved = slot != controller->unused0b ||
			  mission_current_order_code(object_index) !=
				  MISSION_ORDER_DROP_OFF;
	const int group_valid =
		order->variable2 >= 1 &&
		order->variable2 <= g_mission_header.num_flight_groups;
	if (moved || !group_valid) {
		XVT_LOG_DEBUG(
			"ai.dropoff_ended object=%d slot=%d started_slot=%d order=%d group=%d predicted=%d",
			(int)object_index, (int)slot, (int)controller->unused0b,
			mission_current_order_code(object_index),
			(int)order->variable2,
			g_flight_sim_side_effects_suppressed);
		dropoff_end(craft);
		return;
	}
	const uint16_t group = (uint16_t)(order->variable2 - 1);
	if (phase == AI_DROPOFF_PHASE_PUSH) {
		dropoff_push_phase(object_index, group);
	} else {
		dropoff_wait_phase(object_index, group);
	}
	controller->order_progress.goal_progress[slot] =
		controller->waypoint_index;
}
