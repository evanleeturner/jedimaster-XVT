#ifndef XVT_FLIGHT_OBJECT_DAMAGE_H
#define XVT_FLIGHT_OBJECT_DAMAGE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CraftDamageStats {
	uint16_t lastSystemHitTime;
	int damageReceivedTotal;
	int damageReceivedByPlayerOwnedCraft;
	int damageFromCollision;
	int damageFromStarship;
	int damageFromMine;
	int damageFromFlightGroupAmount[48];
	int damageFromPlayer[8];
	int damageFromAiSkill[6];
	uint16_t installedHudFeatureMask;
	uint16_t activeHudFeatureMask;
};

/* Stored as int16_t in the binary (IDB enum DamageSystemId). */
typedef int16_t DamageSystemId;

enum {
	DAMAGE_SYSTEM_00_ENGINES = 0x0,            ///< strings.txt line 14: Engines
	DAMAGE_SYSTEM_01_FLIGHT_CONTROLS = 0x1,    ///< strings.txt line 15: Flight Controls
	DAMAGE_SYSTEM_02_SHIELD_SYSTEM = 0x2,      ///< strings.txt line 16: Shield System
	DAMAGE_SYSTEM_03_CANNON_SYSTEM = 0x3,      ///< strings.txt line 17: Cannon System
	DAMAGE_SYSTEM_04_TARGETING_COMPUTER = 0x4, ///< strings.txt line 18: Targeting Computer
	DAMAGE_SYSTEM_05_WARHEAD_LAUNCHER = 0x5,   ///< strings.txt line 19: Warhead Launcher
	DAMAGE_SYSTEM_06_BEAM_SYSTEM = 0x6,        ///< strings.txt line 20: Beam System
	DAMAGE_SYSTEM_07_COMMUNICATIONS = 0x7,     ///< strings.txt line 21: Communications
	DAMAGE_SYSTEM_08_COUNTERMEASURES = 0x8,    ///< strings.txt line 22: Countermeasures
	DAMAGE_SYSTEM_09_HYPERDRIVE = 0x9,         ///< strings.txt line 23: Hyperdrive
	DAMAGE_SYSTEM_10_DAMAGE_ASSESSMENT = 0xA,  ///< strings.txt line 24: Damage Assessment
	DAMAGE_SYSTEM_ID_COUNT = 0xB,
};

extern const char* g_strDamageSystemNames[DAMAGE_SYSTEM_ID_COUNT];
extern int16_t g_damageMfdDamagedSystemCountCached;
extern int16_t g_damageMfdRedrawAllRows;
extern int16_t g_damageMfdLastSelectedSystemId;
extern int16_t g_damageMfdCurrentSystemId;

void Damage_QueueCraftBillboards(uint16_t objectIndex);
uint16_t Damage_QueueCraftBillboardsForObjectType(unsigned int objectIndex, int objectType);
int16_t Damage_DisplayMfdPage(void);
int16_t Damage_FindAdjacentDamagedSystem(int16_t currentSystemIdx, int16_t directionStep);
void Damage_DrawMfdSystemStatusRow(DamageSystemId systemId, int16_t y, int16_t valueX);

#ifdef __cplusplus
}
#endif

#endif
