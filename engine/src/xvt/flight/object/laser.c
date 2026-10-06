#include "xvt/flight/object/laser.h"

#include <limits.h>

#include "xvt/assets/model_bounds.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/model_mesh_internal.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paifight.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/targeting.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/render/renderer.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/log/log_both_builds.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/player_timing.h"
#include "xvt_runtime/timing/reference_motion.h"

/* The figures of every shot type, a constant table laid out as
 * projectile_type_data_tables; the last four types have zeros. */
// GLOBAL: XVT 0x51A3B8
const struct projectile_type_data_tables g_projectile_type_data = {
	{
		250,  500,  200, 400,	200,  400,   10000, 3000,
		1000, 800,  800, 15000, 6000, 65000, 35000, 3000,
		6000, 9000, 500, 2000,	0,    0,     0,	    0,
	},
	{
		2000, 2000, 1800, 1800, 1400, 1600, 250, 500,
		1000, 900,  400,  300,	600,  25,   175, 600,
		350,  400,  500,  225,	0,    0,    0,	 0,
	},
	{1,  1,	  1,  1,  1,  2,  40, 25, 3, 3, 5, 45,
	 25, 120, 90, 25, 40, 35, 5,  10, 0, 0, 0, 0},
	{0, 0x8000, 0, 0x8000, 0x8000, 0, 0, 0, 0, 0, 0, 0,
	 0, 0,	    0, 0,      0,      0, 0, 0, 0, 0, 0, 0},
	{
		921, 921, 921, 921, 921, 921, 512, 512, 921, 921, 921, 512,
		512, 48,  512, 512, 512, 512, 256, 256, 0,   0,	  0,   0,
	},
	{0, 0, 0, 0, 0, 0, 2, 1, 0, 0, 0, 2,
	 1, 2, 2, 1, 1, 2, 1, 1, 0, 0, 0, 0},
	{0,  0,	 0,  0,	 0,  0,	 15, 10, 0, 0, 0, 20,
	 25, 40, 30, 10, 25, 25, 10, 10, 0, 0, 0, 0},
};
/* Shot object type of each warhead choice a flight group can make (its
 * fg.warhead, 0 for none): spawn loads it into a craft's launchers, and
 * laser_createprojectilefromstatic fires it from a static object. */
// GLOBAL: XVT 0x5241F8
const uint8_t g_warhead_type_ids[11] = {0x00, 0x96, 0x97, 0x90, 0x8F, 0x95,
					0x94, 0x98, 0x99, 0x9A, 0x90};
/* Share of a launcher's capacity loaded with each warhead choice, in
 * 65,536ths (0xFFFF loads it full); spawn loads at least 1.
 * paiman_boardmaneuver reads it too. */
// GLOBAL: XVT 0x524208
const uint16_t g_warhead_ammo_fraction_q16[12] = {
	0x0000, 0x4000, 0x8000, 0xFFFF, 0xC000, 0xFFFF,
	0xC000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000,
};
/* Hit points a damageable component starts with at spawn, by its mesh
 * component type; a shield generator on object type 54 gets twice this
 * less one. */
// GLOBAL: XVT 0x524220
const uint8_t g_mesh_type_component_max_hp[32] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0x18, 0x04, 0xFF, 0xFF, 0x40, 0xFF, 0x20,
	0x30, 0x30, 0x30, 0x70, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x18,
	0x20, 0x30, 0x30, 0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
/* For each platform object type 60 to 64, 12 component indices whose
 * components start destroyed (hit points 0, state 4) when the flight group
 * carries a beam: the first 6 for a tractor beam, all 12 for any other;
 * 0xFF entries are skipped. */
// GLOBAL: XVT 0x524240
const uint8_t g_platform_beam_disabled_component_ids[60] = {
	0x16, 0x17, 0x15, 0x14, 0x13, 0x05, 0x0F, 0x10, 0x11, 0x12, 0x18, 0x06,
	0x03, 0x05, 0x1B, 0x09, 0x0A, 0xFF, 0x01, 0x04, 0x1A, 0x07, 0x08, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0x05, 0x0D, 0x0F, 0x13, 0x14, 0x18, 0x0B, 0x0E, 0x06, 0x11, 0x12, 0x17,
	0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0xFF, 0x15, 0x16, 0x17, 0x18, 0x1E, 0xFF,
};

