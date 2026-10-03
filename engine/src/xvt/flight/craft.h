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

/* Stored as uint8_t in the binary (IDB enum power_recharge_level). */
typedef uint8_t power_recharge_level;

enum {
	POWER_RECHARGE_FULLY_REDIRECTED_TO_ENGINES = 0x0,
	POWER_RECHARGE_PARTIALLY_REDIRECTED_TO_ENGINES = 0x1,
	POWER_RECHARGE_MAINTENANCE = 0x2,
	POWER_RECHARGE_INCREASED = 0x3,
	POWER_RECHARGE_MAXIMUM = 0x4,
	POWER_RECHARGE_LEVEL_COUNT = 0x5,
};

extern int g_craft_data_pool_capacity;
extern struct craft_data *g_craft_data_pool_base;

/* Stored as uint8_t in the binary (IDB enum shield_distribution_mode). */
typedef uint8_t shield_distribution_mode;

enum {
	SHIELD_DISTRIBUTION_FULLY_FORWARD = 0x0,
	SHIELD_DISTRIBUTION_EVEN = 0x1,
	SHIELD_DISTRIBUTION_FULLY_AFT = 0x2,
};

/* Stored as int8_t in the binary (IDB enum beam_type). */
typedef int8_t beam_type;

enum {
	BEAM_TYPE_NONE = 0x0,
	BEAM_TYPE_TRACTOR = 0x1,
	BEAM_TYPE_JAMMING = 0x2,
	BEAM_TYPE_DECOY = 0x3,
	BEAM_TYPE_ENERGY_TRANSFER = 0x4,
	BEAM_TYPE_FUTURE_1 = 0x5,
	BEAM_TYPE_FUTURE_2 = 0x6,
};

/* Stored as int8_t in the binary (IDB enum countermeasure_type). */
typedef int8_t countermeasure_type;

enum {
	COUNTERMEASURE_TYPE_NONE = 0x0,
	COUNTERMEASURE_TYPE_CHAFF = 0x1,
	COUNTERMEASURE_TYPE_FLARE = 0x2,
	COUNTERMEASURE_TYPE_CLUSTER_MINE = 0x3,
	COUNTERMEASURE_TYPE_FUTURE = 0x4,
};

/* Stored as uint8_t in the binary. */
typedef uint8_t craft_object_kind;

enum {
	CRAFT_OBJECT_KIND_ACTIVE = 0x0,
	CRAFT_OBJECT_KIND_UNKNOWN_1 = 0x1,
	CRAFT_OBJECT_KIND_DISABLED = 0x2,
	CRAFT_OBJECT_KIND_BREAKING_UP = 0x3,
	CRAFT_OBJECT_KIND_EXPLODING = 0x4,
	CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE = 0x5,
	CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE = 0x6,
};

/* Stored as a uint16_t bitmask in craft_data::system_flags and working_subsystems. */
typedef uint16_t craft_subsystem_flag;

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

struct craft_weapon_stats {
	/* Laser shots fired; laser_firelasersystem adds them, spawn zeroes it.
	 * Only the modern build's snapshot copy reads it. */
	uint16_t laser_shots_fired;
	/* Laser hits scored, added by mission_record_projectile_hit_stats; zeroed
	 * at spawn and read only by the modern build's snapshot copy. */
	uint16_t laser_hits_scored;
	/* Ion shots fired; kept like laser_shots_fired. */
	uint16_t ion_shots_fired;
	/* Ion hits scored; kept like laser_hits_scored. */
	uint16_t ion_hits_scored;
	/* Warheads fired, added by laser_firemissile; kept like
	 * laser_shots_fired. */
	uint8_t warheads_fired;
	/* Warhead hits scored; kept like laser_hits_scored. */
	uint8_t warhead_hits_scored;
};

struct craft_weapon_slot {
	/* Projectile object type the slot fires, set at spawn: its laser
	 * group's type, 2 for a turret mount (laser_group_mount_type 2), or its
	 * launcher's warhead. */
	uint8_t projectile_type_id;
	/* A gun's charge, 127 (LASER_CHARGE_FULL) at spawn, moved each
	 * simulated second by laser_recharge_level and spent by firing; a turret
	 * slot keeps its refire countdown in the low 7 bits. */
	int8_t laser_charge;
	/* Warheads left in a launcher slot, set at spawn from the launcher's
	 * capacity and the flight group's warhead setting, one taken per
	 * launch; laser_fireturretslot reads it in a turret slot as an ion
	 * flag. */
	uint8_t ammo_count;
	/* Missile defense order updates before this turret slot picks another
	 * missile; paifight_missiledefenseorder sets 20 and takes 1 an
	 * update. */
	uint8_t missile_defense_cooldown;
};

