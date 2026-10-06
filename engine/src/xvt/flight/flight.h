#ifndef XVT_FLIGHT_FLIGHT_H
#define XVT_FLIGHT_FLIGHT_H

#include <stddef.h>
#include <stdint.h>

#include "xvt/flight/mission/mission.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

enum flight_simulation_timing {
	SIMULATION_TICKS_PER_SECOND = 236,
};

extern int g_local_transient_slot_start;
extern int g_local_debris_slot_end;

extern uint16_t g_cur_craft_model_index;

extern uint16_t g_elapsed_ticks;
extern uint16_t g_sim_steps_per_second;
extern int g_game_time;
extern int g_single_object_update_override_idx;
extern void *g_flight_main_window_handle;
extern uint32_t g_dynamic_music_last_update_ms;
extern int g_dynamic_music_track_remaining_ms;
extern uint8_t g_dynamic_music_state;
extern uint8_t g_dynamic_music_outcome_latched;

/* The flight's shared countdown timers, in ticks: flight_update_timers counts
 * each down by the step's ticks to 0, and the code each one paces reloads it
 * when it runs. Saved and restored with the world state. */
struct flight_global_countdown_timers {
	uint16_t unused_timer00; /* Not used by name. */
	/* Until mission_update_logic next checks the goals; reloaded with
	 * 236. */
	uint16_t mission_goal_evaluation_timer;
	/* Until flight_object_update_special_behavior next runs its pass; it
	 * reloads it with 29 (SPECIAL_BEHAVIOR_UPDATE_TICKS). */
	uint16_t special_behavior_update_timer;
	/* Until mission_update_flight_group_arrivals next checks arrival triggers;
	 * it reloads it with 236, and collide_damagecraft sets 0 when it
	 * destroys a player's craft. */
	uint16_t mission_arrival_trigger_scan_timer;
	/* Until mission_update_flight_group_arrivals next checks arrival delays;
	 * reloaded with 236. */
	uint16_t mission_arrival_delay_scan_timer;
	/* Until mission_update_logic next checks the mission messages; it
	 * reloads it with 1,180 (MISSION_MESSAGE_REFRESH_TICKS). */
	uint16_t mission_message_scan_timer;
	uint16_t unused_timer0c; /* Not used by name. */
	uint16_t unused_timer0e; /* Not used by name. */
	uint16_t unused_timer10; /* Not used by name. */
	uint16_t unused_timer12; /* Not used by name. */
	/* Until laser_weaponsfire next updates weapon power; reloaded with
	 * 236. */
	uint16_t weapon_power_update_timer;
};

extern struct flight_global_countdown_timers g_flight_global_countdown_timers;

enum flight_launch_argument {
	FLIGHT_LAUNCH_ARG_MISSION_PATH,
	FLIGHT_LAUNCH_ARG_FORMAL_NAME,
	FLIGHT_LAUNCH_ARG_PILOT_NAME,
	FLIGHT_LAUNCH_ARG_IS_HOST,
	FLIGHT_LAUNCH_ARG_MP_GAME_NAME,
	FLIGHT_LAUNCH_ARG_UNUSED,
	FLIGHT_LAUNCH_ARG_NUM_PLAYERS,
	FLIGHT_LAUNCH_ARG_COUNT,
};

/* The launch command line split into arguments by xvt_flight_entry_prepare. */
struct flight_launch_args {
	char *program_name; /* Set to "xtie"; nothing reads it. */
	char *sentinel;	    /* Set to "/trebla"; nothing reads it. */
	/* Pointers into the command line, one per FLIGHT_LAUNCH_ARG_ index: the
	 * mission file, the formal name and the player's name, whether this
	 * player hosts, the game name, one nothing reads, and the player
	 * count. */
	char *arguments[FLIGHT_LAUNCH_ARG_COUNT];
};

extern const uint16_t g_graphics_detail_distance_threshold_by_preset[4];
extern const uint16_t g_star_grid_divisor_by_graphics_detail_preset[4];
extern const uint16_t g_backdrops_enabled_by_graphics_detail_preset[4];
extern const uint16_t g_debris_enabled_by_graphics_detail_preset[4];
extern uint16_t g_graphics_detail_distance_threshold;
extern int g_generate_mission_palette;
extern uint8_t g_transform_light_direction_to_object_space;
extern uint16_t g_star_grid_divisor;
extern uint8_t g_backdrops_enabled;
extern uint8_t g_debris_enabled;
extern int g_flight_sim_side_effects_suppressed;
extern int g_flight_sfx_side_effect_gate;
extern uint8_t g_dormant_flight_region_session_early_return_flag;
extern uint8_t g_flight_alt_m_toggle;
extern uint8_t g_flight_conf_sfx_enabled;
extern uint8_t g_flight_conf_voice_enabled;
extern uint16_t g_local_beam_target_obj_idx;
extern const uint16_t g_subsystem_id_to_flag[12];
extern const uint8_t g_subsystem_message_arg_by_id[10];
extern const uint16_t g_subsystem_repair_duration[12];
extern const uint16_t g_subsystem_failure_hud_mask_by_random_slot[16];

