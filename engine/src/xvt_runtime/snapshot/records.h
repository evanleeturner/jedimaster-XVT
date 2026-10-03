#ifndef XVT_RUNTIME_SNAPSHOT_RECORDS_H
#define XVT_RUNTIME_SNAPSHOT_RECORDS_H

#include "xvt/flight/flight.h"
#include "xvt/flight/player/player.h"

/* Original 32-bit world records; live game structs stay naturally aligned.
 *
 * Each Xvt* record below is the packed on-image form of a live game struct; the checks after
 * the pack block pin the six top-level record sizes. The Encode/Decode pairs at the end
 * translate between the two forms. */
#pragma pack(push, 1)

struct xvt_snapshot_mobile_object_proximity_list {
	uint8_t count;
	int32_t contact_ticks[16];
	uint16_t obj_idx[16];
	int32_t rebuild_ticks;
};

struct xvt_snapshot_ai_controller {
	uint8_t current_order_slot;
	struct ai_order_progress order_progress;
	uint8_t skipped_to_order4;
	uint8_t running_plan_id;
	uint8_t current_plan_id;
	uint8_t waypoint_index;
	uint8_t saved_plan_id;
	int32_t think_interval;
	int32_t think_timer;
	int16_t saved_rand_seed;
	uint16_t target_obj_idx;
	uint16_t target_signature;
	uint16_t target_component;
	uint8_t has_live_target;
	int32_t aim_point_x;
	int32_t aim_point_y;
	int32_t aim_point_z;
	uint16_t candidate_target_idx;
	uint8_t escort_target_fg;
	uint16_t target_z_angle;
	uint16_t target_roll;
	uint16_t target_xy_angle;
	ai_maneuver_mode maneuver_mode;
	uint8_t maneuver_phase;
	int32_t maneuver_timer;
	int16_t secondary_maneuver_timer;
};

struct xvt_snapshot_ai_flight_state {
	uint16_t threat_obj_idx;
	uint16_t impact_obj_idx;
	uint8_t go_home_flag;
	uint8_t mission_aborted_flag;
	uint8_t depart_timer_flag;
	uint8_t depart_clock_hours;
	uint8_t depart_clock_minutes;
	uint8_t depart_clock_seconds;
	uint8_t warheads_fired_this_maneuver;
	uint8_t hits_this_maneuver;
	uint8_t boarded_accounting_done;
	uint8_t times_boarded;
	uint8_t docking_accounting_done;
	uint8_t docked_target_count;
	uint16_t docked_target_signatures[10];
	int16_t max_speed_cache;
	int16_t motion_scale;
	uint8_t climb_state;
	uint8_t dive_state;
	int16_t pitch_rate;
	int16_t pitch_accel;
	uint8_t pitch_state;
	uint8_t pitch_through_loop;
	uint16_t pitch_step_scale;
	int16_t roll_rate;
	int16_t roll_accel;
	uint8_t roll_state;
	uint16_t roll_step;
	int16_t turn_rate;
	int16_t turn_accel;
	uint8_t turn_state;
	int16_t turn_step;
	uint8_t formation_type;
	uint8_t separation;
};

struct xvt_snapshot_craft_damage_stats {
	uint16_t last_system_hit_time;
	int32_t damage_received_total;
	int32_t damage_received_by_player_owned_craft;
	int32_t damage_from_collision;
	int32_t damage_from_starship;
	int32_t damage_from_mine;
	int32_t damage_from_flight_group_amount[48];
	int32_t damage_from_player[8];
	int32_t damage_from_ai_skill[6];
	uint16_t installed_hud_feature_mask;
	uint16_t active_hud_feature_mask;
};

struct xvt_snapshot_player_view_state {
	int32_t camera_world_x;
	int32_t camera_world_y;
	int32_t camera_world_z;
	uint16_t camera_focus_obj_idx;
	uint16_t aim_target_idx;
	int16_t view_pitch;
	int16_t view_yaw;
	int16_t view_roll;
	int16_t view_angle_d;
	int16_t hud_aim_x;
	int16_t hud_aim_y;
	uint8_t hud_state_live;
	uint8_t hud_state_mirror;
	uint8_t hud_aim_x_snap_state;
	uint8_t saved_hud_state_byte;
	uint8_t unused20;
	int16_t saved_hud_aim_x;
	int16_t saved_hud_aim_y;
	int16_t player_input_blocked;
	int16_t camera_distance_step;
	uint16_t external_camera_active;
	int32_t camera_distance;
	int16_t target_camera_active;
	int16_t camera_roll_history[60];
	int16_t camera_pitch_history[60];
	int16_t camera_yaw_history[60];
	uint16_t unused199;
};