/* Runs one step of the weapon systems of every craft and mine. A power
 * step comes when the weapon_power_update_timer of
 * g_flight_global_countdown_timers is 0, which resets it to
 * SIMULATION_TICKS_PER_SECOND (in the modern build only when
 * xvt_flight_timing_reference_due). First it clears beam_effect_accum of every
 * craft (family 0). Then, for a player's craft in warhead mode, it updates
 * the lock (missile_lock_state in g_players, warhead_lock_ticks): with no
 * target or no rounds the lock drops to 0; a target closer than 101,805
 * world units (244,332 for a freighter, starship or platform) and inside
 * the aim cone builds it, half as fast against active chaff; otherwise it
 * bleeds away; 354 ticks lock a missile boat, 708 any other craft. While a
 * player's beam is on it drains 125 charge every 59 ticks and acts on the
 * current target when that is in range and in the cone. For each hostile
 * starship, X/7 factory and repair yard it calls
 * collide_apply_hostile_proximity_weapon_disruption on the player's craft. For
 * an AI craft, on a power step, it sets the shield and laser recharge
 * levels, moves laser charge into the front shield of a starfighter whose
 * shield is below full, and on difficulty 2 lets a damaged starship
 * recharge shields by its live shield generators. On a power step every
 * craft then recharges shields, lasers and beam by its recharge levels,
 * drops engine overdrive when its lasers run dry, and counts down chaff.
 * Next, for each craft whose weapon_fire_inhibit_timer is 0: unless a
 * jamming beam holds it (beam_effect_accum[2]), it counts down cannon
 * cooldowns and fires AI bursts through laser_firelasersystem; it fires
 * each gunner slot that has a target (laser_fireturretslot); and it counts
 * down launcher cooldowns. Last it runs laser_update_mine_weapon_fire for each
 * mine in the static slots. The modern build steps AI cannon fire, turrets
 * and mines on the reference clock. Writes g_cur_craft and
 * g_local_beam_target_obj_idx. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x404710
void laser_weaponsfire(void)
{
	enum {
		BEAM_EFFECT_COUNT = 5,
		PLAYER_SHIELD_RECHARGE_RATE = 20,
		LARGE_CRAFT_SHIELD_RECHARGE_RATE = 5,
		MISSILE_LOCK_FIGHTER_RANGE = 101805,
		MISSILE_LOCK_LARGE_CRAFT_RANGE = 244332,
		MISSILE_BOAT_LOCK_TICKS = 354,
		DEFAULT_LOCK_TICKS = 708,
		BEAM_FIRE_COOLDOWN_TICKS = 59,
		BEAM_DRAIN_AMOUNT = 125,
		BEAM_TARGET_RANGE = 0x20000,
		BEAM_END_MESSAGE_BASE = 242,
		SYSTEM_MESSAGE_PANE_STATE = 256,
		AI_SHIELD_TRANSFER_LOW = 250,
		AI_SHIELD_TRANSFER_MEDIUM = 500,
		AI_SHIELD_TRANSFER_HIGH = 10000,
		AI_SHIELD_TRANSFER_PULSE = 100,
		MISSILE_BOAT_SHIELD_TRANSFER = 32,
		DEFAULT_SHIELD_TRANSFER = 4,
		LASER_CHARGE_LOW_THRESHOLD = 32,
		LASER_CHARGE_HIGH_THRESHOLD = 96,
		MAXIMUM_LASER_CHARGE = 127,
		BEAM_RECHARGE_STEP = 125,
		MAXIMUM_BEAM_CHARGE = 9999,
		ENGINE_OVERDRIVE_ACTIVE = 0,
		ENGINE_OVERDRIVE_DISENGAGED = -1,
		TURRET_PROJECTILE_TYPE = 2,
	};

	char do_periodic_power_update = 0;
	if (g_flight_global_countdown_timers.weapon_power_update_timer == 0 &&
	    xvt_flight_timing_reference_due()) {
		do_periodic_power_update = 1;
		g_flight_global_countdown_timers.weapon_power_update_timer =
			SIMULATION_TICKS_PER_SECOND;
	}

	{
		for (uint16_t clear_obj_idx = g_active_region_object_slot_start;
		     clear_obj_idx < g_active_region_craft_object_slot_end;
		     ++clear_obj_idx) {
			if (g_object_table[clear_obj_idx].object_type ==
				    CRAFT_SPECIES_UNKNOWN ||
			    g_object_table[clear_obj_idx].mobj->family != 0) {
				continue;
			}
			struct craft_data *craft =
				g_object_table[clear_obj_idx].mobj->p_craft;
			for (uint16_t effect_index = 0;
			     effect_index < BEAM_EFFECT_COUNT; ++effect_index) {
				craft->beam_effect_accum[effect_index] = 0;
			}
		}
	}

	uint16_t object_idx;
	for (object_idx = g_active_region_object_slot_start;
	     object_idx < g_active_region_craft_object_slot_end; ++object_idx) {
		if (g_object_table[object_idx].object_type ==
			    CRAFT_SPECIES_UNKNOWN ||
		    g_object_table[object_idx].mobj->family != 0) {
			continue;
		}

		int16_t shield_recharge_rate;
		if (g_object_table[object_idx].player_owner_idx != -1) {
			g_cur_craft = g_object_table[object_idx].mobj->p_craft;
			int player_idx =
				g_object_table[object_idx].player_owner_idx;
			if (g_players[player_idx].selected_weapon_mode != 0) {
				int first_warhead_slot =
					g_model_defs[get_model_index_from_type(
							     g_object_table[object_idx]
								     .object_type)]
						.warhead_launcher_first_slot
							[g_players[player_idx]
								 .selected_weapon_bank];
				uint16_t target_obj_idx =
					(uint16_t)g_players[player_idx]
						.current_target_object_idx;
				int16_t warhead_count =
					g_cur_craft
						->weapon_slots
							[first_warhead_slot]
						.ammo_count +
					g_cur_craft
						->weapon_slots
							[first_warhead_slot + 1]
						.ammo_count;
				if (target_obj_idx == UINT16_MAX ||
				    warhead_count == 0) {
					if (g_players[player_idx]
						    .missile_lock_state == 2) {
						XVT_LOG_DEBUG(
							"combat.lock_lost slot=%d object=%d target=%d reason=\"%s\" predicted=%d",
							player_idx,
							(int)object_idx,
							(int)(int16_t)
								target_obj_idx,
							target_obj_idx ==
									UINT16_MAX
								? "no_target"
								: "no_rounds",
							g_flight_sim_side_effects_suppressed);
					}
					g_players[player_idx]
						.missile_lock_state = 0;
					g_cur_craft->warhead_lock_ticks = 0;
					xvt_player_timing_lock_half(player_idx,
								    0);
				} else {
					uint8_t target_genus =
						g_object_table[target_obj_idx]
							.genus_id;
					if (target_genus ==
						    CRAFT_GENUS_STARSHIP ||
					    target_genus ==
						    CRAFT_GENUS_PLATFORM) {
						object_direction_and_distance_to_mesh_center(
							object_idx,
							target_obj_idx,
							(uint16_t)g_players[player_idx]
								.selected_target_component);
					} else {
						pai_object_ref_direction_to_object_ref(
							object_idx,
							target_obj_idx);
					}
					unsigned int lock_range =
						MISSILE_LOCK_FIGHTER_RANGE;
					if (target_obj_idx <
						    g_active_region_craft_object_slot_end &&
					    (target_genus ==
						     CRAFT_GENUS_FREIGHTER ||
					     target_genus ==
						     CRAFT_GENUS_STARSHIP ||
					     target_genus ==
						     CRAFT_GENUS_PLATFORM)) {
						lock_range =
							MISSILE_LOCK_LARGE_CRAFT_RANGE;
					}
					if ((unsigned int)trig2_polardistance <
						    lock_range &&
					    targeting_test_aim_cone(
						    target_obj_idx, 0,
						    player_idx) != 0) {
						g_cur_craft
							->warhead_lock_ticks +=
							g_elapsed_ticks;
						if (target_obj_idx <
						    g_active_region_craft_object_slot_end) {
							struct craft_data *target_craft =
								g_object_table[target_obj_idx]
									.mobj
									->p_craft;
							if (target_craft->cm_type_id ==
								    COUNTERMEASURE_TYPE_CHAFF &&
							    target_craft->chaff_active_seconds !=
								    0) {

								g_cur_craft
									->warhead_lock_ticks -=
									xvt_player_timing_lock_half(
										player_idx,
										1);
							}
						}
						model_index missile_boat_model_index =
							get_model_index_from_type(
								CRAFT_SPECIES_MISSILE_BOAT);
						uint16_t lock_threshold =
							get_model_index_from_type(
								g_object_table[object_idx]
									.object_type) ==
									missile_boat_model_index
								? MISSILE_BOAT_LOCK_TICKS
								: DEFAULT_LOCK_TICKS;
						if (g_players[player_idx]
								    .missile_lock_state !=
							    2 &&
						    g_cur_craft->warhead_lock_ticks >=
							    (int16_t)
								    lock_threshold) {
							XVT_LOG_DEBUG(
								"combat.lock_gained slot=%d object=%d target=%d ticks=%d needed=%u predicted=%d",
								player_idx,
								(int)object_idx,
								(int)target_obj_idx,
								(int)g_cur_craft
									->warhead_lock_ticks,
								(unsigned)
									lock_threshold,
								g_flight_sim_side_effects_suppressed);
						}
						if (g_cur_craft
							    ->warhead_lock_ticks >=
						    (int16_t)lock_threshold) {
							g_players[player_idx]
								.missile_lock_state =
								2;
						} else {
							g_players[player_idx]
								.missile_lock_state =
								1;
						}
					} else {
						if (g_players[player_idx]
							    .missile_lock_state ==
						    2) {
							XVT_LOG_DEBUG(
								"combat.lock_lost slot=%d object=%d target=%d reason=\"out_of_aim\" predicted=%d",
								player_idx,
								(int)object_idx,
								(int)target_obj_idx,
								g_flight_sim_side_effects_suppressed);
						}
						int16_t lock_ticks =
							g_cur_craft
								->warhead_lock_ticks;
						if (lock_ticks > 0) {

							lock_ticks -=
								xvt_player_timing_lock_half(
									player_idx,
									2);

							lock_ticks -=
								g_elapsed_ticks;
							g_cur_craft
								->warhead_lock_ticks =
								lock_ticks;
							if (g_cur_craft
								    ->warhead_lock_ticks <
							    0) {
								g_cur_craft
									->warhead_lock_ticks =
									0;
							}
						}
						g_players[player_idx]
							.missile_lock_state = 0;
					}
				}
			}

			{
				uint16_t beam_target_obj_idx = UINT16_MAX;
				if ((g_cur_craft->working_subsystems &
				     CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0 &&
				    g_cur_craft->beam_active != 0 &&
				    g_cur_craft->beam_type_id !=
					    BEAM_TYPE_NONE &&
				    g_players[player_idx].awaiting_new_craft ==
					    0) {
					if (g_players[player_idx]
						    .beam_fire_cooldown_timer ==
					    0) {
						g_players[player_idx]
							.beam_fire_cooldown_timer =
							BEAM_FIRE_COOLDOWN_TICKS;
						int16_t beam_charge =
							(int16_t)(g_cur_craft
									  ->beam_charge -
								  BEAM_DRAIN_AMOUNT);
						if (beam_charge < 0) {
							beam_charge = 0;
						}
						g_cur_craft->beam_charge =
							(uint16_t)beam_charge;
						if (beam_charge == 0 &&
						    g_cur_craft->beam_active !=
							    0) {
							g_cur_craft
								->beam_active =
								0;
							g_cur_craft
								->beam_output =
								0;
							XVT_LOG_DEBUG(
								"combat.beam_depleted object=%d slot=%d beam=%d cause=\"use\" predicted=%d",
								(int)object_idx,
								player_idx,
								(int)g_cur_craft
									->beam_type_id,
								g_flight_sim_side_effects_suppressed);
							if (player_idx ==
							    g_local_player) {
								msg_emit_in_flight_message(
									(in_flight_message_id)((uint8_t)g_cur_craft
												       ->beam_type_id +
											       BEAM_END_MESSAGE_BASE),
									g_local_player);
							}
						}
					}

					{
						uint16_t candidate_obj_idx =
							(uint16_t)g_players[player_idx]
								.current_target_object_idx;
						if (candidate_obj_idx !=
							    UINT16_MAX &&
						    candidate_obj_idx <
							    g_active_region_craft_object_slot_end &&
						    g_object_table[candidate_obj_idx]
								    .mobj
								    ->p_craft
								    ->object_kind ==
							    CRAFT_OBJECT_KIND_ACTIVE &&
						    targeting_test_aim_cone(
							    candidate_obj_idx,
							    0,
							    player_idx) != 0 &&
						    (unsigned int)g_last_rough_distance <
							    BEAM_TARGET_RANGE) {
							beam_target_obj_idx =
								candidate_obj_idx;
						}
					}
					if (beam_target_obj_idx != UINT16_MAX) {
						struct craft_data *target_craft =
							g_object_table
								[beam_target_obj_idx]
									.mobj
									->p_craft;
						beam_type beam_type =
							g_cur_craft
								->beam_type_id;
						if (beam_type ==
							    BEAM_TYPE_TRACTOR ||
						    beam_type ==
							    BEAM_TYPE_JAMMING) {
							if (target_craft->cm_type_id ==
								    COUNTERMEASURE_TYPE_CHAFF &&
							    target_craft->chaff_active_seconds !=
								    0) {
								if (player_idx ==
									    g_local_player &&
								    hud_get_system_message_pane_state() !=
									    SYSTEM_MESSAGE_PANE_STATE) {
									msg_emit_in_flight_message(
										IFMSG_256_BEAM_DISRUPTED_BY_TARGET_S_COUNTERMEASURES,
										player_idx);
								}
								beam_target_obj_idx =
									UINT16_MAX;
							} else {
								target_craft->beam_effect_accum
									[(uint8_t)
										 beam_type] +=
									(uint16_t)g_cur_craft
										->beam_output;
							}
						}
					}
					if (player_idx == g_local_player) {
						g_local_beam_target_obj_idx =
							beam_target_obj_idx;
						fsfx_update_beam_system_loop(
							1, player_idx);
					}
				} else if (player_idx == g_local_player) {
					g_local_beam_target_obj_idx =
						UINT16_MAX;
					if ((g_cur_craft->system_flags &
					     CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) !=
					    0) {
						fsfx_update_beam_system_loop(
							0, player_idx);
					}
				}
			}

			shield_recharge_rate = PLAYER_SHIELD_RECHARGE_RATE;
			{
				for (uint16_t scan_obj_idx =
					     g_active_region_object_slot_start;
				     scan_obj_idx <
				     g_active_region_craft_object_slot_end;
				     ++scan_obj_idx) {
					struct object_record *hostile_object =
						&g_object_table[scan_obj_idx];
					if (hostile_object->object_type !=
						    CRAFT_SPECIES_UNKNOWN &&
					    (hostile_object->genus_id ==
						     CRAFT_GENUS_STARSHIP ||
					     (hostile_object->genus_id ==
						      CRAFT_GENUS_PLATFORM &&
					      (hostile_object->object_type ==
						       CRAFT_SPECIES_X7_FACTORY ||
					       hostile_object->object_type ==
						       CRAFT_SPECIES_REPAIR_YARD)))) {
						int hostile_team =
							hostile_object->mobj
								->team;
						int player_team =
							g_mission_flight_groups
								[g_object_table[object_idx]
									 .flight_group_idx]
									.fg
									.team;
						if (hostile_team !=
							    player_team &&
						    g_mission_teams[hostile_team]
								    .allies[player_team] <
							    1) {
							collide_apply_hostile_proximity_weapon_disruption(
								object_idx,
								scan_obj_idx);
						}
					}
				}
			}
		} else {
			if (do_periodic_power_update == 0) {
				continue;
			}
			{
				uint8_t genus_id =
					g_object_table[object_idx].genus_id;
				g_cur_craft = g_object_table[object_idx]
						      .mobj->p_craft;
				if (genus_id == CRAFT_GENUS_STARFIGHTER) {
					g_cur_craft->shield_recharge_level =
						POWER_RECHARGE_MAINTENANCE;
					g_cur_craft->laser_recharge_level =
						POWER_RECHARGE_MAINTENANCE;
					if ((g_cur_craft->system_flags &
					     CRAFT_SUBSYSTEM_FLAG_SHIELDS) !=
					    0) {
						uint16_t group_ai =
							g_mission_flight_groups
								[g_object_table[object_idx]
									 .flight_group_idx]
									.fg
									.group_ai;
						if (g_cur_craft->ai_controller
							    .maneuver_mode ==
						    AI_MANEUVER_MODE_AVOID_ATTACKER) {
							if (group_ai == 5) {
								g_cur_craft
									->shield_recharge_level =
									POWER_RECHARGE_FULLY_REDIRECTED_TO_ENGINES;
							} else if (group_ai ==
									   4 ||
								   group_ai ==
									   3) {
								g_cur_craft
									->shield_recharge_level =
									POWER_RECHARGE_PARTIALLY_REDIRECTED_TO_ENGINES;
							} else {
								g_cur_craft
									->shield_recharge_level =
									POWER_RECHARGE_MAINTENANCE;
							}
							g_cur_craft
								->laser_recharge_level =
								POWER_RECHARGE_INCREASED;
						}
						int max_shield =
							craft_get_object_max_shield(
								object_idx);
						if (g_cur_craft
							    ->shield_energy[0] <
						    max_shield) {
							if (g_cur_craft
								    ->laser_recharge_level ==
							    POWER_RECHARGE_MAINTENANCE) {
								g_cur_craft
									->laser_recharge_level =
									POWER_RECHARGE_MAXIMUM;
							}
							uint16_t transfer_limit;
							if (g_cur_craft
								    ->shield_energy
									    [0] >
							    0) {
								if (group_ai <
								    2) {
									transfer_limit =
										0;
								} else {
									int16_t update_mask;

									if (group_ai ==
									    5) {
										update_mask =
											1;
									} else if (
										group_ai ==
										4) {
										update_mask =
											3;
									} else if (
										group_ai ==
										3) {
										update_mask =
											7;
									} else {
										update_mask =
											15;
									}
									transfer_limit =
										((uint8_t)(update_mask &
											   g_mission_elapsed_clock
												   .seconds) ==
										 update_mask)
											? AI_SHIELD_TRANSFER_PULSE
											: 0;
								}
							} else {
								if (g_cur_craft
									    ->shield_recharge_level ==
								    POWER_RECHARGE_MAINTENANCE) {
									g_cur_craft
										->shield_recharge_level =
										POWER_RECHARGE_MAXIMUM;
								}
								if (group_ai <
								    2) {
									transfer_limit =
										AI_SHIELD_TRANSFER_LOW;
								} else if (
									group_ai <
									3) {
									transfer_limit =
										AI_SHIELD_TRANSFER_MEDIUM;
								} else {
									transfer_limit =
										AI_SHIELD_TRANSFER_HIGH;
								}
							}
							int16_t total_laser_charge =
								0;
							uint16_t slot_index;
							for (slot_index = 0;
							     slot_index <
							     g_cur_craft
								     ->laser_slot_count;
							     ++slot_index) {
								int8_t charge =
									g_cur_craft
										->weapon_slots
											[slot_index]
										.laser_charge;
								if (charge >
								    0) {
									total_laser_charge +=
										charge;
								}
							}
							slot_index = 0;
							uint16_t
								transfer_count =
									0;
							int transfer_amount =
								g_object_table[object_idx]
											.object_type ==
										CRAFT_SPECIES_MISSILE_BOAT
									? MISSILE_BOAT_SHIELD_TRANSFER
									: DEFAULT_SHIELD_TRANSFER;
							while (total_laser_charge !=
								       0 &&
							       transfer_count <
								       transfer_limit) {
								if (g_cur_craft
									    ->weapon_slots
										    [slot_index]
									    .laser_charge >
								    0) {
									--total_laser_charge;
									--g_cur_craft
										  ->weapon_slots
											  [slot_index]
										  .laser_charge;
									g_cur_craft
										->shield_energy
											[0] +=
										transfer_amount;
									if (g_cur_craft
										    ->shield_energy
											    [0] >=
									    max_shield) {
										total_laser_charge =
											0;
									}
								}
								++slot_index;
								if (slot_index >=
								    g_cur_craft
									    ->laser_slot_count) {
									slot_index =
										0;
								}
								++transfer_count;
							}
							if (transfer_count !=
							    0) {
								XVT_LOG_DEBUG(
									"combat.shield_from_lasers object=%d steps=%u limit=%u front=%d max=%d predicted=%d",
									(int)object_idx,
									(unsigned)
										transfer_count,
									(unsigned)
										transfer_limit,
									g_cur_craft
										->shield_energy
											[0],
									max_shield,
									g_flight_sim_side_effects_suppressed);
							}
						}
					}

					if (g_cur_craft->laser_recharge_level ==
					    POWER_RECHARGE_MAINTENANCE) {
						int total_charge = 0;
						uint16_t charged_slot_count = 0;
						for (uint16_t slot_index = 0;
						     slot_index <
						     g_cur_craft
							     ->laser_slot_count;
						     ++slot_index) {
							if (g_cur_craft
								    ->weapon_slots
									    [slot_index]
								    .projectile_type_id !=
							    0) {
								total_charge +=
									g_cur_craft
										->weapon_slots
											[slot_index]
										.laser_charge;
								++charged_slot_count;
							}
						}
						if (charged_slot_count != 0) {
							int16_t average_charge =
								(int16_t)(total_charge /
									  (int)charged_slot_count);
							if (average_charge <
							    LASER_CHARGE_LOW_THRESHOLD) {
								g_cur_craft
									->laser_recharge_level =
									POWER_RECHARGE_MAXIMUM;
							} else {
								g_cur_craft
									->laser_recharge_level =
									average_charge <
											LASER_CHARGE_HIGH_THRESHOLD
										? POWER_RECHARGE_INCREASED
										: POWER_RECHARGE_MAINTENANCE;
							}
						}
					}
					shield_recharge_rate =
						PLAYER_SHIELD_RECHARGE_RATE;
				} else {
					g_cur_craft->shield_recharge_level =
						POWER_RECHARGE_MAINTENANCE;
					int16_t live_shield_generators = 0;
					g_cur_craft->laser_recharge_level =
						POWER_RECHARGE_MAINTENANCE;
					if (g_flight_mission_state.difficulty ==
						    2 &&
					    genus_id == CRAFT_GENUS_STARSHIP) {
						if (g_cur_craft->hull_damage !=
						    0) {
							if (g_object_table[object_idx]
									    .object_type ==
								    CRAFT_SPECIES_INTERDICTOR ||
							    g_object_table[object_idx]
									    .object_type ==
								    CRAFT_SPECIES_VICTORY_STAR_DESTROYER ||
							    g_object_table[object_idx]
									    .object_type ==
								    CRAFT_SPECIES_IMPERIAL_STAR_DESTROYER ||
							    g_object_table[object_idx]
									    .object_type ==
								    CRAFT_SPECIES_SUPER_STAR_DESTROYER) {
								int mesh_count =
									g_object_table[object_idx]
												.object_type <
											(int)(sizeof(g_object_type_mesh_cache) /
											      sizeof(g_object_type_mesh_cache
													     [0]))
										? g_object_type_mesh_cache
											  [g_object_table[object_idx]
												   .object_type]
												  .mesh_count
										: model_mesh_get_object_type_mesh_count(
											  g_object_table[object_idx]
												  .object_type);
								for (int mesh_index =
									     0;
								     mesh_index <
								     mesh_count;
								     ++mesh_index) {
									mesh_component_type mesh_type =
										g_object_table[object_idx]
													.object_type <
												(int)(sizeof(g_object_type_mesh_cache) /
												      sizeof(g_object_type_mesh_cache
														     [0]))
											? model_mesh_get_cached_object_type_mesh_type(
												  g_object_table[object_idx]
													  .object_type,
												  mesh_index)
											: model_mesh_get_object_type_mesh_type(
												  g_object_table[object_idx]
													  .object_type,
												  mesh_index);
									if (mesh_type ==
										    MESH_COMPONENT_08_SHLD_GEN &&
									    g_cur_craft->component_hp
											    [mesh_index] !=
										    0) {
										++live_shield_generators;
									}
								}
							} else {
								live_shield_generators =
									1;
							}
						}
						g_cur_craft
							->shield_recharge_level =
							POWER_RECHARGE_INCREASED;
					}
					shield_recharge_rate =
						LARGE_CRAFT_SHIELD_RECHARGE_RATE *
						live_shield_generators;
				}
			}
		}

		if (do_periodic_power_update != 0) {
			g_cur_craft = g_object_table[object_idx].mobj->p_craft;
			if (g_object_table[object_idx].object_type ==
			    CRAFT_SPECIES_Y_WING) {
				shield_recharge_rate *= 2;
			}
			if ((g_cur_craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0 &&
			    shield_recharge_rate != 0) {
				int16_t shield_delta =
					(int16_t)(shield_recharge_rate *
						  ((uint8_t)g_cur_craft
							   ->shield_recharge_level -
						   POWER_RECHARGE_MAINTENANCE));
				if (shield_delta != 0) {
					if ((shield_delta > 0 &&
					     (g_cur_craft->shield_energy[0] <
						      2 * g_model_defs[g_cur_craft
									       ->model_index]
								      .shield_strength ||
					      g_cur_craft->shield_energy[1] <
						      2 * g_model_defs[g_cur_craft
									       ->model_index]
								      .shield_strength)) ||
					    (shield_delta < 0 &&
					     (g_cur_craft->shield_energy[0] >
						      0 ||
					      g_cur_craft->shield_energy[1] >
						      0))) {
						XVT_LOG_DEBUG(
							"combat.shield_recharged object=%d slot=%d delta=%d distribution=%d recharge=%d front=%d rear=%d max=%d predicted=%d",
							(int)object_idx,
							g_object_table[object_idx]
								.player_owner_idx,
							(int)shield_delta,
							(int)g_cur_craft
								->shield_distrib_mode,
							(int)g_cur_craft
								->shield_recharge_level,
							g_cur_craft
								->shield_energy
									[0],
							g_cur_craft
								->shield_energy
									[1],
							2 * g_model_defs[g_cur_craft
										 ->model_index]
									.shield_strength,
							g_flight_sim_side_effects_suppressed);
					}
					if (g_cur_craft->shield_distrib_mode ==
					    SHIELD_DISTRIBUTION_FULLY_FORWARD) {
						craft_adjust_current_shield_energy(
							object_idx, 0,
							shield_delta);
					} else if (
						g_cur_craft
							->shield_distrib_mode ==
						SHIELD_DISTRIBUTION_FULLY_AFT) {
						craft_adjust_current_shield_energy(
							object_idx, 1,
							shield_delta);
					} else {
						int16_t half_delta =
							shield_delta / 2;
						craft_adjust_current_shield_energy(
							object_idx, 0,
							half_delta);
						craft_adjust_current_shield_energy(
							object_idx, 1,
							half_delta);
					}
				}
			}

			uint16_t slot_index;
			if ((g_cur_craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0) {
				for (slot_index = 0;
				     slot_index < g_cur_craft->laser_slot_count;
				     ++slot_index) {
					uint8_t projectile_type =
						g_cur_craft
							->weapon_slots
								[slot_index]
							.projectile_type_id;
					if (projectile_type == 0 ||
					    projectile_type ==
						    TURRET_PROJECTILE_TYPE) {
						continue;
					}
					int16_t charge_basis =
						(int16_t)((uint8_t)g_cur_craft
								  ->laser_recharge_level -
							  POWER_RECHARGE_MAINTENANCE);
					if (g_cur_craft->engine_overdrive_off ==
					    ENGINE_OVERDRIVE_ACTIVE) {
						charge_basis =
							(int16_t)((uint8_t)g_cur_craft
									  ->laser_recharge_level -
								  6);
					}
					int16_t charge_delta;
					if (g_object_table[object_idx]
							    .object_type ==
						    CRAFT_SPECIES_TIE_FIGHTER ||
					    g_object_table[object_idx]
							    .object_type ==
						    CRAFT_SPECIES_TIE_BOMBER) {
						charge_delta =
							(int16_t)(3 *
								  charge_basis);
					} else {
						charge_delta =
							(int16_t)(2 *
								  charge_basis);
					}
					g_cur_craft->weapon_slots[slot_index]
						.laser_charge +=
						(int8_t)charge_delta;
					if (charge_delta < 0 &&
					    g_cur_craft
							    ->weapon_slots
								    [slot_index]
							    .laser_charge < 0) {
						g_cur_craft
							->weapon_slots
								[slot_index]
							.laser_charge = 0;
					}
					if (charge_delta > 0 &&
					    g_cur_craft
							    ->weapon_slots
								    [slot_index]
							    .laser_charge < 0) {
						g_cur_craft
							->weapon_slots
								[slot_index]
							.laser_charge =
							MAXIMUM_LASER_CHARGE;
					}
				}
			}

			if (g_cur_craft->engine_overdrive_off ==
			    ENGINE_OVERDRIVE_ACTIVE) {
				int any_laser_charge = 0;
				for (slot_index = 0;
				     slot_index < g_cur_craft->laser_slot_count;
				     ++slot_index) {
					if (g_cur_craft
						    ->weapon_slots[slot_index]
						    .laser_charge > 0) {
						any_laser_charge = 1;
					}
				}
				if (any_laser_charge == 0) {
					g_cur_craft->engine_overdrive_off =
						ENGINE_OVERDRIVE_DISENGAGED;
					XVT_LOG_DEBUG(
						"combat.overdrive_dropped object=%d slot=%d predicted=%d",
						(int)object_idx,
						g_object_table[object_idx]
							.player_owner_idx,
						g_flight_sim_side_effects_suppressed);
					msg_emit_in_flight_message(
						IFMSG_285_ENGINE_OVERDRIVE_BOOSTERS_DISENGAGED,
						g_local_player);
					fsfx_play_sound(FLIGHT_SOUND_POWER_DOWN,
							-1, g_local_player);
				}
			}

			if ((g_cur_craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0) {
				int16_t beam_present =
					(int16_t)(g_cur_craft->beam_charge +
						  BEAM_RECHARGE_STEP *
							  ((uint8_t)g_cur_craft
								   ->beam_recharge_level -
							   POWER_RECHARGE_MAINTENANCE));
				if (beam_present < 0) {
					beam_present = 0;
				}
				if (beam_present > MAXIMUM_BEAM_CHARGE) {
					beam_present = MAXIMUM_BEAM_CHARGE;
				}
				g_cur_craft->beam_charge =
					(uint16_t)beam_present;
				if (beam_present == 0 &&
				    g_cur_craft->beam_active != 0) {
					g_cur_craft->beam_active = 0;
					g_cur_craft->beam_output = 0;
					XVT_LOG_DEBUG(
						"combat.beam_depleted object=%d slot=%d beam=%d cause=\"power\" predicted=%d",
						(int)object_idx,
						g_object_table[object_idx]
							.player_owner_idx,
						(int)g_cur_craft->beam_type_id,
						g_flight_sim_side_effects_suppressed);
					if (g_object_table[object_idx]
						    .player_owner_idx ==
					    g_local_player) {
						msg_emit_in_flight_message(
							(in_flight_message_id)((uint8_t)g_cur_craft
										       ->beam_type_id +
									       BEAM_END_MESSAGE_BASE),
							g_local_player);
					}
				}
			}

			if (g_cur_craft->chaff_active_seconds != 0) {
				--g_cur_craft->chaff_active_seconds;
				if (g_cur_craft->chaff_active_seconds == 0) {
					XVT_LOG_DEBUG(
						"combat.chaff_ended object=%d slot=%d cm=%d predicted=%d",
						(int)object_idx,
						g_object_table[object_idx]
							.player_owner_idx,
						(int)g_cur_craft->cm_type_id,
						g_flight_sim_side_effects_suppressed);
				}
				if (g_cur_craft->cm_type_id ==
					    COUNTERMEASURE_TYPE_CHAFF &&
				    g_cur_craft->chaff_active_seconds == 0 &&
				    g_object_table[object_idx]
						    .player_owner_idx != -1) {
					msg_emit_in_flight_message(
						IFMSG_368_CHAFF_BURST_EXPENDED,
						g_object_table[object_idx]
							.player_owner_idx);
				}
			}
		}
	}

	for (object_idx = g_active_region_object_slot_start;
	     object_idx < g_active_region_craft_object_slot_end; ++object_idx) {
		if (g_object_table[object_idx].object_type ==
			    CRAFT_SPECIES_UNKNOWN ||
		    g_object_table[object_idx].mobj->family != 0) {
			continue;
		}
		g_cur_craft = g_object_table[object_idx].mobj->p_craft;
		if (g_cur_craft->weapon_fire_inhibit_timer != 0) {
			continue;
		}

		/* slotIndex counts cannon groups in the first loop below (the
		 * laser_state arrays, passed to laser_firelasersystem as its
		 * group), weapon slots in the turret loop, then warhead
		 * launchers. */
		uint16_t slot_index;
		if (g_cur_craft->beam_effect_accum[2] == 0 &&
		    (!xvt_flight_timing_is_unlocked() ||
		     g_object_table[object_idx].player_owner_idx != -1 ||
		     xvt_flight_timing_reference_due())) {
			struct xvt_flight_clock cannon_clock = {
				g_elapsed_ticks, g_sim_steps_per_second};
			if (g_object_table[object_idx].player_owner_idx == -1) {
				cannon_clock =
					xvt_flight_timing_enter_reference();
			}

			for (slot_index = 0;
			     slot_index < g_cur_craft->cannon_group_count;
			     ++slot_index) {
				int16_t cooldown = g_cur_craft->laser_state
							   .fire_cooldown_ticks
								   [slot_index];
				if (cooldown != 0) {
					cooldown = (int16_t)(cooldown -
							     g_elapsed_ticks);
					if (cooldown < 0) {
						cooldown = 0;
					}
					g_cur_craft->laser_state
						.fire_cooldown_ticks
							[slot_index] = cooldown;
				}
				if (g_object_table[object_idx]
						    .player_owner_idx == -1 &&
				    (int16_t)g_elapsed_ticks > cooldown &&
				    g_cur_craft->laser_state
						    .link_mode[slot_index] !=
					    0) {
					if ((g_cur_craft->working_subsystems &
					     CRAFT_SUBSYSTEM_FLAG_CANNONS) !=
						    0 &&
					    g_cur_craft->object_kind ==
						    CRAFT_OBJECT_KIND_ACTIVE) {
						laser_firelasersystem(
							object_idx, slot_index);
					}
					--g_cur_craft->laser_state
						  .burst_remaining[slot_index];
					g_cur_craft->laser_state
						.fire_cooldown_ticks
							[slot_index] +=
						2 * g_elapsed_ticks;
					g_cur_craft->laser_state
						.next_fire_timestamp
							[slot_index] +=
						2 * g_elapsed_ticks;
					if (g_cur_craft->laser_state
						    .burst_remaining
							    [slot_index] == 0) {
						g_cur_craft->laser_state
							.link_mode[slot_index] =
							0;
					}
				}
			}

			xvt_flight_timing_restore_clock(cannon_clock);
		}

		for (slot_index = 0; slot_index < g_cur_craft->laser_slot_count;
		     ++slot_index) {
			if (g_cur_craft->weapon_slots[slot_index]
				    .projectile_type_id ==
			    TURRET_PROJECTILE_TYPE) {
				uint16_t target_obj_idx =
					g_cur_craft
						->turret_target_states
							[slot_index]
						.target_obj_idx;
				if (target_obj_idx != UINT16_MAX) {

					if (xvt_flight_timing_reference_due()) {
						struct xvt_flight_clock weapon_clock =
							xvt_flight_timing_enter_reference();
						laser_fireturretslot(
							object_idx, slot_index,
							target_obj_idx);
						xvt_flight_timing_restore_clock(
							weapon_clock);
					}
				}
			}
		}

		for (slot_index = 0;
		     slot_index < g_cur_craft->warhead_launcher_count;
		     ++slot_index) {
			int16_t cooldown =
				g_cur_craft->warhead_launcher_cooldown_ticks
					[slot_index];
			if (cooldown != 0) {
				cooldown =
					(int16_t)(cooldown - g_elapsed_ticks);
				if (cooldown < 0) {
					cooldown = 0;
				}
				g_cur_craft->warhead_launcher_cooldown_ticks
					[slot_index] = cooldown;
			}
		}
	}

	for (object_idx = g_region_main_object_slot_end;
	     object_idx <
	     g_region_main_object_slot_end + g_region_static_object_slot_count;
	     ++object_idx) {
		if (g_object_table[object_idx].object_type !=
			    CRAFT_SPECIES_UNKNOWN &&
		    g_object_table[object_idx].genus_id == CRAFT_GENUS_MINE) {

			if (xvt_flight_timing_reference_due()) {
				struct xvt_flight_clock weapon_clock =
					xvt_flight_timing_enter_reference();
				laser_update_mine_weapon_fire(object_idx);
				xvt_flight_timing_restore_clock(weapon_clock);
			}
		}
	}
}

