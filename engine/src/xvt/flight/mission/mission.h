#ifndef XVT_FLIGHT_MISSION_MISSION_H
#define XVT_FLIGHT_MISSION_MISSION_H

#include <stdint.h>

#include "xvt/assets/object_type.h"
#include "xvt/flight/mission/goals.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

struct mission_order {
	/* Order code; picks the AI plans through
	 * g_order_leader_builtin_plan_name_index and
	 * g_order_follower_builtin_plan_name_index, 0 for none. */
	uint8_t order;
	uint8_t throttle; /* Index into g_order_throttle_to_craft_throttle_speed. */
	uint8_t variable1; /* Parameter whose meaning depends on the order. */
	/* Parameter whose meaning depends on the order; for a drop-off, the
	 * destination flight group plus 1. */
	uint8_t variable2;
	uint8_t variable3; /* Loaded with the record; nothing reads it. */
	uint8_t variable4; /* Loaded with the record; nothing reads it. */
	/* Trigger variable types of targets 3 and 4. */
	uint8_t secondary_target_types[2];
	uint8_t secondary_targets
		[2]; /* Trigger variables of targets 3 and 4. */
	/* 1 joins targets 3 and 4 with OR, else AND. */
	uint8_t target3_or_target4;
	uint8_t unused0;      /* Loaded with the record; nothing reads it. */
	uint8_t target1_type; /* Trigger variable type of target 1. */
	uint8_t target1;      /* Trigger variable of target 1. */
	uint8_t target2_type; /* Trigger variable type of target 2. */
	uint8_t target2;      /* Trigger variable of target 2. */
	/* 1 joins targets 1 and 2 with OR, else AND. */
	uint8_t target1_or_target2;
	uint8_t unused1; /* Loaded with the record; nothing reads it. */
	/* Speed for the order; paiman_initmaneuver sets commanded_speed to 5
	 * times it. */
	uint8_t speed;
	/* Order text mission_setup_draw_flight_assignments shows for the first
	 * order; empty shows a default. */
	char designation[16];
	uint8_t reserved[47]; /* Loaded with the record; nothing reads it. */
};

#pragma pack(pop)
typedef char
	xvt_size_mission_order[(sizeof(struct mission_order) == 82) ? 1 : -1];

extern uint16_t g_target_proximity_blink_bit;
extern uint8_t g_flight_runtime_state_initialized;

#pragma pack(push, 1)

