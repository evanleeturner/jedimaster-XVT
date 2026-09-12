#ifndef XVT_FLIGHT_OBJECT_LASER_H
#define XVT_FLIGHT_OBJECT_LASER_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct WarheadGuidanceState {
	int8_t sourcePlayerIdx;
	uint8_t homingTier;
	uint16_t targetComponentIdx;
	uint16_t targetObjIdx;
	uint16_t targetSignature;
	uint16_t minSpeed;
};

struct CraftLaserState {
	uint8_t projectileTypeId[2];
	uint8_t linkMode[2];
	uint8_t burstRemaining[2];
	uint8_t nextSlot[2];
	int16_t fireCooldownTicks[2];
	int lastFireTimestamp[2];
};

/* Stored as int16_t in the binary (IDB enum WarheadKindIndex). */
typedef int16_t WarheadKindIndex;

enum {
	WARHEAD_KIND_PROTON_TORPEDO = 0x0,
	WARHEAD_KIND_CONCUSSION_MISSILE = 0x1,
	WARHEAD_KIND_ADVANCED_PROTON_TORPEDO = 0x2,
	WARHEAD_KIND_ADVANCED_CONCUSSION_MISSILE = 0x3,
	WARHEAD_KIND_SPACE_BOMB = 0x4,
	WARHEAD_KIND_HEAVY_ROCKET = 0x5,
	WARHEAD_KIND_MAGNETIC_PULSE = 0x6,
};

enum {
	PROJECTILE_OBJECT_TYPE_REBEL_LASER = 137,
	PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER = 138,
	PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER = 139,
	PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER = 140,
	PROJECTILE_OBJECT_TYPE_ION_LASER = 141,
	PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER = 142,
	WARHEAD_OBJECT_TYPE_PROTON_TORPEDO = 143,
	WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE = 144,
	PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER_2 = 145,
	PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER_2 = 146,
	PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER_2 = 147,
	WARHEAD_OBJECT_TYPE_ADVANCED_PROTON_TORPEDO = 148,
	WARHEAD_OBJECT_TYPE_ADVANCED_CONCUSSION_MISSILE = 149,
	WARHEAD_OBJECT_TYPE_SPACE_BOMB = 150,
	WARHEAD_OBJECT_TYPE_HEAVY_ROCKET = 151,
	WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE = 152,
	WARHEAD_OBJECT_TYPE_ION_PULSE = 153,
	WARHEAD_OBJECT_TYPE_LASER_3 = 154,
	COUNTERMEASURE_PROJECTILE_OBJECT_TYPE = 155,
};

enum {
	PROJECTILE_OBJECT_TYPE_FIRST = 137,
	PROJECTILE_OBJECT_TYPE_COUNT = 24,
};

struct ProjectileTypeDataTables {
	unsigned int damage[PROJECTILE_OBJECT_TYPE_COUNT];
	uint16_t speed[PROJECTILE_OBJECT_TYPE_COUNT];
	uint16_t lifetimeSeconds[PROJECTILE_OBJECT_TYPE_COUNT];
	uint16_t lifetimeFracQ16[PROJECTILE_OBJECT_TYPE_COUNT];
	int16_t launchOffset[PROJECTILE_OBJECT_TYPE_COUNT];
	uint8_t warheadClass[PROJECTILE_OBJECT_TYPE_COUNT];
	uint16_t warheadPointValue[PROJECTILE_OBJECT_TYPE_COUNT];
};

extern const struct ProjectileTypeDataTables g_projectileDamageByObjectType;
extern const uint8_t g_warheadTypeIds[11];
extern const uint16_t g_warheadAmmoCounts[12];
extern const uint8_t g_meshTypeComponentMaxHp[32];
extern const uint8_t g_platformBeamDisabledComponentIds[60];
extern int g_laserFireTimestampTrackingEnabled;

void laser_weaponsfire(void);
uint16_t laser_GetProjectileLifetimeTicks(int projectileObjectType);
void laser_fireplayerweapon(int playerIdx);
void laser_firelasersystem(int objectIndex, int laserSystemIndex);
void laser_firerocketsystem(int objectIndex, unsigned int launcherIndex);
int laser_firemissile(int objectIndex, int weaponSlotIndex, int projectileTypeId, unsigned int launcherIndex);
int laser_createprojectile(int sourceObjectIndex, int weaponSlotIndex, int projectileObjectType);
uint16_t laser_createprojectilefromstatic(uint16_t sourceObjIdx, uint16_t targetObjIdx);
int laser_createcountermeasureprojectile(unsigned int ownerObjIdx, int projectileObjectType);
void laser_warnplayer(uint16_t projectileGuidanceIdx);
void laser_UpdateMineWeaponFire(uint16_t mineObjIdx);
void laser_firewarheadlauncher(uint16_t sourceObjIdx, uint16_t weaponSlotIdx, uint16_t targetRef);

#ifdef __cplusplus
}
#endif

#endif