/* Returns the life of a shot of projectile_object_type in ticks: 236 for each
 * of its lifetimeSeconds plus its lifetime_frac_q16 share of 236, rounded.
 * Does not check the type. */
// FUNCTION: XVT 0x405860
uint16_t laser_get_projectile_lifetime_ticks(int projectile_object_type)
{
	uint16_t whole_seconds_ticks =
		(uint16_t)(236u * g_projectile_type_data.lifetime_seconds
					  [projectile_object_type -
					   PROJECTILE_OBJECT_TYPE_FIRST]);
	whole_seconds_ticks =
		(uint16_t)(whole_seconds_ticks +
			   math2_fraction(
				   g_projectile_type_data.lifetime_frac_q16
					   [projectile_object_type -
					    PROJECTILE_OBJECT_TYPE_FIRST],
				   236u));
	return whole_seconds_ticks;
}

/* Fires the selected weapon of player_idx's craft, when it can. Does nothing
 * when the player has no craft, and only reports IFMSG_361 (firing jammed)
 * while a jamming beam holds the craft (beam_effect_accum[2]) and it has no
 * active chaff. In cannon mode the selected group fires through
 * laser_firelasersystem once its cooldown is below 1.5 times
 * g_elapsed_ticks, or with the cannons out the local player gets a system
 * message; while g_laser_fire_timestamp_tracking_enabled is set, the group's
 * next_fire_timestamp against the player's lockstep_timestamp decides the
 * cooldown instead. In warhead mode the selected launcher fires through
 * laser_firewarheadsystem on the same cooldown test, or with the launchers
 * out the local player gets a system message; when both of the launcher's
 * slots are then empty, the player goes back to cannon group 0 with a
 * 118-tick cooldown. */
