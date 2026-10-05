#include "xvt/flight/ai/paiorder.h"

#include <string.h>

#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/pai_targetability.h"
#include "xvt/flight/ai/paifight.h"
#include "xvt/flight/ai/paiman.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/log/log_both_builds.h"

/* Rough distance in world units, by skill tier 0 to 2, beyond which
 * paiorder_stillattackorder forgets an attacker that is not a warhead. Entry 3
 * is 0. Nothing writes it. */
// GLOBAL: XVT 0x5243A8
static int g_ai_still_attack_last_attacker_range_by_skill[4] = {0x8000, 0xC000,
								0xE000, 0};

/* Class 0, 1 or 2 of an object's bearing for each eighth of a circle (0x2000
 * angle units) it lies off the craft's yaw, counting from the yaw: 0 for the
 * first and last eighth, 2 for the two in the middle, 1 for the rest. The
 * under-attack, on-tail and avoid-hit orders pick maneuvers by it. Nothing
 * writes it. */
// GLOBAL: XVT 0x5243D8
static uint8_t g_ai_threat_bearing_class_by_octant[8] = {0, 1, 1, 2,
							 2, 1, 1, 0};
/* Rough distance in world units, by skill tier 0 to 2, within which
 * paiorder_underattackorder and paiorder_avoidhitorder look for an enemy
 * starfighter pointed at the craft. Entry 3 is 0. Nothing writes it. */
// GLOBAL: XVT 0x5243B8
static int g_ai_attacker_search_range_by_skill[4] = {0x2000, 0x3000, 0x4000, 0};
/* Rough distance in world units, by skill tier 0 to 2, within which a homing
 * warhead aimed at the craft counts as a threat; the under-attack and avoid-hit
 * orders triple it for a concussion missile, and avoid-hit for type 149 too.
 * paifight_gunnerselfdefenseorder reads it as well. Entry 3 is 0. Nothing
 * writes it. */
// GLOBAL: XVT 0x5243C8
int g_ai_warhead_threat_range_by_skill[4] = {0x800, 0x1000, 0x1800, 0};
/* Four maneuvers paiorder_underattackorder picks from by the low two bits of a
 * game_rand draw, for an attacker in bearing class 0 or 1; paiman_attackmaneuver
 * reads it too. Nothing writes it. */
// GLOBAL: XVT 0x5243E0
uint8_t g_ai_under_attack_front_side_maneuver_choices[4] = {
	AI_MANEUVER_MODE_ZOOM, AI_MANEUVER_MODE_DIVE,
	AI_MANEUVER_MODE_SPLITS_DIVE, AI_MANEUVER_MODE_IMMELMANN};
/* Eight maneuvers paiorder_underattackorder picks from by the low three bits of
 * a game_rand draw, for an attacker in bearing class 2; paiman_attackmaneuver
 * reads it too. Nothing writes it. */
// GLOBAL: XVT 0x5243E8
uint8_t g_ai_under_attack_rear_maneuver_choices[8] = {
	AI_MANEUVER_MODE_TURN_INSIDE,	 AI_MANEUVER_MODE_SPLITS_DIVE,
	AI_MANEUVER_MODE_TURN_INSIDE,	 AI_MANEUVER_MODE_TURN_INSIDE,
	AI_MANEUVER_MODE_AVOID_ATTACKER, AI_MANEUVER_MODE_AVOID_ATTACKER,
	AI_MANEUVER_MODE_SCISSORS,	 AI_MANEUVER_MODE_AVOID_ATTACKER,
};

/* The order handlers by order id, as the plan text's order tokens number them;
 * pai_process_plan calls them. A handler returns nonzero when its order fires,
 * which switches the craft to the plan paired with the order. Nothing writes
 * it. */
// GLOBAL: XVT 0x5243F0
pai_order_func g_order_table[48] = {
	paiorder_nullhandler,
	paiorder_updatecourseorder,
	paiorder_underattackorder,
	paiorder_stillattackorder,
	paiorder_flyhomeorder,
	paifight_fightershootorder,
	paifight_gunnerselfdefenseorder,
	paifight_gunneroffenseorder,
	paifight_missiledefenseorder,
	paifight_scanfortargetorder,
	paiorder_waitrunorder,
	paiorder_breakofforder,
	paiorder_leaderdeadorder,
	paifight_coverleaderorder,
	paifight_followleadatkorder,
	paiorder_abortmissionorder,
	paiorder_ontailorder,
	paiorder_alwaysorder,
	paifight_checkescortorder,
	paiorder_leadergohomeorder,
	paiorder_hyperspaceorder,
	paiorder_enterhangarorder,
	paiorder_mothershiporder,
	paifight_escorttargetorder,
	paiorder_lookforcrafttoboardorder,
	paiorder_abortboardorder,
	paiorder_returnboardorder,
	paiorder_awaitboardorder,
	paiorder_makedisabledorder,
	paiorder_neartargetorder,
	paiorder_rocketsonboardorder,
	paiorder_avoidhitorder,
	paiorder_waitforallreturnorder,
	paiorder_waitforallcreateorder,
	paiorder_evasiveorder,
	paiorder_targetfromplayerorder,
	paiorder_avoidstarshiporder,
	paiorder_checkhyperorder,
	paiorder_stopgohomeorder,
	paiorder_completegohomeorder,
	paiorder_completegootherorder,
	paiorder_completefolloworder,
	paiorder_waitgootherorder,
	paiorder_orderswitchorder,
	paiorder_killselforder,
	paiorder_dropoffdestorder,
	paiorder_abortmotherwaitorder,
	paiorder_playerinputorder,
};

/* Order 47: does nothing and returns 0. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4661E0
int16_t paiorder_playerinputorder(void) { return 0; }

/* Order 0: does nothing and returns 0. */
// FUNCTION: XVT 0x4661F0
int16_t paiorder_nullhandler(void) { return 0; }

/* Order 1: runs the step function of the craft's maneuver_mode from
 * g_ai_course_order_maneuver_table, through g_ai_current_maneuver_proc, which it sets,
 * and returns what that returns. Does not check maneuver_mode against the
 * table's 34 entries. */
// FUNCTION: XVT 0x466200
int16_t paiorder_updatecourseorder(void)
{
	g_ai_current_maneuver_proc =
		g_ai_course_order_maneuver_table[g_pai_context.controller
							 ->maneuver_mode];
	return g_ai_current_maneuver_proc();
}

/* Order 2: for a starfighter or transport still on its plan's own maneuver,
 * picks a maneuver against an attacker; returns 0 on every path. With no
 * attacker known it looks first for a homing warhead aimed at the craft within
 * g_ai_warhead_threat_range_by_skill (three times that for a concussion missile).
 * For one it records the warhead in last_attacker_obj_idx, turns away from it in
 * bearing class 0 or turns inside otherwise, then, carrying countermeasures,
 * adds 10 to chaff_active_seconds and uses a round (none when the flight group's
 * status1 or status2 is 21) for chaff with a working countermeasure system, or
 * else fires a countermeasure when cm_fire_cooldown_timer is 0, and returns. Next
 * it looks for an AI-flown enemy starfighter within
 * g_ai_attacker_search_range_by_skill whose nose points within 0x2000 angle units of
 * the craft in yaw and pitch, only while the craft is active, and records the
 * first. For a known attacker it then picks by bearing class with game_rand: in
 * class 1, turns inside when closer than 0x2000 on a quarter of the draws, else
 * one of the front and side choices; in class 0, one of those choices on just
 * over half the draws, else a head-on attack; in class 2, one of the rear
 * choices when the attacker is faster or within 0x8000, else speeds away. A
 * non-craft attacker counts as speed 900. Each pick runs
 * paiman_initmaneuver. */
// FUNCTION: XVT 0x466220
int16_t paiorder_underattackorder(void)
{
	unsigned int self_obj_idx = g_pai_context.object_index;

	if (g_object_table[self_obj_idx].genus_id != CRAFT_GENUS_STARFIGHTER &&
	    g_object_table[self_obj_idx].genus_id != CRAFT_GENUS_TRANSPORT) {
		return 0;
	}
	if (g_pai_context.controller->maneuver_mode ==
	    g_pai_context.initial_maneuver_id) {
		if (g_cur_craft->last_attacker_obj_idx == UINT16_MAX) {
			int max_range_score = g_ai_warhead_threat_range_by_skill
				[g_pai_context.skill_tier];
			uint16_t object_idx;
			for (object_idx =
				     (uint16_t)g_projectile_object_slot_start;
			     object_idx < g_projectile_object_slot_end;
			     ++object_idx) {
				uint8_t object_type =
					g_object_table[object_idx].object_type;
				if (object_type != 0 &&
				    g_object_table[object_idx]
						    .mobj->p_warhead_guidance
						    ->homing_tier != 0 &&
				    g_object_table[object_idx]
						    .mobj->p_warhead_guidance
						    ->target_obj_idx ==
					    g_pai_context.object_index) {
					int threat_range = max_range_score * 3;
					if (object_type !=
					    WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE) {
						threat_range = max_range_score;
					}
					if (pai_is_object_within_range_of_craft(
						    object_idx,
						    (unsigned int)
							    threat_range) ==
					    1) {
						g_cur_craft
							->last_attacker_obj_idx =
							object_idx;
						pai_object_ref_direction_to_object_ref(
							g_pai_context
								.object_index,
							object_idx);
						if (g_ai_threat_bearing_class_by_octant
							    [(uint16_t)(trig2_xyangle -
									g_object_table[g_pai_context
											       .object_index]
										.yaw) >>
							     13] == 0) {
							g_pai_context
								.controller
								->maneuver_mode =
								AI_MANEUVER_MODE_TURN_AWAY;
						} else {
							g_pai_context
								.controller
								->maneuver_mode =
								AI_MANEUVER_MODE_TURN_INSIDE;
						}
						paiman_initmaneuver();
						if (g_cur_craft->cm_type_id !=
							    COUNTERMEASURE_TYPE_NONE &&
						    g_cur_craft->cm_ammo_count !=
							    0) {
							if (g_cur_craft->cm_type_id ==
								    COUNTERMEASURE_TYPE_CHAFF &&
							    (g_cur_craft
								     ->working_subsystems &
							     CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES) !=
								    0) {
								g_cur_craft
									->chaff_active_seconds +=
									10;
								if (g_mission_flight_groups[g_pai_context
												    .craft_flight_group_index]
										    .fg
										    .status1 !=
									    21 &&
								    g_mission_flight_groups[g_pai_context
												    .craft_flight_group_index]
										    .fg
										    .status2 !=
									    21) {
									--g_cur_craft
										  ->cm_ammo_count;
								}
							} else if (
								g_cur_craft
									->cm_fire_cooldown_timer ==
								0) {
								laser_createcountermeasureprojectile(
									g_pai_context
										.object_index,
									COUNTERMEASURE_PROJECTILE_OBJECT_TYPE);
							}
						}
						XVT_LOG_DEBUG(
							"ai.warhead_evaded object=%d warhead=%d type=%d via=\"under_attack\" maneuver=%d cm=%d ammo=%d seconds=%u predicted=%d",
							(int)g_pai_context
								.object_index,
							(int)object_idx,
							(int)g_object_table
								[object_idx]
									.object_type,
							(int)g_pai_context
								.controller
								->maneuver_mode,
							(int)g_cur_craft
								->cm_type_id,
							(int)g_cur_craft
								->cm_ammo_count,
							(unsigned)g_cur_craft
								->chaff_active_seconds,
							g_flight_sim_side_effects_suppressed);
						return 0;
					}
				}
			}

			max_range_score = g_ai_attacker_search_range_by_skill
				[g_pai_context.skill_tier];
			if (g_cur_craft->last_attacker_obj_idx == UINT16_MAX) {
				for (object_idx = (uint16_t)
					     g_active_region_object_slot_start;
				     object_idx <
				     g_active_region_craft_object_slot_end;
				     ++object_idx) {
					if (g_object_table[object_idx]
							    .player_owner_idx ==
						    -1 &&
					    g_object_table[object_idx]
							    .object_type != 0) {
						int object_team =
							g_mission_flight_groups
								[g_object_table[object_idx]
									 .flight_group_idx]
									.fg
									.team;
						int source_team =
							g_object_table
								[g_pai_context
									 .object_index]
									.mobj
									->team;
						int is_enemy =
							object_team == source_team
								? 0
								: g_mission_teams[source_team]
										  .allies[object_team] ==
									  0;
						if (is_enemy &&
						    g_cur_craft->object_kind ==
							    CRAFT_OBJECT_KIND_ACTIVE &&
						    g_object_table[object_idx]
								    .genus_id ==
							    CRAFT_GENUS_STARFIGHTER &&
						    pai_is_object_within_range_of_craft(
							    object_idx,
							    (unsigned int)
								    max_range_score) ==
							    1) {
							pai_object_ref_direction_to_object_ref(
								object_idx,
								self_obj_idx);
							uint16_t horizontal_angle =
								(uint16_t)(trig2_xyangle -
									   g_object_table[object_idx]
										   .yaw);
							if (horizontal_angle >=
							    0x8000) {
								horizontal_angle =
									(uint16_t)-horizontal_angle;
							}
							uint16_t vertical_angle =
								(uint16_t)(trig2_pitch -
									   g_object_table[object_idx]
										   .pitch);
							if (vertical_angle >=
							    0x8000) {
								vertical_angle =
									(uint16_t)-vertical_angle;
							}
							if (horizontal_angle <
								    0x2000 &&
							    vertical_angle <
								    0x2000) {
								g_cur_craft
									->last_attacker_obj_idx =
									object_idx;
								break;
							}
						}
					}
				}
			}
		}

		if (g_cur_craft->last_attacker_obj_idx != UINT16_MAX) {
			pai_object_ref_direction_to_object_ref(
				self_obj_idx,
				g_cur_craft->last_attacker_obj_idx);
			uint8_t threat_bearing =
				g_ai_threat_bearing_class_by_octant
					[(uint16_t)(trig2_xyangle -
						    g_object_table[self_obj_idx]
							    .yaw) >>
					 13];
			uint16_t own_max_speed =
				g_model_defs[g_cur_craft->model_index]
					.max_speed;
			struct mobile_object *attacker =
				g_object_table[g_cur_craft
						       ->last_attacker_obj_idx]
					.mobj;
			uint16_t attacker_max_speed;
			if (attacker->family == 0) {
				attacker_max_speed =
					g_model_defs[attacker->p_craft
							     ->model_index]
						.max_speed;
			} else {
				attacker_max_speed = 900;
			}
			uint16_t random_value = (uint16_t)game_rand();
			uint8_t maneuver_mode;
			if (threat_bearing == 1) {
				if (trig2_polardistance >= 0x2000 ||
				    random_value >= 0x4000) {
					maneuver_mode =
						g_ai_under_attack_front_side_maneuver_choices
							[random_value & 3];
				} else {
					maneuver_mode =
						AI_MANEUVER_MODE_TURN_INSIDE;
				}
			} else if (threat_bearing == 0) {
				maneuver_mode =
					random_value <= 0x8000
						? g_ai_under_attack_front_side_maneuver_choices
							  [random_value & 3]
						: AI_MANEUVER_MODE_HEAD_ON_ATTACK;
			} else {
				if (own_max_speed < attacker_max_speed ||
				    trig2_polardistance <= 0x8000) {
					maneuver_mode =
						g_ai_under_attack_rear_maneuver_choices
							[random_value & 7];
				} else {
					maneuver_mode =
						AI_MANEUVER_MODE_SPEED_AWAY;
				}
			}
			g_pai_context.controller->maneuver_mode = maneuver_mode;
			XVT_LOG_DEBUG(
				"ai.attack_answered object=%d attacker=%d bearing=%d distance=%d speed=%d attacker_speed=%d draw=%d maneuver=%d predicted=%d",
				(int)self_obj_idx,
				(int)g_cur_craft->last_attacker_obj_idx,
				(int)threat_bearing, trig2_polardistance,
				(int)own_max_speed, (int)attacker_max_speed,
				(int)random_value, (int)maneuver_mode,
				g_flight_sim_side_effects_suppressed);
			paiman_initmaneuver();
		}
	}
	return 0;
}

