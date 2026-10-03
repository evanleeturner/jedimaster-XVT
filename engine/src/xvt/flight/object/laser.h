#ifndef XVT_FLIGHT_OBJECT_LASER_H
#define XVT_FLIGHT_OBJECT_LASER_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Guidance record of one shot, an entry of g_projectile_guidance_states
 * reached through mobile_object.p_warhead_guidance; every shot gets one. */
struct warhead_guidance_state {
	/* Player behind the shot, -1 for none; collide_damagecraft credits the
	 * damage to this player. */
	int8_t source_player_idx;
	/* Homing strength, 0 to 6; 0 does not home. A warhead from launcher 0
	 * or 1 gets its craft's whole simulated seconds of lock, at most 6; a
	 * warhead fired from a static object 3 to 6 at random; a countermeasure
	 * that finds a target 6. With g_projectile_homing_profile_base_by_object_type
	 * it picks the entry in the homing tables. */
	uint8_t homing_tier;
	/* Mesh whose center a homing shot steers for on a craft; UINT16_MAX
	 * steers for mesh 0's. */
	uint16_t target_component_idx;
	/* Object the shot is aimed at; UINT16_MAX for none. */
	uint16_t target_obj_idx;
	/* object_signature of the target when the shot was fired, 0 for none; a
	 * homing shot explodes when a target in a mobile slot no longer
	 * carries it. */
	uint16_t target_signature;
	/* Speed a homing shot builds back up to after turning, also the speed
	 * the proximity estimate uses: the shot's speed when fired, or for a
	 * countermeasure its type's speed plus its owner's. Mine shots leave
	 * it as it was. */
	uint16_t cruise_speed;
};

/* Firing state of a craft's two cannon groups (craft_data.laser_state), one
 * entry per group in each array. */
struct craft_laser_state {
	/* Shot object type the group fires, from the model; 0 for no group. */
	uint8_t projectile_type_id[2];
	/* How the group fires: 1 one cannon at a time, 2 pairs, 3 all at once.
	 * On an AI craft 0 holds fire, and paifight_fightershootorder sets a
	 * mode to start a burst. */
	uint8_t link_mode[2];
	/* Shots left in an AI burst; laser_weaponsfire sets link_mode to 0 when
	 * it reaches 0. */
	uint8_t burst_remaining[2];
	/* Next weapon slot to fire in modes 1 and 2. */
	uint8_t next_slot[2];
	/* Ticks before the group may fire again: laser_firelasersystem adds 47
	 * per shot plus 2, and laser_weaponsfire counts it down. */
	int16_t fire_cooldown_ticks[2];
	/* Lockstep time stamp at which the group may fire again, moved forward
	 * by the same amounts as fire_cooldown_ticks. While
	 * g_laser_fire_timestamp_tracking_enabled is set, laser_fireplayerweapon
	 * lets the group fire once the player's lockstep_timestamp has passed
	 * it. */
	int next_fire_timestamp[2];
};

/* Stored as int16_t in the binary (IDB enum warhead_kind_index). */
typedef int16_t warhead_kind_index;

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

/* Figures for each shot object type, indexed from PROJECTILE_OBJECT_TYPE_FIRST
 * (137): the layout of g_projectile_type_data. */
struct projectile_type_data_tables {
	/* Damage on a hit. Cannon, warhead and countermeasure shots fired by a
	 * craft add the craft's speed; turret, mine and static-fired shots do
	 * not. */
	unsigned int damage[PROJECTILE_OBJECT_TYPE_COUNT];
	/* Speed in the game's units. Cannon and warhead shots fired by a craft
	 * add the craft's speed; turret and static-fired shots fly at it, mine
	 * shots and countermeasures at half. */
	uint16_t speed[PROJECTILE_OBJECT_TYPE_COUNT];
	/* Whole simulated seconds of life; laser_get_projectile_lifetime_ticks
	 * adds lifetime_frac_q16. */
	uint16_t lifetime_seconds[PROJECTILE_OBJECT_TYPE_COUNT];
	/* Further fraction of a simulated second of life, in 65,536ths. */
	uint16_t lifetime_frac_q16[PROJECTILE_OBJECT_TYPE_COUNT];
	/* World units along the firing direction from the hardpoint at which
	 * the shot is placed; a warhead from a freighter, starship or platform
	 * is moved that far up or down instead. */
	int16_t launch_offset[PROJECTILE_OBJECT_TYPE_COUNT];
	/* 0 for cannon shots; for warheads, 1 for those the AI uses on small
	 * targets and 2 for those it uses on freighters, starships and
	 * platforms. */
	uint8_t warhead_class[PROJECTILE_OBJECT_TYPE_COUNT];
	/* Points a warhead takes off its firer's score and team score when
	 * fired (laser_firemissile); mission_compute_craft_point_value reads it
	 * too. */
	uint16_t warhead_point_value[PROJECTILE_OBJECT_TYPE_COUNT];
};

extern const struct projectile_type_data_tables g_projectile_type_data;
extern const uint8_t g_warhead_type_ids[11];
extern const uint16_t g_warhead_ammo_fraction_q16[12];
extern const uint8_t g_mesh_type_component_max_hp[32];
extern const uint8_t g_platform_beam_disabled_component_ids[60];
extern int g_laser_fire_timestamp_tracking_enabled;

void laser_weaponsfire(void);
uint16_t laser_get_projectile_lifetime_ticks(int projectile_object_type);
void laser_fireplayerweapon(int player_idx);
void laser_firelasersystem(int object_index, int laser_system_index);
void laser_firewarheadsystem(int object_index, unsigned int launcher_index);
int laser_firemissile(int object_index, int weapon_slot_index,
		      int projectile_type_id, unsigned int launcher_index);
int laser_createprojectile(int source_object_index, int weapon_slot_index,
			   int projectile_object_type);
uint16_t laser_createprojectilefromstatic(uint16_t source_obj_idx,
					  uint16_t target_obj_idx);
int laser_createcountermeasureprojectile(unsigned int owner_obj_idx,
					 int projectile_object_type);
void laser_warnplayer(uint16_t projectile_guidance_idx);
void laser_update_mine_weapon_fire(uint16_t mine_obj_idx);
void laser_fireturretslot(uint16_t source_obj_idx, uint16_t weapon_slot_idx,
			  uint16_t target_ref);

#ifdef __cplusplus
}
#endif

#endif