// FUNCTION: XVT 0x405890
void laser_fireplayerweapon(int player_idx)
{
	enum {
		WEAPON_COOLDOWN_TICKS = 118,
		SYSTEM_NAME_MESSAGE_ARG = 87,
		LASER_SYSTEM_NAME_BASE = 92,
		WARHEAD_SYSTEM_NAME = 94
	};

	int object_index = g_players[player_idx].object_index;

	if (object_index == -1) {
		return;
	}
	struct craft_data *craft = g_object_table[object_index].mobj->p_craft;
	if (craft->beam_effect_accum[2] != 0 &&
	    (craft->cm_type_id != COUNTERMEASURE_TYPE_CHAFF ||
	     craft->chaff_active_seconds == 0)) {
		msg_emit_in_flight_message(
			IFMSG_361_WEAPON_FIRING_JAMMED_BY_BEAM_SYSTEM,
			player_idx);
		return;
	}
	if (g_players[player_idx].selected_weapon_mode == 0) {
		int selected_weapon =
			g_players[player_idx].selected_weapon_bank;
		int16_t cooldown =
			craft->laser_state.fire_cooldown_ticks[selected_weapon];
		if (g_laser_fire_timestamp_tracking_enabled != 0) {
			int lockstep_timestamp =
				g_players[player_idx].lockstep_timestamp;
			if (cooldown != 0) {
				int last_fire_timestamp =
					craft->laser_state.next_fire_timestamp
						[selected_weapon];
				if (last_fire_timestamp < lockstep_timestamp) {
					cooldown = 0;
					craft->laser_state.next_fire_timestamp
						[selected_weapon] =
						lockstep_timestamp;
				} else {
					cooldown =
						(int16_t)(2 * g_elapsed_ticks);
				}
			} else {
				craft->laser_state
					.next_fire_timestamp[selected_weapon] =
					lockstep_timestamp;
			}
		}
		if ((int16_t)(g_elapsed_ticks + (g_elapsed_ticks >> 1)) >
		    cooldown) {
			if ((craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0) {
				laser_firelasersystem(
					g_players[player_idx].object_index,
					g_players[player_idx]
						.selected_weapon_bank);
			} else if (player_idx == g_local_player) {
				int16_t selected_warhead =
					g_players[player_idx]
						.selected_weapon_bank;
				g_msg_arg_table[1] = SYSTEM_NAME_MESSAGE_ARG;
				g_msg_arg_table[0] =
					(uint16_t)(selected_warhead +
						   LASER_SYSTEM_NAME_BASE);
				msg_emit_in_flight_message(
					IFMSG_086_ARG_SYSTEM_IS_ARG,
					player_idx);
			}
		}
		return;
	}
	if (craft->warhead_launcher_cooldown_ticks
		    [g_players[player_idx].selected_weapon_bank] <
	    (int16_t)(g_elapsed_ticks + (g_elapsed_ticks >> 1))) {
		if ((craft->working_subsystems &
		     CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER) != 0) {
			laser_firewarheadsystem(
				object_index,
				g_players[player_idx].selected_weapon_bank);
			craft = g_object_table[object_index].mobj->p_craft;
			{
				int first_slot =
					g_model_defs[craft->model_index].warhead_launcher_first_slot
						[g_players[player_idx]
							 .selected_weapon_bank];
				if (craft->weapon_slots[first_slot + 1]
						    .ammo_count +
					    craft->weapon_slots[first_slot]
						    .ammo_count ==
				    0) {
					XVT_LOG_DEBUG(
						"combat.launcher_empty slot=%d object=%d launcher=%d predicted=%d",
						player_idx, object_index,
						(int)g_players[player_idx]
							.selected_weapon_bank,
						g_flight_sim_side_effects_suppressed);
					g_players[player_idx]
						.selected_weapon_mode = 0;
					g_players[player_idx]
						.selected_weapon_bank = 0;
					craft->laser_state
						.fire_cooldown_ticks[0] =
						WEAPON_COOLDOWN_TICKS;
					craft->laser_state
						.next_fire_timestamp[0] =
						g_players[player_idx]
							.lockstep_timestamp +
						WEAPON_COOLDOWN_TICKS;
				}
			}
		} else if (player_idx == g_local_player) {
			g_msg_arg_table[0] = WARHEAD_SYSTEM_NAME;
			g_msg_arg_table[1] = SYSTEM_NAME_MESSAGE_ARG;
			msg_emit_in_flight_message(IFMSG_086_ARG_SYSTEM_IS_ARG,
						   player_idx);
		}
	}
}

/* Fires cannon group laser_system_index of the craft in object_index by its
 * link_mode: 1 the next slot alone, 2 every other slot from the next one
 * (half the group's slots), 3 every slot; the group's next_slot moves on.
 * Only slots with a weapon and charge above 0 fire. Each shot is
 * laser_createprojectile of the group's weapon type, one type higher when
 * the slot's charge is 64 or more, given the player's or the AI's current
 * target in its guidance record; unless the flight group's status is 21 it
 * costs the slot 3 charge on a player's TIE Fighter or TIE Bomber, 4 on
 * another player's craft and 1 on an AI craft. Adds the shots to the ion
 * or laser counts in weapon_stats (and the player's mission_stats), and 47
 * per shot plus 2 to the group's fire_cooldown_ticks and next_fire_timestamp.
 * With the S-foils closed nothing fires and the local player gets a
 * message. With any other link_mode the modern build returns at once; the
 * original build's text goes on with first_slot and last_slot unset. Writes
 * g_cur_craft. */
// FUNCTION: XVT 0x405AC0
void laser_firelasersystem(int object_index, int laser_system_index)
{
	int owner_player_idx = g_object_table[object_index].player_owner_idx;
	model_index model_index;
	uint16_t first_slot;
	uint16_t current_slot;
	uint16_t last_slot;
	uint16_t slot_step;
	uint16_t shot_limit;
	uint16_t shots_fired;
	struct ai_controller *ai_controller;

	g_cur_craft = g_object_table[object_index].mobj->p_craft;
	ai_controller = &g_cur_craft->ai_controller;
	model_index = g_cur_craft->model_index;
	if (g_cur_craft->s_foil_state != 0) {
		if (owner_player_idx == g_local_player) {
			msg_emit_in_flight_message(
				IFMSG_130_CANNONS_CANNOT_FIRE_WITH_S_FOILS_CLOSED,
				g_local_player);
		}
		return;
	}
	shots_fired = 0;
	switch (g_cur_craft->laser_state.link_mode[laser_system_index]) {
	case 1:
		if (g_model_defs[model_index].laser_group_first_slot
				    [laser_system_index] >
			    g_cur_craft->laser_state
				    .next_slot[laser_system_index] ||
		    g_model_defs[model_index]
				    .laser_group_last_slot[laser_system_index] <
			    g_cur_craft->laser_state
				    .next_slot[laser_system_index]) {
			g_cur_craft->laser_state.next_slot[laser_system_index] =
				g_model_defs[model_index].laser_group_first_slot
					[laser_system_index];
		}
		first_slot = g_cur_craft->laser_state
				     .next_slot[laser_system_index]++;
		last_slot = first_slot;
		if (g_model_defs[model_index]
			    .laser_group_last_slot[laser_system_index] <
		    g_cur_craft->laser_state.next_slot[laser_system_index]) {
			g_cur_craft->laser_state.next_slot[laser_system_index] =
				(uint8_t)g_model_defs[model_index]
					.laser_group_first_slot
						[laser_system_index];
		}
		shot_limit = 1;
		slot_step = 1;
		break;
	case 2:
		if (g_model_defs[model_index].laser_group_first_slot
				    [laser_system_index] >
			    g_cur_craft->laser_state
				    .next_slot[laser_system_index] ||
		    g_model_defs[model_index]
				    .laser_group_last_slot[laser_system_index] <
			    g_cur_craft->laser_state
				    .next_slot[laser_system_index]) {
			g_cur_craft->laser_state.next_slot[laser_system_index] =
				g_model_defs[model_index].laser_group_first_slot
					[laser_system_index];
		}
		first_slot =
			g_cur_craft->laser_state.next_slot[laser_system_index];
		g_cur_craft->laser_state.next_slot[laser_system_index] ^= 1;
		if (g_cur_craft->laser_state.next_slot[laser_system_index] >
		    g_model_defs[model_index]
			    .laser_group_last_slot[laser_system_index]) {
			g_cur_craft->laser_state.next_slot[laser_system_index] =
				(uint8_t)g_model_defs[model_index]
					.laser_group_first_slot
						[laser_system_index];
		}
		last_slot = g_model_defs[model_index]
				    .laser_group_last_slot[laser_system_index];
		shot_limit =
			(last_slot -
			 g_model_defs[model_index]
				 .laser_group_first_slot[laser_system_index] +
			 1) /
			2;
		slot_step = 2;
		break;
	case 3:
		first_slot =
			g_model_defs[model_index]
				.laser_group_first_slot[laser_system_index];
		last_slot = g_model_defs[model_index]
				    .laser_group_last_slot[laser_system_index];
		slot_step = 1;
		shot_limit = last_slot - first_slot + 1;
		break;
	default:
		XVT_LOG_DEBUG(
			"combat.fire_mode_invalid object=%d slot=%d bank=%d link=%d predicted=%d",
			object_index, owner_player_idx, laser_system_index,
			(int)g_cur_craft->laser_state
				.link_mode[laser_system_index],
			g_flight_sim_side_effects_suppressed);
		return;
	}
	current_slot = first_slot;
	if (current_slot <= last_slot) {
		do {
			if (g_cur_craft->weapon_slots[current_slot]
					    .projectile_type_id != 0 &&
			    g_cur_craft->weapon_slots[current_slot]
					    .laser_charge > 0) {
				/* From here first_slot holds the projectile
				 * type to fire: the group's weapon type, one
				 * higher when the slot's charge is 64 or more.
				 * The ion check after the loop reads it; if no
				 * slot fired it still holds the first slot. */
				first_slot =
					g_model_defs[model_index]
						.laser_group_weapon_type
							[laser_system_index];
				if (g_cur_craft->weapon_slots[current_slot]
					    .laser_charge >= 64) {
					++first_slot;
				}
				{
					unsigned int projectile_index =
						laser_createprojectile(
							object_index,
							current_slot,
							first_slot);
					if (projectile_index != UINT_MAX) {
						if (g_mission_flight_groups
								    [g_object_table[object_index]
									     .flight_group_idx]
									    .fg
									    .status1 !=
							    21 &&
						    g_mission_flight_groups
								    [g_object_table[object_index]
									     .flight_group_idx]
									    .fg
									    .status2 !=
							    21) {
							if (owner_player_idx !=
							    -1) {
								if (get_model_index_from_type(
									    5) ==
									    model_index ||
								    get_model_index_from_type(
									    7) ==
									    model_index) {
									g_cur_craft
										->weapon_slots
											[current_slot]
										.laser_charge -=
										3;
								} else {
									g_cur_craft
										->weapon_slots
											[current_slot]
										.laser_charge -=
										4;
								}
							} else {
								g_cur_craft
									->weapon_slots
										[current_slot]
									.laser_charge--;
							}
						}
						if (shot_limit >= 2) {
							if ((shots_fired & 1) ==
							    0) {
								fsfx_triggerweaponsfx(
									projectile_index,
									owner_player_idx);
							}
						} else if (shots_fired < 2) {
							fsfx_triggerweaponsfx(
								projectile_index,
								owner_player_idx);
						}
						if (g_cur_craft
							    ->weapon_slots
								    [current_slot]
							    .laser_charge < 0) {
							g_cur_craft
								->weapon_slots
									[current_slot]
								.laser_charge =
								0;
						}
						{
							/* From here
							 * projectile_index
							 * indexes
							 * g_projectile_guidance_states. */
							projectile_index -=
								g_projectile_object_slot_start;
							if (owner_player_idx !=
							    -1) {
								g_projectile_guidance_states
									[projectile_index]
										.target_obj_idx =
									g_players[owner_player_idx]
										.current_target_object_idx;
								if ((uint16_t)g_players
									    [owner_player_idx]
										    .current_target_object_idx !=
								    UINT16_MAX) {
									unsigned int target_object_index =
										(uint16_t)g_players
											[owner_player_idx]
												.current_target_object_idx;
									g_projectile_guidance_states
										[projectile_index]
											.target_signature =
										g_object_table[target_object_index]
											.object_signature;
								} else {
									g_projectile_guidance_states
										[projectile_index]
											.target_signature =
										0;
								}
							} else {
								g_projectile_guidance_states
									[projectile_index]
										.target_obj_idx =
									ai_controller
										->target_obj_idx;
								if (ai_controller
									    ->target_obj_idx !=
								    UINT16_MAX) {
									if (ai_controller
										    ->target_obj_idx <
									    0x8000) {
										g_projectile_guidance_states
											[projectile_index]
												.target_signature =
											g_object_table
												[ai_controller
													 ->target_obj_idx]
													.object_signature;
									} else {
										g_projectile_guidance_states
											[projectile_index]
												.target_signature =
											0;
									}
								} else {
									g_projectile_guidance_states
										[projectile_index]
											.target_signature =
										0;
								}
							}
							++shots_fired;
							g_projectile_guidance_states
								[projectile_index]
									.source_player_idx =
								(int8_t)owner_player_idx;
						}
					}
				}
			}
			--shot_limit;
			if (shot_limit == 0) {
				break;
			}
			current_slot += slot_step;
		} while (current_slot <= last_slot);
	}
	if (first_slot == PROJECTILE_OBJECT_TYPE_ION_LASER ||
	    first_slot == PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER) {
		g_cur_craft->weapon_stats.ion_shots_fired +=
			(uint16_t)shots_fired;
		if (owner_player_idx != -1) {
			g_players[owner_player_idx]
				.mission_stats.ion_shots_fired +=
				(uint16_t)shots_fired;
		}
	} else {
		g_cur_craft->weapon_stats.laser_shots_fired +=
			(uint16_t)shots_fired;
		if (owner_player_idx != -1) {
			g_players[owner_player_idx]
				.mission_stats.laser_shots_fired +=
				(uint16_t)shots_fired;
		}
	}
	g_cur_craft->laser_state.fire_cooldown_ticks[laser_system_index] +=
		(int16_t)(47 * shots_fired + 2);
	g_cur_craft->laser_state.next_fire_timestamp[laser_system_index] +=
		47 * shots_fired + 2;
	if (owner_player_idx != -1 && shots_fired != 0) {
		XVT_LOG_DEBUG(
			"combat.cannons_fired slot=%d object=%d bank=%d link=%d shots=%u type=%d target=%d cooldown=%d fired=%u predicted=%d",
			owner_player_idx, object_index, laser_system_index,
			(int)g_cur_craft->laser_state
				.link_mode[laser_system_index],
			(unsigned)shots_fired, (int)first_slot,
			(int)g_players[owner_player_idx]
				.current_target_object_idx,
			(int)g_cur_craft->laser_state
				.fire_cooldown_ticks[laser_system_index],
			(unsigned)(first_slot == PROJECTILE_OBJECT_TYPE_ION_LASER ||
						   first_slot ==
							   PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER
					   ? g_players[owner_player_idx]
						     .mission_stats
						     .ion_shots_fired
					   : g_players[owner_player_idx]
						     .mission_stats
						     .laser_shots_fired),
			g_flight_sim_side_effects_suppressed);
	}
}

/* Fires launcher launcher_index of the craft in object_index through
 * laser_firemissile: both of its slots when the low 7 bits of
 * warhead_launcher_flags are 3, else the slot flag bit 0x80 picks (the
 * second when set). Adds 472 ticks to the launcher's cooldown whether or
 * not anything fired. For the local player's craft, unless a slot that
 * failed still holds rounds, it shows the message for no shot, one or two,
 * by the warhead kind of the player's selected launcher. Writes
 * g_cur_craft. */
// FUNCTION: XVT 0x406030
void laser_firewarheadsystem(int object_index, unsigned int launcher_index)
{
	g_cur_craft = g_object_table[object_index].mobj->p_craft;
	int16_t shots_fired;
	int16_t incomplete;
	{
		unsigned int launcher_flags =
			(uint8_t)g_cur_craft
				->warhead_launcher_flags[launcher_index];
		uint16_t launcher_slot =
			g_model_defs[g_cur_craft->model_index]
				.warhead_launcher_first_slot[launcher_index];
		shots_fired = 0;
		incomplete = 0;
		if (((uint16_t)launcher_flags & 0x7F) == 3) {
			int weapon_slot_index = launcher_slot;
			if (laser_firemissile(object_index, weapon_slot_index,
					      g_cur_craft->warhead_slot_type_ids
						      [launcher_index],
					      launcher_index) != -1) {
				shots_fired = 1;
			} else if (g_cur_craft->weapon_slots[weapon_slot_index]
					   .ammo_count != 0) {
				incomplete = 1;
			}
			++launcher_slot;
			if (laser_firemissile(object_index, launcher_slot,
					      g_cur_craft->warhead_slot_type_ids
						      [launcher_index],
					      launcher_index) != -1) {
				++shots_fired;
			} else if (g_cur_craft->weapon_slots[launcher_slot]
					   .ammo_count != 0) {
				incomplete = 1;
			}
		} else {
			if (((uint16_t)launcher_flags & 0x80) != 0) {
				++launcher_slot;
				if (laser_firemissile(
					    object_index, launcher_slot,
					    g_cur_craft->warhead_slot_type_ids
						    [launcher_index],
					    launcher_index) != -1) {
					shots_fired = 1;
				} else if (g_cur_craft
						   ->weapon_slots[launcher_slot]
						   .ammo_count != 0) {
					incomplete = 1;
				}
			} else {
				if (laser_firemissile(
					    object_index, launcher_slot,
					    g_cur_craft->warhead_slot_type_ids
						    [launcher_index],
					    launcher_index) != -1) {
					shots_fired = 1;
				} else if (g_cur_craft
						   ->weapon_slots[launcher_slot]
						   .ammo_count != 0) {
					incomplete = 1;
				}
			}
		}
	}
	g_cur_craft->warhead_launcher_cooldown_ticks[launcher_index] += 472;
	XVT_LOG_DEBUG(
		"combat.launcher_fired object=%d launcher=%u shots=%d incomplete=%d cooldown=%d predicted=%d",
		object_index, (unsigned)launcher_index, (int)shots_fired,
		(int)incomplete,
		(int)g_cur_craft
			->warhead_launcher_cooldown_ticks[launcher_index],
		g_flight_sim_side_effects_suppressed);
	if (g_object_table[object_index].player_owner_idx == g_local_player &&
	    incomplete == 0) {
		struct player_data *player = &g_players[g_local_player];
		warhead_kind_index warhead_kind =
			object_type_get_warhead_kind_index(
				g_cur_craft->warhead_slot_type_ids
					[player->selected_weapon_bank]);
		if (shots_fired == 0) {
			msg_emit_in_flight_message(
				(in_flight_message_id)((uint16_t)warhead_kind +
						       38),
				g_local_player);
		} else if (shots_fired == 1) {
			msg_emit_in_flight_message(
				(in_flight_message_id)((uint16_t)warhead_kind +
						       48),
				g_local_player);
		} else if (shots_fired == 2) {
			msg_emit_in_flight_message(
				(in_flight_message_id)((uint16_t)warhead_kind +
						       58),
				g_local_player);
		}
	}
}

/* Fires one warhead of projectile_type_id from weapon slot weapon_slot_index of
 * the craft in object_index, when the slot has a weapon and rounds left.
 * Counts it in weapon_stats.warheads_fired (and the player's warheads_fired),
 * takes its warhead_point_value off the player's mission_score and the team's
 * mission score, plays the weapon sound, and uses a round unless the
 * flight group's status is 21. Fills the guidance record: for launchers 0
 * and 1 a homing_tier of the craft's whole simulated seconds of lock (at
 * most 6); the player's or the AI's target, target component and
 * signature; and source_player_idx. Warns a player whose craft is the target
 * (laser_warnplayer). For launchers 0 and 1 it sets flag bit 0x80 so the
 * next shot comes from the slot with more rounds (the first on a tie).
 * Returns the shot's index in g_projectile_guidance_states, or -1 when
 * nothing fired. Expects g_cur_craft to be the craft of object_index and
 * does not check it. */
// FUNCTION: XVT 0x406290
int laser_firemissile(int object_index, int weapon_slot_index,
		      int projectile_type_id, unsigned int launcher_index)
{
	int owner_player_idx = g_object_table[object_index].player_owner_idx;
	struct ai_controller *controller = &g_cur_craft->ai_controller;
	int projectile_index = -1;

	if ((weapon_slot_index + g_cur_craft->weapon_slots)
			    ->projectile_type_id != 0 &&
	    g_cur_craft->weapon_slots[weapon_slot_index].ammo_count != 0) {
		projectile_index = laser_createprojectile(
			object_index, weapon_slot_index, projectile_type_id);
		if (projectile_index != -1) {
			++g_cur_craft->weapon_stats.warheads_fired;
			if (owner_player_idx != -1) {
				++g_players[owner_player_idx].warheads_fired;
				g_players[owner_player_idx]
					.mission_stats.mission_score -=
					g_projectile_type_data.warhead_point_value
						[projectile_type_id -
						 PROJECTILE_OBJECT_TYPE_FIRST];
			}
			g_flight_mission_state.runtime.team_scores
				[TEAM_SCORE_MISSION]
				[g_mission_flight_groups
					 [g_object_table[object_index]
						  .flight_group_idx]
						 .fg.team] -=
				g_projectile_type_data.warhead_point_value
					[projectile_type_id -
					 PROJECTILE_OBJECT_TYPE_FIRST];
			fsfx_triggerweaponsfx((unsigned int)projectile_index,
					      owner_player_idx);
			if (g_mission_flight_groups[g_object_table[object_index]
							    .flight_group_idx]
					    .fg.status1 != 21 &&
			    g_mission_flight_groups[g_object_table[object_index]
							    .flight_group_idx]
					    .fg.status2 != 21) {
				--g_cur_craft->weapon_slots[weapon_slot_index]
					  .ammo_count;
			}

			/* From here projectile_index indexes
			 * g_projectile_guidance_states, and that index is what
			 * this function returns. */
			projectile_index -= g_projectile_object_slot_start;
			if (launcher_index < 2) {
				g_projectile_guidance_states[projectile_index]
					.homing_tier =
					(uint8_t)(g_cur_craft
							  ->warhead_lock_ticks /
						  SIMULATION_TICKS_PER_SECOND);
				if (g_projectile_guidance_states
					    [projectile_index]
						    .homing_tier > 6) {
					g_projectile_guidance_states
						[projectile_index]
							.homing_tier = 6;
				}
			}
			if (owner_player_idx != -1) {
				g_projectile_guidance_states[projectile_index]
					.target_obj_idx =
					(uint16_t)g_players[owner_player_idx]
						.current_target_object_idx;
				g_projectile_guidance_states[projectile_index]
					.target_component_idx =
					(uint16_t)g_players[owner_player_idx]
						.selected_target_component;
				if ((uint16_t)g_players[owner_player_idx]
					    .current_target_object_idx !=
				    UINT16_MAX) {
					g_projectile_guidance_states
						[projectile_index]
							.target_signature =
						g_object_table
							[(uint16_t)g_players
								 [owner_player_idx]
									 .current_target_object_idx]
								.object_signature;
				} else {
					g_projectile_guidance_states
						[projectile_index]
							.target_signature = 0;
				}
			} else {
				g_projectile_guidance_states[projectile_index]
					.target_obj_idx =
					controller->target_obj_idx;
				g_projectile_guidance_states[projectile_index]
					.target_component_idx =
					controller->target_component;
				if (controller->target_obj_idx != UINT16_MAX) {
					if (controller->target_obj_idx <
					    0x8000) {
						g_projectile_guidance_states
							[projectile_index]
								.target_signature =
							g_object_table[controller
									       ->target_obj_idx]
								.object_signature;
					} else {
						g_projectile_guidance_states
							[projectile_index]
								.target_signature =
							0;
					}
				} else {
					g_projectile_guidance_states
						[projectile_index]
							.target_signature = 0;
				}
			}
			g_projectile_guidance_states[projectile_index]
				.source_player_idx = (int8_t)owner_player_idx;
			if (g_projectile_guidance_states[projectile_index]
					    .target_obj_idx != UINT16_MAX &&
			    g_object_table[g_projectile_guidance_states
						   [projectile_index]
							   .target_obj_idx]
					    .player_owner_idx != -1) {
				laser_warnplayer((uint16_t)projectile_index);
			}
			if (launcher_index < 2) {
				int launcher_slot =
					g_model_defs[g_cur_craft->model_index]
						.warhead_launcher_first_slot
							[launcher_index];
				if (g_cur_craft->weapon_slots[launcher_slot]
					    .ammo_count >=
				    g_cur_craft->weapon_slots[launcher_slot + 1]
					    .ammo_count) {
					g_cur_craft->warhead_launcher_flags
						[launcher_index] &=
						(int8_t)~0x80;
				} else {
					g_cur_craft->warhead_launcher_flags
						[launcher_index] |=
						(int8_t)0x80;
				}
			}
			XVT_LOG_DEBUG(
				"combat.warhead_fired projectile=%d source=%d slot=%d type=%d launcher=%u weapon_slot=%d target=%d homing=%d ammo=%d cost=%u score=%d team_score=%d predicted=%d",
				projectile_index +
					g_projectile_object_slot_start,
				object_index, owner_player_idx,
				projectile_type_id, (unsigned)launcher_index,
				weapon_slot_index,
				(int)(int16_t)g_projectile_guidance_states
					[projectile_index]
						.target_obj_idx,
				(int)g_projectile_guidance_states
					[projectile_index]
						.homing_tier,
				(int)g_cur_craft
					->weapon_slots[weapon_slot_index]
					.ammo_count,
				(unsigned)g_projectile_type_data
					.warhead_point_value
						[projectile_type_id -
						 PROJECTILE_OBJECT_TYPE_FIRST],
				owner_player_idx != -1
					? g_players[owner_player_idx]
						  .mission_stats.mission_score
					: 0,
				g_flight_mission_state.runtime.team_scores
					[TEAM_SCORE_MISSION]
					[g_mission_flight_groups
						 [g_object_table[object_index]
							  .flight_group_idx]
							 .fg.team],
				g_flight_sim_side_effects_suppressed);
		}
	}
	return projectile_index;
}

/* Creates a shot of projectile_object_type from weapon slot weapon_slot_index
 * of the object in source_object_index and returns its slot, or -1 when none
 * is free. A player's shot takes the first free slot of that player's 12
 * (of the last 4 for a warhead), else of the 32 shared player slots; any
 * other shot a free slot of the other-shot range. The shot copies the
 * firer's IFF and angles, flies at its type's speed plus the firer's, does
 * its type's damage plus the firer's speed (at least its type's damage),
 * lives laser_get_projectile_lifetime_ticks, and starts at the weapon
 * hardpoint (twice as far out for an Imperial Star Destroyer, object type
 * 53). A warhead from a freighter, starship or platform is then moved
 * launch_offset up or down and points straight up or down; any other shot
 * is moved launch_offset along the firer's forward axis and takes the
 * firer's move vector and axes. prevWorld* keeps the hardpoint position,
 * and a player's shot starts at the player's lockstep_timestamp. Its
 * guidance record is reset (no target, no homing, cruise_speed its speed)
 * and linked. Writes g_rotated*. */
// FUNCTION: XVT 0x4065D0
int laser_createprojectile(int source_object_index, int weapon_slot_index,
			   int projectile_object_type)
{
	uint16_t projectile_index;
	uint16_t projectile_genus;

	if (g_object_table[source_object_index].player_owner_idx != -1) {
		projectile_genus = CRAFT_GENUS_PLAYER_PROJECTILE;
		projectile_index =
			(uint16_t)(12 * g_object_table[source_object_index]
						   .player_owner_idx +
				   g_object_slot_range_by_genus
					   [CRAFT_GENUS_PLAYER_PROJECTILE]
						   .start);
		uint16_t range_end = (uint16_t)(projectile_index + 12);
		if (g_projectile_type_data
			    .warhead_class[projectile_object_type -
					   PROJECTILE_OBJECT_TYPE_FIRST] != 0) {
			projectile_index = (uint16_t)(projectile_index + 8);
		}
		for (; projectile_index < range_end; ++projectile_index) {
			if (g_object_table[projectile_index].object_type == 0) {
				g_object_table[projectile_index]
					.mobj->source_obj_idx = 0;
				g_object_table[projectile_index]
					.mobj->effect_size = 0;
				break;
			}
		}
		if (projectile_index < range_end) {
			collide_reset_object_proximity_for_slot(
				projectile_index);
		} else {
			projectile_index =
				(uint16_t)(g_object_slot_range_by_genus
						   [CRAFT_GENUS_PLAYER_PROJECTILE]
							   .start +
					   96);
			range_end = (uint16_t)(projectile_index + 32);
			for (; projectile_index < range_end;
			     ++projectile_index) {
				if (g_object_table[projectile_index]
					    .object_type == 0) {
					g_object_table[projectile_index]
						.mobj->source_obj_idx = 0;
					g_object_table[projectile_index]
						.mobj->effect_size = 0;
					break;
				}
			}
			if (projectile_index < range_end) {
				collide_reset_object_proximity_for_slot(
					projectile_index);
			} else {
				if (g_flight_sim_side_effects_suppressed == 0) {
					XVT_LOG_WARN(
						"combat.shot_no_slot slot=%d source=%d type=%d",
						g_object_table
							[source_object_index]
								.player_owner_idx,
						source_object_index,
						projectile_object_type);
				}
				return -1;
			}
		}
	} else {
		projectile_genus = CRAFT_GENUS_OTHER_PROJECTILE;
		projectile_index =
			object_alloc_slot_for_genus(projectile_genus);
	}
	if (projectile_index != UINT16_MAX) {
		struct object_record *source =
			&g_object_table[source_object_index];
		int16_t source_type = source->object_type;
		g_object_table[projectile_index].mobj->family = 1;
		g_object_table[projectile_index].genus_id =
			(uint8_t)projectile_genus;
		g_object_table[projectile_index].object_type =
			(uint8_t)projectile_object_type;
		g_object_table[projectile_index].mobj->seconds_alive = 1;
		g_object_table[projectile_index].mobj->source_obj_idx =
			(uint16_t)source_object_index;
		g_object_table[projectile_index].mobj->source_object_type =
			(uint8_t)source_type;
		model_index model_index =
			get_model_index_from_type(source_type);
		g_object_table[projectile_index].mobj->iff = source->mobj->iff;
		g_object_table[projectile_index].pitch = source->pitch;
		g_object_table[projectile_index].roll = source->roll;
		g_object_table[projectile_index].yaw = source->yaw;
		g_object_table[projectile_index].mobj->speed =
			(uint16_t)(source->mobj->speed +
				   g_projectile_type_data.speed
					   [projectile_object_type -
					    PROJECTILE_OBJECT_TYPE_FIRST]);
		g_projectile_guidance_states[projectile_index -
					     g_projectile_object_slot_start]
			.cruise_speed =
			g_object_table[projectile_index].mobj->speed;
		g_object_table[projectile_index].mobj->damage_amount =
			source->mobj->speed +
			g_projectile_type_data
				.damage[projectile_object_type -
					PROJECTILE_OBJECT_TYPE_FIRST];
		if (g_object_table[projectile_index].mobj->damage_amount <
		    g_projectile_type_data
			    .damage[projectile_object_type -
				    PROJECTILE_OBJECT_TYPE_FIRST]) {
			g_object_table[projectile_index].mobj->damage_amount =
				g_projectile_type_data
					.damage[projectile_object_type -
						PROJECTILE_OBJECT_TYPE_FIRST];
		}
		g_object_table[projectile_index].mobj->lifetime_timer =
			laser_get_projectile_lifetime_ticks(
				projectile_object_type);
		int world_x = source->world_x;
		int world_y = source->world_y;
		int world_z = source->world_z;
		int16_t hardpoint_z =
			g_model_defs[model_index]
				.weapon_hardpoints[weapon_slot_index]
				.z;
		pai_calcrotatedpoint(
			source,
			g_model_defs[model_index]
				.weapon_hardpoints[weapon_slot_index]
				.x,
			hardpoint_z,
			g_model_defs[model_index]
				.weapon_hardpoints[weapon_slot_index]
				.y);
		if (source_type == 53) {
			g_rotated_x *= 2;
			g_rotated_y *= 2;
			g_rotated_z *= 2;
		}
		world_x += g_rotated_x;
		world_y += g_rotated_y;
		world_z += g_rotated_z;
		g_object_table[projectile_index].mobj->prev_world_x = world_x;
		g_object_table[projectile_index].mobj->prev_world_y = world_y;
		g_object_table[projectile_index].mobj->prev_world_z = world_z;
		if (source->player_owner_idx != -1) {
			g_object_table[projectile_index]
				.mobj->sim_state_timestamp =
				g_players[source->player_owner_idx]
					.lockstep_timestamp;
		}
		if (g_projectile_type_data.warhead_class
				    [projectile_object_type -
				     PROJECTILE_OBJECT_TYPE_FIRST] != 0 &&
		    (source->genus_id == CRAFT_GENUS_STARSHIP ||
		     source->genus_id == CRAFT_GENUS_FREIGHTER ||
		     source->genus_id == CRAFT_GENUS_PLATFORM)) {
			if (hardpoint_z >= 0) {
				world_z +=
					g_projectile_type_data.launch_offset
						[projectile_object_type -
						 PROJECTILE_OBJECT_TYPE_FIRST];
				g_object_table[projectile_index].pitch = 0;
			} else {
				world_z -=
					g_projectile_type_data.launch_offset
						[projectile_object_type -
						 PROJECTILE_OBJECT_TYPE_FIRST];
				g_object_table[projectile_index].pitch =
					INT16_MIN;
			}
			g_object_table[projectile_index]
				.mobj->orient_matrix_dirty = 1;
			g_object_table[projectile_index]
				.mobj->move_vector_dirty =
				g_object_table[projectile_index]
					.mobj->orient_matrix_dirty;
			g_object_table[projectile_index].world_x = world_x;
			g_object_table[projectile_index].world_y = world_y;
			g_object_table[projectile_index].world_z = world_z;
		} else {
			world_x += math_mul_q15(
				g_projectile_type_data.launch_offset
					[projectile_object_type -
					 PROJECTILE_OBJECT_TYPE_FIRST],
				source->mobj->cached_fwd_x);
			world_y += math_mul_q15(
				g_projectile_type_data.launch_offset
					[projectile_object_type -
					 PROJECTILE_OBJECT_TYPE_FIRST],
				source->mobj->cached_fwd_y);
			world_z += math_mul_q15(
				g_projectile_type_data.launch_offset
					[projectile_object_type -
					 PROJECTILE_OBJECT_TYPE_FIRST],
				source->mobj->cached_fwd_z);
			g_object_table[projectile_index].world_x = world_x;
			g_object_table[projectile_index].world_y = world_y;
			g_object_table[projectile_index].world_z = world_z;
			g_object_table[projectile_index].mobj->move_x =
				source->mobj->move_x;
			g_object_table[projectile_index].mobj->move_y =
				source->mobj->move_y;
			g_object_table[projectile_index].mobj->move_z =
				source->mobj->move_z;
			g_object_table[projectile_index].mobj->cached_side_x =
				source->mobj->cached_side_x;
			g_object_table[projectile_index].mobj->cached_side_y =
				source->mobj->cached_side_y;
			g_object_table[projectile_index].mobj->cached_side_z =
				source->mobj->cached_side_z;
			g_object_table[projectile_index].mobj->cached_up_x =
				source->mobj->cached_up_x;
			g_object_table[projectile_index].mobj->cached_up_y =
				source->mobj->cached_up_y;
			g_object_table[projectile_index].mobj->cached_up_z =
				source->mobj->cached_up_z;
			g_object_table[projectile_index].mobj->cached_fwd_x =
				source->mobj->cached_fwd_x;
			g_object_table[projectile_index].mobj->cached_fwd_y =
				source->mobj->cached_fwd_y;
			g_object_table[projectile_index].mobj->cached_fwd_z =
				source->mobj->cached_fwd_z;
			g_object_table[projectile_index]
				.mobj->orient_matrix_dirty = 0;
			g_object_table[projectile_index]
				.mobj->move_vector_dirty =
				g_object_table[projectile_index]
					.mobj->orient_matrix_dirty;
		}
		uint16_t guidance_index =
			(uint16_t)(projectile_index -
				   g_projectile_object_slot_start);
		g_projectile_guidance_states[guidance_index].homing_tier = 0;
		g_projectile_guidance_states[guidance_index].target_obj_idx =
			UINT16_MAX;
		g_projectile_guidance_states[guidance_index].target_signature =
			0;
		g_projectile_guidance_states[guidance_index]
			.target_component_idx = UINT16_MAX;
		g_projectile_guidance_states[guidance_index].source_player_idx =
			-1;
		g_object_table[projectile_index].mobj->p_warhead_guidance =
			&g_projectile_guidance_states[guidance_index];
		return projectile_index;
	}
	return -1;
}

/* Fires the warhead of the flight group of static object source_obj_idx
 * (through g_warhead_type_ids) from that object at target_obj_idx;
 * static_apply_static_hit calls it as a Mine Type C is destroyed. Returns
 * UINT16_MAX when the group has no warhead or no slot is found, else the
 * new slot: a free other-shot slot or, with none free, a cannon shot of the
 * group's team in the other-shot range, which is taken over. The warhead
 * takes the group's IFF, all angles 0, its type's speed and damage, starts
 * 384 world units above the static object, homes at a random tier of 3 to
 * 6, and warns a player whose craft is the target (laser_warnplayer). With
 * no free slot, the search reads warhead_class by each slot's object type
 * without checking that it is a shot type. */
// FUNCTION: XVT 0x406D10
uint16_t laser_createprojectilefromstatic(uint16_t source_obj_idx,
					  uint16_t target_obj_idx)
{
	int flight_group_idx = g_object_table[source_obj_idx].flight_group_idx;
	uint16_t projectile_type =
		g_warhead_type_ids[g_mission_flight_groups[flight_group_idx]
					   .fg.warhead];
	if (projectile_type == 0) {
		return UINT16_MAX;
	}
	uint16_t object_index = object_alloc_slot_for_genus(7);
	if (object_index == UINT16_MAX) {
		for (object_index =
			     (uint16_t)(g_projectile_object_slot_start + 128);
		     object_index < g_projectile_object_slot_end;
		     object_index++) {
			if (g_projectile_type_data.warhead_class
					    [g_object_table[object_index]
						     .object_type -
					     PROJECTILE_OBJECT_TYPE_FIRST] ==
				    0 &&
			    g_object_table[object_index].mobj->team ==
				    g_mission_flight_groups[flight_group_idx]
					    .fg.team) {
				break;
			}
		}
		if (object_index != g_projectile_object_slot_end &&
		    g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_WARN(
				"combat.shot_slot_taken projectile=%d old_type=%d source=%d team=%d",
				(int)object_index,
				(int)g_object_table[object_index].object_type,
				(int)source_obj_idx,
				(int)g_mission_flight_groups[flight_group_idx]
					.fg.team);
		}
	}
	if (g_projectile_object_slot_end == object_index) {
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_WARN(
				"combat.static_warhead_dropped source=%d type=%d target=%d",
				(int)source_obj_idx, (int)projectile_type,
				(int)(int16_t)target_obj_idx);
		}
		return UINT16_MAX;
	}

	g_object_table[object_index].mobj->family = 1;
	g_object_table[object_index].genus_id = 7;
	g_object_table[object_index].object_type = (uint8_t)projectile_type;
	g_object_table[object_index].mobj->seconds_alive = 1;
	g_object_table[object_index].mobj->source_obj_idx = source_obj_idx;
	g_object_table[object_index].mobj->source_object_type =
		g_object_table[source_obj_idx].object_type;
	g_object_table[object_index].mobj->iff =
		g_mission_flight_groups[flight_group_idx].fg.iff;
	g_object_table[object_index].pitch = 0;
	g_object_table[object_index].roll = 0;
	g_object_table[object_index].yaw = 0;
	g_object_table[object_index].mobj->speed =
		g_projectile_type_data
			.speed[projectile_type - PROJECTILE_OBJECT_TYPE_FIRST];
	g_projectile_guidance_states[object_index -
				     g_projectile_object_slot_start]
		.cruise_speed = g_object_table[object_index].mobj->speed;
	g_object_table[object_index].mobj->damage_amount =
		g_projectile_type_data
			.damage[projectile_type - PROJECTILE_OBJECT_TYPE_FIRST];
	g_object_table[object_index].mobj->lifetime_timer =
		laser_get_projectile_lifetime_ticks(projectile_type);
	mission_resolve_object_or_mission_point_world_loc(source_obj_idx, 0);
	g_object_table[object_index].mobj->prev_world_x = g_world_loc_x;
	g_object_table[object_index].world_x =
		g_object_table[object_index].mobj->prev_world_x;
	g_object_table[object_index].mobj->prev_world_y = g_world_loc_y;
	g_object_table[object_index].world_y =
		g_object_table[object_index].mobj->prev_world_y;
	g_object_table[object_index].mobj->prev_world_z = g_world_loc_z + 384;
	g_object_table[object_index].world_z =
		g_object_table[object_index].mobj->prev_world_z;
	uint16_t guidance_index =
		(uint16_t)(object_index - g_projectile_object_slot_start);
	g_projectile_guidance_states[guidance_index].homing_tier =
		(uint8_t)((game_rand() & 3) + 3);
	g_projectile_guidance_states[guidance_index].target_obj_idx =
		target_obj_idx;
	if (target_obj_idx != UINT16_MAX && target_obj_idx < 0x8000) {
		g_projectile_guidance_states[guidance_index].target_signature =
			g_object_table[target_obj_idx].object_signature;
	} else {
		g_projectile_guidance_states[guidance_index].target_signature =
			0;
	}
	g_projectile_guidance_states[guidance_index].source_player_idx = -1;
	g_object_table[object_index].mobj->p_warhead_guidance =
		&g_projectile_guidance_states[guidance_index];
	XVT_LOG_DEBUG(
		"combat.static_warhead_fired projectile=%d source=%d type=%d target=%d homing=%d predicted=%d",
		(int)object_index, (int)source_obj_idx, (int)projectile_type,
		(int)(int16_t)target_obj_idx,
		(int)g_projectile_guidance_states[guidance_index].homing_tier,
		g_flight_sim_side_effects_suppressed);
	if (g_object_table[target_obj_idx].player_owner_idx != -1) {
		laser_warnplayer(guidance_index);
	}
	return object_index;
}