/* Order 3: returns 1, after setting last_attacker_obj_idx to 0xFFFF, when the
 * craft is off its plan's maneuver and its recorded attacker is done: an object
 * in the projectile slots that is gone or no longer targets the craft, or
 * another object not within g_ai_still_attack_last_attacker_range_by_skill. Else 0.
 * Sets g_last_rough_distance for an attacker that is not a warhead. */
// FUNCTION: XVT 0x4666E0
int16_t paiorder_stillattackorder(void)
{
	if (g_pai_context.controller->maneuver_mode !=
	    g_pai_context.initial_maneuver_id) {
		uint16_t *last_attacker_obj_idx_ptr =
			&g_cur_craft->last_attacker_obj_idx;
		uint16_t last_attacker_obj_idx = *last_attacker_obj_idx_ptr;
		if (last_attacker_obj_idx != UINT16_MAX) {
			unsigned int attacker_index = last_attacker_obj_idx;
			if (g_projectile_object_slot_start <=
				    (int)attacker_index &&
			    g_projectile_object_slot_end >
				    (int)attacker_index) {
				struct object_record *attacker =
					&g_object_table[attacker_index];
				struct warhead_guidance_state *guidance =
					attacker->mobj->p_warhead_guidance;
				if (attacker->object_type == 0 ||
				    guidance->target_obj_idx !=
					    g_pai_context.object_index) {
					*last_attacker_obj_idx_ptr = UINT16_MAX;
					XVT_LOG_DEBUG(
						"ai.attacker_forgotten object=%d attacker=%d why=\"warhead_done\" distance=-1 range=-1 predicted=%d",
						(int)g_pai_context.object_index,
						(int)last_attacker_obj_idx,
						g_flight_sim_side_effects_suppressed);
					return 1;
				}
			} else if (
				last_attacker_obj_idx != UINT16_MAX &&
				!pai_is_object_within_range_of_craft(
					attacker_index,
					(unsigned int)
						g_ai_still_attack_last_attacker_range_by_skill
							[g_pai_context
								 .skill_tier])) {
				g_cur_craft->last_attacker_obj_idx = UINT16_MAX;
				XVT_LOG_DEBUG(
					"ai.attacker_forgotten object=%d attacker=%d why=\"out_of_range\" distance=%d range=%d predicted=%d",
					(int)g_pai_context.object_index,
					(int)last_attacker_obj_idx,
					g_last_rough_distance,
					g_ai_still_attack_last_attacker_range_by_skill
						[g_pai_context.skill_tier],
					g_flight_sim_side_effects_suppressed);
				return 1;
			}
		}
	}
	return 0;
}

/* Order 4: steers the craft home and returns 1 once it is within 2,048 world
 * units of the outside hangar point of its mothership, else 0. Sets its
 * separation to 1. The mothership is the leader of the captured departure
 * mothership group for a captured craft whose group leaves by one, else of the
 * departure mothership group when departure_method is set, else of the alternate
 * one when used; one in a player's flight group does not count. With one, it
 * targets it, aims at the outside hangar point of its model and, through
 * paiman_setspeed, slows to speed 150 within 0x10000, 100 within 0x8000 and 75
 * within 0x4000. With none, it targets mission point 13 when enabled, else the
 * group's current point, and returns 0. Sets the trig2_ globals with the
 * mothership. */
// FUNCTION: XVT 0x4667B0
int16_t paiorder_flyhomeorder(void)
{
	g_cur_craft->ai_flight.separation = 1;
	uint16_t mothership_object = UINT16_MAX;
	if (g_cur_craft->captured_by_flight_group != 0) {
		if (g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.captured_depart_via_mothership != 0) {
			uint8_t mothership_flight_group =
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.captured_departure_mothership;
			mothership_object = pai_find_mothership_object(
				mothership_flight_group);
		}
	} else {
		if (g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.departure_method != 0) {
			mothership_object = pai_find_mothership_object(
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg.departure_mothership);
		}
		if (mothership_object == UINT16_MAX &&
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
				    .fg.alternate_mothership_used != 0) {
			mothership_object = pai_find_mothership_object(
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg.alternate_mothership);
		}
	}
	if (mothership_object != UINT16_MAX &&
	    g_mission_flight_groups[g_object_table[mothership_object]
					    .flight_group_idx]
			    .player_owner_idx != -1) {
		mothership_object = UINT16_MAX;
	}
	if (mothership_object != UINT16_MAX) {
		if (g_pai_context.controller->target_obj_idx !=
		    mothership_object) {
			XVT_LOG_DEBUG(
				"ai.mothership_chosen object=%d mothership=%d fg=%d previous=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)mothership_object,
				(int)g_object_table[mothership_object]
					.flight_group_idx,
				(int)g_pai_context.controller->target_obj_idx,
				g_flight_sim_side_effects_suppressed);
		}
		g_pai_context.controller->target_obj_idx = mothership_object;
		g_pai_context.controller->target_signature =
			g_object_table[mothership_object].object_signature;
		g_pai_context.controller->has_live_target = 0;
		int model_index = g_object_table[mothership_object]
					  .mobj->p_craft->model_index;
		pai_rotate_local_vector_to_world_scratch(
			&g_object_table[mothership_object],
			g_model_defs[model_index].hangar_points.outside.side,
			g_model_defs[model_index].hangar_points.outside.up,
			g_model_defs[model_index]
				.hangar_points.outside.forward);
		g_pai_context.controller->aim_point_x =
			g_rotated_x + g_object_table[mothership_object].world_x;
		g_pai_context.controller->aim_point_y =
			g_rotated_y + g_object_table[mothership_object].world_y;
		g_pai_context.controller->aim_point_z =
			g_rotated_z + g_object_table[mothership_object].world_z;
		pai_calc_angles_to_aim_point();
		if (trig2_polardistance < 0x10000) {
			paiman_setspeed(g_pai_context.object_index, 0x96);
		}
		if (trig2_polardistance < 0x8000) {
			paiman_setspeed(g_pai_context.object_index, 0x64);
		}
		if (trig2_polardistance < 0x4000) {
			paiman_setspeed(g_pai_context.object_index, 0x4B);
		}
		return trig2_polardistance < 2048;
	}
	if (g_pai_context.controller->target_obj_idx != 0x800Du &&
	    g_pai_context.controller->target_obj_idx != 0x8000u) {
		XVT_LOG_DEBUG(
			"ai.no_mothership object=%d fg=%d previous=%d exit=%d predicted=%d",
			(int)g_pai_context.object_index,
			(int)g_pai_context.craft_flight_group_index,
			(int)g_pai_context.controller->target_obj_idx,
			(int)(g_mission_flight_groups
				      [g_pai_context.craft_flight_group_index]
					      .fg.mission_point_enabled[13] !=
			      0),
			g_flight_sim_side_effects_suppressed);
	}

	if (g_mission_flight_groups[g_pai_context.craft_flight_group_index]
		    .fg.mission_point_enabled[13] != 0) {
		g_pai_context.controller->target_obj_idx = 0x800D;
	} else {
		g_pai_context.controller->target_obj_idx = 0x8000;
	}
	g_pai_context.controller->target_signature = 0;
	g_pai_context.controller->has_live_target = 0;
	pai_update_aim_point_from_order_target();
	return 0;
}

/* Order 45: aims the craft at formation slot 0 of flight group variable2 minus
 * 1, as mission_resolve_formation_slot_world_loc places it, plus 932 in Z, and
 * returns 1 once within 2,048 world units, setting waypoint_index to 0; else 0.
 * Returns 0 at once when that group has any arrived outcome. Sets the throttle
 * to 0xC000 within 0x4000 and to 0x6000 within 4,096. Sets the g_worldLoc and
 * trig2_ globals. */
// FUNCTION: XVT 0x46A140
int16_t paiorder_dropoffdestorder(void)
{
	struct mission_order *order =
		&g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			 .fg.orders[g_pai_context.order_slot];
	uint16_t destination_flight_group = (uint16_t)(order->variable2 - 1);
	if (order->variable2 == 0 ||
	    order->variable2 > g_mission_header.num_flight_groups) {
		XVT_LOG_DEBUG(
			"ai.dropoff_group_invalid object=%d fg=%d order=%d group=%d groups=%d predicted=%d",
			(int)g_pai_context.object_index,
			(int)g_pai_context.craft_flight_group_index,
			(int)g_pai_context.order_slot, (int)order->variable2,
			(int)g_mission_header.num_flight_groups,
			g_flight_sim_side_effects_suppressed);
	}

	if (g_mission_fg_stats[destination_flight_group]
		    .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED] != 0) {
		return 0;
	}

	mission_resolve_formation_slot_world_loc(destination_flight_group, 0,
						 UINT16_MAX);
	g_pai_context.controller->aim_point_x = g_world_loc_x;
	g_pai_context.controller->aim_point_y = g_world_loc_y;
	g_pai_context.controller->aim_point_z = g_world_loc_z + 932;
	pai_calc_angles_to_aim_point();
	if (trig2_polardistance < 0x4000) {
		paiman_setpower(g_pai_context.object_index, 0xC000);
	}
	if (trig2_polardistance < 4096) {
		paiman_setpower(g_pai_context.object_index, 0x6000);
	}
	if (trig2_polardistance >= 2048) {
		return 0;
	}

	g_pai_context.controller->waypoint_index = 0;
	XVT_LOG_DEBUG("ai.dropoff_reached object=%d dest=%d predicted=%d",
		      (int)g_pai_context.object_index,
		      (int)destination_flight_group,
		      g_flight_sim_side_effects_suppressed);
	return 1;
}

/* Order 21: flies into the mothership's hangar and removes the craft there.
 * Sets its separation to 1 and think_interval to 29 ticks. The mothership is the
 * leader of the captured departure mothership group for a captured craft, else
 * of the departure mothership group, else of the alternate one when used;
 * departure_method, captured_depart_via_mothership and a player's ownership are not
 * checked here. With one it targets it, aims at the inside hangar point of its
 * model and sets the speed to the mothership's plus 25, or 40 when the
 * mothership is below 25. Within 1,024 world units of that point (512 for a
 * craft not a starship) it removes every AI-flown craft of its flight group
 * that follows a leader in the follow-leader maneuver, then itself and the
 * object it carries: for each it adds to g_mission_fg_stats the departure
 * outcomes and team scores its flags call for, records the outcome by
 * mothership kind and frees the object, emitting message 141 for each craft.
 * Returns 0 with a mothership; with none, sets the speed to 35 and returns 1.
 * Sets the trig2_ globals. */
