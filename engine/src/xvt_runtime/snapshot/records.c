#include "xvt_runtime/snapshot/records.h"

#include <stddef.h>
#include <string.h>

struct xvt_snapshot_field {
	size_t native_offset;
	size_t record_offset;
	size_t size;
};

/* One table entry: where a field sits in the live struct, where it sits in the
 * packed snapshot record, and its size in the record. Both structs name the
 * field the same way. Each table below defines a shorter macro for its own
 * pair of structs and undefines it after the table. */
#define SNAPSHOT_FIELD(native, record, field)                                  \
	{offsetof(native, field), offsetof(record, field),                     \
	 sizeof(((record *)0)->field)}

/* Copy declared fields only: native padding never enters a snapshot or checksum. */
static void xvt_snapshot_copy_fields(void *record, void *live,
				     const struct xvt_snapshot_field *fields,
				     size_t count, int restore)
{
	for (size_t i = 0; i < count; ++i) {
		void *disk = (uint8_t *)record + fields[i].record_offset;
		void *native = (uint8_t *)live + fields[i].native_offset;
		if (restore) {
			memcpy(native, disk, fields[i].size);
		} else {
			memcpy(disk, native, fields[i].size);
		}
	}
}

#define OBJECT_RECORD_FIELD(field)                                             \
	SNAPSHOT_FIELD(struct object_record,                                   \
		       struct xvt_snapshot_object_record, field)
static const struct xvt_snapshot_field g_object_record_fields[] = {
	OBJECT_RECORD_FIELD(object_signature),
	OBJECT_RECORD_FIELD(genus_id),
	OBJECT_RECORD_FIELD(object_type),
	OBJECT_RECORD_FIELD(world_x),
	OBJECT_RECORD_FIELD(world_y),
	OBJECT_RECORD_FIELD(world_z),
	OBJECT_RECORD_FIELD(yaw),
	OBJECT_RECORD_FIELD(pitch),
	OBJECT_RECORD_FIELD(roll),
	OBJECT_RECORD_FIELD(flight_group_idx),
	OBJECT_RECORD_FIELD(type_specific_word),
	OBJECT_RECORD_FIELD(type_specific_byte),
	OBJECT_RECORD_FIELD(player_owner_idx),
};
#undef OBJECT_RECORD_FIELD

void xvt_snapshot_encode_object_record(
	struct xvt_snapshot_object_record *record,
	const struct object_record *live)
{
	xvt_snapshot_copy_fields((void *)record, (void *)live,
				 g_object_record_fields,
				 sizeof(g_object_record_fields) /
					 sizeof(g_object_record_fields[0]),
				 0);
	record->mobj =
		live->mobj
			? (uint32_t)((live->mobj - g_mobile_object_pool_base) *
					     sizeof(struct
						    xvt_snapshot_mobile_object) +
				     1)
			: 0;
}

void xvt_snapshot_decode_object_record(
	struct object_record *live,
	const struct xvt_snapshot_object_record *record)
{
	xvt_snapshot_copy_fields((void *)record, (void *)live,
				 g_object_record_fields,
				 sizeof(g_object_record_fields) /
					 sizeof(g_object_record_fields[0]),
				 1);
	live->mobj =
		record->mobj
			? &g_mobile_object_pool_base
				  [(record->mobj - 1) /
				   sizeof(struct xvt_snapshot_mobile_object)]
			: NULL;
}

#define MOBILE_OBJECT_FIELD(field)                                             \
	SNAPSHOT_FIELD(struct mobile_object,                                   \
		       struct xvt_snapshot_mobile_object, field)