/* Launches a shot of projectile_object_type, a countermeasure as a rule,
 * backward from the craft in owner_obj_idx and returns its slot, or -1 when
 * none is free; slots are found as in laser_createprojectile. The shot
 * copies the owner's IFF, team and roll, points opposite the owner (a half
 * turn of yaw, pitch mirrored), flies at half its type's speed with a
 * cruise_speed of its type's speed plus the owner's, does its type's damage
 * plus the owner's speed, and starts behind the owner by its own model's Y
 * size plus the owner's largest Y. It uses one of cm_ammo_count unless the
 * flight group's status is 21, and sets cm_fire_cooldown_timer to 472. A
 * COUNTERMEASURE_PROJECTILE_OBJECT_TYPE homes at tier 6 on the nearest
 * warhead aimed at the owner or, with none, on the nearest active craft
 * closer than 0x8000 that is after the owner (an AI craft targeting it, or
 * a hostile player's craft). Its count of countermeasures already chasing
 * a warhead tests the outer warhead's type, not each shot's, so it is 0
 * unless that warhead is itself a countermeasure. A shot aimed at a craft
 * lives half as long. Queues voice 37 when the target is the local
 * player's, and plays the weapon sound for a player's shot. Writes
 * trig2_*movedist and the trig2 outputs. */
