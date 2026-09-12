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
	uint8_t currentOrderSlot;
	AiOrderScratch orderScratch;
	uint8_t orderStateFlag;
	uint8_t pendingPlanId;
	uint8_t currentPlanId;
	uint8_t waypointIndex;
	uint8_t savedPlanId;
	int thinkInterval;
	int thinkTimer;
	int16_t savedRandSeed;
	uint16_t targetObjIdx;
	uint16_t targetSignature;
	uint16_t targetComponent;
	uint8_t hasLiveTarget;
	int aimPointX;
	int aimPointY;
	int aimPointZ;
	uint16_t candidateTargetIdx;
	uint8_t escortTargetFG;
	uint16_t targetZAngle;
	uint16_t targetRoll;
	uint16_t targetXYAngle;
	AiManeuverMode maneuverMode;
	uint8_t maneuverPhase;
	int maneuverTimer;
	int16_t aiPlanState;
};

struct AiFlightState {
	uint16_t threatObjIdx;
	uint16_t impactObjIdx;
	uint8_t goHomeFlag;
	uint8_t missionAbortedFlag;
	uint8_t departTimerFlag;
	uint8_t departClockHours;
	uint8_t departClockMinutes;
	uint8_t departClockSeconds;
	uint8_t maneuverCounter;
	uint8_t reactionTimer;
	uint8_t boardedAccountingDone;
	uint8_t orderActionCounter;
	uint8_t orderActionFlag;
	uint8_t objSignatureCount;
	uint16_t objSignatures[10];
	int16_t maxSpeedCache;
	int16_t motionScale;
	uint8_t climbState;
	uint8_t diveState;
	int16_t pitchRate;
	int16_t pitchAccel;
	uint8_t headingState;
	uint8_t headingForce;
	uint16_t headingStep;
	int16_t rollRate;
	int16_t rollAccel;
	uint8_t enterFlag;
	uint16_t rollStep;
	int16_t turnRate;
	int16_t turnAccel;
	uint8_t turnState;
	int16_t turnStep;
	uint8_t formationType;
	uint8_t separation;
};

struct PaiPlanTokenDef {
	char name[80];
	int16_t value;
};

#pragma pack(push, 1)

struct PaiPlanRecord {
	char name[80];
	uint8_t isDefined;
	uint32_t dataOffset;
};

#pragma pack(pop)
typedef char xvt_size_PaiPlanRecord[(sizeof(PaiPlanRecord) == 85) ? 1 : -1];

struct PaiContext {
	uint16_t objectIndex;
	CraftData* craft;
	AiController* controller;
	uint16_t leaderObjectIndex;
	CraftData* targetCraft;
	uint16_t orderFlightGroupIndex;
	uint16_t orderSlot;
	int32_t currentPointX;
	int32_t currentPointY;
	int32_t currentPointZ;
	uint16_t skillTier;
	uint16_t initialManeuverId;
	uint8_t* planCursor;
	uint8_t requireLiveOrderTarget;
	uint8_t nullPlanId;
	uint8_t targetSearchFlags;
	int32_t targetSearchOriginX;
	int32_t targetSearchOriginY;
	int32_t targetSearchOriginZ;
};

extern PaiContext g_paiContext;
extern int g_paiSkipToOrder4Checked;
extern int g_targetRangeScore;
extern uint16_t g_aiSkillValueQ16ByLevel[8];
extern const uint16_t g_aiThinkIntervalBySkill[8];
extern PaiPlanRecord g_planTable[256];
extern uint8_t g_planOrderData[0x20000];
extern int g_rotatedX;
extern int g_rotatedY;
extern int g_rotatedZ;
extern uint8_t* g_planDataPtrs[256];
extern int g_planCount;
extern const char* const g_builtinPlanNameTable[76];

enum { PAI_PLAN_REPORT_MESSAGE_COUNT = 74 };

extern const uint8_t g_planReportMessageIdByPlanId[PAI_PLAN_REPORT_MESSAGE_COUNT];
extern uint8_t g_builtinPlanIdByNameIndex[256];
extern uint8_t g_orderLeaderBuiltinPlanNameIndex[40];
extern const uint8_t g_orderFollowerBuiltinPlanNameIndex[40];

void pai_UpdateAllCraftAI(void);
void pai_ApplyPendingPlanTargetAndManeuver(unsigned int objectIdx);
void pai_ProcessPlan(void);
void pai_setupcraftcontext(uint16_t objectIdx);
int pai_SkillValueToTier(uint16_t skillValue);
uint16_t pai_FindMothershipObject(int16_t mothershipFlightGroupIdx);
int pai_IsObjectTargetableNearCurrentPoint(int unused, unsigned int objIdx, int expandRange);
int16_t pai_IsObjectWithinCurrentOrderRange(uint16_t objIdx);
int16_t pai_OrderSlotCanBoardTarget(uint16_t orderSlot);
int16_t pai_FindBoardingTargetFromOrder(uint16_t orderSlot);
int pai_IsObjectWithinCurrentPointRange(unsigned int objIdx, unsigned int maxRangeScore);
void pai_UpdateAimPointFromOrderTarget(void);
void pai_SetFlightGroupFormation(unsigned int flightGroupIdx, unsigned int formationType,
								 unsigned int formationSpacing);
void pai_ObjectRefDirectionToObjectRef(unsigned int fromRef, unsigned int toRef);
void pai_ObjectRefUpdateApproxRangeScore(unsigned int fromRef, unsigned int toRef);
void pai_calcrotatedpoint(ObjectRecord* obj, int16_t sideArg, int16_t upArg, int16_t fwdArg);
void pai_RotateLocalVectorToWorldScratch(ObjectRecord* objRecord, int localSide, int localUp, int localFwd);
void pai_CalcAnglesToAimPoint(void);
int16_t pai_FindNearestBoardingTarget(uint16_t target1Type, uint16_t target1, int16_t targetOrMode,
									  uint16_t target2Type, uint16_t target2);
int16_t pai_IsPlanCompleteForOrderSlot(uint16_t planId, uint16_t orderSlot);
int16_t pai_IsBoardingPlanCompleteForOrderSlot(uint16_t planId, uint16_t orderSlot);
int16_t pai_CurrentOrderTargetsMatchObject(uint16_t objectIdx);
uint16_t pai_GetEffectiveSkillValue(CraftData* craft);
int pai_OrderSlotMatchingObjectHasOrderClass(int objectIdx, int orderClass, int targetObjIdx);
int pai_FindPlanTableIndexByName(const char* planName);
int pai_FindFreePlanTableIndex(void);
int pai_FindTargetTokenIndex(const char* token);
int pai_FindManeuverTokenIndex(const char* token);
int pai_FindOrderTokenIndex(const char* token);
int pai_ReadPlanTextToken(char* token, XvtFile* stream);
int pai_CompilePlansFromText(const char* baseName);
int pai_loadplans(char* baseName);
void pai_cacheBuiltinPlanIds(void);
uint8_t* pai_getplandataptrbyname(const char* planName);
int pai_findplanbyname(const char* planName);

#ifdef __cplusplus
}
#endif

#endif
