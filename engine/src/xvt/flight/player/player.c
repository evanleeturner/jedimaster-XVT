#include "xvt/flight/player/player.h"

#include "xvt_runtime/hooks/orientation_hook.h"
#include "xvt_runtime/timing/flight_timing.h"

#include "xvt_runtime/timing/player_timing.h"
#include "xvt_runtime/input/flight_controls.h"
#include <limits.h>
#include <string.h>
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paiman.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/targeting.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/net_session.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/log/log_both_builds.h"

/* Per player, countdown timers for HUD panes and redraws, in ticks.
 * flight_update_timers counts each down by g_elapsed_ticks for participating
 * players; player_bind_to_available_craft zeroes a player's set. Many functions
 * write them, chiefly in the HUD, message and collision code. */
// GLOBAL: XVT 0x9D8B80
struct player_flight_transient_timers g_player_flight_transient_timers[8];
/* Slot, 0 to 7, of the player at this machine. Two functions write it, both at
 * flight start from net_session_find_player_slot_by_dpid: flight_main_loop in the
 * original build and xvt_flight_loading_globals in the modern one. */
// GLOBAL: XVT 0x9ECC34
int g_local_player;
/* Each player's flight state: the craft flown, targeting, saved settings,
 * mission tallies, camera and chat. Many functions write it. */
// GLOBAL: XVT 0x9E9670
struct player_data g_players[8];
/* Each player's four taunt lines, 70 bytes each, sent as chat by keys 155 to
 * 158. Alone, a player gets g_game_config.taunts; in multiplayer each slot holds
 * what arrived over the network. Written by
 * flight_net_sync_player_options_and_taunts in the original build and by
 * xvt_flight_network_read_taunts and xvt_flight_network_exchange_options in the
 * modern one. */
// GLOBAL: XVT 0x9D7800
char g_player_taunt_text[8][4][70] = {{{0}}};

/* Binds a player to one of their flight groups' craft that is not breaking up,
 * exploding or entering hyperspace: the one with signature
 * preferred_object_signature when given and found, else the next after
 * previous_object_idx, wrapping through the active region's craft slots. Returns
 * 1, changing nothing, when there is none; else 0. The craft becomes the
 * player's: laser banks and launchers reset, then the player's saved link
 * modes, warhead flags, shield distribution (the front bank's energy shared out
 * again, at most twice shield_strength a bank) and recharge levels restored, and
 * the saved throttle when the signature matched or previous_object_idx was given.
 * The player gets object_index and bound_object_signature; loses hyperspace,
 * missile lock, the pending action and beam timers, input smoothing, key mods
 * and engine wash state; keeps the weapon selection only on a signature match;
 * and with reset_targeting_state 1 gets the target box on and no target or
 * presets. When previous_object_idx names an object, the craft's AI target
 * becomes the player's target. The craft gets plan nullpln and no AI target;
 * the player's transient timers, hardpoint position, HUD aim and camera are
 * reset. The local player also gets the craft name redrawn and
 * FLIGHT_SOUND_BOMB_1. Ends with flight_compute_live_world_state_checksum. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x45A1E0
int player_bind_to_available_craft(int player_idx, uint32_t previous_object_idx,
				   int preferred_object_signature,
				   int reset_targeting_state)
{
	enum {
		OBJECT_TYPE_NONE = 0,
		WEAPON_BANK_COUNT = 2,
		LASER_LINK_DEFAULT = 1,
		/* WARHEAD_LINK_DEFAULT sets bit 0 again right after this mask,
		 * so 0x81 keeps only the side-select bit, as the 0x80 mask of
		 * the same name does later in this file. */
		WARHEAD_KEEP_SIDE_SELECT_MASK = 0x81,
		WARHEAD_SAVED_STATE_PRESERVE_MASK = 0x80,
		WARHEAD_LINK_DEFAULT = 1,
		GUNNER_LASER_MOUNT_TYPE = 2,
		TARGET_OBJECT_INDEX_LIMIT = 0x8000,
		DEFAULT_CAMERA_DISTANCE = 1024,
	};

	int selected_object_idx;

	int found_craft = 0;
	int matched_preferred_signature = 0;
	if (preferred_object_signature != 0) {
		for (selected_object_idx = g_active_region_object_slot_start;
		     selected_object_idx <
		     g_active_region_craft_object_slot_end;
		     ++selected_object_idx) {
			if (g_object_table[selected_object_idx].object_type !=
			    OBJECT_TYPE_NONE) {
				uint8_t object_kind =
					g_object_table[selected_object_idx]
						.mobj->p_craft->object_kind;
				if (g_mission_flight_groups
					    [g_object_table[selected_object_idx]
						     .flight_group_idx]
						    .player_owner_idx ==
				    player_idx) {
					if (object_kind !=
						    CRAFT_OBJECT_KIND_BREAKING_UP &&
					    object_kind !=
						    CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE &&
					    object_kind !=
						    CRAFT_OBJECT_KIND_EXPLODING &&
					    g_object_table[selected_object_idx]
							    .object_signature ==
						    preferred_object_signature) {
						found_craft = 1;
						matched_preferred_signature = 1;
						break;
					}
				}
			}
		}
	}

	if (found_craft == 0) {
		selected_object_idx = previous_object_idx;
		int objects_remaining = g_active_region_craft_object_slot_end -
					g_active_region_object_slot_start;
		while (objects_remaining != 0) {
			++selected_object_idx;
			if (selected_object_idx >=
			    g_active_region_craft_object_slot_end) {
				selected_object_idx =
					g_active_region_object_slot_start;
			}
			if (g_object_table[selected_object_idx].object_type !=
			    OBJECT_TYPE_NONE) {
				uint8_t object_kind =
					g_object_table[selected_object_idx]
						.mobj->p_craft->object_kind;
				if (g_mission_flight_groups
					    [g_object_table[selected_object_idx]
						     .flight_group_idx]
						    .player_owner_idx ==
				    player_idx) {
					if (object_kind !=
						    CRAFT_OBJECT_KIND_BREAKING_UP &&
					    object_kind !=
						    CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE &&
					    object_kind !=
						    CRAFT_OBJECT_KIND_EXPLODING) {
						break;
					}
				}
			}
			--objects_remaining;
		}
		if (objects_remaining == 0) {
			XVT_LOG_DEBUG(
				"player.craft_unavailable slot=%d previous=%d preferred=%d predicted=%d",
				player_idx, (int)previous_object_idx,
				preferred_object_signature,
				g_flight_sim_side_effects_suppressed);
			return 1;
		}
	}

	g_object_table[selected_object_idx].player_owner_idx = player_idx;
	g_object_table[selected_object_idx].mobj->orient_matrix_dirty = 1;
	g_object_table[selected_object_idx].mobj->move_vector_dirty = 1;
	collide_reset_object_proximity_for_slot((uint16_t)selected_object_idx);
	struct craft_data *craft =
		g_object_table[selected_object_idx].mobj->p_craft;
	{
		for (int weapon_bank = 0; weapon_bank < WEAPON_BANK_COUNT;
		     ++weapon_bank) {
			craft->laser_state.link_mode[weapon_bank] =
				LASER_LINK_DEFAULT;
			craft->laser_state.burst_remaining[weapon_bank] = 0;
			craft->laser_state.next_slot[weapon_bank] = 0;
			craft->laser_state.fire_cooldown_ticks[weapon_bank] = 0;
			craft->laser_state.next_fire_timestamp[weapon_bank] = 0;
			model_index model_index = get_model_index_from_type(
				g_object_table[selected_object_idx]
					.object_type);
			if (craft->laser_state
					    .projectile_type_id[weapon_bank] !=
				    0 &&
			    g_model_defs[model_index].laser_group_mount_type
					    [weapon_bank] !=
				    GUNNER_LASER_MOUNT_TYPE) {
				craft->laser_state.next_slot[weapon_bank] =
					g_model_defs[model_index]
						.laser_group_first_slot
							[weapon_bank];
			}
		}
	}
	craft->laser_state.link_mode[0] =
		g_players[player_idx].saved_craft_settings.laser_link_mode[0];
	craft->laser_state.link_mode[1] =
		g_players[player_idx].saved_craft_settings.laser_link_mode[1];
	{
		for (int launcher_index = 0; launcher_index < WEAPON_BANK_COUNT;
		     ++launcher_index) {
			craft->warhead_launcher_flags[launcher_index] =
				(int8_t)((craft->warhead_launcher_flags
						  [launcher_index] &
					  WARHEAD_KEEP_SIDE_SELECT_MASK) |
					 WARHEAD_LINK_DEFAULT);
			craft->warhead_launcher_cooldown_ticks[launcher_index] =
				0;
		}
	}
	craft->warhead_launcher_flags[0] =
		(int8_t)((craft->warhead_launcher_flags[0] &
			  WARHEAD_SAVED_STATE_PRESERVE_MASK) |
			 g_players[player_idx]
				 .saved_craft_settings
				 .warhead_launcher_flags[0]);
	craft->warhead_launcher_flags[1] =
		(int8_t)((craft->warhead_launcher_flags[1] &
			  WARHEAD_SAVED_STATE_PRESERVE_MASK) |
			 g_players[player_idx]
				 .saved_craft_settings
				 .warhead_launcher_flags[1]);
	craft->warhead_lock_ticks = 0;
	{
		enum { FRONT_SHIELD = 0, REAR_SHIELD = 1 };

		int max_shield_per_face =
			2 * g_model_defs[craft->model_index].shield_strength;
		uint8_t shield_distribution_mode =
			g_players[player_idx]
				.saved_craft_settings.shield_distrib_mode;
		craft->shield_distrib_mode = shield_distribution_mode;
		switch (shield_distribution_mode) {
		case SHIELD_DISTRIBUTION_FULLY_FORWARD:
			if (craft->shield_energy[FRONT_SHIELD] >
			    max_shield_per_face) {
				craft->shield_energy[REAR_SHIELD] =
					craft->shield_energy[FRONT_SHIELD] -
					max_shield_per_face;
				craft->shield_energy[FRONT_SHIELD] =
					max_shield_per_face;
			}
			break;
		case SHIELD_DISTRIBUTION_EVEN:
			craft->shield_energy[FRONT_SHIELD] >>= 1;
			craft->shield_energy[REAR_SHIELD] =
				craft->shield_energy[FRONT_SHIELD];
			break;
		case SHIELD_DISTRIBUTION_FULLY_AFT:
			craft->shield_energy[REAR_SHIELD] =
				craft->shield_energy[FRONT_SHIELD];
			craft->shield_energy[FRONT_SHIELD] = 0;
			if (craft->shield_energy[REAR_SHIELD] >
			    max_shield_per_face) {
				craft->shield_energy[FRONT_SHIELD] =
					craft->shield_energy[REAR_SHIELD] -
					max_shield_per_face;
				craft->shield_energy[REAR_SHIELD] =
					max_shield_per_face;
			}
			break;
		}
	}
	craft->shield_recharge_level =
		g_players[player_idx]
			.saved_craft_settings.shield_recharge_level;
	craft->laser_recharge_level =
		g_players[player_idx].saved_craft_settings.laser_recharge_level;
	craft->beam_recharge_level =
		g_players[player_idx].saved_craft_settings.beam_level;

	g_players[player_idx].object_index = selected_object_idx;
	g_players[player_idx].bound_object_signature =
		g_object_table[selected_object_idx].object_signature;
	g_players[player_idx].awaiting_new_craft = 0;
	g_players[player_idx].hyperspace_phase = 0;
	if (matched_preferred_signature != 0 ||
	    previous_object_idx != UINT32_MAX) {
		craft->throttle_speed =
			g_players[player_idx]
				.saved_craft_settings.throttle_speed;
	}
	if (matched_preferred_signature == 0) {
		g_players[player_idx].selected_weapon_bank = 0;
		g_players[player_idx].selected_weapon_mode = 0;
	}
	g_players[player_idx].target_cycle_start = -1;
	g_players[player_idx].targeting_state = -1;
	g_players[player_idx].selected_target_component = 0;
	if (reset_targeting_state == 1) {
		g_players[player_idx].target_box_enabled = 1;
		g_players[player_idx].current_target_object_idx = -1;
		int16_t *target_preset_slots =
			g_players[player_idx].target_preset_slot;
		memset(target_preset_slots, 0xFF,
		       sizeof(g_players[player_idx].target_preset_slot));
	}
	if (previous_object_idx != UINT32_MAX &&
	    g_object_table[previous_object_idx].mobj != NULL) {
		g_players[player_idx].current_target_object_idx = -1;
		uint16_t target_object_idx =
			craft->ai_controller.target_obj_idx;
		if (target_object_idx < TARGET_OBJECT_INDEX_LIMIT) {
			g_players[player_idx].current_target_object_idx =
				(int16_t)target_object_idx;
			g_players[player_idx].selected_target_component = 0;
		}
	}
	if ((uint16_t)g_players[player_idx].current_target_object_idx ==
	    selected_object_idx) {
		g_players[player_idx].current_target_object_idx = -1;
	}
	player_validate_current_targets(player_idx);
	g_players[player_idx].missile_lock_state = 0;
	craft->ai_controller.running_plan_id =
		(uint8_t)pai_find_plan_id_by_name_or_zero("nullpln");
	craft->ai_controller.target_obj_idx = UINT16_MAX;
	g_players[player_idx].pending_action_timer = 0;
	g_players[player_idx].beam_fire_cooldown_timer = 0;
	g_players[player_idx].yaw_roll_swap = 0;
	g_players[player_idx].smoothed_input_yaw = 0;
	g_players[player_idx].smoothed_input_pitch = 0;
	g_players[player_idx].saved_key_mods = 0;
	g_players[player_idx].key_mods_hold_timer = 0;
	g_players[player_idx].engine_wash_source_obj_idx = -1;
	memset(&g_player_flight_transient_timers[player_idx], 0,
	       sizeof(g_player_flight_transient_timers[player_idx]));
	{
		model_index model_index = get_model_index_from_type(
			g_object_table[g_players[player_idx].object_index]
				.object_type);
		pai_calcrotatedpoint(
			&g_object_table[g_players[player_idx].object_index], 0,
			g_model_defs[model_index].primary_hardpoint_z,
			g_model_defs[model_index].primary_hardpoint_y);
	}
	g_players[player_idx].hardpoint_world_x = g_rotated_x;
	g_players[player_idx].hardpoint_world_y = g_rotated_y;
	g_players[player_idx].hardpoint_world_z = g_rotated_z;
	g_players[player_idx].prev_hardpoint_world_x =
		g_players[player_idx].hardpoint_world_x;
	g_players[player_idx].prev_hardpoint_world_y =
		g_players[player_idx].hardpoint_world_y;
	g_players[player_idx].prev_hardpoint_world_z =
		g_players[player_idx].hardpoint_world_z;
	g_players[player_idx].view_state.hud_aim_x_snap_state = 0;
	g_players[player_idx].view_state.hud_aim_x = 0;
	g_players[player_idx].view_state.hud_aim_y = 0;
	g_players[player_idx].view_state.external_camera_active = 0;
	g_players[player_idx].view_state.camera_distance =
		DEFAULT_CAMERA_DISTANCE;
	g_players[player_idx].view_state.player_input_blocked = 0;
	g_players[player_idx].view_state.target_camera_active = 0;
	g_players[player_idx].view_state.camera_focus_obj_idx =
		(uint16_t)g_players[player_idx].object_index;
	if (g_players[player_idx].saved_hud_view_state == HUD_VIEW_HUD_ONLY) {
		hud_force_player_view_state(HUD_VIEW_HUD_ONLY, player_idx);
	} else {
		hud_force_player_view_state(HUD_VIEW_FORWARD, player_idx);
	}
	if (player_idx == g_local_player) {
		hud_draw_craft_name_fps_and_network_status();
		fsfx_play_sound(FLIGHT_SOUND_BOMB_1, -1, player_idx);
	}
	flight_compute_live_world_state_checksum();
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO(
			"player.craft_bound slot=%d object=%d fg=%d craft=%d kind=\"%s\" player=%u tick=%d",
			player_idx, selected_object_idx,
			(int)g_object_table[selected_object_idx]
				.flight_group_idx,
			(int)g_mission_flight_groups
				[g_object_table[selected_object_idx]
					 .flight_group_idx]
					.fg.craft_type,
			matched_preferred_signature != 0    ? "same"
			: previous_object_idx != UINT32_MAX ? "next"
							    : "replacement",
			(unsigned)g_players[player_idx].network.direct_play_id,
			g_game_time);
	}
	XVT_LOG_DEBUG(
		"player.craft_bound_state slot=%d object=%d signature=%u throttle=%u distribution=%d front=%d rear=%d target=%d predicted=%d",
		player_idx, selected_object_idx,
		(unsigned)g_players[player_idx].bound_object_signature,
		(unsigned)craft->throttle_speed,
		(int)craft->shield_distrib_mode, craft->shield_energy[0],
		craft->shield_energy[1],
		(int)g_players[player_idx].current_target_object_idx,
		g_flight_sim_side_effects_suppressed);
	return 0;
}

/* Hands the player's craft back to the AI. Returns 0, changing nothing, when
 * require_multiple_craft is 1 and the player owns at most one craft not breaking
 * up, exploding or entering hyperspace, or when the player has no craft; else
 * 1. Saves the craft settings, then clears the craft's owner, proximity lists,
 * laser bank state, launcher cooldowns and lock, sets its launchers to link
 * mode 1 keeping bit 7, and puts all its shield energy in the front bank.
 * Clears the player's craft, hyperspace, missile lock, input smoothing and
 * engine wash state. With assign_ai_plan the craft takes its flight group's first
 * order: plan, throttle (0 for nullpln, the stationary plans and disabledpln,
 * 0x8000 for escortldr1pln) and speed, and the player's target as candidate
 * target when an order aims at it; otherwise plan nullpln and no steering.
 * current_order_slot and the plan ids are written to the craft g_cur_craft points
 * at on entry, which is this craft only when the caller left it there;
 * g_cur_craft ends at this craft. */
