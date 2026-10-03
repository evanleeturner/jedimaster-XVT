#ifndef XVT_FLIGHT_MISSION_MISSION_H
#define XVT_FLIGHT_MISSION_MISSION_H

#include "xvt/assets/object_type.h"
#include "xvt/flight/mission/goals.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

struct MissionOrder {
	/* Order code; picks the AI plans through
	 * g_orderLeaderBuiltinPlanNameIndex and
	 * g_orderFollowerBuiltinPlanNameIndex, 0 for none. */
	uint8_t order;
	uint8_t throttle;  /* Index into g_orderThrottleToCraftThrottleSpeed. */
	uint8_t variable1; /* Parameter whose meaning depends on the order. */
	/* Parameter whose meaning depends on the order; for a drop-off, the
	 * destination flight group plus 1. */
	uint8_t variable2;
	uint8_t variable3; /* Loaded with the record; nothing reads it. */
	uint8_t variable4; /* Loaded with the record; nothing reads it. */
	/* Trigger variable types of targets 3 and 4. */
	uint8_t secondaryTargetTypes[2];
	uint8_t secondaryTargets[2]; /* Trigger variables of targets 3 and 4. */
	/* 1 joins targets 3 and 4 with OR, else AND. */
	uint8_t target3OrTarget4;
	uint8_t unused0;     /* Loaded with the record; nothing reads it. */
	uint8_t target1Type; /* Trigger variable type of target 1. */
	uint8_t target1;     /* Trigger variable of target 1. */
	uint8_t target2Type; /* Trigger variable type of target 2. */
	uint8_t target2;     /* Trigger variable of target 2. */
	/* 1 joins targets 1 and 2 with OR, else AND. */
	uint8_t target1OrTarget2;
	uint8_t unused1; /* Loaded with the record; nothing reads it. */
	/* Speed for the order; paiman_initmaneuver sets commandedSpeed to 5
	 * times it. */
	uint8_t speed;
	/* Order text MissionSetup_DrawFlightAssignments shows for the first
	 * order; empty shows a default. */
	char designation[16];
	uint8_t reserved[47]; /* Loaded with the record; nothing reads it. */
};

#pragma pack(pop)
typedef char xvt_size_MissionOrder[(sizeof(MissionOrder) == 82) ? 1 : -1];

extern uint16_t g_targetProximityBlinkBit;
extern uint8_t g_flightRuntimeStateInitialized;

#pragma pack(push, 1)

