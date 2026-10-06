/* Tests for xvt/flight/object/static.c, the static objects' collision test
 * and what a hit on one does. Each check builds the flight it needs in the
 * game's own tables: a small object table this file owns, with two craft
 * slots, three shot slots and two static slots, the craft and guidance records
 * behind them, and the flight group and object type figures the code reads.
 * No game data is read. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/object/static.h"
#include "xvt/flight/player/player.h"

enum {
	TEST_SLOTS = 7,
	TEST_CRAFT_END = 2,  /* Slots 0 and 1 hold craft. */
	TEST_SHOT_START = 2, /* Slots 2 to 4 hold shots and effects. */
	TEST_MAIN_END = 5,   /* Slots 5 and 6 hold static objects. */
	TEST_STATIC_COUNT = 2,
	TEST_SHOT = 2,
	TEST_MINE = 5,
	TEST_FIGHTER_TYPE = 1,
	TEST_MINE_TYPE = 75,
	TEST_MINE_EXTENT = 800,
	TEST_NO_HIT = 0xFFFF, /* The box test's hit, as returned. */
	EFFECT_DEFAULT = 0x81,
	EFFECT_LASER_IMPACT = 0x83,
	EFFECT_ION_IMPACT = 0x84,
};

static struct object_record g_test_objects[TEST_SLOTS];
static struct mobile_object g_test_mobiles[TEST_MAIN_END];
static struct craft_data g_test_craft[TEST_CRAFT_END];
static struct warhead_guidance_state
	g_test_guidance[TEST_MAIN_END - TEST_SHOT_START];

static void place(int slot, int x, int y, int z)
{
	g_test_objects[slot].world_x = x;
	g_test_objects[slot].world_y = y;
	g_test_objects[slot].world_z = z;
}

/* Two computer-flown starfighters in flight groups 0 and 1, aiming at
 * nothing; a working mine of flight group 2 at (1,000, 0, 0), of a type that is
 * no space craft; empty shot slots, which are also the explosion slots. Sounds
 * are off, so nothing is queued. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_test_guidance, 0, sizeof g_test_guidance);
	memset(g_mission_flight_groups, 0,
	       3 * sizeof g_mission_flight_groups[0]);
	memset(g_mission_fg_stats, 0, sizeof g_mission_fg_stats);
	g_object_table = g_test_objects;
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = TEST_CRAFT_END;
	g_projectile_object_slot_start = TEST_SHOT_START;
	g_projectile_object_slot_end = TEST_MAIN_END;
	g_region_main_object_slot_end = TEST_MAIN_END;
	g_region_static_object_slot_count = TEST_STATIC_COUNT;
	g_projectile_guidance_states = g_test_guidance;
	g_object_slot_range_by_genus[CRAFT_GENUS_EXPLOSION].start =
		TEST_SHOT_START;
	g_object_slot_range_by_genus[CRAFT_GENUS_EXPLOSION].end = TEST_MAIN_END;
	g_object_type_table[TEST_MINE_TYPE].max_bounds_extent =
		TEST_MINE_EXTENT;
	g_object_type_table[TEST_MINE_TYPE].family_id = CRAFT_FAMILY_WEAPON;
	g_object_type_table[TEST_MINE_TYPE].asset_flags = 0;
	g_object_type_table[CRAFT_SPECIES_CONTAINER_CLASS_H].max_bounds_extent =
		TEST_MINE_EXTENT;
	g_object_type_table[CRAFT_SPECIES_CONTAINER_CLASS_H].asset_flags = 0;
	g_flight_conf_sfx_enabled = 0;
	g_flight_sfx_side_effect_gate = 0;
	g_flight_sim_side_effects_suppressed = 0;
	g_local_player = 0;
	for (int group = 0; group < 3; ++group) {
		g_mission_flight_groups[group].player_owner_idx = -1;
		g_mission_flight_groups[group].fg.team = (uint8_t)group;
	}
	g_mission_flight_groups[2].fg.craft_type = TEST_MINE_TYPE;
	for (int slot = 0; slot < TEST_SLOTS; ++slot) {
		g_test_objects[slot].player_owner_idx = -1;
	}
	for (int slot = 0; slot < TEST_MAIN_END; ++slot) {
		g_test_objects[slot].mobj = &g_test_mobiles[slot];
		g_test_mobiles[slot].proximity_list.rebuild_ticks = 0x7FFF;
	}
	for (int slot = 0; slot < TEST_CRAFT_END; ++slot) {
		g_test_objects[slot].object_type = TEST_FIGHTER_TYPE;
		g_test_objects[slot].genus_id = CRAFT_GENUS_STARFIGHTER;
		g_test_objects[slot].flight_group_idx = (uint8_t)slot;
		g_test_mobiles[slot].p_craft = &g_test_craft[slot];
		g_test_craft[slot].ai_controller.target_obj_idx = UINT16_MAX;
	}
	g_test_objects[TEST_MINE].object_type = TEST_MINE_TYPE;
	g_test_objects[TEST_MINE].genus_id = CRAFT_GENUS_MINE;
	g_test_objects[TEST_MINE].flight_group_idx = 2;
	g_test_objects[TEST_MINE].type_specific_word = 1023;
	place(TEST_MINE, 1000, 0, 0);
}

/* A shot of type in the first shot slot, fired by craft 0, which moves this
 * step from (x0, y0, z0) to (x1, y1, z1); the sweep globals are set as
 * collide_collisions sets them for it. */
