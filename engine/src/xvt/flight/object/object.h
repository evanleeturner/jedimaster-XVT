#ifndef XVT_FLIGHT_OBJECT_OBJECT_H
#define XVT_FLIGHT_OBJECT_OBJECT_H

#include "xvt/flight/ai/pai.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The objects near one moving object that collide_collisions tests it
 * against, up to 16; collide_InsertMobileObjectProximityCandidate places
 * each entry in rising order of contactTicks. */
struct MobileObjectProximityList {
	uint8_t count; /* Entries in use, 0 to 16. */
	/* Per entry, ticks left before collide_collisions tests the pair again.
	 * collide_InsertMobileObjectProximityCandidate sets it from the gap
	 * between the two objects' bounds over their combined speeds (0 when
	 * the bounds overlap). */
	int contactTicks[16];
	uint16_t objIdx[16]; /* Per entry, the nearby object's slot. */
	/* Ticks left before collide_collisions rebuilds the list: 0x7FFF after
	 * a rebuild, lowered to the contactTicks of a candidate a full list
	 * drops, and 0 to rebuild on the next pass. */
	int rebuildTicks;
};

/* The moving part of an object: one per slot below g_regionMainObjectSlotEnd,
 * in g_mobileObjectPoolBase, reached through ObjectRecord.mobj. */
struct MobileObject {
	/* Object family, a CraftFamily value: 0 space craft, 1 weapon, 3
	 * debris, 5 explosion, and so on. Spawn copies it from the object type
	 * table; the code that makes shots, debris and effects sets 1, 3 or
	 * 5. */
	uint8_t family;
	/* Size of an explosion or impact effect, 0 for the default: scales the
	 * effect's billboard, and from 4 up brightens its point light.
	 * Object_UpdateLifetimeAndMovement sets maxBoundsExtent >> 9 for the
	 * final explosion of a craft other than a starfighter that has no
	 * fuselage mesh. */
	uint8_t effectSize;
	/* Game time, in ticks, this object has been simulated to; 0 means it
	 * moves with the shared clock. When set,
	 * Object_UpdateLifetimeAndMovement steps the object from here to
	 * g_gameTime + g_elapsedTicks and advances it. A shot from a player's
	 * craft starts at that player's lockstepTimestamp. */
	int32_t simStateTimestamp;
	int prevWorldX; /* world_x before the latest move. */
	int prevWorldY; /* world_y before the latest move. */
	/* world_z before the latest move. Collision tests sweep each object
	 * from prevWorldX, prevWorldY, prevWorldZ to its current position. */
	int prevWorldZ;
	/* Objects near this one that collide_collisions tests it against. */
	MobileObjectProximityList proximityList;
	/* Spin about the roll axis after an impact or a breakup.
	 * Object_UpdateLifetimeAndMovement turns roll by 4 times this per
	 * simulated second and, for a craft whose aiFlight.impactObjIdx is
	 * set, moves it toward 0 by 4,096 per simulated second, clearing
	 * impactObjIdx when it gets there. */
	int16_t rollImpulseRate;
	/* Speed in the game's units: Object_UpdateLifetimeAndMovement moves the
	 * object (4,660 times speed + 128) / 256 world units per simulated
	 * second along moveX, moveY, moveZ. Flight_AccelerateObjectSpeed caps
	 * it at 3,600. */
	uint16_t speed;
	/* Fraction of a speed unit, in 65,536ths, that
	 * Flight_AccelerateObjectSpeed and Flight_DecelerateObjectSpeed carry
	 * from step to step. */
	uint16_t speedRemainder;
	/* Damage this object deals when it hits. A craft gets its type's
	 * maxBoundsExtent, times 8 for a Container Class H, then times 4 when
	 * under 0x2000; a shot fired by a craft gets its type's damage plus the
	 * craft's speed, never less than the type's damage, and a shot from a
	 * turret, mine or static object the type's damage alone. */
	unsigned int damageAmount;
	/* Ticks left before the object expires; 0 means no limit. At 0,
	 * Object_UpdateLifetimeAndMovement explodes a craft, warhead or small
	 * debris and removes a laser shot or anything else. */
	uint16_t lifetimeTimer;
	/* Simulated seconds since the object was made or turned into an
	 * effect: Flight_UpdateTimers adds 1 every SIMULATION_TICKS_PER_SECOND
	 * ticks. Shots start at 1. collide_collisions skips a pair, shots
	 * aside, while either object is younger than 3 seconds, and offers a
	 * player the hangar only after 45. */
	uint16_t secondsAlive;
	/* Slot of the object that made this one: the craft, turret or mine
	 * that fired a shot; a craft's own slot. Object_AllocSlotForGenus sets
	 * 0. */
	uint16_t sourceObjIdx;
	/* Object type of that source; a detached component keeps its craft's
	 * type here, which picks the model to draw. Mine shots set 0. */
	uint8_t sourceObjectType;
	/* IFF code: 0 rebel, 1 and 4 imperial, 2 blue (the map's colors). Spawn
	 * sets it from the flight group and shots fired by craft copy their
	 * source's; Mission_Init sets 0xFF in the craft slots. */
	uint8_t iff;
	/* Team, 0 to 9, set at spawn and on capture
	 * (paiman_TransferObjectToAiTeam); countermeasure shots copy their
	 * owner's. */
	uint8_t team;
	/* Variant of the model's switchable nodes to draw; spawn sets the
	 * flight group's markings. */
	uint8_t nodeSwitchIndex;
	/* 1 while moveX, moveY, moveZ must be recomputed from pitch and yaw;
	 * FVIEW_calcrotatemove clears it. */
	uint8_t moveVectorDirty;
	int16_t moveX; /* Unit direction of travel, X, in Q15 (32,768 is 1). */
	int16_t moveY; /* Unit direction of travel, Y, in Q15. */
	int16_t moveZ; /* Unit direction of travel, Z, in Q15. */
	/* 1 while the cached axes below must be recomputed from the angles;
	 * FVIEW_calcrotateorient clears it. */
	uint8_t orientMatrixDirty;
	int16_t cachedFwdX;  /* Forward axis with roll, X, in Q15. */
	int16_t cachedFwdY;  /* Forward axis with roll, Y, in Q15. */
	int16_t cachedFwdZ;  /* Forward axis with roll, Z, in Q15. */
	int16_t cachedSideX; /* Side axis with roll, X, in Q15. */
	int16_t cachedSideY; /* Side axis with roll, Y, in Q15. */
	int16_t cachedSideZ; /* Side axis with roll, Z, in Q15. */
	int16_t cachedUpX;   /* Up axis with roll, X, in Q15. */
	int16_t cachedUpY;   /* Up axis with roll, Y, in Q15. */
	int16_t cachedUpZ;   /* Up axis with roll, Z, in Q15. */
	/* A shot's guidance record in g_projectileGuidanceStates, set when the
	 * shot is made; Mission_Init sets NULL, and an effect left in a shot
	 * slot keeps the pointer. */
	WarheadGuidanceState *pWarheadGuidance;
	/* The craft record in g_craftDataPoolBase for an object in a craft
	 * slot; NULL in most other slots. */
	CraftData *pCraft;
	/* A record in g_mobileObjectCharDataPool. Mission_Init sets NULL, and
	 * no code points it elsewhere except to carry its own value through a
	 * world-state save and load. */
	MobileObjectCharData *pCharData;
};

