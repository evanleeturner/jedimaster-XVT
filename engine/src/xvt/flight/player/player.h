#ifndef XVT_FLIGHT_PLAYER_PLAYER_H
#define XVT_FLIGHT_PLAYER_PLAYER_H

#include <stdint.h>

#include "xvt/flight/craft.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct player_view_state {
	/* Camera world X for the player's view or map; many functions write it,
	 * chiefly the view, map and camera control code. */
	int camera_world_x;
	int camera_world_y; /* Camera world Y; kept like camera_world_x. */
	/* Camera world Z, also the map camera's height; kept like
	 * camera_world_x. */
	int camera_world_z;
	/* Object the camera follows: the player's craft in the cockpit, the
	 * target on the target camera; UINT16_MAX for a free camera. */
	uint16_t camera_focus_obj_idx;
	/* Object a map-camera key keeps the free camera looking at, UINT16_MAX
	 * for none; zooming moves toward it and keeps clear of it. */
	uint16_t aim_target_idx;
	/* View pitch, 65,536 units a circle; the view and map code set the
	 * three view angles. */
	int16_t view_pitch;
	int16_t view_yaw;  /* View yaw; kept like view_pitch. */
	int16_t view_roll; /* View roll; kept like view_pitch. */
	/* Fourth angle flight_view_update_player_camera passes to
	 * fview_build_camera_orient; no game code writes it. */
	int16_t view_angle_d;
	/* Look offset of the view from the craft's nose, passed to
	 * fview_build_camera_orient; input, view keys and the map move it. */
	int16_t hud_aim_x;
	int16_t hud_aim_y; /* Second look offset; kept like hud_aim_x. */
	/* HUD view the player shows (HUD_VIEW_ value), set first by
	 * hud_set_hud_view_state; hud_force_player_view_state sets 0xFF to force a
	 * change. */
	uint8_t hud_state_live;
	/* Copy of hud_state_live made at the end of hud_set_hud_view_state, so it
	 * holds the previous view while the display is rebuilt. */
	uint8_t hud_state_mirror;
	/* 0 or 8; the keypad 0 key flips it and sets hud_aim_x to it shifted left
	 * 10. */
	uint8_t hud_aim_x_snap_state;
	/* hud_state_live kept when an outside camera takes over from the cockpit;
	 * player_update_hud_view_for_camera_focus restores it. */
	uint8_t saved_hud_state_byte;
	/* No game code reads or writes it; only the modern build's snapshot
	 * copies it. */
	uint8_t unused20;
	/* hud_aim_x kept with saved_hud_state_byte and restored with it. */
	int16_t saved_hud_aim_x;
	/* hud_aim_y kept with saved_hud_state_byte and restored with it. */
	int16_t saved_hud_aim_y;
	/* Nonzero while the stick drives the camera instead of the craft, as
	 * after the craft is lost or the player is out of the mission. */
	int16_t player_input_blocked;
	/* Zoom step player_update_flight_controls_and_camera sets for the camera's
	 * mode, shrinking back to 32 without zoom keys. */
	int16_t camera_distance_step;
	/* 1 while the view is outside the cockpit. */
	uint16_t external_camera_active;
	/* Distance of the outside camera from what it follows: 1024 on binding,
	 * changed by the zoom and camera keys. */
	int camera_distance;
	/* 1 while the target camera is on, set by its key in
	 * flight_process_player_actions, else 0. */
	int16_t target_camera_active;
	/* player_update_hud_view_for_camera_focus fills all 60 with view_roll; no
	 * game code reads it. */
	int16_t camera_roll_history[60];
	/* Filled like camera_roll_history with view_pitch; no game code reads
	 * it. */
	int16_t camera_pitch_history[60];
	/* Filled like camera_roll_history with view_yaw; no game code reads it. */
	int16_t camera_yaw_history[60];
	/* No game code reads or writes it; only the modern build's snapshot
	 * copies it. */
	uint16_t unused199;
};

struct player_hyperspace_runtime {
	/* Ticks since the current hyperspace jump stage began:
	 * flight_object_update_player_hyperspace_transition adds g_elapsed_ticks and
	 * sets 0 between stages; player_handle_hyperspace_command sets 0. */
	unsigned int phase_elapsed_ticks;
};

