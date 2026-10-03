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
extern CraftData *g_craftDataPoolBase;

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
	/* Laser shots fired; laser_firelasersystem adds them, spawn zeroes it.
	 * Only the modern build's snapshot copy reads it. */
	uint16_t laserShotsFired;
	/* Laser hits scored, added by Mission_RecordProjectileHitStats; zeroed
	 * at spawn and read only by the modern build's snapshot copy. */
	uint16_t laserHitsScored;
	/* Ion shots fired; kept like laserShotsFired. */
	uint16_t ionShotsFired;
	/* Ion hits scored; kept like laserHitsScored. */
	uint16_t ionHitsScored;
	/* Warheads fired, added by laser_firemissile; kept like
	 * laserShotsFired. */
	uint8_t warheadsFired;
	/* Warhead hits scored; kept like laserHitsScored. */
	uint8_t warheadHitsScored;
};

struct CraftWeaponSlot {
	/* Projectile object type the slot fires, set at spawn: its laser
	 * group's type, 2 for a turret mount (laserGroupMountType 2), or its
	 * launcher's warhead. */
	uint8_t projectileTypeId;
	/* A gun's charge, 127 (LASER_CHARGE_FULL) at spawn, moved each
	 * simulated second by laserRechargeLevel and spent by firing; a turret
	 * slot keeps its refire countdown in the low 7 bits. */
	int8_t laserCharge;
	/* Warheads left in a launcher slot, set at spawn from the launcher's
	 * capacity and the flight group's warhead setting, one taken per
	 * launch; laser_fireturretslot reads it in a turret slot as an ion
	 * flag. */
	uint8_t ammoCount;
	/* Missile defense order updates before this turret slot picks another
	 * missile; paifight_missiledefenseorder sets 20 and takes 1 an
	 * update. */
	uint8_t missileDefenseCooldown;
};

