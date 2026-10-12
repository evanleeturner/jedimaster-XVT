/* The one way into the computer pilots from the rest of the engine. The 1997
 * AI that answered here is gone; the pilot program will answer here instead.
 * Until it does, no craft decides anything: each keeps the speed and heading
 * it spawned with, its activity stays AI_ACTIVITY_NONE, and the questions
 * below about orders and targets answer no. */
#include "xvt/flight/ai_brain.h"

#include <limits.h>
#include <string.h>

#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_geometry.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/log/log.h"

/* AI skill as a fraction of 65,536 for each flight group AI level:
 * mission_init_flight_group_object_slot copies the entry for g_spawn_group_ai into a
 * new craft's ai_skill. Levels 4 and 5 hold 0xFFFF, which math2_fraction treats
 * as a whole; entries 6 and 7 are 0. pai_is_object_targetable_near_craft and
 * pai_is_object_within_skill_range_of_craft index it by the skill tier, 0 to 2,
 * instead. Nothing writes it. */
// GLOBAL: XVT 0x524100
uint16_t g_ai_skill_value_q16_by_level[8] = {0x0000, 0x4000, 0x8000, 0xC000,
					     0xFFFF, 0xFFFF, 0x0000, 0x0000};
/* Ticks between two AI thinks of a craft for each flight group AI level, from
 * 708 down to 29; mission_init_flight_group_object_slot copies the entry into the
 * craft's think_interval. Entries 6 and 7 are 0. */
// GLOBAL: XVT 0x524110
const uint16_t g_ai_think_interval_by_skill[8] = {
	708, 472, SIMULATION_TICKS_PER_SECOND, 118, 59, 29, 0, 0};

/* The HUD status line of each activity, an index in g_str_in_flight_messages:
 * chosen by meaning among the in-flight status lines; the 1997 table
 * that keyed them by plan id needed the retail plan file's order. */
const uint8_t g_ai_activity_status_message_id[AI_ACTIVITY_COUNT] = {
	[AI_ACTIVITY_NONE] = IFMSG_159_HOLDING_STEADY,
	[AI_ACTIVITY_HOLDING_STATION] = IFMSG_177_HOLDING_STATION,
	[AI_ACTIVITY_IN_FORMATION] = IFMSG_183_FLYING_FLIGHT_PLAN,
	[AI_ACTIVITY_WAITING_FOR_GO] = IFMSG_187_AWAITING_FURTHER_ORDERS,
	[AI_ACTIVITY_WAITING] = IFMSG_188_WAITING,
	[AI_ACTIVITY_ARRIVING_FROM_HYPERSPACE] =
		IFMSG_182_HYPERING_INTO_COMBAT_AREA,
	[AI_ACTIVITY_LEAVING_BY_HYPERSPACE] =
		IFMSG_181_HYPERING_OUT_OF_COMBAT_AREA,
	[AI_ACTIVITY_LEAVING_HANGAR] = IFMSG_180_EXITING_HANGAR,
	[AI_ACTIVITY_ENTERING_HANGAR] = IFMSG_179_ENTERING_HANGAR,
	[AI_ACTIVITY_GOING_HOME] = IFMSG_178_FLYING_HOME,
	[AI_ACTIVITY_GOING_HOME_EVADING] = IFMSG_178_FLYING_HOME,
	[AI_ACTIVITY_BOARDING] = IFMSG_173_DOCKING,
	[AI_ACTIVITY_DISABLED] = IFMSG_159_HOLDING_STEADY,
};

/* Readies the pilots for a new flight. Nothing to ready until the pilot
 * program starts here. */
void ai_brain_start_flight(void) {}

/* Lets each craft whose think timer has run out decide again, once per
 * reference step. No craft decides until the pilot program answers here. */
void ai_brain_think_due_craft(void) {}

/* Whether the object matches the targets of the order g_cur_craft works on.
 * Answers 0 until the pilot program answers. */
int16_t ai_brain_order_targets_match_object(uint16_t object_index)
{
	(void)object_index;
	return 0;
}

/* Whether one of the object's flight group orders in slots 0 to 2 would send
 * it after the target with the leader plan named by leader_plan_name_index
 * in 1997 terms. Answers 0 until the pilot program answers. */
int ai_brain_order_gives_plan_on_target(int object_index,
					int leader_plan_name_index,
					int target_object_index)
{
	(void)object_index;
	(void)leader_plan_name_index;
	(void)target_object_index;
	return 0;
}

/* A mine's target search: the nearest targetable object that matches the
 * targets. Answers 0xFFFF, no target, until the pilot program answers, so
 * mines hold fire. */
int16_t ai_brain_find_mine_target(int16_t target1_type, uint16_t target1,
				  int16_t target1_or_target2,
				  int16_t target2_type, uint16_t target2,
				  int16_t require_clear_sweep)
{
	(void)target1_type;
	(void)target1;
	(void)target1_or_target2;
	(void)target2_type;
	(void)target2;
	(void)require_clear_sweep;
	return -1;
}

/* Returns the AI skill the craft flies with: the skill in the character data of
 * the object effective_ai_object_link points to, while that object still has the
 * saved signature and has character data, else the craft's own ai_skill. Clears
 * effective_ai_object_link when the signature no longer matches. */
// FUNCTION: XVT 0x404620
uint16_t ai_brain_effective_skill(struct craft_data *craft)
{
	struct object_record *linked_object = craft->effective_ai_object_link;
	if (linked_object == 0) {
		return craft->ai_skill;
	}

	if (linked_object->object_signature !=
	    craft->effective_ai_object_signature) {
		craft->effective_ai_object_link = 0;
		return craft->ai_skill;
	}

	if (linked_object->mobj == 0) {
		return craft->ai_skill;
	}
	if (linked_object->mobj->p_char_data != 0) {
		return linked_object->mobj->p_char_data->skill_value;
	}

	return craft->ai_skill;
}

/* Returns the index of the first craft in the active region's craft slots that
 * belongs to the flight group, is not breaking up or exploding, and has no
 * leader; 0xFFFF when there is none. Any flight group works; the callers pass a
 * mothership's. */
// FUNCTION: XVT 0x402E20
uint16_t ai_brain_find_mothership(int16_t mothership_flight_group_idx)
{
	uint16_t object_index = (uint16_t)g_active_region_object_slot_start;
	while (object_index < g_active_region_craft_object_slot_end) {
		struct object_record *object = &g_object_table[object_index];
		if (object->object_type != 0) {
			struct craft_data *craft = object->mobj->p_craft;
			uint8_t object_kind = craft->object_kind;
			if (object_kind != CRAFT_OBJECT_KIND_EXPLODING &&
			    object_kind != CRAFT_OBJECT_KIND_BREAKING_UP &&
			    object->flight_group_idx ==
				    (uint16_t)mothership_flight_group_idx &&
			    (uint8_t)craft->leader_obj_idx == UINT8_MAX) {
				return object_index;
			}
		}

		++object_index;
	}

	return UINT16_MAX;
}