static const struct xvt_snapshot_field g_mobile_object_fields[] = {
	MOBILE_OBJECT_FIELD(family),
	MOBILE_OBJECT_FIELD(effect_size),
	MOBILE_OBJECT_FIELD(sim_state_timestamp),
	MOBILE_OBJECT_FIELD(prev_world_x),
	MOBILE_OBJECT_FIELD(prev_world_y),
	MOBILE_OBJECT_FIELD(prev_world_z),
	MOBILE_OBJECT_FIELD(proximity_list.count),
	MOBILE_OBJECT_FIELD(proximity_list.contact_ticks),
	MOBILE_OBJECT_FIELD(proximity_list.obj_idx),
	MOBILE_OBJECT_FIELD(proximity_list.rebuild_ticks),
	MOBILE_OBJECT_FIELD(roll_impulse_rate),
	MOBILE_OBJECT_FIELD(speed),
	MOBILE_OBJECT_FIELD(speed_remainder),
	MOBILE_OBJECT_FIELD(damage_amount),
	MOBILE_OBJECT_FIELD(lifetime_timer),
	MOBILE_OBJECT_FIELD(seconds_alive),
	MOBILE_OBJECT_FIELD(source_obj_idx),
	MOBILE_OBJECT_FIELD(source_object_type),
	MOBILE_OBJECT_FIELD(iff),
	MOBILE_OBJECT_FIELD(team),
	MOBILE_OBJECT_FIELD(node_switch_index),
	MOBILE_OBJECT_FIELD(move_vector_dirty),
	MOBILE_OBJECT_FIELD(move_x),
	MOBILE_OBJECT_FIELD(move_y),
	MOBILE_OBJECT_FIELD(move_z),
	MOBILE_OBJECT_FIELD(orient_matrix_dirty),
	MOBILE_OBJECT_FIELD(cached_fwd_x),
	MOBILE_OBJECT_FIELD(cached_fwd_y),
	MOBILE_OBJECT_FIELD(cached_fwd_z),
	MOBILE_OBJECT_FIELD(cached_side_x),
	MOBILE_OBJECT_FIELD(cached_side_y),
	MOBILE_OBJECT_FIELD(cached_side_z),
	MOBILE_OBJECT_FIELD(cached_up_x),
	MOBILE_OBJECT_FIELD(cached_up_y),
	MOBILE_OBJECT_FIELD(cached_up_z),
};
#undef MOBILE_OBJECT_FIELD

void xvt_snapshot_encode_mobile_object(
	struct xvt_snapshot_mobile_object *record,
	const struct mobile_object *live)
{
	xvt_snapshot_copy_fields((void *)record, (void *)live,
				 g_mobile_object_fields,
				 sizeof(g_mobile_object_fields) /
					 sizeof(g_mobile_object_fields[0]),
				 0);
	record->p_warhead_guidance =
		live->p_warhead_guidance
			? (uint32_t)((live->p_warhead_guidance -
				      g_projectile_guidance_states) *
					     sizeof(struct
						    warhead_guidance_state) +
				     1)
			: 0;
	record->p_craft =
		live->p_craft
			? (uint32_t)((live->p_craft - g_craft_data_pool_base) *
					     sizeof(struct
						    xvt_snapshot_craft_data) +
				     1)
			: 0;
	record->p_char_data =
		live->p_char_data
			? (uint32_t)((live->p_char_data -
				      g_mobile_object_char_data_pool) *
					     sizeof(struct
						    xvt_snapshot_mobile_object_char_data) +
				     1)
			: 0;
}

void xvt_snapshot_decode_mobile_object(
	struct mobile_object *live,
	const struct xvt_snapshot_mobile_object *record)
{
	xvt_snapshot_copy_fields((void *)record, (void *)live,
				 g_mobile_object_fields,
				 sizeof(g_mobile_object_fields) /
					 sizeof(g_mobile_object_fields[0]),
				 1);
	live->p_warhead_guidance =
		record->p_warhead_guidance
			? &g_projectile_guidance_states
				  [(record->p_warhead_guidance - 1) /
				   sizeof(struct warhead_guidance_state)]
			: NULL;
	live->p_craft =
		record->p_craft
			? &g_craft_data_pool_base
				  [(record->p_craft - 1) /
				   sizeof(struct xvt_snapshot_craft_data)]
			: NULL;
	live->p_char_data =
		record->p_char_data
			? &g_mobile_object_char_data_pool
				  [(record->p_char_data - 1) /
				   sizeof(struct
					  xvt_snapshot_mobile_object_char_data)]
			: NULL;
}

#define CRAFT_DATA_FIELD(field)                                                \
	SNAPSHOT_FIELD(struct craft_data, struct xvt_snapshot_craft_data, field)
