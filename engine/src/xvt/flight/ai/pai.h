#ifndef XVT_FLIGHT_AI_PAI_H
#define XVT_FLIGHT_AI_PAI_H

#include <stdint.h>
#include <stdio.h>

#include "xvt/assets/file.h"
#include "xvt/flight/ai/paiorder.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stored as uint8_t in the binary. Values index the maneuver dispatch tables. */
typedef uint8_t ai_maneuver_mode;

enum {
	AI_MANEUVER_MODE_NULL = 0,
	AI_MANEUVER_MODE_TURN_INSIDE = 1,
	AI_MANEUVER_MODE_SPLITS = 2,
	AI_MANEUVER_MODE_IMMELMANN = 3,
	AI_MANEUVER_MODE_SCISSORS = 4,
	AI_MANEUVER_MODE_RENDEZVOUS = 5,
	AI_MANEUVER_MODE_CRUISE = 6,
	AI_MANEUVER_MODE_HEAD_TOWARD_FULL = 7,
	AI_MANEUVER_MODE_RUN_AWAY = 8,
	AI_MANEUVER_MODE_HEAD_ON_ATTACK = 9,
	AI_MANEUVER_MODE_FOLLOW_LEADER = 10,
	AI_MANEUVER_MODE_SETUP_ATTACK = 11,
	AI_MANEUVER_MODE_ATTACK = 12,
	AI_MANEUVER_MODE_ZOOM = 13,
	AI_MANEUVER_MODE_DIVE = 14,
	AI_MANEUVER_MODE_SPLITS_DIVE = 15,
	AI_MANEUVER_MODE_SPEED_AWAY = 16,
	AI_MANEUVER_MODE_ESCORT = 17,
	AI_MANEUVER_MODE_BOARD = 18,
	AI_MANEUVER_MODE_AWAIT_BOARD = 19,
	AI_MANEUVER_MODE_HEAD_TOWARD = 20,
	AI_MANEUVER_MODE_INTO_HYPERSPACE = 21,
	AI_MANEUVER_MODE_OUT_OF_HYPERSPACE = 22,
	AI_MANEUVER_MODE_ROCKET_ATTACK = 23,
	AI_MANEUVER_MODE_TURN_AWAY = 24,
	AI_MANEUVER_MODE_STOP = 25,
	AI_MANEUVER_MODE_OUT_OF_HANGAR = 26,
	AI_MANEUVER_MODE_EVASIVE = 27,
	AI_MANEUVER_MODE_AVOID_STARSHIP = 28,
	AI_MANEUVER_MODE_WAIT = 29,
	AI_MANEUVER_MODE_DROPOFF = 30,
	AI_MANEUVER_MODE_KAMIKAZE = 31,
	AI_MANEUVER_MODE_AVOID_ATTACKER = 32,
	AI_MANEUVER_MODE_KAMIKAZE_COPY = 33,
	AI_MANEUVER_MODE_COUNT = 34,
};