static void sweep_shot(uint8_t type, int x0, int y0, int z0, int x1, int y1,
		       int z1)
{
	g_test_objects[TEST_SHOT].object_type = type;
	g_test_objects[TEST_SHOT].genus_id = CRAFT_GENUS_OTHER_PROJECTILE;
	place(TEST_SHOT, x1, y1, z1);
	g_test_mobiles[TEST_SHOT].prev_world_x = x0;
	g_test_mobiles[TEST_SHOT].prev_world_y = y0;
	g_test_mobiles[TEST_SHOT].prev_world_z = z0;
	g_test_mobiles[TEST_SHOT].family = 1;
	g_test_mobiles[TEST_SHOT].speed = 900;
	g_test_mobiles[TEST_SHOT].seconds_alive = 1;
	g_test_mobiles[TEST_SHOT].lifetime_timer = 40;
	g_test_mobiles[TEST_SHOT].effect_size = 3;
	g_test_mobiles[TEST_SHOT].source_obj_idx = 0;
	g_test_mobiles[TEST_SHOT].p_warhead_guidance = &g_test_guidance[0];
	g_test_objects[TEST_SHOT].yaw = 0x2000;
	g_test_objects[TEST_SHOT].pitch = 0x1000;
	g_test_objects[TEST_SHOT].roll = 0x0800;
	g_collision_segment_start_world_x = x0;
	g_collision_segment_start_world_y = y0;
	g_collision_segment_start_world_z = z0;
	g_collision_probe_world_x = x1;
	g_collision_probe_world_y = y1;
	g_collision_probe_world_z = z1;
}

/* A laser shot from craft 0, which aims at the mine, passing through the
 * mine's center this step. */
static void shot_through_mine(void)
{
	fresh_world();
	g_test_craft[0].ai_controller.target_obj_idx = TEST_MINE;
	sweep_shot(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 0, 0, 2000, 0, 0);
}

/* ------------------------------------------------------------------------ */
/* The collision test. */

static void check_box_hit(void)
{
	shot_through_mine();
	g_collision_sweep_start_x = 7;
	g_collision_sweep_end_z = 7;
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE),
		TEST_NO_HIT);
	/* The static's sweep starts and ends at its position. */
	XVT_ASSERT_INT_EQ(g_collision_sweep_start_x, 1000);
	XVT_ASSERT_INT_EQ(g_collision_sweep_end_x, 1000);
	XVT_ASSERT_INT_EQ(g_collision_sweep_start_y, 0);
	XVT_ASSERT_INT_EQ(g_collision_sweep_end_y, 0);
	XVT_ASSERT_INT_EQ(g_collision_sweep_start_z, 0);
	XVT_ASSERT_INT_EQ(g_collision_sweep_end_z, 0);
	XVT_ASSERT_INT_EQ(g_world_loc_x, 1000);
}

static void check_box_hit_across(void)
{
	/* The same mine hit by shots crossing it along Y and along Z. */
	shot_through_mine();
	sweep_shot(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 1000, -1000, 0, 1000,
		   1000, 0);
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE),
		TEST_NO_HIT);
	sweep_shot(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 1000, 0, -1000, 1000, 0,
		   1000);
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE),
		TEST_NO_HIT);
}