struct player_saved_craft_settings {
	/* The last craft's throttle_speed, restored on binding when the
	 * signature matched or a previous craft was given. */
	uint16_t throttle_speed;
	/* The last craft's laser recharge level. */
	power_recharge_level laser_recharge_level;
	/* The last craft's shield recharge level. */
	power_recharge_level shield_recharge_level;
	power_recharge_level
		beam_level; /* The last craft's beam_recharge_level. */
	/* The last craft's shield distribution. */
	shield_distribution_mode shield_distrib_mode;
	/* The last craft's laser link mode per group. */
	uint8_t laser_link_mode[2];
	/* The low two bits of the last craft's launcher flags. */
	uint8_t warhead_launcher_flags[2];
};

struct player_mission_runtime_stats {
	/* Mission score: kills and goals add to it, friendly kills and
	 * penalties take from it; zeroed at spawn when a craft is bound to the
	 * player. */
	int mission_score;
	/* Promotion points this mission, the Better total of
	 * mission_credit_destruction_damage_contributors, less 500 for each
	 * friendly destroyed. */
	int rating_promo_points;
	/* The Worse total of mission_credit_destruction_damage_contributors plus 4
	 * a destroyed mine; the career takes them only while its own worse
	 * total is below half the promotion threshold. */
	int worse_rating_promo_points;
	int field_0c; /* Set to 0 at spawn; nothing reads it. */
	int field_10; /* Set to 0 at spawn; nothing reads it. */
	/* Place among the teams in which the player's team met its primary
	 * goal, set by mission_update_logic; nothing reads it. */
	int primary_goal_finish_place;
	/* Laser shots this player fired (laser_firelasersystem); the career
	 * adds them. */
	uint16_t laser_shots_fired;
	/* Laser hits this player scored (mission_record_projectile_hit_stats); the
	 * career adds them. */
	uint16_t laser_hits_scored;
	/* Ion shots this player fired; kept like laser_shots_fired. */
	uint16_t ion_shots_fired;
	/* Ion hits this player scored; kept like laser_hits_scored. */
	uint16_t ion_hits_scored;
};

#pragma pack(push, 1)

struct per_mission_kills {
	/* Warhead hits this player scored
	 * (mission_record_projectile_hit_stats). */
	uint16_t warhead_hits;
	/* Craft this player inspected (collide_collisions); nothing reads
	 * it. */
	uint16_t num_craft_inspected;
	/* Special cargo craft this player inspected (collide_collisions). */
	uint16_t num_special_inspected;
	/* Kills by victim flight group where this player did at least 0xAAAA /
	 * 65,536 of the damage (mission_credit_player_kill_contribution). */
	uint16_t kills_full_on_flight_group[48];
	/* Kills by victim flight group where this player did at least 0x5999 /
	 * 65,536 of the damage. */
	uint16_t kills_shared_on_flight_group[48];
	/* Kills by victim flight group where this player did at least 0x0CCC /
	 * 65,536 of the damage. */
	uint16_t kills_assist_on_flight_group[48];
	/* Full kills of craft flown by players, by the victim's pilot_rating. */
	uint16_t kills_full_on_player_rating[25];
	/* Shared kills of craft flown by players, by the victim's
	 * pilot_rating. */
	uint16_t kills_shared_on_player_rating[25];
	/* Assists on craft flown by players, by the victim's pilot_rating. */
	uint16_t kills_assist_on_player_rating[25];
	/* Full kills of AI craft, by the victim flight group's AI level. */
	uint16_t kills_full_on_ai_rating[6];
	/* Shared kills of AI craft, by AI level. */
	uint16_t kills_shared_on_ai_rating[6];
	/* Assists on AI craft, by AI level. */
	uint16_t kills_assist_on_ai_rating[6];
	/* Full kills of each other player's craft. */
	uint16_t kills_full_on_player[8];
	/* Shared kills of each other player's craft. */
	uint16_t kills_shared_on_player[8];
	/* Craft of its own or an allied team this player destroyed with at
	 * least a shared part of the damage. */
	uint16_t friendlies_killed;
	/* Craft this player lost (mission_record_player_craft_loss). */
	uint16_t total_craft_losses;
	/* Losses where collisions did the most damage. */
	uint16_t losses_by_collisions;
	/* Losses where starships did the most damage. */
	uint16_t losses_by_starships;
	uint16_t losses_by_mines; /* Losses where mines did the most damage. */
	/* Full kills each other player made on this player's craft. */
	uint16_t kills_full_from_player[8];
	/* Shared kills each other player made on this player's craft. */
	uint16_t kills_shared_from_player[8];
	/* Losses of this player's craft credited in full to a flight group
	 * (mission_credit_destruction_damage_contributors). */
	uint16_t kills_full_from_flight_group[48];
	/* Losses of this player's craft with shared credit to a flight
	 * group. */
	uint16_t kills_shared_from_flight_group[48];
	/* This player's craft fully killed by players, by the killer's
	 * pilot_rating. */
	uint16_t killed_by_player_rating[25];
	/* This player's craft fully killed by AI craft, by the killer's AI
	 * level. */
	uint16_t killed_by_ai_rating[6];
};

