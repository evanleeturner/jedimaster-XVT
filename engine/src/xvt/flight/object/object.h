#ifndef XVT_FLIGHT_OBJECT_OBJECT_H
#define XVT_FLIGHT_OBJECT_OBJECT_H

#include <stdint.h>

#include "xvt/flight/ai/pai.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The objects near one moving object that collide_collisions tests it
 * against, up to 16; collide_insert_mobile_object_proximity_candidate places
 * each entry in rising order of contact_ticks. */
struct mobile_object_proximity_list {
	uint8_t count; /* Entries in use, 0 to 16. */
	/* Per entry, ticks left before collide_collisions tests the pair again.
	 * collide_insert_mobile_object_proximity_candidate sets it from the gap
	 * between the two objects' bounds over their combined speeds (0 when
	 * the bounds overlap). */
	int contact_ticks[16];
	uint16_t obj_idx[16]; /* Per entry, the nearby object's slot. */
	/* Ticks left before collide_collisions rebuilds the list: 0x7FFF after
	 * a rebuild, lowered to the contact_ticks of a candidate a full list
	 * drops, and 0 to rebuild on the next pass. */
	int rebuild_ticks;
};

/* The moving part of an object: one per slot below g_region_main_object_slot_end,
 * in g_mobile_object_pool_base, reached through object_record.mobj. */
struct mobile_object {
	/* Object family, a craft_family value: 0 space craft, 1 weapon, 3
	 * debris, 5 explosion, and so on. Spawn copies it from the object type
	 * table; the code that makes shots, debris and effects sets 1, 3 or
	 * 5. */
	uint8_t family;
	/* Size of an explosion or impact effect, 0 for the default: scales the
	 * effect's billboard, and from 4 up brightens its point light.
	 * object_update_lifetime_and_movement sets max_bounds_extent >> 9 for the
	 * final explosion of a craft other than a starfighter that has no
	 * fuselage mesh. */
	uint8_t effect_size;
	/* Game time, in ticks, this object has been simulated to; 0 means it
	 * moves with the shared clock. When set,
	 * object_update_lifetime_and_movement steps the object from here to
	 * g_game_time + g_elapsed_ticks and advances it. A shot from a player's
	 * craft starts at that player's lockstep_timestamp. */
	int32_t sim_state_timestamp;
	int prev_world_x; /* world_x before the latest move. */
	int prev_world_y; /* world_y before the latest move. */
	/* world_z before the latest move. Collision tests sweep each object
	 * from prev_world_x, prev_world_y, prev_world_z to its current position. */
	int prev_world_z;
	/* Objects near this one that collide_collisions tests it against. */
	struct mobile_object_proximity_list proximity_list;
	/* Spin about the roll axis after an impact or a breakup.
	 * object_update_lifetime_and_movement turns roll by 4 times this per
	 * simulated second and, for a craft whose ai_flight.impact_obj_idx is
	 * set, moves it toward 0 by 4,096 per simulated second, clearing
	 * impact_obj_idx when it gets there. */
	int16_t roll_impulse_rate;
	/* Speed in the game's units: object_update_lifetime_and_movement moves the
	 * object (4,660 times speed + 128) / 256 world units per simulated
	 * second along move_x, move_y, move_z. flight_accelerate_object_speed caps
	 * it at 3,600. */
	uint16_t speed;
	/* Fraction of a speed unit, in 65,536ths, that
	 * flight_accelerate_object_speed and flight_decelerate_object_speed carry
	 * from step to step. */
	uint16_t speed_remainder;
	/* Damage this object deals when it hits. A craft gets its type's
	 * max_bounds_extent, times 8 for a Container Class H, then times 4 when
	 * under 0x2000; a shot fired by a craft gets its type's damage plus the
	 * craft's speed, never less than the type's damage, and a shot from a
	 * turret, mine or static object the type's damage alone. */
	unsigned int damage_amount;
	/* Ticks left before the object expires; 0 means no limit. At 0,
	 * object_update_lifetime_and_movement explodes a craft, warhead or small
	 * debris and removes a laser shot or anything else. */
	uint16_t lifetime_timer;
	/* Simulated seconds since the object was made or turned into an
	 * effect: flight_update_timers adds 1 every SIMULATION_TICKS_PER_SECOND
	 * ticks. Shots start at 1. collide_collisions skips a pair, shots
	 * aside, while either object is younger than 3 seconds, and offers a
	 * player the hangar only after 45. */
	uint16_t seconds_alive;
	/* Slot of the object that made this one: the craft, turret or mine
	 * that fired a shot; a craft's own slot. object_alloc_slot_for_genus sets
	 * 0. */
	uint16_t source_obj_idx;
	/* Object type of that source; a detached component keeps its craft's
	 * type here, which picks the model to draw. Mine shots set 0. */
	uint8_t source_object_type;
	/* IFF code: 0 rebel, 1 and 4 imperial, 2 blue (the map's colors). Spawn
	 * sets it from the flight group and shots fired by craft copy their
	 * source's; mission_init sets 0xFF in the craft slots. */
	uint8_t iff;
	/* Team, 0 to 9, set at spawn and on capture
	 * (paiman_transfer_object_to_ai_team); countermeasure shots copy their
	 * owner's. */
	uint8_t team;
	/* Variant of the model's switchable nodes to draw; spawn sets the
	 * flight group's markings. */
	uint8_t node_switch_index;
	/* 1 while move_x, move_y, move_z must be recomputed from pitch and yaw;
	 * fview_calcrotatemove clears it. */
	uint8_t move_vector_dirty;
	int16_t move_x; /* Unit direction of travel, X, in Q15 (32,768 is 1). */
	int16_t move_y; /* Unit direction of travel, Y, in Q15. */
	int16_t move_z; /* Unit direction of travel, Z, in Q15. */
	/* 1 while the cached axes below must be recomputed from the angles;
	 * fview_calcrotateorient clears it. */
	uint8_t orient_matrix_dirty;
	int16_t cached_fwd_x;  /* Forward axis with roll, X, in Q15. */
	int16_t cached_fwd_y;  /* Forward axis with roll, Y, in Q15. */
	int16_t cached_fwd_z;  /* Forward axis with roll, Z, in Q15. */
	int16_t cached_side_x; /* Side axis with roll, X, in Q15. */
	int16_t cached_side_y; /* Side axis with roll, Y, in Q15. */
	int16_t cached_side_z; /* Side axis with roll, Z, in Q15. */
	int16_t cached_up_x;   /* Up axis with roll, X, in Q15. */
	int16_t cached_up_y;   /* Up axis with roll, Y, in Q15. */
	int16_t cached_up_z;   /* Up axis with roll, Z, in Q15. */
	/* A shot's guidance record in g_projectile_guidance_states, set when the
	 * shot is made; mission_init sets NULL, and an effect left in a shot
	 * slot keeps the pointer. */
	struct warhead_guidance_state *p_warhead_guidance;
	/* The craft record in g_craft_data_pool_base for an object in a craft
	 * slot; NULL in most other slots. */
	struct craft_data *p_craft;
	/* A record in g_mobile_object_char_data_pool. mission_init sets NULL, and
	 * no code points it elsewhere except to carry its own value through a
	 * world-state save and load. */
	struct mobile_object_char_data *p_char_data;
};