struct CraftData {
	/* Number shown after the flight group's name: one more than the group's
	 * craft spawned before it, or the running craft count of its global
	 * unit. */
	int craftIndexInGroup;
	/* Index into g_modelDefs, set at spawn from the object type. */
	uint8_t modelIndex;
	/* Object slot of the craft this one follows, 255 for a leader. At spawn
	 * the first craft of each batch a flight group sends leads the rest;
	 * the AI order code names a new leader when the old one is lost. */
	uint8_t leader_obj_idx;
	/* No game code reads or writes it; only the modern build's snapshot
	 * copies it. */
	uint8_t field_006;
	/* The craft's state, a CraftObjectKind: active, disabled, breaking up,
	 * exploding, or entering or leaving hyperspace. Set at spawn; many
	 * functions change it, chiefly in the collision and AI code. */
	CraftObjectKind objectKind;
	/* 1 once Mission_RecordCraftOutcome has counted this craft's outcome,
	 * so it counts only once; 0 at spawn. */
	uint8_t missionAccountingDone;
	/* AI skill as a fraction of 65,536, from g_aiSkillValueQ16ByLevel by
	 * the flight group's AI level at spawn; the AI reads it through
	 * pai_GetEffectiveSkillValue. */
	uint16_t aiSkill;
	/* No game code reads or writes it; only the modern build's snapshot
	 * copies it. */
	uint8_t field_00B[2];
	/* Pitch the steering code gives the object while the craft is active,
	 * 65,536 units a circle; player input (USER_calcdeltapitch) and the AI
	 * turn it. */
	uint16_t pitch;
	/* The object's yaw, copied at spawn and after each steering update; no
	 * game code reads it, only the modern build's snapshot copies it. */
	uint16_t yaw;
	/* No game code reads or writes it; only the modern build's snapshot
	 * copies it. */
	int16_t breakupPitchRate;
	/* No game code reads or writes it; only the modern build's snapshot
	 * copies it. */
	int16_t breakupYawRate;
	/* Per BeamType, beam output aimed at this craft: laser_weaponsfire
	 * clears it each call, then adds the beamOutput of each tractor (1) or
	 * jamming (2) beam on it. A tractor entry, without active chaff, stops
	 * the craft turning; a jamming entry slows or blocks its guns.
	 * collide_ApplyHostileProximityWeaponDisruption sets the jamming entry
	 * to 163840. */
	int beamEffectAccum[5];
	/* S-foil bits: 1 while the foils move, 2 when closed or closing; 0
	 * open. A key or the hyperspace command starts a move;
	 * FlightObject_UpdateSpecialBehavior moves the meshes and ends it.
	 * Lasers do not fire while it is nonzero. */
	uint8_t sFoilState;
	/* The AI's orders, plans, target and maneuver state. */
	AiController aiController;
	/* Object slot of the craft this one picked up (boardtopickuppln) and
	 * moves along with itself; UINT16_MAX for none. */
	uint16_t carriedObjectIndex;
	/* Object slot of the craft carrying this one, UINT16_MAX for none; a
	 * carried craft stays out of the collision proximity lists. */
	uint16_t carrierObjIdx;
	/* Object slot of the attacker the craft answers, UINT16_MAX for none.
	 * collide.c records a hitting craft when none is held (never a
	 * starship, platform or freighter) and, on a player's craft, every
	 * hitter; the AI also sets and clears it, and each new plan clears
	 * it. */
	uint16_t lastAttackerObjIdx;
	/* Mission second, by Mission_ClockToSeconds, when lastAttackerObjIdx
	 * was last recorded; 0 with each new plan. */
	uint16_t lastHitMissionSecond;
	/* Steering, formation and order bookkeeping the AI and steering code
	 * keep. */
	AiFlightState aiFlight;
	/* The craft's place in its flight group from 0 (g_spawnCraftOrdinal),
	 * used for its formation slot, special cargo and radio voice. */
	uint8_t craftOrdinal;
	/* Distance still to push the craft along X: the movement code adds at
	 * most the model's maxPushRate per SIMULATION_TICKS_PER_SECOND ticks
	 * and takes off what moved. The AI sets it; 0 at spawn. */
	int pushAccumX;
	/* Distance still to push the craft along Y; kept like pushAccumX. */
	int pushAccumY;
	/* Distance still to push the craft along Z; kept like pushAccumX. */
	int pushAccumZ;
	/* Throttle as a fraction of 65,536, 0 stopped and 0xFFFF full; the HUD
	 * shows it as a percent. Player keys, the AI and spawn set it. */
	uint16_t throttleSpeed;
	/* 0 while the engine overdrive boosters are on, doubling the speed the
	 * steering aims for and draining laser charge; 0xFFFF off. Only a craft
	 * with the model of specialSlamCraftType toggles them, with the N key,
	 * and they drop when no gun holds charge. */
	uint16_t engineOverdriveOff;
	/* Speed the current order asks for, 5 times the order's speed, set by
	 * paiman_initmaneuver; Flight_UpdateCraftSteeringAndSpeed aims for a
	 * nonzero value in place of the throttle when its throttle fraction is
	 * full. */
	int16_t commandedSpeed;
	/* Hull damage taken, 0 at spawn; the collision code adds to it and
	 * compares it with hullMax and systemDamageHullThreshold. */
	unsigned int hullDamage;
	/* Hull damage, from the model, at and above which collide_damagecraft
	 * turns a hit into an internal hit that can knock out HUD features; the
	 * AI also breaks off sooner past it. 0x7FFF on proving grounds course
	 * objects. */
	unsigned int systemDamageHullThreshold;
	/* Hull strength, the model's hullStrength; the collision code compares
	 * hullDamage with it. 0x7FFF on proving grounds course objects. */
	unsigned int hullMax;
	/* Ion damage against the model's systemStrength: ion hits add 1, 2 or
	 * 4. When little strength is left, hits disable subsystems; with none
	 * working it is set to systemStrength. The AI sets 0 when it restores
	 * the craft's systems. */
	int16_t subsystemDamage;
	/* Damage received, by source, and the HUD feature masks. */
	CraftDamageStats damageStats;
	/* Subsystems the craft has installed, CRAFT_SUBSYSTEM_FLAG bits; set at
	 * spawn from the model and flight group. */
	CraftSubsystemFlag systemFlags;
	/* Installed subsystems now working: systemFlags at spawn, bits cleared
	 * by damage and set again by repair. Many functions write it. */
	CraftSubsystemFlag workingSubsystems;
	/* Ticks an AI craft's guns stay silent after a magnetic pulse hit, read
	 * as unsigned and held at 0xFFFF; collide_laserhitcraft adds to it and
	 * Flight_UpdateTimers counts it down. */
	int16_t weaponFireInhibitTimer;
	/* Set to 0 at spawn; no game code reads it, only the modern build's
	 * snapshot copies it. */
	uint8_t unusedMissionFlag;
	/* While 0, Mission_RecordCraftOutcome counts the craft as not disabled;
	 * only spawn and ProvingGrounds_InitCourseObjects write it, both with
	 * 0. */
	uint8_t notDisabledAccountingSuppress;
	/* 0 unless captured; then a flag bit with the capturing flight group in
	 * the low 7 bits. Set and cleared by paiman_TransferObjectToAiTeam;
	 * cleared when the craft is destroyed. */
	uint8_t capturedByFlightGroup;
	/* Per team, nonzero once that team has hit the craft: bit 0 marks the
	 * first hit; collide.c also keeps a hit count in bits 4 to 6 and a flag
	 * in bit 7. 0 at spawn. */
	int8_t attackedByTeam[10];
	/* Per team, 0 until that team inspects the craft, then the order of
	 * that inspection among the teams, from 1; the craft's own team starts
	 * at 1. */
	uint8_t identifiedOrderByTeam[10];
	/* Outcome of the last boarding involving the craft, set by
	 * paiman_boardmaneuver: 1 its cargo was taken, 2 it got cargo, swapped
	 * it or was contacted, 3 it was repaired; 0 at spawn. */
	uint8_t boardingState;
	/* Cargo name shown on inspection: the flight group's special cargo for
	 * its special cargo craft, else its cargo; boarding moves it between
	 * craft. */
	char specialCargoName[16];
	/* Bank 0 front, 1 rear. Craft_AdjustCurrentShieldEnergy holds a bank
	 * between 0 and twice the model's shieldStrength. At spawn a player's
	 * craft gets shieldStrength in each bank, an AI craft twice it in
	 * front. */
	int shieldEnergy[2]; ///< Front and rear shield banks.
	/* Shield recharge setting, POWER_RECHARGE_MAINTENANCE at spawn. */
	PowerRechargeLevel shieldRechargeLevel;
	/* How shield energy is shared between the banks: even for a player's
	 * craft at spawn, else fully forward. */
	ShieldDistributionMode shieldDistribMode;
	/* Laser groups not of mount type 2, counted at spawn; with none the
	 * cannons leave systemFlags. */
	uint8_t cannonGroupCount;
	/* Laser recharge setting, POWER_RECHARGE_MAINTENANCE at spawn. */
	PowerRechargeLevel laserRechargeLevel;
	/* Weapon slots of the laser groups, which come first in weaponSlots;
	 * counted at spawn. */
	uint8_t laserSlotCount;
	/* Per laser group: projectile type, link mode, burst and fire
	 * timing. */
	CraftLaserState laserState;
	/* Launchers with a warhead type, 0 to 2, counted at spawn. */
	uint8_t warheadLauncherCount;
	/* Warhead object type each launcher fires, from the flight group's
	 * warhead setting; 0 for none. */
	uint8_t warheadSlotTypeIds[2];
	/* Per launcher, a mode in the low 7 bits, 1 at spawn; after a launch
	 * laser_firemissile sets bit 7 when the launcher's second slot holds
	 * more warheads than its first. */
	int8_t warheadLauncherFlags[2];
	/* Ticks before each launcher fires again: laser_firewarheadsystem adds
	 * 472 a volley and laser_weaponsfire counts it down. */
	int16_t warheadLauncherCooldownTicks[2];
	/* Ticks of warhead lock built on the player's target in
	 * laser_weaponsfire, which a target's countermeasures can take back;
	 * cleared on a new target, a new craft and each new AI maneuver. */
	int16_t warheadLockTicks;
	/* Beam the craft carries, from the flight group; none for object types
	 * 1 to 5. */
	BeamType beamTypeId;
	/* Beam recharge setting, POWER_RECHARGE_MAINTENANCE at spawn. */
	PowerRechargeLevel beamRechargeLevel;
	/* Beam energy, BEAM_CHARGE_FULL at spawn (0 without a beam): drained
	 * while the beam is on, moved by the beam recharge level each simulated
	 * second. */
	uint16_t beamCharge;
	/* 1 while the beam is on: the B key turns it on and off, and it goes
	 * off when beamCharge runs out. */
	uint8_t beamActive;
	/* -1 while the beam is on, 0 off; added as 0xFFFF to the
	 * beamEffectAccum entry of the craft it is aimed at. */
	int16_t beamOutput;
	/* Only spawn writes it, with -1; the HUD compares it. */
	int16_t beamTargetObjIdx;
	/* Countermeasure type from the flight group; none takes countermeasures
	 * out of systemFlags. */
	CountermeasureType cmTypeId;
	/* Countermeasures left: the model's count at spawn, two thirds of it
	 * for flares; each launch takes one. */
	uint8_t cmAmmoCount;
	/* Simulated seconds of chaff left: a launch adds to it and
	 * laser_weaponsfire takes 1 each simulated second. */
	uint16_t chaffActiveSeconds;
	/* Ticks before another countermeasure launches: a launch sets 472,
	 * Flight_UpdateTimers counts it down. */
	uint16_t cmFireCooldownTimer;
	/* Shots fired and hits scored by this craft. */
	CraftWeaponStats weaponStats;
	/* No game code reads or writes it; only the modern build's snapshot
	 * copies it. */
	uint8_t field_256[73];
	/* Set to 0 at spawn; no game code reads it, only the modern build's
	 * snapshot copies it. */
	uint16_t field_29F;
	/* Per subsystem, its row on the damage display, the identity at spawn;
	 * on a player's craft the knocked-out system with the lowest row is
	 * repaired first, and Damage_DisplayMfdPage lets a solo player reorder
	 * the rows. */
	uint8_t systemDisplaySlotBySystem[DAMAGE_SYSTEM_ID_COUNT];
	/* Per subsystem, 100 at spawn and after repair, 0 when knocked out. */
	uint16_t systemHealth[DAMAGE_SYSTEM_ID_COUNT];
	/* Per subsystem, simulated seconds of repair left: knocking the system
	 * out sets it from g_subsystemRepairDuration, and on a player's craft
	 * Flight_UpdateTimers takes 1 a second from the one under repair. */
	uint16_t systemRepairSeconds[DAMAGE_SYSTEM_ID_COUNT];
	/* Per mesh: 0 while in place and drawn, 2 destroyed or hidden, 4
	 * knocked off or disabled at spawn. The entry after the type's last
	 * mesh holds the fuselage damage animation step. */
	uint8_t componentState[50];
	/* Per mesh, a rotation in 256ths of a circle that the renderer turns
	 * the mesh by: turrets, S-foils, dishes and proving grounds targets. */
	uint8_t meshRotation[50];
	/* Per mesh, hit points in units of 16 damage: from
	 * g_meshTypeComponentMaxHp at spawn for a damageable mesh, 255 for one
	 * that cannot be hurt, 0 when destroyed. */
	uint8_t componentHp[50];
	/* Object a player's order (commandId 155) told this AI craft to leave
	 * alone, UINT16_MAX for none; the AI's targeting checks it. */
	uint16_t playerCommandAvoidTargetObjIdx;
	/* Weapon slots, laser groups first, then launchers, laid out by
	 * FeDiskIo_BuildModelDef. */
	CraftWeaponSlot weaponSlots[16];
	/* Signature effectiveAiObjectLink's object must still carry; only spawn
	 * writes it, with 0. */
	uint16_t effectiveAiObjectSignature;
	/* Per weapon slot, the turret's target and retarget timing. */
	TurretTargetState turretTargetStates[16];
	/* No game code reads or writes it; the world checksums subtract its
	 * size from the record bytes they sum. */
	uint8_t field_3F2[44];
	/* Objects tied to the turrets. No code points one at an object: every
	 * write clears, rebases or restores one, so they stay NULL. */
	struct ObjectRecord *turretObjectLinks[16];
	/* Object whose character data gives this craft its AI skill while its
	 * signature matches effectiveAiObjectSignature. Only a branch that
	 * never runs points it at an object, so it stays NULL. */
	struct ObjectRecord *effectiveAiObjectLink;
};

