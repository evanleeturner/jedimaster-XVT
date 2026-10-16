/* The boarded side of a boarding, ported from the 1997 AI's Disabled and
 * Await Boarding plans (paiorder_makedisabledorder, paiorder_awaitboardorder
 * and the restore inside paiorder_abortmissionorder and
 * paiorder_stopgohomeorder, in paiorder.c, deleted with the 1997 AI). A
 * fault of the 1997 plans not kept: the count had no end, so a craft that could
 * not move its order on counted past variable1 until the byte wrapped. A
 * captured craft is left alone, as 1997's capture gave it a fly-home plan. */
#include "xvt/flight/ai_boarded.h"

#include <stdint.h>

#include "xvt/flight/ai_state.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt_runtime/log/log.h"

enum {
	/* The departure mark of a craft whose delay has run (ai_books.c). */
	BOARDED_DEPARTURE_DUE = 2,
};

static struct craft_data *boarded_craft(uint16_t object_index)
{
	return g_object_table[object_index].mobj->p_craft;
}

static const struct mission_order *boarded_order(uint16_t object_index)
{
	return &g_mission_flight_groups[g_object_table[object_index]
						.flight_group_idx]
			.fg.orders[boarded_craft(object_index)
					   ->ai_controller.current_order_slot];
}

/* Whether the craft on Await Boarding has been released: its abort mark is
 * set or its departure is due. Disabled is never released. */
static int boarded_released(uint16_t object_index)
{
	const struct ai_flight_state *flight =
		&boarded_craft(object_index)->ai_flight;
	return boarded_order(object_index)->order ==
		       MISSION_ORDER_AWAIT_BOARDING &&
	       (flight->mission_aborted_flag != 0 ||
		flight->depart_timer_flag == BOARDED_DEPARTURE_DUE);
}

/* Counts the boarding done to the craft, when its state says one was, and
 * restores the craft once variable1 boardings are done: below that the state
 * is cleared, so one boarding counts once. */
static void boarded_count(uint16_t object_index)
{
	struct craft_data *craft = boarded_craft(object_index);
	struct ai_controller *controller = &craft->ai_controller;
	const struct mission_order *order = boarded_order(object_index);
	const uint8_t slot = controller->current_order_slot;
	const uint8_t state = craft->boarding_state;
	if (state != 2 && state != 3) {
		return;
	}
	++controller->order_progress.goal_progress[slot];
	XVT_LOG_DEBUG(
		"ai.boarding_counted object=%d order=%d slot=%d progress=%d boarded=%d needed=%d state=%d predicted=%d",
		(int)object_index, (int)order->order, (int)slot,
		(int)controller->order_progress.goal_progress[slot],
		(int)craft->ai_flight.times_boarded, (int)order->variable1,
		(int)state, g_flight_sim_side_effects_suppressed);
	if (craft->ai_flight.times_boarded < order->variable1) {
		craft->boarding_state = 0;
		return;
	}
	craft->working_subsystems = craft->system_flags;
	craft->subsystem_damage = 0;
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO(
			"ai.boarded_restored object=%d fg=%d order=%d boarded=%d needed=%d tick=%d",
			(int)object_index,
			(int)g_object_table[object_index].flight_group_idx,
			(int)order->order, (int)craft->ai_flight.times_boarded,
			(int)order->variable1, g_game_time);
	}
	if (order->order == MISSION_ORDER_DISABLED &&
	    craft->ai_flight.times_boarded == order->variable1) {
		msg_emit_craft_message(object_index, craft,
				       IFMSG_138_HAS_BEEN_REPAIRED);
	}
}

void ai_boarded_step(uint16_t object_index)
{
	struct craft_data *craft = boarded_craft(object_index);
	const struct mission_order *order = boarded_order(object_index);
	if (g_object_table[object_index].player_owner_idx != -1 ||
	    (order->order != MISSION_ORDER_DISABLED &&
	     order->order != MISSION_ORDER_AWAIT_BOARDING) ||
	    craft->captured_by_flight_group != 0 ||
	    boarded_released(object_index) ||
	    craft->ai_controller.order_progress.goal_progress
			    [craft->ai_controller.current_order_slot] >=
		    order->variable1) {
		return;
	}
	if (craft->working_subsystems != 0) {
		XVT_LOG_DEBUG(
			"ai.self_disabled object=%d working=%04x predicted=%d",
			(int)object_index, (unsigned)craft->working_subsystems,
			g_flight_sim_side_effects_suppressed);
	}
	craft->working_subsystems = 0;
	boarded_count(object_index);
}

void ai_boarded_release(uint16_t object_index)
{
	struct craft_data *craft = boarded_craft(object_index);
	if (boarded_order(object_index)->order !=
		    MISSION_ORDER_AWAIT_BOARDING ||
	    craft->subsystem_damage != 0) {
		return;
	}
	craft->working_subsystems = craft->system_flags;
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO("ai.boarded_released object=%d fg=%d tick=%d",
			     (int)object_index,
			     (int)g_object_table[object_index].flight_group_idx,
			     g_game_time);
	}
}