/* What the gunner of one weapon slot of a craft is firing at
 * (craft_data.turret_target_states). */
struct turret_target_state {
	/* Object the slot's turret fires at; for a gunner slot,
	 * laser_weaponsfire calls laser_fireturretslot while it is not
	 * UINT16_MAX. */
	uint16_t target_obj_idx;
	/* Ticks before the gunner AI may pick a new target; flight_update_timers
	 * counts it down. */
	int16_t retarget_cooldown_timer;
};

/* A character record, an entry of g_mobile_object_char_data_pool. */
struct mobile_object_char_data {
	/* AI skill, which pai_get_effective_skill_value reads through a craft's
	 * effective_ai_object_link; no code sets it other than by clearing or
	 * copying the whole record. */
	uint16_t skill_value;
	/* Never read or written by name, save in the modern build's snapshot
	 * field table, which copies it. */
	uint8_t unused02[2];
	/* AI state; flight_update_timers counts down its think and maneuver
	 * timers. */
	struct ai_controller ai_controller;
	/* Never read or written by name, save in the modern build's snapshot
	 * field table, which copies it. */
	uint8_t unused40[12];
};

/* One slot of g_object_table. Slots below g_region_main_object_slot_end hold
 * craft, shots, debris and effects, each with a mobile_object; the
 * g_region_static_object_slot_count slots after them hold the mission's static
 * objects. */