// FUNCTION: XVT 0x45A850
int player_unbind_from_current_craft(int player_index,
				     int require_multiple_craft,
				     int assign_ai_plan)
{
	enum {
		MAX_OWNED_CRAFT_WITHOUT_REPLACEMENT = 1,
		WEAPON_BANK_COUNT = 2,
		ORDER_SLOT_COUNT = 3,
		WARHEAD_KEEP_SIDE_SELECT_MASK = 0x80,
		WARHEAD_LINK_DEFAULT = 1,
		ESCORT_THROTTLE_SPEED = 0x8000,
	};

	if (require_multiple_craft == 1) {
		uint16_t owned_craft_count = 0;
		for (int object_slot = g_active_region_object_slot_start;
		     object_slot < g_active_region_craft_object_slot_end;
		     ++object_slot) {
			if (g_object_table[object_slot].object_type != 0 &&
			    g_mission_flight_groups[g_object_table[object_slot]
							    .flight_group_idx]
					    .player_owner_idx == player_index &&
			    g_object_table[object_slot]
					    .mobj->p_craft->object_kind !=
				    CRAFT_OBJECT_KIND_BREAKING_UP &&
			    g_object_table[object_slot]
					    .mobj->p_craft->object_kind !=
				    CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE &&
			    g_object_table[object_slot]
					    .mobj->p_craft->object_kind !=
				    CRAFT_OBJECT_KIND_EXPLODING) {
				++owned_craft_count;
			}
		}
		if (owned_craft_count <= MAX_OWNED_CRAFT_WITHOUT_REPLACEMENT) {
			XVT_LOG_DEBUG(
				"player.craft_kept slot=%d owned=%u predicted=%d",
				player_index, (unsigned)owned_craft_count,
				g_flight_sim_side_effects_suppressed);
			return 0;
		}
	}

	int object_idx = g_players[player_index].object_index;
	if (object_idx == -1) {
		XVT_LOG_DEBUG(
			"player.craft_release_skipped slot=%d predicted=%d",
			player_index, g_flight_sim_side_effects_suppressed);
		return 0;
	}

	g_object_table[object_idx].player_owner_idx = -1;
	g_object_table[object_idx].mobj->orient_matrix_dirty = 1;
	g_object_table[object_idx].mobj->move_vector_dirty = 1;
	collide_reset_neighbor_proximity_lists((uint16_t)object_idx);
	collide_reset_object_proximity_for_slot((uint16_t)object_idx);
	player_save_craft_settings(player_index);

	struct craft_data *craft = g_object_table[object_idx].mobj->p_craft;
	for (int laser_bank = 0; laser_bank < WEAPON_BANK_COUNT; ++laser_bank) {
		craft->laser_state.link_mode[laser_bank] = 0;
		craft->laser_state.burst_remaining[laser_bank] = 0;
		craft->laser_state.next_slot[laser_bank] = 0;
		craft->laser_state.fire_cooldown_ticks[laser_bank] = 0;
		craft->laser_state.next_fire_timestamp[laser_bank] = 0;
	}
	for (int launcher_index = 0; launcher_index < WEAPON_BANK_COUNT;
	     ++launcher_index) {
		craft->warhead_launcher_flags[launcher_index] =
			(int8_t)((craft->warhead_launcher_flags
					  [launcher_index] &
				  WARHEAD_KEEP_SIDE_SELECT_MASK) |
				 WARHEAD_LINK_DEFAULT);
		craft->warhead_launcher_cooldown_ticks[launcher_index] = 0;
	}
	craft->warhead_lock_ticks = 0;
	craft->shield_energy[0] += craft->shield_energy[1];
	craft->shield_energy[1] = 0;
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_FORWARD;

	g_players[player_index].object_index = -1;
	g_players[player_index].awaiting_new_craft = 0;
	g_players[player_index].hyperspace_phase = 0;
	g_players[player_index].missile_lock_state = 0;
	g_players[player_index].yaw_roll_swap = 0;
	g_players[player_index].smoothed_input_yaw = 0;
	g_players[player_index].smoothed_input_pitch = 0;
	g_players[player_index].saved_key_mods = 0;
	g_players[player_index].key_mods_hold_timer = 0;
	g_players[player_index].engine_wash_source_obj_idx = -1;
	XVT_LOG_DEBUG(
		"player.release_plan_craft slot=%d object=%d same=%d predicted=%d",
		player_index, object_idx, g_cur_craft == craft,
		g_flight_sim_side_effects_suppressed);
	g_cur_craft->ai_controller.current_order_slot = 0;

	if (assign_ai_plan != 0) {
		uint16_t plan_id = g_builtin_plan_id_by_name_index
			[g_order_leader_builtin_plan_name_index
				 [g_mission_flight_groups
					  [g_object_table[object_idx]
						   .flight_group_idx]
						  .fg.orders[0]
						  .order]];

		g_cur_craft->ai_controller.current_plan_id = plan_id;
		g_cur_craft->ai_controller.running_plan_id = plan_id;
		const char *plan_name = g_plan_table[plan_id].name;
		uint16_t throttle_speed;
		if (strcmp(plan_name, "nullpln") == 0 ||
		    strcmp(plan_name, "stationaryldrpln") == 0 ||
		    strcmp(plan_name, "stationaryflwpln") == 0 ||
		    strcmp(plan_name, "disabledpln") == 0) {
			throttle_speed = 0;
		} else if (strcmp(plan_name, "escortldr1pln") == 0) {
			throttle_speed = ESCORT_THROTTLE_SPEED;
		} else {
			throttle_speed =
				g_order_throttle_to_craft_throttle_speed
					[g_mission_flight_groups
						 [g_object_table[object_idx]
							  .flight_group_idx]
							 .fg.orders[0]
							 .throttle];
		}
		craft->throttle_speed = throttle_speed;
		g_object_table[object_idx].mobj->speed =
			(uint16_t)math2_fraction(
				g_model_defs[craft->model_index].max_speed,
				throttle_speed);
		g_object_table[object_idx].mobj->speed_remainder = 0;
		g_cur_craft = craft;
		pai_setupcraftcontext((uint16_t)object_idx);
		pai_apply_running_plan_target_and_maneuver(
			(unsigned int)object_idx);

		if (g_players[player_index].current_target_object_idx != -1) {
			int target_obj_idx = (uint16_t)g_players[player_index]
						     .current_target_object_idx;
			if (g_active_region_craft_object_slot_end >
				    target_obj_idx ||
			    (g_region_main_object_slot_end <= target_obj_idx &&
			     g_region_main_object_slot_end +
					     g_region_static_object_slot_count >
				     target_obj_idx)) {
				int target_matches_order = 0;
				for (unsigned int order_slot = 0;
				     order_slot < ORDER_SLOT_COUNT;
				     ++order_slot) {
					g_pai_context.order_slot =
						(uint16_t)order_slot;
					if (pai_current_order_targets_match_object(
						    (uint16_t)g_players[player_index]
							    .current_target_object_idx) !=
					    0) {
						target_matches_order = 1;
					}
				}
				if (target_matches_order != 0) {
					craft->ai_controller
						.candidate_target_idx =
						(uint16_t)g_players[player_index]
							.current_target_object_idx;
				}
			}
		}
	} else {
		g_cur_craft->ai_controller.running_plan_id =
			(uint8_t)pai_find_plan_id_by_name_or_zero("nullpln");
		g_cur_craft->ai_controller.current_plan_id =
			g_cur_craft->ai_controller.running_plan_id;
		g_cur_craft = craft;
		pai_setupcraftcontext((uint16_t)object_idx);
		pai_apply_running_plan_target_and_maneuver(
			(unsigned int)object_idx);
		craft->ai_flight.roll_state = 0;
		craft->ai_flight.pitch_state = 0;
		craft->ai_flight.turn_state = 0;
		craft->ai_flight.climb_state = 0;
		craft->ai_flight.dive_state = 0;
	}
	XVT_LOG_INFO(
		"player.craft_released slot=%d object=%d fg=%d orders=%d plan=%d tick=%d",
		player_index, object_idx,
		(int)g_object_table[object_idx].flight_group_idx,
		assign_ai_plan != 0, (int)craft->ai_controller.running_plan_id,
		g_game_time);
	return 1;
}

/* Copies the throttle, recharge levels, shield distribution, laser link modes
 * and the low two bits of each warhead launcher's flags from the player's craft
 * into saved_craft_settings. Does not check that the player has a craft. */
// FUNCTION: XVT 0x45ACD0
void player_save_craft_settings(int player_index)
{
	struct craft_data *craft =
		g_object_table[g_players[player_index].object_index]
			.mobj->p_craft;
	g_players[player_index].saved_craft_settings.throttle_speed =
		craft->throttle_speed;
	g_players[player_index].saved_craft_settings.laser_recharge_level =
		craft->laser_recharge_level;
	g_players[player_index].saved_craft_settings.shield_recharge_level =
		craft->shield_recharge_level;
	g_players[player_index].saved_craft_settings.beam_level =
		craft->beam_recharge_level;
	g_players[player_index].saved_craft_settings.shield_distrib_mode =
		craft->shield_distrib_mode;
	g_players[player_index].saved_craft_settings.laser_link_mode[0] =
		craft->laser_state.link_mode[0];
	g_players[player_index].saved_craft_settings.laser_link_mode[1] =
		craft->laser_state.link_mode[1];
	g_players[player_index].saved_craft_settings.warhead_launcher_flags[0] =
		(uint8_t)(craft->warhead_launcher_flags[0] & 3);
	g_players[player_index].saved_craft_settings.warhead_launcher_flags[1] =
		(uint8_t)(craft->warhead_launcher_flags[1] & 3);
	XVT_LOG_DEBUG(
		"player.settings_saved slot=%d object=%d throttle=%u laser_level=%d shield_level=%d beam_level=%d distribution=%d links=\"%d,%d\" warheads=\"%d,%d\" predicted=%d",
		player_index, g_players[player_index].object_index,
		(unsigned)g_players[player_index]
			.saved_craft_settings.throttle_speed,
		(int)g_players[player_index]
			.saved_craft_settings.laser_recharge_level,
		(int)g_players[player_index]
			.saved_craft_settings.shield_recharge_level,
		(int)g_players[player_index].saved_craft_settings.beam_level,
		(int)g_players[player_index]
			.saved_craft_settings.shield_distrib_mode,
		(int)g_players[player_index]
			.saved_craft_settings.laser_link_mode[0],
		(int)g_players[player_index]
			.saved_craft_settings.laser_link_mode[1],
		(int)g_players[player_index]
			.saved_craft_settings.warhead_launcher_flags[0],
		(int)g_players[player_index]
			.saved_craft_settings.warhead_launcher_flags[1],
		g_flight_sim_side_effects_suppressed);
}

/* Turns one player's stick input into craft rotation or camera movement, after
 * flight_input_apply_deadzone, doubling g_scaled_input_pitch. In the cockpit (input
 * not blocked, no map camera) and outside hyperspace, the model's roll and
 * pitch rates scale with throttle (a third at a stop, full at a third of full
 * throttle, two thirds at full; a craft without engines counts as stopped) and
 * with power: each level the shield and laser recharge levels together sit
 * below 4 adds 0xC00 / 65536 of the rate, each level above takes as much
 * (lasers count twice without shields). The input sets a desired yaw and pitch,
 * zero when flight controls are out or a tractor beam holds the craft without
 * chaff; smoothed_input_yaw and smoothed_input_pitch move toward them, and the
 * steps, scaled by elapsed ticks, turn the craft through
 * player_apply_pitch_yaw_steps, yaw also rolling it. With the roll modifier key
 * (g_flight_key_mods & 0xE is 2) yaw input rolls the craft at twice the step
 * instead; the modern build takes that roll from xvt_flight_controls_roll_step. In
 * hyperspace nothing turns. With input blocked or a map camera, it drives the
 * camera: steps the map camera's transition in map_camera_state, pans the map
 * camera or moves the HUD aim or view by input, and with g_flight_key_mods & 0xF
 * of 1 or 2 moves the camera in or out by camera_distance_step; otherwise it
 * shrinks camera_distance_step back to 32. The modern build's unlocked timing
 * scales the steps with XvtPlayerTiming instead. */