#pragma pack(pop)
typedef char xvt_size_per_mission_kills
	[(sizeof(struct per_mission_kills) == 808) ? 1 : -1];

/* Stored as int8_t in the binary (IDB enum flight_chat_recipient_mode). */
typedef int8_t flight_chat_recipient_mode;

enum {
	FLIGHT_CHAT_RECIPIENT_INACTIVE = 0x0,
	FLIGHT_CHAT_RECIPIENT_TEAM = 0x1,
	FLIGHT_CHAT_RECIPIENT_ENEMY = 0x2,
	FLIGHT_CHAT_RECIPIENT_ALL = 0x3,
};

struct player_network_runtime_tail {
	/* Flight resolution the player runs, exchanged at flight start; the HUD
	 * reads it. */
	uint16_t flight_resolution_mode;
	/* The player's DirectPlay id, 0 for an empty slot; the career commit
	 * matches players by it. */
	int direct_play_id;
};

struct player_data {
	/* Object slot of the craft the player flies, -1 for none. */
	int object_index;
	/* Signature of the bound craft; a different one in its slot means the
	 * craft is gone. */
	unsigned int bound_object_signature;
	/* The player's rating, exchanged at flight start (g_pilot_data.rating
	 * when alone); weighs kills and the kill tables. */
	uint16_t pilot_rating;
	/* IFF of the flight group the player gets, set by mission_init. */
	int16_t iff;
	/* Team of the flight group the player gets, set by mission_init. */
	int16_t team;
	/* Flight group of the craft bound at spawn. */
	uint16_t bound_flight_group_idx;
	/* 0 not in the flight, 1 flying, 2 out of the mission. */
	uint8_t participation_state;
	/* 1 after the craft is destroyed, until the player is bound to another
	 * or leaves. */
	uint8_t awaiting_new_craft;
	/* The bound model's engine_glow_count, set at spawn; no game code reads
	 * it, only the modern build's snapshot copies it. */
	uint8_t bound_craft_engine_glow_count;
	/* Map camera, 0 off: bit 7 set while it opens or is open, the low 7
	 * bits a count up to 0x7F that rises by elapsed ticks with bit 7 set
	 * and falls toward 1 without it; 0xFF when watching after leaving the
	 * mission. */
	uint8_t map_camera_state;
	/* Hyperspace jump stage: 0 none, 1 lining up, 2 leaving. */
	uint8_t hyperspace_phase;
	/* Timing for the hyperspace jump. */
	struct player_hyperspace_runtime hyperspace_runtime;
	/* 1 to draw target boxes; only ever set to 1, at flight start and on a
	 * reset bind. */
	uint8_t target_box_enabled;
	/* Object the player has targeted, -1 for none; player_set_target sets it
	 * and player_validate_current_targets drops it. */
	int16_t current_target_object_idx;
	/* Object the cycle keys start from without a target: the target last
	 * dropped, -1 after binding. */
	int16_t target_cycle_start;
	/* Stored targets, -1 empty: Shift+F5 to F7 store the current target in
	 * slots 0 to 2, F5 to F7 recall it; a slot is cleared when its object's
	 * outcome is recorded. Slot 3 is never stored. */
	int16_t target_preset_slot[4];
	/* Warhead lock shown on the HUD: 0 none, 1 locking, 2 locked; cleared
	 * on a new target or craft. */
	uint8_t missile_lock_state;
	/* Cannon group or launcher selected, cycled by the W key. */
	uint8_t selected_weapon_bank;
	/* 0 with cannons selected, 1 with warheads. */
	uint8_t selected_weapon_mode;
	/* Mesh of the current target aimed at; craft_damage_component moves it
	 * on when that one is destroyed. */
	int16_t selected_target_component;
	/* -1 at flight start and on binding, 0 when a target is dropped; no
	 * game code reads it, only the modern build's snapshot copies it. */
	int16_t targeting_state;
	/* Object whose engine wash strikes the player's craft, -1 for none:
	 * collide_collisions clears it at each check and
	 * collide_apply_engine_wash_damage keeps the strongest; the sound code
	 * plays it. */
	int16_t engine_wash_source_obj_idx;
	/* Strength of that wash; the sound uses a tenth of it. */
	uint16_t engine_wash_strength;
	/* Throttle kept by Shift+9 or Shift+0 and restored by 9 or 0 with the
	 * three presets below; at spawn 21845 and full. */
	int16_t throttle_preset[2];
	/* Laser recharge level of each preset: maintenance and increased at
	 * spawn. */
	power_recharge_level laser_preset[2];
	/* Shield recharge level of each preset: maintenance at spawn. */
	power_recharge_level shield_preset[2];
	/* Beam recharge level of each preset: maintenance at spawn. */
	power_recharge_level beam_preset[2];
	/* Settings of the last craft flown, written by player_save_craft_settings
	 * and restored on binding. */
	struct player_saved_craft_settings saved_craft_settings;
	/* View the cockpit returns to, HUD only or forward; the view key sets
	 * it, forward at flight start. */
	uint8_t saved_hud_view_state;
	/* Request or order awaiting the player's answer, by id 1 to 9, 0 for
	 * none; hud_update_flight_message_panes clears it when pending_action_timer
	 * runs out. */
	uint8_t pending_action_id;
	int16_t pending_action_param; /* Object the pending request names. */
	/* Player who made the pending request. */
	uint16_t pending_action_issuer_player_idx;
	/* 1 while yaw input rolls the craft, as of the last controls update; a
	 * change clears the input smoothing. */
	int16_t yaw_roll_swap;
	/* Yaw input eased toward the stick by
	 * player_update_flight_controls_and_camera, before scaling by elapsed
	 * ticks. */
	int16_t smoothed_input_yaw;
	/* Pitch input eased like smoothed_input_yaw. */
	int16_t smoothed_input_pitch;
	/* g_flight_key_mods at the player's last update, to spot the target key's
	 * press and release. */
	uint16_t saved_key_mods;
	/* Ticks the target key has been held; a tap shorter than
	 * TARGET_TAP_MAX_TICKS picks a target. */
	uint16_t key_mods_hold_timer;
	/* Offset, in world axes, from the craft to its primary hardpoint,
	 * recomputed as the craft moves; the view uses it. */
	int hardpoint_world_x;
	/* Second hardpoint offset; kept like hardpoint_world_x. */
	int hardpoint_world_y;
	/* Third hardpoint offset; kept like hardpoint_world_x. */
	int hardpoint_world_z;
	int prev_hardpoint_world_x; /* hardpoint_world_x before its last update. */
	int prev_hardpoint_world_y; /* hardpoint_world_y before its last update. */
	int prev_hardpoint_world_z; /* hardpoint_world_z before its last update. */
	/* Score, promotion points and shots for this mission. */
	struct player_mission_runtime_stats mission_stats;
	/* Warheads this player fired this mission (laser_firemissile). */
	uint16_t warheads_fired;
	/* Kill and loss tallies for this mission. */
	struct per_mission_kills per_mission_kills;
	char msg_text[50];  /* Chat line being typed, followed by a cursor _. */
	uint8_t msg_length; /* Characters typed into msg_text, at most 48. */
	/* Who a chat line goes to; inactive when not typing. */
	flight_chat_recipient_mode chat_recipient_mode;
	struct player_view_state view_state; /* Camera and HUD view state. */
	struct player_network_runtime_tail
		network; /* Network identity and resolution. */
	/* Game time of the last input frame applied to the player's craft
	 * (flight_advance_one_step, xvt_flight_sim_advance in the modern build); 0
	 * at flight start. */
	int lockstep_timestamp;
	/* World X of the player's craft after that frame, kept to be put back
	 * before later frames are replayed. */
	int saved_x;
	int saved_y;		       /* World Y kept like saved_x. */
	int saved_z;		       /* World Z kept like saved_x. */
	int16_t saved_roll;	       /* Roll kept like saved_x. */
	int16_t saved_pitch;	       /* Pitch kept like saved_x. */
	int16_t saved_yaw;	       /* Yaw kept like saved_x. */
	uint16_t saved_lifetime_timer; /* lifetime_timer kept like saved_x. */
	int16_t saved_speed;	       /* Speed kept like saved_x. */
	int16_t saved_speed_remainder; /* speed_remainder kept like saved_x. */
	int16_t saved_roll_impulse_rate; /* roll_impulse_rate kept like saved_x. */
	/* Signature of the craft the saved state belongs to; a mismatch skips
	 * restoring it. */
	uint16_t saved_object_signature;
	/* awaiting_new_craft when the state was saved; a mismatch skips restoring
	 * it. */
	uint8_t saved_awaiting_new_craft;
	/* Ticks left to answer the pending request, 1416 when made;
	 * flight_update_timers counts it down. */
	int pending_action_timer;
	/* Ticks before the beam drains charge again, BEAM_FIRE_COOLDOWN_TICKS
	 * after each drain; flight_update_timers counts it down. */
	int beam_fire_cooldown_timer;
	int field_5b5; /* Set to 0 at flight start; no game code reads it. */
	/* g_game_time at which collide_collisions next checks engine wash; 0 at
	 * flight start. */
	int next_engine_wash_check_time;
};