struct xvt_flight_group {
	/* Flight group name, shown on the goals page and in craft names. */
	char name[20];
	/* Up to four 4-character role entries: a team selector (1 to 9, A,
	 * O, F or H) and a code such as COM or MIS. mission_load_file makes
	 * it upper case; mission_init_flight_runtime_state reads it into
	 * runtime.team_fg_designation_code. */
	char craft_role[16];
	/* Loaded with the record; nothing reads it. */
	uint8_t reserved_craft_role[4];
	/* Cargo text of every craft but the special cargo craft. */
	char cargo[20];
	char special_cargo[20]; /* Cargo text of the special cargo craft. */
	/* Number in the group of the special cargo craft. */
	uint8_t special_cargo_craft;
	/* Nonzero makes mission_init pick special_cargo_craft at random. */
	uint8_t random_special_cargo_craft;
	craft_species
		craft_type; ///< Primary craft_species for this mission flight group.
	/* Craft in each round; for mines, the side of the square grid. */
	uint8_t number_of_craft;
	/* Status code: adjusts warheads, shields, hyperdrive and turrets at
	 * spawn and some combat and AI rules; for a backdrop it picks the
	 * image. */
	uint8_t status1;
	/* Warhead choice, an index into g_warhead_type_ids and
	 * g_warhead_ammo_fraction_q16. */
	uint8_t warhead;
	/* Beam type of each craft; object types 1 to 5 get none. */
	uint8_t beam;
	uint8_t iff;  /* IFF of the group's craft. */
	uint8_t team; /* Team, 0 to 9. */
	/* AI level; mission_init raises or lowers it for difficulty and
	 * balance, at most 5 when it raises it. */
	uint8_t group_ai;
	/* Paint scheme, copied to each craft's node_switch_index. */
	uint8_t markings;
	/* Who may radio orders to the group besides its own player and team:
	 * team radio - 1, or the player whose group has player number radio
	 * - 8; 0 none (player_can_radio_command_craft). */
	uint8_t radio;
	uint8_t unused5c; /* Loaded with the record; nothing reads it. */
	/* Formation of each round, a row of g_form_pos_x, g_form_pos_y and
	 * g_form_pos_z. */
	uint8_t formation;
	/* Formation spacing; offsets grow with formation_spacing + 1. */
	uint8_t formation_spacing;
	/* Global group number, matched by trigger variable type 8. */
	uint8_t global_group;
	uint8_t unused60; /* Loaded with the record; nothing reads it. */
	/* Rounds after the first; the craft total is number_of_craft times
	 * number_of_waves + 1. mission_init sets 0 or 99 for player groups as
	 * player_flight_group_wave_mode says. */
	uint8_t number_of_waves;
	uint8_t waves_delay; /* Copied from the TIE format; nothing reads it. */
	/* Rule that ends new rounds: 1 a craft departed, 2 the team's primary
	 * goal complete, 3 the primary failed or the prevent status is 1. */
	uint8_t stop_arriving_when;
	/* Player number, 1 to 8, that may fly the group; 0 for an AI group. */
	uint8_t player_number;
	/* Nonzero keeps the group from arriving unless a player owns it. */
	uint8_t arrive_only_if_human;
	/* Number in the group of the craft the owner flies; for a player's
	 * group mission_init sets 0 when it has one craft, and 1 for 0 when
	 * it has more. */
	uint8_t player_craft;
	uint8_t yaw;   /* Yaw byte of a static object. */
	uint8_t pitch; /* Pitch byte of a static object. */
	uint8_t roll;  /* Roll byte of a static object. */
	/* Copied from the TIE format; nothing reads it. */
	uint8_t legacy_perma_death_enabled;
	/* Copied from the TIE format; nothing reads it. */
	uint8_t legacy_perma_death_id;
	/* Copied from the TIE format; nothing reads it. */
	uint8_t reserved_perma_death;
	/* Index into g_fg_arrival_difficulty_masks: the difficulties the group
	 * arrives in. */
	uint8_t arrival_difficulty;
	/* The two arrival trigger pairs. */
	struct mission_trigger_pair arrival_triggers[2];
	/* 1 joins the two arrival pairs with OR, else AND. */
	uint8_t arrivals12_or_arrivals34;
	/* Random part of the arrival delay, minutes. */
	uint8_t arrival_rand_delay_minutes;
	uint8_t arrival_delay_minutes; /* Fixed arrival delay, minutes. */
	uint8_t arrival_delay_seconds; /* Fixed arrival delay, seconds. */
	/* Trigger pair that sends the craft home and stops new rounds. */
	struct mission_trigger_pair departure_trigger;
	/* Delay after the departure trigger, minutes. */
	uint8_t departure_delay_minutes;
	/* Delay after the departure trigger, seconds. */
	uint8_t departure_delay_seconds;
	/* Condition, 1 to 9, on which a craft aborts its mission (shields,
	 * cannons, launcher, hull or attack); tested by
	 * paiorder_abortmissionorder. */
	uint8_t abort_trigger;
	/* Random part of the arrival delay, seconds. */
	uint8_t arrival_rand_delay_seconds;
	/* Nothing reads it; the TIE loader copies cur_start_fg into its
	 * second byte and reserved_mothership. */
	int16_t editor_mothership;
	uint8_t reserved_mothership; /* Nothing reads it. */
	/* Flight group the group arrives from when arrival_method is set. */
	uint8_t arrival_mothership;
	/* Nonzero: arrive from arrival_mothership's hangar; 0: by hyperspace. */
	uint8_t arrival_method;
	/* Flight group the group departs into when departure_method is set. */
	uint8_t departure_mothership;
	/* Nonzero: depart into departure_mothership; 0: by hyperspace. */
	uint8_t departure_method;
	/* Second mothership to depart into, used when
	 * alternate_mothership_used is set. */
	uint8_t alternate_mothership;
	/* Nonzero when alternate_mothership is set. */
	uint8_t alternate_mothership_used;
	/* Mothership a captured craft departs into, used when
	 * captured_depart_via_mothership is set. */
	uint8_t captured_departure_mothership;
	/* Nonzero when captured_departure_mothership is set. */
	uint8_t captured_depart_via_mothership;
	/* The four orders; the AI follows the current one. */
	struct mission_order orders[4];
	/* Trigger pair that moves the AI to order 4 once it holds; not
	 * tested when both conditions are MISSION_COND_ALWAYS_TRUE. */
	struct mission_trigger_pair skip_to_order4;
	struct flight_group_goal goals[8]; /* The group's eight goals. */
	/* Loaded with the record; nothing reads it. */
	uint8_t reserved_goals_tail;
	/* Mission point x, in mission units (256 world units). Entry 0 is
	 * the start, 1 to 3 other starts, 4 the point arriving craft face;
	 * the AI's waypoints start at entry 4, and it also reads 12 and 13. */
	int16_t mission_point_x[22];
	/* Mission point y; mission_resolve_object_or_mission_point_world_loc
	 * negates it. */
	int16_t mission_point_y[22];
	int16_t mission_point_z[22]; /* Mission point z. */
	/* Nonzero for each mission point that is set. */
	uint16_t mission_point_enabled[22];
	/* Nothing reads it; the TIE loader copies its waypoint flags into
	 * entries 0 to 2. */
	uint8_t reserved_options_prefix[10];
	/* 1 hides craft numbers on the HUD; nonzero numbers craft within the
	 * group even with a global unit. */
	uint8_t disable_wave_numbering;
	/* Mission time, minutes, when the craft depart; 0:00 for none. */
	uint8_t departure_clock_min;
	/* Mission time, seconds, when the craft depart. */
	uint8_t departure_clock_sec;
	uint8_t countermeasures; /* Countermeasure type of each craft. */
	/* Nonzero sets a destroyed craft's breakup to
	 * CRAFT_EXPLOSION_TIME_STEP_TICKS (1180) times it minus 1179 ticks;
	 * 0 gives 8 to 15 simulated seconds at random. */
	uint8_t craft_explosion_time;
	uint8_t status2; /* Second status code, tested with status1. */
	/* Global unit: its craft are numbered across its groups, trigger
	 * variable type 23 matches it, and with radio nonzero a player whose
	 * own group is in it may radio the group. */
	uint8_t global_unit;
	/* Nothing reads it; the TIE loader copies way_brief_shown into entry
	 * 0. */
	uint8_t reserved_options[8];
	uint8_t handicap; /* Loaded with the record; nothing reads it. */
	/* Warheads a player may choose; mission_init applies the choice. */
	uint8_t optional_warheads[8];
	/* Beams a player may choose; mission_init applies the choice. */
	uint8_t optional_beams[6];
	/* Countermeasures a player may choose; mission_init applies the
	 * choice. */
	uint8_t optional_countermeasures[4];
	/* Kind of craft choice; with random variation on, 4 lets
	 * mission_init pick an optional craft at random for a group no
	 * player owns. */
	uint8_t optional_craft_category;
	craft_species optional_craft
		[10]; ///< Alternative craft_species values exposed by mission loadout selection.
	/* number_of_craft that goes with each optional craft. */
	uint8_t number_of_optional_craft[10];
	/* number_of_waves that goes with each optional craft. */
	uint8_t number_of_optional_craft_waves[10];
	uint8_t reserved_tail; /* Loaded with the record; nothing reads it. */
};

