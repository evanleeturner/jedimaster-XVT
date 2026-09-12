#ifndef XVT_FLIGHT_CRAFT_H
#define XVT_FLIGHT_CRAFT_H

#include "xvt/assets/object_type.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stored as uint8_t in the binary (IDB enum PowerRechargeLevel). */
typedef uint8_t PowerRechargeLevel;

enum {
	POWER_RECHARGE_FULLY_REDIRECTED_TO_ENGINES = 0x0,
	POWER_RECHARGE_PARTIALLY_REDIRECTED_TO_ENGINES = 0x1,
	POWER_RECHARGE_MAINTENANCE = 0x2,
	POWER_RECHARGE_INCREASED = 0x3,
	POWER_RECHARGE_MAXIMUM = 0x4,
	POWER_RECHARGE_LEVEL_COUNT = 0x5,
};

extern int g_craftDataPoolCapacity;
extern CraftData* g_craftDataPoolBase;

/* Stored as uint8_t in the binary (IDB enum ShieldDistributionMode). */
typedef uint8_t ShieldDistributionMode;

enum {
	SHIELD_DISTRIBUTION_FULLY_FORWARD = 0x0,
	SHIELD_DISTRIBUTION_EVEN = 0x1,
	SHIELD_DISTRIBUTION_FULLY_AFT = 0x2,
};

/* Stored as int8_t in the binary (IDB enum BeamType). */
typedef int8_t BeamType;

enum {
	BEAM_TYPE_NONE = 0x0,
	BEAM_TYPE_TRACTOR = 0x1,
	BEAM_TYPE_JAMMING = 0x2,
	BEAM_TYPE_DECOY = 0x3,
	BEAM_TYPE_ENERGY_TRANSFER = 0x4,
	BEAM_TYPE_FUTURE_1 = 0x5,
	BEAM_TYPE_FUTURE_2 = 0x6,
};

/* Stored as int8_t in the binary (IDB enum CountermeasureType). */
typedef int8_t CountermeasureType;

enum {
	COUNTERMEASURE_TYPE_NONE = 0x0,
	COUNTERMEASURE_TYPE_CHAFF = 0x1,
	COUNTERMEASURE_TYPE_FLARE = 0x2,
	COUNTERMEASURE_TYPE_CLUSTER_MINE = 0x3,
	COUNTERMEASURE_TYPE_FUTURE = 0x4,
};

/* Stored as uint8_t in the binary. */
typedef uint8_t CraftObjectKind;

enum {
	CRAFT_OBJECT_KIND_ACTIVE = 0x0,
	CRAFT_OBJECT_KIND_UNKNOWN_1 = 0x1,
	CRAFT_OBJECT_KIND_DISABLED = 0x2,
	CRAFT_OBJECT_KIND_BREAKING_UP = 0x3,
	CRAFT_OBJECT_KIND_EXPLODING = 0x4,
	CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE = 0x5,
	CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE = 0x6,
};

/* Stored as a uint16_t bitmask in CraftData::systemFlags and workingSubsystems. */
typedef uint16_t CraftSubsystemFlag;

enum {
	CRAFT_SUBSYSTEM_FLAG_ENGINES = 0x0040,
	CRAFT_SUBSYSTEM_FLAG_FLIGHT_CONTROLS = 0x0020,
	CRAFT_SUBSYSTEM_FLAG_SHIELDS = 0x0001,
	CRAFT_SUBSYSTEM_FLAG_CANNONS = 0x0010,
	CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER = 0x0004,
	CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER = 0x0008,
	CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM = 0x0100,
	CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS = 0x0200,
	CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES = 0x0002,
	CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE = 0x0080,
	CRAFT_SUBSYSTEM_FLAGS_ALL = 0x03FF,
	CRAFT_SUBSYSTEM_COUNT = 10,
};

struct CraftWeaponStats {
	uint16_t laserShotsFired;
	uint16_t laserHitsScored;
	uint16_t ionShotsFired;
	uint16_t ionHitsScored;
	uint8_t warheadsFired;
	uint8_t warheadHitsScored;
};

struct CraftWeaponSlot {
	uint8_t projectileTypeId;
	int8_t laserCharge;
	uint8_t count;
	uint8_t missileDefenseCooldown;
};