struct ai_controller {
	/* Mission order slot, 0 to 3, the craft works on: 0 at spawn, moved by
	 * the order-switching orders, 3 after a skip to order 4. */
	uint8_t current_order_slot;
	/* Completion state and goal progress of each order slot. */
	struct ai_order_progress order_progress;
	/* 1 once the skip-to-order-4 trigger moved the craft to slot 3. */
	uint8_t skipped_to_order4;
	/* The plan the craft runs now: pai_setupcraftcontext reads its orders,
	 * and pai_process_plan replaces it when one of them fires. */
	uint8_t running_plan_id;
	/* Leader plan of the craft's current order, for a follower too, set
	 * when the order starts or changes; plans and orders test it by
	 * name. */
	uint8_t current_plan_id;
	/* The mission point the craft is flying to; waypoints are points 4 to 11. The drop-off order reuses
	 * it: it starts at 0 when the craft reaches the destination group, names the formation slot of the
	 * next craft to deliver, and counts the deliveries made. */
	uint8_t waypoint_index; /* 4 at spawn; past 11 it wraps to 4. */
	/* Plan the craft ran when a player's radio command put it on
	 * craftwaitforgopln or starshipwaitforgopln; the commands that release
	 * it from craftwaitforgopln put it back. 0 at spawn. */
	uint8_t saved_plan_id;
	/* Ticks between two AI thinks: the AI level's g_ai_think_interval_by_skill
	 * entry at spawn; a few orders and maneuvers set 29 or 59. */
	int think_interval;
	/* Ticks to the next AI think. flight_update_timers lowers it by the
	 * ticks that pass; at 0 or below pai_update_all_craft_ai thinks and adds
	 * think_interval. */
	int think_timer;
	/* The craft's own state of the game's random generator:
	 * pai_update_all_craft_ai and collide_damagecraft swap it into
	 * g_game_rand_feedback_state while they work on the craft. game_rand() XOR
	 * 0xBEEF at spawn. */
	int16_t saved_rand_seed;
	/* What the craft steers or fires at: an object index, 0x8000 plus a
	 * mission point of its flight group (0x8000 alone for the group's
	 * current point), or 0xFFFF for none. */
	uint16_t target_obj_idx;
	/* The target object's object_signature when it was chosen, to spot its
	 * slot holding another object later; 0 for a mission point. */
	uint16_t target_signature;
	/* Mesh of the target that warheads fired now aim at; laser.c copies it
	 * into each warhead's guidance. 0xFFFF at spawn. */
	uint16_t target_component;
	/* 1 when a target search or command set the target, 0 when an order
	 * pointed the craft at a mission point or, in
	 * paifight_coverleaderorder, at its leader's attacker. Apart from
	 * paiorder_leaderdeadorder copying it, read only for the cannons on
	 * disableldr1pln: with it set the ion cannons fire, their speed setting
	 * the aim ahead, and without it the others fire. */
	uint8_t has_live_target;
	/* X of the world point the craft steers at: its target's position, a
	 * point ahead of it, a hangar or docking point, or a mission point. */
	int aim_point_x;
	int aim_point_y; /* Y of the aim point. */
	/* Z of the aim point; the climb test of the steering code compares it
	 * with the craft's Z. */
	int aim_point_z;
	/* Target a player's command gave the craft, which the target orders
	 * take up; 0xFFFF for none. Another radio command sets AI_TARGET_ABORT
	 * (251), which paiorder_evasiveorder answers by dropping the target. */
	uint16_t candidate_target_idx;
	/* Flight group the craft escorts, set by paifight_checkescortorder; 255
	 * for none and at spawn. */
	uint8_t escort_target_fg;
	/* Pitch the steering turns the craft to, in angle units, 0x4000 being
	 * level; 0x4000 at spawn. */
	uint16_t target_z_angle;
	/* Roll the steering turns the craft to; with roll_state 3 only its top
	 * bit counts, choosing the way the craft keeps rolling. */
	uint16_t target_roll;
	/* Heading the steering turns the craft to, in angle units. */
	uint16_t target_xy_angle;
	/* Maneuver the craft flies, an AI_MANEUVER_MODE_ value;
	 * paiorder_updatecourseorder runs its step function on each think. */
	ai_maneuver_mode maneuver_mode;
	/* Step within the maneuver; paiman_initmaneuver sets 0. */
	uint8_t maneuver_phase;
	/* Ticks left in the maneuver or its current step; flight_update_timers
	 * lowers it to 0. The HUD's target display shows it as the order
	 * time. */
	int maneuver_timer;
	/* Ticks to a maneuver's next sub-step, such as a weave or a course
	 * update; flight_update_timers lowers it to 0. */
	int16_t secondary_maneuver_timer;
};