// FUNCTION: XVT 0x407090
int laser_createcountermeasureprojectile(unsigned int owner_obj_idx,
					 int projectile_object_type)
{
	uint16_t projectile_index;
	uint16_t projectile_genus;

	if (g_object_table[owner_obj_idx].player_owner_idx != -1) {
		projectile_genus = 6;
		int range_start =
			g_object_slot_range_by_genus[6].start +
			12 * g_object_table[owner_obj_idx].player_owner_idx;
		projectile_index = (uint16_t)range_start;
		unsigned int range_end = range_start + 12;
		if (g_projectile_type_data
			    .warhead_class[projectile_object_type -
					   PROJECTILE_OBJECT_TYPE_FIRST] != 0) {
			projectile_index += 8;
		}
		for (; projectile_index < range_end; ++projectile_index) {
			if (g_object_table[projectile_index].object_type == 0) {
				g_object_table[projectile_index]
					.mobj->source_obj_idx = 0;
				g_object_table[projectile_index]
					.mobj->effect_size = 0;
				break;
			}
		}
		if (projectile_index >= range_end) {
			int fallback_start =
				(uint16_t)g_object_slot_range_by_genus[6]
					.start +
				96;

			projectile_index = (uint16_t)fallback_start;
			range_end = fallback_start + 32;
			for (; projectile_index < range_end;
			     ++projectile_index) {
				if (g_object_table[projectile_index]
					    .object_type == 0) {
					g_object_table[projectile_index]
						.mobj->source_obj_idx = 0;
					g_object_table[projectile_index]
						.mobj->effect_size = 0;
					break;
				}
			}
		}
		if (projectile_index >= range_end) {
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_WARN(
					"combat.shot_no_slot slot=%d source=%d type=%d",
					g_object_table[owner_obj_idx]
						.player_owner_idx,
					(int)owner_obj_idx,
					projectile_object_type);
			}
			return -1;
		}
	} else {
		projectile_genus = 7;
		projectile_index = object_alloc_slot_for_genus(7);
	}
	if (projectile_index != UINT16_MAX) {

		struct object_record *owner = &g_object_table[owner_obj_idx];
		int owner_type = owner->object_type;
		g_object_table[projectile_index].mobj->family = 1;
		g_object_table[projectile_index].genus_id = projectile_genus;
		g_object_table[projectile_index].object_type =
			(uint8_t)projectile_object_type;
		g_object_table[projectile_index].mobj->seconds_alive = 1;
		g_object_table[projectile_index].mobj->source_obj_idx =
			(uint16_t)owner_obj_idx;
		g_object_table[projectile_index].mobj->source_object_type =
			(uint8_t)owner_type;
		get_model_index_from_type(owner_type);
		g_object_table[projectile_index].mobj->iff = owner->mobj->iff;
		g_object_table[projectile_index].mobj->team = owner->mobj->team;
		g_object_table[projectile_index].pitch =
			(int16_t)(INT16_MIN - owner->pitch);
		g_object_table[projectile_index].roll = owner->roll;
		g_object_table[projectile_index].yaw =
			(int16_t)(owner->yaw + 0x8000);
		g_object_table[projectile_index].mobj->speed =
			g_projectile_type_data
				.speed[projectile_object_type -
				       PROJECTILE_OBJECT_TYPE_FIRST] >>
			1;
		g_projectile_guidance_states[projectile_index -
					     g_projectile_object_slot_start]
			.cruise_speed =
			g_projectile_type_data
				.speed[projectile_object_type -
				       PROJECTILE_OBJECT_TYPE_FIRST] +
			owner->mobj->speed;
		g_object_table[projectile_index].mobj->damage_amount =
			g_projectile_type_data
				.damage[projectile_object_type -
					PROJECTILE_OBJECT_TYPE_FIRST] +
			owner->mobj->speed;
		if (g_object_table[projectile_index].mobj->damage_amount <
		    g_projectile_type_data
			    .damage[projectile_object_type -
				    PROJECTILE_OBJECT_TYPE_FIRST]) {
			g_object_table[projectile_index].mobj->damage_amount =
				g_projectile_type_data
					.damage[projectile_object_type -
						PROJECTILE_OBJECT_TYPE_FIRST];
		}
		g_object_table[projectile_index].mobj->lifetime_timer =
			laser_get_projectile_lifetime_ticks(
				projectile_object_type);
		g_object_table[projectile_index].mobj->orient_matrix_dirty = 1;
		g_object_table[projectile_index].mobj->move_vector_dirty =
			g_object_table[projectile_index]
				.mobj->orient_matrix_dirty;
		struct object_record *projectile =
			&g_object_table[projectile_index];
		projectile->world_x = owner->world_x;
		projectile->mobj->prev_world_x = projectile->world_x;
		projectile->world_y = owner->world_y;
		projectile->mobj->prev_world_y = projectile->world_y;
		projectile->world_z = owner->world_z;
		projectile->mobj->prev_world_z = projectile->world_z;
		{
			int offset = model_bounds_get_size_y(
				projectile->object_type);

			offset = (uint16_t)(offset +
					    model_bounds_get_max_y(owner_type));
			fview_calcrotatemove(projectile->pitch, projectile->yaw,
					     projectile);
			struct model_mesh_scale_operation move_operation;
			move_operation.scale = offset;
			move_operation.value = projectile->mobj->move_x;
			trig2_xmovedist = math_mul_q15(move_operation.value,
						       move_operation.scale);
			move_operation.scale = offset;
			move_operation.value = projectile->mobj->move_y;
			trig2_ymovedist = math_mul_q15(move_operation.value,
						       move_operation.scale);
			move_operation.scale = offset;
			move_operation.value = projectile->mobj->move_z;
			trig2_zmovedist = math_mul_q15(move_operation.value,
						       move_operation.scale);
		}
		object_add_trig_move_delta_and_clamp_world_position(
			(uint32_t *)projectile);
		uint16_t guidance_index =
			projectile_index - g_projectile_object_slot_start;
		projectile->mobj->p_warhead_guidance =
			&g_projectile_guidance_states[guidance_index];
		g_projectile_guidance_states[guidance_index].homing_tier = 0;
		g_projectile_guidance_states[guidance_index].target_obj_idx =
			UINT16_MAX;
		g_projectile_guidance_states[guidance_index].target_signature =
			0;
		g_projectile_guidance_states[guidance_index]
			.target_component_idx = UINT16_MAX;
		g_projectile_guidance_states[guidance_index].source_player_idx =
			g_object_table[owner_obj_idx].player_owner_idx;
		struct craft_data *craft =
			g_object_table[owner_obj_idx].mobj->p_craft;
		if (g_mission_flight_groups[g_object_table[owner_obj_idx]
						    .flight_group_idx]
				    .fg.status1 != 21 &&
		    g_mission_flight_groups[g_object_table[owner_obj_idx]
						    .flight_group_idx]
				    .fg.status2 != 21) {
			--craft->cm_ammo_count;
		}
		craft->cm_fire_cooldown_timer = 472;

		if (projectile_object_type ==
		    COUNTERMEASURE_PROJECTILE_OBJECT_TYPE) {
			uint16_t nearest_target = UINT16_MAX;
			uint16_t nearest_intercepted_target = UINT16_MAX;
			unsigned int nearest_target_distance = UINT_MAX;
			unsigned int nearest_intercepted_target_distance =
				UINT_MAX;

			unsigned int candidate_index =
				g_projectile_object_slot_start;
			if ((unsigned int)g_projectile_object_slot_end >
			    candidate_index) {
				do {
					uint8_t candidate_type =
						g_object_table[candidate_index]
							.object_type;

					if (candidate_type != 0 &&
					    g_object_table[candidate_index]
							    .mobj->family ==
						    1 &&
					    g_projectile_type_data.warhead_class
							    [candidate_type -
							     PROJECTILE_OBJECT_TYPE_FIRST] !=
						    0 &&
					    g_projectile_guidance_states
							    [candidate_index -
							     g_projectile_object_slot_start]
								    .target_obj_idx ==
						    owner_obj_idx) {
						int interceptor_count = 0;

						for (unsigned int inner_index =
							     g_projectile_object_slot_start;
						     inner_index <
						     (unsigned int)
							     g_projectile_object_slot_end;
						     ++inner_index) {
							if (candidate_type ==
								    COUNTERMEASURE_PROJECTILE_OBJECT_TYPE &&
							    g_projectile_guidance_states
									    [inner_index -
									     g_projectile_object_slot_start]
										    .target_obj_idx ==
								    candidate_index) {
								++interceptor_count;
							}
						}
						pai_object_ref_direction_to_object_ref(
							owner_obj_idx,
							candidate_index);
						if (interceptor_count != 0) {
							if ((unsigned int)
								    trig2_polardistance <
							    nearest_intercepted_target_distance) {
								nearest_intercepted_target_distance =
									trig2_polardistance;
								nearest_intercepted_target =
									(uint16_t)
										candidate_index;
							}
						} else if (
							(unsigned int)
								trig2_polardistance <
							nearest_target_distance) {
							nearest_target_distance =
								trig2_polardistance;
							nearest_target = (uint16_t)
								candidate_index;
						}
					}
					++candidate_index;
				} while ((unsigned int)
						 g_projectile_object_slot_end >
					 candidate_index);
			}
			if (nearest_target == UINT16_MAX) {
				for (uint16_t craft_index = (uint16_t)
					     g_active_region_object_slot_start;
				     craft_index <
				     g_active_region_craft_object_slot_end;
				     ++craft_index) {
					struct object_record *candidate =
						&g_object_table[craft_index];

					if (candidate->object_type != 0) {
						struct craft_data
							*candidate_craft =
								candidate->mobj
									->p_craft;

						if (candidate_craft
							    ->object_kind ==
						    CRAFT_OBJECT_KIND_ACTIVE) {
							int is_enemy = 0;

							if (candidate
								    ->player_owner_idx ==
							    -1) {
								if (candidate_craft
									    ->ai_controller
									    .target_obj_idx ==
								    owner_obj_idx) {
									is_enemy =
										1;
								}
							} else {
								int hostile_player =
									0;
								int owner_team =
									g_mission_flight_groups
										[g_object_table
											 [(uint16_t)
												  owner_obj_idx]
												 .flight_group_idx]
											.fg
											.team;
								uint16_t candidate_team =
									(uint16_t)g_players
										[candidate
											 ->player_owner_idx]
											.team;

								if (owner_team !=
								    candidate_team) {
									hostile_player =
										g_mission_teams[candidate_team]
											.allies[owner_team] <
										1;
								}
								if (hostile_player !=
								    0) {
									is_enemy =
										1;
								}
							}
							if (is_enemy != 0) {
								pai_object_ref_direction_to_object_ref(
									owner_obj_idx,
									craft_index);
								if ((unsigned int)trig2_polardistance <
									    nearest_target_distance &&
								    trig2_polardistance <
									    0x8000) {
									nearest_target =
										craft_index;
									nearest_target_distance =
										trig2_polardistance;
								}
							}
						}
					}
				}
			}
			if (nearest_target != UINT16_MAX) {
				g_projectile_guidance_states[guidance_index]
					.target_obj_idx = nearest_target;
				g_projectile_guidance_states[guidance_index]
					.homing_tier = 6;
				g_projectile_guidance_states[guidance_index]
					.target_signature =
					g_object_table[nearest_target]
						.object_signature;
			} else if (nearest_intercepted_target != UINT16_MAX) {
				g_projectile_guidance_states[guidance_index]
					.target_obj_idx =
					nearest_intercepted_target;
				g_projectile_guidance_states[guidance_index]
					.homing_tier = 6;
				g_projectile_guidance_states[guidance_index]
					.target_signature =
					g_object_table
						[nearest_intercepted_target]
							.object_signature;
			}
		}
		if (g_projectile_guidance_states[guidance_index]
				    .target_obj_idx >=
			    g_active_region_object_slot_start &&
		    g_projectile_guidance_states[guidance_index]
				    .target_obj_idx <
			    g_active_region_craft_object_slot_end) {
			g_object_table[projectile_index]
				.mobj->lifetime_timer >>= 1;
		}
		if (g_projectile_guidance_states[guidance_index]
				    .target_obj_idx != UINT16_MAX &&
		    g_object_table[g_projectile_guidance_states[guidance_index]
					   .target_obj_idx]
				    .player_owner_idx == g_local_player) {
			fsfx_queue_voice_sfx(37, 0, 0, 0, UINT16_MAX);
		}
		if (g_object_table[owner_obj_idx].player_owner_idx != -1) {
			fsfx_triggerweaponsfx(
				projectile_index,
				g_object_table[owner_obj_idx].player_owner_idx);
		}
		XVT_LOG_DEBUG(
			"combat.countermeasure_fired projectile=%d source=%d slot=%d type=%d target=%d life=%u ammo=%d predicted=%d",
			(int)projectile_index, (int)owner_obj_idx,
			g_object_table[owner_obj_idx].player_owner_idx,
			projectile_object_type,
			(int)(int16_t)
				g_projectile_guidance_states[guidance_index]
					.target_obj_idx,
			(unsigned)g_object_table[projectile_index]
				.mobj->lifetime_timer,
			(int)craft->cm_ammo_count,
			g_flight_sim_side_effects_suppressed);
		return projectile_index;
	}
	return -1;
}

/* Warns the player whose craft is the target of the shot with guidance
 * index projectile_guidance_idx, unless that player already has a pending
 * action: sets pending_action_id 1, no issuing player, the shot's slot as
 * pending_action_param and a pending_action_timer of 1,416 ticks. The local
 * player also gets the missile warning message and a wingman voice line.
 * Does nothing when no player owns the target; does not check that the
 * shot has one. */
// FUNCTION: XVT 0x407910
void laser_warnplayer(uint16_t projectile_guidance_idx)
{
	int player_owner_idx =
		g_object_table
			[g_projectile_guidance_states[projectile_guidance_idx]
				 .target_obj_idx]
				.player_owner_idx;
	if (player_owner_idx == -1 ||
	    g_players[player_owner_idx].pending_action_id != 0) {
		return;
	}
	g_players[player_owner_idx].pending_action_id = 1;
	g_players[player_owner_idx].pending_action_issuer_player_idx =
		UINT16_MAX;
	g_players[player_owner_idx].pending_action_param =
		projectile_guidance_idx + g_projectile_object_slot_start;
	g_players[player_owner_idx].pending_action_timer = 1416;
	XVT_LOG_DEBUG(
		"combat.missile_warning slot=%d projectile=%d predicted=%d",
		player_owner_idx,
		(int)projectile_guidance_idx + g_projectile_object_slot_start,
		g_flight_sim_side_effects_suppressed);
	if (player_owner_idx == g_local_player) {
		msg_emit_in_flight_message(
			IFMSG_116_MISSILE_WARNING_KEY_TO_TARGET,
			player_owner_idx);
		fsfx_speak_wingman_event(
			g_local_player, -1, 12, -1,
			g_players[player_owner_idx].object_index, UINT16_MAX);
	}
}

/* Lets the mine in static slot mine_obj_idx fire while it works
 * (type_specific_word not 0). Each call takes half of g_elapsed_ticks off its
 * countdown in type_specific_byte[1]; when that runs out the countdown is
 * reset to 236 (two simulated seconds) and the mine looks for the nearest
 * target matching its flight group's first order, targets 1 and 2 and then
 * targets 3 and 4 (paifight_find_nearest_matching_target_from_origin; a Mine
 * Type B asks for a target that is not disabled). It fires only at a
 * target closer than 0x10000 world units, aiming ahead of a moving one by
 * its last step's motion times the expected flight steps (plus a random 0
 * to 3, less 1), from a point 150 world units out (170 for mines after
 * Type B) on the side facing the target; a Mine Type C or later does not
 * fire at a target more than an eighth of a turn below the horizontal. Aim
 * error is likelier at longer range and against targets faster than 187
 * (the speed term is 16 bits and wraps from 700 up). The shot is an ion turbo
 * laser from a Mine Type B, else an imperial (IFF 1 or 4) or rebel turbo
 * laser, at half its type's speed and twice its life, with no homing.
 * Writes g_paifightSearchOrigin*, g_pai_context.require_undisabled_target,
 * g_worldLoc* and the trig2 outputs. */