#pragma pack(pop)
typedef char xvt_size_xvt_flight_group[(sizeof(struct xvt_flight_group) == 1378)
					       ? 1
					       : -1];

#pragma pack(push, 1)

struct mission_flight_group {
	struct xvt_flight_group
		fg; /* The record as loaded, adjusted by mission_init. */
	int player_owner_idx; /* Player slot that owns the group, -1 for none. */
};

#pragma pack(pop)
typedef char xvt_size_mission_flight_group
	[(sizeof(struct mission_flight_group) == 1382) ? 1 : -1];

extern struct mission_flight_group g_mission_flight_groups[48];
extern struct global_goal g_mission_global_goals[10][7];
extern uint16_t g_current_flight_group_idx;
extern uint16_t g_mission_file_version;
extern uint8_t g_initial_spawn_bind_player_craft_slots;
extern uint8_t g_spawn_leader_obj_idx;
extern char g_mission_debug_buffer[256];
extern uint16_t g_mission_fg_override_string_handles[48][8][3];
extern uint16_t g_global_goal_override_string_handles[10][7][4][3];
extern int g_world_loc_x;
extern int g_world_loc_y;
extern int g_world_loc_z;
#pragma pack(push, 1)

struct team {
	char name[16]; /* Team name, shown on the scoreboard. */
	uint8_t reserved10
		[8]; ///< Mission-file reserved bytes; loaded with the 0x1E5-byte team record and
	///< otherwise unreferenced.
	/* Per team, nonzero when allied; mission_load_file marks each team
	 * allied with itself. */
	uint8_t allies[10];
	/* Texts for the local player: 0 and 1 on primary success, 2 and 3 on
	 * failure, 4 and 5 on bonus success. */
	char end_of_mission_messages[6][64];
	/* Nothing reads it; the TIE loader copies loss_msg_delay into entry
	 * 1. */
	uint8_t eom_raw_delay[3];
	uint8_t eom_source_fg
		[3];	       /* Loaded with the record; nothing reads it. */
	char voice_ids[3][20]; /* Loaded with the record; nothing reads it. */
	uint8_t reserved_tail; ///< Reserved tail byte of the 0x1E5-byte mission team record.
};

#pragma pack(pop)
typedef char xvt_size_team[(sizeof(struct team) == 485) ? 1 : -1];

extern struct team g_mission_teams[10];

/* Stored as int8_t in the binary (IDB enum mission_type). */
typedef int8_t mission_type;

enum {
	MISSION_TYPE_TRAINING = 0x0,
	MISSION_TYPE_SIMULATOR_1 = 0x1,
	MISSION_TYPE_MELEE = 0x2,
	MISSION_TYPE_SIMULATOR_2 = 0x3,
	MISSION_TYPE_COMBAT = 0x4,
};

#pragma pack(push, 1)