struct object_record {
	/* Stamp from g_next_object_signature given at spawn; code that keeps a
	 * slot number compares it to tell whether the slot still holds the
	 * same object. */
	uint16_t object_signature;
	uint8_t genus_id;    /* Genus, a CRAFT_GENUS_* value. */
	uint8_t object_type; /* Object type; 0 marks a free slot. */
	/* World position, X, in world units;
	 * object_add_trig_move_delta_and_clamp_world_position keeps each axis within
	 * 0x1000000 either way of 0. */
	int world_x;
	int world_y;	/* World position, Y. */
	int world_z;	/* World position, Z; the map grid lies at -65,536. */
	uint16_t yaw;	/* Heading, in 65,536ths of a full turn. */
	uint16_t pitch; /* Angle in 65,536ths of a full turn. */
	uint16_t roll;	/* Angle in 65,536ths of a full turn. */
	uint8_t flight_group_idx; /* Mission flight group of the object. */
	/* For a static object, its working-systems word: 1,023 when placed,
	 * set to 0 when an ion shot disables it (static_apply_static_hit). A mine
	 * fires only while it is not 0. */
	uint16_t type_specific_word;
	/* [0]: step in the type's texture frame sequence, which picks what is
	 * drawn; twice the mesh index for a detached component; 2 for impact
	 * effects. [1]: a mine's fire countdown in 2-tick steps
	 * (laser_update_mine_weapon_fire), or a detached component's breakup
	 * frame step. */
	uint8_t type_specific_byte[2];
	/* Player, 0 to 7, flying this craft; -1 for none. */
	int player_owner_idx;
	/* The object's mobile_object in g_mobile_object_pool_base; NULL in the
	 * static slots. */
	struct mobile_object *mobj;
};

/* A run of slots in g_object_table. */
struct object_slot_range {
	uint16_t start; /* First slot. */
	uint16_t end;	/* One past the last slot. */
};

/* Pool entries object_relink_mobile_object_pointers would link to one slot's
 * mobile_object; -1 for none. */
struct mobile_object_link_indices {
	/* Index in g_projectile_guidance_states; always -1 (only mission_init
	 * writes it). */
	int warhead_guidance_idx;
	/* Index in g_craft_data_pool_base; always -1 (only mission_init writes
	 * it). */
	int craft_data_idx;
	/* Index in g_mobile_object_char_data_pool; always -1 (only mission_init
	 * writes it). */
	int char_data_idx;
};

extern struct mobile_object_link_indices g_mobile_object_link_indices[488];
extern int g_spawn_object_type_by_object_slot[488];
extern struct mobile_object *g_mobile_object_pool_base;
extern struct mobile_object_char_data *g_mobile_object_char_data_pool;
extern struct warhead_guidance_state *g_projectile_guidance_states;
extern int g_active_region_craft_object_slot_end;
extern int g_mobile_object_char_data_count;
extern int g_mobile_object_char_data_slot_start;
extern int g_mobile_object_char_data_slot_end;
extern unsigned int g_debris_object_slots_total;
extern int g_debris_object_slot_start;
extern int g_debris_object_slot_end;
extern int g_local_debris_slot_count;
extern int g_projectile_object_slot_start;
extern int g_region_main_object_slot_end;
extern int g_region_static_object_slot_count;
extern int g_projectile_object_slot_end;
extern unsigned int g_explosion_object_slot_end;
extern int g_explosion_object_slot_start;
extern unsigned int g_projectile_object_slots_total;
extern int g_active_region_object_slot_start;
extern struct object_slot_range g_object_slot_range_by_genus[20];
extern struct object_record *g_object_table;

void object_update_lifetime_and_movement(void);
int object_add_trig_move_delta_and_clamp_world_position(uint32_t *object_words);
void render_non_craft_scene_object(uint16_t object_index);
uint16_t object_spawn_detached_component(uint16_t source_object_index,
					 int16_t mesh_index);
uint16_t object_spawn_effect_fragment(uint16_t source_obj_idx);
uint16_t object_spawn_local_effect_fragment(uint16_t source_obj_idx);
uint16_t object_alloc_slot_for_genus(uint16_t genus_id);
uint16_t object_find_free_mission_slot(void);
void object_copy_state_preserving_storage(unsigned int dst_obj_idx,
					  unsigned int src_obj_idx);
void object_relink_mobile_object_pointers(void);
unsigned int object_direction_and_distance_to_mesh_center(
	uint16_t from_obj_idx, uint16_t target_obj_idx, unsigned int mesh_idx);
uint8_t object_has_active_decoy_beam(uint16_t obj_idx);

#ifdef __cplusplus
}
#endif

#endif
