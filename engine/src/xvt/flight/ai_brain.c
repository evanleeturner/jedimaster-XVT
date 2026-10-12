/* The one way into the computer pilots from the rest of the engine. Flight,
 * the player's commands, the HUD, lasers, missions and flight loading call
 * these, never the pilots' own files, so a second pilot program can later be
 * chosen here, per flight, without touching the callers. Today every entry
 * hands its call to the 1997 pilots in ai/ unchanged, with the same arguments.
 *
 * The callers still read and set some of the pilots' state themselves: the
 * plan a craft runs, looked up and compared by name, and fields of
 * g_pai_context. Those reads are not routed here yet. */
#include "xvt/flight/ai_brain.h"

#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paifight.h"
#include "xvt/flight/flight.h"

/* Readies the pilots for a new flight: loads the AI plans from the plan file
 * named by g_pai_plan_resource_base_name ("paiplan"), then caches the plan id
 * of each built-in plan name. Called once per flight, before any craft is set
 * up. A plan file that fails to load leaves the table pai_loadplans left. */
void ai_brain_start_flight(void)
{
	pai_loadplans((char *)g_pai_plan_resource_base_name);
	pai_cache_builtin_plan_ids();
}

/* Lets each craft whose think timer has run out decide again, once per
 * reference step: pai_update_all_craft_ai. */
void ai_brain_think_due_craft(void) { pai_update_all_craft_ai(); }

/* Starts again the plan the craft's AI controller holds now. The caller has
 * just set running_plan_id, or made the craft, and set g_cur_craft to its
 * craft. Sets up g_pai_context for the craft, then applies the plan's target
 * and maneuver and spreads its next think over the think interval. */
void ai_brain_restart_craft_plan(unsigned int object_index)
{
	pai_setupcraftcontext((uint16_t)object_index);
	pai_apply_running_plan_target_and_maneuver(object_index);
}

/* Sets up g_pai_context for one craft without changing its plan, so the
 * caller can then ask about its orders. */
void ai_brain_prepare_craft(uint16_t object_index)
{
	pai_setupcraftcontext(object_index);
}

/* Returns 1 when the object matches the targets of the order slot in
 * g_pai_context, else 0. The context must be set up first. */
int16_t ai_brain_order_targets_match_object(uint16_t object_index)
{
	return pai_current_order_targets_match_object(object_index);
}

/* Returns 1 when one of the object's flight group orders in slots 0 to 2
 * gives a leader the plan named by leader_plan_name_index and targets
 * target_object_index, else 0. The HUD asks this for this machine's own
 * player while it describes a target, so it leaves g_pai_context as it found
 * it: a context left behind would differ between the machines of a network
 * game. */
int ai_brain_order_gives_plan_on_target(int object_index,
					int leader_plan_name_index,
					int target_object_index)
{
	return pai_setup_context_and_find_order_plan_on_target(
		object_index, leader_plan_name_index, target_object_index);
}

/* A mine's target search: the nearest targetable object that matches the
 * target conditions, measured from g_paifight_search_origin_x, Y and Z, or -1.
 * It is shared code in the pilots' files that only the mines call; it stays
 * in C whichever pilots fly. */
int16_t ai_brain_find_mine_target(int16_t target1_type, uint16_t target1,
				  int16_t target1_or_target2,
				  int16_t target2_type, uint16_t target2,
				  int16_t require_clear_sweep)
{
	return paifight_find_nearest_matching_target_from_origin(
		target1_type, target1, target1_or_target2, target2_type,
		target2, require_clear_sweep);
}

/* The AI skill the craft flies with: the skill in the character data of the
 * object effective_ai_object_link points to, while that object still has the
 * saved signature and has character data, else the craft's own ai_skill.
 * Clears effective_ai_object_link when the signature no longer matches. A
 * turret fires with it. Shared code that stays in C whichever pilots fly. */
uint16_t ai_brain_effective_skill(struct craft_data *craft)
{
	return pai_get_effective_skill_value(craft);
}

/* The first craft of the flight group, in the active region, that is not
 * breaking up or exploding and has no leader; 0xFFFF when there is none.
 * Shared code that stays in C whichever pilots fly. */
uint16_t ai_brain_find_mothership(int16_t mothership_flight_group_idx)
{
	return pai_find_mothership_object(mothership_flight_group_idx);
}
