#include "xvt/flight/ai/paifight.h"

#include <limits.h>
#include <string.h>

#include "xvt/assets/model_mesh.h"
#include "xvt/flight/ai/pai_targetability.h"
#include "xvt/flight/ai/paiorder.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/log/log.h"

/* Flight group of the nearest object paifight_searchforclosestingroup found,
 * which only it writes; paifight_checkescortorder copies it into
 * escort_target_fg. */
// GLOBAL: XVT 0x9A1FDC
uint8_t g_ai_escort_candidate_fg_idx = 0;
/* Two marks per object slot, 1 when the object may be a turret target: element
 * 2 times the slot plus the set number, set 0 for an order's first pair of
 * target conditions and set 1 for its second.
 * paifight_build_gunner_target_candidate_set writes it;
 * paifight_find_nearest_gunner_target_in_candidate_set reads it. */
// GLOBAL: XVT 0x99F930
uint8_t g_paifight_gunner_target_candidate_set[976] = {0};
/* X of the point turret and mine target searches measure from, in world units:
 * a turret's hardpoint in world space, the craft's position, or a mine's
 * position. Written by paifight_missiledefenseorder,
 * paifight_gunnerselfdefenseorder, paifight_gunneroffenseorder and
 * laser_update_mine_weapon_fire. */
// GLOBAL: XVT 0xA08144
int g_paifight_search_origin_x = 0;
/* Y of the point turret and mine target searches measure from; see
 * g_paifight_search_origin_x. */
// GLOBAL: XVT 0xA08140
int g_paifight_search_origin_y = 0;
/* Z of the point turret and mine target searches measure from; see
 * g_paifight_search_origin_x. */
// GLOBAL: XVT 0xA08148
int g_paifight_search_origin_z = 0;
/* Counts the line-of-fire tests
 * paifight_find_nearest_gunner_target_in_candidate_set runs; set to 0 each time
 * the simulation is run up to a new target time, by
 * xvt_flight_sim_step_to_time. Nothing reads it. */
// GLOBAL: XVT 0xA8F750
int g_gunner_collision_probe_count = 0;
/* Distance in world units, by skill tier 0 to 2, within which
 * paifight_fightershootorder fires the cannons, before its changes for the
 * target's speed and size. */
// GLOBAL: XVT 0x524290
static const unsigned int g_ai_fighter_shoot_max_range_by_skill[3] = {
	0x6000, 0x8000, 0xA000};
/* Shots in each cannon burst paifight_fightershootorder sets, by skill tier 0
 * to 2; entry 3 is 0. */
// GLOBAL: XVT 0x52429C
static const uint8_t g_ai_fighter_shoot_burst_length_by_skill[4] = {3, 4, 5, 0};

/* Order 9: returns 1 after giving the craft a target, with its signature and
 * has_live_target 1, else 0; only while the craft is on its plan's maneuver. The
 * candidate target comes first when there is one (not 0xFFFF or
 * AI_TARGET_ABORT) and it can be targeted; one that cannot is cleared. Else it
 * searches by the current plan, with all three target_search_flags (7) and
 * require_undisabled_target set for disableldr1pln: the nearest order target on
 * capfreeldr1pln, disableldr1pln or kamikaze1pln, the nearest escort leader on
 * capescortersldr1pln, else the nearest attacker of an order target. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x45C370
int16_t paifight_scanfortargetorder(void)
{
	enum { TARGET_SEARCH_ALL_REQUIREMENTS = 7 };

	if (g_pai_context.controller->maneuver_mode ==
	    g_pai_context.initial_maneuver_id) {
		uint16_t candidate_target_idx =
			g_pai_context.controller->candidate_target_idx;
		if (candidate_target_idx != UINT16_MAX &&
		    candidate_target_idx != AI_TARGET_ABORT) {
			int valid_target =
				pai_is_object_targetable(candidate_target_idx);
			if (valid_target != 0) {
				g_pai_context.controller->target_obj_idx =
					candidate_target_idx;
				g_pai_context.controller->target_signature =
					g_object_table[candidate_target_idx]
						.object_signature;
				g_pai_context.controller->has_live_target = 1;
				XVT_LOG_DEBUG(
					"ai.target_picked object=%d target=%d reason=\"command\" leader=%d escort=%d plan=%d orderplan=%d order=%d predicted=%d",
					(int)g_pai_context.object_index,
					(int)g_pai_context.controller
						->target_obj_idx,
					(int)g_pai_context.leader_object_index,
					(int)g_pai_context.controller
						->escort_target_fg,
					(int)g_pai_context.controller
						->running_plan_id,
					(int)g_pai_context.controller
						->current_plan_id,
					(int)g_pai_context.order_slot,
					g_flight_sim_side_effects_suppressed);
				return 1;
			}
			g_pai_context.controller->candidate_target_idx =
				UINT16_MAX;
			XVT_LOG_DEBUG(
				"ai.command_target_dropped object=%d target=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)candidate_target_idx,
				g_flight_sim_side_effects_suppressed);
		}

		struct pai_plan_record *plan =
			&g_plan_table[g_pai_context.controller
					      ->current_plan_id];
		if (strcmp(plan->name, "disableldr1pln") == 0) {
			g_pai_context.require_undisabled_target = 1;
		} else {
			g_pai_context.require_undisabled_target = 0;
		}
		g_pai_context.target_search_flags =
			TARGET_SEARCH_ALL_REQUIREMENTS;
		int16_t target_object;
		if (strcmp(plan->name, "capfreeldr1pln") == 0 ||
		    strcmp(plan->name, "disableldr1pln") == 0 ||
		    strcmp(plan->name, "kamikaze1pln") == 0) {
			target_object =
				paifight_find_attack_order_target_from_order(
					g_pai_context.order_slot);
		} else if (strcmp(plan->name, "capescortersldr1pln") == 0) {
			target_object =
				paifight_target_escort_leader_from_order(
					g_pai_context.order_slot);
		} else {
			target_object =
				paifight_find_attacker_of_order_target_from_order(
					g_pai_context.order_slot);
		}
		if (target_object != -1) {
			g_pai_context.controller->target_obj_idx =
				(uint16_t)target_object;
			g_pai_context.controller->target_signature =
				g_object_table[(uint16_t)target_object]
					.object_signature;
			g_pai_context.controller->has_live_target = 1;
			XVT_LOG_DEBUG(
				"ai.target_picked object=%d target=%d reason=\"search\" leader=%d escort=%d plan=%d orderplan=%d order=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.controller->target_obj_idx,
				(int)g_pai_context.leader_object_index,
				(int)g_pai_context.controller->escort_target_fg,
				(int)g_pai_context.controller->running_plan_id,
				(int)g_pai_context.controller->current_plan_id,
				(int)g_pai_context.order_slot,
				g_flight_sim_side_effects_suppressed);
			return 1;
		}
	}
	return 0;
}

/* Returns what paifight_find_nearest_attack_order_target finds for the order slot's
 * first pair of target conditions, or when that is -1, for its second pair. */
// FUNCTION: XVT 0x45C630
int16_t paifight_find_attack_order_target_from_order(uint16_t order_slot)
{
	int order_index = order_slot;
	int16_t result = paifight_find_nearest_attack_order_target(
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target1_type,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target1,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target1_or_target2,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target2_type,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target2);
	if (result == -1) {
		result = paifight_find_nearest_attack_order_target(
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.secondary_target_types[0],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.secondary_targets[0],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.target3_or_target4,
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.secondary_target_types[1],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.secondary_targets[1]);
	}
	return result;
}

/* Returns the nearest object that matches the target conditions (either one
 * when target_or_mode is 1, else both), or -1 when none qualifies. A craft must
 * be outside the craft's own flight group, not the one the player told it to
 * avoid, targetable, in working order and not captured by its own team when
 * require_undisabled_target is set, and within skill range when target_search_flags
 * has 4; a static object with behavior flag 2 must be targetable and, with flag
 * 4, within range. It first counts these; with none it returns -1. Of them it
 * takes the nearest that passes paifight_target_has_attack_capacity when flag 1 is
 * set, measuring craft from the search origin in g_pai_context when flag 0x20 is
 * set, else from the craft, and skipping a craft farther than 0x4000 with its
 * decoy beam on. Sets g_last_rough_distance. */
// FUNCTION: XVT 0x45C720
int16_t paifight_find_nearest_attack_order_target(int16_t target1_type,
						  uint16_t target1,
						  int16_t target_or_mode,
						  int16_t target2_type,
						  uint16_t target2)
{
	enum {
		TARGET_RELATION_OR = 1,
		TARGETABLE_STATIC_MODEL_FLAG = 2,
		TARGET_SEARCH_REQUIRE_CAPACITY = 1,
		TARGET_SEARCH_REQUIRE_ORDER_RANGE = 4,
		TARGET_SEARCH_USE_ORIGIN = 0x20,
		ACTIVE_DECOY_IGNORE_RANGE = 0x4000
	};

	int candidate_count = 0;
	int16_t trigger1_matches;
	int16_t trigger2_matches;
	struct mobile_object *mobile_object;
	struct craft_data *craft;
	int valid_target;

	{
		for (uint16_t object_index =
			     (uint16_t)g_active_region_object_slot_start;
		     object_index < (int)g_active_region_craft_object_slot_end;
		     ++object_index) {
			int object_array_index = object_index;
			if (g_object_table[object_array_index].object_type ==
				    0 ||
			    g_object_table[object_array_index]
					    .flight_group_idx ==
				    g_pai_context.craft_flight_group_index) {
				continue;
			}
			trigger1_matches =
				mission_object_matches_trigger_variable(
					object_index, target1_type, target1);
			trigger2_matches =
				mission_object_matches_trigger_variable(
					object_index, target2_type, target2);
			if (target_or_mode == TARGET_RELATION_OR) {
				trigger1_matches |= trigger2_matches;
			} else {
				trigger1_matches &= trigger2_matches;
			}
			if (trigger1_matches == 0 ||
			    g_cur_craft->player_command_avoid_target_obj_idx ==
				    object_index) {
				continue;
			}

			valid_target = pai_is_object_targetable(object_index);
			if (valid_target == 0) {
				continue;
			}
			mobile_object = g_object_table[object_array_index].mobj;
			craft = mobile_object->p_craft;
			if ((g_pai_context.require_undisabled_target == 0 ||
			     (craft->working_subsystems != 0 &&
			      (g_pai_context.require_undisabled_target == 0 ||
			       craft->captured_by_flight_group == 0 ||
			       g_object_table[g_pai_context.object_index]
					       .mobj->team !=
				       g_object_table[object_array_index]
					       .mobj->team))) &&
			    ((g_pai_context.target_search_flags &
			      TARGET_SEARCH_REQUIRE_ORDER_RANGE) == 0 ||
			     pai_is_object_within_skill_range_of_craft(
				     object_index))) {
				++candidate_count;
			}
		}
	}

	{
		for (uint16_t object_index =
			     (uint16_t)g_region_main_object_slot_end;
		     object_index < g_region_static_object_slot_count +
					    g_region_main_object_slot_end;
		     ++object_index) {
			int object_array_index = object_index;
			uint16_t object_type =
				g_object_table[object_array_index].object_type;
			if (object_type == 0 ||
			    (g_object_type_table[object_type].behavior_flags &
			     TARGETABLE_STATIC_MODEL_FLAG) == 0) {
				continue;
			}
			trigger1_matches =
				mission_object_matches_trigger_variable(
					object_index, target1_type, target1);
			trigger2_matches =
				mission_object_matches_trigger_variable(
					object_index, target2_type, target2);
			if (target_or_mode == TARGET_RELATION_OR) {
				trigger1_matches |= trigger2_matches;
			} else {
				trigger1_matches &= trigger2_matches;
			}
			if (trigger1_matches == 0 ||
			    g_cur_craft->player_command_avoid_target_obj_idx ==
				    object_index) {
				continue;
			}

			valid_target = pai_is_object_targetable(object_index);
			if (valid_target != 0 &&
			    ((g_pai_context.target_search_flags &
			      TARGET_SEARCH_REQUIRE_ORDER_RANGE) == 0 ||
			     pai_is_object_within_skill_range_of_craft(
				     object_index))) {
				++candidate_count;
			}
		}
	}

	if (candidate_count == 0) {
		return -1;
	}
	unsigned int best_score = UINT_MAX;
	uint16_t best_object = UINT16_MAX;

	{
		for (uint16_t object_index =
			     (uint16_t)g_active_region_object_slot_start;
		     object_index < (int)g_active_region_craft_object_slot_end;
		     ++object_index) {
			int object_array_index = object_index;
			if (g_object_table[object_array_index].object_type ==
				    0 ||
			    g_object_table[object_array_index]
					    .flight_group_idx ==
				    g_pai_context.craft_flight_group_index) {
				continue;
			}
			trigger1_matches =
				mission_object_matches_trigger_variable(
					object_index, target1_type, target1);
			trigger2_matches =
				mission_object_matches_trigger_variable(
					object_index, target2_type, target2);
			if (target_or_mode == TARGET_RELATION_OR) {
				trigger1_matches |= trigger2_matches;
			} else {
				trigger1_matches &= trigger2_matches;
			}
			if (trigger1_matches == 0 ||
			    g_cur_craft->player_command_avoid_target_obj_idx ==
				    object_index) {
				continue;
			}

			valid_target = pai_is_object_targetable(object_index);
			if (valid_target == 0) {
				continue;
			}
			mobile_object = g_object_table[object_array_index].mobj;
			craft = mobile_object->p_craft;
			if ((g_pai_context.require_undisabled_target == 0 ||
			     (craft->working_subsystems != 0 &&
			      (g_pai_context.require_undisabled_target == 0 ||
			       craft->captured_by_flight_group == 0 ||
			       g_object_table[g_pai_context.object_index]
					       .mobj->team !=
				       g_object_table[object_array_index]
					       .mobj->team))) &&
			    ((g_pai_context.target_search_flags &
			      TARGET_SEARCH_REQUIRE_ORDER_RANGE) == 0 ||
			     pai_is_object_within_skill_range_of_craft(
				     object_index)) &&
			    ((g_pai_context.target_search_flags &
			      TARGET_SEARCH_REQUIRE_CAPACITY) == 0 ||
			     paifight_target_has_attack_capacity(
				     object_index,
				     (uint16_t)candidate_count))) {
				if ((g_pai_context.target_search_flags &
				     TARGET_SEARCH_USE_ORIGIN) != 0) {
					g_last_rough_distance = collide_roughdistance3d(
						g_object_table[object_array_index]
								.world_x -
							g_pai_context
								.target_search_origin_x,
						g_object_table[object_array_index]
								.world_y -
							g_pai_context
								.target_search_origin_y,
						g_object_table[object_array_index]
								.world_z -
							g_pai_context
								.target_search_origin_z);
				} else {
					pai_object_ref_update_rough_distance(
						g_pai_context.object_index,
						object_index);
				}
				if ((g_last_rough_distance <=
					     ACTIVE_DECOY_IGNORE_RANGE ||
				     object_has_active_decoy_beam(
					     object_index) != 1) &&
				    best_score >
					    (unsigned int)
						    g_last_rough_distance) {
					best_score = g_last_rough_distance;
					best_object = object_index;
				}
			}
		}
	}

	{
		for (uint16_t object_index =
			     (uint16_t)g_region_main_object_slot_end;
		     object_index < g_region_static_object_slot_count +
					    g_region_main_object_slot_end;
		     ++object_index) {
			int object_array_index = object_index;
			uint16_t object_type =
				g_object_table[object_array_index].object_type;
			if (object_type == 0 ||
			    (g_object_type_table[object_type].behavior_flags &
			     TARGETABLE_STATIC_MODEL_FLAG) == 0) {
				continue;
			}
			trigger1_matches =
				mission_object_matches_trigger_variable(
					object_index, target1_type, target1);
			trigger2_matches =
				mission_object_matches_trigger_variable(
					object_index, target2_type, target2);
			if (target_or_mode == TARGET_RELATION_OR) {
				trigger1_matches |= trigger2_matches;
			} else {
				trigger1_matches &= trigger2_matches;
			}
			if (trigger1_matches == 0 ||
			    g_cur_craft->player_command_avoid_target_obj_idx ==
				    object_index) {
				continue;
			}

			valid_target = pai_is_object_targetable(object_index);
			if (valid_target != 0 &&
			    ((g_pai_context.target_search_flags &
			      TARGET_SEARCH_REQUIRE_ORDER_RANGE) == 0 ||
			     pai_is_object_within_skill_range_of_craft(
				     object_index)) &&
			    ((g_pai_context.target_search_flags &
			      TARGET_SEARCH_REQUIRE_CAPACITY) == 0 ||
			     paifight_target_has_attack_capacity(
				     object_index,
				     (uint16_t)candidate_count))) {
				pai_object_ref_update_rough_distance(
					g_pai_context.object_index,
					object_index);
				if (best_score >
				    (unsigned int)g_last_rough_distance) {
					best_score = g_last_rough_distance;
					best_object = object_index;
				}
			}
		}
	}
	if (best_object == UINT16_MAX) {
		XVT_LOG_DEBUG(
			"ai.targets_refused object=%d candidates=%d undisabled=%d predicted=%d",
			(int)g_pai_context.object_index, candidate_count,
			(int)g_pai_context.require_undisabled_target,
			g_flight_sim_side_effects_suppressed);
	}
	return (int16_t)best_object;
}

/* Returns what paifight_target_nearest_escort_leader finds for the order slot's
 * first pair of target conditions, or when that is -1, for its second pair. */