/* The flight's mission state: options taken from the game configuration at
 * flight start, the proving grounds counters, the time limits and the mission's
 * runtime goal state. Saved and restored with the world state and folded into
 * the checksums; the snapshot code lists every field. */
struct flight_mission_state {
	/* 1 once the mission is to end: by the time limit
	 * (flight_update_timers), when no connected player is left or the local
	 * player has left (flight_recount_players_and_check_mission_end), on
	 * quitting, and from the network code. The simulation stops stepping at
	 * it. */
	uint8_t mission_end_pending;
	/* Nonzero while the mission is the proving grounds course; only
	 * mission_init writes it. */
	uint8_t proving_grounds_mode_active;
	/* Craft type flown on the proving grounds course: flight start sets 2
	 * for the traincourse launch option, else 0, before the mission
	 * loads. */
	uint8_t proving_grounds_craft_type;
	/* Proving grounds level: flight start sets 4 for traincourse, else
	 * 0. */
	uint8_t proving_grounds_level;
	/* Proving grounds score in points; flight start credits 10,000 for each
	 * level below the starting one. */
	uint32_t proving_grounds_score;
	/* Zeroed by mission_init and copied by the snapshot; nothing reads
	 * it. */
	uint8_t unused08[2];
	/* Course checkpoints passed this level. */
	uint16_t proving_grounds_checkpoints_passed;
	/* Zeroed by mission_init and copied by the snapshot; nothing reads
	 * it. */
	uint8_t unused0c[2];
	/* Course checkpoints left this level. */
	uint16_t proving_grounds_checkpoints_remaining;
	/* Course targets hit, counted by craft_damage_component. */
	uint16_t proving_grounds_targets_destroyed;
	/* Bonus points for time left when a level is finished, counted up by
	 * proving_grounds_update_course. */
	uint16_t proving_grounds_time_bonus;
	/* Game difficulty for the flight: g_game_config.difficulty (easy when
	 * past hard), or medium for a multiplayer combat engagement. */
	uint8_t difficulty;
	/* Collisions option (g_game_config.collisions); read by
	 * collide_collisions. */
	uint8_t collisions_enabled;
	/* Craft jumping option, for the J key (g_game_config.craft_jumping). */
	uint8_t craft_jumping_enabled;
	/* Random setup option (g_game_config.random_setup), forced to 0 in a
	 * combat engagement sequence; read by the mission setup and
	 * arrivals. */
	uint8_t random_variation_enabled;
	/* Battle length option (g_game_config.battle_length_index); set at flight
	 * start, and no flight code reads it here. */
	uint8_t battle_length_index;
	/* Locate players option (g_game_config.locate_players); read by the
	 * targeting, HUD and map code. */
	uint8_t locate_players_enabled;
	/* AI opponents option: g_game_config.ai_opponents in multiplayer, 1 solo;
	 * mission_init also writes it. */
	uint8_t ai_opponents_enabled;
	/* The g_game_config.craft_waves option; read by the flight group arrival
	 * and replacement code. */
	uint8_t player_flight_group_wave_mode;
	/* Mission time limit in minutes: the multiplayer option, or 255 solo,
	 * which mission_init replaces with the mission file's own limit; 0 for
	 * none. Starting the team victory limit replaces it
	 * (flight_update_timers). */
	uint8_t mission_time_limit_minutes;
	/* Minutes the mission goes on once one team is left or has met its
	 * goals: g_game_config.last_team_time_limit_minutes in multiplayer, 0
	 * solo. */
	uint8_t team_victory_time_limit_minutes;
	/* 1 once flight_update_timers has started the team victory limit. */
	uint8_t team_victory_time_limit_started;
	/* Set to 1 at flight start; collide_laserhitcraft reads it. */
	uint8_t craft_impact_bounce_enabled;
	/* Players connected: the session's count at flight start, then kept by
	 * mission_update_logic. */
	int32_t connected_player_count;
	/* Most players connected at once this mission; above 1 it lets the team
	 * victory limit start. */
	int32_t max_connected_player_count_this_mission;
	/* The mission's runtime goal and score state, set up by
	 * mission_init_flight_runtime_state. */
	struct mission_flight_runtime_state runtime;
	/* Per mission message, 1 once its trigger has fired
	 * (mission_update_logic). */
	uint8_t message_triggered[64];
	/* Per mission message, the delay left before it shows, counted down
	 * once per message check (1,180 ticks); set from the message's delay5s
	 * when it triggers. */
	uint8_t message_delay_countdown[64];
	/* Per global unit, the craft placed so far;
	 * mission_init_flight_group_object_slot counts up and numbers each craft by
	 * it. */
	int32_t global_unit_craft_count[11];
};