// FUNCTION: XVT 0x480570
void player_update_flight_controls_and_camera(int player_idx)
{
	enum {
		BASE_THROTTLE_SCALE = 0x5555,
		ROLL_RATE_SEGMENT = 0x3800,
		PITCH_RATE_SEGMENT = 0x1400,
		POWER_RECHARGE_NEUTRAL_TOTAL = 4,
		POWER_BALANCE_SCALE = 0xC00,
		CONTROL_SMOOTHING_THRESHOLD = 8,
		CONTROL_SMOOTHING_BASE_STEP = 4,
		KEY_MODIFIER_MASK = 0xE,
		ROLL_CONTROL_MODIFIER = 2,
		MAP_CAMERA_DIRECTION_BIT = 0x80,
		MAP_CAMERA_STATE_MASK = 0x7F,
		MAP_CAMERA_MAX_TRANSITION = 0x7F,
		MAP_CAMERA_YAW_DEADZONE = 128,
		MAP_CAMERA_PITCH_DEADZONE = 48,
		CAMERA_DISTANCE_DEFAULT_STEP = 32,
		CAMERA_DISTANCE_DECAY_SHIFT = 3,
		CAMERA_DISTANCE_COORDINATE_SHIFT = 14,
		CAMERA_DISTANCE_LARGE_LIMIT = 28672,
		CAMERA_DISTANCE_LARGE_THRESHOLD = 57344,
		CAMERA_DISTANCE_MAP_MIN_STEP = 2048,
		CAMERA_DISTANCE_FOCUS_MIN_STEP = 256,
		CAMERA_DISTANCE_FREE_MIN_STEP = 1024,
		CAMERA_DISTANCE_FREE_MAX_STEP = 0x4000,
		CAMERA_DISTANCE_NORMAL_MAX = 5120,
		CAMERA_MINIMUM = 48,
		CAMERA_CLEARANCE = 512,
		CAMERA_WORLD_LIMIT = 0x1000000,
	};

	struct craft_data *craft;
	struct mobile_object *mobile_object;
	struct object_record *target_object;
	int16_t desired_yaw;
	int16_t independent_roll_step;
	int16_t modern_distance_step;
	int16_t previous_distance_step;
	int16_t desired_pitch;
	int16_t yaw_step;
	int16_t pitch_step;
	int16_t smoothed_input;
	int16_t input_difference;
	int16_t smoothing_step;
	int16_t absolute_yaw;
	int16_t absolute_pitch;
	int16_t *camera_distance_step;
	int16_t power_balance;
	int16_t recharge_level_total;
	int16_t laser_recharge_level;
	uint16_t throttle_scale;
	uint16_t roll_rate;
	uint16_t pitch_rate;
	uint16_t segment_count;
	uint16_t segment_fraction;
	uint16_t input_magnitude;
	uint16_t power_scale;
	int16_t transition_magnitude;
	uint16_t key_mode;
	uint16_t camera_key_mode;
	uint8_t camera_state;
	int target_clearance;
	int distance;
	int movement;
	int camera_scale_x;
	int camera_scale_y;
	int camera_scale_z;
	int clearance_scale_x;
	int clearance_scale_y;
	int clearance_scale_z;
	int reverse_scale_x;
	int reverse_scale_y;
	int reverse_scale_z;
	int camera_movement_x;
	int camera_movement_y;
	int camera_movement_z;
	int clearance_movement_x;
	int clearance_movement_y;
	int clearance_movement_z;
	int reverse_movement_x;
	int reverse_movement_y;
	int reverse_movement_z;

	xvt_player_timing_begin_controls(player_idx);
	flight_input_apply_deadzone();
	g_scaled_input_pitch *= 2;
	if (g_players[player_idx].view_state.player_input_blocked == 0 &&
	    g_players[player_idx].map_camera_state == 0) {
		if (g_players[player_idx].hyperspace_phase == 0) {
			mobile_object = g_object_table[g_players[player_idx]
							       .object_index]
						.mobj;
			craft = mobile_object->p_craft;
			throttle_scale = craft->throttle_speed;
			if ((craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_ENGINES) == 0) {
				throttle_scale = 0;
			}
			if (throttle_scale < BASE_THROTTLE_SCALE) {
				throttle_scale =
					(uint16_t)(2 * throttle_scale +
						   BASE_THROTTLE_SCALE);
			} else {
				throttle_scale =
					(uint16_t)((BASE_THROTTLE_SCALE -
						    throttle_scale) /
							   2 -
						   1);
			}

			laser_recharge_level =
				(uint8_t)craft->laser_recharge_level;
			if ((craft->system_flags &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				recharge_level_total =
					(int16_t)((uint8_t)craft
							  ->shield_recharge_level +
						  laser_recharge_level);
			} else {
				recharge_level_total =
					(int16_t)(2 * laser_recharge_level);
			}
			power_balance = (int16_t)(POWER_RECHARGE_NEUTRAL_TOTAL -
						  recharge_level_total);
			if (power_balance < 0) {
				power_scale = (uint16_t)(-POWER_BALANCE_SCALE *
							 power_balance);
			} else {
				power_scale = (uint16_t)(POWER_BALANCE_SCALE *
							 power_balance);
			}
			roll_rate = (uint16_t)math2_fraction(
				craft->ai_flight.roll_rate, throttle_scale);
			if (power_balance > 0) {
				roll_rate =
					(uint16_t)(roll_rate +
						   math2_fraction(roll_rate,
								  power_scale));
			} else {
				roll_rate =
					(uint16_t)(roll_rate -
						   math2_fraction(roll_rate,
								  power_scale));
			}
			segment_count = roll_rate / ROLL_RATE_SEGMENT;
			segment_fraction = (uint16_t)math2_ratio_q16(
				roll_rate % ROLL_RATE_SEGMENT,
				ROLL_RATE_SEGMENT);
			input_magnitude = (uint16_t)g_scaled_input_yaw;
			if (input_magnitude >= 0x8000u) {
				input_magnitude = (uint16_t)-g_scaled_input_yaw;
			}
			desired_yaw =
				(int16_t)(math2_fraction(input_magnitude,
							 segment_fraction) +
					  input_magnitude * segment_count);
			if ((uint16_t)g_scaled_input_yaw >= 0x8000u) {
				desired_yaw = (int16_t)-desired_yaw;
			}
			g_abs_scaled_input_yaw = g_scaled_input_yaw;
			if ((uint16_t)g_scaled_input_yaw >= 0x8000u) {
				g_abs_scaled_input_yaw =
					(int16_t)-g_scaled_input_yaw;
			}

			pitch_rate = (uint16_t)math2_fraction(
				craft->ai_flight.pitch_rate, throttle_scale);
			if (power_balance > 0) {
				pitch_rate =
					(uint16_t)(pitch_rate +
						   math2_fraction(pitch_rate,
								  power_scale));
			} else {
				pitch_rate =
					(uint16_t)(pitch_rate -
						   math2_fraction(pitch_rate,
								  power_scale));
			}
			segment_count = pitch_rate / PITCH_RATE_SEGMENT;
			segment_fraction = (uint16_t)math2_ratio_q16(
				pitch_rate % PITCH_RATE_SEGMENT,
				PITCH_RATE_SEGMENT);
			input_magnitude = (uint16_t)g_scaled_input_pitch;
			if (input_magnitude >= 0x8000u) {
				input_magnitude =
					(uint16_t)-g_scaled_input_pitch;
			}
			desired_pitch =
				(int16_t)(math2_fraction(input_magnitude,
							 segment_fraction) +
					  input_magnitude * segment_count);
			if ((uint16_t)g_scaled_input_pitch >= 0x8000u) {
				desired_pitch = (int16_t)-desired_pitch;
			}
			if ((craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_FLIGHT_CONTROLS) == 0 ||
			    (craft->beam_effect_accum[1] != 0 &&
			     craft->chaff_active_seconds == 0)) {
				desired_yaw = 0;
				desired_pitch = 0;
			}

			key_mode = 0;
			if ((g_flight_key_mods & KEY_MODIFIER_MASK) ==
			    ROLL_CONTROL_MODIFIER) {
				key_mode = 1;
			}
			if (g_players[player_idx].yaw_roll_swap == key_mode) {
				smoothed_input = g_players[player_idx]
							 .smoothed_input_yaw;
				input_difference =
					(int16_t)((uint16_t)desired_yaw -
						  (uint16_t)smoothed_input);
				if (xvt_flight_timing_is_unlocked()) {
					g_players[player_idx]
						.smoothed_input_yaw =
						(int16_t)(smoothed_input +
							  xvt_player_timing_slew(
								  player_idx,
								  XVT_PLAYER_SLEW_YAW,
								  input_difference));
				} else if (input_difference != 0) {
					smoothing_step = input_difference;
					if (input_difference < 0) {
						smoothing_step =
							(int16_t)-input_difference;
					}
					if (smoothing_step <
					    CONTROL_SMOOTHING_THRESHOLD) {
						g_players[player_idx]
							.smoothed_input_yaw =
							(int16_t)(smoothed_input +
								  input_difference);
					} else {
						if (g_sim_steps_per_second >
						    CONTROL_SMOOTHING_BASE_STEP) {
							smoothing_step =
								(int16_t)(smoothing_step /
									  (int)g_sim_steps_per_second);
							if (smoothing_step ==
							    0) {
								smoothing_step =
									1;
							}
							smoothing_step *=
								CONTROL_SMOOTHING_BASE_STEP;
						}
						if (input_difference < 0) {
							g_players[player_idx]
								.smoothed_input_yaw -=
								smoothing_step;
						} else {
							g_players[player_idx]
								.smoothed_input_yaw +=
								smoothing_step;
						}
					}
				}
				smoothed_input = g_players[player_idx]
							 .smoothed_input_pitch;
				input_difference =
					(int16_t)((uint16_t)desired_pitch -
						  (uint16_t)smoothed_input);
				if (xvt_flight_timing_is_unlocked()) {
					g_players[player_idx]
						.smoothed_input_pitch =
						(int16_t)(smoothed_input +
							  xvt_player_timing_slew(
								  player_idx,
								  XVT_PLAYER_SLEW_PITCH,
								  input_difference));
				} else if (input_difference != 0) {
					smoothing_step = input_difference;
					if (input_difference < 0) {
						smoothing_step =
							(int16_t)-input_difference;
					}
					if (smoothing_step <
					    CONTROL_SMOOTHING_THRESHOLD) {
						g_players[player_idx]
							.smoothed_input_pitch =
							(int16_t)(smoothed_input +
								  input_difference);
					} else {
						if (g_sim_steps_per_second >
						    CONTROL_SMOOTHING_BASE_STEP) {
							smoothing_step =
								(int16_t)(smoothing_step /
									  (int)g_sim_steps_per_second);
							if (smoothing_step ==
							    0) {
								smoothing_step =
									1;
							}
							smoothing_step *=
								CONTROL_SMOOTHING_BASE_STEP;
						}
						if (input_difference < 0) {
							g_players[player_idx]
								.smoothed_input_pitch -=
								smoothing_step;
						} else {
							g_players[player_idx]
								.smoothed_input_pitch +=
								smoothing_step;
						}
					}
				}
			} else {
				g_players[player_idx].smoothed_input_yaw = 0;
				g_players[player_idx].smoothed_input_pitch = 0;
				XVT_LOG_DEBUG(
					"player.roll_mode slot=%d roll=%d predicted=%d",
					player_idx, (int)key_mode,
					g_flight_sim_side_effects_suppressed);
				xvt_player_timing_clear(player_idx,
							XVT_PLAYER_SLEW_YAW);
				xvt_player_timing_clear(player_idx,
							XVT_PLAYER_SLEW_PITCH);
				xvt_player_timing_clear(player_idx,
							XVT_PLAYER_YAW);
				xvt_player_timing_clear(player_idx,
							XVT_PLAYER_PITCH);
			}
			g_players[player_idx].yaw_roll_swap = (int16_t)key_mode;
			yaw_step =
				(int16_t)(xvt_flight_timing_is_unlocked()
						  ? xvt_player_timing_scale(
							    player_idx,
							    XVT_PLAYER_YAW,
							    g_players[player_idx]
								    .smoothed_input_yaw,
							    g_elapsed_ticks,
							    236)
						  : player_scale_control_step_by_elapsed_ticks(
							    g_players[player_idx]
								    .smoothed_input_yaw));
			pitch_step =
				(int16_t)(xvt_flight_timing_is_unlocked()
						  ? xvt_player_timing_scale(
							    player_idx,
							    XVT_PLAYER_PITCH,
							    g_players[player_idx]
								    .smoothed_input_pitch,
							    g_elapsed_ticks,
							    236)
						  : player_scale_control_step_by_elapsed_ticks(
							    g_players[player_idx]
								    .smoothed_input_pitch));
			independent_roll_step = xvt_flight_controls_roll_step(
				player_idx, roll_rate, key_mode ? yaw_step : 0);
			if ((craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_FLIGHT_CONTROLS) == 0 ||
			    (craft->beam_effect_accum[1] != 0 &&
			     craft->chaff_active_seconds == 0)) {
				yaw_step = 0;
				pitch_step = 0;
				independent_roll_step = 0;
			}
			if (key_mode != 0) {
				yaw_step = independent_roll_step;
				if (pitch_step != 0) {
					player_apply_pitch_yaw_steps(
						pitch_step, 0,
						(uint16_t)g_players[player_idx]
							.object_index,
						craft);
					g_object_table[g_players[player_idx]
							       .object_index]
						.mobj->orient_matrix_dirty = 1;
					g_object_table[g_players[player_idx]
							       .object_index]
						.mobj->move_vector_dirty =
						g_object_table
							[g_players[player_idx]
								 .object_index]
								.mobj
								->orient_matrix_dirty;
				}
				if (yaw_step != 0) {
					g_object_table[g_players[player_idx]
							       .object_index]
						.roll -= 2 * yaw_step;
					g_object_table[g_players[player_idx]
							       .object_index]
						.mobj->orient_matrix_dirty = 1;
					g_object_table[g_players[player_idx]
							       .object_index]
						.mobj->move_vector_dirty =
						g_object_table
							[g_players[player_idx]
								 .object_index]
								.mobj
								->orient_matrix_dirty;
				}
			} else if (pitch_step != 0 || yaw_step != 0) {
				player_apply_pitch_yaw_steps(
					pitch_step, (int16_t)-yaw_step,
					(uint16_t)g_players[player_idx]
						.object_index,
					craft);
				g_object_table[g_players[player_idx]
						       .object_index]
					.mobj->orient_matrix_dirty = 1;
				g_object_table[g_players[player_idx]
						       .object_index]
					.mobj->move_vector_dirty =
					g_object_table[g_players[player_idx]
							       .object_index]
						.mobj->orient_matrix_dirty;
				if (yaw_step != 0) {
					g_object_table[g_players[player_idx]
							       .object_index]
						.roll -= yaw_step;
				}
			}
			if (!key_mode && independent_roll_step) {
				g_object_table[g_players[player_idx]
						       .object_index]
					.roll -= 2 * independent_roll_step;
				mobile_object->orient_matrix_dirty = 1;
				mobile_object->move_vector_dirty = 1;
			}
		}
		return;
	}

	camera_state = g_players[player_idx].map_camera_state;
	if (camera_state != 0) {
		if ((camera_state & MAP_CAMERA_DIRECTION_BIT) != 0) {
			transition_magnitude =
				(int16_t)(camera_state & MAP_CAMERA_STATE_MASK);
			if (transition_magnitude < MAP_CAMERA_MAX_TRANSITION) {
				if (g_flight_sim_side_effects_suppressed == 0) {
					if (transition_magnitude +
						    (uint16_t)g_elapsed_ticks >
					    MAP_CAMERA_MAX_TRANSITION) {
						transition_magnitude =
							(int16_t)(MAP_CAMERA_MAX_TRANSITION -
								  g_elapsed_ticks);
					}
					transition_magnitude =
						(int16_t)(transition_magnitude +
							  g_elapsed_ticks);
					transition_magnitude |=
						MAP_CAMERA_DIRECTION_BIT;
					g_players[player_idx].map_camera_state =
						(uint8_t)transition_magnitude;
				}
			} else {
				if (g_players[player_idx]
					    .view_state.camera_focus_obj_idx !=
				    UINT16_MAX) {
					g_players[player_idx]
						.view_state.camera_world_x =
						g_object_table
							[g_players[player_idx]
								 .view_state
								 .camera_focus_obj_idx]
								.world_x;
					g_players[player_idx]
						.view_state.camera_world_y =
						g_object_table
							[g_players[player_idx]
								 .view_state
								 .camera_focus_obj_idx]
								.world_y;
					g_players[player_idx]
						.view_state.camera_world_z =
						g_object_table
							[g_players[player_idx]
								 .view_state
								 .camera_focus_obj_idx]
								.world_z;
					g_players[player_idx]
						.view_state.camera_world_z +=
						g_players[player_idx]
							.view_state
							.camera_distance;
					XVT_LOG_DEBUG(
						"player.map_overview slot=%d focus=%d height=%d predicted=%d",
						player_idx,
						(int)g_players[player_idx]
							.view_state
							.camera_focus_obj_idx,
						g_players[player_idx]
							.view_state
							.camera_world_z,
						g_flight_sim_side_effects_suppressed);
				}
				g_players[player_idx]
					.view_state.camera_focus_obj_idx =
					UINT16_MAX;
			}
		} else {
			transition_magnitude =
				(int16_t)(camera_state & MAP_CAMERA_STATE_MASK);
			if (transition_magnitude > 1 &&
			    g_flight_sim_side_effects_suppressed == 0) {
				if (g_elapsed_ticks >= transition_magnitude) {
					transition_magnitude =
						(int16_t)(g_elapsed_ticks + 1);
				}
				transition_magnitude =
					(int16_t)(transition_magnitude -
						  g_elapsed_ticks);
				g_players[player_idx].map_camera_state =
					(uint8_t)transition_magnitude;
			}
		}
		if (g_players[player_idx].map_camera_state != 0) {
			absolute_yaw = g_scaled_input_yaw;
			absolute_pitch = g_scaled_input_pitch;
			if (absolute_yaw < 0) {
				absolute_yaw = (int16_t)-absolute_yaw;
			}
			if (absolute_pitch < 0) {
				absolute_pitch = (int16_t)-absolute_pitch;
			}
			if (absolute_yaw < MAP_CAMERA_YAW_DEADZONE) {
				g_scaled_input_yaw = 0;
			}
			if (absolute_pitch < MAP_CAMERA_PITCH_DEADZONE) {
				g_scaled_input_pitch = 0;
			}
		}
	}

	yaw_step =
		(int16_t)(xvt_flight_timing_is_unlocked()
				  ? xvt_player_timing_scale(
					    player_idx, XVT_PLAYER_CAMERA_YAW,
					    g_scaled_input_yaw, g_elapsed_ticks,
					    236)
				  : player_scale_control_step_by_elapsed_ticks(
					    g_scaled_input_yaw));
	pitch_step =
		(int16_t)(xvt_flight_timing_is_unlocked()
				  ? xvt_player_timing_scale(
					    player_idx, XVT_PLAYER_CAMERA_PITCH,
					    g_scaled_input_pitch,
					    g_elapsed_ticks, 236)
				  : player_scale_control_step_by_elapsed_ticks(
					    g_scaled_input_pitch));
	if ((g_players[player_idx].map_camera_state &
	     MAP_CAMERA_DIRECTION_BIT) != 0) {
		g_players[player_idx].view_state.camera_world_x +=
			yaw_step *
			((g_players[player_idx].view_state.camera_world_z >>
			  CAMERA_DISTANCE_COORDINATE_SHIFT) +
			 1);
		g_players[player_idx].view_state.camera_world_y +=
			pitch_step *
			((g_players[player_idx].view_state.camera_world_z >>
			  CAMERA_DISTANCE_COORDINATE_SHIFT) +
			 1);
	} else {
		if (g_players[player_idx].view_state.camera_focus_obj_idx !=
		    UINT16_MAX) {
			g_players[player_idx].view_state.hud_aim_y += yaw_step;
			g_players[player_idx].view_state.hud_aim_x +=
				pitch_step;
		} else {
			if (pitch_step != 0 || yaw_step != 0) {
				flight_view_rotate_view_by_input(
					pitch_step, -yaw_step, player_idx);
			}
		}
	}

	camera_key_mode = g_flight_key_mods & 0xF;
	previous_distance_step =
		g_players[player_idx].view_state.camera_distance_step;
	if (camera_key_mode != 1 && camera_key_mode != 2) {
		if (g_players[player_idx].map_camera_state != 0) {
			uint16_t current_distance_step =
				g_players[player_idx]
					.view_state.camera_distance_step;
			if (current_distance_step > 0x100u) {
				uint16_t decayed_distance_step =
					current_distance_step;
				current_distance_step >>=
					CAMERA_DISTANCE_DECAY_SHIFT;
				decayed_distance_step -= current_distance_step;
				decayed_distance_step -=
					CAMERA_DISTANCE_DEFAULT_STEP;

				g_players[player_idx]
					.view_state.camera_distance_step =
					xvt_flight_timing_is_unlocked()
						? (int16_t)(previous_distance_step +
							    xvt_player_timing_scale(
								    player_idx,
								    XVT_PLAYER_ZOOM,
								    (int)decayed_distance_step -
									    (uint16_t)
										    previous_distance_step,
								    g_elapsed_ticks,
								    8))
						: (int16_t)
							  decayed_distance_step;

			} else {
				g_players[player_idx]
					.view_state.camera_distance_step =
					CAMERA_DISTANCE_DEFAULT_STEP;
			}
		} else {
			g_players[player_idx].view_state.camera_distance_step =
				CAMERA_DISTANCE_DEFAULT_STEP;
		}
		return;
	}

	if (g_players[player_idx].map_camera_state > 1) {
		distance = g_players[player_idx].view_state.camera_world_z;
		camera_distance_step =
			&g_players[player_idx].view_state.camera_distance_step;
		if ((distance & ~1) > CAMERA_DISTANCE_LARGE_THRESHOLD) {
			*camera_distance_step = CAMERA_DISTANCE_LARGE_LIMIT;
		} else {
			*camera_distance_step = (int16_t)(distance >> 1);
		}
		if ((uint16_t)*camera_distance_step <
		    CAMERA_DISTANCE_MAP_MIN_STEP) {
			*camera_distance_step = CAMERA_DISTANCE_MAP_MIN_STEP;
		}
	} else if (g_players[player_idx].map_camera_state == 1) {
		if (g_players[player_idx].view_state.camera_focus_obj_idx !=
		    UINT16_MAX) {
			distance = g_players[player_idx]
					   .view_state.camera_distance;
			camera_distance_step =
				&g_players[player_idx]
					 .view_state.camera_distance_step;
			if ((distance & ~1) > CAMERA_DISTANCE_LARGE_THRESHOLD) {
				*camera_distance_step =
					CAMERA_DISTANCE_LARGE_LIMIT;
			} else {
				*camera_distance_step =
					(int16_t)(distance >> 1);
			}
			if ((uint16_t)*camera_distance_step <
			    CAMERA_DISTANCE_FOCUS_MIN_STEP) {
				*camera_distance_step =
					CAMERA_DISTANCE_FOCUS_MIN_STEP;
			}
		} else if (g_players[player_idx].view_state.aim_target_idx !=
			   UINT16_MAX) {
			target_object =
				&g_object_table[g_players[player_idx]
							.view_state
							.aim_target_idx];
			camera_distance_step =
				&g_players[player_idx]
					 .view_state.camera_distance_step;
			distance = collide_roughdistance3d(
				target_object->world_x -
					g_players[player_idx]
						.view_state.camera_world_x,
				target_object->world_y -
					g_players[player_idx]
						.view_state.camera_world_y,
				target_object->world_z -
					g_players[player_idx]
						.view_state.camera_world_z);
			if (distance <= CAMERA_DISTANCE_LARGE_LIMIT) {
				*camera_distance_step = (int16_t)distance;
			} else {
				*camera_distance_step =
					CAMERA_DISTANCE_LARGE_LIMIT;
			}
			if ((uint16_t)*camera_distance_step <
			    CAMERA_DISTANCE_FREE_MIN_STEP) {
				*camera_distance_step =
					CAMERA_DISTANCE_FREE_MIN_STEP;
				xvt_player_timing_clear(player_idx,
							XVT_PLAYER_DISTANCE);
				xvt_player_timing_clear(player_idx,
							XVT_PLAYER_ZOOM);
			}
		} else {
			uint16_t current_distance_step;
			camera_distance_step =
				&g_players[player_idx]
					 .view_state.camera_distance_step;
			current_distance_step =
				(uint16_t)(g_players[player_idx]
						   .view_state
						   .camera_distance_step +
					   CAMERA_DISTANCE_DEFAULT_STEP);
			*camera_distance_step = (int16_t)current_distance_step;

			*camera_distance_step =
				xvt_flight_timing_is_unlocked()
					? (int16_t)(previous_distance_step +
						    xvt_player_timing_scale(
							    player_idx,
							    XVT_PLAYER_ZOOM,
							    (current_distance_step +
							     (current_distance_step >>
							      3)) - previous_distance_step,
							    g_elapsed_ticks, 8))
					: (int16_t)(current_distance_step +
						    (current_distance_step >>
						     3));

			if ((uint16_t)*camera_distance_step >
			    CAMERA_DISTANCE_FREE_MAX_STEP) {
				*camera_distance_step =
					CAMERA_DISTANCE_FREE_MAX_STEP;
				xvt_player_timing_clear(player_idx,
							XVT_PLAYER_DISTANCE);
				xvt_player_timing_clear(player_idx,
							XVT_PLAYER_ZOOM);
			}
		}
	} else {
		camera_distance_step =
			&g_players[player_idx].view_state.camera_distance_step;

		*camera_distance_step =
			xvt_flight_timing_is_unlocked()
				? (int16_t)(previous_distance_step +
					    xvt_player_timing_scale(
						    player_idx, XVT_PLAYER_ZOOM,
						    (previous_distance_step +
						     CAMERA_DISTANCE_DEFAULT_STEP) -
							    previous_distance_step,
						    g_elapsed_ticks, 8))
				: (int16_t)(previous_distance_step +
					    CAMERA_DISTANCE_DEFAULT_STEP);

		if ((uint16_t)*camera_distance_step > 0x400u) {
			*camera_distance_step = CAMERA_DISTANCE_FREE_MIN_STEP;
			xvt_player_timing_clear(player_idx,
						XVT_PLAYER_DISTANCE);
			xvt_player_timing_clear(player_idx, XVT_PLAYER_ZOOM);
		}
	}

	modern_distance_step =
		xvt_flight_timing_is_unlocked()
			? (int16_t)xvt_player_timing_scale(
				  player_idx, XVT_PLAYER_DISTANCE,
				  *camera_distance_step, g_elapsed_ticks, 236)
			: 0;
	if (camera_key_mode == 1) {
		if (g_players[player_idx].map_camera_state == 0) {
			g_players[player_idx].view_state.camera_distance -=
				(int16_t)(xvt_flight_timing_is_unlocked()
						  ? modern_distance_step
						  : player_scale_control_step_by_elapsed_ticks(
							    *camera_distance_step));
			if (g_players[player_idx].view_state.camera_distance <
			    CAMERA_MINIMUM) {
				g_players[player_idx]
					.view_state.camera_distance =
					CAMERA_MINIMUM;
				xvt_player_timing_clear(player_idx,
							XVT_PLAYER_DISTANCE);
				xvt_player_timing_clear(player_idx,
							XVT_PLAYER_ZOOM);
			}
			return;
		}
		if (g_players[player_idx].view_state.camera_focus_obj_idx !=
		    UINT16_MAX) {
			g_players[player_idx].view_state.camera_distance -=
				(int16_t)(xvt_flight_timing_is_unlocked()
						  ? modern_distance_step
						  : player_scale_control_step_by_elapsed_ticks(
							    *camera_distance_step));
			target_clearance =
				g_object_type_table
					[g_object_table
						 [g_players[player_idx]
							  .view_state
							  .camera_focus_obj_idx]
							 .object_type]
						.max_bounds_extent +
				CAMERA_CLEARANCE;
			if (g_players[player_idx].view_state.camera_distance <
			    target_clearance) {
				g_players[player_idx]
					.view_state.camera_distance =
					target_clearance;
				xvt_player_timing_clear(player_idx,
							XVT_PLAYER_DISTANCE);
				xvt_player_timing_clear(player_idx,
							XVT_PLAYER_ZOOM);
			}
			return;
		}
		if (g_players[player_idx].view_state.aim_target_idx !=
		    UINT16_MAX) {
			fview_build_camera_orient(
				0, g_players[player_idx].view_state.view_pitch,
				g_players[player_idx].view_state.view_yaw, 0, 0,
				0, NULL);
		} else {
			fview_build_camera_orient(
				0, g_players[player_idx].view_state.view_pitch,
				g_players[player_idx].view_state.view_yaw, 0,
				g_players[player_idx].view_state.hud_aim_x,
				g_players[player_idx].view_state.hud_aim_y,
				NULL);
		}
		camera_scale_x = g_cam_mat_r2_x;
		camera_movement_x =
			(int16_t)(xvt_flight_timing_is_unlocked()
					  ? modern_distance_step
					  : player_scale_control_step_by_elapsed_ticks(
						    *camera_distance_step));
		camera_movement_x =
			math_mul_q15(camera_movement_x, camera_scale_x);
		g_players[player_idx].view_state.camera_world_x +=
			camera_movement_x;
		camera_scale_y = g_cam_mat_r2_y;
		camera_movement_y =
			(int16_t)(xvt_flight_timing_is_unlocked()
					  ? modern_distance_step
					  : player_scale_control_step_by_elapsed_ticks(
						    *camera_distance_step));
		camera_movement_y =
			math_mul_q15(camera_movement_y, camera_scale_y);
		g_players[player_idx].view_state.camera_world_y +=
			camera_movement_y;
		camera_scale_z = g_cam_mat_r2_z;
		camera_movement_z =
			(int16_t)(xvt_flight_timing_is_unlocked()
					  ? modern_distance_step
					  : player_scale_control_step_by_elapsed_ticks(
						    *camera_distance_step));
		camera_movement_z =
			math_mul_q15(camera_movement_z, camera_scale_z);
		g_players[player_idx].view_state.camera_world_z +=
			camera_movement_z;
		if (g_players[player_idx].view_state.aim_target_idx !=
		    UINT16_MAX) {
			target_object =
				&g_object_table[g_players[player_idx]
							.view_state
							.aim_target_idx];
			distance = collide_roughdistance3d(
				target_object->world_x -
					g_players[player_idx]
						.view_state.camera_world_x,
				target_object->world_y -
					g_players[player_idx]
						.view_state.camera_world_y,
				target_object->world_z -
					g_players[player_idx]
						.view_state.camera_world_z);
			target_clearance =
				g_object_type_table
					[g_object_table[g_players[player_idx]
								.view_state
								.aim_target_idx]
						 .object_type]
						.max_bounds_extent +
				CAMERA_CLEARANCE;
			if (target_clearance > distance) {
				target_clearance =
					g_object_type_table
						[g_object_table
							 [g_players[player_idx]
								  .view_state
								  .camera_focus_obj_idx]
								 .object_type]
							.max_bounds_extent +
					CAMERA_CLEARANCE;
				movement = distance - target_clearance;
				clearance_scale_x = g_cam_mat_r2_x;
				clearance_movement_x = math_mul_q15(
					movement, clearance_scale_x);
				g_players[player_idx]
					.view_state.camera_world_x +=
					clearance_movement_x;
				clearance_scale_y = g_cam_mat_r2_y;
				clearance_movement_y = math_mul_q15(
					movement, clearance_scale_y);
				g_players[player_idx]
					.view_state.camera_world_y +=
					clearance_movement_y;
				clearance_scale_z = g_cam_mat_r2_z;
				clearance_movement_z = math_mul_q15(
					movement, clearance_scale_z);
				g_players[player_idx]
					.view_state.camera_world_z +=
					clearance_movement_z;
			}
		}
		if (g_players[player_idx].map_camera_state > 1 &&
		    g_players[player_idx].view_state.camera_world_z <
			    CAMERA_MINIMUM) {
			g_players[player_idx].view_state.camera_world_z =
				CAMERA_MINIMUM;
		}
		if (g_players[player_idx].view_state.camera_world_x <
		    -CAMERA_WORLD_LIMIT) {
			g_players[player_idx].view_state.camera_world_x =
				-CAMERA_WORLD_LIMIT;
		}
		if (g_players[player_idx].view_state.camera_world_x >
		    CAMERA_WORLD_LIMIT) {
			g_players[player_idx].view_state.camera_world_x =
				CAMERA_WORLD_LIMIT;
		}
		if (g_players[player_idx].view_state.camera_world_y <
		    -CAMERA_WORLD_LIMIT) {
			g_players[player_idx].view_state.camera_world_y =
				-CAMERA_WORLD_LIMIT;
		}
		if (g_players[player_idx].view_state.camera_world_y >
		    CAMERA_WORLD_LIMIT) {
			g_players[player_idx].view_state.camera_world_y =
				CAMERA_WORLD_LIMIT;
		}
		if (g_players[player_idx].view_state.camera_world_z <
		    -CAMERA_WORLD_LIMIT) {
			g_players[player_idx].view_state.camera_world_z =
				-CAMERA_WORLD_LIMIT;
		}
		if (g_players[player_idx].view_state.camera_world_z >
		    CAMERA_WORLD_LIMIT) {
			g_players[player_idx].view_state.camera_world_z =
				CAMERA_WORLD_LIMIT;
		}
	} else {
		if (g_players[player_idx].map_camera_state == 0) {
			g_players[player_idx].view_state.camera_distance +=
				(int16_t)(xvt_flight_timing_is_unlocked()
						  ? modern_distance_step
						  : player_scale_control_step_by_elapsed_ticks(
							    *camera_distance_step));
			if (g_players[player_idx].view_state.camera_distance >
			    CAMERA_DISTANCE_NORMAL_MAX) {
				g_players[player_idx]
					.view_state.camera_distance =
					CAMERA_DISTANCE_NORMAL_MAX;
			}
			return;
		}
		if (g_players[player_idx].view_state.camera_focus_obj_idx !=
		    UINT16_MAX) {
			g_players[player_idx].view_state.camera_distance +=
				(int16_t)(xvt_flight_timing_is_unlocked()
						  ? modern_distance_step
						  : player_scale_control_step_by_elapsed_ticks(
							    *camera_distance_step));
			return;
		}
		if (g_players[player_idx].view_state.aim_target_idx !=
		    UINT16_MAX) {
			fview_build_camera_orient(
				0, g_players[player_idx].view_state.view_pitch,
				g_players[player_idx].view_state.view_yaw, 0, 0,
				0, NULL);
		} else {
			fview_build_camera_orient(
				0, g_players[player_idx].view_state.view_pitch,
				g_players[player_idx].view_state.view_yaw, 0,
				g_players[player_idx].view_state.hud_aim_x,
				g_players[player_idx].view_state.hud_aim_y,
				NULL);
		}
		reverse_scale_x = g_cam_mat_r2_x;
		reverse_movement_x =
			(int16_t)(xvt_flight_timing_is_unlocked()
					  ? modern_distance_step
					  : player_scale_control_step_by_elapsed_ticks(
						    *camera_distance_step));
		reverse_movement_x =
			math_mul_q15(reverse_movement_x, reverse_scale_x);
		g_players[player_idx].view_state.camera_world_x -=
			reverse_movement_x;
		reverse_scale_y = g_cam_mat_r2_y;
		reverse_movement_y =
			(int16_t)(xvt_flight_timing_is_unlocked()
					  ? modern_distance_step
					  : player_scale_control_step_by_elapsed_ticks(
						    *camera_distance_step));
		reverse_movement_y =
			math_mul_q15(reverse_movement_y, reverse_scale_y);
		g_players[player_idx].view_state.camera_world_y -=
			reverse_movement_y;
		reverse_scale_z = g_cam_mat_r2_z;
		reverse_movement_z =
			(int16_t)(xvt_flight_timing_is_unlocked()
					  ? modern_distance_step
					  : player_scale_control_step_by_elapsed_ticks(
						    *camera_distance_step));
		reverse_movement_z =
			math_mul_q15(reverse_movement_z, reverse_scale_z);
		g_players[player_idx].view_state.camera_world_z -=
			reverse_movement_z;
		if (g_players[player_idx].view_state.camera_world_x <
		    -CAMERA_WORLD_LIMIT) {
			g_players[player_idx].view_state.camera_world_x =
				-CAMERA_WORLD_LIMIT;
		}
		if (g_players[player_idx].view_state.camera_world_x >
		    CAMERA_WORLD_LIMIT) {
			g_players[player_idx].view_state.camera_world_x =
				CAMERA_WORLD_LIMIT;
		}
		if (g_players[player_idx].view_state.camera_world_y <
		    -CAMERA_WORLD_LIMIT) {
			g_players[player_idx].view_state.camera_world_y =
				-CAMERA_WORLD_LIMIT;
		}
		if (g_players[player_idx].view_state.camera_world_y >
		    CAMERA_WORLD_LIMIT) {
			g_players[player_idx].view_state.camera_world_y =
				CAMERA_WORLD_LIMIT;
		}
		if (g_players[player_idx].view_state.camera_world_z <
		    -CAMERA_WORLD_LIMIT) {
			g_players[player_idx].view_state.camera_world_z =
				-CAMERA_WORLD_LIMIT;
		}
		if (g_players[player_idx].view_state.camera_world_z >
		    CAMERA_WORLD_LIMIT) {
			g_players[player_idx].view_state.camera_world_z =
				CAMERA_WORLD_LIMIT;
		}
	}
}

/* Edits and sends the player's chat line for g_current_action_key: 8 deletes a
 * character, 9 cycles the recipients (team, enemy, all), 13 sends msg_text with
 * IFMSG_374 to every participating player the recipients admit (team: the same
 * team; enemy: another team not allied with the sender's), then IFMSG_378; 27
 * cancels with IFMSG_379; 155 to 158 send taunts 0 to 3 from g_player_taunt_text
 * the same way. Any other key adds a character while the line holds fewer than
 * 48. Key 0 only redraws the line, for the local player while
 * flight_group_message_pane_timer is below SIMULATION_TICKS_PER_SECOND. Sending or
 * cancelling sets chat_recipient_mode back to inactive; sending sets
 * g_msg_sender_iff to 3. */
// FUNCTION: XVT 0x481420
void flight_chat_handle_input(int player_idx)
{
	struct player_data *player;
	uint8_t message_length;
	int recipient_index;
	struct player_data *recipient;
	int should_send;
	flight_chat_recipient_mode recipient_mode;
	const char *taunt_text;

	switch (g_current_action_key) {
	case 8:
		player = &g_players[player_idx];
		message_length = player->msg_length;
		if (message_length != 0) {
			--message_length;
			player->msg_length = message_length;
			player->msg_text[message_length] = '_';
			player->msg_text[message_length + 1] = '\0';
			XVT_LOG_DEBUG(
				"player.chat_edited slot=%d edit=\"delete\" length=%d recipients=%d predicted=%d",
				player_idx, (int)player->msg_length,
				(int)player->chat_recipient_mode,
				g_flight_sim_side_effects_suppressed);
		}
		msg_add_message_ptr(0, player->msg_text);
		msg_emit_in_flight_message(
			(in_flight_message_id)((uint8_t)player
						       ->chat_recipient_mode +
					       IFMSG_374_FROM_ARG_ARG),
			player_idx);
		return;

	case 9:
		player = &g_players[player_idx];
		++player->chat_recipient_mode;
		if ((uint8_t)player->chat_recipient_mode >
		    FLIGHT_CHAT_RECIPIENT_ALL) {
			player->chat_recipient_mode =
				FLIGHT_CHAT_RECIPIENT_TEAM;
		}
		XVT_LOG_DEBUG(
			"player.chat_edited slot=%d edit=\"recipients\" length=%d recipients=%d predicted=%d",
			player_idx, (int)player->msg_length,
			(int)player->chat_recipient_mode,
			g_flight_sim_side_effects_suppressed);
		msg_add_message_ptr(0, player->msg_text);
		msg_emit_in_flight_message(
			(in_flight_message_id)((uint8_t)player
						       ->chat_recipient_mode +
					       IFMSG_374_FROM_ARG_ARG),
			player_idx);
		return;

	case 13:
		player = &g_players[player_idx];
		player->msg_text[player->msg_length] = '\0';
		msg_add_message_ptr(0, net_session_get_player_name(player_idx));
		msg_add_message_ptr(1, player->msg_text);
		for (recipient_index = 0; recipient_index < 8;
		     ++recipient_index) {
			recipient = &g_players[recipient_index];
			if (recipient->participation_state != 0) {
				g_msg_sender_iff = 3;
				recipient_mode = player->chat_recipient_mode;
				should_send = 0;
				if (recipient_mode ==
				    FLIGHT_CHAT_RECIPIENT_TEAM) {
					if (player->team == recipient->team) {
						should_send = 1;
					}
				} else if (recipient_mode ==
					   FLIGHT_CHAT_RECIPIENT_ENEMY) {
					if (recipient->team != player->team &&
					    g_mission_teams[(uint16_t)recipient
								    ->team]
							    .allies[(uint16_t)player
									    ->team] ==
						    0) {
						should_send = 1;
					}
				} else {
					should_send = 1;
				}
				if (should_send != 0) {
					msg_emit_in_flight_message(
						IFMSG_374_FROM_ARG_ARG,
						recipient_index);
				}
			}
		}
		XVT_LOG_DEBUG(
			"player.chat_sent slot=%d recipients=%d length=%d predicted=%d",
			player_idx, (int)player->chat_recipient_mode,
			(int)player->msg_length,
			g_flight_sim_side_effects_suppressed);
		player->chat_recipient_mode = FLIGHT_CHAT_RECIPIENT_INACTIVE;
		msg_emit_in_flight_message(IFMSG_378_MESSAGE_SENT, player_idx);
		return;

	case 27:
		player = &g_players[player_idx];
		player->chat_recipient_mode = FLIGHT_CHAT_RECIPIENT_INACTIVE;
		XVT_LOG_DEBUG(
			"player.chat_edited slot=%d edit=\"cancelled\" length=%d recipients=%d predicted=%d",
			player_idx, (int)player->msg_length,
			(int)player->chat_recipient_mode,
			g_flight_sim_side_effects_suppressed);
		msg_emit_in_flight_message(IFMSG_379_MESSAGE_ABORTED,
					   player_idx);
		return;

	case 155:
	case 156:
	case 157:
	case 158:
		player = &g_players[player_idx];
		player->msg_text[player->msg_length] = '\0';
		msg_add_message_ptr(0, net_session_get_player_name(player_idx));
		taunt_text = g_player_taunt_text[player_idx]
						[g_current_action_key - 155];
		msg_add_message_ptr(1, taunt_text);
		for (recipient_index = 0; recipient_index < 8;
		     ++recipient_index) {
			recipient = &g_players[recipient_index];
			if (recipient->participation_state != 0) {
				g_msg_sender_iff = 3;
				recipient_mode = player->chat_recipient_mode;
				should_send = 0;
				if (recipient_mode ==
				    FLIGHT_CHAT_RECIPIENT_TEAM) {
					if (player->team == recipient->team) {
						should_send = 1;
					}
				} else if (recipient_mode ==
					   FLIGHT_CHAT_RECIPIENT_ENEMY) {
					if (recipient->team != player->team &&
					    g_mission_teams[(uint16_t)recipient
								    ->team]
							    .allies[(uint16_t)player
									    ->team] ==
						    0) {
						should_send = 1;
					}
				} else {
					should_send = 1;
				}
				if (should_send != 0) {
					msg_emit_in_flight_message(
						IFMSG_374_FROM_ARG_ARG,
						recipient_index);
				}
			}
		}
		XVT_LOG_DEBUG(
			"player.taunt_sent slot=%d taunt=%d recipients=%d predicted=%d",
			player_idx, (int)g_current_action_key - 155,
			(int)player->chat_recipient_mode,
			g_flight_sim_side_effects_suppressed);
		player->chat_recipient_mode = FLIGHT_CHAT_RECIPIENT_INACTIVE;
		msg_emit_in_flight_message(IFMSG_378_MESSAGE_SENT, player_idx);
		return;

	default:
		player = &g_players[player_idx];
		if (g_current_action_key != 0) {
			message_length = player->msg_length;
			if (message_length < 48) {
				player->msg_text[message_length] =
					(char)g_current_action_key;
				player->msg_text[message_length + 1] = '_';
				player->msg_text[message_length + 2] = '\0';
				++player->msg_length;
				XVT_LOG_DEBUG(
					"player.chat_edited slot=%d edit=\"typed\" length=%d recipients=%d predicted=%d",
					player_idx, (int)player->msg_length,
					(int)player->chat_recipient_mode,
					g_flight_sim_side_effects_suppressed);
			}
			msg_add_message_ptr(0, player->msg_text);
			msg_emit_in_flight_message(
				(in_flight_message_id)((uint8_t)player
							       ->chat_recipient_mode +
						       IFMSG_374_FROM_ARG_ARG),
				player_idx);
		} else if (player_idx == g_local_player &&
			   (int16_t)g_player_flight_transient_timers[player_idx]
					   .flight_group_message_pane_timer <
				   SIMULATION_TICKS_PER_SECOND) {
			msg_add_message_ptr(0, player->msg_text);
			msg_emit_in_flight_message(
				(in_flight_message_id)((uint8_t)player
							       ->chat_recipient_mode +
						       IFMSG_374_FROM_ARG_ARG),
				player_idx);
		}
		return;
	}
}

/* Returns the nearest object, of a flight group not on the player's team, that
 * is an objective of kind goal_type for that team, or -1 when none. A craft
 * counts when active or arriving, with a pending flight group goal of that kind
 * enabled for the team with points of 0 or more, or matching one of the team's
 * global goal triggers for that kind; craft msg_build_target_description marks
 * actionable come first. Static objects count, as actionable, with such a
 * pending flight group goal. Skips empty slots, the player's own craft,
 * explosions and objects with an active decoy beam. Writes trig2's polar
 * results. */
// FUNCTION: XVT 0x481940
int16_t player_find_nearest_objective(int goal_type, int player_idx)
{
	enum {
		FLIGHT_GROUP_GOAL_COUNT = 8,
		GOAL_STATE_PENDING = 4,
	};

	unsigned int object_idx;

	unsigned int player_team = (uint16_t)g_players[player_idx].team;
	uint16_t best_actionable_object = UINT16_MAX;
	uint16_t best_objective_object = UINT16_MAX;
	unsigned int best_actionable_range = UINT_MAX;
	unsigned int best_objective_range = UINT_MAX;

	for (object_idx = g_active_region_object_slot_start;
	     object_idx < (unsigned int)g_active_region_craft_object_slot_end;
	     ++object_idx) {
		if (g_object_table[object_idx].object_type == 0 ||
		    object_idx ==
			    (unsigned int)g_players[player_idx].object_index ||
		    g_object_table[object_idx].genus_id ==
			    CRAFT_GENUS_EXPLOSION ||
		    object_has_active_decoy_beam(object_idx) != 0) {
			continue;
		}
		int flight_group_idx =
			g_object_table[object_idx].flight_group_idx;
		if (g_mission_flight_groups[flight_group_idx].fg.team ==
		    player_team) {
			continue;
		}
		unsigned int goal_index = 0;
		struct flight_group_goal *goals =
			g_mission_flight_groups[flight_group_idx].fg.goals;
		int objective = 0;
		uint8_t *enabled_team_goals =
			&goals[0].enabled_teams[player_team];
		for (; goal_index < FLIGHT_GROUP_GOAL_COUNT; ++goal_index) {
			struct flight_group_goal *goal = &goals[goal_index];
			if (enabled_team_goals[goal_index * sizeof(*goal)] !=
				    0 &&
			    goal->goal_kind == (uint8_t)goal_type &&
			    goal->points >= 0 &&
			    g_mission_fg_stats[g_object_table[object_idx]
						       .flight_group_idx]
					    .goal_state
						    [FLIGHT_GROUP_GOAL_COUNT *
							     player_team +
						     goal_index] ==
				    GOAL_STATE_PENDING) {
				objective = 1;
			}
		}
		int trigger_condition =
			g_mission_global_goals[player_team][goal_type]
				.trigger_pairs[0]
				.triggers[0]
				.condition;
		if (trigger_condition != MISSION_COND_NEVER &&
		    trigger_condition != MISSION_COND_ALWAYS_TRUE &&
		    mission_object_matches_trigger_variable(
			    object_idx,
			    g_mission_global_goals[player_team][goal_type]
				    .trigger_pairs[0]
				    .triggers[0]
				    .variable_type,
			    g_mission_global_goals[player_team][goal_type]
				    .trigger_pairs[0]
				    .triggers[0]
				    .variable) != 0) {
			objective = 1;
		}
		trigger_condition =
			g_mission_global_goals[player_team][goal_type]
				.trigger_pairs[0]
				.triggers[1]
				.condition;
		if (trigger_condition != MISSION_COND_NEVER &&
		    trigger_condition != MISSION_COND_ALWAYS_TRUE &&
		    mission_object_matches_trigger_variable(
			    object_idx,
			    g_mission_global_goals[player_team][goal_type]
				    .trigger_pairs[0]
				    .triggers[1]
				    .variable_type,
			    g_mission_global_goals[player_team][goal_type]
				    .trigger_pairs[0]
				    .triggers[1]
				    .variable) != 0) {
			objective = 1;
		}
		/* The original reads the second pair's condition but the first pair's variable. */
		trigger_condition =
			g_mission_global_goals[player_team][goal_type]
				.trigger_pairs[1]
				.triggers[0]
				.condition;
		if (trigger_condition != MISSION_COND_NEVER &&
		    trigger_condition != MISSION_COND_ALWAYS_TRUE &&
		    mission_object_matches_trigger_variable(
			    object_idx,
			    g_mission_global_goals[player_team][goal_type]
				    .trigger_pairs[0]
				    .triggers[0]
				    .variable_type,
			    g_mission_global_goals[player_team][goal_type]
				    .trigger_pairs[0]
				    .triggers[0]
				    .variable) != 0) {
			objective = 1;
		}
		trigger_condition =
			g_mission_global_goals[player_team][goal_type]
				.trigger_pairs[1]
				.triggers[1]
				.condition;
		if (trigger_condition != MISSION_COND_NEVER &&
		    trigger_condition != MISSION_COND_ALWAYS_TRUE &&
		    mission_object_matches_trigger_variable(
			    object_idx,
			    g_mission_global_goals[player_team][goal_type]
				    .trigger_pairs[1]
				    .triggers[1]
				    .variable_type,
			    g_mission_global_goals[player_team][goal_type]
				    .trigger_pairs[1]
				    .triggers[1]
				    .variable) != 0) {
			objective = 1;
		}
		if (objective != 0 &&
		    (g_object_table[object_idx].mobj->p_craft->object_kind ==
			     CRAFT_OBJECT_KIND_ACTIVE ||
		     g_object_table[object_idx].mobj->p_craft->object_kind ==
			     CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE)) {
			player_compute_polar_to_object_ref(player_idx,
							   object_idx);
			int actionable = msg_build_target_description(
				object_idx, player_idx, 0, 1);
			if (actionable != 0) {
				if (best_actionable_range >
				    (unsigned int)trig2_polardistance) {
					best_actionable_range = (unsigned int)
						trig2_polardistance;
					best_actionable_object = object_idx;
				}
			} else if (best_objective_range >
				   (unsigned int)trig2_polardistance) {
				best_objective_range =
					(unsigned int)trig2_polardistance;
				best_objective_object = object_idx;
			}
		}
	}

	for (object_idx = g_region_main_object_slot_end;
	     object_idx < (unsigned int)(g_region_main_object_slot_end +
					 g_region_static_object_slot_count);
	     ++object_idx) {
		if (g_object_table[object_idx].object_type == 0) {
			continue;
		}
		int flight_group_idx =
			g_object_table[object_idx].flight_group_idx;
		if (g_mission_flight_groups[flight_group_idx].fg.team ==
		    g_players[player_idx].team) {
			continue;
		}
		unsigned int goal_index = 0;
		struct flight_group_goal *goals =
			g_mission_flight_groups[flight_group_idx].fg.goals;
		uint8_t *enabled_team_goals =
			&goals[0].enabled_teams[player_team];
		for (; goal_index < FLIGHT_GROUP_GOAL_COUNT; ++goal_index) {
			struct flight_group_goal *goal = &goals[goal_index];
			if (enabled_team_goals[goal_index * sizeof(*goal)] !=
				    0 &&
			    goal->goal_kind == (uint8_t)goal_type &&
			    goal->points >= 0 &&
			    g_mission_fg_stats[flight_group_idx].goal_state
					    [FLIGHT_GROUP_GOAL_COUNT *
						     player_team +
					     goal_index] ==
				    GOAL_STATE_PENDING) {
				player_compute_polar_to_object_ref(player_idx,
								   object_idx);
				if (best_actionable_range >
				    (unsigned int)trig2_polardistance) {
					best_actionable_range = (unsigned int)
						trig2_polardistance;
					best_actionable_object = object_idx;
				}
			}
		}
	}
	int16_t selected_object = (int16_t)best_actionable_object;
	if (best_actionable_object == UINT16_MAX) {
		selected_object = (int16_t)best_objective_object;
	}
	return selected_object;
}

/* Returns step * g_elapsed_ticks / SIMULATION_TICKS_PER_SECOND: a rate per
 * SIMULATION_TICKS_PER_SECOND ticks turned into this update's share. */
// FUNCTION: XVT 0x481D70
int player_scale_control_step_by_elapsed_ticks(int16_t step)
{
	return math2_ab_over_c32(step, g_elapsed_ticks,
				 SIMULATION_TICKS_PER_SECOND);
}

/* Moves shield energy from bank src_bank to bank dst_bank of the player's craft,
 * as much as dst_bank lacks of craft_get_object_max_shield; nothing when src_bank is
 * empty or dst_bank full. Checks neither the banks nor that the player has a
 * craft. */
// FUNCTION: XVT 0x481EA0
void player_transfer_shield_bank_energy(uint16_t dst_bank, uint16_t src_bank,
					int player_idx)
{
	struct player_data *player = &g_players[player_idx];
	if (g_object_table[player->object_index]
		    .mobj->p_craft->shield_energy[src_bank] > 0) {
		int16_t object_max_shield =
			(int16_t)craft_get_object_max_shield(
				player->object_index);
		struct craft_data *craft =
			g_object_table[player->object_index].mobj->p_craft;
		int dst_energy = craft->shield_energy[dst_bank];
		int *dst_shield_energy = &craft->shield_energy[dst_bank];
		int16_t transfer_capacity = object_max_shield - dst_energy;
		if (transfer_capacity > 0) {
			int src_energy = craft->shield_energy[src_bank];
			if (src_energy > transfer_capacity) {
				*dst_shield_energy =
					dst_energy + transfer_capacity;
				g_object_table[player->object_index]
					.mobj->p_craft
					->shield_energy[src_bank] -=
					transfer_capacity;
				XVT_LOG_DEBUG(
					"player.shield_moved slot=%d shield=%d moved=%d front=%d rear=%d predicted=%d",
					player_idx, (int)dst_bank,
					(int)transfer_capacity,
					craft->shield_energy[0],
					craft->shield_energy[1],
					g_flight_sim_side_effects_suppressed);
			} else {
				*dst_shield_energy = dst_energy + src_energy;
				g_object_table[player->object_index]
					.mobj->p_craft
					->shield_energy[src_bank] = 0;
				XVT_LOG_DEBUG(
					"player.shield_moved slot=%d shield=%d moved=%d front=%d rear=%d predicted=%d",
					player_idx, (int)dst_bank, src_energy,
					craft->shield_energy[0],
					craft->shield_energy[1],
					g_flight_sim_side_effects_suppressed);
			}
		}
	}
}

/* Sets the HUD view for where the player's camera looks. With the external
 * camera on: the target camera view while target_camera_active is set, else full
 * screen, and all 60 entries of the camera roll, pitch and yaw histories set to
 * the current view angles. With it off: input unblocked; back on the player's
 * own craft, the saved HUD aim and view return; on another object, the HUD aim
 * is centered, the view goes full screen and the external camera turns on. */
// FUNCTION: XVT 0x481FB0
void player_update_hud_view_for_camera_focus(int player_idx)
{
	enum {
		CAMERA_HISTORY_SAMPLE_COUNT = 60,
	};

	if (g_players[player_idx].view_state.external_camera_active != 0) {
		if (g_players[player_idx].view_state.target_camera_active !=
		    0) {
			hud_set_hud_view_state(HUD_VIEW_TARGET_CAMERA,
					       player_idx);
		} else {
			hud_set_hud_view_state(HUD_VIEW_FULL_SCREEN,
					       player_idx);
		}
		for (uint16_t sample_index = 0;
		     sample_index < CAMERA_HISTORY_SAMPLE_COUNT;
		     ++sample_index) {
			g_players[player_idx]
				.view_state.camera_roll_history[sample_index] =
				g_players[player_idx].view_state.view_roll;
			g_players[player_idx]
				.view_state.camera_pitch_history[sample_index] =
				g_players[player_idx].view_state.view_pitch;
			g_players[player_idx]
				.view_state.camera_yaw_history[sample_index] =
				g_players[player_idx].view_state.view_yaw;
		}
	} else {
		g_players[player_idx].view_state.player_input_blocked = 0;
		if (g_players[player_idx].view_state.camera_focus_obj_idx ==
		    g_players[player_idx].object_index) {
			g_players[player_idx].view_state.hud_aim_x =
				g_players[player_idx]
					.view_state.saved_hud_aim_x;
			g_players[player_idx].view_state.hud_aim_y =
				g_players[player_idx]
					.view_state.saved_hud_aim_y;
			hud_set_hud_view_state(
				g_players[player_idx]
					.view_state.saved_hud_state_byte,
				player_idx);
		} else {
			g_players[player_idx].view_state.hud_aim_x = 0;
			g_players[player_idx].view_state.hud_aim_y = 0;
			hud_set_hud_view_state(HUD_VIEW_FULL_SCREEN,
					       player_idx);
			g_players[player_idx]
				.view_state.external_camera_active = 1;
		}
	}
	XVT_LOG_DEBUG(
		"player.view_focus slot=%d focus=%d own=%d external=%d target_camera=%d view=%d predicted=%d",
		player_idx,
		(int)g_players[player_idx].view_state.camera_focus_obj_idx,
		g_players[player_idx].view_state.camera_focus_obj_idx ==
			g_players[player_idx].object_index,
		(int)g_players[player_idx].view_state.external_camera_active,
		(int)g_players[player_idx].view_state.target_camera_active,
		(int)g_players[player_idx].view_state.hud_state_live,
		g_flight_sim_side_effects_suppressed);
}

/* Returns the object the player is looking at, or UINT16_MAX: among targetable
 * objects (type behavior_flags bit 0) in the main and static slots other than
 * the player's craft, the nearest inside the narrow aim cone; failing that, the
 * one closest to the aim line when its g_target_angle_score is below 50, or below
 * 250 with the map camera. Without the map camera, a pick with an active decoy
 * beam is refused with IFMSG_257. */
// FUNCTION: XVT 0x4820B0
uint16_t player_pick_target_in_sight(int player_idx)
{
	uint16_t best_angular_target = UINT16_MAX;
	uint16_t best_target = UINT16_MAX;
	uint16_t best_angle = UINT16_MAX;
	unsigned int best_range = UINT_MAX;
	uint16_t object_idx;

	for (object_idx = (uint16_t)g_active_region_object_slot_start;
	     object_idx < g_region_main_object_slot_end; ++object_idx) {
		if (g_object_table[object_idx].object_type != 0 &&
		    g_players[player_idx].object_index != object_idx &&
		    (g_object_type_table[g_object_table[object_idx].object_type]
			     .behavior_flags &
		     1) != 0) {
			if (targeting_test_aim_cone(object_idx, 1,
						    player_idx)) {
				if (best_range >
				    (unsigned int)g_last_rough_distance) {
					best_target = object_idx;
					best_range = g_last_rough_distance;
				}
			} else {
				if (best_angle <= g_target_angle_score) {
					continue;
				}
				best_angle = g_target_angle_score;
				best_angular_target = object_idx;
			}
		}
	}
	for (object_idx = (uint16_t)g_region_main_object_slot_end;
	     object_idx <
	     g_region_static_object_slot_count + g_region_main_object_slot_end;
	     ++object_idx) {
		if (g_object_table[object_idx].object_type != 0 &&
		    (g_object_type_table[g_object_table[object_idx].object_type]
			     .behavior_flags &
		     1) != 0) {
			if (targeting_test_aim_cone(object_idx, 1,
						    player_idx)) {
				if (best_range >
				    (unsigned int)g_last_rough_distance) {
					best_target = object_idx;
					best_range = g_last_rough_distance;
				}
			} else {
				if (best_angle <= g_target_angle_score) {
					continue;
				}
				best_angle = g_target_angle_score;
				best_angular_target = object_idx;
			}
		}
	}
	if (best_target == UINT16_MAX) {
		if (g_players[player_idx].map_camera_state != 0) {
			if (best_angle < 0xFA) {
				best_target = best_angular_target;
			}
		} else if (best_angle < 0x32) {
			best_target = best_angular_target;
		}
	}
	if (g_players[player_idx].map_camera_state == 0 &&
	    object_has_active_decoy_beam(best_target) == 1) {
		XVT_LOG_DEBUG(
			"player.pick_decoyed slot=%d target=%d predicted=%d",
			player_idx, (int)best_target,
			g_flight_sim_side_effects_suppressed);
		best_target = UINT16_MAX;
		msg_emit_in_flight_message(
			IFMSG_257_TARGET_ACQUISITION_BLOCKED_BY_DECOY_BEAM,
			player_idx);
	}
	return best_target;
}

/* Besides picking the next target, this leaves g_cur_craft pointing at the last
 * craft it examined. */
/* Steps from current_obj_idx by direction through the main and static slots,
 * wrapping, and returns the first targetable object (type behavior_flags bit 0)
 * other than the player's craft. Objects with mobile data are skipped when
 * explosions, under an active decoy beam, or craft breaking up or exploding.
 * Returns UINT16_MAX after a full lap with none. */
// FUNCTION: XVT 0x4822C0
uint16_t player_cycle_target_any_iff(uint16_t current_obj_idx,
				     int16_t direction, int player_idx)
{
	int object_count = g_region_static_object_slot_count;
	int16_t remaining_objects = (int16_t)object_count;
	remaining_objects += (int16_t)g_region_main_object_slot_end;
	for (;;) {
		if (remaining_objects-- == 0) {
			return UINT16_MAX;
		}
		current_obj_idx += direction;
		object_count = g_region_static_object_slot_count;
		if (current_obj_idx >= 0x8000u) {
			current_obj_idx =
				(uint16_t)(object_count +
					   (int16_t)
						   g_region_main_object_slot_end -
					   1);
		} else if (object_count + g_region_main_object_slot_end ==
			   current_obj_idx) {
			current_obj_idx = 0;
		}

		if (g_players[player_idx].object_index != current_obj_idx) {
			struct object_record *object =
				&g_object_table[current_obj_idx];
			if (object->object_type != 0 &&
			    (g_object_type_table[object->object_type]
				     .behavior_flags &
			     1) != 0) {
				if (object->mobj == NULL) {
					break;
				}
				if (object->genus_id != CRAFT_GENUS_EXPLOSION &&
				    object_has_active_decoy_beam(
					    current_obj_idx) == 0) {
					struct mobile_object *mobile_object =
						g_object_table[current_obj_idx]
							.mobj;
					if (mobile_object->family != 0) {
						break;
					}
					g_cur_craft = mobile_object->p_craft;
					uint8_t object_kind =
						g_cur_craft->object_kind;
					if (object_kind !=
						    CRAFT_OBJECT_KIND_BREAKING_UP &&
					    object_kind !=
						    CRAFT_OBJECT_KIND_EXPLODING) {
						break;
					}
				}
			}
		}
	}
	return current_obj_idx;
}

/* Besides picking the next target, this leaves g_cur_craft pointing at the last
 * craft it examined. */
/* Steps like player_cycle_target_any_iff, with filters. target_flags bit 2 skips
 * projectile slots, bit 1 skips slots from g_projectile_object_slot_end up, bit 0
 * skips mines. iff_filter 1 keeps the player's team, 2 objects not hostile to
 * it, 3 hostile objects, 4 craft flown by players; the team is the mobile team,
 * or the flight group's for an object without mobile data. Returns UINT16_MAX
 * after a full lap with none. */
// FUNCTION: XVT 0x4823E0
uint16_t player_cycle_target(uint16_t current_obj_idx, int16_t direction,
			     int player_idx, int iff_filter, int target_flags)
{
	uint16_t object_index = current_obj_idx;
	int object_count = g_region_static_object_slot_count;
	int16_t remaining_objects = (int16_t)object_count;
	remaining_objects += (int16_t)g_region_main_object_slot_end;
	struct object_record *object;
	object_type_id object_type;
	int object_team;
	uint16_t player_team;
	for (;;) {
		if (remaining_objects-- == 0) {
			return UINT16_MAX;
		}

		object_index += direction;
		object_count = g_region_static_object_slot_count;
		if (object_index >= 0x8000u) {
			object_index =
				(uint16_t)(object_count +
					   (int16_t)
						   g_region_main_object_slot_end -
					   1);
		} else if (object_count + g_region_main_object_slot_end ==
			   object_index) {
			object_index = 0;
		}

		if (g_players[player_idx].object_index != object_index &&
		    ((target_flags & 4) == 0 ||
		     g_projectile_object_slot_start > object_index ||
		     g_projectile_object_slot_end <= object_index) &&
		    ((target_flags & 2) == 0 ||
		     g_projectile_object_slot_end > object_index)) {
			object = &g_object_table[object_index];
			object_type = object->object_type;
			if (object_type != 0 &&
			    (g_object_type_table[object_type].behavior_flags &
			     1) != 0 &&
			    ((target_flags & 1) == 0 ||
			     object->genus_id != CRAFT_GENUS_MINE)) {
				object_team =
					object->mobj == NULL
						? g_mission_flight_groups
							  [object->flight_group_idx]
								  .fg.team
						: object->mobj->team;

				/* Cases 2 and 3 overwrite the object's team
				 * with a 0/1 flag: 1 when hostile to the
				 * player. */
				switch (iff_filter) {
				case 1:
					if ((uint16_t)g_players[player_idx]
						    .team != object_team) {
						continue;
					}
					break;

				case 2:
					player_team =
						(uint16_t)g_players[player_idx]
							.team;
					if (player_team == object_team) {
						object_team = 0;
					} else {
						object_team =
							g_mission_teams[player_team]
								.allies[object_team] ==
							0;
					}
					if (object_team == 1) {
						continue;
					}
					break;

				case 3:
					player_team =
						(uint16_t)g_players[player_idx]
							.team;
					if (player_team == object_team) {
						object_team = 0;
					} else {
						object_team =
							g_mission_teams[player_team]
								.allies[object_team] ==
							0;
					}
					if (object_team == 0) {
						continue;
					}
					break;

				case 4:
					if (object->player_owner_idx == -1) {
						continue;
					}
					break;

				default:
					break;
				}

				if (object->mobj == NULL) {
					break;
				}
				if (object->genus_id != CRAFT_GENUS_EXPLOSION &&
				    object_has_active_decoy_beam(
					    object_index) == 0) {
					struct mobile_object *mobile_object =
						g_object_table[object_index]
							.mobj;
					if (mobile_object->family != 0) {
						break;
					}
					g_cur_craft = mobile_object->p_craft;
					uint8_t object_kind =
						g_cur_craft->object_kind;
					if (object_kind !=
						    CRAFT_OBJECT_KIND_BREAKING_UP &&
					    object_kind !=
						    CRAFT_OBJECT_KIND_EXPLODING) {
						break;
					}
				}
			}
		}
	}

	return object_index;
}

/* Makes an object the player's current target. Ignores UINT16_MAX, empty slots,
 * explosions and the player's own craft. With the targeting computer of the
 * player's craft out, emits IFMSG_086 and changes nothing. For a new target:
 * plays FLIGHT_SOUND_TARGET_SELECTED; picks its component in a craft slot (the
 * first main hull or fuselage with the map camera, else
 * player_select_target_component_mesh), or 0; aims the camera at it while
 * target_camera_active is set; clears missile_lock_state and the craft's
 * warhead_lock_ticks; and describes it with msg_build_target_description with the
 * map camera, or when the craft's first HUD feature is active and the player's
 * view is HUD only or forward, or the local player's is the target camera.
 * Without the map camera, does not check that the player has a craft before
 * reading its HUD features. */
// FUNCTION: XVT 0x4833D0
void player_set_target(int new_target_obj_idx, int player_idx)
{
	enum { OBJECT_TYPE_MESH_CACHE_COUNT = 73 };

	if ((uint16_t)new_target_obj_idx == UINT16_MAX) {
		return;
	}
	int target_obj_idx = (uint16_t)new_target_obj_idx;
	if (g_object_table[target_obj_idx].object_type == 0 ||
	    g_object_table[target_obj_idx].genus_id == 13) {
		return;
	}
	int player_object_idx = g_players[player_idx].object_index;
	if (player_object_idx == target_obj_idx) {
		return;
	}
	int can_target = 1;
	if (player_object_idx != -1) {
		if ((g_object_table[player_object_idx]
			     .mobj->p_craft->working_subsystems &
		     CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) == 0) {
			can_target = 0;
		}
	}
	if (can_target != 0) {
		if ((uint16_t)new_target_obj_idx != UINT16_MAX &&
		    (uint16_t)g_players[player_idx].current_target_object_idx !=
			    (uint16_t)new_target_obj_idx) {
			fsfx_play_sound(FLIGHT_SOUND_TARGET_SELECTED, -1,
					player_idx);
			XVT_LOG_DEBUG(
				"player.target_changed slot=%d target=%d previous=%d map=%d predicted=%d",
				player_idx, target_obj_idx,
				(int)g_players[player_idx]
					.current_target_object_idx,
				g_players[player_idx].map_camera_state != 0,
				g_flight_sim_side_effects_suppressed);
			g_players[player_idx].current_target_object_idx =
				(uint16_t)new_target_obj_idx;
			g_players[player_idx].selected_target_component = 0;
			if (g_active_region_craft_object_slot_end >
			    target_obj_idx) {
				if (g_players[player_idx].map_camera_state !=
				    0) {
					int object_type =
						g_object_table
							[(uint16_t)g_players[player_idx]
								 .current_target_object_idx]
								.object_type;
					int mesh_count;
					if (object_type <
					    OBJECT_TYPE_MESH_CACHE_COUNT) {
						mesh_count =
							g_object_type_mesh_cache
								[object_type]
									.mesh_count;
					} else {
						mesh_count =
							model_mesh_get_object_type_mesh_count(
								object_type);
					}
					for (uint16_t mesh_index = 0;
					     mesh_index < mesh_count;
					     ++mesh_index) {
						int cached_mesh_index =
							mesh_index;
						object_type =
							g_object_table
								[(uint16_t)g_players
									 [player_idx]
										 .current_target_object_idx]
									.object_type;
						int mesh_type;
						if (object_type <
						    OBJECT_TYPE_MESH_CACHE_COUNT) {
							mesh_type = model_mesh_get_cached_object_type_mesh_type(
								object_type,
								cached_mesh_index);
						} else {
							mesh_type = model_mesh_get_object_type_mesh_type(
								object_type,
								mesh_index);
						}
						if (mesh_type ==
							    MESH_COMPONENT_01_MAIN_HULL ||
						    mesh_type ==
							    MESH_COMPONENT_03_FUSELAGE) {
							g_players[player_idx]
								.selected_target_component =
								mesh_index;
							break;
						}
					}
				} else {
					g_players[player_idx]
						.selected_target_component =
						player_select_target_component_mesh(
							(uint16_t)
								new_target_obj_idx,
							player_idx);
				}
			}
			XVT_LOG_DEBUG(
				"player.target_component slot=%d target=%d component=%d predicted=%d",
				player_idx, target_obj_idx,
				(int)g_players[player_idx]
					.selected_target_component,
				g_flight_sim_side_effects_suppressed);
			if (g_players[player_idx]
				    .view_state.target_camera_active != 0) {
				g_players[player_idx]
					.view_state.camera_focus_obj_idx =
					g_players[player_idx]
						.current_target_object_idx;
			}
			g_players[player_idx].missile_lock_state = 0;
			player_object_idx = g_players[player_idx].object_index;
			if (player_object_idx != -1) {
				g_object_table[player_object_idx]
					.mobj->p_craft->warhead_lock_ticks = 0;
			}
			if (g_players[player_idx].map_camera_state != 0) {
				msg_build_target_description(new_target_obj_idx,
							     player_idx, 1, 0);
				return;
			}
			player_object_idx = g_players[player_idx].object_index;
			if ((g_object_table[player_object_idx]
				     .mobj->p_craft->damage_stats
				     .active_hud_feature_mask &
			     1) == 0) {
				return;
			}
			if (g_players[player_idx].view_state.hud_state_live ==
				    HUD_VIEW_HUD_ONLY ||
			    g_players[player_idx].view_state.hud_state_live ==
				    HUD_VIEW_FORWARD ||
			    g_players[g_local_player]
					    .view_state.hud_state_live ==
				    HUD_VIEW_TARGET_CAMERA) {
				msg_build_target_description(new_target_obj_idx,
							     player_idx, 1, 0);
			}
		}
	} else {
		XVT_LOG_DEBUG(
			"player.target_refused slot=%d target=%d predicted=%d",
			player_idx, target_obj_idx,
			g_flight_sim_side_effects_suppressed);
		g_msg_arg_table[0] = IFMSG_096_TARGETING_COMPUTER;
		g_msg_arg_table[1] = IFMSG_087_DAMAGED_AND_INOPERATIVE;
		msg_emit_in_flight_message(IFMSG_086_ARG_SYSTEM_IS_ARG,
					   player_idx);
	}
}

/* Returns the component to aim at on a new target: for a starship or platform,
 * the main hull or fuselage mesh nearest the player's craft (0 if none is
 * nearer than 0x1000000); for anything else the first main hull or fuselage
 * mesh, or the mesh count, one past the last mesh, when it has none. */
// FUNCTION: XVT 0x483800
uint16_t player_select_target_component_mesh(uint16_t target_obj_idx,
					     unsigned int player_idx)
{
	unsigned int nearest_distance = 0x1000000;
	/* Prefer the nearest hull or fuselage component for capital ships. */
	uint16_t genus_id = g_object_table[target_obj_idx].genus_id;

	int object_type;
	int mesh_type_index;
	uint16_t mesh_index;
	int mesh_count;
	mesh_component_type mesh_type;
	if (genus_id == CRAFT_GENUS_STARSHIP ||
	    genus_id == CRAFT_GENUS_PLATFORM) {
		uint16_t selected_mesh_idx = 0;
		object_type = g_object_table[target_obj_idx].object_type;
		if (object_type < (int)(sizeof(g_object_type_mesh_cache) /
					sizeof(g_object_type_mesh_cache[0]))) {
			mesh_count = g_object_type_mesh_cache[object_type]
					     .mesh_count;
		} else {
			mesh_count = model_mesh_get_object_type_mesh_count(
				object_type);
		}

		for (mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
			mesh_type_index = mesh_index;
			object_type =
				g_object_table[target_obj_idx].object_type;
			if (object_type <
			    (int)(sizeof(g_object_type_mesh_cache) /
				  sizeof(g_object_type_mesh_cache[0]))) {
				mesh_type =
					model_mesh_get_cached_object_type_mesh_type(
						object_type, mesh_type_index);
			} else {
				mesh_type =
					model_mesh_get_object_type_mesh_type(
						object_type, mesh_index);
			}
			if (mesh_type == MESH_COMPONENT_01_MAIN_HULL ||
			    mesh_type == MESH_COMPONENT_03_FUSELAGE) {
				unsigned int distance =
					object_direction_and_distance_to_mesh_center(
						g_players[player_idx]
							.object_index,
						target_obj_idx, mesh_index);
				if (distance < nearest_distance) {
					selected_mesh_idx = mesh_index;
					nearest_distance = distance;
				}
			}
		}
		return selected_mesh_idx;
	}

	object_type = g_object_table[target_obj_idx].object_type;
	if (object_type < (int)(sizeof(g_object_type_mesh_cache) /
				sizeof(g_object_type_mesh_cache[0]))) {
		mesh_count = g_object_type_mesh_cache[object_type].mesh_count;
	} else {
		mesh_count = model_mesh_get_object_type_mesh_count(object_type);
	}

	for (mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
		mesh_type_index = mesh_index;
		object_type = g_object_table[target_obj_idx].object_type;
		if (object_type < (int)(sizeof(g_object_type_mesh_cache) /
					sizeof(g_object_type_mesh_cache[0]))) {
			mesh_type = model_mesh_get_cached_object_type_mesh_type(
				object_type, mesh_type_index);
		} else {
			mesh_type = model_mesh_get_object_type_mesh_type(
				object_type, mesh_type_index);
		}
		if (mesh_type == MESH_COMPONENT_01_MAIN_HULL ||
		    mesh_type == MESH_COMPONENT_03_FUSELAGE) {
			return mesh_index;
		}
	}
	return mesh_index;
}

/* Applies a pitch step and a yaw step to a craft's orientation. It returns the
 * new roll, which both callers drop. The yaw step is ignored while the roll
 * modifier key is held (g_flight_key_mods & 0xE is 2). The modern build turns the
 * angles with xvt_orientation_apply_pitch_yaw and writes the object's pitch, yaw
 * and roll and craft->pitch. The original build rotates the object's axes in
 * g_curMatR0 to g_curMatR2 and writes the new pitch to craft->pitch only, and
 * yaw and roll to the object. */
// FUNCTION: XVT 0x483A00
int16_t player_apply_pitch_yaw_steps(int16_t pitch_angle_q16,
				     int16_t yaw_angle_q16,
				     uint16_t object_index,
				     struct craft_data *craft)
{
	struct object_record *object = &g_object_table[object_index];
	struct xvt_orientation_angles current = {object->yaw, object->pitch,
						 object->roll};
	struct xvt_orientation_angles updated = xvt_orientation_apply_pitch_yaw(
		current, pitch_angle_q16,
		(g_flight_key_mods & 0xE) == 2 ? 0 : yaw_angle_q16);
	/* BoP also retains the commanded pitch in craft_data. Publish a coherent
	 * orientation immediately, as XWA does, before the per-object step gate. */
	object->pitch = updated.pitch;
	craft->pitch = object->pitch;
	object->yaw = updated.yaw;
	object->roll = updated.roll;
	return (int16_t)updated.roll;
}

/* Besides answering, this leaves g_cur_craft pointing at the targeted craft
 * when no player flies it. */
/* Tells whether the player may order their current target by radio. Returns 0
 * when there is none, it lies past the craft slots, a player flies it, or it is
 * not active or has no working subsystem; 1 when it belongs to the player's
 * flight group. Otherwise returns 0 when its group's radio setting is 0, and 1
 * when its group is on the player's team, shares the player's group's global
 * unit, has radio minus that group's player_number equal to 8, or radio minus
 * the player's team equal to 1; else 0. */
// FUNCTION: XVT 0x4841B0
int16_t player_can_radio_command_craft(int player_idx)
{
	if (g_players[player_idx].current_target_object_idx == -1) {
		return 0;
	}
	int current_target_object_idx =
		(uint16_t)g_players[player_idx].current_target_object_idx;
	if (g_active_region_craft_object_slot_end <=
	    current_target_object_idx) {
		return 0;
	}
	struct object_record *target_object =
		&g_object_table[current_target_object_idx];
	if (target_object->player_owner_idx != -1) {
		return 0;
	}
	g_cur_craft = target_object->mobj->p_craft;
	if (g_cur_craft->object_kind != CRAFT_OBJECT_KIND_ACTIVE) {
		return 0;
	}
	if (g_cur_craft->working_subsystems == 0) {
		return 0;
	}
	int flight_group_idx = target_object->flight_group_idx;
	int bound_flight_group_idx =
		(uint16_t)g_players[player_idx].bound_flight_group_idx;
	if (flight_group_idx == bound_flight_group_idx) {
		return 1;
	}
	uint8_t radio = g_mission_flight_groups[flight_group_idx].fg.radio;
	if (radio == 0) {
		return 0;
	}
	int player_team = (uint16_t)g_players[player_idx].team;
	if (g_mission_flight_groups[flight_group_idx].fg.team == player_team) {
		return 1;
	}
	uint8_t global_unit =
		g_mission_flight_groups[flight_group_idx].fg.global_unit;
	if (global_unit != 0 &&
	    g_mission_flight_groups[bound_flight_group_idx].fg.global_unit ==
		    global_unit) {
		return 1;
	}
	if (radio - g_mission_flight_groups[bound_flight_group_idx]
			    .fg.player_number ==
	    8) {
		return 1;
	}
	return radio - player_team == 1;
}

/* Passes the player's order about a target to their AI wingmen; does nothing
 * when the target is given and not hostile to the player's team. Wingmen are
 * active AI craft of that team in the player's flight group, or in a group
 * sharing its global unit or whose radio setting minus the player's group's
 * player_number is 8. For commandId 155 each avoids the target
 * (player_command_avoid_target_obj_idx) and drops it as candidate target. For any
 * other command each takes it as candidate target and stops avoiding it, after
 * leaving craftwaitforgopln for its saved plan; craft on the still, formation,
 * fly-home, into-hyperspace or hangar plans are skipped. For the local player
 * the last wingman answers by radio. Writes g_cur_craft when a wingman leaves
 * craftwaitforgopln. */
// FUNCTION: XVT 0x484320
void player_issue_ai_wingman_target_order(uint16_t target_obj_idx,
					  uint16_t command_id,
					  uint16_t response_index,
					  int player_idx)
{
	if (target_obj_idx != UINT16_MAX) {
		int target_team =
			g_mission_flight_groups[g_object_table[target_obj_idx]
							.flight_group_idx]
				.fg.team;
		int target_is_hostile;
		if (target_team == (uint16_t)g_players[player_idx].team) {
			target_is_hostile = 0;
		} else {
			target_is_hostile =
				!g_mission_teams[(uint16_t)g_players[player_idx]
							 .team]
					 .allies[target_team];
		}
		if (!target_is_hostile) {
			XVT_LOG_DEBUG(
				"player.order_ignored slot=%d command=%d target=%d predicted=%d",
				player_idx, (int)command_id,
				(int)target_obj_idx,
				g_flight_sim_side_effects_suppressed);
			return;
		}
	}

	uint16_t matching_wingmen = 0;
	uint16_t last_wingman = UINT16_MAX;
	for (uint16_t object_index =
		     (uint16_t)g_active_region_object_slot_start;
	     object_index < g_active_region_craft_object_slot_end;
	     object_index++) {
		if (g_players[player_idx].object_index == object_index) {
			continue;
		}
		struct object_record *object = &g_object_table[object_index];
		if (object->player_owner_idx != -1 ||
		    object->object_type == 0) {
			continue;
		}
		int flight_group_idx = object->flight_group_idx;
		if (g_mission_flight_groups[flight_group_idx].fg.team !=
		    (uint16_t)g_players[player_idx].team) {
			continue;
		}
		struct craft_data *craft = object->mobj->p_craft;
		if (craft->object_kind != CRAFT_OBJECT_KIND_ACTIVE) {
			continue;
		}
		uint16_t bound_flight_group_idx =
			g_players[player_idx].bound_flight_group_idx;
		int player_slot =
			g_mission_flight_groups[bound_flight_group_idx]
				.fg.player_number -
			1;
		if (flight_group_idx != bound_flight_group_idx) {
			uint8_t global_unit =
				g_mission_flight_groups[flight_group_idx]
					.fg.global_unit;
			if ((global_unit == 0 ||
			     g_mission_flight_groups[bound_flight_group_idx]
					     .fg.global_unit != global_unit) &&
			    g_mission_flight_groups[flight_group_idx].fg.radio -
					    player_slot !=
				    9) {
				continue;
			}
		}
		struct ai_controller *ai = &craft->ai_controller;
		if (command_id != 155) {
			const char *plan_name =
				g_plan_table[ai->running_plan_id].name;
			if (strcmp(plan_name, "nullpln") == 0 ||
			    strcmp(plan_name, "stationaryldrpln") == 0 ||
			    strcmp(plan_name, "stationaryflwpln") == 0 ||
			    strcmp(plan_name, "formldr1pln") == 0 ||
			    strcmp(plan_name, "formflw1pln") == 0 ||
			    strcmp(plan_name, "formevadeldr1pln") == 0 ||
			    strcmp(plan_name, "formevadeflw1pln") == 0 ||
			    strcmp(plan_name, "flyhomeevadepln") == 0 ||
			    strcmp(plan_name, "intohyperspacepln") == 0 ||
			    strcmp(plan_name, "enterhangarpln") == 0) {
				continue;
			}
			if (strcmp(plan_name, "craftwaitforgopln") == 0) {
				ai->running_plan_id = ai->saved_plan_id;
				g_cur_craft = craft;
				pai_setupcraftcontext(object_index);
				pai_apply_running_plan_target_and_maneuver(
					object_index);
				XVT_LOG_DEBUG(
					"player.wingman_released slot=%d object=%d plan=%d predicted=%d",
					player_idx, (int)object_index,
					(int)ai->running_plan_id,
					g_flight_sim_side_effects_suppressed);
			}
			ai->candidate_target_idx = target_obj_idx;
			if (craft->player_command_avoid_target_obj_idx ==
			    target_obj_idx) {
				craft->player_command_avoid_target_obj_idx =
					UINT16_MAX;
			}
		} else {
			craft->player_command_avoid_target_obj_idx =
				target_obj_idx;
			if (ai->candidate_target_idx == target_obj_idx) {
				ai->candidate_target_idx = UINT16_MAX;
			}
		}
		matching_wingmen++;
		last_wingman = object_index;
	}
	XVT_LOG_DEBUG(
		"player.order_passed slot=%d command=%d target=%d wingmen=%u last=%d predicted=%d",
		player_idx, (int)command_id,
		target_obj_idx == UINT16_MAX ? -1 : (int)target_obj_idx,
		(unsigned)matching_wingmen,
		last_wingman == UINT16_MAX ? -1 : (int)last_wingman,
		g_flight_sim_side_effects_suppressed);
	if (player_idx == g_local_player && last_wingman != UINT16_MAX) {
		uint8_t *wingman_craft =
			(uint8_t *)g_object_table[last_wingman].mobj->p_craft;
		if (matching_wingmen == 1) {
			msg_radio_message(last_wingman, wingman_craft,
					  command_id, response_index, 0);
		} else {
			msg_radio_message(last_wingman, wingman_craft,
					  command_id, response_index, 1);
		}
	}
}

/* Returns the craft nearest the target among those attacking it, or -1 when
 * none or the target is UINT16_MAX. Looks at active craft with a working
 * subsystem and no active decoy beam, other than the target, excluded_obj_idx and
 * explosions. An AI craft attacks when its AI target is the target and its
 * maneuver is attack or rocket attack. A player's craft attacks when it hit the
 * target craft last and the target is not a player's or was hit under 5 mission
 * seconds ago; or when its player has the target selected and it lies within
 * rough distance 0x10000 in the wide aim cone, or its warhead lock is building.
 * Writes g_last_rough_distance, g_target_angle_score and trig2's polar results. */
// FUNCTION: XVT 0x4846F0
int16_t player_find_attacker_of_target(uint16_t target_obj_idx,
				       int16_t excluded_obj_idx)
{
	if (target_obj_idx == UINT16_MAX) {
		return -1;
	}
	unsigned int nearest_distance = UINT_MAX;
	uint16_t nearest = UINT16_MAX;
	for (uint16_t object_idx = (uint16_t)g_active_region_object_slot_start;
	     object_idx < g_active_region_craft_object_slot_end; ++object_idx) {
		if (g_object_table[object_idx].object_type == 0 ||
		    target_obj_idx == object_idx ||
		    object_idx == (uint16_t)excluded_obj_idx ||
		    g_object_table[object_idx].genus_id ==
			    CRAFT_GENUS_EXPLOSION) {
			continue;
		}
		struct craft_data *craft =
			g_object_table[object_idx].mobj->p_craft;
		int qualifies = 0;
		if (craft->working_subsystems == 0 ||
		    craft->object_kind != CRAFT_OBJECT_KIND_ACTIVE ||
		    object_has_active_decoy_beam(object_idx)) {
			continue;
		}
		if (g_object_table[object_idx].player_owner_idx == -1) {
			struct ai_controller *ai = &craft->ai_controller;
			if (ai->target_obj_idx != target_obj_idx ||
			    (ai->maneuver_mode != AI_MANEUVER_MODE_ATTACK &&
			     ai->maneuver_mode !=
				     AI_MANEUVER_MODE_ROCKET_ATTACK)) {
				continue;
			}
			qualifies = 1;
		} else {
			if (g_active_region_craft_object_slot_end >
			    target_obj_idx) {
				struct craft_data *target_craft =
					g_object_table[target_obj_idx]
						.mobj->p_craft;
				if (target_craft->last_attacker_obj_idx ==
					    object_idx &&
				    (g_object_table[target_obj_idx]
						     .player_owner_idx == -1 ||
				     (uint16_t)mission_clock_to_seconds(
					     g_mission_elapsed_clock.hours,
					     g_mission_elapsed_clock.minutes,
					     g_mission_elapsed_clock.seconds) -
						     target_craft
							     ->last_hit_mission_second <
					     5)) {
					qualifies = 1;
				}
			}
			int player_owner_idx =
				g_object_table[object_idx].player_owner_idx;
			if ((uint16_t)g_players[player_owner_idx]
				    .current_target_object_idx ==
			    target_obj_idx) {
				pai_object_ref_update_rough_distance(
					object_idx, target_obj_idx);
				if (g_last_rough_distance < 0x10000 &&
				    targeting_test_aim_cone(target_obj_idx, 0,
							    player_owner_idx)) {
					qualifies = 1;
				}
				if (craft->warhead_lock_ticks != 0) {
					qualifies = 1;
				}
			}
		}
		if (qualifies != 0) {
			pai_object_ref_direction_to_object_ref(target_obj_idx,
							       object_idx);
			if (nearest_distance >
			    (unsigned int)trig2_polardistance) {
				nearest_distance = trig2_polardistance;
				nearest = object_idx;
			}
		}
	}
	return (int16_t)nearest;
}

/* Starts the player's view after their craft is destroyed: hyperspace off and
 * awaiting_new_craft 1. With the map camera, only clears the local player's ready
 * message queue. Otherwise the camera is freed at the craft's last position
 * with input blocked, the view goes full screen, beam and missile warning
 * sounds stop and FLIGHT_SOUND_MISSILE_LOCK_3 plays; when the local player was
 * killed by an object in a craft slot, the killer is named: IFMSG_399 also
 * naming source_player_idx's craft when that player is flying and does not fly
 * the killer, else IFMSG_398. Does not check that the player has a craft. */
// FUNCTION: XVT 0x484A30
void player_start_post_destruction_state(int player_idx,
					 unsigned int source_object_index,
					 int source_player_idx)
{
	enum {
		KILL_MESSAGE_NAME_SIZE = 64,
		OBJECT_DISPLAY_NAME_AND_TYPE = 3,
	};

	g_players[player_idx].hyperspace_phase = 0;
	if (g_players[player_idx].map_camera_state != 0) {
		if (player_idx == g_local_player) {
			hud_clear_ready_message_queue();
		}
	} else {
		g_players[player_idx].view_state.camera_focus_obj_idx =
			UINT16_MAX;
		g_players[player_idx].view_state.camera_world_x =
			g_object_table[g_players[player_idx].object_index]
				.mobj->prev_world_x;
		g_players[player_idx].view_state.camera_world_y =
			g_object_table[g_players[player_idx].object_index]
				.mobj->prev_world_y;
		int local_player = g_local_player;
		g_players[player_idx].view_state.camera_world_z =
			g_object_table[g_players[player_idx].object_index]
				.mobj->prev_world_z;
		g_players[player_idx].view_state.external_camera_active = 1;
		g_players[player_idx].view_state.player_input_blocked = 1;
		g_players[player_idx].view_state.hud_aim_y = 0;
		g_players[player_idx].view_state.hud_aim_x = 0;
		if (player_idx == local_player) {
			hud_clear_ready_message_queue();
		}

		fsfx_update_beam_system_loop(0, player_idx);
		fsfx_update_incoming_missile_warning(0);
		fsfx_play_sound(FLIGHT_SOUND_MISSILE_LOCK_3, -1, player_idx);
		hud_set_hud_view_state(HUD_VIEW_FULL_SCREEN, player_idx);
		if (player_idx == g_local_player &&
		    source_object_index != UINT_MAX &&
		    source_object_index <
			    (unsigned int)
				    g_active_region_craft_object_slot_end) {
			if (g_active_flight_player_count > 1) {
				char source_name[KILL_MESSAGE_NAME_SIZE];
				if (source_player_idx != -1 &&
				    g_object_table[source_object_index]
						    .player_owner_idx !=
					    source_player_idx &&
				    g_players[source_player_idx].object_index !=
					    -1) {
					player_append_kill_message_actor_name(
						0, source_name,
						(int)source_object_index);
					char assisting_player_name
						[KILL_MESSAGE_NAME_SIZE];
					player_append_kill_message_actor_name(
						1, assisting_player_name,
						g_players[source_player_idx]
							.object_index);
					msg_emit_in_flight_message(
						IFMSG_399_YOU_WERE_KILLED_BY_ARG_MOST_DAMAGE_WAS_DONE_BY_ARG,
						g_local_player);
				} else {
					player_append_kill_message_actor_name(
						0, source_name,
						(int)source_object_index);
					msg_emit_in_flight_message(
						IFMSG_398_YOU_WERE_KILLED_BY_ARG,
						g_local_player);
				}
			} else {
				hud_format_object_display_name(
					(uint16_t)source_object_index,
					OBJECT_DISPLAY_NAME_AND_TYPE);
				msg_add_message_ptr(
					0, g_flight_text_scratch_buffer);
				msg_emit_in_flight_message(
					IFMSG_398_YOU_WERE_KILLED_BY_ARG,
					g_local_player);
			}
		}
	}
	g_players[player_idx].awaiting_new_craft = 1;
	XVT_LOG_DEBUG(
		"player.awaiting_craft slot=%d object=%d source=%d source_type=%d by=%d map=%d predicted=%d",
		player_idx, g_players[player_idx].object_index,
		source_object_index >= UINT16_MAX ? -1
						  : (int)source_object_index,
		source_object_index <
				(unsigned int)
					g_active_region_craft_object_slot_end
			? (int)g_object_table[source_object_index].object_type
			: -1,
		source_player_idx, g_players[player_idx].map_camera_state != 0,
		g_flight_sim_side_effects_suppressed);
}

/* Copies a name for the object into text and makes it message argument slot. A
 * hostile craft the local player's team has not identified (unless
 * locate_players_enabled) is named by hud_format_object_display_name style 1 in a
 * melee, else 3; a craft a player flies, by that player's name; anything else
 * by style 3. Does not check the size of text. */
// FUNCTION: XVT 0x484C50
void player_append_kill_message_actor_name(int slot, char *text,
					   int object_index)
{
	struct object_record *object = &g_object_table[object_index];
	int player_owner_idx = object->player_owner_idx;
	struct craft_data *craft = object->mobj->p_craft;
	int is_enemy;
	if (g_flight_mission_state.locate_players_enabled != 0 ||
	    craft->identified_order_by_team[(uint16_t)g_players[g_local_player]
						    .team] != 0) {
		is_enemy = 0;
	} else {
		int player_team = (uint16_t)g_players[g_local_player].team;
		int team = g_mission_flight_groups
				   [g_object_table[(uint16_t)object_index]
					    .flight_group_idx]
					   .fg.team;
		if (team == player_team) {
			is_enemy = 0;
		} else {
			is_enemy =
				g_mission_teams[player_team].allies[team] == 0;
		}
	}
	char *player_name;
	if (is_enemy == 1) {
		if (g_mission_header.mission_type == MISSION_TYPE_MELEE) {
			hud_format_object_display_name((uint16_t)object_index,
						       1);
			player_name = g_flight_text_scratch_buffer;
		} else {
			hud_format_object_display_name((uint16_t)object_index,
						       3);
			player_name = g_flight_text_scratch_buffer;
		}
	} else if (player_owner_idx != -1) {
		player_name = net_session_get_player_name(player_owner_idx);
	} else {
		hud_format_object_display_name((uint16_t)object_index, 3);
		player_name = g_flight_text_scratch_buffer;
	}
	strcpy(text, player_name);
	msg_add_message_ptr((uint16_t)slot, text);
}

/* Sets trig2's polar results for the direction from the player's craft to an
 * object or mission point reference, or from the player's camera when the
 * player has no craft. Writes g_world_loc_x, g_world_loc_y and g_world_loc_z. */
// FUNCTION: XVT 0x485000
void player_compute_polar_to_object_ref(int player_idx, unsigned int object_ref)
{
	unsigned int object_index = g_players[player_idx].object_index;
	if (object_index == UINT32_MAX) {
		mission_resolve_object_or_mission_point_world_loc(object_ref,
								  0);
		int delta_x = g_players[player_idx].view_state.camera_world_x;
		int delta_y = g_players[player_idx].view_state.camera_world_y;
		int delta_z = g_players[player_idx].view_state.camera_world_z;
		delta_x = g_world_loc_x - delta_x;
		delta_y = g_world_loc_y - delta_y;
		delta_z = g_world_loc_z - delta_z;
		trig2_ctop(delta_x, delta_y, delta_z);
	} else {
		pai_object_ref_direction_to_object_ref(object_index,
						       object_ref);
	}
}

/* Takes the player out of the mission (participation_state 2). While any player
 * is still connected (participation_state 1), the player watches: map camera
 * fully open, craft list view, input blocked, camera at height 0x40000 with
 * camera_distance 0x40000, or over the current target with camera_distance 16
 * times its max_bounds_extent; no craft and no pending action; engine, chaff and
 * beam sound loops updated. With none connected, sets mission_end_pending. */
// FUNCTION: XVT 0x485080
void player_end_flight_participation(int player_idx)
{
	enum {
		PLAYER_CONNECTED = 1,
		PLAYER_OUT_OF_MISSION = 2,
		EXTERNAL_CAMERA_INITIAL_DISTANCE = 0x40000,
		CAMERA_TARGET_EXTENT_SCALE = 16,
	};

	g_players[player_idx].participation_state = PLAYER_OUT_OF_MISSION;
	unsigned int active_player_count = 0;
	for (unsigned int player_index = 0;
	     player_index < sizeof(g_players) / sizeof(g_players[0]);
	     ++player_index) {
		if (g_players[player_index].participation_state ==
		    PLAYER_CONNECTED) {
			++active_player_count;
		}
	}
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO("player.out_of_mission slot=%d flying=%u tick=%d",
			     player_idx, active_player_count, g_game_time);
		XVT_LOG_INFO("battle.player_out who=%d flying=%u tick=%d",
			     player_idx, active_player_count, g_game_time);
	}
	if (active_player_count != 0) {
		g_players[player_idx].map_camera_state = UINT8_MAX;
		hud_set_hud_view_state(HUD_VIEW_CRAFT_LIST, player_idx);
		g_players[player_idx].view_state.player_input_blocked = 1;
		g_players[player_idx].view_state.external_camera_active = 1;
		g_players[player_idx].view_state.camera_distance =
			EXTERNAL_CAMERA_INITIAL_DISTANCE;
		g_players[player_idx].view_state.camera_focus_obj_idx =
			UINT16_MAX;
		g_players[player_idx].view_state.aim_target_idx = UINT16_MAX;
		g_players[player_idx].view_state.camera_world_z =
			EXTERNAL_CAMERA_INITIAL_DISTANCE;
		if ((uint16_t)g_players[player_idx].current_target_object_idx !=
		    UINT16_MAX) {
			g_players[player_idx].view_state.camera_world_x =
				g_object_table
					[(uint16_t)g_players[player_idx]
						 .current_target_object_idx]
						.world_x;
			g_players[player_idx].view_state.camera_world_y =
				g_object_table
					[(uint16_t)g_players[player_idx]
						 .current_target_object_idx]
						.world_y;
			g_players[player_idx].view_state.camera_distance =
				CAMERA_TARGET_EXTENT_SCALE *
				g_object_type_table
					[g_object_table
						 [(uint16_t)g_players[player_idx]
							  .current_target_object_idx]
							 .object_type]
						.max_bounds_extent;
		}
		g_players[player_idx].awaiting_new_craft = 0;
		g_players[player_idx].object_index = -1;
		g_players[player_idx].pending_action_timer = 0;
		g_players[player_idx].pending_action_id = 0;
		fsfx_update_player_engine_loop();
		fsfx_update_chaff_loop();
		fsfx_update_beam_effect_loops();
	} else {
		g_flight_mission_state.mission_end_pending = 1;
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_INFO(
				"flight.mission_ending reason=\"last_player_out\" slot=%d tick=%d",
				player_idx, g_game_time);
		}
	}
}

