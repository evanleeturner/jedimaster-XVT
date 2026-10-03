#ifndef XVT_FLIGHT_AI_PAI_H
#define XVT_FLIGHT_AI_PAI_H

#include "xvt/assets/file.h"
#include "xvt/flight/ai/paiorder.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stored as uint8_t in the binary. Values index the maneuver dispatch tables. */
typedef uint8_t AiManeuverMode;

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
	AI_MANEUVER_MODE_DODGE = 33,
	AI_MANEUVER_MODE_COUNT = 34,
};

struct AiController {
	/* Mission order slot, 0 to 3, the craft works on: 0 at spawn, moved by
	 * the order-switching orders, 3 after a skip to order 4. */
	uint8_t currentOrderSlot;
	/* Completion state and goal progress of each order slot. */
	AiOrderProgress orderProgress;
	/* 1 once the skip-to-order-4 trigger moved the craft to slot 3. */
	uint8_t skippedToOrder4;
	/* Despite the name, the plan the craft runs now: pai_setupcraftcontext
	 * reads its orders, and pai_ProcessPlan replaces it when one of them
	 * fires. */
	uint8_t pendingPlanId;
	/* Leader plan of the craft's current order, for a follower too, set
	 * when the order starts or changes; plans and orders test it by
	 * name. */
	uint8_t currentPlanId;
	/* The mission point the craft is flying to; waypoints are points 4 to 11. The drop-off order reuses
	 * it: it starts at 0 when the craft reaches the destination group, names the formation slot of the
	 * next craft to deliver, and counts the deliveries made. */
	uint8_t waypointIndex; /* 4 at spawn; past 11 it wraps to 4. */
	/* Plan the craft ran when a player's radio command put it on
	 * craftwaitforgopln or starshipwaitforgopln; the commands that release
	 * it from craftwaitforgopln put it back. 0 at spawn. */
	uint8_t savedPlanId;
	/* Ticks between two AI thinks: the AI level's g_aiThinkIntervalBySkill
	 * entry at spawn; a few orders and maneuvers set 29 or 59. */
	int thinkInterval;
	/* Ticks to the next AI think. Flight_UpdateTimers lowers it by the
	 * ticks that pass; at 0 or below pai_UpdateAllCraftAI thinks and adds
	 * thinkInterval. */
	int thinkTimer;
	/* The craft's own state of the game's random generator:
	 * pai_UpdateAllCraftAI and collide_damagecraft swap it into
	 * g_gameRandFeedbackState while they work on the craft. GameRand() XOR
	 * 0xBEEF at spawn. */
	int16_t savedRandSeed;
	/* What the craft steers or fires at: an object index, 0x8000 plus a
	 * mission point of its flight group (0x8000 alone for the group's
	 * current point), or 0xFFFF for none. */
	uint16_t targetObjIdx;
	/* The target object's objectSignature when it was chosen, to spot its
	 * slot holding another object later; 0 for a mission point. */
	uint16_t targetSignature;
	/* Mesh of the target that warheads fired now aim at; laser.c copies it
	 * into each warhead's guidance. 0xFFFF at spawn. */
	uint16_t targetComponent;
	/* 1 when a target search or command set the target, 0 when an order
	 * pointed the craft at a mission point or, in
	 * paifight_coverleaderorder, at its leader's attacker. Apart from
	 * paiorder_leaderdeadorder copying it, read only for the cannons on
	 * disableldr1pln: with it set the ion cannons fire, their speed setting
	 * the aim ahead, and without it the others fire. */
	uint8_t hasLiveTarget;
	/* X of the world point the craft steers at: its target's position, a
	 * point ahead of it, a hangar or docking point, or a mission point. */
	int aimPointX;
	int aimPointY; /* Y of the aim point. */
	/* Z of the aim point; the climb test of the steering code compares it
	 * with the craft's Z. */
	int aimPointZ;
	/* Target a player's command gave the craft, which the target orders
	 * take up; 0xFFFF for none. Another radio command sets AI_TARGET_ABORT
	 * (251), which paiorder_evasiveorder answers by dropping the target. */
	uint16_t candidateTargetIdx;
	/* Flight group the craft escorts, set by paifight_checkescortorder; 255
	 * for none and at spawn. */
	uint8_t escortTargetFG;
	/* Pitch the steering turns the craft to, in angle units, 0x4000 being
	 * level; 0x4000 at spawn. */
	uint16_t targetZAngle;
	/* Roll the steering turns the craft to; with rollState 3 only its top
	 * bit counts, choosing the way the craft keeps rolling. */
	uint16_t targetRoll;
	/* Heading the steering turns the craft to, in angle units. */
	uint16_t targetXYAngle;
	/* Maneuver the craft flies, an AI_MANEUVER_MODE_ value;
	 * paiorder_updatecourseorder runs its step function on each think. */
	AiManeuverMode maneuverMode;
	/* Step within the maneuver; paiman_initmaneuver sets 0. */
	uint8_t maneuverPhase;
	/* Ticks left in the maneuver or its current step; Flight_UpdateTimers
	 * lowers it to 0. The HUD's target display shows it as the order
	 * time. */
	int maneuverTimer;
	/* Ticks to a maneuver's next sub-step, such as a weave or a course
	 * update; Flight_UpdateTimers lowers it to 0. */
	int16_t secondaryManeuverTimer;
};