struct craft_data {
	/* Number shown after the flight group's name: one more than the group's
	 * craft spawned before it, or the running craft count of its global
	 * unit. */
	int craft_index_in_group;
	/* Index into g_model_defs, set at spawn from the object type. */
	uint8_t model_index;
	/* Object slot of the craft this one follows, 255 for a leader. At spawn
	 * the first craft of each batch a flight group sends leads the rest;
	 * the AI order code names a new leader when the old one is lost. */
	uint8_t leader_obj_idx;
	/* No game code reads or writes it; only the modern build's snapshot
	 * copies it. */
	uint8_t unused006;
	/* The craft's state, a craft_object_kind: active, disabled, breaking up,
	 * exploding, or entering or leaving hyperspace. Set at spawn; many
	 * functions change it, chiefly in the collision and AI code. */
	craft_object_kind object_kind;
	/* 1 once mission_record_craft_outcome has counted this craft's outcome,
	 * so it counts only once; 0 at spawn. */
	uint8_t mission_accounting_done;
	/* AI skill as a fraction of 65,536, from g_ai_skill_value_q16_by_level by
	 * the flight group's AI level at spawn; the AI reads it through
	 * pai_get_effective_skill_value. */
	uint16_t ai_skill;
	/* No game code reads or writes it; only the modern build's snapshot
	 * copies it. */
	uint8_t unused00b[2];
	/* Pitch the steering code gives the object while the craft is active,
	 * 65,536 units a circle; player input (player_apply_pitch_yaw_steps) and
	 * the AI turn it. */
	uint16_t pitch;
	/* The object's yaw, copied at spawn and after each steering update; no
	 * game code reads it, only the modern build's snapshot copies it. */
	uint16_t yaw;
	/* No game code reads or writes it; only the modern build's snapshot
	 * copies it. */
	int16_t breakup_pitch_rate;
	/* No game code reads or writes it; only the modern build's snapshot
	 * copies it. */
	int16_t breakup_yaw_rate;
	/* Per beam_type, beam output aimed at this craft: laser_weaponsfire
	 * clears it each call, then adds the beam_output of each tractor (1) or
	 * jamming (2) beam on it. A tractor entry, without active chaff, stops
	 * the craft turning; a jamming entry slows or blocks its guns.
	 * collide_apply_hostile_proximity_weapon_disruption sets the jamming entry
	 * to 163840. */
	int beam_effect_accum[5];
	/* S-foil bits: 1 while the foils move, 2 when closed or closing; 0
	 * open. A key or the hyperspace command starts a move;
	 * flight_object_update_special_behavior moves the meshes and ends it.
	 * Lasers do not fire while it is nonzero. */
	uint8_t s_foil_state;
	/* The AI's orders, plans, target and maneuver state. */
	struct ai_controller ai_controller;
	/* Object slot of the craft this one picked up (boardtopickuppln) and
	 * moves along with itself; UINT16_MAX for none. */
	uint16_t carried_object_index;
	/* Object slot of the craft carrying this one, UINT16_MAX for none; a
	 * carried craft stays out of the collision proximity lists. */
	uint16_t carrier_obj_idx;
	/* Object slot of the attacker the craft answers, UINT16_MAX for none.
	 * collide.c records a hitting craft when none is held (never a
	 * starship, platform or freighter) and, on a player's craft, every
	 * hitter; the AI also sets and clears it, and each new plan clears
	 * it. */
	uint16_t last_attacker_obj_idx;
	/* Mission second, by mission_clock_to_seconds, when last_attacker_obj_idx
	 * was last recorded; 0 with each new plan. */
	uint16_t last_hit_mission_second;
	/* Steering, formation and order bookkeeping the AI and steering code
	 * keep. */
	struct ai_flight_state ai_flight;
	/* The craft's place in its flight group from 0 (g_spawn_craft_ordinal),
	 * used for its formation slot, special cargo and radio voice. */
	uint8_t craft_ordinal;
	/* Distance still to push the craft along X: the movement code adds at
	 * most the model's max_push_rate per SIMULATION_TICKS_PER_SECOND ticks
	 * and takes off what moved. The AI sets it; 0 at spawn. */
	int push_accum_x;
	/* Distance still to push the craft along Y; kept like push_accum_x. */
	int push_accum_y;
	/* Distance still to push the craft along Z; kept like push_accum_x. */
	int push_accum_z;
	/* Throttle as a fraction of 65,536, 0 stopped and 0xFFFF full; the HUD
	 * shows it as a percent. Player keys, the AI and spawn set it. */
	uint16_t throttle_speed;
	/* 0 while the engine overdrive boosters are on, doubling the speed the
	 * steering aims for and draining laser charge; 0xFFFF off. Only a craft
	 * with the model of special_slam_craft_type toggles them, with the N key,
	 * and they drop when no gun holds charge. */
	uint16_t engine_overdrive_off;
	/* Speed the current order asks for, 5 times the order's speed, set by
	 * paiman_initmaneuver; flight_update_craft_steering_and_speed aims for a
	 * nonzero value in place of the throttle when its throttle fraction is
	 * full. */
	int16_t commanded_speed;
	/* Hull damage taken, 0 at spawn; the collision code adds to it and
	 * compares it with hull_max and system_damage_hull_threshold. */
	unsigned int hull_damage;
	/* Hull damage, from the model, at and above which collide_damagecraft
	 * turns a hit into an internal hit that can knock out HUD features; the
	 * AI also breaks off sooner past it. 0x7FFF on proving grounds course
	 * objects. */
	unsigned int system_damage_hull_threshold;
	/* Hull strength, the model's hull_strength; the collision code compares
	 * hull_damage with it. 0x7FFF on proving grounds course objects. */
	unsigned int hull_max;
	/* Ion damage against the model's system_strength: ion hits add 1, 2 or
	 * 4. When little strength is left, hits disable subsystems; with none
	 * working it is set to system_strength. The AI sets 0 when it restores
	 * the craft's systems. */
	int16_t subsystem_damage;
	/* Damage received, by source, and the HUD feature masks. */
	struct craft_damage_stats damage_stats;
	/* Subsystems the craft has installed, CRAFT_SUBSYSTEM_FLAG bits; set at
	 * spawn from the model and flight group. */
	craft_subsystem_flag system_flags;
	/* Installed subsystems now working: system_flags at spawn, bits cleared
	 * by damage and set again by repair. Many functions write it. */
	craft_subsystem_flag working_subsystems;
	/* Ticks an AI craft's guns stay silent after a magnetic pulse hit, read
	 * as unsigned and held at 0xFFFF; collide_laserhitcraft adds to it and
	 * flight_update_timers counts it down. */
	int16_t weapon_fire_inhibit_timer;
	/* Set to 0 at spawn; no game code reads it, only the modern build's
	 * snapshot copies it. */
	uint8_t unused_mission_flag;
	/* While 0, mission_record_craft_outcome counts the craft as not disabled;
	 * only spawn and proving_grounds_init_course_objects write it, both with
	 * 0. */
	uint8_t not_disabled_accounting_suppress;
	/* 0 unless captured; then a flag bit with the capturing flight group in
	 * the low 7 bits. Set and cleared by paiman_transfer_object_to_ai_team;
	 * cleared when the craft is destroyed. */
	uint8_t captured_by_flight_group;
	/* Per team, nonzero once that team has hit the craft: bit 0 marks the
	 * first hit; collide.c also keeps a hit count in bits 4 to 6 and a flag
	 * in bit 7. 0 at spawn. */
	int8_t attacked_by_team[10];
	/* Per team, 0 until that team inspects the craft, then the order of
	 * that inspection among the teams, from 1; the craft's own team starts
	 * at 1. */
	uint8_t identified_order_by_team[10];
	/* Outcome of the last boarding involving the craft, set by
	 * paiman_boardmaneuver: 1 its cargo was taken, 2 it got cargo, swapped
	 * it or was contacted, 3 it was repaired; 0 at spawn. */
	uint8_t boarding_state;
	/* Cargo name shown on inspection: the flight group's special cargo for
	 * its special cargo craft, else its cargo; boarding moves it between
	 * craft. */
	char special_cargo_name[16];
	/* Bank 0 front, 1 rear. craft_adjust_current_shield_energy holds a bank
	 * between 0 and twice the model's shield_strength. At spawn a player's
	 * craft gets shield_strength in each bank, an AI craft twice it in
	 * front. */
	int shield_energy[2]; ///< Front and rear shield banks.
	/* Shield recharge setting, POWER_RECHARGE_MAINTENANCE at spawn. */
	power_recharge_level shield_recharge_level;
	/* How shield energy is shared between the banks: even for a player's
	 * craft at spawn, else fully forward. */
	shield_distribution_mode shield_distrib_mode;
	/* Laser groups not of mount type 2, counted at spawn; with none the
	 * cannons leave system_flags. */
	uint8_t cannon_group_count;
	/* Laser recharge setting, POWER_RECHARGE_MAINTENANCE at spawn. */
	power_recharge_level laser_recharge_level;
	/* Weapon slots of the laser groups, which come first in weapon_slots;
	 * counted at spawn. */
	uint8_t laser_slot_count;
	/* Per laser group: projectile type, link mode, burst and fire
	 * timing. */
	struct craft_laser_state laser_state;
	/* Launchers with a warhead type, 0 to 2, counted at spawn. */
	uint8_t warhead_launcher_count;
	/* Warhead object type each launcher fires, from the flight group's
	 * warhead setting; 0 for none. */
	uint8_t warhead_slot_type_ids[2];
	/* Per launcher, a mode in the low 7 bits, 1 at spawn; after a launch
	 * laser_firemissile sets bit 7 when the launcher's second slot holds
	 * more warheads than its first. */
	int8_t warhead_launcher_flags[2];
	/* Ticks before each launcher fires again: laser_firewarheadsystem adds
	 * 472 a volley and laser_weaponsfire counts it down. */
	int16_t warhead_launcher_cooldown_ticks[2];
	/* Ticks of warhead lock built on the player's target in
	 * laser_weaponsfire, which a target's countermeasures can take back;
	 * cleared on a new target, a new craft and each new AI maneuver. */
	int16_t warhead_lock_ticks;
	/* Beam the craft carries, from the flight group; none for object types
	 * 1 to 5. */
	beam_type beam_type_id;
	/* Beam recharge setting, POWER_RECHARGE_MAINTENANCE at spawn. */
	power_recharge_level beam_recharge_level;
	/* Beam energy, BEAM_CHARGE_FULL at spawn (0 without a beam): drained
	 * while the beam is on, moved by the beam recharge level each simulated
	 * second. */
	uint16_t beam_charge;
	/* 1 while the beam is on: the B key turns it on and off, and it goes
	 * off when beam_charge runs out. */
	uint8_t beam_active;
	/* -1 while the beam is on, 0 off; added as 0xFFFF to the
	 * beam_effect_accum entry of the craft it is aimed at. */
	int16_t beam_output;
	/* Only spawn writes it, with -1; the HUD compares it. */
	int16_t beam_target_obj_idx;
	/* Countermeasure type from the flight group; none takes countermeasures
	 * out of system_flags. */
	countermeasure_type cm_type_id;
	/* Countermeasures left: the model's count at spawn, two thirds of it
	 * for flares; each launch takes one. */
	uint8_t cm_ammo_count;
	/* Simulated seconds of chaff left: a launch adds to it and
	 * laser_weaponsfire takes 1 each simulated second. */
	uint16_t chaff_active_seconds;
	/* Ticks before another countermeasure launches: a launch sets 472,
	 * flight_update_timers counts it down. */
	uint16_t cm_fire_cooldown_timer;
	/* Shots fired and hits scored by this craft. */
	struct craft_weapon_stats weapon_stats;
	/* No game code reads or writes it; only the modern build's snapshot
	 * copies it. */
	uint8_t unused256[73];
	/* Set to 0 at spawn; no game code reads it, only the modern build's
	 * snapshot copies it. */
	uint16_t field_29f;
	/* Per subsystem, its row on the damage display, the identity at spawn;
	 * on a player's craft the knocked-out system with the lowest row is
	 * repaired first, and damage_display_mfd_page lets a solo player reorder
	 * the rows. */
	uint8_t system_display_slot_by_system[DAMAGE_SYSTEM_ID_COUNT];
	/* Per subsystem, 100 at spawn and after repair, 0 when knocked out. */
	uint16_t system_health[DAMAGE_SYSTEM_ID_COUNT];
	/* Per subsystem, simulated seconds of repair left: knocking the system
	 * out sets it from g_subsystem_repair_duration, and on a player's craft
	 * flight_update_timers takes 1 a second from the one under repair. */
	uint16_t system_repair_seconds[DAMAGE_SYSTEM_ID_COUNT];
	/* Per mesh: 0 while in place and drawn, 2 destroyed or hidden, 4
	 * knocked off or disabled at spawn. The entry after the type's last
	 * mesh holds the fuselage damage animation step. */
	uint8_t component_state[50];
	/* Per mesh, a rotation in 256ths of a circle that the renderer turns
	 * the mesh by: turrets, S-foils, dishes and proving grounds targets. */
	uint8_t mesh_rotation[50];
	/* Per mesh, hit points in units of 16 damage: from
	 * g_mesh_type_component_max_hp at spawn for a damageable mesh, 255 for one
	 * that cannot be hurt, 0 when destroyed. */
	uint8_t component_hp[50];
	/* Object a player's order (commandId 155) told this AI craft to leave
	 * alone, UINT16_MAX for none; the AI's targeting checks it. */
	uint16_t player_command_avoid_target_obj_idx;
	/* Weapon slots, laser groups first, then launchers, laid out by
	 * fe_disk_io_build_model_def. */
	struct craft_weapon_slot weapon_slots[16];
	/* Signature effective_ai_object_link's object must still carry; only spawn
	 * writes it, with 0. */
	uint16_t effective_ai_object_signature;
	/* Per weapon slot, the turret's target and retarget timing. */
	struct turret_target_state turret_target_states[16];
	/* No game code reads or writes it; the world checksums subtract its
	 * size from the record bytes they sum. */
	uint8_t unused3f2[44];
	/* Objects tied to the turrets. No code points one at an object: every
	 * write clears, rebases or restores one, so they stay NULL. */
	struct object_record *turret_object_links[16];
	/* Object whose character data gives this craft its AI skill while its
	 * signature matches effective_ai_object_signature. Only a branch that
	 * never runs points it at an object, so it stays NULL. */
	struct object_record *effective_ai_object_link;
};