// FUNCTION: XVT 0x45D1C0
int16_t paifight_target_escort_leader_from_order(uint16_t order_slot)
{
	int order_index = order_slot;
	int16_t result = paifight_target_nearest_escort_leader(
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target1_type,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target1,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target1_or_target2,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target2_type,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target2);
	if (result == -1) {
		result = paifight_target_nearest_escort_leader(
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.secondary_target_types[0],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.secondary_targets[0],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.target3_or_target4,
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.secondary_target_types[1],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.secondary_targets[1]);
	}
	return result;
}

/* Returns the nearest craft on escortldr1pln whose escort_target_fg is a flight
 * group that matches the target conditions (either one when target_relation_op is
 * 1, else both), or -1. The craft must not be the one the player told this
 * craft to avoid, must be targetable, within skill range when target_search_flags
 * has 4, and pass paifight_target_has_attack_capacity when it has 1. Makes the one
 * it finds the controller's target, with its signature and has_live_target 1.
 * Sets g_last_rough_distance. */
// FUNCTION: XVT 0x45D2B0
int16_t paifight_target_nearest_escort_leader(int16_t target1_type,
					      uint16_t target1,
					      int16_t target_relation_op,
					      int16_t target2_type,
					      uint16_t target2)
{
	uint16_t best_object_index = UINT16_MAX;
	unsigned int best_range_score = UINT32_MAX;
	uint16_t flight_group_idx = 0;
	if ((int16_t)g_mission_header.num_flight_groups > 0) {
		do {
			int16_t matches_target1 =
				mission_flight_group_matches_trigger_variable(
					flight_group_idx, target1_type,
					target1);
			int16_t matches_target2 =
				mission_flight_group_matches_trigger_variable(
					flight_group_idx, target2_type,
					target2);
			if (target_relation_op == 1) {
				matches_target1 |= matches_target2;
			} else {
				matches_target1 &= matches_target2;
			}

			if (matches_target1 != 0) {
				uint16_t object_index = (uint16_t)
					g_active_region_object_slot_start;
				while (object_index <
				       (int)g_active_region_craft_object_slot_end) {
					if (g_object_table[object_index]
						    .object_type != 0) {
						struct craft_data *craft =
							g_object_table
								[object_index]
									.mobj
									->p_craft;
						if (strcmp(g_plan_table
								   [craft->ai_controller
									    .current_plan_id]
									   .name,
							   "escortldr1pln") ==
							    0 &&
						    craft->ai_controller
								    .escort_target_fg ==
							    flight_group_idx &&
						    g_cur_craft->player_command_avoid_target_obj_idx !=
							    object_index) {
							int valid_target =
								pai_is_object_targetable(
									object_index);

							if (valid_target != 0 &&
							    ((g_pai_context
								      .target_search_flags &
							      4) == 0 ||
							     pai_is_object_within_skill_range_of_craft(
								     object_index)) &&
							    ((g_pai_context
								      .target_search_flags &
							      1) == 0 ||
							     paifight_target_has_attack_capacity(
								     object_index,
								     UINT8_MAX))) {
								pai_object_ref_update_rough_distance(
									g_pai_context
										.object_index,
									object_index);
								if (best_range_score >
								    (unsigned int)
									    g_last_rough_distance) {
									best_object_index =
										object_index;
									best_range_score =
										g_last_rough_distance;
								}
							}
						}
					}
					++object_index;
				}
			}
			++flight_group_idx;
		} while (flight_group_idx <
			 (int16_t)g_mission_header.num_flight_groups);
	}

	if (best_object_index != UINT16_MAX) {
		if (g_pai_context.controller->target_obj_idx !=
		    best_object_index) {
			XVT_LOG_DEBUG(
				"ai.target_picked object=%d target=%d reason=\"escort_leader\" leader=%d escort=%d plan=%d orderplan=%d order=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)best_object_index,
				(int)g_pai_context.leader_object_index,
				(int)g_pai_context.controller->escort_target_fg,
				(int)g_pai_context.controller->running_plan_id,
				(int)g_pai_context.controller->current_plan_id,
				(int)g_pai_context.order_slot,
				g_flight_sim_side_effects_suppressed);
		}
		g_pai_context.controller->target_obj_idx = best_object_index;
		g_pai_context.controller->target_signature =
			g_object_table[best_object_index].object_signature;
		g_pai_context.controller->has_live_target = 1;
	}
	return best_object_index;
}

/* Returns what paifight_find_nearest_attacker_of_matching_target finds for the order
 * slot's first pair of target conditions, or when that is -1, for its second
 * pair. */
// FUNCTION: XVT 0x45D5F0
int16_t paifight_find_attacker_of_order_target_from_order(uint16_t order_slot)
{
	int16_t result = paifight_find_nearest_attacker_of_matching_target(
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_slot]
			.target1_type,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_slot]
			.target1,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_slot]
			.target1_or_target2,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_slot]
			.target2_type,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_slot]
			.target2);
	if (result == -1) {
		result = paifight_find_nearest_attacker_of_matching_target(
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_slot]
					.secondary_target_types[0],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_slot]
					.secondary_targets[0],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_slot]
					.target3_or_target4,
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_slot]
					.secondary_target_types[1],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_slot]
					.secondary_targets[1]);
	}
	return result;
}

/* Returns the nearest craft attacking a craft that matches the target
 * conditions (either one when target_or_mode is 1, else both) and that some team
 * has attacked; -1 when none. An attacker is a craft in setup attack, attack or
 * rocket attack on that craft, or its ai_flight.threat_obj_idx; it is skipped when
 * the player told this craft to avoid the attacked craft. It must be
 * targetable, an enemy of the craft's team when a player flies it, within skill
 * range when target_search_flags has 4, and pass paifight_target_has_attack_capacity
 * when it has 1. It is measured from the search origin in g_pai_context with
 * flag 0x20, else from the craft; with flag 0x10 it must also lie within
 * AI_TARGET_RANGE_MAX and be an enemy. Sets g_last_rough_distance. */
// FUNCTION: XVT 0x45D6E0
int16_t paifight_find_nearest_attacker_of_matching_target(
	int16_t target1_type, uint16_t target1, int16_t target_or_mode,
	int16_t target2_type, uint16_t target2)
{
	uint16_t target_object_index =
		(uint16_t)g_active_region_object_slot_start;
	unsigned int best_range_score = UINT32_MAX;
	uint16_t best_object_index = UINT16_MAX;
	while (target_object_index <
	       (int)g_active_region_craft_object_slot_end) {
		if (g_object_table[target_object_index].object_type != 0) {
			uint16_t team_index = 0;
			struct craft_data *target_craft =
				g_object_table[target_object_index]
					.mobj->p_craft;
			int16_t has_attacker = 0;
			do {
				if (target_craft
					    ->attacked_by_team[team_index] !=
				    0) {
					has_attacker = 1;
				}
				++team_index;
			} while (team_index < 10);

			if (has_attacker != 0) {
				int16_t matches_target1 =
					mission_object_matches_trigger_variable(
						target_object_index,
						target1_type, target1);
				int16_t matches_target2 =
					mission_object_matches_trigger_variable(
						target_object_index,
						target2_type, target2);
				if (target_or_mode == 1) {
					matches_target1 |= matches_target2;
				} else {
					matches_target1 &= matches_target2;
				}

				if (matches_target1 != 0) {
					uint16_t object_index = (uint16_t)
						g_active_region_object_slot_start;
					while (object_index <
					       (int)g_active_region_craft_object_slot_end) {
						if (g_object_table[object_index]
							    .object_type != 0) {
							struct craft_data *craft =
								g_object_table[object_index]
									.mobj
									->p_craft;
							struct ai_controller
								*controller =
									&craft->ai_controller;
							int16_t maneuver_mode =
								controller
									->maneuver_mode;
							if ((((maneuver_mode ==
								       AI_MANEUVER_MODE_SETUP_ATTACK ||
							       maneuver_mode ==
								       AI_MANEUVER_MODE_ATTACK ||
							       maneuver_mode ==
								       AI_MANEUVER_MODE_ROCKET_ATTACK) &&
							      controller->target_obj_idx ==
								      target_object_index) ||
							     target_craft->ai_flight
									     .threat_obj_idx ==
								     object_index) &&
							    g_cur_craft->player_command_avoid_target_obj_idx !=
								    target_object_index) {
								int valid_target = pai_is_object_targetable(
									object_index);

								if (valid_target !=
								    0) {
									if (g_object_table[object_index]
										    .player_owner_idx !=
									    -1) {
										int object_team =
											g_mission_flight_groups
												[g_object_table[object_index]
													 .flight_group_idx]
													.fg
													.team;
										int source_team =
											g_object_table[g_pai_context
													       .object_index]
												.mobj
												->team;
										valid_target =
											object_team == source_team
												? 0
												: g_mission_teams[source_team]
														  .allies[object_team] <
													  1;
									}

									if (valid_target !=
										    0 &&
									    ((g_pai_context
										      .target_search_flags &
									      4) == 0 ||
									     pai_is_object_within_skill_range_of_craft(
										     object_index)) &&
									    ((g_pai_context
										      .target_search_flags &
									      1) == 0 ||
									     paifight_target_has_attack_capacity(
										     object_index,
										     UINT8_MAX))) {
										if ((g_pai_context
											     .target_search_flags &
										     0x20) !=
										    0) {
											g_last_rough_distance = collide_roughdistance3d(
												g_object_table[object_index]
														.world_x -
													g_pai_context
														.target_search_origin_x,
												g_object_table[object_index]
														.world_y -
													g_pai_context
														.target_search_origin_y,
												g_object_table[object_index]
														.world_z -
													g_pai_context
														.target_search_origin_z);
										} else {
											pai_object_ref_update_rough_distance(
												g_pai_context
													.object_index,
												object_index);
										}

										if ((g_pai_context
											     .target_search_flags &
										     0x10) !=
										    0) {
											if (g_last_rough_distance >
											    AI_TARGET_RANGE_MAX) {
												valid_target =
													0;
											} else {
												int object_team =
													g_mission_flight_groups
														[g_object_table[object_index]
															 .flight_group_idx]
															.fg
															.team;
												int source_team =
													g_object_table[g_pai_context
															       .object_index]
														.mobj
														->team;
												valid_target =
													object_team == source_team
														? 0
														: g_mission_teams[source_team]
																  .allies[object_team] <
															  1;
											}
										} else {
											valid_target =
												1;
										}

										if (valid_target !=
											    0 &&
										    (unsigned int)g_last_rough_distance <
											    best_range_score) {
											best_object_index =
												object_index;
											best_range_score =
												g_last_rough_distance;
										}
									}
								}
							}
						}
						++object_index;
					}
				}
			}
		}
		++target_object_index;
	}
	return (int16_t)best_object_index;
}

/* Returns 1 when fewer craft than the limit are in setup attack, attack or
 * rocket attack on the target (those told to avoid it not counted), else 0. For
 * a target in the craft slots the limit is 100 when candidate_count is 1, else 6
 * for a starship or platform, 4 for a freighter and 2 for the rest; for any
 * other target, 2. For a player's target the mission difficulty sets it: 4 or 2
 * at difficulty 0, 8 or 3 at 1, 100 or 4 at 2, the first when candidate_count is
 * 1. */
// FUNCTION: XVT 0x45DBA0
int16_t paifight_target_has_attack_capacity(uint16_t target_obj_idx,
					    uint16_t candidate_count)
{
	uint16_t attacker_count = 0;
	for (uint16_t object_index =
		     (uint16_t)g_active_region_object_slot_start;
	     object_index < g_active_region_craft_object_slot_end;
	     ++object_index) {
		struct object_record *object = &g_object_table[object_index];
		if (object->object_type != 0) {
			struct craft_data *craft = object->mobj->p_craft;
			struct ai_controller *controller =
				&craft->ai_controller;
			uint16_t attack_target_obj_idx =
				controller->target_obj_idx;
			if (target_obj_idx == attack_target_obj_idx &&
			    object_index != target_obj_idx &&
			    craft->player_command_avoid_target_obj_idx !=
				    attack_target_obj_idx) {
				uint8_t maneuver_mode =
					controller->maneuver_mode;
				if (maneuver_mode ==
					    AI_MANEUVER_MODE_SETUP_ATTACK ||
				    maneuver_mode == AI_MANEUVER_MODE_ATTACK ||
				    maneuver_mode ==
					    AI_MANEUVER_MODE_ROCKET_ATTACK) {
					++attacker_count;
				}
			}
		}
	}
	uint16_t candidate_total;
	uint16_t attacker_limit;
	if (target_obj_idx < g_active_region_craft_object_slot_end) {
		candidate_total = candidate_count;
		if (candidate_count != 1) {
			uint8_t genus_id =
				g_object_table[target_obj_idx].genus_id;
			if (genus_id == 4 || genus_id == 5) {
				attacker_limit = 6;
			} else if (genus_id == 3) {
				attacker_limit = 4;
			} else {
				attacker_limit = 2;
			}
		} else {
			attacker_limit = 100;
		}
	} else {
		attacker_limit = 2;
		candidate_total = candidate_count;
	}
	if (g_object_table[target_obj_idx].player_owner_idx != -1) {
		int difficulty = g_flight_mission_state.difficulty;
		if (difficulty != 0) {
			if (difficulty == 1) {
				attacker_limit = candidate_total == 1 ? 8 : 3;
			} else if (difficulty == 2) {
				attacker_limit = candidate_total == 1 ? 100 : 4;
			}
		} else {
			attacker_limit = candidate_total == 1 ? 4 : 2;
		}
	}

	return attacker_count < attacker_limit;
}

/* Besides answering, this sets g_pai_context's require_undisabled_target and
 * target_search_flags for the search, and for an escort-leader order
 * (capescortersldr1pln) makes the nearest escort leader the AI's target. */
/* Returns 1 when the search for the order slot's leader plan finds a target,
 * else 0: the nearest order target for capfreeldr1pln, disableldr1pln and
 * kamikaze1pln, the nearest escort leader for capescortersldr1pln, else the
 * nearest attacker of an order target. Sets g_last_rough_distance. */
// FUNCTION: XVT 0x45DD00
int16_t paifight_search_order_slot_target(uint16_t order_slot)
{
	struct pai_plan_record *plan =
		&g_plan_table
			[g_builtin_plan_id_by_name_index
				 [g_order_leader_builtin_plan_name_index
					  [g_mission_flight_groups
						   [g_pai_context
							    .craft_flight_group_index]
							   .fg
							   .orders[order_slot]
							   .order]]];
	if (strcmp(plan->name, "disableldr1pln") == 0) {
		g_pai_context.require_undisabled_target = 1;
	} else {
		g_pai_context.require_undisabled_target = 0;
	}
	g_pai_context.target_search_flags = 7;
	int16_t target_object;
	if (strcmp(plan->name, "capfreeldr1pln") == 0 ||
	    strcmp(plan->name, "disableldr1pln") == 0 ||
	    strcmp(plan->name, "kamikaze1pln") == 0) {
		target_object = paifight_find_attack_order_target_from_order(
			order_slot);
	} else if (strcmp(plan->name, "capescortersldr1pln") == 0) {
		target_object =
			paifight_target_escort_leader_from_order(order_slot);
	} else {
		target_object =
			paifight_find_attacker_of_order_target_from_order(
				order_slot);
	}
	return target_object != -1;
}

/* Besides answering, this sets g_pai_context's require_undisabled_target and
 * target_search_flags for the search, and for an escort-leader order
 * (capescortersldr1pln) makes the nearest escort leader the AI's target. */
/* Returns 1 when the order slot still has a target, else 0: for a
 * capescortersldr1pln leader plan, an escort leader found (made the target),
 * else at least one target counted by
 * paifight_count_remaining_order_targets_from_order_slot. Its target_search_flags of 2
 * leaves out the range and capacity tests. */
// FUNCTION: XVT 0x45DDF0
int16_t paifight_search_order_slot_remaining_targets(uint16_t order_slot)
{
	struct pai_plan_record *plan =
		&g_plan_table
			[g_builtin_plan_id_by_name_index
				 [g_order_leader_builtin_plan_name_index
					  [g_mission_flight_groups
						   [g_pai_context
							    .craft_flight_group_index]
							   .fg
							   .orders[order_slot]
							   .order]]];
	if (strcmp(plan->name, "disableldr1pln") == 0) {
		g_pai_context.require_undisabled_target = 1;
	} else {
		g_pai_context.require_undisabled_target = 0;
	}
	g_pai_context.target_search_flags = 2;
	int16_t result;
	if (strcmp(plan->name, "capescortersldr1pln") == 0) {
		result = paifight_target_escort_leader_from_order(order_slot);
	} else {
		result = paifight_count_remaining_order_targets_from_order_slot(
			order_slot);
	}
	return result != -1;
}

/* Returns what paifight_count_remaining_order_targets counts for the order slot's
 * first pair of target conditions, or when that is -1, for its second pair. */
// FUNCTION: XVT 0x45DEB0
int16_t
paifight_count_remaining_order_targets_from_order_slot(uint16_t order_slot)
{
	int order_index = order_slot;
	int16_t result = paifight_count_remaining_order_targets(
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target1_type,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target1,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target1_or_target2,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target2_type,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_index]
			.target2);
	if (result == -1) {
		result = paifight_count_remaining_order_targets(
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.secondary_target_types[0],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.secondary_targets[0],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.target3_or_target4,
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.secondary_target_types[1],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_index]
					.secondary_targets[1]);
	}
	return result;
}

/* Returns how many targetable objects belong to flight groups that match the
 * target conditions (either one when target_relation_op is 1, else both), or -1
 * when none do. Craft of the craft's own flight group do not count, nor, with
 * require_undisabled_target set, craft not in working order or captured by its
 * own team; static objects count when their type has behavior flag 2. */
