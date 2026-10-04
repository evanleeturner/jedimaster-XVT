#ifndef XVT_FLIGHT_MISSION_GOALS_H
#define XVT_FLIGHT_MISSION_GOALS_H

#include <stdint.h>

#include "xvt/assets/object_type.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stored as int8_t in the binary (IDB enum mission_goal_amount). */
typedef int8_t mission_goal_amount;

enum {
	GOAL_AMT_100 = 0x0,
	GOAL_AMT_75 = 0x1,
	GOAL_AMT_50 = 0x2,
	GOAL_AMT_25 = 0x3,
	GOAL_AMT_AT_LEAST_1 = 0x4,
	GOAL_AMT_ALL_BUT_1 = 0x5,
	GOAL_AMT_ALL_SPECIAL_CARGO = 0x6,
	GOAL_AMT_ALL_NON_SPECIAL = 0x7,
	GOAL_AMT_ALL_EXCEPT_PLAYER = 0x8,
	GOAL_AMT_PLAYER_FG = 0x9,
	/* The _OF_SUBSET amounts measure against the craft counted as arrived so far instead of the flight
	 * groups' totals, and they never report failure. */
	GOAL_AMT_100_OF_SUBSET = 0xA,
	GOAL_AMT_75_OF_SUBSET = 0xB,
	GOAL_AMT_50_OF_SUBSET = 0xC,
	GOAL_AMT_25_OF_SUBSET = 0xD,
	GOAL_AMT_AT_LEAST_1_ALT = 0xE,
	GOAL_AMT_ALL_BUT_1_OF_SUBSET = 0xF,
	GOAL_AMT_66 = 0x10,
	GOAL_AMT_33 = 0x11,
};

typedef enum mission_condition_type {
	MISSION_COND_ALWAYS_TRUE = 0,
	MISSION_COND_ARRIVED = 1,
	MISSION_COND_DESTROYED = 2,
	MISSION_COND_ATTACKED = 3,
	MISSION_COND_CAPTURED = 4,
	MISSION_COND_INSPECTED = 5,
	MISSION_COND_BOARDED = 6,
	MISSION_COND_DOCKED = 7,
	MISSION_COND_DISABLED = 8,
	MISSION_COND_SURVIVED = 9,
	MISSION_COND_NEVER = 10,
	MISSION_COND_DEPARTED = 12,
	MISSION_COND_PRIMARY_GOAL_COMPLETE = 13,
	MISSION_COND_PRIMARY_GOAL_FAILED = 14,
	MISSION_COND_BONUS_GOAL_COMPLETE = 17,
	MISSION_COND_BONUS_GOAL_FAILED = 18,
	MISSION_COND_REINFORCEMENTS_CALLED = 20,
	MISSION_COND_SHIELDS_DEPLETED = 21,
	MISSION_COND_HULL_DAMAGE_ABOVE_50 = 22,
	MISSION_COND_NO_WARHEADS = 23,
	MISSION_COND_CANNONS_DISABLED = 24,
	MISSION_COND_NOT_ARRIVED = 25,
	MISSION_COND_NOT_ATTACKED = 26,
	MISSION_COND_NOT_DISABLED = 27,
	MISSION_COND_NOT_CAPTURED = 28,
	MISSION_COND_NOT_INSPECTED = 29,
	MISSION_COND_COMPLETED_MISSION = 30,
	MISSION_COND_NOT_BOARDED = 31,
	MISSION_COND_FAILED_MISSION = 32,
	MISSION_COND_NOT_DOCKED = 33,
	MISSION_COND_SHIELDS_BELOW_50 = 34,
	MISSION_COND_SHIELDS_BELOW_25 = 35,
	MISSION_COND_HULL_DAMAGE_ABOVE_25 = 36,
	MISSION_COND_HULL_DAMAGE_ABOVE_75 = 37,
	MISSION_COND_ALWAYS_FAILED = 38,
	MISSION_COND_NO_CONDITION = 39,
	MISSION_COND_PLAYER_CONNECTED = 41,
	MISSION_COND_PLAYER_DISCONNECTED = 42,
	MISSION_COND_DESTROYED_OR_DEPARTED = 43,
	MISSION_COND_CAPTURED_AND_DEPARTED = 44,
	MISSION_COND_NOT_DEPARTED = 45,
	MISSION_COND_CAPTURED_BY_DESTINATION = 46,
} mission_condition_type;

#pragma pack(push, 1)

struct mission_trigger {
	uint8_t condition; /* A MISSION_COND_ value. */
	/* What variable selects: a GOAL_TARGET_ trigger variable type. */
	uint8_t variable_type;
	/* The flight group, species, team or other value variable_type
	 * selects. */
	uint8_t variable;
	mission_goal_amount
		amount; ///< Encoded goal amount threshold (100%, 75%, 50%, 25%, subset and special-cargo variants).
};

#pragma pack(pop)
typedef char xvt_size_mission_trigger[(sizeof(struct mission_trigger) == 4)
					      ? 1
					      : -1];

#pragma pack(push, 1)