extern uint8_t *g_world_state_dup_buffer;
extern unsigned int g_world_checksum_region_lengths[16];
extern uint8_t *g_world_state_buffer;
extern int g_world_state_dup_size;
extern uint16_t g_world_state_dup_handle;
extern unsigned int g_world_checksum[16];
extern unsigned int g_world_state_size;
extern uint16_t g_world_state_handle;
extern int g_active_flight_player_count;
extern int g_flight_player_count;
extern int g_last_local_replay_input_timestamp;
extern unsigned int g_flight_update_duration_histogram[20];
extern int g_flight_packet_drop_score;
extern int g_predicted_frame_delta;
extern int g_flight_last_step_target_timestamp;
extern int g_flight_prev_host_packet_drop_count;
extern int g_flight_conf_no_pilot;
extern struct flight_mission_state g_flight_mission_state;
extern int g_flight_net_buffer_world_messages_until_checksum;
extern unsigned int g_flight_net_world_checksum_epoch;
extern int g_internet_play_enabled;
extern uint8_t g_world_state_reserved_byte;
extern int g_world_state_debris_slot_count;
extern int g_unused_world_state_serialized_dword;
extern int g_flight_conf_new_net;
extern int g_pre_flight_resolution_mode;
extern const float g_lod_config_max_value;
extern const float g_lod_config_scale_factor;
extern const float g_lod_config_curve_double;
extern const float g_lod_config_curve_threshold;
extern const float g_mipmap_config_scale_factor;
extern uint8_t g_dynamic_music_initial_start_minute_choices[4];
extern uint8_t g_dynamic_music_initial_start_second_choices[4];
extern const char g_pai_plan_resource_base_name[8];
extern int g_flight_conf_train_course;
extern int g_unused_flight_cmd_line_plus_switch_flag;
extern int g_flight_conf_no_launcher;
extern uint8_t g_flight_conf_music_enabled;
extern uint8_t g_unused_flight_runtime_block[768];
extern uint8_t g_flight_noise_table[512];
extern int g_unused_flight_startup_object_pass_state;
extern uint8_t g_unused_flight_message_runtime_state;
extern int g_unused_flight_session_reset_state;
extern xvt_file *g_unused_flight_debug_log_file;
extern int g_laser_fire_timestamp_tracking_enabled;
extern int g_unused_flight_transient_reset_state;
extern uint8_t g_unused_flight_network_block[48];
extern struct player_data g_local_player_snapshot_on_options_sync_failure;
extern int g_flight_in_progress_launch;
extern struct flight_launch_args g_flight_launch_args;
extern int g_flight_started_with_dash_arg;
extern uint32_t g_flight_sound_init_start_time_ms;

void flight_reset_unused_resume_slots(void);
void flight_update_timers(void);
void flight_update_dynamic_music_state(void);
void flight_alloc_world_state_buffers(void);
void flight_free_world_state_buffers(void);
void flight_save_world_state(void);
void flight_restore_world_state(void);
size_t flight_calculate_world_state_buffer_size(void);
void flight_checksum_world_state(int unused_arg0, int unused_arg1);
void flight_main_loop(int unused);
void flight_run_mission_loop(void);
int flight_update_active_player_count(void);
int flight_recount_players_and_check_mission_end(void);
int flight_compute_live_world_state_checksum(void);
unsigned int flight_checksum_buffer_rotate_xor(const void *data,
					       unsigned int size);

static __inline uint32_t flight_rotate_checksum_left(uint32_t checksum)
{
	return (checksum << 1) | (checksum >> 31);
}

void flight_process_player_actions(int player_idx);
char flight_apply_graphics_detail_preset(uint16_t preset);
int flight_main(char *mission_cmd_line);
int flight_update_and_focus_main_window(void);
int32_t flight_pump_window_messages(void);
void flight_update_craft_steering_and_speed(void);
void flight_slew_object_speed_toward_target(unsigned int object_idx,
					    int target_speed, int allow_decel,
					    int throttle_fraction);
void flight_accelerate_object_speed(int object_idx,
				    int acceleration_per_second);
void flight_decelerate_object_speed(int object_idx,
				    int deceleration_per_second);
void flight_update_dive_pullout_pitch_target(int object_idx);

#ifdef __cplusplus
}
#endif

#endif