struct mission_header {
	int16_t num_flight_groups; /* Flight groups in g_mission_flight_groups. */
	uint16_t num_messages;	   /* Messages in g_mission_messages. */
	/* Nothing in this build reads time_limit_min or time_limit_sec; the mission countdown comes from
	 * time_limit_minutes. A TIE-format mission stores its header's backdrop byte in time_limit_min. */
	uint8_t time_limit_min;
	uint8_t time_limit_sec;
	uint8_t win_type; /* Loaded; nothing reads it. */
	/* Seed of the backdrop and asteroid field layout in mission_init. */
	uint8_t backdrop;
	uint8_t rescue;		     /* Loaded; nothing reads it. */
	uint8_t all_waypoints_shown; /* Loaded; nothing reads it. */
	uint8_t variables[8];	     /* Loaded; nothing reads it. */
	/* Names of IFF 2 to 5; goals_outputgoal skips a '1' at the start. */
	char iff_names[4][20];
	mission_type
		mission_type; ///< One-byte mission mode; XVT uses the shared legacy values through SKIRMISH (0..4).
	uint8_t goals_unimportant; ///< Nonzero suppresses normal mission-goal importance/failure handling.
	uint8_t time_limit_minutes; ///< Mission countdown duration in whole minutes; zero disables the
	///< header-supplied limit.
	uint8_t reserved[61]; /* Loaded with the record; nothing reads it. */
};

#pragma pack(pop)
typedef char xvt_size_mission_header[(sizeof(struct mission_header) == 162)
					     ? 1
					     : -1];

extern struct mission_header g_mission_header;
extern uint16_t g_asteroid_field_rand_seed;

struct mission_clock {
	uint8_t reserved[3]; /* Nothing uses it by name. */
	/* Hours; only the elapsed clock uses them, wrapping at 24. */
	uint8_t hours;
	uint8_t minutes; /* Minutes; the elapsed clock wraps at 60. */
	uint8_t seconds; /* Seconds, 0 to 59. */
	/* Ticks left in the elapsed clock's current second; the countdown's
	 * stays 0. */
	int16_t subsecond_ticks;
};

extern struct mission_clock g_mission_elapsed_clock;
extern struct mission_clock g_mission_countdown_clock;

#pragma pack(push, 1)

struct mission_message {
	char message
		[64]; ///< Message text passed directly to the in-flight message queue.
	uint8_t sent_to_team
		[10]; ///< Nonzero entry allows the corresponding player IFF/team to receive the message.
	struct mission_trigger_pair trigger_pairs
		[2]; ///< Two trigger pairs evaluated before the message becomes active.
	char voice
		[16]; ///< Voice resource name stored by the mission file format; not consumed by XVT's runtime
		      ///< message path.
	uint8_t delay5s; ///< Raw per-message delay copied to the runtime countdown.
	uint8_t trigger_pair1_or_trigger_pair2; ///< Value 1 combines the trigger-pair results with OR; other values
						///< use AND.
};

#pragma pack(pop)
typedef char xvt_size_mission_message[(sizeof(struct mission_message) == 114)
					      ? 1
					      : -1];

enum { MISSION_MESSAGE_COUNT = 64 };

extern struct mission_message g_mission_messages[MISSION_MESSAGE_COUNT];

enum {
	TEAM_SCORE_BONUS = 0,
	TEAM_SCORE_MISSION = 1,
};

typedef enum flight_group_outcome_index {
	FLIGHT_GROUP_OUTCOME_TOTAL = 0,
	FLIGHT_GROUP_OUTCOME_ARRIVED = 1,
	FLIGHT_GROUP_OUTCOME_DESTROYED = 2,
	FLIGHT_GROUP_OUTCOME_LOST_WITH_MOTHERSHIP = 3,
	FLIGHT_GROUP_OUTCOME_ATTACKED = 4,
	FLIGHT_GROUP_OUTCOME_NOT_ATTACKED = 5,
	FLIGHT_GROUP_OUTCOME_CAPTURED = 6,
	FLIGHT_GROUP_OUTCOME_NOT_CAPTURED = 7,
	FLIGHT_GROUP_OUTCOME_INSPECTED = 8,
	FLIGHT_GROUP_OUTCOME_NOT_INSPECTED = 9,
	FLIGHT_GROUP_OUTCOME_BOARDED = 10,
	FLIGHT_GROUP_OUTCOME_NOT_BOARDED = 11,
	FLIGHT_GROUP_OUTCOME_DOCKED = 12,
	FLIGHT_GROUP_OUTCOME_NOT_DOCKED = 13,
	FLIGHT_GROUP_OUTCOME_DISABLED = 14,
	FLIGHT_GROUP_OUTCOME_NOT_DISABLED = 15,
	FLIGHT_GROUP_OUTCOME_DEPARTED = 16,
	FLIGHT_GROUP_OUTCOME_LEFT_REGION = 17,
	FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT = 18,
	FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT = 19,
	FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT = 20,
	FLIGHT_GROUP_OUTCOME_ABORTED = 21,
	FLIGHT_GROUP_OUTCOME_NOT_DEPARTED = 22,
	FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION = 23,
	FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION = 24,
	FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION = 25,
	FLIGHT_GROUP_OUTCOME_FAILED_MISSION = 26,
	FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE = 27,
	FLIGHT_GROUP_OUTCOME_COUNT = 28,
} flight_group_outcome_index;