/* What the gunner of one weapon slot of a craft is firing at
 * (CraftData.turretTargetStates). */
struct TurretTargetState {
	/* Object the slot's turret fires at; for a gunner slot,
	 * laser_weaponsfire calls laser_fireturretslot while it is not
	 * UINT16_MAX. */
	uint16_t targetObjIdx;
	/* Ticks before the gunner AI may pick a new target; Flight_UpdateTimers
	 * counts it down. */
	int16_t retargetCooldownTimer;
};

/* A character record, an entry of g_mobileObjectCharDataPool. */
struct MobileObjectCharData {
	/* AI skill, which pai_GetEffectiveSkillValue reads through a craft's
	 * effectiveAiObjectLink; no code sets it other than by clearing or
	 * copying the whole record. */
	uint16_t skillValue;
	/* Never read or written by name, save in the modern build's snapshot
	 * field table, which copies it. */
	uint8_t unused02[2];
	/* AI state; Flight_UpdateTimers counts down its think and maneuver
	 * timers. */
	AiController aiController;
	/* Never read or written by name, save in the modern build's snapshot
	 * field table, which copies it. */
	uint8_t unused40[12];
};

/* One slot of g_objectTable. Slots below g_regionMainObjectSlotEnd hold
 * craft, shots, debris and effects, each with a MobileObject; the
 * g_regionStaticObjectSlotCount slots after them hold the mission's static
 * objects. */