// FUNCTION: XVT 0x466A70
int16_t paiorder_enterhangarorder(void)
{
	g_cur_craft->ai_flight.separation = 1;
	g_pai_context.controller->think_interval = 29;
	uint16_t mothership_object;
	uint16_t outcome_id;
	if (g_cur_craft->captured_by_flight_group != 0) {
		mothership_object = pai_find_mothership_object(
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.captured_departure_mothership);
		outcome_id = FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT;
	} else {
		mothership_object = pai_find_mothership_object(
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.departure_mothership);
		outcome_id = FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT;
		if (mothership_object == UINT16_MAX &&
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
				    .fg.alternate_mothership_used != 0) {
			mothership_object = pai_find_mothership_object(
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg.alternate_mothership);
			outcome_id =
				FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT;
		}
	}
	if (mothership_object != UINT16_MAX) {
		g_pai_context.controller->target_obj_idx = mothership_object;
		g_pai_context.controller->target_signature =
			g_object_table[mothership_object].object_signature;
		g_pai_context.controller->has_live_target = 0;
		uint16_t model_index = g_object_table[mothership_object]
					       .mobj->p_craft->model_index;
		pai_rotate_local_vector_to_world_scratch(
			&g_object_table[mothership_object],
			g_model_defs[model_index].hangar_points.inside.side,
			g_model_defs[model_index].hangar_points.inside.up,
			g_model_defs[model_index].hangar_points.inside.forward);
		g_pai_context.controller->aim_point_x =
			g_rotated_x + g_object_table[mothership_object].world_x;
		g_pai_context.controller->aim_point_y =
			g_rotated_y + g_object_table[mothership_object].world_y;
		g_pai_context.controller->aim_point_z =
			g_rotated_z + g_object_table[mothership_object].world_z;
	}
	pai_calc_angles_to_aim_point();
	if (mothership_object != UINT16_MAX) {
		if (g_object_table[mothership_object].mobj->speed >= 25) {
			paiman_setspeed(
				g_pai_context.object_index,
				g_object_table[mothership_object].mobj->speed +
					25);
		} else {
			paiman_setspeed(g_pai_context.object_index, 40);
		}

		if ((g_object_table[g_pai_context.object_index].genus_id ==
				     CRAFT_GENUS_STARSHIP
			     ? 1024
			     : 512) > trig2_polardistance) {
			int special_cargo;
			for (uint16_t object_index = (uint16_t)
				     g_active_region_object_slot_start;
			     object_index <
			     g_active_region_craft_object_slot_end;
			     ++object_index) {
				int table_index = object_index;
				struct object_record *object =
					&g_object_table[table_index];
				if (object->object_type == 0 ||
				    object->flight_group_idx !=
					    g_pai_context
						    .craft_flight_group_index) {
					continue;
				}
				struct craft_data *other_craft =
					object->mobj->p_craft;
				struct ai_controller *other_controller =
					&other_craft->ai_controller;
				if (other_craft->leader_obj_idx == UINT8_MAX ||
				    other_controller->maneuver_mode !=
					    AI_MANEUVER_MODE_FOLLOW_LEADER ||
				    object->player_owner_idx != -1) {
					continue;
				}
				if (other_craft->captured_by_flight_group ==
				    0) {
					if (other_controller->skipped_to_order4 ==
						    0 &&
					    (other_craft->ai_flight
							     .go_home_flag !=
						     0 ||
					     (other_craft->ai_flight
							      .mission_aborted_flag ==
						      0 &&
					      other_craft->ai_flight
							      .depart_timer_flag ==
						      0))) {
						special_cargo = 0;
						++g_mission_fg_stats[g_pai_context
									     .craft_flight_group_index]
							  .outcome_count
								  [FLIGHT_GROUP_OUTCOME_DEPARTED];
						if (g_mission_flight_groups
							    [g_pai_context
								     .craft_flight_group_index]
								    .fg
								    .special_cargo_craft ==
						    other_craft
							    ->craft_ordinal) {
							g_mission_fg_stats[g_pai_context
										   .craft_flight_group_index]
								.special_cargo_outcome
									[FLIGHT_GROUP_OUTCOME_DEPARTED] =
								1;
							special_cargo = 1;
						}
						mission_apply_team_goal_score_all_enabled_teams(
							12,
							g_pai_context
								.craft_flight_group_index,
							special_cargo);
					}
					if (other_craft->captured_by_flight_group ==
						    0 &&
					    other_controller->skipped_to_order4 ==
						    1 &&
					    other_craft->ai_flight
							    .mission_aborted_flag ==
						    0 &&
					    other_craft->ai_flight
							    .depart_timer_flag ==
						    0) {
						++g_mission_fg_stats[g_pai_context
									     .craft_flight_group_index]
							  .outcome_count
								  [FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE];
						if (g_mission_flight_groups
							    [g_pai_context
								     .craft_flight_group_index]
								    .fg
								    .special_cargo_craft ==
						    other_craft
							    ->craft_ordinal) {
							g_mission_fg_stats[g_pai_context
										   .craft_flight_group_index]
								.special_cargo_outcome
									[FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE] =
								1;
						}
					}
				}
				msg_emit_craft_message(object_index,
						       other_craft, 141);
				mission_record_craft_outcome(
					object_index,
					g_pai_context.craft_flight_group_index,
					outcome_id);
				g_object_table[table_index].object_type = 0;
				craft_free_linked_objects(other_craft);
				if (g_flight_sim_side_effects_suppressed == 0) {
					XVT_LOG_INFO(
						"ai.entered_hangar object=%d fg=%d as=\"follower\" led_by=%d hangar=%d outcome=%d tick=%d",
						(int)object_index,
						(int)g_pai_context
							.craft_flight_group_index,
						(int)g_pai_context.object_index,
						(int)mothership_object,
						(int)outcome_id, g_game_time);
				}
			}

			if (g_cur_craft->captured_by_flight_group == 0 &&
			    g_pai_context.controller->skipped_to_order4 == 0 &&
			    (g_cur_craft->ai_flight.go_home_flag != 0 ||
			     (g_cur_craft->ai_flight.mission_aborted_flag ==
				      0 &&
			      g_cur_craft->ai_flight.depart_timer_flag == 0))) {
				++g_mission_fg_stats[g_pai_context
							     .craft_flight_group_index]
					  .outcome_count
						  [FLIGHT_GROUP_OUTCOME_DEPARTED];
				special_cargo = 0;
				if (g_mission_flight_groups
					    [g_pai_context
						     .craft_flight_group_index]
						    .fg.special_cargo_craft ==
				    g_cur_craft->craft_ordinal) {
					g_mission_fg_stats[g_pai_context
								   .craft_flight_group_index]
						.special_cargo_outcome
							[FLIGHT_GROUP_OUTCOME_DEPARTED] =
						1;
					special_cargo = 1;
				}
				mission_apply_team_goal_score_all_enabled_teams(
					12,
					g_pai_context.craft_flight_group_index,
					special_cargo);
			}
			if (g_cur_craft->captured_by_flight_group == 0 &&
			    g_pai_context.controller->skipped_to_order4 == 1 &&
			    g_cur_craft->ai_flight.mission_aborted_flag == 0 &&
			    g_cur_craft->ai_flight.depart_timer_flag == 0) {
				++g_mission_fg_stats[g_pai_context
							     .craft_flight_group_index]
					  .outcome_count
						  [FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE];
				if (g_mission_flight_groups
					    [g_pai_context
						     .craft_flight_group_index]
						    .fg.special_cargo_craft ==
				    g_cur_craft->craft_ordinal) {
					g_mission_fg_stats[g_pai_context
								   .craft_flight_group_index]
						.special_cargo_outcome
							[FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE] =
						1;
				}
			}
			unsigned int team;
			unsigned int other_team;
			if (g_cur_craft->captured_by_flight_group != 0) {
				team = g_object_table[g_pai_context
							      .object_index]
					       .mobj->team;
				++g_mission_fg_stats
					  [g_pai_context
						   .craft_flight_group_index]
						  .team_captured_departed_count
							  [team];
				special_cargo = 0;
				if (g_mission_flight_groups
					    [g_pai_context
						     .craft_flight_group_index]
						    .fg.special_cargo_craft ==
				    g_cur_craft->craft_ordinal) {
					++g_mission_fg_stats
						  [g_pai_context
							   .craft_flight_group_index]
							  .team_special_cargo_captured_departed
								  [team];
					special_cargo = 1;
				}
				mission_apply_team_goal_score_for_team(
					44,
					g_pai_context.craft_flight_group_index,
					special_cargo, (uint8_t)team);
				for (other_team = 0; other_team < 10;
				     ++other_team) {
					if (other_team != team &&
					    g_mission_flight_groups
							    [g_pai_context
								     .craft_flight_group_index]
								    .fg.team !=
						    other_team) {
						++g_mission_fg_stats[g_pai_context
									     .craft_flight_group_index]
							  .team_uncaptured_lost
								  [other_team];
						if (g_mission_flight_groups
							    [g_pai_context
								     .craft_flight_group_index]
								    .fg
								    .special_cargo_craft ==
						    g_cur_craft
							    ->craft_ordinal) {
							g_mission_fg_stats[g_pai_context
										   .craft_flight_group_index]
								.team_special_cargo_uncaptured_lost
									[other_team] =
								1;
						}
					}
				}
			}
			msg_emit_craft_message(g_pai_context.object_index,
					       g_cur_craft, 141);
			mission_record_craft_outcome(
				g_pai_context.object_index,
				g_pai_context.craft_flight_group_index,
				outcome_id);
			g_object_table[g_pai_context.object_index].object_type =
				0;
			craft_free_linked_objects(g_cur_craft);
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_INFO(
					"ai.entered_hangar object=%d fg=%d as=\"own\" led_by=%d hangar=%d outcome=%d tick=%d",
					(int)g_pai_context.object_index,
					(int)g_pai_context
						.craft_flight_group_index,
					(int)g_pai_context.object_index,
					(int)mothership_object, (int)outcome_id,
					g_game_time);
			}

			uint16_t carried_object_index =
				g_cur_craft->carried_object_index;
			if (carried_object_index != UINT16_MAX &&
			    g_object_table[carried_object_index].mobj != NULL) {
				uint16_t carried_group_index =
					g_object_table[carried_object_index]
						.flight_group_idx;
				mission_record_craft_outcome(
					carried_object_index,
					carried_group_index, outcome_id);
				struct craft_data *carried_craft =
					g_object_table[carried_object_index]
						.mobj->p_craft;
				if (carried_craft->captured_by_flight_group !=
				    0) {
					team = g_object_table
						       [carried_object_index]
							       .mobj->team;
					++g_mission_fg_stats[carried_group_index]
						  .team_captured_departed_count
							  [team];
					special_cargo = 0;
					if (g_mission_flight_groups
						    [carried_group_index]
							    .fg
							    .special_cargo_craft ==
					    carried_craft->craft_ordinal) {
						++g_mission_fg_stats
							  [carried_group_index]
								  .team_special_cargo_captured_departed
									  [team];
						special_cargo = 1;
					}
					mission_apply_team_goal_score_for_team(
						44, carried_group_index,
						special_cargo, (uint8_t)team);
					for (other_team = 0; other_team < 10;
					     ++other_team) {
						if (other_team != team &&
						    g_mission_flight_groups
								    [carried_group_index]
									    .fg
									    .team !=
							    other_team) {
							++g_mission_fg_stats[carried_group_index]
								  .team_uncaptured_lost
									  [other_team];
							if (g_mission_flight_groups
								    [carried_group_index]
									    .fg
									    .special_cargo_craft ==
							    carried_craft
								    ->craft_ordinal) {
								g_mission_fg_stats[carried_group_index]
									.team_special_cargo_uncaptured_lost
										[other_team] =
									1;
							}
						}
					}
					++g_mission_fg_stats[carried_group_index]
						  .outcome_count
							  [FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION];
					if (g_mission_flight_groups
						    [carried_group_index]
							    .fg
							    .special_cargo_craft ==
					    carried_craft->craft_ordinal) {
						g_mission_fg_stats[carried_group_index]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION] =
							1;
					}
				} else {
					++g_mission_fg_stats[carried_group_index]
						  .outcome_count
							  [FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION];
					special_cargo = 0;
					if (g_mission_flight_groups
						    [carried_group_index]
							    .fg
							    .special_cargo_craft ==
					    carried_craft->craft_ordinal) {
						g_mission_fg_stats[carried_group_index]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION] =
							1;
						special_cargo = 1;
					}
					mission_apply_team_goal_score_all_enabled_teams(
						46, carried_group_index,
						special_cargo);
				}
				g_object_table[carried_object_index]
					.object_type = 0;
				craft_free_linked_objects(carried_craft);
				if (g_flight_sim_side_effects_suppressed == 0) {
					XVT_LOG_INFO(
						"ai.entered_hangar object=%d fg=%d as=\"cargo\" led_by=%d hangar=%d outcome=%d tick=%d",
						(int)carried_object_index,
						(int)carried_group_index,
						(int)g_pai_context.object_index,
						(int)mothership_object,
						(int)outcome_id, g_game_time);
				}
			}
		}
		return 0;
	}
	paiman_setspeed(g_pai_context.object_index, 35);
	XVT_LOG_DEBUG("ai.hangar_gone object=%d fg=%d predicted=%d",
		      (int)g_pai_context.object_index,
		      (int)g_pai_context.craft_flight_group_index,
		      g_flight_sim_side_effects_suppressed);
	return 1;
}

/* Order 10: returns 1 when the craft is on its plan's maneuver and its target
 * is closer, by rough distance, than its effective skill value plus 0x20000
 * world units; else 0. Does not check that target_obj_idx names an object. Sets
 * g_last_rough_distance. */
// FUNCTION: XVT 0x467320
int16_t paiorder_waitrunorder(void)
{
	if (g_pai_context.controller->maneuver_mode ==
	    g_pai_context.initial_maneuver_id) {
		uint16_t effective_skill =
			pai_get_effective_skill_value(g_cur_craft);
		if (pai_is_object_within_range_of_craft(
			    g_pai_context.controller->target_obj_idx,
			    (unsigned int)effective_skill + 0x20000) == 1) {
			return 1;
		}
	}
	return 0;
}

/* Order 11: returns 1 and drops the target (target 0xFFFF, signature 0, no live
 * target, candidate 0xFFFF) when the player told the craft to avoid it, it
 * cannot be targeted or its slot holds another object now. Next, on
 * disableldr1pln, it returns 1 when the target craft has no working subsystems,
 * clearing only the candidate. Then it drops the target and returns 1 when it
 * is a player's craft with its decoy beam on farther than 0x4000, or, in a
 * version 14 mission, has no working subsystems and is neither the candidate
 * nor a target of the current order. Else 0. Sets g_last_rough_distance on the
 * decoy test. */
// FUNCTION: XVT 0x467380
int16_t paiorder_breakofforder(void)
{
	uint16_t target_obj_idx = g_pai_context.controller->target_obj_idx;

	if (g_cur_craft->player_command_avoid_target_obj_idx ==
	    target_obj_idx) {
		g_pai_context.controller->target_obj_idx = UINT16_MAX;
		g_pai_context.controller->target_signature = 0;
		g_pai_context.controller->has_live_target = 0;
		g_pai_context.controller->candidate_target_idx = UINT16_MAX;
		XVT_LOG_DEBUG(
			"ai.target_dropped object=%d target=%d reason=\"player_avoid\" predicted=%d",
			(int)g_pai_context.object_index, (int)target_obj_idx,
			g_flight_sim_side_effects_suppressed);
		return 1;
	}

	int target_valid = pai_is_object_targetable(target_obj_idx);

	if (target_valid == 0) {
		g_pai_context.controller->target_obj_idx = UINT16_MAX;
		g_pai_context.controller->target_signature = 0;
		g_pai_context.controller->has_live_target = 0;
		g_pai_context.controller->candidate_target_idx = UINT16_MAX;
		XVT_LOG_DEBUG(
			"ai.target_dropped object=%d target=%d reason=\"not_targetable\" predicted=%d",
			(int)g_pai_context.object_index, (int)target_obj_idx,
			g_flight_sim_side_effects_suppressed);
		return 1;
	}

	if (g_pai_context.controller->target_signature !=
	    g_object_table[target_obj_idx].object_signature) {
		g_pai_context.controller->target_obj_idx = UINT16_MAX;
		g_pai_context.controller->target_signature = 0;
		g_pai_context.controller->has_live_target = 0;
		g_pai_context.controller->candidate_target_idx = UINT16_MAX;
		XVT_LOG_DEBUG(
			"ai.target_dropped object=%d target=%d reason=\"replaced\" predicted=%d",
			(int)g_pai_context.object_index, (int)target_obj_idx,
			g_flight_sim_side_effects_suppressed);
		return 1;
	}

	if (strcmp(g_plan_table[g_pai_context.controller->current_plan_id].name,
		   "disableldr1pln") == 0) {
		if (g_active_region_craft_object_slot_end <=
		    (int)target_obj_idx) {
		} else if (g_object_table[target_obj_idx]
				   .mobj->p_craft->working_subsystems == 0) {
			g_pai_context.controller->candidate_target_idx =
				UINT16_MAX;
			XVT_LOG_DEBUG(
				"ai.target_dropped object=%d target=%d reason=\"disabled_done\" predicted=%d",
				(int)g_pai_context.object_index,
				(int)target_obj_idx,
				g_flight_sim_side_effects_suppressed);
			return 1;
		}
	}

	if (g_object_table[target_obj_idx].player_owner_idx != -1) {
		if (g_active_region_craft_object_slot_end <=
		    (int)target_obj_idx) {
		} else if (object_has_active_decoy_beam(target_obj_idx) == 1) {
			pai_object_ref_update_rough_distance(
				g_pai_context.object_index, target_obj_idx);
			if (g_last_rough_distance > 0x4000) {
				g_pai_context.controller->target_obj_idx =
					UINT16_MAX;
				g_pai_context.controller->target_signature = 0;
				g_pai_context.controller->has_live_target = 0;
				g_pai_context.controller->candidate_target_idx =
					UINT16_MAX;
				XVT_LOG_DEBUG(
					"ai.target_dropped object=%d target=%d reason=\"decoy\" predicted=%d",
					(int)g_pai_context.object_index,
					(int)target_obj_idx,
					g_flight_sim_side_effects_suppressed);
				return 1;
			}
		}
	}

	if (g_mission_file_version == 14) {
		uint16_t target_working_subsystems =
			g_object_table[target_obj_idx].type_specific_word;
		if (g_object_table[target_obj_idx].mobj != NULL &&
		    g_object_table[target_obj_idx].mobj->p_craft != NULL) {
			target_working_subsystems =
				g_object_table[target_obj_idx]
					.mobj->p_craft->working_subsystems;
		}
		if (target_working_subsystems == 0 &&
		    g_pai_context.controller->candidate_target_idx !=
			    target_obj_idx &&
		    pai_current_order_targets_match_object(target_obj_idx) ==
			    0) {
			g_pai_context.controller->target_obj_idx = UINT16_MAX;
			g_pai_context.controller->target_signature = 0;
			g_pai_context.controller->has_live_target = 0;
			g_pai_context.controller->candidate_target_idx =
				UINT16_MAX;
			XVT_LOG_DEBUG(
				"ai.target_dropped object=%d target=%d reason=\"disabled_ignored\" predicted=%d",
				(int)g_pai_context.object_index,
				(int)target_obj_idx,
				g_flight_sim_side_effects_suppressed);
			return 1;
		}
	}
	return 0;
}

/* Order 15: returns 1 when the flight group's abort trigger holds for the
 * craft, else 0; 0 at once when max_speed_cache is 0. Triggers 1 to 9: shields
 * out (shield system down, or both banks empty on a freighter or starship),
 * cannons down, warheads out (launcher down or no rounds), hull damage reaching
 * half of hull_max, attacked by any team, shields down to half or to a quarter
 * of the maximum, and hull damage reaching a quarter or three quarters. Trigger
 * 2 tests the cannons but reports IFMSG_394_WARHEADS_OUT. The first time it
 * aborts it counts the aborted outcome, undoes a not-departed count and clears
 * depart_timer_flag, has the tactical officer announce the withdrawal, sends
 * message 387 to the player who owns the flight group, and restores working
 * subsystems when the current order's leader plan is waitforboardpln and
 * subsystem_damage is 0. Sets mission_aborted_flag each time it aborts. */