struct mission_flight_runtime_state {
	/* Per team, its bonus score (TEAM_SCORE_BONUS) and mission score
	 * (TEAM_SCORE_MISSION). */
	int team_scores[2][10];
	/* Per team: full, shared and assist kills (rows 0 to 2) and craft
	 * lost (row 3). */
	uint16_t team_kill_stats[4][10];
	/* Per team and flight group: craft it inspected (row 0, by
	 * collide_collisions) and captured (row 1, by
	 * paiman_transfer_object_to_ai_team). Nothing reads it by name. */
	uint16_t team_fg_inspected_captured_counts[2][10][48];
	/* Per team and flight group, the role code 1 to 12 from craft_role, 0
	 * for none. */
	uint8_t team_fg_designation_code[10][48];
	/* 1 once any team's primary goal is complete, else 0. */
	uint8_t global_primary_goal_status;
	/* Set to 0 by mission_init_flight_runtime_state; no game code reads it,
	 * only the modern build's snapshot copies it. */
	uint16_t global_goal_status_unused;
	/* 1 once any team's bonus goal is complete, else 0. */
	uint8_t global_bonus_goal_status;
	/* Per team, the last state of global goals 0 to 2: 1 met, 2 failed,
	 * 4 undecided, 0 none. */
	uint8_t team_global_goal_state[10][3];
	/* Per team, its primary, prevent and bonus status: 0 open, 1
	 * complete, 2 failed. */
	uint8_t team_goal_status[10][3];
	/* Per team, global goal and trigger: craft that met it (row 0) and
	 * the total (row 1), for the goals page. */
	uint16_t global_goal_trigger_counts[2][10][3][4];
	/* Per team, elapsed seconds when its primary goal completed. */
	unsigned int team_mission_completion_time_seconds[10];
	/* Per team, 1 when it has a flight group, not a backdrop, that has
	 * craft and may arrive. */
	uint8_t team_has_countable_craft[10];
	/* Per team, 1 once it called reinforcements. */
	uint8_t team_reinforcements_called[10];
};

struct mission_fg_runtime_stats {
	/* 1 once the group's arrival started or was closed. */
	uint8_t has_arrived;
	uint8_t waves_remaining; /* Rounds still to come. */
	/* 1 while the arrival delay counts down. */
	uint8_t arrival_delay_pending;
	/* Nonzero when the group may arrive at this difficulty and its
	 * player-connection arrival triggers have not failed. */
	uint8_t arrival_enabled;
	int16_t arrival_delay_timer; /* Simulated seconds left before arrival. */
	/* Start point, 0x8000 plus a mission point index. */
	uint16_t current_mission_point_ref;
	/* Craft created so far; numbers craft in the group. */
	uint16_t spawned_craft_count;
	/* Craft per FLIGHT_GROUP_OUTCOME_ index; FLIGHT_GROUP_OUTCOME_TOTAL
	 * counts every craft of every round. */
	uint16_t outcome_count[FLIGHT_GROUP_OUTCOME_COUNT];
	/* The same outcomes for the special cargo craft, mostly 0 or 1. */
	uint8_t special_cargo_outcome[FLIGHT_GROUP_OUTCOME_COUNT];
	/* Per team, craft of the group it inspected. */
	uint8_t team_inspected[10];
	/* Per team, the special cargo craft inspected. */
	uint8_t team_special_cargo_inspected[10];
	/* Per team, craft that ended before that team identified them. */
	uint8_t team_uninspected_lost[10];
	/* Per team, the special cargo craft ended uninspected. */
	uint8_t team_special_cargo_uninspected_lost[10];
	/* Per team, craft it captured that departed. */
	uint8_t team_captured_departed_count[10];
	/* Per team, the special cargo craft captured and departed. */
	uint8_t team_special_cargo_captured_departed[10];
	/* Per team, craft that ended without that team capturing them. */
	uint8_t team_uncaptured_lost[10];
	/* Per team, the special cargo craft ended uncaptured. */
	uint8_t team_special_cargo_uncaptured_lost[10];
	/* mission_init_flight_runtime_state zeroes this with the counts above; nothing in this build reads it. */
	uint8_t team_event_extra[4][10];
	/* Per team, eight goal states (index 8 times team plus goal): 4
	 * pending, 1 met, 2 failed, 8 a met per-craft bonus goal, 0 none. */
	uint8_t goal_state[80];
};

extern struct mission_fg_runtime_stats g_mission_fg_stats[48];
extern const uint8_t g_mission_condition_uses_count_by_condition[48];
extern uint16_t g_mission_condition_total_count;
extern uint16_t g_mission_condition_current_count;
extern int g_prepared_spawn_mission_x;
extern int g_prepared_spawn_mission_y;
extern int g_prepared_spawn_mission_z;
extern uint16_t g_prepared_spawn_yaw_byte;
extern uint16_t g_prepared_spawn_pitch_byte;
extern uint16_t g_prepared_spawn_roll_byte;
extern uint16_t g_next_object_signature;
extern const uint8_t g_genus_convert[12];
extern const uint8_t g_family_convert[4];

#pragma pack(push, 1)