struct XvtFlightGroup {
	/* Flight group name, shown on the goals page and in craft names. */
	char name[20];
	/* Up to four 4-character role entries: a team selector (1 to 9, A,
	 * O, F or H) and a code such as COM or MIS. Mission_LoadFile makes
	 * it upper case; Mission_InitFlightRuntimeState reads it into
	 * runtime.teamFgDesignationCode. */
	char craftRole[16];
	/* Loaded with the record; nothing reads it. */
	uint8_t reservedCraftRole[4];
	/* Cargo text of every craft but the special cargo craft. */
	char cargo[20];
	char specialCargo[20]; /* Cargo text of the special cargo craft. */
	/* Number in the group of the special cargo craft. */
	uint8_t specialCargoCraft;
	/* Nonzero makes Mission_Init pick specialCargoCraft at random. */
	uint8_t randomSpecialCargoCraft;
	CraftSpecies
		craftType; ///< Primary CraftSpecies for this mission flight group.
	/* Craft in each round; for mines, the side of the square grid. */
	uint8_t numberOfCraft;
	/* Status code: adjusts warheads, shields, hyperdrive and turrets at
	 * spawn and some combat and AI rules; for a backdrop it picks the
	 * image. */
	uint8_t status1;
	/* Warhead choice, an index into g_warheadTypeIds and
	 * g_warheadAmmoFractionQ16. */
	uint8_t warhead;
	/* Beam type of each craft; object types 1 to 5 get none. */
	uint8_t beam;
	uint8_t iff;  /* IFF of the group's craft. */
	uint8_t team; /* Team, 0 to 9. */
	/* AI level; Mission_Init raises or lowers it for difficulty and
	 * balance, at most 5 when it raises it. */
	uint8_t groupAI;
	/* Paint scheme, copied to each craft's nodeSwitchIndex. */
	uint8_t markings;
	/* Who may radio orders to the group besides its own player and team:
	 * team radio - 1, or the player whose group has player number radio
	 * - 8; 0 none (Player_CanRadioCommandCraft). */
	uint8_t radio;
	uint8_t unused5C; /* Loaded with the record; nothing reads it. */
	/* Formation of each round, a row of g_formPosX, g_formPosY and
	 * g_formPosZ. */
	uint8_t formation;
	/* Formation spacing; offsets grow with formationSpacing + 1. */
	uint8_t formationSpacing;
	/* Global group number, matched by trigger variable type 8. */
	uint8_t globalGroup;
	uint8_t unused60; /* Loaded with the record; nothing reads it. */
	/* Rounds after the first; the craft total is numberOfCraft times
	 * numberOfWaves + 1. Mission_Init sets 0 or 99 for player groups as
	 * playerFlightGroupWaveMode says. */
	uint8_t numberOfWaves;
	uint8_t wavesDelay; /* Copied from the TIE format; nothing reads it. */
	/* Rule that ends new rounds: 1 a craft departed, 2 the team's primary
	 * goal complete, 3 the primary failed or the prevent status is 1. */
	uint8_t stopArrivingWhen;
	/* Player number, 1 to 8, that may fly the group; 0 for an AI group. */
	uint8_t playerNumber;
	/* Nonzero keeps the group from arriving unless a player owns it. */
	uint8_t arriveOnlyIfHuman;
	/* Number in the group of the craft the owner flies; for a player's
	 * group Mission_Init sets 0 when it has one craft, and 1 for 0 when
	 * it has more. */
	uint8_t playerCraft;
	uint8_t yaw;   /* Yaw byte of a static object. */
	uint8_t pitch; /* Pitch byte of a static object. */
	uint8_t roll;  /* Roll byte of a static object. */
	/* Copied from the TIE format; nothing reads it. */
	uint8_t legacyPermaDeathEnabled;
	/* Copied from the TIE format; nothing reads it. */
	uint8_t legacyPermaDeathId;
	/* Copied from the TIE format; nothing reads it. */
	uint8_t reservedPermaDeath;
	/* Index into g_fgArrivalDifficultyMasks: the difficulties the group
	 * arrives in. */
	uint8_t arrivalDifficulty;
	/* The two arrival trigger pairs. */
	MissionTriggerPair arrivalTriggers[2];
	/* 1 joins the two arrival pairs with OR, else AND. */
	uint8_t arrivals12OrArrivals34;
	/* Random part of the arrival delay, minutes. */
	uint8_t arrivalRandDelayMinutes;
	uint8_t arrivalDelayMinutes; /* Fixed arrival delay, minutes. */
	uint8_t arrivalDelaySeconds; /* Fixed arrival delay, seconds. */
	/* Trigger pair that sends the craft home and stops new rounds. */
	MissionTriggerPair departureTrigger;
	/* Delay after the departure trigger, minutes. */
	uint8_t departureDelayMinutes;
	/* Delay after the departure trigger, seconds. */
	uint8_t departureDelaySeconds;
	/* Condition, 1 to 9, on which a craft aborts its mission (shields,
	 * cannons, launcher, hull or attack); tested by
	 * paiorder_abortmissionorder. */
	uint8_t abortTrigger;
	/* Random part of the arrival delay, seconds. */
	uint8_t arrivalRandDelaySeconds;
	/* Nothing reads it; the TIE loader copies cur_start_fg into its
	 * second byte and reservedMothership. */
	int16_t editorMothership;
	uint8_t reservedMothership; /* Nothing reads it. */
	/* Flight group the group arrives from when arrivalMethod is set. */
	uint8_t arrivalMothership;
	/* Nonzero: arrive from arrivalMothership's hangar; 0: by hyperspace. */
	uint8_t arrivalMethod;
	/* Flight group the group departs into when departureMethod is set. */
	uint8_t departureMothership;
	/* Nonzero: depart into departureMothership; 0: by hyperspace. */
	uint8_t departureMethod;
	/* Second mothership to depart into, used when
	 * alternateMothershipUsed is set. */
	uint8_t alternateMothership;
	/* Nonzero when alternateMothership is set. */
	uint8_t alternateMothershipUsed;
	/* Mothership a captured craft departs into, used when
	 * capturedDepartViaMothership is set. */
	uint8_t capturedDepartureMothership;
	/* Nonzero when capturedDepartureMothership is set. */
	uint8_t capturedDepartViaMothership;
	/* The four orders; the AI follows the current one. */
	MissionOrder orders[4];
	/* Trigger pair that moves the AI to order 4 once it holds; not
	 * tested when both conditions are MISSION_COND_ALWAYS_TRUE. */
	MissionTriggerPair skipToOrder4;
	FlightGroupGoal goals[8]; /* The group's eight goals. */
	/* Loaded with the record; nothing reads it. */
	uint8_t reservedGoalsTail;
	/* Mission point x, in mission units (256 world units). Entry 0 is
	 * the start, 1 to 3 other starts, 4 the point arriving craft face;
	 * the AI's waypoints start at entry 4, and it also reads 12 and 13. */
	int16_t missionPointX[22];
	/* Mission point y; Mission_ResolveObjectOrMissionPointWorldLoc
	 * negates it. */
	int16_t missionPointY[22];
	int16_t missionPointZ[22]; /* Mission point z. */
	/* Nonzero for each mission point that is set. */
	uint16_t missionPointEnabled[22];
	/* Nothing reads it; the TIE loader copies its waypoint flags into
	 * entries 0 to 2. */
	uint8_t reservedOptionsPrefix[10];
	/* 1 hides craft numbers on the HUD; nonzero numbers craft within the
	 * group even with a global unit. */
	uint8_t disableWaveNumbering;
	/* Mission time, minutes, when the craft depart; 0:00 for none. */
	uint8_t departureClockMin;
	/* Mission time, seconds, when the craft depart. */
	uint8_t departureClockSec;
	uint8_t countermeasures; /* Countermeasure type of each craft. */
	/* Nonzero sets a destroyed craft's breakup to
	 * CRAFT_EXPLOSION_TIME_STEP_TICKS (1180) times it minus 1179 ticks;
	 * 0 gives 8 to 15 simulated seconds at random. */
	uint8_t craftExplosionTime;
	uint8_t status2; /* Second status code, tested with status1. */
	/* Global unit: its craft are numbered across its groups, trigger
	 * variable type 23 matches it, and with radio nonzero a player whose
	 * own group is in it may radio the group. */
	uint8_t globalUnit;
	/* Nothing reads it; the TIE loader copies way_brief_shown into entry
	 * 0. */
	uint8_t reservedOptions[8];
	uint8_t handicap; /* Loaded with the record; nothing reads it. */
	/* Warheads a player may choose; Mission_Init applies the choice. */
	uint8_t optionalWarheads[8];
	/* Beams a player may choose; Mission_Init applies the choice. */
	uint8_t optionalBeams[6];
	/* Countermeasures a player may choose; Mission_Init applies the
	 * choice. */
	uint8_t optionalCountermeasures[4];
	/* Kind of craft choice; with random variation on, 4 lets
	 * Mission_Init pick an optional craft at random for a group no
	 * player owns. */
	uint8_t optionalCraftCategory;
	CraftSpecies optionalCraft
		[10]; ///< Alternative CraftSpecies values exposed by mission loadout selection.
	/* numberOfCraft that goes with each optional craft. */
	uint8_t numberOfOptionalCraft[10];
	/* numberOfWaves that goes with each optional craft. */
	uint8_t numberOfOptionalCraftWaves[10];
	uint8_t reservedTail; /* Loaded with the record; nothing reads it. */
};