// FUNCTION: XVT 0x446C60
void laser_update_mine_weapon_fire(uint16_t mine_obj_idx)
{
	enum {
		MINE_COOLDOWN_RESET = -20,
		MINE_FIRE_RANGE = 0x10000,
		MINE_TYPE_B_LIVE_TARGET = CRAFT_SPECIES_MINE_TYPE_B,
		MINE_TYPE_C_FIRST_SIDE_ONLY = CRAFT_SPECIES_MINE_TYPE_C,
		SMALL_MINE_LAUNCH_OFFSET = 150,
		LARGE_MINE_LAUNCH_OFFSET = 170,
		TARGET_SPEED_ACCURACY_CUTOFF = 188,
		TARGET_SPEED_ACCURACY_BASE = 24063,
		MINE_PROJECTILE_SPEED_SHIFT = 1,
		MINE_PROJECTILE_LIFETIME_SCALE = 2,
		AIM_ERROR_BASE = 256,
		AIM_ERROR_MASK = 0x3FF,
		ANGLE_WRAPPED = 0x8000,
		ANGLE_ONE_EIGHTH = 0x2000,
		ANGLE_THREE_EIGHTHS = 0x6000,
		ANGLE_FIVE_EIGHTHS = 0xA000,
		ANGLE_SEVEN_EIGHTHS = 0xE000,
	};

	if (g_object_table[mine_obj_idx].type_specific_word == 0) {
		return;
	}

	{
		uint8_t cooldown =
			g_object_table[mine_obj_idx].type_specific_byte[1];
		uint16_t cooldown_step = g_elapsed_ticks >> 1;

		if ((unsigned int)cooldown > cooldown_step) {
			g_object_table[mine_obj_idx].type_specific_byte[1] =
				(uint8_t)(cooldown - cooldown_step);
			return;
		}
	}
	g_object_table[mine_obj_idx].type_specific_byte[1] =
		(uint8_t)MINE_COOLDOWN_RESET;

	int mine_x = g_object_table[mine_obj_idx].world_x;
	int mine_y = g_object_table[mine_obj_idx].world_y;
	int mine_z = g_object_table[mine_obj_idx].world_z;
	g_paifight_search_origin_x = mine_x;
	g_paifight_search_origin_y = mine_y;
	g_paifight_search_origin_z = mine_z;
	uint16_t flight_group_idx =
		g_object_table[mine_obj_idx].flight_group_idx;
	g_pai_context.require_undisabled_target = 1;
	if (g_object_table[mine_obj_idx].object_type !=
	    MINE_TYPE_B_LIVE_TARGET) {
		g_pai_context.require_undisabled_target = 0;
	}
	uint16_t target_ref = paifight_find_nearest_matching_target_from_origin(
		g_mission_flight_groups[flight_group_idx]
			.fg.orders[0]
			.target1_type,
		g_mission_flight_groups[flight_group_idx].fg.orders[0].target1,
		g_mission_flight_groups[flight_group_idx]
			.fg.orders[0]
			.target1_or_target2,
		g_mission_flight_groups[flight_group_idx]
			.fg.orders[0]
			.target2_type,
		g_mission_flight_groups[flight_group_idx].fg.orders[0].target2,
		0);
	if (target_ref == UINT16_MAX) {
		target_ref = paifight_find_nearest_matching_target_from_origin(
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[0]
				.secondary_target_types[0],
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[0]
				.secondary_targets[0],
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[0]
				.target3_or_target4,
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[0]
				.secondary_target_types[1],
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[0]
				.secondary_targets[1],
			0);
	}
	if (target_ref == UINT16_MAX) {
		return;
	}

	mission_resolve_object_or_mission_point_world_loc(target_ref, 0);
	int target_x = g_world_loc_x;
	int target_y = g_world_loc_y;
	int target_z = g_world_loc_z;
	if ((unsigned int)collide_roughdistance3d(
		    target_x - mine_x, target_y - mine_y, target_z - mine_z) >=
	    MINE_FIRE_RANGE) {
		return;
	}

	int lead_target_x;
	int lead_target_y;
	int lead_target_z;
	if (g_object_table[target_ref].mobj != NULL) {
		trig2_ctop(g_object_table[target_ref].world_x - mine_x,
			   g_object_table[target_ref].world_y - mine_y,
			   g_object_table[target_ref].world_z - mine_z);
		trig2_polardistance *= g_sim_steps_per_second;
		if (g_object_table[mine_obj_idx].object_type ==
		    MINE_TYPE_B_LIVE_TARGET) {
			trig2_polardistance >>= 15;
		} else {
			trig2_polardistance >>= 14;
		}
		uint16_t lead_frames = (uint16_t)trig2_polardistance;
		lead_frames = (uint16_t)(lead_frames + (game_rand() & 3));
		lead_frames--;
		lead_target_x =
			g_object_table[target_ref].world_x +
			lead_frames *
				((xvt_flight_timing_is_unlocked()
					  ? xvt_reference_motion_axis_displacement(
						    target_ref, 0)
					  : (g_object_table[target_ref]
						     .world_x -
					     g_object_table[target_ref]
						     .mobj->prev_world_x)));
		lead_target_y =
			g_object_table[target_ref].world_y +
			lead_frames *
				((xvt_flight_timing_is_unlocked()
					  ? xvt_reference_motion_axis_displacement(
						    target_ref, 1)
					  : (g_object_table[target_ref]
						     .world_y -
					     g_object_table[target_ref]
						     .mobj->prev_world_y)));
		lead_target_z =
			g_object_table[target_ref].world_z +
			lead_frames *
				((xvt_flight_timing_is_unlocked()
					  ? xvt_reference_motion_axis_displacement(
						    target_ref, 2)
					  : (g_object_table[target_ref]
						     .world_z -
					     g_object_table[target_ref]
						     .mobj->prev_world_z)));
	} else {
		lead_target_x = target_x;
		lead_target_y = target_y;
		lead_target_z = target_z;
	}

	trig2_ctop(lead_target_x - mine_x, lead_target_y - mine_y,
		   lead_target_z - mine_z);
	int16_t projectile_yaw = trig2_xyangle;
	int16_t projectile_pitch = trig2_pitch;
	{
		uint16_t launch_offset =
			g_object_table[mine_obj_idx].object_type >
					MINE_TYPE_B_LIVE_TARGET
				? LARGE_MINE_LAUNCH_OFFSET
				: SMALL_MINE_LAUNCH_OFFSET;

		if (trig2_pitch < ANGLE_ONE_EIGHTH) {
			mine_z += launch_offset;
		} else if (trig2_pitch > ANGLE_THREE_EIGHTHS) {
			if (g_object_table[mine_obj_idx].object_type >=
			    MINE_TYPE_C_FIRST_SIDE_ONLY) {
				return;
			}
			mine_z -= launch_offset;
		} else if (trig2_xyangle < ANGLE_ONE_EIGHTH ||
			   trig2_xyangle > ANGLE_SEVEN_EIGHTHS) {
			mine_y += launch_offset;
		} else if (trig2_xyangle < ANGLE_THREE_EIGHTHS) {
			mine_x += launch_offset;
		} else if (trig2_xyangle < ANGLE_FIVE_EIGHTHS) {
			mine_y -= launch_offset;
		} else {
			mine_x -= launch_offset;
		}
	}

	{
		int16_t range_score = -1;

		if (trig2_polardistance < MINE_FIRE_RANGE) {
			range_score = (int16_t)trig2_polardistance;
		}
		uint16_t inverted_range = (uint16_t)~range_score;
		uint16_t target_speed_accuracy;
		if (g_object_table[target_ref].mobj == NULL) {
			target_speed_accuracy = UINT16_MAX;
		} else {
			uint16_t target_speed =
				g_object_table[target_ref].mobj->speed;

			target_speed_accuracy = UINT16_MAX;
			if (target_speed >= TARGET_SPEED_ACCURACY_CUTOFF) {
				target_speed_accuracy =
					(uint16_t)(TARGET_SPEED_ACCURACY_BASE -
						   (target_speed << 7));
			}
		}
		uint16_t accuracy_threshold =
			math2_fraction(inverted_range, target_speed_accuracy);
		if ((uint16_t)game_rand() > accuracy_threshold) {
			int16_t aim_error =
				(int16_t)((game_rand() - AIM_ERROR_BASE) &
					  AIM_ERROR_MASK);

			if ((uint16_t)game_rand() >= 0x8000u) {
				aim_error = (int16_t)-aim_error;
			}
			projectile_yaw = (int16_t)(projectile_yaw + aim_error);
			aim_error = (int16_t)((game_rand() - AIM_ERROR_BASE) &
					      AIM_ERROR_MASK);
			if ((uint16_t)game_rand() >= 0x8000u) {
				projectile_pitch =
					(int16_t)(projectile_pitch - aim_error);
				if ((projectile_pitch & ANGLE_WRAPPED) != 0) {
					projectile_pitch = 0;
				}
			} else {
				projectile_pitch =
					(int16_t)(projectile_pitch + aim_error);
				if ((projectile_pitch & ANGLE_WRAPPED) != 0) {
					projectile_pitch = INT16_MAX;
				}
			}
		}
	}

	uint16_t projectile_obj_idx =
		object_alloc_slot_for_genus(CRAFT_GENUS_OTHER_PROJECTILE);
	if (projectile_obj_idx != UINT16_MAX) {
		g_object_table[projectile_obj_idx].mobj->family = 1;
		g_object_table[projectile_obj_idx].genus_id =
			CRAFT_GENUS_OTHER_PROJECTILE;
		uint16_t projectile_object_type;
		if (g_object_table[mine_obj_idx].object_type ==
		    MINE_TYPE_B_LIVE_TARGET) {
			projectile_object_type =
				PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER;
		} else if (g_mission_flight_groups[flight_group_idx].fg.iff ==
				   1 ||
			   g_mission_flight_groups[flight_group_idx].fg.iff ==
				   4) {
			projectile_object_type =
				PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER;
		} else {
			projectile_object_type =
				PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER;
		}
		g_object_table[projectile_obj_idx].object_type =
			(uint8_t)projectile_object_type;
		g_object_table[projectile_obj_idx].mobj->seconds_alive = 1;
		g_object_table[projectile_obj_idx].mobj->source_obj_idx =
			mine_obj_idx;
		g_object_table[projectile_obj_idx].mobj->source_object_type = 0;
		g_object_table[projectile_obj_idx].mobj->iff =
			g_mission_flight_groups[g_object_table[mine_obj_idx]
							.flight_group_idx]
				.fg.iff;
		g_object_table[projectile_obj_idx].pitch = projectile_pitch;
		g_object_table[projectile_obj_idx].roll = 0;
		g_object_table[projectile_obj_idx].yaw = projectile_yaw;
		g_object_table[projectile_obj_idx].mobj->orient_matrix_dirty =
			1;
		g_object_table[projectile_obj_idx].mobj->move_vector_dirty =
			g_object_table[projectile_obj_idx]
				.mobj->orient_matrix_dirty;
		g_object_table[projectile_obj_idx].mobj->speed =
			g_projectile_type_data
				.speed[projectile_object_type -
				       PROJECTILE_OBJECT_TYPE_FIRST] >>
			MINE_PROJECTILE_SPEED_SHIFT;
		g_object_table[projectile_obj_idx].mobj->lifetime_timer =
			(uint16_t)(MINE_PROJECTILE_LIFETIME_SCALE *
				   laser_get_projectile_lifetime_ticks(
					   projectile_object_type));
		g_object_table[projectile_obj_idx].mobj->damage_amount =
			g_projectile_type_data
				.damage[projectile_object_type -
					PROJECTILE_OBJECT_TYPE_FIRST];
		fview_calcrotatemove(projectile_pitch, projectile_yaw,
				     &g_object_table[projectile_obj_idx]);
		g_object_table[projectile_obj_idx].mobj->prev_world_x = mine_x;
		g_object_table[projectile_obj_idx].mobj->prev_world_y = mine_y;
		g_object_table[projectile_obj_idx].mobj->prev_world_z = mine_z;
		int16_t launch_offset =
			g_projectile_type_data
				.launch_offset[projectile_object_type -
					       PROJECTILE_OBJECT_TYPE_FIRST];
		mine_x += math_mul_q15(g_fview_move_x_q15, launch_offset);
		mine_y += math_mul_q15(g_fview_move_y_q15, launch_offset);
		mine_z += math_mul_q15(g_fview_move_z_q15, launch_offset);
		g_object_table[projectile_obj_idx].world_x = mine_x;
		g_object_table[projectile_obj_idx].world_y = mine_y;
		g_object_table[projectile_obj_idx].world_z = mine_z;
		fsfx_triggerweaponsfx(projectile_obj_idx, g_local_player);
		int guidance_index = (uint16_t)(projectile_obj_idx -
						g_projectile_object_slot_start);
		g_projectile_guidance_states[guidance_index].homing_tier = 0;
		g_projectile_guidance_states[guidance_index].target_obj_idx =
			target_ref;
		if (target_ref == UINT16_MAX || target_ref >= 0x8000u) {
			g_projectile_guidance_states[guidance_index]
				.target_signature = 0;
		} else {
			g_projectile_guidance_states[guidance_index]
				.target_signature =
				g_object_table[target_ref].object_signature;
		}
		g_projectile_guidance_states[guidance_index].source_player_idx =
			-1;
		g_object_table[projectile_obj_idx].mobj->p_warhead_guidance =
			&g_projectile_guidance_states[guidance_index];
		XVT_LOG_DEBUG(
			"combat.mine_fired projectile=%d source=%d mine=%d type=%d target=%d predicted=%d",
			(int)projectile_obj_idx, (int)mine_obj_idx,
			(int)g_object_table[mine_obj_idx].object_type,
			(int)projectile_object_type, (int)(int16_t)target_ref,
			g_flight_sim_side_effects_suppressed);
	}
}

/* Runs the gunner of weapon slot weapon_slot_idx of g_cur_craft, the craft in
 * source_obj_idx, against target_ref. Does nothing when the craft has no
 * working systems or the slot's mesh is destroyed. The low 7 bits of the
 * slot's laser_charge are a refire countdown: while it runs, each call takes
 * off a step set by the gunner's skill (pai_get_effective_skill_value), or,
 * while a jamming beam holds the craft (beam_effect_accum[2]), 1 or 2 during
 * part of each simulated second and nothing at all from 0x28000 up; and
 * nothing fires. At 0 it is reset to 59 and the turret tries to fire: from
 * the slot's hardpoint (on odd subsecond ticks the mesh's other hardpoint,
 * when it has one; turned with a rotating turret's mesh; on a Super Star
 * Destroyer the hull vertex nearest the target), at a target within
 * 0x14000 world units, unless collide_check_swept_model_collision finds the
 * craft's own hull in the way. It aims ahead of a moving target by its last
 * step's motion times the expected flight steps, scaled by the gunner's
 * skill. The shot is an ion laser when the slot's ammo_count is set, else a
 * rebel (IFF 0 or 2) or imperial turbo laser, the heavier kind (ion turbo
 * laser, turbo laser 2) when the slot is in a group of turbo laser 2
 * weapons; it flies at its type's speed for three times its life, with no
 * homing. The random aim error never applies: its threshold is UINT16_MAX.
 * Writes g_collisionProbeWorld*, g_collisionSegmentStartWorld*, and on a
 * Super Star Destroyer g_turret_fire_hull_mesh_ordinal and
 * g_collide_sweep_reject_near_start_hits, plus g_rotated*, g_worldLoc* and the
 * trig2 outputs. Expects g_cur_craft to be the source's craft and does not
 * check it. */