static const struct xvt_snapshot_field g_craft_data_fields[] = {
	CRAFT_DATA_FIELD(craft_index_in_group),
	CRAFT_DATA_FIELD(model_index),
	CRAFT_DATA_FIELD(leader_obj_idx),
	CRAFT_DATA_FIELD(unused006),
	CRAFT_DATA_FIELD(object_kind),
	CRAFT_DATA_FIELD(mission_accounting_done),
	CRAFT_DATA_FIELD(ai_skill),
	CRAFT_DATA_FIELD(unused00b),
	CRAFT_DATA_FIELD(pitch),
	CRAFT_DATA_FIELD(yaw),
	CRAFT_DATA_FIELD(breakup_pitch_rate),
	CRAFT_DATA_FIELD(breakup_yaw_rate),
	CRAFT_DATA_FIELD(beam_effect_accum),
	CRAFT_DATA_FIELD(s_foil_state),
	CRAFT_DATA_FIELD(ai_controller.current_order_slot),
	CRAFT_DATA_FIELD(ai_controller.order_progress),
	CRAFT_DATA_FIELD(ai_controller.skipped_to_order4),
	CRAFT_DATA_FIELD(ai_controller.running_plan_id),
	CRAFT_DATA_FIELD(ai_controller.current_plan_id),
	CRAFT_DATA_FIELD(ai_controller.waypoint_index),
	CRAFT_DATA_FIELD(ai_controller.saved_plan_id),
	CRAFT_DATA_FIELD(ai_controller.think_interval),
	CRAFT_DATA_FIELD(ai_controller.think_timer),
	CRAFT_DATA_FIELD(ai_controller.saved_rand_seed),
	CRAFT_DATA_FIELD(ai_controller.target_obj_idx),
	CRAFT_DATA_FIELD(ai_controller.target_signature),
	CRAFT_DATA_FIELD(ai_controller.target_component),
	CRAFT_DATA_FIELD(ai_controller.has_live_target),
	CRAFT_DATA_FIELD(ai_controller.aim_point_x),
	CRAFT_DATA_FIELD(ai_controller.aim_point_y),
	CRAFT_DATA_FIELD(ai_controller.aim_point_z),
	CRAFT_DATA_FIELD(ai_controller.candidate_target_idx),
	CRAFT_DATA_FIELD(ai_controller.escort_target_fg),
	CRAFT_DATA_FIELD(ai_controller.target_z_angle),
	CRAFT_DATA_FIELD(ai_controller.target_roll),
	CRAFT_DATA_FIELD(ai_controller.target_xy_angle),
	CRAFT_DATA_FIELD(ai_controller.maneuver_mode),
	CRAFT_DATA_FIELD(ai_controller.maneuver_phase),
	CRAFT_DATA_FIELD(ai_controller.maneuver_timer),
	CRAFT_DATA_FIELD(ai_controller.secondary_maneuver_timer),
	CRAFT_DATA_FIELD(carried_object_index),
	CRAFT_DATA_FIELD(carrier_obj_idx),
	CRAFT_DATA_FIELD(last_attacker_obj_idx),
	CRAFT_DATA_FIELD(last_hit_mission_second),
	CRAFT_DATA_FIELD(ai_flight.threat_obj_idx),
	CRAFT_DATA_FIELD(ai_flight.impact_obj_idx),
	CRAFT_DATA_FIELD(ai_flight.go_home_flag),
	CRAFT_DATA_FIELD(ai_flight.mission_aborted_flag),
	CRAFT_DATA_FIELD(ai_flight.depart_timer_flag),
	CRAFT_DATA_FIELD(ai_flight.depart_clock_hours),
	CRAFT_DATA_FIELD(ai_flight.depart_clock_minutes),
	CRAFT_DATA_FIELD(ai_flight.depart_clock_seconds),
	CRAFT_DATA_FIELD(ai_flight.warheads_fired_this_maneuver),
	CRAFT_DATA_FIELD(ai_flight.hits_this_maneuver),
	CRAFT_DATA_FIELD(ai_flight.boarded_accounting_done),
	CRAFT_DATA_FIELD(ai_flight.times_boarded),
	CRAFT_DATA_FIELD(ai_flight.docking_accounting_done),
	CRAFT_DATA_FIELD(ai_flight.docked_target_count),
	CRAFT_DATA_FIELD(ai_flight.docked_target_signatures),
	CRAFT_DATA_FIELD(ai_flight.max_speed_cache),
	CRAFT_DATA_FIELD(ai_flight.motion_scale),
	CRAFT_DATA_FIELD(ai_flight.climb_state),
	CRAFT_DATA_FIELD(ai_flight.dive_state),
	CRAFT_DATA_FIELD(ai_flight.pitch_rate),
	CRAFT_DATA_FIELD(ai_flight.pitch_accel),
	CRAFT_DATA_FIELD(ai_flight.pitch_state),
	CRAFT_DATA_FIELD(ai_flight.pitch_through_loop),
	CRAFT_DATA_FIELD(ai_flight.pitch_step_scale),
	CRAFT_DATA_FIELD(ai_flight.roll_rate),
	CRAFT_DATA_FIELD(ai_flight.roll_accel),
	CRAFT_DATA_FIELD(ai_flight.roll_state),
	CRAFT_DATA_FIELD(ai_flight.roll_step),
	CRAFT_DATA_FIELD(ai_flight.turn_rate),
	CRAFT_DATA_FIELD(ai_flight.turn_accel),
	CRAFT_DATA_FIELD(ai_flight.turn_state),
	CRAFT_DATA_FIELD(ai_flight.turn_step),
	CRAFT_DATA_FIELD(ai_flight.formation_type),
	CRAFT_DATA_FIELD(ai_flight.separation),
	CRAFT_DATA_FIELD(craft_ordinal),
	CRAFT_DATA_FIELD(push_accum_x),
	CRAFT_DATA_FIELD(push_accum_y),
	CRAFT_DATA_FIELD(push_accum_z),
	CRAFT_DATA_FIELD(throttle_speed),
	CRAFT_DATA_FIELD(engine_overdrive_off),
	CRAFT_DATA_FIELD(commanded_speed),
	CRAFT_DATA_FIELD(hull_damage),
	CRAFT_DATA_FIELD(system_damage_hull_threshold),
	CRAFT_DATA_FIELD(hull_max),
	CRAFT_DATA_FIELD(subsystem_damage),
	CRAFT_DATA_FIELD(damage_stats.last_system_hit_time),
	CRAFT_DATA_FIELD(damage_stats.damage_received_total),
	CRAFT_DATA_FIELD(damage_stats.damage_received_by_player_owned_craft),
	CRAFT_DATA_FIELD(damage_stats.damage_from_collision),
	CRAFT_DATA_FIELD(damage_stats.damage_from_starship),
	CRAFT_DATA_FIELD(damage_stats.damage_from_mine),
	CRAFT_DATA_FIELD(damage_stats.damage_from_flight_group_amount),
	CRAFT_DATA_FIELD(damage_stats.damage_from_player),
	CRAFT_DATA_FIELD(damage_stats.damage_from_ai_skill),
	CRAFT_DATA_FIELD(damage_stats.installed_hud_feature_mask),
	CRAFT_DATA_FIELD(damage_stats.active_hud_feature_mask),
	CRAFT_DATA_FIELD(system_flags),
	CRAFT_DATA_FIELD(working_subsystems),
	CRAFT_DATA_FIELD(weapon_fire_inhibit_timer),
	CRAFT_DATA_FIELD(unused_mission_flag),
	CRAFT_DATA_FIELD(not_disabled_accounting_suppress),
	CRAFT_DATA_FIELD(captured_by_flight_group),
	CRAFT_DATA_FIELD(attacked_by_team),
	CRAFT_DATA_FIELD(identified_order_by_team),
	CRAFT_DATA_FIELD(boarding_state),
	CRAFT_DATA_FIELD(special_cargo_name),
	CRAFT_DATA_FIELD(shield_energy),
	CRAFT_DATA_FIELD(shield_recharge_level),
	CRAFT_DATA_FIELD(shield_distrib_mode),
	CRAFT_DATA_FIELD(cannon_group_count),
	CRAFT_DATA_FIELD(laser_recharge_level),
	CRAFT_DATA_FIELD(laser_slot_count),
	CRAFT_DATA_FIELD(laser_state),
	CRAFT_DATA_FIELD(warhead_launcher_count),
	CRAFT_DATA_FIELD(warhead_slot_type_ids),
	CRAFT_DATA_FIELD(warhead_launcher_flags),
	CRAFT_DATA_FIELD(warhead_launcher_cooldown_ticks),
	CRAFT_DATA_FIELD(warhead_lock_ticks),
	CRAFT_DATA_FIELD(beam_type_id),
	CRAFT_DATA_FIELD(beam_recharge_level),
	CRAFT_DATA_FIELD(beam_charge),
	CRAFT_DATA_FIELD(beam_active),
	CRAFT_DATA_FIELD(beam_output),
	CRAFT_DATA_FIELD(beam_target_obj_idx),
	CRAFT_DATA_FIELD(cm_type_id),
	CRAFT_DATA_FIELD(cm_ammo_count),
	CRAFT_DATA_FIELD(chaff_active_seconds),
	CRAFT_DATA_FIELD(cm_fire_cooldown_timer),
	CRAFT_DATA_FIELD(weapon_stats),
	CRAFT_DATA_FIELD(unused256),
	CRAFT_DATA_FIELD(field_29f),
	CRAFT_DATA_FIELD(system_display_slot_by_system),
	CRAFT_DATA_FIELD(system_health),
	CRAFT_DATA_FIELD(system_repair_seconds),
	CRAFT_DATA_FIELD(component_state),
	CRAFT_DATA_FIELD(mesh_rotation),
	CRAFT_DATA_FIELD(component_hp),
	CRAFT_DATA_FIELD(player_command_avoid_target_obj_idx),
	CRAFT_DATA_FIELD(weapon_slots),
	CRAFT_DATA_FIELD(effective_ai_object_signature),
	CRAFT_DATA_FIELD(turret_target_states),
	CRAFT_DATA_FIELD(unused3f2),
};
#undef CRAFT_DATA_FIELD

