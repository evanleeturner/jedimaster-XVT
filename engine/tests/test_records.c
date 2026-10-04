/* Checks the packed world records (xvt_runtime/snapshot/records.h) against the
 * promises in its header, on live structs, records and pools this file fills
 * itself: no game data is read.
 *
 * Live structs and records are filled with a byte pattern that changes from one
 * offset to the next, so a field copied to or from the wrong place shows up.
 * Every pointer in a live struct, and every link in a record, is set to none or
 * into one of this file's pools before it is translated. The field lists below
 * are the record fields the header declares, with the nested packed records
 * spelled out member by member; the links have checks of their own. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/snapshot/records.h"

/* One listed field: where it sits and how big it is in the record and in the live struct. */
struct field {
	size_t record_offset;
	size_t record_size;
	size_t live_offset;
	size_t live_size;
	const char *name;
};

#define FIELD(record_type, live_type, member)                                  \
	{offsetof(record_type, member), sizeof(((record_type *)0)->member),    \
	 offsetof(live_type, member), sizeof(((live_type *)0)->member),        \
	 #member}
#define OBJECT_FIELD(member)                                                   \
	FIELD(struct xvt_snapshot_object_record, struct object_record, member)
#define MOBILE_FIELD(member)                                                   \
	FIELD(struct xvt_snapshot_mobile_object, struct mobile_object, member)
#define CRAFT_FIELD(member)                                                    \
	FIELD(struct xvt_snapshot_craft_data, struct craft_data, member)
#define CHAR_DATA_FIELD(member)                                                \
	FIELD(struct xvt_snapshot_mobile_object_char_data,                     \
	      struct mobile_object_char_data, member)
#define PLAYER_FIELD(member)                                                   \
	FIELD(struct xvt_snapshot_player_data, struct player_data, member)
#define MISSION_FIELD(member)                                                  \
	FIELD(struct xvt_snapshot_flight_mission_state,                        \
	      struct flight_mission_state, member)

static const struct field k_object_fields[] = {
	OBJECT_FIELD(object_signature),
	OBJECT_FIELD(genus_id),
	OBJECT_FIELD(object_type),
	OBJECT_FIELD(world_x),
	OBJECT_FIELD(world_y),
	OBJECT_FIELD(world_z),
	OBJECT_FIELD(yaw),
	OBJECT_FIELD(pitch),
	OBJECT_FIELD(roll),
	OBJECT_FIELD(flight_group_idx),
	OBJECT_FIELD(type_specific_word),
	OBJECT_FIELD(type_specific_byte),
	OBJECT_FIELD(player_owner_idx),
};

static const struct field k_mobile_fields[] = {
	MOBILE_FIELD(family),
	MOBILE_FIELD(effect_size),
	MOBILE_FIELD(sim_state_timestamp),
	MOBILE_FIELD(prev_world_x),
	MOBILE_FIELD(prev_world_y),
	MOBILE_FIELD(prev_world_z),
	MOBILE_FIELD(proximity_list.count),
	MOBILE_FIELD(proximity_list.contact_ticks),
	MOBILE_FIELD(proximity_list.obj_idx),
	MOBILE_FIELD(proximity_list.rebuild_ticks),
	MOBILE_FIELD(roll_impulse_rate),
	MOBILE_FIELD(speed),
	MOBILE_FIELD(speed_remainder),
	MOBILE_FIELD(damage_amount),
	MOBILE_FIELD(lifetime_timer),
	MOBILE_FIELD(seconds_alive),
	MOBILE_FIELD(source_obj_idx),
	MOBILE_FIELD(source_object_type),
	MOBILE_FIELD(iff),
	MOBILE_FIELD(team),
	MOBILE_FIELD(node_switch_index),
	MOBILE_FIELD(move_vector_dirty),
	MOBILE_FIELD(move_x),
	MOBILE_FIELD(move_y),
	MOBILE_FIELD(move_z),
	MOBILE_FIELD(orient_matrix_dirty),
	MOBILE_FIELD(cached_fwd_x),
	MOBILE_FIELD(cached_fwd_y),
	MOBILE_FIELD(cached_fwd_z),
	MOBILE_FIELD(cached_side_x),
	MOBILE_FIELD(cached_side_y),
	MOBILE_FIELD(cached_side_z),
	MOBILE_FIELD(cached_up_x),
	MOBILE_FIELD(cached_up_y),
	MOBILE_FIELD(cached_up_z),
};

static const struct field k_craft_fields[] = {
	CRAFT_FIELD(craft_index_in_group),
	CRAFT_FIELD(model_index),
	CRAFT_FIELD(leader_obj_idx),
	CRAFT_FIELD(unused006),
	CRAFT_FIELD(object_kind),
	CRAFT_FIELD(mission_accounting_done),
	CRAFT_FIELD(ai_skill),
	CRAFT_FIELD(unused00b),
	CRAFT_FIELD(pitch),
	CRAFT_FIELD(yaw),
	CRAFT_FIELD(breakup_pitch_rate),
	CRAFT_FIELD(breakup_yaw_rate),
	CRAFT_FIELD(beam_effect_accum),
	CRAFT_FIELD(s_foil_state),
	CRAFT_FIELD(ai_controller.current_order_slot),
	CRAFT_FIELD(ai_controller.order_progress),
	CRAFT_FIELD(ai_controller.skipped_to_order4),
	CRAFT_FIELD(ai_controller.running_plan_id),
	CRAFT_FIELD(ai_controller.current_plan_id),
	CRAFT_FIELD(ai_controller.waypoint_index),
	CRAFT_FIELD(ai_controller.saved_plan_id),
	CRAFT_FIELD(ai_controller.think_interval),
	CRAFT_FIELD(ai_controller.think_timer),
	CRAFT_FIELD(ai_controller.saved_rand_seed),
	CRAFT_FIELD(ai_controller.target_obj_idx),
	CRAFT_FIELD(ai_controller.target_signature),
	CRAFT_FIELD(ai_controller.target_component),
	CRAFT_FIELD(ai_controller.has_live_target),
	CRAFT_FIELD(ai_controller.aim_point_x),
	CRAFT_FIELD(ai_controller.aim_point_y),
	CRAFT_FIELD(ai_controller.aim_point_z),
	CRAFT_FIELD(ai_controller.candidate_target_idx),
	CRAFT_FIELD(ai_controller.escort_target_fg),
	CRAFT_FIELD(ai_controller.target_z_angle),
	CRAFT_FIELD(ai_controller.target_roll),
	CRAFT_FIELD(ai_controller.target_xy_angle),
	CRAFT_FIELD(ai_controller.maneuver_mode),
	CRAFT_FIELD(ai_controller.maneuver_phase),
	CRAFT_FIELD(ai_controller.maneuver_timer),
	CRAFT_FIELD(ai_controller.secondary_maneuver_timer),
	CRAFT_FIELD(carried_object_index),
	CRAFT_FIELD(carrier_obj_idx),
	CRAFT_FIELD(last_attacker_obj_idx),
	CRAFT_FIELD(last_hit_mission_second),
	CRAFT_FIELD(ai_flight.threat_obj_idx),
	CRAFT_FIELD(ai_flight.impact_obj_idx),
	CRAFT_FIELD(ai_flight.go_home_flag),
	CRAFT_FIELD(ai_flight.mission_aborted_flag),
	CRAFT_FIELD(ai_flight.depart_timer_flag),
	CRAFT_FIELD(ai_flight.depart_clock_hours),
	CRAFT_FIELD(ai_flight.depart_clock_minutes),
	CRAFT_FIELD(ai_flight.depart_clock_seconds),
	CRAFT_FIELD(ai_flight.warheads_fired_this_maneuver),
	CRAFT_FIELD(ai_flight.hits_this_maneuver),
	CRAFT_FIELD(ai_flight.boarded_accounting_done),
	CRAFT_FIELD(ai_flight.times_boarded),
	CRAFT_FIELD(ai_flight.docking_accounting_done),
	CRAFT_FIELD(ai_flight.docked_target_count),
	CRAFT_FIELD(ai_flight.docked_target_signatures),
	CRAFT_FIELD(ai_flight.max_speed_cache),
	CRAFT_FIELD(ai_flight.motion_scale),
	CRAFT_FIELD(ai_flight.climb_state),
	CRAFT_FIELD(ai_flight.dive_state),
	CRAFT_FIELD(ai_flight.pitch_rate),
	CRAFT_FIELD(ai_flight.pitch_accel),
	CRAFT_FIELD(ai_flight.pitch_state),
	CRAFT_FIELD(ai_flight.pitch_through_loop),
	CRAFT_FIELD(ai_flight.pitch_step_scale),
	CRAFT_FIELD(ai_flight.roll_rate),
	CRAFT_FIELD(ai_flight.roll_accel),
	CRAFT_FIELD(ai_flight.roll_state),
	CRAFT_FIELD(ai_flight.roll_step),
	CRAFT_FIELD(ai_flight.turn_rate),
	CRAFT_FIELD(ai_flight.turn_accel),
	CRAFT_FIELD(ai_flight.turn_state),
	CRAFT_FIELD(ai_flight.turn_step),
	CRAFT_FIELD(ai_flight.formation_type),
	CRAFT_FIELD(ai_flight.separation),
	CRAFT_FIELD(craft_ordinal),
	CRAFT_FIELD(push_accum_x),
	CRAFT_FIELD(push_accum_y),
	CRAFT_FIELD(push_accum_z),
	CRAFT_FIELD(throttle_speed),
	CRAFT_FIELD(engine_overdrive_off),
	CRAFT_FIELD(commanded_speed),
	CRAFT_FIELD(hull_damage),
	CRAFT_FIELD(system_damage_hull_threshold),
	CRAFT_FIELD(hull_max),
	CRAFT_FIELD(subsystem_damage),
	CRAFT_FIELD(damage_stats.last_system_hit_time),
	CRAFT_FIELD(damage_stats.damage_received_total),
	CRAFT_FIELD(damage_stats.damage_received_by_player_owned_craft),
	CRAFT_FIELD(damage_stats.damage_from_collision),
	CRAFT_FIELD(damage_stats.damage_from_starship),
	CRAFT_FIELD(damage_stats.damage_from_mine),
	CRAFT_FIELD(damage_stats.damage_from_flight_group_amount),
	CRAFT_FIELD(damage_stats.damage_from_player),
	CRAFT_FIELD(damage_stats.damage_from_ai_skill),
	CRAFT_FIELD(damage_stats.installed_hud_feature_mask),
	CRAFT_FIELD(damage_stats.active_hud_feature_mask),
	CRAFT_FIELD(system_flags),
	CRAFT_FIELD(working_subsystems),
	CRAFT_FIELD(weapon_fire_inhibit_timer),
	CRAFT_FIELD(unused_mission_flag),
	CRAFT_FIELD(not_disabled_accounting_suppress),
	CRAFT_FIELD(captured_by_flight_group),
	CRAFT_FIELD(attacked_by_team),
	CRAFT_FIELD(identified_order_by_team),
	CRAFT_FIELD(boarding_state),
	CRAFT_FIELD(special_cargo_name),
	CRAFT_FIELD(shield_energy),
	CRAFT_FIELD(shield_recharge_level),
	CRAFT_FIELD(shield_distrib_mode),
	CRAFT_FIELD(cannon_group_count),
	CRAFT_FIELD(laser_recharge_level),
	CRAFT_FIELD(laser_slot_count),
	CRAFT_FIELD(laser_state),
	CRAFT_FIELD(warhead_launcher_count),
	CRAFT_FIELD(warhead_slot_type_ids),
	CRAFT_FIELD(warhead_launcher_flags),
	CRAFT_FIELD(warhead_launcher_cooldown_ticks),
	CRAFT_FIELD(warhead_lock_ticks),
	CRAFT_FIELD(beam_type_id),
	CRAFT_FIELD(beam_recharge_level),
	CRAFT_FIELD(beam_charge),
	CRAFT_FIELD(beam_active),
	CRAFT_FIELD(beam_output),
	CRAFT_FIELD(beam_target_obj_idx),
	CRAFT_FIELD(cm_type_id),
	CRAFT_FIELD(cm_ammo_count),
	CRAFT_FIELD(chaff_active_seconds),
	CRAFT_FIELD(cm_fire_cooldown_timer),
	CRAFT_FIELD(weapon_stats),
	CRAFT_FIELD(unused256),
	CRAFT_FIELD(field_29f),
	CRAFT_FIELD(system_display_slot_by_system),
	CRAFT_FIELD(system_health),
	CRAFT_FIELD(system_repair_seconds),
	CRAFT_FIELD(component_state),
	CRAFT_FIELD(mesh_rotation),
	CRAFT_FIELD(component_hp),
	CRAFT_FIELD(player_command_avoid_target_obj_idx),
	CRAFT_FIELD(weapon_slots),
	CRAFT_FIELD(effective_ai_object_signature),
	CRAFT_FIELD(turret_target_states),
	CRAFT_FIELD(unused3f2),
};

static const struct field k_char_fields[] = {
	CHAR_DATA_FIELD(skill_value),
	CHAR_DATA_FIELD(unused02),
	CHAR_DATA_FIELD(ai_controller.current_order_slot),
	CHAR_DATA_FIELD(ai_controller.order_progress),
	CHAR_DATA_FIELD(ai_controller.skipped_to_order4),
	CHAR_DATA_FIELD(ai_controller.running_plan_id),
	CHAR_DATA_FIELD(ai_controller.current_plan_id),
	CHAR_DATA_FIELD(ai_controller.waypoint_index),
	CHAR_DATA_FIELD(ai_controller.saved_plan_id),
	CHAR_DATA_FIELD(ai_controller.think_interval),
	CHAR_DATA_FIELD(ai_controller.think_timer),
	CHAR_DATA_FIELD(ai_controller.saved_rand_seed),
	CHAR_DATA_FIELD(ai_controller.target_obj_idx),
	CHAR_DATA_FIELD(ai_controller.target_signature),
	CHAR_DATA_FIELD(ai_controller.target_component),
	CHAR_DATA_FIELD(ai_controller.has_live_target),
	CHAR_DATA_FIELD(ai_controller.aim_point_x),
	CHAR_DATA_FIELD(ai_controller.aim_point_y),
	CHAR_DATA_FIELD(ai_controller.aim_point_z),
	CHAR_DATA_FIELD(ai_controller.candidate_target_idx),
	CHAR_DATA_FIELD(ai_controller.escort_target_fg),
	CHAR_DATA_FIELD(ai_controller.target_z_angle),
	CHAR_DATA_FIELD(ai_controller.target_roll),
	CHAR_DATA_FIELD(ai_controller.target_xy_angle),
	CHAR_DATA_FIELD(ai_controller.maneuver_mode),
	CHAR_DATA_FIELD(ai_controller.maneuver_phase),
	CHAR_DATA_FIELD(ai_controller.maneuver_timer),
	CHAR_DATA_FIELD(ai_controller.secondary_maneuver_timer),
	CHAR_DATA_FIELD(unused40),
};

static const struct field k_player_fields[] = {
	PLAYER_FIELD(object_index),
	PLAYER_FIELD(bound_object_signature),
	PLAYER_FIELD(pilot_rating),
	PLAYER_FIELD(iff),
	PLAYER_FIELD(team),
	PLAYER_FIELD(bound_flight_group_idx),
	PLAYER_FIELD(participation_state),
	PLAYER_FIELD(awaiting_new_craft),
	PLAYER_FIELD(bound_craft_engine_glow_count),
	PLAYER_FIELD(map_camera_state),
	PLAYER_FIELD(hyperspace_phase),
	PLAYER_FIELD(hyperspace_runtime),
	PLAYER_FIELD(target_box_enabled),
	PLAYER_FIELD(current_target_object_idx),
	PLAYER_FIELD(target_cycle_start),
	PLAYER_FIELD(target_preset_slot),
	PLAYER_FIELD(missile_lock_state),
	PLAYER_FIELD(selected_weapon_bank),
	PLAYER_FIELD(selected_weapon_mode),
	PLAYER_FIELD(selected_target_component),
	PLAYER_FIELD(targeting_state),
	PLAYER_FIELD(engine_wash_source_obj_idx),
	PLAYER_FIELD(engine_wash_strength),
	PLAYER_FIELD(throttle_preset),
	PLAYER_FIELD(laser_preset),
	PLAYER_FIELD(shield_preset),
	PLAYER_FIELD(beam_preset),
	PLAYER_FIELD(saved_craft_settings),
	PLAYER_FIELD(saved_hud_view_state),
	PLAYER_FIELD(pending_action_id),
	PLAYER_FIELD(pending_action_param),
	PLAYER_FIELD(pending_action_issuer_player_idx),
	PLAYER_FIELD(yaw_roll_swap),
	PLAYER_FIELD(smoothed_input_yaw),
	PLAYER_FIELD(smoothed_input_pitch),
	PLAYER_FIELD(saved_key_mods),
	PLAYER_FIELD(key_mods_hold_timer),
	PLAYER_FIELD(hardpoint_world_x),
	PLAYER_FIELD(hardpoint_world_y),
	PLAYER_FIELD(hardpoint_world_z),
	PLAYER_FIELD(prev_hardpoint_world_x),
	PLAYER_FIELD(prev_hardpoint_world_y),
	PLAYER_FIELD(prev_hardpoint_world_z),
	PLAYER_FIELD(mission_stats),
	PLAYER_FIELD(warheads_fired),
	PLAYER_FIELD(per_mission_kills),
	PLAYER_FIELD(msg_text),
	PLAYER_FIELD(msg_length),
	PLAYER_FIELD(chat_recipient_mode),
	PLAYER_FIELD(view_state.camera_world_x),
	PLAYER_FIELD(view_state.camera_world_y),
	PLAYER_FIELD(view_state.camera_world_z),
	PLAYER_FIELD(view_state.camera_focus_obj_idx),
	PLAYER_FIELD(view_state.aim_target_idx),
	PLAYER_FIELD(view_state.view_pitch),
	PLAYER_FIELD(view_state.view_yaw),
	PLAYER_FIELD(view_state.view_roll),
	PLAYER_FIELD(view_state.view_angle_d),
	PLAYER_FIELD(view_state.hud_aim_x),
	PLAYER_FIELD(view_state.hud_aim_y),
	PLAYER_FIELD(view_state.hud_state_live),
	PLAYER_FIELD(view_state.hud_state_mirror),
	PLAYER_FIELD(view_state.hud_aim_x_snap_state),
	PLAYER_FIELD(view_state.saved_hud_state_byte),
	PLAYER_FIELD(view_state.unused20),
	PLAYER_FIELD(view_state.saved_hud_aim_x),
	PLAYER_FIELD(view_state.saved_hud_aim_y),
	PLAYER_FIELD(view_state.player_input_blocked),
	PLAYER_FIELD(view_state.camera_distance_step),
	PLAYER_FIELD(view_state.external_camera_active),
	PLAYER_FIELD(view_state.camera_distance),
	PLAYER_FIELD(view_state.target_camera_active),
	PLAYER_FIELD(view_state.camera_roll_history),
	PLAYER_FIELD(view_state.camera_pitch_history),
	PLAYER_FIELD(view_state.camera_yaw_history),
	PLAYER_FIELD(view_state.unused199),
	PLAYER_FIELD(network.flight_resolution_mode),
	PLAYER_FIELD(network.direct_play_id),
	PLAYER_FIELD(lockstep_timestamp),
	PLAYER_FIELD(saved_x),
	PLAYER_FIELD(saved_y),
	PLAYER_FIELD(saved_z),
	PLAYER_FIELD(saved_roll),
	PLAYER_FIELD(saved_pitch),
	PLAYER_FIELD(saved_yaw),
	PLAYER_FIELD(saved_lifetime_timer),
	PLAYER_FIELD(saved_speed),
	PLAYER_FIELD(saved_speed_remainder),
	PLAYER_FIELD(saved_roll_impulse_rate),
	PLAYER_FIELD(saved_object_signature),
	PLAYER_FIELD(saved_awaiting_new_craft),
	PLAYER_FIELD(pending_action_timer),
	PLAYER_FIELD(beam_fire_cooldown_timer),
	PLAYER_FIELD(field_5b5),
	PLAYER_FIELD(next_engine_wash_check_time),
};

static const struct field k_mission_fields[] = {
	MISSION_FIELD(mission_end_pending),
	MISSION_FIELD(proving_grounds_mode_active),
	MISSION_FIELD(proving_grounds_craft_type),
	MISSION_FIELD(proving_grounds_level),
	MISSION_FIELD(proving_grounds_score),
	MISSION_FIELD(unused08),
	MISSION_FIELD(proving_grounds_checkpoints_passed),
	MISSION_FIELD(unused0c),
	MISSION_FIELD(proving_grounds_checkpoints_remaining),
	MISSION_FIELD(proving_grounds_targets_destroyed),
	MISSION_FIELD(proving_grounds_time_bonus),
	MISSION_FIELD(difficulty),
	MISSION_FIELD(collisions_enabled),
	MISSION_FIELD(craft_jumping_enabled),
	MISSION_FIELD(random_variation_enabled),
	MISSION_FIELD(battle_length_index),
	MISSION_FIELD(locate_players_enabled),
	MISSION_FIELD(ai_opponents_enabled),
	MISSION_FIELD(player_flight_group_wave_mode),
	MISSION_FIELD(mission_time_limit_minutes),
	MISSION_FIELD(team_victory_time_limit_minutes),
	MISSION_FIELD(team_victory_time_limit_started),
	MISSION_FIELD(craft_impact_bounce_enabled),
	MISSION_FIELD(connected_player_count),
	MISSION_FIELD(max_connected_player_count_this_mission),
	MISSION_FIELD(runtime.team_scores),
	MISSION_FIELD(runtime.team_kill_stats),
	MISSION_FIELD(runtime.team_fg_inspected_captured_counts),
	MISSION_FIELD(runtime.team_fg_designation_code),
	MISSION_FIELD(runtime.global_primary_goal_status),
	MISSION_FIELD(runtime.global_goal_status_unused),
	MISSION_FIELD(runtime.global_bonus_goal_status),
	MISSION_FIELD(runtime.team_global_goal_state),
	MISSION_FIELD(runtime.team_goal_status),
	MISSION_FIELD(runtime.global_goal_trigger_counts),
	MISSION_FIELD(runtime.team_mission_completion_time_seconds),
	MISSION_FIELD(runtime.team_has_countable_craft),
	MISSION_FIELD(runtime.team_reinforcements_called),
	MISSION_FIELD(message_triggered),
	MISSION_FIELD(message_delay_countdown),
	MISSION_FIELD(global_unit_craft_count),
};

/* The pools the links point into. */
static struct object_record g_test_objects[4];
static struct mobile_object g_test_mobiles[3];
static struct craft_data g_test_craft[3];
static struct mobile_object_char_data g_test_char_data[3];
static struct warhead_guidance_state g_test_guidance[3];

static void use_test_pools(void)
{
	g_object_table = g_test_objects;
	g_mobile_object_pool_base = g_test_mobiles;
	g_craft_data_pool_base = g_test_craft;
	g_mobile_object_char_data_pool = g_test_char_data;
	g_projectile_guidance_states = g_test_guidance;
}

/* Fills size bytes from a seeded generator: neighbouring bytes differ in all
 * but rare cases, and two seeds give unrelated patterns. */
static void fill(void *data, size_t size, uint32_t seed)
{
	uint8_t *bytes = data;
	uint32_t state = seed * 2654435761u + 1u;
	for (size_t i = 0; i < size; ++i) {
		state = state * 1103515245u + 12345u;
		bytes[i] = (uint8_t)(state >> 16);
	}
}

/* The check fails, naming the field, when a listed field differs in size or in any byte between the
 * record and the live struct. */
static void check_fields(const struct field *fields, size_t count,
			 const void *record, const void *live)
{
	for (size_t i = 0; i < count; ++i) {
		const struct field *f = &fields[i];
		int same = f->record_size == f->live_size &&
			   memcmp((const uint8_t *)record + f->record_offset,
				  (const uint8_t *)live + f->live_offset,
				  f->record_size) == 0;
		if (!same) {
			fprintf(stderr,
				"field %s differs between the record and the live struct\n",
				f->name);
		}
		XVT_ASSERT_TRUE(same);
	}
}

/* One record type: its two sizes, its Encode/Decode pair, how to clear its
 * links on either side, and its listed fields. */
struct record_kind {
	const char *name;
	size_t record_size;
	size_t live_size;
	void (*encode)(void *record, const void *live);
	void (*decode)(void *live, const void *record);
	void (*clear_live_links)(void *live);
	void (*clear_record_links)(void *record);
	const struct field *fields;
	size_t field_count;
};

static void encode_object(void *record, const void *live)
{
	xvt_snapshot_encode_object_record(record, live);
}

static void decode_object(void *live, const void *record)
{
	xvt_snapshot_decode_object_record(live, record);
}

static void clear_object_live(void *live)
{
	((struct object_record *)live)->mobj = NULL;
}

static void clear_object_record(void *record)
{
	((struct xvt_snapshot_object_record *)record)->mobj = 0;
}

static void encode_mobile(void *record, const void *live)
{
	xvt_snapshot_encode_mobile_object(record, live);
}

static void decode_mobile(void *live, const void *record)
{
	xvt_snapshot_decode_mobile_object(live, record);
}

static void clear_mobile_live(void *live)
{
	struct mobile_object *mobile = live;
	mobile->p_warhead_guidance = NULL;
	mobile->p_craft = NULL;
	mobile->p_char_data = NULL;
}

static void clear_mobile_record(void *record)
{
	struct xvt_snapshot_mobile_object *mobile = record;
	mobile->p_warhead_guidance = 0;
	mobile->p_craft = 0;
	mobile->p_char_data = 0;
}

static void encode_craft(void *record, const void *live)
{
	xvt_snapshot_encode_craft_data(record, live);
}

static void decode_craft(void *live, const void *record)
{
	xvt_snapshot_decode_craft_data(live, record);
}

static void clear_craft_live(void *live)
{
	struct craft_data *craft = live;
	for (int i = 0; i < 16; ++i) {
		craft->turret_object_links[i] = NULL;
	}
	craft->effective_ai_object_link = NULL;
}

static void clear_craft_record(void *record)
{
	struct xvt_snapshot_craft_data *craft = record;
	for (int i = 0; i < 16; ++i) {
		craft->turret_object_links[i] = 0;
	}
	craft->effective_ai_object_link = 0;
}

static void encode_char(void *record, const void *live)
{
	xvt_snapshot_encode_mobile_object_char_data(record, live);
}

static void decode_char(void *live, const void *record)
{
	xvt_snapshot_decode_mobile_object_char_data(live, record);
}

static void encode_player(void *record, const void *live)
{
	xvt_snapshot_encode_player_data(record, live);
}

static void decode_player(void *live, const void *record)
{
	xvt_snapshot_decode_player_data(live, record);
}

static void encode_mission(void *record, const void *live)
{
	xvt_snapshot_encode_flight_mission_state(record, live);
}

static void decode_mission(void *live, const void *record)
{
	xvt_snapshot_decode_flight_mission_state(live, record);
}

/* Character, player and mission-state records carry no links. */
static void no_links(void *data) { (void)data; }

static const struct record_kind k_kinds[] = {
	{"object", sizeof(struct xvt_snapshot_object_record),
	 sizeof(struct object_record), encode_object, decode_object,
	 clear_object_live, clear_object_record, k_object_fields,
	 sizeof k_object_fields / sizeof k_object_fields[0]},
	{"mobile", sizeof(struct xvt_snapshot_mobile_object),
	 sizeof(struct mobile_object), encode_mobile, decode_mobile,
	 clear_mobile_live, clear_mobile_record, k_mobile_fields,
	 sizeof k_mobile_fields / sizeof k_mobile_fields[0]},
	{"craft", sizeof(struct xvt_snapshot_craft_data),
	 sizeof(struct craft_data), encode_craft, decode_craft,
	 clear_craft_live, clear_craft_record, k_craft_fields,
	 sizeof k_craft_fields / sizeof k_craft_fields[0]},
	{"character", sizeof(struct xvt_snapshot_mobile_object_char_data),
	 sizeof(struct mobile_object_char_data), encode_char, decode_char,
	 no_links, no_links, k_char_fields,
	 sizeof k_char_fields / sizeof k_char_fields[0]},
	{"player", sizeof(struct xvt_snapshot_player_data),
	 sizeof(struct player_data), encode_player, decode_player, no_links,
	 no_links, k_player_fields,
	 sizeof k_player_fields / sizeof k_player_fields[0]},
	{"mission state", sizeof(struct xvt_snapshot_flight_mission_state),
	 sizeof(struct flight_mission_state), encode_mission, decode_mission,
	 no_links, no_links, k_mission_fields,
	 sizeof k_mission_fields / sizeof k_mission_fields[0]},
};

#define KIND_COUNT (sizeof k_kinds / sizeof k_kinds[0])

static void *allocate(size_t size)
{
	void *data = malloc(size);
	XVT_ASSERT_TRUE(data != NULL);
	return data;
}

/* Encode fills every byte of the record whatever it held before, and each listed field is the live
 * field's bytes as they are. */
static void check_encode_copies_fields(void)
{
	use_test_pools();
	for (size_t k = 0; k < KIND_COUNT; ++k) {
		const struct record_kind *kind = &k_kinds[k];
		for (uint32_t seed = 1; seed <= 2; ++seed) {
			void *live = allocate(kind->live_size);
			uint8_t *zeros = allocate(kind->record_size);
			uint8_t *ones = allocate(kind->record_size);
			fill(live, kind->live_size, seed);
			kind->clear_live_links(live);
			memset(zeros, 0x00, kind->record_size);
			memset(ones, 0xFF, kind->record_size);
			kind->encode(zeros, live);
			kind->encode(ones, live);
			if (memcmp(zeros, ones, kind->record_size) != 0) {
				fprintf(stderr,
					"the %s record keeps bytes Encode did not write\n",
					kind->name);
			}
			XVT_ASSERT_INT_EQ(
				memcmp(zeros, ones, kind->record_size), 0);
			check_fields(kind->fields, kind->field_count, zeros,
				     live);
			free(live);
			free(zeros);
			free(ones);
		}
	}
}

/* Decode writes each listed field back as the record holds it. */
static void check_decode_writes_fields(void)
{
	use_test_pools();
	for (size_t k = 0; k < KIND_COUNT; ++k) {
		const struct record_kind *kind = &k_kinds[k];
		for (uint32_t seed = 3; seed <= 4; ++seed) {
			void *record = allocate(kind->record_size);
			void *live = allocate(kind->live_size);
			fill(record, kind->record_size, seed);
			kind->clear_record_links(record);
			memset(live, 0xA5, kind->live_size);
			kind->decode(live, record);
			check_fields(kind->fields, kind->field_count, record,
				     live);
			free(record);
			free(live);
		}
	}
}

/* A record decoded into a live struct and encoded again gives the same bytes,
 * whatever the live struct held before: native padding never reaches the
 * record. */
static void check_round_trip(void)
{
	use_test_pools();
	for (size_t k = 0; k < KIND_COUNT; ++k) {
		const struct record_kind *kind = &k_kinds[k];
		void *live = allocate(kind->live_size);
		void *other = allocate(kind->live_size);
		uint8_t *record = allocate(kind->record_size);
		uint8_t *again = allocate(kind->record_size);
		fill(live, kind->live_size, 5);
		kind->clear_live_links(live);
		kind->encode(record, live);

		memset(other, 0x5A, kind->live_size);
		kind->decode(other, record);
		kind->encode(again, other);
		if (memcmp(again, record, kind->record_size) != 0) {
			fprintf(stderr,
				"the %s record does not survive a round trip\n",
				kind->name);
		}
		XVT_ASSERT_INT_EQ(memcmp(again, record, kind->record_size), 0);
		free(live);
		free(other);
		free(record);
		free(again);
	}
}

/* An object's mobile record is linked by its byte offset in the mobile record array, plus 1. */
static void check_object_link(void)
{
	use_test_pools();
	struct object_record live;
	memset(&live, 0, sizeof live);

	live.mobj = &g_test_mobiles[2];
	struct xvt_snapshot_object_record record;
	xvt_snapshot_encode_object_record(&record, &live);
	XVT_ASSERT_INT_EQ(record.mobj,
			  2 * sizeof(struct xvt_snapshot_mobile_object) + 1);
	live.mobj = &g_test_mobiles[0];
	xvt_snapshot_encode_object_record(&record, &live);
	XVT_ASSERT_INT_EQ(record.mobj, 1);
	live.mobj = NULL;
	xvt_snapshot_encode_object_record(&record, &live);
	XVT_ASSERT_INT_EQ(record.mobj, 0);

	record.mobj =
		(uint32_t)(1 * sizeof(struct xvt_snapshot_mobile_object) + 1);
	xvt_snapshot_decode_object_record(&live, &record);
	XVT_ASSERT_TRUE(live.mobj == &g_test_mobiles[1]);
	record.mobj = 0;
	xvt_snapshot_decode_object_record(&live, &record);
	XVT_ASSERT_TRUE(live.mobj == NULL);
}

/* A mobile record links its guidance, craft and character records the same way,
 * each by its offset in its own pool's record array. */
static void check_mobile_links(void)
{
	use_test_pools();
	struct mobile_object live;
	memset(&live, 0, sizeof live);

	live.p_warhead_guidance = &g_test_guidance[0];
	live.p_craft = &g_test_craft[1];
	live.p_char_data = &g_test_char_data[2];
	struct xvt_snapshot_mobile_object record;
	xvt_snapshot_encode_mobile_object(&record, &live);
	XVT_ASSERT_INT_EQ(record.p_warhead_guidance, 1);
	XVT_ASSERT_INT_EQ(record.p_craft,
			  1 * sizeof(struct xvt_snapshot_craft_data) + 1);
	XVT_ASSERT_INT_EQ(
		record.p_char_data,
		2 * sizeof(struct xvt_snapshot_mobile_object_char_data) + 1);

	memset(&live, 0, sizeof live);
	xvt_snapshot_decode_mobile_object(&live, &record);
	XVT_ASSERT_TRUE(live.p_warhead_guidance == &g_test_guidance[0]);
	XVT_ASSERT_TRUE(live.p_craft == &g_test_craft[1]);
	XVT_ASSERT_TRUE(live.p_char_data == &g_test_char_data[2]);

	/* A guidance link further into its pool comes back to the same entry. */
	live.p_warhead_guidance = &g_test_guidance[2];
	xvt_snapshot_encode_mobile_object(&record, &live);
	live.p_warhead_guidance = NULL;
	xvt_snapshot_decode_mobile_object(&live, &record);
	XVT_ASSERT_TRUE(live.p_warhead_guidance == &g_test_guidance[2]);

	live.p_warhead_guidance = NULL;
	live.p_craft = NULL;
	live.p_char_data = NULL;
	xvt_snapshot_encode_mobile_object(&record, &live);
	XVT_ASSERT_INT_EQ(record.p_warhead_guidance, 0);
	XVT_ASSERT_INT_EQ(record.p_craft, 0);
	XVT_ASSERT_INT_EQ(record.p_char_data, 0);
	live.p_craft = &g_test_craft[0];
	xvt_snapshot_decode_mobile_object(&live, &record);
	XVT_ASSERT_TRUE(live.p_warhead_guidance == NULL);
	XVT_ASSERT_TRUE(live.p_craft == NULL);
	XVT_ASSERT_TRUE(live.p_char_data == NULL);
}

/* A craft record links 16 turret objects and one AI object in the object table,
 * by their offsets in the object record array. */
static void check_craft_links(void)
{
	use_test_pools();
	struct craft_data *live = allocate(sizeof *live);
	struct xvt_snapshot_craft_data *record = allocate(sizeof *record);
	memset(live, 0, sizeof *live);
	for (int i = 0; i < 16; ++i) {
		live->turret_object_links[i] =
			i % 5 == 4 ? NULL : &g_test_objects[i % 5];
	}
	live->effective_ai_object_link = &g_test_objects[3];
	xvt_snapshot_encode_craft_data(record, live);
	for (int i = 0; i < 16; ++i) {
		uint32_t expected =
			i % 5 == 4
				? 0
				: (uint32_t)((i %
					      5) * sizeof(struct
							  xvt_snapshot_object_record) +
					     1);
		XVT_ASSERT_INT_EQ(record->turret_object_links[i], expected);
	}
	XVT_ASSERT_INT_EQ(record->effective_ai_object_link,
			  3 * sizeof(struct xvt_snapshot_object_record) + 1);

	memset(live, 0, sizeof *live);
	xvt_snapshot_decode_craft_data(live, record);
	for (int i = 0; i < 16; ++i) {
		XVT_ASSERT_TRUE(live->turret_object_links[i] ==
				(i % 5 == 4 ? NULL : &g_test_objects[i % 5]));
	}
	XVT_ASSERT_TRUE(live->effective_ai_object_link == &g_test_objects[3]);

	record->effective_ai_object_link = 0;
	xvt_snapshot_decode_craft_data(live, record);
	XVT_ASSERT_TRUE(live->effective_ai_object_link == NULL);
	free(live);
	free(record);
}

/* Decode does not range-check a link: one just past the pool gives the pointer
 * just past the pool's last entry, not none and not a clamped entry. */
static void check_links_not_range_checked(void)
{
	use_test_pools();
	struct xvt_snapshot_object_record record;
	memset(&record, 0, sizeof record);
	record.mobj =
		(uint32_t)(3 * sizeof(struct xvt_snapshot_mobile_object) + 1);
	struct object_record live;
	xvt_snapshot_decode_object_record(&live, &record);
	XVT_ASSERT_TRUE(live.mobj == g_test_mobiles + 3);
}

int main(void)
{
	check_encode_copies_fields();
	check_decode_writes_fields();
	check_round_trip();
	check_object_link();
	check_mobile_links();
	check_craft_links();
	check_links_not_range_checked();
	return 0;
}