// FUNCTION: XVT 0x45DFA0
int16_t paifight_count_remaining_order_targets(int16_t target1_type,
					       uint16_t target1,
					       int16_t target_relation_op,
					       int16_t target2_type,
					       uint16_t target2)
{
	uint16_t object_index = (uint16_t)g_active_region_object_slot_start;
	int target_count = 0;
	uint16_t flight_group_idx;
	int16_t matches_target1;
	int16_t matches_target2;
	int valid_target;
	while (object_index < (int)g_active_region_craft_object_slot_end) {
		int object_array_index = object_index;
		if (g_object_table[object_array_index].object_type != 0) {
			flight_group_idx = g_object_table[object_array_index]
						   .flight_group_idx;
			if (g_pai_context.craft_flight_group_index !=
			    flight_group_idx) {
				matches_target1 =
					mission_flight_group_matches_trigger_variable(
						flight_group_idx, target1_type,
						target1);
				matches_target2 =
					mission_flight_group_matches_trigger_variable(
						flight_group_idx, target2_type,
						target2);
				if (target_relation_op == 1) {
					matches_target1 |= matches_target2;
				} else {
					matches_target1 &= matches_target2;
				}

				if (matches_target1 != 0) {
					valid_target = pai_is_object_targetable(
						object_index);

					if (valid_target != 0) {
						struct mobile_object *mobile_object =
							g_object_table
								[object_array_index]
									.mobj;
						struct craft_data *craft =
							mobile_object->p_craft;
						if (g_pai_context.require_undisabled_target ==
							    0 ||
						    (craft->working_subsystems !=
							     0 &&
						     (g_pai_context.require_undisabled_target ==
							      0 ||
						      craft->captured_by_flight_group ==
							      0 ||
						      g_object_table[g_pai_context
									     .object_index]
								      .mobj
								      ->team !=
							      mobile_object
								      ->team))) {
							++target_count;
						}
					}
				}
			}
		}
		++object_index;
	}

	uint16_t static_object_index = (uint16_t)g_region_main_object_slot_end;
	while (static_object_index < (int)(g_region_main_object_slot_end +
					   g_region_static_object_slot_count)) {
		struct object_record *object =
			&g_object_table[static_object_index];
		if (object->object_type != 0 &&
		    (g_object_type_table[object->object_type].behavior_flags &
		     2) != 0) {
			flight_group_idx = object->flight_group_idx;
			matches_target1 =
				mission_flight_group_matches_trigger_variable(
					flight_group_idx, target1_type,
					target1);
			matches_target2 =
				mission_flight_group_matches_trigger_variable(
					flight_group_idx, target2_type,
					target2);
			if (target_relation_op == 1) {
				matches_target1 |= matches_target2;
			} else {
				matches_target1 &= matches_target2;
			}

			if (matches_target1 != 0) {
				valid_target = pai_is_object_targetable(
					static_object_index);
				if (valid_target != 0) {
					++target_count;
				}
			}
		}
		++static_object_index;
	}

	if (target_count != 0) {
		return (int16_t)target_count;
	}
	return -1;
}

/* Order 23: returns 1 after giving the craft a target, with its signature and
 * has_live_target 1, else 0; only while the craft is on its plan's maneuver. The
 * candidate target comes first, as in paifight_scanfortargetorder. Else it
 * takes the nearest other craft, within a rough 0x40000, whose own target is a
 * live object of flight group escort_target_fg and that passes
 * paifight_target_has_attack_capacity. Sets g_last_rough_distance. */
// FUNCTION: XVT 0x45E440
int16_t paifight_escorttargetorder(void)
{
	uint16_t source_obj_idx = g_pai_context.object_index;
	if (g_pai_context.controller->maneuver_mode ==
	    g_pai_context.initial_maneuver_id) {
		uint16_t candidate_target_idx =
			g_pai_context.controller->candidate_target_idx;
		if (candidate_target_idx != UINT16_MAX &&
		    candidate_target_idx != AI_TARGET_ABORT) {
			unsigned int object_index = candidate_target_idx;
			int valid_target =
				pai_is_object_targetable(object_index);

			if (valid_target != 0) {
				g_pai_context.controller->target_obj_idx =
					candidate_target_idx;
				g_pai_context.controller->target_signature =
					g_object_table[candidate_target_idx]
						.object_signature;
				g_pai_context.controller->has_live_target = 1;
				XVT_LOG_DEBUG(
					"ai.target_picked object=%d target=%d reason=\"command\" leader=%d escort=%d plan=%d orderplan=%d order=%d predicted=%d",
					(int)g_pai_context.object_index,
					(int)g_pai_context.controller
						->target_obj_idx,
					(int)g_pai_context.leader_object_index,
					(int)g_pai_context.controller
						->escort_target_fg,
					(int)g_pai_context.controller
						->running_plan_id,
					(int)g_pai_context.controller
						->current_plan_id,
					(int)g_pai_context.order_slot,
					g_flight_sim_side_effects_suppressed);
				return 1;
			}
			g_pai_context.controller->candidate_target_idx =
				UINT16_MAX;
			XVT_LOG_DEBUG(
				"ai.command_target_dropped object=%d target=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)candidate_target_idx,
				g_flight_sim_side_effects_suppressed);
		}

		uint16_t best_target_obj_idx = UINT16_MAX;
		uint16_t scan_obj_idx =
			(uint16_t)g_active_region_object_slot_start;
		unsigned int best_range_score = UINT32_MAX;
		uint16_t escort_flight_group_idx =
			g_pai_context.controller->escort_target_fg;
		for (; scan_obj_idx < g_active_region_craft_object_slot_end;
		     ++scan_obj_idx) {
			struct object_record *object =
				&g_object_table[scan_obj_idx];
			if (object->object_type != 0 &&
			    g_pai_context.object_index != scan_obj_idx) {
				int targets_escort_flight_group = 0;
				uint16_t target_obj_idx =
					object->mobj->p_craft->ai_controller
						.target_obj_idx;
				if (target_obj_idx < 0x8000 &&
				    target_obj_idx != UINT16_MAX) {
					struct object_record *target =
						&g_object_table[target_obj_idx];
					if (target->object_type != 0) {
						targets_escort_flight_group =
							target->flight_group_idx ==
							escort_flight_group_idx;
					}
				}
				if (targets_escort_flight_group != 0 &&
				    paifight_target_has_attack_capacity(
					    scan_obj_idx, 0xFF)) {
					pai_object_ref_update_rough_distance(
						source_obj_idx, scan_obj_idx);
					if (best_range_score >
					    (unsigned int)
						    g_last_rough_distance) {
						if (g_last_rough_distance <
						    0x40000) {
							best_target_obj_idx =
								scan_obj_idx;
							best_range_score =
								g_last_rough_distance;
						}
					}
				}
			}
		}

		if (best_target_obj_idx != UINT16_MAX) {
			g_pai_context.controller->target_obj_idx =
				best_target_obj_idx;
			g_pai_context.controller->target_signature =
				g_object_table[best_target_obj_idx]
					.object_signature;
			g_pai_context.controller->has_live_target = 1;
			XVT_LOG_DEBUG(
				"ai.target_picked object=%d target=%d reason=\"escort_threat\" leader=%d escort=%d plan=%d orderplan=%d order=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.controller->target_obj_idx,
				(int)g_pai_context.leader_object_index,
				(int)g_pai_context.controller->escort_target_fg,
				(int)g_pai_context.controller->running_plan_id,
				(int)g_pai_context.controller->current_plan_id,
				(int)g_pai_context.order_slot,
				g_flight_sim_side_effects_suppressed);
			return 1;
		}
	}
	return 0;
}

/* Order 5: aims and fires the craft's cannons and warheads at its target;
 * returns 0 on every path, at once when no subsystem works. With a target that
 * cannot be targeted it sets every cannon group's link mode to 0. Else the
 * cannons fire when the target lies within 0x800 of the craft's yaw and pitch
 * and inside g_ai_fighter_shoot_max_range_by_skill; for a target in the region's main
 * slots that is 0x4000 less when it moves faster than 25 with its yaw within
 * 0x2000 of the craft's, 0x2000 less within 0x5000, and 0x6000 more for a
 * platform or starship. The link mode is 3 within 0x2000, 2 within 0x4000, else
 * 1, with a burst from g_ai_fighter_shoot_burst_length_by_skill; ion cannons fire
 * only on disableldr1pln with a live target, and other cannons not then. For a
 * target in the craft slots without an active decoy beam, in rocket attack, it
 * fires warheads of the class the target calls for when the target's front
 * shield and remaining hull outlast the homing warheads already aimed at it,
 * fewer of them are coming than a limit, weapons are not inhibited and it has
 * fired fewer than its per-maneuver limit. On disableldr1pln the hull counts
 * only as a tenth of hull_max, once its damage passes that, and the craft's own
 * warheads count as already coming. It builds warhead_lock_ticks by think_interval
 * (three quarters of it below tier 2) while within 0x300 and fire range, and
 * lets it fall otherwise; at 472 times the tier plus 1 (half that at tier 2
 * with a damaged hull) it picks target_component and fires each ready launcher,
 * counting warheads_fired_this_maneuver. */
