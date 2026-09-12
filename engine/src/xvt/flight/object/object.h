#ifndef XVT_FLIGHT_OBJECT_OBJECT_H
#define XVT_FLIGHT_OBJECT_OBJECT_H

#include "xvt/flight/ai/pai.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct MobileObjectProximityList {
	uint8_t count;
	int score[16];
	uint16_t objIdx[16];
	int overflowScore;
};

struct MobileObject {
	uint8_t state;
	uint8_t lightIntensityScale;
	int32_t simStateTimestamp;
	int prevWorldX;
	int prevWorldY;
	int prevWorldZ;
	MobileObjectProximityList proximityList;
	int16_t rollImpulseRate;
	uint16_t speed;
	uint16_t speedRemainder;
	unsigned int damageAmount;
	uint16_t lifetimeTimer;
	uint16_t framesAlive;
	uint16_t sourceObjIdx;
	uint8_t sourceObjectType;
	uint8_t iff;
	uint8_t team;
	uint8_t nodeSwitchIndex;
	uint8_t moveVectorDirty;
	int16_t moveX;
	int16_t moveY;
	int16_t moveZ;
	uint8_t orientMatrixDirty;
	int16_t cachedFwdX;
	int16_t cachedFwdY;
	int16_t cachedFwdZ;
	int16_t cachedSideX;
	int16_t cachedSideY;
	int16_t cachedSideZ;
	int16_t cachedUpX;
	int16_t cachedUpY;
	int16_t cachedUpZ;
	WarheadGuidanceState* pWarheadGuidance;
	CraftData* pCraft;
	MobileObjectCharData* pCharData;
};

struct TurretTargetState {
	uint16_t targetObjIdx;
	int16_t retargetCooldownTimer;
};

struct MobileObjectCharData {
	uint16_t skillValue;
	uint8_t reserved02[2];
	AiController aiController;
	uint8_t reserved40[12];
};

struct ObjectRecord {
	uint16_t objectSignature;
	uint8_t genusId;
	uint8_t objectType;
	int world_x;
	int world_y;
	int world_z;
	uint16_t yaw;
	uint16_t pitch;
	uint16_t roll;
	uint8_t flightGroupIdx;
	uint16_t typeSpecificWord;
	uint8_t typeSpecificByte[2];
	int playerOwnerIdx;
	struct MobileObject* mobj;
};

struct ObjectSlotRange {
	uint16_t start;
	uint16_t end;
};

struct MobileObjectLinkIndices {
	int warheadGuidanceIdx;
	int craftDataIdx;
	int charDataIdx;
};

extern MobileObjectLinkIndices g_mobileObjectLinkIndices[488];
extern int g_spawnObjectTypeByObjectSlot[488];
extern MobileObject* g_mobileObjectPoolBase;
extern MobileObjectCharData* g_mobileObjectCharDataPool;
extern WarheadGuidanceState* g_projectileGuidanceStates;
extern int g_activeRegionCraftObjectSlotEnd;
extern int g_mobileObjectCharDataCount;
extern int g_mobileObjectCharDataSlotStart;
extern int g_mobileObjectCharDataSlotEnd;
extern unsigned int g_debrisObjectSlotsTotal;
extern int g_debrisObjectSlotStart;
extern int g_debrisObjectSlotEnd;
extern int g_regionMainObjectSlotStart;
extern int g_projectileObjectSlotStart;
extern int g_regionMainObjectSlotEnd;
extern int g_regionStaticObjectSlotCount;
extern int g_projectileObjectSlotEnd;
extern unsigned int g_explosionObjectSlotEnd;
extern int g_explosionObjectSlotStart;
extern unsigned int g_projectileObjectSlotsTotal;
extern int g_activeRegionObjectSlotStart;
extern ObjectSlotRange g_objectSlotRangeByGenus[20];
extern ObjectRecord* g_objectTable;

void Object_UpdateLifetimeAndMovement(void);
int Object_AddTrigMoveDeltaAndClampWorldPosition(uint32_t* mobileObject);
void RenderNonCraftSceneObject(uint16_t objectIndex);
uint16_t Object_SpawnDetachedComponent(uint16_t sourceObjectIndex, int16_t meshIndex);
uint16_t Object_SpawnEffectFragment(uint16_t sourceObjIdx);
uint16_t Object_SpawnLocalEffectFragment(uint16_t sourceObjIdx);
uint16_t Object_AllocSlotForGenus(uint16_t genusId);
uint16_t Object_FindFreeMissionSlot(void);
void Object_CopyStatePreservingStorage(unsigned int dstObjIdx, unsigned int srcObjIdx);
void Object_RelinkMobileObjectPointers(void);
unsigned int Object_DirectionAndDistanceToMeshCenter(uint16_t fromObjIdx, uint16_t targetObjIdx,
													 unsigned int meshIdx);
uint8_t Object_HasActiveDecoyBeam(uint16_t objIdx);

#ifdef __cplusplus
}
#endif

#endif