// FUNCTION: XVT 0x467710
int16_t paiorder_abortmissionorder(void)
{
	if (g_cur_craft->ai_flight.max_speed_cache == 0) {
		return 0;
	}

	int abort_mission = 0;
	int abort_reason_message;
	uint16_t launcher_index;
	uint8_t launcher_count;
	int16_t has_warheads;
	switch (g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.abort_trigger) {
	case 1:
		if ((g_cur_craft->working_subsystems &
		     CRAFT_SUBSYSTEM_FLAG_SHIELDS) == 0) {
			abort_mission = 1;
		}
		if ((g_object_table[g_pai_context.object_index].genus_id == 3 ||
		     g_object_table[g_pai_context.object_index].genus_id ==
			     4) &&
		    g_cur_craft->shield_energy[0] +
				    g_cur_craft->shield_energy[1] ==
			    0) {
			abort_mission = 1;
		}
		abort_reason_message = IFMSG_390_SHIELDS_OUT;
		break;

	case 2:
		if ((g_cur_craft->working_subsystems &
		     CRAFT_SUBSYSTEM_FLAG_CANNONS) == 0) {
			abort_mission = 1;
		}
		abort_reason_message = IFMSG_394_WARHEADS_OUT;
		break;

	case 3:
		if ((g_cur_craft->working_subsystems &
		     CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER) == 0) {
			abort_mission = 1;
		}
		has_warheads = 0;
		launcher_index = 0;
		launcher_count = g_cur_craft->warhead_launcher_count;
		while (launcher_index < launcher_count) {
			if (g_cur_craft
				    ->warhead_slot_type_ids[launcher_index] !=
			    0) {
				uint16_t last_weapon_slot =
					g_model_defs[g_cur_craft->model_index]
						.warhead_launcher_last_slot
							[launcher_index];
				uint16_t weapon_slot_index =
					g_model_defs[g_cur_craft->model_index]
						.warhead_launcher_first_slot
							[launcher_index];
				while (!(last_weapon_slot <
					 weapon_slot_index)) {
					if (g_cur_craft
						    ->weapon_slots
							    [weapon_slot_index]
						    .ammo_count != 0) {
						has_warheads = 1;
					}
					++weapon_slot_index;
				}
			}
			++launcher_index;
		}
		if (has_warheads == 0) {
			abort_mission = 1;
		}
		abort_reason_message = IFMSG_394_WARHEADS_OUT;
		break;

	case 4:
		if (math2_longfraction(g_cur_craft->hull_max, 0x8000) <=
		    g_cur_craft->hull_damage) {
			abort_mission = 1;
		}
		abort_reason_message = IFMSG_392_HULL_AT_50;
		break;

	case 5:
		/* The launcher counter is reused here as a team index over the
		 * ten attacked_by_team entries. */
		for (launcher_index = 0; launcher_index < 10;
		     ++launcher_index) {
			if (g_cur_craft->attacked_by_team[launcher_index] !=
			    0) {
				abort_mission = 1;
			}
		}
		abort_reason_message = IFMSG_395_UNDER_ATTACK;
		break;

	case 6:
		if ((unsigned int)(g_cur_craft->shield_energy[0] +
				   g_cur_craft->shield_energy[1]) <=
		    (uint16_t)math2_fraction(
			    (uint16_t)craft_get_object_max_shield(
				    g_pai_context.object_index),
			    0x8000)) {
			abort_mission = 1;
		}
		abort_reason_message = IFMSG_388_SHIELDS_AT_50;
		break;

	case 7:
		if ((unsigned int)(g_cur_craft->shield_energy[0] +
				   g_cur_craft->shield_energy[1]) <=
		    (uint16_t)math2_fraction(
			    (uint16_t)craft_get_object_max_shield(
				    g_pai_context.object_index),
			    0x4000)) {
			abort_mission = 1;
		}
		abort_reason_message = IFMSG_389_SHIELDS_AT_25;
		break;

	case 8:
		if (math2_longfraction(g_cur_craft->hull_max, 0x4000) <=
		    g_cur_craft->hull_damage) {
			abort_mission = 1;
		}
		abort_reason_message = IFMSG_391_HULL_AT_75;
		break;

	case 9:
		if (math2_longfraction(g_cur_craft->hull_max, 0xC000) <=
		    g_cur_craft->hull_damage) {
			abort_mission = 1;
		}
		abort_reason_message = IFMSG_393_HULL_AT_25;
		break;
	}

	if (abort_mission != 0) {
		if (g_cur_craft->ai_flight.mission_aborted_flag == 0) {
			++g_mission_fg_stats[g_pai_context
						     .craft_flight_group_index]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ABORTED];
			if (g_cur_craft->craft_ordinal ==
			    g_mission_flight_groups
				    [g_pai_context.craft_flight_group_index]
					    .fg.special_cargo_craft) {
				g_mission_fg_stats[g_pai_context
							   .craft_flight_group_index]
					.special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_ABORTED] =
					1;
			}
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_INFO(
					"ai.mission_aborted object=%d fg=%d trigger=%d aborted=%u departing=%d tick=%d",
					(int)g_pai_context.object_index,
					(int)g_pai_context
						.craft_flight_group_index,
					(int)g_mission_flight_groups
						[g_pai_context
							 .craft_flight_group_index]
							.fg.abort_trigger,
					(unsigned)g_mission_fg_stats
						[g_pai_context
							 .craft_flight_group_index]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_ABORTED],
					(int)g_cur_craft->ai_flight
						.depart_timer_flag,
					g_game_time);
			}
			if (g_cur_craft->ai_flight.depart_timer_flag != 0) {
				--g_mission_fg_stats[g_pai_context
							     .craft_flight_group_index]
					  .outcome_count
						  [FLIGHT_GROUP_OUTCOME_NOT_DEPARTED];
				if (g_cur_craft->craft_ordinal ==
				    g_mission_flight_groups
					    [g_pai_context
						     .craft_flight_group_index]
						    .fg.special_cargo_craft) {
					g_mission_fg_stats[g_pai_context
								   .craft_flight_group_index]
						.special_cargo_outcome
							[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] =
						0;
				}
				g_cur_craft->ai_flight.depart_timer_flag = 0;
			}

			fsfx_speak_tactical_officer_event(
				TACTICAL_VOICE_STATUS, TACTICAL_MSG_WITHDRAWING,
				g_pai_context.object_index, UINT16_MAX);
			if (g_mission_flight_groups
				    [g_pai_context.craft_flight_group_index]
					    .player_owner_idx != -1) {
				g_msg_sender_iff =
					(uint8_t)g_object_table
						[g_pai_context.object_index]
							.mobj->iff;
				msg_add_message_ptr(
					0,
					g_mission_flight_groups
						[g_pai_context
							 .craft_flight_group_index]
							.fg.name);
				g_msg_arg_table[1] = (uint16_t)
					hud_mission_fg_get_craft_number_if_shown(
						g_pai_context
							.craft_flight_group_index,
						g_cur_craft);
				g_msg_arg_table[2] =
					(uint16_t)abort_reason_message;
				msg_emit_in_flight_message(
					IFMSG_387_WINGMAN_ARG_ARG_ABORTING_MISSION_ARG,
					g_mission_flight_groups
						[g_pai_context
							 .craft_flight_group_index]
							.player_owner_idx);
			}

			if (strcmp(g_plan_table
					   [g_builtin_plan_id_by_name_index
						    [g_order_leader_builtin_plan_name_index
							     [g_mission_flight_groups[g_pai_context
											      .craft_flight_group_index]
								      .fg
								      .orders[g_pai_context
										      .order_slot]
								      .order]]]
						   .name,
				   "waitforboardpln") == 0 &&
			    g_cur_craft->subsystem_damage == 0) {
				g_cur_craft->working_subsystems =
					g_cur_craft->system_flags;
			}
			XVT_LOG_DEBUG(
				"ai.abort_state object=%d working=%04x front=%d rear=%d hull=%u hull_max=%u special=%d predicted=%d",
				(int)g_pai_context.object_index,
				(unsigned)g_cur_craft->working_subsystems,
				g_cur_craft->shield_energy[0],
				g_cur_craft->shield_energy[1],
				(unsigned)g_cur_craft->hull_damage,
				(unsigned)g_cur_craft->hull_max,
				(int)(g_cur_craft->craft_ordinal ==
				      g_mission_flight_groups
					      [g_pai_context
						       .craft_flight_group_index]
						      .fg.special_cargo_craft),
				g_flight_sim_side_effects_suppressed);
		}
		g_cur_craft->ai_flight.mission_aborted_flag = 1;
	}

	return (int16_t)abort_mission;
}

/* Order 12: returns 1 and makes this craft the flight group's leader when its
 * leader is gone: an empty slot, in another flight group, breaking up or
 * exploding, aborted, or flown by a player. This craft then has no leader and
 * takes the old leader's separation, waypoint_index and target, with its aim
 * point (in the modern build only when there is a target); every other craft of
 * the group in the active region's craft slots gets this craft as leader.
 * Returns 0 for a craft with no leader or a leader index at or past the end of
 * those slots. With the leader in place it returns 0, first setting
 * think_interval to 59 ticks when the leader runs enterhangarpln. */
// FUNCTION: XVT 0x467C50
int16_t paiorder_leaderdeadorder(void)
{
	uint8_t leader_object_index = g_cur_craft->leader_obj_idx;
	if (leader_object_index == UINT8_MAX) {
		return 0;
	}
	if (g_active_region_craft_object_slot_end <= (int)leader_object_index) {
		return 0;
	}
	struct object_record *leader_object =
		&g_object_table[leader_object_index];
	struct craft_data *leader_craft = leader_object->mobj->p_craft;
	struct ai_controller *leader_controller = &leader_craft->ai_controller;
	uint8_t leader_invalid = 0;
	if (leader_object->object_type == 0) {
		leader_invalid = 1;
	}
	if (g_object_table[g_pai_context.object_index].flight_group_idx !=
	    leader_object->flight_group_idx) {
		leader_invalid = 1;
	}
	if (leader_craft->object_kind == CRAFT_OBJECT_KIND_BREAKING_UP ||
	    leader_craft->object_kind == CRAFT_OBJECT_KIND_EXPLODING) {
		leader_invalid = 1;
	}
	if (leader_craft->ai_flight.mission_aborted_flag != 0) {
		leader_invalid = 1;
	}
	if (leader_object->player_owner_idx != -1) {
		leader_invalid = 1;
	}
	if (leader_invalid != 0) {
		for (uint16_t object_index =
			     (uint16_t)g_active_region_object_slot_start;
		     object_index < (int)g_active_region_craft_object_slot_end;
		     ++object_index) {
			struct object_record *object =
				&g_object_table[object_index];
			struct craft_data *craft = object->mobj->p_craft;
			if (object->object_type != 0 &&
			    object->flight_group_idx ==
				    g_pai_context.craft_flight_group_index) {
				struct ai_controller *controller =
					&craft->ai_controller;
				if (g_pai_context.object_index ==
				    object_index) {
					craft->leader_obj_idx = UINT8_MAX;
					craft->ai_flight.separation =
						leader_craft->ai_flight
							.separation;
					controller->waypoint_index =
						leader_controller
							->waypoint_index;
					controller->target_obj_idx =
						leader_controller
							->target_obj_idx;
					controller->target_signature =
						leader_controller
							->target_signature;
					controller->has_live_target =
						leader_controller
							->has_live_target;
#ifdef XVT_MODERN
					/* A leader without a target has no aim point to resolve. */
					if (controller->target_obj_idx !=
					    UINT16_MAX) {
						pai_update_aim_point_from_order_target();
					}
#else
					pai_update_aim_point_from_order_target();
#endif
				} else {
					craft->leader_obj_idx =
						(uint8_t)g_pai_context
							.object_index;
				}
			}
		}
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_INFO(
				"ai.leader_replaced object=%d fg=%d leader=%d tick=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.craft_flight_group_index,
				(int)leader_object_index, g_game_time);
		}
		XVT_LOG_DEBUG(
			"ai.leader_lost object=%d leader=%d empty=%d moved=%d wrecked=%d aborted=%d slot=%d target=%d waypoint=%d predicted=%d",
			(int)g_pai_context.object_index,
			(int)leader_object_index,
			(int)(leader_object->object_type == 0),
			(int)(g_object_table[g_pai_context.object_index]
				      .flight_group_idx !=
			      leader_object->flight_group_idx),
			(int)(leader_craft->object_kind ==
				      CRAFT_OBJECT_KIND_BREAKING_UP ||
			      leader_craft->object_kind ==
				      CRAFT_OBJECT_KIND_EXPLODING),
			(int)leader_craft->ai_flight.mission_aborted_flag,
			leader_object->player_owner_idx,
			(int)g_pai_context.controller->target_obj_idx,
			(int)g_pai_context.controller->waypoint_index,
			g_flight_sim_side_effects_suppressed);
	} else if (strcmp(g_plan_table[g_pai_context.leader_or_self_craft
					       ->ai_controller.running_plan_id]
				  .name,
			  "enterhangarpln") == 0) {
		if (g_pai_context.controller->think_interval != 59) {
			XVT_LOG_DEBUG(
				"ai.leader_docking object=%d leader=%d interval=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)leader_object_index,
				g_pai_context.controller->think_interval,
				g_flight_sim_side_effects_suppressed);
		}
		g_pai_context.controller->think_interval = 59;
	}
	return leader_invalid;
}

/* Order 16: when the craft is on its plan's maneuver and its recorded attacker
 * is in bearing class 2, switches at random to turn inside, zoom, scissors or
 * dive and runs paiman_initmaneuver. Returns 0 on every path. */
// FUNCTION: XVT 0x467E20
int16_t paiorder_ontailorder(void)
{
	uint16_t object_index = g_pai_context.object_index;
	if (g_pai_context.controller->maneuver_mode ==
		    g_pai_context.initial_maneuver_id &&
	    g_cur_craft->last_attacker_obj_idx != UINT16_MAX) {
		pai_object_ref_direction_to_object_ref(
			object_index, g_cur_craft->last_attacker_obj_idx);
		if (g_ai_threat_bearing_class_by_octant
			    [(uint16_t)(trig2_xyangle -
					g_object_table[object_index].yaw) >>
			     13] == 2) {
			int16_t random_maneuver = game_rand() & 3;
			uint8_t maneuver_mode;
			if (random_maneuver == 0) {
				maneuver_mode = AI_MANEUVER_MODE_TURN_INSIDE;
			} else if (random_maneuver == 1) {
				maneuver_mode = AI_MANEUVER_MODE_ZOOM;
			} else if (random_maneuver == 2) {
				maneuver_mode = AI_MANEUVER_MODE_SCISSORS;
			} else {
				maneuver_mode = AI_MANEUVER_MODE_DIVE;
			}
			g_pai_context.controller->maneuver_mode = maneuver_mode;
			XVT_LOG_DEBUG(
				"ai.tail_shaken object=%d attacker=%d distance=%d maneuver=%d predicted=%d",
				(int)object_index,
				(int)g_cur_craft->last_attacker_obj_idx,
				trig2_polardistance, (int)maneuver_mode,
				g_flight_sim_side_effects_suppressed);
			paiman_initmaneuver();
		}
	}
	return 0;
}

/* Order 17: returns 1. */
// FUNCTION: XVT 0x467EE0
int16_t paiorder_alwaysorder(void) { return 1; }

/* Order 19: returns 1 when the leader, or the craft itself when it has none,
 * runs flyhomepln or flyhomeevadepln; else 0. */
// FUNCTION: XVT 0x467EF0
int16_t paiorder_leadergohomeorder(void)
{
	struct ai_controller *leader_controller =
		&g_pai_context.leader_or_self_craft->ai_controller;

	return strcmp(g_plan_table[leader_controller->running_plan_id].name,
		      "flyhomepln") == 0 ||
	       strcmp(g_plan_table[leader_controller->running_plan_id].name,
		      "flyhomeevadepln") == 0;
}

/* Order 20. A follower that has not aborted and is not on flyhomeevadepln jumps
 * with its leader: when its model has a hyperdrive, its group leaves by
 * hyperspace (departure_method 0, or for a captured craft no departure by
 * mothership) and its leader is entering hyperspace, it switches straight to
 * intohyperspacepln with the into-hyperspace maneuver at maneuver_phase 1, a
 * maneuver timer of 2,360 ticks and a second one of 944, clears its roll, pitch
 * and turn states and push, sets its object_kind to entering hyperspace and sets
 * full power. Such a follower returns 0 either way. Any other craft returns 1
 * when its model has a hyperdrive and its group leaves by hyperspace; else
 * 0. */