struct e_mission_struct {
	uint8_t time_min; /* Not read. */
	uint8_t time_sec; /* Not read. */
	uint8_t win_type; /* Not read. */
	uint8_t backdrop; /* Copied to g_mission_header.time_limit_min. */
	uint8_t rescue;	  /* Copied to g_mission_header.rescue. */
	/* Copied to g_mission_header.all_waypoints_shown. */
	uint8_t all_way_shown;
	uint8_t mis_var[8];  /* Copied to g_mission_header.variables. */
	int8_t win_bonus[2]; /* Not read. */
	/* Copied to team 0's end-of-mission texts 0 and 1. */
	uint8_t win_msg1[2][64];
	/* Copied to team 0's end-of-mission texts 4 and 5. */
	uint8_t win_msg2[2][64];
	/* Copied to team 0's end-of-mission texts 2 and 3. */
	uint8_t loss_msg[2][64];
	uint8_t loss_msg_delay; /* Copied to team 0's eom_raw_delay[1]. */
	uint8_t loss_unused;	/* Not read. */
	/* Entry 0 is copied to all four IFF names; a '1' at the start in entry
	 * iff - 2 puts IFF 2, 3 and 5 on team 1. */
	char neutral_name[4][12];
};

#pragma pack(pop)
typedef char xvt_size_e_mission_struct[(sizeof(struct e_mission_struct) == 450)
					       ? 1
					       : -1];

#pragma pack(push, 1)

struct e_cond_struct {
	uint8_t cond; /* Copied to mission_trigger.condition. */
	uint8_t type; /* Copied to mission_trigger.variable_type. */
	uint8_t id;   /* Copied to mission_trigger.variable. */
	uint8_t pct;  /* Copied to mission_trigger.amount. */
};

#pragma pack(pop)
typedef char
	xvt_size_e_cond_struct[(sizeof(struct e_cond_struct) == 4) ? 1 : -1];

#pragma pack(push, 1)

struct eai_struct {
	uint8_t order; /* Copied to mission_order.order. */
	uint8_t speed; /* Copied by position to mission_order.throttle. */
	/* Copied by position to mission_order.variable1 to variable4. */
	uint8_t var[4];
	/* Copied by position to mission_order.secondary_target_types. */
	uint8_t target_type[2];
	/* Copied by position to mission_order.secondary_targets. */
	uint8_t target_id[2];
	/* Copied by position to mission_order.target3_or_target4. */
	uint8_t target_op;
	uint8_t target_unused; /* Copied by position to mission_order.unused0. */
	uint8_t pri_type;      /* Copied to mission_order.target1_type. */
	uint8_t pri_id;	       /* Copied to mission_order.target1. */
	uint8_t secondary_type; /* Copied to mission_order.target2_type. */
	uint8_t secondary_id;	/* Copied to mission_order.target2. */
	/* Copied by position to mission_order.target1_or_target2. */
	uint8_t pri_secondary_op;
	/* Copied by position to mission_order.unused1. */
	uint8_t pri_secondary_unused;
};

#pragma pack(pop)
typedef char xvt_size_eai_struct[(sizeof(struct eai_struct) == 18) ? 1 : -1];

#pragma pack(push, 1)

/* One TIE-format flight group record; mission_load_file copies its fields
 * into the xvt_flight_group fields named below. */