struct craft_tech_stats {
	int craft_type; ///< craft_species selected by the Tech Library. specdesc.txt uses craft_type-1;
	///< build_craft_tech_stats also uses this value directly as the parallel object/model slot.
	craft_genus
		genus_id; ///< craft_genus copied from g_object_type_table[craft_type].
	/* Speed rating: the model's max_speed times 4/9, rounded. */
	int speed_rating;
	/* Acceleration rating: the model's accel_rate times 4/9, rounded. */
	int acceleration_rating;
	/* Maneuver rating: pitch_rate plus roll_rate times
	 * g_craft_tech_maneuver_rating_scale, rounded. */
	int maneuver_rating;
	/* Laser cannons, counted from the model's laser groups or a fixed
	 * figure. */
	int laser_count;
	int ion_count; /* Ion cannons, counted the same way. */
	/* Warheads: capacity times slots summed over the launchers, or a fixed
	 * figure. */
	int warhead_rating;
	/* Shield rating: shield_strength / 50, 0 without shields, scaled by
	 * genus. */
	int shield_rating;
	int hull_rating; /* Hull rating: hull_strength / 105, scaled by genus. */
	int unused_rating; /* Nothing reads or writes it by name. */
};

extern struct craft_data *g_cur_craft;

void craft_adjust_current_shield_energy(unsigned int unused_object_idx,
					uint16_t shield_index, int16_t delta);
int craft_get_object_max_shield(uint16_t obj_idx);
model_index get_model_index_from_type(object_type_id object_type);
int build_craft_tech_stats(struct craft_tech_stats *stats);
void craft_clear_turret_object_links(struct craft_data *craft);
void craft_free_linked_objects(struct craft_data *craft);
void craft_detach_damageable_component(uint16_t object_index,
				       int16_t detach_all);
warhead_kind_index object_type_get_warhead_kind_index(uint16_t object_type);
int craft_is_selectable_damage_component_mesh(int object_type, int mesh_index);
int craft_damage_component(uint16_t victim_obj_idx, int16_t hit_mesh_index,
			   unsigned int damage_amount, uint16_t source_obj_idx);
void craft_spawn_main_hull_explosion_effects(uint16_t object_idx,
					     int16_t force_main_explosion);
int craft_spawn_explosion_object_at_mesh(struct object_record *obj_record,
					 uint16_t mesh_index, int effect_size,
					 uint16_t use_random_vertex);

#ifdef __cplusplus
}
#endif

#endif
