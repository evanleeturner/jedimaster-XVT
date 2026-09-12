#ifndef XVT_FLIGHT_MISSION_GOALS_H
#define XVT_FLIGHT_MISSION_GOALS_H

#include "xvt/assets/object_type.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stored as int8_t in the binary (IDB enum MissionGoalAmount). */
typedef int8_t MissionGoalAmount;

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
	GOAL_AMT_100_OF_SUBSET = 0xA,
	GOAL_AMT_75_OF_SUBSET = 0xB,
	GOAL_AMT_50_OF_SUBSET = 0xC,
	GOAL_AMT_25_OF_SUBSET = 0xD,
	GOAL_AMT_AT_LEAST_1_ALT = 0xE,
	GOAL_AMT_ALL_BUT_1_OF_SUBSET = 0xF,
	GOAL_AMT_66 = 0x10,
	GOAL_AMT_33 = 0x11,
};

typedef enum MissionConditionType {
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
	MISSION_COND_NEVER_FALSE = 10,
	MISSION_COND_DEPARTED = 12,
	MISSION_COND_PRIMARY_GOAL_COMPLETE = 13,
	MISSION_COND_PRIMARY_GOAL_FAILED = 14,
	MISSION_COND_BONUS_GOAL_COMPLETE = 17,
	MISSION_COND_BONUS_GOAL_FAILED = 18,
	MISSION_COND_REINFORCEMENT_NOT_CALLED = 20,
	MISSION_COND_SHIELDS_DEPLETED = 21,
	MISSION_COND_HULL_ABOVE_50 = 22,
	MISSION_COND_NO_WARHEADS = 23,
	MISSION_COND_SYSTEM_DAMAGED = 24,
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
	MISSION_COND_HULL_ABOVE_25 = 36,
	MISSION_COND_HULL_ABOVE_75 = 37,
	MISSION_COND_ALWAYS_PENDING = 38,
	MISSION_COND_NO_CONDITION = 39,
	MISSION_COND_PLAYER_CONNECTED = 41,
	MISSION_COND_PLAYER_DISCONNECTED = 42,
	MISSION_COND_DESTROYED_OR_DEPARTED = 43,
	MISSION_COND_ORDER_COMPLETED = 44,
	MISSION_COND_DESTROYED_OR_CAPTURED = 45,
	MISSION_COND_CAPTURED_BY_DESTINATION = 46,
} MissionConditionType;

#pragma pack(push, 1)

struct MissionTrigger {
	uint8_t condition;
	uint8_t variableType;
	uint8_t variable;
	MissionGoalAmount
		amount; ///< Encoded goal amount threshold (100%, 75%, 50%, 25%, subset and special-cargo variants).
};

#pragma pack(pop)
typedef char xvt_size_MissionTrigger[(sizeof(MissionTrigger) == 4) ? 1 : -1];

#pragma pack(push, 1)

struct MissionTriggerPair {
	MissionTrigger triggers[2];
	uint8_t reserved[2];
	uint8_t trigger1OrTrigger2;
};

#pragma pack(pop)
typedef char xvt_size_MissionTriggerPair[(sizeof(MissionTriggerPair) == 11) ? 1 : -1];

#pragma pack(push, 1)

struct FlightGroupGoal {
	uint8_t type;
	uint8_t eventCondition;
	MissionGoalAmount amount; ///< Encoded goal amount threshold used by Mission_EvaluateCondition.
	int8_t points;
	uint8_t enabledTeams[10];
	uint8_t timeLimit5s;
	uint8_t activeSequence; ///< Sequential-goal selector in the mission format; XVT loads it but has no
							///< direct runtime read.
	uint8_t reserved[62];   ///< Reserved mission-file storage.
};

#pragma pack(pop)
typedef char xvt_size_FlightGroupGoal[(sizeof(FlightGroupGoal) == 78) ? 1 : -1];

#pragma pack(push, 1)

struct GlobalGoal {
	MissionTriggerPair triggerPairs[2]; ///< Two trigger pairs evaluated to determine the goal state.
	char name[16];                      ///< Mission-file goal name.
	uint8_t version; ///< Mission-file goal format version; not consumed by XVT runtime logic.
	uint8_t triggerPair1OrTriggerPair2; ///< Value 1 combines the trigger-pair results with OR; other values
										///< use AND.
	uint8_t rawDelay; ///< Mission-file goal delay metadata; loaded but not consumed by XVT runtime logic.
	int8_t rawPoints; ///< Signed score unit; XVT awards 250 times this value.
};

#pragma pack(pop)
typedef char xvt_size_GlobalGoal[(sizeof(GlobalGoal) == 42) ? 1 : -1];

typedef enum GoalTargetType {
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
} GoalTargetType;

typedef enum GoalOperatorStringId {
	GOAL_OPERATOR_STR_AND = 0x0,
	GOAL_OPERATOR_STR_OR = 0x1,
} GoalOperatorStringId;

/* Stored as int16_t in the binary (IDB enum GoalTitleStringId). */
typedef int16_t GoalTitleStringId;

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

typedef enum GoalPercentageStringId {
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
} GoalPercentageStringId;

