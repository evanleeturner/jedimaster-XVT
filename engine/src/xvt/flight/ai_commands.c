/* A computer craft's commands from a player, for the pilot program. The 1997
 * AI took the candidate target up in its target orders (paifight_
 * scanfortargetorder, paiorder_targetfromplayerorder), dropped it on the
 * evade command (paiorder_evasiveorder) and skipped the avoided target in
 * every search, all deleted with it; the program does each now. */
#include "xvt/flight/ai_commands.h"

#include <stdint.h>

#include "xvt/flight/ai_state.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt_runtime/log/log.h"

void ai_commands_view_craft(uint16_t object_index,
			    struct ai_wire_craft_view *view)
{
	struct craft_data *craft = g_object_table[object_index].mobj->p_craft;
	struct ai_controller *controller = &craft->ai_controller;
	uint16_t candidate = controller->candidate_target_idx;
	if (candidate != UINT16_MAX && candidate != AI_TARGET_ABORT &&
	    g_object_table[candidate].object_type == 0) {
		controller->candidate_target_idx = UINT16_MAX;
		XVT_LOG_DEBUG(
			"ai.command_target_gone object=%d target=%d predicted=%d",
			(int)object_index, (int)candidate,
			g_flight_sim_side_effects_suppressed);
		candidate = UINT16_MAX;
	}
	view->commands = 0;
	if (controller->wait_command != 0) {
		view->commands |= AI_WIRE_COMMAND_WAIT;
	}
	if (candidate == AI_TARGET_ABORT) {
		view->commands |= AI_WIRE_COMMAND_EVADE;
		candidate = UINT16_MAX;
	}
	view->command_target = candidate;
	view->avoid_target = craft->player_command_avoid_target_obj_idx;
}

void ai_commands_evaded(uint16_t object_index)
{
	struct ai_controller *controller =
		&g_object_table[object_index].mobj->p_craft->ai_controller;
	controller->candidate_target_idx = UINT16_MAX;
	XVT_LOG_DEBUG("ai.craft_evaded object=%d predicted=%d",
		      (int)object_index, g_flight_sim_side_effects_suppressed);
}