struct efg_struct {
	char name[12];	       /* Copied to name. */
	char cmdr[12];	       /* Copied to craft_role. */
	char contents[2][12];  /* Copied to cargo and special_cargo. */
	uint8_t special_craft; /* Copied to special_cargo_craft. */
	uint8_t special_flag;  /* Copied to random_special_cargo_craft. */
	craft_species
		species; ///< craft_species stored in the legacy EFG mission record.
	uint8_t count;	      /* Copied to number_of_craft. */
	uint8_t status;	      /* Copied to status1. */
	uint8_t warhead;      /* Copied to warhead. */
	uint8_t beam;	      /* Copied to beam. */
	uint8_t side;	      /* Copied to iff; also decides the team. */
	uint8_t skill;	      /* Copied to group_ai. */
	uint8_t camoflage;    /* Copied to markings. */
	uint8_t camo_flag;    /* Copied to radio. */
	uint8_t camo_unused;  /* Copied to unused5c. */
	uint8_t formation;    /* Copied to formation. */
	uint8_t form_spacing; /* Copied to formation_spacing. */
	uint8_t set;	      /* Copied to global_group. */
	uint8_t set_unused;   /* Copied to unused60. */
	uint8_t waves;	      /* Copied to number_of_waves. */
	uint8_t wave_delay;   /* Copied to waves_delay. */
	/* Nonzero: player number 1, player_craft player_flag - 1. */
	uint8_t player_flag;
	uint8_t heading;     /* Copied to yaw. */
	uint8_t pitch;	     /* Copied to pitch. */
	uint8_t rotation;    /* Copied to roll. */
	uint8_t link_flag;   /* Copied to legacy_perma_death_enabled. */
	uint8_t link_code;   /* Copied to legacy_perma_death_id. */
	uint8_t link_unused; /* Copied to reserved_perma_death. */
	uint8_t difficulty;  /* Copied to arrival_difficulty. */
	struct e_cond_struct start_cond[2]; /* Copied to arrival_triggers[0]. */
	uint8_t start_op; /* Copied to arrival_triggers[0].trigger1_or_trigger2. */
	uint8_t start_unused;	 /* Not read. */
	uint8_t start_delay_min; /* Copied to arrival_delay_minutes. */
	uint8_t start_delay_sec; /* Copied to arrival_delay_seconds. */
	struct e_cond_struct
		stop_cond;   /* Copied to departure_trigger.triggers[0]. */
	uint8_t stop_min;    /* Copied to departure_delay_minutes. */
	uint8_t stop_sec;    /* Copied to departure_delay_seconds. */
	uint8_t stop_abort;  /* Copied to abort_trigger. */
	uint8_t stop_unused; /* Not read. */
	/* Copied over editor_mothership's second byte and reserved_mothership. */
	int16_t cur_start_fg;
	uint8_t start_fg;		/* Copied to arrival_mothership. */
	uint8_t start_fg_used;		/* Copied to arrival_method. */
	uint8_t pri_stop_fg;		/* Copied to departure_mothership. */
	uint8_t pri_stop_fg_used;	/* Copied to departure_method. */
	uint8_t secondary_stop_fg;	/* Copied to alternate_mothership. */
	uint8_t secondary_stop_fg_used; /* Copied to alternate_mothership_used. */
	uint8_t capture_fg;	 /* Copied to captured_departure_mothership. */
	uint8_t capture_fg_used; /* Copied to captured_depart_via_mothership. */
	/* Copied to orders 0 to 2; order 2's code comes from ai[1]. */
	struct eai_struct ai[3];
	uint8_t pri_win_cond; /* Goal 0, a primary goal: its event_condition. */
	uint8_t pri_win_pct;  /* Goal 0's amount. */
	/* Goal 1, a bonus goal: its event_condition. */
	uint8_t secondary_win_cond;
	uint8_t secondary_win_pct; /* Goal 1's amount. */
	uint8_t loss_cond;    /* Goal 2, a prevent goal: its event_condition. */
	uint8_t loss_pct;     /* Goal 2's amount. */
	uint8_t bonus_cond;   /* Goal 3, a bonus goal: its event_condition. */
	uint8_t bonus_pct;    /* Goal 3's amount. */
	int8_t bonus_points;  /* Goal 3's points. */
	uint8_t bonus_unused; /* Not read. */
	int16_t way_x[15];    /* Copied to mission_point_x 0 to 14. */
	int16_t way_y[15];    /* Copied to mission_point_y 0 to 14. */
	int16_t way_z[15];    /* Copied to mission_point_z 0 to 14. */
	int16_t way_used[15]; /* Copied to mission_point_enabled 0 to 14. */
	uint8_t way_shown;    /* Copied to reserved_options_prefix[0]. */
	uint8_t way_unused;   /* Copied to reserved_options_prefix[1]. */
	uint8_t way_brief_link;	 /* Copied to reserved_options_prefix[2]. */
	uint8_t way_brief_shown; /* Copied to reserved_options[0]. */
};

#pragma pack(pop)
typedef char xvt_size_efg_struct[(sizeof(struct efg_struct) == 292) ? 1 : -1];

#pragma pack(push, 1)

struct e_mission_goal {
	/* Copied to trigger_pairs[0] of team 0's global goal. */
	struct e_cond_struct subcond[2];
	/* First 16 bytes copied to name, the 17th to version. */
	uint8_t editor_name[17];
	uint8_t or_joined; /* Copied to trigger_pairs[0].trigger1_or_trigger2. */
	/* The TIE-format loader copies _pad[0] into global_goal.raw_delay; nothing reads _pad[1]. */
	uint8_t pad[2];
};

#pragma pack(pop)
typedef char
	xvt_size_e_mission_goal[(sizeof(struct e_mission_goal) == 28) ? 1 : -1];

#pragma pack(push, 1)

struct xvt_v10_flight_group_text {
	char name[12]; /* Copied to name in a branch that never runs. */
	/* Copied to craft_role in a branch that never runs. */
	char craft_role[12];
	char cargo[12]; /* Copied to cargo in a branch that never runs. */
	/* Copied to special_cargo in a branch that never runs. */
	char special_cargo[12];
};

#pragma pack(pop)
typedef char xvt_size_xvt_v10_flight_group_text
	[(sizeof(struct xvt_v10_flight_group_text) == 48) ? 1 : -1];

#pragma pack(push, 1)

struct tie_radio_message {
	char message[64]; /* Copied to mission_message.message. */
	struct e_cond_struct conditions[2]; /* Copied to trigger_pairs[0]. */
	char voice[16];	 /* Copied to mission_message.voice. */
	uint8_t delay5s; /* Copied to mission_message.delay5s. */
	/* Copied to trigger_pairs[0].trigger1_or_trigger2. */
	uint8_t condition1_or_condition2;
};

#pragma pack(pop)
typedef char xvt_size_tie_radio_message[(sizeof(struct tie_radio_message) == 90)
						? 1
						: -1];

#pragma pack(push, 1)