/* For a player other than the local one, says that player has no more craft and
 * is out (IFMSG_381), then, while the local player is still connected, how many
 * connected players remain (IFMSG_382) or that the local player is the only one
 * (IFMSG_383). */
// FUNCTION: XVT 0x4851D0
void player_emit_remote_player_departed_messages(int player_idx)
{
	if (g_local_player != player_idx) {
		msg_add_message_ptr(0, net_session_get_player_name(player_idx));
		msg_emit_in_flight_message(
			IFMSG_381_ARG_HAS_NO_MORE_CRAFT_AND_IS_OUT_OF_THE_MISSION,
			g_local_player);
		int connected_count = 0;
		for (int player_index = 0; player_index < 8; ++player_index) {
			if (g_players[player_index].participation_state == 1) {
				++connected_count;
			}
		}
		if (g_players[g_local_player].participation_state == 1) {
			if (connected_count == 1) {
				msg_emit_in_flight_message(
					IFMSG_383_YOU_ARE_THE_ONLY_PLAYER_LEFT,
					g_local_player);
			} else {
				g_msg_arg_table[0] = (uint16_t)connected_count;
				msg_emit_in_flight_message(
					IFMSG_382_THERE_ARE_ARG_PLAYERS_LEFT_INCLUDING_YOURSELF,
					g_local_player);
			}
		}
	}
}