typedef enum GoalFamilyLowStringId {
	GOAL_FAMILY_LOW_STR_SPACE_CRAFT = 0x0,
	GOAL_FAMILY_LOW_STR_WEAPONS = 0x1,
	GOAL_FAMILY_LOW_STR_SATELLITES = 0x2,
	GOAL_FAMILY_LOW_STR_PLACEHOLDER_3 = 0x3,
	GOAL_FAMILY_LOW_STR_PLACEHOLDER_4 = 0x4,
	GOAL_FAMILY_LOW_STR_PLACEHOLDER_5 = 0x5,
	GOAL_FAMILY_LOW_STR_PLACEHOLDER_6 = 0x6,
} GoalFamilyLowStringId;

typedef enum GoalFamilyHighStringId {
	GOAL_FAMILY_HIGH_STR_STARFIGHTERS = 0x0,
	GOAL_FAMILY_HIGH_STR_TRANSPORT_CRAFT = 0x1,
	GOAL_FAMILY_HIGH_STR_UTILITY_CRAFT = 0x2,
	GOAL_FAMILY_HIGH_STR_FREIGHTER_CRAFT = 0x3,
	GOAL_FAMILY_HIGH_STR_STARSHIPS = 0x4,
	GOAL_FAMILY_HIGH_STR_PLATFORMS = 0x5,
	GOAL_FAMILY_HIGH_STR_PLACEHOLDER_6 = 0x6,
	GOAL_FAMILY_HIGH_STR_PLACEHOLDER_7 = 0x7,
	GOAL_FAMILY_HIGH_STR_MINES = 0x8,
	GOAL_FAMILY_HIGH_STR_PLACEHOLDER_9 = 0x9,
	GOAL_FAMILY_HIGH_STR_PLACEHOLDER_10 = 0xA,
	GOAL_FAMILY_HIGH_STR_PLACEHOLDER_11 = 0xB,
	GOAL_FAMILY_HIGH_STR_PLACEHOLDER_12 = 0xC,
	GOAL_FAMILY_HIGH_STR_PLACEHOLDER_13 = 0xD,
	GOAL_FAMILY_HIGH_STR_PLACEHOLDER_14 = 0xE,
	GOAL_FAMILY_HIGH_STR_PLACEHOLDER_15 = 0xF,
} GoalFamilyHighStringId;

typedef enum GoalConjunctionStringId {
	GOAL_CONJ_STR_OF = 0x0,
	GOAL_CONJ_STR_OF_ALL = 0x1,
	GOAL_CONJ_STR_GROUP = 0x2,
	GOAL_CONJ_STR_ALL_BUT = 0x3,
	GOAL_CONJ_STR_AND = 0x4,
	GOAL_CONJ_STR_COMMA = 0x5,
	GOAL_CONJ_STR_FLIGHT_GROUPS = 0x6,
	GOAL_CONJ_STR_LESS_THAN = 0x7,
} GoalConjunctionStringId;

typedef enum GoalSideStringId {
	GOAL_SIDE_STR_REBEL_CRAFT = 0x0,
	GOAL_SIDE_STR_IMPERIAL_CRAFT = 0x1,
	GOAL_SIDE_STR_CRAFT = 0x2,
} GoalSideStringId;

typedef enum FlightGroupGoalStatusStringId {
	FG_GOAL_STATUS_STR_NONE = 0x0,
	FG_GOAL_STATUS_STR_INSPECT = 0xF,
	FG_GOAL_STATUS_STR_DESTROY = 0x10,
	FG_GOAL_STATUS_STR_DISABLE = 0x11,
	FG_GOAL_STATUS_STR_ATTACK = 0x12,
	FG_GOAL_STATUS_STR_CAPTURE = 0x13,
	FG_GOAL_STATUS_STR_BOARD = 0x14,
} FlightGroupGoalStatusStringId;

extern uint8_t g_goalConditionTextVariantCount[48];
extern uint8_t g_goalTitleColorByIndex[8];
extern const char* g_strGoalCondFeminine[188][14];
extern const char* g_strGoalCondNeutered[188][14];
extern const char* g_strGoalCondMasculine[188][14];
extern uint8_t g_craftGender[80];
extern const char* g_strGoalOperators[2];
extern const char* g_strGoalTitles[9];
extern const char* g_strGoalPercentages[14];
extern const char* g_strGoalFamilyNames0To6[7];
extern const char* g_strGoalFamilyNames7To22[16];
extern const char* g_strGoalConjunctions[8];
extern const char* g_strGoalEscape[3];
extern const char* g_strGoalSides[3];
extern const char* g_strWarheadUnknown;
extern const char* g_strBuoyNames[16];
extern const char* g_strStatusStrings[9];
extern const char* g_strWarheadNames[13];
extern const char* g_strSpeciesNamesPlural[73];
extern const char* g_strWingmanCommands[10];

int16_t goals_outputgoal(uint16_t targetId, uint16_t condition, uint16_t targetType, uint16_t goalStatus,
						 uint16_t amountOp, uint16_t timeLimit5SecUnits, const char* conditionTextOverride,
						 int percentComplete, int goalTitleIndex);
int16_t goals_DrawConditionText(unsigned int craftSpecies, uint16_t condition, uint16_t amountTextVariant,
								int16_t conditionRowBase);
int16_t goals_DrawObjectTypeName(uint16_t craftSpecies, int16_t usePluralName, int16_t useShortName);

#ifdef __cplusplus
}
#endif

#endif