void xvt_snapshot_encode_craft_data(struct xvt_snapshot_craft_data *record,
				    const struct craft_data *live)
{
	xvt_snapshot_copy_fields(
		(void *)record, (void *)live, g_craft_data_fields,
		sizeof(g_craft_data_fields) / sizeof(g_craft_data_fields[0]),
		0);
	for (size_t i = 0; i < sizeof(record->turret_object_links) /
				       sizeof(record->turret_object_links[0]);
	     ++i) {
		record->turret_object_links[i] =
			live->turret_object_links[i]
				? (uint32_t)((live->turret_object_links[i] -
					      g_object_table) *
						     sizeof(struct
							    xvt_snapshot_object_record) +
					     1)
				: 0;
	}
	record->effective_ai_object_link =
		live->effective_ai_object_link
			? (uint32_t)((live->effective_ai_object_link -
				      g_object_table) *
					     sizeof(struct
						    xvt_snapshot_object_record) +
				     1)
			: 0;
}

void xvt_snapshot_decode_craft_data(
	struct craft_data *live, const struct xvt_snapshot_craft_data *record)
{
	xvt_snapshot_copy_fields(
		(void *)record, (void *)live, g_craft_data_fields,
		sizeof(g_craft_data_fields) / sizeof(g_craft_data_fields[0]),
		1);
	for (size_t i = 0; i < sizeof(record->turret_object_links) /
				       sizeof(record->turret_object_links[0]);
	     ++i) {
		live->turret_object_links[i] =
			record->turret_object_links[i]
				? &g_object_table
					  [(record->turret_object_links[i] -
					    1) /
					   sizeof(struct
						  xvt_snapshot_object_record)]
				: NULL;
	}
	live->effective_ai_object_link =
		record->effective_ai_object_link
			? &g_object_table
				  [(record->effective_ai_object_link - 1) /
				   sizeof(struct xvt_snapshot_object_record)]
			: NULL;
}