/* Drops the player's current target when its slot is empty, it is an explosion,
 * or it has mobile data and an active decoy beam; also, without the map camera,
 * when the player's targeting computer is out. On dropping it, sets
 * target_cycle_start to the old target and targeting_state 0, and from the target
 * camera view returns to the forward view on the player's craft with IFMSG_224
 * (for the local player also setting g_hud_cached_target_object_idx to -2 and
 * g_render_object_ref_flags to 0). Does not check that the player has a craft
 * before testing its targeting computer. */
// FUNCTION: XVT 0x485270
void player_validate_current_targets(int player_idx)
{
	enum {
		OBJECT_GENUS_EXPLOSION = 13,
	};

	uint16_t current_target_object_idx =
		(uint16_t)g_players[player_idx].current_target_object_idx;
	if (current_target_object_idx == UINT16_MAX) {
		return;
	}

	struct object_record *target_object =
		&g_object_table[current_target_object_idx];
	if (target_object->mobj != NULL) {
		if (target_object->object_type == 0 ||
		    target_object->genus_id == OBJECT_GENUS_EXPLOSION ||
		    object_has_active_decoy_beam(current_target_object_idx) !=
			    0) {
			g_players[player_idx].current_target_object_idx = -1;
		}
	} else if (target_object->object_type == 0 ||
		   target_object->genus_id == OBJECT_GENUS_EXPLOSION) {
		g_players[player_idx].current_target_object_idx = -1;
	}

	if (g_players[player_idx].map_camera_state == 0 &&
	    (g_object_table[g_players[player_idx].object_index]
		     .mobj->p_craft->working_subsystems &
	     CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) == 0) {
		g_players[player_idx].current_target_object_idx = -1;
	}
	if ((uint16_t)g_players[player_idx].current_target_object_idx ==
	    UINT16_MAX) {
		XVT_LOG_DEBUG(
			"player.target_dropped slot=%d target=%d reason=\"%s\" camera=%d predicted=%d",
			player_idx, (int)current_target_object_idx,
			target_object->object_type == 0 ? "empty"
			: target_object->genus_id == OBJECT_GENUS_EXPLOSION
				? "explosion"
			: (g_players[player_idx].map_camera_state == 0 &&
			   (g_object_table[g_players[player_idx].object_index]
				    .mobj->p_craft->working_subsystems &
			    CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) == 0)
				? "computer"
				: "decoy",
			g_players[player_idx].view_state.hud_state_live ==
				HUD_VIEW_TARGET_CAMERA,
			g_flight_sim_side_effects_suppressed);
		g_players[player_idx].target_cycle_start =
			(int16_t)current_target_object_idx;
		g_players[player_idx].targeting_state = 0;
		if (g_players[player_idx].view_state.hud_state_live ==
		    HUD_VIEW_TARGET_CAMERA) {
			g_players[player_idx]
				.view_state.external_camera_active = 0;
			int local_player = g_local_player;
			g_players[player_idx].view_state.target_camera_active =
				0;
			g_players[player_idx].view_state.camera_focus_obj_idx =
				(uint16_t)g_players[player_idx].object_index;
			g_players[player_idx].view_state.player_input_blocked =
				0;
			if (player_idx == local_player) {
				g_hud_cached_target_object_idx = -2;
				g_render_object_ref_flags = 0;
			}
			hud_set_hud_view_state(HUD_VIEW_FORWARD, player_idx);
			g_players[player_idx].view_state.hud_aim_x = 0;
			g_players[player_idx].view_state.hud_aim_y = 0;
			msg_emit_in_flight_message(
				IFMSG_224_THREAT_DISPLAY_TARGET_NO_LONGER_AVAILABLE,
				player_idx);
		}
	}
}