struct CraftData {
	int craftIndexInGroup;
	uint8_t modelIndex;
	uint8_t leader_obj_idx;
	uint8_t field_006;
	CraftObjectKind objectKind;
	uint8_t missionAccountingDone;
	uint16_t aiSkill;
	uint8_t field_00B[2];
	uint16_t pitch;
	uint16_t yaw;
	int16_t breakupPitchRate;
	int16_t breakupYawRate;
	int beamEffectAccum[5];
	uint8_t sFoilState;
	AiController aiController;
	uint16_t carriedObjectIndex;
	uint16_t carrierObjIdx;
	uint16_t lastAttackerObjIdx;
	uint16_t lastHitTimestamp;
	AiFlightState aiFlight;
	uint8_t waveNumber;
	int pushAccumX;
	int pushAccumY;
	int pushAccumZ;
	uint16_t throttleSpeed;
	uint16_t engineOutputScale;
	int16_t commandedSpeed;
	unsigned int hullDamage;
	unsigned int systemDamageHullThreshold;
	unsigned int hullMax;
	int16_t subsystemDamage;
	CraftDamageStats damageStats;
	CraftSubsystemFlag systemFlags;
	CraftSubsystemFlag workingSubsystems;
	int16_t weaponFireInhibitTimer;
	uint8_t unusedMissionFlag;
	uint8_t notDisabledAccountingSuppress;
	uint8_t wasCaptured;
	int8_t attackedByTeam[10];
	uint8_t iffVisibility[10];
	uint8_t boardingState;
	char specialCargoName[16];
	int shieldEnergy[2]; ///< Front and rear shield banks.
	PowerRechargeLevel shieldRedirect;
	ShieldDistributionMode shieldDistribMode;
	uint8_t cannonClassCount;
	PowerRechargeLevel laserRedirect;
	uint8_t laserSlotCount;
	CraftLaserState laserState;
	uint8_t warheadLauncherCount;
	uint8_t warheadSlotTypeIds[2];
	int8_t warheadLauncherFlags[2];
	int16_t warheadLauncherCooldownTicks[2];
	int16_t warheadLockTicks;
	BeamType beamTypeId;
	PowerRechargeLevel beamLevel;
	uint16_t beamPresent;
	uint8_t beamActive;
	int16_t beamTimer;
	int16_t beamTargetObjIdx;
	CountermeasureType cmTypeId;
	uint8_t cmAmmoCount;
	uint16_t chaffActiveTimer;
	uint16_t cmFireCooldownTimer;
	CraftWeaponStats weaponStats;
	uint8_t field_256[73];
	uint16_t field_29F;
	uint8_t systemDisplaySlotBySystem[DAMAGE_SYSTEM_ID_COUNT];
	uint16_t systemHealth[DAMAGE_SYSTEM_ID_COUNT];
	uint16_t systemTimer[DAMAGE_SYSTEM_ID_COUNT];
	uint8_t componentState[50];
	uint8_t meshRotation[50];
	uint8_t componentHp[50];
	uint16_t playerCommandAvoidTargetObjIdx;
	CraftWeaponSlot weaponSlots[16];
	uint16_t effectiveAiObjectSignature;
	TurretTargetState turretTargetStates[16];
	uint8_t field_3F2[44];
	struct ObjectRecord* turretObjectLinks[16];
	struct ObjectRecord* effectiveAiObjectLink;
};

struct CraftTechStats {
	int craftType; ///< CraftSpecies selected by the Tech Library. specdesc.txt uses craftType-1;
				   ///< BuildCraftTechStats also uses this value directly as the parallel object/model slot.
	CraftGenus genusId; ///< CraftGenus copied from g_modelTypeTable[craftType].
	int speedRating;
	int accelerationRating;
	int maneuverRating;
	int laserCount;
	int ionCount;
	int warheadRating;
	int shieldRating;
	int hullRating;
	int sizeRating;
};

extern CraftData* g_curCraft;

void Craft_AdjustCurrentShieldEnergy(unsigned int unusedObjectIdx, uint16_t shieldIndex, int16_t delta);
int Craft_GetObjectMaxShield(uint16_t objIdx);
ModelIndex GetModelIndexFromType(ObjectTypeId objectType);
int BuildCraftTechStats(CraftTechStats* stats);
void Craft_ClearTurretObjectLinks(CraftData* craft);
void Craft_ClearEffectiveAiObjectLink(CraftData* craft);
void Craft_DetachDamageableComponent(uint16_t objectIndex, int16_t detachAll);
WarheadKindIndex ObjectType_GetWarheadKindIndex(uint16_t objectType);
int Craft_IsSelectableDamageComponentMesh(int objectType, int meshIndex);
int Craft_DamageComponent(uint16_t victimObjIdx, int16_t hitMeshIndex, unsigned int damageAmount,
						  uint16_t sourceObjIdx);
void Craft_SpawnMainHullExplosionEffects(uint16_t objectIdx, int16_t forceMainExplosion);
int Craft_SpawnExplosionObjectAtMesh(ObjectRecord* objRecord, uint16_t meshIndex, int effectSize,
									 uint16_t useRandomVertex);

#ifdef __cplusplus
}
#endif

#endif