// FUNCTION: XVT 0x4A7900
void laser_fireturretslot(uint16_t source_obj_idx, uint16_t weapon_slot_idx,
			  uint16_t target_ref)
{
	enum {
		LASER_CHARGE_VALUE_MASK = 0x7F,
		LASER_CHARGE_FLAG_MASK = 0x80,
		TURRET_REFIRE_TICKS = 59,
		BEAM_FIRE_BLOCK_THRESHOLD = 0x28000,
		BEAM_SINGLE_DRAIN_THRESHOLD = 0x18000,
		BEAM_DOUBLE_DRAIN_THRESHOLD = 0x8000,
		BEAM_DRAIN_TICK_THRESHOLD = 118,
		SKILL_FAST_DRAIN_THRESHOLD = 0xAAAA,
		SKILL_MEDIUM_DRAIN_THRESHOLD = 0x5555,
		SKILL_SLOW_DRAIN_THRESHOLD = 0x4000,
		SKILL_SLOW_DRAIN_DIVISOR = 6,
		SUPER_STAR_DESTROYER_OBJECT_TYPE = 54,
		IMPERIAL_STAR_DESTROYER_OBJECT_TYPE = 53,
		MODEL_TYPE_CACHE_CAPACITY = 73,
		TURRET_FIRE_RANGE = 0x14000,
		ANIMATED_MESH_ANGLE_SHIFT = 8,
		WEAPON_GROUP_COUNT = 2,
		PROJECTILE_LIFETIME_SCALE = 3,
		AIM_ERROR_BASE = 256,
		AIM_ERROR_MASK = 0x3FF,
		ANGLE_WRAPPED = 0x8000,
	};

	if (g_cur_craft->working_subsystems == 0) {
		return;
	}

	int model_index = g_cur_craft->model_index;
	uint16_t main_hull_mesh_idx =
		g_model_defs[model_index]
			.weapon_hardpoints[weapon_slot_idx]
			.mesh_idx;
	uint8_t alternate_hardpoint_idx =
		g_model_defs[model_index]
			.weapon_hardpoints[weapon_slot_idx]
			.alternate_mesh_hardpoint_idx;
	int mesh_idx = main_hull_mesh_idx;
	if (g_cur_craft->component_hp[mesh_idx] == 0) {
		return;
	}

	struct object_record *source_object = &g_object_table[source_obj_idx];
	uint16_t effective_skill = pai_get_effective_skill_value(g_cur_craft);
	uint8_t charge = (uint8_t)g_cur_craft->weapon_slots[weapon_slot_idx]
				 .laser_charge;
	uint8_t refire_countdown = charge & LASER_CHARGE_VALUE_MASK;
	if (refire_countdown != 0) {
		uint8_t countdown_step;
		if (g_cur_craft->beam_effect_accum[2] != 0) {
			if ((unsigned int)g_cur_craft->beam_effect_accum[2] >=
			    BEAM_FIRE_BLOCK_THRESHOLD) {
				return;
			}
			if ((unsigned int)g_cur_craft->beam_effect_accum[2] >=
			    BEAM_SINGLE_DRAIN_THRESHOLD) {
				if (g_mission_elapsed_clock.subsecond_ticks <
				    BEAM_DRAIN_TICK_THRESHOLD) {
					return;
				}
				countdown_step = 1;
			} else if ((unsigned int)
					   g_cur_craft->beam_effect_accum[2] >=
				   BEAM_DOUBLE_DRAIN_THRESHOLD) {
				if (g_mission_elapsed_clock.subsecond_ticks <
				    BEAM_DRAIN_TICK_THRESHOLD) {
					return;
				}
				countdown_step = 2;
			} else {
				countdown_step = 1;
			}
		} else if (effective_skill >= SKILL_FAST_DRAIN_THRESHOLD) {
			countdown_step = (uint8_t)(g_elapsed_ticks >> 1);
		} else if (effective_skill >= SKILL_MEDIUM_DRAIN_THRESHOLD) {
			countdown_step = (uint8_t)(g_elapsed_ticks >> 2);
		} else if (effective_skill >= SKILL_SLOW_DRAIN_THRESHOLD) {
			countdown_step = (uint8_t)(g_elapsed_ticks /
						   SKILL_SLOW_DRAIN_DIVISOR);
		} else {
			countdown_step = (uint8_t)(g_elapsed_ticks >> 3);
		}
		if (countdown_step == 0) {
			countdown_step = 1;
		}
		uint8_t previous_countdown = refire_countdown;
		refire_countdown = (uint8_t)(refire_countdown - countdown_step);
		if (refire_countdown > previous_countdown) {
			refire_countdown = 0;
		}
		g_cur_craft->weapon_slots[weapon_slot_idx].laser_charge =
			charge & LASER_CHARGE_FLAG_MASK;
		g_cur_craft->weapon_slots[weapon_slot_idx].laser_charge |=
			(int8_t)refire_countdown;
		return;
	}

	g_cur_craft->weapon_slots[weapon_slot_idx].laser_charge =
		charge & LASER_CHARGE_FLAG_MASK;
	g_cur_craft->weapon_slots[weapon_slot_idx].laser_charge |=
		TURRET_REFIRE_TICKS;
	int launch_x = source_object->world_x;
	int launch_y = source_object->world_y;
	int launch_z = source_object->world_z;
	if (source_object->object_type == SUPER_STAR_DESTROYER_OBJECT_TYPE) {
		mission_resolve_object_or_mission_point_world_loc(target_ref,
								  0);
		int local_x = g_world_loc_x - launch_x;
		int local_y = g_world_loc_y - launch_y;
		int local_z = g_world_loc_z - launch_z;
		if (source_object->mobj == NULL) {
			return;
		}
		if (source_object->mobj->orient_matrix_dirty != 0) {
			fview_calcrotatemove(source_object->pitch,
					     source_object->yaw, source_object);
			fview_calcrotateorient(source_object->roll, 0,
					       source_object);
		}
		g_rotated_x = math_dot3q15(source_object->mobj->cached_side_x,
					   source_object->mobj->cached_side_y,
					   source_object->mobj->cached_side_z,
					   local_x, local_y, local_z);
		g_rotated_y = -math_dot3q15(source_object->mobj->cached_fwd_x,
					    source_object->mobj->cached_fwd_y,
					    source_object->mobj->cached_fwd_z,
					    local_x, local_y, local_z);
		g_rotated_z = math_dot3q15(source_object->mobj->cached_up_x,
					   source_object->mobj->cached_up_y,
					   source_object->mobj->cached_up_z,
					   local_x, local_y, local_z);
		main_hull_mesh_idx =
			(uint16_t)model_mesh_find_nearest_main_hull_by_bounds(
				source_object->object_type, g_rotated_x,
				g_rotated_y, g_rotated_z);
		uint8_t nearest_rank = 0;
		for (int previous_slot_idx = 0;
		     previous_slot_idx < weapon_slot_idx; ++previous_slot_idx) {
			if (g_cur_craft->turret_target_states[previous_slot_idx]
				    .target_obj_idx == target_ref) {
				++nearest_rank;
			}
		}
		uint8_t nearest_vertex_idx =
			model_mesh_find_nearest_vertex_for_point(
				source_object->object_type, g_rotated_x,
				g_rotated_y, g_rotated_z, main_hull_mesh_idx,
				nearest_rank);
		local_x = model_mesh_get_vertex_x(source_object->object_type,
						  main_hull_mesh_idx,
						  nearest_vertex_idx);
		local_y = -model_mesh_get_vertex_y(source_object->object_type,
						   main_hull_mesh_idx,
						   nearest_vertex_idx);
		local_z = model_mesh_get_vertex_z(source_object->object_type,
						  main_hull_mesh_idx,
						  nearest_vertex_idx);
		g_rotated_x = math_dot3q15(source_object->mobj->cached_side_x,
					   source_object->mobj->cached_up_x,
					   source_object->mobj->cached_fwd_x,
					   local_x, local_z, local_y);
		g_rotated_y = math_dot3q15(source_object->mobj->cached_side_y,
					   source_object->mobj->cached_up_y,
					   source_object->mobj->cached_fwd_y,
					   local_x, local_z, local_y);
		g_rotated_z = math_dot3q15(source_object->mobj->cached_side_z,
					   source_object->mobj->cached_up_z,
					   source_object->mobj->cached_fwd_z,
					   local_x, local_z, local_y);
	} else {
		int16_t hardpoint_x;
		int16_t hardpoint_y;
		int16_t hardpoint_z;
		if (alternate_hardpoint_idx == UINT8_MAX ||
		    (g_mission_elapsed_clock.subsecond_ticks & 1) == 0) {
			hardpoint_x =
				g_model_defs[model_index]
					.weapon_hardpoints[weapon_slot_idx]
					.x;
			hardpoint_y =
				g_model_defs[model_index]
					.weapon_hardpoints[weapon_slot_idx]
					.y;
			hardpoint_z =
				g_model_defs[model_index]
					.weapon_hardpoints[weapon_slot_idx]
					.z;
		} else if (source_object->object_type ==
			   IMPERIAL_STAR_DESTROYER_OBJECT_TYPE) {
			hardpoint_x =
				(int16_t)(model_mesh_get_hardpoint_x(
						  source_object->object_type,
						  mesh_idx,
						  alternate_hardpoint_idx) >>
					  1);
			hardpoint_y =
				(int16_t)(model_mesh_get_hardpoint_y(
						  source_object->object_type,
						  mesh_idx,
						  alternate_hardpoint_idx) >>
					  1);
			hardpoint_z =
				(int16_t)(model_mesh_get_hardpoint_z(
						  source_object->object_type,
						  mesh_idx,
						  alternate_hardpoint_idx) >>
					  1);
		} else {
			hardpoint_x = (int16_t)model_mesh_get_hardpoint_x(
				source_object->object_type, mesh_idx,
				alternate_hardpoint_idx);
			hardpoint_y = (int16_t)model_mesh_get_hardpoint_y(
				source_object->object_type, mesh_idx,
				alternate_hardpoint_idx);
			hardpoint_z = (int16_t)model_mesh_get_hardpoint_z(
				source_object->object_type, mesh_idx,
				alternate_hardpoint_idx);
		}
		int mesh_type;
		{
			int source_object_type = source_object->object_type;

			if (source_object_type < MODEL_TYPE_CACHE_CAPACITY) {
				mesh_type =
					model_mesh_get_cached_object_type_mesh_type(
						source_object_type, mesh_idx);
			} else {
				mesh_type =
					model_mesh_get_object_type_mesh_type(
						source_object_type, mesh_idx);
			}
		}
		if (mesh_type == MESH_COMPONENT_21_ROTATING_LASR_TUR) {
			g_rotated_x = hardpoint_x;
			g_rotated_y = hardpoint_y;
			g_rotated_z = hardpoint_z;
			if (source_object->object_type ==
			    IMPERIAL_STAR_DESTROYER_OBJECT_TYPE) {
				g_rotated_x *= 2;
				g_rotated_y *= 2;
				g_rotated_z *= 2;
			}
			model_mesh_apply_animated_mesh_rotation_to_point(
				(int16_t)(g_cur_craft->mesh_rotation[mesh_idx]
					  << ANIMATED_MESH_ANGLE_SHIFT),
				source_object->object_type, mesh_idx,
				g_rotated_x, g_rotated_y, g_rotated_z);
			if (source_object->object_type ==
			    IMPERIAL_STAR_DESTROYER_OBJECT_TYPE) {
				g_rotated_x >>= 1;
				g_rotated_y >>= 1;
				g_rotated_z >>= 1;
			}
			hardpoint_x = (int16_t)g_rotated_x;
			hardpoint_y = (int16_t)g_rotated_y;
			hardpoint_z = (int16_t)g_rotated_z;
		}
		pai_calcrotatedpoint(source_object, hardpoint_x, hardpoint_z,
				     hardpoint_y);
	}
	if (source_object->object_type == IMPERIAL_STAR_DESTROYER_OBJECT_TYPE) {
		g_rotated_x *= 2;
		g_rotated_y *= 2;
		g_rotated_z *= 2;
	}
	launch_x += g_rotated_x;
	launch_y += g_rotated_y;
	launch_z += g_rotated_z;

	int target_obj_idx = target_ref;
	mission_resolve_object_or_mission_point_world_loc(
		(uint16_t)target_obj_idx, 0);
	int target_x = g_world_loc_x;
	int target_y = g_world_loc_y;
	int target_z = g_world_loc_z;
	int16_t fires_ion;
	{
		int target_delta[3];

		target_delta[0] = target_x - launch_x;
		target_delta[1] = target_y - launch_y;
		target_delta[2] = target_z - launch_z;
		if ((unsigned int)collide_roughdistance3d(
			    target_delta[0], target_delta[1], target_delta[2]) >
		    TURRET_FIRE_RANGE) {
			return;
		}
		g_collision_probe_world_x = target_x;
		g_collision_segment_start_world_x = launch_x;
		g_collision_probe_world_y = target_y;
		g_collision_segment_start_world_y = launch_y;
		g_collision_probe_world_z = target_z;
		g_collision_segment_start_world_z = launch_z;
		int collision_blocked;
		if (source_object->object_type ==
		    SUPER_STAR_DESTROYER_OBJECT_TYPE) {
			g_turret_fire_hull_mesh_ordinal = main_hull_mesh_idx;
			g_collide_sweep_reject_near_start_hits = 1;
			collision_blocked = collide_check_swept_model_collision(
				source_obj_idx, source_obj_idx);
			g_collide_sweep_reject_near_start_hits = 0;
		} else {
			collision_blocked = collide_check_swept_model_collision(
				source_obj_idx, source_obj_idx);
		}
		if (collision_blocked != 0) {
			return;
		}

		fires_ion =
			g_cur_craft->weapon_slots[weapon_slot_idx].ammo_count;
		if (g_object_table[target_obj_idx].mobj != NULL) {
			trig2_ctop(target_delta[0], target_delta[1],
				   target_delta[2]);
			trig2_polardistance *= g_sim_steps_per_second;
			if (fires_ion != 0) {
				trig2_polardistance >>= 15;
			} else {
				trig2_polardistance >>= 14;
			}
			uint16_t lead_frames = (uint16_t)trig2_polardistance;
			lead_frames =
				(uint16_t)(lead_frames + (game_rand() & 3));
			--lead_frames;
			if (g_object_table[target_obj_idx].mobj->speed == 0) {
				lead_frames = 0;
			}
			uint16_t lead_scale = (uint16_t)math2_fraction(
				lead_frames, effective_skill);
			target_x +=
				lead_scale *
				((xvt_flight_timing_is_unlocked()
					  ? xvt_reference_motion_axis_displacement(
						    target_ref, 0) +
						    (target_x -
						     g_object_table
							     [target_obj_idx]
								     .world_x)
					  : (target_x -
					     g_object_table[target_obj_idx]
						     .mobj->prev_world_x)));
			target_y +=
				lead_scale *
				((xvt_flight_timing_is_unlocked()
					  ? xvt_reference_motion_axis_displacement(
						    target_ref, 1) +
						    (target_y -
						     g_object_table
							     [target_obj_idx]
								     .world_y)
					  : (target_y -
					     g_object_table[target_obj_idx]
						     .mobj->prev_world_y)));
			target_z +=
				lead_scale *
				((xvt_flight_timing_is_unlocked()
					  ? xvt_reference_motion_axis_displacement(
						    target_ref, 2) +
						    (target_z -
						     g_object_table
							     [target_obj_idx]
								     .world_z)
					  : (target_z -
					     g_object_table[target_obj_idx]
						     .mobj->prev_world_z)));
		}
	}
	trig2_ctop(target_x - launch_x, target_y - launch_y,
		   target_z - launch_z);
	uint16_t projectile_yaw = trig2_xyangle;
	int16_t projectile_pitch = trig2_pitch;
	{
		uint16_t accuracy_threshold = UINT16_MAX;

		if ((uint16_t)game_rand() > accuracy_threshold) {
			int16_t aim_error =
				(int16_t)((game_rand() - AIM_ERROR_BASE) &
					  AIM_ERROR_MASK);

			if ((uint16_t)game_rand() >= 0x8000u) {
				aim_error = (int16_t)-aim_error;
			}
			projectile_yaw = (uint16_t)(projectile_yaw + aim_error);
			aim_error = (int16_t)((game_rand() - AIM_ERROR_BASE) &
					      AIM_ERROR_MASK);
			if ((uint16_t)game_rand() >= 0x8000u) {
				projectile_pitch =
					(int16_t)(projectile_pitch - aim_error);
				if ((projectile_pitch & ANGLE_WRAPPED) != 0) {
					projectile_pitch = 0;
				}
			} else {
				projectile_pitch =
					(int16_t)(projectile_pitch + aim_error);
				if ((projectile_pitch & ANGLE_WRAPPED) != 0) {
					projectile_pitch = INT16_MAX;
				}
			}
		}
	}
	uint16_t projectile_obj_idx =
		object_alloc_slot_for_genus(CRAFT_GENUS_OTHER_PROJECTILE);
	if (projectile_obj_idx == UINT16_MAX) {
		return;
	}

	g_object_table[projectile_obj_idx].mobj->family = 1;
	g_object_table[projectile_obj_idx].genus_id =
		CRAFT_GENUS_OTHER_PROJECTILE;
	g_object_table[projectile_obj_idx].mobj->iff = source_object->mobj->iff;
	uint16_t heavy_turbo_laser = 0;
	for (uint16_t weapon_group_index = 0;
	     weapon_group_index < WEAPON_GROUP_COUNT; ++weapon_group_index) {
		if (g_model_defs[model_index].laser_group_first_slot
				    [weapon_group_index] <= weapon_slot_idx &&
		    g_model_defs[model_index].laser_group_last_slot
				    [weapon_group_index] >= weapon_slot_idx &&
		    (g_model_defs[model_index].laser_group_weapon_type
				     [weapon_group_index] ==
			     PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER_2 ||
		     g_model_defs[model_index].laser_group_weapon_type
				     [weapon_group_index] ==
			     PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER_2)) {
			heavy_turbo_laser = 1;
		}
	}
	uint16_t projectile_type;
	if (fires_ion != 0) {
		projectile_type =
			heavy_turbo_laser != 0
				? PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER
				: PROJECTILE_OBJECT_TYPE_ION_LASER;
	} else if (source_object->mobj->iff == 0 ||
		   source_object->mobj->iff == 2) {
		projectile_type =
			heavy_turbo_laser != 0
				? PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER_2
				: PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER;
	} else {
		projectile_type =
			heavy_turbo_laser != 0
				? PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER_2
				: PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER;
	}
	g_object_table[projectile_obj_idx].object_type =
		(uint8_t)projectile_type;
	g_object_table[projectile_obj_idx].mobj->seconds_alive = 1;
	g_object_table[projectile_obj_idx].mobj->source_obj_idx =
		source_obj_idx;
	g_object_table[projectile_obj_idx].mobj->source_object_type =
		source_object->object_type;
	g_object_table[projectile_obj_idx].pitch = projectile_pitch;
	g_object_table[projectile_obj_idx].roll = 0;
	g_object_table[projectile_obj_idx].yaw = (int16_t)projectile_yaw;
	g_object_table[projectile_obj_idx].mobj->orient_matrix_dirty = 1;
	g_object_table[projectile_obj_idx].mobj->move_vector_dirty =
		g_object_table[projectile_obj_idx].mobj->orient_matrix_dirty;
	g_object_table[projectile_obj_idx].mobj->speed =
		g_projectile_type_data
			.speed[projectile_type - PROJECTILE_OBJECT_TYPE_FIRST];
	g_object_table[projectile_obj_idx].mobj->damage_amount =
		g_projectile_type_data
			.damage[projectile_type - PROJECTILE_OBJECT_TYPE_FIRST];
	g_object_table[projectile_obj_idx].mobj->lifetime_timer =
		(uint16_t)(PROJECTILE_LIFETIME_SCALE *
			   laser_get_projectile_lifetime_ticks(
				   projectile_type));
	fview_calcrotatemove(projectile_pitch, (int16_t)projectile_yaw,
			     &g_object_table[projectile_obj_idx]);
	g_object_table[projectile_obj_idx].mobj->prev_world_x = launch_x;
	g_object_table[projectile_obj_idx].mobj->prev_world_y = launch_y;
	g_object_table[projectile_obj_idx].mobj->prev_world_z = launch_z;
	int launch_offset =
		(int16_t)g_projectile_type_data
			.launch_offset[projectile_type -
				       PROJECTILE_OBJECT_TYPE_FIRST];
	launch_x += math_mul_q15(g_fview_move_x_q15, launch_offset);
	launch_y += math_mul_q15(g_fview_move_y_q15, launch_offset);
	launch_z += math_mul_q15(g_fview_move_z_q15, launch_offset);
	g_object_table[projectile_obj_idx].world_x = launch_x;
	g_object_table[projectile_obj_idx].world_y = launch_y;
	g_object_table[projectile_obj_idx].world_z = launch_z;

	int guidance_index =
		(uint16_t)(projectile_obj_idx - g_projectile_object_slot_start);
	g_projectile_guidance_states[guidance_index].homing_tier = 0;
	g_projectile_guidance_states[guidance_index].target_obj_idx =
		target_ref;
	if (target_ref == UINT16_MAX || target_ref >= 0x8000u) {
		g_projectile_guidance_states[guidance_index].target_signature =
			0;
	} else {
		g_projectile_guidance_states[guidance_index].target_signature =
			g_object_table[target_ref].object_signature;
	}
	g_projectile_guidance_states[guidance_index].cruise_speed =
		g_object_table[projectile_obj_idx].mobj->speed;
	g_projectile_guidance_states[guidance_index].source_player_idx = -1;
	g_object_table[projectile_obj_idx].mobj->p_warhead_guidance =
		&g_projectile_guidance_states[guidance_index];
	fsfx_triggerweaponsfx(projectile_obj_idx, g_local_player);
}