#define MOBILE_OBJECT_CHAR_DATA_FIELD(field)                                   \
	SNAPSHOT_FIELD(struct mobile_object_char_data,                         \
		       struct xvt_snapshot_mobile_object_char_data, field)
static const struct xvt_snapshot_field g_mobile_object_char_data_fields[] = {
	MOBILE_OBJECT_CHAR_DATA_FIELD(skill_value),
	MOBILE_OBJECT_CHAR_DATA_FIELD(unused02),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.current_order_slot),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.order_progress),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.skipped_to_order4),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.running_plan_id),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.current_plan_id),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.waypoint_index),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.saved_plan_id),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.think_interval),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.think_timer),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.saved_rand_seed),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.target_obj_idx),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.target_signature),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.target_component),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.has_live_target),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.aim_point_x),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.aim_point_y),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.aim_point_z),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.candidate_target_idx),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.escort_target_fg),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.target_z_angle),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.target_roll),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.target_xy_angle),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.maneuver_mode),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.maneuver_phase),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.maneuver_timer),
	MOBILE_OBJECT_CHAR_DATA_FIELD(ai_controller.secondary_maneuver_timer),
	MOBILE_OBJECT_CHAR_DATA_FIELD(unused40),
};
#undef MOBILE_OBJECT_CHAR_DATA_FIELD