struct CraftTechStats {
	int craftType; ///< CraftSpecies selected by the Tech Library. specdesc.txt uses craftType-1;
	///< BuildCraftTechStats also uses this value directly as the parallel object/model slot.
	CraftGenus
		genusId; ///< CraftGenus copied from g_objectTypeTable[craftType].
	/* Speed rating: the model's maxSpeed times 4/9, rounded. */
	int speedRating;
	/* Acceleration rating: the model's accelRate times 4/9, rounded. */
	int accelerationRating;
	/* Maneuver rating: pitchRate plus rollRate times
	 * g_craftTechManeuverRatingScale, rounded. */
	int maneuverRating;
	/* Laser cannons, counted from the model's laser groups or a fixed
	 * figure. */
	int laserCount;
	int ionCount; /* Ion cannons, counted the same way. */
	/* Warheads: capacity times slots summed over the launchers, or a fixed
	 * figure. */
	int warheadRating;
	/* Shield rating: shieldStrength / 50, 0 without shields, scaled by
	 * genus. */
	int shieldRating;
	int hullRating; /* Hull rating: hullStrength / 105, scaled by genus. */
	int sizeRating; /* Nothing reads or writes it by name. */
};

extern CraftData *g_curCraft;

void Craft_AdjustCurrentShieldEnergy(unsigned int unusedObjectIdx,
				     uint16_t shieldIndex, int16_t delta);
int Craft_GetObjectMaxShield(uint16_t objIdx);
ModelIndex GetModelIndexFromType(ObjectTypeId objectType);
int BuildCraftTechStats(CraftTechStats *stats);
void Craft_ClearTurretObjectLinks(CraftData *craft);
void Craft_FreeLinkedObjects(CraftData *craft);
void Craft_DetachDamageableComponent(uint16_t objectIndex, int16_t detachAll);
WarheadKindIndex ObjectType_GetWarheadKindIndex(uint16_t objectType);
int Craft_IsSelectableDamageComponentMesh(int objectType, int meshIndex);
int Craft_DamageComponent(uint16_t victimObjIdx, int16_t hitMeshIndex,
			  unsigned int damageAmount, uint16_t sourceObjIdx);
void Craft_SpawnMainHullExplosionEffects(uint16_t objectIdx,
					 int16_t forceMainExplosion);
int Craft_SpawnExplosionObjectAtMesh(ObjectRecord *objRecord,
				     uint16_t meshIndex, int effectSize,
				     uint16_t useRandomVertex);

#ifdef __cplusplus
}
#endif

#endif