struct xvt_snapshot_player_network_runtime_tail {
	uint16_t flight_resolution_mode;
	int32_t direct_play_id;
};

struct xvt_snapshot_object_record {
	uint16_t object_signature;
	uint8_t genus_id;
	uint8_t object_type;
	int32_t world_x;
	int32_t world_y;
	int32_t world_z;
	int16_t yaw;
	int16_t pitch;
	int16_t roll;
	uint8_t flight_group_idx;
	uint16_t type_specific_word;
	uint8_t type_specific_byte[2];
	int32_t player_owner_idx;
	uint32_t mobj;
};

struct xvt_snapshot_mobile_object {
	uint8_t family;
	uint8_t effect_size;
	int32_t sim_state_timestamp;
	int32_t prev_world_x;
	int32_t prev_world_y;
	int32_t prev_world_z;
	struct xvt_snapshot_mobile_object_proximity_list proximity_list;
	int16_t roll_impulse_rate;
	uint16_t speed;
	uint16_t speed_remainder;
	uint32_t damage_amount;
	uint16_t lifetime_timer;
	uint16_t seconds_alive;
	uint16_t source_obj_idx;
	uint8_t source_object_type;
	uint8_t iff;
	uint8_t team;
	uint8_t node_switch_index;
	uint8_t move_vector_dirty;
	int16_t move_x;
	int16_t move_y;
	int16_t move_z;
	uint8_t orient_matrix_dirty;
	int16_t cached_fwd_x;
	int16_t cached_fwd_y;
	int16_t cached_fwd_z;
	int16_t cached_side_x;
	int16_t cached_side_y;
	int16_t cached_side_z;
	int16_t cached_up_x;
	int16_t cached_up_y;
	int16_t cached_up_z;
	uint32_t p_warhead_guidance;
	uint32_t p_craft;
	uint32_t p_char_data;
};

struct xvt_snapshot_craft_data {
	int32_t craft_index_in_group;
	uint8_t model_index;
	uint8_t leader_obj_idx;
	uint8_t unused006;
	craft_object_kind object_kind;
	uint8_t mission_accounting_done;
	uint16_t ai_skill;
	uint8_t unused00b[2];
	uint16_t pitch;
	uint16_t yaw;
	int16_t breakup_pitch_rate;
	int16_t breakup_yaw_rate;
	int32_t beam_effect_accum[5];
	uint8_t s_foil_state;
	struct xvt_snapshot_ai_controller ai_controller;
	uint16_t carried_object_index;
	uint16_t carrier_obj_idx;
	uint16_t last_attacker_obj_idx;
	uint16_t last_hit_mission_second;
	struct xvt_snapshot_ai_flight_state ai_flight;
	uint8_t craft_ordinal;
	int32_t push_accum_x;
	int32_t push_accum_y;
	int32_t push_accum_z;
	uint16_t throttle_speed;
	uint16_t engine_overdrive_off;
	int16_t commanded_speed;
	uint32_t hull_damage;
	uint32_t system_damage_hull_threshold;
	uint32_t hull_max;
	int16_t subsystem_damage;
	struct xvt_snapshot_craft_damage_stats damage_stats;
	craft_subsystem_flag system_flags;
	craft_subsystem_flag working_subsystems;
	int16_t weapon_fire_inhibit_timer;
	uint8_t unused_mission_flag;
	uint8_t not_disabled_accounting_suppress;
	uint8_t captured_by_flight_group;
	int8_t attacked_by_team[10];
	uint8_t identified_order_by_team[10];
	uint8_t boarding_state;
	char special_cargo_name[16];
	int32_t shield_energy[2];
	power_recharge_level shield_recharge_level;
	shield_distribution_mode shield_distrib_mode;
	uint8_t cannon_group_count;
	power_recharge_level laser_recharge_level;
	uint8_t laser_slot_count;
	struct craft_laser_state laser_state;
	uint8_t warhead_launcher_count;
	uint8_t warhead_slot_type_ids[2];
	int8_t warhead_launcher_flags[2];
	int16_t warhead_launcher_cooldown_ticks[2];
	int16_t warhead_lock_ticks;
	beam_type beam_type_id;
	power_recharge_level beam_recharge_level;
	uint16_t beam_charge;
	uint8_t beam_active;
	int16_t beam_output;
	int16_t beam_target_obj_idx;
	countermeasure_type cm_type_id;
	uint8_t cm_ammo_count;
	uint16_t chaff_active_seconds;
	uint16_t cm_fire_cooldown_timer;
	struct craft_weapon_stats weapon_stats;
	uint8_t unused256[73];
	uint16_t field_29f;
	uint8_t system_display_slot_by_system[DAMAGE_SYSTEM_ID_COUNT];
	uint16_t system_health[DAMAGE_SYSTEM_ID_COUNT];
	uint16_t system_repair_seconds[DAMAGE_SYSTEM_ID_COUNT];
	uint8_t component_state[50];
	uint8_t mesh_rotation[50];
	uint8_t component_hp[50];
	uint16_t player_command_avoid_target_obj_idx;
	struct craft_weapon_slot weapon_slots[16];
	uint16_t effective_ai_object_signature;
	struct turret_target_state turret_target_states[16];
	uint8_t unused3f2[44];
	uint32_t turret_object_links[16];
	uint32_t effective_ai_object_link;
};