struct player_flight_transient_timers {
	/* Ticks the ready message pane shows its message: 354, 1416 or 1652 by
	 * kind. */
	uint16_t ready_message_pane_timer;
	/* Ticks the shield hit flash shows, SHIELD_HIT_FLASH_TICKS after a
	 * shield hit. */
	uint16_t shield_hit_flash_timer;
	/* Ticks the hull hit flash shows; each hull hit adds
	 * HULL_HIT_FLASH_TICKS. */
	uint16_t hull_hit_flash_timer;
	/* Ticks the system message pane shows its message, 472 or 1888;
	 * hud_clear_ready_message_queue sets 0. */
	uint16_t system_message_pane_timer;
	/* Ticks the flight group message pane shows its message, 1888 when
	 * set. */
	uint16_t flight_group_message_pane_timer;
	/* Ticks before the target description is rebuilt, 1180 when set. */
	uint16_t target_description_refresh_timer;
	/* Ticks before the craft list page redraws, REFRESH_TICKS when set. */
	uint16_t mfd_craft_list_refresh_timer;
	/* Two-second countdown; expiry forces a mission-goals MFD redraw. */
	uint16_t mission_goals_refresh_timer;
};

extern struct player_flight_transient_timers
	g_player_flight_transient_timers[8];
extern int g_local_player;
extern struct player_data g_players[8];
extern char g_player_taunt_text[8][4][70];