// FUNCTION: XVT 0x467F60
int16_t paiorder_hyperspaceorder(void)
{
	if (g_cur_craft->leader_obj_idx != UINT8_MAX &&
	    g_cur_craft->ai_flight.mission_aborted_flag == 0 &&
	    strcmp(g_plan_table[g_pai_context.controller->running_plan_id].name,
		   "flyhomeevadepln") != 0) {
		int16_t can_enter_hyperspace = 0;
		if (g_model_defs[g_cur_craft->model_index].has_hyperdrive !=
		    0) {
			if (g_cur_craft->captured_by_flight_group == 0) {
				if (g_mission_flight_groups
					    [g_pai_context
						     .craft_flight_group_index]
						    .fg.departure_method == 0) {
					can_enter_hyperspace = 1;
				}
			} else if (
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.captured_depart_via_mothership ==
				0) {
				can_enter_hyperspace = 1;
			}
		}
		if (can_enter_hyperspace != 0 &&
		    g_pai_context.leader_or_self_craft->object_kind ==
			    CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE) {
			g_pai_context.controller->running_plan_id =
				(uint8_t)pai_find_plan_id_by_name_or_zero(
					"intohyperspacepln");
			g_cur_craft->object_kind =
				CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE;
			g_cur_craft->ai_flight.roll_state = 0;
			g_cur_craft->ai_flight.pitch_state = 0;
			g_cur_craft->ai_flight.turn_state = 0;
			g_pai_context.controller->maneuver_mode =
				AI_MANEUVER_MODE_INTO_HYPERSPACE;
			g_cur_craft->push_accum_x = 0;
			g_cur_craft->push_accum_y = g_cur_craft->push_accum_x;
			g_cur_craft->push_accum_z = g_cur_craft->push_accum_y;
			g_pai_context.controller->maneuver_phase = 1;
			g_pai_context.controller->secondary_maneuver_timer =
				944;
			g_pai_context.controller->maneuver_timer = 2360;
			paiman_setpower(g_pai_context.object_index, UINT16_MAX);
			XVT_LOG_DEBUG(
				"ai.jump_joined object=%d leader=%d plan=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_cur_craft->leader_obj_idx,
				(int)g_pai_context.controller->running_plan_id,
				g_flight_sim_side_effects_suppressed);
		}
	} else if (g_model_defs[g_cur_craft->model_index].has_hyperdrive != 0) {
		if (g_cur_craft->captured_by_flight_group == 0) {
			if (g_mission_flight_groups
				    [g_pai_context.craft_flight_group_index]
					    .fg.departure_method == 0) {
				return 1;
			}
		} else if (g_mission_flight_groups
				   [g_pai_context.craft_flight_group_index]
					   .fg.captured_depart_via_mothership ==
			   0) {
			return 1;
		}
	}
	return 0;
}

/* Order 22: returns 1 when the craft's target, or its leader's when it has one,
 * is mission point 13 (0x800D); else 0, and always 0 for a platform. */
// FUNCTION: XVT 0x468180
int16_t paiorder_mothershiporder(void)
{
	if (g_object_table[g_pai_context.object_index].genus_id ==
	    CRAFT_GENUS_PLATFORM) {
		return 0;
	}
	if (g_cur_craft->leader_obj_idx == UINT8_MAX) {
		return g_pai_context.controller->target_obj_idx == 0x800Du;
	}
	return g_pai_context.leader_or_self_craft->ai_controller
		       .target_obj_idx == 0x800Du;
}

/* Order 24: returns 1 after setting a boarding target, with its signature and
 * has_live_target 1: the candidate target when there is one (not 0xFFFF or
 * AI_TARGET_ABORT) and it can be targeted, else what
 * pai_find_boarding_target_from_order finds for the current order slot. Clears a
 * candidate that cannot be targeted. Returns 0 when there is none. */