struct xvt_snapshot_mobile_object_char_data {
	uint16_t skill_value;
	uint8_t unused02[2];
	struct xvt_snapshot_ai_controller ai_controller;
	uint8_t unused40[12];
};

struct xvt_snapshot_player_data {
	int32_t object_index;
	uint32_t bound_object_signature;
	uint16_t pilot_rating;
	int16_t iff;
	int16_t team;
	uint16_t bound_flight_group_idx;
	uint8_t participation_state;
	uint8_t awaiting_new_craft;
	uint8_t bound_craft_engine_glow_count;
	uint8_t map_camera_state;
	uint8_t hyperspace_phase;
	struct player_hyperspace_runtime hyperspace_runtime;
	uint8_t target_box_enabled;
	int16_t current_target_object_idx;
	int16_t target_cycle_start;
	int16_t target_preset_slot[4];
	uint8_t missile_lock_state;
	uint8_t selected_weapon_bank;
	uint8_t selected_weapon_mode;
	int16_t selected_target_component;
	int16_t targeting_state;
	int16_t engine_wash_source_obj_idx;
	uint16_t engine_wash_strength;
	int16_t throttle_preset[2];
	power_recharge_level laser_preset[2];
	power_recharge_level shield_preset[2];
	power_recharge_level beam_preset[2];
	struct player_saved_craft_settings saved_craft_settings;
	uint8_t saved_hud_view_state;
	uint8_t pending_action_id;
	int16_t pending_action_param;
	uint16_t pending_action_issuer_player_idx;
	int16_t yaw_roll_swap;
	int16_t smoothed_input_yaw;
	int16_t smoothed_input_pitch;
	uint16_t saved_key_mods;
	uint16_t key_mods_hold_timer;
	int32_t hardpoint_world_x;
	int32_t hardpoint_world_y;
	int32_t hardpoint_world_z;
	int32_t prev_hardpoint_world_x;
	int32_t prev_hardpoint_world_y;
	int32_t prev_hardpoint_world_z;
	struct player_mission_runtime_stats mission_stats;
	uint16_t warheads_fired;
	struct per_mission_kills per_mission_kills;
	char msg_text[50];
	uint8_t msg_length;
	flight_chat_recipient_mode chat_recipient_mode;
	struct xvt_snapshot_player_view_state view_state;
	struct xvt_snapshot_player_network_runtime_tail network;
	int32_t lockstep_timestamp;
	int32_t saved_x;
	int32_t saved_y;
	int32_t saved_z;
	int16_t saved_roll;
	int16_t saved_pitch;
	int16_t saved_yaw;
	uint16_t saved_lifetime_timer;
	int16_t saved_speed;
	int16_t saved_speed_remainder;
	int16_t saved_roll_impulse_rate;
	uint16_t saved_object_signature;
	uint8_t saved_awaiting_new_craft;
	int32_t pending_action_timer;
	int32_t beam_fire_cooldown_timer;
	int32_t field_5b5;
	int32_t next_engine_wash_check_time;
};

struct xvt_snapshot_mission_flight_runtime_state {
	int32_t team_scores[2][10];
	uint16_t team_kill_stats[4][10];
	uint16_t team_fg_inspected_captured_counts[2][10][48];
	uint8_t team_fg_designation_code[10][48];
	uint8_t global_primary_goal_status;
	uint16_t global_goal_status_unused;
	uint8_t global_bonus_goal_status;
	uint8_t team_global_goal_state[10][3];
	uint8_t team_goal_status[10][3];
	uint16_t global_goal_trigger_counts[2][10][3][4];
	int32_t team_mission_completion_time_seconds[10];
	uint8_t team_has_countable_craft[10];
	uint8_t team_reinforcements_called[10];
};