struct ai_flight_state {
	/* Object whose shot last hit the craft, set by collide.c on each hit;
	 * 0xFFFF when a plan starts. */
	uint16_t threat_obj_idx;
	/* Object the craft last collided with, kept while the roll that gave it
	 * lasts; 0xFFFF otherwise and at spawn. */
	uint16_t impact_obj_idx;
	/* 1 once all the craft's orders are complete, or its leader's are; it
	 * lets the departure count as departed. */
	uint8_t go_home_flag;
	/* 1 once the craft has aborted the mission, by
	 * paiorder_abortmissionorder or a player's radio command; the abort
	 * outcome is counted once. */
	uint8_t mission_aborted_flag;
	/* 1 once the flight group's departure has started for the craft, by
	 * paiorder_stopgohomeorder or from its leader; the delay counts from
	 * the departClock fields. */
	uint8_t depart_timer_flag;
	/* Mission clock hours when the departure started. */
	uint8_t depart_clock_hours;
	/* Mission clock minutes when the departure started. */
	uint8_t depart_clock_minutes;
	/* Mission clock seconds when the departure started. */
	uint8_t depart_clock_seconds;
	/* Warheads fired since the maneuver started; paifight_fightershootorder
	 * stops at its limit. */
	uint8_t warheads_fired_this_maneuver;
	/* Hits taken since the maneuver started; paiman_attackmaneuver breaks
	 * off at the model's reaction_threshold. */
	uint8_t hits_this_maneuver;
	/* 1 once a boarding of this craft counted its group's
	 * FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION outcome. */
	uint8_t boarded_accounting_done;
	uint8_t times_boarded; /* Times other craft have boarded this one. */
	/* 1 once this craft's first docking counted its group's
	 * FLIGHT_GROUP_OUTCOME_FAILED_MISSION outcome. */
	uint8_t docking_accounting_done;
	/* Signatures recorded in docked_target_signatures, at most 9 after a
	 * docking: a tenth docking writes slot 9, which no search reads, and
	 * later ones overwrite it. */
	uint8_t docked_target_count;
	/* Signatures of objects the craft has docked with, so it does not board
	 * them again. Slot 0 also keeps think_interval while the craft arrives
	 * from hyperspace. */
	uint16_t docked_target_signatures[10];
	/* The model's max_speed, copied at spawn: the base of the AI's speeds,
	 * and 0 for a craft that cannot move, which several orders skip. */
	int16_t max_speed_cache;
	/* Set to -1 at spawn; nothing reads it except the world snapshot's
	 * copy. */
	int16_t motion_scale;
	/* 1 while a climb runs, which the steering code ends at the aim point's
	 * Z by setting a level pitch. Only paiman_cruisemaneuver sets 1, and
	 * its own paiman_setflighttotarget call clears it in the same think. */
	uint8_t climb_state;
	/* 1 while a dive pull-out runs, which
	 * flight_update_dive_pullout_pitch_target steps and ends with 2. Nothing
	 * sets it to 1, so that function never runs. */
	uint8_t dive_state;
	/* The model's pitch rate, in angle units per
	 * SIMULATION_TICKS_PER_SECOND ticks at a full step, copied at spawn. */
	int16_t pitch_rate;
	/* Fraction of 65,536 scaling the pitch step; -1, all of it, from spawn
	 * on. */
	int16_t pitch_accel;
	/* 0 no pitch; 1 lowers the pitch value toward target_z_angle; 2 raises
	 * it; 3 reached. */
	uint8_t pitch_state;
	/* 1 to pitch on past target_z_angle: when the pitch crosses 0 or 0x8000
	 * the steering flips yaw and roll by half a circle, clears this and
	 * turns back toward the target. */
	uint8_t pitch_through_loop;
	/* Fraction of 65,536 scaling the pitch step. */
	uint16_t pitch_step_scale;
	/* The model's roll rate, in angle units per SIMULATION_TICKS_PER_SECOND
	 * ticks at a full step, copied at spawn. */
	int16_t roll_rate;
	/* Fraction of 65,536 scaling the roll step; -1, all of it, from spawn
	 * on. */
	int16_t roll_accel;
	/* 0 no roll; 1 or 2 roll to target_roll (nothing sets 2); 3 keep
	 * rolling; 4 reached. In 0 and 4 the steering banks the craft into its
	 * turns. */
	uint8_t roll_state;
	/* Fraction of 65,536 scaling the roll step, which the steering
	 * doubles. */
	uint16_t roll_step;
	/* The model's yaw rate, in angle units per SIMULATION_TICKS_PER_SECOND
	 * ticks at a full step, copied at spawn. */
	int16_t turn_rate;
	/* Fraction of 65,536 scaling the turn step; -1, all of it, from spawn
	 * on. */
	int16_t turn_accel;
	/* 0 no turn; 1, 2 or 3 turn to target_xy_angle, 3 being set when the yaw
	 * gets there. */
	uint8_t turn_state;
	/* Fraction of 65,536 scaling the turn step, kept in 16 signed bits. */
	int16_t turn_step;
	/* Formation, 0 to 33, indexing g_form_pos_x, Y and Z: the flight group's
	 * at spawn, set again when the group leaves a hangar. */
	uint8_t formation_type;
	/* Formation spacing; a follower scales its g_formPos offsets by its
	 * leader's plus 1. The flight group's at spawn; going home sets 1. */
	uint8_t separation;
};