#pragma pack(pop)
typedef char xvt_size_XvtFlightGroup[(sizeof(XvtFlightGroup) == 1378) ? 1 : -1];

#pragma pack(push, 1)

struct MissionFlightGroup {
	XvtFlightGroup fg; /* The record as loaded, adjusted by Mission_Init. */
	int playerOwnerIdx; /* Player slot that owns the group, -1 for none. */
};

#pragma pack(pop)
typedef char xvt_size_MissionFlightGroup[(sizeof(MissionFlightGroup) == 1382)
						 ? 1
						 : -1];

extern MissionFlightGroup g_missionFlightGroups[48];
extern GlobalGoal g_missionGlobalGoals[10][7];
extern uint16_t g_currentFlightGroupIdx;
extern uint16_t g_missionFileVersion;
extern uint8_t g_initialSpawnBindPlayerCraftSlots;
extern uint8_t g_spawnLeaderObjIdx;
extern char g_missionDebugBuffer[256];
extern uint16_t g_missionFgOverrideStringHandles[48][8][3];
extern uint16_t g_globalGoalOverrideStringHandles[10][7][4][3];
extern int g_worldLocX;
extern int g_worldLocY;
extern int g_worldLocZ;
#pragma pack(push, 1)

struct Team {
	char name[16]; /* Team name, shown on the scoreboard. */
	uint8_t reserved10
		[8]; ///< Mission-file reserved bytes; loaded with the 0x1E5-byte team record and
	///< otherwise unreferenced.
	/* Per team, nonzero when allied; Mission_LoadFile marks each team
	 * allied with itself. */
	uint8_t allies[10];
	/* Texts for the local player: 0 and 1 on primary success, 2 and 3 on
	 * failure, 4 and 5 on bonus success. */
	char endOfMissionMessages[6][64];
	/* Nothing reads it; the TIE loader copies loss_msg_delay into entry
	 * 1. */
	uint8_t eomRawDelay[3];
	uint8_t eomSourceFG[3]; /* Loaded with the record; nothing reads it. */
	char voiceIDs[3][20];	/* Loaded with the record; nothing reads it. */
	uint8_t reservedTail; ///< Reserved tail byte of the 0x1E5-byte mission team record.
};

#pragma pack(pop)
typedef char xvt_size_Team[(sizeof(Team) == 485) ? 1 : -1];

extern Team g_missionTeams[10];

/* Stored as int8_t in the binary (IDB enum MissionType). */
typedef int8_t MissionType;

enum {
	MISSION_TYPE_TRAINING = 0x0,
	MISSION_TYPE_SIMULATOR_1 = 0x1,
	MISSION_TYPE_MELEE = 0x2,
	MISSION_TYPE_SIMULATOR_2 = 0x3,
	MISSION_TYPE_COMBAT = 0x4,
};