// FUNCTION: XVT 0x4681E0
int16_t paiorder_lookforcrafttoboardorder(void)
{
	uint16_t candidate_target_idx =
		g_pai_context.controller->candidate_target_idx;
	if (candidate_target_idx != UINT16_MAX &&
	    candidate_target_idx != AI_TARGET_ABORT) {
		if (pai_is_object_targetable(candidate_target_idx) != 0) {
			g_pai_context.controller->target_obj_idx =
				candidate_target_idx;
			g_pai_context.controller->target_signature =
				g_object_table[candidate_target_idx]
					.object_signature;
			g_pai_context.controller->has_live_target = 1;
			XVT_LOG_DEBUG(
				"ai.boarding_target object=%d target=%d source=\"command\" predicted=%d",
				(int)g_pai_context.object_index,
				(int)candidate_target_idx,
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
	candidate_target_idx = (uint16_t)pai_find_boarding_target_from_order(
		g_pai_context.order_slot);
	if (candidate_target_idx != UINT16_MAX) {
		g_pai_context.controller->target_obj_idx = candidate_target_idx;
		g_pai_context.controller->target_signature =
			g_object_table[candidate_target_idx].object_signature;
		g_pai_context.controller->has_live_target = 1;
		XVT_LOG_DEBUG(
			"ai.boarding_target object=%d target=%d source=\"search\" predicted=%d",
			(int)g_pai_context.object_index,
			(int)candidate_target_idx,
			g_flight_sim_side_effects_suppressed);
		return 1;
	}
	return 0;
}

/* Order 25: returns 1 and gives up boarding when, with maneuver_phase below 3,
 * the target's slot is empty, its mobile object family is 5, its signature
 * changed or the craft has no working subsystems, or, at maneuver_phase 1 or 2,
 * a player flies the target and it moves. Giving up clears the push, targets
 * the group's current mission point and sets the aim point there. Else 0. Does
 * not check target_obj_idx for 0xFFFF. */
// FUNCTION: XVT 0x4683F0
int16_t paiorder_abortboardorder(void)
{
	int16_t should_abort = 0;
	uint8_t maneuver_phase = g_pai_context.controller->maneuver_phase;
	uint16_t target_obj_idx;
	struct object_record *target;
	if (maneuver_phase < 3) {
		target_obj_idx = g_pai_context.controller->target_obj_idx;
		target = &g_object_table[target_obj_idx];
		if (target->mobj != NULL) {
			if (target->object_type == 0) {
				should_abort = 1;
			}
			if (target->mobj->family == 5) {
				should_abort = 1;
			}
		} else if (target->object_type == 0) {
			should_abort = 1;
		}
		if (g_cur_craft->working_subsystems == 0) {
			should_abort = 1;
		}
		if (g_pai_context.controller->target_signature !=
		    target->object_signature) {
			should_abort = 1;
		}
	}

	if (maneuver_phase == 1 || maneuver_phase == 2) {
		target = &g_object_table[target_obj_idx];
		if (target->player_owner_idx != -1 &&
		    target->mobj->speed != 0) {
			should_abort = 1;
		}
	}

	if (should_abort) {
		XVT_LOG_DEBUG(
			"ai.boarding_abandoned object=%d target=%d phase=%d working=%04x predicted=%d",
			(int)g_pai_context.object_index,
			(int)g_pai_context.controller->target_obj_idx,
			(int)maneuver_phase,
			(unsigned)g_cur_craft->working_subsystems,
			g_flight_sim_side_effects_suppressed);
		g_cur_craft->push_accum_z = 0;
		g_cur_craft->push_accum_y = g_cur_craft->push_accum_z;
		g_cur_craft->push_accum_x = g_cur_craft->push_accum_y;
		g_pai_context.controller->target_obj_idx = 0x8000;
		g_pai_context.controller->target_signature = 0;
		g_pai_context.controller->has_live_target = 0;
		pai_update_aim_point_from_order_target();
		return 1;
	}
	return 0;
}

/* Order 26: returns 1 when the craft's live position is within 0x4000 world
 * units of its aim point, else 0. Sets the trig2_ globals. */
// FUNCTION: XVT 0x468520
int16_t paiorder_returnboardorder(void)
{
	pai_calc_angles_to_aim_point();
	return trig2_polardistance < 0x4000;
}

/* Order 27: counts a boarding. When boarding_state is 2 or 3 it adds 1 to the
 * current order slot's goal_progress; once times_boarded reaches the order's
 * variable1 it restores the working subsystems and clears subsystem_damage,
 * emitting "has been repaired" when they are equal on disabledpln; while
 * times_boarded is below variable1 it sets boarding_state to 0 instead. Returns 0
 * on every path. */
// FUNCTION: XVT 0x468540
int16_t paiorder_awaitboardorder(void)
{
	uint8_t boarding_state = g_cur_craft->boarding_state;
	if (boarding_state == 2 || boarding_state == 3) {
		++g_pai_context.controller->order_progress
			  .goal_progress[g_pai_context.order_slot];
		XVT_LOG_DEBUG(
			"ai.boarding_counted object=%d order=%d progress=%d boarded=%d needed=%d state=%d predicted=%d",
			(int)g_pai_context.object_index,
			(int)g_pai_context.order_slot,
			(int)g_pai_context.controller->order_progress
				.goal_progress[g_pai_context.order_slot],
			(int)g_cur_craft->ai_flight.times_boarded,
			(int)g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[g_pai_context.order_slot]
					.variable1,
			(int)boarding_state,
			g_flight_sim_side_effects_suppressed);
		if (g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[g_pai_context.order_slot]
			    .variable1 <=
		    g_cur_craft->ai_flight.times_boarded) {
			g_cur_craft->working_subsystems =
				g_cur_craft->system_flags;
			g_cur_craft->subsystem_damage = 0;
			if (g_mission_flight_groups
					    [g_pai_context
						     .craft_flight_group_index]
						    .fg
						    .orders[g_pai_context
								    .order_slot]
						    .variable1 ==
				    g_cur_craft->ai_flight.times_boarded &&
			    strcmp(g_plan_table[g_pai_context.controller
							->current_plan_id]
					   .name,
				   "disabledpln") == 0) {
				msg_emit_craft_message(
					g_pai_context.object_index, g_cur_craft,
					IFMSG_138_HAS_BEEN_REPAIRED);
				return 0;
			}
		} else {
			g_cur_craft->boarding_state = 0;
		}
	}
	return 0;
}

/* Order 28: disables the craft, setting working_subsystems to 0, and returns
 * 0. */
// FUNCTION: XVT 0x468670
int16_t paiorder_makedisabledorder(void)
{
	if (g_cur_craft->working_subsystems != 0) {
		XVT_LOG_DEBUG(
			"ai.self_disabled object=%d working=%04x predicted=%d",
			(int)g_pai_context.object_index,
			(unsigned)g_cur_craft->working_subsystems,
			g_flight_sim_side_effects_suppressed);
	}
	g_cur_craft->working_subsystems = 0;
	return 0;
}

/* Order 29: returns 1 when the craft's live position is within 0x4000 world
 * units of its aim point, else 0. Sets the trig2_ globals. */
// FUNCTION: XVT 0x468690
int16_t paiorder_neartargetorder(void)
{
	pai_calc_angles_to_aim_point();
	return trig2_polardistance < 0x4000;
}

/* Order 30: returns 1 when a warhead launcher of the craft holds a warhead of
 * the class its target calls for and has a round left, else 0. A freighter,
 * starship or platform target in the craft slots calls for class 2, and so does
 * a transport in a version 14 mission; any other target calls for class 1. In a
 * version 14 mission the magnetic pulse and type 153 also count as class 2.
 * Does not check for an empty launcher's type 0. */
// FUNCTION: XVT 0x4686B0
int16_t paiorder_rocketsonboardorder(void)
{
	unsigned int target_obj_idx = g_pai_context.controller->target_obj_idx;
	uint16_t required_warhead_class;
	if (g_active_region_craft_object_slot_end > (int)target_obj_idx) {
		int16_t target_genus = g_object_table[target_obj_idx].genus_id;
		if (target_genus == 3 || target_genus == 5 ||
		    target_genus == 4 ||
		    (target_genus == 1 && g_mission_file_version == 14)) {
			required_warhead_class = 2;
		} else {
			required_warhead_class = 1;
		}
	} else {
		required_warhead_class = 1;
	}

	uint16_t matches_required_class;
	for (uint16_t launcher_index = 0;
	     launcher_index < g_cur_craft->warhead_launcher_count;
	     ++launcher_index) {
		uint8_t projectile_type =
			g_cur_craft->warhead_slot_type_ids[launcher_index];
		if (g_projectile_type_data
			    .warhead_class[projectile_type -
					   PROJECTILE_OBJECT_TYPE_FIRST] !=
		    required_warhead_class) {
			if (g_mission_file_version == 14 &&
			    (projectile_type ==
				     WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE ||
			     projectile_type == 153)) {
				matches_required_class = 1;
				if (required_warhead_class != 2) {
					matches_required_class = 0;
				}
			} else {
				matches_required_class = 0;
			}
			if (!matches_required_class) {
				continue;
			}
		}
		uint16_t last_weapon_slot =
			g_model_defs[g_cur_craft->model_index]
				.warhead_launcher_last_slot[launcher_index];
		uint16_t weapon_slot =
			g_model_defs[g_cur_craft->model_index]
				.warhead_launcher_first_slot[launcher_index];
		while (weapon_slot <= last_weapon_slot) {
			if (g_cur_craft->weapon_slots[weapon_slot].ammo_count !=
			    0) {
				return 1;
			}
			++weapon_slot;
		}
	}

	return 0;
}

/* Order 31: for a craft not a freighter or starship, still on its plan's
 * maneuver, dodges threats; returns 0 on every path. With no attacker known it
 * looks for a homing warhead aimed at it within g_ai_warhead_threat_range_by_skill
 * (three times that for a concussion missile or type 149). For one it records
 * it as last_attacker_obj_idx, turns inside in bearing class 0 or avoids the
 * attacker otherwise, then with countermeasure rounds adds 10 to an empty
 * chaff_active_seconds and uses a round (none when the flight group's status1 or
 * status2 is 21) for chaff with a working countermeasure system, or fires a
 * flare when cm_fire_cooldown_timer is 0, and returns. Next it looks, while the
 * craft is active, for an enemy starfighter within
 * g_ai_attacker_search_range_by_skill pointed within 0x2000 angle units of it. With
 * an attacker known, a craft not a utility vehicle whose front shield is below
 * 500 with shields working, or whose hull_damage has reached
 * system_damage_hull_threshold, fires a flare when the attacker is within 0x8000,
 * and when a player flies the attacker switches to avoiding it at full
 * power. */
// FUNCTION: XVT 0x468820
int16_t paiorder_avoidhitorder(void)
{
	uint8_t genus_id = g_object_table[g_pai_context.object_index].genus_id;

	if (genus_id == CRAFT_GENUS_FREIGHTER ||
	    genus_id == CRAFT_GENUS_STARSHIP) {
		return 0;
	}

	if (g_pai_context.controller->maneuver_mode ==
	    g_pai_context.initial_maneuver_id) {
		if (g_cur_craft->last_attacker_obj_idx == UINT16_MAX) {
			int max_range_score = g_ai_warhead_threat_range_by_skill
				[g_pai_context.skill_tier];
			uint16_t object_idx;
			for (object_idx =
				     (uint16_t)g_projectile_object_slot_start;
			     object_idx < g_projectile_object_slot_end;
			     ++object_idx) {
				if (g_object_table[object_idx].object_type !=
					    0 &&
				    g_object_table[object_idx]
						    .mobj->p_warhead_guidance
						    ->homing_tier != 0 &&
				    g_object_table[object_idx]
						    .mobj->p_warhead_guidance
						    ->target_obj_idx ==
					    g_pai_context.object_index) {
					if (pai_is_object_within_range_of_craft(
						    object_idx,
						    (unsigned int)((g_object_table[object_idx]
										    .object_type ==
									    WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE ||
								    g_object_table[object_idx]
										    .object_type ==
									    149)
									   ? max_range_score *
										     3
									   : max_range_score)) ==
					    1) {
						g_cur_craft
							->last_attacker_obj_idx =
							object_idx;
						pai_object_ref_direction_to_object_ref(
							g_pai_context
								.object_index,
							object_idx);
						if (g_ai_threat_bearing_class_by_octant
							    [(uint16_t)(trig2_xyangle -
									g_object_table[g_pai_context
											       .object_index]
										.yaw) >>
							     13] == 0) {
							g_pai_context
								.controller
								->maneuver_mode =
								AI_MANEUVER_MODE_TURN_INSIDE;
						} else {
							g_pai_context
								.controller
								->maneuver_mode =
								AI_MANEUVER_MODE_AVOID_ATTACKER;
						}
						paiman_initmaneuver();
						if (g_cur_craft
							    ->cm_ammo_count !=
						    0) {
							if (g_cur_craft->cm_type_id ==
								    COUNTERMEASURE_TYPE_CHAFF &&
							    (g_cur_craft
								     ->working_subsystems &
							     CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES) !=
								    0) {
								if (g_cur_craft
									    ->chaff_active_seconds ==
								    0) {
									g_cur_craft
										->chaff_active_seconds +=
										10;
									if (g_mission_flight_groups[g_pai_context
													    .craft_flight_group_index]
											    .fg
											    .status1 !=
										    21 &&
									    g_mission_flight_groups[g_pai_context
													    .craft_flight_group_index]
											    .fg
											    .status2 !=
										    21) {
										--g_cur_craft
											  ->cm_ammo_count;
									}
								}
							} else if (
								g_cur_craft->cm_type_id ==
									COUNTERMEASURE_TYPE_FLARE &&
								g_cur_craft->cm_fire_cooldown_timer ==
									0) {
								laser_createcountermeasureprojectile(
									g_pai_context
										.object_index,
									COUNTERMEASURE_PROJECTILE_OBJECT_TYPE);
							}
						}
						XVT_LOG_DEBUG(
							"ai.warhead_evaded object=%d warhead=%d type=%d via=\"avoid_hit\" maneuver=%d cm=%d ammo=%d seconds=%u predicted=%d",
							(int)g_pai_context
								.object_index,
							(int)object_idx,
							(int)g_object_table
								[object_idx]
									.object_type,
							(int)g_pai_context
								.controller
								->maneuver_mode,
							(int)g_cur_craft
								->cm_type_id,
							(int)g_cur_craft
								->cm_ammo_count,
							(unsigned)g_cur_craft
								->chaff_active_seconds,
							g_flight_sim_side_effects_suppressed);
						return 0;
					}
				}
			}
			max_range_score = g_ai_attacker_search_range_by_skill
				[g_pai_context.skill_tier];
			if (g_cur_craft->last_attacker_obj_idx == UINT16_MAX) {
				for (object_idx = (uint16_t)
					     g_active_region_object_slot_start;
				     object_idx <
				     g_active_region_craft_object_slot_end;
				     ++object_idx) {
					if (g_object_table[object_idx]
						    .object_type == 0) {
						continue;
					}
					int object_team =
						g_mission_flight_groups
							[g_object_table[object_idx]
								 .flight_group_idx]
								.fg.team;
					int source_team =
						g_object_table
							[g_pai_context
								 .object_index]
								.mobj->team;
					int is_hostile =
						source_team == object_team
							? 0
							: g_mission_teams[source_team]
									  .allies[object_team] ==
								  0;
					if (is_hostile &&
					    g_cur_craft->object_kind ==
						    CRAFT_OBJECT_KIND_ACTIVE &&
					    g_object_table[object_idx]
							    .genus_id ==
						    CRAFT_GENUS_STARFIGHTER &&
					    pai_is_object_within_range_of_craft(
						    object_idx,
						    (unsigned int)
							    max_range_score) ==
						    1) {
						pai_object_ref_direction_to_object_ref(
							object_idx,
							g_pai_context
								.object_index);
						uint16_t horizontal_angle =
							(uint16_t)(trig2_xyangle -
								   g_object_table[object_idx]
									   .yaw);
						if (horizontal_angle >=
						    0x8000) {
							horizontal_angle =
								(uint16_t)-horizontal_angle;
						}
						uint16_t vertical_angle =
							(uint16_t)(trig2_pitch -
								   g_object_table[object_idx]
									   .pitch);
						if (vertical_angle >= 0x8000) {
							vertical_angle =
								(uint16_t)-vertical_angle;
						}
						if (horizontal_angle < 0x2000 &&
						    vertical_angle < 0x2000) {
							g_cur_craft
								->last_attacker_obj_idx =
								object_idx;
							break;
						}
					}
				}
			}
		}
		if (g_cur_craft->last_attacker_obj_idx != UINT16_MAX &&
		    g_object_table[g_pai_context.object_index].genus_id !=
			    CRAFT_GENUS_UTILITY_VEHICLE &&
		    (((g_cur_craft->working_subsystems &
		       CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0 &&
		      g_cur_craft->shield_energy[0] < 500) ||
		     g_cur_craft->hull_damage >=
			     g_cur_craft->system_damage_hull_threshold)) {
			pai_object_ref_direction_to_object_ref(
				g_cur_craft->last_attacker_obj_idx,
				g_pai_context.object_index);
			if (trig2_polardistance < 0x8000 &&
			    g_cur_craft->cm_type_id ==
				    COUNTERMEASURE_TYPE_FLARE &&
			    g_cur_craft->cm_ammo_count != 0 &&
			    g_cur_craft->cm_fire_cooldown_timer == 0) {
				laser_createcountermeasureprojectile(
					g_pai_context.object_index,
					COUNTERMEASURE_PROJECTILE_OBJECT_TYPE);
			}
			if (g_object_table[g_cur_craft->last_attacker_obj_idx]
				    .player_owner_idx != -1) {
				g_pai_context.controller->maneuver_mode =
					AI_MANEUVER_MODE_AVOID_ATTACKER;
				XVT_LOG_DEBUG(
					"ai.attacker_avoided object=%d attacker=%d slot=%d front=%d hull=%u predicted=%d",
					(int)g_pai_context.object_index,
					(int)g_cur_craft->last_attacker_obj_idx,
					g_object_table
						[g_cur_craft
							 ->last_attacker_obj_idx]
							.player_owner_idx,
					g_cur_craft->shield_energy[0],
					(unsigned)g_cur_craft->hull_damage,
					g_flight_sim_side_effects_suppressed);
				paiman_initmaneuver();
				paiman_setpower(g_pai_context.object_index,
						UINT16_MAX);
			}
		}
	}

	return 0;
}

/* Order 32: returns 1 when every other flight group that departs to this
 * craft's group as its mothership has arrived, has waves_remaining 0 and has no
 * craft left in the active region's craft slots; else 0. A group whose
 * arrival_enabled is 0 and that no player owns is left out. */
// FUNCTION: XVT 0x468CF0
int16_t paiorder_waitforallreturnorder(void)
{
	uint16_t flight_group_index = 0;
	if ((int16_t)g_mission_header.num_flight_groups > 0) {
		do {
			if (!((g_mission_fg_stats[flight_group_index]
					       .arrival_enabled == 0 &&
			       g_mission_flight_groups[flight_group_index]
					       .player_owner_idx == -1) ||
			      g_pai_context.craft_flight_group_index ==
				      flight_group_index ||
			      g_mission_flight_groups[flight_group_index]
					      .fg.departure_method == 0 ||
			      g_mission_flight_groups[flight_group_index]
					      .fg.departure_mothership !=
				      g_pai_context.craft_flight_group_index)) {
				if (g_mission_fg_stats[flight_group_index]
						    .has_arrived == 0 ||
				    g_mission_fg_stats[flight_group_index]
						    .waves_remaining != 0) {
					return 0;
				}
				for (uint16_t object_index =
					     g_active_region_object_slot_start;
				     object_index <
				     g_active_region_craft_object_slot_end;
				     ++object_index) {
					struct object_record *object =
						&g_object_table[object_index];
					if (object->object_type != 0 &&
					    object->flight_group_idx ==
						    flight_group_index) {
						return 0;
					}
				}
			}
			++flight_group_index;
		} while ((int16_t)g_mission_header.num_flight_groups >
			 (int)flight_group_index);
	}
	return 1;
}

/* Order 33: returns 1 when every other flight group that arrives from this
 * craft's group as its mothership has arrived with waves_remaining 0; else 0. A
 * group whose arrival_enabled is 0 and that no player owns is left out. */
// FUNCTION: XVT 0x468E40
int16_t paiorder_waitforallcreateorder(void)
{
	uint16_t flight_group_index = 0;
	if ((int16_t)g_mission_header.num_flight_groups > 0) {
		do {
			if (!((g_mission_fg_stats[flight_group_index]
					       .arrival_enabled == 0 &&
			       g_mission_flight_groups[flight_group_index]
					       .player_owner_idx == -1) ||
			      g_pai_context.craft_flight_group_index ==
				      flight_group_index ||
			      g_mission_flight_groups[flight_group_index]
					      .fg.arrival_method == 0 ||
			      g_mission_flight_groups[flight_group_index]
					      .fg.arrival_mothership !=
				      g_pai_context.craft_flight_group_index ||
			      (g_mission_fg_stats[flight_group_index]
					       .has_arrived != 0 &&
			       g_mission_fg_stats[flight_group_index]
					       .waves_remaining == 0))) {
				return 0;
			}
			++flight_group_index;
		} while ((int16_t)g_mission_header.num_flight_groups >
			 (int)flight_group_index);
	}

	return 1;
}

/* Order 34: returns 1 and drops both target and candidate (0xFFFF) when the
 * craft is on its plan's maneuver and its candidate target is AI_TARGET_ABORT;
 * else 0. */
// FUNCTION: XVT 0x468F20
int16_t paiorder_evasiveorder(void)
{
	if (g_pai_context.controller->maneuver_mode !=
		    g_pai_context.initial_maneuver_id ||
	    g_pai_context.controller->candidate_target_idx != AI_TARGET_ABORT) {
		return 0;
	}
	XVT_LOG_DEBUG(
		"ai.target_dropped object=%d target=%d reason=\"evade\" predicted=%d",
		(int)g_pai_context.object_index,
		(int)g_pai_context.controller->target_obj_idx,
		g_flight_sim_side_effects_suppressed);

	g_pai_context.controller->target_obj_idx = 0xffff;
	g_pai_context.controller->target_signature = 0;
	g_pai_context.controller->has_live_target = 0;
	g_pai_context.controller->candidate_target_idx = 0xffff;

	return 1;
}

/* Order 35: makes the candidate target the craft's target, with its signature
 * and has_live_target 1, when it names an object (not 0xFFFF or AI_TARGET_ABORT)
 * that can be targeted and is not the target already; clears a candidate that
 * cannot be targeted. Returns 0 on every path. */
// FUNCTION: XVT 0x468F80
int16_t paiorder_targetfromplayerorder(void)
{
	uint16_t candidate_target_idx =
		g_pai_context.controller->candidate_target_idx;
	if (candidate_target_idx == UINT16_MAX ||
	    candidate_target_idx == AI_TARGET_ABORT) {
		return 0;
	}
	unsigned int object_index =
		g_pai_context.controller->candidate_target_idx;
	int valid_target = pai_is_object_targetable(object_index);
	if (valid_target != 0) {
		candidate_target_idx =
			g_pai_context.controller->candidate_target_idx;
		if (g_pai_context.controller->target_obj_idx ==
		    candidate_target_idx) {
			return 0;
		}
		XVT_LOG_DEBUG(
			"ai.command_target_taken object=%d target=%d previous=%d predicted=%d",
			(int)g_pai_context.object_index,
			(int)candidate_target_idx,
			(int)g_pai_context.controller->target_obj_idx,
			g_flight_sim_side_effects_suppressed);
		g_pai_context.controller->target_obj_idx = candidate_target_idx;
		g_pai_context.controller->target_signature =
			g_object_table[g_pai_context.controller->target_obj_idx]
				.object_signature;
		g_pai_context.controller->has_live_target = 1;
		return 0;
	}
	g_pai_context.controller->candidate_target_idx = UINT16_MAX;
	XVT_LOG_DEBUG(
		"ai.command_target_dropped object=%d target=%d predicted=%d",
		(int)g_pai_context.object_index, (int)object_index,
		g_flight_sim_side_effects_suppressed);
	return 0;
}

/* Order 36. Off the avoid-starship maneuver, it asks
 * collide_craftstarshipcollision whether the craft will hit something within 6
 * simulated seconds, restoring g_cur_craft after. When it will, and the object
 * is neither the craft nor what it carries, nor the target it is attacking or
 * rocket attacking (a Calamari cruiser or Imperial Star Destroyer target still
 * counts), it sets target_xy_angle a quarter turn off its yaw (plus for an odd
 * craft_ordinal, minus for an even one) and target_z_angle a quarter turn off its
 * pitch (minus when the old target_z_angle is above 0x4000, else plus), and
 * switches to the avoid-starship maneuver; for an object at or past the end of
 * the region's main slots it sets secondary_maneuver_timer to 3 to 6 times
 * SIMULATION_TICKS_PER_SECOND at random. Returns 0 there. On the avoid-starship
 * maneuver it returns 1 once secondary_maneuver_timer has run out, else 0. */
// FUNCTION: XVT 0x469150
int16_t paiorder_avoidstarshiporder(void)
{
	enum {
		COLLISION_LOOKAHEAD_SECONDS = 6,
		QUARTER_TURN = 0x4000,
		MIN_AVOIDANCE_SECONDS = 3,
		AVOIDANCE_DURATION_MASK = 3,
	};

	int16_t maneuver_mode = g_pai_context.controller->maneuver_mode;
	if (maneuver_mode != AI_MANEUVER_MODE_AVOID_STARSHIP) {
		int16_t source_object_index = g_pai_context.object_index;

		struct craft_data *saved_craft = g_cur_craft;
		uint16_t collision_object_index =
			collide_craftstarshipcollision(
				source_object_index,
				COLLISION_LOOKAHEAD_SECONDS);
		g_cur_craft = saved_craft;
		if (collision_object_index != UINT16_MAX) {
			if (g_pai_context.controller->target_obj_idx ==
				    collision_object_index &&
			    (maneuver_mode == AI_MANEUVER_MODE_ATTACK ||
			     maneuver_mode == AI_MANEUVER_MODE_ROCKET_ATTACK)) {
				uint8_t object_type =
					g_object_table[collision_object_index]
						.object_type;
				if (object_type !=
					    CRAFT_SPECIES_CALAMARI_CRUISER &&
				    object_type !=
					    CRAFT_SPECIES_IMPERIAL_STAR_DESTROYER) {
					return 0;
				}
			}
			if (collision_object_index !=
				    g_pai_context.object_index &&
			    saved_craft->carried_object_index !=
				    collision_object_index) {
				if ((saved_craft->craft_ordinal & 1) != 0) {
					g_pai_context.controller
						->target_xy_angle =
						(uint16_t)(g_object_table
								   [g_pai_context
									    .object_index]
									   .yaw +
							   QUARTER_TURN);
				} else {
					g_pai_context.controller
						->target_xy_angle =
						(uint16_t)(g_object_table
								   [g_pai_context
									    .object_index]
									   .yaw -
							   QUARTER_TURN);
				}

				if (g_pai_context.controller->target_z_angle >
				    QUARTER_TURN) {
					g_pai_context.controller
						->target_z_angle =
						(uint16_t)(g_object_table
								   [g_pai_context
									    .object_index]
									   .pitch -
							   QUARTER_TURN);
				} else {
					g_pai_context.controller
						->target_z_angle =
						(uint16_t)(g_object_table
								   [g_pai_context
									    .object_index]
									   .pitch +
							   QUARTER_TURN);
				}

				g_pai_context.controller->maneuver_mode =
					AI_MANEUVER_MODE_AVOID_STARSHIP;
				paiman_initmaneuver();
				if (collision_object_index >=
				    g_region_main_object_slot_end) {
					g_pai_context.controller
						->secondary_maneuver_timer =
						SIMULATION_TICKS_PER_SECOND *
						((game_rand() &
						  AVOIDANCE_DURATION_MASK) +
						 MIN_AVOIDANCE_SECONDS);
				}
				XVT_LOG_DEBUG(
					"ai.obstacle_avoided object=%d obstacle=%d heading=%u pitch=%u timer=%d predicted=%d",
					(int)g_pai_context.object_index,
					(int)collision_object_index,
					(unsigned)g_pai_context.controller
						->target_xy_angle,
					(unsigned)g_pai_context.controller
						->target_z_angle,
					(int)g_pai_context.controller
						->secondary_maneuver_timer,
					g_flight_sim_side_effects_suppressed);
			} else {
				return 0;
			}
		}
	} else if (g_pai_context.controller->secondary_maneuver_timer == 0) {
		return 1;
	}
	return 0;
}

/* Order 37: returns 1 when the flight group's departure_method is nonzero, which
 * paiorder_flyhomeorder takes as a departure by mothership, else 0. */
// FUNCTION: XVT 0x469310
int16_t paiorder_checkhyperorder(void)
{
	return g_mission_flight_groups[g_pai_context.craft_flight_group_index]
		       .fg.departure_method != 0;
}

/* Order 38: returns 1 when the flight group's departure is due for the craft,
 * else 0; 0 at once when max_speed_cache is 0. The departure starts when the
 * mission clock's minutes and seconds reach the group's departure time, or when
 * its departure trigger pair has a condition and holds: the craft then records
 * the clock in depart_clock_hours, depart_clock_minutes and depart_clock_seconds,
 * counts the not-departed outcome and sets depart_timer_flag. Once the group's
 * departure delay has passed since then, it has the tactical officer announce
 * the withdrawal, sends message 385 or 386 to the local player, restores
 * working subsystems when the current order's leader plan is waitforboardpln
 * and subsystem_damage is 0, and returns 1. */
// FUNCTION: XVT 0x469340
int16_t paiorder_stopgohomeorder(void)
{
	enum {
		DEPART_TIMER_ACTIVE = 1,
		SECONDS_PER_MINUTE = 60,
		MINUTES_PER_HOUR = 60,
		MESSAGE_MODEL_SLOT = 0,
		MESSAGE_FLIGHT_GROUP_SLOT = 1,
	};

	if (g_cur_craft->ai_flight.max_speed_cache == 0) {
		return 0;
	}

	int depart_now = 0;
	uint8_t departure_clock_seconds =
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.departure_clock_sec;
	uint8_t departure_clock_minutes =
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.departure_clock_min;
	if (departure_clock_seconds + departure_clock_minutes != 0) {
		if (g_mission_elapsed_clock.minutes > departure_clock_minutes) {
			depart_now = 1;
		} else if (g_mission_elapsed_clock.minutes ==
				   departure_clock_minutes &&
			   g_mission_elapsed_clock.seconds >=
				   departure_clock_seconds) {
			depart_now = 1;
		}
	}

	if (g_cur_craft->ai_flight.depart_timer_flag == 0) {
		if ((g_mission_flight_groups[g_pai_context
						     .craft_flight_group_index]
				     .fg.departure_trigger.triggers[0]
				     .condition != 0 ||
		     g_mission_flight_groups[g_pai_context
						     .craft_flight_group_index]
				     .fg.departure_trigger.triggers[1]
				     .condition != 0) &&
		    (mission_evaluate_trigger_pair(
			     &g_mission_flight_groups
				      [g_pai_context.craft_flight_group_index]
					      .fg.departure_trigger,
			     0) &
		     1) != 0) {
			depart_now = 1;
		}
		if (depart_now == 1) {
			g_cur_craft->ai_flight.depart_clock_hours =
				g_mission_elapsed_clock.hours;
			g_cur_craft->ai_flight.depart_clock_minutes =
				g_mission_elapsed_clock.minutes;
			g_cur_craft->ai_flight.depart_clock_seconds =
				g_mission_elapsed_clock.seconds;
			uint8_t *depart_timer_flag =
				&g_cur_craft->ai_flight.depart_timer_flag;
			if (*depart_timer_flag == 0) {
				uint16_t flight_group_index =
					g_pai_context.craft_flight_group_index;
				++g_mission_fg_stats[flight_group_index].outcome_count
					  [FLIGHT_GROUP_OUTCOME_NOT_DEPARTED];
				if (g_mission_flight_groups[flight_group_index]
					    .fg.special_cargo_craft ==
				    g_cur_craft->craft_ordinal) {
					g_mission_fg_stats[flight_group_index].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] =
						1;
				}
			}
			*depart_timer_flag = DEPART_TIMER_ACTIVE;
			XVT_LOG_DEBUG(
				"ai.departure_started object=%d fg=%d from=\"own\" count=%u minutes=%d seconds=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.craft_flight_group_index,
				(unsigned)g_mission_fg_stats
					[g_pai_context.craft_flight_group_index]
						.outcome_count
							[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED],
				(int)g_mission_elapsed_clock.minutes,
				(int)g_mission_elapsed_clock.seconds,
				g_flight_sim_side_effects_suppressed);
		}
	}

	if (g_cur_craft->ai_flight.depart_timer_flag == DEPART_TIMER_ACTIVE) {
		unsigned int departure_delay_seconds =
			SECONDS_PER_MINUTE *
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg.departure_delay_minutes +
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.departure_delay_seconds;
		unsigned int elapsed_seconds =
			SECONDS_PER_MINUTE *
				(g_mission_elapsed_clock.minutes +
				 MINUTES_PER_HOUR *
					 (g_mission_elapsed_clock.hours -
					  g_cur_craft->ai_flight
						  .depart_clock_hours) -
				 g_cur_craft->ai_flight.depart_clock_minutes) -
			g_cur_craft->ai_flight.depart_clock_seconds +
			g_mission_elapsed_clock.seconds;
		if (departure_delay_seconds == 0 ||
		    elapsed_seconds >= departure_delay_seconds) {
			fsfx_speak_tactical_officer_event(
				TACTICAL_VOICE_STATUS, TACTICAL_MSG_WITHDRAWING,
				g_pai_context.object_index, UINT16_MAX);
			g_msg_sender_iff =
				(uint8_t)g_object_table[g_pai_context
								.object_index]
					.mobj->iff;
			msg_add_message_ptr(
				MESSAGE_MODEL_SLOT,
				&g_model_defs[g_cur_craft->model_index]);
			msg_add_message_ptr(
				MESSAGE_FLIGHT_GROUP_SLOT,
				&g_mission_flight_groups
					[g_pai_context
						 .craft_flight_group_index]);
			if (hud_mission_fg_get_craft_number_if_shown(
				    g_pai_context.craft_flight_group_index,
				    g_cur_craft) == 0) {
				msg_emit_in_flight_message(
					IFMSG_385_ARG_ARG_WITHDRAWING_FROM_COMBAT_AREA,
					g_local_player);
			} else {
				msg_emit_in_flight_message(
					IFMSG_386_FLIGHT_GROUP_ARG_ARG_WITHDRAWING_FROM_COMBAT_AREA,
					g_local_player);
			}

			uint16_t order =
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.order;
			uint8_t plan_id = g_builtin_plan_id_by_name_index
				[g_order_leader_builtin_plan_name_index[order]];
			if (strcmp(g_plan_table[plan_id].name,
				   "waitforboardpln") == 0 &&
			    g_cur_craft->subsystem_damage == 0) {
				g_cur_craft->working_subsystems =
					g_cur_craft->system_flags;
			}
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_INFO(
					"ai.withdrew object=%d fg=%d tick=%d",
					(int)g_pai_context.object_index,
					(int)g_pai_context
						.craft_flight_group_index,
					g_game_time);
			}
			XVT_LOG_DEBUG(
				"ai.withdraw_timing object=%d delay=%u elapsed=%u working=%04x predicted=%d",
				(int)g_pai_context.object_index,
				departure_delay_seconds, elapsed_seconds,
				(unsigned)g_cur_craft->working_subsystems,
				g_flight_sim_side_effects_suppressed);
			return 1;
		}
	}

	return 0;
}

/* Order 39: marks the current order slot done and returns 1 once all orders are
 * done; else 0. A slot whose completion_state is not yet 2 or 3 becomes 2 when
 * pai_is_plan_complete_for_order_slot says its plan is complete, else 3 when
 * pai_is_boarding_plan_complete_for_order_slot does. With the slot done it returns 0
 * when max_speed_cache is 0, 1 after skipping to order 4, else 1 when every order
 * the group has in slots 0 to 2 is 2 or 3. Sets go_home_flag when they are all
 * 2. */
// FUNCTION: XVT 0x469690
int16_t paiorder_completegohomeorder(void)
{
	uint8_t completion_state =
		g_pai_context.controller->order_progress
			.completion_state[g_pai_context.order_slot];
	if (completion_state != 2 && completion_state != 3) {
		if (pai_is_plan_complete_for_order_slot(
			    g_pai_context.controller->current_plan_id,
			    g_pai_context.order_slot) != 0) {
			g_pai_context.controller->order_progress
				.completion_state[g_pai_context.order_slot] = 2;
			XVT_LOG_DEBUG(
				"ai.order_completed object=%d order=%d state=2 plan=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.order_slot,
				(int)g_pai_context.controller->current_plan_id,
				g_flight_sim_side_effects_suppressed);
		} else if (pai_is_boarding_plan_complete_for_order_slot(
				   g_pai_context.controller->current_plan_id,
				   g_pai_context.order_slot) != 0) {
			g_pai_context.controller->order_progress
				.completion_state[g_pai_context.order_slot] = 3;
			XVT_LOG_DEBUG(
				"ai.order_completed object=%d order=%d state=3 plan=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.order_slot,
				(int)g_pai_context.controller->current_plan_id,
				g_flight_sim_side_effects_suppressed);
		}
	}

	completion_state = g_pai_context.controller->order_progress
				   .completion_state[g_pai_context.order_slot];
	if (completion_state != 2 && completion_state != 3) {
		return 0;
	}
	if (g_cur_craft->ai_flight.max_speed_cache == 0) {
		return 0;
	}
	if (g_pai_context.controller->skipped_to_order4 == 1) {
		return 1;
	}

	uint16_t order_slot = 0;
	unsigned int active_order_count = 0;
	uint16_t flight_group_idx = g_pai_context.craft_flight_group_index;
	int completed_order_count = 0;
	int completed_boarding_order_count = 0;
	do {
		if (g_mission_flight_groups[flight_group_idx]
			    .fg.orders[order_slot]
			    .order != 0) {
			++active_order_count;
			completion_state =
				g_pai_context.controller->order_progress
					.completion_state[order_slot];
			if (completion_state == 2) {
				++completed_order_count;
			}
			if (completion_state == 3) {
				++completed_boarding_order_count;
			}
		}
		++order_slot;
	} while (order_slot < 3);
	if (active_order_count != 0 &&
	    (unsigned int)completed_order_count == active_order_count &&
	    g_cur_craft->ai_flight.go_home_flag == 0) {
		g_cur_craft->ai_flight.go_home_flag = 1;
		XVT_LOG_DEBUG(
			"ai.orders_done object=%d fg=%d from=\"own\" predicted=%d",
			(int)g_pai_context.object_index,
			(int)g_pai_context.craft_flight_group_index,
			g_flight_sim_side_effects_suppressed);
	}
	int16_t all_orders_complete =
		(unsigned int)(completed_boarding_order_count +
			       completed_order_count) >= active_order_count;
	return all_orders_complete;
}

/* Order 40: moves the craft to its next order and returns 1, else returns 0; 0
 * at once after skipping to order 4. First, unless g_pai_skip_to_order4_checked is
 * set, it tests the flight group's skip-to-order-4 trigger pair when either
 * condition is not MISSION_COND_ALWAYS_TRUE: when it holds, the craft skips to
 * slot 3 (skipped_to_order4 1) and it returns 1; when not, it sets
 * g_pai_skip_to_order4_checked. Then, when the current slot's completion_state is 2,
 * the slot is below 2 and the next slot has an order, it moves to that slot. A
 * move sets the slot in g_pai_context and the controller, current_plan_id to the
 * order's leader plan, and g_pai_context.variable_plan_id to the leader or
 * follower plan, for the "variablepln" switch. */
// FUNCTION: XVT 0x4697F0
int16_t paiorder_completegootherorder(void)
{
	enum {
		ORDER_NONE = 0,
		ORDER_STATE_SKIPPED_TO_ORDER4 = 1,
		ORDER_COMPLETION_COMPLETE = 2,
		THIRD_ORDER_SLOT = 2,
		FOURTH_ORDER_SLOT = 3,
	};

	if (g_pai_context.controller->skipped_to_order4 ==
	    ORDER_STATE_SKIPPED_TO_ORDER4) {
		return 0;
	}
	int order;
	if (g_pai_skip_to_order4_checked == 0) {
		if (g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
				    .fg.skip_to_order4.triggers[0]
				    .condition != MISSION_COND_ALWAYS_TRUE ||
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
				    .fg.skip_to_order4.triggers[1]
				    .condition != MISSION_COND_ALWAYS_TRUE) {
			if ((mission_evaluate_trigger_pair(
				     &g_mission_flight_groups
					      [g_pai_context
						       .craft_flight_group_index]
						      .fg.skip_to_order4,
				     0) &
			     1) != 0) {
				XVT_LOG_DEBUG(
					"ai.order_moved object=%d from=%d to=3 plan=%d why=\"skipped\" predicted=%d",
					(int)g_pai_context.object_index,
					(int)g_pai_context.order_slot,
					(int)g_builtin_plan_id_by_name_index
						[g_order_leader_builtin_plan_name_index
							 [g_mission_flight_groups
								  [g_pai_context
									   .craft_flight_group_index]
									  .fg
									  .orders[3]
									  .order]],
					g_flight_sim_side_effects_suppressed);
				g_pai_context.controller->skipped_to_order4 =
					ORDER_STATE_SKIPPED_TO_ORDER4;
				g_pai_context.order_slot = FOURTH_ORDER_SLOT;
				g_pai_context.controller->current_order_slot =
					FOURTH_ORDER_SLOT;
				order = g_mission_flight_groups
						[g_pai_context
							 .craft_flight_group_index]
							.fg
							.orders[g_pai_context
									.order_slot]
							.order;
				g_pai_context.controller->current_plan_id =
					g_builtin_plan_id_by_name_index
						[g_order_leader_builtin_plan_name_index
							 [order]];
				if (g_cur_craft->leader_obj_idx == UINT8_MAX) {
					g_pai_context.variable_plan_id =
						g_builtin_plan_id_by_name_index
							[g_order_leader_builtin_plan_name_index
								 [order]];
				} else {
					g_pai_context.variable_plan_id =
						g_builtin_plan_id_by_name_index
							[g_order_follower_builtin_plan_name_index
								 [order]];
				}
				return 1;
			}
			g_pai_skip_to_order4_checked = 1;
		}
	}

	if (g_pai_context.controller->order_progress
			    .completion_state[g_pai_context.order_slot] !=
		    ORDER_COMPLETION_COMPLETE ||
	    g_pai_context.order_slot == THIRD_ORDER_SLOT ||
	    g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			    .fg.orders[g_pai_context.order_slot + 1]
			    .order == ORDER_NONE) {
		return 0;
	}

	++g_pai_context.order_slot;
	g_pai_context.controller->current_order_slot =
		(uint8_t)g_pai_context.order_slot;
	order = g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[g_pai_context.order_slot]
			.order;
	g_pai_context.controller->current_plan_id =
		g_builtin_plan_id_by_name_index
			[g_order_leader_builtin_plan_name_index[order]];
	if (g_cur_craft->leader_obj_idx == UINT8_MAX) {
		g_pai_context.variable_plan_id = g_builtin_plan_id_by_name_index
			[g_order_leader_builtin_plan_name_index[order]];
	} else {
		g_pai_context.variable_plan_id = g_builtin_plan_id_by_name_index
			[g_order_follower_builtin_plan_name_index[order]];
	}
	XVT_LOG_DEBUG(
		"ai.order_moved object=%d from=%d to=%d plan=%d why=\"next\" predicted=%d",
		(int)g_pai_context.object_index,
		(int)g_pai_context.order_slot - 1,
		(int)g_pai_context.order_slot,
		(int)g_pai_context.controller->current_plan_id,
		g_flight_sim_side_effects_suppressed);
	return 1;
}

/* Order 42: returns 1 after moving the craft to the first later order slot,
 * below 3, whose plan name is capldr1pln to capldr5pln, capescortersldr1pln,
 * caprespondldr1pln or capflw1pln to capflw4pln; else 0, and 0 at once after
 * skipping to order 4. It reads that name as g_plan_table[order], by the mission
 * order number itself, not by the order's plan id as the other order functions
 * do. A move sets the controller's current_order_slot, not
 * g_pai_context.order_slot, current_plan_id to the order's leader plan and
 * g_pai_context.variable_plan_id to the leader or follower plan. */
// FUNCTION: XVT 0x469A10
int16_t paiorder_waitgootherorder(void)
{
	if (g_pai_context.controller->skipped_to_order4 == 1) {
		return 0;
	}
	uint16_t order_slot = g_pai_context.order_slot + 1;
	while (order_slot < 3) {
		uint16_t order =
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_slot]
					.order;
		const char *plan_name = g_plan_table[order].name;
		if (strcmp(plan_name, "capldr1pln") == 0 ||
		    strcmp(plan_name, "capescortersldr1pln") == 0 ||
		    strcmp(plan_name, "caprespondldr1pln") == 0 ||
		    strcmp(plan_name, "capldr2pln") == 0 ||
		    strcmp(plan_name, "capldr3pln") == 0 ||
		    strcmp(plan_name, "capldr4pln") == 0 ||
		    strcmp(plan_name, "capldr5pln") == 0 ||
		    strcmp(plan_name, "capflw1pln") == 0 ||
		    strcmp(plan_name, "capflw2pln") == 0 ||
		    strcmp(plan_name, "capflw3pln") == 0 ||
		    strcmp(plan_name, "capflw4pln") == 0) {
			g_pai_context.controller->current_order_slot =
				(uint8_t)order_slot;
			g_pai_context.controller->current_plan_id =
				g_builtin_plan_id_by_name_index
					[g_order_leader_builtin_plan_name_index
						 [order]];
			if (g_cur_craft->leader_obj_idx == UINT8_MAX) {
				g_pai_context.variable_plan_id =
					g_builtin_plan_id_by_name_index
						[g_order_leader_builtin_plan_name_index
							 [order]];
			} else {
				g_pai_context.variable_plan_id =
					g_builtin_plan_id_by_name_index
						[g_order_follower_builtin_plan_name_index
							 [order]];
			}
			XVT_LOG_DEBUG(
				"ai.order_moved object=%d from=%d to=%d plan=%d why=\"capture\" predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.order_slot, (int)order_slot,
				(int)g_pai_context.controller->current_plan_id,
				g_flight_sim_side_effects_suppressed);
			return 1;
		}
		++order_slot;
	}
	return 0;
}

/* Order 43: returns 1 after taking the craft back to an earlier order it can
 * work on again, else 0; 0 at once after skipping to order 4. It first runs the
 * same skip-to-order-4 test as paiorder_completegootherorder, returning 1 on a
 * skip. Then, past slot 0, it takes the first earlier slot not marked complete
 * whose leader plan is capfreeldr1pln, caprespondldr1pln, capescortersldr1pln
 * or disableldr1pln with a target found now, or a boardto plan other than
 * boardtopickuppln with a target it can board. The switch back sets the
 * controller's current_order_slot, not g_pai_context.order_slot, current_plan_id and
 * g_pai_context.variable_plan_id. */
// FUNCTION: XVT 0x469BD0
int16_t paiorder_orderswitchorder(void)
{
	enum {
		ORDER_STATE_SKIPPED_TO_ORDER4 = 1,
		ORDER_COMPLETION_COMPLETE = 2,
		FOURTH_ORDER_SLOT = 3,
	};

	if (g_pai_context.controller->skipped_to_order4 ==
	    ORDER_STATE_SKIPPED_TO_ORDER4) {
		return 0;
	}

	int order;
	if (g_pai_skip_to_order4_checked == 0) {
		int flight_group_index = g_pai_context.craft_flight_group_index;
		if (g_mission_flight_groups[flight_group_index]
				    .fg.skip_to_order4.triggers[0]
				    .condition != MISSION_COND_ALWAYS_TRUE ||
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
				    .fg.skip_to_order4.triggers[1]
				    .condition != MISSION_COND_ALWAYS_TRUE) {
			if ((mission_evaluate_trigger_pair(
				     &g_mission_flight_groups
					      [flight_group_index]
						      .fg.skip_to_order4,
				     0) &
			     1) != 0) {
				XVT_LOG_DEBUG(
					"ai.order_moved object=%d from=%d to=3 plan=%d why=\"skipped\" predicted=%d",
					(int)g_pai_context.object_index,
					(int)g_pai_context.order_slot,
					(int)g_builtin_plan_id_by_name_index
						[g_order_leader_builtin_plan_name_index
							 [g_mission_flight_groups
								  [g_pai_context
									   .craft_flight_group_index]
									  .fg
									  .orders[3]
									  .order]],
					g_flight_sim_side_effects_suppressed);
				g_pai_context.controller->skipped_to_order4 =
					ORDER_STATE_SKIPPED_TO_ORDER4;
				g_pai_context.order_slot = FOURTH_ORDER_SLOT;
				g_pai_context.controller->current_order_slot =
					FOURTH_ORDER_SLOT;
				order = g_mission_flight_groups
						[g_pai_context
							 .craft_flight_group_index]
							.fg
							.orders[g_pai_context
									.order_slot]
							.order;
				g_pai_context.controller->current_plan_id =
					g_builtin_plan_id_by_name_index
						[g_order_leader_builtin_plan_name_index
							 [order]];
				if (g_cur_craft->leader_obj_idx == UINT8_MAX) {
					g_pai_context.variable_plan_id =
						g_builtin_plan_id_by_name_index
							[g_order_leader_builtin_plan_name_index
								 [order]];
				} else {
					g_pai_context.variable_plan_id =
						g_builtin_plan_id_by_name_index
							[g_order_follower_builtin_plan_name_index
								 [order]];
				}
				return 1;
			}
			g_pai_skip_to_order4_checked = 1;
		}
	}

	if (g_pai_context.order_slot == 0) {
		return 0;
	}

	int16_t found_order = 0;
	uint16_t order_slot = 0;
	while (order_slot < g_pai_context.order_slot) {
		if (found_order != 0) {
			break;
		}
		if (g_pai_context.controller->order_progress
			    .completion_state[order_slot] !=
		    ORDER_COMPLETION_COMPLETE) {
			order = g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg.orders[order_slot]
						.order;
			const char *plan_name =
				g_plan_table
					[g_builtin_plan_id_by_name_index
						 [g_order_leader_builtin_plan_name_index
							  [order]]]
						.name;
			if (strcmp(plan_name, "capfreeldr1pln") == 0 ||
			    strcmp(plan_name, "caprespondldr1pln") == 0 ||
			    strcmp(plan_name, "capescortersldr1pln") == 0 ||
			    strcmp(plan_name, "disableldr1pln") == 0) {
				if (paifight_search_order_slot_target(
					    order_slot) != 0) {
					found_order = 1;
				}
			} else if ((strcmp(plan_name, "boardtogivepln") == 0 ||
				    strcmp(plan_name, "boardtotakepln") == 0 ||
				    strcmp(plan_name, "boardtoexchangepln") ==
					    0 ||
				    strcmp(plan_name, "boardtocapturepln") ==
					    0 ||
				    strcmp(plan_name, "boardtodestroypln") ==
					    0 ||
				    strcmp(plan_name, "boardtocontactpln") ==
					    0 ||
				    strcmp(plan_name, "boardtorepairpln") ==
					    0) &&
				   pai_order_slot_can_board_target(
					   order_slot) != 0) {
				found_order = 1;
			}
		}
		++order_slot;
	}

	if (found_order != 0) {
		--order_slot;
		g_pai_context.controller->current_order_slot =
			(uint8_t)order_slot;
		order = g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_slot]
					.order;
		g_pai_context.controller->current_plan_id =
			g_builtin_plan_id_by_name_index
				[g_order_leader_builtin_plan_name_index[order]];
		if (g_cur_craft->leader_obj_idx == UINT8_MAX) {
			g_pai_context.variable_plan_id =
				g_builtin_plan_id_by_name_index
					[g_order_leader_builtin_plan_name_index
						 [order]];
		} else {
			g_pai_context.variable_plan_id =
				g_builtin_plan_id_by_name_index
					[g_order_follower_builtin_plan_name_index
						 [order]];
		}
		XVT_LOG_DEBUG(
			"ai.order_moved object=%d from=%d to=%d plan=%d why=\"back\" predicted=%d",
			(int)g_pai_context.object_index,
			(int)g_pai_context.order_slot, (int)order_slot,
			(int)g_pai_context.controller->current_plan_id,
			g_flight_sim_side_effects_suppressed);
		return 1;
	}

	return 0;
}