struct pai_plan_token_def {
	/* Token as the plan text spells it; empty in the end entry. */
	char name[80];
	int16_t value; /* Byte the token compiles to. */
};

#pragma pack(push, 1)

struct pai_plan_record {
	/* Plan name, up to 79 characters; empty for a free entry. */
	char name[80];
	/* 1 once the plan text defined the plan, 0 while it is only named. */
	uint8_t is_defined;
	/* Where the plan's bytes start in g_plan_order_data. */
	uint32_t data_offset;
};

#pragma pack(pop)
typedef char xvt_size_pai_plan_record[(sizeof(struct pai_plan_record) == 85)
					      ? 1
					      : -1];

struct pai_context {
	uint16_t object_index;		  /* Object index of the craft. */
	struct craft_data *craft;	  /* The craft's data. */
	struct ai_controller *controller; /* The craft's AI controller. */
	/* The leader's object index, 255 when the craft has none. */
	uint16_t leader_object_index;
	/* The leader's craft data, or the craft's own when it has no leader. */
	struct craft_data *leader_or_self_craft;
	uint16_t craft_flight_group_index; /* The craft's flight group. */
	/* Order slot being worked on: the craft's current_order_slot at setup,
	 * moved by the order-switching orders. */
	uint16_t order_slot;
	/* X of the craft at setup; range tests measure from it. */
	int32_t craft_position_x;
	int32_t craft_position_y; /* Y of the craft at setup. */
	int32_t craft_position_z; /* Z of the craft at setup. */
	/* Skill tier, 0 to 2, of the craft's effective skill. */
	uint16_t skill_tier;
	/* Maneuver byte of the running plan; orders compare maneuver_mode with
	 * it to tell whether the craft still flies its plan's own maneuver. */
	uint16_t initial_maneuver_id;
	/* Next order id in the running plan's bytes; pai_process_plan walks
	 * it. */
	uint8_t *plan_cursor;
	/* 1 when target searches skip craft with no working subsystems; the
	 * disable plans and laser_update_mine_weapon_fire set it, setup clears
	 * it. */
	uint8_t require_undisabled_target;
	/* The plan "variablepln" stands for: nullpln's id at setup, then the
	 * plan an order switch picks for the new order. */
	uint8_t variable_plan_id;
	/* Tests the target searches apply: 1 attack capacity, 4 skill range,
	 * 0x10 range and enemy, 0x20 measure from targetSearchOrigin; 2 is set
	 * but nothing tests it. Setup leaves it as it was. */
	uint8_t target_search_flags;
	/* X of the point searches measure from with target_search_flags 0x20, a
	 * turret hardpoint or the craft's position, set by
	 * paifight_gunneroffenseorder. */
	int32_t target_search_origin_x;
	int32_t target_search_origin_y; /* Y of that search origin. */
	int32_t target_search_origin_z; /* Z of that search origin. */
};