/* Runs player_validate_current_targets for every participating player. */
// FUNCTION: XVT 0x4853C0
void player_validate_all_current_targets(void)
{
	for (unsigned int player_idx = 0;
	     player_idx < sizeof(g_players) / sizeof(g_players[0]);
	     ++player_idx) {
		if (g_players[player_idx].participation_state != 0) {
			player_validate_current_targets((int)player_idx);
		}
	}
}

/* Returns 1 when a craft of one of the player's flight groups in the active
 * region is not breaking up, exploding or entering hyperspace; else 0. */
// FUNCTION: XVT 0x4853F0
int player_has_available_owned_craft(int player_idx)
{
	int object_index = -1;
	unsigned int remaining_objects = g_active_region_craft_object_slot_end -
					 g_active_region_object_slot_start;
	if (remaining_objects != 0) {
		do {
			++object_index;
			if (object_index >=
			    g_active_region_craft_object_slot_end) {
				object_index =
					g_active_region_object_slot_start;
			}
			struct object_record *object =
				&g_object_table[object_index];
			if (object->object_type != 0) {
				struct craft_data *craft =
					object->mobj->p_craft;
				if (g_mission_flight_groups
					    [object->flight_group_idx]
						    .player_owner_idx ==
				    player_idx) {
					uint8_t object_kind =
						craft->object_kind;
					if (object_kind !=
						    CRAFT_OBJECT_KIND_BREAKING_UP &&
					    object_kind !=
						    CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE &&
					    object_kind !=
						    CRAFT_OBJECT_KIND_EXPLODING) {
						break;
					}
				}
			}
			--remaining_objects;
		} while (remaining_objects != 0);
	}
	return remaining_objects != 0;
}