struct remote_player_render_sample {
	/* 1 while the sample holds the remote craft as last drawn;
	 * flight_sync_capture_samples_and_restore_poses sets it. */
	int valid;
	/* Signature of the sampled craft; smoothing skips a craft whose
	 * signature differs. */
	uint16_t object_signature;
	int world_x; /* World X the craft was drawn at. */
	int world_y; /* World Y the craft was drawn at. */
	int world_z; /* World Z the craft was drawn at. */
	/* Change in roll since the previous sample, 0 without one. */
	int roll_delta;
	int pitch_delta; /* Change in pitch since the previous sample. */
	int yaw_delta;	 /* Change in yaw since the previous sample. */
	int16_t roll;	 /* Roll the craft was drawn at. */
	int16_t pitch;	 /* Pitch the craft was drawn at. */
	int16_t yaw;	 /* Yaw the craft was drawn at. */
	int16_t move_x;	 /* The craft's move vector X when sampled. */
	int16_t move_y;	 /* Move vector Y when sampled. */
	int16_t move_z;	 /* Move vector Z when sampled. */
	uint16_t speed_magnitude; /* The craft's speed when sampled. */
	/* The craft's simulation time stamp when sampled. */
	int sim_state_timestamp;
};

struct remote_player_saved_sim_pose {
	/* 1 while a simulated pose is saved for
	 * flight_sync_capture_samples_and_restore_poses to put back. */
	int valid;
	char gap4[2];	/* Nothing reads or writes it. */
	int world_x;	/* Simulated world X. */
	int world_y;	/* Simulated world Y. */
	int world_z;	/* Simulated world Z. */
	char gap18[12]; /* Nothing reads or writes it. */
	int16_t roll;	/* Simulated roll. */
	int16_t pitch;	/* Simulated pitch. */
	int16_t yaw;	/* Simulated yaw. */
	char gap36[12]; /* Nothing reads or writes it. */
};