#pragma pack(push, 1)

struct MissionHeader {
	int16_t numFlightGroups; /* Flight groups in g_missionFlightGroups. */
	uint16_t numMessages;	 /* Messages in g_missionMessages. */
	/* Nothing in this build reads timeLimitMin or timeLimitSec; the mission countdown comes from
	 * timeLimitMinutes. A TIE-format mission stores its header's backdrop byte in timeLimitMin. */
	uint8_t timeLimitMin;
	uint8_t timeLimitSec;
	uint8_t winType; /* Loaded; nothing reads it. */
	/* Seed of the backdrop and asteroid field layout in Mission_Init. */
	uint8_t backdrop;
	uint8_t rescue;		   /* Loaded; nothing reads it. */
	uint8_t allWaypointsShown; /* Loaded; nothing reads it. */
	uint8_t variables[8];	   /* Loaded; nothing reads it. */
	/* Names of IFF 2 to 5; goals_outputgoal skips a '1' at the start. */
	char iffNames[4][20];
	MissionType
		missionType; ///< One-byte mission mode; XVT uses the shared legacy values through SKIRMISH (0..4).
	uint8_t goalsUnimportant; ///< Nonzero suppresses normal mission-goal importance/failure handling.
	uint8_t timeLimitMinutes; ///< Mission countdown duration in whole minutes; zero disables the
	///< header-supplied limit.
	uint8_t reserved[61]; /* Loaded with the record; nothing reads it. */
};

#pragma pack(pop)
typedef char xvt_size_MissionHeader[(sizeof(MissionHeader) == 162) ? 1 : -1];

extern MissionHeader g_missionHeader;
extern uint16_t g_asteroidFieldRandSeed;

struct MissionClock {
	uint8_t reserved[3]; /* Nothing uses it by name. */
	/* Hours; only the elapsed clock uses them, wrapping at 24. */
	uint8_t hours;
	uint8_t minutes; /* Minutes; the elapsed clock wraps at 60. */
	uint8_t seconds; /* Seconds, 0 to 59. */
	/* Ticks left in the elapsed clock's current second; the countdown's
	 * stays 0. */
	int16_t subsecondTicks;
};

extern MissionClock g_missionElapsedClock;
extern MissionClock g_missionCountdownClock;

#pragma pack(push, 1)

struct MissionMessage {
	char message
		[64]; ///< Message text passed directly to the in-flight message queue.
	uint8_t sentToTeam
		[10]; ///< Nonzero entry allows the corresponding player IFF/team to receive the message.
	MissionTriggerPair triggerPairs
		[2]; ///< Two trigger pairs evaluated before the message becomes active.
	char voice
		[16]; ///< Voice resource name stored by the mission file format; not consumed by XVT's runtime
		      ///< message path.
	uint8_t delay5s; ///< Raw per-message delay copied to the runtime countdown.
	uint8_t triggerPair1OrTriggerPair2; ///< Value 1 combines the trigger-pair results with OR; other values
					    ///< use AND.
};

#pragma pack(pop)
typedef char xvt_size_MissionMessage[(sizeof(MissionMessage) == 114) ? 1 : -1];

enum { MISSION_MESSAGE_COUNT = 64 };

extern MissionMessage g_missionMessages[MISSION_MESSAGE_COUNT];

enum {
	TEAM_SCORE_BONUS = 0,
	TEAM_SCORE_MISSION = 1,
};

typedef enum FlightGroupOutcomeIndex {
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
} FlightGroupOutcomeIndex;

struct MissionFlightRuntimeState {
	/* Per team, its bonus score (TEAM_SCORE_BONUS) and mission score
	 * (TEAM_SCORE_MISSION). */
	int teamScores[2][10];
	/* Per team: full, shared and assist kills (rows 0 to 2) and craft
	 * lost (row 3). */
	uint16_t teamKillStats[4][10];
	/* Per team and flight group: craft it inspected (row 0, by
	 * collide_collisions) and captured (row 1, by
	 * paiman_TransferObjectToAiTeam). Nothing reads it by name. */
	uint16_t teamFgInspectedCapturedCounts[2][10][48];
	/* Per team and flight group, the role code 1 to 12 from craftRole, 0
	 * for none. */
	uint8_t teamFgDesignationCode[10][48];
	/* 1 once any team's primary goal is complete, else 0. */
	uint8_t globalPrimaryGoalStatus;
	/* Set to 0 by Mission_InitFlightRuntimeState; no game code reads it,
	 * only the modern build's snapshot copies it. */
	uint16_t globalGoalStatusUnused;
	/* 1 once any team's bonus goal is complete, else 0. */
	uint8_t globalBonusGoalStatus;
	/* Per team, the last state of global goals 0 to 2: 1 met, 2 failed,
	 * 4 undecided, 0 none. */
	uint8_t teamGlobalGoalState[10][3];
	/* Per team, its primary, prevent and bonus status: 0 open, 1
	 * complete, 2 failed. */
	uint8_t teamGoalStatus[10][3];
	/* Per team, global goal and trigger: craft that met it (row 0) and
	 * the total (row 1), for the goals page. */
	uint16_t globalGoalTriggerCounts[2][10][3][4];
	/* Per team, elapsed seconds when its primary goal completed. */
	unsigned int teamMissionCompletionTimeSeconds[10];
	/* Per team, 1 when it has a flight group, not a backdrop, that has
	 * craft and may arrive. */
	uint8_t teamHasCountableCraft[10];
	/* Per team, 1 once it called reinforcements. */
	uint8_t teamReinforcementsCalled[10];
};