struct AiFlightState {
	/* Object whose shot last hit the craft, set by collide.c on each hit;
	 * 0xFFFF when a plan starts. */
	uint16_t threatObjIdx;
	/* Object the craft last collided with, kept while the roll that gave it
	 * lasts; 0xFFFF otherwise and at spawn. */
	uint16_t impactObjIdx;
	/* 1 once all the craft's orders are complete, or its leader's are; it
	 * lets the departure count as departed. */
	uint8_t goHomeFlag;
	/* 1 once the craft has aborted the mission, by
	 * paiorder_abortmissionorder or a player's radio command; the abort
	 * outcome is counted once. */
	uint8_t missionAbortedFlag;
	/* 1 once the flight group's departure has started for the craft, by
	 * paiorder_stopgohomeorder or from its leader; the delay counts from
	 * the departClock fields. */
	uint8_t departTimerFlag;
	/* Mission clock hours when the departure started. */
	uint8_t departClockHours;
	/* Mission clock minutes when the departure started. */
	uint8_t departClockMinutes;
	/* Mission clock seconds when the departure started. */
	uint8_t departClockSeconds;
	/* Warheads fired since the maneuver started; paifight_fightershootorder
	 * stops at its limit. */
	uint8_t warheadsFiredThisManeuver;
	/* Hits taken since the maneuver started; paiman_attackmaneuver breaks
	 * off at the model's reactionThreshold. */
	uint8_t hitsThisManeuver;
	/* 1 once a boarding of this craft counted its group's
	 * FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION outcome. */
	uint8_t boardedAccountingDone;
	uint8_t timesBoarded; /* Times other craft have boarded this one. */
	/* 1 once this craft's first docking counted its group's
	 * FLIGHT_GROUP_OUTCOME_FAILED_MISSION outcome. */
	uint8_t dockingAccountingDone;
	/* Signatures recorded in dockedTargetSignatures, at most 9 after a
	 * docking: a tenth docking writes slot 9, which no search reads, and
	 * later ones overwrite it. */
	uint8_t dockedTargetCount;
	/* Signatures of objects the craft has docked with, so it does not board
	 * them again. Slot 0 also keeps thinkInterval while the craft arrives
	 * from hyperspace. */
	uint16_t dockedTargetSignatures[10];
	/* The model's maxSpeed, copied at spawn: the base of the AI's speeds,
	 * and 0 for a craft that cannot move, which several orders skip. */
	int16_t maxSpeedCache;
	/* Set to -1 at spawn; nothing reads it except the world snapshot's
	 * copy. */
	int16_t motionScale;
	/* 1 while a climb runs, which the steering code ends at the aim point's
	 * Z by setting a level pitch. Only paiman_cruisemaneuver sets 1, and
	 * its own paiman_setflighttotarget call clears it in the same think. */
	uint8_t climbState;
	/* 1 while a dive pull-out runs, which
	 * Flight_UpdateDivePulloutPitchTarget steps and ends with 2. Nothing
	 * sets it to 1, so that function never runs. */
	uint8_t diveState;
	/* The model's pitch rate, in angle units per
	 * SIMULATION_TICKS_PER_SECOND ticks at a full step, copied at spawn. */
	int16_t pitchRate;
	/* Fraction of 65,536 scaling the pitch step; -1, all of it, from spawn
	 * on. */
	int16_t pitchAccel;
	/* 0 no pitch; 1 lowers the pitch value toward targetZAngle; 2 raises
	 * it; 3 reached. */
	uint8_t pitchState;
	/* 1 to pitch on past targetZAngle: when the pitch crosses 0 or 0x8000
	 * the steering flips yaw and roll by half a circle, clears this and
	 * turns back toward the target. */
	uint8_t pitchThroughLoop;
	/* Fraction of 65,536 scaling the pitch step. */
	uint16_t pitchStepScale;
	/* The model's roll rate, in angle units per SIMULATION_TICKS_PER_SECOND
	 * ticks at a full step, copied at spawn. */
	int16_t rollRate;
	/* Fraction of 65,536 scaling the roll step; -1, all of it, from spawn
	 * on. */
	int16_t rollAccel;
	/* 0 no roll; 1 or 2 roll to targetRoll (nothing sets 2); 3 keep
	 * rolling; 4 reached. In 0 and 4 the steering banks the craft into its
	 * turns. */
	uint8_t rollState;
	/* Fraction of 65,536 scaling the roll step, which the steering
	 * doubles. */
	uint16_t rollStep;
	/* The model's yaw rate, in angle units per SIMULATION_TICKS_PER_SECOND
	 * ticks at a full step, copied at spawn. */
	int16_t turnRate;
	/* Fraction of 65,536 scaling the turn step; -1, all of it, from spawn
	 * on. */
	int16_t turnAccel;
	/* 0 no turn; 1, 2 or 3 turn to targetXYAngle, 3 being set when the yaw
	 * gets there. */
	uint8_t turnState;
	/* Fraction of 65,536 scaling the turn step, kept in 16 signed bits. */
	int16_t turnStep;
	/* Formation, 0 to 33, indexing g_formPosX, Y and Z: the flight group's
	 * at spawn, set again when the group leaves a hangar. */
	uint8_t formationType;
	/* Formation spacing; a follower scales its g_formPos offsets by its
	 * leader's plus 1. The flight group's at spawn; going home sets 1. */
	uint8_t separation;
};