int player_bind_to_available_craft(int player_idx, uint32_t previous_object_idx,
				   int preferred_object_signature,
				   int reset_targeting_state);
int player_unbind_from_current_craft(int player_index,
				     int require_multiple_craft,
				     int assign_ai_plan);
void player_save_craft_settings(int player_index);
void player_update_flight_controls_and_camera(int player_idx);
void flight_chat_handle_input(int player_idx);
int16_t player_find_nearest_objective(int goal_type, int player_idx);
int player_scale_control_step_by_elapsed_ticks(int16_t step);
void player_transfer_shield_bank_energy(uint16_t dst_bank, uint16_t src_bank,
					int player_idx);
void player_update_hud_view_for_camera_focus(int player_idx);
uint16_t player_pick_target_in_sight(int player_idx);
uint16_t player_cycle_target_any_iff(uint16_t current_obj_idx,
				     int16_t direction, int player_idx);
uint16_t player_cycle_target(uint16_t current_obj_idx, int16_t direction,
			     int player_idx, int iff_filter, int target_flags);
void player_set_target(int new_target_obj_idx, int player_idx);
uint16_t player_select_target_component_mesh(uint16_t target_obj_idx,
					     unsigned int player_idx);
int16_t player_apply_pitch_yaw_steps(int16_t pitch_angle_q16,
				     int16_t yaw_angle_q16,
				     uint16_t object_index,
				     struct craft_data *craft);
int16_t player_can_radio_command_craft(int player_idx);
void player_issue_ai_wingman_target_order(uint16_t target_obj_idx,
					  uint16_t command_id,
					  uint16_t response_index,
					  int player_idx);
int16_t player_find_attacker_of_target(uint16_t target_obj_idx,
				       int16_t excluded_obj_idx);
void player_start_post_destruction_state(int player_idx,
					 unsigned int source_object_index,
					 int source_player_idx);
void player_append_kill_message_actor_name(int slot, char *text,
					   int object_index);
void player_compute_polar_to_object_ref(int player_idx,
					unsigned int object_ref);
void player_end_flight_participation(int player_idx);
void player_emit_remote_player_departed_messages(int player_idx);
void player_validate_current_targets(int player_idx);
void player_validate_all_current_targets(void);
int player_has_available_owned_craft(int player_idx);
void player_update_participation_state(void);
int player_find_nearest_enemy_fighter(int player_idx, int excluded_object_idx);
void player_handle_hyperspace_command(struct craft_data *craft,
				      unsigned int player_idx);

#ifdef __cplusplus
}
#endif

#endif