void xvt_snapshot_encode_mobile_object_char_data(
	struct xvt_snapshot_mobile_object_char_data *record,
	const struct mobile_object_char_data *live)
{
	xvt_snapshot_copy_fields(
		(void *)record, (void *)live, g_mobile_object_char_data_fields,
		sizeof(g_mobile_object_char_data_fields) /
			sizeof(g_mobile_object_char_data_fields[0]),
		0);
}

void xvt_snapshot_decode_mobile_object_char_data(
	struct mobile_object_char_data *live,
	const struct xvt_snapshot_mobile_object_char_data *record)
{
	xvt_snapshot_copy_fields(
		(void *)record, (void *)live, g_mobile_object_char_data_fields,
		sizeof(g_mobile_object_char_data_fields) /
			sizeof(g_mobile_object_char_data_fields[0]),
		1);
}

#define PLAYER_DATA_FIELD(field)                                               \
	SNAPSHOT_FIELD(struct player_data, struct xvt_snapshot_player_data,    \
		       field)
static const struct xvt_snapshot_field g_player_data_fields[] = {
	PLAYER_DATA_FIELD(object_index),
	PLAYER_DATA_FIELD(bound_object_signature),
	PLAYER_DATA_FIELD(pilot_rating),
	PLAYER_DATA_FIELD(iff),
	PLAYER_DATA_FIELD(team),
	PLAYER_DATA_FIELD(bound_flight_group_idx),
	PLAYER_DATA_FIELD(participation_state),
	PLAYER_DATA_FIELD(awaiting_new_craft),
	PLAYER_DATA_FIELD(bound_craft_engine_glow_count),
	PLAYER_DATA_FIELD(map_camera_state),
	PLAYER_DATA_FIELD(hyperspace_phase),
	PLAYER_DATA_FIELD(hyperspace_runtime),
	PLAYER_DATA_FIELD(target_box_enabled),
	PLAYER_DATA_FIELD(current_target_object_idx),
	PLAYER_DATA_FIELD(target_cycle_start),
	PLAYER_DATA_FIELD(target_preset_slot),
	PLAYER_DATA_FIELD(missile_lock_state),
	PLAYER_DATA_FIELD(selected_weapon_bank),
	PLAYER_DATA_FIELD(selected_weapon_mode),
	PLAYER_DATA_FIELD(selected_target_component),
	PLAYER_DATA_FIELD(targeting_state),
	PLAYER_DATA_FIELD(engine_wash_source_obj_idx),
	PLAYER_DATA_FIELD(engine_wash_strength),
	PLAYER_DATA_FIELD(throttle_preset),
	PLAYER_DATA_FIELD(laser_preset),
	PLAYER_DATA_FIELD(shield_preset),
	PLAYER_DATA_FIELD(beam_preset),
	PLAYER_DATA_FIELD(saved_craft_settings),
	PLAYER_DATA_FIELD(saved_hud_view_state),
	PLAYER_DATA_FIELD(pending_action_id),
	PLAYER_DATA_FIELD(pending_action_param),
	PLAYER_DATA_FIELD(pending_action_issuer_player_idx),
	PLAYER_DATA_FIELD(yaw_roll_swap),
	PLAYER_DATA_FIELD(smoothed_input_yaw),
	PLAYER_DATA_FIELD(smoothed_input_pitch),
	PLAYER_DATA_FIELD(saved_key_mods),
	PLAYER_DATA_FIELD(key_mods_hold_timer),
	PLAYER_DATA_FIELD(hardpoint_world_x),
	PLAYER_DATA_FIELD(hardpoint_world_y),
	PLAYER_DATA_FIELD(hardpoint_world_z),
	PLAYER_DATA_FIELD(prev_hardpoint_world_x),
	PLAYER_DATA_FIELD(prev_hardpoint_world_y),
	PLAYER_DATA_FIELD(prev_hardpoint_world_z),
	PLAYER_DATA_FIELD(mission_stats),
	PLAYER_DATA_FIELD(warheads_fired),
	PLAYER_DATA_FIELD(per_mission_kills),
	PLAYER_DATA_FIELD(msg_text),
	PLAYER_DATA_FIELD(msg_length),
	PLAYER_DATA_FIELD(chat_recipient_mode),
	PLAYER_DATA_FIELD(view_state.camera_world_x),
	PLAYER_DATA_FIELD(view_state.camera_world_y),
	PLAYER_DATA_FIELD(view_state.camera_world_z),
	PLAYER_DATA_FIELD(view_state.camera_focus_obj_idx),
	PLAYER_DATA_FIELD(view_state.aim_target_idx),
	PLAYER_DATA_FIELD(view_state.view_pitch),
	PLAYER_DATA_FIELD(view_state.view_yaw),
	PLAYER_DATA_FIELD(view_state.view_roll),
	PLAYER_DATA_FIELD(view_state.view_angle_d),
	PLAYER_DATA_FIELD(view_state.hud_aim_x),
	PLAYER_DATA_FIELD(view_state.hud_aim_y),
	PLAYER_DATA_FIELD(view_state.hud_state_live),
	PLAYER_DATA_FIELD(view_state.hud_state_mirror),
	PLAYER_DATA_FIELD(view_state.hud_aim_x_snap_state),
	PLAYER_DATA_FIELD(view_state.saved_hud_state_byte),
	PLAYER_DATA_FIELD(view_state.unused20),
	PLAYER_DATA_FIELD(view_state.saved_hud_aim_x),
	PLAYER_DATA_FIELD(view_state.saved_hud_aim_y),
	PLAYER_DATA_FIELD(view_state.player_input_blocked),
	PLAYER_DATA_FIELD(view_state.camera_distance_step),
	PLAYER_DATA_FIELD(view_state.external_camera_active),
	PLAYER_DATA_FIELD(view_state.camera_distance),
	PLAYER_DATA_FIELD(view_state.target_camera_active),
	PLAYER_DATA_FIELD(view_state.camera_roll_history),
	PLAYER_DATA_FIELD(view_state.camera_pitch_history),
	PLAYER_DATA_FIELD(view_state.camera_yaw_history),
	PLAYER_DATA_FIELD(view_state.unused199),
	PLAYER_DATA_FIELD(network.flight_resolution_mode),
	PLAYER_DATA_FIELD(network.direct_play_id),
	PLAYER_DATA_FIELD(lockstep_timestamp),
	PLAYER_DATA_FIELD(saved_x),
	PLAYER_DATA_FIELD(saved_y),
	PLAYER_DATA_FIELD(saved_z),
	PLAYER_DATA_FIELD(saved_roll),
	PLAYER_DATA_FIELD(saved_pitch),
	PLAYER_DATA_FIELD(saved_yaw),
	PLAYER_DATA_FIELD(saved_lifetime_timer),
	PLAYER_DATA_FIELD(saved_speed),
	PLAYER_DATA_FIELD(saved_speed_remainder),
	PLAYER_DATA_FIELD(saved_roll_impulse_rate),
	PLAYER_DATA_FIELD(saved_object_signature),
	PLAYER_DATA_FIELD(saved_awaiting_new_craft),
	PLAYER_DATA_FIELD(pending_action_timer),
	PLAYER_DATA_FIELD(beam_fire_cooldown_timer),
	PLAYER_DATA_FIELD(field_5b5),
	PLAYER_DATA_FIELD(next_engine_wash_check_time),
};
#undef PLAYER_DATA_FIELD