static void check_box_size(void)
{
	/* The box has 3/8 of the extent each way: 300 units for this mine. A
	 * shot passing 290 units off the center hits, one 310 off does not. */
	shot_through_mine();
	sweep_shot(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 290, 0, 2000, 290, 0);
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE),
		TEST_NO_HIT);
	sweep_shot(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 0, -290, 2000, 0,
		   -290);
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE),
		TEST_NO_HIT);
	sweep_shot(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 310, 0, 2000, 310, 0);
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE), 0);
	sweep_shot(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 0, -310, 2000, 0,
		   -310);
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE), 0);
}

static void check_large_model_test(void)
{
	/* From LARGE_MODEL_EXTENT (1,095) up, or for a Container Class H, the
	 * model test decides; it finds nothing in a type whose asset_flags
	 * bit 0 is clear, where the box test would have found a hit. */
	shot_through_mine();
	g_object_type_table[TEST_MINE_TYPE].max_bounds_extent = 1094;
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE),
		TEST_NO_HIT);
	g_object_type_table[TEST_MINE_TYPE].max_bounds_extent = 1095;
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE), 0);
	g_object_type_table[TEST_MINE_TYPE].max_bounds_extent =
		TEST_MINE_EXTENT;
	g_test_objects[TEST_MINE].object_type = CRAFT_SPECIES_CONTAINER_CLASS_H;
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE), 0);
}

static void check_source_not_below_static(void)
{
	/* The shot's source slot is not below the static's: 0. */
	shot_through_mine();
	g_test_mobiles[TEST_SHOT].source_obj_idx = TEST_MINE;
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE), 0);
	/* One below it is tested. */
	g_test_objects[TEST_MINE - 1].object_type = 0;
	g_test_mobiles[TEST_SHOT].source_obj_idx = TEST_MINE - 1;
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE),
		TEST_NO_HIT);
}

static void check_genus_passed_over(void)
{
	/* Obstacles and small debris are never hit. */
	shot_through_mine();
	g_test_objects[TEST_MINE].genus_id = CRAFT_GENUS_OBSTACLE;
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE), 0);
	g_test_objects[TEST_MINE].genus_id = CRAFT_GENUS_SMALL_DEBRIS;
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE), 0);
}

static void check_ai_target_only(void)
{
	/* A computer-flown source hits a static only when it aims at it. */
	shot_through_mine();
	g_test_craft[0].ai_controller.target_obj_idx = TEST_MINE + 1;
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE), 0);
	/* Normal debris is hit whatever it aims at. */
	g_test_objects[TEST_MINE].genus_id = CRAFT_GENUS_NORMAL_DEBRIS;
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE),
		TEST_NO_HIT);
	/* So is anything by a player's source. */
	g_test_objects[TEST_MINE].genus_id = CRAFT_GENUS_MINE;
	g_test_objects[0].player_owner_idx = 0;
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE),
		TEST_NO_HIT);
	/* A source past the craft slots is not asked what it aims at. */
	g_test_objects[0].player_owner_idx = -1;
	g_active_region_craft_object_slot_end = 0;
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE),
		TEST_NO_HIT);
}

static void check_out_of_reach(void)
{
	/* The shot's sweep plus the hit radius cannot reach the mine: a short
	 * step 4,000 units short of it. */
	shot_through_mine();
	sweep_shot(PROJECTILE_OBJECT_TYPE_REBEL_LASER, -3100, 0, 0, -3000, 0,
		   0);
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE), 0);

	/* Within MAX_DISTANCE (0x20000) on every axis but farther by the rough
	 * distance: 0, though the long sweep would reach. */
	place(TEST_MINE, 0x1F000, 0x10000, 0);
	sweep_shot(PROJECTILE_OBJECT_TYPE_REBEL_LASER, -0x30000, 0, 0, 0, 0, 0);
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE), 0);

	/* At exactly MAX_DISTANCE by the rough distance it is still tested: a
	 * long shot through the mine's center, ending that far past it. */
	place(TEST_MINE, 0x1C000, 0x10000, 0);
	sweep_shot(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0x38000, 0x20000, 0, 0,
		   0, 0);
	XVT_ASSERT_INT_EQ(
		static_test_swept_static_collision(TEST_SHOT, TEST_MINE),
		TEST_NO_HIT);
}

/* ------------------------------------------------------------------------ */
/* What a hit does. */

/* The impact effect a hit left in slot: at the hit point (the sweep's start
 * plus the hit offset), an explosion that does not move. */