/* Keeps players in or out of the mission. A player awaiting a new craft whose
 * bound craft is gone (slot empty or signature changed) has
 * mission_process_flight_group_wave_completion run for the bound group and is bound
 * to another of their craft (IFMSG_290 for the local player), or with none left
 * leaves the mission with a notice. A watching player (map camera) still in the
 * mission with no craft left after the same processing leaves too. Then each
 * participating player flagged in g_player_abort_flags quits: the craft goes to
 * the AI, participation_state becomes 0, the active player count is updated, a
 * player other than the host is marked gone on the network, IFMSG_380 is shown,
 * and for the local player mission_end_pending is set and, as host, the session
 * abort is broadcast. Skips craft changes and quits while
 * g_flight_sim_side_effects_suppressed is set. */
// FUNCTION: XVT 0x485490
void player_update_participation_state(void)
{
	enum {
		PLAYER_OUT_OF_MISSION = 2,
	};

	unsigned int player_idx;

	for (player_idx = 0;
	     player_idx < sizeof(g_players) / sizeof(g_players[0]);
	     ++player_idx) {
		struct player_data *player = &g_players[player_idx];
		if (player->participation_state != 0) {
			if (player->awaiting_new_craft != 0) {
				if (g_flight_sim_side_effects_suppressed == 0 &&
				    player->object_index != -1) {
					struct object_record *object =
						&g_object_table
							[player->object_index];
					if (object->object_type == 0 ||
					    player->bound_object_signature !=
						    object->object_signature) {
						XVT_LOG_DEBUG(
							"player.craft_gone slot=%d object=%d fg=%d empty=%d predicted=%d",
							(int)player_idx,
							player->object_index,
							(int)player
								->bound_flight_group_idx,
							object->object_type ==
								0,
							g_flight_sim_side_effects_suppressed);
						mission_process_flight_group_wave_completion(
							player->bound_flight_group_idx);
						if (player_bind_to_available_craft(
							    (int)player_idx,
							    UINT32_MAX, 0,
							    0) != 0) {
							player_end_flight_participation(
								(int)player_idx);
							player_emit_remote_player_departed_messages(
								(int)player_idx);
						} else if (g_local_player ==
							   (int)player_idx) {
							msg_emit_local_player_craft_message(
								IFMSG_290_PREVIOUS_CRAFT_DESTROYED_NOW_PILOTING_ARG_ARG_ARG);
						}
					}
				}
			} else if (player->map_camera_state != 0 &&
				   player->participation_state !=
					   PLAYER_OUT_OF_MISSION &&
				   g_flight_sim_side_effects_suppressed == 0 &&
				   player_has_available_owned_craft(
					   (int)player_idx) == 0) {
				mission_process_flight_group_wave_completion(
					player->bound_flight_group_idx);
				if (player_has_available_owned_craft(
					    (int)player_idx) == 0) {
					XVT_LOG_DEBUG(
						"player.map_without_craft slot=%d fg=%d predicted=%d",
						(int)player_idx,
						(int)player
							->bound_flight_group_idx,
						g_flight_sim_side_effects_suppressed);
					player_end_flight_participation(
						(int)player_idx);
					player_emit_remote_player_departed_messages(
						(int)player_idx);
				}
			}
		}
	}

	if (g_flight_sim_side_effects_suppressed == 0) {
		for (player_idx = 0;
		     player_idx < sizeof(g_player_abort_flags) /
					  sizeof(g_player_abort_flags[0]);
		     ++player_idx) {
			if (g_player_abort_flags[player_idx] != 0 &&
			    g_players[player_idx].participation_state != 0) {
				XVT_LOG_INFO(
					"battle.player_left who=%d tick=%d",
					(int)player_idx, g_game_time);
				if (g_players[player_idx].object_index != -1) {
					player_unbind_from_current_craft(
						(int)player_idx, 0, 1);
				}
				if (g_local_player == (int)player_idx) {
					g_flight_mission_state
						.mission_end_pending = 1;
					XVT_LOG_INFO(
						"flight.mission_ending reason=\"local_aborted\" slot=%d tick=%d",
						(int)player_idx, g_game_time);
				}
				g_players[player_idx].participation_state = 0;
				flight_update_active_player_count();
				XVT_LOG_DEBUG(
					"player.quit_flight slot=%d player=%u local=%d active=%d predicted=%d",
					(int)player_idx,
					(unsigned)g_players[player_idx]
						.network.direct_play_id,
					g_local_player == (int)player_idx,
					g_active_flight_player_count,
					g_flight_sim_side_effects_suppressed);
				if (net_session_get_host_dplay_id() !=
				    g_players[player_idx]
					    .network.direct_play_id) {
					flight_net_mark_pilot_network_player_left(
						(int)player_idx);
				}
				msg_add_message_ptr(0,
						    net_session_get_player_name(
							    (int)player_idx));
				msg_emit_in_flight_message(
					IFMSG_380_ARG_HAS_QUIT_THE_MISSION,
					g_local_player);
				if (g_local_player == (int)player_idx &&
				    net_session_is_local_host() != 0) {
					flight_net_broadcast_host_session_abort();
				}
			}
		}
	}
}