void xvt_snapshot_encode_player_data(struct xvt_snapshot_player_data *record,
				     const struct player_data *live)
{
	xvt_snapshot_copy_fields(
		(void *)record, (void *)live, g_player_data_fields,
		sizeof(g_player_data_fields) / sizeof(g_player_data_fields[0]),
		0);
}

void xvt_snapshot_decode_player_data(
	struct player_data *live, const struct xvt_snapshot_player_data *record)
{
	xvt_snapshot_copy_fields(
		(void *)record, (void *)live, g_player_data_fields,
		sizeof(g_player_data_fields) / sizeof(g_player_data_fields[0]),
		1);
}

#define FLIGHT_MISSION_STATE_FIELD(field)                                      \
	SNAPSHOT_FIELD(struct flight_mission_state,                            \
		       struct xvt_snapshot_flight_mission_state, field)
static const struct xvt_snapshot_field g_flight_mission_state_fields[] = {
	FLIGHT_MISSION_STATE_FIELD(mission_end_pending),
	FLIGHT_MISSION_STATE_FIELD(proving_grounds_mode_active),
	FLIGHT_MISSION_STATE_FIELD(proving_grounds_craft_type),
	FLIGHT_MISSION_STATE_FIELD(proving_grounds_level),
	FLIGHT_MISSION_STATE_FIELD(proving_grounds_score),
	FLIGHT_MISSION_STATE_FIELD(unused08),
	FLIGHT_MISSION_STATE_FIELD(proving_grounds_checkpoints_passed),
	FLIGHT_MISSION_STATE_FIELD(unused0c),
	FLIGHT_MISSION_STATE_FIELD(proving_grounds_checkpoints_remaining),
	FLIGHT_MISSION_STATE_FIELD(proving_grounds_targets_destroyed),
	FLIGHT_MISSION_STATE_FIELD(proving_grounds_time_bonus),
	FLIGHT_MISSION_STATE_FIELD(difficulty),
	FLIGHT_MISSION_STATE_FIELD(collisions_enabled),
	FLIGHT_MISSION_STATE_FIELD(craft_jumping_enabled),
	FLIGHT_MISSION_STATE_FIELD(random_variation_enabled),
	FLIGHT_MISSION_STATE_FIELD(battle_length_index),
	FLIGHT_MISSION_STATE_FIELD(locate_players_enabled),
	FLIGHT_MISSION_STATE_FIELD(ai_opponents_enabled),
	FLIGHT_MISSION_STATE_FIELD(player_flight_group_wave_mode),
	FLIGHT_MISSION_STATE_FIELD(mission_time_limit_minutes),
	FLIGHT_MISSION_STATE_FIELD(team_victory_time_limit_minutes),
	FLIGHT_MISSION_STATE_FIELD(team_victory_time_limit_started),
	FLIGHT_MISSION_STATE_FIELD(craft_impact_bounce_enabled),
	FLIGHT_MISSION_STATE_FIELD(connected_player_count),
	FLIGHT_MISSION_STATE_FIELD(max_connected_player_count_this_mission),
	FLIGHT_MISSION_STATE_FIELD(runtime.team_scores),
	FLIGHT_MISSION_STATE_FIELD(runtime.team_kill_stats),
	FLIGHT_MISSION_STATE_FIELD(runtime.team_fg_inspected_captured_counts),
	FLIGHT_MISSION_STATE_FIELD(runtime.team_fg_designation_code),
	FLIGHT_MISSION_STATE_FIELD(runtime.global_primary_goal_status),
	FLIGHT_MISSION_STATE_FIELD(runtime.global_goal_status_unused),
	FLIGHT_MISSION_STATE_FIELD(runtime.global_bonus_goal_status),
	FLIGHT_MISSION_STATE_FIELD(runtime.team_global_goal_state),
	FLIGHT_MISSION_STATE_FIELD(runtime.team_goal_status),
	FLIGHT_MISSION_STATE_FIELD(runtime.global_goal_trigger_counts),
	FLIGHT_MISSION_STATE_FIELD(
		runtime.team_mission_completion_time_seconds),
	FLIGHT_MISSION_STATE_FIELD(runtime.team_has_countable_craft),
	FLIGHT_MISSION_STATE_FIELD(runtime.team_reinforcements_called),
	FLIGHT_MISSION_STATE_FIELD(message_triggered),
	FLIGHT_MISSION_STATE_FIELD(message_delay_countdown),
	FLIGHT_MISSION_STATE_FIELD(global_unit_craft_count),
};
#undef FLIGHT_MISSION_STATE_FIELD
#undef SNAPSHOT_FIELD

void xvt_snapshot_encode_flight_mission_state(
	struct xvt_snapshot_flight_mission_state *record,
	const struct flight_mission_state *live)
{
	xvt_snapshot_copy_fields(
		record, (void *)live, g_flight_mission_state_fields,
		sizeof(g_flight_mission_state_fields) /
			sizeof(g_flight_mission_state_fields[0]),
		0);
}

void xvt_snapshot_decode_flight_mission_state(
	struct flight_mission_state *live,
	const struct xvt_snapshot_flight_mission_state *record)
{
	xvt_snapshot_copy_fields(
		(void *)record, live, g_flight_mission_state_fields,
		sizeof(g_flight_mission_state_fields) /
			sizeof(g_flight_mission_state_fields[0]),
		1);
}
