#ifndef XVT_FLIGHT_OBJECT_DAMAGE_H
#define XVT_FLIGHT_OBJECT_DAMAGE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A craft's damage tally by source, which collide_damagecraft adds to and
 * the mission scoring reads, and its HUD features (CraftData.damageStats).
 * Mission_InitFlightGroupObjectSlot resets it at spawn. */
struct CraftDamageStats {
	/* Set to 0 at spawn and by ProvingGrounds_InitCourseObjects; nothing
	 * reads it. */
	uint16_t lastSystemHitTime;
	/* All damage dealt to the craft, after the scaling by the victim's
	 * genus and before shields take their share. */
	int damageReceivedTotal;
	/* The part of damageReceivedTotal taken while a player flew it. */
	int damageReceivedByPlayerOwnedCraft;
	/* Damage from colliding with a craft, whoever flew it, or with normal
	 * debris. */
	int damageFromCollision;
	/* Damage from objects other than craft (shots, chiefly) whose source is
	 * neither a player nor a starfighter and is not in a static slot, and
	 * from engine wash. */
	int damageFromStarship;
	/* Damage from objects other than craft whose source is in a static
	 * slot (mines) with no player behind it. */
	int damageFromMine;
	/* Damage by the attacker's flight group: the craft that hit, or the
	 * firer of the shot. Spawn clears only entries 0 to 9 (TEAM_COUNT in
	 * Mission_InitFlightGroupObjectSlot). */
	int damageFromFlightGroupAmount[48];
	/* Damage by the player behind the source: the player flying a craft
	 * that hit; for a shot, the player flying its firer, or the player its
	 * guidance record names. */
	int damageFromPlayer[8];
	/* Damage from AI starfighters' shots, by the attacker's flight group
	 * AI rating (groupAI). */
	int damageFromAiSkill[6];
	/* HUD features the craft is fitted with: all at spawn, less the shield
	 * and beam features when it has none. */
	uint16_t installedHudFeatureMask;
	/* HUD features working now: the fitted set at spawn.
	 * collide_damagecraft knocks out random features on a hull hit once
	 * hullDamage has reached systemDamageHullThreshold, and a boarding
	 * repair (paiman_boardmaneuver) restores them. */
	uint16_t activeHudFeatureMask;
};

/* Stored as int16_t in the binary (IDB enum DamageSystemId). */
typedef int16_t DamageSystemId;

enum {
	DAMAGE_SYSTEM_00_ENGINES = 0x0, ///< strings.txt line 14: Engines
	DAMAGE_SYSTEM_01_FLIGHT_CONTROLS =
		0x1, ///< strings.txt line 15: Flight Controls
	DAMAGE_SYSTEM_02_SHIELD_SYSTEM =
		0x2, ///< strings.txt line 16: Shield System
	DAMAGE_SYSTEM_03_CANNON_SYSTEM =
		0x3, ///< strings.txt line 17: Cannon System
	DAMAGE_SYSTEM_04_TARGETING_COMPUTER =
		0x4, ///< strings.txt line 18: Targeting Computer
	DAMAGE_SYSTEM_05_WARHEAD_LAUNCHER =
		0x5, ///< strings.txt line 19: Warhead Launcher
	DAMAGE_SYSTEM_06_BEAM_SYSTEM =
		0x6, ///< strings.txt line 20: Beam System
	DAMAGE_SYSTEM_07_COMMUNICATIONS =
		0x7, ///< strings.txt line 21: Communications
	DAMAGE_SYSTEM_08_COUNTERMEASURES =
		0x8, ///< strings.txt line 22: Countermeasures
	DAMAGE_SYSTEM_09_HYPERDRIVE = 0x9, ///< strings.txt line 23: Hyperdrive
	DAMAGE_SYSTEM_10_DAMAGE_ASSESSMENT =
		0xA, ///< strings.txt line 24: Damage Assessment
	DAMAGE_SYSTEM_ID_COUNT = 0xB,
};

extern const char *g_strDamageSystemNames[DAMAGE_SYSTEM_ID_COUNT];
extern int16_t g_damageMfdDamagedSystemCountCached;
extern int16_t g_damageMfdRedrawAllRows;
extern int16_t g_damageMfdLastSelectedSystemId;
extern int16_t g_damageMfdCurrentSystemId;

void Damage_QueueCraftBillboards(uint16_t objectIndex);
uint16_t Damage_QueueCraftBillboardsForObjectType(unsigned int objectIndex,
						  int objectType);
int16_t Damage_DisplayMfdPage(void);
int16_t Damage_FindAdjacentDamagedSystem(int16_t currentSystemIdx,
					 int16_t directionStep);
void Damage_DrawMfdSystemStatusRow(DamageSystemId systemId, int16_t y,
				   int16_t valueX);

#ifdef __cplusplus
}
#endif

#endif