/* Order 41: keeps a follower on its leader's order. A follower takes on its
 * leader's go_home_flag and depart_timer_flag when they are set, counting the
 * not-departed outcome when it takes the second. When the leader's
 * current_order_slot differs from the craft's order slot, the craft moves to that
 * slot, with current_plan_id and g_pai_context.variable_plan_id set as on any order
 * move, and it returns 1; else 0. */
// FUNCTION: XVT 0x469F40
int16_t paiorder_completefolloworder(void)
{
	struct ai_controller *leader_controller =
		&g_pai_context.leader_or_self_craft->ai_controller;
	if (g_pai_context.leader_object_index != UINT8_MAX) {
		if (g_pai_context.leader_or_self_craft->ai_flight
				    .go_home_flag == 1 &&
		    g_cur_craft->ai_flight.go_home_flag == 0) {
			g_cur_craft->ai_flight.go_home_flag = 1;
			XVT_LOG_DEBUG(
				"ai.orders_done object=%d fg=%d from=\"leader\" predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.craft_flight_group_index,
				g_flight_sim_side_effects_suppressed);
		}
		if (g_pai_context.leader_or_self_craft->ai_flight
				    .depart_timer_flag == 1 &&
		    g_cur_craft->ai_flight.depart_timer_flag == 0) {
			g_cur_craft->ai_flight.depart_timer_flag = 1;
			uint16_t flight_group_index =
				g_pai_context.craft_flight_group_index;
			int runtime_flight_group_index =
				g_pai_context.craft_flight_group_index;
			++g_mission_fg_stats[runtime_flight_group_index]
				  .outcome_count
					  [FLIGHT_GROUP_OUTCOME_NOT_DEPARTED];
			if (g_mission_flight_groups[flight_group_index]
				    .fg.special_cargo_craft ==
			    g_cur_craft->craft_ordinal) {
				g_mission_fg_stats[runtime_flight_group_index]
					.special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] =
					1;
			}
			XVT_LOG_DEBUG(
				"ai.departure_started object=%d fg=%d from=\"leader\" count=%u minutes=%d seconds=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.craft_flight_group_index,
				(unsigned)g_mission_fg_stats
					[g_pai_context.craft_flight_group_index]
						.outcome_count
							[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED],
				(int)g_mission_elapsed_clock.minutes,
				(int)g_mission_elapsed_clock.seconds,
				g_flight_sim_side_effects_suppressed);
		}
	}

	uint16_t current_order_slot = leader_controller->current_order_slot;
	if (g_pai_context.order_slot != current_order_slot) {
		XVT_LOG_DEBUG(
			"ai.order_moved object=%d from=%d to=%d plan=%d why=\"leader\" predicted=%d",
			(int)g_pai_context.object_index,
			(int)g_pai_context.order_slot, (int)current_order_slot,
			(int)g_builtin_plan_id_by_name_index
				[g_order_leader_builtin_plan_name_index
					 [g_mission_flight_groups
						  [g_pai_context
							   .craft_flight_group_index]
							  .fg
							  .orders[current_order_slot]
							  .order]],
			g_flight_sim_side_effects_suppressed);
		g_pai_context.order_slot = current_order_slot;
		g_pai_context.controller->current_order_slot =
			(uint8_t)current_order_slot;
		int order = g_mission_flight_groups
				    [g_pai_context.craft_flight_group_index]
					    .fg.orders[g_pai_context.order_slot]
					    .order;
		{
			struct ai_controller *current_controller =
				g_pai_context.controller;
			int plan_name_index =
				g_order_leader_builtin_plan_name_index[order];
			current_controller->current_plan_id =
				g_builtin_plan_id_by_name_index
					[plan_name_index];
		}
		if (g_pai_context.leader_object_index == UINT8_MAX) {
			g_pai_context.variable_plan_id =
				g_builtin_plan_id_by_name_index
					[g_order_leader_builtin_plan_name_index
						 [order]];
			return 1;
		}
		g_pai_context.variable_plan_id = g_builtin_plan_id_by_name_index
			[g_order_follower_builtin_plan_name_index[order]];
		return 1;
	}
	return 0;
}