struct MissionFgRuntimeStats {
	/* 1 once the group's arrival started or was closed. */
	uint8_t hasArrived;
	uint8_t wavesRemaining; /* Rounds still to come. */
	/* 1 while the arrival delay counts down. */
	uint8_t arrivalDelayPending;
	/* Nonzero when the group may arrive at this difficulty and its
	 * player-connection arrival triggers have not failed. */
	uint8_t arrivalEnabled;
	int16_t arrivalDelayTimer; /* Simulated seconds left before arrival. */
	/* Start point, 0x8000 plus a mission point index. */
	uint16_t currentMissionPointRef;
	/* Craft created so far; numbers craft in the group. */
	uint16_t spawnedCraftCount;
	/* Craft per FLIGHT_GROUP_OUTCOME_ index; FLIGHT_GROUP_OUTCOME_TOTAL
	 * counts every craft of every round. */
	uint16_t outcomeCount[FLIGHT_GROUP_OUTCOME_COUNT];
	/* The same outcomes for the special cargo craft, mostly 0 or 1. */
	uint8_t specialCargoOutcome[FLIGHT_GROUP_OUTCOME_COUNT];
	/* Per team, craft of the group it inspected. */
	uint8_t teamInspected[10];
	/* Per team, the special cargo craft inspected. */
	uint8_t teamSpecialCargoInspected[10];
	/* Per team, craft that ended before that team identified them. */
	uint8_t teamUninspectedLost[10];
	/* Per team, the special cargo craft ended uninspected. */
	uint8_t teamSpecialCargoUninspectedLost[10];
	/* Per team, craft it captured that departed. */
	uint8_t teamCapturedDepartedCount[10];
	/* Per team, the special cargo craft captured and departed. */
	uint8_t teamSpecialCargoCapturedDeparted[10];
	/* Per team, craft that ended without that team capturing them. */
	uint8_t teamUncapturedLost[10];
	/* Per team, the special cargo craft ended uncaptured. */
	uint8_t teamSpecialCargoUncapturedLost[10];
	/* Mission_InitFlightRuntimeState zeroes this with the counts above; nothing in this build reads it. */
	uint8_t teamEventExtra[4][10];
	/* Per team, eight goal states (index 8 times team plus goal): 4
	 * pending, 1 met, 2 failed, 8 a met per-craft bonus goal, 0 none. */
	uint8_t goalState[80];
};

extern MissionFgRuntimeStats g_missionFgStats[48];
extern const uint8_t g_missionConditionUsesCountByCondition[48];
extern uint16_t g_missionConditionTotalCount;
extern uint16_t g_missionConditionCurrentCount;
extern int g_preparedSpawnMissionX;
extern int g_preparedSpawnMissionY;
extern int g_preparedSpawnMissionZ;
extern uint16_t g_preparedSpawnYawByte;
extern uint16_t g_preparedSpawnPitchByte;
extern uint16_t g_preparedSpawnRollByte;
extern uint16_t g_nextObjectSignature;
extern const uint8_t g_genusConvert[12];
extern const uint8_t g_familyConvert[4];

#pragma pack(push, 1)

struct EMissionStruct {
	uint8_t time_min; /* Not read. */
	uint8_t time_sec; /* Not read. */
	uint8_t win_type; /* Not read. */
	uint8_t backdrop; /* Copied to g_missionHeader.timeLimitMin. */
	uint8_t rescue;	  /* Copied to g_missionHeader.rescue. */
	/* Copied to g_missionHeader.allWaypointsShown. */
	uint8_t all_way_shown;
	uint8_t mis_var[8];  /* Copied to g_missionHeader.variables. */
	int8_t win_bonus[2]; /* Not read. */
	/* Copied to team 0's end-of-mission texts 0 and 1. */
	uint8_t win_msg1[2][64];
	/* Copied to team 0's end-of-mission texts 4 and 5. */
	uint8_t win_msg2[2][64];
	/* Copied to team 0's end-of-mission texts 2 and 3. */
	uint8_t loss_msg[2][64];
	uint8_t loss_msg_delay; /* Copied to team 0's eomRawDelay[1]. */
	uint8_t loss_unused;	/* Not read. */
	/* Entry 0 is copied to all four IFF names; a '1' at the start in entry
	 * iff - 2 puts IFF 2, 3 and 5 on team 1. */
	char neutral_name[4][12];
};