static void assert_effect(int slot, int effect_type)
{
	XVT_ASSERT_INT_EQ(g_test_objects[slot].object_type, effect_type);
	XVT_ASSERT_INT_EQ(g_test_objects[slot].genus_id, CRAFT_GENUS_EXPLOSION);
	XVT_ASSERT_INT_EQ(g_test_objects[slot].world_x,
			  g_collision_segment_start_world_x +
				  g_collision_hit_offset_x);
	XVT_ASSERT_INT_EQ(g_test_objects[slot].world_y,
			  g_collision_segment_start_world_y +
				  g_collision_hit_offset_y);
	XVT_ASSERT_INT_EQ(g_test_objects[slot].world_z,
			  g_collision_segment_start_world_z +
				  g_collision_hit_offset_z);
	XVT_ASSERT_INT_EQ(g_test_mobiles[slot].family, 5);
	XVT_ASSERT_INT_EQ(g_test_objects[slot].type_specific_byte[0], 2);
	XVT_ASSERT_INT_EQ(g_test_mobiles[slot].speed, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[slot].effect_size, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[slot].seconds_alive, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[slot].lifetime_timer, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[slot].pitch, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[slot].yaw, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[slot].roll, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[slot].orient_matrix_dirty, 1);
	XVT_ASSERT_INT_EQ(g_test_mobiles[slot].move_vector_dirty, 1);
}

static int destroyed_count(void)
{
	return g_mission_fg_stats[2]
		.outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED];
}

static int disabled_count(void)
{
	return g_mission_fg_stats[2]
		.outcome_count[FLIGHT_GROUP_OUTCOME_DISABLED];
}

/* A hit by the shot on the static: the sweep set, a hit offset given. */
static void hit_static(uint8_t shot_type, uint8_t genus)
{
	shot_through_mine();
	g_test_objects[TEST_MINE].genus_id = genus;
	sweep_shot(shot_type, 0, 0, 0, 2000, 0, 0);
	g_collision_hit_offset_x = 640;
	g_collision_hit_offset_y = 12;
	g_collision_hit_offset_z = -8;
	static_apply_static_hit(TEST_SHOT, TEST_MINE);
}

static void check_debris_survives(void)
{
	/* Normal debris survives; a cannon shot leaves a laser impact, an ion
	 * shot an ion impact, a warhead the default effect. */
	hit_static(PROJECTILE_OBJECT_TYPE_REBEL_LASER,
		   CRAFT_GENUS_NORMAL_DEBRIS);
	assert_effect(TEST_SHOT, EFFECT_LASER_IMPACT);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].object_type,
			  TEST_MINE_TYPE);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].type_specific_word, 1023);
	XVT_ASSERT_INT_EQ(destroyed_count(), 0);
	XVT_ASSERT_INT_EQ(disabled_count(), 0);

	hit_static(PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER,
		   CRAFT_GENUS_NORMAL_DEBRIS);
	assert_effect(TEST_SHOT, EFFECT_LASER_IMPACT);
	hit_static(PROJECTILE_OBJECT_TYPE_ION_LASER, CRAFT_GENUS_NORMAL_DEBRIS);
	assert_effect(TEST_SHOT, EFFECT_ION_IMPACT);
	hit_static(PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER,
		   CRAFT_GENUS_NORMAL_DEBRIS);
	assert_effect(TEST_SHOT, EFFECT_ION_IMPACT);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].type_specific_word, 1023);
	hit_static(WARHEAD_OBJECT_TYPE_PROTON_TORPEDO,
		   CRAFT_GENUS_NORMAL_DEBRIS);
	assert_effect(TEST_SHOT, EFFECT_DEFAULT);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].object_type,
			  TEST_MINE_TYPE);
	XVT_ASSERT_INT_EQ(destroyed_count(), 0);
}

static void check_ion_disables(void)
{
	/* An ion shot disables the static: systems word 0, a disabled outcome,
	 * an ion impact; the static stays. */
	hit_static(PROJECTILE_OBJECT_TYPE_ION_LASER, CRAFT_GENUS_MINE);
	assert_effect(TEST_SHOT, EFFECT_ION_IMPACT);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].type_specific_word, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].object_type,
			  TEST_MINE_TYPE);
	XVT_ASSERT_INT_EQ(disabled_count(), 1);
	XVT_ASSERT_INT_EQ(destroyed_count(), 0);

	hit_static(PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER, CRAFT_GENUS_MINE);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].type_specific_word, 0);
	XVT_ASSERT_INT_EQ(disabled_count(), 1);
}