/* Order 44: starts the craft's self-destruct countdown when none runs: sets its
 * lifetime_timer to variable1 of the current order times 1,180 ticks (five times
 * SIMULATION_TICKS_PER_SECOND), or, for variable1 0, 2 to 5 such units at
 * random. object_update_lifetime_and_movement destroys the craft when it runs out.
 * Does not check that the product fits the 16-bit timer. Returns 0 on every
 * path. */
// FUNCTION: XVT 0x46A0A0
int16_t paiorder_killselforder(void)
{
	if (g_object_table[g_pai_context.object_index].mobj->lifetime_timer ==
	    0) {
		uint16_t delay_five_second_units =
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[g_pai_context.order_slot]
					.variable1;
		if (delay_five_second_units == 0) {
			delay_five_second_units =
				((uint16_t)game_rand() & 3) + 2;
		}

		g_object_table[g_pai_context.object_index]
			.mobj->lifetime_timer =
			(uint16_t)(delay_five_second_units * 1180u);
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_INFO(
				"ai.self_destruct_set object=%d fg=%d ticks=%d tick=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.craft_flight_group_index,
				(int)g_object_table[g_pai_context.object_index]
					.mobj->lifetime_timer,
				g_game_time);
		}
		if (g_flight_sim_side_effects_suppressed == 0 &&
		    delay_five_second_units > 55) {
			XVT_LOG_WARN(
				"ai.self_destruct_wrapped object=%d units=%d ticks=%d",
				(int)g_pai_context.object_index,
				(int)delay_five_second_units,
				(int)g_object_table[g_pai_context.object_index]
					.mobj->lifetime_timer);
		}
	}
	return 0;
}

/* Order 46: returns 1 when the mothership groups the craft would depart to have
 * all arrived, comparing each group's FLIGHT_GROUP_OUTCOME_TOTAL and
 * FLIGHT_GROUP_OUTCOME_ARRIVED counts: for a captured craft, the captured
 * departure mothership group, and only when it departs by one; else the
 * departure mothership group when departure_method is set and the alternate one
 * when used, which with neither gives 1. Returns 0 when the target lies in the
 * active region's craft slots or below them, or the model has no hyperdrive. */
// FUNCTION: XVT 0x46A250
int16_t paiorder_abortmotherwaitorder(void)
{
	if ((int)g_pai_context.controller->target_obj_idx <
	    g_active_region_craft_object_slot_end) {
		return 0;
	}
	if (g_model_defs[g_cur_craft->model_index].has_hyperdrive == 0) {
		return 0;
	}

	int16_t result = 0;
	if (g_cur_craft->captured_by_flight_group != 0) {
		if (g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.captured_depart_via_mothership != 0) {
			uint16_t mothership_flight_group =
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.captured_departure_mothership;
			if (g_mission_fg_stats[mothership_flight_group]
				    .outcome_count
					    [FLIGHT_GROUP_OUTCOME_TOTAL] ==
			    g_mission_fg_stats[mothership_flight_group]
				    .outcome_count
					    [FLIGHT_GROUP_OUTCOME_ARRIVED]) {
				result = 1;
			}
		}
	} else {
		int16_t departure_mothership_ready = 1;
		int16_t alternate_mothership_ready = 1;
		if (g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.departure_method != 0) {
			uint16_t mothership_flight_group =
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg.departure_mothership;
			if (g_mission_fg_stats[mothership_flight_group]
				    .outcome_count
					    [FLIGHT_GROUP_OUTCOME_TOTAL] !=
			    g_mission_fg_stats[mothership_flight_group]
				    .outcome_count
					    [FLIGHT_GROUP_OUTCOME_ARRIVED]) {
				departure_mothership_ready = 0;
			}
		}
		if (g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.alternate_mothership_used != 0) {
			uint16_t mothership_flight_group =
				g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg.alternate_mothership;
			if (g_mission_fg_stats[mothership_flight_group]
				    .outcome_count
					    [FLIGHT_GROUP_OUTCOME_TOTAL] !=
			    g_mission_fg_stats[mothership_flight_group]
				    .outcome_count
					    [FLIGHT_GROUP_OUTCOME_ARRIVED]) {
				alternate_mothership_ready = 0;
			}
		}
		result = (int16_t)(departure_mothership_ready &
				   alternate_mothership_ready);
	}
	return result;
}