#pragma pack(pop)
typedef char xvt_size_EMissionStruct[(sizeof(EMissionStruct) == 450) ? 1 : -1];

#pragma pack(push, 1)

struct ECondStruct {
	uint8_t cond; /* Copied to MissionTrigger.condition. */
	uint8_t type; /* Copied to MissionTrigger.variableType. */
	uint8_t id;   /* Copied to MissionTrigger.variable. */
	uint8_t pct;  /* Copied to MissionTrigger.amount. */
};

#pragma pack(pop)
typedef char xvt_size_ECondStruct[(sizeof(ECondStruct) == 4) ? 1 : -1];

#pragma pack(push, 1)

struct EAIStruct {
	uint8_t order; /* Copied to MissionOrder.order. */
	uint8_t speed; /* Copied by position to MissionOrder.throttle. */
	/* Copied by position to MissionOrder.variable1 to variable4. */
	uint8_t var[4];
	/* Copied by position to MissionOrder.secondaryTargetTypes. */
	uint8_t target_type[2];
	/* Copied by position to MissionOrder.secondaryTargets. */
	uint8_t target_id[2];
	/* Copied by position to MissionOrder.target3OrTarget4. */
	uint8_t target_op;
	uint8_t target_unused; /* Copied by position to MissionOrder.unused0. */
	uint8_t pri_type;      /* Copied to MissionOrder.target1Type. */
	uint8_t pri_id;	       /* Copied to MissionOrder.target1. */
	uint8_t secondary_type; /* Copied to MissionOrder.target2Type. */
	uint8_t secondary_id;	/* Copied to MissionOrder.target2. */
	/* Copied by position to MissionOrder.target1OrTarget2. */
	uint8_t pri_secondary_op;
	/* Copied by position to MissionOrder.unused1. */
	uint8_t pri_secondary_unused;
};

#pragma pack(pop)
typedef char xvt_size_EAIStruct[(sizeof(EAIStruct) == 18) ? 1 : -1];

#pragma pack(push, 1)

/* One TIE-format flight group record; Mission_LoadFile copies its fields
 * into the XvtFlightGroup fields named below. */
struct EFGStruct {
	char name[12];	       /* Copied to name. */
	char cmdr[12];	       /* Copied to craftRole. */
	char contents[2][12];  /* Copied to cargo and specialCargo. */
	uint8_t special_craft; /* Copied to specialCargoCraft. */
	uint8_t special_flag;  /* Copied to randomSpecialCargoCraft. */
	CraftSpecies
		species; ///< CraftSpecies stored in the legacy EFG mission record.
	uint8_t count;	 /* Copied to numberOfCraft. */
	uint8_t status;	 /* Copied to status1. */
	uint8_t warhead; /* Copied to warhead. */
	uint8_t beam;	 /* Copied to beam. */
	uint8_t side;	 /* Copied to iff; also decides the team. */
	uint8_t skill;	 /* Copied to groupAI. */
	uint8_t camoflage;    /* Copied to markings. */
	uint8_t camo_flag;    /* Copied to radio. */
	uint8_t camo_unused;  /* Copied to unused5C. */
	uint8_t formation;    /* Copied to formation. */
	uint8_t form_spacing; /* Copied to formationSpacing. */
	uint8_t set;	      /* Copied to globalGroup. */
	uint8_t set_unused;   /* Copied to unused60. */
	uint8_t waves;	      /* Copied to numberOfWaves. */
	uint8_t wave_delay;   /* Copied to wavesDelay. */
	/* Nonzero: player number 1, playerCraft player_flag - 1. */
	uint8_t player_flag;
	uint8_t heading;	   /* Copied to yaw. */
	uint8_t pitch;		   /* Copied to pitch. */
	uint8_t rotation;	   /* Copied to roll. */
	uint8_t link_flag;	   /* Copied to legacyPermaDeathEnabled. */
	uint8_t link_code;	   /* Copied to legacyPermaDeathId. */
	uint8_t link_unused;	   /* Copied to reservedPermaDeath. */
	uint8_t difficulty;	   /* Copied to arrivalDifficulty. */
	ECondStruct start_cond[2]; /* Copied to arrivalTriggers[0]. */
	uint8_t start_op; /* Copied to arrivalTriggers[0].trigger1OrTrigger2. */
	uint8_t start_unused;	 /* Not read. */
	uint8_t start_delay_min; /* Copied to arrivalDelayMinutes. */
	uint8_t start_delay_sec; /* Copied to arrivalDelaySeconds. */
	ECondStruct stop_cond;	 /* Copied to departureTrigger.triggers[0]. */
	uint8_t stop_min;	 /* Copied to departureDelayMinutes. */
	uint8_t stop_sec;	 /* Copied to departureDelaySeconds. */
	uint8_t stop_abort;	 /* Copied to abortTrigger. */
	uint8_t stop_unused;	 /* Not read. */
	/* Copied over editorMothership's second byte and reservedMothership. */
	int16_t cur_start_fg;
	uint8_t start_fg;		/* Copied to arrivalMothership. */
	uint8_t start_fg_used;		/* Copied to arrivalMethod. */
	uint8_t pri_stop_fg;		/* Copied to departureMothership. */
	uint8_t pri_stop_fg_used;	/* Copied to departureMethod. */
	uint8_t secondary_stop_fg;	/* Copied to alternateMothership. */
	uint8_t secondary_stop_fg_used; /* Copied to alternateMothershipUsed. */
	uint8_t capture_fg;	 /* Copied to capturedDepartureMothership. */
	uint8_t capture_fg_used; /* Copied to capturedDepartViaMothership. */
	/* Copied to orders 0 to 2; order 2's code comes from ai[1]. */
	EAIStruct ai[3];
	uint8_t pri_win_cond; /* Goal 0, a primary goal: its eventCondition. */
	uint8_t pri_win_pct;  /* Goal 0's amount. */
	/* Goal 1, a bonus goal: its eventCondition. */
	uint8_t secondary_win_cond;
	uint8_t secondary_win_pct; /* Goal 1's amount. */
	uint8_t loss_cond;    /* Goal 2, a prevent goal: its eventCondition. */
	uint8_t loss_pct;     /* Goal 2's amount. */
	uint8_t bonus_cond;   /* Goal 3, a bonus goal: its eventCondition. */
	uint8_t bonus_pct;    /* Goal 3's amount. */
	int8_t bonus_points;  /* Goal 3's points. */
	uint8_t bonus_unused; /* Not read. */
	int16_t way_x[15];    /* Copied to missionPointX 0 to 14. */
	int16_t way_y[15];    /* Copied to missionPointY 0 to 14. */
	int16_t way_z[15];    /* Copied to missionPointZ 0 to 14. */
	int16_t way_used[15]; /* Copied to missionPointEnabled 0 to 14. */
	uint8_t way_shown;    /* Copied to reservedOptionsPrefix[0]. */
	uint8_t way_unused;   /* Copied to reservedOptionsPrefix[1]. */
	uint8_t way_brief_link;	 /* Copied to reservedOptionsPrefix[2]. */
	uint8_t way_brief_shown; /* Copied to reservedOptions[0]. */
};