struct PaiPlanTokenDef {
	/* Token as the plan text spells it; empty in the end entry. */
	char name[80];
	int16_t value; /* Byte the token compiles to. */
};

#pragma pack(push, 1)

struct PaiPlanRecord {
	/* Plan name, up to 79 characters; empty for a free entry. */
	char name[80];
	/* 1 once the plan text defined the plan, 0 while it is only named. */
	uint8_t isDefined;
	/* Where the plan's bytes start in g_planOrderData. */
	uint32_t dataOffset;
};

#pragma pack(pop)
typedef char xvt_size_PaiPlanRecord[(sizeof(PaiPlanRecord) == 85) ? 1 : -1];

struct PaiContext {
	uint16_t objectIndex;	  /* Object index of the craft. */
	CraftData *craft;	  /* The craft's data. */
	AiController *controller; /* The craft's AI controller. */
	/* The leader's object index, 255 when the craft has none. */
	uint16_t leaderObjectIndex;
	/* The leader's craft data, or the craft's own when it has no leader. */
	CraftData *leaderOrSelfCraft;
	uint16_t craftFlightGroupIndex; /* The craft's flight group. */
	/* Order slot being worked on: the craft's currentOrderSlot at setup,
	 * moved by the order-switching orders. */
	uint16_t orderSlot;
	/* X of the craft at setup; range tests measure from it. */
	int32_t craftPositionX;
	int32_t craftPositionY; /* Y of the craft at setup. */
	int32_t craftPositionZ; /* Z of the craft at setup. */
	/* Skill tier, 0 to 2, of the craft's effective skill. */
	uint16_t skillTier;
	/* Maneuver byte of the running plan; orders compare maneuverMode with
	 * it to tell whether the craft still flies its plan's own maneuver. */
	uint16_t initialManeuverId;
	/* Next order id in the running plan's bytes; pai_ProcessPlan walks
	 * it. */
	uint8_t *planCursor;
	/* 1 when target searches skip craft with no working subsystems; the
	 * disable plans and laser_UpdateMineWeaponFire set it, setup clears
	 * it. */
	uint8_t requireUndisabledTarget;
	/* Despite the name, the plan "variablepln" stands for: nullpln's id at
	 * setup, then the plan an order switch picks for the new order. */
	uint8_t nullPlanId;
	/* Tests the target searches apply: 1 attack capacity, 4 skill range,
	 * 0x10 range and enemy, 0x20 measure from targetSearchOrigin; 2 is set
	 * but nothing tests it. Setup leaves it as it was. */
	uint8_t targetSearchFlags;
	/* X of the point searches measure from with targetSearchFlags 0x20, a
	 * turret hardpoint or the craft's position, set by
	 * paifight_gunneroffenseorder. */
	int32_t targetSearchOriginX;
	int32_t targetSearchOriginY; /* Y of that search origin. */
	int32_t targetSearchOriginZ; /* Z of that search origin. */
};