struct ObjectRecord {
	/* Stamp from g_nextObjectSignature given at spawn; code that keeps a
	 * slot number compares it to tell whether the slot still holds the
	 * same object. */
	uint16_t objectSignature;
	uint8_t genusId;    /* Genus, a CRAFT_GENUS_* value. */
	uint8_t objectType; /* Object type; 0 marks a free slot. */
	/* World position, X, in world units;
	 * Object_AddTrigMoveDeltaAndClampWorldPosition keeps each axis within
	 * 0x1000000 either way of 0. */
	int world_x;
	int world_y;	/* World position, Y. */
	int world_z;	/* World position, Z; the map grid lies at -65,536. */
	uint16_t yaw;	/* Heading, in 65,536ths of a full turn. */
	uint16_t pitch; /* Angle in 65,536ths of a full turn. */
	uint16_t roll;	/* Angle in 65,536ths of a full turn. */
	uint8_t flightGroupIdx; /* Mission flight group of the object. */
	/* For a static object, its working-systems word: 1,023 when placed,
	 * set to 0 when an ion shot disables it (static_ApplyStaticHit). A mine
	 * fires only while it is not 0. */
	uint16_t typeSpecificWord;
	/* [0]: step in the type's texture frame sequence, which picks what is
	 * drawn; twice the mesh index for a detached component; 2 for impact
	 * effects. [1]: a mine's fire countdown in 2-tick steps
	 * (laser_UpdateMineWeaponFire), or a detached component's breakup
	 * frame step. */
	uint8_t typeSpecificByte[2];
	/* Player, 0 to 7, flying this craft; -1 for none. */
	int playerOwnerIdx;
	/* The object's MobileObject in g_mobileObjectPoolBase; NULL in the
	 * static slots. */
	struct MobileObject *mobj;
};

/* A run of slots in g_objectTable. */
struct ObjectSlotRange {
	uint16_t start; /* First slot. */
	uint16_t end;	/* One past the last slot. */
};

/* Pool entries Object_RelinkMobileObjectPointers would link to one slot's
 * MobileObject; -1 for none. */
struct MobileObjectLinkIndices {
	/* Index in g_projectileGuidanceStates; always -1 (only Mission_Init
	 * writes it). */
	int warheadGuidanceIdx;
	/* Index in g_craftDataPoolBase; always -1 (only Mission_Init writes
	 * it). */
	int craftDataIdx;
	/* Index in g_mobileObjectCharDataPool; always -1 (only Mission_Init
	 * writes it). */
	int charDataIdx;
};

extern MobileObjectLinkIndices g_mobileObjectLinkIndices[488];
extern int g_spawnObjectTypeByObjectSlot[488];
extern MobileObject *g_mobileObjectPoolBase;
extern MobileObjectCharData *g_mobileObjectCharDataPool;
extern WarheadGuidanceState *g_projectileGuidanceStates;
extern int g_activeRegionCraftObjectSlotEnd;
extern int g_mobileObjectCharDataCount;
extern int g_mobileObjectCharDataSlotStart;
extern int g_mobileObjectCharDataSlotEnd;
extern unsigned int g_debrisObjectSlotsTotal;
extern int g_debrisObjectSlotStart;
extern int g_debrisObjectSlotEnd;
extern int g_localDebrisSlotCount;
extern int g_projectileObjectSlotStart;
extern int g_regionMainObjectSlotEnd;
extern int g_regionStaticObjectSlotCount;
extern int g_projectileObjectSlotEnd;
extern unsigned int g_explosionObjectSlotEnd;
extern int g_explosionObjectSlotStart;
extern unsigned int g_projectileObjectSlotsTotal;
extern int g_activeRegionObjectSlotStart;
extern ObjectSlotRange g_objectSlotRangeByGenus[20];
extern ObjectRecord *g_objectTable;

void Object_UpdateLifetimeAndMovement(void);
int Object_AddTrigMoveDeltaAndClampWorldPosition(uint32_t *objectWords);
void RenderNonCraftSceneObject(uint16_t objectIndex);
uint16_t Object_SpawnDetachedComponent(uint16_t sourceObjectIndex,
				       int16_t meshIndex);
uint16_t Object_SpawnEffectFragment(uint16_t sourceObjIdx);
uint16_t Object_SpawnLocalEffectFragment(uint16_t sourceObjIdx);
uint16_t Object_AllocSlotForGenus(uint16_t genusId);
uint16_t Object_FindFreeMissionSlot(void);
void Object_CopyStatePreservingStorage(unsigned int dstObjIdx,
				       unsigned int srcObjIdx);
void Object_RelinkMobileObjectPointers(void);
unsigned int Object_DirectionAndDistanceToMeshCenter(uint16_t fromObjIdx,
						     uint16_t targetObjIdx,
						     unsigned int meshIdx);
uint8_t Object_HasActiveDecoyBeam(uint16_t objIdx);

#ifdef __cplusplus
}
#endif

#endif