// FUNCTION: XVT 0x45E780
int16_t paifight_fightershootorder(void)
{
	enum {
		MAX_SHOOTING_SKILL_TIER = 2,
		FAST_TARGET_SPEED = 25,
		ANGLE_HALF_TURN = 0x8000,
		MAX_TARGET_ANGLE = 0x800,
		MAX_WARHEAD_ANGLE = 0x300,
		FAST_TARGET_SAME_HEADING_ANGLE = 0x2000,
		FAST_TARGET_OBLIQUE_ANGLE = 0x5000,
		FAST_TARGET_SAME_HEADING_RANGE_REDUCTION = 0x4000,
		FAST_TARGET_OBLIQUE_RANGE_REDUCTION = 0x2000,
		LARGE_TARGET_RANGE_BONUS = 0x6000,
		LASER_LINK_CLOSE_RANGE = 0x2000,
		LASER_LINK_MEDIUM_RANGE = 0x4000,
		LASER_LINK_ALL_CANNONS = 3,
		LARGE_TARGET_INCOMING_LIMIT = 16,
		SMALL_TARGET_INCOMING_LIMIT = 2,
		LARGE_TARGET_MANEUVER_LIMIT = 6,
		SMALL_TARGET_MANEUVER_LIMIT = 1,
		LARGE_TARGET_WARHEAD_CLASS = 2,
		SMALL_TARGET_WARHEAD_CLASS = 1,
		SPECIAL_WARHEAD_EXTRA_COUNT = 7,
		SPECIAL_WARHEAD_INHIBIT_LIMIT = 0x49C,
		MISSION_VERSION_SPECIAL_WARHEADS = 14,
		DISABLE_HULL_THRESHOLD_DIVISOR = 10,
		LARGE_TARGET_DAMAGE_SHIFT = 4,
		FREIGHTER_DAMAGE_SHIFT = 2,
		WARHEAD_LOCK_TICKS_PER_SKILL_LEVEL = 472,
		LOWER_SKILL_LOCK_RATE = 0xC000,
		WEAPON_INHIBIT_BEAM_EFFECT_SLOT = 2,
		PAIRED_FIRE_LAUNCHER_FLAGS = 3,
		NORMAL_LAUNCHER_FLAGS = 1,
		IMBALANCED_LAUNCHER_FLAGS = 0x81
	};

	if (g_cur_craft->working_subsystems == 0) {
		return 0;
	}

	uint16_t target_index = g_pai_context.controller->target_obj_idx;
	uint16_t burst_remaining = 0;
	int is_valid_target = pai_is_object_targetable(target_index);

	uint16_t slot_index;
	if (is_valid_target != 0) {
		unsigned int max_range = g_ai_fighter_shoot_max_range_by_skill
			[g_pai_context.skill_tier];
		uint16_t target_angle;
		if ((int)g_region_main_object_slot_end > target_index) {
			struct object_record *target_object =
				&g_object_table[target_index];
			struct mobile_object *target_mobile =
				target_object->mobj;
			if (target_mobile != NULL &&
			    target_mobile->speed > FAST_TARGET_SPEED) {
				target_angle =
					(uint16_t)(g_object_table
							   [g_pai_context
								    .object_index]
								   .yaw -
						   target_object->yaw);
				if (target_angle >= ANGLE_HALF_TURN) {
					target_angle = (uint16_t)-target_angle;
				}
				if (target_angle <
				    FAST_TARGET_SAME_HEADING_ANGLE) {
					max_range -=
						FAST_TARGET_SAME_HEADING_RANGE_REDUCTION;
				} else if (target_angle <
					   FAST_TARGET_OBLIQUE_ANGLE) {
					max_range -=
						FAST_TARGET_OBLIQUE_RANGE_REDUCTION;
				}
			}
			if (target_object->genus_id == CRAFT_GENUS_PLATFORM ||
			    target_object->genus_id == CRAFT_GENUS_STARSHIP) {
				max_range += LARGE_TARGET_RANGE_BONUS;
			}
		}

		pai_object_ref_direction_to_object_ref(
			g_pai_context.object_index, target_index);
		target_angle =
			(uint16_t)(trig2_xyangle -
				   g_object_table[g_pai_context.object_index]
					   .yaw);
		if (target_angle >= ANGLE_HALF_TURN) {
			target_angle = (uint16_t)-target_angle;
		}
		uint16_t pitch_angle =
			(uint16_t)(trig2_pitch - g_cur_craft->pitch);
		if (pitch_angle >= ANGLE_HALF_TURN) {
			pitch_angle = (uint16_t)-pitch_angle;
		}
		uint16_t link_mode;
		if (target_angle >= MAX_TARGET_ANGLE ||
		    pitch_angle >= MAX_TARGET_ANGLE ||
		    (unsigned int)trig2_polardistance >= max_range) {
			link_mode = 0;
		} else {
			link_mode = LASER_LINK_ALL_CANNONS;
			if (trig2_polardistance >= LASER_LINK_CLOSE_RANGE) {
				link_mode = (uint16_t)(trig2_polardistance <
						       LASER_LINK_MEDIUM_RANGE);
				++link_mode;
			}
			burst_remaining =
				g_ai_fighter_shoot_burst_length_by_skill
					[g_pai_context.skill_tier];
		}

		uint16_t cannon_class_count = g_cur_craft->cannon_group_count;
		uint16_t current_plan_id =
			g_pai_context.controller->current_plan_id;
		for (slot_index = 0; slot_index < cannon_class_count;
		     ++slot_index) {
			uint16_t slot_link;

			if (link_mode != 0) {
				if (g_cur_craft->laser_state
					    .projectile_type_id[slot_index] !=
				    PROJECTILE_OBJECT_TYPE_ION_LASER) {
					if (g_pai_context.controller
						    ->has_live_target != 0) {
						slot_link =
							strcmp(g_plan_table[current_plan_id]
								       .name,
							       "disableldr1pln") !=
									0
								? link_mode
								: 0;
					} else {
						slot_link = link_mode;
					}
				} else if (strcmp(g_plan_table[current_plan_id]
							  .name,
						  "disableldr1pln") == 0 &&
					   g_pai_context.controller
							   ->has_live_target ==
						   1) {
					slot_link = link_mode;
				} else {
					slot_link = 0;
				}
			} else {
				slot_link = 0;
			}
			g_cur_craft->laser_state.link_mode[slot_index] =
				slot_link;
			g_cur_craft->laser_state.burst_remaining[slot_index] =
				burst_remaining;
		}

		if ((int)g_active_region_craft_object_slot_end <=
			    target_index ||
		    object_has_active_decoy_beam(target_index)) {
			return 0;
		}

		{
			uint16_t incoming_limit;
			uint16_t warheads_per_maneuver_limit;
			uint16_t required_warhead_class;
			unsigned int fire_range;
			if (g_object_table[target_index].genus_id ==
				    CRAFT_GENUS_STARSHIP ||
			    g_object_table[target_index].genus_id ==
				    CRAFT_GENUS_PLATFORM ||
			    g_object_table[target_index].genus_id ==
				    CRAFT_GENUS_FREIGHTER ||
			    (g_object_table[target_index].genus_id ==
				     CRAFT_GENUS_TRANSPORT &&
			     g_mission_file_version ==
				     MISSION_VERSION_SPECIAL_WARHEADS)) {
				required_warhead_class =
					LARGE_TARGET_WARHEAD_CLASS;
				warheads_per_maneuver_limit =
					LARGE_TARGET_MANEUVER_LIMIT;
				incoming_limit = LARGE_TARGET_INCOMING_LIMIT;
				/* Typed literals preserve the original unsigned range selection. */
				fire_range =
					g_pai_context.skill_tier ==
							MAX_SHOOTING_SKILL_TIER
						? 244332u
						: 203610u;
			} else {
				fire_range = 101805u;
				incoming_limit = SMALL_TARGET_INCOMING_LIMIT;
				required_warhead_class =
					SMALL_TARGET_WARHEAD_CLASS;
				warheads_per_maneuver_limit =
					SMALL_TARGET_MANEUVER_LIMIT;
			}

			struct craft_data *target_craft =
				g_object_table[target_index].mobj->p_craft;
			unsigned int target_durability =
				(unsigned int)target_craft->shield_energy[0];
			unsigned int incoming_damage = 0;
			uint16_t launcher_index;
			if (strcmp(g_plan_table[current_plan_id].name,
				   "disableldr1pln") == 0) {
				unsigned int hull_threshold =
					target_craft->hull_max /
					DISABLE_HULL_THRESHOLD_DIVISOR;
				if (target_craft->hull_damage >
				    hull_threshold) {
					target_durability += hull_threshold;
				}
				unsigned int launcher_count =
					g_cur_craft->warhead_launcher_count;
				for (launcher_index = 0;
				     launcher_index < launcher_count;
				     ++launcher_index) {
					uint8_t projectile_type =
						g_cur_craft
							->warhead_slot_type_ids
								[launcher_index];
					if (g_projectile_type_data.warhead_class
							    [projectile_type -
							     PROJECTILE_OBJECT_TYPE_FIRST] ==
						    required_warhead_class ||
					    (g_mission_file_version ==
						     MISSION_VERSION_SPECIAL_WARHEADS &&
					     (projectile_type ==
						      WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE ||
					      projectile_type ==
						      WARHEAD_OBJECT_TYPE_ION_PULSE) &&
					     required_warhead_class ==
						     LARGE_TARGET_WARHEAD_CLASS)) {
						unsigned int missile_damage =
							g_projectile_type_data.damage
								[projectile_type -
								 PROJECTILE_OBJECT_TYPE_FIRST];
						if (g_object_table[target_index]
								    .genus_id ==
							    CRAFT_GENUS_STARSHIP ||
						    g_object_table[target_index]
								    .genus_id ==
							    CRAFT_GENUS_PLATFORM) {
							missile_damage >>=
								LARGE_TARGET_DAMAGE_SHIFT;
						}
						if (g_object_table[target_index]
							    .genus_id ==
						    CRAFT_GENUS_FREIGHTER) {
							missile_damage >>=
								FREIGHTER_DAMAGE_SHIFT;
						}
						incoming_damage +=
							missile_damage;
					}
				}
			} else {
				target_durability = target_craft->hull_max +
						    target_durability -
						    target_craft->hull_damage;
			}

			uint16_t projectile_object_index;
			for (projectile_object_index =
				     g_projectile_object_slot_start;
			     projectile_object_index <
			     (int)g_projectile_object_slot_end;
			     ++projectile_object_index) {
				struct object_record *projectile_object =
					&g_object_table
						[projectile_object_index];
				if (projectile_object->object_type >=
				    PROJECTILE_OBJECT_TYPE_FIRST) {
					struct warhead_guidance_state *guidance =
						projectile_object->mobj
							->p_warhead_guidance;
					if (guidance->homing_tier != 0 &&
					    guidance->target_obj_idx ==
						    target_index) {
						unsigned int missile_damage =
							g_projectile_type_data.damage
								[projectile_object
									 ->object_type -
								 PROJECTILE_OBJECT_TYPE_FIRST];
						if (g_object_table[target_index]
								    .genus_id ==
							    CRAFT_GENUS_STARSHIP ||
						    g_object_table[target_index]
								    .genus_id ==
							    CRAFT_GENUS_PLATFORM) {
							missile_damage >>=
								LARGE_TARGET_DAMAGE_SHIFT;
						}
						if (g_object_table[target_index]
							    .genus_id ==
						    CRAFT_GENUS_FREIGHTER) {
							missile_damage >>=
								FREIGHTER_DAMAGE_SHIFT;
						}
						incoming_damage +=
							missile_damage;
					}
				}
			}
			if (target_durability > incoming_damage) {
				uint16_t incoming_count = 0;
				for (projectile_object_index =
					     g_projectile_object_slot_start;
				     projectile_object_index <
				     (int)g_projectile_object_slot_end;
				     ++projectile_object_index) {
					struct object_record *projectile_object =
						&g_object_table
							[projectile_object_index];
					uint8_t projectile_type =
						projectile_object->object_type;
					/* Impact effects remain in projectile slots after a hit. */
					if (projectile_type <
						    PROJECTILE_OBJECT_TYPE_FIRST ||
					    projectile_type >=
						    PROJECTILE_OBJECT_TYPE_FIRST +
							    PROJECTILE_OBJECT_TYPE_COUNT) {
						continue;
					}
					if (projectile_type != 0 &&
					    (g_projectile_type_data.warhead_class
							     [projectile_type -
							      PROJECTILE_OBJECT_TYPE_FIRST] ==
						     required_warhead_class ||
					     (g_mission_file_version ==
						      MISSION_VERSION_SPECIAL_WARHEADS &&
					      (projectile_type ==
						       WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE ||
					       projectile_type ==
						       WARHEAD_OBJECT_TYPE_ION_PULSE) &&
					      required_warhead_class ==
						      LARGE_TARGET_WARHEAD_CLASS))) {
						struct warhead_guidance_state *guidance =
							projectile_object->mobj
								->p_warhead_guidance;
						if (guidance->homing_tier !=
							    0 &&
						    guidance->target_obj_idx ==
							    target_index) {
							++incoming_count;
							if (g_mission_file_version ==
								    MISSION_VERSION_SPECIAL_WARHEADS &&
							    (projectile_type ==
								     WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE ||
							     projectile_type ==
								     WARHEAD_OBJECT_TYPE_ION_PULSE) &&
							    required_warhead_class ==
								    LARGE_TARGET_WARHEAD_CLASS) {
								incoming_count +=
									SPECIAL_WARHEAD_EXTRA_COUNT;
							}
						}
					}
				}
				if (incoming_count < incoming_limit &&
				    g_pai_context.controller->maneuver_mode ==
					    AI_MANEUVER_MODE_ROCKET_ATTACK &&
				    g_cur_craft->weapon_fire_inhibit_timer ==
					    0 &&
				    g_cur_craft->beam_effect_accum
						    [WEAPON_INHIBIT_BEAM_EFFECT_SLOT] ==
					    0 &&
				    g_cur_craft->ai_flight
						    .warheads_fired_this_maneuver <
					    warheads_per_maneuver_limit) {
					if (target_angle >= MAX_WARHEAD_ANGLE ||
					    pitch_angle >= MAX_WARHEAD_ANGLE ||
					    (unsigned int)trig2_polardistance >=
						    fire_range) {
						g_cur_craft
							->warhead_lock_ticks -=
							g_pai_context
								.controller
								->think_interval;
						if (g_cur_craft
							    ->warhead_lock_ticks <
						    0) {
							g_cur_craft
								->warhead_lock_ticks =
								0;
						}
					} else {
						uint16_t lock_threshold =
							(uint16_t)(WARHEAD_LOCK_TICKS_PER_SKILL_LEVEL *
								   (g_pai_context
									    .skill_tier +
								    1));
						if (g_pai_context.skill_tier ==
						    MAX_SHOOTING_SKILL_TIER) {
							g_cur_craft
								->warhead_lock_ticks +=
								g_pai_context
									.controller
									->think_interval;
							if (g_cur_craft
								    ->hull_damage >=
							    g_cur_craft
								    ->system_damage_hull_threshold) {
								lock_threshold >>=
									1;
							}
						} else {
							g_cur_craft
								->warhead_lock_ticks += math2_fraction(
								g_pai_context
									.controller
									->think_interval,
								LOWER_SKILL_LOCK_RATE);
						}
						if (g_cur_craft
							    ->warhead_lock_ticks >=
						    (int)lock_threshold) {
							for (launcher_index = 0;
							     launcher_index <
							     g_cur_craft
								     ->warhead_launcher_count;
							     ++launcher_index) {
								if (g_cur_craft->warhead_launcher_cooldown_ticks
									    [launcher_index] ==
								    0) {
									uint8_t projectile_type =
										g_cur_craft
											->warhead_slot_type_ids
												[launcher_index];
									if (g_projectile_type_data
											    .warhead_class
												    [projectile_type -
												     PROJECTILE_OBJECT_TYPE_FIRST] ==
										    required_warhead_class ||
									    (g_mission_file_version ==
										     MISSION_VERSION_SPECIAL_WARHEADS &&
									     (projectile_type ==
										      WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE ||
									      projectile_type ==
										      WARHEAD_OBJECT_TYPE_ION_PULSE) &&
									     required_warhead_class ==
										     LARGE_TARGET_WARHEAD_CLASS &&
									     (target_index ==
										      UINT16_MAX ||
									      g_object_table[target_index]
											      .mobj
											      ->p_craft
											      ->weapon_fire_inhibit_timer ==
										      0 ||
									      (uint16_t)g_object_table[target_index]
											      .mobj
											      ->p_craft
											      ->weapon_fire_inhibit_timer <
										      SPECIAL_WARHEAD_INHIBIT_LIMIT))) {
										if (g_pai_context.skill_tier ==
											    MAX_SHOOTING_SKILL_TIER &&
										    g_cur_craft->hull_damage >=
											    g_cur_craft
												    ->system_damage_hull_threshold) {
											g_cur_craft
												->warhead_launcher_flags
													[launcher_index] =
												PAIRED_FIRE_LAUNCHER_FLAGS;
										} else {
											int weapon_slot_index =
												g_model_defs[g_cur_craft
														     ->model_index]
													.warhead_launcher_first_slot
														[launcher_index];
											g_cur_craft
												->warhead_launcher_flags
													[launcher_index] =
												NORMAL_LAUNCHER_FLAGS;
											if (g_cur_craft
												    ->weapon_slots
													    [weapon_slot_index]
												    .ammo_count <
											    g_cur_craft
												    ->weapon_slots
													    [weapon_slot_index +
													     1]
												    .ammo_count) {
												g_cur_craft
													->warhead_launcher_flags
														[launcher_index] =
													(int8_t)IMBALANCED_LAUNCHER_FLAGS;
											}
										}
										g_pai_context
											.controller
											->target_component =
											paifight_select_target_component_mesh(
												target_index);
										laser_firewarheadsystem(
											g_pai_context
												.object_index,
											launcher_index);
										++g_cur_craft
											  ->ai_flight
											  .warheads_fired_this_maneuver;
										XVT_LOG_DEBUG(
											"ai.warheads_fired object=%d launcher=%u target=%d component=%d lock=%d needed=%d fired=%d limit=%d flags=%d predicted=%d",
											(int)g_pai_context
												.object_index,
											(unsigned)
												launcher_index,
											(int)target_index,
											(int)g_pai_context
												.controller
												->target_component,
											(int)g_cur_craft
												->warhead_lock_ticks,
											(int)lock_threshold,
											(int)g_cur_craft
												->ai_flight
												.warheads_fired_this_maneuver,
											(int)warheads_per_maneuver_limit,
											(int)(uint8_t)g_cur_craft
												->warhead_launcher_flags
													[launcher_index],
											g_flight_sim_side_effects_suppressed);
										if (g_object_table[target_index]
												    .genus_id ==
											    CRAFT_GENUS_STARFIGHTER ||
										    g_object_table[target_index]
												    .genus_id ==
											    CRAFT_GENUS_TRANSPORT) {
											g_cur_craft
												->warhead_lock_ticks =
												0;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	} else {
		for (slot_index = 0;
		     slot_index < g_cur_craft->cannon_group_count;
		     ++slot_index) {
			g_cur_craft->laser_state.link_mode[slot_index] = 0;
		}
	}
	return 0;
}

/* Returns the mesh of the target to aim warheads at: one of its main hull or
 * fuselage meshes, the one nearest the craft for object type 54, else one
 * picked with game_rand_range. Returns 0 for a target outside the craft slots. */
// FUNCTION: XVT 0x45F1E0
uint16_t paifight_select_target_component_mesh(uint16_t target_obj_idx)
{
	uint16_t candidate_count = 0;

	int select_nearest = g_object_table[target_obj_idx].object_type == 54;
	candidate_count = 0;
	uint8_t candidate_meshes[52];
	candidate_meshes[0] = 0;
	unsigned int nearest_distance = 0x1000000;
	if (g_active_region_craft_object_slot_end > target_obj_idx) {
		int object_type = g_object_table[target_obj_idx].object_type;
		int mesh_count;
		if (object_type < 73) {
			mesh_count = g_object_type_mesh_cache[object_type]
					     .mesh_count;
		} else {
			mesh_count = model_mesh_get_object_type_mesh_count(
				object_type);
		}

		uint16_t selected_mesh_idx = 0;
		for (uint16_t mesh_index = 0; mesh_index < mesh_count;
		     ++mesh_index) {
			int adjusted_mesh_index = mesh_index;

			object_type =
				g_object_table[target_obj_idx].object_type;
			mesh_component_type mesh_type;
			if (object_type < 73) {
				if (adjusted_mesh_index < 0) {
					mesh_type = MESH_COMPONENT_00_DEFAULT;
				} else {
					int cached_mesh_count =
						g_object_type_mesh_cache
							[object_type]
								.mesh_count;
					if (cached_mesh_count <= mesh_index) {
						adjusted_mesh_index =
							cached_mesh_count - 1;
					}
					mesh_type =
						g_object_type_mesh_cache[object_type]
							.mesh_types
								[adjusted_mesh_index];
				}
			} else {
				mesh_type =
					model_mesh_get_object_type_mesh_type(
						object_type, mesh_index);
			}

			if ((uint16_t)mesh_type ==
				    MESH_COMPONENT_01_MAIN_HULL ||
			    (uint16_t)mesh_type == MESH_COMPONENT_03_FUSELAGE) {
				if (select_nearest) {
					unsigned int distance =
						object_direction_and_distance_to_mesh_center(
							g_pai_context
								.object_index,
							target_obj_idx,
							mesh_index);

					if (distance < nearest_distance) {
						selected_mesh_idx = mesh_index;
						nearest_distance = distance;
					}
				} else {
					candidate_meshes[candidate_count++] =
						(uint8_t)mesh_index;
				}
			}
		}

		if (select_nearest) {
			return selected_mesh_idx;
		}
		return candidate_meshes[game_rand_range(candidate_count)];
	}
	return 0;
}

/* Order 8: fires defense warheads from launchers of the craft; returns 0 on
 * every path. Not for a starfighter, a craft breaking up, with no subsystem
 * working or with weapons inhibited. Each launcher slot with an intact mesh,
 * holding warhead type 0x90 or 0x95 and a round, first waits out its
 * missile_defense_cooldown, one per think. Then it picks a target, measured from
 * the slot's hardpoint, which it puts in g_paifight_search_origin_x, Y and Z
 * (hardpoint doubled for object type 53): the nearest homing warhead aimed at
 * the craft more than 0x4000 and less than 0x40000 away that no homing warhead
 * of tier 5 or more chases, else the nearest targetable craft within 0x40000
 * with fewer than two homing warheads on it that attacks the craft or is an
 * enemy player's craft targeting it. It fires a homing warhead at it with a
 * random homing tier of 3 to 6 and a cooldown of 20 thinks. The attack test
 * compares a craft's own last_attacker_obj_idx and threat_obj_idx with its own
 * index. Leaves the slot's turret target and target_component set. Sets
 * g_last_rough_distance. */
// FUNCTION: XVT 0x45F380
int16_t paifight_missiledefenseorder(void)
{
	if (g_cur_craft->object_kind == CRAFT_OBJECT_KIND_BREAKING_UP) {
		return 0;
	}
	if (g_cur_craft->working_subsystems == 0) {
		return 0;
	}
	if (g_cur_craft->weapon_fire_inhibit_timer != 0) {
		return 0;
	}
	if (g_object_table[g_pai_context.object_index].genus_id == 0) {
		return 0;
	}

	uint16_t launcher_index = 0;
	while (launcher_index < g_cur_craft->warhead_launcher_count) {
		uint16_t weapon_slot_index =
			g_model_defs[g_cur_craft->model_index]
				.warhead_launcher_first_slot[launcher_index];
		while (g_model_defs[g_cur_craft->model_index]
				       .warhead_launcher_last_slot
					       [launcher_index] +
			       1 >
		       weapon_slot_index) {
			uint8_t mesh_index =
				g_model_defs[g_cur_craft->model_index]
					.weapon_hardpoints[weapon_slot_index]
					.mesh_idx;

			if (g_cur_craft->component_state[mesh_index] == 0) {
				if ((g_cur_craft
						     ->weapon_slots
							     [weapon_slot_index]
						     .projectile_type_id ==
					     0x90 ||
				     g_cur_craft
						     ->weapon_slots
							     [weapon_slot_index]
						     .projectile_type_id ==
					     0x95) &&
				    g_cur_craft->weapon_slots[weapon_slot_index]
						    .ammo_count != 0) {
					if (g_cur_craft
						    ->weapon_slots
							    [weapon_slot_index]
						    .missile_defense_cooldown !=
					    0) {
						--g_cur_craft
							  ->weapon_slots
								  [weapon_slot_index]
							  .missile_defense_cooldown;
					} else {
						uint16_t *turret_target_index =
							&g_cur_craft
								 ->turret_target_states
									 [weapon_slot_index]
								 .target_obj_idx;
						*turret_target_index =
							UINT16_MAX;
						g_paifight_search_origin_x =
							g_pai_context
								.craft_position_x;
						g_paifight_search_origin_y =
							g_pai_context
								.craft_position_y;
						g_paifight_search_origin_z =
							g_pai_context
								.craft_position_z;
						pai_calcrotatedpoint(
							&g_object_table
								[g_pai_context
									 .object_index],
							g_model_defs[g_cur_craft
									     ->model_index]
								.weapon_hardpoints
									[weapon_slot_index]
								.x,
							g_model_defs[g_cur_craft
									     ->model_index]
								.weapon_hardpoints
									[weapon_slot_index]
								.z,
							g_model_defs[g_cur_craft
									     ->model_index]
								.weapon_hardpoints
									[weapon_slot_index]
								.y);
						if (g_object_table
							    [g_pai_context
								     .object_index]
								    .object_type ==
						    53) {
							g_rotated_x *= 2;
							g_rotated_y *= 2;
							g_rotated_z *= 2;
						}
						g_paifight_search_origin_x +=
							g_rotated_x;
						g_paifight_search_origin_y +=
							g_rotated_y;
						g_paifight_search_origin_z +=
							g_rotated_z;

						int16_t selected_target = -1;
						unsigned int best_range =
							0x40000;
						uint16_t candidate_index;
						for (candidate_index = (uint16_t)
							     g_projectile_object_slot_start;
						     candidate_index <
						     g_projectile_object_slot_end;
						     ++candidate_index) {
							struct object_record *projectile =
								&g_object_table
									[candidate_index];

							if (projectile
								    ->object_type ==
							    0) {
								continue;
							}
							struct warhead_guidance_state *guidance =
								&g_projectile_guidance_states
									[(uint16_t)(candidate_index -
										    g_projectile_object_slot_start)];
							if (guidance->homing_tier ==
								    0 ||
							    guidance->target_obj_idx !=
								    g_pai_context
									    .object_index) {
								continue;
							}

							int16_t incoming_count =
								0;
							for (uint16_t other_index =
								     (uint16_t)
									     g_projectile_object_slot_start;
							     other_index <
							     g_projectile_object_slot_end;
							     ++other_index) {
								if (g_object_table[other_index]
										    .object_type ==
									    0 ||
								    other_index ==
									    candidate_index) {
									continue;
								}
								struct warhead_guidance_state *other_guidance =
									&g_projectile_guidance_states
										[(uint16_t)(other_index -
											    g_projectile_object_slot_start)];
								if (other_guidance->homing_tier >=
									    5 &&
								    other_guidance->target_obj_idx ==
									    candidate_index) {
									++incoming_count;
								}
							}
							if ((uint16_t)
								    incoming_count >=
							    1) {
								continue;
							}

							int range = collide_roughdistance3d(
								projectile->world_x -
									g_paifight_search_origin_x,
								projectile->world_y -
									g_paifight_search_origin_y,
								projectile->world_z -
									g_paifight_search_origin_z);
							g_last_rough_distance =
								range;
							if (range > 0x4000) {
								if ((unsigned int)
									    range <
								    best_range) {
									selected_target =
										candidate_index;
									best_range =
										range;
								}
							}
						}

						if (selected_target != -1) {
							*turret_target_index =
								selected_target;
						} else {
							best_range = 0x40000;
							selected_target = -1;
							for (candidate_index =
								     (uint16_t)
									     g_active_region_object_slot_start;
							     candidate_index <
							     g_active_region_craft_object_slot_end;
							     ++candidate_index) {
								{
									struct object_record *candidate =
										&g_object_table
											[candidate_index];
									if (candidate
										    ->object_type ==
									    0) {
										continue;
									}
									struct craft_data *candidate_craft =
										candidate
											->mobj
											->p_craft;
									if (!((g_pai_context.object_index ==
										       candidate_craft
											       ->ai_controller
											       .target_obj_idx &&
									       (candidate_craft->ai_controller
												.maneuver_mode ==
											AI_MANEUVER_MODE_ATTACK ||
										candidate_craft->ai_controller
												.maneuver_mode ==
											AI_MANEUVER_MODE_ROCKET_ATTACK)) ||
									      candidate_craft->last_attacker_obj_idx ==
										      (uint16_t)
											      candidate_index ||
									      candidate_craft->ai_flight
											      .threat_obj_idx ==
										      candidate_index)) {
										int player_owner =
											candidate
												->player_owner_idx;
										if (player_owner ==
										    -1) {
											continue;
										}
										int candidate_team =
											g_mission_flight_groups
												[candidate
													 ->flight_group_idx]
													.fg
													.team;
										int own_team =
											g_object_table[g_pai_context
													       .object_index]
												.mobj
												->team;
										int is_enemy;
										if (own_team ==
										    candidate_team) {
											is_enemy =
												0;
										} else {
											is_enemy =
												g_mission_teams[own_team]
													.allies[candidate_team] ==
												0;
										}
										if (is_enemy !=
										    1) {
											continue;
										}
										if (g_players[player_owner]
											    .current_target_object_idx !=
										    g_pai_context
											    .object_index) {
											continue;
										}
									}
								}

								int candidate_valid =
									pai_is_object_targetable(
										candidate_index);
								if (candidate_valid ==
								    0) {
									continue;
								}

								uint16_t incoming_count =
									0;
								for (uint16_t other_index =
									     (uint16_t)
										     g_projectile_object_slot_start;
								     other_index <
								     g_projectile_object_slot_end;
								     ++other_index) {
									if (g_object_table[other_index]
											    .object_type ==
										    0 ||
									    other_index ==
										    candidate_index) {
										continue;
									}
									struct warhead_guidance_state *other_guidance =
										&g_projectile_guidance_states
											[(uint16_t)(other_index -
												    g_projectile_object_slot_start)];
									if (other_guidance->homing_tier !=
										    0 &&
									    other_guidance->target_obj_idx ==
										    candidate_index) {
										++incoming_count;
									}
								}
								if (incoming_count >=
								    2) {
									continue;
								}

								unsigned int range = collide_roughdistance3d(
									g_object_table[candidate_index]
											.world_x -
										g_paifight_search_origin_x,
									g_object_table[candidate_index]
											.world_y -
										g_paifight_search_origin_y,
									g_object_table[candidate_index]
											.world_z -
										g_paifight_search_origin_z);
								g_last_rough_distance =
									range;
								if (range <
								    best_range) {
									selected_target =
										candidate_index;
									best_range =
										range;
								}
							}
						}

						if (selected_target != -1) {
							*turret_target_index =
								selected_target;
						}
						if (*turret_target_index !=
						    UINT16_MAX) {
							uint16_t old_target =
								g_pai_context
									.controller
									->target_obj_idx;
							g_pai_context
								.controller
								->target_obj_idx =
								*turret_target_index;
							g_pai_context
								.controller
								->target_component =
								paifight_select_target_component_mesh(
									g_pai_context
										.controller
										->target_obj_idx);
							int projectile_index = laser_firemissile(
								g_pai_context
									.object_index,
								weapon_slot_index,
								g_cur_craft
									->weapon_slots
										[weapon_slot_index]
									.projectile_type_id,
								UINT16_MAX);
							if (projectile_index !=
							    -1) {
								g_projectile_guidance_states
									[projectile_index]
										.homing_tier =
									(game_rand() &
									 3) +
									3;
								g_cur_craft
									->weapon_slots
										[weapon_slot_index]
									.missile_defense_cooldown =
									20;
								XVT_LOG_DEBUG(
									"ai.defense_fired object=%d weapon_slot=%d kind=\"%s\" target=%d component=%d projectile=%d homing=%d ammo=%d predicted=%d",
									(int)g_pai_context
										.object_index,
									(int)weapon_slot_index,
									*turret_target_index <
											g_active_region_craft_object_slot_end
										? "craft"
									: g_object_table[*turret_target_index]
												.mobj
												->family ==
											1
										? "warhead"
									: g_object_table[*turret_target_index]
												.mobj
												->family ==
											5
										? "explosion"
										: "other",
									(int)*turret_target_index,
									(int)g_pai_context
										.controller
										->target_component,
									projectile_index +
										g_projectile_object_slot_start,
									(int)g_projectile_guidance_states
										[projectile_index]
											.homing_tier,
									(int)g_cur_craft
										->weapon_slots
											[weapon_slot_index]
										.ammo_count,
									g_flight_sim_side_effects_suppressed);
							}
							g_pai_context
								.controller
								->target_obj_idx =
								old_target;
						}
					}
				}
			}
			++weapon_slot_index;
		}
		++launcher_index;
	}
	return 0;
}

/* Order 6: aims each turret of the craft at whatever attacks it; returns 0 on
 * every path. Clears require_undisabled_target. Unless the flight group's status1
 * or status2 is 14, each turret slot (weapon type 2) whose retarget timer has
 * run out loses its target and gets ammo_count 0; for a craft breaking up, with
 * no subsystem working or with weapons inhibited it stops there. The search
 * origin is the turret's hardpoint (doubled for an Imperial Star Destroyer), or
 * the craft's position for the Super Star Destroyer. The turret keeps the last
 * attacker when it can be targeted, lies within AI_TARGET_RANGE_MAX (plus the
 * model's extent for the Super Star Destroyer), the line of fire is clear, it
 * is not the board2pln target nor a shieldless disableldr1pln target. Else it
 * clears last_attacker_obj_idx and takes the nearest targetable craft attacking
 * this one with a clear line of fire. A turret given a target adds 472 ticks to
 * its retarget timer; on the Super Star Destroyer a turret whose last attacker
 * two turrets already have stays idle. Last, a starfighter with countermeasure
 * rounds answers the first homing warhead aimed at it within
 * g_ai_warhead_threat_range_by_skill (three times that for concussion missiles) when
 * its countermeasure system works: chaff adds 10 to an empty
 * chaff_active_seconds, using a round unless the flight group's status1 or
 * status2 is 21; a flare fires when none chases that warhead yet. Sets the
 * collision probe globals and g_last_rough_distance. */
// FUNCTION: XVT 0x45FBF0
int16_t paifight_gunnerselfdefenseorder(void)
{
	enum {
		DISABLED_FLIGHT_GROUP_STATUS = 14,
		GUNNER_WEAPON_TYPE = 2,
		RETARGET_COOLDOWN_TICKS = 472,
		MAX_SHARED_TURRETS = 2,
		CHAFF_ACTIVE_SECONDS = 10,
		UNLIMITED_AMMO_STATUS = 21,
	};

	g_pai_context.require_undisabled_target = 0;
	uint8_t object_type =
		g_object_table[g_pai_context.object_index].object_type;
	int is_super_star_destroyer =
		object_type == CRAFT_SPECIES_SUPER_STAR_DESTROYER;
	int flight_group_array_index =
		g_object_table[g_pai_context.object_index].flight_group_idx;
	if (g_mission_flight_groups[flight_group_array_index].fg.status1 !=
		    DISABLED_FLIGHT_GROUP_STATUS &&
	    g_mission_flight_groups[flight_group_array_index].fg.status2 !=
		    DISABLED_FLIGHT_GROUP_STATUS) {
		for (uint16_t weapon_slot_index = 0;
		     weapon_slot_index < g_cur_craft->laser_slot_count;
		     ++weapon_slot_index) {
			if (g_cur_craft->weapon_slots[weapon_slot_index]
				    .projectile_type_id != GUNNER_WEAPON_TYPE) {
				continue;
			}
			struct turret_target_state *turret_state =
				&g_cur_craft->turret_target_states
					 [weapon_slot_index];
			if (turret_state->retarget_cooldown_timer > 0) {
				continue;
			}

			turret_state->target_obj_idx = UINT16_MAX;
			g_cur_craft->weapon_slots[weapon_slot_index]
				.ammo_count = 0;
			if (g_cur_craft->object_kind ==
				    CRAFT_OBJECT_KIND_BREAKING_UP ||
			    g_cur_craft->working_subsystems == 0 ||
			    g_cur_craft->weapon_fire_inhibit_timer != 0) {
				continue;
			}

			g_paifight_search_origin_x =
				g_pai_context.craft_position_x;
			g_paifight_search_origin_y =
				g_pai_context.craft_position_y;
			g_paifight_search_origin_z =
				g_pai_context.craft_position_z;
			if (!is_super_star_destroyer) {
				pai_calcrotatedpoint(
					&g_object_table[g_pai_context
								.object_index],
					g_model_defs[g_cur_craft->model_index]
						.weapon_hardpoints
							[weapon_slot_index]
						.x,
					g_model_defs[g_cur_craft->model_index]
						.weapon_hardpoints
							[weapon_slot_index]
						.z,
					g_model_defs[g_cur_craft->model_index]
						.weapon_hardpoints
							[weapon_slot_index]
						.y);
				if (g_object_table[g_pai_context.object_index]
					    .object_type ==
				    CRAFT_SPECIES_IMPERIAL_STAR_DESTROYER) {
					g_rotated_x *= 2;
					g_rotated_y *= 2;
					g_rotated_z *= 2;
				}
				g_paifight_search_origin_x += g_rotated_x;
				g_paifight_search_origin_y += g_rotated_y;
				g_paifight_search_origin_z += g_rotated_z;
				g_collision_segment_start_world_x =
					g_paifight_search_origin_x;
				g_collision_segment_start_world_y =
					g_paifight_search_origin_y;
				g_collision_segment_start_world_z =
					g_paifight_search_origin_z;
			}

			uint16_t last_attacker_obj_idx =
				g_cur_craft->last_attacker_obj_idx;
			int valid_target =
				pai_is_object_targetable(last_attacker_obj_idx);

			int search_for_target = valid_target == 0;
			if (valid_target != 0) {
				g_last_rough_distance = collide_roughdistance3d(
					g_object_table[last_attacker_obj_idx]
							.world_x -
						g_paifight_search_origin_x,
					g_object_table[last_attacker_obj_idx]
							.world_y -
						g_paifight_search_origin_y,
					g_object_table[last_attacker_obj_idx]
							.world_z -
						g_paifight_search_origin_z);
				unsigned int max_range_score =
					AI_TARGET_RANGE_MAX;
				if (is_super_star_destroyer) {
					max_range_score +=
						g_object_type_table
							[g_object_table
								 [g_pai_context
									  .object_index]
									 .object_type]
								.max_bounds_extent;
				}
				if ((unsigned int)g_last_rough_distance >=
				    max_range_score) {
					search_for_target = 1;
				} else {
					int clear_sweep = 1;
					if (!is_super_star_destroyer) {
						mission_resolve_object_or_mission_point_world_loc(
							last_attacker_obj_idx,
							0);
						g_collision_probe_world_x =
							g_world_loc_x;
						g_collision_probe_world_y =
							g_world_loc_y;
						g_collision_probe_world_z =
							g_world_loc_z;
						clear_sweep =
							collide_check_swept_model_collision(
								g_pai_context
									.object_index,
								g_pai_context
									.object_index) ==
							0;
					}
					if (!clear_sweep ||
					    (strcmp(g_plan_table
							    [g_pai_context
								     .controller
								     ->current_plan_id]
								    .name,
						    "board2pln") == 0 &&
					     g_pai_context.controller
							     ->target_obj_idx ==
						     last_attacker_obj_idx)) {
						search_for_target = 1;
					} else if (
						strcmp(g_plan_table
							       [g_pai_context
									.controller
									->current_plan_id]
								       .name,
						       "disableldr1pln") == 0 &&
						last_attacker_obj_idx ==
							g_pai_context
								.controller
								->target_obj_idx &&
						g_object_table[g_pai_context
								       .controller
								       ->target_obj_idx]
								.mobj->p_craft
								->shield_energy
									[0] ==
							0) {
						search_for_target = 1;
					} else {
						if (is_super_star_destroyer) {
							int shared_turret_count =
								0;
							for (int turret_index =
								     0;
							     turret_index <
								     g_cur_craft
									     ->laser_slot_count &&
							     shared_turret_count <
								     MAX_SHARED_TURRETS;
							     ++turret_index) {
								if (g_cur_craft
									    ->turret_target_states
										    [turret_index]
									    .target_obj_idx ==
								    last_attacker_obj_idx) {
									++shared_turret_count;
								}
							}
							if (shared_turret_count >=
							    MAX_SHARED_TURRETS) {
								continue;
							}
						}
						turret_state->target_obj_idx =
							last_attacker_obj_idx;
						turret_state
							->retarget_cooldown_timer +=
							RETARGET_COOLDOWN_TICKS;
						continue;
					}
				}
			}

			if (search_for_target) {
				g_cur_craft->last_attacker_obj_idx = UINT16_MAX;
				unsigned int best_range_score =
					AI_TARGET_RANGE_MAX;
				int16_t best_target_obj_idx = -1;
				if (is_super_star_destroyer) {
					best_range_score +=
						g_object_type_table
							[g_object_table
								 [g_pai_context
									  .object_index]
									 .object_type]
								.max_bounds_extent;
				}

				for (uint16_t candidate_obj_idx = (uint16_t)
					     g_active_region_object_slot_start;
				     candidate_obj_idx <
				     g_active_region_craft_object_slot_end;
				     ++candidate_obj_idx) {
					int candidate_array_index =
						candidate_obj_idx;
					if (g_object_table
						    [candidate_array_index]
							    .object_type == 0) {
						continue;
					}
					struct craft_data *candidate_craft =
						g_object_table
							[candidate_array_index]
								.mobj->p_craft;
					if (candidate_craft->ai_controller
							    .target_obj_idx !=
						    g_pai_context
							    .object_index ||
					    (candidate_craft->ai_controller
							     .maneuver_mode !=
						     AI_MANEUVER_MODE_ATTACK &&
					     candidate_craft->ai_controller
							     .maneuver_mode !=
						     AI_MANEUVER_MODE_ROCKET_ATTACK)) {
						continue;
					}

					int candidate_valid = 0;
					struct object_record *candidate_object =
						&g_object_table
							[candidate_array_index];
					candidate_valid =
						pai_is_object_targetable(
							candidate_obj_idx);
					if (candidate_valid == 0) {
						continue;
					}

					g_last_rough_distance = collide_roughdistance3d(
						candidate_object->world_x -
							g_paifight_search_origin_x,
						candidate_object->world_y -
							g_paifight_search_origin_y,
						candidate_object->world_z -
							g_paifight_search_origin_z);
					if (is_super_star_destroyer) {
						for (int turret_index = 0;
						     turret_index <
						     g_cur_craft
							     ->laser_slot_count;
						     ++turret_index) {
							if (g_cur_craft
								    ->turret_target_states
									    [turret_index]
								    .target_obj_idx ==
							    candidate_obj_idx) {
								g_last_rough_distance +=
									AI_TURRET_TARGET_PENALTY;
							}
						}
					}
					if ((unsigned int)
						    g_last_rough_distance >=
					    best_range_score) {
						continue;
					}

					int clear_sweep = 1;
					if (!is_super_star_destroyer) {
						mission_resolve_object_or_mission_point_world_loc(
							candidate_obj_idx, 0);
						g_collision_probe_world_x =
							g_world_loc_x;
						g_collision_probe_world_y =
							g_world_loc_y;
						g_collision_probe_world_z =
							g_world_loc_z;
						clear_sweep =
							collide_check_swept_model_collision(
								g_pai_context
									.object_index,
								g_pai_context
									.object_index) ==
							0;
					}
					if (clear_sweep) {
						best_target_obj_idx = (int16_t)
							candidate_obj_idx;
						best_range_score = (unsigned int)
							g_last_rough_distance;
					}
				}

				if (best_target_obj_idx != -1) {
					turret_state->target_obj_idx =
						best_target_obj_idx;
					if (turret_state
						    ->retarget_cooldown_timer <=
					    0) {
						turret_state
							->retarget_cooldown_timer +=
							RETARGET_COOLDOWN_TICKS;
					}
					XVT_LOG_DEBUG(
						"ai.turret_targeted object=%d turret=%d target=%d reason=\"nearest_attacker\" timer=%d ion_fire=%d predicted=%d",
						(int)g_pai_context.object_index,
						(int)weapon_slot_index,
						(int)turret_state
							->target_obj_idx,
						(int)turret_state
							->retarget_cooldown_timer,
						(int)g_cur_craft
							->weapon_slots
								[weapon_slot_index]
							.ammo_count,
						g_flight_sim_side_effects_suppressed);
				}
			}
		}
	}

	if (g_object_table[g_pai_context.object_index].genus_id ==
		    CRAFT_GENUS_STARFIGHTER &&
	    g_cur_craft->cm_type_id != COUNTERMEASURE_TYPE_NONE &&
	    g_cur_craft->cm_ammo_count != 0) {
		int threat_range =
			g_ai_warhead_threat_range_by_skill[g_pai_context
								   .skill_tier];
		for (uint16_t threat_projectile_obj_idx =
			     (uint16_t)g_projectile_object_slot_start;
		     threat_projectile_obj_idx < g_projectile_object_slot_end;
		     ++threat_projectile_obj_idx) {
			struct object_record *projectile_object =
				&g_object_table[threat_projectile_obj_idx];
			if (projectile_object->object_type == 0) {
				continue;
			}
			struct warhead_guidance_state *guidance =
				projectile_object->mobj->p_warhead_guidance;
			if (guidance->homing_tier == 0 ||
			    guidance->target_obj_idx !=
				    g_pai_context.object_index) {
				continue;
			}
			int max_range_score;
			if (projectile_object->object_type ==
				    WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE ||
			    projectile_object->object_type ==
				    WARHEAD_OBJECT_TYPE_ADVANCED_CONCUSSION_MISSILE) {
				max_range_score = 3 * threat_range;
			} else {
				max_range_score = threat_range;
			}
			if (pai_is_object_within_range_of_craft(
				    threat_projectile_obj_idx,
				    max_range_score) == 1) {
				if ((g_cur_craft->working_subsystems &
				     CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES) !=
				    0) {
					if (g_cur_craft->cm_type_id ==
					    COUNTERMEASURE_TYPE_CHAFF) {
						if (g_cur_craft
							    ->chaff_active_seconds ==
						    0) {
							g_cur_craft
								->chaff_active_seconds +=
								CHAFF_ACTIVE_SECONDS;
							if (g_mission_flight_groups[g_pai_context
											    .craft_flight_group_index]
									    .fg
									    .status1 !=
								    UNLIMITED_AMMO_STATUS &&
							    g_mission_flight_groups[g_pai_context
											    .craft_flight_group_index]
									    .fg
									    .status2 !=
								    UNLIMITED_AMMO_STATUS) {
								--g_cur_craft
									  ->cm_ammo_count;
							}
							XVT_LOG_DEBUG(
								"ai.chaff_dropped object=%d threat=%d seconds=%d ammo=%d predicted=%d",
								(int)g_pai_context
									.object_index,
								(int)threat_projectile_obj_idx,
								(int)g_cur_craft
									->chaff_active_seconds,
								(int)g_cur_craft
									->cm_ammo_count,
								g_flight_sim_side_effects_suppressed);
						}
					} else if (g_cur_craft->cm_type_id ==
						   COUNTERMEASURE_TYPE_FLARE) {
						int flare_already_targeting_threat =
							0;
						for (uint16_t flare_obj_idx = (uint16_t)
							     g_projectile_object_slot_start;
						     flare_obj_idx <
						     g_projectile_object_slot_end;
						     ++flare_obj_idx) {
							struct object_record *flare_object =
								&g_object_table
									[flare_obj_idx];
							if (flare_object
								    ->object_type !=
							    COUNTERMEASURE_PROJECTILE_OBJECT_TYPE) {
								continue;
							}
							struct warhead_guidance_state
								*flare_guidance =
									flare_object
										->mobj
										->p_warhead_guidance;
							if (flare_guidance->homing_tier !=
								    0 &&
							    flare_guidance->target_obj_idx ==
								    threat_projectile_obj_idx) {
								flare_already_targeting_threat =
									1;
								break;
							}
						}
						if (!flare_already_targeting_threat &&
						    g_cur_craft->cm_fire_cooldown_timer ==
							    0) {
							laser_createcountermeasureprojectile(
								g_pai_context
									.object_index,
								COUNTERMEASURE_PROJECTILE_OBJECT_TYPE);
						}
					}
				}
				return 0;
			}
		}
	}
	return 0;
}

/* Order 7: aims the craft's idle turrets at its order's targets; returns 0 on
 * every path. Nothing happens for a craft breaking up, with no subsystem
 * working, of a flight group with status1 or status2 14, or whose model has no
 * gunner mount. It sets target_search_flags 0x30 and require_undisabled_target, 1
 * on starshipdisablepln or disableldr1pln. Each turret slot (weapon type 2)
 * with no target and its retarget timer run out gets
 * SIMULATION_TICKS_PER_SECOND more on the timer and ammo_count 0; the two
 * candidate sets are built once for the order's two pairs of target conditions.
 * The search origin is the turret's hardpoint (doubled for an Imperial Star
 * Destroyer), or the craft's position for the Super Star Destroyer, also kept
 * in g_pai_context. On starshipprotectpln and starshipescortpln the turret takes
 * the nearest attacker of an order target; else the nearest target in set 0,
 * else set 1, with ammo_count 1 on the disable plans. A target found adds
 * SIMULATION_TICKS_PER_SECOND more to the timer. */
// FUNCTION: XVT 0x4606E0
int16_t paifight_gunneroffenseorder(void)
{
	enum {
		DISABLED_FLIGHT_GROUP_STATUS = 14,
		GUNNER_WEAPON_TYPE = 2,
		GUNNER_MOUNT_TYPE = 2,
		TARGET_SEARCH_FLAGS = 0x30,
	};

	int candidate_sets_need_build = 1;
	if (g_cur_craft->object_kind == CRAFT_OBJECT_KIND_BREAKING_UP) {
		return 0;
	}
	if (g_cur_craft->working_subsystems == 0) {
		return 0;
	}

	if (g_mission_flight_groups[g_object_table[g_pai_context.object_index]
					    .flight_group_idx]
			    .fg.status1 == DISABLED_FLIGHT_GROUP_STATUS ||
	    g_mission_flight_groups[g_object_table[g_pai_context.object_index]
					    .flight_group_idx]
			    .fg.status2 == DISABLED_FLIGHT_GROUP_STATUS) {
		return 0;
	}

	uint16_t mount_index = 0;
	int16_t has_gunner_mount = 0;
	for (; mount_index < 2; ++mount_index) {
		if (g_model_defs[g_cur_craft->model_index]
			    .laser_group_mount_type[mount_index] ==
		    GUNNER_MOUNT_TYPE) {
			has_gunner_mount = 1;
		}
	}
	if (!has_gunner_mount) {
		return 0;
	}

	g_pai_context.require_undisabled_target = 0;
	g_pai_context.target_search_flags = TARGET_SEARCH_FLAGS;
	if (strcmp(g_plan_table[g_pai_context.controller->current_plan_id].name,
		   "starshipprotectpln") != 0 &&
	    strcmp(g_plan_table[g_pai_context.controller->current_plan_id].name,
		   "starshipescortpln") != 0) {
		g_pai_context.require_undisabled_target =
			strcmp(g_plan_table[g_pai_context.controller
						    ->current_plan_id]
				       .name,
			       "starshipdisablepln") == 0 ||
			strcmp(g_plan_table[g_pai_context.controller
						    ->current_plan_id]
				       .name,
			       "disableldr1pln") == 0;
	}

	int expanded_probe =
		g_object_table[g_pai_context.object_index].object_type ==
		CRAFT_SPECIES_SUPER_STAR_DESTROYER;
	for (uint16_t weapon_slot_index = 0;
	     weapon_slot_index < g_cur_craft->laser_slot_count;
	     ++weapon_slot_index) {
		struct turret_target_state *turret_state =
			&g_cur_craft->turret_target_states[weapon_slot_index];

		if (g_cur_craft->weapon_slots[weapon_slot_index]
				    .projectile_type_id != GUNNER_WEAPON_TYPE ||
		    turret_state->target_obj_idx != UINT16_MAX ||
		    turret_state->retarget_cooldown_timer > 0) {
			continue;
		}

		turret_state->retarget_cooldown_timer +=
			SIMULATION_TICKS_PER_SECOND;
		if (candidate_sets_need_build == 1) {
			paifight_build_gunner_target_candidate_set(
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.target1_type,
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.target1,
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.target1_or_target2,
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.target2_type,
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.target2,
				0);
			paifight_build_gunner_target_candidate_set(
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.secondary_target_types[0],
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.secondary_targets[0],
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.target3_or_target4,
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.secondary_target_types[1],
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.secondary_targets[1],
				1);
			candidate_sets_need_build = 0;
		}

		g_paifight_search_origin_x = g_pai_context.craft_position_x;
		g_paifight_search_origin_y = g_pai_context.craft_position_y;
		g_paifight_search_origin_z = g_pai_context.craft_position_z;
		if (expanded_probe) {
			g_pai_context.target_search_origin_x =
				g_pai_context.craft_position_x;
			g_pai_context.target_search_origin_y =
				g_pai_context.craft_position_y;
			g_pai_context.target_search_origin_z =
				g_pai_context.craft_position_z;
		} else {
			pai_calcrotatedpoint(
				&g_object_table[g_pai_context.object_index],
				g_model_defs[g_cur_craft->model_index]
					.weapon_hardpoints[weapon_slot_index]
					.x,
				g_model_defs[g_cur_craft->model_index]
					.weapon_hardpoints[weapon_slot_index]
					.z,
				g_model_defs[g_cur_craft->model_index]
					.weapon_hardpoints[weapon_slot_index]
					.y);
			if (g_object_table[g_pai_context.object_index]
				    .object_type ==
			    CRAFT_SPECIES_IMPERIAL_STAR_DESTROYER) {
				g_rotated_x *= 2;
				g_rotated_y *= 2;
				g_rotated_z *= 2;
			}
			g_paifight_search_origin_x += g_rotated_x;
			g_pai_context.target_search_origin_x =
				g_paifight_search_origin_x;
			g_paifight_search_origin_y += g_rotated_y;
			g_pai_context.target_search_origin_y =
				g_paifight_search_origin_y;
			g_paifight_search_origin_z += g_rotated_z;
			g_pai_context.target_search_origin_z =
				g_paifight_search_origin_z;
			g_collision_segment_start_world_x =
				g_paifight_search_origin_x;
			g_collision_segment_start_world_y =
				g_paifight_search_origin_y;
			g_collision_segment_start_world_z =
				g_paifight_search_origin_z;
		}

		g_cur_craft->weapon_slots[weapon_slot_index].ammo_count = 0;
		int16_t target_object_index;
		if (strcmp(g_plan_table[g_pai_context.controller
						->current_plan_id]
				   .name,
			   "starshipprotectpln") == 0 ||
		    strcmp(g_plan_table[g_pai_context.controller
						->current_plan_id]
				   .name,
			   "starshipescortpln") == 0) {
			target_object_index =
				paifight_find_attacker_of_order_target_from_order(
					g_pai_context.order_slot);
			turret_state->target_obj_idx = target_object_index;
			if (target_object_index != -1) {
				turret_state->retarget_cooldown_timer +=
					SIMULATION_TICKS_PER_SECOND;
				XVT_LOG_DEBUG(
					"ai.turret_targeted object=%d turret=%d target=%d reason=\"protect\" timer=%d ion_fire=%d predicted=%d",
					(int)g_pai_context.object_index,
					(int)weapon_slot_index,
					(int)turret_state->target_obj_idx,
					(int)turret_state
						->retarget_cooldown_timer,
					(int)g_cur_craft
						->weapon_slots
							[weapon_slot_index]
						.ammo_count,
					g_flight_sim_side_effects_suppressed);
			}
			continue;
		}

		target_object_index =
			paifight_find_nearest_gunner_target_in_candidate_set(
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.target1_type,
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.target1,
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.target1_or_target2,
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.target2_type,
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.target2,
				0);
		if (target_object_index != -1) {
			turret_state->target_obj_idx = target_object_index;
			turret_state->retarget_cooldown_timer +=
				SIMULATION_TICKS_PER_SECOND;
			if (strcmp(g_plan_table[g_pai_context.controller
							->current_plan_id]
					   .name,
				   "starshipdisablepln") == 0 ||
			    strcmp(g_plan_table[g_pai_context.controller
							->current_plan_id]
					   .name,
				   "disableldr1pln") == 0) {
				g_cur_craft->weapon_slots[weapon_slot_index]
					.ammo_count = 1;
			}
			XVT_LOG_DEBUG(
				"ai.turret_targeted object=%d turret=%d target=%d reason=\"order_first\" timer=%d ion_fire=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)weapon_slot_index,
				(int)turret_state->target_obj_idx,
				(int)turret_state->retarget_cooldown_timer,
				(int)g_cur_craft
					->weapon_slots[weapon_slot_index]
					.ammo_count,
				g_flight_sim_side_effects_suppressed);
		} else {
			target_object_index = paifight_find_nearest_gunner_target_in_candidate_set(
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.secondary_target_types[0],
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.secondary_targets[0],
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.target3_or_target4,
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.secondary_target_types[1],
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.secondary_targets[1],
				1);
			if (target_object_index != -1) {
				turret_state->target_obj_idx =
					target_object_index;
				turret_state->retarget_cooldown_timer +=
					SIMULATION_TICKS_PER_SECOND;
				if (strcmp(g_plan_table
						   [g_pai_context.controller
							    ->current_plan_id]
							   .name,
					   "starshipdisablepln") == 0 ||
				    strcmp(g_plan_table
						   [g_pai_context.controller
							    ->current_plan_id]
							   .name,
					   "disableldr1pln") == 0) {
					g_cur_craft
						->weapon_slots
							[weapon_slot_index]
						.ammo_count = 1;
				}
				XVT_LOG_DEBUG(
					"ai.turret_targeted object=%d turret=%d target=%d reason=\"order_second\" timer=%d ion_fire=%d predicted=%d",
					(int)g_pai_context.object_index,
					(int)weapon_slot_index,
					(int)turret_state->target_obj_idx,
					(int)turret_state
						->retarget_cooldown_timer,
					(int)g_cur_craft
						->weapon_slots
							[weapon_slot_index]
						.ammo_count,
					g_flight_sim_side_effects_suppressed);
			}
		}
	}
	return 0;
}

/* Returns the nearest turret target from g_paifight_search_origin_x, Y and Z, or
 * -1: a craft marked in candidate set candidate_set_idx, or a targetable static
 * object with behavior flag 2 that matches the target conditions, closer than
 * AI_TARGET_RANGE_MAX (plus the model's extent for the Super Star Destroyer,
 * which also adds AI_TURRET_TARGET_PENALTY for each turret already on it), with
 * a clear line of fire except on the Super Star Destroyer. In a version 14
 * mission it then gives up the pick when a craft in the craft slots that is not
 * an enemy blocks the line of fire: any such craft when the turret's craft is
 * stopped, else one that is not a starfighter, transport or utility vehicle.
 * Sets the collision probe globals, g_last_rough_distance and
 * g_gunner_collision_probe_count. */
// FUNCTION: XVT 0x460DD0
int16_t paifight_find_nearest_gunner_target_in_candidate_set(
	int16_t target1_type, uint16_t target1, int16_t target1_or_target2,
	int16_t target2_type, uint16_t target2, int candidate_set_idx)
{
	enum {
		TARGETABLE_STATIC_MODEL_FLAG = 2,
		MISSION_VERSION_WITH_GUNNER_OBSTRUCTION_CHECK = 14,
	};

	uint16_t best_object_index = UINT16_MAX;
	int expanded_probe =
		g_object_table[g_pai_context.object_index].object_type ==
		CRAFT_SPECIES_SUPER_STAR_DESTROYER;
	unsigned int best_range_score = AI_TARGET_RANGE_MAX;
	if (expanded_probe) {
		best_range_score +=
			g_object_type_table
				[g_object_table[g_pai_context.object_index]
					 .object_type]
					.max_bounds_extent;
	}

	for (uint16_t object_index =
		     (uint16_t)g_active_region_object_slot_start;
	     object_index < g_active_region_craft_object_slot_end;
	     ++object_index) {
		if (g_paifight_gunner_target_candidate_set[2 * object_index +
							   candidate_set_idx] ==
		    0) {
			continue;
		}

		int object_array_index = object_index;
		g_last_rough_distance = collide_roughdistance3d(
			g_object_table[object_array_index].world_x -
				g_paifight_search_origin_x,
			g_object_table[object_array_index].world_y -
				g_paifight_search_origin_y,
			g_object_table[object_array_index].world_z -
				g_paifight_search_origin_z);
		if (expanded_probe) {
			for (int turret_index = 0;
			     turret_index < g_cur_craft->laser_slot_count;
			     ++turret_index) {
				if (g_cur_craft
					    ->turret_target_states[turret_index]
					    .target_obj_idx == object_index) {
					g_last_rough_distance +=
						AI_TURRET_TARGET_PENALTY;
				}
			}
		}
		if ((unsigned int)g_last_rough_distance >= best_range_score) {
			continue;
		}

		int clear_sweep;
		if (expanded_probe) {
			clear_sweep = 1;
		} else {
			g_collision_probe_world_x =
				g_object_table[object_array_index].world_x;
			g_collision_probe_world_y =
				g_object_table[object_array_index].world_y;
			g_collision_probe_world_z =
				g_object_table[object_array_index].world_z;
			++g_gunner_collision_probe_count;
			clear_sweep = collide_check_swept_model_collision(
					      g_pai_context.object_index,
					      g_pai_context.object_index) == 0;
		}
		if (clear_sweep) {
			best_object_index = object_index;
			best_range_score = (unsigned int)g_last_rough_distance;
		}
	}

	{
		for (uint16_t static_object_index =
			     (uint16_t)g_region_main_object_slot_end;
		     static_object_index <
		     g_region_main_object_slot_end +
			     g_region_static_object_slot_count;
		     ++static_object_index) {
			int object_array_index = static_object_index;
			if (g_object_table[object_array_index].object_type ==
				    0 ||
			    (g_object_type_table
				     [g_object_table[object_array_index]
					      .object_type]
					     .behavior_flags &
			     TARGETABLE_STATIC_MODEL_FLAG) == 0) {
				continue;
			}

			int16_t matches_target1 =
				mission_object_matches_trigger_variable(
					static_object_index, target1_type,
					target1);
			int16_t matches_target2 =
				mission_object_matches_trigger_variable(
					static_object_index, target2_type,
					target2);
			if (target1_or_target2 == 1) {
				matches_target1 |= matches_target2;
			} else {
				matches_target1 &= matches_target2;
			}
			if (matches_target1 == 0) {
				continue;
			}

			int valid_target =
				pai_is_object_targetable(static_object_index);
			if (valid_target == 0) {
				continue;
			}

			mission_resolve_object_or_mission_point_world_loc(
				static_object_index, 0);
			g_last_rough_distance = collide_roughdistance3d(
				g_world_loc_x - g_paifight_search_origin_x,
				g_world_loc_y - g_paifight_search_origin_y,
				g_world_loc_z - g_paifight_search_origin_z);
			if (expanded_probe) {
				for (int turret_index = 0;
				     turret_index <
				     g_cur_craft->laser_slot_count;
				     ++turret_index) {
					if (g_cur_craft
						    ->turret_target_states
							    [turret_index]
						    .target_obj_idx ==
					    static_object_index) {
						g_last_rough_distance +=
							AI_TURRET_TARGET_PENALTY;
					}
				}
			}
			if ((unsigned int)g_last_rough_distance >=
			    best_range_score) {
				continue;
			}

			int clear_sweep;
			if (expanded_probe) {
				clear_sweep = 1;
			} else {
				g_collision_probe_world_x = g_world_loc_x;
				g_collision_probe_world_y = g_world_loc_y;
				g_collision_probe_world_z = g_world_loc_z;
				++g_gunner_collision_probe_count;
				clear_sweep =
					collide_check_swept_model_collision(
						g_pai_context.object_index,
						g_pai_context.object_index) ==
					0;
			}
			if (clear_sweep) {
				best_object_index = static_object_index;
				best_range_score =
					(unsigned int)g_last_rough_distance;
			}
		}
	}

	if (!expanded_probe && best_range_score > AI_TARGET_RANGE_MAX) {
		best_object_index = UINT16_MAX;
	}

	if (best_object_index != UINT16_MAX &&
	    g_mission_file_version ==
		    MISSION_VERSION_WITH_GUNNER_OBSTRUCTION_CHECK) {
		struct craft_data *saved_craft = g_cur_craft;
		for (uint16_t blocker_object_index =
			     (uint16_t)g_active_region_object_slot_start;
		     blocker_object_index <
		     g_active_region_craft_object_slot_end;
		     ++blocker_object_index) {
			int blocker_array_index = blocker_object_index;
			struct object_record *blocker =
				&g_object_table[blocker_array_index];
			int blocker_team = g_mission_flight_groups
						   [blocker->flight_group_idx]
							   .fg.team;
			struct mobile_object *source_mobile =
				g_object_table[g_pai_context.object_index].mobj;
			int source_team = source_mobile->team;
			int enemy = source_team != blocker_team &&
				    g_mission_teams[source_team]
						    .allies[blocker_team] < 1;
			if (!enemy &&
			    (source_mobile->speed == 0 ||
			     (blocker->genus_id != CRAFT_GENUS_STARFIGHTER &&
			      blocker->genus_id != CRAFT_GENUS_TRANSPORT &&
			      blocker->genus_id !=
				      CRAFT_GENUS_UTILITY_VEHICLE))) {
				g_last_rough_distance = collide_roughdistance3d(
					blocker->world_x -
						g_paifight_search_origin_x,
					blocker->world_y -
						g_paifight_search_origin_y,
					blocker->world_z -
						g_paifight_search_origin_z);
				g_last_rough_distance -=
					g_object_type_table
						[blocker->object_type]
							.max_bounds_extent;
				if ((unsigned int)g_last_rough_distance <=
				    best_range_score) {
					struct object_record *best_object =
						&g_object_table
							[best_object_index];

					g_collision_probe_world_x =
						best_object->world_x;
					g_collision_probe_world_y =
						best_object->world_y;
					g_collision_probe_world_z =
						best_object->world_z;
					if (collide_check_swept_model_collision(
						    blocker_object_index,
						    blocker_object_index) !=
					    0) {
						XVT_LOG_DEBUG(
							"ai.turret_blocked object=%d target=%d blocker=%d range=%u predicted=%d",
							(int)g_pai_context
								.object_index,
							(int)best_object_index,
							(int)blocker_object_index,
							best_range_score,
							g_flight_sim_side_effects_suppressed);
						best_object_index = UINT16_MAX;
						break;
					}
				}
			}
		}
		g_cur_craft = saved_craft;
	}

	return (int16_t)best_object_index;
}

/* Fills candidate set candidate_set_idx of g_paifight_gunner_target_candidate_set:
 * marks each live craft in the active region's craft slots that matches the
 * target conditions, is targetable and, with require_undisabled_target set, in
 * working order; clears the mark of every other slot there. */
// FUNCTION: XVT 0x461460
void paifight_build_gunner_target_candidate_set(
	int16_t target1_type, uint16_t target1, int16_t target1_or_target2,
	int16_t target2_type, uint16_t target2, uint16_t candidate_set_idx)
{
	for (uint16_t object_index =
		     (uint16_t)g_active_region_object_slot_start;
	     object_index < g_active_region_craft_object_slot_end;
	     ++object_index) {
		int candidate_set_offset = candidate_set_idx;
		g_paifight_gunner_target_candidate_set[2 * object_index +
						       candidate_set_offset] =
			0;
		struct object_record *object = &g_object_table[object_index];
		if (object->object_type == 0) {
			continue;
		}
		int16_t matches_first = mission_object_matches_trigger_variable(
			object_index, target1_type, target1);
		int16_t matches_second =
			mission_object_matches_trigger_variable(
				object_index, target2_type, target2);
		if (target1_or_target2 == 1) {
			matches_first |= matches_second;
		} else {
			matches_first &= matches_second;
		}
		if (matches_first == 0) {
			continue;
		}
		struct craft_data *craft =
			g_object_table[object_index].mobj->p_craft;
		if (g_pai_context.require_undisabled_target != 0 &&
		    craft->working_subsystems == 0) {
			continue;
		}

		if (pai_is_object_targetable(object_index)) {
			g_paifight_gunner_target_candidate_set
				[2 * object_index + candidate_set_offset] = 1;
		}
	}
}

/* Returns the nearest targetable object that matches the target conditions,
 * measured from g_paifight_search_origin_x, Y and Z, or -1 when none lies within
 * AI_TARGET_RANGE_MAX. A craft must be in working order when
 * require_undisabled_target is set; a static object needs behavior flag 2. With
 * require_clear_sweep nonzero the line of fire must be clear. Sets
 * g_last_rough_distance and the collision probe globals. Only
 * laser_update_mine_weapon_fire calls it. */
// FUNCTION: XVT 0x461690
int16_t paifight_find_nearest_matching_target_from_origin(
	int16_t target1_type, uint16_t target1, int16_t target1_or_target2,
	int16_t target2_type, uint16_t target2, int16_t require_clear_sweep)
{
	enum {
		TARGETABLE_STATIC_MODEL_FLAG = 2,
	};

	uint16_t best_object_index = UINT16_MAX;
	unsigned int best_range_score = UINT_MAX;
	uint16_t object_index;
	int object_array_index;
	int16_t matches_target1;
	int16_t matches_target2;
	int valid_target;
	unsigned int range_score;
	for (object_index = (uint16_t)g_active_region_object_slot_start;
	     object_index < g_active_region_craft_object_slot_end;
	     ++object_index) {
		object_array_index = object_index;
		if (g_object_table[object_array_index].object_type == 0) {
			continue;
		}

		matches_target1 = mission_object_matches_trigger_variable(
			object_index, target1_type, target1);
		matches_target2 = mission_object_matches_trigger_variable(
			object_index, target2_type, target2);
		if (target1_or_target2 == 1) {
			matches_target1 |= matches_target2;
		} else {
			matches_target1 &= matches_target2;
		}
		if (matches_target1 == 0) {
			continue;
		}
		if (g_pai_context.require_undisabled_target != 0 &&
		    g_object_table[object_array_index]
				    .mobj->p_craft->working_subsystems == 0) {
			continue;
		}

		valid_target = pai_is_object_targetable(object_index);
		if (valid_target == 0) {
			if (g_object_table[object_array_index].mobj != NULL &&
			    g_object_table[object_array_index].mobj->p_craft !=
				    NULL &&
			    g_object_table[object_array_index]
					    .mobj->p_craft->beam_type_id ==
				    BEAM_TYPE_DECOY &&
			    g_object_table[object_array_index]
					    .mobj->p_craft->beam_active != 0) {
				XVT_LOG_DEBUG(
					"ai.mine_decoy_refused target=%d context=%d range=%u predicted=%d",
					(int)object_index,
					(int)g_pai_context.object_index,
					(unsigned)g_last_rough_distance,
					g_flight_sim_side_effects_suppressed);
			}
			continue;
		}

		range_score = (unsigned int)collide_roughdistance3d(
			g_object_table[object_array_index].world_x -
				g_paifight_search_origin_x,
			g_object_table[object_array_index].world_y -
				g_paifight_search_origin_y,
			g_object_table[object_array_index].world_z -
				g_paifight_search_origin_z);
		g_last_rough_distance = (int)range_score;
		if (range_score >= best_range_score) {
			continue;
		}
		if (require_clear_sweep != 0) {
			mission_resolve_object_or_mission_point_world_loc(
				object_index, 0);
			g_collision_probe_world_x = g_world_loc_x;
			g_collision_probe_world_y = g_world_loc_y;
			g_collision_probe_world_z = g_world_loc_z;
			if (collide_check_swept_model_collision(
				    g_pai_context.object_index,
				    g_pai_context.object_index) != 0) {
				continue;
			}
			range_score = (unsigned int)g_last_rough_distance;
		}
		best_object_index = object_index;
		best_range_score = range_score;
	}

	for (object_index = (uint16_t)g_region_main_object_slot_end;
	     object_index <
	     g_region_main_object_slot_end + g_region_static_object_slot_count;
	     ++object_index) {
		object_array_index = object_index;
		if (g_object_table[object_array_index].object_type == 0 ||
		    (g_object_type_table[g_object_table[object_array_index]
						 .object_type]
			     .behavior_flags &
		     TARGETABLE_STATIC_MODEL_FLAG) == 0) {
			continue;
		}

		matches_target1 = mission_object_matches_trigger_variable(
			object_index, target1_type, target1);
		matches_target2 = mission_object_matches_trigger_variable(
			object_index, target2_type, target2);
		if (target1_or_target2 == 1) {
			matches_target1 |= matches_target2;
		} else {
			matches_target1 &= matches_target2;
		}
		if (matches_target1 == 0) {
			continue;
		}

		valid_target = pai_is_object_targetable(object_index);
		if (valid_target == 0) {
			continue;
		}

		mission_resolve_object_or_mission_point_world_loc(object_index,
								  0);
		range_score = (unsigned int)collide_roughdistance3d(
			g_world_loc_x - g_paifight_search_origin_x,
			g_world_loc_y - g_paifight_search_origin_y,
			g_world_loc_z - g_paifight_search_origin_z);
		g_last_rough_distance = (int)range_score;
		if (range_score >= best_range_score) {
			continue;
		}
		if (require_clear_sweep != 0) {
			g_collision_probe_world_x = g_world_loc_x;
			g_collision_probe_world_y = g_world_loc_y;
			g_collision_probe_world_z = g_world_loc_z;
			if (collide_check_swept_model_collision(
				    g_pai_context.object_index,
				    g_pai_context.object_index) != 0) {
				continue;
			}
			range_score = (unsigned int)g_last_rough_distance;
		}
		best_object_index = object_index;
		best_range_score = range_score;
	}

	if (best_range_score > AI_TARGET_RANGE_MAX) {
		best_object_index = UINT16_MAX;
	}
	if (best_object_index != UINT16_MAX) {
		XVT_LOG_DEBUG(
			"ai.mine_target_found target=%d range=%u context=%d undisabled=%d x=%d y=%d z=%d predicted=%d",
			(int)best_object_index, best_range_score,
			(int)g_pai_context.object_index,
			(int)g_pai_context.require_undisabled_target,
			g_paifight_search_origin_x, g_paifight_search_origin_y,
			g_paifight_search_origin_z,
			g_flight_sim_side_effects_suppressed);
	}
	return (int16_t)best_object_index;
}

/* Order 13: returns 1 after making the leader's last attacker the craft's
 * target, with its signature and has_live_target 0, else 0. That happens when the
 * attacker is a live starfighter or transport, the leader is active, no other
 * craft of the flight group targets it yet, and, for a flight group a player
 * owns, the player has not told the craft to avoid it. Returns 0 for a craft
 * with no leader. */
// FUNCTION: XVT 0x461C00
int16_t paifight_coverleaderorder(void)
{
	if (g_pai_context.leader_object_index == UINT8_MAX) {
		return 0;
	}

	struct craft_data *leader_craft =
		g_object_table[g_pai_context.leader_object_index].mobj->p_craft;
	if (leader_craft == NULL) {
		return 0;
	}

	uint16_t last_attacker_obj_idx = leader_craft->last_attacker_obj_idx;
	if (last_attacker_obj_idx != UINT16_MAX &&
	    g_object_table[last_attacker_obj_idx].object_type != 0 &&
	    leader_craft->object_kind == CRAFT_OBJECT_KIND_ACTIVE) {
		if ((g_mission_flight_groups
				     [g_object_table[g_pai_context.object_index]
					      .flight_group_idx]
					     .player_owner_idx == -1 ||
		     g_cur_craft->player_command_avoid_target_obj_idx !=
			     last_attacker_obj_idx) &&
		    (g_object_table[last_attacker_obj_idx].genus_id == 0 ||
		     g_object_table[last_attacker_obj_idx].genus_id == 1)) {
			int already_covered = 0;
			for (uint16_t object_index = (uint16_t)
				     g_active_region_object_slot_start;
			     object_index <
			     g_active_region_craft_object_slot_end;
			     ++object_index) {
				if (object_index !=
				    g_pai_context.object_index) {
					struct object_record *candidate_object =
						&g_object_table[object_index];
					if (candidate_object->object_type !=
						    0 &&
					    candidate_object->flight_group_idx ==
						    g_pai_context
							    .craft_flight_group_index) {
						struct craft_data
							*candidate_craft =
								candidate_object
									->mobj
									->p_craft;
						if (candidate_craft != NULL &&
						    candidate_craft->ai_controller
								    .target_obj_idx ==
							    last_attacker_obj_idx) {
							already_covered = 1;
						}
					}
				}
			}

			if (!already_covered) {
				g_pai_context.controller->target_obj_idx =
					last_attacker_obj_idx;
				g_pai_context.controller->target_signature =
					g_object_table[last_attacker_obj_idx]
						.object_signature;
				g_pai_context.controller->has_live_target = 0;
				XVT_LOG_DEBUG(
					"ai.target_picked object=%d target=%d reason=\"leader_attacker\" leader=%d escort=%d plan=%d orderplan=%d order=%d predicted=%d",
					(int)g_pai_context.object_index,
					(int)g_pai_context.controller
						->target_obj_idx,
					(int)g_pai_context.leader_object_index,
					(int)g_pai_context.controller
						->escort_target_fg,
					(int)g_pai_context.controller
						->running_plan_id,
					(int)g_pai_context.controller
						->current_plan_id,
					(int)g_pai_context.order_slot,
					g_flight_sim_side_effects_suppressed);
				return 1;
			}
		}
	}

	return 0;
}

/* Order 14: returns 1 after giving the craft a target near its leader's, with
 * its signature and has_live_target 1, else 0; 0 at once when an AI leader is not
 * attacking. Sets require_undisabled_target on disableldr1pln. The candidate
 * target comes first, when it can be targeted and, under a player leader, lies
 * within a rough 0x50000; one farther away makes it return 0. Without a leader
 * it tests object 255, the no-leader index, as the leader. Else it starts from
 * an AI leader's target, or under a player leader from the last craft hostile
 * to the leader whose last attacker the player flies. From that craft it takes,
 * starting craft_ordinal slots on, the first craft of the same flight group that
 * is targetable near this craft (range expanded) and, on capfreeldr1pln,
 * disableldr1pln or kamikaze1pln, matches the current order; for a static
 * object, the first after it that does both. Does not check last_attacker_obj_idx
 * for 0xFFFF before reading that object. The avoid test compares with the loop
 * count, not the candidate, and a static candidate that fails the first test
 * stops the search there. */
// FUNCTION: XVT 0x461DA0
int16_t paifight_followleadatkorder(void)
{
	enum { PLAYER_LEADER_TARGET_RANGE = 0x50000 };

	int leader_object_index = g_pai_context.leader_object_index;
	int leader_player_owner =
		g_object_table[leader_object_index].player_owner_idx;
	struct ai_controller *leader_controller =
		&g_pai_context.leader_or_self_craft->ai_controller;
	uint16_t maneuver_mode = leader_controller->maneuver_mode;
	struct pai_plan_record *plan =
		&g_plan_table[g_pai_context.controller->current_plan_id];
	if (strcmp(plan->name, "disableldr1pln") == 0) {
		g_pai_context.require_undisabled_target = 1;
	} else {
		g_pai_context.require_undisabled_target = 0;
	}
	if (maneuver_mode != AI_MANEUVER_MODE_ATTACK &&
	    maneuver_mode != AI_MANEUVER_MODE_ROCKET_ATTACK &&
	    leader_player_owner == -1) {
		return 0;
	}

	uint16_t candidate_target_idx =
		g_pai_context.controller->candidate_target_idx;
	if (candidate_target_idx != UINT16_MAX &&
	    candidate_target_idx != AI_TARGET_ABORT) {
		int valid_target =
			pai_is_object_targetable(candidate_target_idx);

		if (valid_target != 0) {
			if (leader_player_owner != -1) {
				pai_object_ref_update_rough_distance(
					g_pai_context.object_index,
					candidate_target_idx);
				if (g_last_rough_distance >
				    PLAYER_LEADER_TARGET_RANGE) {
					XVT_LOG_DEBUG(
						"ai.command_target_far object=%d leader=%d slot=%d target=%d range=%d predicted=%d",
						(int)g_pai_context.object_index,
						(int)g_pai_context
							.leader_object_index,
						leader_player_owner,
						(int)candidate_target_idx,
						g_last_rough_distance,
						g_flight_sim_side_effects_suppressed);
					return 0;
				}
			}
			g_pai_context.controller->target_obj_idx =
				candidate_target_idx;
			g_pai_context.controller->target_signature =
				g_object_table[candidate_target_idx]
					.object_signature;
			g_pai_context.controller->has_live_target = 1;
			XVT_LOG_DEBUG(
				"ai.target_picked object=%d target=%d reason=\"command\" leader=%d escort=%d plan=%d orderplan=%d order=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.controller->target_obj_idx,
				(int)g_pai_context.leader_object_index,
				(int)g_pai_context.controller->escort_target_fg,
				(int)g_pai_context.controller->running_plan_id,
				(int)g_pai_context.controller->current_plan_id,
				(int)g_pai_context.order_slot,
				g_flight_sim_side_effects_suppressed);
			return 1;
		}

		g_pai_context.controller->candidate_target_idx = UINT16_MAX;
		XVT_LOG_DEBUG(
			"ai.command_target_dropped object=%d target=%d predicted=%d",
			(int)g_pai_context.object_index,
			(int)candidate_target_idx,
			g_flight_sim_side_effects_suppressed);
	}

	uint16_t target_obj_idx = UINT16_MAX;
	leader_player_owner =
		g_object_table[leader_object_index].player_owner_idx;
	if (leader_player_owner == -1) {
		target_obj_idx = leader_controller->target_obj_idx;
	} else {
		for (uint16_t object_index =
			     (uint16_t)g_active_region_object_slot_start;
		     (int)object_index < g_active_region_craft_object_slot_end;
		     ++object_index) {
			struct object_record *object =
				&g_object_table[object_index];
			if (object->object_type != 0) {
				struct craft_data *craft =
					object->mobj->p_craft;
				if ((g_pai_context.require_undisabled_target ==
					     0 ||
				     craft->working_subsystems != 0) &&
				    g_cur_craft->player_command_avoid_target_obj_idx !=
					    object_index &&
				    g_object_table[craft->last_attacker_obj_idx]
						    .player_owner_idx ==
					    leader_player_owner) {
					int object_team =
						g_mission_flight_groups
							[object->flight_group_idx]
								.fg.team;
					int leader_team =
						g_object_table
							[leader_object_index]
								.mobj->team;
					int is_hostile;

					if (object_team == leader_team) {
						is_hostile = 0;
					} else {
						is_hostile =
							g_mission_teams[leader_team]
								.allies[object_team] ==
							0;
					}
					if (is_hostile != 0) {
						target_obj_idx = object_index;
					}
				}
			}
		}
	}

	if (target_obj_idx == UINT16_MAX) {
		return 0;
	}

	struct object_record *target = &g_object_table[target_obj_idx];
	if (target->mobj != NULL) {
		if (target->mobj->p_craft != NULL) {
			uint16_t flight_group_idx = target->flight_group_idx;
			uint16_t candidate_index =
				(uint16_t)(g_cur_craft->craft_ordinal +
					   target_obj_idx);
			if ((int)candidate_index >=
			    g_active_region_craft_object_slot_end) {
				candidate_index = (uint16_t)
					g_active_region_object_slot_start;
			}

			for (uint16_t object_index = (uint16_t)
				     g_active_region_object_slot_start;
			     (int)object_index <
			     g_active_region_craft_object_slot_end;
			     ++object_index) {
				struct object_record *candidate =
					&g_object_table[candidate_index];
				struct craft_data *candidate_craft =
					candidate->mobj->p_craft;
				if (candidate_craft == NULL) {
					if ((int)++candidate_index >=
					    g_active_region_craft_object_slot_end) {
						candidate_index = (uint16_t)
							g_active_region_object_slot_start;
					}
					continue;
				}
				if ((g_pai_context.require_undisabled_target !=
					     0 &&
				     candidate_craft->working_subsystems ==
					     0) ||
				    g_cur_craft->player_command_avoid_target_obj_idx ==
					    object_index) {
					if ((int)++candidate_index >=
					    g_active_region_craft_object_slot_end) {
						candidate_index = (uint16_t)
							g_active_region_object_slot_start;
					}
					continue;
				}
				if (candidate->object_type != 0 &&
				    candidate->flight_group_idx ==
					    flight_group_idx &&
				    pai_is_object_targetable_near_craft(
					    g_pai_context.object_index,
					    candidate_index, 1)) {
					if (strcmp(plan->name,
						   "capfreeldr1pln") != 0 &&
					    strcmp(plan->name,
						   "disableldr1pln") != 0 &&
					    strcmp(plan->name,
						   "kamikaze1pln") != 0) {
						g_pai_context.controller
							->target_obj_idx =
							candidate_index;
						g_pai_context.controller
							->target_signature =
							g_object_table[candidate_index]
								.object_signature;
						g_pai_context.controller
							->has_live_target = 1;
						XVT_LOG_DEBUG(
							"ai.target_picked object=%d target=%d reason=\"leader_target\" leader=%d escort=%d plan=%d orderplan=%d order=%d predicted=%d",
							(int)g_pai_context
								.object_index,
							(int)g_pai_context
								.controller
								->target_obj_idx,
							(int)g_pai_context
								.leader_object_index,
							(int)g_pai_context
								.controller
								->escort_target_fg,
							(int)g_pai_context
								.controller
								->running_plan_id,
							(int)g_pai_context
								.controller
								->current_plan_id,
							(int)g_pai_context
								.order_slot,
							g_flight_sim_side_effects_suppressed);
						return 1;
					}
					if (pai_current_order_targets_match_object(
						    candidate_index) != 0) {
						g_pai_context.controller
							->target_obj_idx =
							candidate_index;
						g_pai_context.controller
							->target_signature =
							g_object_table[candidate_index]
								.object_signature;
						g_pai_context.controller
							->has_live_target = 1;
						XVT_LOG_DEBUG(
							"ai.target_picked object=%d target=%d reason=\"leader_target\" leader=%d escort=%d plan=%d orderplan=%d order=%d predicted=%d",
							(int)g_pai_context
								.object_index,
							(int)g_pai_context
								.controller
								->target_obj_idx,
							(int)g_pai_context
								.leader_object_index,
							(int)g_pai_context
								.controller
								->escort_target_fg,
							(int)g_pai_context
								.controller
								->running_plan_id,
							(int)g_pai_context
								.controller
								->current_plan_id,
							(int)g_pai_context
								.order_slot,
							g_flight_sim_side_effects_suppressed);
						return 1;
					}
				}
				if ((int)++candidate_index >=
				    g_active_region_craft_object_slot_end) {
					candidate_index = (uint16_t)
						g_active_region_object_slot_start;
				}
			}
		}
	} else {
		uint16_t static_candidate_idx = (uint16_t)(target_obj_idx + 1);
		uint16_t flight_group_idx = target->flight_group_idx;
		if ((int)static_candidate_idx >=
		    g_region_main_object_slot_end +
			    g_region_static_object_slot_count) {
			static_candidate_idx =
				(uint16_t)g_region_main_object_slot_end;
		}
		for (uint16_t scanned = (uint16_t)g_region_main_object_slot_end;
		     (int)scanned < g_region_main_object_slot_end +
					    g_region_static_object_slot_count;
		     ++scanned) {
			struct object_record *candidate =
				&g_object_table[static_candidate_idx];
			if ((g_pai_context.require_undisabled_target == 0 ||
			     candidate->type_specific_word != 0) &&
			    g_cur_craft->player_command_avoid_target_obj_idx !=
				    static_candidate_idx) {
				if (candidate->object_type != 0 &&
				    candidate->flight_group_idx ==
					    flight_group_idx &&
				    pai_is_object_targetable_near_craft(
					    g_pai_context.object_index,
					    static_candidate_idx, 1) &&
				    pai_current_order_targets_match_object(
					    static_candidate_idx) != 0) {
					g_pai_context.controller
						->target_obj_idx =
						static_candidate_idx;
					g_pai_context.controller
						->target_signature =
						g_object_table
							[static_candidate_idx]
								.object_signature;
					g_pai_context.controller
						->has_live_target = 1;
					XVT_LOG_DEBUG(
						"ai.target_picked object=%d target=%d reason=\"leader_target\" leader=%d escort=%d plan=%d orderplan=%d order=%d predicted=%d",
						(int)g_pai_context.object_index,
						(int)g_pai_context.controller
							->target_obj_idx,
						(int)g_pai_context
							.leader_object_index,
						(int)g_pai_context.controller
							->escort_target_fg,
						(int)g_pai_context.controller
							->running_plan_id,
						(int)g_pai_context.controller
							->current_plan_id,
						(int)g_pai_context.order_slot,
						g_flight_sim_side_effects_suppressed);
					return 1;
				}
				if ((int)++static_candidate_idx >=
				    g_region_main_object_slot_end +
					    g_region_static_object_slot_count) {
					static_candidate_idx = (uint16_t)
						g_region_main_object_slot_end;
				}
			}
		}
	}

	return 0;
}

/* Order 18: sets escort_target_fg to the flight group of the nearest object that
 * matches the current order's first pair of target conditions, or else its
 * second pair; to 255 when neither finds one. Returns 0 on every path. */
// FUNCTION: XVT 0x462550
int16_t paifight_checkescortorder(void)
{
	g_pai_context.controller->escort_target_fg = -1;
	if (paifight_searchforclosestingroup(
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[g_pai_context.order_slot]
			    .target1_type,
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[g_pai_context.order_slot]
			    .target1,
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[g_pai_context.order_slot]
			    .target1_or_target2,
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[g_pai_context.order_slot]
			    .target2_type,
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[g_pai_context.order_slot]
			    .target2) != -1) {
		g_pai_context.controller->escort_target_fg =
			g_ai_escort_candidate_fg_idx;
		return 0;
	}

	if (paifight_searchforclosestingroup(
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[g_pai_context.order_slot]
			    .secondary_target_types[0],
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[g_pai_context.order_slot]
			    .secondary_targets[0],
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[g_pai_context.order_slot]
			    .target3_or_target4,
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[g_pai_context.order_slot]
			    .secondary_target_types[1],
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[g_pai_context.order_slot]
			    .secondary_targets[1]) != -1) {
		g_pai_context.controller->escort_target_fg =
			g_ai_escort_candidate_fg_idx;
		return 0;
	}

	return 0;
}

/* Returns the nearest live object, in the craft slots or the static slots, of a
 * flight group that matches the target conditions, or -1; sets
 * g_ai_escort_candidate_fg_idx to its flight group and g_last_rough_distance. Does
 * not check that the object can be targeted. */
// FUNCTION: XVT 0x462690
int16_t paifight_searchforclosestingroup(int16_t target1_type, uint16_t target1,
					 int16_t target1_or_target2,
					 int16_t target2_type, uint16_t target2)
{
	uint16_t flight_group_idx = 0;
	uint16_t best_object_index = (int16_t)UINT16_MAX;
	unsigned int best_range_score = UINT32_MAX;
	uint16_t object_index;
	while (g_mission_header.num_flight_groups > flight_group_idx) {
		int16_t matches_target1 =
			mission_flight_group_matches_trigger_variable(
				flight_group_idx, target1_type, target1);
		int16_t matches_target2 =
			mission_flight_group_matches_trigger_variable(
				flight_group_idx, target2_type, target2);
		if (target1_or_target2 == 1) {
			matches_target1 |= matches_target2;
		} else {
			matches_target1 &= matches_target2;
		}

		if (matches_target1 != 0) {
			for (object_index = (uint16_t)
				     g_active_region_object_slot_start;
			     object_index <
			     (int)g_active_region_craft_object_slot_end;
			     ++object_index) {
				if (g_object_table[object_index].object_type !=
					    0 &&
				    g_object_table[object_index]
						    .flight_group_idx ==
					    flight_group_idx) {
					pai_object_ref_update_rough_distance(
						g_pai_context.object_index,
						object_index);
					if (best_range_score >
					    (unsigned int)
						    g_last_rough_distance) {
						best_object_index =
							object_index;
						best_range_score =
							g_last_rough_distance;
						g_ai_escort_candidate_fg_idx =
							(uint8_t)
								flight_group_idx;
					}
				}
			}

			for (object_index =
				     (uint16_t)g_region_main_object_slot_end;
			     object_index <
			     (int)(g_region_main_object_slot_end +
				   g_region_static_object_slot_count);
			     ++object_index) {
				if (g_object_table[object_index].object_type !=
					    0 &&
				    g_object_table[object_index]
						    .flight_group_idx ==
					    flight_group_idx) {
					pai_object_ref_update_rough_distance(
						g_pai_context.object_index,
						object_index);
					if (best_range_score >
					    (unsigned int)
						    g_last_rough_distance) {
						best_object_index =
							object_index;
						best_range_score =
							g_last_rough_distance;
						g_ai_escort_candidate_fg_idx =
							(uint8_t)
								flight_group_idx;
					}
				}
			}
		}
		++flight_group_idx;
	}

	return best_object_index;
}

/* Returns 1 when paifight_has_future_fg_targets finds groups still to come for the
 * order slot's first pair of target conditions or its second pair, else 0. */
// FUNCTION: XVT 0x462830
int16_t paifight_order_slot_has_future_targets(uint16_t order_slot)
{
	int order_index = order_slot;
	if (paifight_has_future_fg_targets(
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[order_index]
			    .target1_type,
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[order_index]
			    .target1,
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[order_index]
			    .target1_or_target2,
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[order_index]
			    .target2_type,
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[order_index]
			    .target2) != 0) {
		return 1;
	}

	return paifight_has_future_fg_targets(
		       g_mission_flight_groups
			       [g_pai_context.craft_flight_group_index]
				       .fg.orders[order_index]
				       .secondary_target_types[0],
		       g_mission_flight_groups
			       [g_pai_context.craft_flight_group_index]
				       .fg.orders[order_index]
				       .secondary_targets[0],
		       g_mission_flight_groups
			       [g_pai_context.craft_flight_group_index]
				       .fg.orders[order_index]
				       .target3_or_target4,
		       g_mission_flight_groups
			       [g_pai_context.craft_flight_group_index]
				       .fg.orders[order_index]
				       .secondary_target_types[1],
		       g_mission_flight_groups
			       [g_pai_context.craft_flight_group_index]
				       .fg.orders[order_index]
				       .secondary_targets[1]) != 0;
}

/* Returns 1 when a flight group that matches the target conditions (either one
 * when target_relation_op is 1, else both), with arrival_enabled set or owned by a
 * player, has not arrived yet or has waves_remaining above 0; else 0. */
// FUNCTION: XVT 0x462930
int16_t paifight_has_future_fg_targets(int16_t target1_type, uint16_t target1,
				       int16_t target_relation_op,
				       int16_t target2_type, uint16_t target2)
{
	for (uint16_t flight_group_idx = 0;
	     flight_group_idx < (int16_t)g_mission_header.num_flight_groups;
	     ++flight_group_idx) {
		if (g_mission_fg_stats[flight_group_idx].arrival_enabled != 0 ||
		    g_mission_flight_groups[flight_group_idx]
				    .player_owner_idx != -1) {
			int16_t matches_target1 =
				mission_flight_group_matches_trigger_variable(
					flight_group_idx, target1_type,
					target1);
			int16_t matches_target2 =
				mission_flight_group_matches_trigger_variable(
					flight_group_idx, target2_type,
					target2);

			if (target_relation_op == 1) {
				matches_target1 |= matches_target2;
			} else {
				matches_target1 &= matches_target2;
			}

			if (matches_target1 != 0) {
				if (g_mission_fg_stats[flight_group_idx]
					    .has_arrived == 0) {
					return 1;
				}
				if (g_mission_fg_stats[flight_group_idx]
					    .waves_remaining != 0) {
					return 1;
				}
			}
		}
	}

	return 0;
}