extern struct pai_context g_pai_context;
extern int g_pai_skip_to_order4_checked;
extern int g_last_rough_distance;
extern uint16_t g_ai_skill_value_q16_by_level[8];
extern const uint16_t g_ai_think_interval_by_skill[8];
extern struct pai_plan_record g_plan_table[256];
extern uint8_t g_plan_order_data[0x20000];
extern int g_rotated_x;
extern int g_rotated_y;
extern int g_rotated_z;
extern uint8_t *g_plan_data_ptrs[256];
extern int g_plan_count;
extern const char *const g_builtin_plan_name_table[76];

enum { PAI_PLAN_REPORT_MESSAGE_COUNT = 74 };

extern const uint8_t
	g_plan_report_message_id_by_plan_id[PAI_PLAN_REPORT_MESSAGE_COUNT];
extern uint8_t g_builtin_plan_id_by_name_index[256];
extern uint8_t g_order_leader_builtin_plan_name_index[40];
extern const uint8_t g_order_follower_builtin_plan_name_index[40];

void pai_update_all_craft_ai(void);
void pai_apply_running_plan_target_and_maneuver(unsigned int object_idx);
void pai_process_plan(void);
void pai_setupcraftcontext(uint16_t object_idx);
int pai_skill_value_to_tier(uint16_t skill_value);
uint16_t pai_find_mothership_object(int16_t mothership_flight_group_idx);
int pai_is_object_targetable_near_craft(int unused_craft_obj_idx,
					unsigned int obj_idx, int expand_range);
int16_t pai_is_object_within_skill_range_of_craft(uint16_t obj_idx);
int16_t pai_order_slot_can_board_target(uint16_t order_slot);
int16_t pai_find_boarding_target_from_order(uint16_t order_slot);
int pai_is_object_within_range_of_craft(unsigned int obj_idx,
					unsigned int max_rough_distance);
void pai_update_aim_point_from_order_target(void);
void pai_set_flight_group_formation(unsigned int flight_group_idx,
				    unsigned int formation_type,
				    unsigned int formation_spacing);
void pai_object_ref_direction_to_object_ref(unsigned int from_ref,
					    unsigned int to_ref);
void pai_object_ref_update_rough_distance(unsigned int from_ref,
					  unsigned int to_ref);
void pai_calcrotatedpoint(const struct object_record *obj, int16_t side_arg,
			  int16_t up_arg, int16_t fwd_arg);
void pai_rotate_local_vector_to_world_scratch(
	const struct object_record *obj_record, int local_side, int local_up,
	int local_fwd);
void pai_calc_angles_to_aim_point(void);
int16_t pai_find_nearest_boarding_target(uint16_t target1_type,
					 uint16_t target1,
					 int16_t target_or_mode,
					 uint16_t target2_type,
					 uint16_t target2);
int16_t pai_is_plan_complete_for_order_slot(uint16_t plan_id,
					    uint16_t order_slot);
int16_t pai_is_boarding_plan_complete_for_order_slot(uint16_t plan_id,
						     uint16_t order_slot);
int16_t pai_current_order_targets_match_object(uint16_t object_idx);
uint16_t pai_get_effective_skill_value(struct craft_data *craft);
int pai_setup_context_and_find_order_plan_on_target(int object_idx,
						    int leader_plan_name_index,
						    int target_obj_idx);
int pai_find_plan_table_index_by_name(const char *plan_name);
int pai_find_free_plan_table_index(void);
int pai_find_target_token_index(const char *token);
int pai_find_maneuver_token_index(const char *token);
int pai_find_order_token_index(const char *token);
int pai_read_plan_text_token(char *token, xvt_file *stream);
int pai_compile_plans_from_text(const char *base_name);
int pai_loadplans(const char *base_name);
void pai_cache_builtin_plan_ids(void);
uint8_t *pai_getplandataptrbyname(const char *plan_name);
int pai_find_plan_id_by_name_or_zero(const char *plan_name);

#ifdef __cplusplus
}
#endif

#endif