/* Returns the nearest hostile object for the player, or UINT16_MAX: in the
 * craft slots, an active or arriving craft with a working subsystem whose
 * mobile team differs from the player's and whose flight group's team is not
 * allied, other than explosions, starships, freighters, platforms and craft
 * with an active decoy beam; in the static slots, a hostile mine with a nonzero
 * type_specific_word. Despite the name, any craft but starships, freighters and
 * platforms counts, and mines too. Skips the player's craft and
 * excluded_object_idx. Writes trig2's polar results. */
// FUNCTION: XVT 0x485660
int player_find_nearest_enemy_fighter(int player_idx, int excluded_object_idx)
{
	int is_enemy;

	uint32_t nearest_distance = UINT32_MAX;
	int nearest_object_idx = UINT16_MAX;
	for (int16_t object_idx = g_active_region_object_slot_start;
	     object_idx < g_active_region_craft_object_slot_end; ++object_idx) {
		struct object_record *object = &g_object_table[object_idx];
		if (object->object_type != 0 &&
		    g_players[player_idx].object_index != object_idx &&
		    excluded_object_idx != object_idx) {
			int player_team = (uint16_t)g_players[player_idx].team;
			if (object->mobj->team != player_team) {
				int team =
					g_mission_flight_groups
						[g_object_table[(uint16_t)
									object_idx]
							 .flight_group_idx]
							.fg.team;
				if (team == player_team) {
					is_enemy = 0;
				} else {
					is_enemy = g_mission_teams[player_team]
							   .allies[team] == 0;
				}
				if (is_enemy) {
					uint8_t genus_id = object->genus_id;
					if (genus_id != CRAFT_GENUS_EXPLOSION &&
					    genus_id != CRAFT_GENUS_STARSHIP &&
					    genus_id != CRAFT_GENUS_FREIGHTER &&
					    genus_id != CRAFT_GENUS_PLATFORM &&
					    object_has_active_decoy_beam(
						    object_idx) == 0) {
						struct craft_data *craft =
							g_object_table[object_idx]
								.mobj->p_craft;
						if (craft->working_subsystems !=
						    0) {
							uint8_t object_kind =
								craft->object_kind;
							if (object_kind ==
								    CRAFT_OBJECT_KIND_ACTIVE ||
							    object_kind ==
								    CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE) {
								player_compute_polar_to_object_ref(
									player_idx,
									object_idx);
								if ((uint32_t)
									    trig2_polardistance <
								    nearest_distance) {
									nearest_object_idx =
										object_idx;
									nearest_distance =
										trig2_polardistance;
								}
							}
						}
					}
				}
			}
		}
	}

	for (uint16_t static_object_idx = g_region_main_object_slot_end;
	     (int)(g_region_main_object_slot_end +
		   g_region_static_object_slot_count) >
	     (int16_t)static_object_idx;
	     ++static_object_idx) {
		struct object_record *static_object =
			&g_object_table[(int16_t)static_object_idx];
		if (static_object->object_type != 0 &&
		    static_object->genus_id == CRAFT_GENUS_MINE &&
		    excluded_object_idx != (int16_t)static_object_idx &&
		    static_object->type_specific_word != 0) {
			int static_object_team =
				g_mission_flight_groups
					[g_object_table[static_object_idx]
						 .flight_group_idx]
						.fg.team;
			int static_player_team =
				(uint16_t)g_players[player_idx].team;
			if (static_player_team == static_object_team) {
				is_enemy = 0;
			} else {
				is_enemy =
					g_mission_teams[static_player_team]
						.allies[static_object_team] ==
					0;
			}
			if (is_enemy == 1) {
				player_compute_polar_to_object_ref(
					player_idx, (int16_t)static_object_idx);
				if ((uint32_t)trig2_polardistance <
				    nearest_distance) {
					nearest_object_idx =
						(int16_t)static_object_idx;
					nearest_distance = trig2_polardistance;
				}
			}
		}
	}
	return nearest_object_idx;
}

/* Answers the player's hyperspace key, unless sim side effects are suppressed.
 * With a hyperdrive installed: in the proving grounds, ends the mission and
 * takes the player out; with it working and no working Interdictor or modified
 * strike cruiser of another IFF in the craft slots, starts the jump (IFMSG_106,
 * hyperspace_phase 1 at 0 ticks, forward view, throttle 0, an X-wing's or
 * B-wing's S-foils closing with FLIGHT_SOUND_S_FOIL, IFMSG_113 to the other
 * players); with one present, IFMSG_109; with the hyperdrive damaged,
 * IFMSG_086. Without a hyperdrive, names the player's flight group's departure
 * and alternate motherships that are present (IFMSG_110 or IFMSG_111). Does not
 * check that craft is the player's. */
// FUNCTION: XVT 0x485900
void player_handle_hyperspace_command(struct craft_data *craft,
				      unsigned int player_idx)
{
	enum {
		PLAYER_OUT_OF_MISSION = 2,
		S_FOIL_CLOSING_MASK = 1,
		S_FOIL_CLOSED_MASK = 2,
		HYPERDRIVE_SYSTEM_NAME_MESSAGE_ARG = 99,
		DAMAGED_SYSTEM_STATE_MESSAGE_ARG = 87,
	};

	if (g_flight_sim_side_effects_suppressed == 0) {
		if ((craft->system_flags & CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE) !=
		    0) {
			if (g_flight_mission_state
				    .proving_grounds_mode_active != 0) {
				g_flight_mission_state.mission_end_pending = 1;
				g_players[player_idx].participation_state =
					PLAYER_OUT_OF_MISSION;
				XVT_LOG_INFO(
					"flight.mission_ending reason=\"proving_grounds_hyperspace\" slot=%d tick=%d",
					(int)player_idx, g_game_time);
			} else if ((craft->working_subsystems &
				    CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE) != 0) {
				int16_t interdictor_present = 0;
				for (int16_t scan_object_idx =
					     g_active_region_object_slot_start;
				     scan_object_idx <
				     g_active_region_craft_object_slot_end;
				     ++scan_object_idx) {
					struct object_record *object =
						&g_object_table
							[scan_object_idx];

					if ((object->object_type ==
						     CRAFT_SPECIES_INTERDICTOR ||
					     object->object_type ==
						     CRAFT_SPECIES_MODIFIED_STRIKE_CRUISER) &&
					    (uint16_t)g_players[player_idx]
							    .iff !=
						    (uint8_t)
							    object->mobj->iff &&
					    object->mobj->p_craft
							    ->working_subsystems !=
						    0) {
						interdictor_present = 1;
					}
				}

				if (interdictor_present != 0) {
					XVT_LOG_DEBUG(
						"player.hyperspace_refused slot=%d reason=\"interdictor\" predicted=%d",
						(int)player_idx,
						g_flight_sim_side_effects_suppressed);
					msg_emit_in_flight_message(
						IFMSG_109_INTERDICTOR_PREVENTS_HYPERDRIVE_UNIT_FROM_FUNCTIONING,
						player_idx);
				} else {
					if (g_local_player == (int)player_idx) {
						hud_clear_ready_message_queue();
					}
					msg_emit_in_flight_message(
						IFMSG_106_PREPARING_FOR_JUMP_TO_LIGHT_SPEED,
						player_idx);
					g_players[player_idx].hyperspace_phase =
						1;
					g_players[player_idx]
						.hyperspace_runtime
						.phase_elapsed_ticks = 0;
					g_players[player_idx]
						.view_state
						.target_camera_active = 0;
					g_players[player_idx]
						.view_state
						.external_camera_active = 0;
					g_players[player_idx]
						.view_state
						.player_input_blocked = 0;
					g_players[player_idx]
						.view_state
						.camera_focus_obj_idx =
						(uint16_t)g_players[player_idx]
							.object_index;
					hud_set_hud_view_state(HUD_VIEW_FORWARD,
							       player_idx);
					g_players[player_idx]
						.view_state.hud_aim_x = 0;
					g_players[player_idx]
						.view_state.hud_aim_y = 0;
					craft->throttle_speed = 0;

					uint8_t object_type =
						g_object_table
							[g_players[player_idx]
								 .object_index]
								.object_type;
					if ((object_type ==
						     CRAFT_SPECIES_X_WING ||
					     object_type ==
						     CRAFT_SPECIES_B_WING) &&
					    (craft->s_foil_state &
					     S_FOIL_CLOSED_MASK) == 0) {
						craft->s_foil_state |=
							S_FOIL_CLOSED_MASK;
						craft->s_foil_state |=
							S_FOIL_CLOSING_MASK;
						fsfx_play_sound(
							FLIGHT_SOUND_S_FOIL, -1,
							player_idx);
					}
					XVT_LOG_INFO(
						"player.hyperspace_started slot=%d object=%d fg=%d tick=%d",
						(int)player_idx,
						g_players[player_idx]
							.object_index,
						(int)g_object_table
							[g_players[player_idx]
								 .object_index]
								.flight_group_idx,
						g_game_time);

					if (g_local_player != (int)player_idx) {
						msg_add_message_ptr(
							0,
							net_session_get_player_name(
								(int)player_idx));
						msg_emit_in_flight_message(
							IFMSG_113_ARG_IS_INITIATING_HYPERJUMP,
							g_local_player);
					}
				}
			} else {
				XVT_LOG_DEBUG(
					"player.hyperspace_refused slot=%d reason=\"damaged\" predicted=%d",
					(int)player_idx,
					g_flight_sim_side_effects_suppressed);
				g_msg_arg_table[0] =
					HYPERDRIVE_SYSTEM_NAME_MESSAGE_ARG;
				g_msg_arg_table[1] =
					DAMAGED_SYSTEM_STATE_MESSAGE_ARG;
				msg_emit_in_flight_message(
					IFMSG_086_ARG_SYSTEM_IS_ARG,
					player_idx);
			}
		} else {
			uint16_t departure_mothership_obj_idx = UINT16_MAX;
			uint16_t alternate_mothership_obj_idx =
				departure_mothership_obj_idx;
			for (uint16_t object_idx = (uint16_t)
				     g_active_region_object_slot_start;
			     object_idx < g_active_region_craft_object_slot_end;
			     ++object_idx) {
				if (g_mission_flight_groups
					    [g_object_table
						     [g_players[player_idx]
							      .object_index]
							     .flight_group_idx]
						    .fg.departure_method != 0) {
					if (g_object_table[object_idx]
							    .object_type !=
						    CRAFT_SPECIES_UNKNOWN &&
					    g_mission_flight_groups
							    [g_object_table
								     [g_players[player_idx]
									      .object_index]
									     .flight_group_idx]
								    .fg
								    .departure_mothership ==
						    g_object_table[object_idx]
							    .flight_group_idx) {
						departure_mothership_obj_idx =
							object_idx;
					}
				}
				if (g_mission_flight_groups
					    [g_object_table
						     [g_players[player_idx]
							      .object_index]
							     .flight_group_idx]
						    .fg
						    .alternate_mothership_used !=
				    0) {
					if (g_object_table[object_idx]
							    .object_type !=
						    CRAFT_SPECIES_UNKNOWN &&
					    g_mission_flight_groups
							    [g_object_table
								     [g_players[player_idx]
									      .object_index]
									     .flight_group_idx]
								    .fg
								    .alternate_mothership ==
						    g_object_table[object_idx]
							    .flight_group_idx) {
						alternate_mothership_obj_idx =
							object_idx;
					}
				}
			}
			XVT_LOG_DEBUG(
				"player.hyperspace_unavailable slot=%d departure=%d alternate=%d predicted=%d",
				(int)player_idx,
				departure_mothership_obj_idx == UINT16_MAX
					? -1
					: (int)departure_mothership_obj_idx,
				alternate_mothership_obj_idx == UINT16_MAX
					? -1
					: (int)alternate_mothership_obj_idx,
				g_flight_sim_side_effects_suppressed);

			if (departure_mothership_obj_idx != UINT16_MAX &&
			    alternate_mothership_obj_idx != UINT16_MAX) {
				msg_format_object_name(
					departure_mothership_obj_idx, 0,
					g_flight_text_scratch_buffer);
				msg_add_message_ptr(
					0, g_flight_text_scratch_buffer);
				msg_format_object_name(
					alternate_mothership_obj_idx, 0,
					g_flight_secondary_object_name_buffer);
				msg_add_message_ptr(
					1,
					g_flight_secondary_object_name_buffer);
				msg_emit_in_flight_message(
					IFMSG_111_NO_HYPERDRIVE_RETURN_TO_ARG_OR_TO_ARG,
					player_idx);
			} else if (departure_mothership_obj_idx != UINT16_MAX) {
				msg_format_object_name(
					departure_mothership_obj_idx, 0,
					g_flight_text_scratch_buffer);
				msg_add_message_ptr(
					0, g_flight_text_scratch_buffer);
				msg_emit_in_flight_message(
					IFMSG_110_NO_HYPERDRIVE_RETURN_TO_ARG,
					player_idx);
			} else if (alternate_mothership_obj_idx != UINT16_MAX) {
				msg_format_object_name(
					alternate_mothership_obj_idx, 0,
					g_flight_text_scratch_buffer);
				msg_add_message_ptr(
					0, g_flight_text_scratch_buffer);
				msg_emit_in_flight_message(
					IFMSG_110_NO_HYPERDRIVE_RETURN_TO_ARG,
					player_idx);
			}
		}
	}
}