struct mission_trigger_pair {
	struct mission_trigger triggers[2]; /* The two conditions. */
	uint8_t reserved[2]; /* Loaded with the record; nothing reads it. */
	/* 1 joins the two conditions with OR, else AND. */
	uint8_t trigger1_or_trigger2;
};

#pragma pack(pop)
typedef char xvt_size_mission_trigger_pair
	[(sizeof(struct mission_trigger_pair) == 11) ? 1 : -1];

#pragma pack(push, 1)

struct flight_group_goal {
	uint8_t goal_kind; /* 0 primary, 1 prevent, 2 bonus. */
	/* The MISSION_COND_ value the goal tests on the group. */
	uint8_t event_condition;
	mission_goal_amount
		amount; ///< Encoded goal amount threshold used by mission_evaluate_condition.
	int8_t points; /* Score unit; 250 times it is awarded. */
	/* Per team, nonzero when the goal applies to it. */
	uint8_t enabled_teams[10];
	uint8_t time_limit5s; /* Time limit in 5-second units; 0 for none. */
	uint8_t active_sequence; ///< Sequential-goal selector in the mission format; XVT loads it but has no
	///< direct runtime read.
	uint8_t reserved[62]; ///< Reserved mission-file storage.
};

#pragma pack(pop)
typedef char xvt_size_flight_group_goal[(sizeof(struct flight_group_goal) == 78)
						? 1
						: -1];

#pragma pack(push, 1)

struct global_goal {
	struct mission_trigger_pair trigger_pairs
		[2]; ///< Two trigger pairs evaluated to determine the goal state.
	char name[16]; ///< Mission-file goal name.
	uint8_t version; ///< Mission-file goal format version; not consumed by XVT runtime logic.
	uint8_t trigger_pair1_or_trigger_pair2; ///< Value 1 combines the trigger-pair results with OR; other values
	///< use AND.
	uint8_t raw_delay; ///< Mission-file goal delay metadata; loaded but not consumed by XVT runtime logic.
	int8_t raw_points; ///< Signed score unit; XVT awards 250 times this value.
};

#pragma pack(pop)
typedef char xvt_size_global_goal[(sizeof(struct global_goal) == 42) ? 1 : -1];

typedef enum goal_target_type {
	GOAL_TARGET_NONE = 0x0,
	GOAL_TARGET_FLIGHT_GROUP = 0x1,
	GOAL_TARGET_SPECIES = 0x2,
	GOAL_TARGET_GENUS = 0x3,
	GOAL_TARGET_FAMILY = 0x4,
	GOAL_TARGET_IFF = 0x5,
	GOAL_TARGET_SHIP_ORDER = 0x6,
	GOAL_TARGET_CRAFT_WHEN = 0x7,
	GOAL_TARGET_GLOBAL_GROUP = 0x8,
	GOAL_TARGET_AI_LEVEL = 0x9,
	GOAL_TARGET_STATUS = 0xA,
	GOAL_TARGET_TEAM = 0xC,
	GOAL_TARGET_PLAYER_NUMBER = 0xD,
} goal_target_type;

typedef enum goal_operator_string_id {
	GOAL_OPERATOR_STR_AND = 0x0,
	GOAL_OPERATOR_STR_OR = 0x1,
} goal_operator_string_id;

/* Stored as int16_t in the binary (IDB enum goal_title_string_id). */
typedef int16_t goal_title_string_id;

enum {
	GOAL_TITLE_STR_FAILED_OBJECTIVES = 0x0,
	GOAL_TITLE_STR_OBJECTIVES_TO_ACCOMPLISH = 0x1,
	GOAL_TITLE_STR_CONDITIONS_TO_PREVENT = 0x2,
	GOAL_TITLE_STR_COMPLETED_OBJECTIVES = 0x3,
	GOAL_TITLE_STR_MISSION_OUTCOME = 0x4,
	GOAL_TITLE_STR_VICTORY = 0x5,
	GOAL_TITLE_STR_LOSS = 0x6,
	GOAL_TITLE_STR_UNRESOLVED = 0x7,
	GOAL_TITLE_STR_DRAW = 0x8,
};

typedef enum goal_percentage_string_id {
	GOAL_PERCENT_STR_100 = 0x0,
	GOAL_PERCENT_STR_75 = 0x1,
	GOAL_PERCENT_STR_50 = 0x2,
	GOAL_PERCENT_STR_25 = 0x3,
	GOAL_PERCENT_STR_AT_LEAST_ONE = 0x4,
	GOAL_PERCENT_STR_ALL_BUT_ONE = 0x5,
	GOAL_PERCENT_STR_PLACEHOLDER_6 = 0x6,
	GOAL_PERCENT_STR_PLACEHOLDER_7 = 0x7,
	GOAL_PERCENT_STR_ALL_BUT_YOU = 0x8,
	GOAL_PERCENT_STR_YOU = 0x9,
	GOAL_PERCENT_STR_66 = 0xA,
	GOAL_PERCENT_STR_33 = 0xB,
	GOAL_PERCENT_STR_PLACEHOLDER_12 = 0xC,
	GOAL_PERCENT_STR_PLACEHOLDER_13 = 0xD,
} goal_percentage_string_id;