struct xvt_snapshot_flight_mission_state {
	uint8_t mission_end_pending;
	uint8_t proving_grounds_mode_active;
	uint8_t proving_grounds_craft_type;
	uint8_t proving_grounds_level;
	uint32_t proving_grounds_score;
	uint8_t unused08[2];
	uint16_t proving_grounds_checkpoints_passed;
	uint8_t unused0c[2];
	uint16_t proving_grounds_checkpoints_remaining;
	uint16_t proving_grounds_targets_destroyed;
	uint16_t proving_grounds_time_bonus;
	uint8_t difficulty;
	uint8_t collisions_enabled;
	uint8_t craft_jumping_enabled;
	uint8_t random_variation_enabled;
	uint8_t battle_length_index;
	uint8_t locate_players_enabled;
	uint8_t ai_opponents_enabled;
	uint8_t player_flight_group_wave_mode;
	uint8_t mission_time_limit_minutes;
	uint8_t team_victory_time_limit_minutes;
	uint8_t team_victory_time_limit_started;
	uint8_t craft_impact_bounce_enabled;
	int32_t connected_player_count;
	int32_t max_connected_player_count_this_mission;
	struct xvt_snapshot_mission_flight_runtime_state runtime;
	uint8_t message_triggered[64];
	uint8_t message_delay_countdown[64];
	int32_t global_unit_craft_count[11];
};

#pragma pack(pop)
typedef char xvt_snapshot_size_flight_mission_state
	[(sizeof(struct xvt_snapshot_flight_mission_state) == 3376) ? 1 : -1];
typedef char xvt_snapshot_size_object_record
	[(sizeof(struct xvt_snapshot_object_record) == 35) ? 1 : -1];
typedef char xvt_snapshot_size_mobile_object
	[(sizeof(struct xvt_snapshot_mobile_object) == 177) ? 1 : -1];
typedef char xvt_snapshot_size_craft_data
	[(sizeof(struct xvt_snapshot_craft_data) == 1122) ? 1 : -1];
typedef char xvt_snapshot_size_mobile_object_char_data
	[(sizeof(struct xvt_snapshot_mobile_object_char_data) == 76) ? 1 : -1];
typedef char xvt_snapshot_size_player_data
	[(sizeof(struct xvt_snapshot_player_data) == 1469) ? 1 : -1];

/* Encode fills every byte of record: the listed fields are copied as they are, and each link
 * to a live object or pool entry becomes its byte offset in the matching record array plus 1
 * (0 for NULL). Native padding never reaches the record.
 * Decode writes back the listed fields and turns each link into a pointer into the live array.
 * Live fields with no record field are left as they are. Decode does not range-check links;
 * a link outside its array yields an out-of-bounds pointer.
 * Links: object record -> mobile pool; mobile record -> guidance, craft and character pools;
 * craft record -> 16 turret objects and one AI object in g_object_table. Character, player and
 * mission-state records carry no links. */
void xvt_snapshot_encode_object_record(
	struct xvt_snapshot_object_record *record,
	const struct object_record *live);
void xvt_snapshot_decode_object_record(
	struct object_record *live,
	const struct xvt_snapshot_object_record *record);
void xvt_snapshot_encode_mobile_object(
	struct xvt_snapshot_mobile_object *record,
	const struct mobile_object *live);
void xvt_snapshot_decode_mobile_object(
	struct mobile_object *live,
	const struct xvt_snapshot_mobile_object *record);
void xvt_snapshot_encode_craft_data(struct xvt_snapshot_craft_data *record,
				    const struct craft_data *live);
void xvt_snapshot_decode_craft_data(
	struct craft_data *live, const struct xvt_snapshot_craft_data *record);
void xvt_snapshot_encode_mobile_object_char_data(
	struct xvt_snapshot_mobile_object_char_data *record,
	const struct mobile_object_char_data *live);
void xvt_snapshot_decode_mobile_object_char_data(
	struct mobile_object_char_data *live,
	const struct xvt_snapshot_mobile_object_char_data *record);
void xvt_snapshot_encode_player_data(struct xvt_snapshot_player_data *record,
				     const struct player_data *live);
void xvt_snapshot_decode_player_data(
	struct player_data *live,
	const struct xvt_snapshot_player_data *record);

void xvt_snapshot_encode_flight_mission_state(
	struct xvt_snapshot_flight_mission_state *record,
	const struct flight_mission_state *live);
void xvt_snapshot_decode_flight_mission_state(
	struct flight_mission_state *live,
	const struct xvt_snapshot_flight_mission_state *record);

#endif