#pragma pack(pop)
typedef char xvt_size_EFGStruct[(sizeof(EFGStruct) == 292) ? 1 : -1];

#pragma pack(push, 1)

struct EMissionGoal {
	/* Copied to triggerPairs[0] of team 0's global goal. */
	ECondStruct subcond[2];
	/* First 16 bytes copied to name, the 17th to version. */
	uint8_t editor_name[17];
	uint8_t or_joined; /* Copied to triggerPairs[0].trigger1OrTrigger2. */
	/* The TIE-format loader copies _pad[0] into GlobalGoal.rawDelay; nothing reads _pad[1]. */
	uint8_t _pad[2];
};

#pragma pack(pop)
typedef char xvt_size_EMissionGoal[(sizeof(EMissionGoal) == 28) ? 1 : -1];

#pragma pack(push, 1)

struct XvtV10FlightGroupText {
	char name[12]; /* Copied to name in a branch that never runs. */
	/* Copied to craftRole in a branch that never runs. */
	char craftRole[12];
	char cargo[12]; /* Copied to cargo in a branch that never runs. */
	/* Copied to specialCargo in a branch that never runs. */
	char specialCargo[12];
};

#pragma pack(pop)
typedef char xvt_size_XvtV10FlightGroupText
	[(sizeof(XvtV10FlightGroupText) == 48) ? 1 : -1];

#pragma pack(push, 1)

struct TieRadioMessage {
	char message[64];	   /* Copied to MissionMessage.message. */
	ECondStruct conditions[2]; /* Copied to triggerPairs[0]. */
	char voice[16];		   /* Copied to MissionMessage.voice. */
	uint8_t delay5s;	   /* Copied to MissionMessage.delay5s. */
	/* Copied to triggerPairs[0].trigger1OrTrigger2. */
	uint8_t condition1OrCondition2;
};

#pragma pack(pop)
typedef char xvt_size_TieRadioMessage[(sizeof(TieRadioMessage) == 90) ? 1 : -1];

#pragma pack(push, 1)

