#include "xvt/flight/mission/mission.h"

#include <stdio.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/assets/model_bounds.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paiman.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/flight_object.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/net_session.h"
#include "xvt/render/backdrop.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/renderer.h"
#include "xvt/util/game_rand.h"
#include "xvt/util/memory.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"

/* Text that sprintf fills with debug lines: rating and promotion points,
 * team score and place, update-time histograms. Nothing reads it. 5
 * functions write it: fe_disk_io_commit_flight_results; in the original build
 * flight_run_mission_loop and mission_credit_destruction_damage_contributors; in
 * the modern build xvt_flight_frame_format_update_histogram and
 * xvt_flight_frame_render. */
// GLOBAL: XVT 0xA00750
char g_mission_debug_buffer[256] = {0};

/* Position, in mission units, of the next static object
 * mission_spawn_prepared_object creates, which multiplies it by 256. Only
 * mission_spawn_flight_group_static_objects writes it; also
 * g_prepared_spawn_mission_y and g_prepared_spawn_mission_z. */
// GLOBAL: XVT 0x99F970
int g_prepared_spawn_mission_x = 0;
/* Y of the next static object's position; see g_prepared_spawn_mission_x. */
// GLOBAL: XVT 0x99F974
int g_prepared_spawn_mission_y = 0;
/* Z of the next static object's position; see g_prepared_spawn_mission_x. */
// GLOBAL: XVT 0x99F978
int g_prepared_spawn_mission_z = 0;
/* Yaw byte of the next static object, which mission_spawn_prepared_object
 * shifts up 8 bits into 65,536 units per circle. Only
 * mission_spawn_flight_group_static_objects writes it: the group's yaw, or 0 for
 * debris. */
// GLOBAL: XVT 0x99F97C
uint16_t g_prepared_spawn_yaw_byte = 0;
/* Pitch byte of the next static object; see g_prepared_spawn_yaw_byte. */
// GLOBAL: XVT 0x99F97E
uint16_t g_prepared_spawn_pitch_byte = 0;
/* Roll byte of the next static object; see g_prepared_spawn_yaw_byte. */
// GLOBAL: XVT 0x99F980
uint16_t g_prepared_spawn_roll_byte = 0;
/* Object index each spawned craft takes as its leader_obj_idx, UINT8_MAX until
 * the round's first craft exists; mission_init_flight_group_object_slot copies it
 * into each craft and sets it to the first craft's slot.
 * mission_spawn_flight_group_wave_craft sets UINT8_MAX for a whole round;
 * paiman_dropoffmaneuver sets its basis object before it starts an arrival. */
// GLOBAL: XVT 0x99F982
uint8_t g_spawn_leader_obj_idx = 0;
/* Team of the craft being spawned, from the flight group; set by
 * mission_spawn_flight_group_wave_craft, read by mission_init_flight_group_object_slot
 * into the craft's mobile record. */
// GLOBAL: XVT 0x99F983
static uint8_t g_spawn_team_id = 0;
/* IFF of the craft being spawned, from the flight group; set by
 * mission_spawn_flight_group_wave_craft, read by
 * mission_init_flight_group_object_slot. */
// GLOBAL: XVT 0x99F984
static uint8_t g_spawn_iff = 0;
/* Formation spacing of the round being spawned: the group's formation_spacing,
 * or 0 from a mothership. Set by mission_spawn_flight_group_wave_craft;
 * mission_init_flight_group_object_slot uses 0 instead while
 * g_spawn_from_mothership_flag is set. */
// GLOBAL: XVT 0x99F985
static uint8_t g_spawn_formation_spacing = 0;
/* AI level (group_ai) of the round being spawned; set by
 * mission_spawn_flight_group_wave_craft, used by mission_init_flight_group_object_slot
 * for the craft's skill and think interval. */
// GLOBAL: XVT 0x99F987
static uint8_t g_spawn_group_ai = 0;
/* Yaw of the round being spawned, 65,536 units per circle: toward the
 * mothership's outside hangar point, toward mission point 5, or 0. Set by
 * mission_spawn_flight_group_wave_craft, read by
 * mission_init_flight_group_object_slot. */
// GLOBAL: XVT 0x99F988
static uint16_t g_spawn_yaw = 0;
/* 1 while the round being spawned arrives by hyperspace after the mission
 * start; mission_spawn_flight_group_wave_craft sets it to 0 and then 1 when it
 * moves the round back, and mission_init_flight_group_object_slot then makes the
 * out-of-hyperspace plan the craft's pending plan. */
// GLOBAL: XVT 0x99F98A
static uint8_t g_spawn_out_of_hyperspace_flag = 0;
/* status1 of the round being spawned; set by mission_spawn_flight_group_wave_craft.
 * mission_init_flight_group_object_slot reads it with g_spawn_status2 to adjust
 * warheads, shields, hyperdrive and turrets. */
// GLOBAL: XVT 0x99F98B
static uint8_t g_spawn_status1 = 0;
/* 1 while the round being spawned comes out of a mothership's hangar;
 * mission_spawn_flight_group_wave_craft sets it to 0 and then 1, and
 * mission_init_flight_group_object_slot then uses spacing 0 and makes the
 * from-mothership plan the craft's pending plan. */
// GLOBAL: XVT 0x99F98C
static uint8_t g_spawn_from_mothership_flag = 0;
/* Pitch of the round being spawned, 65,536 units per circle (0x4000 when no
 * point is faced). Set by mission_spawn_flight_group_wave_craft, read by
 * mission_init_flight_group_object_slot. */
// GLOBAL: XVT 0x99F98E
static uint16_t g_spawn_pitch = 0;
/* Number in its flight group of the craft being spawned, from 0. Only
 * mission_spawn_flight_group_wave_craft writes it, stepping through the round;
 * mission_init_flight_group_object_slot reads it. */
// GLOBAL: XVT 0x99F990
static uint16_t g_spawn_craft_ordinal = 0;
/* Object type of the craft just spawned; only
 * mission_init_flight_group_object_slot writes it. Read there and by
 * mission_spawn_flight_group_wave_craft for the arrival message. */
// GLOBAL: XVT 0x99F992
static uint8_t g_spawn_object_type = 0;
/* object_kind given to each spawned craft; mission_spawn_flight_group_wave_craft
 * sets it to CRAFT_OBJECT_KIND_ACTIVE, the only value it ever holds. */
// GLOBAL: XVT 0x9A1080
static craft_object_kind g_spawn_object_kind = CRAFT_OBJECT_KIND_ACTIVE;
/* status2 of the round being spawned; see g_spawn_status1. */
// GLOBAL: XVT 0x9A1081
static uint8_t g_spawn_status2 = 0;
/* World position of the round's first craft, in world units: the mothership's
 * inside hangar point or the group's mission point, moved back for a hyperspace
 * arrival. Only mission_spawn_flight_group_wave_craft writes it;
 * mission_init_flight_group_object_slot places each craft from it. Also
 * g_spawn_world_y and g_spawn_world_z. */
// GLOBAL: XVT 0x9A1084
static int g_spawn_world_x = 0;
/* Y of the round's first craft position; see g_spawn_world_x. */
// GLOBAL: XVT 0x9A1088
static int g_spawn_world_y = 0;
/* Z of the round's first craft position; see g_spawn_world_x. */
// GLOBAL: XVT 0x9A108C
static int g_spawn_world_z = 0;
/* Genus of the round being spawned, from the object type table; set by
 * mission_spawn_flight_group_wave_craft. mission_init_flight_group_object_slot takes
 * its slot range from g_object_slot_range_by_genus by it. */
// GLOBAL: XVT 0x9A1830
static uint8_t g_spawn_genus_id = 0;
/* IFF of the craft just spawned; only mission_init_flight_group_object_slot
 * writes it. Nothing reads it. */
// GLOBAL: XVT 0x9A1831
static uint8_t g_spawned_object_iff = 0;
/* Formation of the round being spawned: the group's formation, 0 from a
 * mothership, or 6 from one with more than 3 craft. Set by
 * mission_spawn_flight_group_wave_craft, read by
 * mission_init_flight_group_object_slot. */
// GLOBAL: XVT 0x9A1832
static uint8_t g_spawn_formation = 0;
/* 1 while mission_init_flight_runtime_state places the craft that start in
 * space, else 0; it is the only writer. While it is 1,
 * mission_init_flight_group_object_slot binds a player's craft to the player. */
// GLOBAL: XVT 0x556ECC
uint8_t g_initial_spawn_bind_player_craft_slots = 0;
/* Seed of the asteroid field's random draws. Flight start sets it to
 * ASTEROID_FIELD_RANDOM_SEED (flight_main_loop in the original build,
 * xvt_flight_loading_globals in the modern one); mission_init sets it to the
 * random state left after backdrop_generate_default_records;
 * mission_spawn_flight_group_static_objects draws debris from it and stores the
 * state it ends at. */
// GLOBAL: XVT 0x9A73F0
uint16_t g_asteroid_field_rand_seed = 0;
/* Signature the next created object gets, then incremented; set to 1 by
 * mission_init_flight_runtime_state. 5 functions write it:
 * mission_init_flight_runtime_state, mission_init_flight_group_object_slot,
 * mission_spawn_prepared_object, flight_restore_world_state in the original build
 * and xvt_snapshot_decode_prefix in the modern one. */
// GLOBAL: XVT 0x9A8D40
uint16_t g_next_object_signature = 0;
/* The loaded mission's header: counts of flight groups and messages, mission
 * type, IFF names, time limit and flags. 3 functions write it:
 * mission_load_file, flight_restore_world_state in the original build and
 * xvt_snapshot_decode_prefix in the modern one. */
// GLOBAL: XVT 0x9A2000
struct mission_header g_mission_header = {0};
/* Per flight group, its runtime state: arrival, rounds, outcome counts by
 * FLIGHT_GROUP_OUTCOME_ value, per-team counts and goal states. Reset by
 * mission_init_flight_runtime_state. Many functions write it, chiefly
 * mission_record_craft_outcome, mission_update_flight_group_arrivals,
 * mission_process_flight_group_wave_completion, mission_update_logic and the AI
 * order and maneuver code. */
// GLOBAL: XVT 0x9A20C0
struct mission_fg_runtime_stats g_mission_fg_stats[48];
/* Per team, the mission's global goals: entries 0 to 2 are the primary,
 * prevent and bonus goals mission_update_logic evaluates. Only
 * mission_load_file writes it. */
// GLOBAL: XVT 0x9A8080
struct global_goal g_mission_global_goals[10][7] = {{{0}}};
/* The mission's flight groups as loaded, with the player who owns each
 * (player_owner_idx, -1 for none). mission_load_file fills it and mission_init
 * adjusts craft, loadout, AI level and rounds for the players and settings;
 * many functions read it. */
// GLOBAL: XVT 0x9D8C30
struct mission_flight_group g_mission_flight_groups[48];
/* Per team, its name, allies and end-of-mission texts. Only mission_load_file
 * writes it; it also marks each team allied with itself. */
// GLOBAL: XVT 0x9FD440
struct team g_mission_teams[10] = {{0}};
/* Index of the flight group being spawned or scanned; the spawn functions
 * read it. 5 functions write it: mission_init, mission_init_flight_runtime_state
 * and mission_update_flight_group_arrivals as a loop index,
 * mission_process_flight_group_wave_completion and paiman_dropoffmaneuver
 * before a spawn. */
// GLOBAL: XVT 0x9A1834
uint16_t g_current_flight_group_idx = 0;
/* Mission time elapsed, in simulated seconds: flight_update_timers counts
 * subsecond_ticks down by the ticks elapsed and adds a second each
 * SIMULATION_TICKS_PER_SECOND ticks. mission_init sets it to 0. */
// GLOBAL: XVT 0x9ED228
struct mission_clock g_mission_elapsed_clock;
/* Time left on the mission's countdown, minutes and seconds;
 * flight_update_timers takes one off each simulated second until it reaches
 * 0:00. mission_init sets it from the time limit, 0 for none. */
// GLOBAL: XVT 0x9D8B70
struct mission_clock g_mission_countdown_clock = {{0, 0, 0}, 0, 0, 0, 0};
/* The mission's scripted messages. mission_init clears each text's first
 * byte and mission_load_file fills it; mission_update_logic sends them. */
// GLOBAL: XVT 0x9FE800
struct mission_message g_mission_messages[MISSION_MESSAGE_COUNT] = {0};
/* Per flight group, per goal, three memory handles of the goal's replacement
 * texts from the file, 0 for none. Only mission_load_file writes it;
 * mfd_draw_mission_goals_page draws them and mission_free_override_string_handles
 * frees them without clearing the entries. */
// GLOBAL: XVT 0x9D8120
uint16_t g_mission_fg_override_string_handles[48][8][3] = {0};
/* Per team, global goal and trigger, three memory handles of replacement
 * texts from the file, 0 for none; as g_mission_fg_override_string_handles. */
// GLOBAL: XVT 0x9E8F60
uint16_t g_global_goal_override_string_handles[10][7][4][3] = {0};
/* A world position, in world units, left by the function that last
 * resolved one; callers read it right after. 9 functions write it:
 * mission_resolve_object_or_mission_point_world_loc,
 * mission_resolve_formation_slot_world_loc, mission_spawn_flight_group_wave_craft,
 * flight_object_update_special_behavior, flight_map_draw_grid,
 * flight_map_draw_object_overlay, targeting_test_aim_cone,
 * targeting_project_object_or_mission_point and
 * targeting_compute_projected_object_extent. */
// GLOBAL: XVT 0xA07CDC
int g_world_loc_x = 0;
/* Y of the resolved world position; the same 9 functions write it as
 * g_world_loc_x. */
// GLOBAL: XVT 0xA07CD8
int g_world_loc_y = 0;
/* Z of the resolved world position; the same 9 functions write it as
 * g_world_loc_x. */
// GLOBAL: XVT 0xA07CD4
int g_world_loc_z = 0;
/* Format version of the loaded mission file: 12, 13, 14, or 0xFFFF for the
 * TIE format. mission_load_file reads it from the file;
 * flight_restore_world_state in the original build and
 * xvt_snapshot_decode_prefix in the modern one restore it. Some AI, damage and
 * spawn rules apply only to version 14. */
// GLOBAL: XVT 0xA08294
uint16_t g_mission_file_version = 0;
/* The header of a TIE-format mission, read and converted by
 * mission_load_file, its only user. */
// GLOBAL: XVT 0x556AA8
static struct e_mission_struct g_tie_mission_header = {0};
/* One TIE-format flight group record, read and converted by
 * mission_load_file, its only user. */
// GLOBAL: XVT 0x556C70
static struct efg_struct g_tie_flight_group = {0};
/* One TIE-format global goal record, read and converted by
 * mission_load_file, its only user. */
// GLOBAL: XVT 0x556D98
static struct e_mission_goal g_tie_mission_goal = {0};
/* Text of a version 10 flight group record. Only mission_load_file uses it,
 * in a branch that never runs. */
// GLOBAL: XVT 0x556DB8
static struct xvt_v10_flight_group_text g_xvt_v10_flight_group_text = {0};
/* One TIE-format message record, read and converted by mission_load_file, its
 * only user. */
// GLOBAL: XVT 0x556DE8
static struct tie_radio_message g_tie_radio_message = {0};
/* Header of a version 10 mission file. Only mission_load_file uses it, in a
 * branch that never runs. */
// GLOBAL: XVT 0x556E48
static struct xvt_v10_mission_header g_xvt_v10_mission_header = {0};
/* Rating an AI craft counts as, by its group_ai 0 to 5: 2, 4, 7, 9, 10 and 11.
 * Read only by mission_credit_destruction_damage_contributors. */
// GLOBAL: XVT 0x521158
static const int g_default_pilot_rating_by_ai_level[6] = {2, 4, 7, 9, 10, 11};
/* Points a craft is worth for its beam type, by beam_type_id 0 to 5. Read only
 * by mission_compute_craft_point_value. */
// GLOBAL: XVT 0x521170
static const int g_beam_type_point_value[6] = {0, 150, 150, 250, 50, 0};
/* Points a craft is worth for its countermeasure type, by cm_type_id 0 to 3.
 * Read only by mission_compute_craft_point_value. */
// GLOBAL: XVT 0x521188
static const int g_countermeasure_type_point_value[4] = {0, 150, 100, 150};
/* Replacement craft type mission_init gives the player groups of an AI
 * opponent team in a melee tournament, by the craft type of that team's
 * first player group, 0 to 23; types 0 and 17 to 23 map to 0. Read only by
 * mission_init. */
// GLOBAL: XVT 0x5241D0
static const uint8_t g_ai_opponent_craft_type_by_player_craft_type[24] = {
	0, 6, 16, 8, 16, 14, 1, 14, 3, 3, 3, 3,
	3, 3, 5,  5, 2,	 0,  0, 0,  0, 0, 0, 0,
};
/* Maps a goal's genus number, 0 to 11, to the genus_id of the object type
 * table and of g_str_goal_genus_names. Read by
 * mission_flight_group_matches_trigger_variable,
 * mission_object_matches_trigger_variable and goals_outputgoal. */
// GLOBAL: XVT 0x5241E8
const uint8_t g_genus_convert[12] = {0, 1, 3, 4, 2, 5, 8, 9, 2, 0, 0, 0};
/* Maps a goal's family number, 0 to 3, to the family_id of the object type
 * table and of g_str_goal_family_names; read by the same three functions as
 * g_genus_convert. */
// GLOBAL: XVT 0x5241F4
const uint8_t g_family_convert[4] = {0, 1, 2, 0};
/* Per mission condition (MISSION_COND_ value, 0 to 47), 1 when
 * mission_evaluate_condition counts craft for it, 0 when it tests something
 * else. */
// GLOBAL: XVT 0x521128
const uint8_t g_mission_condition_uses_count_by_condition[48] = {
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0,
};
/* Craft counted by the last counting condition mission_evaluate_condition
 * tested (the groups' totals), 0 after any other; only that function writes
 * it. mission_update_logic copies it into runtime.global_goal_trigger_counts. */
// GLOBAL: XVT 0x556354
uint16_t g_mission_condition_total_count = 0;
/* Craft that met the last counting condition mission_evaluate_condition
 * tested, 0 after any other; only that function writes it.
 * mission_update_logic copies it into runtime.global_goal_trigger_counts. */
// GLOBAL: XVT 0x556358
uint16_t g_mission_condition_current_count = 0;
/* Per flight group arrival_difficulty 0 to 7, the difficulty bits it arrives
 * in: 1 easy, 2 medium, 4 hard; 7 all, 6 and 3 two of them, 0 none. Read only
 * by mission_init_flight_runtime_state. */
// GLOBAL: XVT 0x524280
static const uint8_t g_fg_arrival_difficulty_masks[8] = {7, 1, 2, 4,
							 6, 3, 0, 0};
/* Per game difficulty, its bit for g_fg_arrival_difficulty_masks: 1, 2 and 4
 * for GAME_DIFFICULTY_EASY, MEDIUM and HARD (0 to 2), 0 above. Read only by
 * mission_init_flight_runtime_state. */
// GLOBAL: XVT 0x524288
static const uint8_t g_mission_difficulty_arrival_masks[8] = {1, 2, 4, 0,
							      0, 0, 0, 0};
/* 0 or 0x0400, the blinking flag ORed into g_render_object_ref with the local
 * player's target. flight_update_timers flips it each time
 * g_target_proximity_blink_timer runs out: set for 118 ticks and clear for 14
 * while the target is closer than 32 times its max_bounds_extent (the distance
 * is shifted down 5 bits before the test), the other way round otherwise.
 * mission_init_flight_runtime_state sets it to 0; those 2 functions
 * are its only writers. */
// GLOBAL: XVT 0x9D77BC
uint16_t g_target_proximity_blink_bit = 0;
/* Only mission_init_flight_runtime_state writes this, always 1, and its readers
 * act only on a value above 1 (with
 * g_dormant_flight_region_session_early_return_flag set), so in this build
 * their early return never runs. */
// GLOBAL: XVT 0x523440
uint8_t g_flight_runtime_state_initialized = 1;
/* Set to 15 by mission_init_flight_runtime_state, its only writer. Nothing
 * reads it. */
// GLOBAL: XVT 0x9A8C04
static uint16_t g_flight_frame_step_mirror = 0;

/* Returns a flag, the flight group's special_cargo_outcome entry for
 * FLIGHT_GROUP_OUTCOME_INSPECTED, which collide_collisions and
 * paiman_boardmaneuver set to 1 when the special cargo craft is inspected.
 * special_cargo_craft is ignored. Does not check the index. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x415A40
uint16_t mission_is_special_cargo_inspected(unsigned int flight_group_idx,
					    uint16_t special_cargo_craft)
{
	(void)special_cargo_craft;
	return g_mission_fg_stats[(uint16_t)flight_group_idx]
		.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_INSPECTED];
}

/* Runs the mission's goals and its scripted messages; does nothing in proving
 * grounds mode. The original build's flight_step_sim_to_time calls it every
 * simulation step; the modern build's xvt_flight_sim_step_to_time only on steps
 * where xvt_flight_timing_reference_due holds or a mission end is pending. When
 * g_flight_global_countdown_timers.mission_goal_evaluation_timer is 0 or a mission
 * end is pending, it recounts g_flight_mission_state.connected_player_count
 * (raising max_connected_player_count_this_mission), evaluates every pending flight
 * group goal per team into g_mission_fg_stats[].goal_state, adding 250 times the
 * goal's points to the team's bonus score on success (per-craft bonus goals
 * excepted), evaluates each team's three global goals into
 * runtime.team_global_goal_state and runtime.global_goal_trigger_counts, adding 250
 * times raw_points on a first success, and merges both into
 * runtime.team_goal_status. On a new primary success it adds 10,000 points (in a
 * combat mission only when no hostile team finished first), stores the
 * completion time and each team player's finish place; on a new bonus success
 * it adds 2,500. Status changes send in-flight messages (on a primary success
 * to the team's players, and in melee and MISSION_TYPE_SIMULATOR_1 missions to
 * the others too; otherwise to the local player) and the team's end-of-mission
 * texts and voices to the local player. It then sets that timer to
 * SIMULATION_TICKS_PER_SECOND ticks. When mission_message_scan_timer is 0 or less
 * it tests each untriggered mission message's trigger pairs, counts down the
 * delays of triggered ones by one per scan, shows a message to the local player
 * when its delay is 0 and the player's team is a recipient, and sets the timer
 * to 1180 ticks. Also writes g_msg_sender_iff, g_msg_arg_table and
 * g_pending_hud_message_voice_sfx_id. */
// FUNCTION: XVT 0x430A00
void mission_update_logic(void)
{
	enum {
		PLAYER_COUNT = 8,
		TEAM_COUNT = 10,
		FG_GOAL_COUNT = 8,
		GLOBAL_GOAL_COUNT = 3,
		GLOBAL_TRIGGER_COUNT = 4,
		GOAL_PRIMARY = 0,
		GOAL_PREVENT = 1,
		GOAL_BONUS = 2,
		GOAL_STATE_SUCCESS = 1,
		GOAL_STATE_FAILURE = 2,
		GOAL_STATE_PENDING = 4,
		GOAL_RESULT_AWARD_SCORE = 8,
		GOAL_AMOUNT_EACH_CRAFT = 18,
		GOAL_AMOUNT_EACH_SPECIAL_CRAFT = 19,
		MISSION_MESSAGE_REFRESH_TICKS = 1180,
		GLOBAL_GOAL_SCORE_SCALE = 250,
		FLIGHT_GROUP_GOAL_SCORE_SCALE = 250,
		PRIMARY_GOAL_SCORE = 10000,
		BONUS_GOAL_SCORE = 2500,
		TEAM_MESSAGE_COUNT = 2,
		PRIMARY_TEAM_MESSAGE = 0,
		FAILED_TEAM_MESSAGE = 2,
		BONUS_TEAM_MESSAGE = 4,
		PRIMARY_TEAM_VOICE = 111,
		BONUS_TEAM_VOICE = 112,
		FAILED_TEAM_VOICE = 113,
		MISSION_MESSAGE_VOICE_BASE = 95,
		MISSION_MESSAGE_VOICE_COUNT = 16,
		MESSAGE_DIGIT_FIRST = '1',
		MESSAGE_DIGIT_LAST = '6',
		PLACE_ORDINAL_MESSAGE_BASE = 294,
	};

	if (g_flight_mission_state.proving_grounds_mode_active != 0) {
		return;
	}

	if (g_flight_global_countdown_timers.mission_goal_evaluation_timer ==
		    0 ||
	    g_flight_mission_state.mission_end_pending != 0) {
		g_flight_mission_state.connected_player_count = 0;
		int player_index;
		for (player_index = 0; player_index < PLAYER_COUNT;
		     ++player_index) {
			if (g_players[player_index].participation_state != 0) {
				++g_flight_mission_state.connected_player_count;
			}
		}
		if ((unsigned int)g_flight_mission_state
			    .max_connected_player_count_this_mission <
		    (unsigned int)
			    g_flight_mission_state.connected_player_count) {
			g_flight_mission_state
				.max_connected_player_count_this_mission =
				g_flight_mission_state.connected_player_count;
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_DEBUG(
					"mission.player_peak players=%d tick=%d",
					(int)g_flight_mission_state
						.max_connected_player_count_this_mission,
					g_game_time);
			}
		}
		unsigned int mission_elapsed_seconds = mission_clock_to_seconds(
			g_mission_elapsed_clock.hours,
			g_mission_elapsed_clock.minutes,
			g_mission_elapsed_clock.seconds);

		uint16_t flight_group_index;
		uint16_t team_index;
		for (flight_group_index = 0;
		     flight_group_index <
		     (int16_t)g_mission_header.num_flight_groups;
		     ++flight_group_index) {
			for (uint16_t goal_index = 0;
			     goal_index < FG_GOAL_COUNT; ++goal_index) {
				if (g_mission_fg_stats[flight_group_index]
						    .arrival_enabled == 0 &&
				    g_mission_flight_groups[flight_group_index]
						    .player_owner_idx == -1) {
					for (team_index = 0;
					     team_index < TEAM_COUNT;
					     ++team_index) {
						g_mission_fg_stats[flight_group_index]
							.goal_state
								[FG_GOAL_COUNT *
									 team_index +
								 goal_index] =
							0;
					}
				} else {
					for (team_index = 0;
					     team_index < TEAM_COUNT;
					     ++team_index) {
						if (g_mission_flight_groups[flight_group_index]
							    .fg
							    .goals[goal_index]
							    .enabled_teams
								    [team_index] !=
						    0) {
							if (g_mission_fg_stats[flight_group_index]
								    .goal_state
									    [FG_GOAL_COUNT *
										     team_index +
									     goal_index] !=
							    GOAL_STATE_PENDING) {
								continue;
							}
							if (g_mission_flight_groups[flight_group_index]
									    .fg
									    .goals[goal_index]
									    .event_condition ==
								    MISSION_COND_ALWAYS_TRUE ||
							    g_mission_flight_groups[flight_group_index]
									    .fg
									    .goals[goal_index]
									    .event_condition ==
								    MISSION_COND_NEVER) {
								g_mission_fg_stats[flight_group_index]
									.goal_state
										[FG_GOAL_COUNT *
											 team_index +
										 goal_index] =
									0;
								continue;
							}
							int16_t result = mission_evaluate_condition(
								g_mission_flight_groups[flight_group_index]
									.fg
									.goals[goal_index]
									.event_condition,
								GOAL_TARGET_FLIGHT_GROUP,
								flight_group_index,
								(uint8_t)g_mission_flight_groups
									[flight_group_index]
										.fg
										.goals[goal_index]
										.amount,
								0, team_index);
							if (result ==
							    GOAL_STATE_SUCCESS) {
								result =
									GOAL_RESULT_AWARD_SCORE;
								if (g_mission_flight_groups[flight_group_index]
									    .fg
									    .goals[goal_index]
									    .time_limit5s !=
								    0) {
									result =
										5u * g_mission_flight_groups[flight_group_index]
														.fg
														.goals[goal_index]
														.time_limit5s <
												mission_elapsed_seconds
											? GOAL_STATE_FAILURE
											: GOAL_RESULT_AWARD_SCORE;
								}
							}
							if (result ==
								    GOAL_RESULT_AWARD_SCORE &&
							    (g_mission_flight_groups[flight_group_index]
									     .fg
									     .goals[goal_index]
									     .goal_kind !=
								     GOAL_BONUS ||
							     (g_mission_flight_groups[flight_group_index]
									      .fg
									      .goals[goal_index]
									      .amount !=
								      GOAL_AMOUNT_EACH_CRAFT &&
							      g_mission_flight_groups[flight_group_index]
									      .fg
									      .goals[goal_index]
									      .amount !=
								      GOAL_AMOUNT_EACH_SPECIAL_CRAFT))) {
								g_flight_mission_state
									.runtime
									.team_scores
										[TEAM_SCORE_BONUS]
										[team_index] +=
									FLIGHT_GROUP_GOAL_SCORE_SCALE *
									g_mission_flight_groups[flight_group_index]
										.fg
										.goals[goal_index]
										.points;
								result =
									GOAL_STATE_SUCCESS;
							}
							g_mission_fg_stats[flight_group_index]
								.goal_state
									[FG_GOAL_COUNT *
										 team_index +
									 goal_index] =
								(uint8_t)result;
							if (result !=
								    GOAL_STATE_PENDING &&
							    g_flight_sim_side_effects_suppressed ==
								    0) {
								XVT_LOG_INFO(
									"mission.fg_goal_decided fg=%d goal=%d team=%d kind=\"%s\" state=\"%s\" points=%d limit=%u seconds=%u tick=%d",
									(int)flight_group_index,
									(int)goal_index,
									(int)team_index,
									g_mission_flight_groups[flight_group_index]
												.fg
												.goals[goal_index]
												.goal_kind ==
											GOAL_PRIMARY
										? "primary"
									: g_mission_flight_groups[flight_group_index]
												.fg
												.goals[goal_index]
												.goal_kind ==
											GOAL_PREVENT
										? "prevent"
									: g_mission_flight_groups[flight_group_index]
												.fg
												.goals[goal_index]
												.goal_kind ==
											GOAL_BONUS
										? "bonus"
										: "other",
									result == GOAL_STATE_FAILURE
										? "failed"
									: result == GOAL_STATE_SUCCESS
										? "met"
										: "met_per_craft",
									result == GOAL_STATE_SUCCESS
										? FLIGHT_GROUP_GOAL_SCORE_SCALE *
											  g_mission_flight_groups[flight_group_index]
												  .fg
												  .goals[goal_index]
												  .points
										: 0,
									5u * g_mission_flight_groups[flight_group_index]
											.fg
											.goals[goal_index]
											.time_limit5s,
									mission_elapsed_seconds,
									g_game_time);
							}
						} else {
							g_mission_fg_stats[flight_group_index]
								.goal_state
									[FG_GOAL_COUNT *
										 team_index +
									 goal_index] =
								0;
						}
					}
				}
			}
		}

		for (team_index = 0; team_index < TEAM_COUNT; ++team_index) {
			for (uint16_t goal_kind = 0;
			     goal_kind < GLOBAL_GOAL_COUNT; ++goal_kind) {
				uint16_t aggregate_state =
					goal_kind != GOAL_PREVENT;
				uint8_t condition1 =
					g_mission_global_goals
						[team_index][goal_kind]
							.trigger_pairs[0]
							.triggers[0]
							.condition;
				uint8_t condition2 =
					g_mission_global_goals
						[team_index][goal_kind]
							.trigger_pairs[0]
							.triggers[1]
							.condition;
				uint8_t condition3 =
					g_mission_global_goals
						[team_index][goal_kind]
							.trigger_pairs[1]
							.triggers[0]
							.condition;
				uint8_t condition4 =
					g_mission_global_goals
						[team_index][goal_kind]
							.trigger_pairs[1]
							.triggers[1]
							.condition;

				for (uint16_t trigger_index = 0;
				     trigger_index < GLOBAL_TRIGGER_COUNT;
				     ++trigger_index) {
					g_flight_mission_state.runtime
						.global_goal_trigger_counts
							[0][team_index]
							[goal_kind]
							[trigger_index] = 0;
					g_flight_mission_state.runtime
						.global_goal_trigger_counts
							[1][team_index]
							[goal_kind]
							[trigger_index] = 0;
				}
				int16_t saw_success = 0;
				uint16_t global_state;
				if ((condition1 == MISSION_COND_NEVER ||
				     condition1 == MISSION_COND_ALWAYS_TRUE) &&
				    (condition2 == MISSION_COND_NEVER ||
				     condition2 == MISSION_COND_ALWAYS_TRUE) &&
				    (condition3 == MISSION_COND_NEVER ||
				     condition3 == MISSION_COND_ALWAYS_TRUE) &&
				    (condition4 == MISSION_COND_NEVER ||
				     condition4 == MISSION_COND_ALWAYS_TRUE)) {
					global_state = 0;
				} else {
					saw_success = 1;
					int pair1_team = TEAM_COUNT;
					if (condition2 ==
						    MISSION_COND_NO_CONDITION &&
					    g_mission_global_goals
							    [team_index]
							    [goal_kind]
								    .trigger_pairs
									    [0]
								    .triggers[1]
								    .variable_type ==
						    GOAL_TARGET_TEAM) {
						pair1_team =
							g_mission_global_goals
								[team_index]
								[goal_kind]
									.trigger_pairs
										[0]
									.triggers
										[1]
									.variable;
					}
					uint16_t trigger1_result = (uint16_t)mission_evaluate_condition(
						condition1,
						g_mission_global_goals
							[team_index][goal_kind]
								.trigger_pairs
									[0]
								.triggers[0]
								.variable_type,
						g_mission_global_goals
							[team_index][goal_kind]
								.trigger_pairs
									[0]
								.triggers[0]
								.variable,
						(uint8_t)g_mission_global_goals
							[team_index][goal_kind]
								.trigger_pairs
									[0]
								.triggers[0]
								.amount,
						0, (uint16_t)pair1_team);
					g_flight_mission_state.runtime
						.global_goal_trigger_counts
							[0][team_index]
							[goal_kind][0] =
						g_mission_condition_current_count;
					g_flight_mission_state.runtime
						.global_goal_trigger_counts
							[1][team_index]
							[goal_kind][0] =
						g_mission_condition_total_count;
					uint16_t trigger2_result;
					if (condition2 !=
					    MISSION_COND_NO_CONDITION) {
						trigger2_result = (uint16_t)mission_evaluate_condition(
							condition2,
							g_mission_global_goals
								[team_index]
								[goal_kind]
									.trigger_pairs
										[0]
									.triggers
										[1]
									.variable_type,
							g_mission_global_goals
								[team_index]
								[goal_kind]
									.trigger_pairs
										[0]
									.triggers
										[1]
									.variable,
							(uint8_t)g_mission_global_goals
								[team_index]
								[goal_kind]
									.trigger_pairs
										[0]
									.triggers
										[1]
									.amount,
							0,
							(uint16_t)pair1_team);
						g_flight_mission_state.runtime
							.global_goal_trigger_counts
								[0][team_index]
								[goal_kind][1] =
							g_mission_condition_current_count;
						g_flight_mission_state.runtime
							.global_goal_trigger_counts
								[1][team_index]
								[goal_kind][1] =
							g_mission_condition_total_count;
					} else {
						trigger2_result = 0;
					}
					int16_t pair1_result;
					if (condition2 ==
					    MISSION_COND_NO_CONDITION) {
						pair1_result =
							(trigger1_result & 1) !=
									0
								? GOAL_STATE_SUCCESS
								: ((trigger1_result &
								    2) != 0
									   ? GOAL_STATE_FAILURE
									   : GOAL_STATE_PENDING);
					} else if (
						g_mission_global_goals
							[team_index][goal_kind]
								.trigger_pairs
									[0]
								.trigger1_or_trigger2 ==
						GOAL_OPERATOR_STR_OR) {
						pair1_result =
							((trigger1_result |
							  trigger2_result) &
							 1) != 0
								? GOAL_STATE_SUCCESS
								: (((trigger1_result &
								     trigger2_result) &
								    2) != 0
									   ? GOAL_STATE_FAILURE
									   : GOAL_STATE_PENDING);
					} else {
						pair1_result =
							((trigger1_result &
							  trigger2_result) &
							 1) != 0
								? GOAL_STATE_SUCCESS
								: (((trigger1_result |
								     trigger2_result) &
								    2) != 0
									   ? GOAL_STATE_FAILURE
									   : GOAL_STATE_PENDING);
					}

					int pair2_team = TEAM_COUNT;
					if (condition4 ==
						    MISSION_COND_NO_CONDITION &&
					    g_mission_global_goals
							    [team_index]
							    [goal_kind]
								    .trigger_pairs
									    [1]
								    .triggers[1]
								    .variable_type ==
						    GOAL_TARGET_TEAM) {
						pair2_team =
							g_mission_global_goals
								[team_index]
								[goal_kind]
									.trigger_pairs
										[1]
									.triggers
										[1]
									.variable;
					}
					trigger1_result = (uint16_t)mission_evaluate_condition(
						condition3,
						g_mission_global_goals
							[team_index][goal_kind]
								.trigger_pairs
									[1]
								.triggers[0]
								.variable_type,
						g_mission_global_goals
							[team_index][goal_kind]
								.trigger_pairs
									[1]
								.triggers[0]
								.variable,
						(uint8_t)g_mission_global_goals
							[team_index][goal_kind]
								.trigger_pairs
									[1]
								.triggers[0]
								.amount,
						0, (uint16_t)pair2_team);
					g_flight_mission_state.runtime
						.global_goal_trigger_counts
							[0][team_index]
							[goal_kind][2] =
						g_mission_condition_current_count;
					g_flight_mission_state.runtime
						.global_goal_trigger_counts
							[1][team_index]
							[goal_kind][2] =
						g_mission_condition_total_count;
					if (condition4 !=
					    MISSION_COND_NO_CONDITION) {
						trigger2_result = (uint16_t)mission_evaluate_condition(
							condition4,
							g_mission_global_goals
								[team_index]
								[goal_kind]
									.trigger_pairs
										[1]
									.triggers
										[1]
									.variable_type,
							g_mission_global_goals
								[team_index]
								[goal_kind]
									.trigger_pairs
										[1]
									.triggers
										[1]
									.variable,
							(uint8_t)g_mission_global_goals
								[team_index]
								[goal_kind]
									.trigger_pairs
										[1]
									.triggers
										[1]
									.amount,
							0,
							(uint16_t)pair2_team);
						g_flight_mission_state.runtime
							.global_goal_trigger_counts
								[0][team_index]
								[goal_kind][3] =
							g_mission_condition_current_count;
						g_flight_mission_state.runtime
							.global_goal_trigger_counts
								[1][team_index]
								[goal_kind][3] =
							g_mission_condition_total_count;
					} else {
						trigger2_result = 0;
					}
					int16_t pair2_result;
					if (condition4 ==
					    MISSION_COND_NO_CONDITION) {
						pair1_result =
							(trigger1_result & 1) !=
									0
								? GOAL_STATE_SUCCESS
								: ((trigger1_result &
								    2) != 0
									   ? GOAL_STATE_FAILURE
									   : GOAL_STATE_PENDING);
					} else if (
						g_mission_global_goals
							[team_index][goal_kind]
								.trigger_pairs
									[1]
								.trigger1_or_trigger2 ==
						GOAL_OPERATOR_STR_OR) {
						pair2_result =
							((trigger1_result |
							  trigger2_result) &
							 1) != 0
								? GOAL_STATE_SUCCESS
								: (((trigger1_result &
								     trigger2_result) &
								    2) != 0
									   ? GOAL_STATE_FAILURE
									   : GOAL_STATE_PENDING);
					} else {
						pair2_result =
							((trigger1_result &
							  trigger2_result) &
							 1) != 0
								? GOAL_STATE_SUCCESS
								: (((trigger1_result |
								     trigger2_result) &
								    2) != 0
									   ? GOAL_STATE_FAILURE
									   : GOAL_STATE_PENDING);
					}

					if (g_mission_global_goals
						    [team_index][goal_kind]
							    .trigger_pair1_or_trigger_pair2 ==
					    GOAL_OPERATOR_STR_OR) {
						global_state =
							((pair1_result |
							  pair2_result) &
							 1) != 0
								? GOAL_STATE_SUCCESS
								: (((pair1_result &
								     pair2_result) &
								    2) != 0
									   ? GOAL_STATE_FAILURE
									   : GOAL_STATE_PENDING);
					} else {
						global_state =
							((pair1_result &
							  pair2_result) &
							 1) != 0
								? GOAL_STATE_SUCCESS
								: (((pair1_result |
								     pair2_result) &
								    2) != 0
									   ? GOAL_STATE_FAILURE
									   : GOAL_STATE_PENDING);
					}
				}

				if (goal_kind != GOAL_PREVENT) {
					if (global_state ==
					    GOAL_STATE_SUCCESS) {
						saw_success = 1;
						if (g_flight_mission_state
							    .runtime
							    .team_global_goal_state
								    [team_index]
								    [goal_kind] !=
						    global_state) {
							g_flight_mission_state
								.runtime
								.team_scores
									[TEAM_SCORE_BONUS]
									[team_index] +=
								GLOBAL_GOAL_SCORE_SCALE *
								g_mission_global_goals
									[team_index]
									[goal_kind]
										.raw_points;
						}
					} else if (global_state ==
						   GOAL_STATE_FAILURE) {
						aggregate_state =
							GOAL_STATE_FAILURE;
					} else if (global_state ==
						   GOAL_STATE_PENDING) {
						aggregate_state = 0;
					}
				} else if (global_state == GOAL_STATE_SUCCESS) {
					aggregate_state = GOAL_STATE_SUCCESS;
					saw_success = 1;
					if (g_flight_mission_state.runtime
						    .team_global_goal_state
							    [team_index]
							    [goal_kind] !=
					    global_state) {
						g_flight_mission_state.runtime
							.team_scores
								[TEAM_SCORE_BONUS]
								[team_index] +=
							GLOBAL_GOAL_SCORE_SCALE *
							g_mission_global_goals
								[team_index]
								[goal_kind]
									.raw_points;
					}
				}
				if (g_flight_mission_state.runtime
						    .team_global_goal_state
							    [team_index]
							    [goal_kind] !=
					    global_state &&
				    global_state != 0 &&
				    g_flight_sim_side_effects_suppressed == 0) {
					XVT_LOG_DEBUG(
						"mission.global_goal_state team=%d kind=\"%s\" was=%d now=%d met=%d total=%d points=%d score=%d tick=%d",
						(int)team_index,
						goal_kind == GOAL_PRIMARY
							? "primary"
						: goal_kind == GOAL_PREVENT
							? "prevent"
							: "bonus",
						(int)g_flight_mission_state
							.runtime
							.team_global_goal_state
								[team_index]
								[goal_kind],
						(int)global_state,
						(int)g_flight_mission_state
							.runtime
							.global_goal_trigger_counts
								[0][team_index]
								[goal_kind][0],
						(int)g_flight_mission_state
							.runtime
							.global_goal_trigger_counts
								[1][team_index]
								[goal_kind][0],
						global_state == GOAL_STATE_SUCCESS
							? GLOBAL_GOAL_SCORE_SCALE *
								  g_mission_global_goals
									  [team_index]
									  [goal_kind]
										  .raw_points
							: 0,
						g_flight_mission_state.runtime
							.team_scores
								[TEAM_SCORE_BONUS]
								[team_index],
						g_game_time);
				}
				g_flight_mission_state.runtime
					.team_global_goal_state[team_index]
							       [goal_kind] =
					(uint8_t)global_state;

				for (flight_group_index = 0;
				     flight_group_index <
				     (int16_t)
					     g_mission_header.num_flight_groups;
				     ++flight_group_index) {
					for (uint16_t goal_index = 0;
					     goal_index < FG_GOAL_COUNT;
					     ++goal_index) {
						if (g_mission_flight_groups[flight_group_index]
								    .fg
								    .goals[goal_index]
								    .enabled_teams
									    [team_index] !=
							    0 &&
						    g_mission_flight_groups[flight_group_index]
								    .fg
								    .goals[goal_index]
								    .goal_kind ==
							    goal_kind) {
							if (goal_kind !=
							    GOAL_PREVENT) {
								if (g_mission_fg_stats[flight_group_index]
									    .goal_state
										    [FG_GOAL_COUNT *
											     team_index +
										     goal_index] ==
								    GOAL_STATE_SUCCESS) {
									saw_success =
										1;
								} else if (
									g_mission_fg_stats[flight_group_index]
										.goal_state
											[FG_GOAL_COUNT *
												 team_index +
											 goal_index] ==
									GOAL_STATE_FAILURE) {
									aggregate_state =
										GOAL_STATE_FAILURE;
								} else if (
									g_mission_fg_stats[flight_group_index]
											.goal_state
												[FG_GOAL_COUNT *
													 team_index +
												 goal_index] ==
										GOAL_STATE_PENDING &&
									aggregate_state !=
										GOAL_STATE_FAILURE) {
									aggregate_state =
										0;
								}
							} else if (
								g_mission_fg_stats[flight_group_index]
									.goal_state
										[FG_GOAL_COUNT *
											 team_index +
										 goal_index] ==
								GOAL_STATE_SUCCESS) {
								aggregate_state =
									GOAL_STATE_SUCCESS;
								saw_success = 1;
							}
						}
					}
				}

				if (aggregate_state == GOAL_STATE_SUCCESS &&
				    saw_success == 0) {
					aggregate_state = 0;
				}
				if (g_mission_header.mission_type ==
					    MISSION_TYPE_MELEE &&
				    goal_kind == GOAL_PREVENT &&
				    aggregate_state == GOAL_STATE_SUCCESS &&
				    g_flight_mission_state.runtime
						    .team_goal_status
							    [team_index]
							    [GOAL_PRIMARY] ==
					    GOAL_STATE_SUCCESS) {
					aggregate_state = 0;
				}
				if (goal_kind == GOAL_PRIMARY &&
				    g_mission_header.mission_type ==
					    MISSION_TYPE_MELEE &&
				    g_mission_header.goals_unimportant != 0) {
					aggregate_state = 0;
				}

				{
					uint8_t old_state =
						g_flight_mission_state.runtime
							.team_goal_status
								[team_index]
								[goal_kind];

					if (old_state != aggregate_state &&
					    aggregate_state != 0 &&
					    old_state != GOAL_STATE_SUCCESS &&
					    g_mission_header.goals_unimportant ==
						    0) {
						g_flight_mission_state.runtime
							.team_goal_status
								[team_index]
								[goal_kind] =
							(uint8_t)
								aggregate_state;
						if (aggregate_state ==
							    GOAL_STATE_FAILURE &&
						    g_flight_sim_side_effects_suppressed ==
							    0) {
							XVT_LOG_INFO(
								"mission.goal_failed team=%d kind=\"%s\" tick=%d",
								(int)team_index,
								goal_kind == GOAL_PRIMARY
									? "primary"
								: goal_kind == GOAL_PREVENT
									? "prevent"
									: "bonus",
								g_game_time);
							XVT_LOG_INFO(
								"battle.goal_failed team=%d kind=\"%s\" tick=%d",
								(int)team_index,
								goal_kind == GOAL_PRIMARY
									? "primary"
								: goal_kind == GOAL_PREVENT
									? "prevent"
									: "bonus",
								g_game_time);
						}
						if (aggregate_state ==
						    GOAL_STATE_SUCCESS) {
							int completed_team_count =
								0;
							/* player_index is
							 * reused here as a team
							 * index over
							 * team_goal_status. */
							for (player_index = 0;
							     player_index <
							     TEAM_COUNT;
							     ++player_index) {
								if (g_flight_mission_state
									    .runtime
									    .team_goal_status
										    [player_index]
										    [goal_kind] ==
								    GOAL_STATE_SUCCESS) {
									++completed_team_count;
								}
							}
							if (g_flight_sim_side_effects_suppressed ==
							    0) {
								XVT_LOG_INFO(
									"mission.goal_met team=%d kind=\"%s\" rank=%d was=\"%s\" tick=%d",
									(int)team_index,
									goal_kind == GOAL_PRIMARY
										? "primary"
									: goal_kind == GOAL_PREVENT
										? "prevent"
										: "bonus",
									completed_team_count,
									old_state == GOAL_STATE_FAILURE
										? "failed"
										: "open",
									g_game_time);
							}
							if (goal_kind !=
								    GOAL_PREVENT &&
							    g_flight_sim_side_effects_suppressed ==
								    0) {
								XVT_LOG_INFO(
									"battle.goal_met team=%d kind=\"%s\" rank=%d tick=%d",
									(int)team_index,
									goal_kind == GOAL_PRIMARY
										? "primary"
										: "bonus",
									completed_team_count,
									g_game_time);
							}
							if (goal_kind ==
								    GOAL_PREVENT &&
							    g_flight_sim_side_effects_suppressed ==
								    0) {
								XVT_LOG_INFO(
									"battle.goal_failed team=%d kind=\"prevent\" tick=%d",
									(int)team_index,
									g_game_time);
							}
							if (goal_kind ==
							    GOAL_PRIMARY) {
								int hostile_completed_teams =
									0;

								if (g_mission_header
									    .mission_type ==
								    MISSION_TYPE_COMBAT) {
									for (int other_team =
										     0;
									     other_team <
									     TEAM_COUNT;
									     ++other_team) {
										int is_hostile_team =
											team_index !=
												other_team &&
											g_mission_teams[other_team]
													.allies[team_index] ==
												0;
										if (is_hostile_team &&
										    g_flight_mission_state
												    .runtime
												    .team_goal_status
													    [other_team]
													    [goal_kind] ==
											    GOAL_STATE_SUCCESS) {
											++hostile_completed_teams;
										}
									}
								}
								if (hostile_completed_teams ==
								    0) {
									g_flight_mission_state
										.runtime
										.team_scores
											[TEAM_SCORE_BONUS]
											[team_index] +=
										PRIMARY_GOAL_SCORE;
								}
								g_flight_mission_state
									.runtime
									.team_mission_completion_time_seconds
										[team_index] =
									(int)mission_elapsed_seconds;
								g_flight_mission_state
									.runtime
									.global_primary_goal_status =
									GOAL_STATE_SUCCESS;
								if (g_flight_sim_side_effects_suppressed ==
								    0) {
									XVT_LOG_DEBUG(
										"mission.primary_scored team=%d hostile=%d points=%d seconds=%u score=%d tick=%d",
										(int)team_index,
										hostile_completed_teams,
										hostile_completed_teams ==
												0
											? PRIMARY_GOAL_SCORE
											: 0,
										mission_elapsed_seconds,
										g_flight_mission_state
											.runtime
											.team_scores
												[TEAM_SCORE_BONUS]
												[team_index],
										g_game_time);
								}
							} else if (goal_kind ==
								   GOAL_BONUS) {
								g_flight_mission_state
									.runtime
									.global_bonus_goal_status =
									GOAL_STATE_SUCCESS;
								g_flight_mission_state
									.runtime
									.team_scores
										[TEAM_SCORE_BONUS]
										[team_index] +=
									BONUS_GOAL_SCORE;
								if (g_flight_sim_side_effects_suppressed ==
								    0) {
									XVT_LOG_DEBUG(
										"mission.bonus_scored team=%d points=%d score=%d tick=%d",
										(int)team_index,
										BONUS_GOAL_SCORE,
										g_flight_mission_state
											.runtime
											.team_scores
												[TEAM_SCORE_BONUS]
												[team_index],
										g_game_time);
								}
							}
							if (goal_kind ==
							    GOAL_PRIMARY) {
								int team_player_slot =
									-1;

								for (player_index =
									     0;
								     player_index <
								     PLAYER_COUNT;
								     ++player_index) {
									if (g_players[player_index]
											    .participation_state !=
										    0 &&
									    (uint16_t)g_players[player_index]
											    .team ==
										    team_index) {
										team_player_slot =
											player_index;
									}
								}
								for (player_index =
									     0;
								     player_index <
								     PLAYER_COUNT;
								     ++player_index) {
									if (g_players[player_index]
										    .participation_state ==
									    0) {
										continue;
									}
									if ((uint16_t)g_players
										    [player_index]
											    .team ==
									    team_index) {
										g_players[player_index]
											.mission_stats
											.primary_goal_finish_place =
											completed_team_count;
										g_msg_sender_iff =
											g_players[player_index]
												.iff;
										uint8_t announce_victory =
											1;
										if (g_mission_header.mission_type ==
											    MISSION_TYPE_COMBAT &&
										    g_flight_mission_state
												    .runtime
												    .team_goal_status
													    [team_index]
													    [GOAL_PREVENT] ==
											    GOAL_STATE_SUCCESS) {
											announce_victory =
												0;
										}
										if (g_pilot_data.mission_directory_id ==
											    MISSION_DIRECTORY_TRAINING_EXERCISES &&
										    g_pilot_data.mission_sequence_active ==
											    1 &&
										    g_flight_mission_state
												    .runtime
												    .team_goal_status
													    [team_index]
													    [GOAL_PREVENT] ==
											    GOAL_STATE_SUCCESS) {
											announce_victory =
												0;
										}
										if (g_flight_sim_side_effects_suppressed ==
										    0) {
											XVT_LOG_DEBUG(
												"mission.primary_rank slot=%d team=%d rank=%d victory=%d local=%d",
												player_index,
												(int)team_index,
												completed_team_count,
												(int)announce_victory,
												player_index ==
													g_local_player);
										}
										if (announce_victory !=
										    0) {
											msg_emit_in_flight_message(
												IFMSG_193_EXCELLENT_JOB_PRIMARY_MISSION_OBJECTIVES_COMPLETED,
												player_index);
											if (g_mission_header.mission_type ==
												    MISSION_TYPE_MELEE &&
											    g_flight_player_count >
												    1) {
												g_msg_arg_table
													[0] = completed_team_count +
													      PLACE_ORDINAL_MESSAGE_BASE;
												msg_emit_in_flight_message(
													IFMSG_305_YOU_ARE_THE_ARG_TO_COMPLETE_YOUR_MISSION_GOALS,
													player_index);
											}
											if (player_index ==
											    g_local_player) {
												for (uint16_t team_message_index =
													     0;
												     team_message_index <
												     TEAM_MESSAGE_COUNT;
												     ++team_message_index) {
													if (g_mission_teams[team_index]
														    .end_of_mission_messages
															    [PRIMARY_TEAM_MESSAGE +
															     team_message_index]
															    [0] ==
													    '\0') {
														continue;
													}
													msg_add_message_ptr(
														0,
														g_mission_teams[team_index]
															.end_of_mission_messages
																[PRIMARY_TEAM_MESSAGE +
																 team_message_index]);
													g_pending_hud_message_voice_sfx_id =
														PRIMARY_TEAM_VOICE;
													if (team_message_index !=
													    0) {
														g_pending_hud_message_voice_sfx_id =
															0;
													}
													if ((uint8_t)g_mission_teams[team_index]
															    .end_of_mission_messages
																    [PRIMARY_TEAM_MESSAGE +
																     team_message_index]
																    [0] >=
														    MESSAGE_DIGIT_FIRST &&
													    (uint8_t)g_mission_teams[team_index]
															    .end_of_mission_messages
																    [PRIMARY_TEAM_MESSAGE +
																     team_message_index]
																    [0] <=
														    MESSAGE_DIGIT_LAST) {
														msg_emit_in_flight_message(
															IFMSG_207_CODE_01_ARGUMENT,
															player_index);
													} else {
														msg_emit_in_flight_message(
															IFMSG_196_CODE_02_ARGUMENT,
															player_index);
													}
												}
												if (g_mission_header
													    .mission_type ==
												    MISSION_TYPE_COMBAT) {
													fsfx_queue_commander_voice_category(
														1,
														-1);
												} else {
													fsfx_queue_commander_voice_category(
														0,
														-1);
												}
											}
										} else if (
											team_index ==
											0) {
											msg_emit_in_flight_message(
												IFMSG_208_GOOD_WORK_OUR_FORCES_HAVE_BOUNCED_BACK_TO_PULL_OUT_A_DRAW,
												player_index);
										} else {
											msg_emit_in_flight_message(
												IFMSG_209_GOOD_WORK_THE_ALLIANCE_HAS_RALLIED_TO_GAIN_A_DRAW_FROM_THIS_MISSION,
												player_index);
										}
									} else {
										g_msg_arg_table
											[1] = completed_team_count +
											      PLACE_ORDINAL_MESSAGE_BASE;
										if ((g_mission_header.mission_type ==
											     MISSION_TYPE_MELEE ||
										     g_mission_header.mission_type ==
											     MISSION_TYPE_SIMULATOR_1) &&
										    team_player_slot !=
											    -1) {
											msg_add_message_ptr(
												0,
												net_session_get_player_name(
													team_player_slot));
											msg_emit_in_flight_message(
												IFMSG_306_ARG_IS_THE_ARG_TO_COMPLETE_HIS_MISSION_GOALS,
												player_index);
										}
									}
								}
							} else if (
								goal_kind ==
								GOAL_PREVENT) {
								for (player_index =
									     0;
								     player_index <
								     PLAYER_COUNT;
								     ++player_index) {
									if (g_players[player_index]
											    .participation_state !=
										    0 &&
									    (uint16_t)g_players[player_index]
											    .team ==
										    team_index &&
									    g_flight_mission_state
											    .runtime
											    .team_goal_status
												    [team_index]
												    [GOAL_PRIMARY] !=
										    GOAL_STATE_FAILURE &&
									    player_index ==
										    g_local_player) {
										uint8_t announce_failure =
											1;

										if (g_mission_header.mission_type ==
											    MISSION_TYPE_COMBAT &&
										    g_flight_mission_state
												    .runtime
												    .team_goal_status
													    [team_index]
													    [GOAL_PRIMARY] ==
											    GOAL_STATE_SUCCESS) {
											announce_failure =
												0;
										}
										if (g_flight_sim_side_effects_suppressed ==
										    0) {
											XVT_LOG_DEBUG(
												"mission.prevent_told slot=%d team=%d abort=%d",
												player_index,
												(int)team_index,
												(int)announce_failure);
										}
										if (announce_failure !=
										    0) {
											g_msg_sender_iff =
												g_players[player_index]
													.iff;
											msg_emit_in_flight_message(
												IFMSG_206_MISSION_OBJECTIVES_CANNOT_BE_FINISHED_ABORT_MISSION,
												player_index);
											for (uint16_t team_message_index =
												     0;
											     team_message_index <
											     TEAM_MESSAGE_COUNT;
											     ++team_message_index) {
												if (g_mission_teams[team_index]
													    .end_of_mission_messages
														    [FAILED_TEAM_MESSAGE +
														     team_message_index]
														    [0] ==
												    '\0') {
													continue;
												}
												msg_add_message_ptr(
													0,
													g_mission_teams[team_index]
														.end_of_mission_messages
															[FAILED_TEAM_MESSAGE +
															 team_message_index]);
												g_pending_hud_message_voice_sfx_id =
													FAILED_TEAM_VOICE;
												if (team_message_index !=
												    0) {
													g_pending_hud_message_voice_sfx_id =
														0;
												}
												if ((uint8_t)g_mission_teams[team_index]
														    .end_of_mission_messages
															    [FAILED_TEAM_MESSAGE +
															     team_message_index]
															    [0] >=
													    MESSAGE_DIGIT_FIRST &&
												    (uint8_t)g_mission_teams[team_index]
														    .end_of_mission_messages
															    [FAILED_TEAM_MESSAGE +
															     team_message_index]
															    [0] <=
													    MESSAGE_DIGIT_LAST) {
													msg_emit_in_flight_message(
														IFMSG_207_CODE_01_ARGUMENT,
														player_index);
												} else {
													msg_emit_in_flight_message(
														IFMSG_196_CODE_02_ARGUMENT,
														player_index);
												}
											}
											if (g_mission_header
												    .mission_type ==
											    MISSION_TYPE_COMBAT) {
												fsfx_queue_commander_voice_category(
													7,
													-1);
											} else {
												fsfx_queue_commander_voice_category(
													6,
													-1);
											}
										} else if (
											team_index ==
											0) {
											msg_emit_in_flight_message(
												IFMSG_210_WE_HAVE_LOST_OUR_VICTORY_THE_REBELS_HAVE_GAINED_A_DRAW,
												player_index);
										} else {
											msg_emit_in_flight_message(
												IFMSG_211_WE_HAVE_LOST_OUR_VICTORY_THE_EMPIRE_HAS_GAINED_A_DRAW,
												player_index);
										}
									}
								}
							} else {
								for (player_index =
									     0;
								     player_index <
								     PLAYER_COUNT;
								     ++player_index) {
									if (g_players[player_index]
											    .participation_state !=
										    0 &&
									    (uint16_t)g_players[player_index]
											    .team ==
										    team_index &&
									    player_index ==
										    g_local_player) {
										for (uint16_t team_message_index =
											     0;
										     team_message_index <
										     TEAM_MESSAGE_COUNT;
										     ++team_message_index) {
											if (g_mission_teams[team_index]
												    .end_of_mission_messages
													    [BONUS_TEAM_MESSAGE +
													     team_message_index]
													    [0] ==
											    '\0') {
												continue;
											}
											msg_add_message_ptr(
												0,
												g_mission_teams[team_index]
													.end_of_mission_messages
														[BONUS_TEAM_MESSAGE +
														 team_message_index]);
											g_pending_hud_message_voice_sfx_id =
												BONUS_TEAM_VOICE;
											if (team_message_index !=
											    0) {
												g_pending_hud_message_voice_sfx_id =
													0;
											}
											g_msg_sender_iff =
												g_players[player_index]
													.iff;
											if ((uint8_t)g_mission_teams[team_index]
													    .end_of_mission_messages
														    [BONUS_TEAM_MESSAGE +
														     team_message_index]
														    [0] >=
												    MESSAGE_DIGIT_FIRST &&
											    (uint8_t)g_mission_teams[team_index]
													    .end_of_mission_messages
														    [BONUS_TEAM_MESSAGE +
														     team_message_index]
														    [0] <=
												    MESSAGE_DIGIT_LAST) {
												msg_emit_in_flight_message(
													IFMSG_207_CODE_01_ARGUMENT,
													player_index);
											} else {
												msg_emit_in_flight_message(
													IFMSG_196_CODE_02_ARGUMENT,
													player_index);
											}
										}
									}
								}
							}
						} else if (
							aggregate_state ==
								GOAL_STATE_FAILURE &&
							goal_kind ==
								GOAL_PRIMARY) {
							for (player_index = 0;
							     player_index <
							     PLAYER_COUNT;
							     ++player_index) {
								if (g_players[player_index]
										    .participation_state !=
									    0 &&
								    (uint16_t)g_players[player_index]
										    .team ==
									    team_index &&
								    g_flight_mission_state
										    .runtime
										    .team_goal_status
											    [team_index]
											    [GOAL_PREVENT] !=
									    GOAL_STATE_SUCCESS &&
								    player_index ==
									    g_local_player) {
									g_msg_sender_iff =
										g_players[player_index]
											.iff;
									msg_emit_in_flight_message(
										IFMSG_206_MISSION_OBJECTIVES_CANNOT_BE_FINISHED_ABORT_MISSION,
										player_index);
									for (uint16_t team_message_index =
										     0;
									     team_message_index <
									     TEAM_MESSAGE_COUNT;
									     ++team_message_index) {
										if (g_mission_teams[team_index]
											    .end_of_mission_messages
												    [FAILED_TEAM_MESSAGE +
												     team_message_index]
												    [0] ==
										    '\0') {
											continue;
										}
										msg_add_message_ptr(
											0,
											g_mission_teams[team_index]
												.end_of_mission_messages
													[FAILED_TEAM_MESSAGE +
													 team_message_index]);
										g_pending_hud_message_voice_sfx_id =
											FAILED_TEAM_VOICE;
										if (team_message_index !=
										    0) {
											g_pending_hud_message_voice_sfx_id =
												0;
										}
										if ((uint8_t)g_mission_teams[team_index]
												    .end_of_mission_messages
													    [FAILED_TEAM_MESSAGE +
													     team_message_index]
													    [0] >=
											    MESSAGE_DIGIT_FIRST &&
										    (uint8_t)g_mission_teams[team_index]
												    .end_of_mission_messages
													    [FAILED_TEAM_MESSAGE +
													     team_message_index]
													    [0] <=
											    MESSAGE_DIGIT_LAST) {
											msg_emit_in_flight_message(
												IFMSG_207_CODE_01_ARGUMENT,
												player_index);
										} else {
											msg_emit_in_flight_message(
												IFMSG_196_CODE_02_ARGUMENT,
												player_index);
										}
									}
									if (g_mission_header
										    .mission_type ==
									    MISSION_TYPE_COMBAT) {
										fsfx_queue_commander_voice_category(
											7,
											-1);
									} else {
										fsfx_queue_commander_voice_category(
											6,
											-1);
									}
								}
							}
						}
					}
				}
			}
		}
		g_flight_global_countdown_timers.mission_goal_evaluation_timer =
			SIMULATION_TICKS_PER_SECOND;
	}

	if ((int16_t)g_flight_global_countdown_timers
		    .mission_message_scan_timer > 0) {
		return;
	}

	for (uint16_t message_index = 0;
	     message_index < (int16_t)g_mission_header.num_messages;
	     ++message_index) {
		if (g_flight_mission_state.message_triggered[message_index] ==
		    0) {
			int16_t trigger_pair1 =
				(int16_t)mission_evaluate_trigger_pair(
					&g_mission_messages[message_index]
						 .trigger_pairs[0],
					0);
			int16_t trigger_pair2 =
				(int16_t)mission_evaluate_trigger_pair(
					&g_mission_messages[message_index]
						 .trigger_pairs[1],
					0);
			int16_t triggered =
				g_mission_messages[message_index]
							.trigger_pair1_or_trigger_pair2 ==
						GOAL_OPERATOR_STR_OR
					? trigger_pair1 | trigger_pair2
					: trigger_pair1 & trigger_pair2;

			if ((triggered & 1) == 0) {
				continue;
			}
			g_flight_mission_state
				.message_triggered[message_index] = 1;
			g_flight_mission_state
				.message_delay_countdown[message_index] =
				g_mission_messages[message_index].delay5s;
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_DEBUG(
					"mission.message_triggered message=%d pair1=%d pair2=%d delay=%d recipient=%d tick=%d",
					(int)message_index, (int)trigger_pair1,
					(int)trigger_pair2,
					(int)g_mission_messages[message_index]
						.delay5s,
					g_mission_messages[message_index].sent_to_team
							[(uint16_t)g_players
								 [g_local_player]
									 .team] !=
						0,
					g_game_time);
			}
			if (g_mission_messages[message_index].delay5s != 0 ||
			    g_mission_messages[message_index].sent_to_team
					    [(uint16_t)g_players[g_local_player]
						     .team] == 0) {
				continue;
			}
			msg_add_message_ptr(0,
					    &g_mission_messages[message_index]);
			if (message_index < MISSION_MESSAGE_VOICE_COUNT) {
				g_pending_hud_message_voice_sfx_id =
					message_index +
					MISSION_MESSAGE_VOICE_BASE;
			} else {
				g_pending_hud_message_voice_sfx_id = 0;
			}
			msg_emit_in_flight_message(IFMSG_207_CODE_01_ARGUMENT,
						   g_local_player);
		} else {
			if (g_flight_mission_state
				    .message_delay_countdown[message_index] ==
			    0) {
				continue;
			}
			--g_flight_mission_state
				  .message_delay_countdown[message_index];
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_DEBUG(
					"mission.message_delay message=%d left=%d recipient=%d tick=%d",
					(int)message_index,
					(int)g_flight_mission_state
						.message_delay_countdown
							[message_index],
					g_mission_messages[message_index].sent_to_team
							[(uint16_t)g_players
								 [g_local_player]
									 .team] !=
						0,
					g_game_time);
			}
			if (g_flight_mission_state.message_delay_countdown
					    [message_index] != 0 ||
			    g_mission_messages[message_index].sent_to_team
					    [(uint16_t)g_players[g_local_player]
						     .team] == 0) {
				continue;
			}
			msg_add_message_ptr(0,
					    &g_mission_messages[message_index]);
			if (message_index < MISSION_MESSAGE_VOICE_COUNT) {
				g_pending_hud_message_voice_sfx_id =
					message_index +
					MISSION_MESSAGE_VOICE_BASE;
			} else {
				g_pending_hud_message_voice_sfx_id = 0;
			}
			msg_emit_in_flight_message(IFMSG_207_CODE_01_ARGUMENT,
						   g_local_player);
		}
	}
	g_flight_global_countdown_timers.mission_message_scan_timer =
		MISSION_MESSAGE_REFRESH_TICKS;
}

/* Tests a trigger pair: the results of its two conditions from
 * mission_evaluate_condition, ORed when trigger1_or_trigger2 is 1 or the second
 * condition is MISSION_COND_NO_CONDITION, else ANDed. A second trigger of
 * MISSION_COND_NO_CONDITION is not evaluated; when its variable_type is
 * GOAL_TARGET_TEAM, its variable becomes the first condition's team filter.
 * Returns the combined result bits: 1 met, 2 failed, 4 undecided. Leaves the
 * last evaluated condition's counts in g_mission_condition_current_count and
 * g_mission_condition_total_count. */
// FUNCTION: XVT 0x431B50
int mission_evaluate_trigger_pair(
	const struct mission_trigger_pair *trigger_pair,
	int16_t include_departed_as_destroyed)
{
	enum { MISSION_TEAM_COUNT = 10 };

	int team_or_variable = MISSION_TEAM_COUNT;
	if (trigger_pair->triggers[1].condition == MISSION_COND_NO_CONDITION &&
	    trigger_pair->triggers[1].variable_type == GOAL_TARGET_TEAM) {
		team_or_variable = trigger_pair->triggers[1].variable;
	}

	int trigger1_result = (uint16_t)mission_evaluate_condition(
		trigger_pair->triggers[0].condition,
		trigger_pair->triggers[0].variable_type,
		trigger_pair->triggers[0].variable,
		(uint8_t)trigger_pair->triggers[0].amount,
		include_departed_as_destroyed, (uint16_t)team_or_variable);
	int trigger2_result;
	if (trigger_pair->triggers[1].condition != MISSION_COND_NO_CONDITION) {
		trigger2_result = (uint16_t)mission_evaluate_condition(
			trigger_pair->triggers[1].condition,
			trigger_pair->triggers[1].variable_type,
			trigger_pair->triggers[1].variable,
			(uint8_t)trigger_pair->triggers[1].amount,
			include_departed_as_destroyed, MISSION_TEAM_COUNT);
	} else {
		trigger2_result = 0;
	}

	if (trigger_pair->trigger1_or_trigger2 == GOAL_OPERATOR_STR_OR ||
	    trigger_pair->triggers[1].condition == MISSION_COND_NO_CONDITION) {
		return trigger1_result | trigger2_result;
	}
	return trigger1_result & trigger2_result;
}

/* Tests one mission condition. Returns 1 when it is met, 2 when it has
 * failed, 4 while undecided, and 0 for MISSION_COND_NEVER. A condition marked
 * in g_mission_condition_uses_count_by_condition counts craft: with variable_type
 * GOAL_TARGET_NONE it returns 2; else, over every flight group with a craft
 * type that mission_flight_group_matches_trigger_variable matches, it sums the
 * craft that meet and that fail the condition from g_mission_fg_stats (for the
 * shield, hull, warhead and cannon conditions, from the group's live craft in
 * the active region), and compares them with the group totals as amount_type
 * (a GOAL_AMT_ value) says; the subset amounts measure against the craft
 * arrived and never fail, and an amount not handled stays 4. It leaves met
 * and total in g_mission_condition_current_count and
 * g_mission_condition_total_count; every other path leaves both 0. A team_filter
 * below 10 picks one team's counts for the inspected and captured-and-departed
 * conditions. include_departed_as_destroyed nonzero makes MISSION_COND_DESTROYED
 * count the mothership-dependent and left-region outcomes as met, not the
 * left-region ones as failed. Other conditions read the goal statuses in
 * g_flight_mission_state.runtime, the reinforcement flags, or the connected
 * players by team, IFF or player number. Does not check condition_type below
 * 48 or variable against the tables it indexes. */
// FUNCTION: XVT 0x431C10
int16_t mission_evaluate_condition(uint16_t condition_type,
				   int16_t variable_type, uint16_t variable,
				   int16_t amount_type,
				   int16_t include_departed_as_destroyed,
				   uint16_t team_filter)
{
	enum {
		MISSION_TEAM_COUNT = 10,
		MISSION_IFF_COUNT = 6,
		MISSION_PLAYER_COUNT = 8,
	};

	g_mission_condition_current_count = 0;
	g_mission_condition_total_count = 0;
	int16_t status;
	uint16_t flight_group_idx;
	if (g_mission_condition_uses_count_by_condition[condition_type] != 0) {
		if (variable_type == GOAL_TARGET_NONE) {
			return 2;
		}

		uint16_t total = 0;
		uint16_t total_special_cargo = 0;
		uint16_t arrived_total = 0;
		uint16_t met = 0;
		uint16_t met_special_cargo = 0;
		uint16_t failed = 0;
		uint16_t failed_special_cargo = 0;
		for (flight_group_idx = 0;
		     flight_group_idx <
		     (int16_t)g_mission_header.num_flight_groups;
		     ++flight_group_idx) {
			if (g_mission_flight_groups[flight_group_idx]
					    .fg.craft_type ==
				    CRAFT_SPECIES_UNKNOWN ||
			    mission_flight_group_matches_trigger_variable(
				    flight_group_idx, variable_type,
				    variable) == 0) {
				continue;
			}

			total += g_mission_fg_stats[flight_group_idx]
					 .outcome_count
						 [FLIGHT_GROUP_OUTCOME_TOTAL];
			arrived_total +=
				g_mission_fg_stats[flight_group_idx]
					.outcome_count
						[FLIGHT_GROUP_OUTCOME_ARRIVED];
			total_special_cargo +=
				g_mission_fg_stats[flight_group_idx]
					.special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_TOTAL];
			uint16_t special_cargo_arrived =
				g_mission_fg_stats[flight_group_idx]
					.special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_ARRIVED];
			switch ((mission_condition_type)condition_type) {
			case MISSION_COND_ARRIVED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_ARRIVED];
				met_special_cargo += special_cargo_arrived;
				break;

			case MISSION_COND_DESTROYED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] +
				       g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_DESTROYED];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] +
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_DESTROYED];
				if (include_departed_as_destroyed == 0) {
					failed +=
						g_mission_fg_stats[flight_group_idx]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_LEFT_REGION];
					failed_special_cargo +=
						g_mission_fg_stats[flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_LEFT_REGION];
				} else {
					met += g_mission_fg_stats[flight_group_idx]
						       .outcome_count
							       [FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT] +
					       g_mission_fg_stats[flight_group_idx]
						       .outcome_count
							       [FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT] +
					       g_mission_fg_stats[flight_group_idx]
						       .outcome_count
							       [FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT] +
					       g_mission_fg_stats[flight_group_idx]
						       .outcome_count
							       [FLIGHT_GROUP_OUTCOME_LEFT_REGION];
					met_special_cargo +=
						g_mission_fg_stats[flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT] +
						g_mission_fg_stats[flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT] +
						g_mission_fg_stats[flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT] +
						g_mission_fg_stats[flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_LEFT_REGION];
				}
				break;

			case MISSION_COND_ATTACKED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_ATTACKED];
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_ATTACKED];
				failed_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED];
				break;

			case MISSION_COND_CAPTURED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_CAPTURED];
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_CAPTURED];
				failed_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED];
				break;

			case MISSION_COND_INSPECTED:
				if (team_filter < MISSION_TEAM_COUNT) {
					met += g_mission_fg_stats[flight_group_idx]
						       .team_inspected
							       [team_filter];
					met_special_cargo +=
						g_mission_fg_stats[flight_group_idx]
							.team_special_cargo_inspected
								[team_filter];
					failed +=
						g_mission_fg_stats[flight_group_idx]
							.team_uninspected_lost
								[team_filter];
					failed_special_cargo +=
						g_mission_fg_stats[flight_group_idx]
							.team_special_cargo_uninspected_lost
								[team_filter];
				} else {
					met += g_mission_fg_stats[flight_group_idx]
						       .outcome_count
							       [FLIGHT_GROUP_OUTCOME_INSPECTED];
					failed +=
						g_mission_fg_stats[flight_group_idx]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_NOT_INSPECTED];
					met_special_cargo +=
						g_mission_fg_stats[flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_INSPECTED];
					failed_special_cargo +=
						g_mission_fg_stats[flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_NOT_INSPECTED];
				}
				break;

			case MISSION_COND_BOARDED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_BOARDED];
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_NOT_BOARDED];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_BOARDED];
				failed_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_BOARDED];
				break;

			case MISSION_COND_DOCKED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_DOCKED];
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_NOT_DOCKED];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx]
						.special_cargo_outcome
							[FLIGHT_GROUP_OUTCOME_DOCKED];
				failed_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_DOCKED];
				break;

			case MISSION_COND_DISABLED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_DISABLED];
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_NOT_DISABLED];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_DISABLED];
				failed_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_DISABLED];
				break;

			case MISSION_COND_SURVIVED:
				met = g_mission_fg_stats[flight_group_idx]
					      .outcome_count
						      [FLIGHT_GROUP_OUTCOME_TOTAL] -
				      g_mission_fg_stats[flight_group_idx].outcome_count
					      [FLIGHT_GROUP_OUTCOME_DESTROYED] +
				      met -
				      g_mission_fg_stats[flight_group_idx].outcome_count
					      [FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP];
				met_special_cargo =
					g_mission_fg_stats[flight_group_idx]
						.special_cargo_outcome
							[FLIGHT_GROUP_OUTCOME_TOTAL] -
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] +
					met_special_cargo -
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_DESTROYED];
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] +
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_DESTROYED];
				failed_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] +
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_DESTROYED];
				break;

			case MISSION_COND_DEPARTED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_DEPARTED];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_DEPARTED];
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_DESTROYED] +
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_ABORTED] +
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED];
				failed_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_DESTROYED] +
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_ABORTED] +
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED];
				break;

			case MISSION_COND_SHIELDS_DEPLETED:
			case MISSION_COND_HULL_DAMAGE_ABOVE_50:
			case MISSION_COND_NO_WARHEADS:
			case MISSION_COND_CANNONS_DISABLED:
			case MISSION_COND_SHIELDS_BELOW_50:
			case MISSION_COND_SHIELDS_BELOW_25:
			case MISSION_COND_HULL_DAMAGE_ABOVE_25:
			case MISSION_COND_HULL_DAMAGE_ABOVE_75: {
				for (int object_idx =
					     g_active_region_object_slot_start;
				     object_idx <
				     g_active_region_craft_object_slot_end;
				     ++object_idx) {
					struct object_record *object =
						&g_object_table[object_idx];
					if (object->object_type == 0 ||
					    object->flight_group_idx !=
						    flight_group_idx) {
						continue;
					}
					struct craft_data *craft =
						object->mobj->p_craft;
					int16_t matches_condition = 0;
					int max_shield =
						2 *
						g_model_defs[craft->model_index]
							.shield_strength;
					if (condition_type ==
					    MISSION_COND_SHIELDS_DEPLETED) {
						if (craft->shield_energy[0] +
							    craft->shield_energy
								    [1] <=
						    0) {
							matches_condition = 1;
						}
					} else if (
						condition_type ==
						MISSION_COND_SHIELDS_BELOW_25) {
						if (craft->shield_energy[0] +
							    craft->shield_energy
								    [1] <=
						    max_shield / 4) {
							matches_condition = 1;
						}
					} else if (
						condition_type ==
						MISSION_COND_SHIELDS_BELOW_50) {
						if (craft->shield_energy[0] +
							    craft->shield_energy
								    [1] <=
						    max_shield / 2) {
							matches_condition = 1;
						}
					} else if (
						condition_type ==
						MISSION_COND_HULL_DAMAGE_ABOVE_50) {
						if ((craft->hull_max >> 1) <
						    craft->hull_damage) {
							matches_condition = 1;
						}
					} else if (
						condition_type ==
						MISSION_COND_HULL_DAMAGE_ABOVE_25) {
						if ((craft->hull_max >> 2) <
						    craft->hull_damage) {
							matches_condition = 1;
						}
					} else if (
						condition_type ==
						MISSION_COND_HULL_DAMAGE_ABOVE_75) {
						if (3 * (craft->hull_max >> 2) <
						    craft->hull_damage) {
							matches_condition = 1;
						}
					} else if (condition_type ==
						   MISSION_COND_NO_WARHEADS) {
						const struct model_def *model_def =
							&g_model_defs
								[craft->model_index];
						uint16_t warhead_count = 0;
						for (uint16_t launcher_idx = 0;
						     launcher_idx <
						     craft->warhead_launcher_count;
						     ++launcher_idx) {
							warhead_count +=
								craft->weapon_slots
									[model_def
										 ->warhead_launcher_first_slot
											 [launcher_idx]]
										.ammo_count;
							warhead_count +=
								craft->weapon_slots
									[model_def
										 ->warhead_launcher_last_slot
											 [launcher_idx]]
										.ammo_count;
						}
						if (warhead_count == 0) {
							matches_condition = 1;
						}
					} else if (
						(craft->working_subsystems &
						 CRAFT_SUBSYSTEM_FLAG_CANNONS) ==
						0) {
						matches_condition = 1;
					}

					if (matches_condition != 0) {
						++met;
						if (g_mission_flight_groups
							    [flight_group_idx]
								    .fg
								    .special_cargo_craft ==
						    craft->craft_ordinal) {
							++met_special_cargo;
						}
					} else {
						++failed;
						if (g_mission_flight_groups
							    [flight_group_idx]
								    .fg
								    .special_cargo_craft ==
						    craft->craft_ordinal) {
							++failed_special_cargo;
						}
					}
				}
				break;
			}

			case MISSION_COND_NOT_ARRIVED:
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_ARRIVED];
				failed_special_cargo += special_cargo_arrived;
				break;

			case MISSION_COND_NOT_ATTACKED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_NOT_ATTACKED];
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_ATTACKED];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED];
				failed_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_ATTACKED];
				break;

			case MISSION_COND_NOT_DISABLED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_NOT_DISABLED];
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_DISABLED];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_DISABLED];
				failed_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_DISABLED];
				break;

			case MISSION_COND_NOT_CAPTURED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_NOT_CAPTURED];
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_CAPTURED];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED];
				failed_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_CAPTURED];
				break;

			case MISSION_COND_NOT_INSPECTED:
				if (team_filter < MISSION_TEAM_COUNT) {
					met += g_mission_fg_stats[flight_group_idx]
						       .team_uninspected_lost
							       [team_filter];
					met_special_cargo +=
						g_mission_fg_stats[flight_group_idx]
							.team_special_cargo_uninspected_lost
								[team_filter];
					failed +=
						g_mission_fg_stats[flight_group_idx]
							.team_inspected
								[team_filter];
					failed_special_cargo +=
						g_mission_fg_stats[flight_group_idx]
							.team_special_cargo_inspected
								[team_filter];
				} else {
					met += g_mission_fg_stats[flight_group_idx]
						       .outcome_count
							       [FLIGHT_GROUP_OUTCOME_NOT_INSPECTED];
					failed +=
						g_mission_fg_stats[flight_group_idx]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_INSPECTED];
					met_special_cargo +=
						g_mission_fg_stats[flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_NOT_INSPECTED];
					failed_special_cargo +=
						g_mission_fg_stats[flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_INSPECTED];
				}
				break;

			case MISSION_COND_COMPLETED_MISSION:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION];
				break;

			case MISSION_COND_NOT_BOARDED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_NOT_BOARDED];
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_BOARDED];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_BOARDED];
				failed_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_BOARDED];
				break;

			case MISSION_COND_FAILED_MISSION:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_FAILED_MISSION];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_FAILED_MISSION];
				break;

			case MISSION_COND_NOT_DOCKED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_NOT_DOCKED];
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_DOCKED];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_DOCKED];
				failed_special_cargo +=
					g_mission_fg_stats[flight_group_idx]
						.special_cargo_outcome
							[FLIGHT_GROUP_OUTCOME_DOCKED];
				break;

			case MISSION_COND_DESTROYED_OR_DEPARTED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] +
				       g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_DESTROYED] +
				       g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT] +
				       g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT] +
				       g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT] +
				       g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_LEFT_REGION];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] +
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_DESTROYED] +
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT] +
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT] +
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT] +
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_LEFT_REGION];
				break;

			case MISSION_COND_CAPTURED_AND_DEPARTED:
				if (team_filter < MISSION_TEAM_COUNT) {
					met += g_mission_fg_stats[flight_group_idx]
						       .team_captured_departed_count
							       [team_filter];
					met_special_cargo +=
						g_mission_fg_stats[flight_group_idx]
							.team_special_cargo_captured_departed
								[team_filter];
					failed +=
						g_mission_fg_stats[flight_group_idx]
							.team_uncaptured_lost
								[team_filter];
					failed_special_cargo +=
						g_mission_fg_stats[flight_group_idx]
							.team_special_cargo_uncaptured_lost
								[team_filter];
				} else {
					for (uint16_t team_idx = 0;
					     team_idx < MISSION_TEAM_COUNT;
					     ++team_idx) {
						met += g_mission_fg_stats[flight_group_idx]
							       .team_captured_departed_count
								       [team_idx];
						met_special_cargo +=
							g_mission_fg_stats[flight_group_idx]
								.team_special_cargo_captured_departed
									[team_idx];
						if (g_flight_mission_state
							    .runtime
							    .team_has_countable_craft
								    [team_idx] !=
						    0) {
							failed +=
								g_mission_fg_stats[flight_group_idx]
									.team_uncaptured_lost
										[team_idx];
							failed_special_cargo +=
								g_mission_fg_stats[flight_group_idx]
									.team_special_cargo_uncaptured_lost
										[team_idx];
						}
					}
				}
				break;

			case MISSION_COND_NOT_DEPARTED:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] +
				       g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_DESTROYED] +
				       g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_ABORTED] +
				       g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_NOT_DEPARTED];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] +
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_DESTROYED] +
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_ABORTED] +
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED];
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE] +
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_DEPARTED];
				failed_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_DEPARTED] +
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE];
				break;

			case MISSION_COND_CAPTURED_BY_DESTINATION:
				met += g_mission_fg_stats[flight_group_idx].outcome_count
					       [FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION];
				failed +=
					g_mission_fg_stats[flight_group_idx].outcome_count
						[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION];
				met_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION];
				failed_special_cargo +=
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION];
				break;

			default:
				break;
			}
		}

		status = 4;
		if (total != 0) {
			switch ((uint16_t)amount_type) {
			case GOAL_AMT_100:
				if (met >= total) {
					status = 1;
				} else if (failed != 0) {
					status = 2;
				}
				break;
			case GOAL_AMT_75:
				if (math2_ratio_q16(met, total) >= 0xc000u) {
					status = 1;
				} else if (math2_ratio_q16(failed, total) >
					   0x4000u) {
					status = 2;
				}
				break;
			case GOAL_AMT_50:
				if (math2_ratio_q16(met, total) >= 0x8000u) {
					status = 1;
				} else if (math2_ratio_q16(failed, total) >
					   0x8000u) {
					status = 2;
				}
				break;
			case GOAL_AMT_25:
				if (math2_ratio_q16(met, total) >= 0x4000u) {
					status = 1;
				} else if (math2_ratio_q16(failed, total) >
					   0xc000u) {
					status = 2;
				}
				break;
			case GOAL_AMT_AT_LEAST_1:
			case GOAL_AMT_AT_LEAST_1_ALT:
				if (met != 0) {
					status = 1;
				} else if (failed == total) {
					status = 2;
				}
				break;
			case GOAL_AMT_ALL_BUT_1:
				if ((uint16_t)(total - 1) <= met) {
					status = 1;
				} else if (failed > 1) {
					status = 2;
				}
				break;
			case GOAL_AMT_ALL_SPECIAL_CARGO:
				if (total_special_cargo != 0) {
					if (met_special_cargo ==
					    total_special_cargo) {
						status = 1;
					} else if (failed_special_cargo != 0) {
						status = 2;
					}
				}
				break;
			case GOAL_AMT_ALL_NON_SPECIAL:
				if (total - met == total_special_cargo) {
					status = 1;
				} else if ((failed != 0 &&
					    met_special_cargo == 0) ||
					   (failed > 1 &&
					    met_special_cargo != 0)) {
					status = 2;
				}
				break;
			case GOAL_AMT_100_OF_SUBSET:
				if (met >= arrived_total) {
					status = 1;
				}
				break;
			case GOAL_AMT_75_OF_SUBSET:
				if (math2_ratio_q16(met, arrived_total) >=
				    0xc000u) {
					status = 1;
				}
				break;
			case GOAL_AMT_50_OF_SUBSET:
				if (math2_ratio_q16(met, arrived_total) >=
				    0x8000u) {
					status = 1;
				}
				break;
			case GOAL_AMT_25_OF_SUBSET:
				if (math2_ratio_q16(met, arrived_total) >=
				    0x4000u) {
					status = 1;
				}
				break;
			case GOAL_AMT_ALL_BUT_1_OF_SUBSET:
				if (arrived_total - 1 <= met) {
					status = 1;
				}
				break;
			case GOAL_AMT_66:
				if (math2_ratio_q16(met, total) >= 0xaaaau) {
					status = 1;
				} else if (math2_ratio_q16(failed, total) >
					   0x5555u) {
					status = 2;
				}
				break;
			case GOAL_AMT_33:
				if (math2_ratio_q16(met, total) >= 0x5555u) {
					status = 1;
				} else if (math2_ratio_q16(failed, total) >
					   0xaaaau) {
					status = 2;
				}
				break;
			default:
				break;
			}
		}
		g_mission_condition_current_count = met;
		g_mission_condition_total_count = total;
		return status;
	}

	status = 4;
	switch ((mission_condition_type)condition_type) {
	case MISSION_COND_ALWAYS_TRUE:
		status = 1;
		break;
	case MISSION_COND_NEVER:
		status = 0;
		break;
	case MISSION_COND_PRIMARY_GOAL_COMPLETE: {
		uint8_t goal_status;

		if (variable_type == GOAL_TARGET_TEAM) {
			goal_status = g_flight_mission_state.runtime
					      .team_goal_status[variable][0];
		} else {
			goal_status = g_flight_mission_state.runtime
					      .global_primary_goal_status;
		}
		if (goal_status == 1) {
			status = 1;
		} else if (goal_status == 2) {
			status = 2;
		}
		break;
	}
	case MISSION_COND_PRIMARY_GOAL_FAILED: {
		uint8_t goal_status;

		if (variable_type == GOAL_TARGET_TEAM) {
			goal_status = g_flight_mission_state.runtime
					      .team_goal_status[variable][0];
		} else {
			goal_status = g_flight_mission_state.runtime
					      .global_primary_goal_status;
		}
		if (goal_status == 2) {
			status = 1;
		} else if (goal_status == 1) {
			status = 2;
		}
		break;
	}
	case MISSION_COND_BONUS_GOAL_COMPLETE: {
		uint8_t goal_status;

		if (variable_type == GOAL_TARGET_TEAM) {
			goal_status = g_flight_mission_state.runtime
					      .team_goal_status[variable][2];
			if (goal_status == 2) {
				status = 1;
			} else if (goal_status == 1) {
				status = 2;
			}
		} else {
			goal_status = g_flight_mission_state.runtime
					      .global_bonus_goal_status;
			if (goal_status == 1) {
				status = 1;
			} else if (goal_status == 2) {
				status = 2;
			}
		}
		break;
	}
	case MISSION_COND_BONUS_GOAL_FAILED:
		if (g_flight_mission_state.runtime.global_bonus_goal_status ==
		    2) {
			status = 1;
		} else if (g_flight_mission_state.runtime
				   .global_bonus_goal_status == 1) {
			status = 2;
		}
		break;
	case MISSION_COND_REINFORCEMENTS_CALLED:
		status = (g_flight_mission_state.runtime
				  .team_reinforcements_called[variable] == 0) +
			 1;
		break;
	case MISSION_COND_ALWAYS_FAILED:
		status = 2;
		break;

	case MISSION_COND_PLAYER_CONNECTED:
	case MISSION_COND_PLAYER_DISCONNECTED: {
		status = 2;

		uint16_t connected_by_team[MISSION_TEAM_COUNT];
		uint16_t player_craft_by_team[MISSION_TEAM_COUNT];
		uint16_t player_idx;
		for (player_idx = 0; player_idx < MISSION_TEAM_COUNT;
		     ++player_idx) {
			connected_by_team[player_idx] = 0;
			player_craft_by_team[player_idx] = 0;
		}
		uint16_t connected_by_iff[MISSION_IFF_COUNT];
		uint16_t player_craft_by_iff[MISSION_IFF_COUNT];
		for (player_idx = 0; player_idx < MISSION_IFF_COUNT;
		     ++player_idx) {
			connected_by_iff[player_idx] = 0;
			player_craft_by_iff[player_idx] = 0;
		}
		for (player_idx = 0; player_idx < MISSION_PLAYER_COUNT;
		     ++player_idx) {
			if (g_players[player_idx].participation_state != 0) {
				++connected_by_team
					[(uint16_t)g_players[player_idx].team];
				++connected_by_iff
					[(uint16_t)g_players[player_idx].iff];
			}
		}
		for (flight_group_idx = 0;
		     flight_group_idx <
		     (int16_t)g_mission_header.num_flight_groups;
		     ++flight_group_idx) {
			if (g_mission_flight_groups[flight_group_idx]
				    .fg.player_number != 0) {
				++player_craft_by_team
					[g_mission_flight_groups
						 [flight_group_idx]
							 .fg.team];
				++player_craft_by_iff[g_mission_flight_groups
							      [flight_group_idx]
								      .fg.iff];
			}
		}

		if (variable_type == GOAL_TARGET_PLAYER_NUMBER) {
			if (condition_type == MISSION_COND_PLAYER_CONNECTED) {
				for (flight_group_idx = 0;
				     flight_group_idx <
				     (int16_t)
					     g_mission_header.num_flight_groups;
				     ++flight_group_idx) {
					uint8_t player_number =
						g_mission_flight_groups
							[flight_group_idx]
								.fg
								.player_number;
					if (player_number != 0 &&
					    player_number - variable == 1 &&
					    g_mission_flight_groups[flight_group_idx]
							    .player_owner_idx !=
						    -1) {
						status = 1;
					}
				}
			} else {
				for (flight_group_idx = 0;
				     flight_group_idx <
				     (int16_t)
					     g_mission_header.num_flight_groups;
				     ++flight_group_idx) {
					uint8_t player_number =
						g_mission_flight_groups
							[flight_group_idx]
								.fg
								.player_number;
					if (player_number != 0 &&
					    player_number - variable == 1 &&
					    g_mission_flight_groups[flight_group_idx]
							    .player_owner_idx ==
						    -1) {
						status = 1;
					}
				}
			}
			break;
		}

		uint16_t total_players = 0;
		uint16_t current_players = 0;
		if (variable_type == GOAL_TARGET_TEAM) {
			total_players = player_craft_by_team[variable];
			if (condition_type == MISSION_COND_PLAYER_CONNECTED) {
				current_players = connected_by_team[variable];
			} else {
				current_players = total_players -
						  connected_by_team[variable];
			}
		} else if (variable_type == GOAL_TARGET_IFF) {
			total_players = player_craft_by_iff[variable];
			if (condition_type == MISSION_COND_PLAYER_CONNECTED) {
				current_players = connected_by_iff[variable];
			} else {
				current_players = total_players -
						  connected_by_iff[variable];
			}
		}

		switch ((uint16_t)amount_type) {
		case GOAL_AMT_100:
			if (current_players == total_players) {
				status = 1;
			}
			break;
		case GOAL_AMT_75:
			if (math2_ratio_q16(current_players, total_players) >=
			    0xc000u) {
				status = 1;
			}
			break;
		case GOAL_AMT_50:
			if (math2_ratio_q16(current_players, total_players) >=
			    0x8000u) {
				status = 1;
			}
			break;
		case GOAL_AMT_25:
			if (math2_ratio_q16(current_players, total_players) >=
			    0x4000u) {
				status = 1;
			}
			break;
		case GOAL_AMT_AT_LEAST_1:
			if (current_players != 0) {
				status = 1;
			}
			break;
		case GOAL_AMT_ALL_BUT_1:
			if (total_players - current_players == 1) {
				status = 1;
			}
			break;
		case GOAL_AMT_66:
			if (math2_ratio_q16(current_players, total_players) >=
			    0xaaaau) {
				status = 1;
			}
			break;
		case GOAL_AMT_33:
			if (math2_ratio_q16(current_players, total_players) >=
			    0x5555u) {
				status = 1;
			}
			break;
		default:
			break;
		}
		break;
	}

	default:
		break;
	}
	return status;
}

/* Tells whether a flight group falls under a trigger's variable; returns 1
 * when it does, else 0. variable_type 1 matches the group itself, 2 its
 * species (variable + 1), 3 its genus and 4 its family (through
 * g_genus_convert and g_family_convert), 5 its IFF, 6 its first order, 8 its
 * global group, 9 its group_ai, 10 its status1, 12 its team and 23 its global
 * unit; 15 to 21 and 24 match the opposite of 1 to 5, 8, 12 and 23. Type 7
 * matches the groups a player owns for variable 9, those none owns for 10,
 * and every group otherwise; type 11 matches every group; 0 and any other
 * type match none. Does not check variable against the tables it indexes. */
// FUNCTION: XVT 0x432FA0
int16_t mission_flight_group_matches_trigger_variable(uint16_t flight_group_idx,
						      int16_t variable_type,
						      uint16_t variable)
{
	int16_t result = 0;
	uint8_t object_type = g_craft_type_to_object_type
		[g_mission_flight_groups[flight_group_idx].fg.craft_type];
	uint16_t trigger_variable_type = (uint16_t)variable_type;
	switch (trigger_variable_type) {
	case 0:
		return result;

	case 1:
		if (flight_group_idx == variable) {
			result = 1;
			return result;
		}
		break;

	case 2:
		if (g_craft_type_to_object_type[variable + 1] == object_type) {
			result = 1;
			return result;
		}
		break;

	case 3:
		if (g_object_type_table[object_type].genus_id ==
		    g_genus_convert[variable]) {
			result = 1;
			return result;
		}
		break;

	case 4:
		if ((uint8_t)g_object_type_table[object_type].family_id ==
		    g_family_convert[variable]) {
			result = 1;
			return result;
		}
		break;

	case 5:
		if (g_mission_flight_groups[flight_group_idx].fg.iff ==
		    variable) {
			result = 1;
			return result;
		}
		break;

	case 6:
		if (g_mission_flight_groups[flight_group_idx]
			    .fg.orders[0]
			    .order == variable) {
			result = 1;
			return result;
		}
		break;

	case 7:
		result = 0;
		if (variable == 9) {
			if (g_mission_flight_groups[flight_group_idx]
				    .player_owner_idx != -1) {
				result = 1;
				return result;
			}
			break;
		}
		if (variable == 10) {
			if (g_mission_flight_groups[flight_group_idx]
				    .player_owner_idx == -1) {
				result = 1;
				return result;
			}
			break;
		}
		result = 1;
		break;

	case 8:
		if (g_mission_flight_groups[flight_group_idx].fg.global_group ==
		    variable) {
			result = 1;
			return result;
		}
		break;

	case 9:
		if (g_mission_flight_groups[flight_group_idx].fg.group_ai ==
		    variable) {
			result = 1;
			return result;
		}
		break;

	case 10:
		if (g_mission_flight_groups[flight_group_idx].fg.status1 ==
		    variable) {
			result = 1;
			return result;
		}
		break;

	case 11:
		result = 1;
		break;

	case 12:
		if (g_mission_flight_groups[flight_group_idx].fg.team ==
		    variable) {
			result = 1;
			return result;
		}
		break;

	case 15:
		if (flight_group_idx != variable) {
			result = 1;
			return result;
		}
		break;

	case 16:
		if (g_craft_type_to_object_type[variable + 1] != object_type) {
			result = 1;
			return result;
		}
		break;

	case 17:
		if (g_object_type_table[object_type].genus_id !=
		    g_genus_convert[variable]) {
			result = 1;
			return result;
		}
		break;

	case 18:
		if ((uint8_t)g_object_type_table[object_type].family_id !=
		    g_family_convert[variable]) {
			result = 1;
			return result;
		}
		break;

	case 19:
		if (g_mission_flight_groups[flight_group_idx].fg.iff !=
		    variable) {
			result = 1;
			return result;
		}
		break;

	case 20:
		if (g_mission_flight_groups[flight_group_idx].fg.global_group !=
		    variable) {
			result = 1;
			return result;
		}
		break;

	case 21:
		if (g_mission_flight_groups[flight_group_idx].fg.team !=
		    variable) {
			result = 1;
			return result;
		}
		break;

	case 23:
		if (g_mission_flight_groups[flight_group_idx].fg.global_unit ==
		    variable) {
			result = 1;
			return result;
		}
		break;

	case 24:
		if (g_mission_flight_groups[flight_group_idx].fg.global_unit !=
		    variable) {
			result = 1;
			return result;
		}
		break;

	default:
		break;
	}

	return result;
}

/* Tells whether one object falls under a trigger's variable; returns 1 when
 * it does, else 0. The types match as in
 * mission_flight_group_matches_trigger_variable, through the object's flight
 * group, except that IFF and team come from the object's mobile record when
 * it has one. Type 7, for a mobile object only, tests its craft's state by
 * variable: captured (0), identified by another team (1), boarded (2),
 * docked (3), no working subsystems (4), attacked by another team (5), hull
 * damaged (6), special cargo or not (7, 8), owned by a player or not (9, 10),
 * most of the opposites (11 to 18; 15 and 17 never match), hull damage at
 * least a quarter, half or three quarters of hull_max (22 to 24) and no
 * warheads left (25). Type 13 matches when the owning player's bound flight
 * group has player number variable + 1; the original build indexes g_players
 * even when the object has no owner (-1), the modern build answers 0. Type 14
 * matches while the elapsed clock's minutes and seconds, ignoring hours, are
 * at or before variable times 5 seconds; type 22 matches an object not owned
 * by player variable. */
// FUNCTION: XVT 0x433390
int16_t mission_object_matches_trigger_variable(uint16_t object_idx,
						uint16_t variable_type,
						uint16_t variable)
{
	struct object_record *object = &g_object_table[object_idx];
	struct mobile_object *mobile = object->mobj;
	struct craft_data *craft;
	uint16_t flight_group;
	uint16_t team;
	uint16_t object_type;
	int16_t result;

	if (mobile != NULL) {
		craft = mobile->p_craft;
		flight_group = object->flight_group_idx;
		team = mobile->team;
	} else {
		flight_group = object->flight_group_idx;
		team = g_mission_flight_groups[flight_group].fg.team;
	}
	result = 0;
	object_type = g_craft_type_to_object_type
		[g_mission_flight_groups[flight_group].fg.craft_type];

	switch (variable_type) {
	case 0:
		result = 0;
		break;
	case 1:
		if (variable == flight_group) {
			result = 1;
		}
		break;
	case 2:
		if (g_craft_type_to_object_type[variable + 1] == object_type) {
			result = 1;
		}
		break;
	case 3:
		if (g_object_type_table[object_type].genus_id ==
		    g_genus_convert[variable]) {
			result = 1;
		}
		break;
	case 4:
		if ((uint8_t)g_object_type_table[object_type].family_id ==
		    g_family_convert[variable]) {
			result = 1;
		}
		break;
	case 5:
		if (mobile == NULL) {
			if (g_mission_flight_groups[flight_group].fg.iff ==
			    variable) {
				result = 1;
			}
		} else {
			if ((uint8_t)mobile->iff == variable) {
				result = 1;
			}
		}
		break;
	case 6:
		if (g_mission_flight_groups[flight_group].fg.orders[0].order ==
		    variable) {
			result = 1;
		}
		break;
	case 7:
		if (mobile == NULL) {
			break;
		}
		switch (variable) {
		case 0:
			if (craft->captured_by_flight_group != 0) {
				result = 1;
			}
			break;
		case 1: {
			uint16_t index;
			for (index = 0; index < 10; ++index) {
				if (mobile->team != index &&
				    craft->identified_order_by_team[index] !=
					    0) {
					result = 1;
				}
			}
			break;
		}
		case 2:
			if (craft->ai_flight.times_boarded != 0) {
				result = 1;
			}
			break;
		case 3:
			if (craft->ai_flight.docked_target_count != 0) {
				result = 1;
			}
			break;
		case 4:
			if (craft->working_subsystems == 0) {
				result = 1;
			}
			break;
		case 5: {
			uint16_t index;
			for (index = 0; index < 10; ++index) {
				if (mobile->team != index &&
				    craft->attacked_by_team[index] != 0) {
					result = 1;
				}
			}
			break;
		}
		case 6:
			if (craft->hull_damage != 0) {
				result = 1;
			}
			break;
		case 7:
			if (g_mission_flight_groups[flight_group]
				    .fg.special_cargo_craft ==
			    craft->craft_ordinal) {
				result = 1;
			}
			break;
		case 8:
			if (g_mission_flight_groups[flight_group]
				    .fg.special_cargo_craft !=
			    craft->craft_ordinal) {
				result = 1;
			}
			break;
		case 9:
			if (object->player_owner_idx != -1) {
				result = 1;
			}
			break;
		case 10:
			if (object->player_owner_idx == -1) {
				result = 1;
			}
			break;
		case 11: {
			uint16_t index;
			for (index = 0; index < 10; ++index) {
				if (craft->attacked_by_team[index] == 0) {
					result = 1;
				}
			}
			break;
		}
		case 12:
			if (craft->working_subsystems != 0) {
				result = 1;
			}
			break;
		case 13:
			if (craft->captured_by_flight_group == 0) {
				result = 1;
			}
			break;
		case 14: {
			uint16_t index;
			for (index = 0; index < 10; ++index) {
				if (craft->identified_order_by_team[index] ==
				    0) {
					result = 1;
				}
			}
			break;
		}
		case 15:
			result = 0;
			break;
		case 16:
			if (craft->ai_flight.times_boarded == 0) {
				result = 1;
			}
			break;
		case 17:
			result = 0;
			break;
		case 18:
			if (craft->ai_flight.docked_target_count == 0) {
				result = 1;
			}
			break;
		case 22:
			if ((craft->hull_max >> 2) <= craft->hull_damage) {
				result = 1;
			}
			break;
		case 23:
			if ((craft->hull_max >> 1) <= craft->hull_damage) {
				result = 1;
			}
			break;
		case 24:
			if (math2_longfraction(craft->hull_max, 0xC000) <=
			    craft->hull_damage) {
				result = 1;
			}
			break;
		case 25: {
			int16_t warhead_count = 0;
			uint16_t index;
			uint8_t warhead_launcher_count =
				craft->warhead_launcher_count;
			for (index = 0; index < warhead_launcher_count;
			     ++index) {
				warhead_count +=
					craft->weapon_slots
						[g_model_defs[craft->model_index]
							 .warhead_launcher_first_slot
								 [index]]
							.ammo_count;
				warhead_count +=
					craft->weapon_slots
						[g_model_defs[craft->model_index]
							 .warhead_launcher_last_slot
								 [index]]
							.ammo_count;
			}
			if (warhead_count == 0) {
				result = 1;
			}
			break;
		}
		default:
			break;
		}
		break;
	case 8:
		if (g_mission_flight_groups[flight_group].fg.global_group ==
		    variable) {
			result = 1;
		}
		break;
	case 9:
		if (g_mission_flight_groups[flight_group].fg.group_ai ==
		    variable) {
			result = 1;
		}
		break;
	case 10:
		if (g_mission_flight_groups[flight_group].fg.status1 ==
		    variable) {
			result = 1;
		}
		break;
	case 11:
		result = 1;
		break;
	case 12:
		if (variable == team) {
			result = 1;
		}
		break;
	case 13:
		if (object->player_owner_idx >= 0 &&
		    g_mission_flight_groups[g_players[object->player_owner_idx]
						    .bound_flight_group_idx]
					    .fg.player_number -
				    variable ==
			    1) {
			result = 1;
		}
		break;
	case 14: {
		uint16_t target_time = 5 * variable;
		uint16_t target_minutes = target_time / 60;
		if (g_mission_elapsed_clock.minutes < target_minutes) {
			result = 1;
		} else if (g_mission_elapsed_clock.minutes == target_minutes) {
			result = target_time % 60 >=
				 g_mission_elapsed_clock.seconds;
		}
		break;
	}
	case 15:
		if (variable != flight_group) {
			result = 1;
		}
		break;
	case 16:
		if (g_craft_type_to_object_type[variable + 1] != object_type) {
			result = 1;
		}
		break;
	case 17:
		if (g_object_type_table[object_type].genus_id !=
		    g_genus_convert[variable]) {
			result = 1;
		}
		break;
	case 18:
		if ((uint8_t)g_object_type_table[object_type].family_id !=
		    g_family_convert[variable]) {
			result = 1;
		}
		break;
	case 19:
		if (mobile == NULL) {
			if (g_mission_flight_groups[flight_group].fg.iff !=
			    variable) {
				result = 1;
			}
		} else {
			if ((uint8_t)mobile->iff != variable) {
				result = 1;
			}
		}
		break;
	case 20:
		if (g_mission_flight_groups[flight_group].fg.global_group !=
		    variable) {
			result = 1;
		}
		break;
	case 21:
		if (variable != team) {
			result = 1;
		}
		break;
	case 22:
		if (object->player_owner_idx != variable) {
			result = 1;
		}
		break;
	case 23:
		if (g_mission_flight_groups[flight_group].fg.global_unit ==
		    variable) {
			result = 1;
		}
		break;
	case 24:
		if (g_mission_flight_groups[flight_group].fg.global_unit !=
		    variable) {
			result = 1;
		}
		break;
	default:
		break;
	}
	return result;
}

/* Records how one craft left the mission. Does nothing when its
 * mission_accounting_done is already 1; else sets it, adds one to
 * g_mission_fg_stats[flight_group_idx].outcome_count[outcome_id], and marks
 * special_cargo_outcome when the craft is the group's special cargo craft. A
 * destroyed craft takes back its captured, not-departed and aborted counts
 * and adds a not-captured-by-destination; any other end of a player-owned
 * craft with no depart timer or abort, while its team's primary goal is not
 * complete, counts as not departed. Every team that had not identified it
 * counts it uninspected-lost, and it counts as not disabled (unless
 * not_disabled_accounting_suppress is set), not captured (and uncaptured-lost
 * for every other team), not attacked, not boarded and not docked as its
 * state says. On a destroyed craft, groups that arrive from its group are
 * closed with mission_close_unavailable_flight_group_accounting, groups that
 * depart through it move their mothership-dependent counts to lost with
 * mothership, and the group named by any of its orders whose plan is
 * "dropoffldr1pln" is closed; on left-region those counts move to left
 * region. Last, it clears the craft from every craft's last_attacker_obj_idx and
 * every player's target_preset_slot. Does not check that the object has a
 * craft. */
// FUNCTION: XVT 0x433D30
void mission_record_craft_outcome(uint16_t obj_idx, uint16_t flight_group_idx,
				  uint16_t outcome_id)
{
	struct craft_data *craft = g_object_table[obj_idx].mobj->p_craft;

	if (craft->mission_accounting_done == 1) {
		return;
	}
	craft->mission_accounting_done = 1;
	++g_mission_fg_stats[flight_group_idx].outcome_count[outcome_id];
	if (craft->craft_ordinal ==
	    g_mission_flight_groups[flight_group_idx].fg.special_cargo_craft) {
		g_mission_fg_stats[flight_group_idx]
			.special_cargo_outcome[outcome_id] = 1;
	}
	XVT_LOG_DEBUG(
		"mission.craft_outcome object=%d fg=%d outcome=%d slot=%d ordinal=%d special=%d count=%u captured=%d departing=%d aborted=%d predicted=%d",
		(int)obj_idx, (int)flight_group_idx, (int)outcome_id,
		g_object_table[obj_idx].player_owner_idx,
		(int)craft->craft_ordinal,
		craft->craft_ordinal ==
			g_mission_flight_groups[flight_group_idx]
				.fg.special_cargo_craft,
		(unsigned)g_mission_fg_stats[flight_group_idx]
			.outcome_count[outcome_id],
		(int)craft->captured_by_flight_group,
		(int)craft->ai_flight.depart_timer_flag,
		(int)craft->ai_flight.mission_aborted_flag,
		g_flight_sim_side_effects_suppressed);

	if (outcome_id == FLIGHT_GROUP_OUTCOME_DESTROYED) {
		if (craft->captured_by_flight_group != 0) {
			--g_mission_fg_stats[flight_group_idx]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_CAPTURED];
			if (craft->craft_ordinal ==
			    g_mission_flight_groups[flight_group_idx]
				    .fg.special_cargo_craft) {
				g_mission_fg_stats[flight_group_idx]
					.special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_CAPTURED] =
					0;
			}
			craft->captured_by_flight_group = 0;
		}
		if (craft->ai_flight.depart_timer_flag != 0) {
			--g_mission_fg_stats[flight_group_idx].outcome_count
				  [FLIGHT_GROUP_OUTCOME_NOT_DEPARTED];
			if (craft->craft_ordinal ==
			    g_mission_flight_groups[flight_group_idx]
				    .fg.special_cargo_craft) {
				g_mission_fg_stats[flight_group_idx].special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] = 0;
			}
		}
		if (craft->ai_flight.mission_aborted_flag != 0) {
			--g_mission_fg_stats[flight_group_idx]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ABORTED];
			if (craft->craft_ordinal ==
			    g_mission_flight_groups[flight_group_idx]
				    .fg.special_cargo_craft) {
				g_mission_fg_stats[flight_group_idx]
					.special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_ABORTED] =
					0;
			}
		}
		++g_mission_fg_stats[flight_group_idx].outcome_count
			  [FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION];
		if (craft->craft_ordinal ==
		    g_mission_flight_groups[flight_group_idx]
			    .fg.special_cargo_craft) {
			g_mission_fg_stats[flight_group_idx].special_cargo_outcome
				[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION] =
				1;
		}
	} else if (g_object_table[obj_idx].player_owner_idx != -1 &&
		   craft->ai_flight.depart_timer_flag == 0 &&
		   craft->ai_flight.mission_aborted_flag == 0 &&
		   g_flight_mission_state.runtime.team_goal_status
				   [g_object_table[obj_idx].mobj->team][0] !=
			   1) {
		++g_mission_fg_stats[flight_group_idx]
			  .outcome_count[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED];
		if (craft->craft_ordinal ==
		    g_mission_flight_groups[flight_group_idx]
			    .fg.special_cargo_craft) {
			g_mission_fg_stats[flight_group_idx]
				.special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] = 1;
		}
	}

	{
		for (uint16_t index = 0; index < 10; ++index) {
			if (craft->identified_order_by_team[index] == 0) {
				++g_mission_fg_stats[flight_group_idx]
					  .team_uninspected_lost[index];
				if (craft->craft_ordinal ==
				    g_mission_flight_groups[flight_group_idx]
					    .fg.special_cargo_craft) {
					g_mission_fg_stats[flight_group_idx]
						.team_special_cargo_uninspected_lost
							[index] = 1;
				}
			}
		}
	}
	if (craft->not_disabled_accounting_suppress == 0) {
		++g_mission_fg_stats[flight_group_idx]
			  .outcome_count[FLIGHT_GROUP_OUTCOME_NOT_DISABLED];
		if (craft->craft_ordinal ==
		    g_mission_flight_groups[flight_group_idx]
			    .fg.special_cargo_craft) {
			g_mission_fg_stats[flight_group_idx]
				.special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_NOT_DISABLED] = 1;
		}
	}
	if (craft->captured_by_flight_group == 0) {
		++g_mission_fg_stats[flight_group_idx]
			  .outcome_count[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED];
		if (g_mission_flight_groups[flight_group_idx]
			    .fg.special_cargo_craft == craft->craft_ordinal) {
			g_mission_fg_stats[flight_group_idx]
				.special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED] = 1;
		}
		for (uint16_t index = 0; index < 10; ++index) {
			if (g_mission_flight_groups[flight_group_idx].fg.team !=
			    index) {
				++g_mission_fg_stats[flight_group_idx]
					  .team_uncaptured_lost[index];
				g_mission_fg_stats[flight_group_idx]
					.team_special_cargo_uncaptured_lost
						[index] = 1;
			}
		}
	}
	{
		int16_t has_attacked_team = 0;
		for (uint16_t index = 0; index < 10; ++index) {
			if (craft->attacked_by_team[index] == 1) {
				has_attacked_team = 1;
			}
		}
		if (has_attacked_team == 0) {
			++g_mission_fg_stats[flight_group_idx].outcome_count
				  [FLIGHT_GROUP_OUTCOME_NOT_ATTACKED];
			if (craft->craft_ordinal ==
			    g_mission_flight_groups[flight_group_idx]
				    .fg.special_cargo_craft) {
				g_mission_fg_stats[flight_group_idx].special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED] = 1;
			}
		}
	}
	if (craft->ai_flight.times_boarded == 0) {
		++g_mission_fg_stats[flight_group_idx]
			  .outcome_count[FLIGHT_GROUP_OUTCOME_NOT_BOARDED];
		if (craft->craft_ordinal ==
		    g_mission_flight_groups[flight_group_idx]
			    .fg.special_cargo_craft) {
			g_mission_fg_stats[flight_group_idx]
				.special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_NOT_BOARDED] = 1;
		}
	}
	if (craft->ai_flight.docked_target_count == 0) {
		++g_mission_fg_stats[flight_group_idx]
			  .outcome_count[FLIGHT_GROUP_OUTCOME_NOT_DOCKED];
		if (g_mission_flight_groups[flight_group_idx]
			    .fg.special_cargo_craft == craft->craft_ordinal) {
			g_mission_fg_stats[flight_group_idx]
				.special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_NOT_DOCKED] = 1;
		}
	}

	if (outcome_id == FLIGHT_GROUP_OUTCOME_DESTROYED) {
		for (uint16_t index = 0;
		     g_mission_header.num_flight_groups > index; ++index) {
			if (flight_group_idx != index) {
				if (g_mission_flight_groups[index]
						    .fg.arrival_method != 0 &&
				    g_mission_flight_groups[index]
						    .fg.arrival_mothership ==
					    flight_group_idx) {
					mission_close_unavailable_flight_group_accounting(
						index);
				}
				if (g_mission_flight_groups[index]
						    .fg.departure_method != 0 &&
				    g_mission_flight_groups[index]
						    .fg.departure_mothership ==
					    flight_group_idx) {
					g_mission_fg_stats[index].outcome_count
						[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] +=
						g_mission_fg_stats[index].outcome_count
							[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT];
					XVT_LOG_DEBUG(
						"mission.dependents_moved fg=%d mothership=%d kind=\"primary\" to=\"lost_with_mothership\" moved=%u total=%u predicted=%d",
						(int)index,
						(int)flight_group_idx,
						(unsigned)g_mission_fg_stats[index]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT],
						(unsigned)g_mission_fg_stats[index]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP],
						g_flight_sim_side_effects_suppressed);
					g_mission_fg_stats[index].outcome_count
						[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT] =
						0;
				}
				if (g_mission_flight_groups[index]
						    .fg
						    .alternate_mothership_used !=
					    0 &&
				    g_mission_flight_groups[index]
						    .fg.alternate_mothership ==
					    flight_group_idx) {
					g_mission_fg_stats[index].outcome_count
						[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] +=
						g_mission_fg_stats[index].outcome_count
							[FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT];
					XVT_LOG_DEBUG(
						"mission.dependents_moved fg=%d mothership=%d kind=\"alternate\" to=\"lost_with_mothership\" moved=%u total=%u predicted=%d",
						(int)index,
						(int)flight_group_idx,
						(unsigned)g_mission_fg_stats[index]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT],
						(unsigned)g_mission_fg_stats[index]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP],
						g_flight_sim_side_effects_suppressed);
					g_mission_fg_stats[index].outcome_count
						[FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT] =
						0;
				}
				if (g_mission_flight_groups[index]
						    .fg
						    .captured_depart_via_mothership !=
					    0 &&
				    g_mission_flight_groups[index]
						    .fg
						    .captured_departure_mothership ==
					    flight_group_idx) {
					g_mission_fg_stats[index].outcome_count
						[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] +=
						g_mission_fg_stats[index].outcome_count
							[FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT];
					XVT_LOG_DEBUG(
						"mission.dependents_moved fg=%d mothership=%d kind=\"captured\" to=\"lost_with_mothership\" moved=%u total=%u predicted=%d",
						(int)index,
						(int)flight_group_idx,
						(unsigned)g_mission_fg_stats[index]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT],
						(unsigned)g_mission_fg_stats[index]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP],
						g_flight_sim_side_effects_suppressed);
					g_mission_fg_stats[index].outcome_count
						[FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT] =
						0;
				}
			}
		}
		{
			struct mission_order *order =
				g_mission_flight_groups[flight_group_idx]
					.fg.orders;
			int orders_remaining = 4;
			do {
				if (strcmp(g_plan_table
						   [g_builtin_plan_id_by_name_index
							    [g_order_leader_builtin_plan_name_index
								     [order->order]]]
							   .name,
					   "dropoffldr1pln") == 0) {
					if (g_flight_sim_side_effects_suppressed ==
						    0 &&
					    (order->variable2 == 0 ||
					     order->variable2 >
						     g_mission_header
							     .num_flight_groups)) {
						XVT_LOG_WARN(
							"mission.dropoff_group_invalid fg=%d order=%d group=%d groups=%d",
							(int)flight_group_idx,
							4 - orders_remaining,
							(int)order->variable2,
							(int)g_mission_header
								.num_flight_groups);
					}
					mission_close_unavailable_flight_group_accounting(
						(uint16_t)(order->variable2 -
							   1));
				}
				++order;
			} while (--orders_remaining != 0);
		}
	}
	if (outcome_id == FLIGHT_GROUP_OUTCOME_LEFT_REGION) {
		for (uint16_t index = 0;
		     g_mission_header.num_flight_groups > index; ++index) {
			if (flight_group_idx != index) {
				if (g_mission_flight_groups[index]
						    .fg.departure_method != 0 &&
				    g_mission_flight_groups[index]
						    .fg.departure_mothership ==
					    flight_group_idx) {
					g_mission_fg_stats[index].outcome_count
						[FLIGHT_GROUP_OUTCOME_LEFT_REGION] +=
						g_mission_fg_stats[index].outcome_count
							[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT];
					XVT_LOG_DEBUG(
						"mission.dependents_moved fg=%d mothership=%d kind=\"primary\" to=\"left_region\" moved=%u total=%u predicted=%d",
						(int)index,
						(int)flight_group_idx,
						(unsigned)g_mission_fg_stats[index]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT],
						(unsigned)g_mission_fg_stats[index]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_LEFT_REGION],
						g_flight_sim_side_effects_suppressed);
					g_mission_fg_stats[index].outcome_count
						[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT] =
						0;
				}
				if (g_mission_flight_groups[index]
						    .fg
						    .alternate_mothership_used !=
					    0 &&
				    g_mission_flight_groups[index]
						    .fg.alternate_mothership ==
					    flight_group_idx) {
					g_mission_fg_stats[index].outcome_count
						[FLIGHT_GROUP_OUTCOME_LEFT_REGION] +=
						g_mission_fg_stats[index].outcome_count
							[FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT];
					XVT_LOG_DEBUG(
						"mission.dependents_moved fg=%d mothership=%d kind=\"alternate\" to=\"left_region\" moved=%u total=%u predicted=%d",
						(int)index,
						(int)flight_group_idx,
						(unsigned)g_mission_fg_stats[index]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT],
						(unsigned)g_mission_fg_stats[index]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_LEFT_REGION],
						g_flight_sim_side_effects_suppressed);
					g_mission_fg_stats[index].outcome_count
						[FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT] =
						0;
				}
				if (g_mission_flight_groups[index]
						    .fg
						    .captured_depart_via_mothership !=
					    0 &&
				    g_mission_flight_groups[index]
						    .fg
						    .captured_departure_mothership ==
					    flight_group_idx) {
					g_mission_fg_stats[index].outcome_count
						[FLIGHT_GROUP_OUTCOME_LEFT_REGION] +=
						g_mission_fg_stats[index].outcome_count
							[FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT];
					XVT_LOG_DEBUG(
						"mission.dependents_moved fg=%d mothership=%d kind=\"captured\" to=\"left_region\" moved=%u total=%u predicted=%d",
						(int)index,
						(int)flight_group_idx,
						(unsigned)g_mission_fg_stats[index]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT],
						(unsigned)g_mission_fg_stats[index]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_LEFT_REGION],
						g_flight_sim_side_effects_suppressed);
					g_mission_fg_stats[index].outcome_count
						[FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT] =
						0;
				}
			}
		}
	}
	{
		for (int slot_index = g_active_region_object_slot_start;
		     g_active_region_craft_object_slot_end > slot_index;
		     ++slot_index) {
			if (g_object_table[slot_index].object_type != 0) {
				struct craft_data *other_craft =
					g_object_table[slot_index]
						.mobj->p_craft;
				if (other_craft->last_attacker_obj_idx ==
				    obj_idx) {
					other_craft->last_attacker_obj_idx =
						UINT16_MAX;
				}
			}
		}
	}
	{
		for (int slot_index = 0; slot_index < 8; ++slot_index) {
			if (g_players[slot_index].participation_state != 0) {
				for (int preset_index = 0; preset_index < 4;
				     ++preset_index) {
					if (g_players[slot_index]
						    .target_preset_slot
							    [preset_index] ==
					    (int16_t)obj_idx) {
						g_players[slot_index]
							.target_preset_slot
								[preset_index] =
							-1;
					}
				}
			}
		}
	}
}

/* Closes a flight group whose remaining craft can no longer arrive: the craft
 * not yet arrived (total minus arrived) are added to its arrived,
 * lost-with-mothership, not-inspected, not-disabled, not-captured,
 * not-attacked and not-boarded counts, and likewise, arrived aside, to
 * special_cargo_outcome; then has_arrived is set to 1 and waves_remaining to 0.
 * Returns the new arrived
 * count. Writes only g_mission_fg_stats[flight_group_idx]; does not check the
 * index. */
// FUNCTION: XVT 0x434440
int16_t mission_close_unavailable_flight_group_accounting(int flight_group_idx)
{
	int16_t unavailable_count =
		g_mission_fg_stats[flight_group_idx]
			.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] -
		g_mission_fg_stats[flight_group_idx]
			.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED];
	int16_t unavailable_special_cargo_count =
		g_mission_fg_stats[flight_group_idx]
			.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_TOTAL] -
		g_mission_fg_stats[flight_group_idx]
			.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_ARRIVED];
	g_mission_fg_stats[flight_group_idx]
		.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED] +=
		unavailable_count;
	int16_t result = g_mission_fg_stats[flight_group_idx]
				 .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED];
	g_mission_fg_stats[flight_group_idx]
		.outcome_count[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] +=
		unavailable_count;
	g_mission_fg_stats[flight_group_idx].special_cargo_outcome
		[FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP] +=
		unavailable_special_cargo_count;
	g_mission_fg_stats[flight_group_idx]
		.outcome_count[FLIGHT_GROUP_OUTCOME_NOT_INSPECTED] +=
		unavailable_count;
	g_mission_fg_stats[flight_group_idx]
		.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_NOT_INSPECTED] +=
		unavailable_special_cargo_count;
	g_mission_fg_stats[flight_group_idx]
		.outcome_count[FLIGHT_GROUP_OUTCOME_NOT_DISABLED] +=
		unavailable_count;
	g_mission_fg_stats[flight_group_idx]
		.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_NOT_DISABLED] +=
		unavailable_special_cargo_count;
	g_mission_fg_stats[flight_group_idx]
		.outcome_count[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED] +=
		unavailable_count;
	g_mission_fg_stats[flight_group_idx]
		.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED] +=
		unavailable_special_cargo_count;
	g_mission_fg_stats[flight_group_idx]
		.outcome_count[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED] +=
		unavailable_count;
	g_mission_fg_stats[flight_group_idx]
		.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED] +=
		unavailable_special_cargo_count;
	g_mission_fg_stats[flight_group_idx]
		.outcome_count[FLIGHT_GROUP_OUTCOME_NOT_BOARDED] +=
		unavailable_count;
	g_mission_fg_stats[flight_group_idx]
		.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_NOT_BOARDED] +=
		unavailable_special_cargo_count;
	g_mission_fg_stats[flight_group_idx].has_arrived = 1;
	g_mission_fg_stats[flight_group_idx].waves_remaining = 0;
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO(
			"mission.group_arrival_cancelled fg=%d count=%d special=%d arrived=%d",
			flight_group_idx, (int)unavailable_count,
			(int)unavailable_special_cargo_count, (int)result);
	}
	return result;
}

/* Shares out the credit for a destroyed craft. With no craft record or no
 * damage recorded, the source object's player, if any, gets a tier 3 kill
 * credit (and 4 worse-rating points when the victim's genus is 8), the
 * source's team a tier 3 team credit, and it returns. Otherwise each active
 * player's share of the damage sets a tier: 0xAAAA of 0x10000 (two thirds)
 * or more is 3, 0x5999 (about 35%) is 2, 0x0CCC (about 5%) is 1. A player
 * with a tier gets mission_credit_player_kill_contribution; against a team not
 * allied with theirs, also rating points, halved for tier 2 and a tenth for
 * tier 1: into rating_promo_points when the victim's rating is at most 4 below
 * theirs, else into worse_rating_promo_points (always there for a victim of
 * rating weight 0, or one with no player owner flying an idle, formation,
 * hangar, disabled or self-destruct plan); tiers 2 and 3 there also go to
 * mission_record_player_craft_loss_attribution. The victim counts as owned by its
 * group's player when player-owned craft did at least half its damage. Then
 * each team's share, from its flight groups, gets the same tiers and
 * mission_credit_team_kill_contribution; at tier 2 or 3, credit its players did
 * not take goes to the team's most damaging flight group through
 * mission_record_player_craft_loss_attribution and, for an owned victim, the
 * owner's per_mission_kills. Writes g_players[] mission_stats and
 * per_mission_kills, and each rating line into g_mission_debug_buffer
 * (g_flight_text_scratch_buffer in the modern build). */
// FUNCTION: XVT 0x434500
void mission_credit_destruction_damage_contributors(uint16_t source_obj_idx,
						    uint16_t victim_obj_idx)
{
	enum { PLAYER_COUNT = 8, TEAM_COUNT = 10 };

	int special_cargo = 0;
	int player_contribution_by_team[TEAM_COUNT];
	memset(player_contribution_by_team, 0,
	       sizeof(player_contribution_by_team));
	int victim_owner_idx = -1;
	struct craft_data *craft = NULL;
	struct object_record *victim = &g_object_table[victim_obj_idx];
	if (victim->mobj != NULL) {
		craft = victim->mobj->p_craft;
	}
	if (craft == NULL || craft->damage_stats.damage_received_total == 0) {
		XVT_LOG_DEBUG(
			"mission.kill_credit_direct object=%d source=%d slot=%d team=%d record=%d predicted=%d",
			(int)victim_obj_idx, (int)source_obj_idx,
			g_object_table[source_obj_idx].player_owner_idx,
			(int)g_mission_flight_groups
				[g_object_table[source_obj_idx]
					 .flight_group_idx]
					.fg.team,
			craft != NULL, g_flight_sim_side_effects_suppressed);
		if (g_object_table[source_obj_idx].player_owner_idx != -1) {
			mission_credit_player_kill_contribution(
				victim_obj_idx, 0, 3,
				g_object_table[source_obj_idx].player_owner_idx,
				-1, 0);
			if (g_object_table[victim_obj_idx].genus_id == 8) {
				struct player_data *owner_player =
					&g_players
						[g_object_table[source_obj_idx]
							 .player_owner_idx];
				owner_player->mission_stats
					.worse_rating_promo_points += 4;
				XVT_LOG_DEBUG(
					"mission.mine_rating_awarded slot=%d total=%d predicted=%d",
					g_object_table[source_obj_idx]
						.player_owner_idx,
					owner_player->mission_stats
						.worse_rating_promo_points,
					g_flight_sim_side_effects_suppressed);
			}
		}
		mission_credit_team_kill_contribution(
			victim_obj_idx, 0, 3,
			g_mission_flight_groups[g_object_table[source_obj_idx]
							.flight_group_idx]
				.fg.team);
		return;
	}

	int victim_flight_group_index =
		g_object_table[victim_obj_idx].flight_group_idx;
	if (g_mission_flight_groups[victim_flight_group_index]
		    .fg.special_cargo_craft == craft->craft_ordinal) {
		special_cargo = 1;
	}
	if ((uint16_t)math2_longratio_q16(
		    craft->damage_stats.damage_received_by_player_owned_craft,
		    craft->damage_stats.damage_received_total) >= 0x8000u) {
		memcpy(&victim_owner_idx,
		       &g_mission_flight_groups[victim_flight_group_index]
				.player_owner_idx,
		       sizeof(victim_owner_idx));
	}
	int victim_rating;
	if (victim_owner_idx != -1) {
		victim_rating = g_players[victim_owner_idx].pilot_rating;
	} else {
		victim_rating =
			g_mission_flight_groups[g_object_table[victim_obj_idx]
							.flight_group_idx]
				.fg.group_ai;
	}
	XVT_LOG_DEBUG(
		"mission.kill_damage object=%d source=%d fg=%d owner=%d rating=%d special=%d total=%d flown=%d predicted=%d",
		(int)victim_obj_idx, (int)source_obj_idx,
		victim_flight_group_index, victim_owner_idx, victim_rating,
		special_cargo, craft->damage_stats.damage_received_total,
		craft->damage_stats.damage_received_by_player_owned_craft,
		g_flight_sim_side_effects_suppressed);

	int award_rating;
	for (int player_index = 0; player_index < PLAYER_COUNT;
	     ++player_index) {
		if (g_players[player_index].participation_state == 0) {
			continue;
		}
		uint16_t damage_share_q16 = (uint16_t)math2_longratio_q16(
			craft->damage_stats.damage_from_player[player_index],
			craft->damage_stats.damage_received_total);
		int contribution_tier;
		if (damage_share_q16 >= 0xAAAAu) {
			contribution_tier = 3;
		} else if (damage_share_q16 >= 0x5999u) {
			contribution_tier = 2;
		} else if (damage_share_q16 >= 0x0CCCu) {
			contribution_tier = 1;
		} else {
			contribution_tier = 0;
		}
		if (contribution_tier != 0) {
			XVT_LOG_DEBUG(
				"mission.kill_share slot=%d object=%d share=%u tier=%d damage=%d predicted=%d",
				player_index, (int)victim_obj_idx,
				(unsigned)damage_share_q16, contribution_tier,
				craft->damage_stats
					.damage_from_player[player_index],
				g_flight_sim_side_effects_suppressed);
			mission_credit_player_kill_contribution(
				victim_obj_idx, special_cargo,
				contribution_tier, player_index,
				victim_owner_idx, victim_rating);
			{
				struct object_record *damaged_victim =
					&g_object_table[victim_obj_idx];
				if (g_mission_teams[(uint16_t)g_players
							    [player_index]
								    .team]
					    .allies[g_mission_flight_groups
							    [damaged_victim
								     ->flight_group_idx]
								    .fg.team] ==
				    0) {
					uint8_t plan_id =
						craft->ai_controller
							.current_plan_id;
					const char *plan_name =
						g_plan_table[plan_id].name;
					int minimum_rating_award =
						(strcmp(plan_name, "nullpln") ==
							 0 ||
						 strcmp(plan_name,
							"stationaryldrpln") ==
							 0 ||
						 strcmp(plan_name,
							"formldr1pln") == 0 ||
						 strcmp(plan_name,
							"formflw1pln") == 0 ||
						 strcmp(plan_name,
							"formevadeldr1pln") ==
							 0 ||
						 strcmp(plan_name,
							"formevadeflw1pln") ==
							 0 ||
						 strcmp(plan_name,
							"exithangarpln") == 0 ||
						 strcmp(plan_name,
							"enterhangarpln") ==
							 0 ||
						 strcmp(plan_name,
							"disabledpln") == 0 ||
						 strcmp(plan_name,
							"selfdestroypln") ==
							 0) &&
						victim_owner_idx == -1;

					int victim_rating_weight =
						g_model_defs
							[get_model_index_from_type(
								 damaged_victim
									 ->object_type)]
								.rating_weight;
					if (victim_rating_weight == 0) {
						minimum_rating_award = 1;
					}
					int attacker_rating_weight =
						g_model_defs
							[get_model_index_from_type(
								 g_craft_type_to_object_type
									 [g_mission_flight_groups
										  [g_players[player_index]
											   .bound_flight_group_idx]
											  .fg
											  .craft_type])]
								.rating_weight;
					if (victim_owner_idx == -1) {
						award_rating = g_default_pilot_rating_by_ai_level
							[g_mission_flight_groups
								 [victim_flight_group_index]
									 .fg
									 .group_ai];
					} else {
						award_rating =
							g_players[victim_owner_idx]
								.pilot_rating;
					}

					{
						int rating_factor =
							award_rating -
							g_players[player_index]
								.pilot_rating;
						int rating_points;
						if (minimum_rating_award == 0 &&
						    rating_factor >= -4) {
							rating_factor += 4;
							if (g_players[player_index]
								    .pilot_rating <
							    15) {
								if (rating_factor <
								    4) {
									rating_factor =
										4;
								}
							} else if (
								rating_factor <
								3) {
								rating_factor =
									3;
							}
							rating_points =
								rating_factor *
								award_rating *
								victim_rating_weight /
								attacker_rating_weight;
							if (contribution_tier ==
							    2) {
								rating_points /=
									2;
							} else if (
								contribution_tier ==
								1) {
								rating_points /=
									10;
							}
							g_players[player_index]
								.mission_stats
								.rating_promo_points +=
								rating_points;
							XVT_LOG_DEBUG(
								"mission.rating_awarded slot=%d kind=\"better\" points=%d total=%d award=%d factor=%d weight=%d attacker=%d minimum=%d predicted=%d",
								player_index,
								rating_points,
								g_players[player_index]
									.mission_stats
									.rating_promo_points,
								award_rating,
								rating_factor,
								victim_rating_weight,
								attacker_rating_weight,
								minimum_rating_award,
								g_flight_sim_side_effects_suppressed);
							sprintf(g_flight_text_scratch_buffer,
								"Rating points awarded: %d to player: %d Better total: %d\n",
								rating_points,
								player_index,
								g_players[player_index]
									.mission_stats
									.rating_promo_points);
						} else {
							if (minimum_rating_award !=
							    0) {
								award_rating =
									1;
								if (victim_rating_weight ==
								    0) {
									victim_rating_weight =
										1;
								}
							}
							if (award_rating == 0) {
								award_rating =
									1;
							}
							rating_points =
								award_rating *
								victim_rating_weight /
								attacker_rating_weight;
							if (contribution_tier ==
							    2) {
								rating_points /=
									2;
							} else if (
								contribution_tier ==
								1) {
								rating_points /=
									10;
							}
							if (rating_points ==
							    0) {
								rating_points =
									1;
							}
							g_players[player_index]
								.mission_stats
								.worse_rating_promo_points +=
								rating_points;
							XVT_LOG_DEBUG(
								"mission.rating_awarded slot=%d kind=\"worse\" points=%d total=%d award=%d factor=%d weight=%d attacker=%d minimum=%d predicted=%d",
								player_index,
								rating_points,
								g_players[player_index]
									.mission_stats
									.worse_rating_promo_points,
								award_rating,
								rating_factor,
								victim_rating_weight,
								attacker_rating_weight,
								minimum_rating_award,
								g_flight_sim_side_effects_suppressed);
							sprintf(g_flight_text_scratch_buffer,
								"Rating points awarded: %d to player: %d Worse total: %d\n",
								rating_points,
								player_index,
								g_players[player_index]
									.mission_stats
									.worse_rating_promo_points);
						}
					}

					if (contribution_tier == 2 ||
					    contribution_tier == 3) {
						int attribution_credit;
						if (contribution_tier == 2) {
							attribution_credit = 1;
						} else {
							attribution_credit = 2;
						}
						mission_record_player_craft_loss_attribution(
							g_players[player_index]
								.bound_flight_group_idx,
							victim_obj_idx,
							contribution_tier);
						player_contribution_by_team
							[(uint16_t)g_players
								 [player_index]
									 .team] +=
							attribution_credit;
						XVT_LOG_DEBUG(
							"mission.kill_attributed slot=%d team=%d credit=%d team_credit=%d predicted=%d",
							player_index,
							(int)g_players
								[player_index]
									.team,
							attribution_credit,
							player_contribution_by_team
								[(uint16_t)g_players
									 [player_index]
										 .team],
							g_flight_sim_side_effects_suppressed);
					}
				}
			}
		}
	}

	for (int team_index = 0; team_index < TEAM_COUNT; ++team_index) {
		int flight_group_index = 0;
		unsigned int team_damage = 0;
		unsigned int largest_damage = 0;
		int largest_flight_group = 0;
		if ((int16_t)g_mission_header.num_flight_groups > 0) {
			int flight_group_count =
				(int16_t)g_mission_header.num_flight_groups;
			int *damage_from_flight_group =
				&craft->damage_stats
					 .damage_from_flight_group_amount[0];
			struct mission_flight_group *flight_group =
				&g_mission_flight_groups[0];
			do {
				if (flight_group->fg.team == team_index) {
					unsigned int damage =
						*damage_from_flight_group;
					team_damage += damage;
					if (largest_damage < damage) {
						largest_damage = damage;
						largest_flight_group =
							flight_group_index;
					}
				}
				++flight_group_index;
				++flight_group;
				++damage_from_flight_group;
			} while (flight_group_index < flight_group_count);
		}
		{
			int contribution_tier;
			uint16_t damage_share_q16 =
				(uint16_t)math2_longratio_q16(
					team_damage,
					craft->damage_stats
						.damage_received_total);
			if (damage_share_q16 >= 0xAAAAu) {
				contribution_tier = 3;
			} else if (damage_share_q16 >= 0x5999u) {
				contribution_tier = 2;
			} else if (damage_share_q16 >= 0x0CCCu) {
				contribution_tier = 1;
			} else {
				contribution_tier = 0;
			}
			if (contribution_tier != 0) {
				XVT_LOG_DEBUG(
					"mission.team_kill_share team=%d object=%d share=%u tier=%d damage=%u fg=%d predicted=%d",
					team_index, (int)victim_obj_idx,
					(unsigned)damage_share_q16,
					contribution_tier, team_damage,
					largest_flight_group,
					g_flight_sim_side_effects_suppressed);
				mission_credit_team_kill_contribution(
					victim_obj_idx, special_cargo,
					contribution_tier, team_index);
			}
			if ((contribution_tier == 2 ||
			     contribution_tier == 3) &&
			    player_contribution_by_team[team_index] < 2) {
				contribution_tier -= 2;
				int remaining_credit =
					(contribution_tier != 0 ? 2 : 1) -
					player_contribution_by_team[team_index];
				if (remaining_credit == 2) {
					mission_record_player_craft_loss_attribution(
						largest_flight_group,
						victim_obj_idx, 3);
					if (victim_owner_idx != -1) {
						++g_players[victim_owner_idx]
							  .per_mission_kills
							  .kills_full_from_flight_group
								  [largest_flight_group];
					}
					XVT_LOG_DEBUG(
						"mission.kill_credit_group team=%d fg=%d object=%d share=\"full\" owner=%d kills=%u predicted=%d",
						team_index,
						largest_flight_group,
						(int)victim_obj_idx,
						victim_owner_idx,
						victim_owner_idx != -1
							? (unsigned)g_players[victim_owner_idx]
								  .per_mission_kills
								  .kills_full_from_flight_group
									  [largest_flight_group]
							: 0u,
						g_flight_sim_side_effects_suppressed);
				} else if (remaining_credit == 1) {
					mission_record_player_craft_loss_attribution(
						largest_flight_group,
						victim_obj_idx, 2);
					if (victim_owner_idx != -1) {
						++g_players[victim_owner_idx]
							  .per_mission_kills
							  .kills_shared_from_flight_group
								  [largest_flight_group];
					}
					XVT_LOG_DEBUG(
						"mission.kill_credit_group team=%d fg=%d object=%d share=\"shared\" owner=%d kills=%u predicted=%d",
						team_index,
						largest_flight_group,
						(int)victim_obj_idx,
						victim_owner_idx,
						victim_owner_idx != -1
							? (unsigned)g_players[victim_owner_idx]
								  .per_mission_kills
								  .kills_shared_from_flight_group
									  [largest_flight_group]
							: 0u,
						g_flight_sim_side_effects_suppressed);
				}
			}
		}
	}
}

/* Credits one player with a share in destroying a craft. Against a team not
 * allied with the player's, it counts the kill in the player's per_mission_kills
 * (an assist at tier 1, shared at 2, full at 3, by flight group and by the
 * victim's rating; at tiers 2 and 3 an owned victim also by owner, and in the
 * owner's own counts), and adds mission_compute_kill_score_for_object's score (a
 * tenth at tier 1, half at tier 2; doubled at difficulty 2, halved at 0) to
 * mission_score unless mission_apply_flight_group_goal_score returns below 0. A tier
 * 3 kill by the local player passes fsfx_speak_wingman_event a chance of 0 unless
 * a primary goal of the player's team is on the victim's group (24576 for its
 * conditions 0 and 10, 0xF000 for 2). Against an allied craft at tier 2 or 3 it
 * subtracts the score and 500 rating points, counts a friendly kill and sends
 * the friendly-fire and penalty messages, with a tactical voice for the local
 * player. Writes g_players[], g_msg_sender_iff and g_msg_arg_table. */
// FUNCTION: XVT 0x434BC0
void mission_credit_player_kill_contribution(
	uint16_t victim_obj_idx, int special_cargo_flag, int contribution_tier,
	int player_idx, int victim_owner_idx, int victim_rating)
{
	uint16_t flight_group_idx =
		g_object_table[victim_obj_idx].flight_group_idx;
	uint16_t tactical_voice_probability = 0;
	int goal_score_reduction_level = 1;
	for (int goal_index = 0; goal_index < 8; goal_index++) {
		if (g_mission_flight_groups[flight_group_idx]
				    .fg.goals[goal_index]
				    .goal_kind == 0 &&
		    g_mission_flight_groups[flight_group_idx]
				    .fg.goals[goal_index]
				    .enabled_teams[(uint16_t)
							   g_players[player_idx]
								   .team] ==
			    1) {
			switch (g_mission_flight_groups[flight_group_idx]
					.fg.goals[goal_index]
					.event_condition) {
			case 0:
			case 10:
				tactical_voice_probability = 24576;
				break;
			case 2:
				tactical_voice_probability = (uint16_t)-4096;
				break;
			}
		}
	}

	if (g_mission_teams[(uint16_t)g_players[player_idx].team].allies
		    [g_mission_flight_groups[flight_group_idx].fg.team] == 0) {
		int score =
			mission_compute_kill_score_for_object(victim_obj_idx);
		switch (contribution_tier) {
		case 1:
			g_players[player_idx]
				.per_mission_kills.kills_assist_on_flight_group
					[flight_group_idx]++;
			if (victim_owner_idx != -1) {
				g_players[player_idx]
					.per_mission_kills
					.kills_assist_on_player_rating
						[victim_rating]++;
			} else {
				g_players[player_idx]
					.per_mission_kills
					.kills_assist_on_ai_rating
						[victim_rating]++;
			}
			goal_score_reduction_level = 10;
			score /= 10;
			XVT_LOG_DEBUG(
				"mission.kill_counted slot=%d object=%d fg=%d owner=%d rating=%d share=\"assist\" kills=%u predicted=%d",
				player_idx, (int)victim_obj_idx,
				(int)flight_group_idx, victim_owner_idx,
				victim_rating,
				(unsigned)g_players[player_idx]
					.per_mission_kills
					.kills_assist_on_flight_group
						[flight_group_idx],
				g_flight_sim_side_effects_suppressed);
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_INFO(
					"battle.kill by=%d victim=%d owner=%d fg=%d craft=%d share=\"assist\" tick=%d",
					player_idx, (int)victim_obj_idx,
					victim_owner_idx, (int)flight_group_idx,
					(int)g_mission_flight_groups
						[flight_group_idx]
							.fg.craft_type,
					g_game_time);
			}
			break;
		case 2:
			g_players[player_idx]
				.per_mission_kills.kills_shared_on_flight_group
					[flight_group_idx]++;
			if (victim_owner_idx != -1) {
				g_players[player_idx]
					.per_mission_kills
					.kills_shared_on_player
						[victim_owner_idx]++;
				g_players[player_idx]
					.per_mission_kills
					.kills_shared_on_player_rating
						[victim_rating]++;
				g_players[victim_owner_idx]
					.per_mission_kills
					.kills_shared_from_player[player_idx]++;
			} else {
				g_players[player_idx]
					.per_mission_kills
					.kills_shared_on_ai_rating
						[victim_rating]++;
			}
			goal_score_reduction_level = 6;
			score /= 2;
			XVT_LOG_DEBUG(
				"mission.kill_counted slot=%d object=%d fg=%d owner=%d rating=%d share=\"shared\" kills=%u predicted=%d",
				player_idx, (int)victim_obj_idx,
				(int)flight_group_idx, victim_owner_idx,
				victim_rating,
				(unsigned)g_players[player_idx]
					.per_mission_kills
					.kills_shared_on_flight_group
						[flight_group_idx],
				g_flight_sim_side_effects_suppressed);
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_INFO(
					"battle.kill by=%d victim=%d owner=%d fg=%d craft=%d share=\"shared\" tick=%d",
					player_idx, (int)victim_obj_idx,
					victim_owner_idx, (int)flight_group_idx,
					(int)g_mission_flight_groups
						[flight_group_idx]
							.fg.craft_type,
					g_game_time);
			}
			break;
		case 3:
			g_players[player_idx]
				.per_mission_kills
				.kills_full_on_flight_group[flight_group_idx]++;
			if (victim_owner_idx != -1) {
				g_players[player_idx]
					.per_mission_kills.kills_full_on_player
						[victim_owner_idx]++;
				g_players[player_idx]
					.per_mission_kills
					.kills_full_on_player_rating
						[victim_rating]++;
				g_players[victim_owner_idx]
					.per_mission_kills
					.kills_full_from_player[player_idx]++;
			} else {
				g_players[player_idx]
					.per_mission_kills
					.kills_full_on_ai_rating
						[victim_rating]++;
			}
			goal_score_reduction_level = 1;
			XVT_LOG_DEBUG(
				"mission.kill_counted slot=%d object=%d fg=%d owner=%d rating=%d share=\"full\" kills=%u predicted=%d",
				player_idx, (int)victim_obj_idx,
				(int)flight_group_idx, victim_owner_idx,
				victim_rating,
				(unsigned)g_players[player_idx]
					.per_mission_kills
					.kills_full_on_flight_group
						[flight_group_idx],
				g_flight_sim_side_effects_suppressed);
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_INFO(
					"battle.kill by=%d victim=%d owner=%d fg=%d craft=%d share=\"full\" tick=%d",
					player_idx, (int)victim_obj_idx,
					victim_owner_idx, (int)flight_group_idx,
					(int)g_mission_flight_groups
						[flight_group_idx]
							.fg.craft_type,
					g_game_time);
			}
			break;
		default:
			break;
		}
		if (g_flight_mission_state.difficulty == 2) {
			score *= 2;
		} else if (g_flight_mission_state.difficulty == 0) {
			score /= 2;
		}
		if (mission_apply_flight_group_goal_score(
			    2, flight_group_idx, player_idx,
			    (uint16_t)goal_score_reduction_level,
			    special_cargo_flag,
			    (uint16_t)g_players[player_idx].team) >= 0) {
			g_players[player_idx].mission_stats.mission_score +=
				score;
			XVT_LOG_DEBUG(
				"mission.kill_scored slot=%d object=%d tier=%d score=%d total=%d difficulty=%d predicted=%d",
				player_idx, (int)victim_obj_idx,
				contribution_tier, score,
				g_players[player_idx]
					.mission_stats.mission_score,
				(int)g_flight_mission_state.difficulty,
				g_flight_sim_side_effects_suppressed);
		} else {
			XVT_LOG_DEBUG(
				"mission.kill_score_withheld slot=%d object=%d tier=%d score=%d predicted=%d",
				player_idx, (int)victim_obj_idx,
				contribution_tier, score,
				g_flight_sim_side_effects_suppressed);
		}
		if (contribution_tier == 3 && g_local_player == player_idx) {
			fsfx_speak_wingman_event(g_local_player, -1, 21, -1, -1,
						 tactical_voice_probability);
		}
	} else if (contribution_tier == 2 || contribution_tier == 3) {
		int score =
			mission_compute_kill_score_for_object(victim_obj_idx);
		g_players[player_idx].mission_stats.mission_score -= score;
		g_players[player_idx].per_mission_kills.friendlies_killed++;
		g_players[player_idx].mission_stats.rating_promo_points -= 500;
		XVT_LOG_DEBUG(
			"mission.friendly_kill_penalty slot=%d object=%d tier=%d score=%d total=%d promotion=%d friendlies=%u predicted=%d",
			player_idx, (int)victim_obj_idx, contribution_tier,
			score,
			g_players[player_idx].mission_stats.mission_score,
			g_players[player_idx].mission_stats.rating_promo_points,
			(unsigned)g_players[player_idx]
				.per_mission_kills.friendlies_killed,
			g_flight_sim_side_effects_suppressed);
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_INFO(
				"battle.friendly_kill by=%d victim=%d owner=%d fg=%d craft=%d share=\"%s\" penalty=%d tick=%d",
				player_idx, (int)victim_obj_idx,
				victim_owner_idx, (int)flight_group_idx,
				(int)g_mission_flight_groups[flight_group_idx]
					.fg.craft_type,
				contribution_tier == 3 ? "full" : "shared",
				score, g_game_time);
		}
		g_msg_sender_iff = g_players[player_idx].iff;
		msg_emit_in_flight_message(
			IFMSG_281_YOU_HAVE_DESTROYED_A_CRAFT_ON_YOUR_OWN_SIDE,
			player_idx);
		if (score != 0) {
			g_msg_arg_table[0] = (uint16_t)score;
			msg_emit_in_flight_message(
				IFMSG_200_PENALTY_POINTS_DEDUCTED_ARG,
				player_idx);
		}
		if (g_local_player == player_idx) {
			fsfx_speak_tactical_officer_event(
				TACTICAL_VOICE_ORDER,
				TACTICAL_MSG_FRIENDLY_CRAFT_DESTROYED,
				victim_obj_idx, UINT16_MAX);
		}
	}
}

/* Credits a team with a share in destroying a craft. Against a team not
 * allied with it, it counts a full (tier 3), shared (2) or assist (1) kill in
 * runtime.team_kill_stats rows 0, 1 and 2 and adds
 * mission_compute_kill_score_for_object's score (a tenth at tier 1, half at 2;
 * doubled at difficulty 2, halved at 0) to the team's mission score unless
 * mission_apply_flight_group_goal_score returns below 0. Against an allied craft
 * at tier 2 or 3 it subtracts the full score. Writes
 * g_flight_mission_state.runtime. */
// FUNCTION: XVT 0x434F20
void mission_credit_team_kill_contribution(uint16_t victim_obj_idx,
					   int special_cargo_flag,
					   int contribution_tier, int team_idx)
{
	int goal_score_reduction_level = 1;
	uint16_t flight_group_idx =
		g_object_table[victim_obj_idx].flight_group_idx;
	if (g_mission_teams[team_idx].allies
		    [g_mission_flight_groups[flight_group_idx].fg.team] == 0) {
		int score =
			mission_compute_kill_score_for_object(victim_obj_idx);
		switch (contribution_tier) {
		case 1:
			++g_flight_mission_state.runtime
				  .team_kill_stats[2][team_idx];
			goal_score_reduction_level = 10;
			score /= 10;
			XVT_LOG_DEBUG(
				"mission.team_kill_counted team=%d object=%d fg=%d share=\"assist\" kills=%u predicted=%d",
				team_idx, (int)victim_obj_idx,
				(int)flight_group_idx,
				(unsigned)g_flight_mission_state.runtime
					.team_kill_stats[2][team_idx],
				g_flight_sim_side_effects_suppressed);
			break;
		case 2:
			++g_flight_mission_state.runtime
				  .team_kill_stats[1][team_idx];
			goal_score_reduction_level = 6;
			score /= 2;
			XVT_LOG_DEBUG(
				"mission.team_kill_counted team=%d object=%d fg=%d share=\"shared\" kills=%u predicted=%d",
				team_idx, (int)victim_obj_idx,
				(int)flight_group_idx,
				(unsigned)g_flight_mission_state.runtime
					.team_kill_stats[1][team_idx],
				g_flight_sim_side_effects_suppressed);
			break;
		case 3:
			++g_flight_mission_state.runtime
				  .team_kill_stats[0][team_idx];
			goal_score_reduction_level = 1;
			XVT_LOG_DEBUG(
				"mission.team_kill_counted team=%d object=%d fg=%d share=\"full\" kills=%u predicted=%d",
				team_idx, (int)victim_obj_idx,
				(int)flight_group_idx,
				(unsigned)g_flight_mission_state.runtime
					.team_kill_stats[0][team_idx],
				g_flight_sim_side_effects_suppressed);
			break;
		}
		if (g_flight_mission_state.difficulty == 2) {
			score *= 2;
		} else if (g_flight_mission_state.difficulty == 0) {
			score /= 2;
		}
		if (mission_apply_flight_group_goal_score(
			    2, flight_group_idx, -1, goal_score_reduction_level,
			    special_cargo_flag, team_idx) >= 0) {
			g_flight_mission_state.runtime
				.team_scores[TEAM_SCORE_MISSION][team_idx] +=
				score;
			XVT_LOG_DEBUG(
				"mission.team_kill_scored team=%d object=%d tier=%d score=%d total=%d predicted=%d",
				team_idx, (int)victim_obj_idx,
				contribution_tier, score,
				g_flight_mission_state.runtime
					.team_scores[TEAM_SCORE_MISSION]
						    [team_idx],
				g_flight_sim_side_effects_suppressed);
		} else {
			XVT_LOG_DEBUG(
				"mission.team_kill_score_withheld team=%d object=%d tier=%d score=%d predicted=%d",
				team_idx, (int)victim_obj_idx,
				contribution_tier, score,
				g_flight_sim_side_effects_suppressed);
		}
	} else if (contribution_tier == 2 || contribution_tier == 3) {
		g_flight_mission_state.runtime
			.team_scores[TEAM_SCORE_MISSION][team_idx] -=
			mission_compute_kill_score_for_object(victim_obj_idx);
		XVT_LOG_DEBUG(
			"mission.team_friendly_kill team=%d object=%d tier=%d total=%d predicted=%d",
			team_idx, (int)victim_obj_idx, contribution_tier,
			g_flight_mission_state.runtime
				.team_scores[TEAM_SCORE_MISSION][team_idx],
			g_flight_sim_side_effects_suppressed);
	}
}

/* Counts a projectile's hit for the craft that fired it; does nothing when the
 * firing object lies past the active craft slots or is empty. A laser or
 * turbo laser hit adds to the firing craft's weapon_stats.laser_hits_scored, an
 * ion hit to ion_hits_scored, a warhead hit to warhead_hits_scored. When the
 * projectile sits below g_projectile_object_slot_start + 128 and the firing
 * craft's flight group has a player owner, the player's mission_stats count
 * the laser or ion hit, or, for a warhead, per_mission_kills.warhead_hits and
 * the warhead's g_projectile_type_data.warhead_point_value in mission_score. Every
 * warhead hit adds that value to the firing group's team mission score in
 * g_flight_mission_state.runtime. Other types count nothing. */
// FUNCTION: XVT 0x435070
void mission_record_projectile_hit_stats(uint16_t projectile_obj_idx)
{
	int owner_obj_idx =
		g_object_table[projectile_obj_idx].mobj->source_obj_idx;
	if (g_active_region_craft_object_slot_end <= owner_obj_idx) {
		XVT_LOG_DEBUG(
			"mission.hit_uncounted projectile=%d source=%d type=%d reason=\"not_craft\" predicted=%d",
			(int)projectile_obj_idx, owner_obj_idx,
			(int)g_object_table[projectile_obj_idx].object_type,
			g_flight_sim_side_effects_suppressed);
		return;
	}

	if (g_object_table[owner_obj_idx].object_type == 0) {
		XVT_LOG_DEBUG(
			"mission.hit_uncounted projectile=%d source=%d type=%d reason=\"gone\" predicted=%d",
			(int)projectile_obj_idx, owner_obj_idx,
			(int)g_object_table[projectile_obj_idx].object_type,
			g_flight_sim_side_effects_suppressed);
		return;
	}

	uint16_t player_projectile_slot_end =
		(uint16_t)(g_projectile_object_slot_start + 128);
	int owner_player_idx =
		g_mission_flight_groups[g_object_table[owner_obj_idx]
						.flight_group_idx]
			.player_owner_idx;
	struct craft_data *owner_craft =
		g_object_table[owner_obj_idx].mobj->p_craft;

	switch (g_object_table[projectile_obj_idx].object_type) {
	case PROJECTILE_OBJECT_TYPE_REBEL_LASER:
	case PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER:
	case PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER:
	case PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER:
		++owner_craft->weapon_stats.laser_hits_scored;
		if (player_projectile_slot_end > projectile_obj_idx &&
		    owner_player_idx != -1) {
			++g_players[owner_player_idx]
				  .mission_stats.laser_hits_scored;
		}
		XVT_LOG_DEBUG(
			"mission.hit_counted projectile=%d source=%d type=%d kind=\"laser\" slot=%d hits=%u player_hits=%u predicted=%d",
			(int)projectile_obj_idx, owner_obj_idx,
			(int)g_object_table[projectile_obj_idx].object_type,
			owner_player_idx,
			(unsigned)owner_craft->weapon_stats.laser_hits_scored,
			(player_projectile_slot_end > projectile_obj_idx &&
			 owner_player_idx != -1)
				? (unsigned)g_players[owner_player_idx]
					  .mission_stats.laser_hits_scored
				: 0u,
			g_flight_sim_side_effects_suppressed);
		break;

	case PROJECTILE_OBJECT_TYPE_ION_LASER:
	case PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER:
		++owner_craft->weapon_stats.ion_hits_scored;
		if (player_projectile_slot_end > projectile_obj_idx &&
		    owner_player_idx != -1) {
			++g_players[owner_player_idx]
				  .mission_stats.ion_hits_scored;
		}
		XVT_LOG_DEBUG(
			"mission.hit_counted projectile=%d source=%d type=%d kind=\"ion\" slot=%d hits=%u player_hits=%u predicted=%d",
			(int)projectile_obj_idx, owner_obj_idx,
			(int)g_object_table[projectile_obj_idx].object_type,
			owner_player_idx,
			(unsigned)owner_craft->weapon_stats.ion_hits_scored,
			(player_projectile_slot_end > projectile_obj_idx &&
			 owner_player_idx != -1)
				? (unsigned)g_players[owner_player_idx]
					  .mission_stats.ion_hits_scored
				: 0u,
			g_flight_sim_side_effects_suppressed);
		break;

	case WARHEAD_OBJECT_TYPE_PROTON_TORPEDO:
	case WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE:
	case WARHEAD_OBJECT_TYPE_ADVANCED_PROTON_TORPEDO:
	case WARHEAD_OBJECT_TYPE_ADVANCED_CONCUSSION_MISSILE:
	case WARHEAD_OBJECT_TYPE_SPACE_BOMB:
	case WARHEAD_OBJECT_TYPE_HEAVY_ROCKET:
	case WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE:
	case WARHEAD_OBJECT_TYPE_ION_PULSE:
	case WARHEAD_OBJECT_TYPE_LASER_3: {
		++owner_craft->weapon_stats.warhead_hits_scored;
		int flight_group_idx =
			g_object_table[owner_obj_idx].flight_group_idx;
		if (player_projectile_slot_end > projectile_obj_idx &&
		    owner_player_idx != -1) {
			++g_players[owner_player_idx]
				  .per_mission_kills.warhead_hits;
			g_players[owner_player_idx]
				.mission_stats.mission_score +=
				g_projectile_type_data.warhead_point_value
					[g_object_table[projectile_obj_idx]
						 .object_type -
					 PROJECTILE_OBJECT_TYPE_FIRST];
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_INFO(
					"battle.warhead_hit by=%d warhead=%d own=%d score=%u tick=%d",
					owner_player_idx,
					(int)g_object_table[projectile_obj_idx]
						.object_type,
					g_players[owner_player_idx]
							.object_index ==
						owner_obj_idx,
					(unsigned)g_projectile_type_data.warhead_point_value
						[g_object_table
							 [projectile_obj_idx]
								 .object_type -
						 PROJECTILE_OBJECT_TYPE_FIRST],
					g_game_time);
			}
		}
		g_flight_mission_state.runtime.team_scores
			[TEAM_SCORE_MISSION]
			[g_mission_flight_groups[flight_group_idx].fg.team] +=
			g_projectile_type_data.warhead_point_value
				[g_object_table[projectile_obj_idx]
					 .object_type -
				 PROJECTILE_OBJECT_TYPE_FIRST];
		XVT_LOG_DEBUG(
			"mission.warhead_hit_counted projectile=%d source=%d type=%d slot=%d hits=%u player_hits=%u value=%u team=%d team_score=%d predicted=%d",
			(int)projectile_obj_idx, owner_obj_idx,
			(int)g_object_table[projectile_obj_idx].object_type,
			owner_player_idx,
			(unsigned)owner_craft->weapon_stats.warhead_hits_scored,
			(player_projectile_slot_end > projectile_obj_idx &&
			 owner_player_idx != -1)
				? (unsigned)g_players[owner_player_idx]
					  .per_mission_kills.warhead_hits
				: 0u,
			(unsigned)g_projectile_type_data.warhead_point_value
				[g_object_table[projectile_obj_idx]
					 .object_type -
				 PROJECTILE_OBJECT_TYPE_FIRST],
			(int)g_mission_flight_groups[flight_group_idx].fg.team,
			g_flight_mission_state.runtime.team_scores
				[TEAM_SCORE_MISSION]
				[g_mission_flight_groups[flight_group_idx]
					 .fg.team],
			g_flight_sim_side_effects_suppressed);
		break;
	}

	default:
		XVT_LOG_DEBUG(
			"mission.hit_uncounted projectile=%d source=%d type=%d reason=\"type\" predicted=%d",
			(int)projectile_obj_idx, owner_obj_idx,
			(int)g_object_table[projectile_obj_idx].object_type,
			g_flight_sim_side_effects_suppressed);
		break;
	}
}

/* Records the loss of a craft for its team and its player. Takes half of
 * mission_compute_craft_point_value from the group's team mission score and counts
 * a loss in runtime.team_kill_stats row 3; when the object has no player owner
 * and its flight group has none that is active, it returns that half value.
 * Otherwise it takes the same points from the owner's mission_score. With
 * allow_pending_damage_credit nonzero, when the damage players and AI dealt is
 * more than half the hull and shields it had left, it first shares out the kill
 * with mission_credit_destruction_damage_contributors. When at least half its
 * damage is in damage_received_by_player_owned_craft, it counts the loss in the
 * owner's per_mission_kills.total_craft_losses, and once more under collisions,
 * starships or mines when that source did the most damage. Returns, when
 * players did the most damage, the player who did the most; else -1. */
// FUNCTION: XVT 0x435250
int mission_record_player_craft_loss(unsigned int obj_idx,
				     int allow_pending_damage_credit)
{
	enum {
		PLAYER_COUNT = 8,
		AI_SKILL_COUNT = 6,
		PLAYER_DAMAGE_THRESHOLD = 0x8000
	};

	int credited_player_idx = -1;
	int point_penalty = mission_compute_craft_point_value(obj_idx) / 2;
	struct object_record *object = &g_object_table[obj_idx];
	int team_idx =
		g_mission_flight_groups[object->flight_group_idx].fg.team;
	g_flight_mission_state.runtime
		.team_scores[TEAM_SCORE_MISSION][team_idx] -= point_penalty;
	++g_flight_mission_state.runtime.team_kill_stats
		  [3]
		  [g_mission_flight_groups[object->flight_group_idx].fg.team];
	XVT_LOG_DEBUG(
		"mission.craft_lost object=%d fg=%d team=%d penalty=%d team_score=%d losses=%u predicted=%d",
		(int)obj_idx, (int)object->flight_group_idx, team_idx,
		point_penalty,
		g_flight_mission_state.runtime
			.team_scores[TEAM_SCORE_MISSION][team_idx],
		(unsigned)g_flight_mission_state.runtime
			.team_kill_stats[3][team_idx],
		g_flight_sim_side_effects_suppressed);
	int owner_player_idx = object->player_owner_idx;
	if (owner_player_idx == -1) {
		owner_player_idx =
			g_mission_flight_groups[object->flight_group_idx]
				.player_owner_idx;
		if (owner_player_idx == -1 ||
		    g_players[owner_player_idx].participation_state == 0) {
			return point_penalty;
		}
	}

	struct craft_data *craft = object->mobj->p_craft;
	g_players[owner_player_idx].mission_stats.mission_score -=
		point_penalty;
	XVT_LOG_DEBUG(
		"mission.player_craft_lost slot=%d object=%d own=%d penalty=%d score=%d predicted=%d",
		owner_player_idx, (int)obj_idx, object->player_owner_idx != -1,
		point_penalty,
		g_players[owner_player_idx].mission_stats.mission_score,
		g_flight_sim_side_effects_suppressed);
	unsigned int player_idx;
	int ai_skill_idx;
	if (allow_pending_damage_credit != 0) {
		uint32_t remaining_durability;

		if ((uint32_t)craft->hull_damage < (uint32_t)craft->hull_max) {
			remaining_durability = (uint32_t)(craft->hull_max -
							  craft->hull_damage);
		} else {
			remaining_durability = 0;
		}
		if (craft->shield_energy[0] > 0) {
			remaining_durability +=
				(uint16_t)craft->shield_energy[0];
		}
		if (craft->shield_energy[1] > 0) {
			remaining_durability +=
				(uint16_t)craft->shield_energy[1];
		}
		remaining_durability >>= 1;
		uint32_t pending_damage = 0;
		for (player_idx = 0; player_idx < PLAYER_COUNT; ++player_idx) {
			pending_damage +=
				(uint32_t)craft->damage_stats
					.damage_from_player[player_idx];
		}
		for (ai_skill_idx = 0; ai_skill_idx < AI_SKILL_COUNT;
		     ++ai_skill_idx) {
			pending_damage +=
				(uint32_t)craft->damage_stats
					.damage_from_ai_skill[ai_skill_idx];
		}
		if (pending_damage > remaining_durability) {
			mission_credit_destruction_damage_contributors(obj_idx,
								       obj_idx);
		}
		XVT_LOG_DEBUG(
			"mission.pending_damage object=%d pending=%u remaining=%u credited=%d predicted=%d",
			(int)obj_idx, (unsigned)pending_damage,
			(unsigned)remaining_durability,
			pending_damage > remaining_durability,
			g_flight_sim_side_effects_suppressed);
	}

	if ((uint16_t)math2_longratio_q16(
		    (uint32_t)craft->damage_stats
			    .damage_received_by_player_owned_craft,
		    (uint32_t)craft->damage_stats.damage_received_total) >=
	    PLAYER_DAMAGE_THRESHOLD) {
		++g_players[owner_player_idx]
			  .per_mission_kills.total_craft_losses;
		uint32_t player_damage = 0;
		for (player_idx = 0; player_idx < PLAYER_COUNT; ++player_idx) {
			player_damage +=
				(uint32_t)craft->damage_stats
					.damage_from_player[player_idx];
		}
		uint32_t ai_damage = 0;
		for (ai_skill_idx = 0; ai_skill_idx < AI_SKILL_COUNT;
		     ++ai_skill_idx) {
			ai_damage +=
				(uint32_t)craft->damage_stats
					.damage_from_ai_skill[ai_skill_idx];
		}
		uint32_t collision_damage =
			(uint32_t)craft->damage_stats.damage_from_collision;
		uint32_t starship_damage =
			(uint32_t)craft->damage_stats.damage_from_starship;
		uint32_t mine_damage =
			(uint32_t)craft->damage_stats.damage_from_mine;
		uint32_t total_classified_damage =
			player_damage | ai_damage | collision_damage |
			starship_damage | mine_damage;
		if (total_classified_damage != 0) {
			if (collision_damage >= mine_damage &&
			    collision_damage >= starship_damage &&
			    collision_damage >= player_damage &&
			    collision_damage >= ai_damage) {
				++g_players[owner_player_idx]
					  .per_mission_kills
					  .losses_by_collisions;
				if (g_flight_sim_side_effects_suppressed == 0) {
					XVT_LOG_INFO(
						"battle.loss who=%d cause=\"collision\" by=%d eject=%d tick=%d",
						owner_player_idx,
						credited_player_idx,
						allow_pending_damage_credit !=
							0,
						g_game_time);
				}
			} else if (starship_damage >= mine_damage &&
				   starship_damage >= collision_damage &&
				   starship_damage >= player_damage &&
				   starship_damage >= ai_damage) {
				++g_players[owner_player_idx]
					  .per_mission_kills
					  .losses_by_starships;
				if (g_flight_sim_side_effects_suppressed == 0) {
					XVT_LOG_INFO(
						"battle.loss who=%d cause=\"starship\" by=%d eject=%d tick=%d",
						owner_player_idx,
						credited_player_idx,
						allow_pending_damage_credit !=
							0,
						g_game_time);
				}
			} else if (mine_damage >= starship_damage &&
				   mine_damage >= collision_damage &&
				   mine_damage >= player_damage &&
				   mine_damage >= ai_damage) {
				++g_players[owner_player_idx]
					  .per_mission_kills.losses_by_mines;
				if (g_flight_sim_side_effects_suppressed == 0) {
					XVT_LOG_INFO(
						"battle.loss who=%d cause=\"mine\" by=%d eject=%d tick=%d",
						owner_player_idx,
						credited_player_idx,
						allow_pending_damage_credit !=
							0,
						g_game_time);
				}
			} else if (player_damage >= starship_damage &&
				   player_damage >= collision_damage &&
				   player_damage >= mine_damage &&
				   player_damage >= ai_damage) {
				uint32_t max_player_damage = 0;
				int top_player_rating = -1;
				int top_player_idx;
				for (player_idx = 0; player_idx < PLAYER_COUNT;
				     ++player_idx) {
					if ((uint32_t)craft->damage_stats
						    .damage_from_player
							    [player_idx] >
					    max_player_damage) {
						max_player_damage =
							(uint32_t)craft
								->damage_stats
								.damage_from_player
									[player_idx];
						top_player_rating =
							g_players[player_idx]
								.pilot_rating;
						top_player_idx = player_idx;
					}
				}
				if (top_player_rating != -1) {
					credited_player_idx = top_player_idx;
				}
				if (g_flight_sim_side_effects_suppressed == 0) {
					XVT_LOG_INFO(
						"battle.loss who=%d cause=\"player\" by=%d eject=%d tick=%d",
						owner_player_idx,
						credited_player_idx,
						allow_pending_damage_credit !=
							0,
						g_game_time);
				}
			} else {
				uint32_t max_ai_damage = 0;
				for (ai_skill_idx = 0;
				     ai_skill_idx < AI_SKILL_COUNT;
				     ++ai_skill_idx) {
					if ((uint32_t)craft->damage_stats
						    .damage_from_ai_skill
							    [ai_skill_idx] >
					    max_ai_damage) {
						max_ai_damage =
							(uint32_t)craft
								->damage_stats
								.damage_from_ai_skill
									[ai_skill_idx];
					}
				}
				if (g_flight_sim_side_effects_suppressed == 0) {
					XVT_LOG_INFO(
						"battle.loss who=%d cause=\"computer\" by=%d eject=%d tick=%d",
						owner_player_idx,
						credited_player_idx,
						allow_pending_damage_credit !=
							0,
						g_game_time);
				}
			}
		} else if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_INFO(
				"battle.loss who=%d cause=\"unknown\" by=%d eject=%d tick=%d",
				owner_player_idx, credited_player_idx,
				allow_pending_damage_credit != 0, g_game_time);
		}
		XVT_LOG_DEBUG(
			"mission.loss_classified slot=%d object=%d losses=%u from_player=%u from_ai=%u from_collision=%u from_starship=%u from_mine=%u collisions=%u starships=%u mines=%u by=%d predicted=%d",
			owner_player_idx, (int)obj_idx,
			(unsigned)g_players[owner_player_idx]
				.per_mission_kills.total_craft_losses,
			(unsigned)player_damage, (unsigned)ai_damage,
			(unsigned)collision_damage, (unsigned)starship_damage,
			(unsigned)mine_damage,
			(unsigned)g_players[owner_player_idx]
				.per_mission_kills.losses_by_collisions,
			(unsigned)g_players[owner_player_idx]
				.per_mission_kills.losses_by_starships,
			(unsigned)g_players[owner_player_idx]
				.per_mission_kills.losses_by_mines,
			credited_player_idx,
			g_flight_sim_side_effects_suppressed);
	} else {
		XVT_LOG_DEBUG(
			"mission.loss_not_counted slot=%d object=%d flown=%d total=%d predicted=%d",
			owner_player_idx, (int)obj_idx,
			craft->damage_stats
				.damage_received_by_player_owned_craft,
			craft->damage_stats.damage_received_total,
			g_flight_sim_side_effects_suppressed);
	}
	return credited_player_idx;
}

/* Counts a full kill of a player's craft by the attacker group's player
 * rating (killed_by_player_rating) or, for a group with no player owner, by its
 * group_ai (killed_by_ai_rating), in the victim owner's per_mission_kills. Counts
 * only at contribution_tier 3, and only when the victim has a player owner
 * (its own or its group's) and at least half its damage is in
 * damage_received_by_player_owned_craft; tier 2 passes those tests and counts
 * nothing, other tiers return at once. */
// FUNCTION: XVT 0x435530
void mission_record_player_craft_loss_attribution(int attacker_flight_group_idx,
						  int victim_obj_idx,
						  int contribution_tier)
{
	if (contribution_tier != 2 && contribution_tier != 3) {
		return;
	}

	struct object_record *victim = &g_object_table[victim_obj_idx];
	int victim_player_idx = victim->player_owner_idx;
	if (victim_player_idx == -1) {
		victim_player_idx =
			g_mission_flight_groups[victim->flight_group_idx]
				.player_owner_idx;
		if (victim_player_idx == -1) {
			return;
		}
	}

	unsigned int damage_share_q16 = math2_longratio_q16(
		(unsigned int)victim->mobj->p_craft->damage_stats
			.damage_received_by_player_owned_craft,
		(unsigned int)victim->mobj->p_craft->damage_stats
			.damage_received_total);
	if ((uint16_t)damage_share_q16 < 0x8000u) {
		return;
	}

	int attacker_player_idx =
		g_mission_flight_groups[attacker_flight_group_idx]
			.player_owner_idx;
	if (attacker_player_idx != -1) {
		unsigned int pilot_rating =
			g_players[attacker_player_idx].pilot_rating;
		if (contribution_tier == 3) {
			++g_players[victim_player_idx]
				  .per_mission_kills
				  .killed_by_player_rating[pilot_rating];
			XVT_LOG_DEBUG(
				"mission.loss_attributed slot=%d fg=%d by=%d kind=\"player\" rating=%u count=%u predicted=%d",
				victim_player_idx, attacker_flight_group_idx,
				attacker_player_idx, pilot_rating,
				(unsigned)g_players[victim_player_idx]
					.per_mission_kills
					.killed_by_player_rating[pilot_rating],
				g_flight_sim_side_effects_suppressed);
		}
		return;
	}

	{
		unsigned int group_ai =
			g_mission_flight_groups[attacker_flight_group_idx]
				.fg.group_ai;
		if (contribution_tier == 3) {
			++g_players[victim_player_idx]
				  .per_mission_kills
				  .killed_by_ai_rating[group_ai];
			XVT_LOG_DEBUG(
				"mission.loss_attributed slot=%d fg=%d by=%d kind=\"computer\" rating=%u count=%u predicted=%d",
				victim_player_idx, attacker_flight_group_idx,
				-1, group_ai,
				(unsigned)g_players[victim_player_idx]
					.per_mission_kills
					.killed_by_ai_rating[group_ai],
				g_flight_sim_side_effects_suppressed);
		}
	}
}

/* Pays out a flight group's per-craft bonus goals for one event. Each of its
 * 8 goals that is a bonus goal (goal_kind 2) for event_condition, enabled for
 * team_idx, with amount 18, or amount 19 when special_cargo_flag is 1, and whose
 * time limit (time_limit5s, 5-second units, 0 for none) has not passed on the
 * elapsed clock, scores 250 times its points. A goal_score_reduction_level above
 * 1 (above 10 counts as 9) takes level - 1 tenths off a positive score and
 * adds level - 1 tenths more to a negative one. The scores go to the
 * player's mission_score, or with player_idx -1 to the team's mission score;
 * for the local player a nonzero total also sends the bonus or penalty
 * message through g_msg_arg_table. Returns the total, 0 when no goal paid. */
// FUNCTION: XVT 0x435640
int mission_apply_flight_group_goal_score(int16_t event_condition,
					  uint16_t flight_group_idx,
					  int player_idx,
					  uint16_t goal_score_reduction_level,
					  int special_cargo_flag, int team_idx)
{
	unsigned int mission_time_seconds = mission_clock_to_seconds(
		g_mission_elapsed_clock.hours, g_mission_elapsed_clock.minutes,
		g_mission_elapsed_clock.seconds);
	int score_total = 0;
	unsigned int flight_group_number = flight_group_idx;
	struct flight_group_goal *goal =
		g_mission_flight_groups[flight_group_idx].fg.goals;
	int goal_index = 0;
	do {
		if ((goal->goal_kind == 2 &&
		     goal->event_condition == (uint16_t)event_condition &&
		     goal->amount == 18 &&
		     g_mission_flight_groups[flight_group_number]
				     .fg.goals[goal_index]
				     .enabled_teams[team_idx] != 0) ||
		    (goal->goal_kind == 2 &&
		     goal->event_condition == (uint16_t)event_condition &&
		     goal->amount == 19 &&
		     g_mission_flight_groups[flight_group_number]
				     .fg.goals[goal_index]
				     .enabled_teams[team_idx] != 0 &&
		     special_cargo_flag == 1)) {
			unsigned int time_limit_seconds =
				5 * goal->time_limit5s;
			if (time_limit_seconds == 0 ||
			    mission_time_seconds <= time_limit_seconds) {
				int score = 250 * goal->points;
				int score_tenth = score / 10;
				if (goal_score_reduction_level > 10) {
					goal_score_reduction_level = 9;
				}
				if (goal_score_reduction_level > 1) {
					if (score > 0) {
						int score_adjustment = 1;
						score_adjustment -=
							goal_score_reduction_level;
						score_adjustment *= score_tenth;
						score += score_adjustment;
					} else {
						score +=
							score_tenth *
							(goal_score_reduction_level -
							 1);
					}
				}
				if (player_idx != -1) {
					g_players[player_idx]
						.mission_stats.mission_score +=
						score;
				} else {
					g_flight_mission_state.runtime
						.team_scores[TEAM_SCORE_MISSION]
							    [team_idx] += score;
				}
				score_total += score;
				XVT_LOG_DEBUG(
					"mission.bonus_paid fg=%d goal=%d team=%d slot=%d condition=%d points=%d level=%u score=%d total=%d seconds=%u predicted=%d",
					(int)flight_group_idx, goal_index,
					team_idx, player_idx,
					(int)event_condition, (int)goal->points,
					(unsigned)goal_score_reduction_level,
					score,
					player_idx != -1
						? g_players[player_idx]
							  .mission_stats
							  .mission_score
						: g_flight_mission_state.runtime
							  .team_scores
								  [TEAM_SCORE_MISSION]
								  [team_idx],
					mission_time_seconds,
					g_flight_sim_side_effects_suppressed);
				if (g_flight_sim_side_effects_suppressed == 0) {
					XVT_LOG_INFO(
						"battle.bonus_scored team=%d goal=%d fg=%d by=%d score=%d tick=%d",
						team_idx, goal_index,
						(int)flight_group_idx,
						player_idx, score, g_game_time);
				}
			} else {
				XVT_LOG_DEBUG(
					"mission.bonus_late fg=%d goal=%d team=%d slot=%d seconds=%u limit=%u predicted=%d",
					(int)flight_group_idx, goal_index,
					team_idx, player_idx,
					mission_time_seconds,
					time_limit_seconds,
					g_flight_sim_side_effects_suppressed);
			}
		}
		++goal;
		++goal_index;
	} while (goal_index < 8);

	if (score_total != 0 && player_idx == g_local_player) {
		if (score_total > 0) {
			g_msg_arg_table[0] = (uint16_t)score_total;
			msg_emit_in_flight_message(
				IFMSG_199_BONUS_POINTS_AWARDED_ARG, player_idx);
		} else {
			g_msg_arg_table[0] = (uint16_t)-score_total;
			msg_emit_in_flight_message(
				IFMSG_200_PENALTY_POINTS_DEDUCTED_ARG,
				player_idx);
		}
	}
	return score_total;
}

/* Pays a flight group's per-craft bonus goals for one event to every team a
 * goal is enabled for: each bonus goal (goal_kind 2) for event_condition with
 * amount 18, or 19 when special_cargo_flag is 1, adds 250 times its points to
 * runtime.team_scores[TEAM_SCORE_BONUS] of each team in its enabled_teams. When
 * the goal's time limit (time_limit5s, 5-second units) has passed, score is not
 * reloaded: the value left from the previous paying goal, unset before the
 * first, is still multiplied by 250 and added. */
// FUNCTION: XVT 0x435850
void mission_apply_team_goal_score_all_enabled_teams(int16_t event_condition,
						     uint16_t flight_group_idx,
						     int special_cargo_flag)
{
	int score;

	struct flight_group_goal *goal =
		g_mission_flight_groups[flight_group_idx].fg.goals;
	int remaining_goals = 8;
	do {
		if (goal->goal_kind == 2 &&
		    goal->event_condition == (uint16_t)event_condition &&
		    (goal->amount == 18 ||
		     (goal->amount == 19 && special_cargo_flag == 1))) {
			unsigned int elapsed_seconds = mission_clock_to_seconds(
				g_mission_elapsed_clock.hours,
				g_mission_elapsed_clock.minutes,
				g_mission_elapsed_clock.seconds);
			unsigned int time_limit_seconds =
				5 * goal->time_limit5s;
			if (time_limit_seconds == 0 ||
			    elapsed_seconds <= time_limit_seconds) {
				score = goal->points;
			} else if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_WARN(
					"mission.bonus_score_stale fg=%d goal=%d seconds=%u limit=%u",
					(int)flight_group_idx,
					8 - remaining_goals, elapsed_seconds,
					time_limit_seconds);
			}
			score *= 250;
			int team_index = 0;
			do {
				if (goal->enabled_teams[team_index] != 0) {
					g_flight_mission_state.runtime
						.team_scores[0][team_index] +=
						score;
					XVT_LOG_DEBUG(
						"mission.team_bonus_paid team=%d fg=%d goal=%d condition=%d score=%d total=%d late=%d predicted=%d",
						team_index,
						(int)flight_group_idx,
						8 - remaining_goals,
						(int)event_condition, score,
						g_flight_mission_state.runtime
							.team_scores
								[0][team_index],
						time_limit_seconds != 0 &&
							elapsed_seconds >
								time_limit_seconds,
						g_flight_sim_side_effects_suppressed);
					if (g_flight_sim_side_effects_suppressed ==
					    0) {
						XVT_LOG_INFO(
							"battle.bonus_scored team=%d goal=%d fg=%d by=%d score=%d tick=%d",
							team_index,
							8 - remaining_goals,
							(int)flight_group_idx,
							-1, score, g_game_time);
					}
				}
				++team_index;
			} while (team_index < 10);
		}
		goal++;
		--remaining_goals;
	} while (remaining_goals != 0);
}

/* As mission_apply_team_goal_score_all_enabled_teams, but pays only team_idx, and
 * only for goals enabled for that team; it has the same unset score when a
 * time limit has passed. */
// FUNCTION: XVT 0x435930
void mission_apply_team_goal_score_for_team(int16_t event_condition,
					    uint16_t flight_group_idx,
					    int special_cargo_flag,
					    uint8_t team_idx)
{
	struct flight_group_goal *goal =
		g_mission_flight_groups[flight_group_idx].fg.goals;
	int team_index = team_idx;
	uint8_t *enabled_team = &goal->enabled_teams[team_index];
	int remaining_goals = 8;
	int score;
	do {
		if (goal->goal_kind == 2 &&
		    goal->event_condition == (uint16_t)event_condition &&
		    (goal->amount == 18 ||
		     (goal->amount == 19 && special_cargo_flag == 1))) {
			unsigned int elapsed_seconds = mission_clock_to_seconds(
				g_mission_elapsed_clock.hours,
				g_mission_elapsed_clock.minutes,
				g_mission_elapsed_clock.seconds);
			unsigned int time_limit_seconds =
				5 * goal->time_limit5s;
			if (time_limit_seconds == 0 ||
			    elapsed_seconds <= time_limit_seconds) {
				score = goal->points;
			} else if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_WARN(
					"mission.bonus_score_stale fg=%d goal=%d seconds=%u limit=%u",
					(int)flight_group_idx,
					8 - remaining_goals, elapsed_seconds,
					time_limit_seconds);
			}
			score *= 250;
			if (*enabled_team != 0) {
				g_flight_mission_state.runtime
					.team_scores[0][team_index] += score;
				XVT_LOG_DEBUG(
					"mission.team_bonus_paid team=%d fg=%d goal=%d condition=%d score=%d total=%d late=%d predicted=%d",
					team_index, (int)flight_group_idx,
					8 - remaining_goals,
					(int)event_condition, score,
					g_flight_mission_state.runtime
						.team_scores[0][team_index],
					time_limit_seconds != 0 &&
						elapsed_seconds >
							time_limit_seconds,
					g_flight_sim_side_effects_suppressed);
				if (g_flight_sim_side_effects_suppressed == 0) {
					XVT_LOG_INFO(
						"battle.bonus_scored team=%d goal=%d fg=%d by=%d score=%d tick=%d",
						team_index, 8 - remaining_goals,
						(int)flight_group_idx, -1,
						score, g_game_time);
				}
			}
		}
		enabled_team += sizeof(*goal);
		goal++;
		--remaining_goals;
	} while (remaining_goals != 0);
}

/* Returns hours, minutes and seconds as a count of seconds. */
// FUNCTION: XVT 0x435A10
int mission_clock_to_seconds(uint8_t hours, uint8_t minutes, uint8_t seconds)
{
	return 60 * (minutes + 60 * hours) + seconds;
}

/* Returns the score for destroying an object, by its flight group's craft
 * type: mission_compute_craft_point_value for a space craft
 * (CRAFT_FAMILY_SPACE_CRAFT); else 500 for object types 0x46 to 0x4A and
 * 0x50 to 0x54 and 40 for the rest, tripled in a melee mission. */
// FUNCTION: XVT 0x435A40
int mission_compute_kill_score_for_object(int victim_obj_idx)
{
	unsigned int object_type = g_craft_type_to_object_type
		[g_mission_flight_groups[g_object_table[victim_obj_idx]
						 .flight_group_idx]
			 .fg.craft_type];
	if (g_object_type_table[object_type].family_id ==
	    CRAFT_FAMILY_SPACE_CRAFT) {
		return mission_compute_craft_point_value(victim_obj_idx);
	}
	int kill_score;
	if ((object_type >= 0x46 && object_type <= 0x4A) ||
	    (object_type >= 0x50 && object_type <= 0x54)) {
		kill_score = 500;
	} else {
		kill_score = 40;
	}
	if (g_mission_header.mission_type == MISSION_TYPE_MELEE) {
		kill_score *= 3;
	}
	XVT_LOG_DEBUG(
		"mission.kill_value object=%d type=%u value=%d melee=%d predicted=%d",
		victim_obj_idx, object_type, kill_score,
		g_mission_header.mission_type == MISSION_TYPE_MELEE,
		g_flight_sim_side_effects_suppressed);
	return kill_score;
}

/* Returns a craft's point value: 40 times its model's craft_point_value
 * (doubled for CRAFT_SPECIES_SUPER_STAR_DESTROYER), plus each warhead
 * launcher's loaded warheads at their g_projectile_type_data.warhead_point_value,
 * plus g_countermeasure_type_point_value for its countermeasure type and
 * g_beam_type_point_value for its beam type. The original build does not check
 * these types against the tables; the modern build skips an unknown warhead
 * type and counts an unknown countermeasure or beam type as type 0. Does not
 * check that the object has a craft. */
// FUNCTION: XVT 0x435AD0
int mission_compute_craft_point_value(int obj_idx)
{
	struct craft_data *craft = g_object_table[obj_idx].mobj->p_craft;
	unsigned int model_index =
		get_model_index_from_type(g_object_table[obj_idx].object_type);
	unsigned int point_value =
		40 * g_model_defs[model_index].craft_point_value;
	if (g_object_table[obj_idx].object_type ==
	    CRAFT_SPECIES_SUPER_STAR_DESTROYER) {
		point_value *= 2;
	}

	for (unsigned int launcher_index = 0;
	     launcher_index < craft->warhead_launcher_count; ++launcher_index) {
		unsigned int first_slot =
			g_model_defs[model_index]
				.warhead_launcher_first_slot[launcher_index];
		unsigned int weapon_count =
			craft->weapon_slots[first_slot].ammo_count;
		unsigned int last_slot =
			g_model_defs[model_index]
				.warhead_launcher_last_slot[launcher_index];
		weapon_count += craft->weapon_slots[last_slot].ammo_count;
		unsigned int warhead_type =
			craft->warhead_slot_type_ids[launcher_index];
		if (warhead_type >= PROJECTILE_OBJECT_TYPE_FIRST &&
		    warhead_type < PROJECTILE_OBJECT_TYPE_FIRST +
					   PROJECTILE_OBJECT_TYPE_COUNT) {
			point_value +=
				g_projectile_type_data.warhead_point_value
					[warhead_type -
					 PROJECTILE_OBJECT_TYPE_FIRST] *
				weapon_count;
		} else if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_WARN(
				"mission.craft_value_type_unknown object=%d kind=\"warhead\" type=%u",
				obj_idx, warhead_type);
		}
	}

	unsigned int countermeasure_type = (uint8_t)craft->cm_type_id;
	unsigned int beam_type = (uint8_t)craft->beam_type_id;
	if (countermeasure_type >=
	    sizeof(g_countermeasure_type_point_value) /
		    sizeof(g_countermeasure_type_point_value[0])) {
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_WARN(
				"mission.craft_value_type_unknown object=%d kind=\"countermeasure\" type=%u",
				obj_idx, countermeasure_type);
		}
		countermeasure_type = 0;
	}
	if (beam_type >= sizeof(g_beam_type_point_value) /
				 sizeof(g_beam_type_point_value[0])) {
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_WARN(
				"mission.craft_value_type_unknown object=%d kind=\"beam\" type=%u",
				obj_idx, beam_type);
		}
		beam_type = 0;
	}
	point_value += g_countermeasure_type_point_value[countermeasure_type];
	point_value += g_beam_type_point_value[beam_type];
	XVT_LOG_DEBUG(
		"mission.craft_value object=%d type=%d value=%u base=%u launchers=%d countermeasure=%u beam=%u predicted=%d",
		obj_idx, (int)g_object_table[obj_idx].object_type, point_value,
		40u * g_model_defs[model_index].craft_point_value,
		(int)craft->warhead_launcher_count, countermeasure_type,
		beam_type, g_flight_sim_side_effects_suppressed);
	return point_value;
}

/* Returns g_mission_elapsed_clock as a count of seconds. Called only by
 * net_session_send_packet. */
// FUNCTION: XVT 0x448D70
int mission_get_elapsed_clock_seconds(void)
{
	return 60 * (g_mission_elapsed_clock.minutes +
		     60 * g_mission_elapsed_clock.hours) +
	       g_mission_elapsed_clock.seconds;
}

/* Sets up the mission in fileName for flight. Lays out the object table's slot
 * ranges (32 craft, 128 player and 32 other projectiles, 16 debris, 16
 * explosions, 256 character-data, 8 local debris and 64 static slots), frees
 * and reallocates the object, mobile, character, craft and warhead pools, and
 * clears them, the players' object links, the message texts and the proving
 * grounds state. Loads the file with mission_load_file and returns 0 when that
 * fails, leaving the flight surface unlocked. Then binds the network players to
 * their flight groups (with g_flight_conf_no_pilot set, player numbers up to
 * g_active_flight_player_count), applies their chosen craft, warheads, beams and
 * countermeasures, copies craft choices to other player groups under the craft
 * selection and melee opponent rules, picks random optional craft and start
 * points when random variation is on, raises or lowers each group's group_ai for
 * difficulty, melee boosts and combat balance, sets the round count of player
 * groups, marks the models needed, picks random special cargo craft, places
 * backdrop objects, and starts g_mission_elapsed_clock at 0 and
 * g_mission_countdown_clock at the time limit. It sets proving_grounds_mode_active
 * from proving_grounds_craft_type, then clears both before loading; nothing here
 * sets the flag again, so the proving grounds arm at the end (flight group 0's
 * craft, a countdown of 10 minus the level minutes and 59 seconds) runs only if
 * a call in between does. Returns 1. Writes g_mission_flight_groups,
 * g_mission_fg_stats[].current_mission_point_ref, g_players[] iff and team,
 * g_flight_mission_state, g_object_type_table flags, g_asteroid_field_rand_seed,
 * g_current_flight_group_idx and the slot globals; also g_pilot_data's melee
 * tournament state, g_backdrop_model_types, g_backdrop_packed_directions,
 * g_mission_messages, g_object_table and the object pools,
 * g_mobile_object_link_indices, g_object_slot_range_by_genus and g_sim_steps_per_second
 * (0). */
// FUNCTION: XVT 0x452CE0
uint16_t mission_init(const char *file_name)
{
	enum {
		PLAYER_COUNT = 8,
		TEAM_COUNT = 10,
		CRAFT_SLOT_COUNT = 32,
		PLAYER_PROJECTILE_SLOT_COUNT = 128,
		OTHER_PROJECTILE_SLOT_COUNT = 32,
		DEBRIS_SLOT_COUNT = 16,
		EXPLOSION_SLOT_COUNT = 16,
		CHAR_DATA_SLOT_COUNT = 256,
		STATIC_OBJECT_SLOT_COUNT = 64,
		LOCAL_DEBRIS_SLOT_COUNT = 8,
		MOBILE_OBJECT_SLOT_COUNT = 488,
		MISSION_MESSAGE_COUNT_USED = 64,
		MODEL_TYPE_COUNT = 201,
		OPTIONAL_CRAFT_COUNT = 10,
		OPTIONAL_WARHEAD_COUNT = 8,
		OPTIONAL_BEAM_COUNT = 6,
		OPTIONAL_COUNTERMEASURE_COUNT = 4,
		CRAFT_GENUS_RANGE_LIMIT = 6,
		RESERVED_GENUS_RANGE_FIRST = 8,
		RESERVED_GENUS_RANGE_LIMIT = 11,
		DEBRIS_GENUS_RANGE = 11,
		RESERVED_GENUS_RANGE_12 = 12,
		RESERVED_GENUS_RANGE_14 = 14,
		RESERVED_GENUS_RANGE_15 = 15,
		CHAR_DATA_GENUS_RANGE = 16,
		MODEL_ASSET_REQUIRED = 0x10,
		MODEL_FLAG_BACKDROP = 0x20,
		AI_FIGHTER_OBJECT_TYPE_FIRST = 1,
		AI_FIGHTER_OBJECT_TYPE_LAST = 4,
		AI_HEADHUNTER_OBJECT_TYPE = 14,
		MAX_GROUP_AI = 5,
		RANDOM_OPTIONAL_CRAFT_CATEGORY = 4,
		UNLIMITED_PLAYER_WAVES = 99,
		MISSION_POINT_REF_BASE = 0x8000,
		MISSION_POINT_VARIANT_COUNT = 3,
		MULTIPART_MODEL_TYPE = 100,
		MULTIPART_FIRST_DEPENDENT_MODEL_TYPE = 101,
		MULTIPART_LAST_DEPENDENT_MODEL_TYPE = 105,
		BACKDROP_DIRECTION_COUNT = 6,
		BACKDROP_DIRECTION_MAX = BACKDROP_DIRECTION_COUNT - 1,
		BACKDROP_RANDOM_SEED_BIAS = 16657,
		BACKDROP_SOURCE_MODEL_TYPE = 87,
		BACKDROP_FIRST_FREE_MODEL_TYPE = 114,
		BACKDROP_MODEL_TYPE_LIMIT = 116,
		PALETTED_BYTES_PER_PIXEL = 1,
		BACKDROP_BASE_STATUS_COUNT = 8,
		BACKDROP_STATUS_COUNT = 17,
		BACKDROP_STATUS_FRAME_OFFSET = 85,
	};

	flight_surface_unlock();
	g_craft_data_pool_capacity = CRAFT_SLOT_COUNT;
	g_debris_object_slots_total = DEBRIS_SLOT_COUNT;
	g_world_state_debris_slot_count = DEBRIS_SLOT_COUNT;
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = CRAFT_SLOT_COUNT;
	g_projectile_object_slot_start = CRAFT_SLOT_COUNT;
	g_projectile_object_slot_end = CRAFT_SLOT_COUNT +
				       PLAYER_PROJECTILE_SLOT_COUNT +
				       OTHER_PROJECTILE_SLOT_COUNT;
	g_debris_object_slot_start = g_projectile_object_slot_end;
	g_mobile_object_char_data_count = CHAR_DATA_SLOT_COUNT;
	g_projectile_object_slots_total =
		PLAYER_PROJECTILE_SLOT_COUNT + OTHER_PROJECTILE_SLOT_COUNT;
	g_local_debris_slot_count = LOCAL_DEBRIS_SLOT_COUNT;
	g_debris_object_slot_end =
		g_debris_object_slot_start + DEBRIS_SLOT_COUNT;
	g_explosion_object_slot_start = g_debris_object_slot_end;
	g_explosion_object_slot_end =
		g_explosion_object_slot_start + EXPLOSION_SLOT_COUNT;
	g_mobile_object_char_data_slot_start = g_explosion_object_slot_end;
	g_region_static_object_slot_count = STATIC_OBJECT_SLOT_COUNT;
	g_mobile_object_char_data_slot_end =
		g_mobile_object_char_data_slot_start + CHAR_DATA_SLOT_COUNT;
	g_local_transient_slot_start = g_mobile_object_char_data_slot_end;
	g_local_debris_slot_end =
		g_local_transient_slot_start + LOCAL_DEBRIS_SLOT_COUNT;
	g_region_main_object_slot_end = g_local_debris_slot_end;

	if (g_object_table_handle != 0) {
		memory_handle_block_done_stub(g_object_table_handle);
		memory_free_handle(g_object_table_handle);
		g_object_table_handle = 0;
	}
	if (g_mobile_object_pool_handle != 0) {
		memory_handle_block_done_stub(g_mobile_object_pool_handle);
		memory_free_handle(g_mobile_object_pool_handle);
		g_mobile_object_pool_handle = 0;
	}
	if (g_mobile_object_char_data_handle != 0) {
		memory_handle_block_done_stub(g_mobile_object_char_data_handle);
		memory_free_handle(g_mobile_object_char_data_handle);
		g_mobile_object_char_data_handle = 0;
	}
	if (g_craft_data_pool_handle != 0) {
		memory_handle_block_done_stub(g_craft_data_pool_handle);
		memory_free_handle(g_craft_data_pool_handle);
		g_craft_data_pool_handle = 0;
	}
	if (g_warhead_guidance_pool_handle != 0) {
		memory_handle_block_done_stub(g_warhead_guidance_pool_handle);
		memory_free_handle(g_warhead_guidance_pool_handle);
		g_warhead_guidance_pool_handle = 0;
	}

	fe_disk_io_unlock_global_buffers();
	g_object_table_handle = memory_alloc_handle_zeroed(
		(sizeof(struct object_record) *
		 (size_t)(g_region_main_object_slot_end +
			  g_region_static_object_slot_count)),
		0);
	g_mobile_object_pool_handle = memory_alloc_handle_zeroed(
		sizeof(struct mobile_object) *
			(size_t)g_region_main_object_slot_end,
		0);
	g_mobile_object_char_data_handle = memory_alloc_handle_zeroed(
		sizeof(struct mobile_object_char_data) *
			(size_t)g_mobile_object_char_data_count,
		0);
	g_craft_data_pool_handle = memory_alloc_handle_zeroed(
		sizeof(struct craft_data) * (size_t)g_craft_data_pool_capacity,
		0);
	g_warhead_guidance_pool_handle = memory_alloc_handle_zeroed(
		sizeof(struct warhead_guidance_state) *
			(size_t)(g_projectile_object_slots_total + 1),
		0);
	if (g_object_table_handle == 0 || g_mobile_object_pool_handle == 0 ||
	    g_mobile_object_char_data_handle == 0 ||
	    g_craft_data_pool_handle == 0 ||
	    g_warhead_guidance_pool_handle == 0) {
		XVT_LOG_ERROR(
			"mission.pools_alloc_failed objects=%u mobiles=%u chars=%u crafts=%u warheads=%u",
			(unsigned)g_object_table_handle,
			(unsigned)g_mobile_object_pool_handle,
			(unsigned)g_mobile_object_char_data_handle,
			(unsigned)g_craft_data_pool_handle,
			(unsigned)g_warhead_guidance_pool_handle);
	}
	fe_disk_io_lock_global_buffers();

	/* slot is this function's shared loop counter: by loop it holds an
	 * object slot, a genus, a player, an object type, a message, a team, a
	 * mission point reference or a flight group index. */
	uint16_t slot;
	for (slot = 0; slot < MOBILE_OBJECT_SLOT_COUNT; ++slot) {
		g_mobile_object_link_indices[slot].warhead_guidance_idx = -1;
		g_mobile_object_link_indices[slot].craft_data_idx = -1;
		g_mobile_object_link_indices[slot].char_data_idx = -1;
	}
	memset(g_spawn_object_type_by_object_slot, 0xFF,
	       sizeof(g_spawn_object_type_by_object_slot));
	for (slot = 0; slot < CRAFT_GENUS_RANGE_LIMIT; ++slot) {
		g_object_slot_range_by_genus[slot].start =
			(uint16_t)g_active_region_object_slot_start;
		g_object_slot_range_by_genus[slot].end =
			(uint16_t)g_active_region_craft_object_slot_end;
	}
	g_object_slot_range_by_genus[CRAFT_GENUS_PLAYER_PROJECTILE].start =
		(uint16_t)g_projectile_object_slot_start;
	g_object_slot_range_by_genus[CRAFT_GENUS_PLAYER_PROJECTILE].end =
		(uint16_t)(g_projectile_object_slot_start +
			   PLAYER_PROJECTILE_SLOT_COUNT);
	g_object_slot_range_by_genus[CRAFT_GENUS_OTHER_PROJECTILE].start =
		(uint16_t)(g_projectile_object_slot_start +
			   PLAYER_PROJECTILE_SLOT_COUNT);
	g_object_slot_range_by_genus[CRAFT_GENUS_OTHER_PROJECTILE].end =
		(uint16_t)g_projectile_object_slot_end;
	for (slot = RESERVED_GENUS_RANGE_FIRST;
	     slot < RESERVED_GENUS_RANGE_LIMIT; ++slot) {
		g_object_slot_range_by_genus[slot].start = 0;
		g_object_slot_range_by_genus[slot].end = 0;
	}
	g_object_slot_range_by_genus[DEBRIS_GENUS_RANGE].start =
		(uint16_t)g_debris_object_slot_start;
	g_object_slot_range_by_genus[DEBRIS_GENUS_RANGE].end =
		(uint16_t)g_debris_object_slot_end;
	g_object_slot_range_by_genus[RESERVED_GENUS_RANGE_12].start = 0;
	g_object_slot_range_by_genus[RESERVED_GENUS_RANGE_12].end = 0;
	g_object_slot_range_by_genus[CRAFT_GENUS_EXPLOSION].start =
		(uint16_t)g_explosion_object_slot_start;
	g_object_slot_range_by_genus[CRAFT_GENUS_EXPLOSION].end =
		(uint16_t)g_explosion_object_slot_end;
	g_object_slot_range_by_genus[RESERVED_GENUS_RANGE_14].start = 0;
	g_object_slot_range_by_genus[RESERVED_GENUS_RANGE_14].end = 0;
	g_object_slot_range_by_genus[RESERVED_GENUS_RANGE_15].start = 0;
	g_object_slot_range_by_genus[RESERVED_GENUS_RANGE_15].end = 0;
	g_object_slot_range_by_genus[CHAR_DATA_GENUS_RANGE].start =
		(uint16_t)g_mobile_object_char_data_slot_start;
	g_object_slot_range_by_genus[CHAR_DATA_GENUS_RANGE].end =
		(uint16_t)g_mobile_object_char_data_slot_end;
	g_sim_steps_per_second = 0;
	g_flight_mission_state.proving_grounds_mode_active =
		g_flight_mission_state.proving_grounds_craft_type;
	for (slot = 0; slot < g_mobile_object_char_data_count; ++slot) {
		memset(&g_mobile_object_char_data_pool[slot], 0,
		       sizeof(g_mobile_object_char_data_pool[slot]));
	}
	for (slot = 0; slot < g_region_main_object_slot_end; ++slot) {
		g_mobile_object_pool_base[slot].seconds_alive = 0;
		g_mobile_object_pool_base[slot].lifetime_timer = 0;
		g_mobile_object_pool_base[slot].sim_state_timestamp = 0;
		g_mobile_object_pool_base[slot].orient_matrix_dirty = 0;
		g_mobile_object_pool_base[slot].p_craft = NULL;
		g_mobile_object_pool_base[slot].p_warhead_guidance = NULL;
		g_mobile_object_pool_base[slot].p_char_data = NULL;
	}
	for (slot = 0; slot < g_region_main_object_slot_end +
				      g_region_static_object_slot_count;
	     ++slot) {
		g_object_table[slot].object_type = 0;
		g_object_table[slot].player_owner_idx = -1;
		if (slot < g_region_main_object_slot_end) {
			g_object_table[slot].mobj =
				&g_mobile_object_pool_base[slot];
		} else {
			g_object_table[slot].mobj = NULL;
		}
	}
	for (slot = (uint16_t)g_active_region_object_slot_start;
	     slot < g_active_region_craft_object_slot_end; ++slot) {
		g_object_table[slot].mobj->iff = -1;
	}
	for (slot = 0; slot < PLAYER_COUNT; ++slot) {
		g_players[slot].object_index = -1;
		g_players[slot].bound_object_signature = 0;
	}
	for (slot = 0; slot < MODEL_TYPE_COUNT; ++slot) {
		if (g_object_type_table[slot].record_flags != 0) {
			g_object_type_table[slot].asset_flags &=
				(uint8_t)~MODEL_ASSET_REQUIRED;
		}
	}
	for (slot = 0; slot < MISSION_MESSAGE_COUNT_USED; ++slot) {
		g_mission_messages[slot].message[0] = 0;
	}
	for (slot = 0;
	     slot <
	     sizeof(g_flight_mission_state.global_unit_craft_count) /
		     sizeof(g_flight_mission_state.global_unit_craft_count[0]);
	     ++slot) {
		g_flight_mission_state.global_unit_craft_count[slot] = 0;
	}
	memset(g_flight_mission_state.runtime.team_has_countable_craft, 0,
	       sizeof(g_flight_mission_state.runtime.team_has_countable_craft));
	g_flight_mission_state.proving_grounds_mode_active = 0;
	g_flight_mission_state.proving_grounds_craft_type = 0;
	g_flight_mission_state.proving_grounds_level = 0;
	g_flight_mission_state.proving_grounds_score = 0;
	memset(g_flight_mission_state.unused08, 0,
	       sizeof(g_flight_mission_state.unused08));
	g_flight_mission_state.proving_grounds_checkpoints_passed = 0;
	memset(g_flight_mission_state.unused0c, 0,
	       sizeof(g_flight_mission_state.unused0c));
	g_flight_mission_state.proving_grounds_checkpoints_remaining = 0;
	g_flight_mission_state.proving_grounds_targets_destroyed = 0;
	g_flight_mission_state.proving_grounds_time_bonus = 0;

	if (mission_load_file(file_name) == 0) {
		XVT_LOG_ERROR(
			"mission.load_failed file=\"%s\" version=%u reason=\"%s\"",
			file_name, (unsigned)g_mission_file_version,
			(g_mission_file_version == 12 ||
			 g_mission_file_version == 13 ||
			 g_mission_file_version == 14 ||
			 g_mission_file_version == 0xFFFF)
				? "read_error"
				: "unknown_version");
		return 0;
	}

	uint16_t flight_group_idx;
	for (flight_group_idx = 0;
	     flight_group_idx < (int16_t)g_mission_header.num_flight_groups;
	     ++flight_group_idx) {
		g_mission_flight_groups[flight_group_idx].player_owner_idx = -1;
	}

	int team_player_fg_counts[TEAM_COUNT];
	int team_owned_flight_group[TEAM_COUNT];
	int team_idx;
	int player_owned_team_count;
	if (g_flight_conf_no_pilot == 0) {
		int selected_craft_type = 0;
		int selected_warhead = 0;

		for (slot = 0; slot < PLAYER_COUNT; ++slot) {
			if (g_pilot_data.network_players[slot].direct_play_id !=
			    0) {
				uint16_t source_flight_group =
					(uint16_t)g_pilot_data
						.network_players[slot]
						.flight_group_id;

				selected_craft_type =
					g_mission_flight_groups
						[source_flight_group]
							.fg.craft_type;
				selected_warhead = g_mission_flight_groups
							   [source_flight_group]
								   .fg.warhead;
				break;
			}
		}
		for (slot = 0; slot < PLAYER_COUNT; ++slot) {
			if (g_pilot_data.network_players[slot].direct_play_id !=
			    0) {
				uint16_t assigned_flight_group =
					(uint16_t)g_pilot_data
						.network_players[slot]
						.flight_group_id;
				unsigned int player_slot =
					net_session_find_player_slot_by_dpid(
						g_pilot_data
							.network_players[slot]
							.direct_play_id);

				if (player_slot < PLAYER_COUNT) {
					g_mission_flight_groups
						[assigned_flight_group]
							.player_owner_idx =
						(int)player_slot;
					g_players[player_slot].iff =
						g_mission_flight_groups
							[assigned_flight_group]
								.fg.iff;
					g_players[player_slot].team =
						g_mission_flight_groups
							[assigned_flight_group]
								.fg.team;
					if (g_pilot_data.network_players[slot]
						    .craft_id != 0) {
						g_mission_flight_groups
							[assigned_flight_group]
								.fg.craft_type =
							(craft_species)g_pilot_data
								.network_players
									[slot]
								.craft_id;
					} else if (
						g_pilot_data
								.network_players
									[slot]
								.craft_option !=
							-1 &&
						g_pilot_data
								.network_players
									[slot]
								.craft_option <
							OPTIONAL_CRAFT_COUNT) {
						g_mission_flight_groups
							[assigned_flight_group]
								.fg.craft_type =
							g_mission_flight_groups[assigned_flight_group]
								.fg
								.optional_craft
									[g_pilot_data
										 .network_players
											 [slot]
										 .craft_option];
						g_mission_flight_groups
							[assigned_flight_group]
								.fg
								.number_of_craft =
							g_mission_flight_groups[assigned_flight_group]
								.fg
								.number_of_optional_craft
									[g_pilot_data
										 .network_players
											 [slot]
										 .craft_option];
						g_mission_flight_groups
							[assigned_flight_group]
								.fg
								.number_of_waves =
							g_mission_flight_groups[assigned_flight_group]
								.fg
								.number_of_optional_craft_waves
									[g_pilot_data
										 .network_players
											 [slot]
										 .craft_option];
					}
					if (g_pilot_data.network_players[slot]
							    .warhead_option !=
						    -1 &&
					    g_pilot_data.network_players[slot]
							    .warhead_option <
						    OPTIONAL_WARHEAD_COUNT) {
						g_mission_flight_groups
							[assigned_flight_group]
								.fg.warhead =
							g_mission_flight_groups[assigned_flight_group]
								.fg
								.optional_warheads
									[g_pilot_data
										 .network_players
											 [slot]
										 .warhead_option];
					}
					if (g_pilot_data.network_players[slot]
							    .beam_option !=
						    -1 &&
					    g_pilot_data.network_players[slot]
							    .beam_option <
						    OPTIONAL_BEAM_COUNT) {
						int craft_type =
							g_mission_flight_groups
								[assigned_flight_group]
									.fg
									.craft_type;
						if (craft_type >=
							    CRAFT_SPECIES_X_WING &&
						    (craft_type <=
							     CRAFT_SPECIES_TIE_FIGHTER ||
						     craft_type ==
							     CRAFT_SPECIES_Z_95_HEADHUNTER)) {
							g_mission_flight_groups
								[assigned_flight_group]
									.fg
									.beam =
								0;
						} else {
							g_mission_flight_groups
								[assigned_flight_group]
									.fg
									.beam =
								g_mission_flight_groups[assigned_flight_group]
									.fg
									.optional_beams
										[g_pilot_data
											 .network_players
												 [slot]
											 .beam_option];
						}
					}
					if (g_pilot_data.network_players[slot]
							    .countermeasure_option !=
						    -1 &&
					    g_pilot_data.network_players[slot]
							    .countermeasure_option <
						    OPTIONAL_COUNTERMEASURE_COUNT) {
						g_mission_flight_groups
							[assigned_flight_group]
								.fg
								.countermeasures =
							g_mission_flight_groups[assigned_flight_group]
								.fg
								.optional_countermeasures
									[g_pilot_data
										 .network_players
											 [slot]
										 .countermeasure_option];
					}
					XVT_LOG_DEBUG(
						"mission.player_bound slot=%u entry=%d player=%u fg=%d team=%d iff=%d craft=%d count=%d warhead=%d beam=%d countermeasures=%d",
						player_slot, (int)slot,
						(unsigned)g_pilot_data
							.network_players[slot]
							.direct_play_id,
						(int)assigned_flight_group,
						(int)g_mission_flight_groups
							[assigned_flight_group]
								.fg.team,
						(int)g_mission_flight_groups
							[assigned_flight_group]
								.fg.iff,
						(int)g_mission_flight_groups
							[assigned_flight_group]
								.fg.craft_type,
						(int)g_mission_flight_groups
							[assigned_flight_group]
								.fg
								.number_of_craft,
						(int)g_mission_flight_groups
							[assigned_flight_group]
								.fg.warhead,
						(int)g_mission_flight_groups
							[assigned_flight_group]
								.fg.beam,
						(int)g_mission_flight_groups
							[assigned_flight_group]
								.fg
								.countermeasures);
					XVT_LOG_INFO(
						"battle.player_joined who=%u entry=%d player=%u fg=%d team=%d tick=0",
						player_slot, (int)slot,
						(unsigned)g_pilot_data
							.network_players[slot]
							.direct_play_id,
						(int)assigned_flight_group,
						(int)g_mission_flight_groups
							[assigned_flight_group]
								.fg.team);
				} else {
					XVT_LOG_WARN(
						"mission.player_slot_missing entry=%d player=%u",
						(int)slot,
						(unsigned)g_pilot_data
							.network_players[slot]
							.direct_play_id);
				}
			}
		}

		int team_player_owner_counts[TEAM_COUNT];
		for (slot = 0; slot < TEAM_COUNT; ++slot) {
			team_player_owner_counts[slot] = 0;
		}
		/* team_idx is reused here as a flight group index. */
		for (team_idx = 0;
		     team_idx < (int16_t)g_mission_header.num_flight_groups;
		     ++team_idx) {
			if (g_mission_flight_groups[team_idx]
				    .player_owner_idx != -1) {
				++team_player_owner_counts
					[g_mission_flight_groups[team_idx]
						 .fg.team];
			}
		}
		player_owned_team_count = 0;
		for (slot = 0; slot < TEAM_COUNT; ++slot) {
			if (team_player_owner_counts[slot] != 0) {
				++player_owned_team_count;
			}
		}
		if (g_mission_header.mission_type == MISSION_TYPE_MELEE &&
		    player_owned_team_count == 1) {
			g_flight_mission_state.ai_opponents_enabled = 1;
		}
		XVT_LOG_DEBUG("mission.player_teams teams=%d ai=%d",
			      player_owned_team_count,
			      (int)g_flight_mission_state.ai_opponents_enabled);

		if (g_game_config.craft_selection ==
			    CRAFT_SELECTION_HOST_ONLY ||
		    (g_mission_header.mission_type == MISSION_TYPE_MELEE &&
		     g_pilot_data.num_human_players_last_mission == 1 &&
		     (g_pilot_data.mission_sequence_active != 1 ||
		      g_pilot_data.melee_tournament_sequence_state
				      .human_player_count == 1)) ||
		    (g_mission_header.mission_type == MISSION_TYPE_MELEE &&
		     g_game_config.craft_selection == CRAFT_SELECTION_OFF &&
		     g_pilot_data.mission_sequence_active == 1)) {
			uint16_t source_flight_group;

			/* This loop first uses source_flight_group as a network
			 * player index, then stores the first connected
			 * player's flight group in it; if no player is
			 * connected it ends as PLAYER_COUNT. */
			for (source_flight_group = 0;
			     source_flight_group < PLAYER_COUNT;
			     ++source_flight_group) {
				if (g_pilot_data
					    .network_players
						    [source_flight_group]
					    .direct_play_id != 0) {
					source_flight_group =
						(uint16_t)g_pilot_data
							.network_players
								[source_flight_group]
							.flight_group_id;
					break;
				}
			}
			XVT_LOG_DEBUG("mission.craft_shared fg=%d selection=%d",
				      (int)source_flight_group,
				      (int)g_game_config.craft_selection);
			for (flight_group_idx = 0;
			     flight_group_idx <
			     (int16_t)g_mission_header.num_flight_groups;
			     ++flight_group_idx) {
				if (g_mission_flight_groups[flight_group_idx]
					    .fg.player_number != 0) {
					if (g_mission_flight_groups
							    [flight_group_idx]
								    .fg
								    .craft_type ==
						    selected_craft_type &&
					    g_mission_flight_groups
							    [flight_group_idx]
								    .fg
								    .warhead ==
						    selected_warhead) {
						g_mission_flight_groups
							[flight_group_idx]
								.fg.warhead =
							g_mission_flight_groups
								[source_flight_group]
									.fg
									.warhead;
					}
					g_mission_flight_groups
						[flight_group_idx]
							.fg.craft_type =
						g_mission_flight_groups
							[source_flight_group]
								.fg.craft_type;
					g_mission_flight_groups
						[flight_group_idx]
							.fg.number_of_craft =
						g_mission_flight_groups
							[source_flight_group]
								.fg
								.number_of_craft;
					g_mission_flight_groups
						[flight_group_idx]
							.fg.number_of_waves =
						g_mission_flight_groups
							[source_flight_group]
								.fg
								.number_of_waves;
					g_mission_flight_groups
						[flight_group_idx]
							.fg.beam =
						g_mission_flight_groups
							[source_flight_group]
								.fg.beam;
					g_mission_flight_groups
						[flight_group_idx]
							.fg.countermeasures =
						g_mission_flight_groups
							[source_flight_group]
								.fg
								.countermeasures;
				}
			}
		} else if (g_mission_header.mission_type ==
				   MISSION_TYPE_MELEE &&
			   g_game_config.craft_selection ==
				   CRAFT_SELECTION_ON) {
			for (slot = 0; slot < TEAM_COUNT; ++slot) {
				team_player_fg_counts[slot] = 0;
				team_player_owner_counts[slot] = 0;
				team_owned_flight_group[slot] = 0;
			}
			for (flight_group_idx = 0;
			     flight_group_idx <
			     (int16_t)g_mission_header.num_flight_groups;
			     ++flight_group_idx) {
				if (g_mission_flight_groups[flight_group_idx]
					    .fg.player_number != 0) {
					int flight_group_team =
						g_mission_flight_groups
							[flight_group_idx]
								.fg.team;

					++team_player_fg_counts
						[flight_group_team];
					if (g_mission_flight_groups
						    [flight_group_idx]
							    .player_owner_idx !=
					    -1) {
						++team_player_owner_counts
							[flight_group_team];
						team_owned_flight_group
							[flight_group_team] =
								flight_group_idx;
					}
				}
			}
			player_owned_team_count = 0;
			for (slot = 0; slot < TEAM_COUNT; ++slot) {
				if (team_player_owner_counts[slot] != 0) {
					++player_owned_team_count;
				}
			}
			XVT_LOG_DEBUG("mission.craft_by_team teams=%d",
				      player_owned_team_count);
			for (flight_group_idx = 0;
			     flight_group_idx <
			     (int16_t)g_mission_header.num_flight_groups;
			     ++flight_group_idx) {
				if (g_mission_flight_groups[flight_group_idx]
						    .fg.player_number != 0 &&
				    g_mission_flight_groups[flight_group_idx]
						    .player_owner_idx == -1 &&
				    team_player_owner_counts
						    [g_mission_flight_groups
							     [flight_group_idx]
								     .fg
								     .team] !=
					    0) {
					uint16_t source_fg = (uint16_t)
						team_owned_flight_group
							[g_mission_flight_groups
								 [flight_group_idx]
									 .fg
									 .team];

					g_mission_flight_groups
						[flight_group_idx]
							.fg.craft_type =
						g_mission_flight_groups
							[source_fg]
								.fg.craft_type;
					g_mission_flight_groups
						[flight_group_idx]
							.fg.number_of_craft =
						g_mission_flight_groups
							[source_fg]
								.fg
								.number_of_craft;
					g_mission_flight_groups
						[flight_group_idx]
							.fg.number_of_waves =
						g_mission_flight_groups
							[source_fg]
								.fg
								.number_of_waves;
					g_mission_flight_groups
						[flight_group_idx]
							.fg.warhead =
						g_mission_flight_groups
							[source_fg]
								.fg.warhead;
					g_mission_flight_groups
						[flight_group_idx]
							.fg.beam =
						g_mission_flight_groups
							[source_fg]
								.fg.beam;
					g_mission_flight_groups
						[flight_group_idx]
							.fg.countermeasures =
						g_mission_flight_groups
							[source_fg]
								.fg
								.countermeasures;
				}
			}
			if (g_flight_mission_state.ai_opponents_enabled == 1) {
				/* Here slot is a team index, compared with
				 * fg.team and used to index team_standings. */
				for (slot = 0; slot < TEAM_COUNT; ++slot) {
					if (team_player_fg_counts[slot] != 0 &&
					    team_player_owner_counts[slot] ==
						    0) {
						int source_team;
						uint16_t source_fg;

						if (g_pilot_data.mission_sequence_active ==
							    1 &&
						    g_pilot_data.melee_tournament_sequence_state
								    .current_mission_index !=
							    0) {
							int source_team_and_type =
								g_pilot_data
									.melee_tournament_sequence_state
									.team_standings
										[slot]
									.ai_opponent_source_team_and_type_flag;

							source_team =
								source_team_and_type &
								INT32_MAX;
							if (team_player_owner_counts
								    [source_team] ==
							    0) {
								uint16_t target_fg =
									0;
								int replace_craft_type =
									0;

								while (target_fg <
								       (int16_t)g_mission_header
									       .num_flight_groups) {
									if (g_mission_flight_groups[target_fg]
											    .fg
											    .team ==
										    slot &&
									    g_mission_flight_groups[target_fg]
											    .fg
											    .player_number !=
										    0) {
										break;
									}
									{
										int object_type = g_craft_type_to_object_type
											[g_mission_flight_groups[target_fg]
												 .fg
												 .craft_type];

										if (object_type >=
											    AI_FIGHTER_OBJECT_TYPE_FIRST &&
										    (object_type <=
											     AI_FIGHTER_OBJECT_TYPE_LAST ||
										     object_type ==
											     AI_HEADHUNTER_OBJECT_TYPE)) {
											if ((source_team_and_type &
											     INT32_MIN) ==
											    0) {
												replace_craft_type =
													1;
											}
										} else if (
											(source_team_and_type &
											 INT32_MIN) !=
											0) {
											replace_craft_type =
												1;
										}
									}
									++target_fg;
								}
								if (replace_craft_type !=
								    0) {
									uint8_t replacement = g_ai_opponent_craft_type_by_player_craft_type
										[g_mission_flight_groups[target_fg]
											 .fg
											 .craft_type];
									XVT_LOG_DEBUG(
										"mission.ai_team_craft_replaced team=%d craft=%d",
										(int)slot,
										(int)replacement);

									for (flight_group_idx =
										     0;
									     flight_group_idx <
									     (int16_t)g_mission_header
										     .num_flight_groups;
									     ++flight_group_idx) {
										if (g_mission_flight_groups[flight_group_idx]
												    .fg
												    .team ==
											    slot &&
										    g_mission_flight_groups[flight_group_idx]
												    .fg
												    .player_number !=
											    0) {
											g_mission_flight_groups
												[flight_group_idx]
													.fg
													.craft_type =
												replacement;
										}
									}
								}
								source_fg =
									(uint16_t)g_mission_header
										.num_flight_groups;
							} else {
								source_fg = team_owned_flight_group
									[source_team];
							}
						} else {
							int selected_ordinal =
								game_rand_range(
									(uint16_t)
										player_owned_team_count);
							int ordinal = 0;
							uint16_t candidate_team;

							for (candidate_team = 0;
							     candidate_team <
							     TEAM_COUNT;
							     ++candidate_team) {
								if (team_player_fg_counts
										    [candidate_team] !=
									    0 &&
								    team_player_owner_counts
										    [candidate_team] !=
									    0) {
									if (ordinal ==
									    selected_ordinal) {
										source_team =
											candidate_team;
										break;
									}
									++ordinal;
								}
							}
							if (candidate_team ==
							    TEAM_COUNT) {
								XVT_LOG_WARN(
									"mission.ai_team_source_missing team=%d teams=%d",
									(int)slot,
									player_owned_team_count);
								continue;
							}
							if (g_pilot_data.mission_sequence_active ==
								    1 &&
							    g_pilot_data.melee_tournament_sequence_state
									    .team_standings
										    [slot]
									    .ai_opponent_source_team_and_type_flag !=
								    -1) {
								int source_craft_type =
									g_mission_flight_groups
										[team_owned_flight_group
											 [source_team]]
											.fg
											.craft_type;
								int source_object_type = g_craft_type_to_object_type
									[source_craft_type];

								if (source_object_type >=
									    AI_FIGHTER_OBJECT_TYPE_FIRST &&
								    (source_object_type <=
									     AI_FIGHTER_OBJECT_TYPE_LAST ||
								     source_object_type ==
									     AI_HEADHUNTER_OBJECT_TYPE)) {
									g_pilot_data
										.melee_tournament_sequence_state
										.team_standings
											[slot]
										.ai_opponent_source_team_and_type_flag =
										source_team |
										INT32_MIN;
								} else {
									g_pilot_data
										.melee_tournament_sequence_state
										.team_standings
											[slot]
										.ai_opponent_source_team_and_type_flag =
										source_team;
								}
							}
							source_fg = (uint16_t)team_owned_flight_group
								[source_team];
						}
						XVT_LOG_DEBUG(
							"mission.ai_team_craft team=%d fg=%d groups=%d",
							(int)slot,
							(int)source_fg,
							(int)g_mission_header
								.num_flight_groups);

						if (source_fg !=
						    g_mission_header
							    .num_flight_groups) {
							for (flight_group_idx =
								     0;
							     flight_group_idx <
							     (int16_t)g_mission_header
								     .num_flight_groups;
							     ++flight_group_idx) {
								if (g_mission_flight_groups[flight_group_idx]
										    .fg
										    .team ==
									    slot &&
								    g_mission_flight_groups[flight_group_idx]
										    .fg
										    .player_number !=
									    0) {
									g_mission_flight_groups
										[flight_group_idx]
											.fg
											.craft_type =
										g_mission_flight_groups
											[source_fg]
												.fg
												.craft_type;
									g_mission_flight_groups
										[flight_group_idx]
											.fg
											.number_of_craft =
										g_mission_flight_groups
											[source_fg]
												.fg
												.number_of_craft;
									g_mission_flight_groups
										[flight_group_idx]
											.fg
											.number_of_waves =
										g_mission_flight_groups
											[source_fg]
												.fg
												.number_of_waves;
									g_mission_flight_groups
										[flight_group_idx]
											.fg
											.warhead =
										g_mission_flight_groups
											[source_fg]
												.fg
												.warhead;
									g_mission_flight_groups
										[flight_group_idx]
											.fg
											.beam =
										g_mission_flight_groups
											[source_fg]
												.fg
												.beam;
									g_mission_flight_groups
										[flight_group_idx]
											.fg
											.countermeasures =
										g_mission_flight_groups
											[source_fg]
												.fg
												.countermeasures;
								}
							}
						}
					}
				}
			}
		}
	} else {
		for (flight_group_idx = 0;
		     flight_group_idx <
		     (int16_t)g_mission_header.num_flight_groups;
		     ++flight_group_idx) {
			int player_number =
				g_mission_flight_groups[flight_group_idx]
					.fg.player_number;

			if (player_number <= g_active_flight_player_count &&
			    player_number != 0) {
				int player_idx = player_number - 1;

				g_mission_flight_groups[flight_group_idx]
					.player_owner_idx = player_idx;
				g_players[player_idx].iff =
					g_mission_flight_groups
						[flight_group_idx]
							.fg.iff;
				g_players[player_idx].team =
					g_mission_flight_groups
						[flight_group_idx]
							.fg.team;
				XVT_LOG_DEBUG(
					"mission.player_number_bound slot=%d fg=%d team=%d iff=%d",
					player_idx, (int)flight_group_idx,
					(int)g_mission_flight_groups
						[flight_group_idx]
							.fg.team,
					(int)g_mission_flight_groups
						[flight_group_idx]
							.fg.iff);
			} else {
				g_mission_flight_groups[flight_group_idx]
					.player_owner_idx = -1;
			}
		}
	}

	if (g_flight_mission_state.random_variation_enabled != 0) {
		for (flight_group_idx = 0;
		     flight_group_idx <
		     (int16_t)g_mission_header.num_flight_groups;
		     ++flight_group_idx) {
			if (g_mission_flight_groups[flight_group_idx]
					    .player_owner_idx == -1 &&
			    g_mission_flight_groups[flight_group_idx]
					    .fg.optional_craft_category ==
				    RANDOM_OPTIONAL_CRAFT_CATEGORY) {
				unsigned int option_count = 1;
				unsigned int option_idx;

				for (option_idx = 0;
				     option_idx < OPTIONAL_CRAFT_COUNT;
				     ++option_idx) {
					if (g_mission_flight_groups
						    [flight_group_idx]
							    .fg.optional_craft
								    [option_idx] !=
					    CRAFT_SPECIES_UNKNOWN) {
						++option_count;
					}
				}
				unsigned int selected_option =
					game_rand_range((uint16_t)option_count);
				if (option_count > 1 && selected_option != 0) {
					unsigned int ordinal = 1;

					for (option_idx = 0;
					     option_idx < OPTIONAL_CRAFT_COUNT;
					     ++option_idx) {
						if (g_mission_flight_groups[flight_group_idx]
							    .fg.optional_craft
								    [option_idx] !=
						    CRAFT_SPECIES_UNKNOWN) {
							if (ordinal ==
							    selected_option) {
								break;
							}
							++ordinal;
						}
					}
					if (g_mission_flight_groups
						    [flight_group_idx]
							    .fg.optional_craft
								    [option_idx] !=
					    CRAFT_SPECIES_UNKNOWN) {
						g_mission_flight_groups
							[flight_group_idx]
								.fg.craft_type =
							g_mission_flight_groups[flight_group_idx]
								.fg
								.optional_craft
									[option_idx];
						g_mission_flight_groups
							[flight_group_idx]
								.fg
								.number_of_craft =
							g_mission_flight_groups[flight_group_idx]
								.fg
								.number_of_optional_craft
									[option_idx];
						g_mission_flight_groups
							[flight_group_idx]
								.fg
								.number_of_waves =
							g_mission_flight_groups[flight_group_idx]
								.fg
								.number_of_optional_craft_waves
									[option_idx];
						XVT_LOG_DEBUG(
							"mission.random_craft fg=%d option=%u craft=%d count=%d waves=%d",
							(int)flight_group_idx,
							option_idx,
							(int)g_mission_flight_groups
								[flight_group_idx]
									.fg
									.craft_type,
							(int)g_mission_flight_groups
								[flight_group_idx]
									.fg
									.number_of_craft,
							(int)g_mission_flight_groups
								[flight_group_idx]
									.fg
									.number_of_waves);
					}
				}
			}
		}
	}

	if (g_flight_mission_state.difficulty != GAME_DIFFICULTY_MEDIUM &&
	    (g_mission_header.mission_type != MISSION_TYPE_COMBAT ||
	     g_pilot_data.num_human_players_last_mission < 2)) {
		int player_team = (uint16_t)g_players[0].team;
		XVT_LOG_DEBUG(
			"mission.difficulty_applied difficulty=%d team=%d",
			(int)g_flight_mission_state.difficulty, player_team);

		for (flight_group_idx = 0;
		     flight_group_idx <
		     (int16_t)g_mission_header.num_flight_groups;
		     ++flight_group_idx) {
			if (g_flight_mission_state.difficulty ==
			    GAME_DIFFICULTY_HARD) {
				int flight_group_team =
					g_mission_flight_groups
						[flight_group_idx]
							.fg.team;
				int hostile =
					player_team == flight_group_team
						? 0
						: g_mission_teams[flight_group_team]
								  .allies[player_team] ==
							  0;

				if (hostile != 0) {
					g_mission_flight_groups
						[flight_group_idx]
							.fg.group_ai += 2;
					if (g_mission_flight_groups
						    [flight_group_idx]
							    .fg.group_ai >
					    MAX_GROUP_AI) {
						g_mission_flight_groups
							[flight_group_idx]
								.fg.group_ai =
							MAX_GROUP_AI;
					}
				} else if (
					g_mission_header.mission_type !=
						MISSION_TYPE_MELEE &&
					(g_pilot_data.mission_directory_id !=
						 MISSION_DIRECTORY_TRAINING_EXERCISES ||
					 g_pilot_data.mission_sequence_active !=
						 1 ||
					 g_flight_player_count != 1) &&
					g_mission_flight_groups
							[flight_group_idx]
								.fg.group_ai !=
						0) {
					--g_mission_flight_groups
						  [flight_group_idx]
							  .fg.group_ai;
				}
			} else {
				int flight_group_team =
					g_mission_flight_groups
						[flight_group_idx]
							.fg.team;
				int hostile =
					player_team == flight_group_team
						? 0
						: g_mission_teams[flight_group_team]
								  .allies[player_team] ==
							  0;

				if (hostile == 0) {
					g_mission_flight_groups
						[flight_group_idx]
							.fg.group_ai += 2;
					if (g_mission_flight_groups
						    [flight_group_idx]
							    .fg.group_ai >
					    MAX_GROUP_AI) {
						g_mission_flight_groups
							[flight_group_idx]
								.fg.group_ai =
							MAX_GROUP_AI;
					}
				} else if (g_mission_flight_groups
						   [flight_group_idx]
							   .fg.group_ai != 0) {
					--g_mission_flight_groups
						  [flight_group_idx]
							  .fg.group_ai;
				}
			}
		}
	}

	if (g_mission_header.mission_type == MISSION_TYPE_MELEE) {
		int ai_boost_team;

		if (g_pilot_data.mission_sequence_active == 1 &&
		    g_pilot_data.melee_tournament_sequence_state
				    .current_mission_index != 0) {
			ai_boost_team =
				g_pilot_data.melee_tournament_sequence_state
					.ai_boost_first_team;
		} else {
			/* Below, team_owned_flight_group is reused as a
			 * per-team count of player-owned flight groups. */
			memset(team_owned_flight_group, 0,
			       sizeof(team_owned_flight_group));
			memset(team_player_fg_counts, 0,
			       sizeof(team_player_fg_counts));
			for (int flight_group = 0;
			     flight_group < g_mission_header.num_flight_groups;
			     ++flight_group) {
				if (g_mission_flight_groups[flight_group]
					    .fg.player_number != 0) {
					++team_player_fg_counts
						[g_mission_flight_groups
							 [flight_group]
								 .fg.team];
				}
				if (g_mission_flight_groups[flight_group]
					    .player_owner_idx != -1) {
					++team_owned_flight_group
						[g_mission_flight_groups
							 [flight_group]
								 .fg.team];
				}
			}
			int teams_without_owners = 0;
			for (team_idx = 0; team_idx < TEAM_COUNT; ++team_idx) {
				if (team_player_fg_counts[team_idx] != 0 &&
				    team_owned_flight_group[team_idx] == 0) {
					++teams_without_owners;
				}
			}
			int selected_ordinal =
				game_rand_range((uint16_t)teams_without_owners);
			int ordinal = -1;
			for (ai_boost_team = 0; ai_boost_team < TEAM_COUNT;
			     ++ai_boost_team) {
				if (team_player_fg_counts[ai_boost_team] != 0 &&
				    team_owned_flight_group[ai_boost_team] ==
					    0 &&
				    ++ordinal == selected_ordinal) {
					break;
				}
			}
			if (ai_boost_team == TEAM_COUNT) {
				ai_boost_team = team_player_fg_counts[0];
				XVT_LOG_WARN(
					"mission.melee_boost_team_missing team=%d teams=%d",
					ai_boost_team, teams_without_owners);
			}
			if (g_pilot_data.mission_sequence_active == 1) {
				g_pilot_data.melee_tournament_sequence_state
					.ai_boost_first_team = ai_boost_team;
			}
		}
		int boost_count;
		if (g_flight_mission_state.difficulty == GAME_DIFFICULTY_EASY) {
			boost_count = 3;
		} else if (g_flight_mission_state.difficulty ==
			   GAME_DIFFICULTY_MEDIUM) {
			boost_count = 2;
		} else {
			boost_count = 1;
		}
		XVT_LOG_DEBUG("mission.melee_boost team=%d boosts=%d",
			      ai_boost_team, boost_count);
		while (boost_count-- != 0) {
			for (int flight_group = 0;
			     flight_group < g_mission_header.num_flight_groups;
			     ++flight_group) {
				if (g_mission_flight_groups[flight_group]
						    .fg.player_number != 0 &&
				    g_mission_flight_groups[flight_group]
						    .player_owner_idx == -1 &&
				    g_mission_flight_groups[flight_group]
						    .fg.team == ai_boost_team &&
				    g_mission_flight_groups[flight_group]
						    .fg.group_ai <
					    MAX_GROUP_AI) {
					++g_mission_flight_groups[flight_group]
						  .fg.group_ai;
					break;
				}
			}
			++ai_boost_team;
		}
	}

	if (g_mission_header.mission_type == MISSION_TYPE_COMBAT &&
	    g_pilot_data.num_human_players_last_mission >= 2) {
		XVT_LOG_DEBUG("mission.combat_balance balance=%d humans=%u",
			      (int)g_game_config.combat_balance,
			      g_pilot_data.num_human_players_last_mission);
		if (g_game_config.combat_balance ==
		    COMBAT_BALANCE_AUTOBALANCE) {
			if (player_owned_team_count == 1) {
				int handicapped_team =
					(uint16_t)g_players[0].team;

				for (flight_group_idx = 0;
				     flight_group_idx <
				     (int16_t)
					     g_mission_header.num_flight_groups;
				     ++flight_group_idx) {
					uint8_t flight_group_team =
						g_mission_flight_groups
							[flight_group_idx]
								.fg.team;
					int is_opponent =
						handicapped_team ==
								flight_group_team
							? 0
							: g_mission_teams[flight_group_team]
									  .allies[handicapped_team] ==
								  0;

					if (is_opponent) {
						if (g_pilot_data
							    .num_human_players_last_mission ==
						    2) {
							++g_mission_flight_groups
								  [flight_group_idx]
									  .fg
									  .group_ai;
						} else {
							g_mission_flight_groups
								[flight_group_idx]
									.fg
									.group_ai +=
								2;
						}
						if (g_mission_flight_groups
							    [flight_group_idx]
								    .fg
								    .group_ai >
						    MAX_GROUP_AI) {
							g_mission_flight_groups
								[flight_group_idx]
									.fg
									.group_ai =
								MAX_GROUP_AI;
						}
					}
				}
			}
		} else if (g_game_config.combat_balance ==
			   COMBAT_BALANCE_FAVOR_IMPERIAL) {
			int handicapped_team = 1;

			for (flight_group_idx = 0;
			     flight_group_idx <
			     (int16_t)g_mission_header.num_flight_groups;
			     ++flight_group_idx) {
				uint8_t flight_group_team =
					g_mission_flight_groups
						[flight_group_idx]
							.fg.team;
				int is_opponent =
					handicapped_team == flight_group_team
						? 0
						: g_mission_teams[flight_group_team]
								  .allies[handicapped_team] ==
							  0;

				if (is_opponent) {
					if (g_pilot_data
						    .num_human_players_last_mission ==
					    2) {
						++g_mission_flight_groups
							  [flight_group_idx]
								  .fg.group_ai;
					} else {
						g_mission_flight_groups
							[flight_group_idx]
								.fg.group_ai +=
							2;
					}
					if (g_mission_flight_groups
						    [flight_group_idx]
							    .fg.group_ai >
					    MAX_GROUP_AI) {
						g_mission_flight_groups
							[flight_group_idx]
								.fg.group_ai =
							MAX_GROUP_AI;
					}
				}
			}
		} else if (g_game_config.combat_balance ==
			   COMBAT_BALANCE_FAVOR_REBEL) {
			int handicapped_team = 0;

			for (flight_group_idx = 0;
			     flight_group_idx <
			     (int16_t)g_mission_header.num_flight_groups;
			     ++flight_group_idx) {
				uint8_t flight_group_team =
					g_mission_flight_groups
						[flight_group_idx]
							.fg.team;
				int is_opponent =
					handicapped_team == flight_group_team
						? 0
						: g_mission_teams[flight_group_team]
								  .allies[handicapped_team] ==
							  0;

				if (is_opponent) {
					if (g_pilot_data
						    .num_human_players_last_mission ==
					    2) {
						++g_mission_flight_groups
							  [flight_group_idx]
								  .fg.group_ai;
					} else {
						g_mission_flight_groups
							[flight_group_idx]
								.fg.group_ai +=
							2;
					}
					if (g_mission_flight_groups
						    [flight_group_idx]
							    .fg.group_ai >
					    MAX_GROUP_AI) {
						g_mission_flight_groups
							[flight_group_idx]
								.fg.group_ai =
							MAX_GROUP_AI;
					}
				}
			}
		}
	}

	for (flight_group_idx = 0;
	     flight_group_idx < (int16_t)g_mission_header.num_flight_groups;
	     ++flight_group_idx) {
		if (g_mission_flight_groups[flight_group_idx]
			    .player_owner_idx != -1) {
			if (g_mission_flight_groups[flight_group_idx]
				    .fg.number_of_craft > 1u) {
				if (g_mission_flight_groups[flight_group_idx]
					    .fg.player_craft == 0) {
					g_mission_flight_groups
						[flight_group_idx]
							.fg.player_craft = 1;
				}
			}
			if (g_mission_flight_groups[flight_group_idx]
				    .fg.number_of_craft == 1u) {
				g_mission_flight_groups[flight_group_idx]
					.fg.player_craft = 0;
			}
		}
		if (g_mission_flight_groups[flight_group_idx]
				    .fg.player_number != 0 &&
		    (g_mission_flight_groups[flight_group_idx]
				     .player_owner_idx != -1 ||
		     g_mission_header.mission_type == MISSION_TYPE_MELEE)) {
			if (g_flight_mission_state
				    .player_flight_group_wave_mode == 0) {
				g_mission_flight_groups[flight_group_idx]
					.fg.number_of_waves = 0;
			} else if (g_flight_mission_state
					   .player_flight_group_wave_mode ==
				   2) {
				g_mission_flight_groups[flight_group_idx]
					.fg.number_of_waves =
					UNLIMITED_PLAYER_WAVES;
			}
		}
		uint8_t object_type = g_craft_type_to_object_type
			[g_mission_flight_groups[flight_group_idx]
				 .fg.craft_type];
		g_object_type_table[object_type].asset_flags |=
			MODEL_ASSET_REQUIRED;
		if (object_type == MULTIPART_MODEL_TYPE) {
			for (object_type = MULTIPART_FIRST_DEPENDENT_MODEL_TYPE;
			     object_type <= MULTIPART_LAST_DEPENDENT_MODEL_TYPE;
			     ++object_type) {
				g_object_type_table[object_type].asset_flags |=
					MODEL_ASSET_REQUIRED;
			}
		}
		if (g_mission_flight_groups[flight_group_idx]
			    .fg.random_special_cargo_craft != 0) {
			g_mission_flight_groups[flight_group_idx]
				.fg
				.special_cargo_craft = (uint8_t)game_rand_range(
				g_mission_flight_groups[flight_group_idx]
					.fg.number_of_craft);
		}
		uint16_t mission_point_ref = MISSION_POINT_REF_BASE;
		if (g_flight_mission_state.random_variation_enabled != 0) {
			unsigned int option_count = 1;

			for (slot = MISSION_POINT_REF_BASE + 1;
			     slot <= MISSION_POINT_REF_BASE +
					     MISSION_POINT_VARIANT_COUNT;
			     ++slot) {
				if (g_mission_flight_groups[flight_group_idx]
					    .fg.mission_point_enabled
						    [slot -
						     MISSION_POINT_REF_BASE] !=
				    0) {
					++option_count;
				}
			}
			int selected_option =
				game_rand_range((uint16_t)option_count);
			if (option_count > 1 && selected_option != 0) {
				int ordinal = 1;

				for (slot = MISSION_POINT_REF_BASE + 1;
				     slot <=
				     MISSION_POINT_REF_BASE +
					     MISSION_POINT_VARIANT_COUNT;
				     ++slot) {
					if (g_mission_flight_groups[flight_group_idx]
						    .fg.mission_point_enabled
							    [slot -
							     MISSION_POINT_REF_BASE] !=
					    0) {
						if (ordinal ==
						    selected_option) {
							mission_point_ref =
								slot;
							break;
						}
						++ordinal;
					}
				}
			}
		}
		g_mission_fg_stats[flight_group_idx].current_mission_point_ref =
			mission_point_ref;
		if (g_mission_header.mission_type == MISSION_TYPE_MELEE) {
			if (g_mission_flight_groups[flight_group_idx]
				    .fg.player_number != 0) {
				uint16_t marking_object_type =
					g_craft_type_to_object_type
						[g_mission_flight_groups
							 [flight_group_idx]
								 .fg
								 .craft_type];

				if (marking_object_type ==
					    CRAFT_SPECIES_TIE_FIGHTER ||
				    marking_object_type ==
					    CRAFT_SPECIES_TIE_INTERCEPTOR ||
				    marking_object_type ==
					    CRAFT_SPECIES_TIE_BOMBER ||
				    marking_object_type ==
					    CRAFT_SPECIES_TIE_ADVANCED ||
				    marking_object_type ==
					    CRAFT_SPECIES_TIE_DEFENDER) {
					if (++g_mission_flight_groups
						      [flight_group_idx]
							      .fg.markings >
					    3u) {
						g_mission_flight_groups
							[flight_group_idx]
								.fg.markings =
							0;
					}
				}
			}
			if ((g_pilot_data.mission_sequence_active != 1 ||
			     g_pilot_data.melee_tournament_sequence_state
					     .team_standings
						     [g_mission_flight_groups
							      [flight_group_idx]
								      .fg.team]
					     .ai_opponent_source_team_and_type_flag ==
				     -1) &&
			    g_mission_flight_groups[flight_group_idx]
					    .fg.player_number != 0 &&
			    g_mission_flight_groups[flight_group_idx]
					    .player_owner_idx == -1 &&
			    g_flight_mission_state.ai_opponents_enabled == 0) {
				for (slot = 0;
				     slot < (int16_t)g_mission_header
						    .num_flight_groups;
				     ++slot) {
					if (g_mission_flight_groups[slot]
							    .fg.player_number !=
						    0 &&
					    g_mission_flight_groups[slot]
							    .player_owner_idx !=
						    -1 &&
					    g_mission_flight_groups[slot]
							    .fg.team ==
						    g_mission_flight_groups
							    [flight_group_idx]
								    .fg.team) {
						break;
					}
				}
				if (slot == (int16_t)g_mission_header
						    .num_flight_groups) {
					g_mission_flight_groups
						[flight_group_idx]
							.fg
							.arrive_only_if_human =
						1;
				}
			}
		}
		XVT_LOG_DEBUG(
			"mission.group_prepared fg=%d team=%d owner=%d craft=%d count=%d waves=%d ai=%d start=%u player_craft=%d special=%d only_human=%d",
			(int)flight_group_idx,
			(int)g_mission_flight_groups[flight_group_idx].fg.team,
			g_mission_flight_groups[flight_group_idx]
				.player_owner_idx,
			(int)g_mission_flight_groups[flight_group_idx]
				.fg.craft_type,
			(int)g_mission_flight_groups[flight_group_idx]
				.fg.number_of_craft,
			(int)g_mission_flight_groups[flight_group_idx]
				.fg.number_of_waves,
			(int)g_mission_flight_groups[flight_group_idx]
				.fg.group_ai,
			(unsigned)mission_point_ref,
			(int)g_mission_flight_groups[flight_group_idx]
				.fg.player_craft,
			(int)g_mission_flight_groups[flight_group_idx]
				.fg.special_cargo_craft,
			(int)g_mission_flight_groups[flight_group_idx]
				.fg.arrive_only_if_human);
	}

	{
		uint16_t saved_random_state =
			(uint16_t)g_game_rand_feedback_state;

		g_game_rand_feedback_state =
			(int16_t)(g_mission_header.backdrop -
				  BACKDROP_RANDOM_SEED_BIAS);
		backdrop_generate_default_records();
		g_asteroid_field_rand_seed =
			(uint16_t)g_game_rand_feedback_state;
		g_game_rand_feedback_state = (int16_t)saved_random_state;

		uint16_t backdrop_direction_starts[BACKDROP_DIRECTION_COUNT];
		backdrop_direction_starts[0] = 0;
		backdrop_direction_starts[1] = g_backdrop_positive_y_count;
		backdrop_direction_starts[2] = g_backdrop_negative_y_count +
					       g_backdrop_positive_y_count;
		backdrop_direction_starts[3] = backdrop_direction_starts[2] +
					       g_backdrop_positive_x_count;
		backdrop_direction_starts[4] = g_backdrop_negative_x_count +
					       backdrop_direction_starts[3];
		backdrop_direction_starts[5] = g_backdrop_positive_z_count +
					       backdrop_direction_starts[4];
		XVT_LOG_DEBUG(
			"mission.backdrops_generated backdrop=%u seed=%u defaults=%d",
			(unsigned)g_mission_header.backdrop,
			(unsigned)g_asteroid_field_rand_seed,
			backdrop_direction_starts[5] +
				g_backdrop_negative_z_count);

		for (g_current_flight_group_idx = 0;
		     g_current_flight_group_idx <
		     (int16_t)g_mission_header.num_flight_groups;
		     ++g_current_flight_group_idx) {
			int current_flight_group = g_current_flight_group_idx;
			uint8_t craft_type =
				g_mission_flight_groups[current_flight_group]
					.fg.craft_type;

			if (craft_type != CRAFT_SPECIES_UNKNOWN) {
				uint16_t backdrop_type =
					g_craft_type_to_object_type[craft_type];

				if ((g_object_type_table[backdrop_type]
					     .behavior_flags &
				     MODEL_FLAG_BACKDROP) != 0) {
					uint16_t source_backdrop_type =
						backdrop_type;

					if (backdrop_type ==
					    BACKDROP_SOURCE_MODEL_TYPE) {
						backdrop_type =
							BACKDROP_FIRST_FREE_MODEL_TYPE;
						while (backdrop_type <
							       BACKDROP_MODEL_TYPE_LIMIT &&
						       (g_object_type_table
								[backdrop_type]
									.asset_flags &
							MODEL_ASSET_REQUIRED) !=
							       0) {
							++backdrop_type;
						}
						if (backdrop_type ==
						    BACKDROP_MODEL_TYPE_LIMIT) {
							XVT_LOG_WARN(
								"mission.backdrop_types_full fg=%d model=%u",
								current_flight_group,
								(unsigned)
									backdrop_type);
						}
						if (g_flight_bytes_per_pixel ==
							    PALETTED_BYTES_PER_PIXEL &&
						    g_mission_flight_groups
								    [current_flight_group]
									    .fg
									    .status1 >=
							    (unsigned int)
								    BACKDROP_BASE_STATUS_COUNT) {
							g_mission_flight_groups
								[current_flight_group]
									.fg
									.status1 =
								g_mission_flight_groups
									[current_flight_group]
										.fg
										.status1 &
								7;
						}
						if (g_mission_flight_groups
							    [current_flight_group]
								    .fg
								    .status1 <
						    (unsigned int)
							    BACKDROP_BASE_STATUS_COUNT) {
							g_object_type_table
								[backdrop_type]
									.resource_index =
								g_object_type_table
									[source_backdrop_type]
										.resource_index +
								g_mission_flight_groups
									[current_flight_group]
										.fg
										.status1;
							g_object_type_table
								[backdrop_type]
									.palette = g_backdrop_palette_remap_by_flight_group_status
								[g_mission_flight_groups
									 [current_flight_group]
										 .fg
										 .status1];
						} else if (
							g_mission_flight_groups
								[current_flight_group]
									.fg
									.status1 <
							(unsigned int)
								BACKDROP_STATUS_COUNT) {
							g_object_type_table
								[backdrop_type]
									.resource_index =
								g_mission_flight_groups
									[current_flight_group]
										.fg
										.status1 +
								BACKDROP_STATUS_FRAME_OFFSET;
							g_object_type_table
								[backdrop_type]
									.palette = g_backdrop_palette_remap_by_flight_group_status
								[g_mission_flight_groups
									 [current_flight_group]
										 .fg
										 .status1];
						}
					}
					g_object_type_table[backdrop_type]
						.asset_flags |=
						MODEL_ASSET_REQUIRED;
					uint16_t side =
						g_mission_flight_groups
							[current_flight_group]
								.fg
								.mission_point_z
									[0];
					if (side >
					    (unsigned int)
						    BACKDROP_DIRECTION_MAX) {
						XVT_LOG_WARN(
							"mission.backdrop_side_clamped fg=%d side=%u",
							current_flight_group,
							(unsigned)side);
						side = BACKDROP_DIRECTION_MAX;
					}
					uint16_t record_index =
						backdrop_direction_starts
							[side]++;
					if (record_index >= 64) {
						XVT_LOG_WARN(
							"mission.backdrop_table_full fg=%d record=%u",
							current_flight_group,
							(unsigned)record_index);
					}
					g_backdrop_model_types[record_index] =
						(uint8_t)backdrop_type;
					uint8_t packed_y = (uint8_t)(-(
						g_mission_flight_groups
							[current_flight_group]
								.fg
								.mission_point_y
									[0]
						<< 4));
					g_backdrop_packed_directions[record_index] =
						(uint8_t)(packed_y ^
							  ((packed_y ^
							    g_mission_flight_groups[current_flight_group]
								    .fg
								    .mission_point_x
									    [0]) &
							   0xF));
					XVT_LOG_DEBUG(
						"mission.backdrop_placed fg=%d model=%u side=%u record=%u",
						current_flight_group,
						(unsigned)backdrop_type,
						(unsigned)side,
						(unsigned)record_index);
				}
			}
		}
	}

	g_mission_elapsed_clock.subsecond_ticks = 0;
	g_flight_mission_state.team_victory_time_limit_started = 0;
	g_mission_elapsed_clock.hours = 0;
	g_mission_elapsed_clock.minutes = 0;
	g_mission_countdown_clock.subsecond_ticks = 0;
	g_mission_elapsed_clock.seconds = 0;
	g_mission_countdown_clock.hours = 0;
	g_mission_countdown_clock.minutes = 0;
	if (g_flight_mission_state.proving_grounds_mode_active == 0) {
		uint8_t time_limit_minutes =
			g_flight_mission_state.mission_time_limit_minutes;

		if (time_limit_minutes != 0) {
			if (time_limit_minutes != UINT8_MAX) {
				g_mission_countdown_clock.minutes =
					time_limit_minutes;
			} else if (g_mission_header.time_limit_minutes != 0) {
				g_mission_countdown_clock.minutes =
					g_mission_header.time_limit_minutes;
				g_flight_mission_state
					.mission_time_limit_minutes =
					g_mission_header.time_limit_minutes;
			} else {
				g_flight_mission_state
					.mission_time_limit_minutes = 0;
			}
		} else {
			g_flight_mission_state.mission_time_limit_minutes = 0;
		}
		g_mission_countdown_clock.seconds = 0;
	} else {
		g_mission_flight_groups[0].fg.craft_type =
			(craft_species)g_flight_mission_state
				.proving_grounds_craft_type;
		g_object_type_table
			[g_craft_type_to_object_type
				 [g_flight_mission_state
					  .proving_grounds_craft_type]]
				.asset_flags |= MODEL_ASSET_REQUIRED;
		g_mission_countdown_clock.seconds = 59;
		g_mission_countdown_clock.minutes =
			(uint8_t)(10 -
				  g_flight_mission_state.proving_grounds_level);
	}
	XVT_LOG_INFO(
		"mission.prepared version=%u type=%d groups=%d messages=%d difficulty=%d minutes=%d",
		(unsigned)g_mission_file_version,
		(int)g_mission_header.mission_type,
		(int)g_mission_header.num_flight_groups,
		(int)g_mission_header.num_messages,
		(int)g_flight_mission_state.difficulty,
		(int)g_mission_countdown_clock.minutes);
	flight_surface_lock();
	return 1;
}

/* Resets the mission's runtime state when a flight starts and brings in the
 * flight groups that start in space. Per flight group it sets
 * g_mission_fg_stats[].arrival_enabled from g_mission_difficulty_arrival_masks for
 * the difficulty and g_fg_arrival_difficulty_masks for its arrival_difficulty, and
 * clears it when its arrival triggers have already failed, counting only pairs
 * of player-connection conditions joined by AND. It clears the group's counts,
 * sets its craft total to number_of_craft (squared for mines) times number_of_waves
 * + 1 when it may arrive or a player owns it, its special cargo total, and
 * every goal state to 4 (pending). Of the groups with a craft type and craft
 * that may arrive or have a player owner, a player's group, or one whose first
 * arrival condition is MISSION_COND_ALWAYS_TRUE with no delay (unless it is an
 * unowned player-number group with arrive_only_if_human set), arrives at once
 * through mission_start_flight_group_arrival, and one that is not a backdrop marks
 * its team in runtime.team_has_countable_craft. It clears each team's scores, goal
 * states, trigger counts, kill stats and per-group counts, and fills
 * runtime.team_fg_designation_code from the four-character entries of each group's
 * craft_role (a team digit or A, O, F, H, then COM, BAS, STA, MIS, CON, STR,
 * REL, PRI, SEC, TER, RES or MAN for codes 1 to 12). It then resets the
 * players' targeting, view, hyperspace and chat state,
 * g_flight_global_countdown_timers, g_player_flight_transient_timers, the message
 * flags, goal statuses and end flag in g_flight_mission_state, and sets
 * g_sim_steps_per_second, g_elapsed_ticks and g_flight_frame_step_mirror to 15,
 * g_input_timestamp and g_ready_message_queue_count to 0, g_next_object_signature to
 * 1, g_target_proximity_blink_bit to 0 and g_flight_runtime_state_initialized to 1;
 * g_initial_spawn_bind_player_craft_slots is 1 while it runs and 0 after. It also
 * sets g_local_debris_recycle_slot_cursor, g_render_object_ref,
 * g_render_object_ref_flags, g_hud_loaded_panel_set_id, g_action_key,
 * g_flight_initial_texture_cache_flush_pending and g_flight_display_rebuild_pending,
 * and loads the cockpit resources if they are not loaded. Leaves
 * g_current_flight_group_idx at the flight group count. */
// FUNCTION: XVT 0x454A20
void mission_init_flight_runtime_state(void)
{
	enum {
		PLAYER_COUNT = 8,
		TEAM_COUNT = 10,
		MISSION_FLIGHT_GROUP_COUNT = 48,
		FLIGHT_GROUP_GOAL_STATE_COUNT = 80,
		TEAM_GOAL_COUNT = 3,
		GLOBAL_GOAL_TRIGGER_COUNT = 4,
		CONDITION_STATE_COMPLETE = 1,
		CONDITION_STATE_FAILED = 2,
		CONDITION_STATE_PENDING = 4,
		ANY_TEAM = 10,
		MODEL_FLAG_BACKDROP = 0x20,
		DEFAULT_CAMERA_DISTANCE = 1024,
		DEFAULT_SIM_STEP = 15,
		GOAL_STATE_BATCH_SIZE = 8,
	};

	flight_surface_unlock();
	g_initial_spawn_bind_player_craft_slots = 1;
	g_next_object_signature = 1;
	for (g_current_flight_group_idx = 0;
	     g_current_flight_group_idx < g_mission_header.num_flight_groups;
	     ++g_current_flight_group_idx) {
		g_mission_fg_stats[g_current_flight_group_idx].arrival_enabled =
			g_mission_difficulty_arrival_masks
				[g_flight_mission_state.difficulty] &
			g_fg_arrival_difficulty_masks
				[g_mission_flight_groups
					 [g_current_flight_group_idx]
						 .fg.arrival_difficulty];
		if (g_mission_fg_stats[g_current_flight_group_idx]
			    .arrival_enabled == 0) {
			XVT_LOG_DEBUG(
				"mission.arrival_masked fg=%d difficulty=%d setting=%d",
				(int)g_current_flight_group_idx,
				(int)g_flight_mission_state.difficulty,
				(int)g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.arrival_difficulty);
			continue;
		}

		int16_t first_pair_state = CONDITION_STATE_PENDING;
		int16_t second_pair_state = CONDITION_STATE_PENDING;
		uint8_t condition1 =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.arrival_triggers[0]
				.triggers[0]
				.condition;
		uint8_t condition2 =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.arrival_triggers[0]
				.triggers[1]
				.condition;
		if ((condition1 == MISSION_COND_PLAYER_CONNECTED ||
		     condition2 == MISSION_COND_PLAYER_CONNECTED ||
		     condition1 == MISSION_COND_PLAYER_DISCONNECTED ||
		     condition2 == MISSION_COND_PLAYER_DISCONNECTED) &&
		    g_mission_flight_groups[g_current_flight_group_idx]
				    .fg.arrival_triggers[0]
				    .trigger1_or_trigger2 == 0) {
			uint16_t result1 = (uint16_t)mission_evaluate_condition(
				condition1,
				g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.arrival_triggers[0]
						.triggers[0]
						.variable_type,
				g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.arrival_triggers[0]
						.triggers[0]
						.variable,
				(uint8_t)g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.arrival_triggers[0]
						.triggers[0]
						.amount,
				0, ANY_TEAM);
			uint16_t result2 = (uint16_t)mission_evaluate_condition(
				condition2,
				g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.arrival_triggers[0]
						.triggers[1]
						.variable_type,
				g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.arrival_triggers[0]
						.triggers[1]
						.variable,
				(uint8_t)g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.arrival_triggers[0]
						.triggers[1]
						.amount,
				0, ANY_TEAM);
			if ((result1 & result2 & CONDITION_STATE_COMPLETE) !=
			    0) {
				first_pair_state = CONDITION_STATE_COMPLETE;
			} else {
				first_pair_state =
					((result1 | result2) &
					 CONDITION_STATE_FAILED) == 0
						? CONDITION_STATE_PENDING
						: CONDITION_STATE_FAILED;
			}
		}

		condition1 = g_mission_flight_groups[g_current_flight_group_idx]
				     .fg.arrival_triggers[1]
				     .triggers[0]
				     .condition;
		condition2 = g_mission_flight_groups[g_current_flight_group_idx]
				     .fg.arrival_triggers[1]
				     .triggers[1]
				     .condition;
		if ((condition1 == MISSION_COND_PLAYER_CONNECTED ||
		     condition2 == MISSION_COND_PLAYER_CONNECTED ||
		     condition1 == MISSION_COND_PLAYER_DISCONNECTED ||
		     condition2 == MISSION_COND_PLAYER_DISCONNECTED) &&
		    g_mission_flight_groups[g_current_flight_group_idx]
				    .fg.arrival_triggers[1]
				    .trigger1_or_trigger2 == 0) {
			uint16_t result1 = (uint16_t)mission_evaluate_condition(
				condition1,
				g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.arrival_triggers[1]
						.triggers[0]
						.variable_type,
				g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.arrival_triggers[1]
						.triggers[0]
						.variable,
				(uint8_t)g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.arrival_triggers[1]
						.triggers[0]
						.amount,
				0, ANY_TEAM);
			uint16_t result2 = (uint16_t)mission_evaluate_condition(
				condition2,
				g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.arrival_triggers[1]
						.triggers[1]
						.variable_type,
				g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.arrival_triggers[1]
						.triggers[1]
						.variable,
				(uint8_t)g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.arrival_triggers[1]
						.triggers[1]
						.amount,
				0, ANY_TEAM);
			if ((result1 & result2 & CONDITION_STATE_COMPLETE) !=
			    0) {
				second_pair_state = CONDITION_STATE_COMPLETE;
			} else {
				second_pair_state =
					((result1 | result2) &
					 CONDITION_STATE_FAILED) == 0
						? CONDITION_STATE_PENDING
						: CONDITION_STATE_FAILED;
			}
		}

		uint8_t arrival_state;
		if (g_mission_flight_groups[g_current_flight_group_idx]
			    .fg.arrivals12_or_arrivals34 == 1) {
			if ((((uint8_t)first_pair_state |
			      (uint8_t)second_pair_state) &
			     CONDITION_STATE_COMPLETE) != 0) {
				arrival_state = CONDITION_STATE_COMPLETE;
			} else {
				arrival_state =
					((first_pair_state &
					  second_pair_state) &
					 CONDITION_STATE_FAILED) == 0
						? CONDITION_STATE_PENDING
						: CONDITION_STATE_FAILED;
			}
		} else {
			if ((((uint8_t)first_pair_state &
			      (uint8_t)second_pair_state) &
			     CONDITION_STATE_COMPLETE) != 0) {
				arrival_state = CONDITION_STATE_COMPLETE;
			} else {
				arrival_state =
					((first_pair_state |
					  second_pair_state) &
					 CONDITION_STATE_FAILED) == 0
						? CONDITION_STATE_PENDING
						: CONDITION_STATE_FAILED;
			}
		}
		if ((arrival_state & CONDITION_STATE_FAILED) != 0) {
			XVT_LOG_DEBUG(
				"mission.arrival_trigger_failed fg=%d first=%d second=%d",
				(int)g_current_flight_group_idx,
				(int)first_pair_state, (int)second_pair_state);
			g_mission_fg_stats[g_current_flight_group_idx]
				.arrival_enabled = 0;
		}
	}

	int team_index;
	for (g_current_flight_group_idx = 0;
	     g_current_flight_group_idx < g_mission_header.num_flight_groups;
	     ++g_current_flight_group_idx) {
		g_mission_fg_stats[g_current_flight_group_idx].has_arrived = 0;
		g_mission_fg_stats[g_current_flight_group_idx]
			.arrival_delay_pending = 0;
		g_mission_fg_stats[g_current_flight_group_idx].waves_remaining =
			0;
		g_mission_fg_stats[g_current_flight_group_idx]
			.spawned_craft_count = 0;
		g_mission_fg_stats[g_current_flight_group_idx]
			.arrival_delay_timer = 0;
		int16_t number_of_craft =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.number_of_craft;
		if (g_object_type_table
			    [g_craft_type_to_object_type
				     [g_mission_flight_groups
					      [g_current_flight_group_idx]
						      .fg.craft_type]]
				    .genus_id == CRAFT_GENUS_MINE) {
			number_of_craft *= number_of_craft;
		}
		if (g_mission_fg_stats[g_current_flight_group_idx]
				    .arrival_enabled != 0 ||
		    g_mission_flight_groups[g_current_flight_group_idx]
				    .player_owner_idx != -1) {
			g_mission_fg_stats[g_current_flight_group_idx]
				.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] =
				(uint16_t)(number_of_craft *
					   (g_mission_flight_groups
						    [g_current_flight_group_idx]
							    .fg
							    .number_of_waves +
					    1));
			if (g_mission_flight_groups[g_current_flight_group_idx]
					    .fg.random_special_cargo_craft !=
				    0 ||
			    g_mission_flight_groups[g_current_flight_group_idx]
					    .fg.special_cargo_craft <
				    g_mission_flight_groups
					    [g_current_flight_group_idx]
						    .fg.number_of_craft) {
				g_mission_fg_stats[g_current_flight_group_idx]
					.special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_TOTAL] =
					g_mission_flight_groups
						[g_current_flight_group_idx]
							.fg.number_of_waves +
					1;
			} else {
				g_mission_fg_stats[g_current_flight_group_idx]
					.special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_TOTAL] =
					0;
			}
		} else {
			g_mission_fg_stats[g_current_flight_group_idx]
				.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 0;
			g_mission_fg_stats[g_current_flight_group_idx]
				.special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_TOTAL] = 0;
		}
		for (int outcome_index = FLIGHT_GROUP_OUTCOME_ARRIVED;
		     outcome_index < FLIGHT_GROUP_OUTCOME_COUNT;
		     ++outcome_index) {
			g_mission_fg_stats[g_current_flight_group_idx]
				.outcome_count[outcome_index] = 0;
			g_mission_fg_stats[g_current_flight_group_idx]
				.special_cargo_outcome[outcome_index] = 0;
		}
		for (team_index = 0; team_index < TEAM_COUNT; ++team_index) {
			g_mission_fg_stats[g_current_flight_group_idx]
				.team_inspected[team_index] = 0;
			g_mission_fg_stats[g_current_flight_group_idx]
				.team_special_cargo_inspected[team_index] = 0;
			g_mission_fg_stats[g_current_flight_group_idx]
				.team_uninspected_lost[team_index] = 0;
			g_mission_fg_stats[g_current_flight_group_idx]
				.team_special_cargo_uninspected_lost
					[team_index] = 0;
			g_mission_fg_stats[g_current_flight_group_idx]
				.team_captured_departed_count[team_index] = 0;
			g_mission_fg_stats[g_current_flight_group_idx]
				.team_special_cargo_captured_departed
					[team_index] = 0;
			g_mission_fg_stats[g_current_flight_group_idx]
				.team_uncaptured_lost[team_index] = 0;
			g_mission_fg_stats[g_current_flight_group_idx]
				.team_special_cargo_uncaptured_lost
					[team_index] = 0;
			g_mission_fg_stats[g_current_flight_group_idx]
				.team_event_extra[0][team_index] = 0;
			g_mission_fg_stats[g_current_flight_group_idx]
				.team_event_extra[1][team_index] = 0;
			g_mission_fg_stats[g_current_flight_group_idx]
				.team_event_extra[2][team_index] = 0;
			g_mission_fg_stats[g_current_flight_group_idx]
				.team_event_extra[3][team_index] = 0;
		}
		for (int goal_state_index = 0;
		     goal_state_index < FLIGHT_GROUP_GOAL_STATE_COUNT;
		     goal_state_index += GOAL_STATE_BATCH_SIZE) {
			g_mission_fg_stats[g_current_flight_group_idx]
				.goal_state[goal_state_index] =
				CONDITION_STATE_PENDING;
			g_mission_fg_stats[g_current_flight_group_idx]
				.goal_state[goal_state_index + 1] =
				CONDITION_STATE_PENDING;
			g_mission_fg_stats[g_current_flight_group_idx]
				.goal_state[goal_state_index + 2] =
				CONDITION_STATE_PENDING;
			g_mission_fg_stats[g_current_flight_group_idx]
				.goal_state[goal_state_index + 3] =
				CONDITION_STATE_PENDING;
			g_mission_fg_stats[g_current_flight_group_idx]
				.goal_state[goal_state_index + 4] =
				CONDITION_STATE_PENDING;
			g_mission_fg_stats[g_current_flight_group_idx]
				.goal_state[goal_state_index + 5] =
				CONDITION_STATE_PENDING;
			g_mission_fg_stats[g_current_flight_group_idx]
				.goal_state[goal_state_index + 6] =
				CONDITION_STATE_PENDING;
			g_mission_fg_stats[g_current_flight_group_idx]
				.goal_state[goal_state_index + 7] =
				CONDITION_STATE_PENDING;
		}

		if (g_mission_flight_groups[g_current_flight_group_idx]
				    .fg.craft_type != CRAFT_SPECIES_UNKNOWN &&
		    (g_mission_fg_stats[g_current_flight_group_idx]
				     .arrival_enabled != 0 ||
		     g_mission_flight_groups[g_current_flight_group_idx]
				     .player_owner_idx != -1) &&
		    g_mission_fg_stats[g_current_flight_group_idx].outcome_count
				    [FLIGHT_GROUP_OUTCOME_TOTAL] != 0) {
			if (g_mission_flight_groups[g_current_flight_group_idx]
					    .player_owner_idx != -1 ||
			    (g_mission_flight_groups[g_current_flight_group_idx]
					     .fg.arrival_triggers[0]
					     .triggers[0]
					     .condition ==
				     MISSION_COND_ALWAYS_TRUE &&
			     g_mission_flight_groups[g_current_flight_group_idx]
					     .fg.arrival_delay_minutes == 0 &&
			     g_mission_flight_groups[g_current_flight_group_idx]
					     .fg.arrival_delay_seconds == 0 &&
			     (g_mission_flight_groups
					      [g_current_flight_group_idx]
						      .fg.player_number == 0 ||
			      g_mission_flight_groups
					      [g_current_flight_group_idx]
						      .player_owner_idx != -1 ||
			      g_mission_flight_groups
					      [g_current_flight_group_idx]
						      .fg
						      .arrive_only_if_human ==
				      0))) {
				mission_start_flight_group_arrival(UINT16_MAX);
			}
			if ((g_object_type_table
				     [g_craft_type_to_object_type
					      [g_mission_flight_groups
						       [g_current_flight_group_idx]
							       .fg.craft_type]]
					     .behavior_flags &
			     MODEL_FLAG_BACKDROP) == 0) {
				team_index =
					g_mission_flight_groups
						[g_current_flight_group_idx]
							.fg.team;
				g_flight_mission_state.runtime
					.team_has_countable_craft[team_index] =
					1;
			}
		}
		XVT_LOG_DEBUG(
			"mission.group_reset fg=%d total=%u cargo=%u enabled=%u arrived=%u",
			(int)g_current_flight_group_idx,
			(unsigned)g_mission_fg_stats[g_current_flight_group_idx]
				.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL],
			(unsigned)g_mission_fg_stats[g_current_flight_group_idx]
				.special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_TOTAL],
			(unsigned)g_mission_fg_stats[g_current_flight_group_idx]
				.arrival_enabled,
			(unsigned)g_mission_fg_stats[g_current_flight_group_idx]
				.has_arrived);
	}

	for (team_index = 0; team_index < TEAM_COUNT; ++team_index) {
		g_flight_mission_state.runtime
			.team_reinforcements_called[team_index] = 0;
		g_flight_mission_state.runtime
			.team_scores[TEAM_SCORE_BONUS][team_index] = 0;
		g_flight_mission_state.runtime
			.team_scores[TEAM_SCORE_MISSION][team_index] = 0;
		g_flight_mission_state.runtime
			.team_mission_completion_time_seconds[team_index] = 0;
		for (int goal_index = 0; goal_index < TEAM_GOAL_COUNT;
		     ++goal_index) {
			g_flight_mission_state.runtime
				.team_global_goal_state[team_index]
						       [goal_index] =
				CONDITION_STATE_PENDING;
			g_flight_mission_state.runtime
				.team_goal_status[team_index][goal_index] = 0;
			for (int trigger_index = 0;
			     trigger_index < GLOBAL_GOAL_TRIGGER_COUNT;
			     ++trigger_index) {
				g_flight_mission_state.runtime
					.global_goal_trigger_counts
						[0][team_index][goal_index]
						[trigger_index] = 0;
				g_flight_mission_state.runtime
					.global_goal_trigger_counts
						[1][team_index][goal_index]
						[trigger_index] = 0;
			}
		}
		g_flight_mission_state.runtime.team_kill_stats[0][team_index] =
			0;
		g_flight_mission_state.runtime.team_kill_stats[1][team_index] =
			0;
		g_flight_mission_state.runtime.team_kill_stats[2][team_index] =
			0;
		g_flight_mission_state.runtime.team_kill_stats[3][team_index] =
			0;
		for (int team_flight_group_index = 0;
		     team_flight_group_index < MISSION_FLIGHT_GROUP_COUNT;
		     ++team_flight_group_index) {
			g_flight_mission_state.runtime
				.team_fg_inspected_captured_counts
					[0][team_index]
					[team_flight_group_index] = 0;
			g_flight_mission_state.runtime
				.team_fg_inspected_captured_counts
					[1][team_index]
					[team_flight_group_index] = 0;
			g_flight_mission_state.runtime.team_fg_designation_code
				[team_index][team_flight_group_index] = 0;
		}
	}

	g_current_flight_group_idx = 0;
	for (; g_current_flight_group_idx < g_mission_header.num_flight_groups;
	     ++g_current_flight_group_idx) {
		uint8_t flight_group_team =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.team;
		for (int role_offset = 0;
		     role_offset <
		     (int)sizeof(
			     g_mission_flight_groups[g_current_flight_group_idx]
				     .fg.craft_role);
		     role_offset += 4) {
			uint8_t selector =
				g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.craft_role[role_offset];
			if (selector == '\0') {
				break;
			}
			uint8_t team_selector = UINT8_MAX;
			if (selector == 'A') {
				team_selector = 10;
			} else if (selector == 'O') {
				team_selector = 11;
			} else if (selector == 'F') {
				team_selector = 12;
			} else if (selector == 'H') {
				team_selector = 13;
			} else if (selector >= '1' && selector <= '9') {
				team_selector = (uint8_t)(selector - '1');
			}
			if (team_selector == UINT8_MAX) {
				continue;
			}

			char role1 =
				g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.craft_role[role_offset + 1];
			char role2 =
				g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.craft_role[role_offset + 2];
			char role3 =
				g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.craft_role[role_offset + 3];
			int designation_code = 0;
			if (role1 == 'C' && role2 == 'O' && role3 == 'M') {
				designation_code = 1;
			} else if (role1 == 'B' && role2 == 'A' &&
				   role3 == 'S') {
				designation_code = 2;
			} else if (role1 == 'S' && role2 == 'T' &&
				   role3 == 'A') {
				designation_code = 3;
			} else if (role1 == 'M' && role2 == 'I' &&
				   role3 == 'S') {
				designation_code = 4;
			} else if (role1 == 'C' && role2 == 'O' &&
				   role3 == 'N') {
				designation_code = 5;
			} else if (role1 == 'S' && role2 == 'T' &&
				   role3 == 'R') {
				designation_code = 6;
			} else if (role1 == 'R' && role2 == 'E' &&
				   role3 == 'L') {
				designation_code = 7;
			} else if (role1 == 'P' && role2 == 'R' &&
				   role3 == 'I') {
				designation_code = 8;
			} else if (role1 == 'S' && role2 == 'E' &&
				   role3 == 'C') {
				designation_code = 9;
			} else if (role1 == 'T' && role2 == 'E' &&
				   role3 == 'R') {
				designation_code = 10;
			} else if (role1 == 'R' && role2 == 'E' &&
				   role3 == 'S') {
				designation_code = 11;
			} else if (role1 == 'M' && role2 == 'A' &&
				   role3 == 'N') {
				designation_code = 12;
			}
			if (designation_code == 0) {
				continue;
			}

			if (team_selector < TEAM_COUNT) {
				g_flight_mission_state.runtime
					.team_fg_designation_code
						[team_selector]
						[g_current_flight_group_idx] =
					designation_code;
			} else {
				for (team_index = 0; team_index < TEAM_COUNT;
				     ++team_index) {
					if (team_selector == 10 ||
					    (team_selector == 11 &&
					     flight_group_team == team_index) ||
					    (team_selector == 12 &&
					     g_mission_teams[flight_group_team]
							     .allies[team_index] !=
						     0) ||
					    (team_selector == 13 &&
					     g_mission_teams[flight_group_team]
							     .allies[team_index] ==
						     0)) {
						g_flight_mission_state.runtime.team_fg_designation_code
							[team_index]
							[g_current_flight_group_idx] =
							designation_code;
					}
				}
			}
			XVT_LOG_DEBUG(
				"mission.group_role fg=%d selector=%d code=%d",
				(int)g_current_flight_group_idx,
				(int)team_selector, designation_code);
		}
	}

	int player_index;
	for (player_index = 0; player_index < PLAYER_COUNT; ++player_index) {
		g_players[player_index].awaiting_new_craft = 0;
		g_players[player_index].target_box_enabled = 1;
		g_players[player_index].current_target_object_idx = -1;
		g_players[player_index].target_cycle_start = -1;
		g_players[player_index].selected_target_component = -1;
		g_players[player_index].targeting_state = -1;
		g_players[player_index].engine_wash_source_obj_idx = -1;
		for (int preset_index = 0;
		     preset_index <
		     (int)(sizeof(g_players[player_index].target_preset_slot) /
			   sizeof(g_players[player_index]
					  .target_preset_slot[0]));
		     ++preset_index) {
			g_players[player_index]
				.target_preset_slot[preset_index] = -1;
		}
		g_players[player_index].engine_wash_strength = 0;
		g_players[player_index].yaw_roll_swap = 0;
		g_players[player_index].smoothed_input_yaw = 0;
		g_players[player_index].smoothed_input_pitch = 0;
		g_players[player_index].saved_key_mods = 0;
		g_players[player_index].key_mods_hold_timer = 0;
		g_players[player_index].hyperspace_phase = 0;
		g_players[player_index].hyperspace_runtime.phase_elapsed_ticks =
			0;
		memset(g_players[player_index].msg_text, 0,
		       sizeof(g_players[player_index].msg_text));
		g_players[player_index].msg_length = 0;
		g_players[player_index].chat_recipient_mode =
			FLIGHT_CHAT_RECIPIENT_INACTIVE;
	}

	g_local_debris_recycle_slot_cursor =
		(uint16_t)g_local_transient_slot_start;
	if (g_hud_cockpit_resources_loaded == 0) {
		hud_load_cockpit_resources();
	}
	g_target_proximity_blink_bit = 0;
	memset(&g_flight_global_countdown_timers, 0,
	       sizeof(g_flight_global_countdown_timers));
	g_render_object_ref_flags = 0;
	g_flight_runtime_state_initialized = 1;
	g_render_object_ref = UINT16_MAX;
	/* team_index is reused here as a word index into each player's transient timers. */
	for (team_index = 0;
	     team_index < (int)(sizeof(struct player_flight_transient_timers) /
				sizeof(uint16_t));
	     ++team_index) {
		for (player_index = 0; player_index < PLAYER_COUNT;
		     ++player_index) {
			((uint16_t *)&g_player_flight_transient_timers
				 [player_index])[team_index] = 0;
		}
	}
	for (player_index = 0; player_index < PLAYER_COUNT; ++player_index) {
		g_players[player_index].pending_action_timer = 0;
		g_players[player_index].beam_fire_cooldown_timer = 0;
	}
	for (player_index = 0; player_index < PLAYER_COUNT; ++player_index) {
		g_players[player_index].view_state.hud_aim_x_snap_state = 0;
		g_players[player_index].view_state.hud_aim_x = 0;
		g_players[player_index].view_state.hud_aim_y = 0;
		g_players[player_index].view_state.external_camera_active = 0;
		g_players[player_index].view_state.camera_distance =
			DEFAULT_CAMERA_DISTANCE;
		g_players[player_index].view_state.player_input_blocked = 0;
		g_players[player_index].view_state.target_camera_active = 0;
		g_players[player_index].view_state.camera_focus_obj_idx =
			(uint16_t)g_players[player_index].object_index;
	}
	g_hud_loaded_panel_set_id = UINT8_MAX;
	for (player_index = 0; player_index < PLAYER_COUNT; ++player_index) {
		g_players[player_index].saved_hud_view_state = HUD_VIEW_FORWARD;
		hud_force_player_view_state(HUD_VIEW_FORWARD, player_index);
	}
	g_action_key = 0;
	g_flight_initial_texture_cache_flush_pending = 1;
	g_flight_display_rebuild_pending = 1;
	g_sim_steps_per_second = DEFAULT_SIM_STEP;
	g_elapsed_ticks = DEFAULT_SIM_STEP;
	g_flight_frame_step_mirror = DEFAULT_SIM_STEP;
	g_input_timestamp += (int)time_consume_elapsed_ticks();
	g_ready_message_queue_count = 0;
	g_input_timestamp = 0;
	for (int message_index = 0; message_index < MISSION_MESSAGE_COUNT;
	     ++message_index) {
		g_flight_mission_state.message_triggered[message_index] = 0;
		g_flight_mission_state.message_delay_countdown[message_index] =
			0;
	}
	g_flight_mission_state.runtime.global_goal_status_unused = 0;
	g_flight_mission_state.mission_end_pending = 0;
	g_flight_mission_state.max_connected_player_count_this_mission = 0;
	g_flight_mission_state.runtime.global_primary_goal_status = 0;
	g_flight_mission_state.runtime.global_bonus_goal_status = 0;
	g_initial_spawn_bind_player_craft_slots = 0;
	XVT_LOG_INFO("mission.runtime_ready objects=%d",
		     g_next_object_signature - 1);
	flight_surface_lock();
}

/* Brings in flight group g_current_flight_group_idx and sets its has_arrived to 1.
 * A craft type without the static flag (0x80) sets waves_remaining to
 * number_of_waves and spawns the first round with
 * mission_spawn_flight_group_wave_craft(craft_ordinal); a static type sets
 * waves_remaining to number_of_waves in a melee mission, else 0, and is placed
 * with mission_spawn_flight_group_static_objects. Always returns 1. */
// FUNCTION: XVT 0x455600
int16_t mission_start_flight_group_arrival(uint16_t craft_ordinal)
{
	enum { STATIC_MODEL_FLAG = 0x80 };

	uint16_t flight_group_index = g_current_flight_group_idx;

	g_mission_fg_stats[flight_group_index].has_arrived = 1;
	uint8_t object_type = g_craft_type_to_object_type
		[g_mission_flight_groups[flight_group_index].fg.craft_type];
	if ((g_object_type_table[object_type].behavior_flags &
	     STATIC_MODEL_FLAG) == 0) {
		g_mission_fg_stats[flight_group_index].waves_remaining =
			g_mission_flight_groups[flight_group_index]
				.fg.number_of_waves;
		mission_spawn_flight_group_wave_craft(craft_ordinal);
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_INFO(
				"mission.group_arrived fg=%d craft=%d kind=\"craft\" ordinal=%d waves=%d arrived=%d total=%d start=%d tick=%d",
				(int)flight_group_index,
				(int)g_mission_flight_groups[flight_group_index]
					.fg.craft_type,
				craft_ordinal == UINT16_MAX
					? -1
					: (int)craft_ordinal,
				(int)g_mission_fg_stats[flight_group_index]
					.waves_remaining,
				(int)g_mission_fg_stats[flight_group_index]
					.outcome_count
						[FLIGHT_GROUP_OUTCOME_ARRIVED],
				(int)g_mission_fg_stats[flight_group_index]
					.outcome_count
						[FLIGHT_GROUP_OUTCOME_TOTAL],
				(int)g_initial_spawn_bind_player_craft_slots,
				g_game_time);
		}
		return 1;
	}

	if (g_mission_header.mission_type == MISSION_TYPE_MELEE) {
		g_mission_fg_stats[flight_group_index].waves_remaining =
			g_mission_flight_groups[flight_group_index]
				.fg.number_of_waves;
	} else {
		g_mission_fg_stats[flight_group_index].waves_remaining = 0;
	}
	mission_spawn_flight_group_static_objects(craft_ordinal);
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO(
			"mission.group_arrived fg=%d craft=%d kind=\"static\" ordinal=%d waves=%d arrived=%d total=%d start=%d tick=%d",
			(int)flight_group_index,
			(int)g_mission_flight_groups[flight_group_index]
				.fg.craft_type,
			craft_ordinal == UINT16_MAX ? -1 : (int)craft_ordinal,
			(int)g_mission_fg_stats[flight_group_index]
				.waves_remaining,
			(int)g_mission_fg_stats[flight_group_index]
				.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			(int)g_mission_fg_stats[flight_group_index]
				.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL],
			(int)g_initial_spawn_bind_player_craft_slots,
			g_game_time);
	}
	return 1;
}

/* Starts flight group arrivals and new rounds. When
 * mission_arrival_trigger_scan_timer is 0 it sets it to SIMULATION_TICKS_PER_SECOND
 * ticks and, for each group not yet arrived with no delay pending that has
 * craft and may arrive or has a player owner, tests its two arrival trigger
 * pairs (departed craft count as destroyed), joined by OR when
 * arrivals12_or_arrivals34 is 1, else AND. When they hold and the group has a
 * player owner or arrive_only_if_human is clear, it sets arrival_delay_pending and
 * arrival_delay_timer, in seconds: the fixed delay plus game_rand_range of the
 * random delay with random variation on, else half of it. For an arrived group
 * with no player owner, rounds left and none of its craft or static objects
 * still present, it skips a player-number group whose team's primary and
 * prevent statuses are 1 and 1, 1 and 2, or 2 and 1; else it stops the arrivals
 * when the departure trigger pair holds or the stop_arriving_when rule applies (1
 * a craft departed, 2 the team's primary goal complete, 3 the primary failed or
 * the prevent status is 1), counting the craft that never arrived as arrived,
 * not departed, not inspected, not disabled, not captured, not attacked and not
 * boarded, and as mothership-dependent or left region; or it spawns the next
 * round with mission_spawn_current_flight_group_wave when
 * mission_has_capacity_for_current_flight_group_wave allows. When
 * mission_arrival_delay_scan_timer is 0 it sets it the same way, counts each
 * pending delay down by one, and at 0 starts the arrival with
 * mission_start_flight_group_arrival when there is capacity. Uses
 * g_current_flight_group_idx as its loop index; also writes g_cur_craft. */
// FUNCTION: XVT 0x4556B0
void mission_update_flight_group_arrivals(void)
{
	enum {
		STATIC_MODEL_FLAG = 0x80,
		GOAL_STATUS_COMPLETE = 1,
		GOAL_STATUS_FAILED = 2,
		STOP_ARRIVING_CRAFT_DEPARTED = 1,
		STOP_ARRIVING_PRIMARY_COMPLETE = 2,
		STOP_ARRIVING_MISSION_FAILED = 3,
	};

	if (g_flight_global_countdown_timers
		    .mission_arrival_trigger_scan_timer == 0) {
		g_flight_global_countdown_timers
			.mission_arrival_trigger_scan_timer =
			SIMULATION_TICKS_PER_SECOND;

		for (g_current_flight_group_idx = 0;
		     g_current_flight_group_idx <
		     g_mission_header.num_flight_groups;
		     ++g_current_flight_group_idx) {
			if (g_mission_fg_stats[g_current_flight_group_idx]
					    .has_arrived == 0 &&
			    g_mission_fg_stats[g_current_flight_group_idx]
					    .arrival_delay_pending == 0) {
				if ((g_mission_fg_stats
						     [g_current_flight_group_idx]
							     .arrival_enabled !=
					     0 ||
				     g_mission_flight_groups
						     [g_current_flight_group_idx]
							     .player_owner_idx !=
					     -1) &&
				    g_mission_fg_stats[g_current_flight_group_idx]
						    .outcome_count
							    [FLIGHT_GROUP_OUTCOME_TOTAL] !=
					    0) {
					char first_trigger_result = (char)
						mission_evaluate_trigger_pair(
							&g_mission_flight_groups
								 [g_current_flight_group_idx]
									 .fg
									 .arrival_triggers
										 [0],
							1);
					char second_trigger_result = (char)
						mission_evaluate_trigger_pair(
							&g_mission_flight_groups
								 [g_current_flight_group_idx]
									 .fg
									 .arrival_triggers
										 [1],
							1);
					char arrival_triggered;
					unsigned int flight_group_idx =
						g_current_flight_group_idx;

					if (g_mission_flight_groups[flight_group_idx]
						    .fg
						    .arrivals12_or_arrivals34 ==
					    1) {
						arrival_triggered =
							first_trigger_result |
							second_trigger_result;
					} else {
						arrival_triggered =
							first_trigger_result &
							second_trigger_result;
					}

					if ((arrival_triggered & 1) != 0 &&
					    (g_mission_flight_groups[flight_group_idx]
							     .player_owner_idx !=
						     -1 ||
					     g_mission_flight_groups[flight_group_idx]
							     .fg
							     .arrive_only_if_human ==
						     0)) {
						int16_t *arrival_delay_timer =
							&g_mission_fg_stats
								 [flight_group_idx]
									 .arrival_delay_timer;
						int16_t fixed_delay_seconds =
							60 * g_mission_flight_groups
									[flight_group_idx]
										.fg
										.arrival_delay_minutes +
							g_mission_flight_groups
								[flight_group_idx]
									.fg
									.arrival_delay_seconds;

						uint16_t random_delay_seconds =
							60 *
							g_mission_flight_groups
								[flight_group_idx]
									.fg
									.arrival_rand_delay_minutes;
						*arrival_delay_timer =
							fixed_delay_seconds;
						random_delay_seconds +=
							g_mission_flight_groups
								[flight_group_idx]
									.fg
									.arrival_rand_delay_seconds;
						uint16_t random_delay;
						if (g_flight_mission_state
							    .random_variation_enabled !=
						    0) {
							random_delay = game_rand_range(
								random_delay_seconds);
						} else {
							random_delay =
								random_delay_seconds >>
								1;
						}

						*arrival_delay_timer =
							fixed_delay_seconds +
							random_delay;
						g_mission_fg_stats
							[g_current_flight_group_idx]
								.arrival_delay_pending =
							1;
						XVT_LOG_DEBUG(
							"mission.arrival_scheduled fg=%d first=%d second=%d either=%d slot=%d delay=%d fixed=%d random=%d range=%d varied=%d tick=%d predicted=%d",
							(int)flight_group_idx,
							(int)first_trigger_result,
							(int)second_trigger_result,
							(int)g_mission_flight_groups
								[flight_group_idx]
									.fg
									.arrivals12_or_arrivals34,
							g_mission_flight_groups
								[flight_group_idx]
									.player_owner_idx,
							(int)*arrival_delay_timer,
							(int)fixed_delay_seconds,
							(int)random_delay,
							(int)random_delay_seconds,
							(int)g_flight_mission_state
								.random_variation_enabled,
							g_game_time,
							g_flight_sim_side_effects_suppressed);
					}
				}
				continue;
			}

			if (g_mission_fg_stats[g_current_flight_group_idx]
					    .arrival_enabled == 0 ||
			    g_mission_fg_stats[g_current_flight_group_idx]
					    .waves_remaining == 0 ||
			    g_mission_fg_stats[g_current_flight_group_idx]
					    .outcome_count
						    [FLIGHT_GROUP_OUTCOME_TOTAL] ==
				    0 ||
			    g_mission_flight_groups[g_current_flight_group_idx]
					    .player_owner_idx != -1) {
				continue;
			}

			if (g_mission_flight_groups[g_current_flight_group_idx]
				    .fg.player_number != 0) {
				uint8_t team =
					g_mission_flight_groups
						[g_current_flight_group_idx]
							.fg.team;
				uint8_t primary_goal_status =
					g_flight_mission_state.runtime
						.team_goal_status[team][0];

				if (primary_goal_status ==
					    GOAL_STATUS_COMPLETE &&
				    g_flight_mission_state.runtime
						    .team_goal_status[team]
								     [1] ==
					    GOAL_STATUS_COMPLETE) {
					continue;
				}
				if (primary_goal_status ==
					    GOAL_STATUS_COMPLETE &&
				    g_flight_mission_state.runtime
						    .team_goal_status[team]
								     [1] ==
					    GOAL_STATUS_FAILED) {
					continue;
				}
				if (primary_goal_status == GOAL_STATUS_FAILED &&
				    g_flight_mission_state.runtime
						    .team_goal_status[team]
								     [1] ==
					    GOAL_STATUS_COMPLETE) {
					continue;
				}
			}

			{
				unsigned int object_index;
				uint8_t object_type = g_craft_type_to_object_type
					[g_mission_flight_groups
						 [g_current_flight_group_idx]
							 .fg.craft_type];
				char wave_objects_absent = 1;

				if ((g_object_type_table[object_type]
					     .behavior_flags &
				     STATIC_MODEL_FLAG) == 0) {
					for (object_index = (unsigned int)
						     g_active_region_object_slot_start;
					     object_index <
					     (unsigned int)
						     g_active_region_craft_object_slot_end;
					     ++object_index) {
						struct object_record *object =
							&g_object_table
								[object_index];

						if (object->object_type != 0 &&
						    object->mobj->family == 0 &&
						    object->flight_group_idx ==
							    g_current_flight_group_idx) {
							g_cur_craft =
								object->mobj
									->p_craft;
							if (g_cur_craft->object_kind !=
								    CRAFT_OBJECT_KIND_BREAKING_UP &&
							    g_cur_craft->object_kind !=
								    CRAFT_OBJECT_KIND_EXPLODING) {
								wave_objects_absent =
									0;
								break;
							}
						}
					}
				} else {
					object_index = (unsigned int)
						g_region_main_object_slot_end;
					unsigned int static_object_end =
						object_index +
						(unsigned int)
							g_region_static_object_slot_count;
					for (; object_index < static_object_end;
					     ++object_index) {
						if (g_object_table[object_index]
								    .object_type !=
							    0 &&
						    g_object_table[object_index]
								    .flight_group_idx ==
							    g_current_flight_group_idx) {
							wave_objects_absent = 0;
							break;
						}
					}
				}

				if (wave_objects_absent != 0) {
					char stop_arriving = 0;

					if (g_mission_flight_groups
							    [g_current_flight_group_idx]
								    .player_owner_idx ==
						    -1 ||
					    g_flight_mission_state
							    .player_flight_group_wave_mode !=
						    CRAFT_WAVES_UNLIMITED) {
						if ((g_mission_flight_groups[g_current_flight_group_idx]
								     .fg
								     .departure_trigger
								     .triggers
									     [0]
								     .condition !=
							     0 ||
						     g_mission_flight_groups[g_current_flight_group_idx]
								     .fg
								     .departure_trigger
								     .triggers
									     [1]
								     .condition !=
							     0) &&
						    (mission_evaluate_trigger_pair(
							     &g_mission_flight_groups
								      [g_current_flight_group_idx]
									      .fg
									      .departure_trigger,
							     0) &
						     1) != 0) {
							stop_arriving = 1;
						}

						uint8_t stop_condition =
							g_mission_flight_groups
								[g_current_flight_group_idx]
									.fg
									.stop_arriving_when;
						if (stop_condition ==
							    STOP_ARRIVING_CRAFT_DEPARTED &&
						    g_mission_fg_stats[g_current_flight_group_idx]
								    .outcome_count
									    [FLIGHT_GROUP_OUTCOME_DEPARTED] !=
							    0) {
							stop_arriving = 1;
						}
						if (stop_condition ==
							    STOP_ARRIVING_PRIMARY_COMPLETE &&
						    g_flight_mission_state
								    .runtime
								    .team_goal_status
									    [g_mission_flight_groups
										     [g_current_flight_group_idx]
											     .fg
											     .team]
									    [0] ==
							    GOAL_STATUS_COMPLETE) {
							stop_arriving = 1;
						}
						if (stop_condition ==
							    STOP_ARRIVING_MISSION_FAILED &&
						    (g_flight_mission_state
								     .runtime
								     .team_goal_status
									     [g_mission_flight_groups
										      [g_current_flight_group_idx]
											      .fg
											      .team]
									     [0] ==
							     GOAL_STATUS_FAILED ||
						     g_flight_mission_state
								     .runtime
								     .team_goal_status
									     [g_mission_flight_groups
										      [g_current_flight_group_idx]
											      .fg
											      .team]
									     [1] ==
							     GOAL_STATUS_COMPLETE)) {
							stop_arriving = 1;
						}
					}

					if (stop_arriving != 0) {
						uint16_t unavailable_craft_count =
							g_mission_fg_stats[g_current_flight_group_idx]
								.outcome_count
									[FLIGHT_GROUP_OUTCOME_TOTAL] -
							g_mission_fg_stats[g_current_flight_group_idx]
								.outcome_count
									[FLIGHT_GROUP_OUTCOME_ARRIVED];
						uint8_t unavailable_special_cargo_count =
							g_mission_fg_stats[g_current_flight_group_idx]
								.special_cargo_outcome
									[FLIGHT_GROUP_OUTCOME_TOTAL] -
							g_mission_fg_stats[g_current_flight_group_idx]
								.special_cargo_outcome
									[FLIGHT_GROUP_OUTCOME_ARRIVED];

						g_mission_fg_stats[g_current_flight_group_idx]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_ARRIVED] +=
							unavailable_craft_count;
						g_mission_fg_stats[g_current_flight_group_idx]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] +=
							unavailable_craft_count;
						g_mission_fg_stats[g_current_flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] +=
							unavailable_special_cargo_count;
						g_mission_fg_stats[g_current_flight_group_idx]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_NOT_INSPECTED] +=
							unavailable_craft_count;
						g_mission_fg_stats[g_current_flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_NOT_INSPECTED] +=
							unavailable_special_cargo_count;
						g_mission_fg_stats[g_current_flight_group_idx]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_NOT_DISABLED] +=
							unavailable_craft_count;
						g_mission_fg_stats[g_current_flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_NOT_DISABLED] +=
							unavailable_special_cargo_count;
						g_mission_fg_stats[g_current_flight_group_idx]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED] +=
							unavailable_craft_count;
						g_mission_fg_stats[g_current_flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED] +=
							unavailable_special_cargo_count;
						g_mission_fg_stats[g_current_flight_group_idx]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED] +=
							unavailable_craft_count;
						g_mission_fg_stats[g_current_flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED] +=
							unavailable_special_cargo_count;
						g_mission_fg_stats[g_current_flight_group_idx]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_NOT_BOARDED] +=
							unavailable_craft_count;
						g_mission_fg_stats[g_current_flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_NOT_BOARDED] +=
							unavailable_special_cargo_count;
						g_mission_fg_stats
							[g_current_flight_group_idx]
								.waves_remaining =
							0;

						if (g_mission_flight_groups[g_current_flight_group_idx]
								    .fg
								    .departure_method !=
							    0 ||
						    g_mission_flight_groups[g_current_flight_group_idx]
								    .fg
								    .alternate_mothership_used !=
							    0) {
							g_mission_fg_stats[g_current_flight_group_idx]
								.outcome_count
									[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT] +=
								unavailable_craft_count;
						} else {
							g_mission_fg_stats[g_current_flight_group_idx]
								.outcome_count
									[FLIGHT_GROUP_OUTCOME_LEFT_REGION] +=
								unavailable_craft_count;
						}
						if (g_flight_sim_side_effects_suppressed ==
						    0) {
							XVT_LOG_INFO(
								"mission.group_arrivals_stopped fg=%d rule=%d count=%d special=%d tick=%d",
								(int)g_current_flight_group_idx,
								(int)g_mission_flight_groups
									[g_current_flight_group_idx]
										.fg
										.stop_arriving_when,
								(int)unavailable_craft_count,
								(int)unavailable_special_cargo_count,
								g_game_time);
						}
						XVT_LOG_DEBUG(
							"mission.arrivals_stop_reason fg=%d team=%d departed=%d primary=%d prevent=%d counted=\"%s\" predicted=%d",
							(int)g_current_flight_group_idx,
							(int)g_mission_flight_groups
								[g_current_flight_group_idx]
									.fg
									.team,
							(int)g_mission_fg_stats[g_current_flight_group_idx]
								.outcome_count
									[FLIGHT_GROUP_OUTCOME_DEPARTED],
							(int)g_flight_mission_state
								.runtime
								.team_goal_status
									[g_mission_flight_groups
										 [g_current_flight_group_idx]
											 .fg
											 .team]
									[0],
							(int)g_flight_mission_state
								.runtime
								.team_goal_status
									[g_mission_flight_groups
										 [g_current_flight_group_idx]
											 .fg
											 .team]
									[1],
							(g_mission_flight_groups[g_current_flight_group_idx]
									 .fg
									 .departure_method !=
								 0 ||
							 g_mission_flight_groups[g_current_flight_group_idx]
									 .fg
									 .alternate_mothership_used !=
								 0)
								? "mothership_dependent"
								: "left_region",
							g_flight_sim_side_effects_suppressed);
					} else if (
						mission_has_capacity_for_current_flight_group_wave() !=
						0) {
						mission_spawn_current_flight_group_wave();
					}
				}
			}
		}
	}

	if (g_flight_global_countdown_timers.mission_arrival_delay_scan_timer ==
	    0) {
		g_flight_global_countdown_timers
			.mission_arrival_delay_scan_timer =
			SIMULATION_TICKS_PER_SECOND;

		for (g_current_flight_group_idx = 0;
		     g_current_flight_group_idx <
		     g_mission_header.num_flight_groups;
		     ++g_current_flight_group_idx) {
			if (g_mission_fg_stats[g_current_flight_group_idx]
					    .has_arrived == 0 &&
			    g_mission_fg_stats[g_current_flight_group_idx]
					    .arrival_delay_pending == 1) {
				if (g_mission_fg_stats
					    [g_current_flight_group_idx]
						    .arrival_delay_timer == 0) {
					if (mission_has_capacity_for_current_flight_group_wave() !=
					    0) {
						mission_start_flight_group_arrival(
							UINT16_MAX);
					}
				} else {
					--g_mission_fg_stats
						  [g_current_flight_group_idx]
							  .arrival_delay_timer;
					XVT_LOG_DEBUG(
						"mission.arrival_countdown fg=%d left=%d predicted=%d",
						(int)g_current_flight_group_idx,
						(int)g_mission_fg_stats
							[g_current_flight_group_idx]
								.arrival_delay_timer,
						g_flight_sim_side_effects_suppressed);
				}
			}
		}
	}
}

/* Sends a flight group's next round once its current one is gone. Returns at
 * once when it has no rounds left or no craft, while any of its craft in the
 * active region is not breaking up or exploding, or when its team's primary and
 * prevent statuses are 1 and 1, 1 and 2, or 2 and 1. Unless
 * player_flight_group_wave_mode is CRAFT_WAVES_UNLIMITED, it stops the arrivals
 * when the departure trigger pair or the stop_arriving_when rule holds, closing
 * the craft that never arrived into its counts as
 * mission_update_flight_group_arrivals does, and returns. Otherwise it sets
 * g_current_flight_group_idx to the group and, when fewer craft slots are free
 * than the group has craft, frees unowned explosion objects and then unowned
 * craft that are breaking up or exploding, recording each as destroyed with
 * mission_record_craft_outcome; then spawns the round with
 * mission_spawn_current_flight_group_wave. */
// FUNCTION: XVT 0x455C80
void mission_process_flight_group_wave_completion(uint16_t flight_group_idx)
{
	enum {
		GOAL_STATUS_COMPLETE = 1,
		GOAL_STATUS_FAILED = 2,
		STOP_ARRIVING_CRAFT_DEPARTED = 1,
		STOP_ARRIVING_PRIMARY_COMPLETE = 2,
		STOP_ARRIVING_MISSION_FAILED = 3,
	};

	if (g_mission_fg_stats[flight_group_idx].waves_remaining == 0 ||
	    g_mission_fg_stats[flight_group_idx]
			    .outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] == 0) {
		return;
	}

	char wave_complete = 1;
	unsigned int object_index;
	for (object_index = (unsigned int)g_active_region_object_slot_start;
	     object_index < (unsigned int)g_active_region_craft_object_slot_end;
	     ++object_index) {
		struct object_record *object = &g_object_table[object_index];

		if (object->object_type != 0 && object->mobj->family == 0 &&
		    object->flight_group_idx == flight_group_idx &&
		    object->mobj->p_craft->object_kind !=
			    CRAFT_OBJECT_KIND_BREAKING_UP &&
		    object->mobj->p_craft->object_kind !=
			    CRAFT_OBJECT_KIND_EXPLODING) {
			wave_complete = 0;
		}
	}
	if (wave_complete == 0) {
		return;
	}

	if (g_flight_mission_state.runtime.team_goal_status
			    [g_mission_flight_groups[flight_group_idx].fg.team]
			    [0] == GOAL_STATUS_COMPLETE &&
	    g_flight_mission_state.runtime.team_goal_status
			    [g_mission_flight_groups[flight_group_idx].fg.team]
			    [1] == GOAL_STATUS_COMPLETE) {
		XVT_LOG_DEBUG(
			"mission.wave_held fg=%d team=%d primary=%d prevent=%d predicted=%d",
			(int)flight_group_idx,
			(int)g_mission_flight_groups[flight_group_idx].fg.team,
			(int)g_flight_mission_state.runtime.team_goal_status
				[g_mission_flight_groups[flight_group_idx]
					 .fg.team][0],
			(int)g_flight_mission_state.runtime.team_goal_status
				[g_mission_flight_groups[flight_group_idx]
					 .fg.team][1],
			g_flight_sim_side_effects_suppressed);
		return;
	}
	if (g_flight_mission_state.runtime.team_goal_status
			    [g_mission_flight_groups[flight_group_idx].fg.team]
			    [0] == GOAL_STATUS_COMPLETE &&
	    g_flight_mission_state.runtime.team_goal_status
			    [g_mission_flight_groups[flight_group_idx].fg.team]
			    [1] == GOAL_STATUS_FAILED) {
		XVT_LOG_DEBUG(
			"mission.wave_held fg=%d team=%d primary=%d prevent=%d predicted=%d",
			(int)flight_group_idx,
			(int)g_mission_flight_groups[flight_group_idx].fg.team,
			(int)g_flight_mission_state.runtime.team_goal_status
				[g_mission_flight_groups[flight_group_idx]
					 .fg.team][0],
			(int)g_flight_mission_state.runtime.team_goal_status
				[g_mission_flight_groups[flight_group_idx]
					 .fg.team][1],
			g_flight_sim_side_effects_suppressed);
		return;
	}
	if (g_flight_mission_state.runtime.team_goal_status
			    [g_mission_flight_groups[flight_group_idx].fg.team]
			    [0] == GOAL_STATUS_FAILED &&
	    g_flight_mission_state.runtime.team_goal_status
			    [g_mission_flight_groups[flight_group_idx].fg.team]
			    [1] == GOAL_STATUS_COMPLETE) {
		XVT_LOG_DEBUG(
			"mission.wave_held fg=%d team=%d primary=%d prevent=%d predicted=%d",
			(int)flight_group_idx,
			(int)g_mission_flight_groups[flight_group_idx].fg.team,
			(int)g_flight_mission_state.runtime.team_goal_status
				[g_mission_flight_groups[flight_group_idx]
					 .fg.team][0],
			(int)g_flight_mission_state.runtime.team_goal_status
				[g_mission_flight_groups[flight_group_idx]
					 .fg.team][1],
			g_flight_sim_side_effects_suppressed);
		return;
	}

	{
		int stop_arriving = 0;

		if (g_flight_mission_state.player_flight_group_wave_mode !=
		    CRAFT_WAVES_UNLIMITED) {
			if ((g_mission_flight_groups[flight_group_idx]
					     .fg.departure_trigger.triggers[0]
					     .condition != 0 ||
			     g_mission_flight_groups[flight_group_idx]
					     .fg.departure_trigger.triggers[1]
					     .condition != 0) &&
			    (mission_evaluate_trigger_pair(
				     &g_mission_flight_groups[flight_group_idx]
					      .fg.departure_trigger,
				     0) &
			     1) != 0) {
				stop_arriving = 1;
			}
			uint8_t stop_condition =
				g_mission_flight_groups[flight_group_idx]
					.fg.stop_arriving_when;
			if (stop_condition == STOP_ARRIVING_CRAFT_DEPARTED &&
			    g_mission_fg_stats[flight_group_idx].outcome_count
					    [FLIGHT_GROUP_OUTCOME_DEPARTED] !=
				    0) {
				stop_arriving = 1;
			}
			if (stop_condition == STOP_ARRIVING_PRIMARY_COMPLETE &&
			    g_flight_mission_state.runtime.team_goal_status
					    [g_mission_flight_groups
						     [flight_group_idx]
							     .fg.team][0] ==
				    GOAL_STATUS_COMPLETE) {
				stop_arriving = 1;
			}
			if (stop_condition == STOP_ARRIVING_MISSION_FAILED &&
			    (g_flight_mission_state.runtime.team_goal_status
					     [g_mission_flight_groups
						      [flight_group_idx]
							      .fg.team][0] ==
				     GOAL_STATUS_FAILED ||
			     g_flight_mission_state.runtime.team_goal_status
					     [g_mission_flight_groups
						      [flight_group_idx]
							      .fg.team][1] ==
				     GOAL_STATUS_COMPLETE)) {
				stop_arriving = 1;
			}
		}

		if (stop_arriving != 0) {
			uint16_t unavailable_craft_count =
				g_mission_fg_stats[flight_group_idx]
					.outcome_count
						[FLIGHT_GROUP_OUTCOME_TOTAL] -
				g_mission_fg_stats[flight_group_idx]
					.outcome_count
						[FLIGHT_GROUP_OUTCOME_ARRIVED];
			uint8_t unavailable_special_cargo_count =
				g_mission_fg_stats[flight_group_idx]
					.special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_TOTAL] -
				g_mission_fg_stats[flight_group_idx]
					.special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_ARRIVED];

			g_mission_fg_stats[flight_group_idx]
				.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED] +=
				unavailable_craft_count;
			g_mission_fg_stats[flight_group_idx].outcome_count
				[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] +=
				unavailable_craft_count;
			g_mission_fg_stats[flight_group_idx]
				.special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] +=
				unavailable_special_cargo_count;
			g_mission_fg_stats[flight_group_idx].outcome_count
				[FLIGHT_GROUP_OUTCOME_NOT_INSPECTED] +=
				unavailable_craft_count;
			g_mission_fg_stats[flight_group_idx]
				.special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_NOT_INSPECTED] +=
				unavailable_special_cargo_count;
			g_mission_fg_stats[flight_group_idx].outcome_count
				[FLIGHT_GROUP_OUTCOME_NOT_DISABLED] +=
				unavailable_craft_count;
			g_mission_fg_stats[flight_group_idx]
				.special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_NOT_DISABLED] +=
				unavailable_special_cargo_count;
			g_mission_fg_stats[flight_group_idx].outcome_count
				[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED] +=
				unavailable_craft_count;
			g_mission_fg_stats[flight_group_idx]
				.special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED] +=
				unavailable_special_cargo_count;
			g_mission_fg_stats[flight_group_idx].outcome_count
				[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED] +=
				unavailable_craft_count;
			g_mission_fg_stats[flight_group_idx]
				.special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_NOT_ATTACKED] +=
				unavailable_special_cargo_count;
			g_mission_fg_stats[flight_group_idx].outcome_count
				[FLIGHT_GROUP_OUTCOME_NOT_BOARDED] +=
				unavailable_craft_count;
			g_mission_fg_stats[flight_group_idx]
				.special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_NOT_BOARDED] +=
				unavailable_special_cargo_count;
			g_mission_fg_stats[flight_group_idx].waves_remaining =
				0;
			if (g_mission_flight_groups[flight_group_idx]
					    .fg.departure_method != 0 ||
			    g_mission_flight_groups[flight_group_idx]
					    .fg.alternate_mothership_used !=
				    0) {
				g_mission_fg_stats[flight_group_idx].outcome_count
					[FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT] +=
					unavailable_craft_count;
			} else {
				g_mission_fg_stats[flight_group_idx].outcome_count
					[FLIGHT_GROUP_OUTCOME_LEFT_REGION] +=
					unavailable_craft_count;
			}
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_INFO(
					"mission.group_arrivals_stopped fg=%d rule=%d count=%d special=%d tick=%d",
					(int)flight_group_idx,
					(int)g_mission_flight_groups
						[flight_group_idx]
							.fg.stop_arriving_when,
					(int)unavailable_craft_count,
					(int)unavailable_special_cargo_count,
					g_game_time);
			}
			XVT_LOG_DEBUG(
				"mission.arrivals_stop_reason fg=%d team=%d departed=%d primary=%d prevent=%d counted=\"%s\" predicted=%d",
				(int)flight_group_idx,
				(int)g_mission_flight_groups[flight_group_idx]
					.fg.team,
				(int)g_mission_fg_stats[flight_group_idx]
					.outcome_count
						[FLIGHT_GROUP_OUTCOME_DEPARTED],
				(int)g_flight_mission_state.runtime
					.team_goal_status
						[g_mission_flight_groups
							 [flight_group_idx]
								 .fg.team][0],
				(int)g_flight_mission_state.runtime
					.team_goal_status
						[g_mission_flight_groups
							 [flight_group_idx]
								 .fg.team][1],
				(g_mission_flight_groups[flight_group_idx]
						 .fg.departure_method != 0 ||
				 g_mission_flight_groups[flight_group_idx]
						 .fg
						 .alternate_mothership_used !=
					 0)
					? "mothership_dependent"
					: "left_region",
				g_flight_sim_side_effects_suppressed);
			return;
		}
	}

	g_current_flight_group_idx = flight_group_idx;
	{
		unsigned int free_object_slot_count = 0;

		for (object_index =
			     (unsigned int)g_active_region_object_slot_start;
		     object_index <
		     (unsigned int)g_active_region_craft_object_slot_end;
		     ++object_index) {
			if (g_object_table[object_index].object_type == 0) {
				++free_object_slot_count;
			}
		}

		unsigned int number_of_craft =
			g_mission_flight_groups[flight_group_idx]
				.fg.number_of_craft;
		if (free_object_slot_count < number_of_craft) {
			unsigned int slots_to_free =
				number_of_craft - free_object_slot_count;

			for (object_index = (unsigned int)
				     g_active_region_object_slot_start;
			     object_index <
			     (unsigned int)
				     g_active_region_craft_object_slot_end;
			     ++object_index) {
				struct object_record *object =
					&g_object_table[object_index];

				if (object->object_type != 0 &&
				    object->genus_id == CRAFT_GENUS_EXPLOSION &&
				    object->player_owner_idx == -1) {
					object->object_type = 0;
					mission_record_craft_outcome(
						object_index,
						g_object_table[object_index]
							.flight_group_idx,
						FLIGHT_GROUP_OUTCOME_DESTROYED);
					if (--slots_to_free == 0) {
						break;
					}
				}
			}

			if (slots_to_free != 0) {
				for (object_index = (unsigned int)
					     g_active_region_object_slot_start;
				     object_index <
				     (unsigned int)
					     g_active_region_craft_object_slot_end;
				     ++object_index) {
					struct object_record *object =
						&g_object_table[object_index];

					if (object->object_type != 0 &&
					    object->mobj->family == 0 &&
					    object->player_owner_idx == -1 &&
					    (object->mobj->p_craft
							     ->object_kind ==
						     CRAFT_OBJECT_KIND_BREAKING_UP ||
					     object->mobj->p_craft
							     ->object_kind ==
						     CRAFT_OBJECT_KIND_EXPLODING)) {
						struct craft_data *craft =
							object->mobj->p_craft;

						object->object_type = 0;
						craft_free_linked_objects(
							craft);
						mission_record_craft_outcome(
							object_index,
							g_object_table[object_index]
								.flight_group_idx,
							FLIGHT_GROUP_OUTCOME_DESTROYED);
						if (--slots_to_free == 0) {
							break;
						}
					}
				}
			}
			XVT_LOG_DEBUG(
				"mission.wave_room_made fg=%d needed=%u free=%u freed=%u missing=%u predicted=%d",
				(int)flight_group_idx, number_of_craft,
				free_object_slot_count,
				number_of_craft - free_object_slot_count -
					slots_to_free,
				slots_to_free,
				g_flight_sim_side_effects_suppressed);
		}
	}

	mission_spawn_current_flight_group_wave();
}

/* Spawns the next round of flight group g_current_flight_group_idx: craft with
 * mission_spawn_flight_group_wave_craft, a static type (flag 0x80) with
 * mission_spawn_flight_group_static_objects. Then takes one from its waves_remaining
 * when that is above 0, except for a player-number group while
 * player_flight_group_wave_mode is CRAFT_WAVES_UNLIMITED. */
// FUNCTION: XVT 0x456040
void mission_spawn_current_flight_group_wave(void)
{
	enum {
		STATIC_MODEL_FLAG = 0x80,
	};

	uint8_t object_type = g_craft_type_to_object_type
		[g_mission_flight_groups[g_current_flight_group_idx]
			 .fg.craft_type];
	if ((g_object_type_table[object_type].behavior_flags &
	     STATIC_MODEL_FLAG) == 0) {
		mission_spawn_flight_group_wave_craft(UINT16_MAX);
	} else {
		mission_spawn_flight_group_static_objects(UINT16_MAX);
	}

	uint8_t *waves_remaining =
		&g_mission_fg_stats[g_current_flight_group_idx].waves_remaining;
	uint8_t remaining_wave_count = *waves_remaining;
	if (remaining_wave_count != 0) {
		if (g_mission_flight_groups[g_current_flight_group_idx]
			    .fg.player_number != 0) {
			if (g_flight_mission_state
				    .player_flight_group_wave_mode !=
			    CRAFT_WAVES_UNLIMITED) {
				*waves_remaining = remaining_wave_count - 1;
			}
		} else {
			*waves_remaining = remaining_wave_count - 1;
		}
	}
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO(
			"mission.wave_arrived fg=%d craft=%d kind=\"%s\" waves=%d arrived=%d total=%d tick=%d",
			(int)g_current_flight_group_idx,
			(int)g_mission_flight_groups[g_current_flight_group_idx]
				.fg.craft_type,
			(g_object_type_table[object_type].behavior_flags &
			 STATIC_MODEL_FLAG) == 0
				? "craft"
				: "static",
			(int)*waves_remaining,
			(int)g_mission_fg_stats[g_current_flight_group_idx]
				.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			(int)g_mission_fg_stats[g_current_flight_group_idx]
				.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL],
			g_game_time);
	}
}

/* Tells whether flight group g_current_flight_group_idx's next round fits: returns
 * 1 when the active region has at least number_of_craft free craft slots, or
 * always for a static type (flag 0x80); else 0. */
// FUNCTION: XVT 0x4560F0
int mission_has_capacity_for_current_flight_group_wave(void)
{
	uint16_t object_type = g_craft_type_to_object_type
		[g_mission_flight_groups[g_current_flight_group_idx]
			 .fg.craft_type];
	if ((g_object_type_table[object_type].behavior_flags & 0x80) == 0) {
		unsigned int free_slot_count = 0;
		for (unsigned int object_index =
			     (unsigned int)g_active_region_object_slot_start;
		     object_index <
		     (unsigned int)g_active_region_craft_object_slot_end;
		     ++object_index) {
			if (g_object_table[object_index].object_type == 0) {
				++free_slot_count;
			}
		}
		if (g_flight_sim_side_effects_suppressed == 0 &&
		    (free_slot_count <
		     (unsigned int)
			     g_mission_flight_groups[g_current_flight_group_idx]
				     .fg.number_of_craft)) {
			XVT_LOG_DEBUG(
				"mission.wave_no_room fg=%d needed=%d free=%u tick=%d",
				(int)g_current_flight_group_idx,
				(int)g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.number_of_craft,
				free_slot_count, g_game_time);
		}

		return free_slot_count >=
		       g_mission_flight_groups[g_current_flight_group_idx]
			       .fg.number_of_craft;
	}

	return 1;
}

/* Spawns one round of flight group g_current_flight_group_idx, or only craft
 * craft_ordinal when that is not UINT16_MAX. It first sets the g_spawn globals
 * mission_init_flight_group_object_slot reads. After the mission clock has started,
 * a whole round arriving from a mothership (arrival_method set, not in proving
 * grounds) starts at the mothership's inside hangar point facing its outside
 * point, in formation 6 when the group has more than 3 craft; it returns 0 when
 * no craft of the mothership's group with leader_obj_idx UINT8_MAX is present.
 * Else the round starts at mission point 1 (in melee, when team 0 has one
 * player group, the point of a player group picked by round and player number;
 * a player-owned group moved at random by up to 0x7FFF in x and y; in a version
 * 14 file, a player group's point 2 when enabled), faces mission point 5 when
 * enabled, else yaw 0 and pitch 0x4000, and an unowned whole round arriving by
 * hyperspace after the start is moved 8 times 65,535 units back against its
 * heading and marked with g_spawn_out_of_hyperspace_flag. It copies the group's
 * IFF, team, statuses, AI level and genus. Then for each ordinal of the round,
 * or the one asked, while the group's arrived count is below its total, it
 * calls mission_init_flight_group_object_slot, returning 0 when that finds no slot,
 * and counts the craft (and the special cargo craft) as arrived. A whole round
 * after the start is announced with msg_reportfgcreation. Returns 1. Writes the
 * g_spawn globals, g_world_loc_x, g_world_loc_y, g_world_loc_z, g_cur_craft,
 * trig2_xyangle, trig2_pitch and g_mission_fg_stats arrived counts. */
// FUNCTION: XVT 0x456190
int16_t mission_spawn_flight_group_wave_craft(uint16_t craft_ordinal)
{
	enum {
		TEAM_COUNT = 10,
		PLAYER_NUMBER_COUNT = 8,
		MISSION_POINT_1 = 0x8000,
		MISSION_POINT_2 = 0x8001,
		MISSION_POINT_5 = 0x8004,
		DEFAULT_SPAWN_PITCH = 0x4000,
		HYPERSPACE_REVERSE_ANGLE = 0x8000,
		HYPERSPACE_DISTANCE = UINT16_MAX,
		HYPERSPACE_OFFSET_SCALE = 8,
		RANDOM_PLAYER_OFFSET_MASK = 0x7FFF,
		QUICK_START_FORMATION_CENTER = 3,
		LARGE_MOTHERSHIP_FORMATION = 6,
		LARGE_MOTHERSHIP_FORMATION_THRESHOLD = 3,
		MISSION_VERSION_14 = 14,
	};

	uint8_t mission_started = g_mission_elapsed_clock.minutes;
	mission_started |= g_mission_elapsed_clock.seconds;
	mission_started |= g_mission_elapsed_clock.hours;
	g_spawn_out_of_hyperspace_flag = 0;
	g_spawn_from_mothership_flag = 0;

	if (g_mission_flight_groups[g_current_flight_group_idx]
			    .fg.arrival_method != 0 &&
	    mission_started != 0 &&
	    g_flight_mission_state.proving_grounds_mode_active == 0 &&
	    craft_ordinal == UINT16_MAX) {
		uint16_t mothership_object_index = 0;
		int16_t found_mothership = 0;
		int16_t arrival_mothership =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.arrival_mothership;

		for (uint16_t object_index =
			     (uint16_t)g_active_region_object_slot_start;
		     (int)object_index < g_active_region_craft_object_slot_end;
		     ++object_index) {
			if (g_object_table[object_index].object_type == 0) {
				continue;
			}
			g_cur_craft =
				g_object_table[object_index].mobj->p_craft;
			if (g_object_table[object_index].flight_group_idx ==
				    arrival_mothership &&
			    g_cur_craft->leader_obj_idx == UINT8_MAX) {
				mothership_object_index = object_index;
				found_mothership = 1;
				break;
			}
		}
		if (found_mothership == 0) {
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_DEBUG(
					"mission.mothership_missing fg=%d mothership=%d tick=%d",
					(int)g_current_flight_group_idx,
					(int)arrival_mothership, g_game_time);
			}
			return 0;
		}

		{
			g_cur_craft = g_object_table[mothership_object_index]
					      .mobj->p_craft;
			uint16_t model_index = g_cur_craft->model_index;
			pai_rotate_local_vector_to_world_scratch(
				&g_object_table[mothership_object_index],
				g_model_defs[model_index]
					.hangar_points.inside.side,
				g_model_defs[model_index]
					.hangar_points.inside.up,
				g_model_defs[model_index]
					.hangar_points.inside.forward);
			g_spawn_world_x =
				g_rotated_x +
				g_object_table[mothership_object_index].world_x;
			g_spawn_world_y =
				g_rotated_y +
				g_object_table[mothership_object_index].world_y;
			g_spawn_world_z =
				g_rotated_z +
				g_object_table[mothership_object_index].world_z;
			pai_rotate_local_vector_to_world_scratch(
				&g_object_table[mothership_object_index],
				g_model_defs[model_index]
					.hangar_points.outside.side,
				g_model_defs[model_index]
					.hangar_points.outside.up,
				g_model_defs[model_index]
					.hangar_points.outside.forward);
			g_world_loc_x =
				g_rotated_x +
				g_object_table[mothership_object_index].world_x;
			g_world_loc_y =
				g_rotated_y +
				g_object_table[mothership_object_index].world_y;
			g_world_loc_z =
				g_rotated_z +
				g_object_table[mothership_object_index].world_z;
		}
		trig2_ctop(g_world_loc_x - g_spawn_world_x,
			   g_world_loc_y - g_spawn_world_y,
			   g_world_loc_z - g_spawn_world_z);
		g_spawn_from_mothership_flag = 1;
		g_spawn_yaw = (uint16_t)trig2_xyangle;
		g_spawn_pitch = (uint16_t)trig2_pitch;
		g_spawn_formation = 0;
		if (g_mission_flight_groups[g_current_flight_group_idx]
			    .fg.number_of_craft >
		    LARGE_MOTHERSHIP_FORMATION_THRESHOLD) {
			g_spawn_formation = LARGE_MOTHERSHIP_FORMATION;
		}
		g_spawn_formation_spacing = 0;
	} else {
		mission_resolve_object_or_mission_point_world_loc(
			MISSION_POINT_1, g_current_flight_group_idx);
		g_spawn_world_x = g_world_loc_x;
		g_spawn_world_y = g_world_loc_y;
		g_spawn_world_z = g_world_loc_z;

		if (mission_started != 0) {
			if (g_mission_header.mission_type ==
			    MISSION_TYPE_MELEE) {
				if (g_mission_flight_groups
					    [g_current_flight_group_idx]
						    .fg.player_number != 0) {
					uint8_t player_flight_groups_by_team
						[TEAM_COUNT];
					memset(player_flight_groups_by_team, 0,
					       sizeof(player_flight_groups_by_team));
					uint8_t flight_group_index;
					for (flight_group_index = 0;
					     flight_group_index <
					     (int16_t)g_mission_header
						     .num_flight_groups;
					     ++flight_group_index) {
						if (g_mission_flight_groups
							    [flight_group_index]
								    .fg
								    .player_number !=
						    0) {
							++player_flight_groups_by_team
								[g_mission_flight_groups
									 [flight_group_index]
										 .fg
										 .team];
						}
					}
					if (player_flight_groups_by_team[0] ==
					    1) {
						uint8_t wave_index =
							(uint8_t)g_mission_fg_stats
								[g_current_flight_group_idx]
									.spawned_craft_count /
							g_mission_flight_groups
								[g_current_flight_group_idx]
									.fg
									.number_of_craft;

						int8_t player_number_offset =
							(g_mission_flight_groups
								 [g_current_flight_group_idx]
									 .fg
									 .player_number &
							 1) != 0
								? wave_index -
									  QUICK_START_FORMATION_CENTER
								: -QUICK_START_FORMATION_CENTER -
									  wave_index;
						uint8_t target_player_number =
							(uint8_t)((QUICK_START_FORMATION_CENTER *
									   player_number_offset +
								   g_mission_flight_groups
									   [g_current_flight_group_idx]
										   .fg
										   .player_number) &
								  (PLAYER_NUMBER_COUNT -
								   1)) +
							1;
						for (flight_group_index = 0;
						     flight_group_index <
						     (int16_t)g_mission_header
							     .num_flight_groups;
						     ++flight_group_index) {
							if (g_mission_flight_groups
								    [flight_group_index]
									    .fg
									    .player_number ==
							    target_player_number) {
								mission_resolve_object_or_mission_point_world_loc(
									MISSION_POINT_1,
									flight_group_index);
								g_spawn_world_x =
									g_world_loc_x;
								g_spawn_world_y =
									g_world_loc_y;
								g_spawn_world_z =
									g_world_loc_z;
								break;
							}
						}
					}
				}
				if (g_mission_flight_groups
					    [g_current_flight_group_idx]
						    .player_owner_idx != -1) {
					int random_offset =
						game_rand() &
						RANDOM_PLAYER_OFFSET_MASK;

					if ((game_rand() & 1) != 0) {
						g_spawn_world_x +=
							random_offset;
					} else {
						g_spawn_world_x -=
							random_offset;
					}
					random_offset =
						game_rand() &
						RANDOM_PLAYER_OFFSET_MASK;
					if ((game_rand() & 1) != 0) {
						g_spawn_world_y +=
							random_offset;
					} else {
						g_spawn_world_y -=
							random_offset;
					}
				}
			} else if (
				g_mission_file_version == MISSION_VERSION_14 &&
				g_mission_flight_groups
						[g_current_flight_group_idx]
							.fg.player_number !=
					0 &&
				g_mission_flight_groups
						[g_current_flight_group_idx]
							.fg
							.mission_point_enabled
								[1] != 0) {
				mission_resolve_object_or_mission_point_world_loc(
					MISSION_POINT_2,
					g_current_flight_group_idx);
				g_spawn_world_x = g_world_loc_x;
				g_spawn_world_y = g_world_loc_y;
				g_spawn_world_z = g_world_loc_z;
			}
		}

		int16_t spawn_pitch;
		if (g_mission_flight_groups[g_current_flight_group_idx]
			    .fg.mission_point_enabled[4] != 0) {
			mission_resolve_object_or_mission_point_world_loc(
				MISSION_POINT_5, g_current_flight_group_idx);
			trig2_ctop(g_world_loc_x - g_spawn_world_x,
				   g_world_loc_y - g_spawn_world_y,
				   g_world_loc_z - g_spawn_world_z);
			g_spawn_yaw = (uint16_t)trig2_xyangle;
			spawn_pitch = trig2_pitch;
		} else {
			trig2_xyangle = 0;
			g_spawn_yaw = 0;
			spawn_pitch = DEFAULT_SPAWN_PITCH;
			trig2_pitch = DEFAULT_SPAWN_PITCH;
		}
		g_spawn_pitch = (uint16_t)spawn_pitch;

		if (g_flight_mission_state.proving_grounds_mode_active == 0 &&
		    g_mission_flight_groups[g_current_flight_group_idx]
				    .fg.arrival_method == 0 &&
		    mission_started != 0 && craft_ordinal == UINT16_MAX &&
		    g_mission_flight_groups[g_current_flight_group_idx]
				    .player_owner_idx == -1) {
			trig2_xyangle += HYPERSPACE_REVERSE_ANGLE;
			trig2_pitch = HYPERSPACE_REVERSE_ANGLE - trig2_pitch;
			trig2_movexyz(HYPERSPACE_DISTANCE, trig2_xyangle,
				      (uint16_t)trig2_pitch);
			g_spawn_world_x +=
				HYPERSPACE_OFFSET_SCALE * trig2_xmovedist;
			g_spawn_world_y +=
				HYPERSPACE_OFFSET_SCALE * trig2_ymovedist;
			g_spawn_world_z +=
				HYPERSPACE_OFFSET_SCALE * trig2_zmovedist;
			trig2_xmovedist *= HYPERSPACE_OFFSET_SCALE;
			trig2_ymovedist *= HYPERSPACE_OFFSET_SCALE;
			trig2_zmovedist *= HYPERSPACE_OFFSET_SCALE;
			g_spawn_out_of_hyperspace_flag = 1;
		}
		g_spawn_formation =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.formation;
		g_spawn_formation_spacing =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.formation_spacing;
	}

	int spawned_craft_count;
	{
		uint8_t team =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.team;
		uint8_t status1 =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.status1;

		g_spawn_iff =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.iff;
		uint8_t status2 =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.status2;
		g_spawn_team_id = team;
		g_spawn_status1 = status1;
		spawned_craft_count = 0;
		g_spawn_status2 = status2;
		g_spawn_object_kind = CRAFT_OBJECT_KIND_ACTIVE;
		uint8_t group_ai =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.group_ai;
		g_spawn_genus_id =
			g_object_type_table
				[g_craft_type_to_object_type
					 [g_mission_flight_groups
						  [g_current_flight_group_idx]
							  .fg.craft_type]]
					.genus_id;
		g_spawn_group_ai = group_ai;
		XVT_LOG_DEBUG(
			"mission.wave_prepared fg=%d ordinal=%d started=%d mothership=%d hyperspace=%d x=%d y=%d z=%d yaw=%u pitch=%u formation=%d spacing=%d iff=%d team=%d status1=%d status2=%d genus=%d ai=%d predicted=%d",
			(int)g_current_flight_group_idx,
			craft_ordinal == UINT16_MAX ? -1 : (int)craft_ordinal,
			mission_started != 0, (int)g_spawn_from_mothership_flag,
			(int)g_spawn_out_of_hyperspace_flag, g_spawn_world_x,
			g_spawn_world_y, g_spawn_world_z, (unsigned)g_spawn_yaw,
			(unsigned)g_spawn_pitch, (int)g_spawn_formation,
			(int)g_spawn_formation_spacing, (int)g_spawn_iff,
			(int)g_spawn_team_id, (int)g_spawn_status1,
			(int)g_spawn_status2, (int)g_spawn_genus_id,
			(int)g_spawn_group_ai,
			g_flight_sim_side_effects_suppressed);
	}

	if (craft_ordinal == UINT16_MAX) {
		g_spawn_craft_ordinal = 0;
		g_spawn_leader_obj_idx = UINT8_MAX;
		while (g_spawn_craft_ordinal <
		       g_mission_flight_groups[g_current_flight_group_idx]
			       .fg.number_of_craft) {
			if (g_mission_fg_stats[g_current_flight_group_idx]
				    .outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] >
			    g_mission_fg_stats[g_current_flight_group_idx]
				    .outcome_count
					    [FLIGHT_GROUP_OUTCOME_ARRIVED]) {
				if (mission_init_flight_group_object_slot() ==
				    UINT16_MAX) {
					return 0;
				}
				++spawned_craft_count;
				++g_mission_fg_stats[g_current_flight_group_idx]
					  .outcome_count
						  [FLIGHT_GROUP_OUTCOME_ARRIVED];
				if (g_mission_flight_groups
					    [g_current_flight_group_idx]
						    .fg.special_cargo_craft ==
				    g_spawn_craft_ordinal) {
					++g_mission_fg_stats[g_current_flight_group_idx]
						  .special_cargo_outcome
							  [FLIGHT_GROUP_OUTCOME_ARRIVED];
				}
			}
			++g_spawn_craft_ordinal;
		}
	} else if (g_mission_fg_stats[g_current_flight_group_idx]
			   .outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] >
		   g_mission_fg_stats[g_current_flight_group_idx]
			   .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED]) {
		g_spawn_craft_ordinal = craft_ordinal;
		if (mission_init_flight_group_object_slot() == UINT16_MAX) {
			return 0;
		}
		spawned_craft_count = 1;
		++g_mission_fg_stats[g_current_flight_group_idx]
			  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED];
		if (g_mission_flight_groups[g_current_flight_group_idx]
			    .fg.special_cargo_craft == g_spawn_craft_ordinal) {
			++g_mission_fg_stats[g_current_flight_group_idx]
				  .special_cargo_outcome
					  [FLIGHT_GROUP_OUTCOME_ARRIVED];
		}
	}
	if (mission_started != 0 && craft_ordinal == UINT16_MAX &&
	    spawned_craft_count != 0) {
		uint16_t model_index =
			get_model_index_from_type(g_spawn_object_type);

		msg_reportfgcreation(g_current_flight_group_idx, model_index);
	}
	return 1;
}

/* Creates craft g_spawn_craft_ordinal of flight group g_current_flight_group_idx
 * from the g_spawn globals in the first free object slot of
 * g_object_slot_range_by_genus[g_spawn_genus_id]. Returns that slot, or UINT16_MAX
 * when the range is full. When the craft is the group's player_craft, the group
 * has a player owner and g_initial_spawn_bind_player_craft_slots is set, it binds
 * the craft to that player: object_index, bound_object_signature,
 * bound_flight_group_idx, weapon and power presets, and zeroed mission stats and
 * kill counts. It gives the object the next g_next_object_signature, its type,
 * IFF, team, genus, family, markings and number in the group (counted across
 * the global unit when the group has one and disable_wave_numbering is 0), places
 * the first craft at the spawn point and the others at their g_form_pos_x,
 * g_form_pos_y and g_form_pos_z offsets from the first craft, scaled by bounds and
 * formation_spacing, and sets cargo text, lasers, warheads (ammo by the
 * warhead's share of launcher capacity, doubled or halved by status 1 or 2, at
 * most 9 for a player group), hull, shields, beam and countermeasures as the
 * statuses say, component hit points, plans and throttle from its first order,
 * and fresh damage, mission and AI state. It adds one to the group's
 * spawned_craft_count; in a version 14 file object types 37 and 38 get the
 * transport genus. Also writes g_spawn_object_type, g_spawned_object_iff,
 * g_spawn_leader_obj_idx (to this slot when it was UINT8_MAX), g_cur_craft and
 * g_spawn_object_type_by_object_slot. */
// FUNCTION: XVT 0x456AC0
uint16_t mission_init_flight_group_object_slot(void)
{
	enum {
		PLAYER_COUNT = 8,
		TEAM_COUNT = 10,
		AI_RATING_COUNT = 6,
		PLAYER_RATING_COUNT = 25,
		FLIGHT_GROUP_COUNT = 48,
		LASER_GROUP_COUNT = 2,
		WARHEAD_LAUNCHER_COUNT = 2,
		COMPONENT_COUNT = 50,
		ORDER_GOAL_COUNT = 4,
		BEAM_EFFECT_COUNT = 5,
		SPECIAL_CARGO_NAME_LENGTH = 16,
		HUD_FEATURES_ALL = 0x1FFF,
		HUD_FEATURE_SHIELDS = 0x0800,
		HUD_FEATURE_BEAM = 0x1000,
		HUD_FEATURE_BEAM_LEVEL = 0x0010,
		LASER_CHARGE_FULL = 127,
		BEAM_CHARGE_FULL = 9999,
		SHIELD_OVERFLOW_CLAMP = 32700,
		DEFAULT_TARGET_Z_ANGLE = 0x4000,
		DEFAULT_THROTTLE = 0x8000,
		PLAYER_THROTTLE_PRESET = 21845,
		ADVANCED_MISSILE_OBJECT_TYPE = 149,
		SPECIAL_WARHEAD_INDEX = 5,
		SPECIAL_WARHEAD_MODEL_OBJECT_TYPE = 12,
		SPECIAL_BOUNDS_OBJECT_TYPE = 58,
		SPECIAL_SHIELD_GENERATOR_OBJECT_TYPE = 54,
		PLATFORM_OBJECT_TYPE_FIRST = 60,
		PLATFORM_OBJECT_TYPE_END = 65,
		DYNAMIC_MODEL_OBJECT_TYPE_FIRST = 73,
		OUT_OF_HYPERSPACE_PLAN_NAME_INDEX = 52,
		FROM_MOTHERSHIP_PLAN_NAME_INDEX = 50,
		MISSION_VERSION_14 = 14,
		LEGACY_GENUS_OBJECT_TYPE_FIRST = 37,
		LEGACY_GENUS_OBJECT_TYPE_END = 38,
		PLATFORM_BEAM_DISABLED_STRIDE = 12,
	};

	int bound_player_idx = 0;
	object_type_id object_type = g_craft_type_to_object_type
		[g_mission_flight_groups[g_current_flight_group_idx]
			 .fg.craft_type];
	g_spawn_object_type = (uint8_t)object_type;
	uint16_t object_slot_start =
		g_object_slot_range_by_genus[g_spawn_genus_id].start;
	uint16_t object_slot_end =
		g_object_slot_range_by_genus[g_spawn_genus_id].end;
	uint16_t object_index;
	for (object_index = object_slot_start; object_index < object_slot_end;
	     ++object_index) {
		if (g_object_table[object_index].object_type == 0) {
			break;
		}
	}
	if (object_index >= object_slot_end) {
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_WARN(
				"mission.object_table_full fg=%d ordinal=%d genus=%d first_slot=%u end_slot=%u arrived=%d tick=%d",
				(int)g_current_flight_group_idx,
				(int)g_spawn_craft_ordinal,
				(int)g_spawn_genus_id,
				(unsigned)object_slot_start,
				(unsigned)object_slot_end,
				(int)g_mission_fg_stats[g_current_flight_group_idx]
					.outcome_count
						[FLIGHT_GROUP_OUTCOME_ARRIVED],
				g_game_time);
		}
		return UINT16_MAX;
	}

	collide_reset_object_proximity_for_slot(object_index);
	int player_owner_idx =
		g_mission_flight_groups[g_current_flight_group_idx]
			.player_owner_idx;
	uint8_t player_craft_bound;
	if (player_owner_idx != -1 &&
	    g_mission_flight_groups[g_current_flight_group_idx]
			    .fg.player_craft == g_spawn_craft_ordinal &&
	    g_initial_spawn_bind_player_craft_slots != 0) {
		bound_player_idx =
			g_mission_flight_groups[g_current_flight_group_idx]
				.player_owner_idx;
		g_players[player_owner_idx].object_index = object_index;
		player_craft_bound = 1;
		g_object_table[object_index].player_owner_idx =
			player_owner_idx;
	} else {
		player_craft_bound = 0;
		g_object_table[object_index].player_owner_idx = -1;
	}
	uint8_t has_player_owner =
		g_mission_flight_groups[g_current_flight_group_idx]
			.player_owner_idx != -1;
	g_object_table[object_index].mobj->p_craft =
		&g_craft_data_pool_base[object_index - object_slot_start];
	g_cur_craft = &g_craft_data_pool_base[object_index - object_slot_start];
	g_spawn_object_type_by_object_slot[object_index] = object_type;
	g_cur_craft->effective_ai_object_link = NULL;
	g_cur_craft->effective_ai_object_signature = 0;
	g_object_table[object_index].object_type = (uint8_t)object_type;
	g_object_table[object_index].object_signature =
		g_next_object_signature++;
	int max_bounds_extent =
		g_object_type_table[object_type].max_bounds_extent;
	if (object_type == SPECIAL_BOUNDS_OBJECT_TYPE) {
		max_bounds_extent *= 8;
	}
	if (max_bounds_extent < 0x2000) {
		max_bounds_extent *= 4;
	}
	g_object_table[object_index].mobj->damage_amount = max_bounds_extent;
	g_cur_craft->model_index = get_model_index_from_type(object_type);
	uint16_t model_index = g_cur_craft->model_index;
	g_object_table[object_index].mobj->iff = g_spawn_iff;
	g_spawned_object_iff = g_object_table[object_index].mobj->iff;
	g_object_table[object_index].mobj->team = g_spawn_team_id;
	g_object_table[object_index].genus_id = g_spawn_genus_id;
	g_object_table[object_index].mobj->family =
		g_object_type_table[object_type].family_id;
	g_object_table[object_index].mobj->seconds_alive = 0;
	g_object_table[object_index].mobj->lifetime_timer = 0;
	g_object_table[object_index].mobj->sim_state_timestamp = 0;
	g_object_table[object_index].mobj->node_switch_index =
		g_mission_flight_groups[g_current_flight_group_idx].fg.markings;
	g_object_table[object_index].mobj->source_obj_idx = object_index;
	g_object_table[object_index].mobj->source_object_type =
		(uint8_t)object_type;
	g_object_table[object_index].flight_group_idx =
		(uint8_t)g_current_flight_group_idx;
	g_cur_craft->leader_obj_idx = g_spawn_leader_obj_idx;
	if (g_spawn_leader_obj_idx == UINT8_MAX) {
		g_spawn_leader_obj_idx = (uint8_t)object_index;
	}
	g_cur_craft->ai_flight.formation_type = g_spawn_formation;
	uint16_t formation_type = g_cur_craft->ai_flight.formation_type;
	g_cur_craft->craft_ordinal = (uint8_t)g_spawn_craft_ordinal;
	uint16_t craft_ordinal = g_cur_craft->craft_ordinal;
	int16_t formation_spacing = g_spawn_formation_spacing;
	if (g_spawn_from_mothership_flag != 0) {
		formation_spacing = 0;
	}
	g_cur_craft->ai_flight.separation = (uint8_t)formation_spacing;
	g_cur_craft->push_accum_z = 0;
	g_cur_craft->push_accum_y = g_cur_craft->push_accum_z;
	g_cur_craft->push_accum_x = g_cur_craft->push_accum_y;
	if (g_mission_flight_groups[g_current_flight_group_idx]
			    .fg.global_unit == 0 ||
	    g_mission_flight_groups[g_current_flight_group_idx]
			    .fg.disable_wave_numbering != 0) {
		g_cur_craft->craft_index_in_group =
			g_mission_fg_stats[g_current_flight_group_idx]
				.spawned_craft_count;
		++g_cur_craft->craft_index_in_group;
	} else {
		++g_flight_mission_state.global_unit_craft_count
			  [g_mission_flight_groups[g_current_flight_group_idx]
				   .fg.global_unit];
		g_cur_craft->craft_index_in_group =
			g_flight_mission_state.global_unit_craft_count
				[g_mission_flight_groups
					 [g_current_flight_group_idx]
						 .fg.global_unit];
	}

	int16_t bound_size_x = g_model_defs[model_index].bound_size_x;
	int16_t bound_size_z = g_model_defs[model_index].bound_size_z;
	int16_t bound_size_y = g_model_defs[model_index].bound_size_y;
	int16_t spacing_scale = formation_spacing + 1;
	int16_t forward = bound_size_y;
	forward *= spacing_scale;
	forward *= g_form_pos_y[formation_type][craft_ordinal];
	int16_t up = bound_size_z;
	up *= spacing_scale;
	up *= g_form_pos_z[formation_type][craft_ordinal];
	int16_t side = bound_size_x;
	side *= spacing_scale;
	side *= g_form_pos_x[formation_type][craft_ordinal];
	if (spacing_scale == 1) {
		side += g_form_pos_x[formation_type][craft_ordinal] *
			(bound_size_x / 2);
		up += g_form_pos_z[formation_type][craft_ordinal] *
		      (bound_size_z / 2);
		forward += g_form_pos_y[formation_type][craft_ordinal] *
			   (bound_size_y / 4);
	}
	int16_t formation_divisor = g_formation_divisor[formation_type];
	if (formation_divisor != 1) {
		side /= formation_divisor;
		forward /= formation_divisor;
		up /= formation_divisor;
	}
	if (g_spawn_craft_ordinal == 0) {
		g_object_table[object_index].world_x = g_spawn_world_x;
		g_object_table[object_index].world_y = g_spawn_world_y;
		g_object_table[object_index].world_z = g_spawn_world_z;
		g_object_table[object_index].yaw = g_spawn_yaw;
		g_object_table[object_index].pitch = g_spawn_pitch;
		g_object_table[object_index].roll = 0;
		g_object_table[object_index].mobj->orient_matrix_dirty = 1;
		g_object_table[object_index].mobj->move_vector_dirty =
			g_object_table[object_index].mobj->orient_matrix_dirty;
		pai_calcrotatedpoint(&g_object_table[object_index], side, up,
				     forward);
	} else {
		pai_calcrotatedpoint(
			&g_object_table[g_cur_craft->leader_obj_idx], side, up,
			forward);
	}
	if (g_model_defs[model_index].bound_size_shift != 0) {
		g_rotated_x *= 1 << g_model_defs[model_index].bound_size_shift;
		g_rotated_y *= 1 << g_model_defs[model_index].bound_size_shift;
		g_rotated_z *= 1 << g_model_defs[model_index].bound_size_shift;
	}
	g_object_table[object_index].world_x = g_rotated_x + g_spawn_world_x;
	g_object_table[object_index].mobj->prev_world_x =
		g_object_table[object_index].world_x;
	g_object_table[object_index].world_y = g_rotated_y + g_spawn_world_y;
	g_object_table[object_index].mobj->prev_world_y =
		g_object_table[object_index].world_y;
	g_object_table[object_index].world_z = g_rotated_z + g_spawn_world_z;
	g_object_table[object_index].mobj->prev_world_z =
		g_object_table[object_index].world_z;

	uint16_t index;
	{
		const char *cargo =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.special_cargo;
		if (g_mission_flight_groups[g_current_flight_group_idx]
			    .fg.special_cargo_craft !=
		    g_cur_craft->craft_ordinal) {
			cargo = g_mission_flight_groups
					[g_current_flight_group_idx]
						.fg.cargo;
		}
		for (index = 0; index < SPECIAL_CARGO_NAME_LENGTH; ++index) {
			g_cur_craft->special_cargo_name[index] = cargo[index];
		}
	}
	g_cur_craft->boarding_state = 0;
	g_cur_craft->system_flags = CRAFT_SUBSYSTEM_FLAGS_ALL;
	g_cur_craft->damage_stats.installed_hud_feature_mask = HUD_FEATURES_ALL;
	g_object_table[object_index].yaw = g_spawn_yaw;
	g_cur_craft->yaw = g_object_table[object_index].yaw;
	g_object_table[object_index].pitch = g_spawn_pitch;
	g_cur_craft->pitch = g_object_table[object_index].pitch;
	g_object_table[object_index].roll = 0;
	g_object_table[object_index].mobj->roll_impulse_rate = 0;
	g_object_table[object_index].mobj->orient_matrix_dirty = 1;
	g_object_table[object_index].mobj->move_vector_dirty =
		g_object_table[object_index].mobj->orient_matrix_dirty;
	g_cur_craft->ai_flight.roll_state = 0;
	g_cur_craft->ai_flight.pitch_state = 0;
	g_cur_craft->ai_flight.turn_state = 0;
	g_cur_craft->ai_flight.climb_state = 0;
	g_cur_craft->ai_flight.dive_state = g_cur_craft->ai_flight.climb_state;
	g_cur_craft->ai_flight.pitch_through_loop =
		g_cur_craft->ai_flight.dive_state;
	g_cur_craft->ai_flight.motion_scale = -1;
	g_cur_craft->ai_flight.roll_accel = -1;
	g_cur_craft->ai_flight.pitch_accel = -1;
	g_cur_craft->ai_flight.turn_accel = -1;
	g_cur_craft->ai_flight.roll_rate = g_model_defs[model_index].roll_rate;
	g_cur_craft->ai_flight.pitch_rate =
		g_model_defs[model_index].pitch_rate;
	g_cur_craft->ai_flight.turn_rate = g_model_defs[model_index].yaw_rate;
	g_cur_craft->ai_flight.max_speed_cache =
		g_model_defs[model_index].max_speed;
	int ai_skill_level = g_spawn_group_ai;
	g_cur_craft->ai_skill = g_ai_skill_value_q16_by_level[ai_skill_level];
	g_cur_craft->cannon_group_count = 0;
	{
		uint16_t laser_slot_count = 0;
		for (index = 0; index < LASER_GROUP_COUNT; ++index) {
			g_cur_craft->laser_state.projectile_type_id[index] =
				g_model_defs[model_index]
					.laser_group_weapon_type[index];
			if (player_craft_bound != 0) {
				g_cur_craft->laser_state.link_mode[index] = 1;
			} else {
				g_cur_craft->laser_state.link_mode[index] = 0;
			}
			g_cur_craft->laser_state.burst_remaining[index] = 0;
			g_cur_craft->laser_state.next_slot[index] = 0;
			g_cur_craft->laser_state.fire_cooldown_ticks[index] = 0;
			g_cur_craft->laser_state.next_fire_timestamp[index] = 0;
			if (g_cur_craft->laser_state
				    .projectile_type_id[index] == 0) {
				continue;
			}
			laser_slot_count +=
				g_model_defs[model_index]
					.laser_group_slot_count[index];
			uint16_t first_slot =
				g_model_defs[model_index]
					.laser_group_first_slot[index];
			uint16_t last_slot =
				g_model_defs[model_index]
					.laser_group_last_slot[index];
			if (g_model_defs[model_index]
				    .laser_group_mount_type[index] != 2) {
				++g_cur_craft->cannon_group_count;
				g_cur_craft->laser_state.next_slot[index] =
					(uint8_t)first_slot;
			}
			for (uint16_t weapon_slot = first_slot;
			     weapon_slot <= last_slot; ++weapon_slot) {
				if (g_model_defs[model_index]
					    .laser_group_mount_type[index] !=
				    2) {
					g_cur_craft->weapon_slots[weapon_slot]
						.projectile_type_id =
						g_cur_craft->laser_state
							.projectile_type_id
								[index];
				} else {
					g_cur_craft->weapon_slots[weapon_slot]
						.projectile_type_id = 2;
				}
				g_cur_craft->weapon_slots[weapon_slot]
					.laser_charge = LASER_CHARGE_FULL;
				g_cur_craft->weapon_slots[weapon_slot]
					.ammo_count = 0;
				g_cur_craft->turret_target_states[weapon_slot]
					.target_obj_idx = UINT16_MAX;
			}
		}
		g_cur_craft->laser_recharge_level = POWER_RECHARGE_MAINTENANCE;
		g_cur_craft->laser_slot_count = (uint8_t)laser_slot_count;
	}
	if (g_cur_craft->cannon_group_count == 0) {
		g_cur_craft->system_flags ^= CRAFT_SUBSYSTEM_FLAG_CANNONS;
	}

	g_cur_craft->warhead_launcher_count = 0;
	for (index = 0; index < WARHEAD_LAUNCHER_COUNT; ++index) {
		if (index == 0 ||
		    get_model_index_from_type(
			    SPECIAL_WARHEAD_MODEL_OBJECT_TYPE) == model_index) {
			g_cur_craft->warhead_slot_type_ids[index] =
				g_warhead_type_ids
					[g_mission_flight_groups
						 [g_current_flight_group_idx]
							 .fg.warhead];
		} else {
			g_cur_craft->warhead_slot_type_ids[index] = 0;
		}
		if (index == 1 &&
		    get_model_index_from_type(
			    SPECIAL_WARHEAD_MODEL_OBJECT_TYPE) == model_index &&
		    g_flight_mission_state.proving_grounds_mode_active == 0) {
			g_cur_craft->warhead_slot_type_ids[index] =
				ADVANCED_MISSILE_OBJECT_TYPE;
		}
		g_cur_craft->warhead_launcher_flags[index] = 1;
		g_cur_craft->warhead_launcher_cooldown_ticks[index] = 0;
		if (g_cur_craft->warhead_slot_type_ids[index] == 0) {
			continue;
		}
		++g_cur_craft->warhead_launcher_count;
		uint16_t last_slot = g_model_defs[model_index]
					     .warhead_launcher_last_slot[index];
		for (uint16_t weapon_slot =
			     g_model_defs[model_index]
				     .warhead_launcher_first_slot[index];
		     weapon_slot <= last_slot; ++weapon_slot) {
			uint16_t warhead = g_mission_flight_groups
						   [g_current_flight_group_idx]
							   .fg.warhead;
			g_cur_craft->weapon_slots[weapon_slot]
				.projectile_type_id =
				g_cur_craft->warhead_slot_type_ids[index];
			g_cur_craft->turret_target_states[weapon_slot]
				.target_obj_idx = UINT16_MAX;
			g_cur_craft->weapon_slots[weapon_slot].laser_charge =
				LASER_CHARGE_FULL;
			if (index == 1 &&
			    get_model_index_from_type(
				    SPECIAL_WARHEAD_MODEL_OBJECT_TYPE) ==
				    model_index) {
				warhead = SPECIAL_WARHEAD_INDEX;
			}
			uint8_t ammo_count = (uint8_t)math2_fraction(
				g_model_defs[model_index]
					.warhead_launcher_capacity[index],
				g_warhead_ammo_fraction_q16[warhead]);
			if (ammo_count == 0) {
				ammo_count = 1;
			}
			if (g_spawn_status1 == 1 || g_spawn_status2 == 1) {
				ammo_count *= 2;
			} else if (g_spawn_status1 == 2 ||
				   g_spawn_status2 == 2) {
				ammo_count >>= 1;
			}
			if (ammo_count == 0) {
				ammo_count = 1;
			}
			if (has_player_owner != 0 && ammo_count > 9 &&
			    g_object_table[object_index].object_type !=
				    SPECIAL_WARHEAD_MODEL_OBJECT_TYPE) {
				ammo_count = 9;
			}
			g_cur_craft->weapon_slots[weapon_slot].ammo_count =
				ammo_count;
		}
		XVT_LOG_DEBUG(
			"mission.craft_warheads object=%u launcher=%u type=%d first_slot=%d last_slot=%u ammo=%d predicted=%d",
			(unsigned)object_index, (unsigned)index,
			(int)g_cur_craft->warhead_slot_type_ids[index],
			(int)g_model_defs[model_index]
				.warhead_launcher_first_slot[index],
			(unsigned)last_slot,
			(int)g_cur_craft->weapon_slots[last_slot].ammo_count,
			g_flight_sim_side_effects_suppressed);
	}
	g_cur_craft->warhead_lock_ticks = 0;
	if (g_cur_craft->warhead_launcher_count == 0) {
		g_cur_craft->system_flags ^=
			CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER;
	}

	g_cur_craft->weapon_stats.laser_hits_scored = 0;
	g_cur_craft->weapon_stats.laser_shots_fired =
		g_cur_craft->weapon_stats.laser_hits_scored;
	g_cur_craft->weapon_stats.ion_hits_scored = 0;
	g_cur_craft->weapon_stats.ion_shots_fired =
		g_cur_craft->weapon_stats.ion_hits_scored;
	g_cur_craft->weapon_stats.warhead_hits_scored = 0;
	g_cur_craft->weapon_stats.warheads_fired =
		g_cur_craft->weapon_stats.warhead_hits_scored;
	g_cur_craft->field_29f = 0;
	if (player_craft_bound != 0) {
		g_players[bound_player_idx].bound_object_signature =
			g_object_table[object_index].object_signature;
		g_players[bound_player_idx].bound_flight_group_idx =
			g_current_flight_group_idx;
		g_players[bound_player_idx].mission_stats.laser_hits_scored = 0;
		g_players[bound_player_idx].mission_stats.laser_shots_fired = 0;
		g_players[bound_player_idx].mission_stats.ion_hits_scored = 0;
		g_players[bound_player_idx].mission_stats.ion_shots_fired = 0;
		g_players[bound_player_idx].per_mission_kills.warhead_hits = 0;
		g_players[bound_player_idx].warheads_fired = 0;
		g_players[bound_player_idx].mission_stats.mission_score = 0;
		g_players[bound_player_idx].mission_stats.rating_promo_points =
			0;
		g_players[bound_player_idx]
			.mission_stats.worse_rating_promo_points = 0;
		g_players[bound_player_idx].mission_stats.field_0c = 0;
		g_players[bound_player_idx].mission_stats.field_10 = 0;
		g_players[bound_player_idx]
			.mission_stats.primary_goal_finish_place = 0;
		g_players[bound_player_idx]
			.per_mission_kills.friendlies_killed = 0;
		g_players[bound_player_idx]
			.per_mission_kills.num_craft_inspected = 0;
		g_players[bound_player_idx]
			.per_mission_kills.num_special_inspected = 0;
		for (index = 0; index < FLIGHT_GROUP_COUNT; ++index) {
			g_players[bound_player_idx]
				.per_mission_kills
				.kills_full_on_flight_group[index] = 0;
			g_players[bound_player_idx]
				.per_mission_kills
				.kills_shared_on_flight_group[index] = 0;
			g_players[bound_player_idx]
				.per_mission_kills
				.kills_assist_on_flight_group[index] = 0;
			g_players[bound_player_idx]
				.per_mission_kills
				.kills_full_from_flight_group[index] = 0;
			g_players[bound_player_idx]
				.per_mission_kills
				.kills_shared_from_flight_group[index] = 0;
		}
		for (index = 0; index < PLAYER_RATING_COUNT; ++index) {
			g_players[bound_player_idx]
				.per_mission_kills
				.kills_full_on_player_rating[index] = 0;
			g_players[bound_player_idx]
				.per_mission_kills
				.kills_shared_on_player_rating[index] = 0;
			g_players[bound_player_idx]
				.per_mission_kills
				.kills_assist_on_player_rating[index] = 0;
			g_players[bound_player_idx]
				.per_mission_kills
				.killed_by_player_rating[index] = 0;
		}
		for (index = 0; index < AI_RATING_COUNT; ++index) {
			g_players[bound_player_idx]
				.per_mission_kills
				.kills_full_on_ai_rating[index] = 0;
			g_players[bound_player_idx]
				.per_mission_kills
				.kills_shared_on_ai_rating[index] = 0;
			g_players[bound_player_idx]
				.per_mission_kills
				.kills_assist_on_ai_rating[index] = 0;
			g_players[bound_player_idx]
				.per_mission_kills.killed_by_ai_rating[index] =
				0;
		}
		for (index = 0; index < PLAYER_COUNT; ++index) {
			g_players[bound_player_idx]
				.per_mission_kills.kills_full_on_player[index] =
				0;
			g_players[bound_player_idx]
				.per_mission_kills
				.kills_shared_on_player[index] = 0;
			g_players[bound_player_idx]
				.per_mission_kills
				.kills_full_from_player[index] = 0;
			g_players[bound_player_idx]
				.per_mission_kills
				.kills_shared_from_player[index] = 0;
		}
		g_players[bound_player_idx]
			.per_mission_kills.total_craft_losses = 0;
		g_players[bound_player_idx]
			.per_mission_kills.losses_by_collisions = 0;
		g_players[bound_player_idx]
			.per_mission_kills.losses_by_starships = 0;
		g_players[bound_player_idx].per_mission_kills.losses_by_mines =
			0;
	}

	g_cur_craft->hull_max = g_model_defs[model_index].hull_strength;
	g_cur_craft->system_damage_hull_threshold =
		g_model_defs[model_index].system_damage_hull_threshold;
	g_cur_craft->hull_damage = 0;
	g_cur_craft->damage_stats.last_system_hit_time = 0;
	g_cur_craft->subsystem_damage = 0;
	g_cur_craft->damage_stats.damage_received_total = 0;
	g_cur_craft->damage_stats.damage_received_by_player_owned_craft = 0;
	g_cur_craft->damage_stats.damage_from_collision = 0;
	g_cur_craft->damage_stats.damage_from_starship = 0;
	g_cur_craft->damage_stats.damage_from_mine = 0;
	int slot;
	for (slot = 0; slot < PLAYER_COUNT; ++slot) {
		g_cur_craft->damage_stats.damage_from_player[slot] = 0;
	}
	for (slot = 0; slot < TEAM_COUNT; ++slot) {
		g_cur_craft->damage_stats
			.damage_from_flight_group_amount[slot] = 0;
		g_cur_craft->attacked_by_team[slot] = 0;
	}
	for (slot = 0; slot < AI_RATING_COUNT; ++slot) {
		g_cur_craft->damage_stats.damage_from_ai_skill[slot] = 0;
	}
	g_cur_craft->weapon_fire_inhibit_timer = 0;
	g_cur_craft->mission_accounting_done = 0;
	g_cur_craft->unused_mission_flag = 0;
	g_cur_craft->not_disabled_accounting_suppress = 0;
	g_cur_craft->captured_by_flight_group = 0;
	g_cur_craft->s_foil_state = 0;
	for (index = 0; index < BEAM_EFFECT_COUNT; ++index) {
		g_cur_craft->beam_effect_accum[index] = 0;
	}
	g_cur_craft->beam_active = 0;
	g_cur_craft->beam_output = 0;
	g_cur_craft->beam_target_obj_idx = -1;
	for (slot = 0; slot < TEAM_COUNT; ++slot) {
		if (g_object_table[object_index].mobj->team == slot) {
			g_cur_craft->identified_order_by_team[slot] = 1;
		} else {
			g_cur_craft->identified_order_by_team[slot] = 0;
		}
	}

	if (g_model_defs[model_index].has_hyperdrive == 0 &&
	    g_spawn_status1 != 9 && g_spawn_status2 != 9 &&
	    g_spawn_status1 != 16 && g_spawn_status2 != 16) {
		g_cur_craft->system_flags ^= CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE;
	}
	if (g_spawn_status1 == 6 || g_spawn_status2 == 6) {
		g_cur_craft->system_flags ^= CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE;
	}
	g_cur_craft->shield_energy[0] =
		g_model_defs[model_index].shield_strength;
	if (player_craft_bound != 0) {
		g_cur_craft->shield_energy[1] =
			g_model_defs[model_index].shield_strength;
		g_cur_craft->shield_distrib_mode = SHIELD_DISTRIBUTION_EVEN;
	} else {
		int combined_shield_energy =
			g_cur_craft->shield_energy[0] +
			g_model_defs[model_index].shield_strength;

		g_cur_craft->shield_energy[0] = combined_shield_energy;
		g_cur_craft->shield_energy[1] = 0;
		g_cur_craft->shield_distrib_mode =
			SHIELD_DISTRIBUTION_FULLY_FORWARD;
	}
	if (g_spawn_status1 == 3 || g_spawn_status2 == 3) {
		g_cur_craft->shield_energy[0] = 0;
		g_cur_craft->shield_energy[1] = 0;
		g_cur_craft->system_flags ^= CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	} else if (g_spawn_status1 == 4 || g_spawn_status2 == 4) {
		g_cur_craft->shield_energy[0] >>= 1;
		g_cur_craft->shield_energy[1] >>= 1;
		g_cur_craft->system_flags ^= CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	} else if (g_spawn_status1 == 7 || g_spawn_status2 == 7) {
		g_cur_craft->shield_energy[0] = 0;
		g_cur_craft->shield_energy[1] = 0;
	} else if (g_spawn_status1 == 19 || g_spawn_status2 == 19 ||
		   g_spawn_status1 == 13 || g_spawn_status2 == 13) {
		g_cur_craft->shield_energy[0] >>= 1;
		g_cur_craft->shield_energy[1] >>= 1;
	} else if (g_spawn_status1 == 18 || g_spawn_status2 == 18 ||
		   g_spawn_status1 == 12 || g_spawn_status2 == 12) {
		g_cur_craft->shield_energy[0] *= 2;
		g_cur_craft->shield_energy[1] *= 2;
		if (g_cur_craft->shield_energy[0] < 0) {
			g_cur_craft->shield_energy[0] = SHIELD_OVERFLOW_CLAMP;
		}
		if (g_cur_craft->shield_energy[1] < 0) {
			g_cur_craft->shield_energy[1] = SHIELD_OVERFLOW_CLAMP;
		}
	}
	g_cur_craft->shield_recharge_level = POWER_RECHARGE_MAINTENANCE;
	if (g_model_defs[model_index].has_shields == 0 &&
	    g_spawn_status1 != 8 && g_spawn_status2 != 8 &&
	    g_spawn_status1 != 16 && g_spawn_status2 != 16) {
		g_cur_craft->system_flags ^= CRAFT_SUBSYSTEM_FLAG_SHIELDS;
		g_cur_craft->shield_energy[0] = 0;
		g_cur_craft->shield_energy[1] = 0;
		g_cur_craft->damage_stats.installed_hud_feature_mask ^=
			HUD_FEATURE_SHIELDS;
	}
	g_cur_craft->beam_type_id =
		g_mission_flight_groups[g_current_flight_group_idx].fg.beam;
	if (g_spawn_object_type == 1 || g_spawn_object_type == 2 ||
	    g_spawn_object_type == 4 || g_spawn_object_type == 3 ||
	    g_spawn_object_type == 5) {
		g_cur_craft->beam_type_id = BEAM_TYPE_NONE;
	}
	g_cur_craft->beam_recharge_level = POWER_RECHARGE_MAINTENANCE;
	g_cur_craft->beam_charge = BEAM_CHARGE_FULL;
	if (g_cur_craft->beam_type_id == BEAM_TYPE_NONE) {
		g_cur_craft->beam_charge = 0;
		g_cur_craft->system_flags ^= CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM;
		g_cur_craft->damage_stats.installed_hud_feature_mask ^=
			HUD_FEATURE_BEAM;
		g_cur_craft->damage_stats.installed_hud_feature_mask ^=
			HUD_FEATURE_BEAM_LEVEL;
	}
	g_cur_craft->cm_type_id =
		g_mission_flight_groups[g_current_flight_group_idx]
			.fg.countermeasures;
	if (g_cur_craft->cm_type_id != COUNTERMEASURE_TYPE_NONE) {
		g_cur_craft->cm_ammo_count =
			g_model_defs[model_index].countermeasure_count;
		if (g_cur_craft->cm_type_id == COUNTERMEASURE_TYPE_FLARE) {
			g_cur_craft->cm_ammo_count = (uint8_t)math2_fraction(
				g_cur_craft->cm_ammo_count, 0xAAACu);
		}
	} else {
		g_cur_craft->cm_ammo_count = 0;
		g_cur_craft->system_flags ^=
			CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES;
	}
	g_cur_craft->chaff_active_seconds = 0;
	g_cur_craft->cm_fire_cooldown_timer = 0;
	g_cur_craft->working_subsystems = g_cur_craft->system_flags;
	g_cur_craft->damage_stats.active_hud_feature_mask =
		g_cur_craft->damage_stats.installed_hud_feature_mask;
	for (index = 0;
	     index < sizeof(g_object_table[object_index].type_specific_byte);
	     ++index) {
		g_object_table[object_index].type_specific_byte[index] = 0;
	}
	for (index = 0; index < COMPONENT_COUNT; ++index) {
		g_cur_craft->component_state[index] = 0;
		g_cur_craft->mesh_rotation[index] = 0;
		g_cur_craft->component_hp[index] = UINT8_MAX;
	}
	{
		int mesh_count;

		if (object_type < DYNAMIC_MODEL_OBJECT_TYPE_FIRST) {
			mesh_count = g_object_type_mesh_cache[object_type]
					     .mesh_count;
		} else {
			mesh_count = model_mesh_get_object_type_mesh_count(
				object_type);
		}
		for (index = 0; index < mesh_count; ++index) {
			mesh_component_type mesh_type;
			if (object_type < DYNAMIC_MODEL_OBJECT_TYPE_FIRST) {
				unsigned int cached_mesh_index = index;
				if (cached_mesh_index >=
				    (unsigned int)g_object_type_mesh_cache
					    [object_type]
						    .mesh_count) {
					cached_mesh_index =
						g_object_type_mesh_cache
							[object_type]
								.mesh_count -
						1;
				}
				mesh_type =
					g_object_type_mesh_cache[object_type]
						.mesh_types[cached_mesh_index];
			} else {
				mesh_type =
					model_mesh_get_object_type_mesh_type(
						object_type, index);
			}
			if (model_mesh_is_object_type_mesh_damageable(
				    object_type, index) != 0) {
				uint8_t component_hp;

				if (object_type ==
					    SPECIAL_SHIELD_GENERATOR_OBJECT_TYPE &&
				    mesh_type == MESH_COMPONENT_08_SHLD_GEN) {
					component_hp =
						g_mesh_type_component_max_hp
							[mesh_type];
					component_hp += component_hp;
					--component_hp;
				} else {
					component_hp =
						g_mesh_type_component_max_hp
							[mesh_type];
				}
				g_cur_craft->component_hp[index] = component_hp;
			}
			if ((g_spawn_status1 == 5 || g_spawn_status2 == 5 ||
			     g_spawn_status1 == 14 || g_spawn_status2 == 14) &&
			    (mesh_type == MESH_COMPONENT_04_LASR_TUR ||
			     mesh_type == MESH_COMPONENT_21_ROTATING_LASR_TUR ||
			     mesh_type == MESH_COMPONENT_05_LASR_GUN)) {
				g_cur_craft->component_hp[index] = 0;
				g_cur_craft->component_state[index] = 4;
			}
		}
	}
	if (g_spawn_genus_id == CRAFT_GENUS_PLATFORM &&
	    g_spawn_object_type >= PLATFORM_OBJECT_TYPE_FIRST &&
	    g_spawn_object_type < PLATFORM_OBJECT_TYPE_END &&
	    g_mission_flight_groups[g_current_flight_group_idx].fg.beam != 0) {
		uint16_t disabled_count =
			g_mission_flight_groups[g_current_flight_group_idx]
						.fg.beam == BEAM_TYPE_TRACTOR
				? 6
				: 12;
		uint16_t disabled_slot =
			(g_spawn_object_type - PLATFORM_OBJECT_TYPE_FIRST) *
			PLATFORM_BEAM_DISABLED_STRIDE;
		int disabled_slot_end = disabled_slot + disabled_count;

		for (; disabled_slot < disabled_slot_end; ++disabled_slot) {
			uint8_t component_index =
				g_platform_beam_disabled_component_ids
					[disabled_slot];
			if (component_index != UINT8_MAX) {
				g_cur_craft->component_hp[component_index] = 0;
				g_cur_craft->component_state[component_index] =
					4;
			}
		}
	}

	uint8_t engine_glow_count;
	int16_t full_throttle;
	{
		int order = g_mission_flight_groups[g_current_flight_group_idx]
				    .fg.orders[0]
				    .order;
		uint16_t leader_plan = g_builtin_plan_id_by_name_index
			[g_order_leader_builtin_plan_name_index[order]];
		uint8_t follower_plan = g_builtin_plan_id_by_name_index
			[g_order_follower_builtin_plan_name_index[order]];
		g_cur_craft->ai_controller.running_plan_id =
			(uint8_t)leader_plan;
		g_cur_craft->ai_controller.current_plan_id =
			g_cur_craft->ai_controller.running_plan_id;
		if (g_spawn_out_of_hyperspace_flag != 0) {
			follower_plan = g_builtin_plan_id_by_name_index
				[OUT_OF_HYPERSPACE_PLAN_NAME_INDEX];
		} else if (g_spawn_from_mothership_flag != 0) {
			follower_plan = g_builtin_plan_id_by_name_index
				[FROM_MOTHERSHIP_PLAN_NAME_INDEX];
		} else if (g_cur_craft->leader_obj_idx == UINT8_MAX) {
			follower_plan = (uint8_t)leader_plan;
		}
		g_cur_craft->ai_controller.running_plan_id = follower_plan;
		struct pai_plan_record *plan = &g_plan_table[leader_plan];
		uint16_t throttle_speed;
		if ((strcmp(plan->name, "nullpln") == 0 ||
		     strcmp(plan->name, "stationaryldrpln") == 0 ||
		     strcmp(plan->name, "stationaryflwpln") == 0 ||
		     strcmp(plan->name, "disabledpln") == 0) &&
		    player_craft_bound == 0) {
			throttle_speed = 0;
		} else {
			throttle_speed = DEFAULT_THROTTLE;
			if (strcmp(plan->name, "escortldr1pln") != 0) {
				throttle_speed = g_order_throttle_to_craft_throttle_speed
					[g_mission_flight_groups
						 [g_current_flight_group_idx]
							 .fg.orders[0]
							 .throttle];
			}
		}
		engine_glow_count = g_model_defs[model_index].engine_glow_count;
		full_throttle = -1;
		g_cur_craft->object_kind = g_spawn_object_kind;
		g_cur_craft->throttle_speed = throttle_speed;
		g_cur_craft->engine_overdrive_off = (uint16_t)full_throttle;
		g_object_table[object_index].mobj->speed = math2_fraction(
			g_model_defs[model_index].max_speed, throttle_speed);
		g_object_table[object_index].mobj->speed_remainder = 0;
	}
	if (player_craft_bound != 0) {
		g_players[bound_player_idx].selected_weapon_bank = 0;
		g_players[bound_player_idx].selected_weapon_mode = 0;
		g_players[bound_player_idx].bound_craft_engine_glow_count =
			engine_glow_count;
		g_players[bound_player_idx].throttle_preset[0] =
			PLAYER_THROTTLE_PRESET;
		g_players[bound_player_idx].laser_preset[0] =
			POWER_RECHARGE_MAINTENANCE;
		g_players[bound_player_idx].shield_preset[0] =
			POWER_RECHARGE_MAINTENANCE;
		g_players[bound_player_idx].beam_preset[0] =
			POWER_RECHARGE_MAINTENANCE;
		g_players[bound_player_idx].throttle_preset[1] = full_throttle;
		g_players[bound_player_idx].laser_preset[1] =
			POWER_RECHARGE_INCREASED;
		g_players[bound_player_idx].shield_preset[1] =
			POWER_RECHARGE_MAINTENANCE;
		g_players[bound_player_idx].beam_preset[1] =
			POWER_RECHARGE_MAINTENANCE;
	}
	for (index = 0; index < ORDER_GOAL_COUNT; ++index) {
		g_cur_craft->ai_controller.order_progress
			.completion_state[index] = 0;
		g_cur_craft->ai_controller.order_progress.goal_progress[index] =
			0;
	}
	g_cur_craft->ai_controller.current_order_slot = 0;
	g_cur_craft->ai_controller.skipped_to_order4 = 0;
	g_cur_craft->ai_controller.target_obj_idx = UINT16_MAX;
	g_cur_craft->ai_controller.candidate_target_idx =
		g_cur_craft->ai_controller.target_obj_idx;
	g_cur_craft->ai_controller.target_signature = 0;
	g_cur_craft->ai_controller.has_live_target = 0;
	g_cur_craft->ai_controller.target_component = UINT16_MAX;
	g_cur_craft->carried_object_index =
		g_cur_craft->ai_controller.target_component;
	g_cur_craft->carrier_obj_idx = g_cur_craft->carried_object_index;
	g_cur_craft->ai_flight.impact_obj_idx = UINT16_MAX;
	g_cur_craft->ai_controller.escort_target_fg = UINT8_MAX;
	g_cur_craft->ai_flight.go_home_flag = 0;
	g_cur_craft->ai_flight.mission_aborted_flag = 0;
	g_cur_craft->ai_flight.depart_timer_flag = 0;
	g_cur_craft->ai_flight.depart_clock_hours = 0;
	g_cur_craft->ai_flight.depart_clock_minutes = 0;
	g_cur_craft->ai_flight.depart_clock_seconds = 0;
	g_cur_craft->ai_flight.hits_this_maneuver = 0;
	g_cur_craft->commanded_speed = 0;
	g_cur_craft->ai_flight.boarded_accounting_done = 0;
	g_cur_craft->ai_flight.times_boarded = 0;
	g_cur_craft->ai_flight.docking_accounting_done = 0;
	g_cur_craft->ai_flight.docked_target_count = 0;
	g_cur_craft->ai_controller.target_component = UINT16_MAX;
	g_cur_craft->ai_controller.escort_target_fg = UINT8_MAX;
	g_cur_craft->ai_controller.aim_point_x = 0;
	g_cur_craft->ai_controller.aim_point_y = 0;
	g_cur_craft->ai_controller.aim_point_z = 0;
	g_cur_craft->ai_controller.target_z_angle = DEFAULT_TARGET_Z_ANGLE;
	g_cur_craft->ai_controller.target_roll = 0;
	g_cur_craft->ai_controller.target_xy_angle = 0;
	g_cur_craft->ai_controller.waypoint_index = ORDER_GOAL_COUNT;
	g_cur_craft->ai_controller.saved_plan_id = 0;
	g_cur_craft->ai_controller.think_interval =
		g_ai_think_interval_by_skill[ai_skill_level];
	g_cur_craft->ai_controller.saved_rand_seed = game_rand() ^ 0xBEEF;
	g_cur_craft->ai_controller.maneuver_mode = AI_MANEUVER_MODE_NULL;
	g_cur_craft->ai_controller.maneuver_phase = 0;
	g_cur_craft->ai_controller.maneuver_timer = 0;
	for (index = 0; index < CRAFT_SUBSYSTEM_COUNT; ++index) {
		g_cur_craft->system_display_slot_by_system[index] =
			(uint8_t)index;
		g_cur_craft->system_health[index] = 100;
		g_cur_craft->system_repair_seconds[index] = 0;
	}
	pai_setupcraftcontext(object_index);
	pai_apply_running_plan_target_and_maneuver(object_index);
	craft_clear_turret_object_links(g_cur_craft);
	g_cur_craft->player_command_avoid_target_obj_idx = UINT16_MAX;
	++g_mission_fg_stats[g_current_flight_group_idx].spawned_craft_count;
	if (g_mission_file_version == MISSION_VERSION_14 &&
	    (g_object_table[object_index].object_type ==
		     LEGACY_GENUS_OBJECT_TYPE_FIRST ||
	     g_object_table[object_index].object_type ==
		     LEGACY_GENUS_OBJECT_TYPE_END)) {
		g_object_table[object_index].genus_id = CRAFT_GENUS_TRANSPORT;
	}
	XVT_LOG_DEBUG(
		"mission.craft_spawned fg=%d ordinal=%d object=%u type=%u genus=%d signature=%u number=%d leader=%d slot=%d x=%d y=%d z=%d yaw=%u pitch=%u hull=%u front=%d rear=%d lasers=%d warheads=%d beam=%d cm=%d cm_count=%d systems=%04x leader_plan=%d plan=%d throttle=%u speed=%u ai=%d ai_seed=%d predicted=%d",
		(int)g_current_flight_group_idx,
		(int)g_cur_craft->craft_ordinal, (unsigned)object_index,
		(unsigned)object_type,
		(int)g_object_table[object_index].genus_id,
		(unsigned)g_object_table[object_index].object_signature,
		g_cur_craft->craft_index_in_group,
		(int)g_cur_craft->leader_obj_idx,
		g_object_table[object_index].player_owner_idx,
		g_object_table[object_index].world_x,
		g_object_table[object_index].world_y,
		g_object_table[object_index].world_z,
		(unsigned)g_object_table[object_index].yaw,
		(unsigned)g_object_table[object_index].pitch,
		g_cur_craft->hull_max, g_cur_craft->shield_energy[0],
		g_cur_craft->shield_energy[1],
		(int)g_cur_craft->laser_slot_count,
		(int)g_cur_craft->warhead_launcher_count,
		(int)g_cur_craft->beam_type_id, (int)g_cur_craft->cm_type_id,
		(int)g_cur_craft->cm_ammo_count,
		(unsigned)g_cur_craft->system_flags,
		(int)g_cur_craft->ai_controller.current_plan_id,
		(int)g_cur_craft->ai_controller.running_plan_id,
		(unsigned)g_cur_craft->throttle_speed,
		(unsigned)g_object_table[object_index].mobj->speed,
		ai_skill_level, (int)g_cur_craft->ai_controller.saved_rand_seed,
		g_flight_sim_side_effects_suppressed);
	return object_index;
}

/* Places the static objects of flight group g_current_flight_group_idx through
 * mission_spawn_prepared_object, all of them or only craft_ordinal when that is
 * not UINT16_MAX; does nothing when the group has no craft or its type lacks
 * the static flag (0x80). Mines (CRAFT_GENUS_MINE) fill a square grid of
 * number_of_craft by number_of_craft at 64 mission units apart, centered on
 * mission point 1, in the plane status1 & 3 picks (0 x and y, 1 y and z,
 * 2 x and z, 3 all at one point), while the arrived count is below the
 * total. A satellite is one object at mission point 1. Debris
 * (CRAFT_GENUS_NORMAL_DEBRIS) is number_of_craft objects of random type 100 to
 * 105 at random spots within 256 units of point 1, drawn from
 * g_asteroid_field_rand_seed, which it then advances; the game's random state is
 * restored after. Mines and satellites take the group's yaw, pitch and roll.
 * Writes the g_preparedSpawn globals. */
// FUNCTION: XVT 0x4587C0
void mission_spawn_flight_group_static_objects(uint16_t craft_ordinal)
{
	if (g_mission_fg_stats[g_current_flight_group_idx]
		    .outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] == 0) {
		return;
	}

	uint8_t object_type = g_craft_type_to_object_type
		[g_mission_flight_groups[g_current_flight_group_idx]
			 .fg.craft_type];
	if ((g_object_type_table[object_type].behavior_flags & 0x80u) == 0) {
		return;
	}

	int genus_id = g_object_type_table
			       [g_craft_type_to_object_type
					[g_mission_flight_groups
						 [g_current_flight_group_idx]
							 .fg.craft_type]]
				       .genus_id;
	int x_step;
	int y_step;
	int z_row_step;
	int z_column_step;
	uint8_t number_of_craft;
	int grid_extent;
	uint32_t spawn_index;
	int ordinal;
	int base_x;
	int base_y;
	int base_z;
	uint16_t saved_random_state;
	int spawn_x;
	int spawn_y;
	int spawn_z;
	int object_index;
	switch (genus_id) {
	case CRAFT_GENUS_MINE:
		x_step = 0;
		y_step = 0;
		z_row_step = 0;
		z_column_step = 0;
		switch (g_mission_flight_groups[g_current_flight_group_idx]
				.fg.status1 &
			3u) {
		case 0:
			x_step = 64;
			y_step = 64;
			break;
		case 1:
			y_step = 64;
			z_column_step = 64;
			break;
		case 2:
			x_step = 64;
			z_row_step = 64;
			break;
		default:
			break;
		}

		number_of_craft =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.number_of_craft;
		grid_extent = number_of_craft - 1;
		base_x = g_mission_flight_groups[g_current_flight_group_idx]
				 .fg.mission_point_x[0] -
			 (int)((uint32_t)(grid_extent * x_step) >> 1);
		base_y = -(g_mission_flight_groups[g_current_flight_group_idx]
				   .fg.mission_point_y[0] +
			   (int)((uint32_t)(grid_extent * y_step) >> 1));
		base_z = g_mission_flight_groups[g_current_flight_group_idx]
				 .fg.mission_point_z[0] -
			 (int)((uint32_t)(grid_extent * z_row_step) >> 1) -
			 (int)((uint32_t)(grid_extent * z_column_step) >> 1);
		g_prepared_spawn_yaw_byte =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.yaw;
		g_prepared_spawn_pitch_byte =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.pitch;
		g_prepared_spawn_roll_byte =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.roll;
		XVT_LOG_DEBUG(
			"mission.mine_grid fg=%d size=%d plane=%u x=%d y=%d z=%d yaw=%u pitch=%u roll=%u ordinal=%d predicted=%d",
			(int)g_current_flight_group_idx, (int)number_of_craft,
			(unsigned)(g_mission_flight_groups
					   [g_current_flight_group_idx]
						   .fg.status1 &
				   3u),
			base_x, base_y, base_z,
			(unsigned)g_prepared_spawn_yaw_byte,
			(unsigned)g_prepared_spawn_pitch_byte,
			(unsigned)g_prepared_spawn_roll_byte,
			craft_ordinal == UINT16_MAX ? -1 : (int)craft_ordinal,
			g_flight_sim_side_effects_suppressed);

		ordinal = 0;
		spawn_index = 0;
		if (number_of_craft != 0) {
			do {
				int column = 0;
				if (g_mission_flight_groups
					    [g_current_flight_group_idx]
						    .fg.number_of_craft != 0) {
					int x = base_x;
					int z = base_z +
						(int)spawn_index * z_row_step;
					do {
						if ((craft_ordinal ==
							     UINT16_MAX ||
						     craft_ordinal ==
							     ordinal) &&
						    g_mission_fg_stats[g_current_flight_group_idx]
								    .outcome_count
									    [FLIGHT_GROUP_OUTCOME_TOTAL] >
							    g_mission_fg_stats[g_current_flight_group_idx]
								    .outcome_count
									    [FLIGHT_GROUP_OUTCOME_ARRIVED]) {
							g_prepared_spawn_mission_x =
								x;
							g_prepared_spawn_mission_y =
								base_y +
								(int)spawn_index *
									y_step;
							g_prepared_spawn_mission_z =
								z;
							mission_spawn_prepared_object(
								g_current_flight_group_idx,
								CRAFT_GENUS_MINE,
								object_type);
						}
						++ordinal;
						x += x_step;
						z += z_column_step;
						++column;
					} while (
						g_mission_flight_groups
							[g_current_flight_group_idx]
								.fg
								.number_of_craft >
						column);
				}
				++spawn_index;
			} while (g_mission_flight_groups
					 [g_current_flight_group_idx]
						 .fg.number_of_craft >
				 spawn_index);
		}
		break;

	case CRAFT_GENUS_SATELLITE:
		g_prepared_spawn_mission_x =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.mission_point_x[0];
		g_prepared_spawn_mission_y =
			-g_mission_flight_groups[g_current_flight_group_idx]
				 .fg.mission_point_y[0];
		g_prepared_spawn_mission_z =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.mission_point_z[0];
		g_prepared_spawn_yaw_byte =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.yaw;
		g_prepared_spawn_pitch_byte =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.pitch;
		g_prepared_spawn_roll_byte =
			g_mission_flight_groups[g_current_flight_group_idx]
				.fg.roll;
		mission_spawn_prepared_object(g_current_flight_group_idx,
					      CRAFT_GENUS_SATELLITE,
					      object_type);
		break;

	case CRAFT_GENUS_NORMAL_DEBRIS:
		base_x = g_mission_flight_groups[g_current_flight_group_idx]
				 .fg.mission_point_x[0];
		saved_random_state = (uint16_t)g_game_rand_feedback_state;
		spawn_index = 0;
		base_y = -g_mission_flight_groups[g_current_flight_group_idx]
				  .fg.mission_point_y[0];
		base_z = g_mission_flight_groups[g_current_flight_group_idx]
				 .fg.mission_point_z[0];
		g_game_rand_feedback_state =
			(int16_t)g_asteroid_field_rand_seed;
		while (g_mission_flight_groups[g_current_flight_group_idx]
			       .fg.number_of_craft > spawn_index) {
			do {
				spawn_x = (game_rand() & 0x1FF) + base_x - 256;
				spawn_y = (game_rand() & 0x1FF) + base_y - 256;
				spawn_z = (game_rand() & 0x1FF) + base_z - 256;
				int static_object_end =
					g_region_static_object_slot_count;
				object_index = g_region_main_object_slot_end;
				static_object_end += object_index;
				if (object_index < static_object_end) {
					struct object_record *object =
						&g_object_table[object_index];
					do {
						if (object->object_type != 0 &&
						    object->world_x ==
							    spawn_x &&
						    object->world_y ==
							    spawn_y &&
						    object->world_z ==
							    spawn_z) {
							break;
						}
						++object;
						++object_index;
					} while (static_object_end >
						 object_index);
				}
			} while (g_region_static_object_slot_count >
				 object_index);

			g_prepared_spawn_mission_x = spawn_x;
			g_prepared_spawn_mission_y = spawn_y;
			g_prepared_spawn_mission_z = spawn_z;
			g_prepared_spawn_yaw_byte = 0;
			g_prepared_spawn_pitch_byte = 0;
			g_prepared_spawn_roll_byte = 0;
			mission_spawn_prepared_object(
				g_current_flight_group_idx,
				CRAFT_GENUS_NORMAL_DEBRIS,
				(uint8_t)((uint16_t)game_rand() % 6u + 100u));
			++spawn_index;
		}
		g_asteroid_field_rand_seed =
			(uint16_t)g_game_rand_feedback_state;
		g_game_rand_feedback_state = (int16_t)saved_random_state;
		XVT_LOG_DEBUG(
			"mission.debris_field fg=%d count=%d x=%d y=%d z=%d seed=%u rand=%u predicted=%d",
			(int)g_current_flight_group_idx,
			(int)g_mission_flight_groups[g_current_flight_group_idx]
				.fg.number_of_craft,
			base_x, base_y, base_z,
			(unsigned)g_asteroid_field_rand_seed,
			(unsigned)saved_random_state,
			g_flight_sim_side_effects_suppressed);
		break;

	default:
		break;
	}
}

/* Creates one static object from the g_preparedSpawn globals in the slot
 * object_find_free_mission_slot returns: position times 256 into world units,
 * yaw, pitch and roll bytes shifted up 8 bits, the next
 * g_next_object_signature, and the given group, genus and type; for genus 8
 * type_specific_byte[1] is 29 times the low 3 bits of the group's arrived
 * count. Adds
 * one to the group's arrived count. Returns the slot, or UINT16_MAX with
 * nothing done when there is none. */
// FUNCTION: XVT 0x458C80
uint16_t mission_spawn_prepared_object(uint16_t flight_group_idx,
				       int16_t genus_id, uint8_t object_type)
{
	uint16_t object_index = object_find_free_mission_slot();
	if (object_index != UINT16_MAX) {
		g_object_table[object_index].world_x =
			g_prepared_spawn_mission_x;
		g_object_table[object_index].world_y =
			g_prepared_spawn_mission_y;
		g_object_table[object_index].world_z =
			g_prepared_spawn_mission_z;
		g_object_table[object_index].yaw = g_prepared_spawn_yaw_byte;
		g_object_table[object_index].pitch =
			g_prepared_spawn_pitch_byte;
		g_object_table[object_index].roll = g_prepared_spawn_roll_byte;
		g_object_table[object_index].world_x *= 256;
		g_object_table[object_index].world_y *= 256;
		g_object_table[object_index].world_z *= 256;
		g_object_table[object_index].yaw <<= 8;
		g_object_table[object_index].pitch <<= 8;
		g_object_table[object_index].roll <<= 8;
		g_object_table[object_index].object_signature =
			g_next_object_signature++;
		g_object_table[object_index].flight_group_idx =
			(uint8_t)flight_group_idx;
		g_object_table[object_index].genus_id = (uint8_t)genus_id;
		g_object_table[object_index].object_type = object_type;
		g_object_table[object_index].type_specific_word = 1023;
		g_object_table[object_index].type_specific_byte[0] = 0;
		if (genus_id == 8) {
			g_object_table[object_index].type_specific_byte[1] =
				(uint8_t)(29 *
					  (g_mission_fg_stats[flight_group_idx].outcome_count
						   [FLIGHT_GROUP_OUTCOME_ARRIVED] &
					   7));
		} else {
			g_object_table[object_index].type_specific_byte[1] = 0;
		}
		++g_mission_fg_stats[flight_group_idx]
			  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED];
		XVT_LOG_DEBUG(
			"mission.static_placed fg=%d object=%u genus=%d type=%d x=%d y=%d z=%d yaw=%u pitch=%u roll=%u signature=%u countdown=%d arrived=%d predicted=%d",
			(int)flight_group_idx, (unsigned)object_index,
			(int)genus_id, (int)object_type,
			g_object_table[object_index].world_x,
			g_object_table[object_index].world_y,
			g_object_table[object_index].world_z,
			(unsigned)g_object_table[object_index].yaw,
			(unsigned)g_object_table[object_index].pitch,
			(unsigned)g_object_table[object_index].roll,
			(unsigned)g_object_table[object_index].object_signature,
			(int)g_object_table[object_index].type_specific_byte[1],
			(int)g_mission_fg_stats[flight_group_idx]
				.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			g_flight_sim_side_effects_suppressed);
	} else if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_WARN(
			"mission.static_table_full fg=%d genus=%d type=%d arrived=%d tick=%d",
			(int)flight_group_idx, (int)genus_id, (int)object_type,
			(int)g_mission_fg_stats[flight_group_idx]
				.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			g_game_time);
	}
	return object_index;
}

/* Puts a world position in g_world_loc_x, g_world_loc_y and g_world_loc_z. A
 * reference below 0x8000 is an object index and gives that object's position;
 * 0x8000 gives the flight group's current_mission_point_ref, and any other
 * reference mission point ref - 0x8000 of flight_group_idx, times 256 with y
 * negated. Does not check the object index or the point index (0 to 21). */
// FUNCTION: XVT 0x458E20
void mission_resolve_object_or_mission_point_world_loc(
	unsigned int obj_or_mission_point_ref, int flight_group_idx)
{
	unsigned int current_mission_point_ref = obj_or_mission_point_ref;
	if (obj_or_mission_point_ref < 0x8000u) {
		g_world_loc_x =
			g_object_table[obj_or_mission_point_ref].world_x;
		g_world_loc_y =
			g_object_table[obj_or_mission_point_ref].world_y;
		g_world_loc_z =
			g_object_table[obj_or_mission_point_ref].world_z;
		return;
	}

	if (obj_or_mission_point_ref == 0x8000u) {
		current_mission_point_ref = g_mission_fg_stats[flight_group_idx]
						    .current_mission_point_ref;
	}

	int mission_point_idx = (int)(current_mission_point_ref - 0x8000u);
	if (g_flight_sim_side_effects_suppressed == 0 &&
	    (mission_point_idx < 0 || mission_point_idx >= 22)) {
		XVT_LOG_WARN(
			"mission.point_out_of_range fg=%d ref=%u current=%u point=%d",
			flight_group_idx, obj_or_mission_point_ref,
			current_mission_point_ref, mission_point_idx);
	}
	g_world_loc_x = g_mission_flight_groups[flight_group_idx]
				.fg.mission_point_x[mission_point_idx] *
			256;
	g_world_loc_y = -(g_mission_flight_groups[flight_group_idx]
				  .fg.mission_point_y[mission_point_idx] *
			  256);
	g_world_loc_z = g_mission_flight_groups[flight_group_idx]
				.fg.mission_point_z[mission_point_idx] *
			256;
}

/* Puts in g_world_loc_x, g_world_loc_y and g_world_loc_z where craft
 * formation_slot_idx of a flight group stands, raised by its model's
 * model_bounds_get_max_z. Of the static types, genus 9 stands at mission point
 * 1, genus 8 at its cell of the mine grid
 * mission_spawn_flight_group_static_objects lays out, and any other, or a slot
 * past the grid, at the group's current mission point. Other types take the
 * group's current mission point plus the slot's offset in
 * g_form_pos_x, g_form_pos_y and g_form_pos_z, scaled by bounds and
 * formation_spacing and turned to basis_obj_idx's orientation (left in
 * g_rotated_x, g_rotated_y and g_rotated_z); with basis_obj_idx UINT16_MAX the
 * offset is 0. */
// FUNCTION: XVT 0x459B40
void mission_resolve_formation_slot_world_loc(uint16_t flight_group_idx,
					      uint16_t formation_slot_idx,
					      uint16_t basis_obj_idx)
{
	uint16_t object_type =
		g_mission_flight_groups[flight_group_idx].fg.craft_type;
	object_type = g_craft_type_to_object_type[object_type];
	uint16_t model_index = g_object_type_table[object_type].model_index;
	int16_t max_z = (int16_t)model_bounds_get_max_z(object_type);

	mission_resolve_object_or_mission_point_world_loc(0x8000,
							  flight_group_idx);
	if ((g_object_type_table[object_type].behavior_flags & 0x80) != 0) {
		if (g_object_type_table[object_type].genus_id == 9) {
			g_world_loc_x =
				g_mission_flight_groups[flight_group_idx]
					.fg.mission_point_x[0] *
				256;
			g_world_loc_y =
				-(g_mission_flight_groups[flight_group_idx]
					  .fg.mission_point_y[0] *
				  256);
			g_world_loc_z =
				g_mission_flight_groups[flight_group_idx]
					.fg.mission_point_z[0] *
				256;
		} else if (g_object_type_table[object_type].genus_id == 8) {
			int16_t x_step = 0;
			int16_t y_step = 0;
			int16_t z_row_step = 0;
			int16_t z_column_step = 0;
			switch (g_mission_flight_groups[flight_group_idx]
					.fg.status1 &
				3) {
			case 0:
				x_step = 64;
				y_step = 64;
				break;
			case 1:
				y_step = 64;
				z_column_step = 64;
				break;
			case 2:
				x_step = 64;
				z_row_step = 64;
				break;
			}

			int16_t craft_count_minus_one =
				(int16_t)(g_mission_flight_groups
						  [flight_group_idx]
							  .fg.number_of_craft -
					  1);
			int16_t base_x =
				(int16_t)(g_mission_flight_groups
						  [flight_group_idx]
							  .fg
							  .mission_point_x[0] -
					  x_step * craft_count_minus_one / 2);
			int16_t base_y = (int16_t)-(
				g_mission_flight_groups[flight_group_idx]
					.fg.mission_point_y[0] +
				y_step * craft_count_minus_one / 2);
			int16_t base_z =
				(int16_t)(g_mission_flight_groups
						  [flight_group_idx]
							  .fg
							  .mission_point_z[0] -
					  z_row_step * craft_count_minus_one /
						  2 -
					  z_column_step *
						  craft_count_minus_one / 2);

			int16_t row = 0;
			uint16_t slot = 0;
			if (g_mission_flight_groups[flight_group_idx]
				    .fg.number_of_craft != 0) {
				do {
					int16_t column = 0;
					if (g_mission_flight_groups
						    [flight_group_idx]
							    .fg
							    .number_of_craft !=
					    0) {
						do {
							if (slot ==
							    formation_slot_idx) {
								g_world_loc_x =
									(base_x +
									 x_step *
										 column) *
									256;
								g_world_loc_y =
									(base_y +
									 y_step *
										 row) *
									256;
								g_world_loc_z =
									(base_z +
									 z_column_step *
										 column +
									 z_row_step *
										 row) *
										256 +
									max_z;
								return;
							}
							++slot;
							++column;
						} while (
							column <
							(int)g_mission_flight_groups
								[flight_group_idx]
									.fg
									.number_of_craft);
					}
					++row;
				} while (row <
					 (int)g_mission_flight_groups
						 [flight_group_idx]
							 .fg.number_of_craft);
			}
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_WARN(
					"mission.mine_slot_outside_grid fg=%d cell=%u size=%d",
					(int)flight_group_idx,
					(unsigned)formation_slot_idx,
					(int)g_mission_flight_groups
						[flight_group_idx]
							.fg.number_of_craft);
			}
		}
	} else {
		int16_t bound_x = g_model_defs[model_index].bound_size_x;
		int16_t bound_z = g_model_defs[model_index].bound_size_z;
		int16_t bound_y = g_model_defs[model_index].bound_size_y;
		int16_t spacing_scale =
			g_mission_flight_groups[flight_group_idx]
				.fg.formation_spacing +
			1;
		uint16_t formation_index =
			formation_slot_idx +
			6 * g_mission_flight_groups[flight_group_idx]
					.fg.formation;
		int16_t forward = spacing_scale;
		forward *= ((const int16_t *)g_form_pos_y)[formation_index];
		forward *= bound_y;
		int16_t up = spacing_scale;
		up *= ((const int16_t *)g_form_pos_z)[formation_index];
		up *= bound_z;
		int16_t side = spacing_scale;
		side *= ((const int16_t *)g_form_pos_x)[formation_index];
		side *= bound_x;

		if (spacing_scale == 1) {
			side += ((const int16_t *)
					 g_form_pos_x)[formation_index] *
				(bound_x / 2);
			up += ((const int16_t *)g_form_pos_z)[formation_index] *
			      (bound_z / 2);
			forward += ((const int16_t *)
					    g_form_pos_y)[formation_index] *
				   (bound_y / 4);
		}
		int rotated_x;
		int rotated_y;
		int rotated_z;
		if (basis_obj_idx == UINT16_MAX) {
			rotated_y = 0;
			rotated_z = 0;
			mission_resolve_object_or_mission_point_world_loc(
				0x8000, flight_group_idx);
			rotated_x = 0;
		} else {
			pai_calcrotatedpoint(&g_object_table[basis_obj_idx],
					     side, up, forward);
			rotated_x = g_rotated_x;
			rotated_y = g_rotated_y;
			rotated_z = g_rotated_z;
		}
		if (g_model_defs[model_index].bound_size_shift != 0) {
			rotated_x *=
				1 << g_model_defs[model_index].bound_size_shift;
			rotated_y *=
				1 << g_model_defs[model_index].bound_size_shift;
			rotated_z *=
				1 << g_model_defs[model_index].bound_size_shift;
		}
		g_rotated_x = rotated_x;
		g_rotated_y = rotated_y;
		g_rotated_z = rotated_z;
		g_world_loc_x += rotated_x;
		g_world_loc_y += rotated_y;
		g_world_loc_z += rotated_z;
	}
	g_world_loc_z += max_z;
}

/* Reads a mission file into g_mission_header, g_mission_flight_groups,
 * g_mission_messages, g_mission_global_goals, g_mission_teams and the goal text
 * handles. Returns 0 when the file does not open or its version, the first
 * two bytes, read into g_mission_file_version, is not 12, 13, 14 or 0xFFFF;
 * else 1 when fe_disk_io_close_global_stream reports no error, 0 when it does.
 * Versions 12 to 14 read the records as stored: each message at the index
 * the file gives, as many global goals per team as the file counts, a team
 * record when its count is nonzero; they skip the briefings and copy each
 * nonempty 64-byte goal text into a new memory handle in
 * g_mission_fg_override_string_handles and g_global_goal_override_string_handles.
 * The version 10 branches inside never run: that test sits under the one
 * for 12 to 14. Version 0xFFFF, the TIE format, reads g_tie_mission_header, a
 * g_tie_flight_group per flight group, a g_tie_radio_message per message and a
 * g_tie_mission_goal per goal and converts them: all groups on team 0, then
 * team 1 for IFF 0 and 4 and for IFF 2, 3 and 5 whose TIE name starts with
 * '1'; player number 1 for a player group; the four goals as primary, bonus,
 * prevent and bonus; the first 15 waypoints; the third order's code from the
 * second TIE order. Then, for every version, it makes craft_role upper case,
 * marks each team allied with itself, and, where a team's first three global
 * goals have a first condition, turns their trailing MISSION_COND_NEVER
 * trigger slots into MISSION_COND_ALWAYS_TRUE joined by AND. Does not check
 * the counts and indexes it reads against the array sizes. */
// FUNCTION: XVT 0x45AD80
int mission_load_file(const char *file_name)
{
	enum {
		MISSION_VERSION_LEGACY_TIE = UINT16_MAX,
		MISSION_VERSION_XVT_10 = 10,
		MISSION_VERSION_XVT_12 = 12,
		MISSION_VERSION_XVT_13 = 13,
		MISSION_VERSION_XVT_14 = 14,
		TEAM_COUNT = 10,
		LEGACY_IFF_NAME_FIRST = 2,
		LEGACY_WAYPOINT_COUNT = 15,
		PLAYER_COUNT = 8,
		BRIEFING_SKIP_SIZE = 810,
		BRIEFING_TEAM_FLAG_COUNT = 10,
		BRIEFING_STRING_COUNT = 32,
		OVERRIDE_STRING_SIZE = 64,
		FG_GOAL_COUNT = 8,
		OVERRIDE_STRING_COUNT = 3,
		GLOBAL_GOAL_COUNT = 7,
		GLOBAL_GOAL_TRIGGER_COUNT = 4,
		ACTIVE_GLOBAL_GOAL_COUNT = 3,
		CRAFT_ROLE_SIZE = 16,
	};

	if (!fe_disk_io_open_global_stream(file_name, "rb", 1, 0)) {
		return 0;
	}

	xvt_file *stream = g_stream;
	fe_disk_io_read_with_retry_prompt(&g_mission_file_version,
					  sizeof(g_mission_file_version), 1,
					  g_stream);
	if (g_mission_file_version != MISSION_VERSION_XVT_14) {
		if (g_mission_file_version != MISSION_VERSION_LEGACY_TIE &&
		    g_mission_file_version != MISSION_VERSION_XVT_12 &&
		    g_mission_file_version != MISSION_VERSION_XVT_13) {
			return 0;
		}
	}

	int16_t index;
	int16_t string_length;
	int16_t flight_group_idx;
	int16_t message_idx;
	int16_t outer_idx;
	if (g_mission_file_version == MISSION_VERSION_XVT_14 ||
	    g_mission_file_version == MISSION_VERSION_XVT_12 ||
	    g_mission_file_version == MISSION_VERSION_XVT_13) {
		if (g_mission_file_version == MISSION_VERSION_XVT_10) {
			fe_disk_io_read_with_retry_prompt(
				&g_xvt_v10_mission_header,
				sizeof(g_xvt_v10_mission_header), 1, stream);
			g_mission_header.num_flight_groups =
				g_xvt_v10_mission_header.num_flight_groups;
			g_mission_header.num_messages =
				g_xvt_v10_mission_header.num_messages;
			g_mission_header.time_limit_min =
				g_xvt_v10_mission_header.time_limit_min;
			g_mission_header.time_limit_sec =
				g_xvt_v10_mission_header.time_limit_sec;
			g_mission_header.win_type =
				g_xvt_v10_mission_header.win_type;
			g_mission_header.backdrop =
				g_xvt_v10_mission_header.backdrop;
			g_mission_header.rescue =
				g_xvt_v10_mission_header.rescue;
			g_mission_header.all_waypoints_shown =
				g_xvt_v10_mission_header.all_waypoints_shown;
			memcpy(g_mission_header.variables,
			       g_xvt_v10_mission_header.variables,
			       sizeof(g_mission_header.variables));
			memcpy(g_mission_header.iff_names[0],
			       g_xvt_v10_mission_header.iff_names[0],
			       sizeof(g_xvt_v10_mission_header.iff_names[0]));
			memcpy(g_mission_header.iff_names[1],
			       g_xvt_v10_mission_header.iff_names[1],
			       sizeof(g_xvt_v10_mission_header.iff_names[1]));
			memcpy(g_mission_header.iff_names[2],
			       g_xvt_v10_mission_header.iff_names[2],
			       sizeof(g_xvt_v10_mission_header.iff_names[2]));
			memcpy(g_mission_header.iff_names[3],
			       g_xvt_v10_mission_header.iff_names[3],
			       sizeof(g_xvt_v10_mission_header.iff_names[3]));
			g_mission_header.mission_type =
				g_xvt_v10_mission_header.mission_type;
		} else {
			struct mission_header mission_header;
			fe_disk_io_read_with_retry_prompt(
				&mission_header, sizeof(mission_header), 1,
				stream);
			memset(&g_mission_header, 0, sizeof(g_mission_header));
			g_mission_header = mission_header;
		}
		XVT_LOG_DEBUG(
			"mission.file_header version=%u type=%d groups=%d messages=%d backdrop=%u minutes=%u",
			(unsigned)g_mission_file_version,
			(int)g_mission_header.mission_type,
			(int)g_mission_header.num_flight_groups,
			(int)g_mission_header.num_messages,
			(unsigned)g_mission_header.backdrop,
			(unsigned)g_mission_header.time_limit_minutes);
		if (g_mission_header.num_flight_groups > 48 ||
		    g_mission_header.num_messages > MISSION_MESSAGE_COUNT) {
			XVT_LOG_WARN(
				"mission.file_counts_over file=\"%s\" groups=%d messages=%d",
				file_name,
				(int)g_mission_header.num_flight_groups,
				(int)g_mission_header.num_messages);
		}

		for (flight_group_idx = 0;
		     flight_group_idx <
		     (int16_t)g_mission_header.num_flight_groups;
		     ++flight_group_idx) {
			if (g_mission_file_version == MISSION_VERSION_XVT_10) {
				fe_disk_io_read_with_retry_prompt(
					&g_xvt_v10_flight_group_text,
					sizeof(g_xvt_v10_flight_group_text), 1,
					stream);
				memcpy(g_mission_flight_groups[flight_group_idx]
					       .fg.name,
				       g_xvt_v10_flight_group_text.name,
				       sizeof(g_xvt_v10_flight_group_text
						      .name));
				memcpy(g_mission_flight_groups[flight_group_idx]
					       .fg.craft_role,
				       g_xvt_v10_flight_group_text.craft_role,
				       sizeof(g_xvt_v10_flight_group_text
						      .craft_role));
				memcpy(g_mission_flight_groups[flight_group_idx]
					       .fg.cargo,
				       g_xvt_v10_flight_group_text.cargo,
				       sizeof(g_xvt_v10_flight_group_text
						      .cargo));
				memcpy(g_mission_flight_groups[flight_group_idx]
					       .fg.special_cargo,
				       g_xvt_v10_flight_group_text
					       .special_cargo,
				       sizeof(g_xvt_v10_flight_group_text
						      .special_cargo));
				fe_disk_io_read_with_retry_prompt(
					&g_mission_flight_groups
						 [flight_group_idx]
							 .fg
							 .special_cargo_craft,
					sizeof(g_mission_flight_groups
						       [flight_group_idx]
							       .fg) -
						offsetof(
							struct xvt_flight_group,
							special_cargo_craft),
					1, stream);
			} else {
				fe_disk_io_read_with_retry_prompt(
					&g_mission_flight_groups
						 [flight_group_idx]
							 .fg,
					sizeof(g_mission_flight_groups
						       [flight_group_idx]
							       .fg),
					1, stream);
			}
			g_mission_flight_groups[flight_group_idx]
				.player_owner_idx = -1;
		}

		for (message_idx = 0;
		     message_idx < (int16_t)g_mission_header.num_messages;
		     ++message_idx) {
			if (g_mission_file_version == MISSION_VERSION_XVT_10) {
				index = (int16_t)message_idx;
			} else {
				fe_disk_io_read_with_retry_prompt(
					&index, sizeof(index), 1, stream);
			}
			if (index < 0 || index >= MISSION_MESSAGE_COUNT) {
				XVT_LOG_WARN(
					"mission.message_index_invalid order=%d message=%d",
					(int)message_idx, (int)index);
			}
			fe_disk_io_read_with_retry_prompt(
				&g_mission_messages[index],
				sizeof(g_mission_messages[0]), 1, stream);
		}

		int16_t record_count;
		for (outer_idx = 0; outer_idx < TEAM_COUNT; ++outer_idx) {
			fe_disk_io_read_with_retry_prompt(
				&record_count, sizeof(record_count), 1, stream);
			XVT_LOG_DEBUG(
				"mission.team_goals_read team=%d count=%d",
				(int)outer_idx, (int)record_count);
			if (record_count < 0 || record_count > 7) {
				XVT_LOG_WARN(
					"mission.goal_count_invalid team=%d count=%d",
					(int)outer_idx, (int)record_count);
			}
			fe_disk_io_read_with_retry_prompt(
				g_mission_global_goals[outer_idx],
				sizeof(g_mission_global_goals[0][0]),
				record_count, stream);
		}
		for (outer_idx = 0; outer_idx < TEAM_COUNT; ++outer_idx) {
			fe_disk_io_read_with_retry_prompt(
				&record_count, sizeof(record_count), 1, stream);
			if (record_count != 0) {
				fe_disk_io_read_with_retry_prompt(
					&g_mission_teams[outer_idx],
					sizeof(g_mission_teams[outer_idx]), 1,
					stream);
			}
		}

		int16_t slot_idx;
		for (index = 0; index < PLAYER_COUNT; ++index) {
			FILE_RAW_SEEK(stream, BRIEFING_SKIP_SIZE, SEEK_CUR);
			for (slot_idx = BRIEFING_TEAM_FLAG_COUNT; slot_idx != 0;
			     --slot_idx) {
				FILE_RAW_SEEK(stream, 1, SEEK_CUR);
			}
			for (slot_idx = BRIEFING_STRING_COUNT; slot_idx != 0;
			     --slot_idx) {
				fe_disk_io_read_with_retry_prompt(
					&string_length, sizeof(string_length),
					1, stream);
				if (string_length != 0) {
					FILE_RAW_SEEK(stream, string_length,
						      SEEK_CUR);
				}
			}
			for (slot_idx = BRIEFING_STRING_COUNT; slot_idx != 0;
			     --slot_idx) {
				fe_disk_io_read_with_retry_prompt(
					&string_length, sizeof(string_length),
					1, stream);
				if (string_length != 0) {
					FILE_RAW_SEEK(stream, string_length,
						      SEEK_CUR);
				}
			}
		}

		char string_buffer[160];
		for (outer_idx = 0;
		     outer_idx < (int16_t)g_mission_header.num_flight_groups;
		     ++outer_idx) {
			for (index = 0; index < FG_GOAL_COUNT; ++index) {
				for (slot_idx = 0;
				     slot_idx < OVERRIDE_STRING_COUNT;
				     ++slot_idx) {
					fe_disk_io_read_with_retry_prompt(
						string_buffer,
						OVERRIDE_STRING_SIZE, 1,
						stream);
					string_length =
						(int16_t)strlen(string_buffer);
					uint16_t handle;
					if (string_length > 0) {
						handle =
							memory_alloc_handle_zeroed(
								string_length +
									1,
								0);
						if (handle != 0) {
							char *string =
								memory_get_handle_block(
									handle);
							memcpy(string,
							       string_buffer,
							       string_length);
							string[string_length] =
								'\0';
						} else {
							XVT_LOG_WARN(
								"mission.fg_goal_text_lost fg=%d goal=%d text=%d bytes=%d",
								(int)outer_idx,
								(int)index,
								(int)slot_idx,
								string_length +
									1);
						}
						memory_handle_block_done_stub(
							handle);
					} else {
						handle = 0;
					}
					g_mission_fg_override_string_handles
						[outer_idx][index][slot_idx] =
							handle;
				}
			}
		}
		for (outer_idx = 0; outer_idx < TEAM_COUNT; ++outer_idx) {
			for (index = 0; index < GLOBAL_GOAL_COUNT; ++index) {
				for (int16_t trigger_idx = 0;
				     trigger_idx < GLOBAL_GOAL_TRIGGER_COUNT;
				     ++trigger_idx) {
					for (slot_idx = 0;
					     slot_idx < OVERRIDE_STRING_COUNT;
					     ++slot_idx) {
						fe_disk_io_read_with_retry_prompt(
							string_buffer,
							OVERRIDE_STRING_SIZE, 1,
							stream);
						string_length = (int16_t)strlen(
							string_buffer);
						uint16_t handle;
						if (string_length > 0) {
							handle = memory_alloc_handle_zeroed(
								string_length +
									1,
								0);
							if (handle != 0) {
								char *string = memory_get_handle_block(
									handle);
								memcpy(string,
								       string_buffer,
								       string_length);
								string[string_length] =
									'\0';
							} else {
								XVT_LOG_WARN(
									"mission.team_goal_text_lost team=%d goal=%d trigger=%d text=%d bytes=%d",
									(int)outer_idx,
									(int)index,
									(int)trigger_idx,
									(int)slot_idx,
									string_length +
										1);
							}
							memory_handle_block_done_stub(
								handle);
						} else {
							handle = 0;
						}
						g_global_goal_override_string_handles
							[outer_idx][index]
							[trigger_idx]
							[slot_idx] = handle;
					}
				}
			}
		}
	} else {
		int16_t flight_group_count;
		fe_disk_io_read_with_retry_prompt(&flight_group_count,
						  sizeof(flight_group_count), 1,
						  stream);
		int16_t message_count;
		fe_disk_io_read_with_retry_prompt(
			&message_count, sizeof(message_count), 1, stream);
		int16_t goal_count;
		fe_disk_io_read_with_retry_prompt(
			&goal_count, sizeof(goal_count), 1, stream);
		if (flight_group_count > 48 ||
		    message_count > MISSION_MESSAGE_COUNT) {
			XVT_LOG_WARN(
				"mission.file_counts_over file=\"%s\" groups=%d messages=%d",
				file_name, (int)flight_group_count,
				(int)message_count);
		}
		if (goal_count < 0 || goal_count > 7) {
			XVT_LOG_WARN(
				"mission.goal_count_invalid team=0 count=%d",
				(int)goal_count);
		}
		g_mission_header.num_flight_groups = flight_group_count;
		g_mission_header.num_messages = message_count;
		fe_disk_io_read_with_retry_prompt(&g_tie_mission_header,
						  sizeof(g_tie_mission_header),
						  1, stream);
		g_mission_header.time_limit_min = g_tie_mission_header.backdrop;
		g_mission_header.rescue = g_tie_mission_header.rescue;
		g_mission_header.all_waypoints_shown =
			g_tie_mission_header.all_way_shown;
		g_mission_header.variables[0] = g_tie_mission_header.mis_var[0];
		g_mission_header.variables[1] = g_tie_mission_header.mis_var[1];
		g_mission_header.variables[2] = g_tie_mission_header.mis_var[2];
		g_mission_header.variables[3] = g_tie_mission_header.mis_var[3];
		g_mission_header.variables[4] = g_tie_mission_header.mis_var[4];
		g_mission_header.variables[5] = g_tie_mission_header.mis_var[5];
		g_mission_header.variables[6] = g_tie_mission_header.mis_var[6];
		g_mission_header.variables[7] = g_tie_mission_header.mis_var[7];
		strcpy(g_mission_teams[0].end_of_mission_messages[0],
		       (char *)g_tie_mission_header.win_msg1[0]);
		strcpy(g_mission_teams[0].end_of_mission_messages[1],
		       (char *)g_tie_mission_header.win_msg1[1]);
		strcpy(g_mission_teams[0].end_of_mission_messages[2],
		       (char *)g_tie_mission_header.loss_msg[0]);
		strcpy(g_mission_teams[0].end_of_mission_messages[3],
		       (char *)g_tie_mission_header.loss_msg[1]);
		strcpy(g_mission_teams[0].end_of_mission_messages[4],
		       (char *)g_tie_mission_header.win_msg2[0]);
		strcpy(g_mission_teams[0].end_of_mission_messages[5],
		       (char *)g_tie_mission_header.win_msg2[1]);
		g_mission_teams[0].eom_raw_delay[1] =
			g_tie_mission_header.loss_msg_delay;
		strcpy(g_mission_header.iff_names[0],
		       g_tie_mission_header.neutral_name[0]);
		strcpy(g_mission_header.iff_names[1],
		       g_tie_mission_header.neutral_name[0]);
		strcpy(g_mission_header.iff_names[2],
		       g_tie_mission_header.neutral_name[0]);
		strcpy(g_mission_header.iff_names[3],
		       g_tie_mission_header.neutral_name[0]);
		XVT_LOG_DEBUG(
			"mission.tie_header groups=%d messages=%d goals=%d",
			(int)flight_group_count, (int)message_count,
			(int)goal_count);

		for (flight_group_idx = 0;
		     flight_group_idx < flight_group_count;
		     ++flight_group_idx) {
			fe_disk_io_read_with_retry_prompt(
				&g_tie_flight_group, sizeof(g_tie_flight_group),
				1, stream);
			strcpy(g_mission_flight_groups[flight_group_idx]
				       .fg.name,
			       g_tie_flight_group.name);
			strcpy(g_mission_flight_groups[flight_group_idx]
				       .fg.craft_role,
			       g_tie_flight_group.cmdr);
			strcpy(g_mission_flight_groups[flight_group_idx]
				       .fg.cargo,
			       g_tie_flight_group.contents[0]);
			strcpy(g_mission_flight_groups[flight_group_idx]
				       .fg.special_cargo,
			       g_tie_flight_group.contents[1]);
			g_mission_flight_groups[flight_group_idx]
				.fg.special_cargo_craft =
				g_tie_flight_group.special_craft;
			g_mission_flight_groups[flight_group_idx]
				.fg.random_special_cargo_craft =
				g_tie_flight_group.special_flag;
			g_mission_flight_groups[flight_group_idx]
				.fg.craft_type = g_tie_flight_group.species;
			g_mission_flight_groups[flight_group_idx]
				.fg.number_of_craft = g_tie_flight_group.count;
			g_mission_flight_groups[flight_group_idx].fg.status1 =
				g_tie_flight_group.status;
			g_mission_flight_groups[flight_group_idx].fg.warhead =
				g_tie_flight_group.warhead;
			g_mission_flight_groups[flight_group_idx].fg.beam =
				g_tie_flight_group.beam;
			g_mission_flight_groups[flight_group_idx].fg.iff =
				g_tie_flight_group.side;
			g_mission_flight_groups[flight_group_idx].fg.team = 0;
			g_mission_flight_groups[flight_group_idx].fg.group_ai =
				g_tie_flight_group.skill;
			g_mission_flight_groups[flight_group_idx].fg.markings =
				g_tie_flight_group.camoflage;
			g_mission_flight_groups[flight_group_idx].fg.radio =
				g_tie_flight_group.camo_flag;
			g_mission_flight_groups[flight_group_idx].fg.unused5c =
				g_tie_flight_group.camo_unused;
			g_mission_flight_groups[flight_group_idx].fg.formation =
				g_tie_flight_group.formation;
			g_mission_flight_groups[flight_group_idx]
				.fg.formation_spacing =
				g_tie_flight_group.form_spacing;
			g_mission_flight_groups[flight_group_idx]
				.fg.global_group = g_tie_flight_group.set;
			g_mission_flight_groups[flight_group_idx].fg.unused60 =
				g_tie_flight_group.set_unused;
			g_mission_flight_groups[flight_group_idx]
				.fg.number_of_waves = g_tie_flight_group.waves;
			g_mission_flight_groups[flight_group_idx]
				.fg.waves_delay = g_tie_flight_group.wave_delay;
			g_mission_flight_groups[flight_group_idx]
				.fg.stop_arriving_when = 0;
			if (g_tie_flight_group.player_flag != 0) {
				g_mission_flight_groups[flight_group_idx]
					.fg.player_number = 1;
				g_mission_flight_groups[flight_group_idx]
					.fg.player_craft =
					g_tie_flight_group.player_flag - 1;
			} else {
				g_mission_flight_groups[flight_group_idx]
					.fg.player_number = 0;
			}
			g_mission_flight_groups[flight_group_idx]
				.fg.arrive_only_if_human = 0;
			g_mission_flight_groups[flight_group_idx].fg.yaw =
				g_tie_flight_group.heading;
			g_mission_flight_groups[flight_group_idx].fg.pitch =
				g_tie_flight_group.pitch;
			g_mission_flight_groups[flight_group_idx].fg.roll =
				g_tie_flight_group.rotation;
			g_mission_flight_groups[flight_group_idx]
				.fg.legacy_perma_death_enabled =
				g_tie_flight_group.link_flag;
			g_mission_flight_groups[flight_group_idx]
				.fg.legacy_perma_death_id =
				g_tie_flight_group.link_code;
			g_mission_flight_groups[flight_group_idx]
				.fg.reserved_perma_death =
				g_tie_flight_group.link_unused;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_difficulty =
				g_tie_flight_group.difficulty;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_triggers[0]
				.triggers[0]
				.condition =
				g_tie_flight_group.start_cond[0].cond;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_triggers[0]
				.triggers[1]
				.condition =
				g_tie_flight_group.start_cond[1].cond;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_triggers[0]
				.triggers[0]
				.variable_type =
				g_tie_flight_group.start_cond[0].type;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_triggers[0]
				.triggers[1]
				.variable_type =
				g_tie_flight_group.start_cond[1].type;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_triggers[0]
				.triggers[0]
				.amount = g_tie_flight_group.start_cond[0].pct;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_triggers[0]
				.triggers[1]
				.amount = g_tie_flight_group.start_cond[1].pct;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_triggers[0]
				.triggers[0]
				.variable = g_tie_flight_group.start_cond[0].id;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_triggers[0]
				.triggers[1]
				.variable = g_tie_flight_group.start_cond[1].id;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_triggers[0]
				.trigger1_or_trigger2 =
				g_tie_flight_group.start_op;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_rand_delay_minutes = 0;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_rand_delay_seconds = 0;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_delay_minutes =
				g_tie_flight_group.start_delay_min;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_delay_seconds =
				g_tie_flight_group.start_delay_sec;
			g_mission_flight_groups[flight_group_idx]
				.fg.departure_trigger.triggers[0]
				.condition = g_tie_flight_group.stop_cond.cond;
			g_mission_flight_groups[flight_group_idx]
				.fg.departure_trigger.triggers[0]
				.variable_type =
				g_tie_flight_group.stop_cond.type;
			g_mission_flight_groups[flight_group_idx]
				.fg.departure_trigger.triggers[0]
				.amount = g_tie_flight_group.stop_cond.pct;
			g_mission_flight_groups[flight_group_idx]
				.fg.departure_trigger.triggers[0]
				.variable = g_tie_flight_group.stop_cond.id;
			g_mission_flight_groups[flight_group_idx]
				.fg.departure_delay_minutes =
				g_tie_flight_group.stop_min;
			g_mission_flight_groups[flight_group_idx]
				.fg.departure_delay_seconds =
				g_tie_flight_group.stop_sec;
			g_mission_flight_groups[flight_group_idx]
				.fg.abort_trigger =
				g_tie_flight_group.stop_abort;
			memcpy((uint8_t *)&g_mission_flight_groups
					       [flight_group_idx]
						       .fg.editor_mothership +
				       1,
			       &g_tie_flight_group.cur_start_fg,
			       sizeof(g_tie_flight_group.cur_start_fg));
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_mothership =
				g_tie_flight_group.start_fg;
			g_mission_flight_groups[flight_group_idx]
				.fg.arrival_method =
				g_tie_flight_group.start_fg_used;
			g_mission_flight_groups[flight_group_idx]
				.fg.departure_mothership =
				g_tie_flight_group.pri_stop_fg;
			g_mission_flight_groups[flight_group_idx]
				.fg.departure_method =
				g_tie_flight_group.pri_stop_fg_used;
			g_mission_flight_groups[flight_group_idx]
				.fg.alternate_mothership =
				g_tie_flight_group.secondary_stop_fg;
			g_mission_flight_groups[flight_group_idx]
				.fg.alternate_mothership_used =
				g_tie_flight_group.secondary_stop_fg_used;
			g_mission_flight_groups[flight_group_idx]
				.fg.captured_departure_mothership =
				g_tie_flight_group.capture_fg;
			g_mission_flight_groups[flight_group_idx]
				.fg.captured_depart_via_mothership =
				g_tie_flight_group.capture_fg_used;

			memcpy(&g_mission_flight_groups[flight_group_idx]
					.fg.orders[0],
			       &g_tie_flight_group.ai[0],
			       sizeof(g_tie_flight_group.ai[0]));
			memcpy(&g_mission_flight_groups[flight_group_idx]
					.fg.orders[1],
			       &g_tie_flight_group.ai[1],
			       sizeof(g_tie_flight_group.ai[1]));
			memcpy(&g_mission_flight_groups[flight_group_idx]
					.fg.orders[2],
			       &g_tie_flight_group.ai[2],
			       sizeof(g_tie_flight_group.ai[2]));
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[0]
				.target1_type =
				g_tie_flight_group.ai[0].pri_type;
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[0]
				.target2_type =
				g_tie_flight_group.ai[0].secondary_type;
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[1]
				.target1_type =
				g_tie_flight_group.ai[1].pri_type;
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[1]
				.target2_type =
				g_tie_flight_group.ai[1].secondary_type;
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[2]
				.target1_type =
				g_tie_flight_group.ai[2].pri_type;
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[2]
				.target2_type =
				g_tie_flight_group.ai[2].secondary_type;
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[0]
				.order = g_tie_flight_group.ai[0].order;
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[1]
				.order = g_tie_flight_group.ai[1].order;
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[2]
				.order = g_tie_flight_group.ai[1].order;
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[0]
				.target1 = g_tie_flight_group.ai[0].pri_id;
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[0]
				.target2 =
				g_tie_flight_group.ai[0].secondary_id;
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[1]
				.target1 = g_tie_flight_group.ai[1].pri_id;
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[1]
				.target2 =
				g_tie_flight_group.ai[1].secondary_id;
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[2]
				.target1 = g_tie_flight_group.ai[2].pri_id;
			g_mission_flight_groups[flight_group_idx]
				.fg.orders[2]
				.target2 =
				g_tie_flight_group.ai[2].secondary_id;

			g_mission_flight_groups[flight_group_idx]
				.fg.goals[0]
				.goal_kind = 0;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[0]
				.event_condition =
				g_tie_flight_group.pri_win_cond;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[0]
				.amount = g_tie_flight_group.pri_win_pct;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[0]
				.enabled_teams[0] = 1;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[1]
				.goal_kind = 2;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[1]
				.event_condition =
				g_tie_flight_group.secondary_win_cond;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[1]
				.amount = g_tie_flight_group.secondary_win_pct;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[1]
				.enabled_teams[0] = 1;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[2]
				.goal_kind = 1;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[2]
				.event_condition = g_tie_flight_group.loss_cond;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[2]
				.amount = g_tie_flight_group.loss_pct;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[2]
				.enabled_teams[0] = 1;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[3]
				.goal_kind = 2;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[3]
				.event_condition =
				g_tie_flight_group.bonus_cond;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[3]
				.amount = g_tie_flight_group.bonus_pct;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[3]
				.points = g_tie_flight_group.bonus_points;
			g_mission_flight_groups[flight_group_idx]
				.fg.goals[3]
				.enabled_teams[0] = 1;

			for (index = 0; index < LEGACY_WAYPOINT_COUNT;
			     ++index) {
				g_mission_flight_groups[flight_group_idx]
					.fg.mission_point_x[index] =
					g_tie_flight_group.way_x[index];
				g_mission_flight_groups[flight_group_idx]
					.fg.mission_point_y[index] =
					g_tie_flight_group.way_y[index];
				g_mission_flight_groups[flight_group_idx]
					.fg.mission_point_z[index] =
					g_tie_flight_group.way_z[index];
				g_mission_flight_groups[flight_group_idx]
					.fg.mission_point_enabled[index] =
					g_tie_flight_group.way_used[index];
			}
			g_mission_flight_groups[flight_group_idx]
				.fg.reserved_options_prefix[0] =
				g_tie_flight_group.way_shown;
			g_mission_flight_groups[flight_group_idx]
				.fg.reserved_options_prefix[1] =
				g_tie_flight_group.way_unused;
			g_mission_flight_groups[flight_group_idx]
				.fg.reserved_options_prefix[2] =
				g_tie_flight_group.way_brief_link;
			g_mission_flight_groups[flight_group_idx]
				.fg.reserved_options[0] =
				g_tie_flight_group.way_brief_shown;

			if (g_mission_flight_groups[flight_group_idx].fg.iff ==
				    0 ||
			    g_mission_flight_groups[flight_group_idx].fg.iff ==
				    4) {
				g_mission_flight_groups[flight_group_idx]
					.fg.team = 1;
			} else if (g_mission_flight_groups[flight_group_idx]
						   .fg.iff == 2 ||
				   g_mission_flight_groups[flight_group_idx]
						   .fg.iff == 3 ||
				   g_mission_flight_groups[flight_group_idx]
						   .fg.iff == 5) {
				if (g_tie_mission_header.neutral_name
					    [g_mission_flight_groups
						     [flight_group_idx]
							     .fg.iff -
					     LEGACY_IFF_NAME_FIRST][0] == '1') {
					g_mission_flight_groups
						[flight_group_idx]
							.fg.team = 1;
				} else {
					g_mission_flight_groups
						[flight_group_idx]
							.fg.team = 0;
				}
			} else {
				g_mission_flight_groups[flight_group_idx]
					.fg.team = 0;
			}
			g_mission_flight_groups[flight_group_idx]
				.player_owner_idx = -1;
		}

		for (message_idx = 0; message_idx < message_count;
		     ++message_idx) {
			fe_disk_io_read_with_retry_prompt(
				&g_tie_radio_message,
				sizeof(g_tie_radio_message), 1, stream);
			strncpy(g_mission_messages[message_idx].message,
				g_tie_radio_message.message,
				sizeof(g_mission_messages[message_idx]
					       .message));
			g_mission_messages[message_idx]
				.trigger_pairs[0]
				.triggers[0]
				.condition =
				g_tie_radio_message.conditions[0].cond;
			g_mission_messages[message_idx]
				.trigger_pairs[0]
				.triggers[1]
				.condition =
				g_tie_radio_message.conditions[1].cond;
			g_mission_messages[message_idx]
				.trigger_pairs[0]
				.triggers[0]
				.variable_type =
				g_tie_radio_message.conditions[0].type;
			g_mission_messages[message_idx]
				.trigger_pairs[0]
				.triggers[1]
				.variable_type =
				g_tie_radio_message.conditions[1].type;
			g_mission_messages[message_idx]
				.trigger_pairs[0]
				.triggers[0]
				.amount = g_tie_radio_message.conditions[0].pct;
			g_mission_messages[message_idx]
				.trigger_pairs[0]
				.triggers[1]
				.amount = g_tie_radio_message.conditions[1].pct;
			g_mission_messages[message_idx]
				.trigger_pairs[0]
				.trigger1_or_trigger2 =
				g_tie_radio_message.condition1_or_condition2;
			g_mission_messages[message_idx]
				.trigger_pairs[0]
				.triggers[0]
				.variable =
				g_tie_radio_message.conditions[0].id;
			g_mission_messages[message_idx]
				.trigger_pairs[0]
				.triggers[1]
				.variable =
				g_tie_radio_message.conditions[1].id;
			strncpy(g_mission_messages[message_idx].voice,
				g_tie_radio_message.voice,
				sizeof(g_mission_messages[message_idx].voice));
			g_mission_messages[message_idx].delay5s =
				g_tie_radio_message.delay5s;
			g_mission_messages[message_idx].sent_to_team[0] = 1;
		}

		{
			for (int16_t legacy_goal_idx = 0;
			     legacy_goal_idx < goal_count; ++legacy_goal_idx) {
				fe_disk_io_read_with_retry_prompt(
					&g_tie_mission_goal,
					sizeof(g_tie_mission_goal), 1, stream);
				g_mission_global_goals[0][legacy_goal_idx]
					.trigger_pairs[0]
					.triggers[0]
					.condition =
					g_tie_mission_goal.subcond[0].cond;
				g_mission_global_goals[0][legacy_goal_idx]
					.trigger_pairs[0]
					.triggers[1]
					.condition =
					g_tie_mission_goal.subcond[1].cond;
				g_mission_global_goals[0][legacy_goal_idx]
					.trigger_pairs[0]
					.triggers[0]
					.variable_type =
					g_tie_mission_goal.subcond[0].type;
				g_mission_global_goals[0][legacy_goal_idx]
					.trigger_pairs[0]
					.triggers[1]
					.variable_type =
					g_tie_mission_goal.subcond[1].type;
				g_mission_global_goals[0][legacy_goal_idx]
					.trigger_pairs[0]
					.triggers[0]
					.amount =
					g_tie_mission_goal.subcond[0].pct;
				g_mission_global_goals[0][legacy_goal_idx]
					.trigger_pairs[0]
					.triggers[1]
					.amount =
					g_tie_mission_goal.subcond[1].pct;
				g_mission_global_goals[0][legacy_goal_idx]
					.trigger_pairs[0]
					.trigger1_or_trigger2 =
					g_tie_mission_goal.or_joined;
				g_mission_global_goals[0][legacy_goal_idx]
					.trigger_pairs[0]
					.triggers[0]
					.variable =
					g_tie_mission_goal.subcond[0].id;
				g_mission_global_goals[0][legacy_goal_idx]
					.trigger_pairs[0]
					.triggers[1]
					.variable =
					g_tie_mission_goal.subcond[1].id;
				strncpy(g_mission_global_goals[0]
							      [legacy_goal_idx]
								      .name,
					(char *)g_tie_mission_goal.editor_name,
					sizeof(g_mission_global_goals
						       [0][legacy_goal_idx]
							       .name));
				g_mission_global_goals[0][legacy_goal_idx]
					.version =
					g_tie_mission_goal.editor_name[sizeof(
						g_mission_global_goals
							[0][legacy_goal_idx]
								.name)];
				g_mission_global_goals[0][legacy_goal_idx]
					.raw_delay = g_tie_mission_goal.pad[0];
				g_mission_global_goals[0][legacy_goal_idx]
					.raw_points = 0;
			}
		}

		FILE_RAW_SEEK(stream, BRIEFING_SKIP_SIZE, SEEK_CUR);
		{
			for (int16_t first_string_count = BRIEFING_STRING_COUNT;
			     first_string_count != 0; --first_string_count) {
				fe_disk_io_read_with_retry_prompt(
					&string_length, sizeof(string_length),
					1, stream);
				if (string_length != 0) {
					FILE_RAW_SEEK(stream, string_length,
						      SEEK_CUR);
				}
			}
			for (int16_t second_string_count =
				     BRIEFING_STRING_COUNT;
			     second_string_count != 0; --second_string_count) {
				fe_disk_io_read_with_retry_prompt(
					&string_length, sizeof(string_length),
					1, stream);
				if (string_length != 0) {
					FILE_RAW_SEEK(stream, string_length,
						      SEEK_CUR);
				}
			}
		}
	}

	for (flight_group_idx = 0;
	     flight_group_idx < (int16_t)g_mission_header.num_flight_groups;
	     ++flight_group_idx) {
		index = 0;
		if (g_mission_flight_groups[flight_group_idx]
			    .fg.craft_role[0] != '\0') {
			do {
				if (index >= CRAFT_ROLE_SIZE) {
					break;
				}
				char role_character =
					g_mission_flight_groups
						[flight_group_idx]
							.fg.craft_role[index];
				if (role_character >= 'a' &&
				    role_character <= 'z') {
					g_mission_flight_groups
						[flight_group_idx]
							.fg.craft_role[index] =
						role_character - ('a' - 'A');
				}
				++index;
			} while (g_mission_flight_groups[flight_group_idx]
					 .fg.craft_role[index] != '\0');
		}
	}
	for (outer_idx = 0; outer_idx < TEAM_COUNT; ++outer_idx) {
		g_mission_teams[outer_idx].allies[outer_idx] = 1;
	}

	for (outer_idx = 0; outer_idx < TEAM_COUNT; ++outer_idx) {
		for (index = 0; index < ACTIVE_GLOBAL_GOAL_COUNT; ++index) {
			struct global_goal *goal =
				&g_mission_global_goals[outer_idx][index];
			uint8_t condition1 =
				goal->trigger_pairs[0].triggers[0].condition;
			uint8_t condition2 =
				goal->trigger_pairs[0].triggers[1].condition;
			uint8_t condition3 =
				goal->trigger_pairs[1].triggers[0].condition;
			uint8_t condition4 =
				goal->trigger_pairs[1].triggers[1].condition;

			if (condition1 == MISSION_COND_NEVER) {
				if (condition2 == MISSION_COND_NEVER &&
				    condition3 == MISSION_COND_NEVER &&
				    condition4 == MISSION_COND_NEVER) {
					continue;
				}
			} else {
				if (condition2 == MISSION_COND_NEVER &&
				    condition3 == MISSION_COND_NEVER &&
				    condition4 == MISSION_COND_NEVER) {
					goal->trigger_pairs[0]
						.triggers[1]
						.condition =
						MISSION_COND_ALWAYS_TRUE;
					goal->trigger_pairs[0]
						.trigger1_or_trigger2 = 0;
					goal->trigger_pairs[1]
						.triggers[0]
						.condition =
						MISSION_COND_ALWAYS_TRUE;
					goal->trigger_pairs[1]
						.triggers[1]
						.condition =
						MISSION_COND_ALWAYS_TRUE;
					goal->trigger_pairs[1]
						.trigger1_or_trigger2 = 0;
					goal->trigger_pair1_or_trigger_pair2 =
						0;
					XVT_LOG_DEBUG(
						"mission.goal_triggers_filled team=%d goal=%d slots=3",
						(int)outer_idx, (int)index);
					continue;
				}
				if (condition2 != MISSION_COND_NEVER &&
				    condition3 == MISSION_COND_NEVER &&
				    condition4 == MISSION_COND_NEVER) {
					goal->trigger_pairs[1]
						.triggers[0]
						.condition =
						MISSION_COND_ALWAYS_TRUE;
					goal->trigger_pairs[1]
						.triggers[1]
						.condition =
						MISSION_COND_ALWAYS_TRUE;
					goal->trigger_pairs[1]
						.trigger1_or_trigger2 = 0;
					goal->trigger_pair1_or_trigger_pair2 =
						0;
					XVT_LOG_DEBUG(
						"mission.goal_triggers_filled team=%d goal=%d slots=2",
						(int)outer_idx, (int)index);
					continue;
				}
			}
			if (condition1 != MISSION_COND_NEVER &&
			    condition2 != MISSION_COND_NEVER &&
			    condition3 != MISSION_COND_NEVER &&
			    condition4 == MISSION_COND_NEVER) {
				goal->trigger_pairs[1].triggers[1].condition =
					MISSION_COND_ALWAYS_TRUE;
				goal->trigger_pairs[1].trigger1_or_trigger2 = 0;
				XVT_LOG_DEBUG(
					"mission.goal_triggers_filled team=%d goal=%d slots=1",
					(int)outer_idx, (int)index);
			}
		}
	}

	g_stream = stream;
	return fe_disk_io_close_global_stream(0) == 0;
}

/* For each player in the session roster that matches a network player in
 * g_pilot_data by DirectPlay id, stores that id in
 * g_players[slot].network.direct_play_id, slot from
 * net_session_find_player_slot_by_dpid; does not check that the slot is below 8.
 * Always returns 1. */
// FUNCTION: XVT 0x45C250
int mission_sync_pilot_network_players_to_session_slots(void)
{
	int session_player_count;
	int pilot_player_index;

	struct session_player_info *session_players =
		net_session_get_player_roster(&session_player_count);
	for (int session_player_index = 0;
	     session_player_index < session_player_count;
	     ++session_player_index) {
		for (pilot_player_index = 0; pilot_player_index < 8;
		     ++pilot_player_index) {
			if (g_pilot_data.network_players[pilot_player_index]
					    .direct_play_id != 0 &&
			    g_pilot_data.network_players[pilot_player_index]
					    .direct_play_id ==
				    session_players[session_player_index]
					    .direct_play_id) {
				break;
			}
		}

		if (pilot_player_index < 8) {
			XVT_LOG_DEBUG(
				"mission.session_player_bound entry=%d player=%u",
				pilot_player_index,
				(unsigned)session_players[session_player_index]
					.direct_play_id);
			g_players[net_session_find_player_slot_by_dpid(
					  session_players[session_player_index]
						  .direct_play_id)]
				.network.direct_play_id =
				session_players[session_player_index]
					.direct_play_id;
		} else {
			XVT_LOG_WARN(
				"mission.session_player_unmatched player=%u players=%d",
				(unsigned)session_players[session_player_index]
					.direct_play_id,
				session_player_count);
		}
	}

	return 1;
}

/* Frees every nonzero handle in g_mission_fg_override_string_handles (for the
 * mission's flight groups) and g_global_goal_override_string_handles. The handles
 * stay in the arrays; nothing here sets them to 0. */
// FUNCTION: XVT 0x45C2D0
void mission_free_override_string_handles(void)
{
	unsigned int handle;

	for (int flight_group_idx = 0;
	     flight_group_idx < (int16_t)g_mission_header.num_flight_groups;
	     ++flight_group_idx) {
		for (int fg_goal_idx = 0; fg_goal_idx < 8; ++fg_goal_idx) {
			for (int slot_idx = 0; slot_idx < 3; ++slot_idx) {
				handle = g_mission_fg_override_string_handles
					[flight_group_idx][fg_goal_idx]
					[slot_idx];
				if (handle != 0) {
					memory_free_handle(handle);
				}
			}
		}
	}

	for (int team_idx = 0; team_idx < 10; ++team_idx) {
		for (int goal_idx = 0; goal_idx < 7; ++goal_idx) {
			for (int condition_idx = 0; condition_idx < 4;
			     ++condition_idx) {
				for (int text_idx = 0; text_idx < 3;
				     ++text_idx) {
					uint16_t goal_handle =
						g_global_goal_override_string_handles
							[team_idx][goal_idx]
							[condition_idx]
							[text_idx];
					if (goal_handle != 0) {
						handle = goal_handle;
						memory_free_handle(
							(uint16_t)handle);
					}
				}
			}
		}
	}
}