static void check_shot_destroys(void)
{
	/* Any other shot destroys it: slot emptied, a destroyed outcome, the
	 * default effect. */
	hit_static(PROJECTILE_OBJECT_TYPE_REBEL_LASER, CRAFT_GENUS_MINE);
	assert_effect(TEST_SHOT, EFFECT_DEFAULT);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].object_type, 0);
	XVT_ASSERT_INT_EQ(destroyed_count(), 1);
	XVT_ASSERT_INT_EQ(disabled_count(), 0);

	hit_static(WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, CRAFT_GENUS_MINE);
	assert_effect(TEST_SHOT, EFFECT_DEFAULT);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].object_type, 0);
	XVT_ASSERT_INT_EQ(destroyed_count(), 1);
}

/* Craft 0 runs into the static, with the sweep and hit offset set. */
static void ram_static(uint8_t genus)
{
	fresh_world();
	g_test_objects[TEST_MINE].genus_id = genus;
	place(0, 1500, 0, 0);
	g_collision_segment_start_world_x = 0;
	g_collision_segment_start_world_y = 0;
	g_collision_segment_start_world_z = 0;
	g_collision_hit_offset_x = 640;
	g_collision_hit_offset_y = 12;
	g_collision_hit_offset_z = -8;
	g_test_craft[0].hull_damage = 3;
	static_apply_static_hit(0, TEST_MINE);
}

static void check_craft_destroys(void)
{
	/* A craft destroys the static, and the effect takes the first free
	 * explosion slot. The craft is left as it was. */
	g_test_objects[TEST_SHOT].object_type = 0;
	ram_static(CRAFT_GENUS_MINE);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].object_type, 0);
	XVT_ASSERT_INT_EQ(destroyed_count(), 1);
	assert_effect(TEST_SHOT, EFFECT_DEFAULT);
	XVT_ASSERT_INT_EQ(g_test_objects[0].object_type, TEST_FIGHTER_TYPE);
	XVT_ASSERT_INT_EQ(g_test_objects[0].genus_id, CRAFT_GENUS_STARFIGHTER);
	XVT_ASSERT_INT_EQ(g_test_objects[0].world_x, 1500);
	XVT_ASSERT_INT_EQ(g_test_craft[0].hull_damage, 3);

	/* Normal debris survives a craft too, with a laser impact. */
	ram_static(CRAFT_GENUS_NORMAL_DEBRIS);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].object_type,
			  TEST_MINE_TYPE);
	XVT_ASSERT_INT_EQ(destroyed_count(), 0);
	assert_effect(TEST_SHOT, EFFECT_LASER_IMPACT);
	XVT_ASSERT_INT_EQ(g_test_objects[0].object_type, TEST_FIGHTER_TYPE);
}

static void check_craft_no_effect_slot(void)
{
	/* With every explosion slot taken there is no effect; the static is
	 * still destroyed. */
	fresh_world();
	for (int slot = TEST_SHOT_START; slot < TEST_MAIN_END; ++slot) {
		g_test_objects[slot].object_type = 131;
		g_test_objects[slot].genus_id = CRAFT_GENUS_EXPLOSION;
	}
	g_test_objects[TEST_MINE].genus_id = CRAFT_GENUS_MINE;
	g_collision_hit_offset_x = 640;
	static_apply_static_hit(0, TEST_MINE);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].object_type, 0);
	XVT_ASSERT_INT_EQ(destroyed_count(), 1);
	for (int slot = TEST_SHOT_START; slot < TEST_MAIN_END; ++slot) {
		XVT_ASSERT_INT_EQ(g_test_objects[slot].object_type, 131);
		XVT_ASSERT_INT_EQ(g_test_objects[slot].world_x, 0);
	}
	XVT_ASSERT_INT_EQ(g_test_objects[0].object_type, TEST_FIGHTER_TYPE);
	XVT_ASSERT_INT_EQ(g_test_objects[0].world_x, 0);
}

int main(void)
{
	check_box_hit();
	check_box_hit_across();
	check_box_size();
	check_large_model_test();
	check_source_not_below_static();
	check_genus_passed_over();
	check_ai_target_only();
	check_out_of_reach();
	check_debris_survives();
	check_ion_disables();
	check_shot_destroys();
	check_craft_destroys();
	check_craft_no_effect_slot();
	return 0;
}