typedef enum goal_family_string_id {
	GOAL_FAMILY_STR_SPACE_CRAFT = 0x0,
	GOAL_FAMILY_STR_WEAPONS = 0x1,
	GOAL_FAMILY_STR_SATELLITES = 0x2,
	GOAL_FAMILY_STR_PLACEHOLDER_3 = 0x3,
	GOAL_FAMILY_STR_PLACEHOLDER_4 = 0x4,
	GOAL_FAMILY_STR_PLACEHOLDER_5 = 0x5,
	GOAL_FAMILY_STR_PLACEHOLDER_6 = 0x6,
} goal_family_string_id;

typedef enum goal_genus_string_id {
	GOAL_GENUS_STR_STARFIGHTERS = 0x0,
	GOAL_GENUS_STR_TRANSPORT_CRAFT = 0x1,
	GOAL_GENUS_STR_UTILITY_CRAFT = 0x2,
	GOAL_GENUS_STR_FREIGHTER_CRAFT = 0x3,
	GOAL_GENUS_STR_STARSHIPS = 0x4,
	GOAL_GENUS_STR_PLATFORMS = 0x5,
	GOAL_GENUS_STR_PLACEHOLDER_6 = 0x6,
	GOAL_GENUS_STR_PLACEHOLDER_7 = 0x7,
	GOAL_GENUS_STR_MINES = 0x8,
	GOAL_GENUS_STR_PLACEHOLDER_9 = 0x9,
	GOAL_GENUS_STR_PLACEHOLDER_10 = 0xA,
	GOAL_GENUS_STR_PLACEHOLDER_11 = 0xB,
	GOAL_GENUS_STR_PLACEHOLDER_12 = 0xC,
	GOAL_GENUS_STR_PLACEHOLDER_13 = 0xD,
	GOAL_GENUS_STR_PLACEHOLDER_14 = 0xE,
	GOAL_GENUS_STR_PLACEHOLDER_15 = 0xF,
} goal_genus_string_id;

typedef enum goal_conjunction_string_id {
	GOAL_CONJ_STR_OF = 0x0,
	GOAL_CONJ_STR_OF_ALL = 0x1,
	GOAL_CONJ_STR_GROUP = 0x2,
	GOAL_CONJ_STR_ALL_BUT = 0x3,
	GOAL_CONJ_STR_AND = 0x4,
	GOAL_CONJ_STR_COMMA = 0x5,
	GOAL_CONJ_STR_FLIGHT_GROUPS = 0x6,
	GOAL_CONJ_STR_LESS_THAN = 0x7,
} goal_conjunction_string_id;

typedef enum goal_side_string_id {
	GOAL_SIDE_STR_REBEL_CRAFT = 0x0,
	GOAL_SIDE_STR_IMPERIAL_CRAFT = 0x1,
	GOAL_SIDE_STR_CRAFT = 0x2,
} goal_side_string_id;

typedef enum flight_group_goal_status_string_id {
	FG_GOAL_STATUS_STR_NONE = 0x0,
	FG_GOAL_STATUS_STR_INSPECT = 0xF,
	FG_GOAL_STATUS_STR_DESTROY = 0x10,
	FG_GOAL_STATUS_STR_DISABLE = 0x11,
	FG_GOAL_STATUS_STR_ATTACK = 0x12,
	FG_GOAL_STATUS_STR_CAPTURE = 0x13,
	FG_GOAL_STATUS_STR_BOARD = 0x14,
} flight_group_goal_status_string_id;

extern uint8_t g_goal_condition_text_variant_count[48];
extern uint8_t g_goal_title_color_by_index[8];
extern const char *g_str_goal_cond_feminine[188][14];
extern const char *g_str_goal_cond_neutered[188][14];
extern const char *g_str_goal_cond_masculine[188][14];
extern uint8_t g_craft_gender[80];
extern const char *g_str_goal_operators[2];
extern const char *g_str_goal_titles[9];
extern const char *g_str_goal_percentages[14];
extern const char *g_str_goal_family_names[7];
extern const char *g_str_goal_genus_names[16];
extern const char *g_str_goal_conjunctions[8];
extern const char *g_str_goal_escape[3];
extern const char *g_str_goal_sides[3];
extern const char *g_str_unknown;
extern const char *g_str_sat_mine_probe_buoy_pilot_names[16];
extern const char *g_str_status_strings[9];
extern const char *g_str_warhead_names[13];
extern const char *g_str_species_names_plural[73];
extern const char *g_str_wingman_commands[10];

int16_t goals_outputgoal(uint16_t target_id, uint16_t condition,
			 uint16_t target_type, uint16_t goal_status,
			 uint16_t amount_op, uint16_t time_limit5_sec_units,
			 const char *condition_text_override,
			 int percent_complete, int goal_title_index);
int16_t goals_draw_condition_text(unsigned int craft_species,
				  uint16_t condition,
				  uint16_t amount_text_variant,
				  int16_t condition_row_base);
int16_t goals_draw_object_type_name(uint16_t craft_species,
				    int16_t use_plural_name,
				    int16_t use_short_name);

#ifdef __cplusplus
}
#endif

#endif