struct xvt_v10_mission_header {
	uint16_t num_flight_groups;  /* Copied in a branch that never runs. */
	uint16_t num_messages;	     /* Copied in a branch that never runs. */
	uint8_t time_limit_min;	     /* Copied in a branch that never runs. */
	uint8_t time_limit_sec;	     /* Copied in a branch that never runs. */
	uint8_t win_type;	     /* Copied in a branch that never runs. */
	uint8_t backdrop;	     /* Copied in a branch that never runs. */
	uint8_t rescue;		     /* Copied in a branch that never runs. */
	uint8_t all_waypoints_shown; /* Copied in a branch that never runs. */
	uint8_t variables[8];	     /* Copied in a branch that never runs. */
	char iff_names[4][12];	     /* Copied in a branch that never runs. */
	mission_type mission_type;   /* Copied in a branch that never runs. */
	uint8_t goals_unimportant;   /* Not read. */
	uint8_t mission_time_limit;  /* Not read. */
	uint8_t reserved[61];	     /* Not read. */
};

#pragma pack(pop)
typedef char xvt_size_xvt_v10_mission_header
	[(sizeof(struct xvt_v10_mission_header) == 130) ? 1 : -1];

uint16_t mission_is_special_cargo_inspected(unsigned int flight_group_idx,
					    uint16_t special_cargo_craft);
void mission_update_logic(void);
int mission_evaluate_trigger_pair(
	const struct mission_trigger_pair *trigger_pair,
	int16_t include_departed_as_destroyed);
int16_t mission_evaluate_condition(uint16_t condition_type,
				   int16_t variable_type, uint16_t variable,
				   int16_t amount_type,
				   int16_t include_departed_as_destroyed,
				   uint16_t team_filter);
int16_t mission_flight_group_matches_trigger_variable(uint16_t flight_group_idx,
						      int16_t variable_type,
						      uint16_t variable);
int16_t mission_object_matches_trigger_variable(uint16_t object_idx,
						uint16_t variable_type,
						uint16_t variable);
void mission_record_craft_outcome(uint16_t obj_idx, uint16_t flight_group_idx,
				  uint16_t outcome_id);
int16_t mission_close_unavailable_flight_group_accounting(int flight_group_idx);
void mission_credit_destruction_damage_contributors(uint16_t source_obj_idx,
						    uint16_t victim_obj_idx);
void mission_credit_player_kill_contribution(
	uint16_t victim_obj_idx, int special_cargo_flag, int contribution_tier,
	int player_idx, int victim_owner_idx, int victim_rating);
void mission_credit_team_kill_contribution(uint16_t victim_obj_idx,
					   int special_cargo_flag,
					   int contribution_tier, int team_idx);
void mission_record_projectile_hit_stats(uint16_t projectile_obj_idx);
int mission_record_player_craft_loss(unsigned int obj_idx,
				     int allow_pending_damage_credit);
void mission_record_player_craft_loss_attribution(int attacker_flight_group_idx,
						  int victim_obj_idx,
						  int contribution_tier);
int mission_apply_flight_group_goal_score(int16_t event_condition,
					  uint16_t flight_group_idx,
					  int player_idx,
					  uint16_t goal_score_reduction_level,
					  int special_cargo_flag, int team_idx);
void mission_apply_team_goal_score_all_enabled_teams(int16_t event_condition,
						     uint16_t flight_group_idx,
						     int special_cargo_flag);
void mission_apply_team_goal_score_for_team(int16_t event_condition,
					    uint16_t flight_group_idx,
					    int special_cargo_flag,
					    uint8_t team_idx);
int mission_clock_to_seconds(uint8_t hours, uint8_t minutes, uint8_t seconds);
int mission_compute_kill_score_for_object(int victim_obj_idx);
int mission_compute_craft_point_value(int obj_idx);
int mission_get_elapsed_clock_seconds(void);
uint16_t mission_init(char *file_name);
void mission_init_flight_runtime_state(void);
int16_t mission_start_flight_group_arrival(uint16_t craft_ordinal);
void mission_update_flight_group_arrivals(void);
void mission_process_flight_group_wave_completion(uint16_t flight_group_idx);
void mission_spawn_current_flight_group_wave(void);
int mission_has_capacity_for_current_flight_group_wave(void);
int16_t mission_spawn_flight_group_wave_craft(uint16_t craft_ordinal);
uint16_t mission_init_flight_group_object_slot(void);
void mission_spawn_flight_group_static_objects(uint16_t craft_ordinal);
uint16_t mission_spawn_prepared_object(uint16_t flight_group_idx,
				       int16_t genus_id, uint8_t object_type);
void mission_resolve_object_or_mission_point_world_loc(
	unsigned int obj_or_mission_point_ref, int flight_group_idx);
void mission_resolve_formation_slot_world_loc(uint16_t flight_group_idx,
					      uint16_t formation_slot_idx,
					      uint16_t basis_obj_idx);
int mission_load_file(char *file_name);
int mission_sync_pilot_network_players_to_session_slots(void);
void mission_free_override_string_handles(void);

#ifdef __cplusplus
}
#endif

#endif