struct XvtV10MissionHeader {
	uint16_t numFlightGroups;  /* Copied in a branch that never runs. */
	uint16_t numMessages;	   /* Copied in a branch that never runs. */
	uint8_t timeLimitMin;	   /* Copied in a branch that never runs. */
	uint8_t timeLimitSec;	   /* Copied in a branch that never runs. */
	uint8_t winType;	   /* Copied in a branch that never runs. */
	uint8_t backdrop;	   /* Copied in a branch that never runs. */
	uint8_t rescue;		   /* Copied in a branch that never runs. */
	uint8_t allWaypointsShown; /* Copied in a branch that never runs. */
	uint8_t variables[8];	   /* Copied in a branch that never runs. */
	char iffNames[4][12];	   /* Copied in a branch that never runs. */
	MissionType missionType;   /* Copied in a branch that never runs. */
	uint8_t goalsUnimportant;  /* Not read. */
	uint8_t missionTimeLimit;  /* Not read. */
	uint8_t reserved[61];	   /* Not read. */
};

#pragma pack(pop)
typedef char xvt_size_XvtV10MissionHeader[(sizeof(XvtV10MissionHeader) == 130)
						  ? 1
						  : -1];

uint16_t Mission_IsSpecialCargoInspected(unsigned int flightGroupIdx,
					 uint16_t specialCargoCraft);
void Mission_UpdateLogic(void);
int Mission_EvaluateTriggerPair(const MissionTriggerPair *triggerPair,
				int16_t includeDepartedAsDestroyed);
int16_t Mission_EvaluateCondition(uint16_t conditionType, int16_t variableType,
				  uint16_t variable, int16_t amountType,
				  int16_t includeDepartedAsDestroyed,
				  uint16_t teamFilter);
int16_t Mission_FlightGroupMatchesTriggerVariable(uint16_t flightGroupIdx,
						  int16_t variableType,
						  uint16_t variable);
int16_t Mission_ObjectMatchesTriggerVariable(uint16_t objectIdx,
					     uint16_t variableType,
					     uint16_t variable);
void Mission_RecordCraftOutcome(uint16_t objIdx, uint16_t flightGroupIdx,
				uint16_t outcomeId);
int16_t Mission_CloseUnavailableFlightGroupAccounting(int flightGroupIdx);
void Mission_CreditDestructionDamageContributors(uint16_t sourceObjIdx,
						 uint16_t victimObjIdx);
void Mission_CreditPlayerKillContribution(uint16_t victimObjIdx,
					  int specialCargoFlag,
					  int contributionTier, int playerIdx,
					  int victimOwnerIdx, int victimRating);
void Mission_CreditTeamKillContribution(uint16_t victimObjIdx,
					int specialCargoFlag,
					int contributionTier, int teamIdx);
void Mission_RecordProjectileHitStats(uint16_t projectileObjIdx);
int Mission_RecordPlayerCraftLoss(unsigned int objIdx,
				  int allowPendingDamageCredit);
void Mission_RecordPlayerCraftLossAttribution(int attackerFlightGroupIdx,
					      int victimObjIdx,
					      int contributionTier);
int Mission_ApplyFlightGroupGoalScore(int16_t eventCondition,
				      uint16_t flightGroupIdx, int playerIdx,
				      uint16_t goalScoreReductionLevel,
				      int specialCargoFlag, int teamIdx);
void Mission_ApplyTeamGoalScoreAllEnabledTeams(int16_t eventCondition,
					       uint16_t flightGroupIdx,
					       int specialCargoFlag);
void Mission_ApplyTeamGoalScoreForTeam(int16_t eventCondition,
				       uint16_t flightGroupIdx,
				       int specialCargoFlag, uint8_t teamIdx);
int Mission_ClockToSeconds(uint8_t hours, uint8_t minutes, uint8_t seconds);
int Mission_ComputeKillScoreForObject(int victimObjIdx);
int Mission_ComputeCraftPointValue(int objIdx);
int Mission_GetElapsedClockSeconds(void);
uint16_t Mission_Init(char *fileName);
void Mission_InitFlightRuntimeState(void);
int16_t Mission_StartFlightGroupArrival(uint16_t craftOrdinal);
void Mission_UpdateFlightGroupArrivals(void);
void Mission_ProcessFlightGroupWaveCompletion(uint16_t flightGroupIdx);
void Mission_SpawnCurrentFlightGroupWave(void);
int Mission_HasCapacityForCurrentFlightGroupWave(void);
int16_t Mission_SpawnFlightGroupWaveCraft(uint16_t craftOrdinal);
uint16_t Mission_InitFlightGroupObjectSlot(void);
void Mission_SpawnFlightGroupStaticObjects(uint16_t craftOrdinal);
uint16_t Mission_SpawnPreparedObject(uint16_t flightGroupIdx, int16_t genusId,
				     uint8_t objectType);
void Mission_ResolveObjectOrMissionPointWorldLoc(
	unsigned int objOrMissionPointRef, int flightGroupIdx);
void Mission_ResolveFormationSlotWorldLoc(uint16_t flightGroupIdx,
					  uint16_t formationSlotIdx,
					  uint16_t basisObjIdx);
int Mission_LoadFile(char *fileName);
int Mission_SyncPilotNetworkPlayersToSessionSlots(void);
void Mission_FreeOverrideStringHandles(void);

#ifdef __cplusplus
}
#endif

#endif
