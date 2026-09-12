#ifndef XVT_FLIGHT_AI_PAIFIGHT_H
#define XVT_FLIGHT_AI_PAIFIGHT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum AiTargetingConstant {
	AI_TARGET_ABORT = 251,
	AI_DECOY_RANGE_SMALL = 0x8000,
	AI_DECOY_RANGE_LARGE = 0x10000,
	AI_TARGET_RANGE_MAX = 0x10000,
	AI_TURRET_TARGET_PENALTY = 0x8000,
};

extern int g_gunnerCollisionProbeCount;

extern int g_paifightSearchOriginX;
extern int g_paifightSearchOriginY;
extern int g_paifightSearchOriginZ;

extern uint8_t g_aiEscortCandidateFgIdx;
extern uint8_t g_paifightGunnerTargetCandidateSet[976];

int16_t paifight_scanfortargetorder(void);
int16_t paifight_FindAttackOrderTargetFromOrder(uint16_t orderSlot);
int16_t paifight_FindNearestAttackOrderTarget(int16_t target1Type, uint16_t target1, int16_t targetOrMode,
											  int16_t target2Type, uint16_t target2);
int16_t paifight_FindEscortLeaderTargetFromOrder(uint16_t orderSlot);
int16_t paifight_FindNearestEscortLeaderTarget(int16_t target1Type, uint16_t target1,
											   int16_t targetRelationOp, int16_t target2Type,
											   uint16_t target2);
int16_t paifight_FindAttackerOfOrderTargetFromOrder(uint16_t orderSlot);
int16_t paifight_FindNearestAttackerOfMatchingTarget(int16_t target1Type, uint16_t target1,
													 int16_t targetOrMode, int16_t target2Type,
													 uint16_t target2);
int16_t paifight_TargetHasAttackCapacity(uint16_t targetObjIdx, uint16_t candidateCount);
int16_t paifight_OrderSlotCanTarget(uint16_t orderSlot);
int16_t paifight_OrderSlotHasRemainingTargets(uint16_t orderSlot);
int16_t paifight_CountRemainingOrderTargetsFromOrderSlot(uint16_t orderSlot);
int16_t paifight_CountRemainingOrderTargets(int16_t target1Type, uint16_t target1, int16_t targetRelationOp,
											int16_t target2Type, uint16_t target2);
int16_t paifight_escorttargetorder(void);
int16_t paifight_fightershootorder(void);
uint16_t paifight_SelectTargetComponentMesh(uint16_t targetObjIdx);
int16_t paifight_missiledefenseorder(void);
int16_t paifight_gunnerselfdefenseorder(void);
int16_t paifight_gunneroffenseorder(void);
int16_t paifight_FindNearestGunnerTargetInCandidateSet(int16_t target1Type, uint16_t target1,
													   int16_t target1OrTarget2, int16_t target2Type,
													   uint16_t target2, int candidateSetIdx);
void paifight_BuildGunnerTargetCandidateSet(int16_t target1Type, uint16_t target1, int16_t target1OrTarget2,
											int16_t target2Type, uint16_t target2, uint16_t candidateSetIdx);
int16_t paifight_FindNearestMatchingTargetFromOrigin(int16_t target1Type, uint16_t target1,
													 int16_t target1OrTarget2, int16_t target2Type,
													 uint16_t target2, int16_t requireClearSweep);
int16_t paifight_coverleaderorder(void);
int16_t paifight_followleadatkorder(void);
int16_t paifight_checkescortorder(void);
int16_t paifight_searchforclosestingroup(int16_t target1Type, uint16_t target1, int16_t target1OrTarget2,
										 int16_t target2Type, uint16_t target2);
int16_t paifight_OrderSlotHasFutureTargets(uint16_t orderSlot);
int16_t paifight_HasFutureFgTargets(int16_t target1Type, uint16_t target1, int16_t targetRelationOp,
									int16_t target2Type, uint16_t target2);

#ifdef __cplusplus
}
#endif

#endif