extern PaiContext g_paiContext;
extern int g_paiSkipToOrder4Checked;
extern int g_lastRoughDistance;
extern uint16_t g_aiSkillValueQ16ByLevel[8];
extern const uint16_t g_aiThinkIntervalBySkill[8];
extern PaiPlanRecord g_planTable[256];
extern uint8_t g_planOrderData[0x20000];
extern int g_rotatedX;
extern int g_rotatedY;
extern int g_rotatedZ;
extern uint8_t *g_planDataPtrs[256];
extern int g_planCount;
extern const char *const g_builtinPlanNameTable[76];

enum { PAI_PLAN_REPORT_MESSAGE_COUNT = 74 };

extern const uint8_t
	g_planReportMessageIdByPlanId[PAI_PLAN_REPORT_MESSAGE_COUNT];
extern uint8_t g_builtinPlanIdByNameIndex[256];
extern uint8_t g_orderLeaderBuiltinPlanNameIndex[40];
extern const uint8_t g_orderFollowerBuiltinPlanNameIndex[40];

void pai_UpdateAllCraftAI(void);
void pai_ApplyPendingPlanTargetAndManeuver(unsigned int objectIdx);
void pai_ProcessPlan(void);
void pai_setupcraftcontext(uint16_t objectIdx);
int pai_SkillValueToTier(uint16_t skillValue);
uint16_t pai_FindMothershipObject(int16_t mothershipFlightGroupIdx);
int pai_IsObjectTargetableNearCraft(int unused, unsigned int objIdx,
				    int expandRange);
int16_t pai_IsObjectWithinSkillRangeOfCraft(uint16_t objIdx);
int16_t pai_OrderSlotCanBoardTarget(uint16_t orderSlot);
int16_t pai_FindBoardingTargetFromOrder(uint16_t orderSlot);
int pai_IsObjectWithinRangeOfCraft(unsigned int objIdx,
				   unsigned int maxRoughDistance);
void pai_UpdateAimPointFromOrderTarget(void);
void pai_SetFlightGroupFormation(unsigned int flightGroupIdx,
				 unsigned int formationType,
				 unsigned int formationSpacing);
void pai_ObjectRefDirectionToObjectRef(unsigned int fromRef,
				       unsigned int toRef);
void pai_ObjectRefUpdateRoughDistance(unsigned int fromRef, unsigned int toRef);
void pai_calcrotatedpoint(ObjectRecord *obj, int16_t sideArg, int16_t upArg,
			  int16_t fwdArg);
void pai_RotateLocalVectorToWorldScratch(ObjectRecord *objRecord, int localSide,
					 int localUp, int localFwd);
void pai_CalcAnglesToAimPoint(void);
int16_t pai_FindNearestBoardingTarget(uint16_t target1Type, uint16_t target1,
				      int16_t targetOrMode,
				      uint16_t target2Type, uint16_t target2);
int16_t pai_IsPlanCompleteForOrderSlot(uint16_t planId, uint16_t orderSlot);
int16_t pai_IsBoardingPlanCompleteForOrderSlot(uint16_t planId,
					       uint16_t orderSlot);
int16_t pai_CurrentOrderTargetsMatchObject(uint16_t objectIdx);
uint16_t pai_GetEffectiveSkillValue(CraftData *craft);
int pai_SetupContextAndFindOrderPlanOnTarget(int objectIdx,
					     int leaderPlanNameIndex,
					     int targetObjIdx);
int pai_FindPlanTableIndexByName(const char *planName);
int pai_FindFreePlanTableIndex(void);
int pai_FindTargetTokenIndex(const char *token);
int pai_FindManeuverTokenIndex(const char *token);
int pai_FindOrderTokenIndex(const char *token);
int pai_ReadPlanTextToken(char *token, XvtFile *stream);
int pai_CompilePlansFromText(const char *baseName);
int pai_loadplans(char *baseName);
void pai_cacheBuiltinPlanIds(void);
uint8_t *pai_getplandataptrbyname(const char *planName);
int pai_FindPlanIdByNameOrZero(const char *planName);

#ifdef __cplusplus
}
#endif

#endif
